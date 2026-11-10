#include "xyz_profiles.h"
#include <chrono>
#include <fstream>
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    using namespace xyz; namespace fs = std::filesystem;
    CHECK(online_name("   Dirk VR!  ") == "Dirk_VR");
    CHECK(online_name("ab") == "ab_" && online_name("a") == "a__");
    CHECK(online_name("---") == "---" && online_name("!!!").empty());
    CHECK(online_name("12345678901234 x") == "12345678901234x");
    CHECK(online_name("1234567890123456789") == "123456789012345");
    const auto root = fs::temp_directory_path() / ("nfsu2-xyz-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(root));
    struct Cleanup { fs::path p; ~Cleanup() { std::error_code ec; fs::remove_all(p, ec); } } cleanup{root};
    Profiles p(root); CHECK(p.load() && p.all().empty() && !p.selected());
    const auto identity = p.identity(); CHECK(identity.size() == 32);
    CHECK(p.create("Quest Driver") && p.selected()->name == "Quest_Driver");
    const auto first = p.selected()->id;
    CHECK(p.edit(first, "Quest Driver", "VR-Test") && p.selected()->room == "VR-Test");
    CHECK(!p.edit(first, "Quest", std::string("bad\xC0\xAF")));
    CHECK(p.create("PCVR_Driver")); const auto second = p.selected()->id;
    CHECK(first != second && p.identity() == identity && p.all().size() == 2);
    Profiles loaded(root); CHECK(loaded.load() && loaded.identity() == identity && loaded.selected()->id == second);
    CHECK(loaded.select(first) && loaded.selected()->room == "VR-Test");
    CHECK(loaded.remove(first) && loaded.selected()->id == second);
    CHECK(!loaded.remove("unknown") && loaded.all().size() == 1);
    CHECK(!loaded.create("!!!") && loaded.all().size() == 1);
    for (int i = 0; i < 7; ++i) CHECK(loaded.create("Driver" + std::to_string(i)));
    CHECK(!loaded.create("Ninth") && loaded.all().size() == 8);
    CHECK(p.load() && p.all().size() == 8); // All operations persisted.
    auto selected = p.selected()->id;
    // A failed atomic replacement must not change the selected profile in RAM.
    fs::rename(root / "xyz-profiles.json", root / "backup.json");
    fs::create_directory(root / "xyz-profiles.json");
    CHECK(!p.remove(selected) && p.all().size() == 8 && p.selected()->id == selected);
    fs::remove(root / "xyz-profiles.json");
    // Corruption must never be silently replaced by a new empty catalogue.
    { std::ofstream f(root / "xyz-profiles.json"); f << "{\"version\":2}"; }
    CHECK(!p.load() && !p.create("Replacement"));
    std::ifstream file(root / "xyz-profiles.json"); std::string untouched; std::getline(file, untouched);
    CHECK(untouched == "{\"version\":2}");
    file.close();
    fs::remove(root / "xyz-profiles.json"); fs::rename(root / "backup.json", root / "xyz-profiles.json");
    CHECK(p.load() && p.all().size() == 8);
    std::puts("xyz_profiles: names, persistence, selection, limits, failed-write and corruption recovery passed");
    return 0;
}
