#pragma once
#include "copchase_steering.h"
#ifdef __cplusplus
extern "C" {
#endif
/* CopChase v0.3.2 by Vitorini Mods: pure commands reconstructed from the
 * PsychoDriver hook and its reverse/replan scheduler. No guest writes.
 * Physical-body velocity must be applied through a separately validated
 * platform adapter; car cached velocity is not the original write target. */
typedef struct {
    int preset,heat,aggression_percent;
    float acceleration,physics_distance,reverse_speed;
} CopChaseDriverConfig;
enum CopChasePhysicsCommand { COPCHASE_PHYSICS_KEEP, COPCHASE_PHYSICS_PROMOTE,
                              COPCHASE_PHYSICS_RESTORE_NATURAL };
typedef struct {
    uint32_t now_ms,last_selected_ms,last_relocated_ms;
    int selected,pursuit_active,physics_enabled,forced_physics,physics_mode,player_valid;
    float distance,forward,player_speed;
} CopChasePhysicsInput;
typedef struct {
    enum CopChasePhysicsCommand command;
    int forced_physics,clear_driver_state;
} CopChasePhysicsOutput;
typedef struct {
    uint32_t now_ms,reverse_until_ms,reverse_started_ms,contact_elapsed_ms;
    /* up_z is the car's orientation up axis Z, not health. Cop/player
     * velocity and heading use the same world XY axes as the planner. */
    float seconds,cop_vertical_velocity,up_z,original_road_speed;
    int physics_mode;
    CopChasePose cop,player;
    CopChaseSteeringOutput planned;
} CopChaseDriverInput;
typedef struct {
    CopChaseSteeringOutput control;
    /* Exact ASI physical-state delta. Despite the source name, this is
     * applied to the validated physical body, never the car velocity cache. */
    float velocity_delta[3];
    int apply_velocity,reverse_active,reverse_completed;
    uint32_t reverse_until_ms;
} CopChaseDriverOutput;
typedef struct {
    uint32_t reverse_until_ms,reverse_started_ms,stuck_since_ms,last_replan_ms;
    unsigned reverse_attempts;
} CopChaseRecoveryState;
typedef struct {
    uint32_t now_ms,last_relocated_ms;
    /* distance is XYZ; target_alignment is XY target/heading dot divided
     * by distance+0.01. held is the host's upstream exclusion gate. */
    float distance,cop_speed,player_speed,target_alignment;
    int physics_mode,reverse_enabled,arrest_phase,held,special_motion;
    int backup_enabled,lost_contact;
} CopChaseRecoveryInput;
typedef struct {
    int reverse_started,replan,relocation_requested;
} CopChaseRecoveryOutput;
CopChaseDriverConfig copchase_driver_defaults(void);
/* These helpers return0 and clear output for invalid input. State stays
 * unchanged on failure. Physics decisions precede the steering/assist step. */
int copchase_driver_physics(const CopChaseDriverConfig *,const CopChasePhysicsInput *,CopChasePhysicsOutput *);
int copchase_driver_assist(const CopChaseDriverConfig *,const CopChaseDriverInput *,CopChaseDriverOutput *);
void copchase_recovery_init(CopChaseRecoveryState *);
/* Operates only on an already selected, validated chaser. The host performs
 * native replan commands and decides whether a safe relocation is possible.
 * A relocation request never changes position, timers or identity here. */
int copchase_driver_recovery(CopChaseRecoveryState *,const CopChaseDriverConfig *,
                            const CopChaseRecoveryInput *,CopChaseRecoveryOutput *);
#ifdef __cplusplus
}
#endif
