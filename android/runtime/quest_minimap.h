#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "quest_cockpit_vehicle.h"

/* A rigid disc in cabin meters. Fit the mount and its tilt to the dash;
 * the face itself keeps its proportions and reads left to right in every car. */
typedef struct { float center[3],up_y,up_z; } QuestMinimapMount;
typedef struct { float offset[3],pitch,yaw; } QuestMinimapPlacement;
typedef struct {
    int hand,armed[2],hover[2];
    QuestMinimapPlacement start;
    float wrist_forward[3],contact[3];
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
#define QUEST_MINIMAP_ANGLE_LIMIT .78539816f /* 45 degrees from the fitted mount */

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

/* Column-major map-local to cabin transform, shared by drawing and grabbing. */
static inline void quest_minimap_matrix(float m[16],const QuestMinimapMount *mount,const QuestMinimapPlacement *p)
{
    float cp=cosf(p->pitch),sp=sinf(p->pitch),cy=cosf(p->yaw),sy=sinf(p->yaw);
    float up=mount->up_y*cp-mount->up_z*sp,tilt=mount->up_z*cp+mount->up_y*sp;
    const float matrix[16]={cy,0,-sy,0, sy*tilt,up,cy*tilt,0, sy*up,-tilt,cy*up,0,
        mount->center[0]+p->offset[0],mount->center[1]+p->offset[1],mount->center[2]+p->offset[2],1};
    memcpy(m,matrix,sizeof matrix);
}

static inline int quest_minimap_contact(const float m[16],const float hand[3],float local[3])
{
    for(int k=0;k<3;++k)
        local[k]=m[4*k]*(hand[0]-m[12])+m[4*k+1]*(hand[1]-m[13])+m[4*k+2]*(hand[2]-m[14]);
    /* The grip origin lies in the palm. Allow reach in depth and a small
     * radial tolerance at the rim without giving the disc square corners. */
    return fabsf(local[2])<=.055f && hypotf(local[0],local[1])<=QUEST_MINIMAP_RADIUS+.012f;
}

/* Persist once when a changed placement ends, including interruptions. */
static inline int quest_minimap_release(QuestMinimapGrip *g,const QuestMinimapPlacement *p)
{
    int changed=g->hand && (fabsf(p->pitch-g->start.pitch)>.00001f || fabsf(p->yaw-g->start.yaw)>.00001f);
    for(int k=0;g->hand && k<3;++k)changed|=fabsf(p->offset[k]-g->start.offset[k])>.00001f;
    *g=(QuestMinimapGrip){0};
    return changed;
}

static inline int quest_minimap_update(QuestMinimapGrip *g,const QuestMinimapMount *mount,
    QuestMinimapPlacement *p,int active,const int valid[2],const float pose[2][16],
    const float squeeze[2],const int busy[2])
{
    if(!active)return quest_minimap_release(g,p);
    float m[16];quest_minimap_matrix(m,mount,p);
    int save=0;
    if(g->hand) {
        int h=g->hand-1;
        int tracked=valid[h] && !busy[h] && isfinite(squeeze[h]);
        if(tracked) {
            /* Rotate the cabin's original +Z by the change in wrist pose.
             * Both hands have orthonormal axes, including the mirrored glove. */
            float forward[3];
            for(int k=0;k<3;++k)
                forward[k]=pose[h][k]*g->wrist_forward[0]+pose[h][4+k]*g->wrist_forward[1]+pose[h][8+k]*g->wrist_forward[2];
            float pitch=g->start.pitch+atan2f(-forward[1],hypotf(forward[0],forward[2]));
            float yaw=g->start.yaw+atan2f(forward[0],forward[2]);
            p->pitch=fmaxf(-QUEST_MINIMAP_ANGLE_LIMIT,fminf(QUEST_MINIMAP_ANGLE_LIMIT,pitch));
            p->yaw=fmaxf(-QUEST_MINIMAP_ANGLE_LIMIT,fminf(QUEST_MINIMAP_ANGLE_LIMIT,yaw));
            quest_minimap_matrix(m,mount,p);
            for(int k=0;k<3;++k) {
                float arm=m[k]*g->contact[0]+m[4+k]*g->contact[1]+m[8+k]*g->contact[2];
                float offset=pose[h][12+k]-mount->center[k]-arm;
                p->offset[k]=fmaxf(-QUEST_MINIMAP_LIMIT,fminf(QUEST_MINIMAP_LIMIT,offset));
                m[12+k]=mount->center[k]+p->offset[k];
            }
        }
        if(!tracked || squeeze[h]<.15f)save=quest_minimap_release(g,p);
    }
    for(int h=0;h<2;++h) {
        g->hover[h]=0;
        if(!valid[h] || busy[h] || !isfinite(squeeze[h]))g->armed[h]=0;
        else {
            if(squeeze[h]<.15f)g->armed[h]=1;
            float local[3];
            g->hover[h]=!g->hand && g->armed[h] && quest_minimap_contact(m,pose[h]+12,local);
            if(squeeze[h]<.65f)continue;
            if(g->hover[h]) {
                g->hand=h+1;
                for(int k=0;k<3;++k)g->wrist_forward[k]=pose[h][4*k+2];
                memcpy(g->contact,local,sizeof local);
                g->start=*p;
            }
            g->armed[h]=0;
        }
    }
    if(g->hand)g->hover[0]=g->hover[1]=0;
    return save;
}
