#include "copchase_profile.h"
#include "vendor/auth/sha256.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>

namespace {
constexpr size_t path_limit = 4096, filename_limit = 128;
struct Path { std::array<char, path_limit> value{}; size_t length = 0; bool windows = false; };
char lower(char ch) { return ch >= 'A' && ch <= 'Z' ? char(ch + 'a' - 'A') : ch; }
bool slash(char ch) { return ch == '/' || ch == '\\'; }
bool hex(char ch) { ch = lower(ch); return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); }
bool equal_ascii(const char *a, const char *b) {
    while (*a && *b) if (lower(*a++) != lower(*b++)) return false;
    return !*a && !*b;
}
bool normalize(const char *input, Path &out, bool root = false) {
    if (!input) return false;
    size_t length = 0;
    while (length < path_limit && input[length]) ++length;
    if (!length || length == path_limit) return false;
    if (!root && slash(input[length - 1])) return false;
    // Kernel GetFinalPathNameByHandle returns extended drive/UNC paths.
    size_t skip = 0; bool unc = false;
    if (length >= 4 && slash(input[0]) && slash(input[1]) && input[2] == '?' && slash(input[3])) {
        skip = 4;
        if (length >= 8 && lower(input[4]) == 'u' && lower(input[5]) == 'n' && lower(input[6]) == 'c' && slash(input[7])) {
            skip = 8; unc = true;
        }
    }
    if (unc) { out.value[0] = out.value[1] = '/'; out.length = 2; }
    for (size_t i = skip; i < length; ++i) {
        const unsigned char ch = static_cast<unsigned char>(input[i]);
        if (ch < 32 || ch == 127) return false;
        out.value[out.length++] = slash(char(ch)) ? '/' : char(ch);
    }
    while (out.length > 1 && out.value[out.length - 1] == '/') out.value[--out.length] = 0;
    out.windows = out.length >= 3 && ((lower(out.value[0]) >= 'a' && lower(out.value[0]) <= 'z' &&
        out.value[1] == ':' && out.value[2] == '/') || (out.value[0] == '/' && out.value[1] == '/'));
    if (!out.windows && out.value[0] != '/') return false;
    if (!out.windows && std::strchr(input, '\\')) return false;
    if (skip && !out.windows) return false; // No device namespace or relative extended path.
    const size_t start = out.windows ? (out.value[1] == ':' ? 3 : 2) : 1;
    if (start >= out.length) return false;
    unsigned components = 0;
    for (size_t at = start; at < out.length;) {
        size_t end = at;
        while (end < out.length && out.value[end] != '/') {
            const char ch = out.value[end];
            if (ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') return false;
            ++end;
        }
        if (end == at || (end - at == 1 && out.value[at] == '.') ||
            (end - at == 2 && out.value[at] == '.' && out.value[at + 1] == '.') ||
            out.value[end - 1] == '.' || out.value[end - 1] == ' ') return false;
        ++components; at = end + 1;
    }
    if (out.windows && out.value[1] != ':' && components < 2) return false;
    if (out.windows) for (size_t i = 0; i < out.length; ++i) out.value[i] = lower(out.value[i]);
    return true;
}
struct Source {
    std::array<uint8_t, 32> path_hash{}, filename_hash{};
    std::array<char, 13> uid{};
};
struct Segment {
    uintptr_t begin = 0, end = 0, handle = 0;
    uint64_t offset = 0, observed = 0;
    Source source;
};
bool range(const void *buffer, size_t bytes, uintptr_t &begin, uintptr_t &end) {
    begin = reinterpret_cast<uintptr_t>(buffer);
    if (!begin || !bytes || bytes > std::numeric_limits<uintptr_t>::max() - begin) return false;
    end = begin + bytes; return true;
}
bool intersects(const Segment &segment, uintptr_t begin, uintptr_t end) {
    return segment.begin && segment.begin < end && segment.end > begin;
}
uint64_t clock_ms() { return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count()); }
}

struct CopChaseProfileTracker {
    Path root;
    std::array<Segment, COPCHASE_PROFILE_SEGMENTS> segments{};
    uint64_t last_time = 0;
    bool clock_valid = false;

    bool clock(uint64_t now) {
        if (clock_valid && now < last_time) { segments = {}; last_time = now; return false; }
        clock_valid = true; last_time = now;
        for (auto &segment : segments)
            if (segment.begin && now - segment.observed > COPCHASE_PROFILE_MAX_AGE_MS) segment = {};
        return true;
    }
    bool source(const char *path, Source &result) const {
        Path file;
        if (!normalize(path, file) || file.windows != root.windows || file.length <= root.length ||
            std::memcmp(file.value.data(), root.value.data(), root.length) || file.value[root.length] != '/') return false;
        const char *relative = file.value.data() + root.length + 1;
        if (std::strlen(relative) < 14 || relative[12] != '/') return false;
        for (unsigned i = 0; i < 12; ++i) {
            if (!hex(relative[i])) return false;
            result.uid[i] = lower(relative[i]);
        }
        const char *name = relative + 13;
        const size_t length = std::strlen(name);
        if (!length || length > filename_limit || std::strchr(name, '/')) return false;
        if (equal_ascii(name, "SaveMeta.xbx") || equal_ascii(name, "SaveImage.xbx") || equal_ascii(name, "TitleMeta.xbx")) return false;
        std::array<char, filename_limit + 1> normalized{};
        for (size_t i = 0; i < length; ++i) {
            // ASCII case is stable across FAT-style PCVR/Quest paths. Other
            // UTF-8 bytes remain exact; they never become record path text.
            normalized[i] = lower(name[i]);
        }
        nfsu2_sha256(normalized.data(), length, result.filename_hash.data());
        nfsu2_sha256(file.value.data(), file.length, result.path_hash.data());
        return true;
    }
};

extern "C" CopChaseProfileTracker *copchase_profile_tracker_create(const char *root) {
    auto tracker = new(std::nothrow) CopChaseProfileTracker;
    if (!tracker) return nullptr;
    const bool valid = normalize(root, tracker->root, true);
    constexpr char suffix[] = "/udata/4541005a";
    if (!valid || tracker->root.length < sizeof(suffix) - 1 ||
        !equal_ascii(tracker->root.value.data() + tracker->root.length - (sizeof(suffix) - 1), suffix)) {
        delete tracker; return nullptr;
    }
    return tracker;
}
extern "C" void copchase_profile_tracker_destroy(CopChaseProfileTracker *tracker) { delete tracker; }
extern "C" void copchase_profile_tracker_reset(CopChaseProfileTracker *tracker) {
    if (tracker) { tracker->segments = {}; tracker->clock_valid = false; tracker->last_time = 0; }
}
extern "C" void copchase_profile_tracker_observe(CopChaseProfileTracker *tracker, void *handle,
    const char *path, void *buffer, size_t bytes, int64_t offset, uint64_t now) {
    if (!tracker || !bytes) return; // A zero-byte success overwrites nothing.
    uintptr_t begin, end;
    tracker->clock(now);
    if (!range(buffer, bytes, begin, end)) { copchase_profile_tracker_reset(tracker); return; }
    // Any successful write to an observed destination destroys its old proof,
    // including unrelated files, missing paths, and overlapping short reads.
    for (auto &segment : tracker->segments) if (intersects(segment, begin, end)) segment = {};
    Source source;
    if (!handle || offset < 0 || uint64_t(bytes) > uint64_t(INT64_MAX) - uint64_t(offset) || !tracker->source(path, source)) return;
    auto free = std::find_if(tracker->segments.begin(), tracker->segments.end(), [](const Segment &s) { return !s.begin; });
    if (free == tracker->segments.end()) { tracker->segments = {}; free = tracker->segments.begin(); }
    *free = {begin, end, reinterpret_cast<uintptr_t>(handle), uint64_t(offset), now, source};
}
extern "C" int copchase_profile_tracker_claim(CopChaseProfileTracker *tracker, const void *buffer,
    size_t bytes, uint64_t now, CopChaseProfileProof *proof) {
    if (proof) *proof = {};
    if (!tracker || !proof) return 0;
    uintptr_t begin, end;
    if (!range(buffer, bytes, begin, end) || uint64_t(bytes) > uint64_t(INT64_MAX) || !tracker->clock(now)) {
        copchase_profile_tracker_reset(tracker); return 0;
    }
    std::array<const Segment *, COPCHASE_PROFILE_SEGMENTS> covered{};
    size_t count = 0;
    for (const auto &segment : tracker->segments) if (intersects(segment, begin, end)) covered[count++] = &segment;
    std::sort(covered.begin(), covered.begin() + count, [](const Segment *a, const Segment *b) { return a->begin < b->begin; });
    uintptr_t cursor = begin;
    uint64_t origin = 0;
    const Segment *first = count ? covered[0] : nullptr;
    bool valid = first && first->begin <= begin;
    if (valid) {
        origin = first->offset + uint64_t(begin - first->begin);
        valid = uint64_t(bytes) <= uint64_t(INT64_MAX) - origin;
    }
    for (size_t i = 0; valid && i < count && cursor < end; ++i) {
        const Segment &segment = *covered[i];
        if (segment.begin > cursor || segment.end <= cursor || segment.handle != first->handle ||
            segment.source.path_hash != first->source.path_hash ||
            segment.offset + uint64_t(cursor - segment.begin) != origin + uint64_t(cursor - begin)) valid = false;
        else cursor = std::min(segment.end, end);
    }
    valid = valid && cursor == end;
    if (valid) {
        char digest[33]{};
        constexpr char digits[] = "0123456789abcdef";
        for (unsigned i = 0; i < 16; ++i) {
            digest[i * 2] = digits[first->source.filename_hash[i] >> 4];
            digest[i * 2 + 1] = digits[first->source.filename_hash[i] & 15];
        }
        std::snprintf(proof->id, sizeof(proof->id), "4541005a_%s_%s", first->source.uid.data(), digest);
        std::memcpy(proof->container_uid, first->source.uid.data(), sizeof(proof->container_uid));
        proof->file_offset = origin; proof->bytes = bytes;
    }
    for (auto &segment : tracker->segments) if (intersects(segment, begin, end)) segment = {};
    return valid ? 1 : 0;
}

namespace { std::mutex global_mutex; CopChaseProfileTracker *global_tracker; }
extern "C" int copchase_profile_start(const char *root) {
    auto tracker = copchase_profile_tracker_create(root);
    std::lock_guard<std::mutex> lock(global_mutex);
    delete global_tracker; global_tracker = tracker; return tracker ? 1 : 0;
}
extern "C" void copchase_profile_reset(void) {
    std::lock_guard<std::mutex> lock(global_mutex); copchase_profile_tracker_reset(global_tracker);
}
extern "C" void copchase_profile_stop(void) {
    std::lock_guard<std::mutex> lock(global_mutex); delete global_tracker; global_tracker = nullptr;
}
extern "C" void copchase_profile_observe(void *handle, const char *path, void *buffer, size_t bytes, int64_t offset) {
    std::lock_guard<std::mutex> lock(global_mutex);
    copchase_profile_tracker_observe(global_tracker, handle, path, buffer, bytes, offset, clock_ms());
}
extern "C" int copchase_profile_claim(const void *buffer, size_t bytes, CopChaseProfileProof *proof) {
    std::lock_guard<std::mutex> lock(global_mutex);
    return copchase_profile_tracker_claim(global_tracker, buffer, bytes, clock_ms(), proof);
}
