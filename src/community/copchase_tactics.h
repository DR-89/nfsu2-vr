#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Portable reconstruction of CopChase v0.3.2 by Vitorini Mods. Only roles
 * and formation scheduling: no guest pointers, car selection or movement.
 * Call on the ordered selected-chaser list, at the pursuit scheduler cadence.
 * Reset on pursuit/world/player changes; preserve across paused frames by
 * supplying a simulation clock that does not advance while paused. */
enum { COPCHASE_TACTICS_SLOTS=64, COPCHASE_TACTICS_CHASERS=16 };
typedef struct {
    unsigned slot;
    /* Distance is XYZ; forward/lateral/heading dot use player XY axes. */
    float distance,forward,heading_dot,lateral,personality_aggression;
    int preferred_side,force_direct;
} CopChaseTacticsCop;
typedef struct {
    int preset,heat,aggression_percent;
    /* -1 uses the shipped preset: enabled except Slow, heat boost4 only
     * for Insane. Formation profiles do not add Ultimate's steering boost. */
    int formations_enabled,formation_heat_boost;
    float duration_multiplier,gap_multiplier;
} CopChaseTacticsConfig;
typedef struct {
    uint32_t now_ms;
    float speed;
    const CopChaseTacticsCop *cops;
    unsigned count;
    /* The original CRT rand()%1000 sample. Consumed only if a new formation
     * starts; acquire randomness outside this deterministic helper. */
    uint16_t formation_sample;
} CopChaseTacticsInput;
typedef struct {
    int formation,side;
    uint32_t ends_ms,next_ms;
    int8_t member[COPCHASE_TACTICS_SLOTS];
} CopChaseTacticsState;
typedef struct {
    uint8_t role[COPCHASE_TACTICS_SLOTS];
    int formation,side;
    int8_t member[COPCHASE_TACTICS_SLOTS];
    float pressure;
    int started,ended,sample_used;
} CopChaseTacticsOutput;
CopChaseTacticsConfig copchase_tactics_defaults(void);
void copchase_tactics_init(CopChaseTacticsState *state);
/* Fifth original personality draw controls the preferred side. With varied
 * personalities disabled, the original uses registry-slot parity instead. */
int copchase_tactics_preferred_side(unsigned slot,int varied,uint32_t sample);
/* Invalid snapshots return0, clear output and leave state unchanged. */
int copchase_tactics_step(CopChaseTacticsState *state,const CopChaseTacticsConfig *config,
                         const CopChaseTacticsInput *input,CopChaseTacticsOutput *output);
#ifdef __cplusplus
}
#endif
