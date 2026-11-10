#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "quest_cockpit_vehicle.h"
#include "quest_radio_types.h"

typedef struct { float u,v,depth; } QuestRadioPosition;
typedef struct {
    int control,blocked,pointing,previous_valid;
    QuestRadioPosition previous;
} QuestCockpitTouch;
/* 0x8FE16 stops the stream on exact zero. At 0x7F519 the retail mixer
 * scales this same gain by at most 32767 and converts it to an integer.
 * This positive input keeps transport running and still mixes to zero. */
#define QUEST_RADIO_SILENT_GAIN 0x1p-24f
static inline float quest_radio_mix_gain(float level)
{
    return fmaxf(level,QUEST_RADIO_SILENT_GAIN);
}
static inline int quest_radio_volume_changed(float from,float to)
{
    return from!=to && (from==0 || to==0 || to==1 || fabsf(to-from)>.0001f);
}
#define QUEST_RADIO_FACE_WIDTH .236f
#define QUEST_RADIO_RECESS .10f /* meters forward in the fitted cabin */

/* Leave 3 mm on each side of the fitted radio face for the bezel. */
static inline float quest_radio_screen_width(const QuestCockpitVehicle *fit)
{
    return QUEST_RADIO_FACE_WIDTH*fit->far_width-.006f;
}

/* The top of the 16:9 screen sits below the default minimap. Measure down
 * the fitted console face so the screen and the 6 mm gap keep their size. */
static inline void quest_radio_stack_point(float p[3],const QuestCockpitVehicle *fit,float down)
{
    const float slope=-.40f*fit->front_depth;
    float y=-.237f-down/sqrtf(1.f+slope*slope);
    const float local[3]={.3116f,y,-.365f-.40f*(y+.392f)};
    quest_cockpit_fit_point(p,local,fit);
    p[2]-=QUEST_RADIO_RECESS;
}
/* Rendering and fingertip contact share the same key positions. U grows
 * toward the driver's right in both left- and right-hand-drive interiors. */
static inline void quest_radio_point(float p[3],const QuestCockpitVehicle *fit,float u,float v)
{
    quest_radio_stack_point(p,fit,quest_radio_screen_width(fit)*(9.f/16.f)+.006f);
    p[0]+=(u-.5f)*QUEST_RADIO_FACE_WIDTH*fit->far_width;
    p[1]-=v*.053f;
    p[2]+=v*.053f*.40f*fit->front_depth;
}
static inline void quest_radio_screen_point(float p[3],const QuestCockpitVehicle *fit,float u,float v)
{
    float width=quest_radio_screen_width(fit);
    quest_radio_stack_point(p,fit,v*width*(9.f/16.f));
    p[0]+=(u-.5f)*width;
}
/* Four keys along the raised driver's door armrest. U increases to the
 * driver's right on either side of the car. V increases toward the rear. */
static inline void quest_seat_point(float p[3],const QuestCockpitVehicle *fit,float u,float v)
{
    float local[3]={-.322f+(u-.5f)*.047f*fit->side,-.429f,-.216f+v*.164f};
    quest_cockpit_fit_point(p,local,fit);
}
typedef struct { int control;float depth,u; } QuestButtonHit;
static inline QuestRadioPosition quest_radio_position(const float tip[3],const QuestCockpitVehicle *fit)
{
    float a[3],b[3];
    quest_radio_point(a,fit,0,0);quest_radio_point(b,fit,1,1);
    float dy=b[1]-a[1],dz=b[2]-a[2],y=tip[1]-a[1],z=tip[2]-a[2];
    float length2=dy*dy+dz*dz;
    return (QuestRadioPosition){(tip[0]-a[0])/(b[0]-a[0]),(dy*y+dz*z)/length2,(dz*y-dy*z)/sqrtf(length2)};
}
static inline QuestButtonHit quest_radio_hit(QuestRadioPosition p)
{
    if(p.u<0 || p.u>1 || p.v<0 || p.v>1 || p.depth>.095f)return (QuestButtonHit){0};
    int id=p.u<.46f?QUEST_RADIO_VOLUME:(p.u<.64f?QUEST_RADIO_PREV:(p.u<.82f?QUEST_RADIO_PLAY:QUEST_RADIO_NEXT));
    return (QuestButtonHit){id,p.depth,p.u};
}
static inline QuestButtonHit quest_seat_hit(const float tip[3],const QuestCockpitVehicle *fit)
{
    float a[3],b[3];quest_seat_point(a,fit,0,0);quest_seat_point(b,fit,1,1);
    float u=(tip[0]-a[0])/(b[0]-a[0]),v=(tip[2]-a[2])/(b[2]-a[2]),depth=tip[1]-a[1];
    if(u>=0 && u<=1 && v>=0 && v<1 && depth<=.095f)
        return (QuestButtonHit){QUEST_SEAT_FORWARD+(int)(v*4),depth,u};
    return (QuestButtonHit){0};
}
/* Each index finger has its own latch. A hand resting on the armrest or
 * volume slider must not consume the other hand's next press. */
static inline QuestRadioCommand quest_touch_update(QuestCockpitTouch *g,const QuestCockpitVehicle *fit,
    int usable,int radio,const float pose[16],const float tip_local[3],int occupied)
{
    QuestRadioCommand cmd={0};
    if(!usable){*g=(QuestCockpitTouch){.blocked=1};return cmd;}
    float tip[3];
    for(int k=0;k<3;++k)tip[k]=pose[12+k]+pose[k]*tip_local[0]+pose[4+k]*tip_local[1]+pose[8+k]*tip_local[2];
    QuestButtonHit hit={0};
    if(radio) {
        QuestRadioPosition p=quest_radio_position(tip,fit),old=g->previous;
        hit=quest_radio_hit(p);
        /* A quick poke can pass the entire key between tracked frames.
         * Use the inward crossing to select the key at the instant of contact. */
        float travel=old.depth-p.depth;
        if(!g->blocked && g->previous_valid && old.depth>.006f && p.depth<=.006f &&
           travel<.30f && fabsf(p.u-old.u)<1.2f && fabsf(p.v-old.v)<5.f) {
            float t=(old.depth-.006f)/travel;
            QuestButtonHit crossed=quest_radio_hit((QuestRadioPosition){
                old.u+t*(p.u-old.u),old.v+t*(p.v-old.v),.006f});
            if(crossed.control)hit=crossed;
        }
        g->previous=p;
    }
    g->previous_valid=radio;
    if(!hit.control) {
        QuestRadioPosition p=quest_radio_position(tip,fit);
        if(p.u>=.05f && p.u<=.95f && p.v>=1.9f && p.v<=2.5f && p.depth<=.095f)
            hit=(QuestButtonHit){p.u<.5f?QUEST_COCKPIT_LED:QUEST_COCKPIT_PAGE,p.depth,p.u};
    }
    if(!hit.control)hit=quest_seat_hit(tip,fit);
    g->pointing=hit.control;
    if(!hit.control || hit.depth>.025f || hit.depth<-.035f) {
        g->control=0;g->blocked=hit.control && hit.depth<-.035f;
        if(g->blocked)g->pointing=0;
        return cmd;
    }
    if(!g->blocked && hit.depth<=.006f) {
        g->blocked=1;
        if(hit.control!=occupied){g->control=hit.control;cmd.action=hit.control;}
    }
    if(g->control==QUEST_RADIO_VOLUME && hit.control==QUEST_RADIO_VOLUME) {
        cmd.action=QUEST_RADIO_VOLUME;cmd.volume=fmaxf(0,fminf(1,(hit.u-.09f)/.33f));
    }
    return cmd;
}
/* The retail playlist contains 27 indices and separate FE/race enable masks. */
static inline int quest_radio_playing(uint32_t sample_node,uint32_t polled_node,uint16_t voice_node)
{
    return sample_node<81 && polled_node<81 && voice_node<81?(int)(sample_node/3):-1;
}

static inline int quest_radio_track(int current,uint32_t enabled,int direction)
{
    if(current<0 || current>=27)current=direction>0?26:0;
    for(int i=0;i<27;++i) {
        current=(current+27+direction)%27;
        if(enabled&(1u<<current))return current;
    }
    return -1;
}
