#ifdef __ANDROID__
#define XR_USE_PLATFORM_ANDROID
#include <jni.h>
#endif
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <SDL.h>
#include <SDL_system.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ANDROID__
#include <unistd.h>
#endif
#include "quest_xr.h"
#include "quest_color_transfer.h"
#include "quest_culling.h"
#include "quest_frame_timing.h"
#include "quest_cockpit.h"
#include "quest_cockpit_ui.h"
#include "quest_cockpit_vehicle.h"
#include "quest_driving_input.h"
#include "quest_haptics.h"
#include "quest_projection.h"
#include "quest_wheel_grip.h"
#include "quest_shifter.h"
#include "quest_pedals.h"
#include "quest_vr_settings.h"

static XrInstance instance;
static XrSystemId system_id;
static XrSession session;
static XrSpace local_space, view_space;
static XrSessionState state;
static uint32_t session_retry_tick;
static int session_retry_pending;
static unsigned session_retry_count;
static int enabled, running, begun, visible, tracked, recentered, stereo_frame, menu_frame;
static int steamvr_runtime;
static QuestPauseGesture steamvr_pause;
static uint64_t frame_number, submitted_frames;
static XrTime display_time;
#ifdef __ANDROID__
static JavaVM *vm;
static jobject activity;
#endif
static VkDevice device;
static VkPhysicalDevice color_physical;
static XrView views[2];
static XrPosef origin;
static float matrices[2][16];
static float cockpit_world_matrices[2][16];
static float cockpit_matrices[2][16];
static pthread_mutex_t culling_lock = PTHREAD_MUTEX_INITIALIZER;
static float culling_planes[6][4];
static int culling_valid;
static uint32_t culling_tick;
static float units_per_meter = 1.0f;
typedef struct {
    XrSwapchain handle;
    XrSwapchainImageVulkan2KHR *images;
    uint32_t width, height, index, count;
    int acquired;
    VkFormat format;
    QuestColorTransfer color_transfer;
} QuestSwapchain;
static QuestSwapchain eyes[2], cinema;
static PFN_xrCreateVulkanInstanceKHR create_vk_instance;
static PFN_xrGetVulkanGraphicsDevice2KHR get_vk_physical;
static PFN_xrCreateVulkanDeviceKHR create_vk_device;
static PFN_xrPerfSettingsSetPerformanceLevelEXT set_performance;
#ifdef __ANDROID__
static PFN_xrSetAndroidApplicationThreadKHR set_thread;
#endif
static PFN_xrEnumerateDisplayRefreshRatesFB enumerate_rates;
static PFN_xrRequestDisplayRefreshRateFB request_rate;
static PFN_xrGetDisplayRefreshRateFB get_rate;
static uint32_t game_tid;
static XrActionSet actions;
enum { A, B, X, Y, MENU, LCLICK, RCLICK, LTRIGGER, RTRIGGER, LGRIP, RGRIP, LSTICK, RSTICK, ACTION_COUNT };
static XrAction action[ACTION_COUNT];
static XrAction haptic_action[2];
static XrAction hand_action[2];
static XrSpace hand_space[2];
static PFN_xrCreateHandTrackerEXT create_hand_tracker;
static PFN_xrDestroyHandTrackerEXT destroy_hand_tracker;
static PFN_xrLocateHandJointsEXT locate_hand_joints;
static XrHandTrackerEXT replay_trackers[2];
static unsigned replay_bare_hands;
static QuestWheelGrip wheel_grip;
static QuestShifter shifter;
static QuestShiftMotion shift_motion;
static uint32_t shift_click_tick,shift_flash_tick;
static int shift_click_hand,shift_direction,shift_feedback_gear;
static QuestScreenGrip screen_grip;
static int screen_vehicle=-1,screen_stick_captured;
static uint64_t screen_draw_frame;
static QuestTelemetry telemetry;
static QuestPedals pedals;
static int shifter_available,shifter_vehicle;
/* Mailbox protected by input_lock. Only the render thread touches the lever
 * and hand poses. Only the driver tick accesses guest transmission memory. */
static uint32_t gearbox_car,gearbox_tick,shifter_tick,gearbox_epoch,shifter_epoch;
static int gearbox_count,gearbox_gear,gearbox_manual,shifter_request=QUEST_SHIFT_NONE;
/* The grid selection survives a pause, tracking loss and the countdown camera. */
static uint32_t grid_car;
static int grid_gear,grid_count,grid_racing,shifter_gamepad;
static int wheel_configured, hand_valid[2], frame_focused;
static float hand_matrix[2][16], frame_squeeze[2];
static uint16_t frame_buttons;
static uint8_t frame_analog[8];
static int16_t frame_sticks[4];
static QuestHaptics haptics;
static float haptic_sent[2],engine_rumble;
static uint32_t haptic_sent_tick[2];
static int haptic_click_sent[2];
static int haptics_enabled=1;
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t input_buttons;
static uint8_t input_analog[8];
static int16_t input_sticks[4];
static int cockpit_ready, cockpit_requested, cockpit_available, cockpit_active;
static int cockpit_vehicle_index;
static uint32_t cockpit_car,mirror_epoch,mirror_seen_epoch;
static QuestMirrorSetting mirror_settings[QUEST_COCKPIT_VEHICLES];
static unsigned char mirror_known[QUEST_COCKPIT_VEHICLES];
static int camera_choice=-1,camera_dirty;
static QuestCockpitTouch cabin_touch[2];
static QuestMinimapGrip map_grip;
static QuestMinimapShortcut map_shortcut;
static unsigned frame_map_valid,frame_map_down;
static int frame_driving;
static QuestShiftInput shift_input;
static QuestMinimapMount map_mount;
static int map_vehicle=-1;
static uint64_t map_draw_frame;
static float index_tip[3];
static int guide_gears,replay_toggle;
static uint32_t replay_owner;
static int replay_open;
static uint32_t replay_viewer;
static unsigned replay_camera_steps;
static QuestReplayClick replay_click;
static QuestReplayButton replay_button;
static QuestRadioCommand radio_queue[8];
static int radio_count,radio_paused;
static float radio_volume;
static QuestRadioSong radio_song;
static uint32_t radio_owner,radio_tick;
static QuestMirrorMount mirror_mounts[QUEST_MIRRORS];
static uint32_t mirror_mount_tick[QUEST_MIRRORS];
static QuestMirrorGrip mirror_grip;
static int mirror_vehicle,mirror_dirty;
static float mirror_eyes[2][4];
/* The rear mover and XR renderer exchange only car-local snapshots. */
static QuestMirrorCamera rear_source;
static uint32_t rear_source_car,rear_source_tick;
static uint32_t rear_masks[2];
static uint32_t cockpit_window_textures[QUEST_COCKPIT_WINDOWS];
static int driving_active,world_map_available;
static int cockpit_menu=1;
static uint32_t driving_tick;
static uint32_t cockpit_tick;
static int16_t cockpit_steering;
static uint32_t cockpit_steering_tick;
static QuestVRSettings vr_settings;
static void vr_publish_settings(void);

void quest_xr_haptic_input(uint16_t low, uint16_t high)
{
    pthread_mutex_lock(&input_lock);
    quest_haptics_report(&haptics,SDL_GetTicks(),low,high);
    pthread_mutex_unlock(&input_lock);
}

void quest_xr_cockpit_steering_input(int16_t steering)
{
    pthread_mutex_lock(&input_lock);
    cockpit_steering=steering; cockpit_steering_tick=SDL_GetTicks();
    pthread_mutex_unlock(&input_lock);
}
float quest_xr_cockpit_steering(int *from_rim)
{
    pthread_mutex_lock(&input_lock);
    int v=(uint32_t)(SDL_GetTicks()-cockpit_steering_tick)<500?cockpit_steering:0;
    pthread_mutex_unlock(&input_lock);
    /* Animation follows physical wheel angle, not sensitivity-amplified car
     * input. Otherwise faster steering also makes held hands slide. */
    *from_rim=wheel_grip.latched;
    if(*from_rim) return wheel_grip.angle/QUEST_WHEEL_LOCK_RADIANS;
    return fmaxf(-1,fminf(1,quest_wheel_stick_steering((int16_t)v)/vr_settings.sensitivity))*vr_settings.rotation/360.0f;
}

void quest_xr_cockpit_ready(int ready)
{
    pthread_mutex_lock(&input_lock);
    cockpit_ready=ready;
    pthread_mutex_unlock(&input_lock);
}
int quest_xr_cockpit_requested(void)
{
    pthread_mutex_lock(&input_lock);
    int result=cockpit_ready && cockpit_requested;
    pthread_mutex_unlock(&input_lock);
    return result;
}
void quest_xr_cockpit_publish(int available, int active)
{
    pthread_mutex_lock(&input_lock);
    if(!active && cockpit_active) { ++mirror_epoch;rear_source_car=0; }
    cockpit_available=available; cockpit_active=active; cockpit_tick=SDL_GetTicks();
    if(!active && gearbox_car && gearbox_car!=grid_car) { gearbox_car=0;shifter_request=QUEST_SHIFT_NONE;++gearbox_epoch; }
    pthread_mutex_unlock(&input_lock);
}
void quest_xr_cockpit_vehicle_publish(uint32_t car,int index,const uint32_t windows[QUEST_COCKPIT_WINDOWS])
{
    if(index<0 || index>=QUEST_COCKPIT_VEHICLES)index=0;
    pthread_mutex_lock(&input_lock);
    if(car!=cockpit_car || index!=cockpit_vehicle_index) {
        ++mirror_epoch;rear_source_car=0;
    }
    cockpit_car=car;
    cockpit_vehicle_index=index;
    memcpy(cockpit_window_textures,windows,sizeof cockpit_window_textures);
    pthread_mutex_unlock(&input_lock);
}
int quest_xr_cockpit_vehicle_snapshot(uint32_t windows[QUEST_COCKPIT_WINDOWS])
{
    pthread_mutex_lock(&input_lock);
    int index=cockpit_vehicle_index;
    memcpy(windows,cockpit_window_textures,sizeof cockpit_window_textures);
    pthread_mutex_unlock(&input_lock);
    return index;
}
void quest_xr_driving_publish(int driving,int map_available,float engine)
{
    pthread_mutex_lock(&input_lock);
    driving_active=driving;world_map_available=map_available;engine_rumble=engine;driving_tick=SDL_GetTicks();
    pthread_mutex_unlock(&input_lock);
}
unsigned quest_xr_cockpit_menu_publish(int menu,uint32_t viewer)
{
    pthread_mutex_lock(&input_lock);
    if(menu==QUEST_UI_MENU && cockpit_menu!=menu) { shifter_request=QUEST_SHIFT_NONE;++gearbox_epoch; }
    if(menu!=QUEST_UI_REPLAY){replay_owner=0;replay_toggle=0;}
    if(viewer!=replay_viewer || menu!=QUEST_UI_REPLAY) {
        replay_click=(QuestReplayClick){0};replay_camera_steps=0;
    }
    replay_viewer=viewer;
    unsigned steps=replay_camera_steps;replay_camera_steps=0;
    cockpit_menu=menu;
    replay_open=viewer || menu==QUEST_UI_REPLAY;
    pthread_mutex_unlock(&input_lock);
    return steps;
}
int quest_xr_cockpit_menu_active(void)
{
    pthread_mutex_lock(&input_lock);
    int menu=cockpit_menu;
    pthread_mutex_unlock(&input_lock);
    return menu;
}
int quest_xr_take_replay_toggle(uint32_t owner)
{
    pthread_mutex_lock(&input_lock);
    int toggle=owner && owner==replay_owner && replay_toggle;
    replay_owner=owner;replay_toggle=0;
    pthread_mutex_unlock(&input_lock);
    return toggle;
}
unsigned quest_xr_mirror_width(void) {return quest_mirror_widths[vr_settings.mirror_resolution];}
int quest_xr_map_drawn(int vehicle,const QuestMinimapMount *mount,float matrix[16])
{
    int hover=map_vehicle==vehicle && (map_grip.hover[0] || map_grip.hover[1]);
    map_vehicle=vehicle;map_mount=*mount;map_draw_frame=frame_number;
    quest_minimap_matrix(matrix,mount,&vr_settings.map);
    return hover;
}
static int release_map_grip(void)
{
    map_vehicle=-1;
    map_shortcut.armed=0;map_shortcut.pulsing=0;
    screen_vehicle=-1;
    int changed=quest_screen_release(&screen_grip,&vr_settings.cockpit.screen);
    return quest_minimap_release(&map_grip,&vr_settings.map)|changed;
}
int quest_xr_screen_page(void) {return vr_settings.cockpit.screen.page;}
void quest_xr_screen_drawn(int vehicle,float matrix[16],float *width,float *opacity)
{
    const QuestCockpitVehicle *fit=quest_cockpit_vehicle(vehicle);
    QuestMinimapMount mount=quest_screen_mount(fit);
    quest_minimap_matrix(matrix,&mount,&vr_settings.cockpit.screen.placement);
    *width=quest_radio_screen_width(fit)*vr_settings.cockpit.screen.scale;
    *opacity=vr_settings.cockpit.screen.opacity;
    screen_vehicle=vehicle;screen_draw_frame=frame_number;
}
void quest_xr_telemetry_publish(uint32_t car,float rpm,float limit,float speed,float brake)
{
    pthread_mutex_lock(&input_lock);
    quest_telemetry_sample(&telemetry,SDL_GetTicks(),car,rpm,limit,speed,brake);
    pthread_mutex_unlock(&input_lock);
}
uint64_t quest_xr_telemetry_frame(uint8_t *pixels)
{
    uint32_t now=SDL_GetTicks();
    if(pixels){QuestTelemetry snapshot;pthread_mutex_lock(&input_lock);snapshot=telemetry;pthread_mutex_unlock(&input_lock);
        quest_telemetry_render(&snapshot,now,pixels);}
    return (UINT64_C(1)<<63)|((uint64_t)now/50+1);
}
int quest_xr_shifter_mode(void) {return vr_settings.cockpit.shift.mode;}
int quest_xr_shifter_reverse(void) {return vr_settings.cockpit.shift.reverse;}
void quest_xr_index_tip(const float tip[3]) {memcpy(index_tip,tip,sizeof index_tip);}
int quest_xr_guide_gears(void) {return guide_gears;}
uint32_t quest_xr_led_color(int *pulse_to_music)
{
    *pulse_to_music=vr_settings.led_strips && vr_settings.led_music_pulse;
    if(!vr_settings.led_strips)return 0;
    if(vr_settings.cockpit.shift_lights && frame_focused && frame_driving && !quest_xr_cockpit_menu_active()) {
        if(shift_direction && SDL_GetTicks()-shift_flash_tick<160){*pulse_to_music=0;return shift_direction>0?0xFF1208:0x1260FF;}
        if(shift_motion.zone[0] || shift_motion.zone[1]){*pulse_to_music=0;return 0x4DDF88;}
    }
    return quest_led_colors[vr_settings.led_color].rgb;
}

static int driving_input_active(void)
{
    uint32_t now=SDL_GetTicks();
    pthread_mutex_lock(&input_lock);
    /* Input belongs to the live player, not to successful cabin rendering.
     * A camera transition must not switch career actions back to menu axes. */
    int active=driving_active && (uint32_t)(now-driving_tick)<500;
    pthread_mutex_unlock(&input_lock);
    return active;
}
int quest_xr_cockpit_active(void)
{
    pthread_mutex_lock(&input_lock);
    int result=cockpit_ready && cockpit_active && (uint32_t)(SDL_GetTicks()-cockpit_tick)<500;
    pthread_mutex_unlock(&input_lock);
    return result;
}
int quest_xr_cockpit_enabled(void)
{
    pthread_mutex_lock(&input_lock);
    int result=cockpit_ready;
    pthread_mutex_unlock(&input_lock);
    return result;
}
void quest_xr_cockpit_select(int selected)
{
    pthread_mutex_lock(&input_lock);
    int next=cockpit_ready && selected;
    if(next!=cockpit_requested)
        fprintf(stderr,"[Cockpit] %s via normal camera cycle\n",next?"selected":"original camera selected");
    cockpit_requested=next;
    pthread_mutex_unlock(&input_lock);
}
int quest_xr_camera_choice(void)
{
    pthread_mutex_lock(&input_lock);int choice=camera_choice;pthread_mutex_unlock(&input_lock);
    return choice;
}
void quest_xr_camera_remember(int choice)
{
    pthread_mutex_lock(&input_lock);
    if(choice>=0 && choice<=6 && choice!=camera_choice){camera_choice=choice;camera_dirty=1;}
    if(choice>=0 && choice<5 && grid_car){grid_car=0;shifter_request=QUEST_SHIFT_NONE;++gearbox_epoch;}
    pthread_mutex_unlock(&input_lock);
}
QuestRadioCommand quest_xr_radio_exchange(uint32_t owner,float volume,int paused,const QuestRadioSong *song)
{
    QuestRadioCommand cmd={0};uint32_t now=SDL_GetTicks();
    pthread_mutex_lock(&input_lock);
    if(!owner || owner!=radio_owner || now-radio_tick>=500)radio_count=0;
    radio_owner=owner;radio_tick=now;radio_volume=volume;radio_paused=paused;
    if(owner)radio_song=*song;
    if(radio_count) {
        cmd=radio_queue[0];--radio_count;
        memmove(radio_queue,radio_queue+1,(size_t)radio_count*sizeof cmd);
    }
    pthread_mutex_unlock(&input_lock);
    return cmd;
}
uint32_t quest_xr_radio_display(float state[3],QuestRadioSong *song,uint32_t *pressed)
{
    pthread_mutex_lock(&input_lock);
    state[0]=radio_volume;state[1]=(float)radio_paused;
    state[2]=radio_owner && SDL_GetTicks()-radio_tick<500?0.f:-1.f;
    *song=state[2]>=0?radio_song:(QuestRadioSong){0};
    uint32_t hover=0;*pressed=0;
    for(int h=0;h<2;++h) {
        int key=cabin_touch[h].pointing;
        if(key!=QUEST_RADIO_NONE)hover|=1u<<key;
        int contact=cabin_touch[h].control;
        if(contact>=QUEST_RADIO_VOLUME && contact<=QUEST_COCKPIT_PAGE)*pressed|=1u<<contact;
    }
    pthread_mutex_unlock(&input_lock);
    return hover;
}
void quest_xr_cockpit_matrix(int eye, float matrix[16])
{
    memcpy(matrix,cockpit_matrices[eye?1:0],16*sizeof(float));
    for(int c=0;c<3;++c) for(int r=0;r<4;++r) matrix[4*c+r]*=units_per_meter;
}

void quest_xr_wheel_configure(const float pivot[3],const float axis[3],float radius)
{
    quest_wheel_configure(&wheel_grip,pivot,axis,radius); wheel_configured=1;
    vr_publish_settings();
}
void quest_xr_shifter_configure(int available)
{
    pthread_mutex_lock(&input_lock);
    shifter_available=available;
    pthread_mutex_unlock(&input_lock);
    quest_shifter_reset(&shifter);shift_motion=(QuestShiftMotion){0};shift_click_hand=shift_direction=0;
}
void quest_xr_shifter_offset(float *x,float *z)
{
    *x=shifter.x;*z=shifter.z;
}
void quest_xr_pedal_travel(float out[3])
{ memcpy(out,pedals.travel,sizeof pedals.travel); }
int quest_xr_shifter_stage(uint32_t car,int stage,int previous,int manual_gears)
{
    pthread_mutex_lock(&input_lock);
    int enabled=car && manual_gears>=4 && manual_gears<=6 && shifter_available && !shifter_gamepad &&
                cockpit_ready && (cockpit_requested || camera_choice>=5);
    int grid=quest_shifter_before_start(stage);
    if(!enabled || (!grid && stage!=4)) {
        if(grid_car){grid_car=0;shifter_request=QUEST_SHIFT_NONE;++gearbox_epoch;}
    } else if((grid && (grid_car!=car || grid_racing || (stage==2 && previous!=2))) ||
              (stage==4 && previous!=4 && grid_car!=car)) {
        grid_car=car;grid_count=manual_gears;grid_gear=QUEST_SHIFT_N;
        shifter_request=QUEST_SHIFT_NONE;++gearbox_epoch;
    }
    grid_racing=stage==4;
    int gear=grid_car && grid_car==car?grid_gear:QUEST_SHIFT_NONE;
    pthread_mutex_unlock(&input_lock);
    return gear;
}
uint32_t quest_xr_shifter_grid_car(void)
{
    pthread_mutex_lock(&input_lock);
    uint32_t car=grid_racing?0:grid_car;
    pthread_mutex_unlock(&input_lock);
    return car;
}
int quest_xr_shifter_grid_request(uint32_t car)
{
    pthread_mutex_lock(&input_lock);
    int gear=QUEST_SHIFT_NONE;
    if(car && car==grid_car)gear=grid_gear;
    pthread_mutex_unlock(&input_lock);
    return gear;
}
int quest_xr_shifter_driver(uint32_t car,int count,int gear,int manual,int locked)
{
    uint32_t now=SDL_GetTicks();
    pthread_mutex_lock(&input_lock);
    int grid=grid_car && grid_car==car;
    if(grid && !manual){grid_car=0;grid=0;shifter_request=QUEST_SHIFT_NONE;++gearbox_epoch;}
    manual=manual && (!locked || (locked==QUEST_DRIVER_GRID_LOCK && grid));
    if(!grid && (car!=gearbox_car || count!=gearbox_count || manual!=gearbox_manual ||
       now-gearbox_tick>=500)) {
        shifter_request=QUEST_SHIFT_NONE;++gearbox_epoch;
    }
    gearbox_car=car;gearbox_count=count;gearbox_gear=gear;gearbox_manual=manual;gearbox_tick=now;
    if(!manual || !cockpit_active || !cockpit_requested || cockpit_menu || !driving_active ||
       now-cockpit_tick>=500 || now-driving_tick>=500 || now-shifter_tick>=500)
        shifter_request=QUEST_SHIFT_NONE;
    int result=shifter_request;
    if(grid && (!grid_racing || manual)) {
        result=grid_gear;
        if(grid_racing && !locked && grid_gear==QUEST_SHIFT_FIRST && gear==QUEST_SHIFT_FIRST) {
            grid_car=0;shifter_request=QUEST_SHIFT_FIRST;
        }
    }
    pthread_mutex_unlock(&input_lock);
    return result;
}
int quest_xr_hand_matrix(int hand,float steering,float matrix[16],int *grasped)
{
    if(hand<0 || hand>1 || !frame_focused || !quest_xr_cockpit_active()) return 0;
    *grasped=wheel_grip.grabbed[hand];
    if((map_grip.hand==hand+1 || screen_grip.grip.hand==hand+1) && hand_valid[hand]) {
        memcpy(matrix,hand_matrix[hand],64);*grasped=QUEST_HAND_GRIP;return 1;
    }
    if(cabin_touch[hand].pointing && !*grasped && mirror_grip.hand!=hand+1 && shifter.hand!=hand+1 && hand_valid[hand]) {
        memcpy(matrix,hand_matrix[hand],64);*grasped=2;return 1;
    }
    if(mirror_grip.hand==hand+1) {
        int part=mirror_grip.part;float contact[3];
        quest_mirror_rotate(contact,mirror_grip.contact,quest_xr_mirror_angle(mirror_vehicle,part),0);
        memcpy(matrix,mirror_grip.previous,64);
        for(int k=0;k<3;++k)matrix[12+k]=mirror_mounts[part].center[k]+contact[k];
        *grasped=1;return 1;
    }
    if(shifter.hand==hand+1) {
        float knob[3];quest_shifter_knob(&shifter,shifter_vehicle,knob);
        memcpy(matrix,hand_matrix[hand],64);
        memcpy(matrix+12,knob,sizeof knob);
        *grasped=1;return 1;
    }
    if(!hand_valid[hand] && !*grasped) return 0;
    if(*grasped) quest_wheel_hand_matrix(&wheel_grip,hand,steering,matrix);
    else memcpy(matrix,hand_matrix[hand],64);
    return 1;
}

static int check(XrResult result, const char *call)
{
    if (XR_SUCCEEDED(result)) return 1;
    char name[XR_MAX_RESULT_STRING_SIZE] = {0};
    if (instance) xrResultToString(instance, result, name);
    fprintf(stderr, "[XR] %s failed: %s (%d)\n", call, name, result);
    return 0;
}
#define XR_OK(call) check((call), #call)
#include "quest_vr_runtime.inc"

void quest_xr_mirror_mounts(int vehicle,const QuestMirrorMount mounts[3])
{
    if(vehicle!=mirror_vehicle)return;
    uint32_t now=SDL_GetTicks();
    pthread_mutex_lock(&input_lock);
    for(int p=0;p<3;++p)if(mounts[p].valid) {
        mirror_mounts[p]=mounts[p];mirror_mount_tick[p]=now;
    }
    pthread_mutex_unlock(&input_lock);
}
QuestMirrorAngle quest_xr_mirror_angle(int vehicle,int part)
{ return quest_mirror_angle(&mirror_settings[vehicle],part); }
void quest_xr_mirror_normal(float out[3],const QuestMirrorMount *mount,int part,QuestMirrorAngle angle)
{
    const float seat[3]={0,vr_settings.height,vr_settings.distance};
    quest_mirror_normal(out,mount,part,angle,seat);
}
void quest_xr_mirror_eyes(float eyes[2][4])
{ memcpy(eyes,mirror_eyes,sizeof mirror_eyes); }
void quest_xr_mirror_camera(uint32_t car,const QuestMirrorCamera *camera,const uint32_t masks[2])
{
    pthread_mutex_lock(&input_lock);
    if(cockpit_car==car && cockpit_active) {
        rear_source=*camera;rear_source_tick=SDL_GetTicks();rear_source_car=car;
        memcpy(rear_masks,masks,sizeof rear_masks);
    }
    pthread_mutex_unlock(&input_lock);
}
int quest_xr_mirror_mask(uint32_t texture)
{
    pthread_mutex_lock(&input_lock);
    int result=texture && cockpit_active && rear_source_car==cockpit_car &&
        SDL_GetTicks()-rear_source_tick<400 && (texture==rear_masks[0] || texture==rear_masks[1]);
    pthread_mutex_unlock(&input_lock);
    return result;
}
int quest_xr_mirror_source(QuestMirrorCamera *camera,int *vehicle)
{
    pthread_mutex_lock(&input_lock);
    int valid=rear_source_car && rear_source_car==cockpit_car && SDL_GetTicks()-rear_source_tick<400;
    *camera=rear_source;*vehicle=cockpit_vehicle_index;
    pthread_mutex_unlock(&input_lock);
    const float *seat=quest_cockpit_vehicle(*vehicle)->seat;
    camera->origin[0]+=seat[1];camera->origin[1]-=seat[2];camera->origin[2]+=seat[0];
    return valid;
}

static XrPath path(const char *name)
{
    XrPath result = XR_NULL_PATH;
    XR_OK(xrStringToPath(instance, name, &result));
    return result;
}

static int setup_actions(void)
{
    XrActionSetCreateInfo ci = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(ci.actionSetName, "driving");
    strcpy(ci.localizedActionSetName, "Driving");
    if (!XR_OK(xrCreateActionSet(instance, &ci, &actions))) return 0;
    const char *names[ACTION_COUNT] = {"a", "b", "x", "y", "menu", "left_click", "right_click",
        "left_trigger", "right_trigger", "left_grip", "right_grip", "left_stick", "right_stick"};
    const char *bindings[ACTION_COUNT] = {
        "/user/hand/right/input/a/click", "/user/hand/right/input/b/click",
        "/user/hand/left/input/x/click", "/user/hand/left/input/y/click",
        "/user/hand/left/input/menu/click", "/user/hand/left/input/thumbstick/click",
        "/user/hand/right/input/thumbstick/click", "/user/hand/left/input/trigger/value",
        "/user/hand/right/input/trigger/value", "/user/hand/left/input/squeeze/value",
        "/user/hand/right/input/squeeze/value", "/user/hand/left/input/thumbstick",
        "/user/hand/right/input/thumbstick"};
    XrActionSuggestedBinding suggested[ACTION_COUNT+4];
    for (int i = 0; i < ACTION_COUNT; ++i) {
        XrActionCreateInfo ai = {XR_TYPE_ACTION_CREATE_INFO};
        strcpy(ai.actionName, names[i]);
        strcpy(ai.localizedActionName, names[i]);
        ai.actionType = i >= LSTICK ? XR_ACTION_TYPE_VECTOR2F_INPUT :
                       i >= LTRIGGER ? XR_ACTION_TYPE_FLOAT_INPUT : XR_ACTION_TYPE_BOOLEAN_INPUT;
        if (!XR_OK(xrCreateAction(actions, &ai, &action[i]))) return 0;
        suggested[i] = (XrActionSuggestedBinding){action[i], path(bindings[i])};
    }
    const char *haptic_names[2]={"left_vibration","right_vibration"};
    const char *haptic_paths[2]={"/user/hand/left/output/haptic","/user/hand/right/output/haptic"};
    for(int hand=0;hand<2;++hand) {
        XrActionCreateInfo ai={XR_TYPE_ACTION_CREATE_INFO};
        strcpy(ai.actionName,haptic_names[hand]);
        strcpy(ai.localizedActionName,haptic_names[hand]);
        ai.actionType=XR_ACTION_TYPE_VIBRATION_OUTPUT;
        if(!XR_OK(xrCreateAction(actions,&ai,&haptic_action[hand]))) return 0;
        suggested[ACTION_COUNT+hand]=(XrActionSuggestedBinding){haptic_action[hand],path(haptic_paths[hand])};
    }
    const char *pose_names[2]={"left_hand_pose","right_hand_pose"};
    const char *pose_paths[2]={"/user/hand/left/input/grip/pose","/user/hand/right/input/grip/pose"};
    for(int hand=0;hand<2;++hand) {
        XrActionCreateInfo ai={XR_TYPE_ACTION_CREATE_INFO};
        strcpy(ai.actionName,pose_names[hand]); strcpy(ai.localizedActionName,pose_names[hand]);
        ai.actionType=XR_ACTION_TYPE_POSE_INPUT;
        if(!XR_OK(xrCreateAction(actions,&ai,&hand_action[hand]))) return 0;
        suggested[ACTION_COUNT+2+hand]=(XrActionSuggestedBinding){hand_action[hand],path(pose_paths[hand])};
    }
    XrInteractionProfileSuggestedBinding binding = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    binding.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    binding.countSuggestedBindings = ACTION_COUNT+4;
    binding.suggestedBindings = suggested;
    return XR_OK(xrSuggestInteractionProfileBindings(instance, &binding));
}

int quest_xr_initialize(void)
{
    vr_load_settings();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    const char *rumble=getenv("RECOMP_RUMBLE");
    haptics_enabled=!(rumble && *rumble=='0');
    const char *opt = getenv("NFSU2_VR");
    if (opt && !strcmp(opt, "0")) return 1;
#ifdef __ANDROID__
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    if (!env || (*env)->GetJavaVM(env, &vm) != JNI_OK) return 0;
    jobject local_activity = SDL_AndroidGetActivity();
    activity = (*env)->NewGlobalRef(env, local_activity);
    (*env)->DeleteLocalRef(env, local_activity);
    PFN_xrInitializeLoaderKHR init_loader = NULL;
    if (!XR_OK(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&init_loader))) return 0;
    XrLoaderInitInfoAndroidKHR loader = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loader.applicationVM = vm;
    loader.applicationContext = activity;
    if (!XR_OK(init_loader((XrLoaderInitInfoBaseHeaderKHR *)&loader))) return 0;
    const char *extensions[7] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    uint32_t extension_count=2, available_count=0;
#else
    const char *extensions[7]={XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    uint32_t extension_count=1, available_count=0;
#endif
    int have_performance=0, have_thread_settings=0, have_refresh=0;
    int have_hand_tracking=0,have_hand_source=0;
    if(!XR_OK(xrEnumerateInstanceExtensionProperties(NULL,0,&available_count,NULL))) return 0;
    XrExtensionProperties *available=calloc(available_count,sizeof *available);
    if(!available) return 0;
    for(uint32_t i=0;i<available_count;++i) available[i].type=XR_TYPE_EXTENSION_PROPERTIES;
    if(!XR_OK(xrEnumerateInstanceExtensionProperties(NULL,available_count,&available_count,available))) { free(available); return 0; }
    for(uint32_t i=0;i<available_count;++i) {
        if(!strcmp(available[i].extensionName,XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME)) have_performance=1;
#ifdef __ANDROID__
        if(!strcmp(available[i].extensionName,XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME)) have_thread_settings=1;
#endif
        if(!strcmp(available[i].extensionName,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)) have_refresh=1;
        if(!strcmp(available[i].extensionName,XR_EXT_HAND_TRACKING_EXTENSION_NAME))have_hand_tracking=1;
        if(!strcmp(available[i].extensionName,XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME))have_hand_source=1;
    }
    free(available);
    if(have_performance) extensions[extension_count++]=XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME;
#ifdef __ANDROID__
    if(have_thread_settings) extensions[extension_count++]=XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME;
#endif
    if(have_refresh) extensions[extension_count++]=XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
    if(have_hand_tracking && have_hand_source) {
        extensions[extension_count++]=XR_EXT_HAND_TRACKING_EXTENSION_NAME;
        extensions[extension_count++]=XR_EXT_HAND_TRACKING_DATA_SOURCE_EXTENSION_NAME;
    }
    XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
#ifdef __ANDROID__
    XrInstanceCreateInfoAndroidKHR android = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android.applicationVM = vm;
    android.applicationActivity = activity;
    ci.next = &android;
    strcpy(ci.applicationInfo.applicationName, "NFSU2 Quest");
#else
    strcpy(ci.applicationInfo.applicationName, "NFSU2 PCVR");
#endif
    strcpy(ci.applicationInfo.engineName, "xboxrecomp");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
    ci.enabledExtensionCount = extension_count;
    ci.enabledExtensionNames = extensions;
    if (!XR_OK(xrCreateInstance(&ci, &instance))) return 0;
    XrInstanceProperties props = {XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(instance, &props);
    steamvr_runtime=strstr(props.runtimeName,"SteamVR")!=NULL;
    fprintf(stderr, "[XR] runtime: %s\n", props.runtimeName);
    XrSystemGetInfo si = {XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrResult system_result;
    uint32_t system_wait=SDL_GetTicks();
    do {
        system_result=xrGetSystem(instance,&si,&system_id);
        if(system_result!=XR_ERROR_FORM_FACTOR_UNAVAILABLE)break;
        if(SDL_GetTicks()-system_wait>=15000)break;
        SDL_Delay(250);
    } while(1);
    if (!XR_OK(system_result)) return 0;
#define PROC(name, ptr) if (!XR_OK(xrGetInstanceProcAddr(instance, name, (PFN_xrVoidFunction *)&ptr))) return 0
    PROC("xrCreateVulkanInstanceKHR", create_vk_instance);
    PROC("xrGetVulkanGraphicsDevice2KHR", get_vk_physical);
    PROC("xrCreateVulkanDeviceKHR", create_vk_device);
    PFN_xrGetVulkanGraphicsRequirements2KHR requirements;
    PROC("xrGetVulkanGraphicsRequirements2KHR", requirements);
    if(have_performance) PROC("xrPerfSettingsSetPerformanceLevelEXT",set_performance);
#ifdef __ANDROID__
    if(have_thread_settings) PROC("xrSetAndroidApplicationThreadKHR",set_thread);
#endif
    if(have_refresh) {
        PROC("xrEnumerateDisplayRefreshRatesFB",enumerate_rates);
        PROC("xrRequestDisplayRefreshRateFB",request_rate);
        PROC("xrGetDisplayRefreshRateFB",get_rate);
    }
    if(have_hand_tracking && have_hand_source) {
        XrSystemHandTrackingPropertiesEXT hands={XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
        XrSystemProperties system={XR_TYPE_SYSTEM_PROPERTIES,&hands};
        if(xrGetSystemProperties(instance,system_id,&system)==XR_SUCCESS && hands.supportsHandTracking) {
            xrGetInstanceProcAddr(instance,"xrCreateHandTrackerEXT",(PFN_xrVoidFunction *)&create_hand_tracker);
            xrGetInstanceProcAddr(instance,"xrDestroyHandTrackerEXT",(PFN_xrVoidFunction *)&destroy_hand_tracker);
            xrGetInstanceProcAddr(instance,"xrLocateHandJointsEXT",(PFN_xrVoidFunction *)&locate_hand_joints);
        }
    }
#undef PROC
    XrGraphicsRequirementsVulkan2KHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if (!XR_OK(requirements(instance, system_id, &req)) || !setup_actions()) return 0;
    const char *scale = getenv("NFSU2_VR_UNITS_PER_METER");
    if (scale && atof(scale) > 0.01 && atof(scale) < 1000) units_per_meter = (float)atof(scale);
    enabled = 1;
    return 1;
}

int quest_xr_enabled(void) { return enabled; }
void quest_xr_game_thread(void) {
#ifdef __ANDROID__
    game_tid=(uint32_t)gettid();
#endif
}
VkResult quest_xr_vk_instance(const VkInstanceCreateInfo *ci, VkInstance *vk)
{
    XrVulkanInstanceCreateInfoKHR info = {XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    info.systemId = system_id;
    info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    info.vulkanCreateInfo = ci;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (!XR_OK(create_vk_instance(instance, &info, vk, &result))) return VK_ERROR_INITIALIZATION_FAILED;
    return result;
}

VkPhysicalDevice quest_xr_vk_physical(VkInstance vk)
{
    XrVulkanGraphicsDeviceGetInfoKHR info = {XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    info.systemId = system_id;
    info.vulkanInstance = vk;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    XR_OK(get_vk_physical(instance, &info, &physical));
    return physical;
}

VkResult quest_xr_vk_device(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, VkDevice *out)
{
    XrVulkanDeviceCreateInfoKHR info = {XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    info.systemId = system_id;
    info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    info.vulkanPhysicalDevice = pd;
    info.vulkanCreateInfo = ci;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (!XR_OK(create_vk_device(instance, &info, out, &result))) return VK_ERROR_INITIALIZATION_FAILED;
    return result;
}

static int create_swapchain(QuestSwapchain *out, int64_t format, uint32_t width, uint32_t height)
{
    XrSwapchainCreateInfo sc = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    sc.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    sc.format = format; sc.width = width; sc.height = height;
    sc.sampleCount = 1; sc.faceCount = 1; sc.arraySize = 1; sc.mipCount = 1;
    if (!XR_OK(xrCreateSwapchain(session, &sc, &out->handle))) return 0;
    out->width = width; out->height = height; out->format = (VkFormat)format;
    if (!XR_OK(xrEnumerateSwapchainImages(out->handle, 0, &out->count, NULL))) return 0;
    out->images = calloc(out->count, sizeof *out->images);
    if (!out->images) return 0;
    for (uint32_t j = 0; j < out->count; ++j) out->images[j].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
    uint32_t count;
    return XR_OK(xrEnumerateSwapchainImages(out->handle, out->count, &count,
                (XrSwapchainImageBaseHeader *)out->images));
}

int quest_xr_start(VkInstance vk, VkPhysicalDevice pd, VkDevice dev, uint32_t family)
{
    device = dev; color_physical = pd;
    XrGraphicsBindingVulkan2KHR binding = {XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = vk; binding.physicalDevice = pd; binding.device = dev;
    binding.queueFamilyIndex = family;
    XrSessionCreateInfo ci = {XR_TYPE_SESSION_CREATE_INFO};
    ci.next = &binding; ci.systemId = system_id;
    XrResult created;
    uint32_t session_wait=SDL_GetTicks();
    do {
        created=xrCreateSession(instance,&ci,&session);
        if(created!=XR_ERROR_RUNTIME_FAILURE && created!=XR_ERROR_FORM_FACTOR_UNAVAILABLE)break;
        if(SDL_GetTicks()-session_wait>=15000)break;
        SDL_Delay(500);
    } while(1);
    if (!XR_OK(created)) return 0;
#ifdef __ANDROID__
    if(set_thread) {
        if(game_tid) XR_OK(set_thread(session,XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR,game_tid));
        XR_OK(set_thread(session,XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR,(uint32_t)gettid()));
    }
#endif
    XrSessionActionSetsAttachInfo attach = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1; attach.actionSets = &actions;
    if (!XR_OK(xrAttachSessionActionSets(session, &attach))) return 0;
    for(int hand=0;hand<2;++hand) {
        XrActionSpaceCreateInfo pose={XR_TYPE_ACTION_SPACE_CREATE_INFO};
        pose.action=hand_action[hand]; pose.poseInActionSpace.orientation.w=1;
        if(!XR_OK(xrCreateActionSpace(session,&pose,&hand_space[hand]))) return 0;
        if(create_hand_tracker && destroy_hand_tracker && locate_hand_joints) {
            XrHandTrackingDataSourceEXT sources[]={XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT,
                                                  XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT};
            XrHandTrackingDataSourceInfoEXT info={XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT,NULL,2,sources};
            XrHandTrackerCreateInfoEXT tracker={XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT,&info,
                hand?XR_HAND_RIGHT_EXT:XR_HAND_LEFT_EXT,XR_HAND_JOINT_SET_DEFAULT_EXT};
            XrHandTrackerEXT handle=XR_NULL_HANDLE;
            if(XR_OK(create_hand_tracker(session,&tracker,&handle)))replay_trackers[hand]=handle;
        }
    }
    if(replay_trackers[0] || replay_trackers[1])
        fprintf(stderr,"[XR] Replay hand-source tracking available: left=%d right=%d\n",
                replay_trackers[0]!=XR_NULL_HANDLE,replay_trackers[1]!=XR_NULL_HANDLE);
    XrReferenceSpaceCreateInfo space = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.poseInReferenceSpace.orientation.w = 1;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!XR_OK(xrCreateReferenceSpace(session, &space, &local_space))) return 0;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!XR_OK(xrCreateReferenceSpace(session, &space, &view_space))) return 0;
    XrViewConfigurationView config[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    uint32_t count = 0;
    if (!XR_OK(xrEnumerateViewConfigurationViews(instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, config)) || count != 2) return 0;
    if (!XR_OK(xrEnumerateSwapchainFormats(session, 0, &count, NULL))) return 0;
    int64_t *formats = calloc(count, sizeof *formats);
    if (!formats) return 0;
    if (!XR_OK(xrEnumerateSwapchainFormats(session, count, &count, formats))) { free(formats); return 0; }
    VkFormat format = quest_color_format(formats,count);
    free(formats);
    if (!format) { fprintf(stderr, "[XR] No supported sRGB swapchain format; cannot present game colors correctly\n"); return 0; }
    fprintf(stderr,"[XR] Display-encoded game colors: %s swapchains, gamma-preserving copy\n",
        format==VK_FORMAT_B8G8R8A8_SRGB?"BGRA8 sRGB":"RGBA8 sRGB");
    for (int eye = 0; eye < 2; ++eye) {
        uint32_t width=config[eye].recommendedImageRectWidth, height=config[eye].recommendedImageRectHeight;
#ifdef NFSU2_PCVR
        const char *w=getenv("NFSU2_VR_EYE_WIDTH"), *h=getenv("NFSU2_VR_EYE_HEIGHT");
        if(w && h && atoi(w)>=256 && atoi(h)>=256 &&
           (uint32_t)atoi(w)<=config[eye].maxImageRectWidth && (uint32_t)atoi(h)<=config[eye].maxImageRectHeight) {
            width=(uint32_t)atoi(w); height=(uint32_t)atoi(h);
        }
#endif
        if(vr_settings.resolution) {
            unsigned w=quest_vr_resolutions[vr_settings.resolution][0],h=quest_vr_resolutions[vr_settings.resolution][1];
            if(w<=config[eye].maxImageRectWidth && h<=config[eye].maxImageRectHeight) {width=w;height=h;}
            else fprintf(stderr,"[VR settings] %ux%u exceeds runtime limit; using %ux%u\n",w,h,width,height);
        }
        if (!create_swapchain(&eyes[eye], format, width, height)) return 0;
        views[eye].type = XR_TYPE_VIEW;
        fprintf(stderr, "[XR] eye %d: %ux%u, %u images\n", eye, eyes[eye].width, eyes[eye].height, eyes[eye].count);
    }
    /* A 4:3 menu image goes directly to the quad compositor. Do not squeeze
     * it through an eye-shaped stereo image and resample it a second time. */
    XrSystemProperties properties = {XR_TYPE_SYSTEM_PROPERTIES};
    if (!XR_OK(xrGetSystemProperties(instance, system_id, &properties))) return 0;
    uint32_t unit = 480;
    if (unit > properties.graphicsProperties.maxSwapchainImageWidth / 4)
        unit = properties.graphicsProperties.maxSwapchainImageWidth / 4;
    if (unit > properties.graphicsProperties.maxSwapchainImageHeight / 3)
        unit = properties.graphicsProperties.maxSwapchainImageHeight / 3;
    if (!unit || !create_swapchain(&cinema, format, unit * 4, unit * 3)) return 0;
    fprintf(stderr, "[XR] cinema: %ux%u; menu quad 4.8x3.6 m at 3 m\n", cinema.width, cinema.height);
    if(!vr_panel_initialize(pd,format))fprintf(stderr,"[XR] VR settings panel could not be created\n");
    return 1;
}

static void multiply(float *out, const float *a, const float *b)
{
    float m[16] = {0};
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k) m[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    memcpy(out, m, sizeof m);
}

static void pose_matrix(float *m, XrPosef p, int inverse)
{
    float x=p.orientation.x, y=p.orientation.y, z=p.orientation.z, w=p.orientation.w;
    float r[16] = {1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w), 0,
        2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w), 0,
        2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y), 0, 0,0,0,1};
    float t[3] = {p.position.x*units_per_meter, p.position.y*units_per_meter, p.position.z*units_per_meter};
    memcpy(m, r, sizeof r);
    if (inverse) {
        for (int c=0;c<3;++c) for(int row=0;row<3;++row) m[c*4+row]=r[row*4+c];
        for(int row=0;row<3;++row) m[12+row]=-(m[row]*t[0]+m[4+row]*t[1]+m[8+row]*t[2]);
    } else for(int row=0;row<3;++row) m[12+row]=t[row];
}

static void update_matrices(void)
{
    float base[16];
    pose_matrix(base, origin, 0);
    for (int eye=0;eye<2;++eye) {
        float view[16], combined[16], p[16]={0};
        pose_matrix(view, views[eye].pose, 1);
        multiply(combined, view, base);
        XrFovf f=views[eye].fov;
        float l=tanf(f.angleLeft), r=tanf(f.angleRight), d=tanf(f.angleDown), u=tanf(f.angleUp);
        /* Ordinary cameras keep retail depth precision. The seated view
         * also needs the original shell within 0.5 m to remain visible. */
        p[0]=2/(r-l); p[5]=-2/(u-d); p[8]=(r+l)/(r-l); p[9]=-(u+d)/(u-d);
        quest_projection_set_depth(p,0.5f,QUEST_WORLD_FAR);
        multiply(matrices[eye], p, combined);
        /* Seat controls move the common world/interior eye transform, not
         * the asynchronously generated guest camera. Mirror acquisition and
         * the retail shell therefore retain the same fixed car coordinates. */
        quest_cockpit_shift_eye(combined,vr_settings.height*units_per_meter,
                               vr_settings.distance*units_per_meter);
        /* Invert the rigid cabin-to-eye transform. The resulting eye
         * positions include IPD, recenter and the personal seat offsets. */
        for(int k=0;k<3;++k)
            mirror_eyes[eye][k]=-quest_mirror_dot(combined+4*k,combined+12)/units_per_meter;
        quest_projection_set_depth(p,QUEST_COCKPIT_WORLD_NEAR_M*units_per_meter,QUEST_WORLD_FAR);
        multiply(cockpit_world_matrices[eye],p,combined);
        /* Added interior uses a separate depth attachment. */
        quest_projection_set_depth(p,0.025f*units_per_meter,20.0f*units_per_meter);
        multiply(cockpit_matrices[eye],p,combined);
    }
    const float optical_seat[3]={0,vr_settings.height,vr_settings.distance};
    quest_mirror_comfort_eyes(mirror_eyes,optical_seat);
    float planes[6][4];
    int seated=quest_xr_cockpit_active();
    int valid=visible && quest_culling_stereo(planes,
        seated?cockpit_world_matrices[0]:matrices[0],
        seated?cockpit_world_matrices[1]:matrices[1],0.25f*units_per_meter);
    pthread_mutex_lock(&culling_lock);
    culling_valid=valid;
    if (valid) { memcpy(culling_planes,planes,sizeof planes); culling_tick=SDL_GetTicks(); }
    pthread_mutex_unlock(&culling_lock);
}

int quest_xr_culling_planes(float planes[6][4])
{
    pthread_mutex_lock(&culling_lock);
    int valid=culling_valid && (uint32_t)(SDL_GetTicks()-culling_tick)<500;
    if (valid) memcpy(planes,culling_planes,sizeof culling_planes);
    pthread_mutex_unlock(&culling_lock);
    return valid;
}

/* A finite pulse is renewed at most every 50 ms unless intensity changes.
 * If rendering stalls, hardware stops after 100 ms instead of buzzing on. */
static void poll_haptics(int focused)
{
    uint32_t now=SDL_GetTicks();
    pthread_mutex_lock(&input_lock);
    quest_haptics_focus(&haptics,focused && haptics_enabled);
    float road=quest_haptics_sample(&haptics,now);
    float engine=haptics.focused && visible && tracked && driving_active && cockpit_menu==QUEST_UI_HUD &&
        !vr_menu.open && now-driving_tick<250 && vr_settings.engine_rumble?engine_rumble:0;
    pthread_mutex_unlock(&input_lock);
    static float pad_sent;
    static uint32_t pad_tick;
    float pad_amplitude=vr_gamepad_selected?quest_mix_rumble(road,engine,1):0;
    if(vr_gamepad && (pad_amplitude!=pad_sent || (pad_amplitude>0 && now-pad_tick>=50))) {
        uint16_t strength=(uint16_t)(pad_amplitude*65535);
        SDL_GameControllerRumble(vr_gamepad,strength,strength,pad_amplitude>0?100:0);
        pad_sent=pad_amplitude;pad_tick=now;
    }
    static int trace=-1;
    if(trace<0) trace=getenv("NFSU2_VR_HAPTICS_TRACE")!=NULL;
    for(int hand=0;hand<2;++hand) {
        int allow_engine=!vr_settings.grip_feedback || (hand_valid[hand] &&
            (shift_motion.zone[hand] || (wheel_grip.grabbed[hand] && isfinite(frame_squeeze[hand]))));
        float amplitude=vr_gamepad_selected?0:quest_mix_rumble(road,engine,allow_engine);
        uint32_t click_age=now-shift_click_tick;
        int click=focused && haptics_enabled && !vr_menu.open && !vr_gamepad_selected &&
            vr_settings.cockpit.shift_click && hand_valid[hand] && shift_click_hand==hand+1 && click_age<28;
        if(click)amplitude=fmaxf(amplitude,.95f);
        if(amplitude==0 && haptic_sent[hand]==0)continue;
        if(amplitude>0 && click==haptic_click_sent[hand] && fabsf(amplitude-haptic_sent[hand])<1.0f/255.0f && now-haptic_sent_tick[hand]<50)continue;
        XrHapticActionInfo info={XR_TYPE_HAPTIC_ACTION_INFO};
        info.action=haptic_action[hand];
        XrResult result;
        if(amplitude>0) {
            XrHapticVibration pulse={XR_TYPE_HAPTIC_VIBRATION};
            pulse.duration=click?(XrDuration)(28-click_age)*1000000:100000000; pulse.frequency=click?180.f:XR_FREQUENCY_UNSPECIFIED;
            pulse.amplitude=amplitude;
            result=xrApplyHapticFeedback(session,&info,(const XrHapticBaseHeader *)&pulse);
        } else result=xrStopHapticFeedback(session,&info);
        if(XR_FAILED(result)) {
            static unsigned errors;
            if(errors++<4) check(result,"Touch haptics");
        }
        if(trace && (amplitude==0 || fabsf(amplitude-haptic_sent[hand])>=.02f))
            fprintf(stderr,"[XR] haptics hand=%d road=%.3f engine=%.3f amplitude=%.3f result=%d\n",
                    hand,road,allow_engine?engine:0,amplitude,(int)result);
        haptic_sent[hand]=result==XR_SUCCESS?amplitude:0;haptic_sent_tick[hand]=now;
        haptic_click_sent[hand]=result==XR_SUCCESS?click:0;
    }
}

/* Steam Link can expose a tracked bare hand as an Oculus Touch controller,
 * mapping index pinch to its trigger. Ask which device supplies each hand
 * at this frame's display time, before interpreting its buttons or poses. */
static unsigned replay_hand_sources(void)
{
    unsigned mask=replay_bare_hands;
    for(int h=0;h<2;++h) {
        if(!replay_trackers[h])continue;
        XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
        XrHandTrackingDataSourceStateEXT source={XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
        XrHandJointLocationsEXT locations={XR_TYPE_HAND_JOINT_LOCATIONS_EXT,&source,XR_FALSE,XR_HAND_JOINT_COUNT_EXT,joints};
        XrHandJointsLocateInfoEXT locate={XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,NULL,local_space,display_time};
        if(locate_hand_joints(replay_trackers[h],&locate,&locations)!=XR_SUCCESS || !source.isActive)continue;
        if(source.dataSource==XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT)mask|=1u<<h;
        else if(source.dataSource==XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT)mask&=~(1u<<h);
    }
    if(mask!=replay_bare_hands)fprintf(stderr,"[XR] Replay bare hands: left=%u right=%u\n",mask&1u,mask>>1);
    return mask;
}
static void poll_input(void)
{
    static QuestDrivingButtons driving_buttons;
    static int recenter_held;
    XrActiveActionSet active = {actions, XR_NULL_PATH};
    XrActionsSyncInfo sync = {XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets=1; sync.activeActionSets=&active;
    uint16_t buttons=0; uint8_t analog[8]={0}; int16_t sticks[4]={0};
    int focused=state == XR_SESSION_STATE_FOCUSED;
    int synced=focused && xrSyncActions(session,&sync)==XR_SUCCESS;
    pthread_mutex_lock(&input_lock);
    int replay=replay_open;
    uint32_t viewer=replay_viewer;
    pthread_mutex_unlock(&input_lock);
    if(!replay)replay_bare_hands=0;
    else if(synced)replay_bare_hands=replay_hand_sources();
    frame_driving=0;frame_map_valid=frame_map_down=0;
    frame_squeeze[0]=frame_squeeze[1]=NAN;
    if (focused) {
        int pressed[7]={0};
        int touch_y_valid=0,right_click_valid=0;
        for(int i=0;synced && i<ACTION_COUNT;++i) {
            static const uint8_t hand[ACTION_COUNT]={[A]=1,[B]=1,[RCLICK]=1,[RTRIGGER]=1,[RGRIP]=1,[RSTICK]=1};
            if(replay_bare_hands&(1u<<hand[i]))continue;
            XrActionStateGetInfo get={XR_TYPE_ACTION_STATE_GET_INFO}; get.action=action[i];
            if(i < LTRIGGER) {
                XrActionStateBoolean value={XR_TYPE_ACTION_STATE_BOOLEAN};
                if(XR_SUCCEEDED(xrGetActionStateBoolean(session,&get,&value)) && value.isActive) {
                    pressed[i]=value.currentState;
                    if(i==A || i==X)frame_map_valid|=i==A?QUEST_MAP_A:QUEST_MAP_X;
                    if(i==Y)touch_y_valid=1;
                    if(i==RCLICK)right_click_valid=1;
                }
            } else if(i < LSTICK) {
                XrActionStateFloat value={XR_TYPE_ACTION_STATE_FLOAT};
                if(XR_SUCCEEDED(xrGetActionStateFloat(session,&get,&value)) && value.isActive) {
                    uint8_t amount=(uint8_t)(fminf(1,fmaxf(0,value.currentState))*255);
                    if(i==LGRIP || i==RGRIP) frame_squeeze[i-LGRIP]=amount/255.0f;
                    else analog[i==LTRIGGER?6:7]=amount;
                }
            } else {
                XrActionStateVector2f value={XR_TYPE_ACTION_STATE_VECTOR2F};
                if(XR_SUCCEEDED(xrGetActionStateVector2f(session,&get,&value)) && value.isActive) {
                    int index=(i-LSTICK)*2;
                    sticks[index]=(int16_t)(fminf(1,fmaxf(-1,value.currentState.x))*32767);
                    sticks[index+1]=(int16_t)(fminf(1,fmaxf(-1,value.currentState.y))*32767);
                }
            }
        }
        for(int i=0;i<4;++i) analog[i]=pressed[i]?255:0;
        frame_map_down=(pressed[A]?QUEST_MAP_A:0)|(pressed[X]?QUEST_MAP_X:0);
        if(pressed[MENU]) buttons|=0x10;
        uint16_t pad_buttons=0;uint8_t pad_analog[8];int16_t pad_sticks[4];int pad_clicks[2]={0};
        int pad=vr_poll_gamepad(&pad_buttons,pad_analog,pad_sticks,pad_clicks);
        int touch=buttons || pressed[LCLICK] || pressed[RCLICK];
        for(int i=0;i<8;++i)touch|=analog[i]>20;
        for(int i=0;i<4;++i)touch|=abs(sticks[i])>8000;
        touch|=frame_squeeze[0]>.2f || frame_squeeze[1]>.2f;
        if(pad==2 && !wheel_grip.grabbed[0] && !wheel_grip.grabbed[1] && !shifter.hand && !mirror_grip.hand && !cabin_touch[0].control && !cabin_touch[1].control)vr_gamepad_selected=1;
        else if(touch)vr_gamepad_selected=0;
        quest_minimap_mask_buttons(map_shortcut.captured,analog);
        if(pad && vr_gamepad_selected) {
            buttons=pad_buttons;memcpy(analog,pad_analog,8);memcpy(sticks,pad_sticks,8);
            pressed[LCLICK]=pad_clicks[0];pressed[RCLICK]=pad_clicks[1];
            /* Keep tracked Grip for positioning the map, even with a pad
             * supplying the driving packet. Wheel/lever use cabin_input. */
        }
        pthread_mutex_lock(&input_lock);
        int can_toggle=(pad && vr_gamepad_selected) || (synced && !(replay_bare_hands&2u));
        if(quest_replay_button(&replay_button,can_toggle && cockpit_menu==QUEST_UI_REPLAY && replay_owner,
                               SDL_GetTicks(),&analog[B]))replay_toggle=!replay_toggle;
        pthread_mutex_unlock(&input_lock);
        int settings_consumed=vr_settings_input(buttons,analog,sticks);
        if(settings_consumed) {
            frame_map_valid=0;
            memset(&shift_input,0,sizeof shift_input);
            memset(analog,0,8);memset(sticks,0,8);buttons=0;
            memset(pressed,0,sizeof pressed);quest_wheel_reset(&wheel_grip);
        }
        pthread_mutex_lock(&input_lock);
        int replay_input=viewer && viewer==replay_viewer && cockpit_menu==QUEST_UI_REPLAY;
        int click_source=pad && vr_gamepad_selected?2:right_click_valid;
        if(quest_replay_click(&replay_click,replay_input && !settings_consumed?click_source:0,
                              pressed[LCLICK],pressed[RCLICK]))++replay_camera_steps;
        pthread_mutex_unlock(&input_lock);
        quest_driving_analog(&driving_buttons,SDL_GetTicks(),pressed[LCLICK],pressed[RCLICK],
                             quest_xr_cockpit_requested(),analog);
        if(replay_input) {
            analog[5]=0;driving_buttons.camera.consumed=1;driving_buttons.camera.pulsing=0;
        }
        int driving=driving_input_active() && !quest_xr_cockpit_menu_active() && !vr_menu.open;
        if(steamvr_runtime) {
            int gesture=quest_steamvr_y(&steamvr_pause,SDL_GetTicks(),touch_y_valid && !settings_consumed,
                                         driving,pressed[Y]);
            if(!(pad && vr_gamepad_selected))analog[Y]=(gesture&QUEST_Y_TAP)?255:0;
            if(gesture&QUEST_Y_START)buttons|=0x10;
        }
        static int previous_driving=-1;
        if(driving!=previous_driving) {
            fprintf(stderr,"[XR] input mode: %s\n",driving?"driving (A nitro, B handbrake, right stick career actions)":"menu (original face buttons and D-pad)");
            previous_driving=driving;
        }
        frame_driving=driving;
        int recenter_now=pressed[LCLICK] && pressed[RCLICK];
        if(recenter_now && !recenter_held) { recentered=0; quest_wheel_reset(&wheel_grip); }
        recenter_held=recenter_now;
    } else {
        memset(&driving_buttons,0,sizeof driving_buttons);
        replay_button=(QuestReplayButton){0};
        memset(&shift_input,0,sizeof shift_input);recenter_held=0;
    }
    frame_buttons=buttons; memcpy(frame_analog,analog,8); memcpy(frame_sticks,sticks,sizeof sticks);
    frame_focused=focused;
}

/* Sample controller poses at the same predicted display time as the eyes,
 * after recentering. All geometry/angle state belongs to the renderer thread;
 * publish the finished input packet atomically for the guest USB thread. */
static void poll_shifter_grip(int in_cabin,int pedal_active,const float position[2][3])
{
    uint32_t now=SDL_GetTicks();
    pthread_mutex_lock(&input_lock);
    shifter_vehicle=cockpit_vehicle_index;
    shifter_gamepad=vr_gamepad_selected;
    if(shifter_gamepad && grid_car){grid_car=0;shifter_request=QUEST_SHIFT_NONE;++gearbox_epoch;}
    /* The race director owns the lever on the grid. Retail may skip the
     * driver tick entirely, or report NO_INPUT until the starting signal. */
    int grid=grid_car && grid_car==cockpit_car;
    int before_start=grid && !grid_racing;
    int count=grid?grid_count:gearbox_count;
    int selected=grid?grid_gear:gearbox_gear;
    int live=gearbox_car && gearbox_manual && gearbox_count>=4 && gearbox_count<=6 &&
             gearbox_gear>=0 && gearbox_gear<=gearbox_count+1 && now-gearbox_tick<500;
    int usable=shifter_available && (before_start || (live && gearbox_car==cockpit_car));
    int active=in_cabin && (before_start?(cockpit_menu==QUEST_UI_HUD || cockpit_menu==QUEST_UI_NO_HUD):pedal_active);
    int old_hand=shifter.hand,old_gear=shifter.gear;
    float old_throw=fabsf(shifter.z);
    int request=QUEST_SHIFT_NONE;
    if(shifter_epoch!=gearbox_epoch || !active || !usable || shifter_gamepad ||
       frame_analog[2] || frame_analog[3]) {
        quest_shifter_reset(&shifter);shift_motion=(QuestShiftMotion){0};shift_click_hand=shift_direction=0;shifter_epoch=gearbox_epoch;shifter_request=QUEST_SHIFT_NONE;
        shift_feedback_gear=selected;
        if(usable){quest_shifter_detent(&shifter,selected,count);if(vr_settings.cockpit.shift.mode)shifter.x=shifter.z=0;}
    } else {
        shifter.first_only=grid;
        int busy[2];
        for(int h=0;h<2;++h)
            busy[h]=wheel_grip.grabbed[h] || mirror_grip.hand==h+1 || map_grip.hand==h+1 || screen_grip.grip.hand==h+1 || cabin_touch[h].pointing;
        int actual=grid || shifter_request==QUEST_SHIFT_NONE?selected:shifter_request;
        const QuestShiftConfig *config=&vr_settings.cockpit.shift;
        for(int h=0;h<2;++h)if(isfinite(frame_squeeze[h]) && frame_squeeze[h]<.15f)shifter.blocked[h]=0;
        if(!config->mode)request=quest_shifter_update(&shifter,now,shifter_vehicle,count,actual,hand_valid,position,frame_squeeze,busy);
        int motion=quest_shift_motion(&shift_motion,&shifter,config,now,shifter_vehicle,count,actual,
            hand_valid,position,hand_matrix,frame_squeeze,busy);
        if(motion!=QUEST_SHIFT_NONE)request=motion;
        shifter.at_lever|=shift_motion.zone[0] || shift_motion.zone[1];
        if(request!=QUEST_SHIFT_NONE){
            shifter_request=request;if(grid)grid_gear=request;
            if(request!=QUEST_SHIFT_N && request!=actual) {
                shift_click_tick=shift_flash_tick=now;
                shift_click_hand=shifter.hand?shifter.hand:shift_motion.hand;
                /* H-pattern shifts cross neutral. Compare against the last
                 * engaged detent so a lower forward gear still flashes blue. */
                shift_direction=request==shift_feedback_gear?0:request>shift_feedback_gear?1:-1;
                shift_feedback_gear=request;
                if(vr_settings.cockpit.shift_sound)vr_music_shift_click();
            }
        }
    }
    guide_gears=active && usable && (vr_settings.shift_guide==QUEST_GUIDE_ALWAYS ||
        (vr_settings.shift_guide==QUEST_GUIDE_AT_LEVER && shifter.at_lever))?count:0;
    int gear=before_start?grid_gear:(live?gearbox_gear:-1);
    int moving=!vr_settings.cockpit.shift.mode && shifter.hand && shifter.gear==QUEST_SHIFT_N;
    int withdrawing=!vr_settings.cockpit.shift.mode && shifter.hand && shifter.hand==old_hand && old_gear==shifter.gear &&
                    old_gear!=QUEST_SHIFT_N && fabsf(shifter.z)+.001f<old_throw;
    quest_pedals_update(&pedals,now,pedal_active || (before_start && active),frame_analog[7],frame_analog[6],
        gearbox_epoch,gear,moving,request!=QUEST_SHIFT_NONE || withdrawing);
    shifter_tick=now;
    pthread_mutex_unlock(&input_lock);
}
static void poll_wheel_grips(void)
{
    float inverse_origin[16],position[2][3]={{0}};
    pose_matrix(inverse_origin,origin,1);
    for(int hand=0;hand<2;++hand) {
        hand_valid[hand]=0;
        if(!frame_focused || !tracked || !recentered || !wheel_configured) continue;
        XrActionStateGetInfo get={XR_TYPE_ACTION_STATE_GET_INFO}; get.action=hand_action[hand];
        XrActionStatePose pose={XR_TYPE_ACTION_STATE_POSE};
        XrSpaceLocation loc={XR_TYPE_SPACE_LOCATION};
        XrSpaceLocationFlags required=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
            XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        if(xrGetActionStatePose(session,&get,&pose)!=XR_SUCCESS || !pose.isActive ||
           xrLocateSpace(hand_space[hand],local_space,display_time,&loc)!=XR_SUCCESS ||
           (loc.locationFlags&required)!=required) continue;
        float local[16],relative[16]; pose_matrix(local,loc.pose,0); multiply(relative,inverse_origin,local);
        memcpy(hand_matrix[hand],relative,64);
        for(int row=0;row<3;++row) {
            position[hand][row]=relative[12+row]/units_per_meter;
            hand_matrix[hand][12+row]=position[hand][row];
            /* Grip -Z runs from little finger to thumb, +X is the palm
             * normal with opposite handedness. Mesh +Y is fingertips. */
            hand_matrix[hand][row]=relative[8+row];
            hand_matrix[hand][4+row]=-relative[4+row];
            hand_matrix[hand][8+row]=relative[row]*(hand?1:-1);
        }
        /* Track in the immutable cabin's coordinates after moving the eye. */
        position[hand][1]+=vr_settings.height;position[hand][2]+=vr_settings.distance;
        hand_matrix[hand][13]=position[hand][1];hand_matrix[hand][14]=position[hand][2];
        hand_valid[hand]=1;
        for(int k=0;k<16;++k)if(!isfinite(hand_matrix[hand][k]))hand_valid[hand]=0;
    }
    /* A temporary invalid head/hand pose freezes grip motion. Only an actual
     * focus/camera/recenter change resets ownership and requires a regrip. */
    int in_cabin=frame_focused && recentered && wheel_configured && !vr_menu.open &&
                 quest_xr_cockpit_active();
    int cabin_input=in_cabin && !vr_gamepad_selected;
    int ui=quest_xr_cockpit_menu_active();
    int active=cabin_input && ui==QUEST_UI_HUD;
    int touch_active=in_cabin && ui!=QUEST_UI_MENU;
    /* Use this frame's physical stick for a new grab after stick takeover,
     * not the older guest packet (which may still contain the held angle). */
    /* Seed a new grab from the stick's effective steering, so switching
     * input sources does not apply the wheel's deadzone compensation twice. */
    float fallback=quest_wheel_stick_steering(frame_sticks[0]);
    int save_map=0;
    pthread_mutex_lock(&input_lock);
    uint32_t mirror_now=SDL_GetTicks();
    if(mirror_seen_epoch!=mirror_epoch) {
        save_map=release_map_grip();
        memset(mirror_mounts,0,sizeof mirror_mounts);quest_mirror_release(&mirror_grip);
        mirror_seen_epoch=mirror_epoch;mirror_vehicle=cockpit_vehicle_index;
        radio_count=0;cabin_touch[0]=cabin_touch[1]=(QuestCockpitTouch){.blocked=1};
    }
    int save_camera=camera_dirty;camera_dirty=0;
    int radio_busy[2]={wheel_grip.grabbed[0] || shifter.hand==1 || mirror_grip.hand==1,
                       wheel_grip.grabbed[1] || shifter.hand==2 || mirror_grip.hand==2};
    int screen_busy[2]={radio_busy[0] || map_grip.hand==1 || cabin_touch[0].control,
                        radio_busy[1] || map_grip.hand==2 || cabin_touch[1].control};
    int screen_active=cabin_input && visible && ui==QUEST_UI_HUD && screen_vehicle==cockpit_vehicle_index && frame_number-screen_draw_frame<=1;
    if(!screen_active)screen_stick_captured=0;
    save_map|=quest_screen_update(&screen_grip,&vr_settings.cockpit.screen,quest_cockpit_vehicle(cockpit_vehicle_index),
        mirror_now,screen_active,hand_valid,hand_matrix,frame_squeeze,screen_busy,frame_sticks[3]);
    if(screen_grip.grip.hand){radio_busy[screen_grip.grip.hand-1]=1;screen_stick_captured=1;}
    if(screen_stick_captured) {
        if(!screen_grip.grip.hand && abs(frame_sticks[2])<8192 && abs(frame_sticks[3])<8192)screen_stick_captured=0;
        frame_sticks[2]=frame_sticks[3]=0;shift_input=(QuestShiftInput){0};
    }
    int map_busy[2]={radio_busy[0] || cabin_touch[0].control,radio_busy[1] || cabin_touch[1].control};
    int map_active=in_cabin && visible && ui==QUEST_UI_HUD && map_vehicle==cockpit_vehicle_index &&
                   frame_number-map_draw_frame<=1;
    int can_open_map=map_active && frame_driving && !vr_gamepad_selected && frame_map_valid &&
                     cockpit_menu==QUEST_UI_HUD && world_map_available;
    save_map|=quest_minimap_update(&map_grip,&map_mount,&vr_settings.map,map_active,
        hand_valid,hand_matrix,frame_squeeze,map_busy);
    if(map_grip.hand)radio_busy[map_grip.hand-1]=1;
    int radio_active=touch_active && radio_owner==cockpit_car && radio_owner && mirror_now-radio_tick<500;
    int manual_lever=cabin_input && ui!=QUEST_UI_REPLAY && shifter_available && cockpit_car &&
        (grid_car==cockpit_car || (gearbox_car==cockpit_car && gearbox_manual && mirror_now-gearbox_tick<500));
    int seat=0;
    if(!radio_active)radio_count=0;
    for(int h=0;h<2;++h) {
        /* A fingertip can reach the keys while the palm approaches or leaves
         * the knob. Give this hand the same pickup zone as the gear lever. */
        int at_knob=manual_lever && quest_shift_touch_blocked(&shifter,&vr_settings.cockpit.shift,mirror_vehicle,position[h]);
        QuestRadioCommand cmd=quest_touch_update(&cabin_touch[h],quest_cockpit_vehicle(mirror_vehicle),
            touch_active && hand_valid[h] && !radio_busy[h] && !at_knob && !(replay_bare_hands&(1u<<h)),radio_active,hand_matrix[h],index_tip,
            cabin_touch[1-h].control);
        if(cmd.action==QUEST_COCKPIT_LED) {
            vr_settings.led_color=(vr_settings.led_color+1)%QUEST_LED_COLORS;vr_settings.led_strips=1;seat=1;
        } else if(cmd.action==QUEST_COCKPIT_PAGE) {vr_settings.cockpit.screen.page^=1;seat=1;
        } else if(cmd.action>=QUEST_SEAT_FORWARD && cmd.action<=QUEST_SEAT_DOWN) {
            int height=cmd.action==QUEST_SEAT_UP || cmd.action==QUEST_SEAT_DOWN;
            int increase=cmd.action==QUEST_SEAT_UP || cmd.action==QUEST_SEAT_BACK;
            quest_vr_adjust(&vr_settings,height?2:3,increase?1:-1);
            seat=1;
        } else if(cmd.action==QUEST_RADIO_VOLUME && radio_count && radio_queue[radio_count-1].action==cmd.action)
            radio_queue[radio_count-1]=cmd;
        else if(cmd.action && radio_count<8 && (cmd.action!=QUEST_RADIO_VOLUME || quest_radio_volume_changed(radio_volume,cmd.volume)))
            radio_queue[radio_count++]=cmd;
    }
    pthread_mutex_unlock(&input_lock);
    int busy[2]={wheel_grip.grabbed[0] || shifter.hand==1 || cabin_touch[0].control,
                 wheel_grip.grabbed[1] || shifter.hand==2 || cabin_touch[1].control};
    if(screen_grip.grip.hand) {
        int h=screen_grip.grip.hand-1;busy[h]=1;mirror_grip.blocked[h]=1;wheel_grip.blocked[h]=1;shifter.blocked[h]=1;
    }
    if(map_grip.hand) {
        int h=map_grip.hand-1;
        busy[h]=1;mirror_grip.blocked[h]=1;
        wheel_grip.blocked[h]=1;shifter.blocked[h]=1;
    }
    QuestMirrorSetting *setting=&mirror_settings[mirror_vehicle],before=*setting;
    QuestMirrorMount reachable[QUEST_MIRRORS];
    memcpy(reachable,mirror_mounts,sizeof reachable);
    for(int p=0;p<QUEST_MIRRORS;++p)if(mirror_now-mirror_mount_tick[p]>=500)reachable[p].valid=0;
    if(active || (in_cabin && ui==QUEST_UI_REPLAY))quest_mirror_grip(&mirror_grip,setting,reachable,mirror_now,hand_valid,hand_matrix,frame_squeeze,busy);
    else quest_mirror_release(&mirror_grip);
    if(memcmp(&before,setting,sizeof before)) {
        mirror_dirty=1;mirror_known[mirror_vehicle]=1;
    }
    int save_mirrors=mirror_dirty && !mirror_grip.hand &&
                     (vr_save_status>=0 || mirror_now-vr_save_tick>=5000);
    if(save_mirrors || save_camera || seat || save_map)vr_save_settings();
    for(int h=0;h<2;++h)if(cabin_touch[h].pointing) {wheel_grip.blocked[h]=1;shifter.blocked[h]=1;}
    if(mirror_grip.hand) {
        wheel_grip.blocked[mirror_grip.hand-1]=1;
        shifter.blocked[mirror_grip.hand-1]=1;
    }
    for(int h=0;h<2;++h)
        if(cabin_touch[h].control || mirror_grip.hand==h+1)map_grip.hover[h]=0;
    int open_map=quest_minimap_shortcut(&map_shortcut,mirror_now,can_open_map,&map_grip,
                                        frame_map_valid,frame_map_down);
    if(!vr_gamepad_selected)quest_minimap_mask_buttons(map_shortcut.captured,frame_analog);
    /* Resolve Touch buttons using this frame's hands before the driving
     * layout swaps A/B and adds right-stick career actions. */
    if(frame_focused)
        quest_driving_input(&shift_input,mirror_now,frame_driving,&frame_buttons,frame_analog,frame_sticks);
    if(open_map)frame_buttons|=0x0004; /* retail D-pad left */
    int pedal_active=frame_focused && driving_input_active() && quest_xr_cockpit_active() &&
                    !vr_menu.open && !quest_xr_cockpit_menu_active();
    poll_shifter_grip(in_cabin,pedal_active,position);
    if(shifter.hand)wheel_grip.blocked[shifter.hand-1]=1;
    quest_wheel_update(&wheel_grip,SDL_GetTicks(),active,hand_valid,position,frame_squeeze,fallback);
    for(int h=0;h<2;++h)
        if(wheel_grip.grabbed[h] || shifter.hand==h+1)
            map_grip.hover[h]=0;
    frame_sticks[0]=quest_wheel_input(&wheel_grip,active,frame_sticks[0]);
    /* Grips are separate pose/squeeze inputs. Preserve the camera command
     * emitted by the right stick click, including while gripping the wheel. */
    static int previous_mask=-1,trace=-1;
    if(trace<0) {
        const char *e=getenv("NFSU2_VR_GRIP_TRACE");trace=e && *e && *e!='0';
    }
    int mask=wheel_grip.grabbed[0]|(wheel_grip.grabbed[1]<<1);
    if(trace && mask!=previous_mask) {
        fprintf(stderr,"[XR] wheel grip mask=%d tracked=%d,%d squeeze=%.2f,%.2f blocked=%d,%d active=%d steering=%.3f\n",
            mask,hand_valid[0],hand_valid[1],frame_squeeze[0],frame_squeeze[1],
            wheel_grip.blocked[0],wheel_grip.blocked[1],active,wheel_grip.angle/QUEST_WHEEL_LOCK_RADIANS);
        previous_mask=mask;
    }
    pthread_mutex_lock(&input_lock);
    input_buttons=frame_buttons; memcpy(input_analog,frame_analog,8); memcpy(input_sticks,frame_sticks,sizeof input_sticks);
    pthread_mutex_unlock(&input_lock);
}

static void setup_refresh_rate(void)
{
    if (!enumerate_rates || !request_rate || !get_rate) return;
    float requested=(float)quest_refresh_option(getenv("NFSU2_VR_REFRESH_HZ"),getenv("NFSU2_VR_72FPS"));
    uint32_t count=0;
    if (requested && XR_OK(enumerate_rates(session,0,&count,NULL)) && count) {
        float *rates=calloc(count,sizeof *rates);
        if (rates && XR_OK(enumerate_rates(session,count,&count,rates))) {
            int supported=0;
            for(uint32_t i=0;i<count;++i) if(fabsf(rates[i]-requested)<0.01f) supported=1;
            if(supported) XR_OK(request_rate(session,requested));
            else fprintf(stderr,"[XR] %.0f Hz is not listed by the runtime\n",requested);
        }
        free(rates);
    }
    float current=0;
    if(XR_OK(get_rate(session,&current))) fprintf(stderr,"[XR] display refresh %.2f Hz (distinct from game FPS)\n",current);
}

int quest_xr_begin(void)
{
    if (!session || begun) return begun;
    XrEventDataBuffer event={XR_TYPE_EVENT_DATA_BUFFER};
    while(xrPollEvent(instance,&event)==XR_SUCCESS) {
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            state=((XrEventDataSessionStateChanged *)&event)->state;
            fprintf(stderr,"[XR] session state %d\n",state);
            if(state!=XR_SESSION_STATE_FOCUSED) {
                if(release_map_grip())vr_save_settings();
                quest_steamvr_y(&steamvr_pause,0,0,0,0);
                poll_haptics(0);quest_wheel_reset(&wheel_grip);quest_shifter_reset(&shifter);shift_motion=(QuestShiftMotion){0};shift_click_hand=shift_direction=0;
                quest_mirror_release(&mirror_grip);
                memset(&pedals,0,sizeof pedals);
                pthread_mutex_lock(&input_lock);
                replay_click=(QuestReplayClick){0};
                radio_count=0;cabin_touch[0]=cabin_touch[1]=(QuestCockpitTouch){.blocked=1};
                shifter_request=QUEST_SHIFT_NONE;
                input_buttons=0;memset(input_analog,0,sizeof input_analog);memset(input_sticks,0,sizeof input_sticks);
                pthread_mutex_unlock(&input_lock);
            }
            if(state==XR_SESSION_STATE_READY) { session_retry_pending=0;session_retry_count=0; }
            else if(state==XR_SESSION_STATE_STOPPING) { if(running)xrEndSession(session); running=0; }
            else if(state==XR_SESSION_STATE_EXITING || state==XR_SESSION_STATE_LOSS_PENDING) {
                running=0;
#ifdef NFSU2_PCVR
                extern void pcvr_vr_failure(const char *);
                pcvr_vr_failure("The headset session ended. Reconnect your headset, then select Retry to restart only the game.");
#endif
            }
        } else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            running=0;
#ifdef NFSU2_PCVR
            extern void pcvr_vr_failure(const char *);
            pcvr_vr_failure("The OpenXR runtime disconnected. Reconnect your headset and select Retry to restart the game.");
#endif
        } else if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) { recentered=0; quest_wheel_reset(&wheel_grip); }
        else if(event.type==XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB) {
            XrEventDataDisplayRefreshRateChangedFB *rate=(XrEventDataDisplayRefreshRateChangedFB *)&event;
            fprintf(stderr,"[XR] display refresh changed %.2f -> %.2f Hz\n",rate->fromDisplayRefreshRate,rate->toDisplayRefreshRate);
        }
        event=(XrEventDataBuffer){XR_TYPE_EVENT_DATA_BUFFER};
    }
    /* READY is an event, not a repeated notification. A transient failure
     * must not leave audio running forever without ever trying again. */
    if(!running && state==XR_SESSION_STATE_READY &&
       (!session_retry_pending || SDL_GetTicks()-session_retry_tick>=500)) {
        XrSessionBeginInfo begin={XR_TYPE_SESSION_BEGIN_INFO};
        begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        session_retry_pending=1;session_retry_tick=SDL_GetTicks();
        running=XR_OK(xrBeginSession(session,&begin));
        if(!running && ++session_retry_count>=30) {
#ifdef NFSU2_PCVR
            extern void pcvr_vr_failure(const char *);
            pcvr_vr_failure("The headset connection is ready, but OpenXR could not begin the VR session. Reconnect the headset and select Retry.");
#endif
        }
        if(running) {
            session_retry_pending=0;setup_refresh_rate();
            if(set_performance) {
                int cpu=XR_OK(set_performance(session,XR_PERF_SETTINGS_DOMAIN_CPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
                int gpu=XR_OK(set_performance(session,XR_PERF_SETTINGS_DOMAIN_GPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
                fprintf(stderr,"[XR] sustained performance hints: CPU=%d GPU=%d\n",cpu,gpu);
            }
        }
    }
    if(!running) { SDL_Delay(10); return 0; }
    XrFrameWaitInfo wait={XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frame={XR_TYPE_FRAME_STATE};
    XrResult waited=xrWaitFrame(session,&wait,&frame);
#ifdef NFSU2_PCVR
    if(waited==XR_ERROR_SESSION_LOST || waited==XR_ERROR_INSTANCE_LOST || waited==XR_SESSION_LOSS_PENDING) {
        extern void pcvr_vr_failure(const char *);
        pcvr_vr_failure("The headset stopped accepting VR frames. Reconnect it and select Retry to restart the game.");
    }
#endif
    if(!XR_OK(waited)) return 0;
#ifdef NFSU2_PCVR
    static XrDuration previous_period;
    if(frame.predictedDisplayPeriod>0 && frame.predictedDisplayPeriod!=previous_period) {
        fprintf(stderr,"[PCVR] runtime display period %.3f ms (%.2f Hz); independent of game FPS\n",
            frame.predictedDisplayPeriod/1e6,1e9/frame.predictedDisplayPeriod);
        previous_period=frame.predictedDisplayPeriod;
    }
#endif
    XrFrameBeginInfo begin={XR_TYPE_FRAME_BEGIN_INFO};
    if(!XR_OK(xrBeginFrame(session,&begin))) return 0;
    begun=1; visible=frame.shouldRender; display_time=frame.predictedDisplayTime;
    poll_input();
    XrViewLocateInfo locate={XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime=display_time; locate.space=local_space;
    XrViewState view_state={XR_TYPE_VIEW_STATE}; uint32_t count;
    tracked=XR_OK(xrLocateViews(session,&locate,&view_state,2,&count,views)) && count==2 &&
        (view_state.viewStateFlags & (XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT)) ==
        (XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT);
    if(tracked && !recentered) {
        XrSpaceLocation head={XR_TYPE_SPACE_LOCATION};
        if(XR_SUCCEEDED(xrLocateSpace(view_space,local_space,display_time,&head)) &&
           (head.locationFlags & (XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) ==
           (XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            origin=head.pose;
            XrQuaternionf q=head.pose.orientation;
            float yaw=atan2f(2*(q.w*q.y+q.x*q.z),1-2*(q.y*q.y+q.x*q.x));
            origin.orientation=(XrQuaternionf){0,sinf(yaw/2),0,cosf(yaw/2)};
            recentered=1;
            if(release_map_grip())vr_save_settings();
            quest_mirror_release(&mirror_grip);
            quest_wheel_reset(&wheel_grip);
            quest_shifter_reset(&shifter);shift_motion=(QuestShiftMotion){0};shift_click_hand=shift_direction=0;
            pthread_mutex_lock(&input_lock);shifter_request=QUEST_SHIFT_NONE;
            radio_count=0;cabin_touch[0]=cabin_touch[1]=(QuestCockpitTouch){.blocked=1};pthread_mutex_unlock(&input_lock);
        }
    }
    poll_wheel_grips();
    poll_haptics(frame_focused);
    if(tracked && recentered) update_matrices();
    ++frame_number;
    if(frame_number <= 3 || frame_number % 300 == 0)
        fprintf(stderr,"[XR] frame %llu: render=%d tracked=%d recentered=%d flags=0x%llx state=%d\n",
            (unsigned long long)frame_number,visible,tracked,recentered,
            (unsigned long long)view_state.viewStateFlags,state);
    return visible && tracked;
}

void quest_xr_matrix(int eye,int cockpit,float matrix[16])
{
    memcpy(matrix,cockpit?cockpit_world_matrices[eye]:matrices[eye],64);
}
float quest_xr_depth_coefficient(int cockpit)
{
    float near_z=cockpit?QUEST_COCKPIT_WORLD_NEAR_M*units_per_meter:0.5f;
    return -QUEST_WORLD_FAR*near_z/(QUEST_WORLD_FAR-near_z);
}
int quest_xr_tracking(void) { return begun && visible && tracked && recentered; }

void quest_xr_eye_extent(uint32_t *width,uint32_t *height)
{
    *width=eyes[0].width; *height=eyes[0].height;
}

void quest_xr_menu_extent(uint32_t *width,uint32_t *height)
{
    *width=cinema.width; *height=cinema.height;
}

void quest_xr_copy(VkCommandBuffer cb,VkImage left,VkImage right,uint32_t width,uint32_t height,int stereo,int menu,uint32_t source_layers)
{
    stereo_frame=stereo; menu_frame=menu;
    if(!begun || !visible || !tracked || !left) return;
    for(int eye=0;eye<(stereo?2:1);++eye) {
        QuestSwapchain *target=stereo?&eyes[eye]:&cinema;
        VkResult color_result=quest_color_prepare(color_physical,device,&target->color_transfer,
            target->format,width,height,target->width,target->height);
        if(color_result!=VK_SUCCESS) {
            fprintf(stderr,"[XR] Color transfer allocation failed: %d\n",color_result);
            visible=0;return; /* Never submit an uninitialized eye image. */
        }
        XrSwapchainImageAcquireInfo acq={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(!XR_OK(xrAcquireSwapchainImage(target->handle,&acq,&target->index))) return;
        target->acquired=1;
        XrSwapchainImageWaitInfo wait={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout=XR_INFINITE_DURATION;
        if(!XR_OK(xrWaitSwapchainImage(target->handle,&wait))) return;
        VkImage image=target->images[target->index].image;
        VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=image;
        barrier.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        quest_color_copy(cb,&target->color_transfer,eye && right?right:left,
            source_layers==2?(uint32_t)eye:0,width,height,image,target->format,target->width,target->height);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&barrier);
    }
    vr_panel_copy(cb);
}

void quest_xr_end(void)
{
    if(!begun) return;
    int have=stereo_frame?(eyes[0].acquired && eyes[1].acquired):cinema.acquired;
    for(int i=0;i<3;++i) {
        QuestSwapchain *target=i<2?&eyes[i]:&cinema;
        if (target->acquired) {
            XrSwapchainImageReleaseInfo release={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            XR_OK(xrReleaseSwapchainImage(target->handle,&release)); target->acquired=0;
        }
    }
    XrCompositionLayerProjectionView projection_views[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    for(int eye=0;eye<2;++eye) {
        projection_views[eye].pose=views[eye].pose; projection_views[eye].fov=views[eye].fov;
        projection_views[eye].subImage.swapchain=eyes[eye].handle;
        projection_views[eye].subImage.imageRect.extent=(XrExtent2Di){eyes[eye].width,eyes[eye].height};
    }
    XrCompositionLayerProjection projection={XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection.space=local_space; projection.viewCount=2; projection.views=projection_views;
    XrCompositionLayerQuad quad={XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.space=local_space; quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain=cinema.handle;
    quad.subImage.imageRect.extent=(XrExtent2Di){cinema.width,cinema.height};
    quad.pose=origin;
    /* A stationary screen for pre-rendered movies and menu bring-up. */
    float yaw=2*atan2f(origin.orientation.y,origin.orientation.w);
    quad.pose.position.x-=sinf(yaw)*3; quad.pose.position.z-=cosf(yaw)*3;
    quad.size=menu_frame?(XrExtent2Df){4.8f,3.6f}:(XrExtent2Df){3.6f,2.7f};
    const XrCompositionLayerBaseHeader *layer=stereo_frame?
        (XrCompositionLayerBaseHeader *)&projection:(XrCompositionLayerBaseHeader *)&quad;
    const XrCompositionLayerBaseHeader *layers[2]={layer,NULL};
    int have_panel=vr_panel_swap.acquired;
    XrCompositionLayerQuad panel={XR_TYPE_COMPOSITION_LAYER_QUAD};
    panel.space=local_space;panel.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    panel.subImage.swapchain=vr_panel_swap.handle;
    panel.subImage.imageRect.extent=(XrExtent2Di){1024,vr_menu.open?768:80};
    panel.pose=origin;
    panel.pose.position.x-=sinf(yaw)*2.7f;panel.pose.position.z-=cosf(yaw)*2.7f;
    panel.pose.position.y+=vr_menu.open?0:-1.42f;
    panel.size=vr_menu.open?(XrExtent2Df){2.6f,1.95f}:(XrExtent2Df){1.9f,.1484375f};
    if(have_panel) {
        XrSwapchainImageReleaseInfo release={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        XR_OK(xrReleaseSwapchainImage(vr_panel_swap.handle,&release));vr_panel_swap.acquired=0;
        layers[1]=(const XrCompositionLayerBaseHeader *)&panel;
    }
    XrFrameEndInfo end={XR_TYPE_FRAME_END_INFO};
    end.displayTime=display_time; end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount=have && visible && tracked ? 1+have_panel:0; end.layers=layers;
    if(XR_OK(xrEndFrame(session,&end)) && end.layerCount) {
        ++submitted_frames;
        if(submitted_frames <= 3 || submitted_frames % 300 == 0)
            fprintf(stderr,"[XR] submitted %llu: %s\n",(unsigned long long)submitted_frames,
                stereo_frame?"stereo projection":"cinema quad");
    }
    begun=0;
}

int quest_xr_input(uint16_t *buttons,uint8_t analog[8],int16_t sticks[4])
{
    if(!enabled) return 0;
    pthread_mutex_lock(&input_lock);
    *buttons=input_buttons; memcpy(analog,input_analog,8); memcpy(sticks,input_sticks,8);
    pthread_mutex_unlock(&input_lock);
    return 1;
}

void quest_xr_shutdown(void)
{
    pthread_mutex_lock(&input_lock);int save_camera=camera_dirty;camera_dirty=0;pthread_mutex_unlock(&input_lock);
    int save_map=release_map_grip();
    if(mirror_dirty || save_camera || save_map)vr_save_settings();
    if(session) poll_haptics(0);
    if(device) vkDeviceWaitIdle(device);
    if(begun) quest_xr_end();
    if(vr_gamepad){SDL_GameControllerClose(vr_gamepad);vr_gamepad=NULL;}
    if(vr_panel_pixels)vkUnmapMemory(device,vr_panel_memory);
    if(vr_panel_buffer)vkDestroyBuffer(device,vr_panel_buffer,NULL);
    if(vr_panel_memory)vkFreeMemory(device,vr_panel_memory,NULL);
    if(vr_panel_swap.handle)xrDestroySwapchain(vr_panel_swap.handle);
    free(vr_panel_swap.images);free(vr_panel_uploaded);
    quest_color_destroy(device,&cinema.color_transfer);
    for(int i=0;i<2;++i) { quest_color_destroy(device,&eyes[i].color_transfer); if(eyes[i].handle) xrDestroySwapchain(eyes[i].handle); free(eyes[i].images); }
    if(cinema.handle) xrDestroySwapchain(cinema.handle);
    free(cinema.images);
    if(view_space) xrDestroySpace(view_space);
    if(local_space) xrDestroySpace(local_space);
    for(int hand=0;hand<2;++hand) if(hand_space[hand]) xrDestroySpace(hand_space[hand]);
    for(int hand=0;hand<2;++hand)if(replay_trackers[hand])destroy_hand_tracker(replay_trackers[hand]);
    if(session) xrDestroySession(session);
    if(actions) xrDestroyActionSet(actions);
    if(instance) xrDestroyInstance(instance);
#ifdef __ANDROID__
    JNIEnv *env=SDL_AndroidGetJNIEnv();
    if(activity && env) (*env)->DeleteGlobalRef(env,activity);
#endif
    enabled=running=begun=visible=tracked=recentered=0;
    instance=XR_NULL_HANDLE;session=XR_NULL_HANDLE;actions=XR_NULL_HANDLE;
    system_id=XR_NULL_SYSTEM_ID;view_space=local_space=XR_NULL_HANDLE;
    memset(hand_space,0,sizeof hand_space);memset(hand_action,0,sizeof hand_action);
    memset(replay_trackers,0,sizeof replay_trackers);replay_bare_hands=0;
    create_hand_tracker=NULL;destroy_hand_tracker=NULL;locate_hand_joints=NULL;
    memset(action,0,sizeof action);memset(haptic_action,0,sizeof haptic_action);
    memset(eyes,0,sizeof eyes);memset(&cinema,0,sizeof cinema);
    memset(&vr_panel_swap,0,sizeof vr_panel_swap);vr_panel_uploaded=NULL;
    vr_panel_buffer=VK_NULL_HANDLE;vr_panel_memory=VK_NULL_HANDLE;vr_panel_pixels=NULL;
    device=VK_NULL_HANDLE;color_physical=VK_NULL_HANDLE;state=XR_SESSION_STATE_UNKNOWN;
    set_performance=NULL;enumerate_rates=NULL;request_rate=NULL;get_rate=NULL;
    session_retry_pending=0;
#ifdef __ANDROID__
    activity=NULL;
#endif
}
