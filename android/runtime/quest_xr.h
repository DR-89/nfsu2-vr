#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>

/* SDL's Java thread provides the Android loader context before guest boot. */
int quest_xr_initialize(void);
int quest_xr_enabled(void);
void quest_xr_game_thread(void);
VkResult quest_xr_vk_instance(const VkInstanceCreateInfo *, VkInstance *);
VkPhysicalDevice quest_xr_vk_physical(VkInstance);
VkResult quest_xr_vk_device(VkPhysicalDevice, const VkDeviceCreateInfo *, VkDevice *);
int quest_xr_start(VkInstance, VkPhysicalDevice, VkDevice, uint32_t);
int quest_xr_begin(void);
void quest_xr_copy(VkCommandBuffer, VkImage left, VkImage right, uint32_t width, uint32_t height, int stereo, int menu, uint32_t source_layers);
/* Call on the queue-owning render thread after vkQueueSubmit. OpenXR orders
 * its compositor use after that submission; no CPU fence wait is required. */
void quest_xr_end(void);
void quest_xr_matrix(int eye, int cockpit, float matrix[16]);
/* Shared cockpit selection; publication occurs on the guest camera thread. */
void quest_xr_cockpit_ready(int ready);
int quest_xr_cockpit_requested(void);
void quest_xr_cockpit_publish(int available, int active);
#include "quest_cockpit_windows.h"
void quest_xr_cockpit_vehicle_publish(uint32_t car,int index,const uint32_t windows[QUEST_COCKPIT_WINDOWS]);
int quest_xr_cockpit_vehicle_snapshot(uint32_t windows[QUEST_COCKPIT_WINDOWS]);
void quest_xr_driving_publish(int driving);
void quest_xr_cockpit_menu_publish(int menu);
void quest_xr_pause_publish(int paused);
int quest_xr_cockpit_menu_active(void);
int quest_xr_cockpit_active(void);
int quest_xr_cockpit_enabled(void);
void quest_xr_cockpit_select(int selected);
/* 0..4 retail slots, 5/6 cockpit entered after/before the retail cycle. */
int quest_xr_camera_choice(void);
void quest_xr_camera_remember(int choice);
#include "quest_radio.h"
QuestRadioCommand quest_xr_radio_exchange(uint32_t owner,float volume,int paused,const QuestRadioSong *song);
/* Returns the controls currently reachable by an armed Grip, one bit per control. */
uint32_t quest_xr_radio_display(float state[3],QuestRadioSong *song);
void quest_xr_cockpit_matrix(int eye, float matrix[16]);
void quest_xr_cockpit_steering_input(int16_t steering);
float quest_xr_cockpit_steering(int *from_rim);
/* Renderer-thread fitted wheel and controller poses, in cabin meters. */
void quest_xr_wheel_configure(const float pivot[3],const float axis[3],float radius);
int quest_xr_hand_matrix(int hand,float steering,float matrix[16],int *grasped);
/* Player-driver tick (guest thread); returns -1 while retail input owns gears. */
int quest_xr_shifter_stage(uint32_t car,int stage,int previous,int manual_gears);
uint32_t quest_xr_shifter_grid_car(void);
int quest_xr_shifter_grid_request(uint32_t car);
int quest_xr_shifter_driver(uint32_t car,int count,int gear,int manual,int locked);
void quest_xr_shifter_configure(int available);
void quest_xr_shifter_offset(float *x,float *z);
void quest_xr_pedal_travel(float out[3]);
#include "quest_mirrors.h"
/* Camera exchange runs on the guest thread. Glass and tracked poses are
 * owned by the OpenXR/Vulkan thread. */
void quest_xr_mirror_camera(uint32_t car,QuestMirrorCamera *camera,float rear_view[16]);
void quest_xr_mirror_mounts(int vehicle,const QuestMirrorMount mounts[3]);
QuestMirrorAngle quest_xr_mirror_angle(int vehicle,int part);
void quest_xr_mirror_normal(float out[3],const QuestMirrorMount *mount,int part,QuestMirrorAngle angle);
void quest_xr_mirror_eyes(float eyes[2][4]);
int quest_xr_mirror_source(QuestMirrorCamera *camera,int *vehicle);
int quest_xr_culling_planes(float planes[6][4]); /* thread-safe guest camera planes */
float quest_xr_depth_coefficient(int cockpit);
int quest_xr_tracking(void);
void quest_xr_eye_extent(uint32_t *width, uint32_t *height);
void quest_xr_menu_extent(uint32_t *width, uint32_t *height);
int quest_xr_input(uint16_t *buttons, uint8_t analog[8], int16_t sticks[4]);
/* Thread-safe Xbox output reports; actual OpenXR calls stay on the render thread. */
void quest_xr_haptic_input(uint16_t low, uint16_t high);
void quest_xr_shutdown(void);
