#pragma once
#include "copchase_state.h"
#include "copchase_roads.h"
#include "copchase_spikes.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native offline adapter for CopChase v0.3.2 by Vitorini Mods. This does not
 * load the Windows plugin. The host must serialize frame/driver/heading/
 * world_ending/destroy on the guest main thread. Configure/snapshot may run
 * on another thread. No callback may call these APIs recursively.
 *
 * Guards must report CURRENT state even inside driver/heading hooks. World
 * and player generations must change before storage is destroyed/recycled;
 * addresses alone cannot identify a reincarnated guest object. Call
 * world_ending BEFORE World destruction. The adapter never spawns cars. */
typedef struct CopChaseGame CopChaseGame;
typedef struct {
    uint64_t world_generation,player_generation;
    int assets_ready,offline,races_inactive,fleet_retired,paused,streaming_idle;
    /* Live durability/terminal hold. Keep pursuit.enabled unchanged: preserve
     * results and ownership while freezing timers, captures and AI overrides.
     * World/player/context loss still performs normal cancellation. */
    int settlement_pending;
    /* Only a freshly verified, explicitly armed offline Pursuit Sprint may
     * supply these. A nonzero token binds the exact race/world/player; token
     * replacement is a cancellation boundary even if addresses are reused. */
    uint64_t pursuit_race_token;
    uint32_t pursuit_race_world,pursuit_race_player,pursuit_race_manager,pursuit_race_track,pursuit_race_kind;
    int pursuit_race_start_heat,pursuit_race_style,pursuit_race_force_start;
} CopChaseGameGuards;
typedef struct {
    void *context;
    int (*read)(void*,uint32_t address,void *out,uint32_t size);
    int (*write)(void*,uint32_t address,const void *data,uint32_t size);
    /* Success is separate from EAX (zero is a valid native return value).
     * Preserve full guest register/FPU/SIMD/TLS state around every call.
     * 1e47d0 is cdecl, self=0; all other allowed calls are thiscall. */
    int (*call)(void*,uint32_t function,uint32_t self,unsigned count,
                const uint32_t *arguments,uint32_t *eax);
    /* Private 16-byte-aligned guest scratch, independent of damage toggle. */
    uint32_t (*alloc)(void*,uint32_t size);
    void (*free)(void*,uint32_t address);
    int (*guards)(void*,CopChaseGameGuards*);
    /* Optional transaction: road staging is13x12bytes; model reference
     * accounting is up to6x4bytes. Validate every range before any write;
     * callback and caller run on the same guest thread. No partial writes. */
    int (*write_batch)(void*,const CopChaseRoadWrite*,unsigned count);
} CopChaseGameHost;
typedef struct {
    CopChaseConfig pursuit;
    int maximum_vehicles,aggression_percent;
    float catchup_speed,direct_range,maximum_speed,speed_bonus;
    /* Verified current profile/model record; zero means no retained value. */
    float retained_heat;
    uint32_t random_seed;
    int roadblocks_enabled,spikes_enabled,heavy_roadblocks;
    /* Host sets only after world/depth strip rendering is ready. Default0. */
    int spikes_visible;
    float roadblock_chance_multiplier;
    /* Rebalance already-loaded police models outside the player's near view.
     * Uses native SetRide and traffic references; never creates a car. */
    int heat_models;
} CopChaseGameConfig;
enum CopChaseGameBlock {
    COPCHASE_GAME_READY,COPCHASE_GAME_DISABLED,COPCHASE_GAME_ASSETS,
    COPCHASE_GAME_CONTEXT,COPCHASE_GAME_IDENTITY,COPCHASE_GAME_PAUSED,
    COPCHASE_GAME_STREAMING,COPCHASE_GAME_TIME,COPCHASE_GAME_FAULT,
    COPCHASE_GAME_SETTLEMENT
};
enum CopChaseGameRoadEvent {
    COPCHASE_ROADBLOCK_DEPLOYED=1u,COPCHASE_ROADBLOCK_RELEASED=2u,
    COPCHASE_SPIKES_APPROACHING=4u,COPCHASE_TIRES_SPIKED=8u,
    COPCHASE_SPIKES_REMOVED=16u
};
enum CopChaseGameIncidentEvent {
    COPCHASE_INCIDENT_SPEEDING=1u,COPCHASE_INCIDENT_RECKLESS=2u,
    COPCHASE_INCIDENT_ASSAULT=4u,COPCHASE_INCIDENT_HIT_AND_RUN=8u,
    COPCHASE_INCIDENT_RESISTING=16u,COPCHASE_INCIDENT_DAMAGED=256u,
    COPCHASE_INCIDENT_TAKEN_OUT=512u,COPCHASE_INCIDENT_ROLLOVER=1024u,
    COPCHASE_INCIDENT_RAM_TRIGGER=2048u
};
enum CopChaseGameMissing {
    COPCHASE_GAME_NO_BUST_PRESENTATION=1u,COPCHASE_GAME_NO_CASH=2u,
    COPCHASE_GAME_NO_PERSISTENCE=4u,COPCHASE_GAME_NO_HUD=8u,
    COPCHASE_GAME_NO_AUDIO=16u,COPCHASE_GAME_NO_ROADBLOCKS=32u,
    COPCHASE_GAME_NO_SPIKES=64u,COPCHASE_GAME_NO_FORMATION_SCHEDULER=128u,
    COPCHASE_GAME_NO_RECOVERY_BOOST=256u,COPCHASE_GAME_NO_HIDING_MARKERS=512u,
    COPCHASE_GAME_NO_RAM_TRIGGER=1024u
};
typedef struct {
    CopChaseOutput pursuit;
    enum CopChaseGameBlock blocked;
    uint32_t pending_events,missing_features;
    unsigned registered_cars,active_cars,visible_cars,unknown_cars;
    uint64_t activations,releases,abandoned,failures,los_queries,contacts;
    uint64_t driver_overrides,heading_overrides;
    uint64_t impulses,reversals,replans;
    float filtered_speed;
    int release_pending;
    uint32_t pending_road_events;
    unsigned road_cells,held_cars,staging_cars,flat_tyre_mask;
    uint64_t roadblocks_deployed,roadblocks_released,road_stage_frames,spike_hits,spike_impulses;
    int roadblock_active,tyre_scrape;
    CopChaseSpikeStrip spike_strip;
    uint32_t pending_incident_events,infractions,infraction_mask,cops_damaged,cops_taken_out,cost_to_state,incident_bounty;
    uint64_t collision_events,collision_dropped;
    unsigned hiding_markers;
    int hiding_known;
    uint64_t model_reassignments;
} CopChaseGameStatus;
CopChaseGameConfig copchase_game_defaults(void); /* disabled by default */
CopChaseGame *copchase_game_create(const CopChaseGameHost *host);
void copchase_game_configure(CopChaseGame*,const CopChaseGameConfig*);
/* Call once per main-thread safe frame boundary, outside AI Update. Invalid
 * dt freezes. Pause/streaming freezes timers and overrides, not identity
 * checks. Context loss releases owned live cars when streaming is safe. */
void copchase_game_frame(CopChaseGame*,float simulation_seconds);
/* Call before generated native PsychoDriver::Update / heading helper.
 * Return 1 only when this adapter wrote an override. Both preserve native
 * delegation; neither calls guest functions or releases AI behaviors. */
int copchase_game_driver(CopChaseGame*,uint32_t control);
int copchase_game_heading(CopChaseGame*,uint32_t control);
/* Before native001AB4C0 scales the impulse. collision_body is its ECX
 * CarCollisionBody facade, not the rigid body at facade+68. Read-only guest
 * validation and bounded event capture; no native calls or guest writes. */
int copchase_game_collision(CopChaseGame*,uint32_t collision_body,const float raw_impulse[3]);
void copchase_game_snapshot(CopChaseGame*,CopChaseGameStatus*,int take_events);
/* No guest reads/writes/calls: invalidate before world storage is freed. */
void copchase_game_world_ending(CopChaseGame*);
/* Before Car destructor, even if allocator will reuse identical car/driver/
 * control addresses. Invalidates only that identity; no guest IO. A player
 * car ending suppresses hooks until the host advances player_generation. */
void copchase_game_car_ending(CopChaseGame*,uint32_t car);
/* Host must first disable + frame until active_cars==0, or call world_ending
 * before destroying the world. Destroy frees only owned guest scratch. */
void copchase_game_destroy(CopChaseGame*);
#ifdef __cplusplus
}
#endif
