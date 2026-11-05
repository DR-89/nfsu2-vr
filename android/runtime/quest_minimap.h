#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "quest_cockpit_vehicle.h"

/* A rigid disc in cabin meters. Fit the mount and its tilt to the dash;
 * the face itself keeps its proportions and reads left to right in every car. */
typedef struct { float center[3],up_y,up_z; } QuestMinimapMount;
typedef struct {
    int hand,armed[2],hover[2];
    float anchor[3],start[3];
} QuestMinimapGrip;

/* Bits follow the hands: left X, right A. Keep a captured press until
 * physical release, even after the world map takes over the buttons. */
enum { QUEST_MAP_X=1, QUEST_MAP_A=2 };
typedef struct {
    unsigned armed,captured;
    int pulsing;
    uint32_t since;
} QuestMinimapShortcut;

static inline void quest_minimap_mask_buttons(unsigned captured,uint8_t analog[8])
{
    if(captured&QUEST_MAP_X)analog[2]=0;
    if(captured&QUEST_MAP_A)analog[0]=0;
}

static inline int quest_minimap_shortcut(QuestMinimapShortcut *s,uint32_t now,int available,
    const QuestMinimapGrip *grip,unsigned valid,unsigned down)
{
    unsigned fresh=s->armed & valid & down;
    unsigned hover=(grip->hover[0]?QUEST_MAP_X:0)|(grip->hover[1]?QUEST_MAP_A:0);
    s->armed=valid & ~down;
    s->captured &= ~s->armed;
    if(!available)s->pulsing=0;
    else if(grip->hand) {
        /* Grip wins if both inputs arrive together. */
        s->captured |= fresh & (1u<<(grip->hand-1));
    } else if(fresh & hover) {
        s->captured |= fresh & hover;
        s->pulsing=1;s->since=now;
    }
    if(now-s->since>=100)s->pulsing=0;
    return s->pulsing;
}

#define QUEST_MINIMAP_LIMIT 2.f
#define QUEST_MINIMAP_RADIUS .075f

static inline QuestMinimapMount quest_minimap_mount(const QuestCockpitVehicle *fit)
{
    QuestMinimapMount mount;
    const float center[3]={.3116f,-.155f,-.463f};
    quest_cockpit_fit_point(mount.center,center,fit);
    float slope=-.40f*fit->front_depth;
    mount.up_y=1.f/sqrtf(1.f+slope*slope);
    mount.up_z=slope*mount.up_y;
    return mount;
}

static inline int quest_minimap_near(const QuestMinimapMount *mount,const float offset[3],const float hand[3])
{
    float x=hand[0]-offset[0]-mount->center[0];
    float y=hand[1]-offset[1]-mount->center[1],z=hand[2]-offset[2]-mount->center[2];
    float up=y*mount->up_y+z*mount->up_z,depth=z*mount->up_y-y*mount->up_z;
    /* The grip origin lies in the palm. Allow reach in depth and a small
     * radial tolerance at the rim without giving the disc square corners. */
    return fabsf(depth)<=.055f && x*x+up*up<=(QUEST_MINIMAP_RADIUS+.012f)*(QUEST_MINIMAP_RADIUS+.012f);
}

/* Persist once when a changed placement ends, including interruptions. */
static inline int quest_minimap_release(QuestMinimapGrip *g,const float offset[3])
{
    int changed=g->hand && memcmp(g->start,offset,sizeof g->start)!=0;
    *g=(QuestMinimapGrip){0};
    return changed;
}

static inline int quest_minimap_update(QuestMinimapGrip *g,const QuestMinimapMount *mount,
    float offset[3],int active,const int valid[2],const float position[2][3],
    const float squeeze[2],const int busy[2])
{
    if(!active)return quest_minimap_release(g,offset);
    int save=0;
    if(g->hand) {
        int h=g->hand-1;
        int tracked=valid[h] && !busy[h] && isfinite(squeeze[h]);
        if(tracked) {
            for(int k=0;k<3;++k) {
                float delta=position[h][k]-g->anchor[k];
                offset[k]=fmaxf(-QUEST_MINIMAP_LIMIT,fminf(QUEST_MINIMAP_LIMIT,g->start[k]+delta));
            }
        }
        if(!tracked || squeeze[h]<.15f)save=quest_minimap_release(g,offset);
    }
    for(int h=0;h<2;++h) {
        g->hover[h]=0;
        if(!valid[h] || busy[h] || !isfinite(squeeze[h]))g->armed[h]=0;
        else {
            if(squeeze[h]<.15f)g->armed[h]=1;
            g->hover[h]=!g->hand && g->armed[h] && quest_minimap_near(mount,offset,position[h]);
            if(squeeze[h]<.65f)continue;
            if(g->hover[h]) {
                g->hand=h+1;
                memcpy(g->anchor,position[h],sizeof g->anchor);
                memcpy(g->start,offset,sizeof g->start);
            }
            g->armed[h]=0;
        }
    }
    if(g->hand)g->hover[0]=g->hover[1]=0;
    return save;
}
