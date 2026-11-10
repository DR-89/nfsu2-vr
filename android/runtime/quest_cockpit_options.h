#pragma once
#include <stddef.h>
#include <stdio.h>
#include "quest_shift_modes.h"
#include "quest_screen.h"
typedef struct {
    QuestShiftConfig shift;
    QuestScreenSettings screen;
    float steering_deadzone;
    int shift_click,shift_sound,shift_lights;
} QuestCockpitOptions;
typedef struct {
    const char *key,*label,*choices[3];
    size_t offset;int integer;float low,high,step,initial;
} QuestCockpitOption;
#define QOPT(field,key,label,lo,hi,step,init) {key,label,{0},offsetof(QuestCockpitOptions,field),0,lo,hi,step,init}
#define QCHOICE(field,key,label,init,a,b,c) {key,label,{a,b,c},offsetof(QuestCockpitOptions,field),1,0,2,1,init}
#define QCHOICE2(field,key,label,init,a,b) {key,label,{a,b,NULL},offsetof(QuestCockpitOptions,field),1,0,1,1,init}
static const QuestCockpitOption quest_cockpit_options[]={
    QCHOICE2(shift.mode,"shifter_mode","SHIFTER MODE",0,"H-PATTERN","I-PATTERN"),
    QCHOICE2(shift.reverse,"shift_reverse","SEQUENTIAL UPSHIFT",0,"PULL BACK","PUSH FORWARD"),
    QOPT(shift.travel,"shift_travel","LEVER TRAVEL (CM)",.02f,.10f,.005f,.045f),
    QCHOICE(shift.gestures,"shift_gestures","NO-GRIP GESTURES",0,"OFF","SINGLE BOTH","DOUBLE DOWN"),
    QCHOICE2(shift.gesture_reverse,"gesture_reverse","GESTURE UPSHIFT",1,"BACK / UP","FORWARD / DOWN"),
    QCHOICE2(shift.gesture_axis,"gesture_axis","GESTURE AXIS",0,"FORWARD-BACK","UP-DOWN"),
    QOPT(shift.zone,"shift_zone","GESTURE ZONE (CM)",.10f,.30f,.01f,.18f),
    QOPT(shift.gesture_travel,"gesture_travel","FLICK DEADZONE (CM)",.02f,.12f,.005f,.045f),
    QOPT(shift.gesture_angle,"gesture_angle","WRIST DEADZONE (DEG)",10,60,5,20),
    QOPT(shift.gesture_speed,"gesture_speed","FLICK SPEED (M/S)",.1f,1.5f,.05f,.35f),
    QCHOICE2(shift_click,"shift_click","SHIFT HAPTIC CLICK",1,"OFF","ON"),
    QCHOICE2(shift_sound,"shift_sound","SHIFT SOUND",0,"OFF","ON"),
    QCHOICE2(shift_lights,"shift_lights","SHIFT LED CUES",1,"OFF","ON"),
    QOPT(steering_deadzone,"steering_deadzone","WHEEL DEADZONE (%)",0,.25f,.01f,0),
    QCHOICE2(screen.movable,"screen_movable","GRAB DASH SCREEN",1,"OFF","ON"),
    QOPT(screen.opacity,"screen_opacity","SCREEN OPACITY (%)",.05f,1,.05f,1),
    QOPT(screen.scale,"screen_scale","SCREEN SIZE (%)",.5f,3,.1f,1),
    QCHOICE2(screen.page,"screen_page","DASH SCREEN PAGE",0,"VIDEO / MUSIC","TELEMETRY"),
    QOPT(screen.placement.offset[0],"screen_x",NULL,-2,2,.01f,0),
    QOPT(screen.placement.offset[1],"screen_y",NULL,-2,2,.01f,0),
    QOPT(screen.placement.offset[2],"screen_z",NULL,-2,2,.01f,0),
    QOPT(screen.placement.pitch,"screen_pitch",NULL,-QUEST_MINIMAP_ANGLE_LIMIT,QUEST_MINIMAP_ANGLE_LIMIT,.01f,0),
    QOPT(screen.placement.yaw,"screen_yaw",NULL,-QUEST_MINIMAP_ANGLE_LIMIT,QUEST_MINIMAP_ANGLE_LIMIT,.01f,0)
};
#undef QOPT
#undef QCHOICE2
#undef QCHOICE
enum { QUEST_COCKPIT_OPTION_ROWS=18, QUEST_COCKPIT_OPTION_COUNT=sizeof quest_cockpit_options/sizeof *quest_cockpit_options };
static inline float quest_option_get(const QuestCockpitOptions *s,int i)
{
    const QuestCockpitOption *o=&quest_cockpit_options[i];const char *p=(const char *)s+o->offset;
    if(o->integer){int v;memcpy(&v,p,sizeof v);return (float)v;}float v;memcpy(&v,p,sizeof v);return v;
}
static inline void quest_option_set(QuestCockpitOptions *s,int i,float v)
{
    const QuestCockpitOption *o=&quest_cockpit_options[i];char *p=(char *)s+o->offset;
    if(!isfinite(v) || (o->integer && (v<o->low || v>o->high || v!=floorf(v))))v=o->initial;
    v=fmaxf(o->low,fminf(o->high,v));
    if(o->integer){int n=(int)v;memcpy(p,&n,sizeof n);}else memcpy(p,&v,sizeof v);
}
static inline QuestCockpitOptions quest_cockpit_defaults(void)
{ QuestCockpitOptions s={0};for(int i=0;i<QUEST_COCKPIT_OPTION_COUNT;++i)quest_option_set(&s,i,quest_cockpit_options[i].initial);return s; }
static inline void quest_cockpit_adjust(QuestCockpitOptions *s,int i,int step)
{
    const QuestCockpitOption *o=&quest_cockpit_options[i];float v=quest_option_get(s,i)+step*o->step;
    if(o->integer){if(v>o->high)v=o->low;if(v<o->low)v=o->high;}quest_option_set(s,i,v);
}
