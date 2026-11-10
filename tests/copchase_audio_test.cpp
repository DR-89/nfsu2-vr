#include "copchase_audio.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
static unsigned checks;
static void check(bool value, const char *message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); copchase_audio_stop(); std::exit(1); }
}
static void put16(std::ofstream &file, unsigned value) {
    const char data[]{char(value), char(value >> 8)}; file.write(data, 2);
}
static void put32(std::ofstream &file, unsigned value) { put16(file, value); put16(file, value >> 16); }
static void wav(const fs::path &path, unsigned rate, unsigned channels, const std::vector<int16_t> &samples) {
    std::ofstream file(path, std::ios::binary);
    file.write("RIFF", 4); put32(file, 36 + unsigned(samples.size() * 2)); file.write("WAVEfmt ", 8);
    put32(file, 16); put16(file, 1); put16(file, channels); put32(file, rate);
    put32(file, rate * channels * 2); put16(file, channels * 2); put16(file, 16);
    file.write("data", 4); put32(file, unsigned(samples.size() * 2));
    for (int16_t sample : samples) put16(file, uint16_t(sample));
}
static void write(const fs::path &path, const std::string &value) { std::ofstream(path, std::ios::binary) << value; }
static CopChaseAudioStatus status;
static void publish() { copchase_audio_publish(&status); status.events = 0; status.new_flat_tyres = 0; }
static bool wait(const std::function<bool()> &predicate, unsigned milliseconds = 1000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        publish();
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
static std::array<float, 32> samples(float baseline = 0) {
    check(wait([] { return copchase_audio_stats().buffered_frames >= 16; }), "audio worker supplies PCM");
    std::array<float, 32> output; output.fill(baseline);
    for (unsigned i = 0; i != 100; ++i) {
        if (copchase_audio_mix(output.data(), 16) == 16) return output;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(false, "bounded callback gets ring lock"); return output;
}
static bool near(float a, float b) { return std::abs(a - b) < .0001f; }
static void start(const fs::path &base) {
    check(copchase_audio_start(base.u8string().c_str()) == 1, "start fixture");
    status = {}; status.enabled = status.running = 1; status.master_volume = 1;
    status.music_volume = .25f; status.heat = 1; status.generation = 1; publish();
}

int main(int argc, char **argv) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path base = fs::temp_directory_path() / ("nfsu2-copchase-audio-" + std::to_string(nonce));
    const fs::path root = base / "COPCHASE/resources/audio";
    fs::create_directories(root);
    const std::string manifest = R"({"status":["status_01.wav"],"backup":["backup_01.wav"],
      "describe":["describe_01.wav","describe_02.wav"],"tagged":["describe_01.wav"],
      "bad":["bad_01.wav"],"absent":["absent_01.wav"],"ramp":["ramp_01.wav"],
      "stereo":["stereo_01.wav"],"evaded":["backup_01.wav"],"lost":["status_01.wav"]})";
    write(root / "native-manifest.json", manifest);
    write(root / "chatter_tags.txt", "# verified fixture vocabulary\ndescribe_01.wav bmw blue\n");
    wav(root / "status_01.wav", 24000, 1, std::vector<int16_t>(48000, 8192));
    wav(root / "backup_01.wav", 24000, 1, std::vector<int16_t>(48000, -16384));
    wav(root / "describe_01.wav", 24000, 1, std::vector<int16_t>(48000, 24576));
    wav(root / "describe_02.wav", 24000, 1, std::vector<int16_t>(48000, 4096));
    wav(root / "ramp_01.wav", 24000, 1, {0, 32767, 0, -32768});
    std::vector<int16_t> stereo(36000 * 2);
    for (size_t i = 0; i < stereo.size(); i += 2) { stereo[i] = 8192; stereo[i + 1] = -8192; }
    wav(root / "stereo_01.wav", 36000, 2, stereo);
    write(root / "bad_01.wav", "RIFFinvalid");

    check(!copchase_audio_available(), "unstarted optional module unavailable");
    std::array<float, 2> untouched{.1f, -.2f};
    check(copchase_audio_mix(untouched.data(), 1) == 0 && near(untouched[0], .1f), "unstarted callback untouched");
    check(!copchase_audio_start(nullptr), "null startup rejected");
    start(base);
    check(!copchase_audio_music_available(), "manifest alone cannot claim playable music");
    check(copchase_audio_stats().clips == 8, "manifest aliases share clip records");
    check(!copchase_audio_event("scanner", 2, nullptr), "absent scanner bank skipped");
    check(!copchase_audio_event("status", 3, nullptr), "invalid priority rejected");
    check(copchase_audio_event("status", 0, nullptr), "queue background radio");
    auto audio = samples(.1f);
    check(near(audio[0], .35f) && near(audio[1], .35f), "mono 24k resampled and added to stereo");
    check(copchase_audio_event("backup", 0, nullptr), "queue equal priority radio");
    check(wait([] { return copchase_audio_stats().events_dropped > 0; }), "equal priority cannot interrupt");
    check(copchase_audio_stats().events_played == 1, "priority drop preserves voice");
    check(copchase_audio_event("backup", 2, nullptr), "queue priority two");
    check(wait([] { return copchase_audio_stats().events_played == 2; }), "priority two preempts");
    audio = samples(); check(near(audio[0], -.5f), "priority change flushes old PCM");
    status.paused = 1; publish();
    check(!copchase_audio_mix(untouched.data(), 1) && near(untouched[0], .1f), "pause silences immediately");
    check(!copchase_audio_event("status", 2, nullptr), "paused incident rejected");
    status.paused = 0; publish(); audio = samples();
    check(near(audio[0], -.5f), "resume restores stream");
    status.enabled = 0; publish();
    check(!copchase_audio_mix(untouched.data(), 1), "disabled stops callback");
    check(wait([] { return copchase_audio_stats().open_streams == 0; }), "disabled closes streams");

    start(base);
    check(copchase_audio_event("describe", 2, nullptr), "unknown identity describe");
    audio = samples(); check(near(audio[0], .125f), "unknown identity only plays generic line");
    start(base);
    check(copchase_audio_event("tagged", 2, nullptr), "tagged bank queues");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    check(copchase_audio_stats().events_played == 0, "unmatched make and colour suppressed");
    std::snprintf(status.make, sizeof(status.make), "bmw"); std::snprintf(status.colour, sizeof(status.colour), "blue"); publish();
    check(copchase_audio_event("tagged", 2, nullptr), "verified tagged identity");
    audio = samples(); check(near(audio[0], .75f), "matching make and colour accepted");
    start(base);
    check(copchase_audio_event("unknown", 2, "status"), "explicit fallback bank queues");
    audio = samples(); check(near(audio[0], .25f), "missing bank uses explicit fallback");

    start(base);
    check(copchase_audio_event("ramp", 2, nullptr), "queue interpolation fixture");
    audio = samples();
    const float expected[]{0, 32767.f / 65536, 32767.f / 32768, 32767.f / 65536, 0, -.5f, -1, -1};
    for (unsigned i = 0; i < 8; ++i) check(near(audio[i * 2], expected[i]) && near(audio[i * 2 + 1], expected[i]), "24k exact interpolation and final-frame hold");
    check(near(audio[16], 0), "one shot ends without repeating");
    start(base);
    check(copchase_audio_event("stereo", 2, nullptr), "queue 36k stereo");
    audio = samples(); check(near(audio[0], .25f) && near(audio[1], -.25f), "stereo channels preserved during resampling");
    for (unsigned block = 0; block < 64; ++block) {
        check(wait([] { return copchase_audio_stats().buffered_frames >= 256; }), "stream crosses fixed input-cache boundary");
        std::array<float, 512> output{};
        size_t mixed = 0;
        for (unsigned attempt = 0; !mixed && attempt < 100; ++attempt) {
            mixed = copchase_audio_mix(output.data(), 256);
            if (!mixed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(mixed == 256, "streamed block delivered");
        for (size_t frame = 0; frame < mixed; ++frame)
            check(near(output[frame * 2], .25f) && near(output[frame * 2 + 1], -.25f), "stream-cache boundary preserves samples");
    }
    status.master_volume = .5f; publish(); audio = samples(); check(near(audio[0], .125f), "master gain applied at callback");
    status.master_volume = NAN; publish(); audio = samples(); check(near(audio[0], 0), "nonfinite master muted");
    std::this_thread::sleep_for(std::chrono::milliseconds(530));
    check(!copchase_audio_mix(untouched.data(), 1), "stale game heartbeat cannot leave audible loops");

    start(base);
    check(copchase_audio_event("bad", 2, nullptr), "malformed optional WAV queues");
    check(wait([] { return copchase_audio_stats().missing_clips == 1; }), "malformed WAV skipped safely");
    wav(root / "bad_01.wav", 24000, 1, {0, 0, 0, 0});
    fs::resize_file(root / "bad_01.wav", 50);
    check(copchase_audio_event("bad", 2, nullptr), "truncated RIFF queues");
    check(wait([] { return copchase_audio_stats().missing_clips == 2; }), "truncated chunk bounds rejected");
    check(copchase_audio_event("absent", 2, nullptr), "missing optional WAV queues");
    check(wait([] { return copchase_audio_stats().missing_clips == 3; }), "missing WAV skipped safely");
    status.phase = COPCHASE_PURSUIT; status.music = 1; publish();
    check(wait([] { return copchase_audio_stats().missing_clips == 6; }), "optional missing siren and tracks attempted once");
    std::this_thread::sleep_for(std::chrono::milliseconds(40)); publish();
    check(copchase_audio_stats().missing_clips == 6, "missing loops do not repeatedly reopen files");

    // Tiny loop files exercise end wrap without loading large music into RAM.
    wav(root / "siren_3.wav", 32000, 1, std::vector<int16_t>(32, 8192));
    wav(root / "music_heat1.wav", 36000, 1, std::vector<int16_t>(36, 8192));
    wav(root / "music_pursuit.wav", 36000, 1, std::vector<int16_t>(36, -8192));
    wav(root / "pursuit_start.wav", 36000, 1, std::vector<int16_t>(36, 0));
    start(base); status.phase = COPCHASE_PURSUIT; status.music = 1; status.events = COPCHASE_STARTED; publish();
    check(copchase_audio_music_available(), "valid music enables host ownership gate");
    audio = samples(); check(near(audio[0], .125f + .25f * .3f + .25f * .25f), "start mixes radio, siren and music at original gains");
    check(copchase_audio_stats().open_streams <= 4, "bounded voice count");
    status.heat = 5; publish();
    check(wait([] { return copchase_audio_stats().missing_clips > 0; }), "missing higher heat track examined");
    check(copchase_audio_stats().missing_clips == 1, "higher heat missing preserves current music");
    status.phase = COPCHASE_IDLE; status.events = COPCHASE_EVADED; publish();
    check(wait([] { return copchase_audio_stats().events_played == 2; }), "evade radio event mapped");
    audio = samples(); check(near(audio[0], -.5f), "evade stops loops and plays outcome");
    status.events = COPCHASE_CANCELLED; publish();
    check(wait([] { return copchase_audio_stats().open_streams == 0; }), "cancel clears all voices");
    check(copchase_audio_stats().buffered_frames == 0, "cancel drops queued PCM");
    for (unsigned i = 0; i < 20000; ++i) copchase_audio_event("status", 2, nullptr);
    auto stats = copchase_audio_stats();
    check(stats.queued <= 16 && stats.buffered_frames <= 4096 && stats.events_dropped > 0, "incident queue and ring remain bounded under flood");
    copchase_audio_stop(); check(!copchase_audio_available() && !copchase_audio_stats().open_streams, "shutdown joins worker and releases voices");
    copchase_audio_stop();
    check(!copchase_audio_music_available(), "stop releases music availability");
    write(root / "music_heat1.wav", "invalid WAV");
    start(base); check(copchase_audio_music_available(), "valid fallback permits music with corrupt heat-1");
    copchase_audio_stop(); write(root / "music_pursuit.wav", "invalid WAV");
    start(base); check(!copchase_audio_music_available(), "both corrupt music WAVs fail closed");
    copchase_audio_stop();
    wav(root / "music_heat1.wav", 36000, 1, std::vector<int16_t>(36, 8192));
    start(base); check(copchase_audio_music_available(), "only playable heat-1 is sufficient at startup");
    status.phase = COPCHASE_PURSUIT; status.music = 1; status.heat = 5; publish();
    check(wait([] { return !copchase_audio_music_available(); }), "failed initial heat/fallback open releases host music ownership");
    copchase_audio_stop();

    // Puncture effects have independent bounded voices and survive an idle
    // pursuit phase while the verified driving context still exists.
    wav(root / "tyre_pop.wav", 48000, 1, std::vector<int16_t>(16, 4096));
    wav(root / "rim_scrape.wav", 48000, 1, std::vector<int16_t>(16, 8192));
    start(base); status.flat_tyres = 1; status.new_flat_tyres = 1; publish();
    check(wait([] { return copchase_audio_stats().tyre_pops_played == 1; }), "explicit puncture starts pop in idle pursuit phase");
    audio = samples(); check(near(audio[0], .125f), "pop plays at original unattenuated effect gain");
    std::this_thread::sleep_for(std::chrono::milliseconds(25)); publish();
    check(copchase_audio_stats().tyre_pops_played == 1, "persistent flat state does not replay pop");
    audio = samples(); check(near(audio[0], 0), "short tyre pop ends without looping");
    status.new_flat_tyres = 3; publish();
    check(wait([] { return copchase_audio_stats().tyre_pops_played == 2; }), "simultaneous punctures coalesce to one new pop");
    audio = samples(); check(near(audio[0], .125f), "new puncture restarts separate pop voice");

    start(base); status.rim_sound = 1; status.rim_volume_percent = 60; status.speed_mps = 20; publish();
    std::this_thread::sleep_for(std::chrono::milliseconds(25)); publish();
    check(copchase_audio_stats().rim_volume_milli == 0, "healthy tyres cannot scrape");
    status.flat_tyres = 1; status.speed_mps = 4; publish();
    std::this_thread::sleep_for(std::chrono::milliseconds(25)); publish();
    check(copchase_audio_stats().rim_volume_milli == 0, "rim loop requires speed strictly above four");
    status.speed_mps = 5; publish();
    check(wait([] { return copchase_audio_stats().rim_volume_milli == 114; }), "rim starts with integer original gain and sixty-percent setting");
    audio = samples(); check(near(audio[0], .25f * .114f), "rim PCM uses original gain");
    status.speed_mps = 13.5f; publish();
    std::this_thread::sleep_for(std::chrono::milliseconds(25)); publish();
    check(copchase_audio_stats().rim_volume_milli == 114, "rim gain difference forty does not change volume");
    status.speed_mps = 13.625f; publish();
    check(wait([] { return copchase_audio_stats().rim_volume_milli == 155; }), "rim gain difference forty-one updates volume");
    status.speed_mps = 100; publish();
    check(wait([] { return copchase_audio_stats().rim_volume_milli == 300; }), "rim speed gain caps at five hundred before configured percentage");
    status.speed_mps = 2; publish();
    check(wait([] { return copchase_audio_stats().rim_volume_milli == 99; }), "active rim persists below start threshold with updated gain");
    status.speed_mps = 1.5f; publish();
    check(wait([] { return copchase_audio_stats().rim_volume_milli == 0; }), "rim stops exactly at one-point-five");
    status.speed_mps = 2; publish();
    std::this_thread::sleep_for(std::chrono::milliseconds(25)); publish();
    check(copchase_audio_stats().rim_volume_milli == 0, "stopped rim does not restart below four");
    status.speed_mps = 5; publish();
    check(wait([] { return copchase_audio_stats().rim_volume_milli == 114; }), "rim restarts after crossing four");
    check(copchase_audio_event("status", 2, nullptr), "radio overlaps rim effect");
    check(wait([] { return copchase_audio_stats().events_played == 1; }), "overlapping radio opened");
    status.new_flat_tyres = 1; publish();
    check(wait([] { return copchase_audio_stats().tyre_pops_played == 1; }), "new puncture overlaps radio and scrape");
    audio = samples(); check(near(audio[0], .25f + .125f + .25f * .114f), "radio, pop and rim voices sum independently");
    check(copchase_audio_stats().open_streams <= 6, "six voices remain bounded");
    status.flat_tyres = 0; publish();
    check(wait([] { return copchase_audio_stats().rim_volume_milli == 0; }), "repaired tyres close scrape");
    status.flat_tyres = 1; status.rim_sound = 0; publish();
    std::this_thread::sleep_for(std::chrono::milliseconds(25)); publish();
    check(copchase_audio_stats().rim_volume_milli == 0, "rim setting off prevents reopening");
    status.rim_sound = 1; status.rim_volume_percent = 0; publish();
    std::this_thread::sleep_for(std::chrono::milliseconds(25)); publish();
    check(copchase_audio_stats().rim_volume_milli == 0, "zero rim volume closes loop");
    status.rim_volume_percent = 60; status.speed_mps = NAN; publish();
    std::this_thread::sleep_for(std::chrono::milliseconds(25)); publish();
    check(copchase_audio_stats().rim_volume_milli == 0, "invalid speed cannot activate rim loop");
    status.speed_mps = 5; status.paused = 1; status.new_flat_tyres = 1; publish();
    std::this_thread::sleep_for(std::chrono::milliseconds(25)); publish();
    check(copchase_audio_stats().tyre_pops_played == 1 && !copchase_audio_mix(untouched.data(), 1), "paused puncture cannot queue later playback");
    status.paused = 0; status.generation++; publish();
    check(wait([] { return copchase_audio_stats().rim_volume_milli == 114; }), "generation reset reconstructs only current valid rim state");
    status.running = 0; publish();
    check(wait([] { return !copchase_audio_stats().open_streams && !copchase_audio_stats().rim_volume_milli; }), "lost driving context closes tyre voices");
    copchase_audio_stop();
    fs::remove(root / "rim_scrape.wav"); fs::remove(root / "tyre_pop.wav");
    start(base); status.flat_tyres = 1; status.new_flat_tyres = 1; status.speed_mps = 5;
    status.rim_sound = 1; status.rim_volume_percent = 60; publish();
    check(wait([] { return copchase_audio_stats().missing_clips == 2; }), "missing optional tyre effects skip safely");
    std::this_thread::sleep_for(std::chrono::milliseconds(30)); publish();
    check(copchase_audio_stats().missing_clips == 2, "missing rim does not retry every audio block");
    status.speed_mps = 0; publish();
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); publish(); status.speed_mps = 5; publish();
    check(wait([] { return copchase_audio_stats().missing_clips == 3; }), "new rim threshold crossing permits one retry");
    copchase_audio_stop();

    wav(base / "outside.wav", 24000, 1, {0, 0, 0, 0});
    fs::remove(root / "backup_01.wav");
    std::error_code symlink_error;
    fs::create_symlink(base / "outside.wav", root / "backup_01.wav", symlink_error);
    if (!symlink_error) {
        start(base);
        check(copchase_audio_event("backup", 2, nullptr), "symlink resource queues without callback I/O");
        check(wait([] { return copchase_audio_stats().missing_clips == 1; }), "resource symlink escape rejected on worker");
        copchase_audio_stop(); fs::remove(root / "backup_01.wav");
        write(base / "outside-manifest.json", manifest);
        fs::remove(root / "native-manifest.json");
        fs::create_symlink(base / "outside-manifest.json", root / "native-manifest.json");
        check(!copchase_audio_start(base.u8string().c_str()), "manifest symlink escape rejected at startup");
        fs::remove(root / "native-manifest.json");
    } else std::puts("SKIP: filesystem symlink privilege unavailable");

    write(root / "native-manifest.json", "{\"status\":[\"../escape.wav\"]}");
    check(!copchase_audio_start(base.u8string().c_str()), "manifest traversal rejected");
    write(root / "native-manifest.json", "{\"status\":[\"status_01.wav\",\"status_01.wav\"]}");
    check(!copchase_audio_start(base.u8string().c_str()), "duplicate clip rejected");
    write(root / "native-manifest.json", manifest); write(root / "chatter_tags.txt", "describe_01.wav bmw blue unexpected\n");
    check(!copchase_audio_start(base.u8string().c_str()), "invalid annotations fail closed");
    fs::remove(root / "chatter_tags.txt");
    check(!copchase_audio_start(base.u8string().c_str()), "absent annotations never misidentify player");
    // Real local fixture is optional; no mod bytes are part of this test source.
    if (argc == 2) {
        start(fs::u8path(argv[1])); stats = copchase_audio_stats();
        check(copchase_audio_music_available(), "original music WAV validates before host mute");
        check(stats.clips == 1063 && stats.banks == 37, "private original manifest loads completely");
        check(!copchase_audio_event("scanner", 2, nullptr), "original missing scanner stays unavailable");
        status.phase = COPCHASE_PURSUIT; status.music = 1; status.events = COPCHASE_STARTED; publish();
        check(wait([] { return copchase_audio_stats().events_played == 1 && copchase_audio_stats().open_streams == 4; }), "original radio, siren, music and sting open");
        status.flat_tyres = status.new_flat_tyres = status.rim_sound = 1;
        status.rim_volume_percent = 60; status.speed_mps = 5; publish();
        check(wait([] { return copchase_audio_stats().open_streams == 6 && copchase_audio_stats().tyre_pops_played == 1; }), "original tyre pop and rim overlap all four pursuit voices");
        audio = samples();
        check(std::all_of(audio.begin(), audio.end(), [](float value) { return std::isfinite(value) && std::abs(value) <= 1; }), "real original audio safely decoded");
        copchase_audio_stop();
    }
    fs::remove_all(base); // Unique test directory generated above, never game/saves.
    std::printf("CopChase audio: %u checks passed\n", checks);
}
