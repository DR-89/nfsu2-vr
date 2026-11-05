#pragma once
#include <stdint.h>
#include <stdlib.h>

/* The retail recorder stores time in one byte and records pad events before
 * the cockpit applies its physical gear request. Keep those missing inputs
 * alongside the current in-memory race, indexed by the retail frame number
 * so rewinding and jumping to a lap select the same inputs again. */
typedef struct {
    uint32_t ticks, gear_before, gear_after;
    unsigned char human_driver, raw_pedals;
} VrReplayFrame;

typedef struct {
    VrReplayFrame *frames;
    size_t count, capacity;
    uint32_t manager, stream, first, start_time;
} VrReplay;

static inline int vr_replay_matches(const VrReplay *r,uint32_t manager,uint32_t stream,
                                    uint32_t first,uint32_t start_time)
{
    return r->manager==manager && r->stream==stream && r->first==first && r->start_time==start_time;
}

static inline VrReplayFrame *vr_replay_record(VrReplay *r,uint32_t manager,uint32_t stream,
    uint32_t first,uint32_t start_time,uint32_t frame,uint32_t ticks)
{
    if(frame==first || !vr_replay_matches(r,manager,stream,first,start_time)) {
        r->manager=manager;r->stream=stream;r->first=first;r->start_time=start_time;r->count=0;
    }
    if(frame<first)return NULL;
    size_t i=frame-first;
    /* Every recorded step consumes at least 12 bits for channel 2 in the
     * 256 KiB retail stream. Bound allocation even on invalid guest input. */
    if(i>r->count || i>=262144)return NULL;
    if(i==r->capacity) {
        size_t capacity=r->capacity?r->capacity*2:256;
        VrReplayFrame *frames=(VrReplayFrame *)realloc(r->frames,capacity*sizeof *frames);
        if(!frames)return NULL;
        r->capacity=capacity;r->frames=frames;
    }
    r->count=i+1;
    r->frames[i]=(VrReplayFrame){.ticks=ticks};
    return &r->frames[i];
}

static inline VrReplayFrame *vr_replay_find(VrReplay *r,uint32_t manager,uint32_t stream,
    uint32_t first,uint32_t start_time,uint32_t frame)
{
    if(!vr_replay_matches(r,manager,stream,first,start_time) ||
       frame<first || frame-first>=r->count)return NULL;
    return &r->frames[frame-first];
}
