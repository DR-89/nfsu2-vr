#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Xbox retail adapter for the reconstructed CopChase road census. All memory
 * access is supplied by the host. Call only on the simulation thread. */
typedef struct CopChaseRoads CopChaseRoads;
typedef struct {
    uint32_t car,driver,control,type,mover,facade,body;
    uint32_t world_generation,identity_generation;
    unsigned slot;
} CopChaseRoadIdentity;
typedef struct { uint32_t address,size; unsigned char bytes[12]; } CopChaseRoadWrite;
typedef struct {
    void *context;
    int (*read)(void *,uint32_t,void *,uint32_t);
    /* Must prove current world/index membership, generation and original car
     * identity; traffic!=0 additionally excludes player/racers/remote clones. */
    int (*identity)(void *,const CopChaseRoadIdentity *,int traffic);
    /* Optional. Must validate ALL ranges before applying ANY write, under the
     * same simulation-thread ownership; return 0 without writes on failure. */
    int (*write_batch)(void *,const CopChaseRoadWrite *,unsigned);
} CopChaseRoadHost;
typedef struct {
    CopChaseRoadIdentity identity;
    int selected,disabled,held;
} CopChaseRoadCar;
typedef struct {
    uint32_t key;
    float position[3],direction[2];
    uint16_t samples,reserved;
    uint32_t last_ms;
} CopChaseRoadCell;
typedef struct {
    unsigned valid_cars,rejected_cars,path_segments,samples,movement_samples,cells;
    int updated;
} CopChaseRoadCensus;
typedef struct {
    float position[3],forward[2],velocity[2],speed;
    int debug;
} CopChaseRoadQuery;
typedef struct {
    float centre[3],forward[2],travel[2],lane_min,lane_max,walked;
    unsigned snaps,width_samples;
} CopChaseRoadSite;
CopChaseRoads *copchase_roads_create(void);
void copchase_roads_destroy(CopChaseRoads *);
void copchase_roads_reset(CopChaseRoads *,uint32_t world_generation);
/* At most 64 distinct slots, count<=20 native path points; runs every250ms. */
int copchase_roads_census(CopChaseRoads *,const CopChaseRoadHost *,uint32_t world_generation,
                         uint32_t now_ms,const CopChaseRoadCar *,unsigned,CopChaseRoadCensus *);
/* Samples must come from verified native paths or observed traffic movement.
 * This entry also permits deterministic replay fixtures without game memory. */
int copchase_roads_observe(CopChaseRoads *,const float position[3],const float direction[2],uint32_t now_ms);
int copchase_roads_cell(const CopChaseRoads *,int grid_x,int grid_y,CopChaseRoadCell *);
int copchase_roads_query(const CopChaseRoads *,const CopChaseRoadQuery *,CopChaseRoadSite *);
typedef struct { float local[4][3],world[4][3]; } CopChaseRoadWheels;
/* Original descriptor wheel order and +0x120 + i*0x30 local XYZ. */
int copchase_roads_wheels(const CopChaseRoadHost *,const CopChaseRoadIdentity *,CopChaseRoadWheels *);
typedef struct { unsigned count; CopChaseRoadWrite writes[13]; } CopChaseRoadStage;
/* Root promotes to physics1 first, then reacquires identity. No native calls,
 * spawn, index edits or cached pointer ownership are hidden in this function.
 * Plan must be applied immediately; apply revalidates identity and rebuilds it.
 * Call once per render frame for the original eight-frame settling interval. */
int copchase_roads_stage_plan(const CopChaseRoadHost *,const CopChaseRoadIdentity *,
                             const float position[3],const float heading[2],CopChaseRoadStage *);
int copchase_roads_stage(const CopChaseRoadHost *,const CopChaseRoadIdentity *,
                        const float position[3],const float heading[2]);
#ifdef __cplusplus
}
#endif
