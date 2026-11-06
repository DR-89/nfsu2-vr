#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quest_wheel_limits.h"
#include "quest_minimap.h"
#include "quest_radio_types.h"

typedef struct {
    float sensitivity, rotation, height, distance;
    int resolution;
    float return_speed;
    int mirror_resolution,shift_guide;
    QuestMinimapPlacement map;
    int engine_rumble,grip_feedback,master_gain_db;
    int led_strips,led_color,led_music_pulse;
    int music_source;
} QuestVRSettings;

static const struct {const char *name;uint32_t rgb;} quest_led_colors[]={
    {"CYAN",0x32DBE7},{"BLUE",0x5287FF},{"VIOLET",0xA072FF},{"PINK",0xFF64AD},
    {"RED",0xFF5144},{"AMBER",0xFFAB43},{"GREEN",0x4DDF88},{"WHITE",0xE6EFF5}
};
enum { QUEST_LED_COLORS=sizeof quest_led_colors/sizeof *quest_led_colors };

/* 0 and 1 are the original saved OFF and ON settings. */
enum { QUEST_GUIDE_OFF, QUEST_GUIDE_ALWAYS, QUEST_GUIDE_AT_LEVER, QUEST_GUIDE_MODES };
enum { QUEST_VR_RESOLUTIONS=6, QUEST_VR_RETURN_ROW=5, QUEST_VR_MIRROR_ROW, QUEST_VR_GUIDE_ROW,
       QUEST_VR_LED_ROW, QUEST_VR_LED_COLOR_ROW, QUEST_VR_LED_PULSE_ROW,
       QUEST_VR_MUSIC_ROW, QUEST_VR_GAIN_ROW, QUEST_VR_HAPTICS_ROW,
       QUEST_VR_RESET_MIRRORS_ROW, QUEST_VR_DEFAULTS_ROW, QUEST_VR_SAVE_ROW, QUEST_VR_ROWS,
       QUEST_VR_ENGINE_ROW=QUEST_VR_ROWS, QUEST_VR_GRIP_ROW, QUEST_VR_HAPTICS_BACK, QUEST_VR_END };
enum { QUEST_VR_GAIN_LIMIT_DB=12 };
static const unsigned quest_mirror_widths[]={256,512,1024,2048,4096};
enum { QUEST_MIRROR_PRESETS=sizeof quest_mirror_widths/sizeof *quest_mirror_widths };
enum { QUEST_VR_RESET_MIRRORS=8 };
static const unsigned quest_vr_resolutions[QUEST_VR_RESOLUTIONS][2]={
    {0,0},{1280,1344},{1680,1760},{2550,2640},{2940,3080},{3360,3520}
};
static inline QuestVRSettings quest_vr_defaults(void)
{ return (QuestVRSettings){1,360,0,0,0,QUEST_WHEEL_RETURN_DEFAULT,2,QUEST_GUIDE_ALWAYS,{0},0,1,0,1,0,1,QUEST_MUSIC_AUTO}; }
static inline float quest_vr_bound(float v,float lo,float hi,float fallback)
{ return isfinite(v)?fmaxf(lo,fminf(hi,v)):fallback; }
static inline void quest_vr_validate(QuestVRSettings *s)
{
    s->sensitivity=quest_vr_bound(s->sensitivity,.5f,3,1);
    s->rotation=quest_vr_bound(s->rotation,180,1080,360);
    s->height=quest_vr_bound(s->height,-.35f,.35f,0);
    s->distance=quest_vr_bound(s->distance,-.40f,.40f,0);
    s->return_speed=quest_vr_bound(s->return_speed,0,QUEST_WHEEL_RETURN_MAX,QUEST_WHEEL_RETURN_DEFAULT);
    if(s->resolution<0 || s->resolution>=QUEST_VR_RESOLUTIONS) s->resolution=0;
    if(s->mirror_resolution<0 || s->mirror_resolution>=QUEST_MIRROR_PRESETS)s->mirror_resolution=2;
    if(s->shift_guide<0 || s->shift_guide>=QUEST_GUIDE_MODES)s->shift_guide=QUEST_GUIDE_ALWAYS;
    if(s->led_color<0 || s->led_color>=QUEST_LED_COLORS)s->led_color=0;
    if(s->led_strips!=0 && s->led_strips!=1)s->led_strips=1;
    if(s->led_music_pulse!=0 && s->led_music_pulse!=1)s->led_music_pulse=1;
    if(s->music_source<0 || s->music_source>=QUEST_MUSIC_MODES)s->music_source=QUEST_MUSIC_AUTO;
    for(int axis=0;axis<3;++axis)s->map.offset[axis]=quest_vr_bound(s->map.offset[axis],-QUEST_MINIMAP_LIMIT,QUEST_MINIMAP_LIMIT,0);
    s->map.pitch=quest_vr_bound(s->map.pitch,-QUEST_MINIMAP_ANGLE_LIMIT,QUEST_MINIMAP_ANGLE_LIMIT,0);
    s->map.yaw=quest_vr_bound(s->map.yaw,-QUEST_MINIMAP_ANGLE_LIMIT,QUEST_MINIMAP_ANGLE_LIMIT,0);
    if(s->master_gain_db < -QUEST_VR_GAIN_LIMIT_DB)s->master_gain_db=-QUEST_VR_GAIN_LIMIT_DB;
    if(s->master_gain_db > QUEST_VR_GAIN_LIMIT_DB)s->master_gain_db=QUEST_VR_GAIN_LIMIT_DB;
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
                else if(!strcmp(key,"wheel_return_speed")) s->return_speed=value;
                else if(!strcmp(key,"seat_height")) s->height=value;
                else if(!strcmp(key,"seat_distance")) s->distance=value;
                else if(!strcmp(key,"mirror_resolution") && value>=0 && value<QUEST_MIRROR_PRESETS && value==floorf(value)) s->mirror_resolution=(int)value;
                else if(!strcmp(key,"shift_guide") && value>=0 && value<QUEST_GUIDE_MODES && value==floorf(value)) s->shift_guide=(int)value;
                else if(!strcmp(key,"map_x")) s->map.offset[0]=value;
                else if(!strcmp(key,"map_y")) s->map.offset[1]=value;
                else if(!strcmp(key,"map_z")) s->map.offset[2]=value;
                else if(!strcmp(key,"map_pitch")) s->map.pitch=value;
                else if(!strcmp(key,"map_yaw")) s->map.yaw=value;
                else if(!strcmp(key,"engine_rumble") && (value==0 || value==1))s->engine_rumble=(int)value;
                else if(!strcmp(key,"grip_feedback") && (value==0 || value==1))s->grip_feedback=(int)value;
                else if(!strcmp(key,"led_strips") && (value==0 || value==1))s->led_strips=(int)value;
                else if(!strcmp(key,"led_music_pulse") && (value==0 || value==1))s->led_music_pulse=(int)value;
                else if(!strcmp(key,"music_source") && value>=0 && value<QUEST_MUSIC_MODES && value==floorf(value))s->music_source=(int)value;
                else if(!strcmp(key,"led_color") && value>=0 && value<QUEST_LED_COLORS && value==floorf(value))s->led_color=(int)value;
                else if(!strcmp(key,"master_gain_db") && value>=-QUEST_VR_GAIN_LIMIT_DB && value<=QUEST_VR_GAIN_LIMIT_DB &&
                        value==floorf(value))s->master_gain_db=(int)value;
                else if(!strcmp(key,"resolution") && isfinite(value) && value>=0 && value<QUEST_VR_RESOLUTIONS) s->resolution=(int)value;
            }
        }
        text+=n;if(*text)++text;
    }
    quest_vr_validate(s);
}
static inline int quest_vr_format(const QuestVRSettings *s,char *out,size_t capacity)
{
    return snprintf(out,capacity,"version=1\nsteering_sensitivity=%.3f\nwheel_rotation=%.1f\nseat_height=%.3f\nseat_distance=%.3f\nresolution=%d\nwheel_return_speed=%.1f\n"
        "mirror_resolution=%d\nshift_guide=%d\nmap_x=%.3f\nmap_y=%.3f\nmap_z=%.3f\nmap_pitch=%.5f\nmap_yaw=%.5f\n"
        "engine_rumble=%d\ngrip_feedback=%d\nmaster_gain_db=%d\nled_strips=%d\nled_color=%d\nled_music_pulse=%d\nmusic_source=%d\n",
        s->sensitivity,s->rotation,s->height,s->distance,s->resolution,s->return_speed,
        s->mirror_resolution,s->shift_guide,s->map.offset[0],s->map.offset[1],s->map.offset[2],s->map.pitch,s->map.yaw,
        s->engine_rumble,s->grip_feedback,s->master_gain_db,s->led_strips,s->led_color,s->led_music_pulse,s->music_source);
}
static inline void quest_vr_adjust(QuestVRSettings *s,int row,int step)
{
    if(row==0) s->sensitivity+=step*.1f;
    if(row==1) s->rotation+=step*90.0f;
    if(row==2) s->height+=step*.01f;
    if(row==3) s->distance+=step*.01f;
    if(row==4) s->resolution=(s->resolution+step+QUEST_VR_RESOLUTIONS)%QUEST_VR_RESOLUTIONS;
    if(row==QUEST_VR_RETURN_ROW)s->return_speed+=step*30.f;
    if(row==QUEST_VR_MIRROR_ROW)s->mirror_resolution=(s->mirror_resolution+step+QUEST_MIRROR_PRESETS)%QUEST_MIRROR_PRESETS;
    if(row==QUEST_VR_GUIDE_ROW)s->shift_guide=(s->shift_guide+step+QUEST_GUIDE_MODES)%QUEST_GUIDE_MODES;
    if(row==QUEST_VR_LED_COLOR_ROW)s->led_color=(s->led_color+step+QUEST_LED_COLORS)%QUEST_LED_COLORS;
    if(row==QUEST_VR_MUSIC_ROW)s->music_source=(s->music_source+step+QUEST_MUSIC_MODES)%QUEST_MUSIC_MODES;
    if(row==QUEST_VR_GAIN_ROW)s->master_gain_db+=step;
    quest_vr_validate(s);
}

typedef struct { int open,row,held,swallow; uint32_t repeat; int mirrors_centered; } QuestVRMenu;
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
        if(edge&QUEST_VR_OPEN) {m->open=1;m->mirrors_centered=0;m->repeat=now+350;return 4;}
        return 0;
    }
    int haptics=m->row>=QUEST_VR_ENGINE_ROW;
    if(edge&QUEST_VR_BACK) {
        if(haptics){m->row=QUEST_VR_HAPTICS_ROW;return 4;}
        m->open=0;m->swallow=1;return 6;
    }
    int nav=keys&15;
    if(nav && ((edge&15) || (int32_t)(now-m->repeat)>=0)) {
        m->repeat=now+((edge&15)?350:140);
        int first=haptics?QUEST_VR_ENGINE_ROW:0,last=haptics?QUEST_VR_HAPTICS_BACK:QUEST_VR_SAVE_ROW;
        if(nav&QUEST_VR_UP)m->row=m->row==first?last:m->row-1;
        else if(nav&QUEST_VR_DOWN)m->row=m->row==last?first:m->row+1;
        else if(m->row<QUEST_VR_HAPTICS_ROW && m->row!=QUEST_VR_LED_ROW && m->row!=QUEST_VR_LED_PULSE_ROW) {
            quest_vr_adjust(s,m->row,(nav&QUEST_VR_RIGHT)?1:-1);return 5;
        }
    }
    if((m->row==QUEST_VR_ENGINE_ROW || m->row==QUEST_VR_GRIP_ROW) &&
       (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))) {
        int *toggle=m->row==QUEST_VR_ENGINE_ROW?&s->engine_rumble:&s->grip_feedback;
        *toggle=!*toggle;return 5;
    }
    if((m->row==QUEST_VR_LED_ROW || m->row==QUEST_VR_LED_PULSE_ROW) && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))) {
        int *toggle=m->row==QUEST_VR_LED_ROW?&s->led_strips:&s->led_music_pulse;
        *toggle=!*toggle;return 5;
    }
    if(edge&QUEST_VR_ACCEPT) {
        if(m->row==QUEST_VR_HAPTICS_ROW){m->row=QUEST_VR_ENGINE_ROW;return 4;}
        if(m->row==QUEST_VR_HAPTICS_BACK){m->row=QUEST_VR_HAPTICS_ROW;return 4;}
        if(m->row==QUEST_VR_RESET_MIRRORS_ROW) {m->mirrors_centered=1;return 4|QUEST_VR_RESET_MIRRORS;}
        if(m->row==QUEST_VR_DEFAULTS_ROW) {*s=quest_vr_defaults();m->mirrors_centered=1;return 5|QUEST_VR_RESET_MIRRORS;}
        if(m->row==QUEST_VR_SAVE_ROW) {m->open=0;m->swallow=1;return 6;}
    }
    return 4;
}
