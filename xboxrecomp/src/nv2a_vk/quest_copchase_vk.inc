/* Main-view world overlay. Uses the actual tracked world depth attachment;
 * it never draws a spike strip through a road, wall or vehicle. */
#include "quest_copchase_scene.h"
static VkPipelineLayout s_copchase_layout;
static VkPipeline s_copchase_pipeline[2];
static struct {
    uint32_t color,depth,frame,pw,ph,layers;
    VkImage color_image,depth_image;
    VkImageView color_view,depth_view;
    int eye,valid;
} s_copchase_world[2];
static void copchase_world_depth(VkSurf *s,VkDepthBuf *d)
{
    if(!s || !d || !s->image || !s->view || !d->image || !d->view || s->w!=640 || s->h!=480 ||
       s->pw!=d->pw || s->ph!=d->ph || s->layers!=d->layers)return;
    unsigned eye=s_eye?1:0;
    s_copchase_world[eye].color=s->va;s_copchase_world[eye].depth=d->va;
    s_copchase_world[eye].frame=s_frame;s_copchase_world[eye].pw=d->pw;s_copchase_world[eye].ph=d->ph;
    s_copchase_world[eye].layers=d->layers;s_copchase_world[eye].eye=d->eye;
    s_copchase_world[eye].color_image=s->image;s_copchase_world[eye].color_view=s->view;
    s_copchase_world[eye].depth_image=d->image;s_copchase_world[eye].depth_view=d->view;
    s_copchase_world[eye].valid=1;
    if(s->layers==2)s_copchase_world[1]=s_copchase_world[0];
}
/* A later clear discards the tracked world contents even if addresses and
 * attachments stay identical. Invalidate all views of the affected image;
 * another verified world draw can establish a fresh tag. Stencil-only clears
 * preserve the depth proof. Called before either attachment-clear path. */
static void copchase_world_clear(VkSurf *s,VkDepthBuf *d,VkImageAspectFlags aspects)
{
    for(unsigned eye=0;eye<2;++eye)
        if(((aspects&VK_IMAGE_ASPECT_COLOR_BIT) && s && s->image &&
            s_copchase_world[eye].color_image==s->image) ||
           ((aspects&VK_IMAGE_ASPECT_DEPTH_BIT) && d && d->image &&
            s_copchase_world[eye].depth_image==d->image))
            s_copchase_world[eye].valid=0;
}
static VkPipeline copchase_scene_pipeline(int multiview)
{
    if(s_copchase_pipeline[multiview])return s_copchase_pipeline[multiview];
    if(!s_copchase_layout){
        VkPushConstantRange range={VK_SHADER_STAGE_VERTEX_BIT,0,128};
        VkPipelineLayoutCreateInfo layout={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&range;
        if(vkCreatePipelineLayout(s_dev,&layout,NULL,&s_copchase_layout)!=VK_SUCCESS)return VK_NULL_HANDLE;
    }
    char vertex[700];snprintf(vertex,sizeof vertex,
        "#version 450\n%s\nlayout(location=0) in vec3 position;layout(location=1) in vec3 rgb;"
        "layout(location=0) out vec3 shade;layout(push_constant) uniform Camera {mat4 eye[2];} camera;"
        "void main(){gl_Position=camera.eye[%s]*vec4(position,1);shade=rgb;}",
        multiview?"#extension GL_EXT_multiview : require":"",multiview?"gl_ViewIndex":"0");
    const char *fragment="#version 450\nlayout(location=0) in vec3 shade;layout(location=0) out vec4 color;void main(){color=vec4(shade,1);}";
    VkShaderModule vs=compile(GLSLANG_STAGE_VERTEX,vertex),fs=compile(GLSLANG_STAGE_FRAGMENT,fragment);
    if(!vs || !fs){if(vs)vkDestroyShaderModule(s_dev,vs,NULL);if(fs)vkDestroyShaderModule(s_dev,fs,NULL);return VK_NULL_HANDLE;}
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fs,.pName="main"}};
    VkVertexInputBindingDescription binding={0,sizeof(QuestCopChaseVertex),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2]={{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32_SFLOAT,12}};
    VkPipelineVertexInputStateCreateInfo vi={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount=1;vi.pVertexBindingDescriptions=&binding;vi.vertexAttributeDescriptionCount=2;vi.pVertexAttributeDescriptions=attributes;
    VkPipelineInputAssemblyStateCreateInfo ia={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo ms={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth={VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable=depth.depthWriteEnable=VK_TRUE;depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState attachment={0};attachment.colorWriteMask=15;
    VkPipelineColorBlendStateCreateInfo blend={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&attachment;
    const VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic={VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=states;
    VkFormat color=VK_FORMAT_B8G8R8A8_UNORM;VkPipelineRenderingCreateInfo rendering={VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.viewMask=multiview?3:0;rendering.colorAttachmentCount=1;rendering.pColorAttachmentFormats=&color;rendering.depthAttachmentFormat=s_depth_fmt;
    VkGraphicsPipelineCreateInfo ci={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.pNext=&rendering;ci.stageCount=2;ci.pStages=stages;ci.layout=s_copchase_layout;
    ci.pVertexInputState=&vi;ci.pInputAssemblyState=&ia;ci.pViewportState=&viewport;ci.pRasterizationState=&raster;
    ci.pMultisampleState=&ms;ci.pDepthStencilState=&depth;ci.pColorBlendState=&blend;ci.pDynamicState=&dynamic;
    vkCreateGraphicsPipelines(s_dev,VK_NULL_HANDLE,1,&ci,NULL,&s_copchase_pipeline[multiview]);
    vkDestroyShaderModule(s_dev,vs,NULL);vkDestroyShaderModule(s_dev,fs,NULL);return s_copchase_pipeline[multiview];
}
static void copchase_scene_draw(VkSurf *target,int eye)
{
    if(!target || !s_stereo_frame || !quest_xr_tracking())return;
    const unsigned e=eye?1:0;
    if(!s_copchase_world[e].valid || s_copchase_world[e].frame+1!=s_frame || s_copchase_world[e].color!=target->va ||
       s_copchase_world[e].color_image!=target->image || s_copchase_world[e].color_view!=target->view ||
       s_copchase_world[e].pw!=target->pw || s_copchase_world[e].ph!=target->ph || s_copchase_world[e].layers!=target->layers)return;
    VkDepthBuf *depth=NULL;for(unsigned i=0;i<VK_MAX_DEPTH;++i){VkDepthBuf *d=&s_depth[i];
        if(d->image && d->image==s_copchase_world[e].depth_image && d->view==s_copchase_world[e].depth_view &&
           d->va==s_copchase_world[e].depth && d->eye==s_copchase_world[e].eye &&
           d->pw==target->pw && d->ph==target->ph && d->layers==target->layers){depth=d;break;}}
    if(!depth)return;
    QuestCopChaseScene scene;float view[16],matrices[2][16],xr[16];
    if(!quest_xr_copchase_scene(&scene,view) || !scene.enabled){if(!eye)quest_xr_copchase_scene_available(0);return;}
    VkPipeline pipeline=copchase_scene_pipeline(target->layers==2);
    if(!eye)quest_xr_copchase_scene_available(pipeline!=VK_NULL_HANDLE);if(!pipeline)return;
    if(!scene.active)return;
    QuestCopChaseVertex vertices[QUEST_COPCHASE_SCENE_VERTICES];unsigned count=quest_copchase_scene_mesh(vertices,&scene);if(!count)return;
    for(int i=0;i<2;++i){quest_xr_matrix(i?1:eye,s_cockpit_frame,xr);if(!quest_copchase_scene_matrix(matrices[i],xr,view))return;}
    void *mapped=NULL;VkDeviceSize offset=ring_alloc(count*sizeof *vertices,16,&mapped);if(!mapped)return;memcpy(mapped,vertices,count*sizeof *vertices);
    end_rendering();barrier_all();
    quest_depth_layout_transition(s_cb,depth->image,&depth->layout,depth->layers,
        VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,s_depth_optimal>0);
    VkRenderingAttachmentInfo color={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView=target->view;color.imageLayout=VK_IMAGE_LAYOUT_GENERAL;color.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;color.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo z={VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    z.imageView=depth->view;z.imageLayout=depth->layout;z.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;z.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info={VK_STRUCTURE_TYPE_RENDERING_INFO};info.renderArea.extent=(VkExtent2D){target->pw,target->ph};
    info.layerCount=1;info.viewMask=target->layers==2?3:0;info.colorAttachmentCount=1;info.pColorAttachments=&color;info.pDepthAttachment=&z;
    p_begin_rendering(s_cb,&info);set_viewport(target->pw,target->ph);
    vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdBindVertexBuffers(s_cb,0,1,&s_f->ring,&offset);
    vkCmdPushConstants(s_cb,s_copchase_layout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof matrices,matrices);vkCmdDraw(s_cb,count,1,0,0);
    p_end_rendering(s_cb);s_rt=NULL;s_rt_depth=NULL;s_dyn_valid=0;s_bound_pipe=VK_NULL_HANDLE;++s_render_generation;
}
