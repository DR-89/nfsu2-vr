#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float sensitivity, rotation, height, distance;
    int resolution;
} QuestVRSettings;

enum { QUEST_VR_RESOLUTIONS=6, QUEST_VR_ROWS=7 };
static const unsigned quest_vr_resolutions[QUEST_VR_RESOLUTIONS][2]={
    {0,0},{1280,1344},{1680,1760},{2550,2640},{2940,3080},{3360,3520}
};
static inline QuestVRSettings quest_vr_defaults(void)
{ return (QuestVRSettings){1,360,0,0,0}; }
static inline float quest_vr_bound(float v,float lo,float hi,float fallback)
{ return isfinite(v)?fmaxf(lo,fminf(hi,v)):fallback; }
static inline void quest_vr_validate(QuestVRSettings *s)
{
    s->sensitivity=quest_vr_bound(s->sensitivity,.5f,3,1);
    s->rotation=quest_vr_bound(s->rotation,180,1080,360);
    s->height=quest_vr_bound(s->height,-.35f,.35f,0);
    s->distance=quest_vr_bound(s->distance,-.40f,.40f,0);
    if(s->resolution<0 || s->resolution>=QUEST_VR_RESOLUTIONS) s->resolution=0;
}
/* Explicit keys and bounded input: ignore unknown/future settings. */
static inline void quest_vr_parse(QuestVRSettings *s,const char *text)
{
    char line[128],key[64];float value;
    while(*text) {
        size_t n=strcspn(text,"\n");
        if(n<sizeof line) {
            memcpy(line,text,n);line[n]=0;
            if(sscanf(line,"%63[^=]=%f",key,&value)==2) {
                if(!strcmp(key,"steering_sensitivity")) s->sensitivity=value;
                else if(!strcmp(key,"wheel_rotation")) s->rotation=value;
                else if(!strcmp(key,"seat_height")) s->height=value;
                else if(!strcmp(key,"seat_distance")) s->distance=value;
                else if(!strcmp(key,"resolution") && isfinite(value) && value>=0 && value<QUEST_VR_RESOLUTIONS) s->resolution=(int)value;
            }
        }
        text+=n;if(*text)++text;
    }
    quest_vr_validate(s);
}
static inline int quest_vr_format(const QuestVRSettings *s,char *out,size_t capacity)
{
    return snprintf(out,capacity,"version=1\nsteering_sensitivity=%.3f\nwheel_rotation=%.1f\nseat_height=%.3f\nseat_distance=%.3f\nresolution=%d\n",
        s->sensitivity,s->rotation,s->height,s->distance,s->resolution);
}
static inline void quest_vr_adjust(QuestVRSettings *s,int row,int step)
{
    if(row==0) s->sensitivity+=step*.1f;
    if(row==1) s->rotation+=step*90.0f;
    if(row==2) s->height+=step*.01f;
    if(row==3) s->distance+=step*.01f;
    if(row==4) s->resolution=(s->resolution+step+QUEST_VR_RESOLUTIONS)%QUEST_VR_RESOLUTIONS;
    quest_vr_validate(s);
}

typedef struct { int open,row,held,swallow; uint32_t repeat; } QuestVRMenu;
enum { QUEST_VR_UP=1,QUEST_VR_DOWN=2,QUEST_VR_LEFT=4,QUEST_VR_RIGHT=8,
       QUEST_VR_ACCEPT=16,QUEST_VR_BACK=32,QUEST_VR_OPEN=64 };
/* Return bit 1 on data change, bit 2 on closing (persist), bit 4 to consume
 * input. Closing must not also activate the game's underlying pause menu. */
static inline int quest_vr_menu_step(QuestVRMenu *m,QuestVRSettings *s,
                                     uint32_t now,int paused,int keys)
{
    if(!paused) {
        int save=m->open?2:0;
        memset(m,0,sizeof *m);
        m->held=keys; /* X held before pausing is not a new settings press. */
        return save;
    }
    if(m->swallow) { if(!keys)m->swallow=0;m->held=keys;return 4; }
    int edge=keys&~m->held;m->held=keys;
    if(!m->open) {
        if(edge&QUEST_VR_OPEN) {m->open=1;m->repeat=now+350;return 4;}
        return 0;
    }
    if(edge&QUEST_VR_BACK) {m->open=0;m->swallow=1;return 6;}
    int nav=keys&15;
    if(nav && ((edge&15) || (int32_t)(now-m->repeat)>=0)) {
        m->repeat=now+((edge&15)?350:140);
        if(nav&QUEST_VR_UP)m->row=(m->row+QUEST_VR_ROWS-1)%QUEST_VR_ROWS;
        else if(nav&QUEST_VR_DOWN)m->row=(m->row+1)%QUEST_VR_ROWS;
        else if(m->row<5) {quest_vr_adjust(s,m->row,(nav&QUEST_VR_RIGHT)?1:-1);return 5;}
    }
    if(edge&QUEST_VR_ACCEPT) {
        if(m->row==5) {*s=quest_vr_defaults();return 5;}
        if(m->row==6) {m->open=0;m->swallow=1;return 6;}
    }
    return 4;
}
