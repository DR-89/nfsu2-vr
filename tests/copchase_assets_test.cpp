#include "copchase_assets.h"
#include "xyz_regional_assets.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
static std::string soundtrack_directory;
// Deliberate collaborator stub: select a verified directory as the real
// soundtrack module would. Its independent full-pack checks have their own
// test. This keeps the asset routing test independent of the 1.6 GB music pack.
extern "C" const char *vr_soundtrack_redirect(const char *path) {
    if (soundtrack_directory.empty() || !path) return nullptr;
    if (!std::strcmp(path, "\\Device\\CdRom0\\NFSUNDER\\ZDIR.BIN") ||
        !std::strcmp(path, "D:/NFSUNDER/ZDIR.BIN")) return soundtrack_directory.c_str();
    if (!std::strcmp(path, "D:/NFSUNDER/ZZDATA10.BIN")) return "verified-soundtrack-archive";
    return nullptr;
}

static void environment(const char *name, const char *value) {
#ifdef _WIN32
    if (_putenv_s(name, value ? value : "")) throw std::runtime_error("environment update failed");
#else
    if (value ? setenv(name, value, 1) : unsetenv(name)) throw std::runtime_error("environment update failed");
#endif
}
static std::string env(const char *name) { const char *value = std::getenv(name); return value ? value : ""; }
struct Environments {
    struct Value { const char *name; std::string value; bool present; };
    std::vector<Value> values;
    Environments() {
        for (const char *name : {"NFSU2_COPCHASE_ASSETS_PROBE", "NFSU2_COPCHASE_ASSET_VARIANT", "RECOMP_PAD_SCRIPT", "NFSU2_GAME_DIR", "NFSU2_RUNTIME_DIR"}) {
            const char *value = std::getenv(name);
            values.push_back({name, value ? value : "", value != nullptr});
        }
    }
    ~Environments() {
        copchase_assets_start(nullptr, 0);
        for (auto &value : values) environment(value.name, value.present ? value.value.c_str() : nullptr);
    }
};
struct Temporary {
    fs::path parent, root;
    Temporary() {
        parent = fs::canonical(fs::temp_directory_path());
        root = parent / ("nfsu2-copchase-assets-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!fs::create_directory(root)) throw std::runtime_error("cannot create isolated test directory");
    }
    ~Temporary() {
        std::error_code error;
        const auto resolved = fs::weakly_canonical(root, error);
        if (!error && resolved == root && resolved.parent_path() == parent &&
            resolved.filename().u8string().rfind("nfsu2-copchase-assets-test-", 0) == 0)
            fs::remove_all(root, error); // Only this freshly created, bounded fixture.
    }
};
static std::vector<char> read(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("missing private fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
static void write(const fs::path &path, const std::vector<char> &data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.write(data.data(), std::streamsize(data.size()))) throw std::runtime_error("fixture write failed");
}
static uint32_t word(const char *data) {
    const auto p = reinterpret_cast<const unsigned char *>(data);
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static void put(char *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = char(value >> (i * 8));
}
static std::vector<char> restored_directory(std::vector<char> retail) {
    for (size_t i = 0; i < retail.size(); i += 24) {
        const auto key = word(retail.data() + i);
        if (key == 1628346117) {
            put(retail.data() + i + 4, 10); put(retail.data() + i + 8, 0);
            put(retail.data() + i + 12, 850398); put(retail.data() + i + 16, 1626612096);
        } else if (key == 2228279645 || key == 1628345939) {
            put(retail.data() + i + 4, key == 2228279645 ? 11 : 12); put(retail.data() + i + 8, 0);
        }
    }
    return retail;
}

static int verify_preloaded_texture_metadata(const fs::path &pack, const fs::path &game) {
    const auto archive = read(pack / "ZZDATA13.BIN");
    const auto retail = read(game / "NFSUNDER/ZDIR.BIN");
    std::vector<char> original_memory;
    for (size_t at = 0; at < retail.size(); at += 24) {
        if (word(retail.data() + at) != 0x84d0d95du) continue;
        std::ifstream input(game / ("NFSUNDER/ZZDATA" + std::to_string(word(retail.data() + at + 4)) + ".BIN"), std::ios::binary);
        input.seekg(uint64_t(word(retail.data() + at + 8)) * 2048);
        original_memory.resize(word(retail.data() + at + 16));
        CHECK(input.read(original_memory.data(), original_memory.size()));
        break;
    }
    CHECK(original_memory.size() == 38932);
    std::vector<char> previous_memory;
    for (const char *name : {"ZDIR.BIN", "ZDIR-SONGS.BIN"}) {
        const auto directory = read(pack / name);
        std::vector<char> shared, memory;
        unsigned global_rows = 0, shared_rows = 0;
        for (size_t at = 0; at < directory.size(); at += 24) {
            const uint32_t key = word(directory.data() + at);
            if (key != 0x84d0d95du && key != 0x0287bf4fu) continue;
            CHECK(word(directory.data() + at + 4) == 13);
            const uint64_t offset = uint64_t(word(directory.data() + at + 8)) * 2048;
            const uint32_t size = word(directory.data() + at + 16);
            CHECK(offset + size <= archive.size());
            std::vector<char> payload(archive.begin() + size_t(offset), archive.begin() + size_t(offset) + size);
            auto &target = key == 0x84d0d95du ? memory : shared;
            CHECK(target.empty() || target == payload);
            target = std::move(payload);
            if (key == 0x84d0d95du) ++global_rows; else ++shared_rows;
        }
        CHECK(global_rows == 2 && shared_rows == 23 && memory.size() == 38932);
        CHECK(word(memory.data() + 8) == 0x53219999 && word(memory.data() + 12) == 13);
        bool found = false;
        for (size_t entry = 16; entry < 16 + 13 * 20; entry += 20) {
            if (word(memory.data() + entry) != 0x0287bf4fu) continue;
            const uint32_t offset = word(memory.data() + entry + 4), prefix = word(memory.data() + entry + 12);
            CHECK(word(memory.data() + entry + 8) == shared.size());
            CHECK(offset + prefix <= memory.size() && prefix <= shared.size());
            CHECK(!std::memcmp(memory.data() + offset, shared.data(), prefix));
            found = true;
        }
        CHECK(found);
        if (previous_memory.empty()) {
            CHECK(!std::memcmp(memory.data() + 31088, original_memory.data() + 31088, 6840));
            previous_memory = memory;
        } else {
            CHECK(!std::memcmp(memory.data(), previous_memory.data(), 31088));
            CHECK(!std::memcmp(memory.data() + 37928, previous_memory.data() + 37928, memory.size() - 37928));
            CHECK(std::memcmp(memory.data() + 31088, previous_memory.data() + 31088, 6840) != 0);
        }
    }
    std::puts("PASS both duplicate GLOBALMEMORYFILE rows use exact streamed texture prefix; soundtrack metadata composed");
    return 0;
}

int main(int argc, char **argv) {
    try {
        Environments saved;
        environment("NFSU2_COPCHASE_ASSETS_PROBE", nullptr);
        environment("NFSU2_COPCHASE_ASSET_VARIANT", nullptr);
        environment("RECOMP_PAD_SCRIPT", "0:start:1");
        const std::string previous = "copchase-test-existing-cache";
        environment("NFSU2_RUNTIME_DIR", previous.c_str());
        CHECK(!copchase_assets_start(nullptr, 1));
        CHECK(!copchase_assets_start("missing-copchase-test", 1));
        CHECK(!copchase_assets_ready());
        for (const char *flag : {"0", "01", "true", "1 "}) {
            environment("NFSU2_COPCHASE_ASSETS_PROBE", flag);
            CHECK(!copchase_assets_start("missing-copchase-test", 1));
        }
        environment("NFSU2_COPCHASE_ASSETS_PROBE", "1");
        environment("RECOMP_PAD_SCRIPT", nullptr);
        CHECK(!copchase_assets_start("missing-copchase-test", 1));
        environment("RECOMP_PAD_SCRIPT", "");
        CHECK(!copchase_assets_start("missing-copchase-test", 1));
        environment("RECOMP_PAD_SCRIPT", "0:start:1");
        CHECK(!copchase_assets_start("missing-copchase-test", 0));
        CHECK(!copchase_assets_start("missing-copchase-test", 1));
        CHECK(!copchase_assets_redirect("D:/NFSUNDER/ZDIR.BIN"));
        CHECK(!copchase_assets_redirect("\\Device\\Harddisk0\\Partition1\\CACHE\\LocalCache00.bin"));
        CHECK(env("NFSU2_RUNTIME_DIR") == previous);
        CHECK(!xyz_regional_start(nullptr, 0));
        if (argc == 3 || argc == 4) {
            const fs::path game = fs::absolute(fs::u8path(argv[1]));
            const fs::path source = fs::absolute(fs::u8path(argv[2]));
            Temporary temp;
            const auto pack = temp.root / "COPCHASE";
            fs::create_directory(pack);
            for (const char *name : {"ZDIR.BIN", "ZDIR-SONGS.BIN", "ZZDATA13.BIN"})
                fs::copy_file(source / name, pack / name);
            CHECK(verify_preloaded_texture_metadata(pack, game) == 0);
            const auto parent = temp.root.u8string();
            environment("NFSU2_GAME_DIR", game.u8string().c_str());
            for (const char *variant : {"all", "../copchase", "shared-only8", "slot-textures-only8"}) {
                environment("NFSU2_COPCHASE_ASSET_VARIANT", variant);
                CHECK(!copchase_assets_start(parent.c_str(), 1));
                CHECK(!copchase_assets_ready() && env("NFSU2_RUNTIME_DIR") == previous);
            }
            environment("NFSU2_COPCHASE_ASSET_VARIANT", nullptr);
            environment("NFSU2_COPCHASE_ASSETS_PROBE", nullptr);
            environment("RECOMP_PAD_SCRIPT", nullptr);
            CHECK(!copchase_assets_start(parent.c_str(), 0));
            CHECK(copchase_assets_start(parent.c_str(), 1)); // Explicit startup VR setting.
            CHECK(copchase_assets_ready());
            CHECK(!copchase_assets_start(nullptr, 0));
            CHECK(env("NFSU2_RUNTIME_DIR") == previous);
            environment("NFSU2_COPCHASE_ASSETS_PROBE", "1");
            environment("RECOMP_PAD_SCRIPT", nullptr);
            CHECK(!copchase_assets_start(parent.c_str(), 0)); // Partial probe cannot enable.
            environment("RECOMP_PAD_SCRIPT", "0:start:1");
            CHECK(copchase_assets_start(parent.c_str(), 0)); // Both explicit QA gates.
            CHECK(copchase_assets_start(parent.c_str(), 1));
            CHECK(copchase_assets_ready());
            const std::string plain_cache = env("NFSU2_RUNTIME_DIR");
            CHECK(fs::u8path(plain_cache).filename() == "copchase-0.3.2-mixed-original-v2");
            CHECK(fs::is_directory(fs::u8path(plain_cache)));
            for (const char *prefix : {"D:/NFSUNDER/", "d:/nfsunder/", "\\Device\\CdRom0\\NFSUNDER\\", "\\??\\D:\\NFSUNDER\\"}) {
                for (const char *name : {"ZDIR.BIN", "ZZDATA13.BIN"}) {
                    const auto path = std::string(prefix) + name;
                    const char *mapped = copchase_assets_redirect(path.c_str());
                    CHECK(mapped && fs::equivalent(fs::u8path(mapped), pack / name));
                }
            }
            CHECK(copchase_assets_redirect("d:/nfsunder/zdir.bin"));
            CHECK(!std::strcmp(xyz_community_redirect("D:/NFSUNDER/ZDIR.BIN"), copchase_assets_redirect("D:/NFSUNDER/ZDIR.BIN")));
            for (const char *path : {"D:/NFSUNDER/ZZDATA10.BIN", "D:/NFSUNDER/ZZDATA14.BIN", "D:/NFSUNDER/ZDIR.BIN.bak",
                    "D:/NFSUNDER/ZZDATA13.BIN/", "D:/NFSUNDER/../NFSUNDER/ZDIR.BIN", "D:/NFSUNDER//ZDIR.BIN",
                    "D:/UDATA/NFSUNDER/ZDIR.BIN", "\\Device\\Harddisk0\\Partition1\\NFSUNDER\\ZDIR.BIN",
                    "C:/NFSUNDER/ZDIR.BIN", "NFSUNDER/ZDIR.BIN", "D:/NFSUNDER/ZDIR-SONGS.BIN", "D:/CARS/TEXTURES.BIN"})
                CHECK(!copchase_assets_redirect(path));
            CHECK(!copchase_assets_redirect(nullptr));
            CHECK(!copchase_assets_redirect(std::string(300, 'x').c_str()));

            // NFSU2 opens LocalCache through partition1, not the ordinary
            // Z:/raw-partition save mapping. Case aliases must share one file
            // on Android as well as Windows, without touching the game tree.
            const auto local_cache = fs::u8path(plain_cache) / "Partition1Cache";
            const char *cache_guest = "\\Device\\Harddisk0\\Partition1\\CACHE\\LocalCache00.bin";
            CHECK(fs::is_directory(local_cache));
            for (const char *path : {cache_guest, "/device/harddisk0/partition1/cache/localcache00.bin",
                                    "\\DEVICE\\HARDDISK0\\PARTITION1\\CACHE\\LOCALCACHE00.BIN"}) {
                const char *mapped = copchase_assets_redirect(path);
                CHECK(mapped && *mapped && fs::u8path(mapped) == local_cache / "LOCALCACHE00.BIN");
                write(fs::u8path(mapped), {'c', 'a', 'c', 'h', 'e'});
            }
            CHECK(read(local_cache / "LOCALCACHE00.BIN") == std::vector<char>({'c', 'a', 'c', 'h', 'e'}));
            for (const char *path : {"\\Device\\Harddisk0\\Partition1\\CACHE", "\\Device\\Harddisk0\\Partition1\\CACHE\\"})
                CHECK(fs::equivalent(fs::u8path(copchase_assets_redirect(path)), local_cache));
            const char *unclaimed_cache[] = {
                "\\Device\\Harddisk0\\Partition1\\CACHE2\\LocalCache00.bin",
                "\\Device\\Harddisk0\\Partition1\\UDATA\\LocalCache00.bin",
                "\\Device\\Harddisk0\\Partition1\\TDATA\\LocalCache00.bin",
                "\\Device\\Harddisk0\\Partition2\\CACHE\\LocalCache00.bin",
                "D:/CACHE/LocalCache00.bin", "C:/CACHE/LocalCache00.bin", "CACHE/LocalCache00.bin"};
            for (const char *path : unclaimed_cache) CHECK(!copchase_assets_redirect(path));
            for (const char *tail : {"../UDATA/save", "./LocalCache00.bin", "sub/../../save", "/LocalCache00.bin",
                                     "LocalCache00.bin/", "LocalCache00.bin:stream", "LocalCache00.bin.",
                                     "LocalCache00.bin ", "C:/save", "*"}) {
                const std::string path = std::string("/Device/Harddisk0/Partition1/CACHE/") + tail;
                const char *mapped = copchase_assets_redirect(path.c_str());
                CHECK(mapped && !*mapped); // Claimed and rejected; never retail fallback.
                CHECK(xyz_community_redirect(path.c_str()) && !*xyz_community_redirect(path.c_str()));
            }
            const std::string long_cache = std::string("/Device/Harddisk0/Partition1/CACHE/") + std::string(300, 'x');
            CHECK(copchase_assets_redirect(long_cache.c_str()) && !*copchase_assets_redirect(long_cache.c_str()));

            const auto outside = temp.root / "outside-cache";
            fs::create_directory(outside);
            write(outside / "untouched.bin", {'s', 'a', 'v', 'e'});
            std::error_code symlink_error;
            fs::create_directory_symlink(outside, local_cache / "LINK", symlink_error);
            if (!symlink_error) {
                const char *mapped = copchase_assets_redirect("/Device/Harddisk0/Partition1/CACHE/LINK/untouched.bin");
                CHECK(mapped && !*mapped);
                fs::remove(local_cache / "LINK");
                fs::create_symlink(outside / "untouched.bin", local_cache / "ESCAPE.BIN");
                mapped = copchase_assets_redirect("/Device/Harddisk0/Partition1/CACHE/escape.bin");
                CHECK(mapped && !*mapped);
                fs::remove(local_cache / "ESCAPE.BIN");
                CHECK(read(outside / "untouched.bin") == std::vector<char>({'s', 'a', 'v', 'e'}));
                CHECK(!copchase_assets_start(nullptr, 0));
                fs::rename(local_cache, fs::u8path(plain_cache) / "Partition1Cache.saved");
                fs::create_directory_symlink(outside, local_cache);
                CHECK(!copchase_assets_start(parent.c_str(), 1));
                CHECK(!copchase_assets_ready() && env("NFSU2_RUNTIME_DIR") == previous);
                fs::remove(local_cache);
                fs::rename(fs::u8path(plain_cache) / "Partition1Cache.saved", local_cache);
                CHECK(copchase_assets_start(parent.c_str(), 1));
                std::puts("PASS cache directory/file symlink escapes and linked-root mount rejection");
            } else {
                std::fprintf(stderr, "SKIP filesystem symlink cases: %s\n", symlink_error.message().c_str());
            }

            // Only metadata is composed for the soundtrack collaborator stub.
            const auto retail = read(game / "NFSUNDER/ZDIR.BIN");
            const auto song_path = temp.root / "restored-ZDIR.BIN";
            const auto song_data = restored_directory(retail);
            write(song_path, song_data);
            soundtrack_directory = song_path.u8string();
            CHECK(copchase_assets_start(parent.c_str(), 1));
            CHECK(fs::equivalent(fs::u8path(copchase_assets_redirect("D:/NFSUNDER/ZDIR.BIN")), pack / "ZDIR-SONGS.BIN"));
            CHECK(env("NFSU2_RUNTIME_DIR") != plain_cache);
            CHECK(fs::u8path(env("NFSU2_RUNTIME_DIR")).filename() == "copchase-0.3.2-mixed-restored-v2");
            CHECK(fs::u8path(copchase_assets_redirect(cache_guest)).parent_path() != local_cache);
            CHECK(!std::strcmp(xyz_community_redirect("D:/NFSUNDER/ZDIR.BIN"), copchase_assets_redirect("D:/NFSUNDER/ZDIR.BIN")));
            CHECK(!std::strcmp(xyz_community_redirect("D:/NFSUNDER/ZZDATA10.BIN"), "verified-soundtrack-archive"));
            CHECK(!copchase_assets_start(nullptr, 0));
            CHECK(!copchase_assets_ready() && env("NFSU2_RUNTIME_DIR") == previous);
            CHECK(!copchase_assets_redirect(cache_guest));
            CHECK(!std::strcmp(xyz_community_redirect("D:/NFSUNDER/ZDIR.BIN"), soundtrack_directory.c_str()));

            auto corrupt = song_data; corrupt[0] ^= 1; write(song_path, corrupt);
            CHECK(!copchase_assets_start(parent.c_str(), 1));
            CHECK(env("NFSU2_RUNTIME_DIR") == previous);
            write(song_path, song_data);
            fs::rename(pack / "ZDIR-SONGS.BIN", pack / "ZDIR-SONGS.backup");
            CHECK(!copchase_assets_start(parent.c_str(), 1));
            fs::rename(pack / "ZDIR-SONGS.backup", pack / "ZDIR-SONGS.BIN");
            soundtrack_directory.clear();
            fs::rename(pack / "ZDIR.BIN", pack / "ZDIR.backup");
            CHECK(!copchase_assets_start(parent.c_str(), 1));
            fs::rename(pack / "ZDIR.backup", pack / "ZDIR.BIN");
            const auto packed = read(pack / "ZZDATA13.BIN");
            corrupt = packed; corrupt[0] ^= 1; write(pack / "ZZDATA13.BIN", corrupt);
            CHECK(!copchase_assets_start(parent.c_str(), 1)); // Same size, wrong hash.
            CHECK(!copchase_assets_ready() && !copchase_assets_redirect("D:/NFSUNDER/ZZDATA13.BIN"));
            write(pack / "ZZDATA13.BIN", std::vector<char>(packed.begin(), packed.end() - 1));
            CHECK(!copchase_assets_start(parent.c_str(), 1)); // Truncated archive.
            write(pack / "ZZDATA13.BIN", packed);
            CHECK(copchase_assets_start(parent.c_str(), 1));
            CHECK(!copchase_assets_start(nullptr, 0));
            CHECK(env("NFSU2_RUNTIME_DIR") == previous);

            if (argc == 4) {
                const auto factors = fs::absolute(fs::u8path(argv[3]));
                for (const char *variant : {"shared-only8", "slot-textures-only8"}) {
                    const auto factor = pack / "diagnostics" / variant;
                    fs::create_directories(factor);
                    for (const char *name : {"ZDIR.BIN", "ZDIR-SONGS.BIN", "ZZDATA13.BIN"})
                        fs::copy_file(factors / variant / name, factor / name);
                    environment("NFSU2_COPCHASE_ASSET_VARIANT", variant);
                    for (bool restored : {false, true}) {
                        soundtrack_directory = restored ? song_path.u8string() : "";
                        CHECK(copchase_assets_start(parent.c_str(), 1));
                        CHECK(fs::equivalent(fs::u8path(copchase_assets_redirect("D:/NFSUNDER/ZDIR.BIN")),
                                             factor / (restored ? "ZDIR-SONGS.BIN" : "ZDIR.BIN")));
                        CHECK(fs::equivalent(fs::u8path(copchase_assets_redirect("D:/NFSUNDER/ZZDATA13.BIN")), factor / "ZZDATA13.BIN"));
                        CHECK(fs::u8path(env("NFSU2_RUNTIME_DIR")).parent_path() == factor / "RuntimeCache");
                    }
                    CHECK(!copchase_assets_start(nullptr, 0));
                }
                environment("NFSU2_COPCHASE_ASSET_VARIANT", nullptr);
                soundtrack_directory.clear();
                CHECK(copchase_assets_start(parent.c_str(), 1));
                CHECK(fs::equivalent(fs::u8path(copchase_assets_redirect("D:/NFSUNDER/ZZDATA13.BIN")), pack / "ZZDATA13.BIN"));
                CHECK(!copchase_assets_start(nullptr, 0));
                std::puts("PASS pinned shared-only/slot-textures factor variants, both soundtracks and default restoration");
            }

            const auto bad_game = temp.root / "unsupported-game";
            fs::create_directories(bad_game / "NFSUNDER");
            environment("NFSU2_GAME_DIR", bad_game.u8string().c_str());
            corrupt = retail; corrupt[0] ^= 1; write(bad_game / "NFSUNDER/ZDIR.BIN", corrupt);
            CHECK(!copchase_assets_start(parent.c_str(), 1));
            write(bad_game / "NFSUNDER/ZDIR.BIN", retail);
            write(bad_game / "NFSUNDER/ZZDATA13.BIN", {});
            CHECK(!copchase_assets_start(parent.c_str(), 1));
            CHECK(env("NFSU2_RUNTIME_DIR") == previous);
            std::puts("PASS optional private pack: both overlays, partition1 cache isolation, path rejection, fallback, corrupt and missing assets");
        } else if (argc != 1) {
            throw std::runtime_error("usage: copchase_assets_test [original-Xbox-game prepared-CopChase-pack [private-diagnostic-packs]]");
        }
        std::puts("PASS CopChase startup selection, scripted probes and disabled defaults; no game launch");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
