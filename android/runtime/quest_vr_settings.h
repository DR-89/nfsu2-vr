#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quest_wheel_limits.h"
#include "quest_minimap.h"
#include "quest_radio_types.h"
#include "quest_cockpit_options.h"

typedef struct {
    float sensitivity, rotation, height, distance;
    int resolution;
    float return_speed;
    int mirror_resolution,shift_guide;
    QuestMinimapPlacement map;
    int engine_rumble,grip_feedback,master_gain_db;
    int led_strips,led_color,led_music_pulse;
    int music_source;
    int show_hidden_races;
    int visual_rep_remover;
    int online_cars;
    int career_catchup;
    int gearbox_whine;
    int gps_distance;
    int upscaled_hud,detailed_map,subtitles,hq_vinyls,special_vinyls,restored_soundtrack,headlights,crash_damage,regional_cars;
    QuestCockpitOptions cockpit;
    int copchase, copchase_style, copchase_heat, copchase_aggression, copchase_limit;
    int copchase_sprint;
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
       QUEST_VR_MUSIC_ROW, QUEST_VR_GAIN_ROW, QUEST_VR_HAPTICS_ROW, QUEST_VR_MODS_ROW, QUEST_VR_PROFILES_ROW,
       QUEST_VR_ONLINE_CARS_ROW, QUEST_VR_RESET_MIRRORS_ROW, QUEST_VR_COCKPIT_ROW, QUEST_VR_DEFAULTS_ROW, QUEST_VR_SAVE_ROW, QUEST_VR_ROWS,
       QUEST_VR_ENGINE_ROW=QUEST_VR_ROWS, QUEST_VR_GRIP_ROW, QUEST_VR_HAPTICS_BACK,
       QUEST_VR_HIDDEN_RACES_ROW, QUEST_VR_VISUAL_REP_ROW, QUEST_VR_CATCHUP_ROW, QUEST_VR_WHINE_ROW, QUEST_VR_GPS_ROW,
       QUEST_VR_UHUD_ROW, QUEST_VR_DETAILED_MAP_ROW, QUEST_VR_SUBTITLES_ROW, QUEST_VR_STANCE_ROW, QUEST_VR_VINYLS_ROW, QUEST_VR_SPECIAL_VINYLS_ROW, QUEST_VR_SOUNDTRACK_ROW, QUEST_VR_LIGHTS_ROW, QUEST_VR_DAMAGE_ROW, QUEST_VR_REGIONAL_ROW,
       QUEST_VR_COPCHASE_ROW, QUEST_VR_COPCHASE_STYLE_ROW, QUEST_VR_COPCHASE_HEAT_ROW, QUEST_VR_COPCHASE_AGGRESSION_ROW, QUEST_VR_COPCHASE_LIMIT_ROW, QUEST_VR_COPCHASE_SPRINT_ROW,
       QUEST_VR_MODS_BACK, QUEST_VR_COCKPIT_FIRST,
       QUEST_VR_COCKPIT_RESET=QUEST_VR_COCKPIT_FIRST+QUEST_COCKPIT_OPTION_ROWS, QUEST_VR_COCKPIT_BACK, QUEST_VR_END };
enum { QUEST_VR_GAIN_LIMIT_DB=12 };
static const unsigned quest_mirror_widths[]={256,512,1024,2048,4096};
enum { QUEST_MIRROR_PRESETS=sizeof quest_mirror_widths/sizeof *quest_mirror_widths };
enum { QUEST_VR_RESET_MIRRORS=8, QUEST_VR_OPEN_PROFILES=16, QUEST_VR_OPEN_STANCE=32 };
static const unsigned quest_vr_resolutions[QUEST_VR_RESOLUTIONS][2]={
    {0,0},{1280,1344},{1680,1760},{2550,2640},{2940,3080},{3360,3520}
};
static inline QuestVRSettings quest_vr_defaults(void)
{ QuestVRSettings s={1,360,0,0,0,QUEST_WHEEL_RETURN_DEFAULT,2,QUEST_GUIDE_ALWAYS,{0},0,1,0,1,0,1,QUEST_MUSIC_AUTO,0,0,6};s.cockpit=quest_cockpit_defaults();s.copchase_style=1;s.copchase_heat=1;s.copchase_aggression=100;s.copchase_limit=10;return s; }
static inline float quest_vr_bound(float v,float lo,float hi,float fallback)
{ return isfinite(v)?fmaxf(lo,fminf(hi,v)):fallback; }
static inline void quest_vr_validate(QuestVRSettings *s)
{
    for(int i=0;i<QUEST_COCKPIT_OPTION_COUNT;++i)quest_option_set(&s->cockpit,i,quest_option_get(&s->cockpit,i));
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
    if(s->show_hidden_races!=0 && s->show_hidden_races!=1)s->show_hidden_races=0;
    if(s->visual_rep_remover!=0 && s->visual_rep_remover!=1)s->visual_rep_remover=0;
    if(s->online_cars<1 || s->online_cars>64)s->online_cars=6;
    if(s->career_catchup<0 || s->career_catchup>2)s->career_catchup=0;
    if(s->gearbox_whine!=0 && s->gearbox_whine!=1)s->gearbox_whine=0;
    if(s->gps_distance!=0 && s->gps_distance!=1)s->gps_distance=0;
    if(s->upscaled_hud!=0 && s->upscaled_hud!=1)s->upscaled_hud=0;
    if(s->detailed_map!=0 && s->detailed_map!=1)s->detailed_map=0;
    if(s->restored_soundtrack!=0 && s->restored_soundtrack!=1)s->restored_soundtrack=0;
    if(s->headlights<0 || s->headlights>2)s->headlights=0;
    if(s->crash_damage!=0 && s->crash_damage!=1)s->crash_damage=0;
    if(s->regional_cars!=0 && s->regional_cars!=1)s->regional_cars=0;
    if(s->copchase!=0 && s->copchase!=1)s->copchase=0;
    if(s->copchase_sprint!=0 && s->copchase_sprint!=1)s->copchase_sprint=0;
    if(s->copchase_style<0 || s->copchase_style>3)s->copchase_style=1;
    if(s->copchase_heat<1 || s->copchase_heat>5)s->copchase_heat=1;
    if(s->copchase_aggression<50 || s->copchase_aggression>200)s->copchase_aggression=100;
    if(s->copchase_limit<1 || s->copchase_limit>10)s->copchase_limit=10;
    if(s->special_vinyls!=0 && s->special_vinyls!=1)s->special_vinyls=0;
    if(s->hq_vinyls!=0 && s->hq_vinyls!=1)s->hq_vinyls=0;
    if(s->subtitles!=0 && s->subtitles!=1)s->subtitles=0;
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
                for(int i=0;i<QUEST_COCKPIT_OPTION_COUNT;++i)
                    if(!strcmp(key,quest_cockpit_options[i].key))quest_option_set(&s->cockpit,i,value);
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
                else if(!strcmp(key,"show_hidden_races") && (value==0 || value==1))s->show_hidden_races=(int)value;
                else if(!strcmp(key,"visual_rep_remover") && (value==0 || value==1))s->visual_rep_remover=(int)value;
                else if(!strcmp(key,"career_catchup") && value>=0 && value<=2 && value==floorf(value))s->career_catchup=(int)value;
                else if(!strcmp(key,"gearbox_whine") && (value==0 || value==1))s->gearbox_whine=(int)value;
                else if(!strcmp(key,"gps_distance") && (value==0 || value==1))s->gps_distance=(int)value;
                else if(!strcmp(key,"upscaled_hud") && (value==0 || value==1))s->upscaled_hud=(int)value;
                else if(!strcmp(key,"detailed_map") && (value==0 || value==1))s->detailed_map=(int)value;
                else if(!strcmp(key,"subtitles") && (value==0 || value==1))s->subtitles=(int)value;
                else if(!strcmp(key,"restored_soundtrack") && (value==0 || value==1))s->restored_soundtrack=(int)value;
                else if(!strcmp(key,"headlights") && value>=0 && value<=2 && value==floorf(value))s->headlights=(int)value;
                else if(!strcmp(key,"regional_cars") && (value==0 || value==1))s->regional_cars=(int)value;
                else if(!strcmp(key,"copchase") && (value==0 || value==1))s->copchase=(int)value;
                else if(!strcmp(key,"copchase_sprint") && (value==0 || value==1))s->copchase_sprint=(int)value;
                else if(!strcmp(key,"copchase_style") && value>=0 && value<=3 && value==floorf(value))s->copchase_style=(int)value;
                else if(!strcmp(key,"copchase_heat") && value>=1 && value<=5 && value==floorf(value))s->copchase_heat=(int)value;
                else if(!strcmp(key,"copchase_aggression") && value>=50 && value<=200 && value==floorf(value))s->copchase_aggression=(int)value;
                else if(!strcmp(key,"copchase_limit") && value>=1 && value<=10 && value==floorf(value))s->copchase_limit=(int)value;
                else if(!strcmp(key,"crash_damage") && (value==0 || value==1))s->crash_damage=(int)value;
                else if(!strcmp(key,"special_vinyls") && (value==0 || value==1))s->special_vinyls=(int)value;
                else if(!strcmp(key,"hq_vinyls") && (value==0 || value==1))s->hq_vinyls=(int)value;
                else if(!strcmp(key,"online_cars") && value>=1 && value<=64 && value==floorf(value))s->online_cars=(int)value;
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
    int n=snprintf(out,capacity,"version=1\nsteering_sensitivity=%.3f\nwheel_rotation=%.1f\nseat_height=%.3f\nseat_distance=%.3f\nresolution=%d\nwheel_return_speed=%.1f\n"
        "mirror_resolution=%d\nshift_guide=%d\nmap_x=%.3f\nmap_y=%.3f\nmap_z=%.3f\nmap_pitch=%.5f\nmap_yaw=%.5f\n"
        "engine_rumble=%d\ngrip_feedback=%d\nmaster_gain_db=%d\nled_strips=%d\nled_color=%d\nled_music_pulse=%d\nmusic_source=%d\nshow_hidden_races=%d\nvisual_rep_remover=%d\nonline_cars=%d\ncareer_catchup=%d\ngearbox_whine=%d\ngps_distance=%d\nupscaled_hud=%d\ndetailed_map=%d\nsubtitles=%d\nhq_vinyls=%d\nspecial_vinyls=%d\nrestored_soundtrack=%d\nheadlights=%d\ncrash_damage=%d\nregional_cars=%d\n",
        s->sensitivity,s->rotation,s->height,s->distance,s->resolution,s->return_speed,
        s->mirror_resolution,s->shift_guide,s->map.offset[0],s->map.offset[1],s->map.offset[2],s->map.pitch,s->map.yaw,
        s->engine_rumble,s->grip_feedback,s->master_gain_db,s->led_strips,s->led_color,s->led_music_pulse,s->music_source,s->show_hidden_races,s->visual_rep_remover,s->online_cars,s->career_catchup,s->gearbox_whine,s->gps_distance,s->upscaled_hud,s->detailed_map,s->subtitles,s->hq_vinyls,s->special_vinyls,s->restored_soundtrack,s->headlights,s->crash_damage,s->regional_cars);
    if(n<0 || (size_t)n>=capacity)return n;
    int cops=snprintf(out+n,capacity-n,"copchase=%d\ncopchase_style=%d\ncopchase_heat=%d\ncopchase_aggression=%d\ncopchase_limit=%d\ncopchase_sprint=%d\n",
        s->copchase,s->copchase_style,s->copchase_heat,s->copchase_aggression,s->copchase_limit,s->copchase_sprint);
    if(cops<0)return cops;n+=cops;if((size_t)n>=capacity)return n;
    for(int i=0;i<QUEST_COCKPIT_OPTION_COUNT;++i) {
        int added=snprintf(out+n,capacity-n,"%s=%.6g\n",quest_cockpit_options[i].key,quest_option_get(&s->cockpit,i));
        if(added<0)return added;n+=added;if((size_t)n>=capacity)return n;
    }
    return n;
}
static inline void quest_vr_adjust(QuestVRSettings *s,int row,int step)
{
    if(row>=QUEST_VR_COCKPIT_FIRST && row<QUEST_VR_COCKPIT_RESET)quest_cockpit_adjust(&s->cockpit,row-QUEST_VR_COCKPIT_FIRST,step);
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
    if(row==QUEST_VR_CATCHUP_ROW)s->career_catchup=(s->career_catchup+step+3)%3;
    if(row==QUEST_VR_COPCHASE_STYLE_ROW)s->copchase_style=(s->copchase_style+step+4)%4;
    if(row==QUEST_VR_COPCHASE_HEAT_ROW){s->copchase_heat+=step;if(s->copchase_heat<1)s->copchase_heat=1;if(s->copchase_heat>5)s->copchase_heat=5;}
    if(row==QUEST_VR_COPCHASE_AGGRESSION_ROW){s->copchase_aggression+=step*10;if(s->copchase_aggression<50)s->copchase_aggression=50;if(s->copchase_aggression>200)s->copchase_aggression=200;}
    if(row==QUEST_VR_COPCHASE_LIMIT_ROW){s->copchase_limit+=step;if(s->copchase_limit<1)s->copchase_limit=1;if(s->copchase_limit>10)s->copchase_limit=10;}
    if(row==QUEST_VR_GAIN_ROW)s->master_gain_db+=step;
    if(row==QUEST_VR_ONLINE_CARS_ROW){s->online_cars+=step;if(s->online_cars<1)s->online_cars=1;if(s->online_cars>64)s->online_cars=64;}
    quest_vr_validate(s);
}

enum {QUEST_COPCHASE_DATA_UNCHECKED,QUEST_COPCHASE_DATA_READY,QUEST_COPCHASE_DATA_MISSING};
typedef struct { int open,row,held,swallow; uint32_t repeat; int mirrors_centered,copchase_data; } QuestVRMenu;
/* Runtime availability is never serialized with user settings. */
static inline const char *quest_vr_copchase_data_hint(const QuestVRSettings *s,const QuestVRMenu *m)
{
    if(!s->copchase)return "COPCHASE IS OFF. ENABLE AND RESTART FOR POLICE PURSUITS.";
    if(m->copchase_data==QUEST_COPCHASE_DATA_MISSING)return "REINSTALL LOCAL COPCHASE DATA, THEN RESTART THE GAME.";
    if(m->copchase_data==QUEST_COPCHASE_DATA_UNCHECKED)return "SAVE AND RESTART TO LOAD COPCHASE DATA.";
    return "POLICE DATA READY. LOAD A SAVED CAREER FOR OFFLINE FREE ROAM.";
}
enum { QUEST_VR_UP=1,QUEST_VR_DOWN=2,QUEST_VR_LEFT=4,QUEST_VR_RIGHT=8,
       QUEST_VR_ACCEPT=16,QUEST_VR_BACK=32,QUEST_VR_OPEN=64 };
/* Return bit 1 on data change, bit 2 on closing (persist), bit 4 to consume
 * input. Closing must not also activate the game's underlying pause menu. */
static inline int quest_vr_menu_step(QuestVRMenu *m,QuestVRSettings *s,
                                     uint32_t now,int paused,int keys)
{
    if(!paused) {
        int save=m->open?2:0;
        int data=m->copchase_data;
        memset(m,0,sizeof *m);
        m->copchase_data=data;
        m->held=keys; /* X held before pausing is not a new settings press. */
        return save;
    }
    if(m->swallow) { if(!keys)m->swallow=0;m->held=keys;return 4; }
    int edge=keys&~m->held;m->held=keys;
    if(!m->open) {
        if(edge&QUEST_VR_OPEN) {m->open=1;m->mirrors_centered=0;m->repeat=now+350;return 4;}
        return 0;
    }
    int cockpit=m->row>=QUEST_VR_COCKPIT_FIRST;
    int mods=m->row>=QUEST_VR_HIDDEN_RACES_ROW && !cockpit;
    int haptics=m->row>=QUEST_VR_ENGINE_ROW && !mods && !cockpit;
    if(edge&QUEST_VR_BACK) {
        if(cockpit){m->row=QUEST_VR_COCKPIT_ROW;return 4;}
        if(haptics){m->row=QUEST_VR_HAPTICS_ROW;return 4;}
        if(mods){m->row=QUEST_VR_MODS_ROW;return 4;}
        m->open=0;m->swallow=1;return 6;
    }
    int nav=keys&15;
    if(nav && ((edge&15) || (int32_t)(now-m->repeat)>=0)) {
        m->repeat=now+((edge&15)?350:140);
        int first=cockpit?QUEST_VR_COCKPIT_FIRST:mods?QUEST_VR_HIDDEN_RACES_ROW:haptics?QUEST_VR_ENGINE_ROW:0;
        int last=cockpit?QUEST_VR_COCKPIT_BACK:mods?QUEST_VR_MODS_BACK:haptics?QUEST_VR_HAPTICS_BACK:QUEST_VR_SAVE_ROW;
        if(nav&QUEST_VR_UP)m->row=m->row==first?last:m->row-1;
        else if(nav&QUEST_VR_DOWN)m->row=m->row==last?first:m->row+1;
        else if((cockpit && m->row<QUEST_VR_COCKPIT_RESET) || (m->row<QUEST_VR_HAPTICS_ROW && m->row!=QUEST_VR_LED_ROW && m->row!=QUEST_VR_LED_PULSE_ROW) || m->row==QUEST_VR_ONLINE_CARS_ROW || m->row==QUEST_VR_CATCHUP_ROW || (m->row>=QUEST_VR_COPCHASE_STYLE_ROW && m->row<=QUEST_VR_COPCHASE_LIMIT_ROW)) {
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
    if(m->row==QUEST_VR_HIDDEN_RACES_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))) {
        s->show_hidden_races=!s->show_hidden_races;return 5;
    }
    if(m->row==QUEST_VR_VISUAL_REP_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))) {
        s->visual_rep_remover=!s->visual_rep_remover;return 5;
    }
    if(m->row==QUEST_VR_WHINE_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))) {
        s->gearbox_whine=!s->gearbox_whine;return 5;
    }
    if(m->row==QUEST_VR_GPS_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))) {
        s->gps_distance=!s->gps_distance;return 5;
    }
    if((m->row==QUEST_VR_UHUD_ROW || m->row==QUEST_VR_DETAILED_MAP_ROW) && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))) {
        int *toggle=m->row==QUEST_VR_UHUD_ROW?&s->upscaled_hud:&s->detailed_map;*toggle=!*toggle;return 5;
    }
    if(m->row==QUEST_VR_SUBTITLES_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))){s->subtitles=!s->subtitles;return 5;}
    if(m->row==QUEST_VR_VINYLS_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))){s->hq_vinyls=!s->hq_vinyls;return 5;}
    if(m->row==QUEST_VR_LIGHTS_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))){s->headlights=(s->headlights+((edge&QUEST_VR_LEFT)?2:1))%3;return 5;}
    if(m->row==QUEST_VR_REGIONAL_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))){s->regional_cars=!s->regional_cars;return 5;}
    if(m->row==QUEST_VR_COPCHASE_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))){s->copchase=!s->copchase;return 5;}
    if(m->row==QUEST_VR_COPCHASE_SPRINT_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))){
        if(!s->copchase || m->copchase_data!=QUEST_COPCHASE_DATA_READY)return 4;
        s->copchase_sprint=!s->copchase_sprint;return 5;
    }
    if(m->row==QUEST_VR_DAMAGE_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))){s->crash_damage=!s->crash_damage;return 5;}
    if(m->row==QUEST_VR_SPECIAL_VINYLS_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))){s->special_vinyls=!s->special_vinyls;return 5;}
    if(m->row==QUEST_VR_SOUNDTRACK_ROW && (edge&(QUEST_VR_LEFT|QUEST_VR_RIGHT|QUEST_VR_ACCEPT))){s->restored_soundtrack=!s->restored_soundtrack;return 5;}
    if(edge&QUEST_VR_ACCEPT) {
        if(m->row==QUEST_VR_COCKPIT_ROW){m->row=QUEST_VR_COCKPIT_FIRST;return 4;}
        if(m->row==QUEST_VR_COCKPIT_BACK){m->row=QUEST_VR_COCKPIT_ROW;return 4;}
        if(m->row==QUEST_VR_COCKPIT_RESET){s->cockpit.screen=quest_cockpit_defaults().screen;return 5;}
        if(cockpit && m->row<QUEST_VR_COCKPIT_RESET){quest_vr_adjust(s,m->row,1);return 5;}
        if(m->row==QUEST_VR_CATCHUP_ROW){s->career_catchup=(s->career_catchup+1)%3;return 5;}
        if(m->row>=QUEST_VR_COPCHASE_STYLE_ROW && m->row<=QUEST_VR_COPCHASE_LIMIT_ROW){quest_vr_adjust(s,m->row,1);return 5;}
        if(m->row==QUEST_VR_PROFILES_ROW)return 4|QUEST_VR_OPEN_PROFILES;
        if(m->row==QUEST_VR_STANCE_ROW)return 4|QUEST_VR_OPEN_STANCE;
        if(m->row==QUEST_VR_HAPTICS_ROW){m->row=QUEST_VR_ENGINE_ROW;return 4;}
        if(m->row==QUEST_VR_HAPTICS_BACK){m->row=QUEST_VR_HAPTICS_ROW;return 4;}
        if(m->row==QUEST_VR_MODS_ROW){m->row=QUEST_VR_HIDDEN_RACES_ROW;return 4;}
        if(m->row==QUEST_VR_MODS_BACK){m->row=QUEST_VR_MODS_ROW;return 4;}
        if(m->row==QUEST_VR_RESET_MIRRORS_ROW) {m->mirrors_centered=1;return 4|QUEST_VR_RESET_MIRRORS;}
        if(m->row==QUEST_VR_DEFAULTS_ROW) {*s=quest_vr_defaults();m->mirrors_centered=1;return 5|QUEST_VR_RESET_MIRRORS;}
        if(m->row==QUEST_VR_SAVE_ROW) {m->open=0;m->swallow=1;return 6;}
    }
    return 4;
}
