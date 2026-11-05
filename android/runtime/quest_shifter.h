#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "quest_cockpit_vehicle.h"

/* Distances in fitted cabin meters. Slots always go left to right from the
 * driver's perspective, even when the console itself is on the left. Gear
 * IDs match the retail driver: reverse=0, neutral=1, first=2. */
enum { QUEST_SHIFT_NONE=-1, QUEST_SHIFT_R=0, QUEST_SHIFT_N=1, QUEST_SHIFT_FIRST=2 };
enum { QUEST_DRIVER_READY, QUEST_DRIVER_GRID_LOCK, QUEST_DRIVER_NO_INPUT };
static inline int quest_shifter_before_start(int stage) { return stage==2 || stage==3; }
#define QUEST_SHIFT_PITCH .045f
#define QUEST_SHIFT_THROW .060f
typedef struct {
    int hand, blocked[2], gear, lane, rebase, first_only, at_lever;
    float x,z,offset_x,offset_z,last_x,last_z;
    uint32_t squeeze_tick;
} QuestShifter;

static inline int quest_shifter_lane(int gear,int count)
{
    if(gear==QUEST_SHIFT_R)return count==6?3:2;
    if(gear==QUEST_SHIFT_N)return 1;
    return (gear-2)/2;
}
static inline void quest_shifter_detent(QuestShifter *s,int gear,int count)
{
    s->gear=gear;s->lane=quest_shifter_lane(gear,count);
    s->x=(s->lane-1)*QUEST_SHIFT_PITCH;
    s->z=gear==QUEST_SHIFT_N?0:
        (gear==QUEST_SHIFT_R || (gear&1)?QUEST_SHIFT_THROW:-QUEST_SHIFT_THROW);
}
static inline void quest_shifter_reset(QuestShifter *s)
{
    memset(s,0,sizeof *s);
    s->blocked[0]=s->blocked[1]=1;
    quest_shifter_detent(s,QUEST_SHIFT_N,5);
}
static inline void quest_shifter_knob(const QuestShifter *s,int vehicle,float p[3])
{
    /* Center of the upper grip on tripo_part_58. */
    const float center[3]={.312f,-.413f,-.205f};
    quest_cockpit_fit_point(p,center,quest_cockpit_vehicle(vehicle));
    p[0]+=s->x;p[2]+=s->z;
}

/* Return a request only on changing detents. Moving sideways is possible in
 * the neutral crossbar. Pulling diagonally against an engaged rail stays on
 * that rail until the hand returns to (or crosses) the middle. */
static inline int quest_shifter_move(QuestShifter *s,float x,float z,int count)
{
    const float middle=.016f,engage=.043f;
    int previous=s->gear;
    if(s->gear!=QUEST_SHIFT_N && (fabsf(z)<=middle || z*s->z<0))
        s->gear=QUEST_SHIFT_N;
    if(s->gear==QUEST_SHIFT_N) {
        int last=count==6?3:2;
        /* A partially inserted lever is already between the rail walls,
         * even before it has travelled far enough to engage the gear. */
        if(fabsf(s->z)>middle && fabsf(z)>middle && z*s->z>0)x=s->x;
        s->x=fmaxf(-QUEST_SHIFT_PITCH,fminf((last-1)*QUEST_SHIFT_PITCH,x));
        s->lane=(int)floorf(s->x/QUEST_SHIFT_PITCH+1.5f);
        float rail=(s->lane-1)*QUEST_SHIFT_PITCH;
        int target=z<0?2+s->lane*2:3+s->lane*2;
        if(z>0 && s->lane==last)target=QUEST_SHIFT_R;
        if(fabsf(s->x-rail)>.017f || target>count+1 || (s->first_only && target!=QUEST_SHIFT_FIRST)) {
            s->z=0;
            return previous==s->gear?QUEST_SHIFT_NONE:s->gear;
        }
        if(fabsf(z)>middle)s->x=rail;
        /* First clicks in at the forward stop. Allow half a millimetre
         * for controller sampling and fitted-coordinate roundoff. */
        if(fabsf(z)>=(target==QUEST_SHIFT_FIRST?QUEST_SHIFT_THROW-.0005f:engage))s->gear=target;
    }
    s->z=fmaxf(-QUEST_SHIFT_THROW,fminf(QUEST_SHIFT_THROW,z));
    return previous==s->gear?QUEST_SHIFT_NONE:s->gear;
}

static inline int quest_shifter_update(QuestShifter *s,uint32_t now,int vehicle,int count,int actual,
    const int valid[2],const float pos[2][3],const float squeeze[2],const int busy[2])
{
    s->at_lever=0;
    for(int h=0;h<2;++h) {
        if(isfinite(squeeze[h]) && squeeze[h]<.15f) {
            if(s->hand==h+1)s->hand=0;
            s->blocked[h]=0;
        }
    }
    if(!s->hand)quest_shifter_detent(s,actual,count);
    for(int h=0;h<2;++h) {
        int held=s->hand==h+1;
        if(!isfinite(squeeze[h])) {
            if(held) {
                s->rebase=1;
                if(now-s->squeeze_tick>=500) { s->hand=0;s->blocked[h]=1; }
            }
            continue;
        }
        if(held)s->squeeze_tick=now;
        if(!valid[h] || !isfinite(pos[h][0]) || !isfinite(pos[h][1]) || !isfinite(pos[h][2])) {
            if(held)s->rebase=1;
            continue;
        }
        float x=pos[h][0],z=pos[h][2];
        if(!held) {
            if(s->hand || busy[h] || s->blocked[h])continue;
            float knob[3];quest_shifter_knob(s,vehicle,knob);
            float dx=x-knob[0],dy=pos[h][1]-knob[1],dz=z-knob[2];
            if(!(dx*dx+dy*dy+dz*dz<=.065f*.065f))continue;
            s->at_lever=1;
            if(squeeze[h]<.65f)continue;
            s->hand=h+1;s->squeeze_tick=now;s->rebase=1;
        } else s->at_lever=1;
        if(s->rebase || fabsf(x-s->last_x)>.13f || fabsf(z-s->last_z)>.15f) {
            s->offset_x=s->x-x;s->offset_z=s->z-z;s->rebase=0;
        }
        s->last_x=x;s->last_z=z;
        return quest_shifter_move(s,x+s->offset_x,z+s->offset_z,count);
    }
    return QUEST_SHIFT_NONE;
}
