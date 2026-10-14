#pragma once
#include <vulkan/vulkan.h>

/* These images are only attachments after their initial transfer clear.
 * Retain both aspects and every array layer when switching comparison modes.
 * Conservative stages/accesses deliberately match the existing ordering. */
static inline void quest_depth_layout_transition(VkCommandBuffer command,
    VkImage image, VkImageLayout *layout, uint32_t layers,
    VkImageAspectFlags aspects, int optimal)
{
    VkImageLayout next=optimal?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
    if(*layout==next) return;
    VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=barrier.srcAccessMask;
    barrier.oldLayout=*layout; barrier.newLayout=next;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=image;
    barrier.subresourceRange=(VkImageSubresourceRange){aspects,0,1,0,layers};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,0,NULL,0,NULL,1,&barrier);
    *layout=next;
}
