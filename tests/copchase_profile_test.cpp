#include "copchase_profile.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

static unsigned checks;
static void check(bool value, const char *why) {
    ++checks; if (!value) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static void *pointer(uintptr_t value) { return reinterpret_cast<void *>(value); }
static const char *root = "C:/Games/NFSU2/UDATA/4541005a";
static const char *save = "C:/Games/NFSU2/UDATA/4541005a/004e132a0f44/RANDI";
static const char *other = "C:/Games/NFSU2/UDATA/4541005a/004e132a0f44/OTHER";
static constexpr size_t payload = 0xd218;
static void observe(CopChaseProfileTracker *t, uintptr_t address, size_t bytes, int64_t offset,
    uint64_t time = 10, const char *path = save, uintptr_t handle = 42) {
    copchase_profile_tracker_observe(t, pointer(handle), path, pointer(address), bytes, offset, time);
}
static bool claim(CopChaseProfileTracker *t, uintptr_t address, size_t bytes, uint64_t time = 11,
    CopChaseProfileProof *out = nullptr) {
    CopChaseProfileProof result;
    const int accepted = copchase_profile_tracker_claim(t, pointer(address), bytes, time, &result);
    if (!accepted) check(result.id[0] == 0 && result.bytes == 0 && result.file_offset == 0, "failed proof cleared");
    if (out) *out = result;
    return accepted != 0;
}
static CopChaseProfileTracker *fresh() {
    auto *t = copchase_profile_tracker_create(root); check(t != nullptr, "canonical Windows root accepted"); return t;
}

int main() {
    auto *t = fresh(); CopChaseProfileProof proof;
    check(!claim(t, 0x10000, payload), "no identity from unobserved buffer");
    observe(t, 0x10000, payload, 8);
    check(claim(t, 0x10000, payload, 11, &proof), "complete payload binds identity");
    check(proof.file_offset == 8 && proof.bytes == payload && std::strlen(proof.id) == 54 &&
          std::strstr(proof.id, "4541005a_004e132a0f44_") == proof.id, "stable bounded record ID");
    const std::string expected_id = proof.id;
    check(expected_id == "4541005a_004e132a0f44_87af47dc686bdc22e0a65f57600a922c", "identity uses fixed lowercase filename SHA256 vector");
    check(std::strcmp(proof.container_uid, "004e132a0f44") == 0, "container identity exposed separately without filename assumption");
    check(!claim(t, 0x10000, payload, 12), "proof is consumed exactly once");
    observe(t, 0x10000, payload + 8, 0, 20);
    check(claim(t, 0x10008, payload, 21, &proof) && proof.file_offset == 8 && proof.id == expected_id,
          "one whole-file read can cover payload subrange");

    // Separate native task header and payload destinations need no guessed
    // adjacency: the decoder's task/header gate is independently verified.
    observe(t, 0x70000, 8, 0, 30);
    observe(t, 0x10000, payload / 2, 8, 31);
    observe(t, 0x10000 + payload / 2, payload - payload / 2, 8 + payload / 2, 32);
    check(claim(t, 0x10000, payload, 33), "fragmented contiguous payload with independent header buffer");
    copchase_profile_tracker_reset(t);
    observe(t, 0x10000 + payload / 2, payload - payload / 2, 8 + payload / 2, 35);
    observe(t, 0x10000, payload / 2, 8, 36);
    check(claim(t, 0x10000, payload, 37), "completion order need not equal offset order");

    observe(t, 0x10000, payload - 1, 8, 40);
    check(!claim(t, 0x10000, payload, 41), "partial payload rejected");
    observe(t, 0x10000, 100, 8, 42);
    observe(t, 0x10000 + 101, payload - 101, 109, 43);
    check(!claim(t, 0x10000, payload, 44), "one-byte destination gap rejected");
    observe(t, 0x10000, 100, 8, 45);
    observe(t, 0x10000 + 100, payload - 100, 109, 46);
    check(!claim(t, 0x10000, payload, 47), "noncontiguous source offsets rejected");
    observe(t, 0x10000, 100, 8, 48);
    observe(t, 0x10000 + 100, payload - 100, 108, 49, other);
    check(!claim(t, 0x10000, payload, 50), "mixed filenames cannot establish coverage");
    observe(t, 0x10000, 100, 8, 51);
    observe(t, 0x10000 + 100, payload - 100, 108, 52, save, 43);
    check(!claim(t, 0x10000, payload, 53), "different handles cannot concatenate unrelated read lifetimes");
    observe(t, 0x10000, 100, 8, 53);
    observe(t, 0x10000 + 100, payload - 100, 108, 53,
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f45/RANDI");
    check(!claim(t, 0x10000, payload, 53), "same display filename in different UID is a different source");
    observe(t, 0x10000, payload, 8, 54);
    observe(t, 0x10005, 1, 0, 55, nullptr);
    check(!claim(t, 0x10000, payload, 56), "unknown source overwrite invalidates original full coverage");
    observe(t, 0x10000, payload, 8, 57);
    observe(t, 0x10005, 1, 13, 58);
    check(!claim(t, 0x10000, payload, 59), "overlapping partial reread cannot revive discarded proof");
    observe(t, 0x10000, payload, 8, 60, other);
    check(claim(t, 0x10000, payload, 61, &proof) && proof.id != expected_id, "complete buffer reuse establishes only new identity");
    observe(t, 0x10000, payload, 8, 62);
    observe(t, 0x10000 + payload, 100, 0, 63, nullptr);
    check(claim(t, 0x10000, payload, 64), "adjacent unrelated read does not invalidate nonoverlapping memory");
    observe(t, 0x10000, payload, 8, 65);
    observe(t, 0x10005, 0, -1, 66, nullptr);
    check(claim(t, 0x10000, payload, 67), "zero-byte success overwrites nothing");

    observe(t, 0x10000, payload, 8, 100);
    check(claim(t, 0x10000, payload, 100 + COPCHASE_PROFILE_MAX_AGE_MS), "exact age boundary accepted");
    observe(t, 0x10000, payload, 8, 40000);
    check(!claim(t, 0x10000, payload, 40001 + COPCHASE_PROFILE_MAX_AGE_MS), "expired provenance rejected");
    observe(t, 0x10000, payload, 8, 80000);
    check(!claim(t, 0x10000, payload, 79999), "backwards clock invalidates stale proof");
    observe(t, 0x10000, payload, 8, UINT64_MAX - 1);
    check(!claim(t, 0x10000, payload, 1), "clock wrap invalidates stale proof");
    observe(t, std::numeric_limits<uintptr_t>::max() - 5, 6, 0, 2);
    check(!claim(t, std::numeric_limits<uintptr_t>::max() - 5, 6, 3), "destination pointer wrap rejected without dereference");
    observe(t, 0x10000, payload, INT64_MAX - 2, 4);
    check(!claim(t, 0x10000, payload, 5), "source offset wrap rejected");
    observe(t, 0x10000, payload, -1, 6);
    check(!claim(t, 0x10000, payload, 7), "unknown signed offset rejected");
    observe(t, 0x10000, payload, 8, 8, save, 0);
    check(!claim(t, 0x10000, payload, 9), "missing handle rejected");
    for (unsigned i = 0; i < 64; ++i) observe(t, 0x10000 + i * 16, 16, 8 + i * 16, 100 + i);
    check(claim(t, 0x10000, 1024, 164), "full bounded 64-segment coverage accepted");
    for (unsigned i = 0; i < 65; ++i) observe(t, 0x10000 + i * 16, 16, 8 + i * 16, 200 + i);
    check(!claim(t, 0x10000, 1040, 265), "capacity overflow cannot retain partial old proof");
    observe(t, 0x10000, payload, 8, 300);
    copchase_profile_tracker_reset(t);
    check(!claim(t, 0x10000, payload, 301), "explicit new-load/reset drops prior identity");

    const char *invalid_paths[] = {
        "D:/Games/NFSU2/UDATA/4541005a/004e132a0f44/RANDI",
        "C:/Other/UDATA/4541005a/004e132a0f44/RANDI",
        "C:/Games/NFSU2/UDATA/4541005abc/004e132a0f44/RANDI",
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f44/../RANDI",
        "C:/Games/NFSU2/UDATA/4541005a/./004e132a0f44/RANDI",
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f44/RANDI:ads",
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f44/RANDI/",
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f44/RANDI.",
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f44/RANDI ",
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f44/SAVEMETA.XBX",
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f44/saveimage.xbx",
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f44/nested/RANDI",
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f4g/RANDI",
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f4/RANDI",
        "C:/Games/NFSU2/UDATA/4541005a/004e132a0f444/RANDI",
        "C:/Games/NFSU2/UDATA/4541005a//004e132a0f44/RANDI",
        "UDATA/4541005a/004e132a0f44/RANDI", "\\\\.\\PhysicalDrive0",
        "\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\UDATA\\4541005a\\004e132a0f44\\RANDI"
    };
    for (const char *path : invalid_paths) {
        copchase_profile_tracker_reset(t); observe(t, 0x10000, payload, 8, 10, path);
        check(!claim(t, 0x10000, payload), "invalid source path rejected");
    }
    observe(t, 0x10000, payload, 8, 20, "\\\\?\\C:\\GAMES\\nfsu2\\udata\\4541005A\\004E132A0F44\\randi");
    check(claim(t, 0x10000, payload, 21, &proof) && proof.id == expected_id, "Windows long path and case aliases preserve identity");
    observe(t, 0x10000, payload, 8, 22, "C:/Games/NFSU2/UDATA/4541005a/004e132a0f44/R\xc3\x84NDI");
    check(claim(t, 0x10000, payload, 23, &proof) && proof.id != expected_id, "UTF-8 filename retains distinct bounded digest identity");
    copchase_profile_tracker_destroy(t);
    for (const char *bad : {"UDATA/4541005a", "C:/game/OTHER/4541005a", "C:/game/UDATA/4541005b", "C:/game/../UDATA/4541005a"})
        check(copchase_profile_tracker_create(bad) == nullptr, "invalid configured root rejected");
    t = copchase_profile_tracker_create("\\\\?\\UNC\\server\\share\\game\\UDATA\\4541005a\\");
    check(t != nullptr, "extended UNC title root accepted");
    observe(t, 0x10000, payload, 8, 10, "\\\\SERVER\\SHARE\\GAME\\udata\\4541005A\\004E132A0F44\\RANDI");
    check(claim(t, 0x10000, payload, 11, &proof) && proof.id == expected_id, "UNC source retains portable save identity");
    copchase_profile_tracker_destroy(t);
    t = copchase_profile_tracker_create("/storage/emulated/0/NFSU2/UDATA/4541005a");
    check(t != nullptr, "POSIX title root accepted");
    observe(t, 0x10000, payload, 8, 10, "/storage/emulated/0/NFSU2/UDATA/4541005a/004e132a0f44/RANDI");
    check(claim(t, 0x10000, payload, 11, &proof) && proof.id == expected_id, "PCVR and Quest identities agree");
    observe(t, 0x10000, payload, 8, 12, "/storage/emulated/0/nfsu2/UDATA/4541005a/004e132a0f44/RANDI");
    check(!claim(t, 0x10000, payload, 13), "POSIX root containment remains case-sensitive");
    copchase_profile_tracker_destroy(t);

    check(copchase_profile_start(root), "default tracker starts");
    copchase_profile_observe(pointer(42), save, pointer(0x10000), payload, 8);
    check(copchase_profile_claim(pointer(0x10000), payload, &proof) && proof.id == expected_id, "real-clock wrapper binds same identity");
    copchase_profile_observe(pointer(42), save, pointer(0x10000), payload, 8);
    copchase_profile_reset();
    check(!copchase_profile_claim(pointer(0x10000), payload, &proof), "global reset invalidates proof");
    copchase_profile_stop();
    copchase_profile_observe(pointer(42), save, pointer(0x10000), payload, 8);
    check(!copchase_profile_claim(pointer(0x10000), payload, &proof), "stopped observer cannot establish identity");
    std::printf("CopChase profile provenance: %u checks passed\n", checks);
}
