#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native reconstruction of CopChase v0.3.2 by Vitorini Mods. This portable
 * planner does not create vehicles, change physics, or access game memory.
 * Source evidence and the limits of this reconstruction are documented in
 * docs/copchase-native.md. Coordinates and velocities use metres and seconds. */
typedef struct { float position[2], velocity[2], forward[2], left[2]; } CopChasePose;
typedef struct {
    float lead, lateral, speed, aggression;
} CopChasePersonality;
typedef struct {
    int preset, heat, aggression_percent, role;
    int cross, formation, formation_slot, formation_side;
    float formation_pressure;
    float since_contact_ms;
    float catchup_speed, direct_range, maximum_speed, speed_bonus;
    /* The original MostWanted/Ultimate planner uses its smoothed speed
     * estimate when choosing the catch-up speed, and XY velocity for aiming. */
    float smoothed_speed;
    CopChasePersonality personality;
} CopChaseSteeringInput;
typedef struct { float target[2], speed; int use_road_path; } CopChaseSteeringOutput;
CopChaseSteeringInput copchase_steering_defaults(void);
/* The four random integers are the original CRT rand()%1000 samples.
 * Keeping random acquisition outside the planner makes replay deterministic. */
CopChasePersonality copchase_personality(const uint16_t samples[4]);
/* Returns zero, and clears output, for an invalid or non-finite snapshot. */
int copchase_steering(const CopChaseSteeringInput *input,const CopChasePose *cop,
                     const CopChasePose *player,CopChaseSteeringOutput *output);
#ifdef __cplusplus
}
#endif
