/* Standalone device test: one draw, two color/depth layers; a shared texture;
 * depth rejection differing per eye; two-layer feedback copy; presentation
 * blits to separate mono images; and a mono pass using the same shader ABI.
 * No game assets, OpenXR session, or subjective screenshot comparison needed. */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "quest_image_barrier.h"
#ifdef NFSU2_DEPTH_LAYOUT_TEST
#include "quest_depth_layout.h"
static int depth_layout_mode;
#define TEST_DEPTH_FORMAT VK_FORMAT_D24_UNORM_S8_UINT
#define TEST_DEPTH_ASPECTS (VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT)
#else
#define TEST_DEPTH_FORMAT VK_FORMAT_D32_SFLOAT
#define TEST_DEPTH_ASPECTS VK_IMAGE_ASPECT_DEPTH_BIT
#endif
#ifdef NFSU2_VERTEX_LAYOUT_TEST
#include "quest_vertex_layout.h"
static QuestVertexLayout vertex_layout;
static PFN_vkCmdSetVertexInputEXT set_vertex_input;
static VkBuffer vertices;
static uint32_t render_generation, layout_sets, layout_reuses, draw_index;
static VkPipeline current_pipeline;
static int reuse_layout;
#endif

#define CHECK(call) do { VkResult e=(call); if(e!=VK_SUCCESS) { \
    fprintf(stderr,"line %d: %s = %d\n",__LINE__,#call,e); exit(1); } } while(0)
typedef struct { VkImage image; VkImageView view; VkDeviceMemory memory; uint32_t layers; VkImageLayout layout; } Image;
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory_properties;
static VkCommandBuffer cb;
static PFN_vkCmdBeginRendering begin_rendering;
static PFN_vkCmdEndRendering end_rendering;
static int image_barriers;
static uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    for(uint32_t i=0;i<memory_properties.memoryTypeCount;++i)
        if((bits&(1u<<i)) && (memory_properties.memoryTypes[i].propertyFlags&flags)==flags) return i;
    fprintf(stderr,"No memory type\n"); exit(1);
}
static VkDeviceMemory allocate(VkMemoryRequirements req, VkMemoryPropertyFlags flags) {
    VkMemoryAllocateInfo info={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    info.allocationSize=req.size; info.memoryTypeIndex=memory_type(req.memoryTypeBits,flags);
    VkDeviceMemory memory; CHECK(vkAllocateMemory(device,&info,NULL,&memory)); return memory;
}
static void barrier(void) {
    VkMemoryBarrier b={VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    VkPipelineStageFlags stages=image_barriers?quest_image_stages(0):VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    vkCmdPipelineBarrier(cb,stages,stages,
        0,1,&b,0,NULL,0,NULL);
}
static Image image(uint32_t layers, int depth) {
    Image result={0}; result.layers=layers; result.layout=VK_IMAGE_LAYOUT_GENERAL;
    VkImageCreateInfo info={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType=VK_IMAGE_TYPE_2D; info.format=depth?TEST_DEPTH_FORMAT:VK_FORMAT_R8G8B8A8_UNORM;
    info.extent=(VkExtent3D){16,16,1}; info.mipLevels=1; info.arrayLayers=layers;
    info.samples=VK_SAMPLE_COUNT_1_BIT; info.tiling=VK_IMAGE_TILING_OPTIMAL;
    info.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
    CHECK(vkCreateImage(device,&info,NULL,&result.image));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device,result.image,&req);
    result.memory=allocate(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CHECK(vkBindImageMemory(device,result.image,result.memory,0));
    VkImageViewCreateInfo view={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image=result.image; view.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY; view.format=info.format;
    view.subresourceRange=(VkImageSubresourceRange){depth?TEST_DEPTH_ASPECTS:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layers};
    CHECK(vkCreateImageView(device,&view,NULL,&result.view));
    VkImageMemoryBarrier b={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.image=result.image; b.subresourceRange=view.subresourceRange;
    b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
    return result;
}
static VkShaderModule shader(const char *path) {
    FILE *f=fopen(path,"rb"); if(!f) { perror(path); exit(1); }
    fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
    void *data=malloc((size_t)size);
    if(!data || fread(data,1,(size_t)size,f)!=(size_t)size) exit(1);
    fclose(f);
    VkShaderModuleCreateInfo info={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize=(size_t)size; info.pCode=data;
    VkShaderModule module; CHECK(vkCreateShaderModule(device,&info,NULL,&module)); free(data); return module;
}
static VkPipeline pipeline(VkPipelineLayout layout, VkShaderModule vs, VkShaderModule fs, uint32_t mask) {
    VkPipelineShaderStageCreateInfo stages[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vs; stages[0].pName="main";
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=fs; stages[1].pName="main";
    VkPipelineVertexInputStateCreateInfo vi={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport={0,0,16,16,0,1}; VkRect2D scissor={{0,0},{16,16}};
    VkPipelineViewportStateCreateInfo vp={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount=vp.scissorCount=1; vp.pViewports=&viewport; vp.pScissors=&scissor;
    VkPipelineRasterizationStateCreateInfo rs={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode=VK_POLYGON_MODE_FILL; rs.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds={VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable=ds.depthWriteEnable=VK_TRUE; ds.depthCompareOp=VK_COMPARE_OP_LESS;
#ifdef NFSU2_DEPTH_LAYOUT_TEST
    ds.stencilTestEnable=VK_TRUE;
    ds.front.compareOp=VK_COMPARE_OP_EQUAL; ds.front.compareMask=255;
    ds.front.reference=42; ds.back=ds.front;
#endif
    VkPipelineColorBlendAttachmentState attachment={0}; attachment.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo blend={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount=1; blend.pAttachments=&attachment;
    VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering={VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.viewMask=mask; rendering.colorAttachmentCount=1; rendering.pColorAttachmentFormats=&format;
    rendering.depthAttachmentFormat=TEST_DEPTH_FORMAT;
#ifdef NFSU2_DEPTH_LAYOUT_TEST
    rendering.stencilAttachmentFormat=TEST_DEPTH_FORMAT;
#endif
    VkGraphicsPipelineCreateInfo info={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
#ifdef NFSU2_VERTEX_LAYOUT_TEST
    VkDynamicState dynamic_state=VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
    VkPipelineDynamicStateCreateInfo dynamic={VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount=1; dynamic.pDynamicStates=&dynamic_state;
    info.pDynamicState=&dynamic;
#endif
    info.pNext=&rendering; info.stageCount=2; info.pStages=stages; info.pVertexInputState=&vi;
    info.pInputAssemblyState=&ia; info.pViewportState=&vp; info.pRasterizationState=&rs;
    info.pMultisampleState=&ms; info.pDepthStencilState=&ds; info.pColorBlendState=&blend; info.layout=layout;
    VkPipeline result; CHECK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&info,NULL,&result)); return result;
}
static void begin(Image color, Image *depth, int clear) {
    barrier();
#ifdef NFSU2_DEPTH_LAYOUT_TEST
    quest_depth_layout_transition(cb,depth->image,&depth->layout,depth->layers,TEST_DEPTH_ASPECTS,depth_layout_mode);
#endif
    VkRenderingAttachmentInfo a={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO},z=a;
    a.imageView=color.view; a.imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    a.loadOp=clear==1?VK_ATTACHMENT_LOAD_OP_CLEAR:VK_ATTACHMENT_LOAD_OP_LOAD; a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    z=a; z.imageView=depth->view; z.imageLayout=depth->layout;
    z.loadOp=clear==2?VK_ATTACHMENT_LOAD_OP_LOAD:VK_ATTACHMENT_LOAD_OP_CLEAR; z.clearValue.depthStencil.depth=1;
    z.clearValue.depthStencil.stencil=42;
    VkRenderingInfo info={VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent=(VkExtent2D){16,16}; info.layerCount=1;
    info.viewMask=color.layers==2?3:0;
    info.colorAttachmentCount=1; info.pColorAttachments=&a; info.pDepthAttachment=&z;
#ifdef NFSU2_DEPTH_LAYOUT_TEST
    info.pStencilAttachment=&z;
#endif
    begin_rendering(cb,&info);
#ifdef NFSU2_VERTEX_LAYOUT_TEST
    ++render_generation;
#endif
}
#ifdef NFSU2_VERTEX_LAYOUT_TEST
/* Two disjoint screen triangles per logical draw. The second MUST update
 * the buffer offset even when its format is reused. Subsequent logical draws
 * change format, stride, attribute offset and number of bindings. */
static void vertex_input(uint32_t kind, uint32_t half) {
    VkVertexInputBindingDescription2EXT b[2]={{0},{0}};
    VkVertexInputAttributeDescription2EXT a[2]={{0},{0}};
    for(uint32_t i=0;i<2;++i) {
        b[i].sType=VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT;
        b[i].binding=i; b[i].divisor=1;
        a[i].sType=VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT;
        a[i].location=i;
    }
    uint32_t nb=kind==2?2:1;
    b[0].stride=kind==0?32:kind==1?16:8;
    a[0].format=VK_FORMAT_R32G32_SFLOAT;
    a[1].format=kind==1?VK_FORMAT_R8G8B8A8_UNORM:VK_FORMAT_R32G32B32A32_SFLOAT;
    a[1].offset=kind==0?16:kind==1?8:0;
    a[1].binding=kind==2?1:0;
    if(quest_vertex_layout_set(&vertex_layout,reuse_layout,1,render_generation,current_pipeline,nb,b,2,a)) {
        set_vertex_input(cb,nb,b,2,a); ++layout_sets;
    } else ++layout_reuses;
    VkBuffer buffers[2]={vertices,vertices};
    VkDeviceSize offsets[2]={kind*256+half*128,1024};
    vkCmdBindVertexBuffers(cb,0,nb,buffers,offsets);
}
static void create_vertices(void) {
    VkBufferCreateInfo info={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size=1040; info.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    CHECK(vkCreateBuffer(device,&info,NULL,&vertices));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,vertices,&req);
    VkDeviceMemory memory=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK(vkBindBufferMemory(device,vertices,memory,0));
    uint8_t *data; CHECK(vkMapMemory(device,memory,0,info.size,0,(void **)&data));
    memset(data,0,info.size);
    float white[4]={1,1,1,1}; memcpy(data+1024,white,16);
    for(uint32_t kind=0;kind<3;++kind) for(uint32_t half=0;half<2;++half) {
        uint32_t stride=kind==0?32:kind==1?16:8;
        const float positions[2][3][2]={{{-1,-1},{1,-1},{-1,1}},{{1,-1},{1,1},{-1,1}}};
        for(uint32_t i=0;i<3;++i) {
            uint8_t *p=data+kind*256+half*128+i*stride;
            memcpy(p,positions[half][i],8);
            if(kind==0) memcpy(p+16,white,16);
            if(kind==1) memset(p+8,255,4);
        }
    }
    vkUnmapMemory(device,memory);
}
#endif
static void draw(VkPipeline p, VkPipelineLayout layout, VkDescriptorSet set, float left, float right, int mode) {
    struct { float depth[2]; int mode; } params={{left,right},mode};
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,p);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,NULL);
    vkCmdPushConstants(cb,layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof params,&params);
#ifdef NFSU2_VERTEX_LAYOUT_TEST
    current_pipeline=p;
    for(uint32_t half=0;half<2;++half) {
        vertex_input(draw_index%3,half);
        vkCmdDraw(cb,3,1,0,0);
    }
    ++draw_index;
#else
    vkCmdDraw(cb,3,1,0,0);
#endif
}
int main(int argc,char **argv) {
    if(argc<3 || argc>5) { fprintf(stderr,"Usage: test vertex.spv fragment.spv [image-barriers|all] [swap|border]\n"); return 1; }
    image_barriers=argc>=4 && !strcmp(argv[3],"image-barriers");
    int swap=argc==5 && !strcmp(argv[4],"swap"), border=argc==5;
#ifdef NFSU2_DEPTH_LAYOUT_TEST
    depth_layout_mode=argc>=4 && !strcmp(argv[3],"optimal");
    int initial_depth_mode=depth_layout_mode;
#endif
#ifdef NFSU2_VERTEX_LAYOUT_TEST
    reuse_layout=argc>=4 && !strcmp(argv[3],"reuse");
#endif
    VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_3;
    VkInstanceCreateInfo instance_info={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instance_info.pApplicationInfo=&app;
    VkInstance instance; CHECK(vkCreateInstance(&instance_info,NULL,&instance));
    uint32_t count=0;
    CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL));
    if(!count) return 1;
    VkPhysicalDevice *devices=calloc(count,sizeof *devices),gpu;
    if(!devices) return 1;
    CHECK(vkEnumeratePhysicalDevices(instance,&count,devices));
    gpu=devices[0];
    for(uint32_t i=0;i<count;++i) {
        VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(devices[i],&properties);
        if(properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { gpu=devices[i]; break; }
    }
    free(devices);
    VkPhysicalDeviceProperties selected; vkGetPhysicalDeviceProperties(gpu,&selected);
    printf("GPU test device: %s\n",selected.deviceName);
    vkGetPhysicalDeviceMemoryProperties(gpu,&memory_properties);
    VkPhysicalDeviceMultiviewFeatures mv={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
#ifdef NFSU2_VERTEX_LAYOUT_TEST
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vi={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT};
    mv.pNext=&vi;
#endif
    VkPhysicalDeviceVulkan13Features f13={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext=&f13; f13.pNext=&mv; vkGetPhysicalDeviceFeatures2(gpu,&features);
    if(!mv.multiview || !f13.dynamicRendering) { fprintf(stderr,"Required features unavailable\n"); return 1; }
    memset(&f13,0,sizeof f13); f13.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    f13.dynamicRendering=VK_TRUE; f13.pNext=&mv; mv.multiviewGeometryShader=mv.multiviewTessellationShader=VK_FALSE;
    VkQueueFamilyProperties queues[32]; count=32; vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,queues);
    uint32_t family=0; while(family<count && !(queues[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) ++family;
    if(family==count) return 1;
    float priority=1; VkDeviceQueueCreateInfo q={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex=family; q.queueCount=1; q.pQueuePriorities=&priority;
    VkDeviceCreateInfo di={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.pNext=&f13; di.queueCreateInfoCount=1; di.pQueueCreateInfos=&q;
#ifdef NFSU2_VERTEX_LAYOUT_TEST
    if(!vi.vertexInputDynamicState) return 1;
    const char *extension=VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME;
    di.enabledExtensionCount=1; di.ppEnabledExtensionNames=&extension;
#endif
    CHECK(vkCreateDevice(gpu,&di,NULL,&device));
#ifdef NFSU2_VERTEX_LAYOUT_TEST
    set_vertex_input=(PFN_vkCmdSetVertexInputEXT)vkGetDeviceProcAddr(device,"vkCmdSetVertexInputEXT");
    if(!set_vertex_input) return 1;
    create_vertices();
#endif
    begin_rendering=(PFN_vkCmdBeginRendering)vkGetDeviceProcAddr(device,"vkCmdBeginRendering");
    end_rendering=(PFN_vkCmdEndRendering)vkGetDeviceProcAddr(device,"vkCmdEndRendering");
    if(!begin_rendering || !end_rendering) return 1;
    VkQueue queue; vkGetDeviceQueue(device,family,0,&queue);
    VkCommandPoolCreateInfo pool_info={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool_info.queueFamilyIndex=family;
    VkCommandPool pool; CHECK(vkCreateCommandPool(device,&pool_info,NULL,&pool));
    VkCommandBufferAllocateInfo alloc={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool=pool; alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount=1;
    CHECK(vkAllocateCommandBuffers(device,&alloc,&cb));
    VkCommandBufferBeginInfo bi={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CHECK(vkBeginCommandBuffer(cb,&bi));
    Image input=image(2,0), shared=image(1,0), output=image(2,0), depth=image(2,1), snapshot=image(2,0);
    Image presented[2]={image(1,0),image(1,0)}, mono=image(1,0), mono_depth=image(1,1);
    /* Exact UNORM byte centers avoid vendor-specific rounding at half a byte. */
    VkClearColorValue colors[3]={{{128.0f/255,0,0,1}},{{0,128.0f/255,0,1}},{{0,0,16.0f/255,1}}};
    for(int i=0;i<3;++i) {
        VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,i==1?1:0,1};
        vkCmdClearColorImage(cb,i==2?shared.image:input.image,VK_IMAGE_LAYOUT_GENERAL,&colors[i],1,&range);
    }
    VkDescriptorSetLayoutBinding bindings[2]={{0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL},
        {1,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL}};
    VkDescriptorSetLayoutCreateInfo sli={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; sli.bindingCount=2; sli.pBindings=bindings;
    VkDescriptorSetLayout sl; CHECK(vkCreateDescriptorSetLayout(device,&sli,NULL,&sl));
    VkPushConstantRange push={VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,12};
    VkPipelineLayoutCreateInfo li={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount=1; li.pSetLayouts=&sl; li.pushConstantRangeCount=1; li.pPushConstantRanges=&push;
    VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(device,&li,NULL,&layout));
    VkShaderModule vs=shader(argv[1]), fs=shader(argv[2]);
    VkPipeline stereo_pipeline=pipeline(layout,vs,fs,3), mono_pipeline=pipeline(layout,vs,fs,0);
    VkSamplerCreateInfo si={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; si.maxLod=1;
    VkSampler sampler; CHECK(vkCreateSampler(device,&si,NULL,&sampler));
    VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4};
    VkDescriptorPoolCreateInfo dpi={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets=2; dpi.poolSizeCount=1; dpi.pPoolSizes=&size;
    VkDescriptorPool dp; CHECK(vkCreateDescriptorPool(device,&dpi,NULL,&dp));
    VkDescriptorSetLayout layouts[2]={sl,sl}; VkDescriptorSet sets[2];
    VkDescriptorSetAllocateInfo dai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; dai.descriptorPool=dp; dai.descriptorSetCount=2; dai.pSetLayouts=layouts;
    CHECK(vkAllocateDescriptorSets(device,&dai,sets));
    for(int set=0;set<2;++set) for(int b=0;b<2;++b) {
        VkDescriptorImageInfo im={sampler,b?shared.view:set?(swap?output.view:snapshot.view):input.view,VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet w={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet=sets[set]; w.dstBinding=b;
        w.descriptorCount=1; w.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo=&im;
        vkUpdateDescriptorSets(device,1,&w,0,NULL);
    }
    begin(output,&depth,1);
    draw(stereo_pipeline,layout,sets[0],0.25f,0.75f,0);
#ifdef NFSU2_DEPTH_LAYOUT_TEST
    end_rendering(cb);
    depth_layout_mode=!depth_layout_mode;
    begin(output,&depth,2); /* Preserve both depth and stencil across transition. */
#endif
    draw(stereo_pipeline,layout,sets[0],0.5f,0.5f,1);
    end_rendering(cb); barrier();
    VkImageCopy copy={0}; copy.srcSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,2};
    copy.dstSubresource=copy.srcSubresource; copy.extent=(VkExtent3D){16,16,1};
    if(swap) {
        VkImageCopy edge[4]={copy,copy,copy,copy};
        edge[0].extent=edge[1].extent=(VkExtent3D){16,1,1};
        edge[1].srcOffset.y=15;
        edge[2].extent=edge[3].extent=(VkExtent3D){1,14,1};
        edge[2].srcOffset.y=edge[3].srcOffset.y=1; edge[3].srcOffset.x=15;
        for(int i=0;i<4;++i) edge[i].dstOffset=edge[i].srcOffset;
        vkCmdCopyImage(cb,output.image,VK_IMAGE_LAYOUT_GENERAL,snapshot.image,VK_IMAGE_LAYOUT_GENERAL,4,edge);
        Image old=output; output=snapshot; snapshot=old;
    } else vkCmdCopyImage(cb,output.image,VK_IMAGE_LAYOUT_GENERAL,snapshot.image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
#ifdef NFSU2_DEPTH_LAYOUT_TEST
    depth_layout_mode=!depth_layout_mode;
#endif
    begin(output,&depth,0); draw(stereo_pipeline,layout,sets[1],0.5f,0.5f,border?3:2); end_rendering(cb); barrier();
    for(int eye=0;eye<2;++eye) {
        VkImageBlit blit={0}; blit.srcSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,(uint32_t)eye,1};
        blit.dstSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        blit.srcOffsets[1]=blit.dstOffsets[1]=(VkOffset3D){16,16,1};
        vkCmdBlitImage(cb,output.image,VK_IMAGE_LAYOUT_GENERAL,presented[eye].image,VK_IMAGE_LAYOUT_GENERAL,1,&blit,VK_FILTER_NEAREST);
    }
    begin(mono,&mono_depth,1); draw(mono_pipeline,layout,sets[0],0.5f,0.5f,0); end_rendering(cb); barrier();
    VkBufferCreateInfo bci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bci.size=3*16*16*4; bci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer readback; CHECK(vkCreateBuffer(device,&bci,NULL,&readback));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,readback,&req);
    VkDeviceMemory memory=allocate(req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK(vkBindBufferMemory(device,readback,memory,0));
    for(int i=0;i<3;++i) {
        VkBufferImageCopy r={0}; r.bufferOffset=i*16*16*4;
        r.imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; r.imageExtent=(VkExtent3D){16,16,1};
        vkCmdCopyImageToBuffer(cb,i==2?mono.image:presented[i].image,VK_IMAGE_LAYOUT_GENERAL,readback,1,&r);
    }
    VkMemoryBarrier host={VK_STRUCTURE_TYPE_MEMORY_BARRIER}; host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    CHECK(vkEndCommandBuffer(cb));
    VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&cb;
    CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(queue));
    uint8_t *pixels; CHECK(vkMapMemory(device,memory,0,bci.size,0,(void **)&pixels));
    int failed=0; const uint8_t expected[3][4]={{128,0,16,255},{0,128,16,255},{0,0,255,255}};
    for(int i=0;i<3;++i) for(int pixel=0;pixel<256;++pixel) {
        int kind=i==1?(pixel%16<8?1:2):0;
        if(memcmp(pixels+(i*256+pixel)*4,expected[kind],4)) {
            if(!failed) fprintf(stderr,"Image %d pixel %d: %u %u %u %u\n",i,pixel,
                pixels[(i*256+pixel)*4],pixels[(i*256+pixel)*4+1],pixels[(i*256+pixel)*4+2],pixels[(i*256+pixel)*4+3]);
            ++failed;
        }
    }
    printf("Quest multiview: 768 pixels, stereo depth/texture/feedback/presentation + mono pass, image barriers %s, feedback %s: %d failures\n",
        image_barriers?"on":"off",swap?"swap with preserved border":border?"copy with partial coverage":"copy",failed);
    vkUnmapMemory(device,memory);
#ifdef NFSU2_DEPTH_LAYOUT_TEST
    printf("D24S8 layout transitions %s -> %s -> %s: depth and stencil retained in both eyes\n",
        initial_depth_mode?"optimal":"general",initial_depth_mode?"general":"optimal",initial_depth_mode?"optimal":"general");
#endif
#ifdef NFSU2_VERTEX_LAYOUT_TEST
    printf("Vertex layouts: %u set, %u reused; input formats, buffers, mono/stereo passes verified\n",layout_sets,layout_reuses);
    if(reuse_layout && !layout_reuses) ++failed;
#endif
    /* Process exit releases the standalone test's device objects. */
    return failed?1:0;
}
