#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Read-only native Xbox marker collector for Vitorini's CopChase v0.3.2.
 * Call on the guest main thread in a verified live world context. The reader
 * must reject inaccessible ranges; it must not invoke guest code. The list
 * global is resolved afresh on every call: a HUD rebuild can replace it even
 * without a world-generation change. No guest pointers escape this API. */
typedef struct {
    void *context;
    int (*read)(void *, uint32_t address, void *out, uint32_t size);
} CopChaseMarkerReader;
enum CopChaseMarkerResult {
    COPCHASE_MARKERS_UNKNOWN = 0,
    COPCHASE_MARKERS_VALID = 1
};
typedef struct {
    enum CopChaseMarkerResult result;
    unsigned count, visited;
    /* Original collector stops at 64 accepted markers or 300 visited nodes.
     * A bounded valid prefix is retained when either limit is reached. */
    int truncated;
    float xy[64][2];
} CopChaseMarkers;

enum CopChaseMarkerResult copchase_markers_collect(
    const CopChaseMarkerReader *, CopChaseMarkers *);
/* Unknown/malformed snapshots never grant the hiding bonus. The state core
 * remains responsible for its cooldown-phase and speed<3 m/s gates. */
int copchase_markers_hiding(const CopChaseMarkers *, const float player_xy[2],
                           float radius, float *nearest);
#ifdef __cplusplus
}
#endif
