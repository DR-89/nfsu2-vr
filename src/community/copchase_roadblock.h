#pragma once
#include "copchase_spikes.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    unsigned id;
    float position[2];
    int eligible,selected,disabled,model_pending,held;
} CopChaseRoadblockCandidate;
typedef struct {
    int heat,spikes_enabled,heavy,debug;
    float player_position[2],travel_direction[2];
    /* The adapter must supply a verified road sample and measured lateral
     * lane centres. This helper does not invent a road or move any cars. */
    float centre[3],forward[2],lane_min,lane_max;
    unsigned candidate_count;
    CopChaseRoadblockCandidate candidates[64];
    uint32_t random[12];
} CopChaseRoadblockPlanInput;
typedef struct {
    unsigned id;
    float position[3],heading[2];
    int release_chaser;
} CopChaseRoadblockCar;
typedef struct {
    unsigned count,across_count,random_used;
    float centre[3],forward[2],width,angle_radians;
    CopChaseRoadblockCar cars[4];
    CopChaseSpikeStrip strip;
} CopChaseRoadblockPlan;
/* Deterministic reconstruction of original10023800 AFTER road sampling.
 * Returns0 for invalid input or fewer than two eligible cars; output is
 * cleared on failure. Random samples are consumed in original order. */
int copchase_roadblock_plan(const CopChaseRoadblockPlanInput *,CopChaseRoadblockPlan *);
typedef struct {
    uint32_t pursuit_epoch,next_roll_ms,next_attempt_ms;
    int roll_succeeded;
} CopChaseRoadblockSchedule;
typedef struct {
    uint32_t now_ms,pursuit_epoch;
    int preset,heat,active,enabled,block_active,strip_active,arrest_phase;
    float speed,chance_multiplier,insane_roll_ms;
    uint32_t random[3];
} CopChaseRoadblockScheduleInput;
typedef struct { int request;unsigned random_used; } CopChaseRoadblockScheduleOutput;
void copchase_roadblock_schedule_init(CopChaseRoadblockSchedule *);
int copchase_roadblock_schedule(CopChaseRoadblockSchedule *,const CopChaseRoadblockScheduleInput *,CopChaseRoadblockScheduleOutput *);
void copchase_roadblock_attempted(CopChaseRoadblockSchedule *,uint32_t now_ms,int succeeded);
enum CopChaseRoadblockRelease {
    COPCHASE_BLOCK_KEEP,COPCHASE_BLOCK_PURSUIT_OVER,COPCHASE_BLOCK_PASSED,
    COPCHASE_BLOCK_STOPPED,COPCHASE_BLOCK_TIMEOUT,COPCHASE_BLOCK_RAMMED
};
typedef struct {
    int active;
    uint32_t created_ms,stopped_ms;
    float centre[3],forward[2];
} CopChaseRoadblockLifecycle;
typedef struct { uint32_t now_ms;int pursuit_active,recent_ram;float player_position[2],speed; } CopChaseRoadblockLifecycleInput;
/* Release only changes this portable state. Original clears each car's hold
 * and staging counters; host restores its appropriate lifecycle policy and
 * sets scheduler.next_attempt_ms=now+20000. */
int copchase_roadblock_lifecycle(CopChaseRoadblockLifecycle *,const CopChaseRoadblockLifecycleInput *,enum CopChaseRoadblockRelease *);
#ifdef __cplusplus
}
#endif
