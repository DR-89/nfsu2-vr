#pragma once
#include "quest_vr_settings.h"

#include "quest_bitmap_font.h"

static inline void quest_vr_rect(uint32_t *pixels,int x,int y,int w,int h,uint32_t color)
{
    for(int j=y;j<y+h && j<768;++j)for(int i=x;i<x+w && i<1024;++i)
        if(i>=0 && j>=0)pixels[j*1024+i]=color;
}
static inline void quest_vr_draw_text(uint32_t *pixels,int x,int y,int scale,const char *text,uint32_t color,int preserve_case)
{
    for(;*text;++text,x+=6*scale) {
        char c=*text;if(!preserve_case && c>='a' && c<='z')c-=32;
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
static inline void quest_vr_text(uint32_t *pixels,int x,int y,int scale,const char *text,uint32_t color)
{ quest_vr_draw_text(pixels,x,y,scale,text,color,0); }
static inline void quest_vr_text_case(uint32_t *pixels,int x,int y,int scale,const char *text,uint32_t color)
{ quest_vr_draw_text(pixels,x,y,scale,text,color,1); }
static inline void quest_vr_scroll_indicator(uint32_t *pixels,int first,int visible,int total)
{
    const uint32_t accent=0xff4da3ffu,muted=0xff465566u;
    int above=first,below=total-first-visible;
    /* Reserve the right edge for a proportional thumb and both arrows.
     * This is a position indicator; navigation stays on the stick/D-pad. */
    quest_vr_rect(pixels,968,88,32,314,0xff1b2939u);
    for(int line=0;line<7;++line) {
        quest_vr_rect(pixels,984-line,94+line,1+2*line,2,above?accent:muted);
        quest_vr_rect(pixels,978+line,389+line,13-2*line,2,below?accent:muted);
    }
    const int track_y=114,track_h=260;
    int thumb_h=track_h*visible/total;
    if(thumb_h<24)thumb_h=24;
    int thumb_y=track_y+(track_h-thumb_h)*first/(total-visible);
    quest_vr_rect(pixels,979,track_y,10,track_h,muted);
    quest_vr_rect(pixels,975,thumb_y,18,thumb_h,accent);
    quest_vr_rect(pixels,978,thumb_y+thumb_h/2-3,12,2,0xffffffffu);
    quest_vr_rect(pixels,978,thumb_y+thumb_h/2+2,12,2,0xffffffffu);
    quest_vr_rect(pixels,24,410,976,36,0xff1b2939u);
    quest_vr_text(pixels,40,418,3,"UP / DOWN: SCROLL",0xffffffffu);
    char remaining[40];
    if(above && below)snprintf(remaining,sizeof remaining,"%d ABOVE / %d BELOW",above,below);
    else snprintf(remaining,sizeof remaining,"%d MORE %s",above?above:below,above?"ABOVE":"BELOW");
    quest_vr_text(pixels,960-(int)strlen(remaining)*18,418,3,remaining,accent);
}
static inline void quest_vr_panel(uint32_t *pixels,const QuestVRSettings *s,
                                  const QuestVRMenu *m,int saved,int restart_pending)
{
    quest_vr_rect(pixels,0,0,1024,768,0xff141b24u);
    quest_vr_rect(pixels,0,0,1024,7,0xff4da3ffu);
    if(!m->open) {
        quest_vr_text(pixels,44,26,4,"[X] VR SETTINGS",0xffffffffu);return;
    }
    int cockpit=m->row>=QUEST_VR_COCKPIT_FIRST;
    int mods=m->row>=QUEST_VR_HIDDEN_RACES_ROW && !cockpit;
    int haptics=m->row>=QUEST_VR_ENGINE_ROW && !mods && !cockpit;
    quest_vr_text(pixels,40,34,5,cockpit?"COCKPIT CONTROLS":mods?"COMMUNITY MODS":haptics?"ADVANCED HAPTICS / RUMBLE":"VR SETTINGS",0xffffffffu);
    static const char *const labels[]={"STEERING SENSITIVITY","WHEEL ROTATION","SEAT HEIGHT","SEAT DISTANCE","RENDER RESOLUTION","WHEEL RETURN SPEED",
        "MIRROR RESOLUTION","SHIFTER GUIDE","INTERIOR LED STRIPS","LED COLOR","PULSE TO MUSIC",
        "RADIO SOUNDTRACK","MASTER GAIN","ADVANCED HAPTICS / RUMBLE","COMMUNITY MODS","ONLINE PROFILES","ONLINE CARS SHOWN","RESET CAR MIRRORS","COCKPIT CONTROLS","RESET DEFAULTS","SAVE AND BACK",
        "ENGINE REV RUMBLE","GRIP FEEDBACK","BACK","SHOW HIDDEN RACES","VISUAL REPUTATION REMOVER","CAREER RUBBERBANDING","TRANSMISSION WHINE","GPS DISTANCE","UPSCALED HUD","DETAILED MAP","ENGLISH SUBTITLES","STANCE","HQ VINYLS / DOORLINES","SPECIAL VINYL CATEGORY","RESTORED SOUNDTRACK","HEADLIGHTS","CRASH DAMAGE","EUROPEAN CARS",
        "COPCHASE","PURSUIT STYLE","STARTING HEAT","POLICE AGGRESSION","MAX CHASERS","PURSUIT IN SPRINT","BACK"};
    int first=cockpit?QUEST_VR_COCKPIT_FIRST:mods?QUEST_VR_HIDDEN_RACES_ROW:haptics?QUEST_VR_ENGINE_ROW:0;
    int end=cockpit?QUEST_VR_END:mods?QUEST_VR_MODS_BACK+1:haptics?QUEST_VR_HAPTICS_BACK+1:QUEST_VR_ROWS;
    int scrolling=(mods || cockpit) && end-first>10;
    if(scrolling){int total=end-first,offset=m->row-first-9;if(offset<0)offset=0;
        first+=offset;if(end>first+10)end=first+10;
        char range[32];snprintf(range,sizeof range,"%d-%d / %d",offset+1,offset+end-first,total);
        quest_vr_text(pixels,780,46,2,range,0xffc8beb4u);
        quest_vr_scroll_indicator(pixels,offset,end-first,total);
    }
    for(int row=first;row<end;++row) {
        int y=91+(row-first)*((mods || cockpit)?32:haptics?54:26);char value[64]={0};
        if(row==m->row)quest_vr_rect(pixels,24,y-3,scrolling?936:976,25,0xff214171u);
        const char *label=cockpit?(row==QUEST_VR_COCKPIT_BACK?"BACK":row==QUEST_VR_COCKPIT_RESET?"RESET SCREEN":quest_cockpit_options[row-QUEST_VR_COCKPIT_FIRST].label):labels[row];
        quest_vr_text(pixels,40,y,3,label,0xffffffffu);
        if(cockpit && row<QUEST_VR_COCKPIT_RESET) {
            int i=row-QUEST_VR_COCKPIT_FIRST;const QuestCockpitOption *o=&quest_cockpit_options[i];
            float v=quest_option_get(&s->cockpit,i);
            if(o->integer)snprintf(value,sizeof value,"< %s >",o->choices[(int)v]);
            else {
                if(strstr(o->label,"(CM)") || strstr(o->label,"(%)"))v*=100;
                snprintf(value,sizeof value,"< %.2f >",v);
                float fraction=(quest_option_get(&s->cockpit,i)-o->low)/(o->high-o->low);
                quest_vr_rect(pixels,518,y+8,90,5,0xff465566u);
                quest_vr_rect(pixels,518,y+8,(int)(fraction*90),5,0xff4da3ffu);
            }
        }
        if(row==QUEST_VR_COCKPIT_ROW)strcpy(value,"[A] OPEN");
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
        if(row==QUEST_VR_MODS_ROW)strcpy(value,"[A] OPEN");
        if(row==QUEST_VR_PROFILES_ROW)strcpy(value,"[A] OPEN");
        if(row==QUEST_VR_ONLINE_CARS_ROW)snprintf(value,sizeof value,"< %d / 64 >",s->online_cars);
        if(row==QUEST_VR_HIDDEN_RACES_ROW)strcpy(value,s->show_hidden_races?"< ON >":"< OFF >");
        if(row==QUEST_VR_VISUAL_REP_ROW)strcpy(value,s->visual_rep_remover?"< ON >":"< OFF >");
        if(row==QUEST_VR_CATCHUP_ROW){const char *modes[]={"< ORIGINAL >","< NO BOOST >","< OFF >"};strcpy(value,modes[s->career_catchup]);}
        if(row==QUEST_VR_WHINE_ROW)strcpy(value,s->gearbox_whine?"< ON >":"< OFF >");
        if(row==QUEST_VR_GPS_ROW)strcpy(value,s->gps_distance?"< ON >":"< OFF >");
        if(row==QUEST_VR_UHUD_ROW)strcpy(value,s->upscaled_hud?"< ON >":"< OFF >");
        if(row==QUEST_VR_DETAILED_MAP_ROW)strcpy(value,s->detailed_map?"< ON >":"< OFF >");
        if(row==QUEST_VR_SUBTITLES_ROW)strcpy(value,s->subtitles?"< ON >":"< OFF >");
        if(row==QUEST_VR_VINYLS_ROW)strcpy(value,s->hq_vinyls?"< ON >":"< OFF >");
        if(row==QUEST_VR_LIGHTS_ROW){static const char *names[]={"< ORIGINAL >","< OFF >","< HIGH BEAM >"};strcpy(value,names[s->headlights]);}
        if(row==QUEST_VR_SPECIAL_VINYLS_ROW)strcpy(value,s->special_vinyls?"< ON >":"< OFF >");
        if(row==QUEST_VR_REGIONAL_ROW)strcpy(value,s->regional_cars?"< ON >":"< OFF >");
        if(row==QUEST_VR_DAMAGE_ROW)strcpy(value,s->crash_damage?"< ON >":"< OFF >");
        if(row==QUEST_VR_COPCHASE_ROW)strcpy(value,!s->copchase?"< OFF >":
            m->copchase_data==QUEST_COPCHASE_DATA_MISSING?"< DATA MISSING >":"< ON >");
        if(row==QUEST_VR_COPCHASE_STYLE_ROW){static const char *const styles[]={"SLOW","MOST WANTED","ULTIMATE","INSANE"};snprintf(value,sizeof value,"< %s >",styles[s->copchase_style]);}
        if(row==QUEST_VR_COPCHASE_HEAT_ROW)snprintf(value,sizeof value,"< %d >",s->copchase_heat);
        if(row==QUEST_VR_COPCHASE_AGGRESSION_ROW)snprintf(value,sizeof value,"< %d%% >",s->copchase_aggression);
        if(row==QUEST_VR_COPCHASE_LIMIT_ROW)snprintf(value,sizeof value,"< %d >",s->copchase_limit);
        if(row==QUEST_VR_COPCHASE_SPRINT_ROW)strcpy(value,!s->copchase || m->copchase_data!=QUEST_COPCHASE_DATA_READY?"< UNAVAILABLE >":s->copchase_sprint?"< ON >":"< OFF >");
        if(row==QUEST_VR_SOUNDTRACK_ROW)strcpy(value,s->restored_soundtrack?"< ON >":"< OFF >");
        if(row==QUEST_VR_STANCE_ROW)strcpy(value,"[A] OPEN");
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
    if(cockpit)hint="GESTURES NEED NO GRIP. RETURN TO CENTER BETWEEN FLICKS.";
    if(cockpit && m->row>=QUEST_VR_COCKPIT_FIRST+14)hint="HOLD SCREEN: RIGHT STICK UP/DOWN RESIZES. WRIST TILTS IT.";
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
    if(m->row==QUEST_VR_MODS_ROW)hint="OPTIONAL GAMEPLAY AND SOUND CHANGES.";
    if(m->row==QUEST_VR_MODS_ROW && s->copchase && m->copchase_data==QUEST_COPCHASE_DATA_MISSING)
        hint="COPCHASE DATA MISSING OR INVALID. OPEN COMMUNITY MODS.";
    if(m->row==QUEST_VR_STANCE_ROW)hint="ADJUST YOUR CAR'S CAMBER, WHEEL OFFSETS AND TIRE WIDTH.";
    if(m->row==QUEST_VR_GPS_ROW)hint="STRAIGHT-LINE DISTANCE BELOW THE VR MAP. USES SPEEDOMETER UNITS.";
    if(m->row==QUEST_VR_UHUD_ROW || m->row==QUEST_VR_DETAILED_MAP_ROW){
        quest_vr_text(pixels,40,460,3,m->row==QUEST_VR_UHUD_ROW?"SHARPER HUD AND MENU TEXTURES BY LUNICAURA106.":"DETAILED ROADS AND SHORTCUTS BY JEANSBIG.",0xffc8beb4u);
        quest_vr_text(pixels,40,502,3,"HIGHER RESOLUTION USES MORE TEXTURE MEMORY.",0xffc8beb4u);
        hint="SAVE AND RESTART THE GAME TO APPLY CHANGES.";
    }
    if(m->row==QUEST_VR_LIGHTS_ROW)hint="LAMPS AND ROAD BEAM. APPLIES NOW; SHARED IN ONLINE FREE ROAM.";
    if(m->row==QUEST_VR_SPECIAL_VINYLS_ROW)hint="REOPEN THE VINYL SHOP. ORIGINAL UNLOCKS AND PRICES STILL APPLY.";
    if(m->row==QUEST_VR_VINYLS_ROW){
        quest_vr_text(pixels,40,460,3,"1024 VINYLS AND DOORLINES BY ADRIENSHADOW.",0xffc8beb4u);
        quest_vr_text(pixels,40,502,3,"OPTIONAL PACKS. USES MORE TEXTURE MEMORY.",0xffc8beb4u);
        hint="SAVE AND RESTART THE GAME TO APPLY CHANGES.";
    }
    if(m->row==QUEST_VR_REGIONAL_ROW){
        quest_vr_text(pixels,40,460,3,"VAUXHALL CORSA AND PEUGEOT 106. OPTIONAL CAR PACK.",0xffc8beb4u);
        quest_vr_text(pixels,40,502,3,"FOR CAR SELECTION AND ONLINE FREE ROAM.",0xffc8beb4u);
        hint="SAVE AND RESTART THE GAME TO APPLY CHANGES.";
    }
    if(m->row==QUEST_VR_DAMAGE_ROW){
        quest_vr_text(pixels,40,460,3,"DENTS, SCRATCHES AND LOOSE PARTS BY FRS MODS.",0xffc8beb4u);
        quest_vr_text(pixels,40,502,3,"VISUAL DAMAGE. SHARED WITH NEARBY FREE ROAM PLAYERS.",0xffc8beb4u);
        hint="SAVE AND RESTART THE GAME TO APPLY CHANGES.";
    }
    if(m->row>=QUEST_VR_COPCHASE_ROW && m->row<=QUEST_VR_COPCHASE_SPRINT_ROW){
        quest_vr_text(pixels,40,460,3,"COPCHASE BY VITORINI MODS.",0xffc8beb4u);
        quest_vr_text(pixels,40,502,3,"POLICE PURSUITS IN OFFLINE CAREER FREE ROAM.",0xffc8beb4u);
        if(s->copchase && m->copchase_data==QUEST_COPCHASE_DATA_MISSING)
            quest_vr_text(pixels,40,544,3,"COPCHASE DATA MISSING OR INVALID. REINSTALL PACK.",0xff8888ffu);
        hint=m->row==QUEST_VR_COPCHASE_ROW || (s->copchase && m->copchase_data!=QUEST_COPCHASE_DATA_READY)?quest_vr_copchase_data_hint(s,m):
             m->row==QUEST_VR_COPCHASE_LIMIT_ROW?"UPPER LIMIT. HEAT AND NEARBY PATROLS DETERMINE THE ACTIVE COUNT.":
             "ADJUST PURSUIT DIFFICULTY WITH LEFT / RIGHT.";
        if(m->row==QUEST_VR_COPCHASE_SPRINT_ROW){
            quest_vr_rect(pixels,24,492,976,100,0xff141b24u);
            quest_vr_text(pixels,40,502,3,"QUICK RACE: SELECT SPRINT, THEN YOUR TRACK AND CAR.",0xffc8beb4u);
            quest_vr_text(pixels,40,544,3,"OFFLINE ONLY. BUSTED ENDS THE RACE WITHOUT A FINE.",0xffc8beb4u);
            hint=!s->copchase || m->copchase_data!=QUEST_COPCHASE_DATA_READY?quest_vr_copchase_data_hint(s,m):
                s->copchase_style==0?"APPLIES TO YOUR NEXT SPRINT. SLOW USES MOST WANTED.":"APPLIES TO YOUR NEXT SPRINT. USES YOUR STYLE AND HEAT.";
        }
    }
    if(m->row==QUEST_VR_SOUNDTRACK_ROW){
        quest_vr_text(pixels,40,460,3,"UNCENSORED AND RESTORED SONGS BY XGMVX.",0xffc8beb4u);
        quest_vr_text(pixels,40,502,3,"OPTIONAL PACK. SELECT ORIGINAL UNDER RADIO SOUNDTRACK.",0xffc8beb4u);
        hint="SAVE AND RESTART THE GAME TO APPLY CHANGES.";
    }
    if(m->row==QUEST_VR_SUBTITLES_ROW){
        quest_vr_text(pixels,40,460,3,"COMPLETE MOVIE TEXTS BY VILESTUFF.",0xffc8beb4u);
        quest_vr_text(pixels,40,502,3,"ORIGINAL MOVIE TIMING AND SCREEN PLACEMENT.",0xffc8beb4u);
        hint="SAVE AND RESTART THE GAME TO APPLY CHANGES.";
    }
    if(m->row==QUEST_VR_PROFILES_ROW)hint="CREATE AND SELECT LOCAL PROFILES FOR NFSU2.XYZ.";
    if(m->row==QUEST_VR_ONLINE_CARS_ROW)hint="APPLIES NOW. MORE CARS USE MORE MEMORY AND MAY LOWER FRAME RATE.";
    if(m->row==QUEST_VR_HIDDEN_RACES_ROW) {
        quest_vr_text(pixels,40,460,3,"SHOW THE 33 HIDDEN RACES ON THE MAP.",0xffc8beb4u);
        quest_vr_text(pixels,40,502,3,"BASED ON MAXHWOY'S SHOW HIDDEN RACES MOD.",0xffc8beb4u);
        hint="SAVE AND RESTART THE GAME TO APPLY CHANGES.";
    }
    if(m->row==QUEST_VR_VISUAL_REP_ROW) {
        quest_vr_text(pixels,40,460,3,"CAREER STARS FOLLOW YOUR STAGE.",0xffc8beb4u);
        quest_vr_text(pixels,40,502,3,"CHOOSE CAR PARTS FOR THE LOOK YOU WANT.",0xffc8beb4u);
        quest_vr_text(pixels,40,544,3,"BASED ON THELINK2012'S MOD.",0xffc8beb4u);
        hint="SAVE AND RESTART THE GAME TO APPLY CHANGES.";
    }
    if(m->row==QUEST_VR_CATCHUP_ROW){
        quest_vr_text(pixels,40,460,3,"NO BOOST: OPPONENTS AHEAD STILL WAIT.",0xffc8beb4u);
        quest_vr_text(pixels,40,502,3,"OFF: NO WAITING AND NO EXTRA POWER.",0xffc8beb4u);
        quest_vr_text(pixels,40,544,3,"APPLIES TO CAREER RACES ONLY.",0xffc8beb4u);
        hint="SAVE AND RESTART THE GAME TO APPLY CHANGES.";
    }
    if(m->row==QUEST_VR_WHINE_ROW){
        quest_vr_text(pixels,40,502,3,"ROPI'S TRANSMISSION WHINE, WITH GEAR SHIFTS.",0xffc8beb4u);
        quest_vr_text(pixels,40,544,3,"USES THE GAME'S MASTER AND ENGINE VOLUMES.",0xffc8beb4u);
        hint="APPLIES NOW. HEARD ON YOUR OWN CAR.";
    }
    if(restart_pending && m->row==QUEST_VR_SAVE_ROW)hint="SAVE AND RESTART THE GAME TO APPLY PENDING CHANGES.";
    quest_vr_text(pixels,40,644,2,hint,0xffffd79fu);
    quest_vr_text(pixels,40,686,3,"STICK / D-PAD: SELECT + ADJUST   A: SELECT",0xffffffffu);
    const char *back=(haptics || mods || cockpit)?"B: BACK TO VR SETTINGS":"B: SAVE AND BACK TO PAUSE MENU";
    quest_vr_text(pixels,40,726,2,saved<0?"SAVE FAILED - CHECK FREE SPACE. B: BACK":back,saved<0?0xff8888ffu:0xffc8beb4u);
}
