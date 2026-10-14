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
void quest_xr_matrix(int eye, float matrix[16]);
int quest_xr_culling_planes(float planes[6][4]); /* thread-safe guest camera planes */
float quest_xr_depth_coefficient(void);
int quest_xr_tracking(void);
void quest_xr_eye_extent(uint32_t *width, uint32_t *height);
void quest_xr_menu_extent(uint32_t *width, uint32_t *height);
int quest_xr_input(uint16_t *buttons, uint8_t analog[8], int16_t sticks[4]);
void quest_xr_shutdown(void);
