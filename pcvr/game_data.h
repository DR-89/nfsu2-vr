#pragma once
#ifdef __cplusplus
#include <windows.h>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace pcvr_data {
namespace fs = std::filesystem;
struct Expected { const char *path; uint64_t bytes; const char *sha256; };
using Manifest = std::vector<Expected>;
using Progress = std::function<void(uint64_t, uint64_t, const std::wstring &)>;
using Canceled = std::function<bool()>;
const Manifest &retail_manifest();
std::wstring wide(const std::string &text);
std::string utf8(const std::wstring &text);
void check_destination(const fs::path &path);
bool installed(const fs::path &game, const Manifest &files);
void import_iso(const fs::path &iso, const fs::path &game, const Manifest &files,
                const Progress &progress, const Canceled &canceled);
class Lock {
    HANDLE handle = INVALID_HANDLE_VALUE;
public:
    explicit Lock(const fs::path &game);
    ~Lock();
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
};
}
extern "C" {
#endif
/* 1: ready, 0: user canceled, -1: startup error. Lock lasts through gameplay. */
int pcvr_prepare_game_data(void);
void pcvr_release_game_data(void);
#ifdef __cplusplus
}
#endif
