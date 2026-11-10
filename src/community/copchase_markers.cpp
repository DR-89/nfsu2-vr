#include "copchase_markers.h"
#include "copchase_incidents.h"
#include <cmath>
#include <cstring>

namespace {
constexpr uint32_t marker_list_global = 0x003f9d3c;
bool read(const CopChaseMarkerReader &r, uint32_t address, void *out, uint32_t size) {
    return address && !(address & 3u) && size &&
        uint64_t(address) + size <= uint64_t(UINT32_MAX) + 1 &&
        r.read(r.context, address, out, size) != 0;
}
bool position(float f) { return std::isfinite(f) && std::fabs(f) <= 1e6f; }
}

extern "C" CopChaseMarkerResult copchase_markers_collect(
    const CopChaseMarkerReader *reader, CopChaseMarkers *out) {
    if (!out) return COPCHASE_MARKERS_UNKNOWN;
    *out = {};
    if (!reader || !reader->read) return COPCHASE_MARKERS_UNKNOWN;
    uint32_t list = 0, links[2] = {};
    if (!read(*reader, marker_list_global, &list, sizeof list) ||
        !read(*reader, list, links, sizeof links)) return COPCHASE_MARKERS_UNKNOWN;
    CopChaseMarkers result{};
    uint32_t seen[300] = {}, node = links[0], previous = list;
    while (node != list && result.visited < 300 && result.count < 64) {
        for (unsigned i = 0; i < result.visited; ++i)
            if (seen[i] == node) return COPCHASE_MARKERS_UNKNOWN;
        uint32_t words[8] = {};
        if (!read(*reader, node, words, sizeof words) || words[1] != previous ||
            !words[0] || (words[0] & 3u)) return COPCHASE_MARKERS_UNKNOWN;
        seen[result.visited++] = node;
        if (words[7]) {
            uint32_t descriptor[2] = {};
            if (!read(*reader, words[7], descriptor, sizeof descriptor))
                return COPCHASE_MARKERS_UNKNOWN;
            if (descriptor[1] >= 6 && descriptor[1] <= 18) {
                float xy[2];
                std::memcpy(xy, words + 3, sizeof xy);
                if (!position(xy[0]) || !position(xy[1])) return COPCHASE_MARKERS_UNKNOWN;
                result.xy[result.count][0] = xy[0];
                result.xy[result.count++][1] = xy[1];
            }
        }
        previous = node;
        node = words[0];
    }
    if (node == list) {
        if (links[1] != previous) return COPCHASE_MARKERS_UNKNOWN;
    } else {
        /* Preserve the source's bounded prefix. A visible cycle at the cap
         * is still malformed; no additional guest reads are necessary. */
        for (unsigned i = 0; i < result.visited; ++i)
            if (seen[i] == node) return COPCHASE_MARKERS_UNKNOWN;
        result.truncated = 1;
    }
    /* A caller violating the main-thread contract may replace the root
     * during its callback. Do not publish a snapshot of the old list. */
    uint32_t current = 0;
    if (!read(*reader, marker_list_global, &current, sizeof current) || current != list)
        return COPCHASE_MARKERS_UNKNOWN;
    result.result = COPCHASE_MARKERS_VALID;
    *out = result;
    return result.result;
}

extern "C" int copchase_markers_hiding(const CopChaseMarkers *markers,
    const float *xy, float radius, float *nearest) {
    if (nearest) *nearest = 1e9f;
    if (!markers || markers->result != COPCHASE_MARKERS_VALID ||
        markers->count > 64 || markers->visited > 300 || markers->count > markers->visited)
        return 0;
    return copchase_incidents_hiding(xy, markers->xy, markers->count, radius, nearest);
}
