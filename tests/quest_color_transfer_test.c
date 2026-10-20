/* GPU regression for the production XR color handoff. No game assets or
 * headset session needed. Tests encoded ramps, alpha, channel order, both
 * multiview layers, resizing, scratch reuse, and simulated compositor decode.
 * The old UNORM presentation is a negative control (128 becomes ~188). */
#include "quest_color_transfer.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define CHECK(call) do { VkResult e=(call); if(e!=VK_SUCCESS) { \
    fprintf(stderr,"line %d: %s = %d\n",__LINE__,#call,e); exit(1); } } while(0)
#define REQUIRE(ok) do { if(!(ok)) { fprintf(stderr,"line %d: %s\n",__LINE__,#ok);exit(1); } } while(0)
typedef struct { VkImage image; VkDeviceMemory memory; } Image;
static VkDevice dev;
static VkPhysicalDevice physical;
static VkPhysicalDeviceMemoryProperties memory;
static VkCommandBuffer cb;
static VkQueue queue;
static VkBuffer buffer;
static VkDeviceMemory buffer_memory;
static unsigned char *pixels;
static unsigned errors,checked;

static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,const VkDebugUtilsMessengerCallbackDataEXT *data,void *user)
{
    (void)type;(void)user;
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)++errors;
    fprintf(stderr,"%s\n",data->pMessage);return VK_FALSE;
}
static uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags flags)
{
    for(uint32_t i=0;i<memory.memoryTypeCount;++i)
        if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
    REQUIRE(0);return 0;
}
static Image image(VkFormat format,uint32_t w,uint32_t h,uint32_t layers)
{
    Image out={0};
    VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent=(VkExtent3D){w,h,1};
    ci.mipLevels=1;ci.arrayLayers=layers;ci.samples=VK_SAMPLE_COUNT_1_BIT;
    ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    CHECK(vkCreateImage(dev,&ci,NULL,&out.image));
    VkMemoryRequirements req;vkGetImageMemoryRequirements(dev,out.image,&req);
    VkMemoryAllocateInfo alloc={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize=req.size;alloc.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CHECK(vkAllocateMemory(dev,&alloc,NULL,&out.memory));
    CHECK(vkBindImageMemory(dev,out.image,out.memory,0));return out;
}
static void destroy(Image i){vkDestroyImage(dev,i.image,NULL);vkFreeMemory(dev,i.memory,NULL);}
static void transition(VkImage i,uint32_t layers,VkImageLayout old,VkImageLayout next)
{
    VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image=i;b.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layers};
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.oldLayout=old;b.newLayout=next;
    b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);
}
static void begin(void)
{
    CHECK(vkResetCommandBuffer(cb,0));
    VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cb,&begin));
}
static void submit(void)
{
    VkMemoryBarrier host={VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo si={VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;
    CHECK(vkQueueSubmit(queue,1,&si,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(queue));
}
static VkBufferImageCopy region(uint32_t w,uint32_t h,uint32_t layers,VkDeviceSize offset)
{
    VkBufferImageCopy r={0};r.bufferOffset=offset;
    r.imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,layers};
    r.imageExtent=(VkExtent3D){w,h,1};return r;
}
static void blit(VkImage source,VkImage dest,uint32_t w,uint32_t h)
{
    VkImageBlit r={0};
    r.srcSubresource=r.dstSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    r.srcOffsets[1]=r.dstOffsets[1]=(VkOffset3D){(int32_t)w,(int32_t)h,1};
    vkCmdBlitImage(cb,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dest,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&r,VK_FILTER_NEAREST);
}
static double decode(double x){x/=255;return 255*(x<=.04045?x/12.92:pow((x+.055)/1.055,2.4));}
static void ramp_test(VkFormat format,uint32_t w,uint32_t h)
{
    Image source=image(VK_FORMAT_B8G8R8A8_UNORM,256,4,2),target=image(format,w,h,1);
    Image linear=image(VK_FORMAT_R8G8B8A8_UNORM,w,h,1);
    QuestColorTransfer transfer={0};
    CHECK(quest_color_prepare(physical,dev,&transfer,format,256,4,w,h));
    for(unsigned layer=0;layer<2;++layer)for(unsigned y=0;y<4;++y)for(unsigned x=0;x<256;++x) {
        unsigned i=((layer*4+y)*256+x)*4;
        pixels[i]=(unsigned char)(255-x);pixels[i+1]=layer?128:64;
        pixels[i+2]=(unsigned char)(layer?255-x:x);pixels[i+3]=(unsigned char)x;
    }
    begin();
    transition(source.image,2,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy upload=region(256,4,2,0);
    vkCmdCopyBufferToImage(cb,buffer,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&upload);
    transition(source.image,2,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
    submit();
    /* Repeat, including switching eye after scratch has already been read. */
    for(unsigned pass=0;pass<4;++pass) {
        unsigned layer=pass%2;
        begin();
        transition(target.image,1,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        quest_color_copy(cb,&transfer,source.image,layer,256,4,target.image,format,w,h);
        transition(target.image,1,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy read=region(w,h,1,16384);
        vkCmdCopyImageToBuffer(cb,target.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&read);
        transition(linear.image,1,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        blit(target.image,linear.image,w,h); /* Runtime's sRGB -> linear interpretation. */
        transition(linear.image,1,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        read.bufferOffset=65536;
        vkCmdCopyImageToBuffer(cb,linear.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&read);
        submit();
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            double sx=((x+.5)*256/w)-.5;
            if(sx<0)sx=0;if(sx>255)sx=255;
            double rgba[4]={layer?255-sx:sx,layer?128:64,255-sx,sx};
            for(unsigned c=0;c<4;++c) {
                unsigned at=(y*w+x)*4+c;
                unsigned channel=format==VK_FORMAT_B8G8R8A8_SRGB && c!=1 && c!=3?2-c:c;
                REQUIRE(fabs(pixels[16384+at]-rgba[channel])<=1.01);
                unsigned encoded_channel=format==VK_FORMAT_B8G8R8A8_SRGB && c!=1 && c!=3?2-c:c;
                unsigned encoded=pixels[16384+(y*w+x)*4+encoded_channel];
                double expected=c==3?encoded:decode(encoded);
                /* Hardware sRGB approximation plus 8-bit readback rounding. */
                if(fabs(pixels[65536+at]-expected)>2.01) {
                    fprintf(stderr,"decode mismatch format=%d %ux%u pass=%u x=%u c=%u encoded=%u actual=%u expected=%.3f\n",
                        format,w,h,pass,x,c,encoded,pixels[65536+at],expected);
                    REQUIRE(0);
                }
                checked+=2;
            }
        }
    }
    printf("PASS %s %ux%u: both eyes, encoded ramp/alpha, compositor decode, reuse (%s)\n",
        format==VK_FORMAT_B8G8R8A8_SRGB?"BGRA sRGB":"RGBA sRGB",w,h,transfer.image?"scratch":"direct");
    quest_color_destroy(dev,&transfer);destroy(source);destroy(target);destroy(linear);
}
static void panel_test(VkFormat format)
{
    Image target=image(format,256,1,1),decoded=image(VK_FORMAT_R8G8B8A8_UNORM,256,1,1);
    for(unsigned x=0;x<256;++x)((uint32_t *)pixels)[x]=
        quest_color_panel_pixel(0x7f008000u|(x<<16)|(255-x),format);
    begin();transition(target.image,1,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy r=region(256,1,1,0);
    vkCmdCopyBufferToImage(cb,buffer,target.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&r);
    transition(target.image,1,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition(decoded.image,1,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    blit(target.image,decoded.image,256,1);
    transition(decoded.image,1,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    r.bufferOffset=16384;
    vkCmdCopyImageToBuffer(cb,decoded.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&r);submit();
    for(unsigned x=0;x<256;++x) {
        REQUIRE(abs(pixels[16384+4*x]-(int)lround(decode(x)))<=2);
        REQUIRE(abs(pixels[16385+4*x]-55)<=1);
        REQUIRE(abs(pixels[16386+4*x]-(int)lround(decode(255-x)))<=2);
        REQUIRE(pixels[16387+4*x]==127);checked+=4;
    }
    destroy(target);destroy(decoded);
}
static void negative_control(void)
{
    Image source=image(VK_FORMAT_B8G8R8A8_UNORM,1,1,1),target=image(VK_FORMAT_B8G8R8A8_SRGB,1,1,1);
    pixels[0]=pixels[1]=pixels[2]=128;pixels[3]=255;
    begin();transition(source.image,1,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy r=region(1,1,1,0);
    vkCmdCopyBufferToImage(cb,buffer,source.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&r);
    transition(source.image,1,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition(target.image,1,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    blit(source.image,target.image,1,1);
    transition(target.image,1,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    r.bufferOffset=16384;vkCmdCopyImageToBuffer(cb,target.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&r);submit();
    REQUIRE(abs(pixels[16384]-188)<=1);
    printf("Negative control: incorrect linear handoff brightens encoded 128 to %u; corrected path preserves 128.\n",pixels[16384]);
    destroy(source);destroy(target);
}
int main(int argc,char **argv)
{
    int validate=0;
    unsigned device_index=0;
    for(int i=1;i<argc;++i) {
        if(!strcmp(argv[i],"--validation"))validate=1;
        else if(!strncmp(argv[i],"--device=",9))device_index=(unsigned)atoi(argv[i]+9);
        else {fprintf(stderr,"Usage: %s [--validation] [--device=N]\n",argv[0]);return 1;}
    }
    VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
    const char *layer="VK_LAYER_KHRONOS_validation",*extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    if(validate){ci.enabledLayerCount=1;ci.ppEnabledLayerNames=&layer;ci.enabledExtensionCount=1;ci.ppEnabledExtensionNames=&extension;}
    VkInstance instance;CHECK(vkCreateInstance(&ci,NULL,&instance));
    VkDebugUtilsMessengerEXT messenger=VK_NULL_HANDLE;
    if(validate) {
        VkDebugUtilsMessengerCreateInfoEXT debug={VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        debug.pfnUserCallback=validation;
        PFN_vkCreateDebugUtilsMessengerEXT create=(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");
        CHECK(create(instance,&debug,NULL,&messenger));
    }
    uint32_t count=0;CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL));REQUIRE(count);
    VkPhysicalDevice *devices=calloc(count,sizeof *devices);CHECK(vkEnumeratePhysicalDevices(instance,&count,devices));
    REQUIRE(device_index<count);physical=devices[device_index];free(devices);
    VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
    printf("GPU: %s; validation %s\n",properties.deviceName,validate?"ON":"OFF");
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);
    VkQueueFamilyProperties *families=calloc(count,sizeof *families);vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);
    uint32_t family=0;for(;family<count;++family)if(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)break;
    REQUIRE(family<count);free(families);
    float priority=1;VkDeviceQueueCreateInfo q={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex=family;q.queueCount=1;q.pQueuePriorities=&priority;
    VkDeviceCreateInfo dc={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&q;
    CHECK(vkCreateDevice(physical,&dc,NULL,&dev));vkGetDeviceQueue(dev,family,0,&queue);
    VkCommandPoolCreateInfo pc={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pc.queueFamilyIndex=family;pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool;CHECK(vkCreateCommandPool(dev,&pc,NULL,&pool));
    VkCommandBufferAllocateInfo ca={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;CHECK(vkAllocateCommandBuffers(dev,&ca,&cb));
    VkBufferCreateInfo bc={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bc.size=131072;
    bc.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;CHECK(vkCreateBuffer(dev,&bc,NULL,&buffer));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(dev,buffer,&req);
    VkMemoryAllocateInfo alloc={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;
    alloc.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK(vkAllocateMemory(dev,&alloc,NULL,&buffer_memory));CHECK(vkBindBufferMemory(dev,buffer,buffer_memory,0));
    CHECK(vkMapMemory(dev,buffer_memory,0,VK_WHOLE_SIZE,0,(void **)&pixels));
    const int64_t offered[]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_B8G8R8A8_SRGB};
    REQUIRE(quest_color_format(offered,3)==VK_FORMAT_B8G8R8A8_SRGB);
    REQUIRE(quest_color_format(offered,2)==VK_FORMAT_R8G8B8A8_SRGB);
    REQUIRE(quest_color_format(offered,1)==VK_FORMAT_UNDEFINED);
    for(unsigned f=0;f<2;++f) {
        VkFormat format=f?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_B8G8R8A8_SRGB;
        ramp_test(format,256,4);ramp_test(format,128,2);ramp_test(format,512,8);panel_test(format);
    }
    negative_control();
    vkUnmapMemory(dev,buffer_memory);vkDestroyBuffer(dev,buffer,NULL);vkFreeMemory(dev,buffer_memory,NULL);
    vkDestroyCommandPool(dev,pool,NULL);vkDestroyDevice(dev,NULL);
    if(messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug=(PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT");
        destroy_debug(instance,messenger,NULL);
    }
    vkDestroyInstance(instance,NULL);
    printf("%u channel checks; validation %s, observed errors %u\n",checked,validate?"ON":"OFF",errors);
    return errors?1:0;
}
