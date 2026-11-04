/* Included only by the Vulkan OpenXR backend. Immutable shared interior,
 * tracked instruments, original mirror glass and animated wheel/hands. */
#include "quest_cockpit_asset.h"
#include "quest_cockpit.h"
#include "quest_ghost_hands.h"
#include "quest_glove_asset.h"
#include "quest_cockpit_panels.h"
#include "quest_cockpit_occlusion.h"
#include "quest_cockpit_vehicle.h"
#include "quest_radio_display.h"
static int s_cockpit_frame;
static int s_cockpit_vehicle;
static uint32_t s_cockpit_windows[QUEST_COCKPIT_WINDOWS];
static struct {
    int ready;
    VkBuffer geometry, staging;
    VkDeviceMemory geometry_mem, staging_mem;
    VkImage texture, depth;
    VkImageView texture_view, depth_view;
    VkDeviceMemory texture_mem, depth_mem;
    VkSampler sampler,depth_sampler;
    VkDescriptorSetLayout descriptors;
    VkPipelineLayout layout;
    VkPipeline pipeline[2];
    VkPipeline wheel_pipeline[2];
    VkPipeline hand_pipeline[2];
    VkPipeline mirror_pipeline[2];
    VkPipeline radio_pipeline[2];
    QuestRadioFont radio_font;
    QuestRadioSong radio_song;
    uint32_t radio_song_tick;
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
    uint32_t shifter_first, shifter_count;
    QuestCockpitPedal pedals[3];
    float wheel_pivot[3], wheel_axis[3], steering;
} s_cockpit;
/* Static meshes are read every frame. Put them in device-local memory rather
 * than forcing discrete GPUs to fetch vertices over PCIe from upload heaps. */
static struct { VkBuffer buffer;VkDeviceMemory memory; } s_cockpit_uploads[4];
static unsigned s_cockpit_upload_count,s_cockpit_upload_frame;
static void attr_to_float4(const Nv2aRawBatch *,uint32_t,uint32_t,float[4]);
static struct {
    uint64_t hash;
    unsigned vertices,indices,count[3];
    float positions[512][3];uint32_t topology[1536];
    QuestGhostVertex glass[3][192];
} s_mirror_cache[128];

static void cockpit_collect_mirrors(const Nv2aRawBatch *b,const uint32_t *indices,unsigned count,float sx,float sy)
{
    if(b->vertex_count>512 || count>1536)return;
    if(s_cockpit.mirror_frame!=s_frame) {
        s_cockpit.mirror_frame=s_frame;memset(s_cockpit.mirror_count,0,sizeof s_cockpit.mirror_count);
    }
    if(s_cockpit.mirror_count[0] && s_cockpit.mirror_count[1] && s_cockpit.mirror_count[2])return;
    float positions[512][3];uint64_t hash=14695981039346656037ull;
    for(unsigned v=0;v<b->vertex_count;++v) {
        float p[4];if(b->attr_direct&1)attr_to_float4(b,0,v,p);else memcpy(p,b->attrs+(size_t)v*64,16);
        positions[v][0]=-p[1];positions[v][1]=p[2];positions[v][2]=-p[0];
    }
    const unsigned char *bytes=(const unsigned char *)positions;
    for(unsigned i=0;i<b->vertex_count*12;++i)hash=(hash^bytes[i])*1099511628211ull;
    bytes=(const unsigned char *)indices;
    for(unsigned i=0;i<count*4;++i)hash=(hash^bytes[i])*1099511628211ull;
    unsigned slot=(unsigned)(hash%128);
    if(s_mirror_cache[slot].hash!=hash || s_mirror_cache[slot].vertices!=b->vertex_count ||
       s_mirror_cache[slot].indices!=count || memcmp(s_mirror_cache[slot].positions,positions,b->vertex_count*12) ||
       memcmp(s_mirror_cache[slot].topology,indices,count*4)) {
        s_mirror_cache[slot].hash=hash;s_mirror_cache[slot].vertices=b->vertex_count;s_mirror_cache[slot].indices=count;
        memcpy(s_mirror_cache[slot].positions,positions,b->vertex_count*12);memcpy(s_mirror_cache[slot].topology,indices,count*4);
        const float zero[3]={0};
        for(int part=0;part<3;++part)s_mirror_cache[slot].count[part]=quest_cockpit_mirror_mesh(
            s_mirror_cache[slot].glass[part],192,(const float (*)[3])positions,b->vertex_count,indices,count,zero,part);
    }
    /* Convert shell coordinates to the fixed base cabin. Seat changes are
     * already shared by the world and interior XR matrices, never the cache. */
    float center[3]={b->vp_consts[96][3]/sx,b->vp_consts[97][3]/sy,-b->vp_consts[99][3]};
    for(int part=0;part<3;++part)if(!s_cockpit.mirror_count[part]) {
        unsigned n=s_mirror_cache[slot].count[part];s_cockpit.mirror_count[part]=n;
        memcpy(s_cockpit.mirror_vertices[part],s_mirror_cache[slot].glass[part],n*sizeof(QuestGhostVertex));
        for(unsigned v=0;v<n;++v)for(int a=0;a<3;++a)s_cockpit.mirror_vertices[part][v].position[a]+=center[a];
    }
}

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

static int cockpit_static_buffer(VkDeviceSize size,VkBufferUsageFlags usage,
                                  VkBuffer *buffer,VkDeviceMemory *memory,const void *data)
{
    if(s_cockpit_upload_count>=4)return 0;
    unsigned slot=s_cockpit_upload_count++;
    if(!cockpit_buffer(size,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,&s_cockpit_uploads[slot].buffer,
        &s_cockpit_uploads[slot].memory,data))return 0;
    VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=size;ci.usage=usage|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if(vkCreateBuffer(s_dev,&ci,NULL,buffer)!=VK_SUCCESS)return 0;
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(s_dev,*buffer,&req);
    *memory=alloc_mem(req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(!*memory || vkBindBufferMemory(s_dev,*buffer,*memory,0)!=VK_SUCCESS)return 0;
    VkBufferCopy copy={0,0,size};vkCmdCopyBuffer(s_cb,s_cockpit_uploads[slot].buffer,*buffer,1,&copy);
    VkBufferMemoryBarrier barrier={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_INDEX_READ_BIT;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer=*buffer;barrier.size=VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(s_cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,0,NULL,1,&barrier,0,NULL);
    s_cockpit_upload_frame=s_frame;
    return 1;
}

static VkPipeline cockpit_pipeline(int multiview,int ghost)
{
    char vertex[2400],fragment[4096];
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
    if(ghost==0) {
        snprintf(vertex,sizeof vertex,
            "#version 450\n%s\n"
            "layout(location=0) in vec3 position;layout(location=1) in vec3 normal;"
            "layout(location=2) in vec2 uv;layout(location=0) out vec2 texcoord;"
            "layout(location=1) out float shade;layout(location=2) out float cabinZ;"
            "layout(push_constant) uniform Camera {mat4 eye[2];} camera;"
            "layout(set=0,binding=1) uniform Fit {vec4 scales;vec4 limits;vec4 lever;vec4 pedals[3];vec4 travel;} fit;"
            "void main(){vec3 q=position;vec3 n=normal;"
            "if(gl_VertexIndex>=int(fit.pedals[0].x)&&gl_VertexIndex<int(fit.pedals[2].y)){"
            "int i=gl_VertexIndex<int(fit.pedals[0].y)?0:(gl_VertexIndex<int(fit.pedals[1].y)?1:2);"
            "float angle=fit.travel[i]*vec3(.68,.47,.75)[i];float c=cos(angle),s=sin(angle);"
            "mat2 hinge=mat2(c,s,-s,c);q.yz=fit.pedals[i].zw+hinge*(q.yz-fit.pedals[i].zw);n.yz=hinge*n.yz;"
            /* The rest of a right-hand-drive cabin is mirrored. Keep clutch,
             * brake and accelerator ordered left to right around its brake. */
            "if(fit.scales.w<0.){q.x=2.*fit.travel.w-q.x;n.x=-n.x;}}"
            "float sx=(q.x<0?fit.scales.x:fit.scales.y)*fit.scales.w;"
            "q.x*=sx;n.x/=sx;"
            "if(q.z<-.33){q.z=-.33+(q.z+.33)*fit.scales.z;n.z/=fit.scales.z;}"
            /* Bend the gaiter above its fixed base. The complete upper grip
             * translates rigidly by the exact offset used for hand contact. */
            "if(gl_VertexIndex>=int(fit.lever.z)&&gl_VertexIndex<int(fit.lever.w)){"
            "float t=(q.y+.577)/.115;q.xz+=fit.lever.xy*clamp(t,0.,1.);"
            "if(t>0.&&t<1.)n.y-=dot(n.xz,fit.lever.xy)/.115;}"
            "gl_Position=camera.eye[%s]*vec4(q,1);texcoord=uv;cabinZ=position.z;"
            "shade=.78+.22*max(dot(normalize(n),normalize(vec3(-.3,.9,-.2))),0.0);}",
            multiview?"#extension GL_EXT_multiview : require":"",multiview?"gl_ViewIndex":"0");
        snprintf(fragment,sizeof fragment,
            "#version 450\nlayout(location=0) in vec2 texcoord;layout(location=1) in float shade;"
            "layout(location=2) in float cabinZ;"
            "layout(set=0,binding=0) uniform %s atlas;"
            "layout(set=0,binding=1) uniform Fit {vec4 scales;vec4 limits;vec4 lever;vec4 pedals[3];vec4 travel;} fit;"
            "layout(location=0) out vec4 color;"
            "void main(){if(cabinZ>fit.limits.x)discard;"
            "vec4 c=texture(atlas,%s);if(c.a<.5)discard;color=vec4(c.rgb*shade,1);}",
            s_multiview?"sampler2DArray":"sampler2D",s_multiview?"vec3(texcoord,0)":"texcoord");
    } else if(ghost==1) {
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
        snprintf(vertex,sizeof vertex,
            "#version 450\n%s\n"
            "layout(location=0) in vec3 position;layout(location=1) in vec3 normal;layout(location=2) in vec2 uv;"
            "layout(location=0) out vec3 glass;layout(location=1) out vec3 optic;layout(location=2) out vec2 legacy;"
            "layout(location=3) flat out int eye;layout(push_constant) uniform Camera {mat4 eyes[2];} camera;"
            "void main(){eye=%s;gl_Position=camera.eyes[eye]*vec4(position,1);glass=position;optic=normal;legacy=uv;}",
            multiview?"#extension GL_EXT_multiview : require":"",multiview?"int(gl_ViewIndex)":"0");
        snprintf(fragment,sizeof fragment,
            "#version 450\nlayout(location=0) in vec3 glass;layout(location=1) in vec3 optic;"
            "layout(location=2) in vec2 legacy;layout(location=3) flat in int eye;layout(location=0) out vec4 color;"
            "layout(binding=0) uniform %s atlas;layout(binding=3) uniform %s depths;"
            "layout(binding=2) uniform Optics {vec4 eyes[2];vec4 axes[3];vec4 source;vec4 proj;} m;"
            "vec3 rear(vec3 v){return vec3(dot(m.axes[0].xyz,v),dot(m.axes[1].xyz,v),dot(m.axes[2].xyz,v));}"
            "vec3 rgb(vec2 p){return texture(atlas,%s).rgb;}"
            "float depth(vec2 p){return texture(depths,%s).r;}"
            "bool inside(vec2 p){return all(greaterThanEqual(p,vec2(0)))&&all(lessThanEqual(p,vec2(1)));}"
            "void main(){if(m.source.w<.5){color=vec4(rgb(vec2(1.-legacy.x,legacy.y)),1);return;}"
            "vec3 ray=rear(reflect(normalize(glass-m.eyes[eye].xyz),normalize(optic)));"
            "vec3 origin=rear(glass-m.source.xyz);"
            "if(ray.z<=.001){color=vec4(.04,.045,.05,1);return;}"
            /* In the rear pinhole camera UV along a ray is affine in 1/Z.
             * Intersect against the actual rear scene depth, separately for
             * each eye, so a nearby car moves differently from the horizon. */
            "vec2 slope=ray.xy/ray.z;vec2 farUV=.5+.5*m.proj.xy*slope;"
            "vec2 offset=.5*m.proj.xy*(origin.xy-origin.z*slope);vec2 uv=farUV;"
            "if(m.eyes[0].w>.5 && dot(offset,offset)>1.e-8){"
            "float nearZ=max(max(origin.z,.01),-m.proj.w/m.proj.z);"
            "float prev=1./nearZ;float farInv=max(0.,(1.-m.proj.z)/m.proj.w);"
            "for(int i=0;i<=12;i++){float inv=mix(1./nearZ,farInv,float(i)/12.);vec2 p=farUV+offset*inv;"
            "if(inside(p)&&inv<=(depth(p)-m.proj.z)/m.proj.w){"
            "for(int j=0;j<4;j++){float mid=(prev+inv)*.5;vec2 q=farUV+offset*mid;"
            "if(inside(q)&&mid<=(depth(q)-m.proj.z)/m.proj.w)inv=mid;else prev=mid;}"
            "uv=farUV+offset*inv;break;}prev=inv;}}"
            "color=vec4(inside(uv)?rgb(uv):vec3(.04,.045,.05),1);}",
            s_multiview?"sampler2DArray":"sampler2D",s_multiview?"sampler2DArray":"sampler2D",
            s_multiview?"vec3(p,0)":"p",s_multiview?"vec3(p,0)":"p");
    } else if(ghost==7) {
        snprintf(vertex,sizeof vertex,
            "#version 450\n%s\n"
            "layout(location=0) in vec3 position;layout(location=1) in vec3 state;layout(location=2) in vec2 uv;"
            "layout(location=0) out vec2 coord;layout(location=1) flat out vec3 radio;"
            "layout(push_constant) uniform Camera{mat4 eyes[2];}camera;"
            "void main(){gl_Position=camera.eyes[%s]*vec4(position,1);coord=uv;radio=state;}",
            multiview?"#extension GL_EXT_multiview : require":"",multiview?"gl_ViewIndex":"0");
        snprintf(fragment,sizeof fragment,
            "#version 450\nlayout(location=0) in vec2 coord;layout(location=1) flat in vec3 radio;layout(location=0) out vec4 color;"
            "layout(set=0,binding=2,std140) uniform Display{uvec4 cells[%d];vec2 scroll;uint hoverMask;}lcd;"
            "float dotAt(vec2 p,int line){ivec2 xy=ivec2(floor(p));if(xy.x<0||xy.x>=%d||xy.y<0||xy.y>=7)return 0.;"
            "int x=xy.x%%6;if(x>=5)return 0.;uint bits=lcd.cells[xy.x/6][2*line+xy.y/4];"
            "return float((bits>>uint(5*(xy.y%%4)+4-x))&1u);}"
            "float box(vec2 p,vec2 halfSize,float r){vec2 q=abs(p)-halfSize+r;"
            "return length(max(q,0.))+min(max(q.x,q.y),0.)-r;}"
            "float ink(float d){return 1.-smoothstep(-.012,.012,d);}"
            "vec3 controlFace(vec3 bg,float d,int id,vec3 face){"
            "if(radio.z==float(id))face=vec3(.08,.36,.48);"
            "else if((lcd.hoverMask&(1u<<uint(id)))!=0u)"
            "face=mix(vec3(.11,.24,.29),vec3(.43,.90,1.),smoothstep(-.055,-.04,d));"
            "return mix(bg,face,ink(d));}"
            "void main(){vec2 p=vec2(coord.x*4.45,coord.y-.5);"
            "if(coord.y>1.){float rim=box(vec2(p.x-2.225,coord.y-1.39),vec2(2.225,.39),.08);if(rim>0.)discard;"
            "int line=coord.y<1.425?0:1;float top=line==0?1.10:1.465,h=line==0?.265:.22;"
            "vec2 grid=vec2(((coord.x-.04)*%.1f/.92+lcd.scroll[line])*6.,(coord.y-top)*7./h);"
            "vec2 a=min(fwidth(grid)*.28,vec2(.7));float lit=.25*(dotAt(grid+a,line)+dotAt(grid-a,line)"
            "+dotAt(grid+vec2(a.x,-a.y),line)+dotAt(grid+vec2(-a.x,a.y),line));"
            "if(coord.x<.04||coord.x>.96)lit=0.;vec3 bg=mix(vec3(.20,.23,.26),vec3(.026,.044,.055),ink(rim+.025));"
            "color=vec4(mix(bg,line==0?vec3(.85,.96,1.):vec3(.39,.78,.90),lit),1);return;}"
            "float edge=box(p-vec2(2.225,0),vec2(2.225,.5),.12);"
            "if(edge>0.)discard;vec3 col=mix(vec3(.20,.23,.26),vec3(.055,.066,.08),ink(edge+.026));"
            "vec3 light=vec3(.79,.89,.94),blue=vec3(.23,.72,.93);"
            "col=controlFace(col,box(p-vec2(1.025,0),vec2(.98,.36),.07),1,col);"
            "float level=clamp(radio.x,0.,1.);float start=.09*4.45,end=.42*4.45,knob=mix(start,end,level);"
            "float track=ink(box(p-vec2((start+end)*.5,0),vec2((end-start)*.5,.043),.03));"
            "col=mix(col,p.x<=knob?blue:vec3(.22,.26,.30),track);"
            "col=mix(col,light,ink(box(p-vec2(knob,0),vec2(.065,.205),.035)));"
            "vec2 sp=p-vec2(.19,0);float speaker=min(box(sp+vec2(.048,0),vec2(.042,.057),.01),"
            "max(abs(sp.x)-.06,abs(sp.y)-(.10+.8*sp.x)));col=mix(col,light,ink(speaker));"
            "for(int i=0;i<3;++i){vec2 q=p-vec2((.55+.18*float(i))*4.45,0);"
            "col=controlFace(col,box(q,vec2(.355,.36),.07),i+2,vec3(.13,.16,.19));"
            "if(i==0)q.x=-q.x;float icon=max(-q.x-.10,max(q.x-.15,abs(q.y)-(.15-q.x)*.66));"
            "if(i!=1)icon=min(icon,box(q-vec2(.18,0),vec2(.024,.16),.005));"
            "else if(radio.y<.5)icon=box(vec2(abs(q.x)-.083,q.y),vec2(.032,.166),.008);"
            "col=mix(col,i==1?blue:light,ink(icon));}"
            "color=vec4(col*(radio.z<0.?.52:1.),1);}",
            QUEST_RADIO_COLUMNS+1,(QUEST_RADIO_COLUMNS+1)*6,(double)QUEST_RADIO_COLUMNS);
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
    ds.depthTestEnable=(ghost==5||ghost==7)?VK_FALSE:VK_TRUE; ds.depthWriteEnable=(ghost==4||ghost==5||ghost==7)?VK_FALSE:VK_TRUE; ds.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
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
    VkDescriptorSetLayoutBinding bindings[4]={
        {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL},
        {1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,NULL},
        {2,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL},
        {3,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL}};
    VkDescriptorSetLayoutCreateInfo dci={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    dci.bindingCount=4; dci.pBindings=bindings;
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
    sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;
    if(vkCreateSampler(s_dev,&sampler,NULL,&s_cockpit.depth_sampler)!=VK_SUCCESS)goto fail;
    s_cockpit.pipeline[0]=cockpit_pipeline(0,0);
    if(s_multiview) s_cockpit.pipeline[1]=cockpit_pipeline(1,0);
    s_cockpit.wheel_pipeline[0]=cockpit_pipeline(0,6);
    if(s_multiview)s_cockpit.wheel_pipeline[1]=cockpit_pipeline(1,6);
    if(!s_cockpit.pipeline[0] || !s_cockpit.wheel_pipeline[0] ||
       (s_multiview && (!s_cockpit.pipeline[1] || !s_cockpit.wheel_pipeline[1]))) goto fail;
    if(!cockpit_static_buffer((VkDeviceSize)asset.vertices*32+(VkDeviceSize)asset.indices*4,
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
    s_cockpit.shifter_first=asset.shifter_first;s_cockpit.shifter_count=asset.shifter_count;
    memcpy(s_cockpit.pedals,asset.pedals,sizeof asset.pedals);
    quest_xr_shifter_configure(asset.shifter_count!=0);
    memcpy(s_cockpit.wheel_axis,asset.wheel_axis,12);
    s_cockpit.mirror_pipeline[0]=cockpit_pipeline(0,3);
    quest_radio_font_init(&s_cockpit.radio_font);
    for(int e=0;e<1+s_multiview;++e)s_cockpit.radio_pipeline[e]=cockpit_pipeline(e,7);
    if(s_multiview) s_cockpit.mirror_pipeline[1]=cockpit_pipeline(1,3);
    /* Shared transparent cockpit UI: split only during driving. Pause menus
     * use the complete image on one plane; other cameras retain their path. */
    s_cockpit.hud=(VkSurf){.w=640,.h=480,.pw=1280,.ph=960,.layers=1,
        .aa_sx=1,.aa_sy=1,.eye=-1,.va=0xfffffffdu};
    QuestGhostVertex all_panels[QUEST_COCKPIT_VEHICLES][30];
    for(int vehicle=0;vehicle<QUEST_COCKPIT_VEHICLES;++vehicle) {
    QuestGhostVertex *panels=all_panels[vehicle];
    /* Place the gauge on the shared instrument face (about 66 cm ahead),
     * rather than floating 10 cm in front of it. Keep the accepted height
     * and size; the instrument overlay remains visible through baked trim. */
    quest_cockpit_panel(panels,0,-.205f,-.66f,.24f,.24f,440,260,640,480);
    /* Navigation on the radio/heater center stack. Follow its sloping face,
     * then apply the same per-car fit as the dashboard (including RHD). */
    quest_cockpit_panel(panels+6,.3116f,-.155f,-.463f,.20f,.22f,0,260,200,480);
    for(int v=6;v<12;++v) {
        panels[v].position[2]-=.40f*(panels[v].position[1]+.155f);
        panels[v].normal[1]=.37139068f;panels[v].normal[2]=.92847669f;
    }
    /* Remaining race information retains its former windshield plane. */
    quest_cockpit_panel(panels+12,0,.9895833f,-3,2.8f,.9208333f,0,0,640,260);
    quest_cockpit_panel(panels+18,0,.1395833f,-3,1.05f,.7791667f,200,260,440,480);
    quest_cockpit_menu_panel(panels+24);
    /* Gauge and map follow the same static dashboard fit. Menus, mirrors,
     * wheel rotation and controller poses retain their independent spaces. */
    for(int v=0;v<12;++v) {
        float position[3];quest_cockpit_fit_point(position,panels[v].position,quest_cockpit_vehicle(vehicle));
        memcpy(panels[v].position,position,sizeof position);
        /* Mirror the mounting position for RHD, not the map or its labels. */
        if(quest_cockpit_vehicle(vehicle)->side<0)
            panels[v].uv[0]=(v<6?1080.0f/640.0f:200.0f/640.0f)-panels[v].uv[0];
    }
    }
    if(make_image(1280,960,VK_FORMAT_B8G8R8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,&s_cockpit.hud.image,&s_cockpit.hud.view,&s_cockpit.hud.mem) &&
        cockpit_static_buffer(sizeof all_panels,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,&s_cockpit.panels,&s_cockpit.panels_mem,all_panels)) {
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
       cockpit_static_buffer((VkDeviceSize)glove.vertices*64,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
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
        if(hand->count==count && cockpit_static_buffer(2*count*sizeof *hand_data,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
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

typedef struct {
    float eyes[2][4],axes[3][4],source[4],projection[4];
} CockpitMirrorUniform;
static VkImageView cockpit_mirror_depth(VkSurf *rear)
{
    if(!rear->rear_depth_va || rear->rear_frame+1!=s_frame)return VK_NULL_HANDLE;
    for(int i=0;i<VK_MAX_DEPTH;++i) {
        VkDepthBuf *d=&s_depth[i];
        if(d->va!=rear->rear_depth_va || !d->sample_depth || d->used+1!=s_frame ||
           d->eye!=rear->eye || d->pw!=rear->pw || d->ph!=rear->ph)continue;
        quest_depth_layout_transition(s_cb,d->image,&d->layout,d->layers,
            VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,0);
        return d->sample_depth;
    }
    return VK_NULL_HANDLE;
}

static void cockpit_draw(VkSurf *target,int eye)
{
    if(!target || !s_cockpit.ready) return;
    if(s_cockpit_upload_count && (uint32_t)(s_frame-s_cockpit_upload_frame)>VK_FRAMES+1) {
        /* frame_begin has waited for every slot used by the initial uploads. */
        for(unsigned i=0;i<s_cockpit_upload_count;++i) {
            if(s_cockpit_uploads[i].buffer)vkDestroyBuffer(s_dev,s_cockpit_uploads[i].buffer,NULL);
            if(s_cockpit_uploads[i].memory)vkFreeMemory(s_dev,s_cockpit_uploads[i].memory,NULL);
        }
        s_cockpit_upload_count=0;
        if(s_cockpit.staging) {vkDestroyBuffer(s_dev,s_cockpit.staging,NULL);vkFreeMemory(s_dev,s_cockpit.staging_mem,NULL);s_cockpit.staging=VK_NULL_HANDLE;}
        if(s_cockpit.hand_staging) {vkDestroyBuffer(s_dev,s_cockpit.hand_staging,NULL);vkFreeMemory(s_dev,s_cockpit.hand_staging_mem,NULL);s_cockpit.hand_staging=VK_NULL_HANDLE;}
    }
    if(target->pw!=s_cockpit.width || target->ph!=s_cockpit.height || target->layers!=s_cockpit.layers) {
        if(s_cockpit.depth) garbage_add(s_cockpit.depth,s_cockpit.depth_view,s_cockpit.depth_mem);
        s_cockpit.depth=VK_NULL_HANDLE; s_cockpit.width=s_cockpit.height=0;
        if(!make_image_n(target->pw,target->ph,target->layers,s_depth_fmt,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,
            &s_cockpit.depth,&s_cockpit.depth_view,&s_cockpit.depth_mem)) return;
        s_cockpit.width=target->pw; s_cockpit.height=target->ph; s_cockpit.layers=target->layers;
    }
    VkDeviceSize mirror_offsets[3]={0};
    /* Keep the fitted cabin UBO, optics and all three glass meshes in the
     * same fenced ring generation even if this is the last draw of a frame. */
    if(s_f->ring_off+3*s_ubo_align+512+sizeof(QuestRadioLCD)+3*(192*sizeof(QuestGhostVertex)+16)>RING_BYTES)
        flush_frame_wait();
    const QuestCockpitVehicle *fit=quest_cockpit_vehicle(s_cockpit_vehicle);
    VkDeviceSize radio_offset=0,lcd_offset=0;
    int draw_radio=s_cockpit.radio_pipeline[target->layers==2] && !quest_xr_cockpit_menu_active();
    if(draw_radio) {
        const float uv[6][2]={{0,0},{1,0},{1,1.78f},{0,0},{1,1.78f},{0,1.78f}};
        QuestRadioSong song;float state[3];uint32_t hover=quest_xr_radio_display(state,&song);
        uint32_t now=SDL_GetTicks();
        if(memcmp(&song,&s_cockpit.radio_song,sizeof song)) {
            s_cockpit.radio_song=song;s_cockpit.radio_song_tick=now;
        }
        void *mapped=NULL;lcd_offset=ring_alloc(sizeof(QuestRadioLCD),s_ubo_align,&mapped);
        if(!mapped)return;
        QuestRadioLCD *lcd=mapped;
        quest_radio_lcd(lcd,&s_cockpit.radio_font,&song,now-s_cockpit.radio_song_tick);
        lcd->hover_mask=hover;
        radio_offset=ring_alloc(6*sizeof(QuestGhostVertex),16,&mapped);
        if(!mapped)return;
        QuestGhostVertex *vertices=mapped;
        for(int i=0;i<6;++i){
            quest_radio_point(vertices[i].position,fit,uv[i][0],uv[i][1]);
            memcpy(vertices[i].uv,uv[i],sizeof uv[i]);memcpy(vertices[i].normal,state,sizeof state);
        }
    }
    void *fit_mapped=NULL;
    float fit_data[28]={fit->near_width,fit->far_width,fit->front_depth,fit->side,fit->rear_limit,0,0,0,
        0,0,(float)s_cockpit.shifter_first,(float)(s_cockpit.shifter_first+s_cockpit.shifter_count)};
    VkDeviceSize fit_offset=ring_alloc(sizeof fit_data,s_ubo_align,&fit_mapped);
    if(!fit_mapped)return;
    quest_xr_shifter_offset(&fit_data[8],&fit_data[9]);
    for(int i=0;i<3;++i) {
        const QuestCockpitPedal *p=&s_cockpit.pedals[i];
        float *range=fit_data+12+i*4;
        range[0]=(float)p->first;range[1]=(float)(p->first+p->count);
        range[2]=p->hinge[1];range[3]=p->hinge[2];
    }
    quest_xr_pedal_travel(fit_data+24);
    fit_data[27]=s_cockpit.pedals[1].hinge[0];
    memcpy(fit_mapped,fit_data,sizeof fit_data);
    VkDeviceSize panel_offset=(VkDeviceSize)s_cockpit_vehicle*30*sizeof(QuestGhostVertex);
    QuestMirrorMount mounts[3]={0};
    if(s_cockpit.mirror_frame+1==s_frame) for(int part=0;part<3;++part) {
        unsigned count=s_cockpit.mirror_count[part];
        if(!count) continue;
        void *mapped=NULL;
        mirror_offsets[part]=ring_alloc(count*sizeof(QuestGhostVertex),16,&mapped);
        if(!mapped) return;
        const QuestGhostVertex *mesh=s_cockpit.mirror_vertices[part];
        QuestGhostVertex *tilted=mapped;
        mounts[part]=quest_mirror_mount(mesh,count);
        QuestMirrorAngle angle=quest_xr_mirror_angle(s_cockpit_vehicle,part);
        float normal[3];quest_xr_mirror_normal(normal,&mounts[part],part,angle);
        for(unsigned v=0;v<count;++v) {
            float local[3];for(int k=0;k<3;++k)local[k]=mesh[v].position[k]-mounts[part].center[k];
            quest_mirror_rotate(tilted[v].position,local,angle,0);
            for(int k=0;k<3;++k)tilted[v].position[k]+=mounts[part].center[k];
            memcpy(tilted[v].normal,normal,sizeof normal);
            memcpy(tilted[v].uv,mesh[v].uv,sizeof tilted[v].uv);
        }
    }
    if(!eye)quest_xr_mirror_mounts(s_cockpit_vehicle,mounts);
    VkSurf *mirror=cockpit_mirror_surface();
    VkImageView sampled_depth=VK_NULL_HANDLE;
    VkDeviceSize optics_offset=0;
    if(mirror) {
        const QuestMirrorCamera *camera=&mirror->rear_camera;
        CockpitMirrorUniform optics={0};
        int have_camera=mirror->rear_camera_valid && mirror->rear_vehicle==s_cockpit_vehicle &&
                        mirror->rear_frame+1==s_frame;
        quest_xr_mirror_eyes(optics.eyes);
        if(eye)memcpy(optics.eyes[0],optics.eyes[1],16); /* sequential right-eye pass */
        for(int j=0;j<3;++j)memcpy(optics.axes[j],camera->axis[j],12);
        memcpy(optics.source,camera->origin,12);optics.source[3]=(float)have_camera;
        memcpy(optics.projection,mirror->rear_projection,16);
        if(have_camera)sampled_depth=cockpit_mirror_depth(mirror);
        optics.eyes[0][3]=sampled_depth?1.f:0.f;
        void *mapped=NULL;
        optics_offset=ring_alloc(sizeof optics,s_ubo_align,&mapped);
        if(!mapped)return;
        memcpy(mapped,&optics,sizeof optics);
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
    VkDescriptorBufferInfo fit_buffer={s_f->ring,fit_offset,sizeof fit_data};
    VkWriteDescriptorSet fit_write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    fit_write.dstBinding=1;fit_write.descriptorCount=1;fit_write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    fit_write.pBufferInfo=&fit_buffer;
    p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,1,&fit_write);
    VkDeviceSize offset=0;
    vkCmdBindVertexBuffers(s_cb,0,1,&s_cockpit.geometry,&offset);
    vkCmdBindIndexBuffer(s_cb,s_cockpit.geometry,(VkDeviceSize)s_cockpit.vertices*32,VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(s_cb,s_cockpit.wheel_first,1,0,0,0);
    vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.pipeline[target->layers==2]);
    if(mirror && s_cockpit.mirror_pipeline[target->layers==2]) {
        VkDescriptorBufferInfo optics={s_f->ring,optics_offset,sizeof(CockpitMirrorUniform)};
        VkDescriptorImageInfo depths={s_cockpit.depth_sampler,sampled_depth?sampled_depth:mirror->view,VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet extra[2]={fit_write,write};
        extra[0].dstBinding=2;extra[0].pBufferInfo=&optics;
        extra[1].dstBinding=3;extra[1].pImageInfo=&depths;
        p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,2,extra);
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
        /* The gauge and center-stack map are instrument overlays on the
         * fixed dashboard. Draw before the separate wheel/hands so those
         * naturally occlude them, without slicing the cabin or adding black
         * rectangles. Their planes still move with head parallax. */
        image.imageView=s_cockpit.hud.view;
        p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,1,&write);
        vkCmdBindVertexBuffers(s_cb,0,1,&s_cockpit.panels,&panel_offset);
        vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.info_pipeline[target->layers==2]);
        vkCmdDraw(s_cb,12,1,0,0);
        vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.pipeline[target->layers==2]);
        vkCmdBindVertexBuffers(s_cb,0,1,&s_cockpit.geometry,&offset);
        image.imageView=s_cockpit.texture_view;
        p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,1,&write);
    }
    if(draw_radio) {
        vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.radio_pipeline[target->layers==2]);
        VkDescriptorBufferInfo display={s_f->ring,lcd_offset,sizeof(QuestRadioLCD)};
        fit_write.dstBinding=2;fit_write.pBufferInfo=&display;
        p_push_desc(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.layout,0,1,&fit_write);
        vkCmdBindVertexBuffers(s_cb,0,1,&s_f->ring,&radio_offset);
        vkCmdDraw(s_cb,6,1,0,0);
        vkCmdBindVertexBuffers(s_cb,0,1,&s_cockpit.geometry,&offset);
    }
    /* Update only on the first eye, so both eyes use the identical wheel pose.
     * Immutable vertices stay on the GPU; only two matrices change per frame. */
    if(eye==0) {
        uint32_t now=SDL_GetTicks();
        int from_rim;
        float target_angle=quest_xr_cockpit_steering(&from_rim);
        s_cockpit.steering=from_rim?target_angle:quest_cockpit_steering_smooth(s_cockpit.steering,
            target_angle,(uint32_t)(now-s_cockpit.wheel_tick)*.001f);
        s_cockpit.wheel_tick=now;
    }
    float wheel[16];
    vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_cockpit.wheel_pipeline[target->layers==2]);
    quest_cockpit_wheel_angle(wheel,s_cockpit.wheel_pivot,s_cockpit.wheel_axis,-s_cockpit.steering*QUEST_WHEEL_LOCK_RADIANS);
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
        vkCmdBindVertexBuffers(s_cb,0,1,&s_cockpit.panels,&panel_offset);
        if(s_cockpit.hud_split) {
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
