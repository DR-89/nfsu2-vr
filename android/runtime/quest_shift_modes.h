#pragma once
#include "quest_shifter.h"

typedef struct {
    int mode,reverse,gestures,gesture_reverse,gesture_axis;
    float travel,zone,gesture_travel,gesture_angle,gesture_speed;
} QuestShiftConfig;
typedef struct {
    int hand,armed,latched,pending,zone[2],gesture;
    float origin,angle,last,last_angle;
    uint32_t tick,entered,pending_tick;
} QuestShiftMotion;

static inline QuestShiftConfig quest_shift_defaults(void)
{ return (QuestShiftConfig){0,0,0,1,0,.045f,.18f,.045f,20.f,.35f}; }

static inline int quest_shift_zone(int vehicle,const float pos[3],float radius)
{
    QuestShifter neutral={0};float p[3];quest_shifter_knob(&neutral,vehicle,p);
    float d=0;for(int k=0;k<3;++k){if(!isfinite(pos[k]))return 0;float a=pos[k]-p[k];d+=a*a;}
    return d<=radius*radius;
}
static inline int quest_shift_step(int actual,int count,int direction,int first_only)
{
    int next=actual+direction;
    if(next<QUEST_SHIFT_R || next>count+1 || (first_only && next!=QUEST_SHIFT_FIRST))return QUEST_SHIFT_NONE;
    return next;
}
/* One excursion, one shift. Entry, tracking discontinuities and ownership
 * changes establish a new baseline. Return to center before another stroke;
 * the return stroke never becomes an opposite shift. */
static inline int quest_shift_motion(QuestShiftMotion *m,QuestShifter *lever,
    const QuestShiftConfig *c,uint32_t now,int vehicle,int count,int actual,
    const int valid[2],const float pos[2][3],const float pose[2][16],
    const float squeeze[2],const int busy[2])
{
    int h=-1,old=m->hand;
    for(int i=0;i<2;++i) {
        m->zone[i]=valid[i] && !busy[i] && quest_shift_zone(vehicle,pos[i],c->zone);
        if(m->zone[i] && (old==i+1 || h<0))h=i;
    }
    int gesture=c->gestures && h>=0 && (!isfinite(squeeze[h]) || squeeze[h]<.65f);
    if(h<0 || (!gesture && (!c->mode || !isfinite(squeeze[h]) || squeeze[h]<.65f || lever->blocked[h]))) {
        m->hand=0;m->armed=m->latched=m->pending=0;
        if(c->mode){lever->hand=0;lever->x=lever->z=0;lever->gear=actual;}
        return QUEST_SHIFT_NONE;
    }
    float value=c->gesture_axis && gesture?pos[h][1]:pos[h][2];
    float angle=atan2f(-pose[h][9],hypotf(pose[h][8],pose[h][10]))*57.2957795f;
    uint32_t dt=now-m->tick;
    if(!isfinite(angle)){m->hand=0;m->pending=m->armed=0;return QUEST_SHIFT_NONE;}
    if(old!=h+1 || gesture!=m->gesture || !dt || dt>100 || fabsf(value-m->last)>.14f || fabsf(angle-m->last_angle)>70.f) {
        m->hand=h+1;m->origin=value;m->angle=angle;m->entered=now;
        m->armed=m->latched=m->pending=0;m->gesture=gesture;
    }
    float delta=value-m->origin,rotation=angle-m->angle;
    float speed=dt?fabsf(value-m->last)*1000.f/dt:0;
    float angular_speed=dt?fabsf(angle-m->last_angle)*1000.f/dt:0;
    m->last=value;m->last_angle=angle;m->tick=now;
    float threshold=gesture?c->gesture_travel:c->travel;
    int centered=fabsf(delta)<threshold*.4f && (!gesture || fabsf(rotation)<c->gesture_angle*.4f);
    if(now-m->entered<120) {m->origin=value;m->angle=angle;return QUEST_SHIFT_NONE;}
    if(centered){m->armed=1;m->latched=0;}
    if(m->pending && now-m->pending_tick>550)m->pending=0;
    if(c->mode) {
        lever->hand=gesture?0:h+1;lever->x=0;
        lever->z=fmaxf(-QUEST_SHIFT_THROW,fminf(QUEST_SHIFT_THROW,delta));lever->gear=actual;
    }
    if(!m->armed || m->latched)return QUEST_SHIFT_NONE;
    int linear=fabsf(delta)>=threshold && (!gesture || speed>=c->gesture_speed);
    int angular=gesture && fabsf(rotation)>=c->gesture_angle && angular_speed>=c->gesture_speed*180.f;
    if(!linear && !angular)return QUEST_SHIFT_NONE;
    int direction=(linear?delta:rotation)>0?1:-1;
    if(gesture?c->gesture_reverse:c->reverse)direction=-direction;
    m->latched=1;m->armed=0;
    if(gesture && c->gestures==2 && direction<0) {
        if(!m->pending){m->pending=1;m->pending_tick=now;return QUEST_SHIFT_NONE;}
    }
    m->pending=0;
    return quest_shift_step(actual,count,direction,lever->first_only);
}
