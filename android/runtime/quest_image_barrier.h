#ifndef QUEST_IMAGE_BARRIER_H
#define QUEST_IMAGE_BARRIER_H

#include <vulkan/vulkan.h>

/* All GPU image accesses used by the Quest renderer. Vertex/index/uniform
 * buffers are host-written, immutable until the owning frame fence, and never
 * written by GPU commands. Vertex shaders perform no image accesses. This
 * permits the next pass's vertex work to overlap earlier fragment work while
 * retaining RAW/WAW/WAR ordering for every image operation, including FSR reads.
 * Revisit this mask if compute, vertex textures or GPU buffer writes are added. */
static inline VkPipelineStageFlags quest_image_stages(int foveation)
{
    VkPipelineStageFlags stages = VK_PIPELINE_STAGE_TRANSFER_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (foveation) stages |= VK_PIPELINE_STAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
    return stages;
}

#endif
