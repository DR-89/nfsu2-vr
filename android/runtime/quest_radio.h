#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "quest_cockpit_vehicle.h"

enum { QUEST_RADIO_NONE, QUEST_RADIO_VOLUME, QUEST_RADIO_PREV, QUEST_RADIO_PLAY, QUEST_RADIO_NEXT };
typedef struct { int action;float volume; } QuestRadioCommand;
typedef struct { int hand,control,blocked[2];uint32_t hover_mask; } QuestRadioGrip;
typedef struct { char title[64],artist[64]; } QuestRadioSong;
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
/* Shared face coordinates for both rendering and contact. U grows toward
 * the driver's right, including in the two right-hand-drive interiors. */
static inline void quest_radio_point(float p[3],const QuestCockpitVehicle *fit,float u,float v)
{
    float y=-.307f+(.5f-v)*.053f;
    float local[3]={.3116f+(u-.5f)*.236f*fit->side,y,-.365f-.40f*(y+.392f)};
    quest_cockpit_fit_point(p,local,fit);
}
static inline int quest_radio_hit(const float p[3],const QuestCockpitVehicle *fit,float *u)
{
    float lo[3],hi[3];quest_radio_point(lo,fit,0,0);quest_radio_point(hi,fit,1,1);
    *u=(p[0]-lo[0])/(hi[0]-lo[0]);
    float v=(p[1]-lo[1])/(hi[1]-lo[1]);
    float z=lo[2]+v*(hi[2]-lo[2]);
    if(*u<0 || *u>1 || v<-.16f || v>1.f || fabsf(p[2]-z)>.032f)return QUEST_RADIO_NONE;
    if(*u<.46f)return QUEST_RADIO_VOLUME;
    if(*u<.64f)return QUEST_RADIO_PREV;
    return *u<.82f?QUEST_RADIO_PLAY:QUEST_RADIO_NEXT;
}
static inline QuestRadioCommand quest_radio_update(QuestRadioGrip *g,const QuestCockpitVehicle *fit,
    int active,const int valid[2],const float pose[2][16],const float squeeze[2],const int busy[2])
{
    QuestRadioCommand cmd={0};
    g->hover_mask=0;
    if(!active){g->hand=g->control=0;g->blocked[0]=g->blocked[1]=1;return cmd;}
    for(int h=0;h<2;++h) {
        if(!valid[h] || !isfinite(squeeze[h]) || busy[h]) {
            if(g->hand==h+1)g->hand=g->control=0;
            g->blocked[h]=1;continue;
        }
        if(squeeze[h]<.15f){
            g->blocked[h]=0;if(g->hand==h+1)g->hand=g->control=0;
        }
        float finger[3];for(int k=0;k<3;++k)finger[k]=pose[h][12+k]+.045f*pose[h][4+k];
        float u;int control=quest_radio_hit(finger,fit,&u);
        if(!g->blocked[h] && squeeze[h]>=.65f) {
            g->blocked[h]=1;
            if(!g->hand && control){g->hand=h+1;g->control=control;cmd.action=control;}
        }
        if(control && !g->blocked[h])g->hover_mask|=1u<<control;
        if(g->hand==h+1 && g->control==QUEST_RADIO_VOLUME) {
            cmd.action=QUEST_RADIO_VOLUME;cmd.volume=fmaxf(0,fminf(1,(u-.09f)/.33f));
        }
    }
    /* A held control owns the radio until release, even with two hands on it. */
    if(g->hand)g->hover_mask=0;
    return cmd;
}
/* The retail playlist contains 27 indices and separate FE/race enable masks. */
static inline int quest_radio_track(int current,uint32_t enabled,int direction)
{
    if(current<0 || current>=27)current=direction>0?26:0;
    for(int i=0;i<27;++i) {
        current=(current+27+direction)%27;
        if(enabled&(1u<<current))return current;
    }
    return -1;
}
