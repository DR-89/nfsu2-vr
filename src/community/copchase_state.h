#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Native, deterministic state/timer adaptation of CopChase v0.3.2 by
 * Vitorini Mods. It does not spawn/drive cars, query visibility, draw a HUD,
 * award cash/bounty, or save records. Those belong to the game adapter.
 * Evidence and deliberate native safety differences are documented in
 * docs/copchase-native.md. No original plugin bytes are embedded here. */
enum CopChaseStyle { COPCHASE_SLOW, COPCHASE_MOST_WANTED, COPCHASE_ULTIMATE, COPCHASE_INSANE };
enum CopChasePhase { COPCHASE_IDLE, COPCHASE_PURSUIT, COPCHASE_COOLDOWN };
enum CopChaseVisibility { COPCHASE_UNKNOWN, COPCHASE_OCCLUDED, COPCHASE_VISIBLE };
enum CopChaseEvent {
    COPCHASE_STARTED=1u, COPCHASE_LOST_SIGHT=2u, COPCHASE_SPOTTED=4u,
    COPCHASE_EVADED=8u, COPCHASE_BUSTED=16u, COPCHASE_CANCELLED=32u,
    COPCHASE_HEAT_CHANGED=64u
};
enum { COPCHASE_OBSERVATION_LIMIT=64 };
typedef struct {
    int enabled,style,start_heat,slow_max_heat,slow_max_chasers,mw_career_bin;
    int max_chasers,traffic_cars,chasers_scale,disable_busted,hiding_spots;
    float trigger_distance,trigger_speed,unseen_seconds,cooldown_percent;
} CopChaseConfig;
typedef struct {
    int usable,selected,arrest_hold;
    enum CopChaseVisibility visibility;
    float distance,vertical_distance;
} CopChaseObservation;
typedef struct {
    /* Stable host identities, not dereferenced guest addresses. Increment a
     * generation when the same world/player storage is recycled. */
    uint64_t world_generation,player_generation;
    int valid_world,valid_player,allowed_mode,driving,paused,complete_cop_scan;
    float seconds,speed;
    int hiding_spot,rammed_police;
    /* Optional already verified heat of this exact car. No persistence here. */
    float retained_heat;
    /* Incident bounty only; original time bounty is derived below. */
    uint32_t incident_bounty;
    const CopChaseObservation *cops;
    unsigned cop_count;
} CopChaseInput;
typedef struct {
    enum CopChasePhase phase;
    int heat,max_chasers,hiding;
    uint32_t events,bounty;
    float busted,unseen,cooldown,heat_fraction,retained_heat;
    double pursuit_seconds,chase_seconds;
} CopChaseOutput;
typedef struct {
    CopChaseOutput output;
    uint64_t world_generation,player_generation;
    int identity_valid;
    double world_seconds,grace_seconds,unseen_seconds,heat_value,cooldown_value,busted_value;
} CopChaseState;

CopChaseConfig copchase_config_defaults(void);
void copchase_config_validate(CopChaseConfig *config);
void copchase_state_init(CopChaseState *state);
/* Rejects invalid time instead of fast-forwarding. At most .25 simulated
 * seconds are consumed per call, split at .1 s like the original loop.
 * Invalid world/player/mode cancels before the pause gate. Unknown or
 * incomplete visibility cannot start a chase, arrest, reacquire or evade. */
CopChaseOutput copchase_state_step(CopChaseState *state,const CopChaseConfig *config,
                                  const CopChaseInput *input);
#ifdef __cplusplus
}
#endif
