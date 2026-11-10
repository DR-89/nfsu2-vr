#pragma once
#include "quest_minimap.h"
#include "quest_radio.h"

typedef struct { QuestMinimapPlacement placement;float scale,opacity;int movable,page; } QuestScreenSettings;
typedef struct { QuestMinimapGrip grip;float start_scale,start_opacity;uint32_t tick; } QuestScreenGrip;
static inline QuestMinimapMount quest_screen_mount(const QuestCockpitVehicle *fit)
{
    QuestMinimapMount m=quest_minimap_mount(fit);
    quest_radio_screen_point(m.center,fit,.5f,.5f);return m;
}
static inline void quest_screen_point(float p[3],const float matrix[16],float width,float u,float v)
{
    float x=(u-.5f)*width,y=(.5f-v)*width*9.f/16.f;
    for(int k=0;k<3;++k)p[k]=matrix[12+k]+matrix[k]*x+matrix[4+k]*y;
}
static inline int quest_screen_contact(const float m[16],float width,const float p[3],float local[3])
{
    for(int k=0;k<3;++k)local[k]=m[4*k]*(p[0]-m[12])+m[4*k+1]*(p[1]-m[13])+m[4*k+2]*(p[2]-m[14]);
    return fabsf(local[2])<=.055f && fabsf(local[0])<=width*.5f+.012f && fabsf(local[1])<=width*9.f/32.f+.012f;
}
static inline int quest_screen_release(QuestScreenGrip *s,const QuestScreenSettings *p)
{
    int changed=s->grip.hand && (fabsf(s->start_scale-p->scale)>.00001f ||
                                fabsf(s->start_opacity-p->opacity)>.00001f);
    changed|=quest_minimap_release(&s->grip,&p->placement);return changed;
}
static inline int quest_screen_update(QuestScreenGrip *s,QuestScreenSettings *p,
    const QuestCockpitVehicle *fit,uint32_t now,int active,const int valid[2],const float pose[2][16],
    const float squeeze[2],const int busy[2],int16_t stick_x,int16_t stick_y)
{
    uint32_t dt=now-s->tick;s->tick=now;
    if(!active || !p->movable)return quest_screen_release(s,p);
    QuestMinimapGrip *g=&s->grip;QuestMinimapMount mount=quest_screen_mount(fit);
    float matrix[16];quest_minimap_matrix(matrix,&mount,&p->placement);int save=0;
    if(g->hand) {
        int h=g->hand-1;
        if(!valid[h] || busy[h] || !isfinite(squeeze[h]) || squeeze[h]<.15f)save=quest_screen_release(s,p);
        else {
            float horizontal=fabsf(stick_x/32767.f)>.25f?stick_x/32767.f:0;
            float vertical=fabsf(stick_y/32767.f)>.25f?stick_y/32767.f:0;
            float seconds=fminf(dt,100)*.001f;
            p->scale=fmaxf(.5f,fminf(3.f,p->scale+vertical*seconds));
            p->opacity=fmaxf(.05f,fminf(1.f,p->opacity+horizontal*seconds*.5f));
            float forward[3];for(int k=0;k<3;++k)forward[k]=pose[h][k]*g->wrist_forward[0]+pose[h][4+k]*g->wrist_forward[1]+pose[h][8+k]*g->wrist_forward[2];
            p->placement.pitch=fmaxf(-QUEST_MINIMAP_ANGLE_LIMIT,fminf(QUEST_MINIMAP_ANGLE_LIMIT,g->start.pitch+atan2f(-forward[1],hypotf(forward[0],forward[2]))));
            p->placement.yaw=fmaxf(-QUEST_MINIMAP_ANGLE_LIMIT,fminf(QUEST_MINIMAP_ANGLE_LIMIT,g->start.yaw+atan2f(forward[0],forward[2])));
            quest_minimap_matrix(matrix,&mount,&p->placement);
            for(int k=0;k<3;++k) {
                float arm=(matrix[k]*g->contact[0]+matrix[4+k]*g->contact[1])*p->scale/s->start_scale+matrix[8+k]*g->contact[2];
                p->placement.offset[k]=fmaxf(-QUEST_MINIMAP_LIMIT,fminf(QUEST_MINIMAP_LIMIT,pose[h][12+k]-mount.center[k]-arm));
            }
        }
    }
    for(int h=0;h<2;++h) {
        g->hover[h]=0;
        if(!valid[h] || busy[h] || !isfinite(squeeze[h])){g->armed[h]=0;continue;}
        if(squeeze[h]<.15f)g->armed[h]=1;
        float local[3];g->hover[h]=!g->hand && g->armed[h] && quest_screen_contact(matrix,quest_radio_screen_width(fit)*p->scale,pose[h]+12,local);
        if(squeeze[h]<.65f)continue;
        if(g->hover[h]) {
            g->hand=h+1;g->start=p->placement;s->start_scale=p->scale;s->start_opacity=p->opacity;
            memcpy(g->contact,local,sizeof local);
            for(int k=0;k<3;++k)g->wrist_forward[k]=pose[h][4*k+2];
        }
        g->armed[h]=0;
    }
    return save;
}
