#ifndef QUEST_COLOR_TRANSFER_H
#define QUEST_COLOR_TRANSFER_H

#include <vulkan/vulkan.h>
#include <stdint.h>
#include <string.h>

/* The Xbox renderer writes display-encoded BGRA bytes to UNORM targets.
 * OpenXR must receive those bytes in an SRGB swapchain. A UNORM -> SRGB
 * blit would encode them a second time, just like submitting UNORM to XR.
 * Copy matching BGRA images verbatim; resize/channel-convert in UNORM first
 * when necessary, then copy the encoded bytes into the SRGB image.
 * This leaves retail texture sampling, blending and postprocessing alone. */
typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    uint32_t width, height;
    VkFormat format;
} QuestColorTransfer;

static VkFormat quest_color_format(const int64_t *formats, uint32_t count)
{
    /* Matching the guest's BGRA order avoids an intermediate in stereo. */
    const VkFormat preferred[2]={VK_FORMAT_B8G8R8A8_SRGB,VK_FORMAT_R8G8B8A8_SRGB};
    for(unsigned p=0;p<2;++p)
        for(uint32_t i=0;i<count;++i)
            if(formats[i]==preferred[p])return preferred[p];
    return VK_FORMAT_UNDEFINED;
}

static int quest_color_direct(VkFormat format,uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh)
{
    return format==VK_FORMAT_B8G8R8A8_SRGB && sw==dw && sh==dh;
}

static uint32_t quest_color_panel_pixel(uint32_t bgra,VkFormat format)
{
    return format==VK_FORMAT_R8G8B8A8_SRGB?
        (bgra&0xff00ff00u)|((bgra&255u)<<16)|((bgra>>16)&255u):bgra;
}

static void quest_color_destroy(VkDevice dev,QuestColorTransfer *t)
{
    if(t->image)vkDestroyImage(dev,t->image,NULL);
    if(t->memory)vkFreeMemory(dev,t->memory,NULL);
    memset(t,0,sizeof *t);
}

/* One scratch per fixed-size XR target, reused on the renderer's queue.
 * No per-frame allocations and no CPU readback. Call before acquiring XR. */
static VkResult quest_color_prepare(VkPhysicalDevice physical,VkDevice dev,
    QuestColorTransfer *t,VkFormat format,uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh)
{
    if(format!=VK_FORMAT_B8G8R8A8_SRGB && format!=VK_FORMAT_R8G8B8A8_SRGB)
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if(quest_color_direct(format,sw,sh,dw,dh))return VK_SUCCESS;
    VkFormat unorm=format==VK_FORMAT_B8G8R8A8_SRGB?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_R8G8B8A8_UNORM;
    if(t->image)return t->width==dw && t->height==dh && t->format==unorm?
        VK_SUCCESS:VK_ERROR_INITIALIZATION_FAILED; /* Never free an in-flight image. */
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physical,unorm,&properties);
    if(!(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType=VK_IMAGE_TYPE_2D;ci.format=unorm;
    ci.extent=(VkExtent3D){dw,dh,1};ci.mipLevels=1;ci.arrayLayers=1;
    ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;
    ci.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkResult result=vkCreateImage(dev,&ci,NULL,&t->image);
    if(result!=VK_SUCCESS)return result;
    VkMemoryRequirements req;VkPhysicalDeviceMemoryProperties memory;
    vkGetImageMemoryRequirements(dev,t->image,&req);
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    uint32_t type=~0u;
    for(uint32_t i=0;i<memory.memoryTypeCount;++i)
        if((req.memoryTypeBits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {type=i;break;}
    if(type==~0u){quest_color_destroy(dev,t);return VK_ERROR_FEATURE_NOT_PRESENT;}
    VkMemoryAllocateInfo alloc={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;
    result=vkAllocateMemory(dev,&alloc,NULL,&t->memory);
    if(result==VK_SUCCESS)result=vkBindImageMemory(dev,t->image,t->memory,0);
    if(result!=VK_SUCCESS){quest_color_destroy(dev,t);return result;}
    t->width=dw;t->height=dh;t->format=unorm;
    return VK_SUCCESS;
}

/* Caller makes the guest source readable in GENERAL and the acquired
 * destination writable in TRANSFER_DST_OPTIMAL, and transitions it for XR
 * after this call. source_layer selects either eye of multiview images. */
static void quest_color_copy(VkCommandBuffer cb,const QuestColorTransfer *t,
    VkImage source,uint32_t source_layer,uint32_t sw,uint32_t sh,
    VkImage destination,VkFormat format,uint32_t dw,uint32_t dh)
{
    VkImageCopy copy={0};
    copy.srcSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,source_layer,1};
    copy.dstSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    copy.extent=(VkExtent3D){dw,dh,1};
    VkImageLayout source_layout=VK_IMAGE_LAYOUT_GENERAL;
    if(!quest_color_direct(format,sw,sh,dw,dh)) {
        VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=t->image;b.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        /* Discard old pixels, but wait for the preceding copy's read before
         * reusing scratch, including across multiple queued frames. */
        b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,0,NULL,0,NULL,1,&b);
        VkImageBlit blit={0};
        blit.srcSubresource=copy.srcSubresource;blit.dstSubresource=copy.dstSubresource;
        blit.srcOffsets[1]=(VkOffset3D){(int32_t)sw,(int32_t)sh,1};
        blit.dstOffsets[1]=(VkOffset3D){(int32_t)dw,(int32_t)dh,1};
        vkCmdBlitImage(cb,source,source_layout,t->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,&blit,VK_FILTER_LINEAR);
        b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,0,NULL,0,NULL,1,&b);
        source=t->image;source_layout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        copy.srcSubresource.baseArrayLayer=0;
    }
    /* vkCmdCopyImage is a bit-preserving copy, unlike vkCmdBlitImage. */
    vkCmdCopyImage(cb,source,source_layout,destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
}

#endif
