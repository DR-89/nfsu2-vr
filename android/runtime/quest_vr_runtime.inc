/* Included by quest_xr.c. All UI, settings I/O and gamepad sampling belong to
 * the OpenXR/render thread; the guest receives only locked snapshots. */
#include "quest_vr_panel.h"
#include "quest_gamepad.h"
#include "quest_cockpit_fit.h"
#ifdef NFSU2_PCVR
#include <windows.h>
#endif
static QuestVRMenu vr_menu;
static int vr_paused,vr_save_status,vr_start_resolution;
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

static void vr_publish_settings(void)
{
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
    if(folder) {
        snprintf(vr_settings_path,sizeof vr_settings_path,"%svr-settings.ini",folder);SDL_free(folder);
        SDL_RWops *f=SDL_RWFromFile(vr_settings_path,"rb");
        if(f) {
            char text[4096];size_t n=SDL_RWread(f,text,1,sizeof text-1);text[n]=0;
            SDL_RWclose(f);quest_vr_parse(&vr_settings,text);
        }
    }
    vr_start_resolution=vr_settings.resolution;
    vr_publish_settings();
    fprintf(stderr,"[VR settings] loaded: sensitivity=%.2f rotation=%.0f seat=%.2f/%.2f resolution=%d\n",
        vr_settings.sensitivity,vr_settings.rotation,vr_settings.height,vr_settings.distance,vr_settings.resolution);
}
static void vr_save_settings(void)
{
    char text[512],temporary[4112];int n=quest_vr_format(&vr_settings,text,sizeof text);
    vr_save_status=-1;
    if(!*vr_settings_path || n<=0 || n>=(int)sizeof text)return;
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
    int paused;pthread_mutex_lock(&input_lock);paused=vr_paused;pthread_mutex_unlock(&input_lock);
    int keys=buttons&15;
    int x=abs(sticks[2])>abs(sticks[0])?sticks[2]:sticks[0];
    int y=abs(sticks[3])>abs(sticks[1])?sticks[3]:sticks[1];
    if(abs(x)>18000 && abs(x)>abs(y))keys|=x>0?QUEST_VR_RIGHT:QUEST_VR_LEFT;
    else if(abs(y)>18000)keys|=y>0?QUEST_VR_UP:QUEST_VR_DOWN;
    if(analog[0]>127)keys|=QUEST_VR_ACCEPT;
    if(analog[1]>127 || (buttons&0x10))keys|=QUEST_VR_BACK;
    if(analog[2]>127)keys|=QUEST_VR_OPEN;
    int old_open=vr_menu.open,old_row=vr_menu.row;
    int changed=quest_vr_menu_step(&vr_menu,&vr_settings,SDL_GetTicks(),paused,keys);
    if(changed&1)vr_publish_settings();
    if(changed&2) {
        vr_save_settings();
        if(vr_save_status<0 && paused) {vr_menu.open=1;vr_menu.row=6;}
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
        quest_vr_panel(pixels,&vr_settings,&vr_menu,vr_save_status,vr_settings.resolution!=vr_start_resolution);
        if(vr_panel_format==VK_FORMAT_R8G8B8A8_UNORM)
            for(unsigned i=0;i<1024u*768u;++i) {
                uint32_t c=pixels[i];pixels[i]=(c&0xff00ff00u)|((c&255)<<16)|((c>>16)&255);
            }
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
