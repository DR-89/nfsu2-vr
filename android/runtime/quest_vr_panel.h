#pragma once
#include "quest_vr_settings.h"

#include "quest_bitmap_font.h"

static inline void quest_vr_rect(uint32_t *pixels,int x,int y,int w,int h,uint32_t color)
{
    for(int j=y;j<y+h && j<768;++j)for(int i=x;i<x+w && i<1024;++i)
        if(i>=0 && j>=0)pixels[j*1024+i]=color;
}
static inline void quest_vr_text(uint32_t *pixels,int x,int y,int scale,const char *text,uint32_t color)
{
    for(;*text;++text,x+=6*scale) {
        char c=*text;if(c>='a' && c<='z')c-=32;
        const char *rows=NULL;
        for(unsigned i=0;i<sizeof quest_vr_font/sizeof *quest_vr_font;++i)
            if(quest_vr_font[i].c==c){rows=quest_vr_font[i].rows;break;}
        if(!rows)continue;
        for(int j=0;j<7;++j) {
            char hex[3]={rows[j*2],rows[j*2+1],0};unsigned bits=(unsigned)strtoul(hex,NULL,16);
            for(int i=0;i<5;++i)if(bits&(1u<<(4-i)))quest_vr_rect(pixels,x+i*scale,y+j*scale,scale,scale,color);
        }
    }
}
static inline void quest_vr_panel(uint32_t *pixels,const QuestVRSettings *s,
                                  const QuestVRMenu *m,int saved,int resolution_pending)
{
    quest_vr_rect(pixels,0,0,1024,768,0xff141b24u);
    quest_vr_rect(pixels,0,0,1024,7,0xff4da3ffu);
    if(!m->open) {
        quest_vr_text(pixels,44,26,4,"[X] VR SETTINGS",0xffffffffu);return;
    }
    int haptics=m->row>=QUEST_VR_ENGINE_ROW;
    quest_vr_text(pixels,40,34,5,haptics?"ADVANCED HAPTICS / RUMBLE":"VR SETTINGS",0xffffffffu);
    static const char *const labels[]={"STEERING SENSITIVITY","WHEEL ROTATION","SEAT HEIGHT","SEAT DISTANCE","RENDER RESOLUTION","WHEEL RETURN SPEED",
        "MIRROR RESOLUTION","H-PATTERN GUIDE","INTERIOR LED STRIPS","LED COLOR","PULSE TO MUSIC",
        "RADIO SOUNDTRACK","MASTER GAIN","ADVANCED HAPTICS / RUMBLE","RESET CAR MIRRORS","RESET DEFAULTS","SAVE AND BACK",
        "ENGINE REV RUMBLE","GRIP FEEDBACK","BACK"};
    int first=haptics?QUEST_VR_ENGINE_ROW:0,end=haptics?QUEST_VR_END:QUEST_VR_ROWS;
    for(int row=first;row<end;++row) {
        int y=91+(row-first)*(haptics?54:32);char value[64]={0};
        if(row==m->row)quest_vr_rect(pixels,24,y-3,976,25,0xff214171u);
        quest_vr_text(pixels,40,y,3,labels[row],0xffffffffu);
        if(row==0)snprintf(value,sizeof value,"< %d%% >",(int)lrintf(s->sensitivity*100));
        if(row==1)snprintf(value,sizeof value,"< %d DEG >",(int)lrintf(s->rotation));
        if(row==2)snprintf(value,sizeof value,"< %+.0f CM >",s->height*100);
        if(row==3)snprintf(value,sizeof value,"< %+.0f CM >",s->distance*100);
        if(row==4) {
            if(!s->resolution)strcpy(value,"< DEFAULT >");
            else snprintf(value,sizeof value,"< %uX%u >",quest_vr_resolutions[s->resolution][0],quest_vr_resolutions[s->resolution][1]);
        }
        if(row==QUEST_VR_RESET_MIRRORS_ROW && m->mirrors_centered)strcpy(value,"CENTERED");
        if(row==QUEST_VR_MIRROR_ROW) {
            unsigned w=quest_mirror_widths[s->mirror_resolution];
            snprintf(value,sizeof value,"< %uX%u >",w,w/4);
        }
        if(row==QUEST_VR_GUIDE_ROW) {
            static const char *const modes[QUEST_GUIDE_MODES]={"< OFF >","< ALWAYS >","< NEAR SHIFTER >"};
            strcpy(value,modes[s->shift_guide]);
        }
        if(row==QUEST_VR_LED_ROW)strcpy(value,s->led_strips?"< ON >":"< OFF >");
        if(row==QUEST_VR_LED_PULSE_ROW)strcpy(value,s->led_music_pulse?"< ON >":"< OFF >");
        if(row==QUEST_VR_MUSIC_ROW) {
            static const char *const modes[QUEST_MUSIC_MODES]={"< AUTO >","< CUSTOM ONLY >","< EA TRAX ONLY >"};
            strcpy(value,modes[s->music_source]);
        }
        if(row==QUEST_VR_LED_COLOR_ROW) {
            snprintf(value,sizeof value,"< %s >",quest_led_colors[s->led_color].name);
            quest_vr_rect(pixels,576,y+3,27,15,0xff000000u|quest_led_colors[s->led_color].rgb);
        }
        if(row==QUEST_VR_GAIN_ROW)
            snprintf(value,sizeof value,s->master_gain_db?"< %+d DB >":"< %d DB >",s->master_gain_db);
        if(row==QUEST_VR_HAPTICS_ROW)strcpy(value,"[A] OPEN");
        if(row==QUEST_VR_ENGINE_ROW)strcpy(value,s->engine_rumble?"< ON >":"< OFF >");
        if(row==QUEST_VR_GRIP_ROW)strcpy(value,s->grip_feedback?"< ON >":"< OFF >");
        if(row==QUEST_VR_RETURN_ROW) {
            int width=(int)lrintf(184*s->return_speed/QUEST_WHEEL_RETURN_MAX);
            quest_vr_rect(pixels,518,y+8,184,5,0xff465566u);
            quest_vr_rect(pixels,518,y+8,width,5,0xff4da3ffu);
            quest_vr_rect(pixels,514+width,y,8,21,0xffffffffu);
            if(s->return_speed>0)snprintf(value,sizeof value,"%.0f DEG/S",s->return_speed);
            else strcpy(value,"OFF");
        }
        quest_vr_text(pixels,row==QUEST_VR_RETURN_ROW?734:620,y,3,value,0xffffd79fu);
    }
    const char *hint="";
    if(m->row<2)hint="ROTATION IS TOTAL LOCK-TO-LOCK. HIGHER SENSITIVITY = QUICKER TURNS.";
    if(m->row==2 || m->row==3)hint="EACH STEP MOVES THE SEAT ONE CENTIMETER.";
    if(m->row==4)hint="EYE RESOLUTION CHANGES TAKE EFFECT ON THE NEXT GAME START.";
    if(m->row==QUEST_VR_RETURN_ROW)hint="SPEED AT WHICH THE RELEASED WHEEL RETURNS TO CENTER.";
    if(m->row==QUEST_VR_MIRROR_ROW)hint="ALL THREE MIRRORS SHARE THIS IMAGE. LOWER SIZES USE FEWER PIXELS.";
    if(m->row==QUEST_VR_GUIDE_ROW)hint="NEAR SHIFTER SHOWS THE GUIDE WHEN EITHER HAND IS IN GRABBING RANGE.";
    if(m->row==QUEST_VR_LED_ROW)hint="LIGHT THE DASH, SHIFTER AND CABIN TRIM.";
    if(m->row==QUEST_VR_LED_PULSE_ROW)hint="TURN OFF FOR STEADY LIGHT, EVEN WHEN THE RADIO IS SILENT.";
    if(m->row==QUEST_VR_GAIN_ROW)hint="ADJUST ALL GAME AUDIO IN 1 DB STEPS.";
    if(m->row==QUEST_VR_MUSIC_ROW) {
        static const char *const hints[QUEST_MUSIC_MODES]={
            "USE CUSTOM TRACKS IN DRIVING AND REPLAYS. KEEP EA TRAX IN MENUS.",
            "MUTE EA TRAX EVERYWHERE WHEN PLAYABLE CUSTOM TRACKS EXIST.",
            "USE THE ORIGINAL SOUNDTRACK EVEN WHEN CUSTOM TRACKS EXIST."};
        hint=hints[s->music_source];
    }
    if(m->row==QUEST_VR_ENGINE_ROW)hint="VIBRATION BUILDS WITH ENGINE RPM AND THROTTLE.";
    if(m->row==QUEST_VR_GRIP_ROW)hint="ONLY HANDS HOLDING THE WHEEL FEEL ENGINE RUMBLE.";
    if(resolution_pending && m->row==QUEST_VR_SAVE_ROW)hint="RESTART THE GAME TO APPLY YOUR EYE RESOLUTION.";
    quest_vr_text(pixels,40,644,2,hint,0xffffd79fu);
    quest_vr_text(pixels,40,686,3,"STICK / D-PAD: SELECT + ADJUST   A: SELECT",0xffffffffu);
    const char *back=haptics?"B: BACK TO VR SETTINGS":"B: SAVE AND BACK TO PAUSE MENU";
    quest_vr_text(pixels,40,726,2,saved<0?"SAVE FAILED - CHECK FREE SPACE. B: BACK":back,saved<0?0xff8888ffu:0xffc8beb4u);
}
