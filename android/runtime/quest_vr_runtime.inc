/* Included by quest_xr.c. All UI, settings I/O and gamepad sampling belong to
 * the OpenXR/render thread; the guest receives only locked snapshots. */
#include "quest_vr_panel.h"
#include "quest_xyz_profiles.h"
#include "quest_xyz_races.h"
#include "vr_music.h"
#include "vr_whine.h"
#include "apu_xaudio2.h"
#include "quest_gamepad.h"
#include "quest_gps.h"
#include "quest_copchase_panel.h"
#include "quest_cockpit_fit.h"
#ifdef NFSU2_PCVR
#include <windows.h>
#endif
static QuestVRMenu vr_menu;
static QuestXYZProfiles vr_xyz_profiles;
static QuestXYZRaces vr_xyz_races;
static int vr_paused,vr_save_status,vr_start_resolution;
static int vr_start_hidden_races;
static int vr_start_visual_rep;
static int vr_start_catchup;
static int vr_start_uhud,vr_start_detailed_map,vr_start_subtitles,vr_start_vinyls;
static int vr_gps_enabled,vr_gps_metric;
static float vr_gps_metres=-1;
static uint32_t vr_gps_tick;
static QuestCopChaseHUD vr_copchase_hud;
static QuestCopChaseScene vr_copchase_scene;
static float vr_copchase_view[16];
static uint32_t vr_copchase_scene_tick,vr_copchase_view_tick,vr_copchase_ready_tick;
static int vr_copchase_scene_ready;
void quest_xr_copchase_scene_publish(const QuestCopChaseScene *scene)
{
    if(!scene)return;pthread_mutex_lock(&input_lock);vr_copchase_scene=*scene;vr_copchase_scene_tick=SDL_GetTicks();pthread_mutex_unlock(&input_lock);
}
void quest_xr_copchase_camera(const float view[16])
{
    if(!view)return;pthread_mutex_lock(&input_lock);memcpy(vr_copchase_view,view,64);vr_copchase_view_tick=SDL_GetTicks();pthread_mutex_unlock(&input_lock);
}
int quest_xr_copchase_scene(QuestCopChaseScene *scene,float view[16])
{
    if(!scene || !view)return 0;pthread_mutex_lock(&input_lock);uint32_t now=SDL_GetTicks();
    *scene=vr_copchase_scene;memcpy(view,vr_copchase_view,64);
    int fresh=now-vr_copchase_scene_tick<1000 && now-vr_copchase_view_tick<1000;
    pthread_mutex_unlock(&input_lock);return fresh;
}
void quest_xr_copchase_scene_available(int available)
{
    pthread_mutex_lock(&input_lock);vr_copchase_scene_ready=available;vr_copchase_ready_tick=SDL_GetTicks();pthread_mutex_unlock(&input_lock);
}
int quest_xr_copchase_scene_ready(void)
{
    pthread_mutex_lock(&input_lock);int ready=vr_copchase_scene_ready && SDL_GetTicks()-vr_copchase_ready_tick<1000;pthread_mutex_unlock(&input_lock);return ready;
}
static uint32_t vr_copchase_tick,vr_copchase_notice_until;
static QuestCopChaseBust vr_copchase_bust,vr_copchase_bust_view;
static QuestCopChaseConfirm vr_copchase_confirm;
static uint64_t vr_copchase_confirm_high,vr_copchase_confirm_low;
static int vr_copchase_bust_visible;
int quest_xr_copchase_bust_visible(void)
{
    pthread_mutex_lock(&input_lock);int visible=vr_copchase_bust_visible;pthread_mutex_unlock(&input_lock);return visible;
}
void quest_xr_copchase_bust_publish(const QuestCopChaseBust *state)
{
    if(!state)return;pthread_mutex_lock(&input_lock);vr_copchase_bust=*state;
    pthread_mutex_unlock(&input_lock);
}
int quest_xr_copchase_bust_confirm(uint64_t *high,uint64_t *low)
{
    if(!high || !low)return 0;pthread_mutex_lock(&input_lock);
    *high=vr_copchase_confirm_high;*low=vr_copchase_confirm_low;
    vr_copchase_confirm_high=vr_copchase_confirm_low=0;pthread_mutex_unlock(&input_lock);
    return *high && *low;
}
void quest_xr_copchase_publish(const QuestCopChaseHUD *state)
{
    if(!state)return;uint32_t now=SDL_GetTicks();
    pthread_mutex_lock(&input_lock);
    if(state->notice){vr_copchase_hud=*state;vr_copchase_notice_until=now+8000;}
    else if(state->phase || !vr_copchase_hud.notice || (int32_t)(now-vr_copchase_notice_until)>=0)
        vr_copchase_hud=*state;
    vr_copchase_tick=now;pthread_mutex_unlock(&input_lock);
}
void quest_xr_copchase_hud(QuestCopChaseHUD *state)
{
    if(!state)return;pthread_mutex_lock(&input_lock);
    *state=vr_copchase_hud;if(SDL_GetTicks()-vr_copchase_tick>1000)memset(state,0,sizeof *state);
    pthread_mutex_unlock(&input_lock);
}
int quest_xr_gps_enabled(void)
{
    pthread_mutex_lock(&input_lock);int enabled=vr_gps_enabled;pthread_mutex_unlock(&input_lock);return enabled;
}
void quest_xr_gps_publish(float metres,int metric)
{
    pthread_mutex_lock(&input_lock);vr_gps_metres=metres;vr_gps_metric=metric;vr_gps_tick=SDL_GetTicks();pthread_mutex_unlock(&input_lock);
}
void quest_xr_gps_text(char text[24])
{
    pthread_mutex_lock(&input_lock);
    float metres=vr_gps_enabled && SDL_GetTicks()-vr_gps_tick<500?vr_gps_metres:-1;
    int metric=vr_gps_metric;pthread_mutex_unlock(&input_lock);
    quest_gps_format(text,metres,metric);
}
/* Immutable once startup settings are loaded, before the guest thread starts. */
int quest_xr_hidden_races(void) {return vr_start_hidden_races;}
int quest_xr_visual_rep_remover(void) {return vr_start_visual_rep;}
int quest_xr_career_catchup(void) {return vr_start_catchup;}
int quest_xr_upscaled_hud(void) {return vr_start_uhud;}
int quest_xr_detailed_map(void) {return vr_start_detailed_map;}
int quest_xr_subtitles(void) {return vr_start_subtitles;}
int quest_xr_hq_vinyls(void) {return vr_start_vinyls;}
static int vr_start_soundtrack,vr_start_damage,vr_start_regional;
static int vr_start_copchase,vr_copchase_options[5],vr_copchase_sprint;
static int vr_copchase_data;
void quest_xr_copchase_data_status(int checked,int available)
{
    pthread_mutex_lock(&input_lock);
    vr_copchase_data=available?QUEST_COPCHASE_DATA_READY:checked?QUEST_COPCHASE_DATA_MISSING:QUEST_COPCHASE_DATA_UNCHECKED;
    pthread_mutex_unlock(&input_lock);
}
int quest_xr_copchase_at_start(void) {return vr_start_copchase;}
int quest_xr_copchase_sprint(void)
{
    pthread_mutex_lock(&input_lock);
    int enabled=vr_start_copchase && vr_copchase_options[0] && vr_copchase_data==QUEST_COPCHASE_DATA_READY && vr_copchase_sprint;
    pthread_mutex_unlock(&input_lock);return enabled;
}
void quest_xr_copchase_options(int values[5])
{
    if(!values)return;
    pthread_mutex_lock(&input_lock);memcpy(values,vr_copchase_options,sizeof vr_copchase_options);pthread_mutex_unlock(&input_lock);
}
int quest_xr_regional_cars(void) {return vr_start_regional;}
int quest_xr_crash_damage(void) {return vr_start_damage;}
int quest_xr_headlights(void) {return vr_settings.headlights;}
int quest_xr_special_vinyls(void) {return vr_settings.special_vinyls;}
int quest_xr_restored_soundtrack(void) {return vr_start_soundtrack;}
static uint32_t vr_save_tick;
static char vr_settings_path[4096];
static QuestSwapchain vr_panel_swap;
static VkBuffer vr_panel_buffer;
static VkDeviceMemory vr_panel_memory;
static uint32_t *vr_panel_pixels;
static VkDeviceSize vr_panel_stride=1024u*768u*4u;
static int64_t vr_panel_format;
static unsigned vr_panel_revision=1;
static unsigned *vr_panel_uploaded;
static SDL_GameController *vr_gamepad;
static uint32_t vr_gamepad_scan;
static int vr_gamepad_selected;
static int create_swapchain(QuestSwapchain *,int64_t,uint32_t,uint32_t);

#include "quest_stance.h"
static QuestStanceMenu vr_stance;
static void vr_publish_settings(void)
{
    xa2_set_output_gain(powf(10.f,vr_settings.master_gain_db/20.f));
    vr_music_set_source(vr_settings.music_source);
    vr_whine_enable(vr_settings.gearbox_whine);
    pthread_mutex_lock(&input_lock);vr_gps_enabled=vr_settings.gps_distance;pthread_mutex_unlock(&input_lock);
    pthread_mutex_lock(&input_lock);
    vr_copchase_options[0]=vr_settings.copchase;vr_copchase_options[1]=vr_settings.copchase_style;
    vr_copchase_options[2]=vr_settings.copchase_heat;vr_copchase_options[3]=vr_settings.copchase_aggression;
    vr_copchase_options[4]=vr_settings.copchase_limit;
    vr_copchase_sprint=vr_settings.copchase_sprint;
    pthread_mutex_unlock(&input_lock);
    xyz_online_set_car_limit((unsigned)vr_settings.online_cars);
    wheel_grip.return_speed=vr_settings.return_speed;
    wheel_grip.deadzone=vr_settings.cockpit.steering_deadzone;
    float limit=vr_settings.rotation*QUEST_WHEEL_PI/360.0f;
    if(wheel_configured && (wheel_grip.limit!=limit || wheel_grip.sensitivity!=vr_settings.sensitivity)) {
        quest_wheel_reset(&wheel_grip);
        wheel_grip.limit=limit;wheel_grip.sensitivity=vr_settings.sensitivity;
    }
    ++vr_panel_revision;
}
void quest_xr_pause_publish(int paused)
{
    pthread_mutex_lock(&input_lock);vr_paused=paused;pthread_mutex_unlock(&input_lock);
}
static void vr_load_settings(void)
{
    vr_settings=quest_vr_defaults();
    char *folder=SDL_GetPrefPath("OpenSourceDevDirk","NFSU2VR");
    xyz_profiles_init(folder);
    xyz_stance_init(folder);
    if(folder) {
        snprintf(vr_settings_path,sizeof vr_settings_path,"%svr-settings.ini",folder);SDL_free(folder);
        SDL_RWops *f=SDL_RWFromFile(vr_settings_path,"rb");
        if(f) {
            char text[8192];size_t n=SDL_RWread(f,text,1,sizeof text-1);text[n]=0;
            SDL_RWclose(f);quest_vr_parse(&vr_settings,text);
            for(const char *line=text;*line;) {
                char name[32];float y,p,sy,sp;int choice;
                if(sscanf(line,"race_camera=%d",&choice)==1 && choice>=0 && choice<=6)
                    camera_choice=choice;
                if(sscanf(line,"mirror_%31[A-Z0-9]=%f,%f,%f,%f",name,&y,&p,&sy,&sp)==5 &&
                   isfinite(y) && isfinite(p) && isfinite(sy) && isfinite(sp)) {
                    int i=quest_cockpit_vehicle_index(name);
                    if(!strcmp(name,quest_cockpit_vehicle(i)->name)) {
                        mirror_settings[i]=(QuestMirrorSetting){quest_mirror_trim(y,p),quest_mirror_trim(sy,sp)};
                        mirror_known[i]=1;
                    }
                }
                line+=strcspn(line,"\n");if(*line)++line;
            }
        }
    }
    const char *hidden_races=getenv("NFSU2_SHOW_HIDDEN_RACES");
    if(hidden_races && (!strcmp(hidden_races,"0") || !strcmp(hidden_races,"1")))
        vr_settings.show_hidden_races=*hidden_races=='1';
    vr_start_resolution=vr_settings.resolution;
    vr_start_hidden_races=vr_settings.show_hidden_races;
    const char *visual_rep=getenv("NFSU2_VISUAL_REP_REMOVER");
    if(visual_rep && (!strcmp(visual_rep,"0") || !strcmp(visual_rep,"1")))
        vr_settings.visual_rep_remover=*visual_rep=='1';
    vr_start_visual_rep=vr_settings.visual_rep_remover;
    const char *catchup=getenv("NFSU2_CATCHUP");
    if(catchup)vr_settings.career_catchup=!strcmp(catchup,"noboost")?1:!strcmp(catchup,"off")?2:0;
    vr_start_catchup=vr_settings.career_catchup;
    const char *whine=getenv("NFSU2_GEARBOX_WHINE");
    if(whine && (!strcmp(whine,"0") || !strcmp(whine,"1")))vr_settings.gearbox_whine=*whine=='1';
    const char *gps=getenv("NFSU2_GPS_DISTANCE");
    if(gps && (!strcmp(gps,"0") || !strcmp(gps,"1")))vr_settings.gps_distance=*gps=='1';
    const char *hud=getenv("NFSU2_UI_TEXTURES"),*map=getenv("NFSU2_MAP_TEXTURES");
    if(hud)vr_settings.upscaled_hud=!strcmp(hud,"uhud");
    if(map)vr_settings.detailed_map=!strcmp(map,"detailed");
    vr_start_uhud=vr_settings.upscaled_hud;vr_start_detailed_map=vr_settings.detailed_map;
    const char *subs=getenv("NFSU2_SUBTITLES");
    if(subs && (!strcmp(subs,"0") || !strcmp(subs,"1")))vr_settings.subtitles=*subs=='1';
    vr_start_subtitles=vr_settings.subtitles;
    const char *vinyls=getenv("NFSU2_HQ_VINYLS");
    if(vinyls && (!strcmp(vinyls,"0") || !strcmp(vinyls,"1")))vr_settings.hq_vinyls=*vinyls=='1';
    vr_start_vinyls=vr_settings.hq_vinyls;
    const char *songs=getenv("NFSU2_RESTORED_SOUNDTRACK");
    if(songs && (!strcmp(songs,"0") || !strcmp(songs,"1")))vr_settings.restored_soundtrack=*songs=='1';
    vr_start_soundtrack=vr_settings.restored_soundtrack;
    const char *damage=getenv("NFSU2_DAMAGE");
    if(damage && (!strcmp(damage,"0") || !strcmp(damage,"1")))vr_settings.crash_damage=*damage=='1';
    vr_start_damage=vr_settings.crash_damage;
    const char *regional=getenv("NFSU2_REGIONAL_CARS");
    if(regional && (!strcmp(regional,"0") || !strcmp(regional,"1")))vr_settings.regional_cars=*regional=='1';
    vr_start_regional=vr_settings.regional_cars;
    vr_start_copchase=vr_settings.copchase;
    const char *special=getenv("NFSU2_SPECIAL_VINYLS");
    if(special && (!strcmp(special,"0") || !strcmp(special,"1")))vr_settings.special_vinyls=*special=='1';
    const char *music_source=getenv("NFSU2_MUSIC_SOURCE");
    if(music_source){
        if(!strcmp(music_source,"original"))vr_settings.music_source=QUEST_MUSIC_EA_ONLY;
        else if(!strcmp(music_source,"custom"))vr_settings.music_source=QUEST_MUSIC_CUSTOM_ONLY;
        else if(!strcmp(music_source,"auto"))vr_settings.music_source=QUEST_MUSIC_AUTO;
    }
    vr_publish_settings();
    fprintf(stderr,"[VR settings] loaded: sensitivity=%.2f rotation=%.0f seat=%.2f/%.2f resolution=%d\n",
        vr_settings.sensitivity,vr_settings.rotation,vr_settings.height,vr_settings.distance,vr_settings.resolution);
}
static void vr_save_settings(void)
{
    char text[8192],temporary[4112];int n=quest_vr_format(&vr_settings,text,sizeof text);
    vr_save_status=-1;
    vr_save_tick=SDL_GetTicks();
    if(!*vr_settings_path || n<=0 || n>=(int)sizeof text)return;
    int base=snprintf(text+n,sizeof text-n,"race_camera=%d\n",quest_xr_camera_choice());
    if(base<0 || base>=(int)sizeof text-n)return;
    n+=base;
    for(int i=0;i<QUEST_COCKPIT_VEHICLES;++i) {
        const QuestMirrorSetting *m=&mirror_settings[i];
        if(!mirror_known[i])continue;
        int added=snprintf(text+n,sizeof text-n,"mirror_%s=%.6f,%.6f,%.6f,%.6f\n",quest_cockpit_vehicle(i)->name,
            m->inside.yaw,m->inside.pitch,m->outside.yaw,m->outside.pitch);
        if(added<0 || added>=(int)sizeof text-n)return;
        n+=added;
    }
    snprintf(temporary,sizeof temporary,"%s.tmp",vr_settings_path);
    SDL_RWops *f=SDL_RWFromFile(temporary,"wb");if(!f)return;
    int complete=SDL_RWwrite(f,text,1,(size_t)n)==(size_t)n;
    complete &= SDL_RWclose(f)==0;
    if(!complete)return;
#ifdef NFSU2_PCVR
    wchar_t from[4096],to[4096];
    if(MultiByteToWideChar(CP_UTF8,0,temporary,-1,from,4096) &&
       MultiByteToWideChar(CP_UTF8,0,vr_settings_path,-1,to,4096))
        vr_save_status=MoveFileExW(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)?1:-1;
#else
    vr_save_status=rename(temporary,vr_settings_path)==0?1:-1;
#endif
    if(vr_save_status>0)mirror_dirty=0;
    fprintf(stderr,"[VR settings] %s\n",vr_save_status>0?"saved":"save failed");
}
/* SDL covers Xbox, PlayStation and mapped Bluetooth/USB gamepads. Hotplug
 * probing is bounded to once per second; no guest-thread SDL event calls. */
static int vr_poll_gamepad(uint16_t *buttons,uint8_t analog[8],int16_t sticks[4],int clicks[2])
{
    uint32_t now=SDL_GetTicks();
    /* Also run detection when no pad is open. The headless VR renderer has
     * no desktop window/event pump; SDL's update explicitly scans drivers. */
    if(vr_gamepad || (uint32_t)(now-vr_gamepad_scan)>=1000) {
        SDL_GameControllerUpdate();
        SDL_FlushEvents(SDL_JOYAXISMOTION,SDL_JOYBATTERYUPDATED);
        SDL_FlushEvents(SDL_CONTROLLERAXISMOTION,SDL_CONTROLLERSENSORUPDATE);
    }
    if(vr_gamepad && !SDL_GameControllerGetAttached(vr_gamepad)) {
        SDL_GameControllerClose(vr_gamepad);vr_gamepad=NULL;vr_gamepad_selected=0;
    }
    if(!vr_gamepad && (uint32_t)(now-vr_gamepad_scan)>=1000) {
        vr_gamepad_scan=now;
        for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)) {
            vr_gamepad=SDL_GameControllerOpen(i);if(vr_gamepad)break;
        }
        if(vr_gamepad)fprintf(stderr,"[VR] gamepad connected: %s\n",SDL_GameControllerName(vr_gamepad));
    }
    if(!vr_gamepad)return 0;
    return quest_gamepad_sample(vr_gamepad,buttons,analog,sticks,clicks);
}
static int vr_settings_input(uint16_t buttons,const uint8_t analog[8],const int16_t sticks[4])
{
    if(!vr_panel_pixels || !vr_panel_uploaded)return 0;
    int paused,data;pthread_mutex_lock(&input_lock);paused=vr_paused;data=vr_copchase_data;pthread_mutex_unlock(&input_lock);
    if(vr_menu.copchase_data!=data){vr_menu.copchase_data=data;++vr_panel_revision;}
    int keys=buttons&15;
    int x=abs(sticks[2])>abs(sticks[0])?sticks[2]:sticks[0];
    int y=abs(sticks[3])>abs(sticks[1])?sticks[3]:sticks[1];
    if(abs(x)>18000 && abs(x)>abs(y))keys|=x>0?QUEST_VR_RIGHT:QUEST_VR_LEFT;
    else if(abs(y)>18000)keys|=y>0?QUEST_VR_UP:QUEST_VR_DOWN;
    if(analog[0]>127)keys|=QUEST_VR_ACCEPT;
    if(analog[1]>127 || (buttons&0x10))keys|=QUEST_VR_BACK;
    if(analog[2]>127)keys|=QUEST_VR_OPEN;
    if(analog[3]>127)keys|=QUEST_XYZ_REVEAL;
    QuestCopChaseBust bust;
    pthread_mutex_lock(&input_lock);bust=vr_copchase_bust;pthread_mutex_unlock(&input_lock);
    if(memcmp(&bust,&vr_copchase_bust_view,sizeof bust)){
        if(bust.error==1 && vr_copchase_bust_view.error!=1)vr_copchase_confirm.submitted=0;
        if(bust.active && !vr_copchase_bust_view.active)vr_menu.open=1;
        if(!bust.active && vr_copchase_bust_view.active)vr_menu.open=0;
        vr_copchase_bust_view=bust;++vr_panel_revision;
    }
    pthread_mutex_lock(&input_lock);vr_copchase_bust_visible=bust.active && paused==1;pthread_mutex_unlock(&input_lock);
    if(bust.active){
        if(quest_copchase_confirm(&vr_copchase_confirm,&bust,paused==1,keys)){
            pthread_mutex_lock(&input_lock);vr_copchase_confirm_high=bust.token_high;vr_copchase_confirm_low=bust.token_low;
            pthread_mutex_unlock(&input_lock);++vr_panel_revision;
        }
        vr_menu.held=keys;return paused?4:0;
    }
    quest_copchase_confirm(&vr_copchase_confirm,&bust,0,keys);
    if(vr_stance.open){
        QuestStanceMenu previous=vr_stance;xyz_stance_snapshot(&vr_stance.view);
        quest_stance_step(&vr_stance,SDL_GetTicks(),paused!=0,keys);
        if(vr_stance.action>=0){xyz_stance_edit(vr_stance.view.type,vr_stance.action,vr_stance.step);xyz_stance_snapshot(&vr_stance.view);}
        vr_menu.held=keys;
        if(!paused && vr_menu.open){vr_save_settings();vr_menu.open=0;}
        if(memcmp(&previous,&vr_stance,sizeof previous))++vr_panel_revision;
        return paused?4:0;
    }
    // The original world unload hides all pause panels. Present the requested
    // browser once when that career transfer reaches the main menu.
    static uint32_t career_browser_presented;
    if(paused==2){
        XYZRaceBrowser browser;xyz_races_snapshot(&browser);
        if(browser.active && browser.from_career && browser.epoch!=career_browser_presented &&
           (browser.state==XYZ_RACES_READY || browser.state==XYZ_RACES_SEARCHING)){
            career_browser_presented=browser.epoch;vr_xyz_races.view=browser;
            quest_xyz_races_open(&vr_xyz_races,keys);++vr_panel_revision;
        }
    }
    if(vr_xyz_races.open){
        QuestXYZRaces previous=vr_xyz_races;
        XYZRaceBrowser snapshot;xyz_races_snapshot(&snapshot);quest_xyz_races_update(&vr_xyz_races,&snapshot);
        quest_xyz_races_step(&vr_xyz_races,SDL_GetTicks(),paused!=0,keys);
        if(vr_xyz_races.action==XYZ_RACES_SEARCH)xyz_races_search(vr_xyz_races.view.epoch,vr_xyz_races.requested_start);
        if(vr_xyz_races.action==XYZ_RACES_CLOSE)xyz_races_close();
        if(vr_xyz_races.action==XYZ_RACES_JOIN){
            if(!xyz_races_join(vr_xyz_races.view.epoch,vr_xyz_races.selected_index,&vr_xyz_races.selected,vr_xyz_races.password))
                snprintf(vr_xyz_races.notice,sizeof vr_xyz_races.notice,"The race changed. Refresh the list and select it again.");
            quest_xyz_races_clear_password(&vr_xyz_races);
        }
        if(vr_xyz_races.action==XYZ_RACES_SET_READY)xyz_races_ready(vr_xyz_races.view.epoch,!vr_xyz_races.view.local_ready);
        if(vr_xyz_races.action==XYZ_RACES_START)xyz_races_start(vr_xyz_races.view.epoch);
        if(vr_xyz_races.action==XYZ_RACES_LEAVE)xyz_races_leave(vr_xyz_races.view.epoch);
        if(vr_xyz_races.action==XYZ_RACES_CREATE){
            if(!xyz_races_create(vr_xyz_races.view.epoch,&vr_xyz_races.options))
                snprintf(vr_xyz_races.notice,sizeof vr_xyz_races.notice,"The race options changed. Choose a track and try again.");
            else quest_xyz_races_clear_create(&vr_xyz_races);
        }
        vr_xyz_races.action=0;vr_xyz_profiles.held=vr_menu.held=keys;
        if(!paused && vr_menu.open){vr_save_settings();vr_menu.open=vr_xyz_profiles.open=0;}
        if(memcmp(&previous,&vr_xyz_races,sizeof previous))++vr_panel_revision;
        quest_xyz_races_clear_password(&previous);
        quest_xyz_races_clear_create(&previous);
        return paused?4:0;
    }
    if(vr_xyz_profiles.open) {
        QuestXYZProfiles previous=vr_xyz_profiles;
        xyz_profiles_snapshot(&vr_xyz_profiles.list);
        xyz_online_snapshot(&vr_xyz_profiles.online);
        vr_xyz_profiles.main_menu=paused==2;
        vr_xyz_profiles.races_active=xyz_races_active();
        quest_xyz_step(&vr_xyz_profiles,SDL_GetTicks(),paused,keys);
        if(vr_xyz_profiles.action) {
            if(vr_xyz_profiles.action==XYZ_PROFILE_WEBSITE && quest_xyz_can_open_website(&vr_xyz_profiles))vr_save_settings();
            int accepted=vr_xyz_profiles.action==XYZ_PROFILE_WEBSITE?
                quest_xyz_open_website(&vr_xyz_profiles,SDL_OpenURL):
                xyz_profiles_action(vr_xyz_profiles.action,vr_xyz_profiles.index,vr_xyz_profiles.text);
            quest_xyz_clear_text(&vr_xyz_profiles);
            if(accepted && vr_xyz_profiles.action==XYZ_PROFILE_RACES){
                xyz_races_snapshot(&vr_xyz_races.view);quest_xyz_races_open(&vr_xyz_races,keys);++vr_panel_revision;
            }
            xyz_profiles_snapshot(&vr_xyz_profiles.list);
            if(vr_xyz_profiles.action==XYZ_PROFILE_CREATE && vr_xyz_profiles.list.selected>=0)
                vr_xyz_profiles.row=vr_xyz_profiles.list.selected;
            if(vr_xyz_profiles.row>=vr_xyz_profiles.list.count+QUEST_XYZ_ACTIONS)
                vr_xyz_profiles.row=vr_xyz_profiles.list.count+QUEST_XYZ_ACTIONS-1;
            vr_xyz_profiles.action=0;
        }
        vr_menu.held=keys;
        if(!paused && vr_menu.open) {vr_save_settings();vr_menu.open=0;}
        if(memcmp(&previous,&vr_xyz_profiles,sizeof previous))++vr_panel_revision;
        quest_xyz_clear_text(&previous);
        return paused?4:0;
    }
    int old_open=vr_menu.open,old_row=vr_menu.row;
    int changed=quest_vr_menu_step(&vr_menu,&vr_settings,SDL_GetTicks(),paused,keys);
    if(changed&QUEST_VR_OPEN_STANCE){
        memset(&vr_stance,0,sizeof vr_stance);vr_stance.open=1;vr_stance.held=keys;
        xyz_stance_snapshot(&vr_stance.view);++vr_panel_revision;
    }
    if(changed&QUEST_VR_OPEN_PROFILES) {
        xyz_profiles_snapshot(&vr_xyz_profiles.list);
        xyz_online_snapshot(&vr_xyz_profiles.online);
        vr_xyz_profiles.main_menu=paused==2;
        vr_xyz_profiles.races_active=xyz_races_active();
        quest_xyz_open(&vr_xyz_profiles,keys);++vr_panel_revision;
    }
    if(changed&QUEST_VR_RESET_MIRRORS) {
        pthread_mutex_lock(&input_lock);int vehicle=cockpit_vehicle_index;pthread_mutex_unlock(&input_lock);
        mirror_settings[vehicle]=(QuestMirrorSetting){0};mirror_known[vehicle]=1;mirror_dirty=1;
        quest_mirror_release(&mirror_grip);
        ++vr_panel_revision;
    }
    if(changed&1)vr_publish_settings();
    if(changed&2) {
        vr_save_settings();
        if(vr_save_status<0 && paused) {vr_menu.open=1;vr_menu.row=QUEST_VR_SAVE_ROW;}
    }
    if(old_open!=vr_menu.open || old_row!=vr_menu.row || (changed&2))++vr_panel_revision;
    return changed&4;
}
static int vr_panel_initialize(VkPhysicalDevice physical,int64_t format)
{
    vr_panel_format=format;
    if(!create_swapchain(&vr_panel_swap,format,1024,768))return 0;
    VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size=vr_panel_stride*vr_panel_swap.count;ci.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if(vkCreateBuffer(device,&ci,NULL,&vr_panel_buffer)!=VK_SUCCESS)return 0;
    VkMemoryRequirements requirements;VkPhysicalDeviceMemoryProperties memory;
    vkGetBufferMemoryRequirements(device,vr_panel_buffer,&requirements);
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    uint32_t type=~0u;
    for(uint32_t i=0;i<memory.memoryTypeCount;++i)
        if((requirements.memoryTypeBits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&
           (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
           (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}
    if(type==~0u)return 0;
    VkMemoryAllocateInfo alloc={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize=requirements.size;alloc.memoryTypeIndex=type;
    if(vkAllocateMemory(device,&alloc,NULL,&vr_panel_memory)!=VK_SUCCESS ||
       vkBindBufferMemory(device,vr_panel_buffer,vr_panel_memory,0)!=VK_SUCCESS ||
       vkMapMemory(device,vr_panel_memory,0,ci.size,0,(void **)&vr_panel_pixels)!=VK_SUCCESS)return 0;
    vr_panel_uploaded=calloc(vr_panel_swap.count,sizeof *vr_panel_uploaded);
    return vr_panel_uploaded!=NULL;
}
static void vr_panel_copy(VkCommandBuffer cb)
{
    int paused;pthread_mutex_lock(&input_lock);paused=vr_paused;pthread_mutex_unlock(&input_lock);
    if(!paused || !vr_panel_pixels || !vr_panel_uploaded)return;
    XrSwapchainImageAcquireInfo acquire={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(!XR_OK(xrAcquireSwapchainImage(vr_panel_swap.handle,&acquire,&vr_panel_swap.index)))return;
    vr_panel_swap.acquired=1;
    XrSwapchainImageWaitInfo wait={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
    if(!XR_OK(xrWaitSwapchainImage(vr_panel_swap.handle,&wait)))return;
    unsigned index=vr_panel_swap.index;
    uint32_t *pixels=vr_panel_pixels+(vr_panel_stride/4)*index;
    if(vr_panel_uploaded[index]!=vr_panel_revision) {
        if(vr_copchase_bust_view.active)quest_copchase_panel(pixels,&vr_copchase_bust_view,vr_copchase_confirm.submitted);
        else if(vr_stance.open)quest_stance_panel(pixels,&vr_stance);
        else if(vr_xyz_races.open)quest_xyz_races_panel(pixels,&vr_xyz_races);
        else if(vr_xyz_profiles.open)quest_xyz_panel(pixels,&vr_xyz_profiles);
        else quest_vr_panel(pixels,&vr_settings,&vr_menu,vr_save_status,
            vr_settings.resolution!=vr_start_resolution || vr_settings.show_hidden_races!=vr_start_hidden_races ||
            vr_settings.visual_rep_remover!=vr_start_visual_rep || vr_settings.career_catchup!=vr_start_catchup ||
            vr_settings.upscaled_hud!=vr_start_uhud || vr_settings.detailed_map!=vr_start_detailed_map || vr_settings.subtitles!=vr_start_subtitles || vr_settings.hq_vinyls!=vr_start_vinyls || vr_settings.restored_soundtrack!=vr_start_soundtrack || vr_settings.crash_damage!=vr_start_damage || vr_settings.regional_cars!=vr_start_regional || vr_settings.copchase!=vr_start_copchase);
        for(unsigned i=0;i<1024u*768u;++i)
            pixels[i]=quest_color_panel_pixel(pixels[i],(VkFormat)vr_panel_format);
        vr_panel_uploaded[index]=vr_panel_revision;
    }
    VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=vr_panel_swap.images[index].image;
    barrier.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
    VkBufferImageCopy copy={0};copy.bufferOffset=vr_panel_stride*index;
    copy.imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    copy.imageExtent=(VkExtent3D){1024,768,1};
    vkCmdCopyBufferToImage(cb,vr_panel_buffer,barrier.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&barrier);
}
