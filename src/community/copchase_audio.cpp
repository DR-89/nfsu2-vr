#include "copchase_audio.h"
#include "xyz_json.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr unsigned rate = 48000, ring_frames = 4096, block_frames = 256, queue_size = 16;
constexpr unsigned max_clips = 1500, max_banks = 48;
uint64_t now_ms() { return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count()); }
uint16_t u16(const unsigned char *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const unsigned char *p) { return u16(p) | uint32_t(u16(p + 2)) << 16; }
bool token(const std::string &s, size_t limit) {
    return !s.empty() && s.size() <= limit && std::all_of(s.begin(), s.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'; });
}
bool filename(const std::string &s) {
    return s.size() > 4 && s.substr(s.size() - 4) == ".wav" && token(s.substr(0, s.size() - 4), 63);
}
float volume(float value) { return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f; }
std::string text(const fs::path &path, size_t limit) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    auto size = file.tellg();
    if (size < 0 || uint64_t(size) > limit) return {};
    std::string value(size_t(size), '\0'); file.seekg(0);
    return file.read(value.data(), size) ? value : std::string();
}
bool contained(const fs::path &root, const fs::path &path) {
    auto relative = fs::weakly_canonical(path).lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

struct Clip { std::string name, make, colour; };
struct Bank { std::string name; std::vector<unsigned> clips; };
struct Command { unsigned bank = 0, fallback = UINT32_MAX; int priority = 0; };

// Every voice owns a fixed input cache. Music is streamed even though its WAV
// files are ~30 MB; no entire track or unbounded decoded-clip cache is kept.
struct Stream {
    std::ifstream file;
    unsigned channels = 0, source_rate = 0;
    uint64_t data_offset = 0, total_frames = 0, cached_start = 0;
    size_t cached_frames = 0;
    std::array<unsigned char, 32768> cache{};
    double position = 0;
    bool loop = false;
    float gain = 0;
    std::string name;

    void close() { file.close(); file.clear(); channels = 0; name.clear(); cached_frames = 0; position = 0; }
    bool open(const fs::path &root, const std::string &requested, bool repeat, float level) {
        close();
        try {
            const auto path = root / requested;
            if (!filename(requested) || !contained(root, path)) return false;
            file.open(path, std::ios::binary | std::ios::ate);
            if (!file) return false;
            const auto size = file.tellg();
            if (size < 44 || size > 64 * 1024 * 1024) { close(); return false; }
            std::array<unsigned char, 16> header{};
            file.seekg(0); file.read(reinterpret_cast<char *>(header.data()), 12);
            if (!file || std::memcmp(header.data(), "RIFF", 4) || std::memcmp(header.data() + 8, "WAVE", 4) ||
                u32(header.data() + 4) != uint64_t(size) - 8) { close(); return false; }
            bool format = false, data = false;
            uint64_t bytes = 0;
            for (uint64_t at = 12; at + 8 <= uint64_t(size);) {
                file.seekg(at); file.read(reinterpret_cast<char *>(header.data()), 8);
                const uint32_t length = u32(header.data() + 4);
                if (!file || length > uint64_t(size) - at - 8) { close(); return false; }
                if (!std::memcmp(header.data(), "fmt ", 4)) {
                    if (format || length < 16) { close(); return false; }
                    file.read(reinterpret_cast<char *>(header.data()), 16);
                    channels = u16(header.data() + 2); source_rate = u32(header.data() + 4);
                    if (!file || u16(header.data()) != 1 || (channels != 1 && channels != 2) ||
                        source_rate < 8000 || source_rate > 192000 || u16(header.data() + 14) != 16 ||
                        u16(header.data() + 12) != channels * 2 || u32(header.data() + 8) != channels * 2 * source_rate) {
                        close(); return false;
                    }
                    format = true;
                } else if (!std::memcmp(header.data(), "data", 4)) {
                    if (data) { close(); return false; }
                    data = true; data_offset = at + 8; bytes = length;
                }
                at += 8 + uint64_t(length) + (length & 1u);
            }
            if (!format || !data || !bytes || bytes % (channels * 2)) { close(); return false; }
            total_frames = bytes / (channels * 2); loop = repeat; gain = level; name = requested;
            return true;
        } catch (...) { close(); return false; }
    }
    float sample(uint64_t frame, unsigned channel) {
        if (frame >= total_frames) frame = loop ? 0 : total_frames - 1;
        if (!cached_frames || frame < cached_start || frame >= cached_start + cached_frames) {
            cached_start = frame;
            cached_frames = size_t(std::min<uint64_t>(total_frames - frame, cache.size() / (channels * 2)));
            file.clear(); file.seekg(data_offset + frame * channels * 2);
            if (!file.read(reinterpret_cast<char *>(cache.data()), cached_frames * channels * 2)) {
                cached_frames = 0; return 0;
            }
        }
        const auto offset = ((frame - cached_start) * channels + (channels == 1 ? 0 : channel)) * 2;
        return int16_t(u16(cache.data() + offset)) / 32768.f;
    }
    bool mix(float *output, size_t frames) {
        if (!channels) return false;
        for (size_t i = 0; i < frames; ++i) {
            if (position >= double(total_frames)) {
                if (!loop) { close(); break; }
                position = std::fmod(position, double(total_frames));
            }
            const uint64_t a = uint64_t(position);
            const float fraction = float(position - double(a));
            for (unsigned ch = 0; ch < 2; ++ch) {
                const float first = sample(a, ch), second = sample(a + 1, ch);
                output[i * 2 + ch] += (first + (second - first) * fraction) * gain;
            }
            if (!cached_frames) { close(); break; }
            position += double(source_rate) / rate;
        }
        return true;
    }
};

struct Player {
    fs::path root;
    std::vector<Clip> clips;
    std::vector<Bank> banks;
    std::unordered_map<std::string, unsigned> bank_by_name;
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool stopping = false;
    CopChaseAudioStatus status{};
    uint32_t pending_events = 0;
    bool pending_pop = false;
    uint64_t epoch = 0;
    std::array<Command, queue_size> commands{};
    unsigned command_count = 0;
    std::array<float, ring_frames * 2> ring{};
    unsigned head = 0, count = 0;
    std::atomic<bool> ready{false}, audible{false}, music_ready{false};
    std::atomic<uint64_t> heartbeat{0}, played{0}, dropped{0}, missing{0};
    std::atomic<unsigned> streams{0};
    std::atomic<uint64_t> pops{0};
    std::atomic<unsigned> rim_volume{0};
    std::atomic<float> master{0};
} player;

unsigned bank(const char *name) {
    if (!name) return UINT32_MAX;
    auto found = player.bank_by_name.find(name);
    return found == player.bank_by_name.end() ? UINT32_MAX : found->second;
}

bool load_manifest(const fs::path &root) {
    if (!contained(root, root / "native-manifest.json") || !contained(root, root / "chatter_tags.txt")) return false;
    auto document = xyz::parse_json(text(root / "native-manifest.json", 65536));
    if (!document) return false;
    std::unordered_map<std::string, unsigned> names;
    for (const cJSON *item = document->child; item; item = item->next) {
        if (!item->string || !token(item->string, 24) || !cJSON_IsArray(item) || player.banks.size() >= max_banks) return false;
        Bank group{item->string, {}};
        for (const cJSON *clip = item->child; clip; clip = clip->next) {
            if (!cJSON_IsString(clip) || !clip->valuestring || !filename(clip->valuestring)) return false;
            std::string name = clip->valuestring;
            auto found = names.find(name);
            unsigned index;
            if (found == names.end()) {
                if (player.clips.size() >= max_clips) return false;
                index = unsigned(player.clips.size()); names.emplace(name, index);
                player.clips.push_back({name, {}, {}});
            } else index = found->second;
            if (std::find(group.clips.begin(), group.clips.end(), index) != group.clips.end()) return false;
            group.clips.push_back(index);
        }
        player.bank_by_name.emplace(group.name, unsigned(player.banks.size()));
        player.banks.push_back(std::move(group));
    }
    const auto tags = text(root / "chatter_tags.txt", 65536);
    if (tags.empty()) return false; // Never announce unverified car descriptions.
    std::istringstream lines(tags); std::string line;
    unsigned tag_count = 0;
    while (std::getline(lines, line)) {
        if (line.empty() || line[0] == '#' || line == "\r") continue;
        std::istringstream fields(line); std::string name, make, colour, extra;
        if (!(fields >> name >> make) || !filename(name) || !token(make, 15) || ++tag_count > max_clips) return false;
        fields >> colour;
        if ((!colour.empty() && colour != "?" && !token(colour, 7)) || (fields >> extra)) return false;
        auto found = names.find(name);
        if (found == names.end()) continue; // Tags may cover absent upstream clips.
        auto &clip = player.clips[found->second];
        if (!clip.make.empty()) return false;
        clip.make = make; clip.colour = colour;
    }
    return !player.clips.empty();
}

void worker() {
    Stream radio, siren, music, sting, pop, rim;
    std::mt19937 random(0xCC032u);
    CopChaseAudioStatus previous{};
    uint64_t next_ambient = 0, radio_until = 0;
    int radio_priority = 0, attempted_heat = 0;
    bool attempted_siren = false;
    bool attempted_rim = false;
    int rim_level = -1;
    const auto open = [](Stream &stream, const std::string &name, bool loop, float gain) {
        if (!stream.open(player.root, name, loop, gain)) { ++player.missing; return false; }
        return true;
    };
    const auto clear = [&] {
        radio.close(); siren.close(); music.close(); sting.close(); pop.close(); rim.close(); radio_until = next_ambient = 0;
        attempted_siren = false; attempted_heat = 0;
        attempted_rim = false; rim_level = -1; player.rim_volume = 0;
    };
    for (;;) {
        CopChaseAudioStatus status; uint32_t events; uint64_t epoch; bool puncture;
        std::array<Command, queue_size> commands; unsigned command_count;
        {
            std::unique_lock<std::mutex> lock(player.mutex);
            player.wake.wait_for(lock, std::chrono::milliseconds(5), [] { return player.stopping || player.pending_events || player.pending_pop || player.command_count || player.count < ring_frames / 2; });
            if (player.stopping) break;
            status = player.status; events = player.pending_events; player.pending_events = 0;
            puncture = player.pending_pop; player.pending_pop = false;
            command_count = player.command_count; std::copy_n(player.commands.begin(), command_count, commands.begin()); player.command_count = 0;
            epoch = player.epoch;
        }
        const uint64_t now = now_ms();
        const bool expired = now - player.heartbeat.load() > 500;
        if (!status.enabled || !status.running || expired || status.generation != previous.generation ||
            (events & COPCHASE_CANCELLED)) clear();
        if (!status.enabled || !status.running || status.paused || expired || (events & COPCHASE_CANCELLED)) {
            previous = status; player.streams = 0;
            std::unique_lock<std::mutex> lock(player.mutex);
            player.wake.wait_for(lock, std::chrono::milliseconds(10));
            continue;
        }
        auto play_radio = [&](unsigned id, int priority, unsigned fallback) {
            if (id == UINT32_MAX || player.banks[id].clips.empty()) id = fallback;
            if (id == UINT32_MAX || player.banks[id].clips.empty()) return;
            if (now < radio_until && priority < 2 && priority <= radio_priority) { ++player.dropped; return; }
            std::array<unsigned, 200> generic{}; std::array<unsigned, 64> matching{};
            unsigned ng = 0, nm = 0;
            for (unsigned index : player.banks[id].clips) {
                const auto &clip = player.clips[index];
                if (clip.make.empty()) { if (ng < generic.size()) generic[ng++] = index; }
                else if ((clip.make == status.make || clip.make == status.vehicle_class) &&
                    (clip.colour.empty() || (clip.colour == "?" ? !*status.colour : clip.colour == status.colour))) {
                    if (nm < matching.size()) matching[nm++] = index;
                }
            }
            if (!ng && !nm) return;
            const bool preferred = nm && (!ng || random() % 4 != 0);
            const auto &clip = player.clips[preferred ? matching[random() % nm] : generic[random() % ng]];
            if (!open(radio, clip.name, false, 1.f)) return;
            radio_priority = priority;
            radio_until = now + (radio.total_frames * 1000 + radio.source_rate - 1) / radio.source_rate;
            next_ambient = std::max(next_ambient, radio_until + 2500);
            ++player.played;
        };
        auto event = [&](const char *name, int priority, const char *fallback = nullptr) { play_radio(bank(name), priority, bank(fallback)); };
        const bool chasing = status.phase == COPCHASE_PURSUIT || status.phase == COPCHASE_COOLDOWN;
        if (!chasing) { siren.close(); music.close(); sting.close(); attempted_siren = false; attempted_heat = 0; }
        else {
            if (!attempted_siren) { attempted_siren = true; open(siren, "siren_3.wav", true, .30f); }
            if (!status.music) { music.close(); sting.close(); attempted_heat = 0; }
            else if (status.heat != attempted_heat) {
                const std::string name = "music_heat" + std::to_string(status.heat) + ".wav";
                // 1001ECF0 falls back only when starting. The main loop keeps
                // an already playing track if a higher heat file is absent.
                Stream candidate;
                if (open(candidate, name, true, status.music_volume)) music = std::move(candidate);
                else if (!music.channels && !attempted_heat) open(music, "music_pursuit.wav", true, status.music_volume);
                if (!music.channels) player.music_ready = false;
                attempted_heat = status.heat;
            }
            music.gain = status.music_volume;
        }
        if (events & COPCHASE_STARTED) {
            if (chasing && status.music) open(sting, "pursuit_start.wav", false, std::min(1.f, status.music_volume + .15f));
            event("describe", 2); next_ambient = std::max(next_ambient, now + 8000);
        }
        if (events & COPCHASE_LOST_SIGHT) { event("lostvis", 2, "lost"); next_ambient = std::max(next_ambient, now + 3000); }
        if (events & COPCHASE_SPOTTED) event("spotted", 2, "describe");
        if (events & COPCHASE_HEAT_CHANGED) {
            const std::string heat = "heat" + std::to_string(status.heat);
            event(heat.c_str(), 2, status.heat == 10 ? "heat5" : nullptr);
        }
        if (events & COPCHASE_EVADED) { sting.close(); event("evaded", 2, "lost"); }
        if (events & COPCHASE_BUSTED) { sting.close(); event("busted", 2); }
        for (unsigned i = 0; i < command_count; ++i) play_radio(commands[i].bank, commands[i].priority, commands[i].fallback);
        // ASI 1002AAA0 owns separate cc_pop and cc_rim aliases. A new flat
        // restarts only pop, so radio/siren and the continuing scrape overlap.
        if (puncture && open(pop, "tyre_pop.wav", false, 1.f)) ++player.pops;
        const bool scrape = status.flat_tyres && status.rim_sound && status.rim_volume_percent &&
                            status.speed_mps > (rim.channels ? 1.5f : 4.f);
        if (!scrape) { rim.close(); attempted_rim = false; rim_level = -1; player.rim_volume = 0; }
        else {
            if (!attempted_rim) { attempted_rim = true; open(rim, "rim_scrape.wav", true, 0); }
            if (rim.channels) {
                const int level = std::min(500, 150 + int(status.speed_mps * 8.f)) * status.rim_volume_percent / 100;
                if (std::abs(level - rim_level) >= 41) { rim_level = level; rim.gain = level / 1000.f; }
                player.rim_volume = unsigned(std::max(0, rim_level));
            }
        }
        if (chasing && (!next_ambient || now >= next_ambient)) {
            if (status.phase == COPCHASE_COOLDOWN) event("search", 0, random() & 1 ? "status" : "backup");
            else { const char *ambient[] = {"status", "backup", "roadblock", "describe", "status"}; event(ambient[random() % 5], 0); }
            next_ambient = std::max(next_ambient, now + (status.phase == COPCHASE_COOLDOWN ? 10000 : 8000) + random() % 8000);
        }
        previous = status;
        player.streams = unsigned(bool(radio.channels)) + unsigned(bool(siren.channels)) + unsigned(bool(music.channels)) + unsigned(bool(sting.channels)) + unsigned(bool(pop.channels)) + unsigned(bool(rim.channels));
        bool room;
        { std::lock_guard<std::mutex> lock(player.mutex); room = player.count + block_frames <= ring_frames; }
        if (!room) continue;
        std::array<float, block_frames * 2> output{};
        bool has_audio = false;
        for (Stream *voice : {&radio, &siren, &music, &sting, &pop, &rim})
            if (voice->mix(output.data(), block_frames)) has_audio = true;
        if (!has_audio) {
            std::unique_lock<std::mutex> lock(player.mutex); player.wake.wait_for(lock, std::chrono::milliseconds(10)); continue;
        }
        {
            std::lock_guard<std::mutex> lock(player.mutex);
            if (player.epoch != epoch || player.count + block_frames > ring_frames) continue;
            for (unsigned i = 0; i < block_frames; ++i) {
                const unsigned at = (player.head + player.count + i) % ring_frames;
                player.ring[at * 2] = output[i * 2]; player.ring[at * 2 + 1] = output[i * 2 + 1];
            }
            player.count += block_frames;
        }
    }
    clear(); player.streams = 0;
}
}

extern "C" int copchase_audio_start(const char *parent) {
    copchase_audio_stop();
    if (!parent || !*parent) return 0;
    try {
        const auto base = fs::weakly_canonical(fs::absolute(fs::u8path(parent)));
        player.root = fs::weakly_canonical(base / "COPCHASE/resources/audio");
        if (!contained(base, player.root)) return 0;
        if (!load_manifest(player.root)) { copchase_audio_stop(); return 0; }
        Stream initial_music;
        player.music_ready = initial_music.open(player.root, "music_heat1.wav", true, 0) ||
                             initial_music.open(player.root, "music_pursuit.wav", true, 0);
        player.stopping = false; player.status = {}; player.pending_events = player.command_count = 0; player.pending_pop = false;
        player.played = 0; player.dropped = 0; player.missing = 0;
        player.pops = 0; player.rim_volume = 0;
        player.worker = std::thread(worker); player.ready.store(true, std::memory_order_release);
        return 1;
    } catch (...) { copchase_audio_stop(); return 0; }
}
extern "C" void copchase_audio_stop(void) {
    player.ready = false; player.audible = false; player.music_ready = false;
    { std::lock_guard<std::mutex> lock(player.mutex); player.stopping = true; player.count = player.head = player.command_count = 0; ++player.epoch; }
    player.wake.notify_all();
    if (player.worker.joinable()) player.worker.join();
    std::lock_guard<std::mutex> lock(player.mutex);
    player.clips.clear(); player.banks.clear(); player.bank_by_name.clear(); player.root.clear(); player.status = {};
}
extern "C" int copchase_audio_available(void) { return player.ready.load(std::memory_order_acquire); }
extern "C" int copchase_audio_music_available(void) {
    return player.ready.load(std::memory_order_acquire) && player.music_ready.load();
}
extern "C" void copchase_audio_publish(const CopChaseAudioStatus *input) {
    if (!player.ready.load(std::memory_order_acquire) || !input) return;
    CopChaseAudioStatus status = *input;
    status.make[15] = status.vehicle_class[15] = status.colour[7] = 0;
    status.master_volume = volume(status.master_volume); status.music_volume = volume(status.music_volume);
    status.heat = std::clamp(status.heat, 1, 10);
    status.flat_tyres = std::clamp(status.flat_tyres, 0, 4);
    status.new_flat_tyres = std::clamp(status.new_flat_tyres, 0, 4);
    status.rim_volume_percent = std::clamp(status.rim_volume_percent, 0, 100);
    status.speed_mps = std::isfinite(status.speed_mps) ? std::clamp(status.speed_mps, 0.f, 10000.f) : 0.f;
    if (status.phase < COPCHASE_IDLE || status.phase > COPCHASE_COOLDOWN) status.running = 0;
    player.master = status.master_volume;
    player.heartbeat = now_ms(); player.audible = status.enabled && status.running && !status.paused;
    {
        std::lock_guard<std::mutex> lock(player.mutex);
        if (!player.ready.load()) return;
        if (status.generation != player.status.generation || !status.enabled || !status.running || (status.events & COPCHASE_CANCELLED)) {
            player.pending_events = 0; player.command_count = 0; player.pending_pop = false;
        }
        if (status.generation != player.status.generation || status.paused != player.status.paused ||
            !status.enabled || !status.running || status.events || status.new_flat_tyres ||
            status.flat_tyres != player.status.flat_tyres || status.rim_sound != player.status.rim_sound ||
            status.rim_volume_percent != player.status.rim_volume_percent ||
            (status.speed_mps > 4.f) != (player.status.speed_mps > 4.f) ||
            (status.speed_mps > 1.5f) != (player.status.speed_mps > 1.5f)) {
            player.count = player.head = 0; ++player.epoch;
        }
        player.status = status; player.pending_events |= status.events;
        if (status.enabled && status.running && !status.paused && !(status.events & COPCHASE_CANCELLED))
            player.pending_pop |= status.new_flat_tyres != 0;
    }
    player.wake.notify_one();
}
extern "C" int copchase_audio_event(const char *name, int priority, const char *fallback) {
    if (!player.ready.load(std::memory_order_acquire) || !name || priority < 0 || priority > 2) return 0;
    std::lock_guard<std::mutex> lock(player.mutex);
    if (!player.status.enabled || !player.status.running || player.status.paused) return 0;
    const unsigned id = bank(name), alternate = bank(fallback);
    if (id == UINT32_MAX && alternate == UINT32_MAX) return 0;
    if (player.command_count == queue_size) { ++player.dropped; return 0; }
    player.commands[player.command_count++] = {id, alternate, priority};
    if (priority == 2) { player.head = player.count = 0; ++player.epoch; }
    player.wake.notify_one();
    return 1;
}
extern "C" size_t copchase_audio_mix(float *stereo, size_t frames) {
    if (!stereo || !frames || frames > rate || !player.ready.load(std::memory_order_acquire) ||
        !player.audible.load() || now_ms() - player.heartbeat.load() > 500) return 0;
    std::unique_lock<std::mutex> lock(player.mutex, std::try_to_lock);
    if (!lock || !player.count) return 0;
    const size_t count = std::min<size_t>(frames, player.count);
    const float gain = player.master.load();
    for (size_t i = 0; i < count; ++i) {
        const auto at = (player.head + unsigned(i)) % ring_frames;
        for (unsigned ch = 0; ch < 2; ++ch)
            stereo[i * 2 + ch] = std::clamp(stereo[i * 2 + ch] + player.ring[at * 2 + ch] * gain, -1.f, 1.f);
    }
    player.head = (player.head + unsigned(count)) % ring_frames; player.count -= unsigned(count);
    return count;
}
extern "C" CopChaseAudioStats copchase_audio_stats(void) {
    std::lock_guard<std::mutex> lock(player.mutex);
    return {unsigned(player.banks.size()), unsigned(player.clips.size()), player.command_count, player.count,
            player.streams.load(), player.played.load(), player.dropped.load(), player.missing.load(),
            player.pops.load(), player.rim_volume.load()};
}
