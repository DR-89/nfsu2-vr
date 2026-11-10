#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Portable original CopChase v0.3.2 collision accounting. Input events must
 * come from verified native ApplyImpulse, not inferred visual proximity.
 * All functions are pure bounded state operations, no guest or filesystem IO. */
enum CopChaseInfraction { COPCHASE_SPEEDING=0,COPCHASE_RECKLESS=1,COPCHASE_ASSAULT=2,
    COPCHASE_HIT_AND_RUN=3,COPCHASE_RESISTING=4 };
typedef struct {
    int cop_damage;
    float damage_threshold,damage_scale;
    uint32_t hit_bounty,taken_out_bounty,cost_per_hit,cost_per_taken_out;
} CopChaseIncidentConfig;
typedef struct {
    uint64_t identity;
    float health;
    uint32_t last_assault_ms;
    int damaged,wrecked,rollover_counted;
} CopChaseIncidentCop;
typedef struct {
    CopChaseIncidentCop cops[64];
    uint32_t infractions,infraction_mask,cost_to_state,cops_damaged,cops_taken_out,incident_bounty,assaults;
    float busted_peak;
} CopChaseIncidents;
typedef struct { int slot,usable,excluded;float position[3]; } CopChaseWitness;
typedef struct {
    uint32_t now_ms,event_ms;
    int valid,paused,pursuit,player_valid,target_is_player,target_is_traffic,target_slot,cross_slot,pending_trigger;
    uint64_t target_identity;
    float strength,player_position[3],player_velocity[2],target_position[3];
    const CopChaseWitness *witnesses;
    unsigned witness_count;
} CopChaseCollision;
typedef struct {
    int accepted,trigger_slot,trigger_reason,damaged_slot,taken_out_slot;
    uint32_t new_infractions,bounty_added,cost_added;
    float health;
} CopChaseIncidentOutput;
CopChaseIncidentConfig copchase_incident_defaults(void);
void copchase_incidents_init(CopChaseIncidents*);
/* On pursuit START: resets results/distinct hit flags, preserves per-car
 * health/wreck and assault cooldown like the original world registry. */
void copchase_incidents_begin(CopChaseIncidents*);
/* Must bind a changed identity before accepting events for a reused slot. */
int copchase_incidents_bind(CopChaseIncidents*,unsigned slot,uint64_t identity);
CopChaseIncidentOutput copchase_incidents_collision(CopChaseIncidents*,const CopChaseIncidentConfig*,const CopChaseCollision*);
uint32_t copchase_incidents_tick(CopChaseIncidents*,int pursuit,int paused,float speed,float busted);
/* Original main-loop extra score for a selected cop rolling over (upZ<.25),
 * independent of scalar hit-health. Not an invented native health field. */
CopChaseIncidentOutput copchase_incidents_rollover(CopChaseIncidents*,const CopChaseIncidentConfig*,
    unsigned slot,uint64_t identity,int pursuit,int selected,int paused,float up_z);
/* Original hiding test uses XY only, strict distance<radius, max64 previously
 * filtered world markers of types6..18; caller supplies verified positions.
 * Speed<3 gate and cooldown multiplier remain in the state core. */
int copchase_incidents_hiding(const float player_xy[2],const float (*markers_xy)[2],unsigned count,float radius,float *nearest);
#ifdef __cplusplus
}
#endif
