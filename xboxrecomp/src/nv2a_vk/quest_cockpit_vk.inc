/* Included only by the Vulkan OpenXR backend. Immutable shared interior,
 * tracked instruments, original mirror glass and animated wheel/hands. */
#include "quest_cockpit_asset.h"
#include "quest_cockpit.h"
#include "quest_ghost_hands.h"
#include "quest_glove_asset.h"
#include "quest_cockpit_panels.h"
static int s_cockpit_frame;
static struct {
    int ready;
    VkBuffer geometry, staging;
    VkDeviceMemory geometry_mem, staging_mem;
    VkImage texture, depth;
    VkImageView texture_view, depth_view;
    VkDeviceMemory texture_mem, depth_mem;
    VkSampler sampler;
    VkDescriptorSetLayout descriptors;
    VkPipelineLayout layout;
    VkPipeline pipeline[2];
    VkPipeline hand_pipeline[2];
    VkPipeline mirror_pipeline[2];
    QuestGhostVertex mirror_vertices[3][192];
    unsigned mirror_count[3];
    uint32_t mirror_frame;
    VkSurf hud;
    int hud_split;
    VkPipeline hud_pipeline[2], info_pipeline[2];
    VkBuffer panels;
    VkDeviceMemory panels_mem;
    VkBuffer hands;
    VkDeviceMemory hands_mem;
    uint32_t hand_vertices;
    VkBuffer hand_staging;
    VkDeviceMemory hand_staging_mem,hand_texture_mem;
    VkImage hand_texture;
    VkImageView hand_texture_view;
    uint32_t indices, vertices, width, height, layers;
    uint32_t wheel_first, wheel_count, wheel_tick;
    float wheel_pivot[3], wheel_axis[3], steering;
} s_cockpit;

static int cockpit_buffer(VkDeviceSize size,VkBufferUsageFlags usage,
                           VkBuffer *buffer,VkDeviceMemory *memory,const void *data)
{
    VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkMemoryRequirements rq;
    ci.size=size; ci.usage=usage;
    if(vkCreateBuffer(s_dev,&ci,NULL,buffer)!=VK_SUCCESS) return 0;
    vkGetBufferMemoryRequirements(s_dev,*buffer,&rq);
    *memory=alloc_mem(rq,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if(!*memory || vkBindBufferMemory(s_dev,*buffer,*memory,0)!=VK_SUCCESS) return 0;
    void *mapped=NULL;
    if(vkMapMemory(s_dev,*memory,0,size,0,&mapped)!=VK_SUCCESS) return 0;
    memcpy(mapped,data,(size_t)size); vkUnmapMemory(s_dev,*memory);
    return 1;
}

static VkPipeline cockpit_pipeline(int multiview,int ghost)
{
    char vertex[1400],fragment[1100];
    snprintf(vertex,sizeof vertex,
        "#version 450\n%s\n"
        "layout(location=0) in vec3 position; layout(location=1) in vec3 normal;"
        "layout(location=2) in vec2 uv; layout(location=0) out vec2 texcoord;"
        "layout(location=1) out float shade;"
        "layout(push_constant) uniform Camera { mat4 eye[2]; } camera;"
        "void main(){gl_Position=camera.eye[%s]*vec4(position,1);texcoord=uv;"
        "shade=0.78+0.22*max(dot(normalize(normal),normalize(vec3(-.3,.9,-.2))),0.0);}",
        multiview?"#extension GL_EXT_multiview : require":"",multiview?"gl_ViewIndex":"0");
    snprintf(fragment,sizeof fragment,
        "#version 450\nlayout(location=0) in vec2 texcoord;layout(location=1) in float shade;"
        "layout(set=0,binding=0) uniform %s atlas;layout(location=0) out vec4 color;"
        "void main(){vec4 c=texture(atlas,%s);if(c.a<.5)discard;color=vec4(c.rgb*shade,1);}",
        s_multiview?"sampler2DArray":"sampler2D",s_multiview?"vec3(texcoord,0)":"texcoord");
    if(ghost==1) {
        snprintf(vertex,sizeof vertex,
            "#version 450\n%s\n"
            "layout(location=0) in vec3 position;layout(location=1) in vec3 normal;"
            "layout(location=0) out float shade;"
            "layout(push_constant) uniform Camera {mat4 eye[2];} camera;"
            "void main(){gl_Position=camera.eye[%s]*vec4(position,1);"
            "shade=.65+.35*abs(dot(normalize(normal),normalize(vec3(-.3,.7,.6))));}",
            multiview?"#extension GL_EXT_multiview : require":"",multiview?"gl_ViewIndex":"0");
        snprintf(fragment,sizeof fragment,
            "#version 450\nlayout(location=0) in float shade;layout(location=0) out vec4 color;"
            "void main(){color=vec4(vec3(.55,.85,1.0)*shade,.38);}");
    } else if(ghost==3) {
        /* A real rear-camera image is unlit, opaque, and horizontally flipped
         * like the retail mirror quad. Its alpha is not coverage. */
        snprintf(fragment,sizeof fragment,
            "#version 450\nlayout(location=0) in vec2 texcoord;layout(location=1) in float shade;"
            "layout(set=0,binding=0) uniform %s atlas;layout(location=0) out vec4 color;"
            "void main(){vec2 p=vec2(1-texcoord.x,texcoord.y);"
            "color=vec4(texture(atlas,%s).rgb,1);}",
            s_multiview?"sampler2DArray":"sampler2D",s_multiview?"vec3(p,0)":"p");
    } else if(ghost==4 || ghost==5) {
        snprintf(fragment,sizeof fragment,
            "#version 450\nlayout(location=0) in vec2 texcoord;layout(location=1) in float shade;"
            "layout(set=0,binding=0) uniform %s atlas;layout(location=0) out vec4 color;"
            "void main(){vec4 c=texture(atlas,%s);if(c.a<.003)discard;color=c;}",
            s_multiview?"sampler2DArray":"sampler2D",s_multiview?"vec3(texcoord,0)":"texcoord");
    } else if(ghost==2) {
        snprintf(fragment,sizeof fragment,
            "#version 450\nlayout(location=0) in vec2 texcoord;layout(location=1) in float shade;"
            "layout(set=0,binding=0) uniform %s atlas;layout(location=0) out vec4 color;"
            "void main(){vec4 c=texture(atlas,%s);if(c.a<.5)discard;color=vec4(c.rgb*shade,.68);}",
            s_multiview?"sampler2DArray":"sampler2D",s_multiview?"vec3(texcoord,0)":"texcoord");
    }
    VkShaderModule vs=compile(GLSLANG_STAGE_VERTEX,vertex),fs=compile(GLSLANG_STAGE_FRAGMENT,fragment);
    if(!vs || !fs) {
        if(vs) vkDestroyShaderModule(s_dev,vs,NULL);
        if(fs) vkDestroyShaderModule(s_dev,fs,NULL);
        return VK_NULL_HANDLE;
    }
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fs,.pName="main"}};
    VkVertexInputBindingDescription binding={0,32,VK_VERTEX_INPUT_RATE_VERTEX};
    if(ghost) binding.stride=sizeof(QuestGhostVertex);
    VkVertexInputAttributeDescription attributes[3]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32_SFLOAT,12},{2,0,VK_FORMAT_R32G32_SFLOAT,24}};
    VkPipelineVertexInputStateCreateInfo vi={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount=1; vi.pVertexBindingDescriptions=&binding;
    vi.vertexAttributeDescriptionCount=ghost==1?2:3; vi.pVertexAttributeDescriptions=attributes;
    VkPipelineInputAssemblyStateCreateInfo ia={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=VK_CULL_MODE_NONE; raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds={VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable=ghost==5?VK_FALSE:VK_TRUE; ds.depthWriteEnable=(ghost==4||ghost==5)?VK_FALSE:VK_TRUE; ds.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState attachment={0}; attachment.colorWriteMask=15;
    if(ghost==1 || ghost==2 || ghost==4 || ghost==5) {
        attachment.blendEnable=VK_TRUE;
        attachment.srcColorBlendFactor=ghost>=4?VK_BLEND_FACTOR_ONE:VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp=VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.alphaBlendOp=VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount=1; blend.pAttachments=&attachment;
    const VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic={VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount=2; dynamic.pDynamicStates=states;
    const VkFormat color=VK_FORMAT_B8G8R8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering={VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.viewMask=multiview?3:0; rendering.colorAttachmentCount=1;
    rendering.pColorAttachmentFormats=&color; rendering.depthAttachmentFormat=s_depth_fmt;
    VkGraphicsPipelineCreateInfo ci={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.pNext=&rendering; ci.stageCount=2; ci.pStages=stages; ci.layout=s_cockpit.layout;
    ci.pVertexInputState=&vi; ci.pInputAssemblyState=&ia; ci.pViewportState=&viewport;
    ci.pRasterizationState=&raster; ci.pMultisampleState=&ms; ci.pDepthStencilState=&ds;
    ci.pColorBlendState=&blend; ci.pDynamicState=&dynamic;
    VkPipeline result=VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(s_dev,VK_NULL_HANDLE,1,&ci,NULL,&result);
    vkDestroyShaderModule(s_dev,vs,NULL); vkDestroyShaderModule(s_dev,fs,NULL);
    return result;
}

static void cockpit_initialize(void)
{
    const char *option=getenv("NFSU2_VR_COCKPIT");
    if(option && *option=='0') return;
    size_t size=0;
    void *data=SDL_LoadFile("cockpits/shared.ncpk",&size);
    if(!data) {
        char *base=SDL_GetBasePath(); char path[2048];
        snprintf(path,sizeof path,"%scockpits/shared.ncpk",base?base:"");
        SDL_free(base); data=SDL_LoadFile(path,&size);
    }
    QuestCockpitAsset asset;
    if(!quest_cockpit_asset(&asset,data,size)) {
        LOGE("Cockpit asset unavailable/invalid; original cameras remain available\n");
        SDL_free(data); return;
    }
    VkDescriptorSetLayoutBinding binding={0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo dci={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dci.bindingCount=1; dci.pBindings=&binding;
    VkPushConstantRange push={VK_SHADER_STAGE_VERTEX_BIT,0,128};
    VkPipelineLayoutCreateInfo lci={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.setLayoutCount=1; lci.pSetLayouts=&s_cockpit.descriptors;
    lci.pushConstantRangeCount=1; lci.pPushConstantRanges=&push;
    VkSamplerCreateInfo sampler={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if(vkCreateDescriptorSetLayout(s_dev,&dci,NULL,&s_cockpit.descriptors)!=VK_SUCCESS ||
       vkCreatePipelineLayout(s_dev,&lci,NULL,&s_cockpit.layout)!=VK_SUCCESS ||
       vkCreateSampler(s_dev,&sampler,NULL,&s_cockpit.sampler)!=VK_SUCCESS) goto fail;
    s_cockpit.pipeline[0]=cockpit_pipeline(0,0);
    if(s_multiview) s_cockpit.pipeline[1]=cockpit_pipeline(1,0);
    if(!s_cockpit.pipeline[0] || (s_multiview && !s_cockpit.pipeline[1])) goto fail;
    if(!cockpit_buffer((VkDeviceSize)asset.vertices*32+(VkDeviceSize)asset.indices*4,
           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
           &s_cockpit.geometry,&s_cockpit.geometry_mem,asset.vertex_data) ||
       !cockpit_buffer((VkDeviceSize)asset.width*asset.height*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
           &s_cockpit.staging,&s_cockpit.staging_mem,asset.pixels) ||
       !make_image(asset.width,asset.height,VK_FORMAT_R8G8B8A8_UNORM,
           VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,
           &s_cockpit.texture,&s_cockpit.texture_view,&s_cockpit.texture_mem)) goto fail;
    VkBufferImageCopy copy={0};
    copy.imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
    copy.imageExtent=(VkExtent3D){asset.width,asset.height,1};
    vkCmdCopyBufferToImage(s_cb,s_cockpit.staging,s_cockpit.texture,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
    s_cockpit.vertices=asset.vertices; s_cockpit.indices=asset.indices; s_cockpit.ready=1;
    s_cockpit.wheel_first=asset.wheel_first; s_cockpit.wheel_count=asset.wheel_count;
    memcpy(s_cockpit.wheel_pivot,asset.wheel_pivot,12);
    memcpy(s_cockpit.wheel_axis,asset.wheel_axis,12);
    s_cockpit.mirror_pipeline[0]=cockpit_pipeline(0,3);
    if(s_multiview) s_cockpit.mirror_pipeline[1]=cockpit_pipeline(1,3);
    /* Shared transparent cockpit UI: split only during driving. Pause menus
     * use the complete image on one plane; other cameras retain their path. */
    s_cockpit.hud=(VkSurf){.w=640,.h=480,.pw=1280,.ph=960,.layers=1,
        .aa_sx=1,.aa_sy=1,.eye=-1,.va=0xfffffffdu};
    QuestGhostVertex panels[30];
    /* Match the cabin's +10 cm up/+8 cm back eye-relative translation. */
    quest_cockpit_panel(panels,0,-.205f,-.56f,.24f,.24f,440,260,640,480);
    quest_cockpit_panel(panels+6,-.30f,-.21f,-.38f,.22f,.242f,0,260,200,480);
    /* Remaining race information retains its former windshield plane. */
    quest_cockpit_panel(panels+12,0,.9895833f,-3,2.8f,.9208333f,0,0,640,260);
    quest_cockpit_panel(panels+18,0,.1395833f,-3,1.05f,.7791667f,200,260,440,480);
    quest_cockpit_menu_panel(panels+24);
    if(make_image(1280,960,VK_FORMAT_B8G8R8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,&s_cockpit.hud.image,&s_cockpit.hud.view,&s_cockpit.hud.mem) &&
        cockpit_buffer(sizeof panels,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,&s_cockpit.panels,&s_cockpit.panels_mem,panels)) {
        for(int e=0;e<1+s_multiview;++e) {
            s_cockpit.hud_pipeline[e]=cockpit_pipeline(e,4);
            s_cockpit.info_pipeline[e]=cockpit_pipeline(e,5);
        }
    }
    /* Prefer fitted textured Tripo gloves; retain the small fallback if the
     * optional file is missing. Both poses are immutable GPU buffers. */
    size_t glove_size=0;void *glove_data=SDL_LoadFile("cockpits/driver-gloves.nghv",&glove_size);
    if(!glove_data) {
        char *base=SDL_GetBasePath();char glove_path[2048];
        snprintf(glove_path,sizeof glove_path,"%scockpits/driver-gloves.nghv",base?base:"");
        SDL_free(base);glove_data=SDL_LoadFile(glove_path,&glove_size);
    }
    QuestGloveAsset glove;unsigned hand_count=0;int hand_type=1;
    if(quest_glove_asset(&glove,glove_data,glove_size) &&
       cockpit_buffer((VkDeviceSize)glove.vertices*64,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
           &s_cockpit.hands,&s_cockpit.hands_mem,glove.geometry) &&
       cockpit_buffer((VkDeviceSize)glove.width*glove.height*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
           &s_cockpit.hand_staging,&s_cockpit.hand_staging_mem,glove.pixels) &&
       make_image(glove.width,glove.height,VK_FORMAT_R8G8B8A8_UNORM,
           VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_ASPECT_COLOR_BIT,
           &s_cockpit.hand_texture,&s_cockpit.hand_texture_view,&s_cockpit.hand_texture_mem)) {
        VkBufferImageCopy hand_copy={0};
        hand_copy.imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        hand_copy.imageExtent=(VkExtent3D){glove.width,glove.height,1};
        vkCmdCopyBufferToImage(s_cb,s_cockpit.hand_staging,s_cockpit.hand_texture,VK_IMAGE_LAYOUT_GENERAL,1,&hand_copy);
        hand_count=glove.vertices;hand_type=2;
    }
    SDL_free(glove_data);
    QuestGhostMesh *hand=hand_count?NULL:malloc(sizeof *hand);
    QuestGhostVertex *hand_data=hand_count?NULL:malloc(2*QUEST_GHOST_MAX_VERTICES*sizeof *hand_data);
    if(!hand_count && hand && hand_data) {
        quest_ghost_mesh(hand,0); unsigned count=hand->count;
        memcpy(hand_data,hand->vertices,count*sizeof *hand_data);
        quest_ghost_mesh(hand,1);
        memcpy(hand_data+count,hand->vertices,hand->count*sizeof *hand_data);
        if(hand->count==count && cockpit_buffer(2*count*sizeof *hand_data,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               &s_cockpit.hands,&s_cockpit.hands_mem,hand_data)) hand_count=count;
    }
    if(hand_count) {
        s_cockpit.hand_pipeline[0]=cockpit_pipeline(0,hand_type);
        if(s_multiview) s_cockpit.hand_pipeline[1]=cockpit_pipeline(1,hand_type);
        if(s_cockpit.hand_pipeline[0] && (!s_multiview||s_cockpit.hand_pipeline[1])) {
            s_cockpit.hand_vertices=hand_count;
            quest_xr_wheel_configure(asset.wheel_pivot,asset.wheel_axis,.185f);
            LOGE("Cockpit %s gloves: %u triangles per hand, free-position one/two-hand steering enabled\n",
                hand_type==2?"Tripo textured":"fallback",hand_count/3);
        } else LOGE("Cockpit ghost hand initialization failed\n");
    }
    free(hand_data); free(hand);
    quest_xr_cockpit_ready(1);
    LOGE("Cockpit loaded: %u triangles, %ux%u atlas; included in the normal camera cycle\n",
         asset.indices/3,asset.width,asset.height);
    SDL_free(data); return;
fail:
    LOGE("Cockpit GPU initialization failed; original cameras retained\n");
    SDL_free(data);
}

static VkSurf *cockpit_mirror_surface(void)
{
    /* A slot can be reused after leaving a race. Require an actual draw in
     * the just-finished frame, not merely a texture lookup or a stale image. */
    for(int i=0;i<VK_MAX_SURF;++i) {
        VkSurf *s=&s_surf[i];
        if(s->image && s->w==256 && s->h==64 && s->eye==-1 &&
           s->draw_frame+1==s_frame && s->gen) return s;
    }
    return NULL;
}

static void cockpit_draw(VkSurf *target,int eye)
{
    if(!target || !s_cockpit.ready) return;
    if(target->pw!=s_cockpit.width || target->ph!=s_cockpit.height || target->layers!=s_cockpit.layers) {
        if(s_cockpit.depth) garbage_add(s_cockpit.depth,s_cockpit.depth_view,s_cockpit.depth_mem);
        s_cockpit.depth=VK_NULL_HANDLE; s_cockpit.width=s_cockpit.height=0;
        if(!make_image_n(target->pw,target->ph,target->layers,s_depth_fmt,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,
            &s_cockpit.depth,&s_cockpit.depth_view,&s_cockpit.depth_mem)) return;
        s_cockpit.width=target->pw; s_cockpit.height=target->ph; s_cockpit.layers=target->layers;
    }
    VkDeviceSize mirror_offsets[3]={0};
    if(s_cockpit.mirror_frame+1==s_frame) for(int part=0;part<3;++part) {
        unsigned count=s_cockpit.mirror_count[part];
        if(!count) continue;
        void *mapped=NULL;
        mirror_offsets[part]=ring_alloc(count*sizeof(QuestGhostVertex),16,&mapped);
        if(!mapped) return;
        memcpy(mapped,s_cockpit.mirror_vertices[part],count*sizeof(QuestGhostVertex));
    }
    barrier_all();
    VkRenderingAttachmentInfo color={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView=target->view; color.imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD; color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView=s_cockpit.depth_view; depth.imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    depth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; depth.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil.depth=1;
    VkRenderingInfo info={VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea.extent=(VkExtent2D){target->pw,target->ph};
    info.layerCount=1; info.viewMask=target->layers==2?3:0;
    info.colorAttachmentCount=1; info.pColorAttachments=&color; info.pDepthAttachment=&depth;
    p_begin_rendering(s_cb,&info);
    set_viewport(target->pw,target->ph);
    vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.pipeline[target->layers==2]);
    float matrices[2][16];
    quest_xr_cockpit_matrix(eye,matrices[0]); quest_xr_cockpit_matrix(1,matrices[1]);
    vkCmdPushConstants(s_cb,s_cockpit.layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof matrices,matrices);
    VkDescriptorImageInfo image={s_cockpit.sampler,s_cockpit.texture_view,VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstBinding=0; write.descriptorCount=1; write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo=&image;
    p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,1,&write);
    VkDeviceSize offset=0;
    vkCmdBindVertexBuffers(s_cb,0,1,&s_cockpit.geometry,&offset);
    vkCmdBindIndexBuffer(s_cb,s_cockpit.geometry,(VkDeviceSize)s_cockpit.vertices*32,VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(s_cb,s_cockpit.wheel_first,1,0,0,0);
    vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.pipeline[target->layers==2]);
    VkSurf *mirror=cockpit_mirror_surface();
    if(mirror && s_cockpit.mirror_pipeline[target->layers==2]) {
        image.imageView=mirror->view;
        p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,1,&write);
        vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.mirror_pipeline[target->layers==2]);
        if(s_cockpit.mirror_frame+1==s_frame) for(int part=0;part<3;++part) {
            unsigned count=s_cockpit.mirror_count[part];
            if(!count) continue;
            vkCmdBindVertexBuffers(s_cb,0,1,&s_f->ring,&mirror_offsets[part]);
            vkCmdDraw(s_cb,count,1,0,0);
        }
        vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.pipeline[target->layers==2]);
        vkCmdBindVertexBuffers(s_cb,0,1,&s_cockpit.geometry,&offset);
        image.imageView=s_cockpit.texture_view;
        p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,1,&write);
        static int reported;
        if(!reported) {
            reported=1;
            LOGE("Cockpit live rear mirror: surface=%08x %ux%u, original glass, shared rear camera\n",mirror->va,mirror->pw,mirror->ph);
        }
    }
    if(s_cockpit.hud_split && s_cockpit.hud.gen && s_cockpit.hud.draw_frame+1==s_frame && s_cockpit.info_pipeline[target->layers==2]) {
        /* The title's transparent gauge is an instrument overlay on the
         * fixed dashboard. Draw before the separate wheel/hands so those
         * naturally occlude it, without slicing the cabin or adding a black
         * rectangle. Its eye-space plane still moves with head parallax. */
        image.imageView=s_cockpit.hud.view;
        p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,1,&write);
        vkCmdBindVertexBuffers(s_cb,0,1,&s_cockpit.panels,&offset);
        vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.info_pipeline[target->layers==2]);
        vkCmdDraw(s_cb,6,1,0,0);
        vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.pipeline[target->layers==2]);
        vkCmdBindVertexBuffers(s_cb,0,1,&s_cockpit.geometry,&offset);
        image.imageView=s_cockpit.texture_view;
        p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,1,&write);
    }
    /* Update only on the first eye, so both eyes use the identical wheel pose.
     * Immutable vertices stay on the GPU; only two matrices change per frame. */
    if(eye==0) {
        uint32_t now=SDL_GetTicks();
        s_cockpit.steering=quest_cockpit_steering_smooth(s_cockpit.steering,
            quest_xr_cockpit_steering(),(uint32_t)(now-s_cockpit.wheel_tick)*.001f);
        s_cockpit.wheel_tick=now;
    }
    float wheel[16];
    quest_cockpit_wheel(wheel,s_cockpit.wheel_pivot,s_cockpit.wheel_axis,s_cockpit.steering);
    for(int e=0;e<2;++e) quest_cockpit_multiply(matrices[e],matrices[e],wheel);
    vkCmdPushConstants(s_cb,s_cockpit.layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof matrices,matrices);
    vkCmdDrawIndexed(s_cb,s_cockpit.wheel_count,1,s_cockpit.wheel_first,0,0);
    if(s_cockpit.hand_vertices) {
        if(s_cockpit.hand_texture_view) {
            image.imageView=s_cockpit.hand_texture_view;
            p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,1,&write);
        }
        for(int hand=0;hand<2;++hand) {
            float pose[16]; int grasped=0;
            if(!quest_xr_hand_matrix(hand,s_cockpit.steering,pose,&grasped)) continue;
            quest_xr_cockpit_matrix(eye,matrices[0]); quest_xr_cockpit_matrix(1,matrices[1]);
            for(int e=0;e<2;++e) quest_cockpit_multiply(matrices[e],matrices[e],pose);
            vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.hand_pipeline[target->layers==2]);
            vkCmdPushConstants(s_cb,s_cockpit.layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof matrices,matrices);
            VkDeviceSize hand_offset=grasped?(VkDeviceSize)s_cockpit.hand_vertices*sizeof(QuestGhostVertex):0;
            vkCmdBindVertexBuffers(s_cb,0,1,&s_cockpit.hands,&hand_offset);
            vkCmdDraw(s_cb,s_cockpit.hand_vertices,1,0,0);
        }
    }
    if(s_cockpit.hud.gen && s_cockpit.hud.draw_frame+1==s_frame &&
       s_cockpit.hud_pipeline[target->layers==2]) {
        image.imageView=s_cockpit.hud.view;
        p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,1,&write);
        quest_xr_cockpit_matrix(eye,matrices[0]);quest_xr_cockpit_matrix(1,matrices[1]);
        vkCmdPushConstants(s_cb,s_cockpit.layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof matrices,matrices);
        vkCmdBindVertexBuffers(s_cb,0,1,&s_cockpit.panels,&offset);
        if(s_cockpit.hud_split) {
            vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.hud_pipeline[target->layers==2]);
            vkCmdDraw(s_cb,6,1,6,0);
            vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.info_pipeline[target->layers==2]);
            vkCmdDraw(s_cb,12,1,12,0);
        } else {
            /* Draw the entire pause UI after the cabin/hands, without depth
             * occlusion. Both eyes use the mode recorded with this HUD image. */
            vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.info_pipeline[target->layers==2]);
            vkCmdDraw(s_cb,6,1,24,0);
        }
    }
    p_end_rendering(s_cb);
    s_dyn_valid=0; s_bound_pipe=VK_NULL_HANDLE;
    ++s_render_generation;
}
