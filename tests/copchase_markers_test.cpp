#include "copchase_markers.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <vector>

namespace {
constexpr uint32_t global = 0x3f9d3c, sentinel = 0x10000;
struct Memory {
    std::map<uint32_t, unsigned char> bytes;
    unsigned reads = 0, root_reads = 0;
    bool replace = false;
    void put(uint32_t address, const void *p, unsigned size) {
        const auto *b = static_cast<const unsigned char *>(p);
        for (unsigned i = 0; i < size; ++i) bytes[address+i] = b[i];
    }
    void word(uint32_t address, uint32_t v) { put(address, &v, 4); }
    void number(uint32_t address, float v) { put(address, &v, 4); }
    static int read(void *ctx, uint32_t address, void *out, uint32_t size) {
        auto &m = *static_cast<Memory *>(ctx);
        ++m.reads;
        if (address == global && ++m.root_reads == 2 && m.replace) m.word(global, 0x40000);
        for (uint32_t i = 0; i < size; ++i)
            if (!m.bytes.count(address+i)) return 0;
        for (uint32_t i = 0; i < size; ++i)
            static_cast<unsigned char *>(out)[i] = m.bytes.at(address+i);
        return 1;
    }
    void list(unsigned count, uint32_t type = 6) {
        *this = {};
        word(global, sentinel);
        word(sentinel, count ? 0x20000 : sentinel);
        word(sentinel+4, count ? 0x20000+(count-1)*32 : sentinel);
        for (unsigned i = 0; i < count; ++i) {
            const uint32_t address = 0x20000+i*32, desc = 0x30000+i*8;
            uint32_t words[8] = {i+1<count ? address+32 : sentinel,
                i ? address-32 : sentinel, 0, 0, 0, 0, 0, desc};
            put(address, words, sizeof words);
            number(address+12, float(i)); number(address+16, 0);
            word(desc, 0); word(desc+4, type);
        }
    }
};
CopChaseMarkerResult collect(Memory &m, CopChaseMarkers &out) {
    const CopChaseMarkerReader reader{&m, Memory::read};
    return copchase_markers_collect(&reader, &out);
}
}

int main() {
    Memory m; CopChaseMarkers out{}; const float origin[2] = {0, 0}; float nearest;
    assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN && !out.count);
    m.list(0); assert(collect(m, out) == COPCHASE_MARKERS_VALID && !out.count && !out.visited);
    assert(!copchase_markers_hiding(&out, origin, 12, &nearest) && nearest == 1e9f);
    m.list(4); m.word(0x30004, 5); m.word(0x3000c, 6);
    m.word(0x30014, 18); m.word(0x3001c, 19);
    assert(collect(m, out) == COPCHASE_MARKERS_VALID && out.count == 2 && out.visited == 4);
    assert(out.xy[0][0] == 1 && out.xy[1][0] == 2 && !out.truncated);
    assert(copchase_markers_hiding(&out, origin, 12, &nearest) && nearest == 1);
    m.list(1); m.number(0x2000c, 12); assert(collect(m, out) == COPCHASE_MARKERS_VALID);
    assert(!copchase_markers_hiding(&out, origin, 12, &nearest) && nearest == 12);
    assert(copchase_markers_hiding(&out, origin, 12.001f, &nearest));
    m.list(1); m.word(0x2001c, 0); assert(collect(m, out) == COPCHASE_MARKERS_VALID && !out.count);
    m.list(64); assert(collect(m, out) == COPCHASE_MARKERS_VALID && out.count == 64 && !out.truncated);
    m.list(65); assert(collect(m, out) == COPCHASE_MARKERS_VALID && out.count == 64 && out.truncated);
    assert(out.visited == 64 && m.reads == 131);
    m.list(300, 5); assert(collect(m, out) == COPCHASE_MARKERS_VALID && !out.count && !out.truncated);
    m.list(301, 5); assert(collect(m, out) == COPCHASE_MARKERS_VALID && out.visited == 300 && out.truncated);
    assert(m.reads == 603);
    m.list(2); m.word(0x20020, 0x20000); assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN && !out.count);
    m.list(1); m.word(0x20000, 0); assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN);
    m.list(1); m.word(0x20004, 0x20000); assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN);
    m.list(1); m.word(sentinel+4, sentinel); assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN);
    m.list(1); m.bytes.erase(0x30004); assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN);
    m.list(1); m.word(0x2001c, UINT32_MAX-3); assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN);
    m.list(1); m.number(0x2000c, std::numeric_limits<float>::quiet_NaN());
    assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN && !copchase_markers_hiding(&out, origin, 12, &nearest));
    m.list(1); m.number(0x20010, std::numeric_limits<float>::infinity()); assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN);
    m.list(1); m.number(0x20010, 1000001); assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN);
    m.list(1); m.replace = true; assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN && !out.count);
    m.list(1); assert(collect(m, out) == COPCHASE_MARKERS_VALID);
    m.word(global, 0); assert(collect(m, out) == COPCHASE_MARKERS_UNKNOWN && !out.count);
    assert(copchase_markers_collect(nullptr, &out) == COPCHASE_MARKERS_UNKNOWN);
    assert(copchase_markers_collect(nullptr, nullptr) == COPCHASE_MARKERS_UNKNOWN);
    std::puts("CopChase markers: checked list bounds, corruption, identity replacement and hiding PASS");
}
