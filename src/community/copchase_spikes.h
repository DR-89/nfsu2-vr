#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Reconstructed CopChase v0.3.2 wheel/strip test and flat-tyre assistance.
 * No guest access or physics writes. Reset on player/world replacement. */
typedef struct {
    uint8_t flat_mask,previous_valid;
    float previous_longitudinal[4];
} CopChaseSpikeState;
typedef struct {
    float position[3],forward[2],left[2],half_width;
    int active;
} CopChaseSpikeStrip;
typedef struct {
    float position[3],forward[2],left[2],velocity[2];
    float wheel_offset[4][2],seconds;
    int driving,control_locked,arrest_phase;
    CopChaseSpikeStrip strip;
} CopChaseSpikeInput;
typedef struct {
    unsigned newly_flat_mask,flat_count;
    float speed,physical_delta[3];
    int apply_delta,scrape_active;
} CopChaseSpikeOutput;
void copchase_spikes_init(CopChaseSpikeState *);
/* The delta targets the separately validated physical body, as in driver.h.
 * Invalid input clears output and preserves state. Wheel offsets must come
 * from the current car descriptor, not guessed wheel positions. */
int copchase_spikes_step(CopChaseSpikeState *,const CopChaseSpikeInput *,CopChaseSpikeOutput *);
typedef struct {
    CopChaseSpikeStrip strip;
    uint32_t created_ms;
    int height_fixed,near_announced;
} CopChaseSpikeLifecycle;
typedef struct {
    uint32_t now_ms;
    float player_position[2],player_velocity[2];
    int block_active;
    /* Only settled, still valid mode1 block cars are included by the host. */
    unsigned settled_count;
    float settled_height[4];
} CopChaseSpikeLifecycleInput;
typedef struct { int removed,height_changed,approaching; } CopChaseSpikeLifecycleOutput;
int copchase_spikes_lifecycle(CopChaseSpikeLifecycle *,const CopChaseSpikeLifecycleInput *,CopChaseSpikeLifecycleOutput *);
#ifdef __cplusplus
}
#endif
