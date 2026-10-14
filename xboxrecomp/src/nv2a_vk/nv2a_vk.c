/*
 * nv2a_vk.c -- a Vulkan 1.3 renderer for the pushbuffer executor.
 *
 * The same job as nv2a_gl.c, on Vulkan: registered as the executor's back end
 * (nv2a_backend.h, draw_raw), it turns the method shadow into draws. Built
 * instead of nv2a_gl for the Switch's NVK (mesa-switch, linked statically)
 * and, on Linux, any Vulkan driver (lavapipe runs it headless).
 *
 *   surfaces   one image per colour-surface address (B8G8R8A8, rows top-first
 *              like guest memory), depth per zeta address (as nv2a_gl). All
 *              images live in VK_IMAGE_LAYOUT_GENERAL: a surface is drawn
 *              into, sampled and blitted without layout changes, ordered by
 *              memory barriers between rendering passes.
 *   textures   the GL renderer's cache and decoders; uploaded through the
 *              frame's staging ring. DXT stays compressed (BC1/2/3).
 *   shaders    gl_vsh.c / gl_psh.c in their Vulkan dialect, compiled to
 *              SPIR-V at run time by glslang, cached by what generated them.
 *   pipelines  keyed by the shader pair, blend state, colour mask, whether
 *              there is a depth buffer and the topology class; everything
 *              else (depth, stencil, cull, winding, topology, vertex layout,
 *              viewport) is dynamic state.
 *   uniforms   two std140 blocks per draw in the frame ring, textures as
 *              combined image samplers, all through push descriptors.
 *
 * Two frames in flight. Everything runs on the executor's thread.
 *
 * Runtime switches (the GL renderer's names where they mean the same):
 *   RECOMP_GL_DUMP=<prefix>[,every]  write presented frames as BMP
 *   RECOMP_GL_TRACE=1                shader sources on compile errors
 *   RECOMP_GL_SCALE=<k>              render surfaces at k times the title's
 *                                    resolution, fractions allowed (1.5;
 *                                    0.5..4, default 1)
 *   RECOMP_VK_VALIDATION=1           (Linux) enable the Khronos validation layer
 *   RECOMP_VK_HEADLESS=1             (Linux) no window, no present
 */
#include "nv2a_vk.h"
#include "../nv2a_gl/gl_psh.h"
#include "../nv2a_gl/gl_vsh.h"
#include "../kernel/nv2a_backend.h"

#if defined(__SWITCH__)
#include <switch.h>
#define VK_USE_PLATFORM_VI_NN 1
#endif
#include <vulkan/vulkan.h>
#if !defined(__SWITCH__)
#include <SDL.h>
#include <SDL_vulkan.h>
#endif
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef NFSU2_PCVR
#include "pcvr_platform.h"
#include "pcvr_surface_lookup.h"
#include "pcvr_texture_decode.h"
#endif
#ifdef NFSU2_OPENXR
#include "quest_xr.h"
#include "quest_projection.h"
#include "quest_draw_arena.h"
#include "quest_vertex_bindings.h"
#include "quest_vertex_layout.h"
#include "quest_depth_layout.h"
#include "quest_instance_span.h"
#include "quest_instance_state.h"
#include "quest_instance_bytes.h"
#include "quest_image_barrier.h"
#include "quest_feedback.h"
#endif

extern ptrdiff_t xbox_GetMemoryOffset(void);

#define LOGE(...) fprintf(stderr, "  [VK] " __VA_ARGS__)

static int s_state;            /* 0 untried, 1 ready, -1 failed */
static int s_trace;
/* RECOMP_VK_TRACE=<n>: name every step of the first n draws, clears and
 * flips (a crash inside the driver then ends the log at the step). */
static int s_vtrace;
#define VT(...) do { if (s_vtrace > 0) { fprintf(stderr, "  [VKT] " __VA_ARGS__); fflush(stderr); } } while (0)
static uint32_t s_frame;
/* RECOMP_TEX_CHANGES=1: log every cached texture whose guest bytes changed
 * (re-uploaded). Finds textures a title frees while still drawing them. */
static int s_tex_changes = -1;
static int s_drew_any;
static const uint32_t *s_regs;
static const Nv2aRawBatch *s_batch;

/* Hitch accounting, as in nv2a_gl. */
static uint32_t s_hz_progs, s_hz_texs, s_hz_draws;
static uint64_t s_hz_prog_ns, s_hz_tex_ns, s_hz_tex_bytes;
#ifdef NFSU2_PCVR
static uint64_t s_pcvr_pipeline_ns,s_pcvr_cache_ns;
static uint32_t s_pcvr_pipelines;
#endif
static uint64_t hz_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ── Device ───────────────────────────────────────────────────────── */

static VkInstance       s_inst;
static VkPhysicalDevice s_pd;
static VkDevice         s_dev;
static VkQueue          s_queue;
static uint32_t         s_qfam;
static VkPhysicalDeviceMemoryProperties s_memprops;
static VkDeviceSize     s_ubo_align = 256;
static uint32_t         s_ubo_range = 16384;
static VkFormat         s_depth_fmt = VK_FORMAT_D24_UNORM_S8_UINT;
static int              s_have_bc, s_have_depth_clamp;
#ifdef NFSU2_OPENXR
static uint32_t s_vertex_offset_limit;
static uint64_t s_vertex_bindings_before, s_vertex_bindings_after;
static int compact_vertex_bindings(void)
{
    static int option=-1;
    if(option<0) {
        const char *e=getenv("NFSU2_VR_COMPACT_BINDINGS");
        option=e && *e=='1';
    }
    return option;
}
#endif

static PFN_vkCmdPushDescriptorSetKHR  p_push_desc;
#ifdef NFSU2_OPENXR
static PFN_vkCmdPushDescriptorSetWithTemplateKHR p_push_template;
static VkDescriptorUpdateTemplate s_push_template;
static uint32_t s_template_draws;
#endif
static PFN_vkCmdSetVertexInputEXT     p_vertex_input;
static PFN_vkCmdBeginRendering        p_begin_rendering;
static PFN_vkCmdEndRendering          p_end_rendering;
static PFN_vkCmdSetCullMode           p_cull_mode;
static PFN_vkCmdSetFrontFace          p_front_face;
static PFN_vkCmdSetPrimitiveTopology  p_topology;
static PFN_vkCmdSetDepthTestEnable    p_depth_test;
static PFN_vkCmdSetDepthWriteEnable   p_depth_write;
static PFN_vkCmdSetDepthCompareOp     p_depth_op;
static PFN_vkCmdSetStencilTestEnable  p_stencil_test;
static PFN_vkCmdSetStencilOp          p_stencil_op;
/* Fixed for the lifetime of the device. Non-XR builds retain mono images. */
static int s_multiview;
static uint32_t s_render_generation;
static VkPipeline s_bound_pipe;
static uint32_t s_bound_pipe_gen;
#ifdef NFSU2_OPENXR
static PFN_vkCmdSetFragmentShadingRateKHR p_shading_rate;
static int s_foveation;
static int s_image_barriers = -1;
static int s_feedback_swap = -1;
static int s_pb_fast;
static int s_vertex_layout = -1;
static int s_depth_optimal = -1;
static QuestVertexLayout s_vertex_layout_state;
extern int nv2a_pb_scan_survey(int enabled);
extern void nv2a_pb_scan_bulk(int enabled);
extern void nv2a_pb_data_stats(uint64_t *packets,uint64_t *words);
extern void nv2a_pb_data_verify(int enabled);
#ifdef NFSU2_PCVR
extern void nv2a_pb_data_direct(int enabled);
extern int nv2a_pb_data_direct_verified(void);
extern void nv2a_pb_data_shadow(int enabled);
extern int nv2a_pb_data_shadow_verified(void);
extern void nv2a_pb_index_mode(int enabled);
extern int nv2a_pb_index_verified(void);
extern void nv2a_pb_index_packet_mode(int enabled);
extern int nv2a_pb_index_packet_verified(void);
static int s_pb_direct_compare;
static int s_pb_shadow_compare;
static int s_indices_compare;
static int s_index_packets_compare;
#endif
static VkExtent2D s_rate_texel;
#endif

/* Presentation. */
static VkSurfaceKHR   s_surface;
static VkSwapchainKHR s_swapchain;
static VkFormat       s_sc_format;
static VkExtent2D     s_sc_extent;
static uint32_t       s_sc_count;
static VkImage        s_sc_images[8];
static int            s_headless;
#if !defined(__SWITCH__)
static SDL_Window    *s_win;
#endif

static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want)
{
    uint32_t i;
    for (i = 0; i < s_memprops.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (s_memprops.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return UINT32_MAX;
}

static VkDeviceMemory alloc_mem(VkMemoryRequirements rq, VkMemoryPropertyFlags want)
{
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkDeviceMemory m = VK_NULL_HANDLE;
    ai.allocationSize = rq.size;
    ai.memoryTypeIndex = mem_type(rq.memoryTypeBits, want);
    if (ai.memoryTypeIndex == UINT32_MAX && (want & VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
        ai.memoryTypeIndex = mem_type(rq.memoryTypeBits, want & ~VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX)
        return VK_NULL_HANDLE;
    if (vkAllocateMemory(s_dev, &ai, NULL, &m) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return m;
}

/* ── Frames in flight ─────────────────────────────────────────────── */

#define VK_FRAMES 2
#define RING_BYTES (48u << 20)
#ifdef NFSU2_OPENXR
#define XR_GPU_PASS_LIMIT 256
#define XR_GPU_PASS_KINDS 5
typedef struct {
    uint32_t color, depth, width, height, layers;
    uint32_t draws, vertices, indices, blend, alpha, first_shader;
} GpuPassDetail;
typedef struct DescriptorPoolBlock DescriptorPoolBlock;
typedef struct DescriptorEntry DescriptorEntry;
#define DESCRIPTOR_BUCKETS 1024
#define DESCRIPTOR_POOL_SETS 256
static int s_dynamic_descriptors;
static void descriptor_frame_reset(void);
#endif

typedef struct {
    VkImage image; VkImageView view; VkDeviceMemory mem; VkPipeline pipe;
} Garbage;

typedef struct {
    VkCommandPool   pool;
    VkCommandBuffer cb;
    VkFence         fence;
    VkSemaphore     acquired, rendered;
    VkBuffer        ring;
    VkDeviceMemory  ring_mem;
    uint8_t        *ring_ptr;
    VkDeviceSize    ring_off;
    int             submitted;
    Garbage        *garbage;
    uint32_t        ngarbage, garbage_cap;
#ifdef NFSU2_OPENXR
    VkQueryPool     timestamps;
    uint32_t        prof_count;
    uint8_t         prof_kind[XR_GPU_PASS_LIMIT];
    int             prof_open;
    int             prof_capture, prof_tracked, prof_overflow;
    uint32_t        prof_frame, prof_command;
    GpuPassDetail   prof_detail[XR_GPU_PASS_LIMIT];
    DescriptorPoolBlock *descriptor_pools, *descriptor_current;
    DescriptorEntry *descriptor_entries;
    uint32_t descriptor_count, descriptor_capacity;
    uint32_t descriptor_heads[DESCRIPTOR_BUCKETS];
#endif
} Frame;

static Frame  s_fr[VK_FRAMES];
static Frame *s_f;             /* the frame being recorded */
static uint32_t s_fi;
static VkCommandBuffer s_cb;   /* s_f->cb while recording */
static uint32_t s_cb_gen;      /* bumped at every vkBeginCommandBuffer */
static int s_dyn_valid;
static uint32_t s_layout_sets, s_layout_reuses, s_image_writes, s_image_reuses;
static int s_state_cache = -1;

static int state_cache_enabled(void)
{
    if (s_state_cache < 0) {
        const char *e = getenv("RECOMP_VK_STATE_CACHE");
        s_state_cache = e && *e == '1'; /* opt-in while the Quest regression is isolated */
    }
    return s_state_cache;
}
#ifdef NFSU2_OPENXR
static float s_timestamp_period;
static uint32_t s_timestamp_bits;
static uint64_t s_xr_draw_ns, s_xr_snapshot_ns, s_xr_wait_ns, s_xr_gpu_ns;
static uint64_t s_compare_cpu_ns;
static uint32_t s_compare_draws;
static uint64_t s_xr_end_ns, s_xr_begin_ns;
#ifdef NFSU2_PCVR
static uint64_t s_pcvr_phase_ns[5]; /* draw, fence, XR begin/end, statistics */
#endif
static inline void xr_add_time(uint64_t *sum,uint64_t elapsed,unsigned phase)
{
    *sum+=elapsed;
#ifdef NFSU2_PCVR
    s_pcvr_phase_ns[phase]+=elapsed;
#else
    (void)phase;
#endif
}
static uint32_t s_xr_gpu_samples, s_xr_ring_flushes;
static uint32_t s_xr_passes, s_xr_snapshots, s_xr_unused_feedback, s_xr_feedback_swaps;
static uint64_t s_xr_snapshot_pixels;
static uint32_t s_xr_load_clears;
static int s_gpu_profile;
static int s_gpu_capture_control;
static uint32_t s_gpu_capture_left, s_gpu_capture_pending, s_gpu_capture_chunks;
static uint32_t s_gpu_capture_errors;
static FILE *s_gpu_capture_file;
static uint64_t s_gpu_pass_ns[XR_GPU_PASS_KINDS];
static uint32_t s_gpu_pass_count[XR_GPU_PASS_KINDS];
static int s_xr_gpu_wait;

static void gpu_timer_begin(void)
{
    if (!s_f->timestamps) return;
    s_f->prof_count=0; s_f->prof_open=0;
    s_f->prof_capture=s_gpu_capture_control && s_gpu_profile;
    s_f->prof_overflow=0;
    s_f->prof_tracked=0; s_f->prof_frame=s_frame+1; s_f->prof_command=s_cb_gen;
    if(s_f->prof_capture) ++s_gpu_capture_pending;
    vkCmdResetQueryPool(s_cb, s_f->timestamps, 0, s_gpu_profile?2+2*XR_GPU_PASS_LIMIT:2);
    vkCmdWriteTimestamp(s_cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, s_f->timestamps, 0);
}

static void gpu_timer_end(void)
{
    if (s_f->timestamps)
        vkCmdWriteTimestamp(s_cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, s_f->timestamps, 1);
}

/* Only read a slot after its fence. No query wait on the CPU's current frame. */
static void gpu_timer_collect(void)
{
    uint64_t t[2+2*XR_GPU_PASS_LIMIT];
    uint32_t count=2+2*s_f->prof_count;
    if (s_f->timestamps && vkGetQueryPoolResults(s_dev, s_f->timestamps, 0, count,
            sizeof t, t, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
        uint64_t mask = s_timestamp_bits >= 64 ? UINT64_MAX : (1ull << s_timestamp_bits) - 1;
        s_xr_gpu_ns += (uint64_t)(((t[1] - t[0]) & mask) * (double)s_timestamp_period);
        ++s_xr_gpu_samples;
        for(uint32_t i=0;i<s_f->prof_count;++i) {
            uint32_t k=s_f->prof_kind[i];
            uint64_t elapsed=(uint64_t)(((t[3+2*i]-t[2+2*i])&mask)*(double)s_timestamp_period);
            s_gpu_pass_ns[k]+=elapsed;
            ++s_gpu_pass_count[k];
            if(s_f->prof_capture && s_f->prof_tracked && s_gpu_capture_file) {
                const GpuPassDetail *p=&s_f->prof_detail[i];
                fprintf(s_gpu_capture_file,"%u,%u,%u,%u,%08x,%08x,%u,%u,%u,%u,%u,%u,%u,%u,%08x,%.6f\n",
                    s_f->prof_frame,s_f->prof_command,i,k,p->color,p->depth,p->width,p->height,p->layers,
                    p->draws,p->vertices,p->indices,p->blend,p->alpha,p->first_shader,elapsed/1e6);
            }
        }
        if(s_f->prof_capture && s_f->prof_tracked) ++s_gpu_capture_chunks;
    } else if(s_f->prof_capture) {
        ++s_gpu_capture_errors;
        LOGE("XR GPU capture: completed slot query read failed\n");
    }
    if(s_f->prof_capture) {
        if(s_f->prof_overflow) {
            ++s_gpu_capture_errors;
            LOGE("XR GPU capture: frame %u exceeds %u timed passes\n",s_f->prof_frame,XR_GPU_PASS_LIMIT);
        }
        s_f->prof_capture=0;
        if(s_gpu_capture_pending) --s_gpu_capture_pending;
    }
    if(s_gpu_capture_file && !s_gpu_capture_left && !s_gpu_capture_pending) {
        int failed=ferror(s_gpu_capture_file) || s_gpu_capture_errors;
        failed |= fclose(s_gpu_capture_file)!=0; s_gpu_capture_file=NULL;
        LOGE("XR GPU capture %s: %u tracked chunks, %u capture errors, gpu-passes.csv; per-pass timing off\n",
            failed?"incomplete":"complete",s_gpu_capture_chunks,s_gpu_capture_errors);
    }
}

/* Opt-in mode 2 waits for a file trigger and stops after 60 tracked race
 * frames. Keep diagnostic timing and file IO out of ordinary FPS comparisons.
 * The running session need not restart to disable this heavier instrumentation. */
static void gpu_capture_tick(int race)
{
    if(!s_gpu_capture_control) return;
    int tracked=race && quest_xr_tracking();
    if(s_gpu_capture_left) {
        if(s_f->prof_capture && tracked) --s_gpu_capture_left;
        s_gpu_profile=s_gpu_capture_left && tracked;
        return;
    }
    if(s_gpu_capture_file || !tracked || s_frame%60 || !s_f->timestamps) return;
    FILE *trigger=fopen("gpu-passes.start","rb");
    if(!trigger) return;
    fclose(trigger); remove("gpu-passes.start");
    s_gpu_capture_file=fopen("gpu-passes.csv","wb");
    if(!s_gpu_capture_file) { LOGE("XR GPU capture: cannot create output\n"); return; }
    fprintf(s_gpu_capture_file,"frame,command,pass,kind,color_va,depth_va,width,height,layers,draws,vertices,indices,blend_draws,alpha_draws,first_shader,gpu_ms\n");
    s_gpu_capture_left=60; s_gpu_capture_chunks=s_gpu_capture_errors=0; s_gpu_profile=1;
    LOGE("XR GPU capture started: 60 tracked race frames, automatic stop\n");
}

/* Optional, bounded diagnosis at existing pass boundaries. No extra render
 * passes or CPU query waits. Keep it off during ordinary FPS comparisons. */
static void gpu_pass_begin(uint32_t kind,uint32_t color,uint32_t depth,
                           uint32_t width,uint32_t height,uint32_t layers)
{
    if(!s_gpu_profile || !s_f->timestamps) return;
    if(s_f->prof_count==XR_GPU_PASS_LIMIT) { s_f->prof_overflow=1; return; }
    s_f->prof_kind[s_f->prof_count]=(uint8_t)kind;
    s_f->prof_open=1;
    if(s_f->prof_capture) s_f->prof_detail[s_f->prof_count]=(GpuPassDetail){
        .color=color,.depth=depth,.width=width,.height=height,.layers=layers};
    vkCmdWriteTimestamp(s_cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,s_f->timestamps,2+2*s_f->prof_count);
}

static void gpu_pass_end(void)
{
    if(!s_f->prof_open) return;
    vkCmdWriteTimestamp(s_cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,s_f->timestamps,3+2*s_f->prof_count);
    ++s_f->prof_count; s_f->prof_open=0;
}

static int frame_time_compare(const void *a, const void *b)
{
    float x=*(const float *)a, y=*(const float *)b;
    return (x>y)-(x<y);
}

static void xr_frame_times(uint64_t now, int race)
{
    static uint64_t previous;
    static float samples[120];
    static unsigned count;
    static int previous_race;
    int active = race && quest_xr_tracking();
#ifdef NFSU2_PCVR
    extern void pcvr_benchmark_frame(uint64_t now,int tracked,const uint64_t phases[5],
                                     const uint64_t work[4],uint32_t draws);
    const uint64_t work[4]={s_hz_prog_ns,s_hz_tex_ns,s_pcvr_pipeline_ns,s_pcvr_cache_ns};
    pcvr_benchmark_frame(now,active,s_pcvr_phase_ns,work,s_hz_draws);
#endif
    if (active && previous_race && previous) {
        samples[count++] = (float)((now-previous)/1e6);
        if (count == 120) {
            double sum=0;
            for (unsigned i=0;i<count;++i) sum+=samples[i];
            qsort(samples,count,sizeof *samples,frame_time_compare);
            LOGE("XR tracked race: %.1f fps, frame p50 %.2f ms, p95 %.2f ms, max %.2f ms (120 frames)\n",
                 120000.0/sum,samples[59],samples[113],samples[119]);
            count=0;
        }
    } else count=0;
    previous=now; previous_race=active;
}
#endif

static void garbage_add_pipe(VkPipeline pipe);
static void garbage_add(VkImage img, VkImageView view, VkDeviceMemory mem)
{
    Frame *f = s_f;
    if (f->ngarbage == f->garbage_cap) {
        f->garbage_cap = f->garbage_cap ? f->garbage_cap * 2 : 64;
        f->garbage = (Garbage *)realloc(f->garbage, f->garbage_cap * sizeof *f->garbage);
    }
    f->garbage[f->ngarbage].image = img;
    f->garbage[f->ngarbage].view = view;
    f->garbage[f->ngarbage].mem = mem;
    f->garbage[f->ngarbage].pipe = VK_NULL_HANDLE;
    f->ngarbage++;
}

static void garbage_add_pipe(VkPipeline pipe)
{
    garbage_add(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE);
    s_f->garbage[s_f->ngarbage - 1].pipe = pipe;
}

static void garbage_free(Frame *f)
{
    uint32_t i;
    for (i = 0; i < f->ngarbage; i++) {
        if (f->garbage[i].view) vkDestroyImageView(s_dev, f->garbage[i].view, NULL);
        if (f->garbage[i].image) vkDestroyImage(s_dev, f->garbage[i].image, NULL);
        if (f->garbage[i].mem) vkFreeMemory(s_dev, f->garbage[i].mem, NULL);
        if (f->garbage[i].pipe) vkDestroyPipeline(s_dev, f->garbage[i].pipe, NULL);
    }
    f->ngarbage = 0;
}

static void frame_begin(void)
{
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    s_f = &s_fr[s_fi];
    if (s_f->submitted) {
#ifdef NFSU2_OPENXR
        uint64_t t0 = hz_now();
#endif
        vkWaitForFences(s_dev, 1, &s_f->fence, VK_TRUE, UINT64_MAX);
#ifdef NFSU2_OPENXR
        xr_add_time(&s_xr_wait_ns,hz_now()-t0,1);
        gpu_timer_collect();
#endif
        vkResetFences(s_dev, 1, &s_f->fence);
        s_f->submitted = 0;
    }
    garbage_free(s_f);
#ifdef NFSU2_OPENXR
    descriptor_frame_reset(); /* frame slot is no longer in flight */
#endif
    s_f->ring_off = 0;
    vkResetCommandPool(s_dev, s_f->pool, 0);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(s_f->cb, &bi);
    s_cb = s_f->cb;
    s_cb_gen++;
    s_dyn_valid = 0;
#ifdef NFSU2_OPENXR
    gpu_timer_begin();
#endif
}

/* Space in this frame's ring: returns the offset, *ptr the mapping. */
static void flush_frame_wait(void);
static VkDeviceSize ring_alloc(VkDeviceSize size, VkDeviceSize align, void **ptr)
{
    VkDeviceSize at = (s_f->ring_off + align - 1) & ~(align - 1);
    if (at + size > RING_BYTES) {
        /* A frame larger than the ring: finish what is recorded, wait for
         * it, and start over in the same ring. Slow, but only in extremes. */
        static int warned;
        if (!warned++)
            LOGE("frame ring (%u MB) full: flushing mid-frame\n", RING_BYTES >> 20);
        flush_frame_wait();
        at = 0;
        if (size > RING_BYTES) {
            *ptr = NULL;
            return 0;
        }
    }
    s_f->ring_off = at + size;
    *ptr = s_f->ring_ptr + at;
    return at;
}

/* ── Rendering passes and barriers ────────────────────────────────── */

/* w, h are the surface in the title's pixels (anti-aliasing included) --
 * what the shaders, clip rectangles and texture lookups work in. pw, ph are
 * the image: w, h times the render scale. */
typedef struct {
    uint32_t va, w, h, pw, ph, layers;
    int eye;
    VkImage image; VkImageView view; VkDeviceMemory mem;
    uint32_t used;
    uint32_t aa_sx, aa_sy;
    int dirty;                 /* written since the last barrier */
    uint32_t gen;              /* bumped by every draw and clear into it */
} VkSurf;
#ifdef NFSU2_OPENXR
static VkSurf s_feedback_spare;
static int s_feedback_check_active, s_feedback_failed;
static unsigned s_feedback_verified;
#endif

typedef struct {
    uint32_t va, pw, ph, layers;
    int eye;
    VkImage image; VkImageView view; VkDeviceMemory mem;
    VkImageLayout layout;
    uint32_t used;
} VkDepthBuf;

#define VK_MAX_SURF 64
#define VK_MAX_DEPTH 32

/* RECOMP_GL_SCALE, as in nv2a_gl: only the pixels behind a surface grow, so
 * the viewport, clear rectangles, read-backs and the present blit scale and
 * nothing else does. Capped per surface by the device's image limits. */
static double   s_scale = 1.0;
static uint32_t s_max_size = 4096;

/* v title pixels of a surface l wide, in the p stored pixels behind it. */
static uint32_t to_stored(uint32_t v, uint32_t p, uint32_t l)
{
    return (uint32_t)(((uint64_t)v * p + l / 2) / l);
}
static VkSurf     s_surf[VK_MAX_SURF];
#ifdef NFSU2_PCVR
static PcvrSurfaceLookup s_surface_lookup_cache;
static int s_surface_lookup=-1,s_surface_lookup_failed;
static unsigned s_surface_lookup_checked;
static uint64_t s_surface_lookup_hits,s_surface_lookup_misses;
#endif
static VkDepthBuf s_depth[VK_MAX_DEPTH];
static VkSurf    *s_last;
static int s_eye;
#ifdef NFSU2_OPENXR
static VkSurf *s_last_eye[2];
static int s_stereo_frame;
static float s_game_projection[2];
static float s_game_depth[4];
static uint32_t s_world_color_va;
static uint32_t s_xr_kind[5];
static int xr_stereo(void)
{
    static int option=-1;
    if(option<0) {
        const char *e=getenv("NFSU2_VR_STEREO");
        option=!(e && *e=='0');
    }
    return quest_xr_enabled() && option;
}
#endif

static int         s_in_render;
static VkSurf     *s_rt;
static VkDepthBuf *s_rt_depth;

static void barrier_all(void)
{
    VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    int i;
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    VkPipelineStageFlags stages=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
#ifdef NFSU2_OPENXR
    if(s_image_barriers<0) {
        const char *e=getenv("NFSU2_VR_IMAGE_BARRIERS");
        s_image_barriers=xr_stereo() && e && *e=='1';
        LOGE("XR image-stage barriers %s\n",s_image_barriers?"on":"off");
    }
    if(s_image_barriers) stages=quest_image_stages(s_foveation);
#endif
    vkCmdPipelineBarrier(s_cb, stages, stages, 0, 1, &mb, 0, NULL, 0, NULL);
    for (i = 0; i < VK_MAX_SURF; i++)
        s_surf[i].dirty = 0;
}

static void end_rendering(void)
{
    if (s_in_render) {
        p_end_rendering(s_cb);
#ifdef NFSU2_OPENXR
        gpu_pass_end();
#endif
        s_in_render = 0;
    }
}

static VkRect2D s_sc_cur;               /* scissor set in this pass */
#ifdef NFSU2_OPENXR
static VkImageView xr_rate_attachment(const VkSurf *s);
#endif

static void set_viewport(uint32_t pw, uint32_t ph)
{
    VkViewport vp = { 0, 0, (float)pw, (float)ph, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, { pw, ph } };
    vkCmdSetViewport(s_cb, 0, 1, &vp);
    vkCmdSetScissor(s_cb, 0, 1, &sc);
    s_sc_cur = sc;
}

/* SET_SURFACE_CLIP as the scissor, in s's stored pixels. NFSU2's split
 * screen draws each player's world with the clip set to that player's
 * half; the vertex programs place it there but do not stop at the edge.
 * Same as nv2a_gl.c (and xemu). Call inside the pass (begin_rendering
 * resets it to the whole surface). */
static void set_surface_scissor(const uint32_t *r, const VkSurf *s)
{
    uint32_t x0 = (r[0x200 / 4] & 0xFFFF) * s->aa_sx, x1 = x0 + (r[0x200 / 4] >> 16) * s->aa_sx;
    uint32_t y0 = (r[0x204 / 4] & 0xFFFF) * s->aa_sy, y1 = y0 + (r[0x204 / 4] >> 16) * s->aa_sy;
    VkRect2D sc = { { 0, 0 }, { s->pw, s->ph } };

    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x1 > x0 && y1 > y0) {
        sc.offset.x = (int32_t)to_stored(x0, s->pw, s->w);
        sc.offset.y = (int32_t)to_stored(y0, s->ph, s->h);
        sc.extent.width = to_stored(x1, s->pw, s->w) - (uint32_t)sc.offset.x;
        sc.extent.height = to_stored(y1, s->ph, s->h) - (uint32_t)sc.offset.y;
    }
    if (memcmp(&sc, &s_sc_cur, sizeof sc)) {
        vkCmdSetScissor(s_cb, 0, 1, &sc);
        s_sc_cur = sc;
    }
}

static void begin_rendering_clear(VkSurf *s, VkDepthBuf *d,
                                  const VkClearAttachment *clear, uint32_t nclear)
{
    VkRenderingAttachmentInfo ca = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    VkRenderingAttachmentInfo da = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    VkRenderingAttachmentInfo sa;
    VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO };

    if (s_in_render && s_rt == s && s_rt_depth == d)
        return;
    end_rendering();
#ifdef NFSU2_OPENXR
    VkRenderingFragmentShadingRateAttachmentInfoKHR rate =
        { VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR };
    rate.imageView = xr_rate_attachment(s);
    if (rate.imageView) {
        rate.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        rate.shadingRateAttachmentTexelSize = s_rate_texel;
        ri.pNext = &rate;
    }
#endif
    barrier_all();
    ca.imageView = s->view;
#ifdef NFSU2_OPENXR
    if(s_depth_optimal<0) {
        const char *e=getenv("NFSU2_VR_DEPTH_LAYOUT");
        s_depth_optimal=e && *e=='1';
        LOGE("XR depth attachment layout: %s\n",s_depth_optimal?"optimal":"general");
    }
    if(d) quest_depth_layout_transition(s_cb,d->image,&d->layout,d->layers,
        VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT,s_depth_optimal);
#endif
    ca.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    ca.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ri.renderArea.extent.width = s->pw;
    ri.renderArea.extent.height = s->ph;
    if (d) {
        if (d->pw < ri.renderArea.extent.width) ri.renderArea.extent.width = d->pw;
        if (d->ph < ri.renderArea.extent.height) ri.renderArea.extent.height = d->ph;
    }
    ri.layerCount = 1;
    ri.viewMask = s->layers == 2 ? 3 : 0;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    if (d) {
        da.imageView = d->view;
        da.imageLayout = d->layout;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        sa = da;
        ri.pDepthAttachment = &da;
        ri.pStencilAttachment = &sa;
    }
    /* Full clears at the start of a pass need no old attachment data.
     * Treat depth and stencil separately: a depth-only clear must keep stencil. */
    for(uint32_t i=0;i<nclear;++i) {
        if(clear[i].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
            ca.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; ca.clearValue=clear[i].clearValue;
        }
        if(d && (clear[i].aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)) {
            da.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; da.clearValue=clear[i].clearValue;
        }
        if(d && (clear[i].aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)) {
            sa.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; sa.clearValue=clear[i].clearValue;
        }
    }
#ifdef NFSU2_OPENXR
    gpu_pass_begin(s->w==640 && s->h==480?0:s->w==320 && s->h==240?1:
                   s->w==1280 && s->h==480?2:s->w<=256 && s->h<=256?3:4,
                   s->va,d?d->va:0,s->pw,s->ph,s->layers);
#endif
    p_begin_rendering(s_cb, &ri);
    ++s_render_generation;
    if (s_multiview) {
        /* Rebind pass-local state, including when switching to mono probes. */
        s_dyn_valid = 0;
        s_bound_pipe = VK_NULL_HANDLE;
    }
#ifdef NFSU2_OPENXR
    ++s_xr_passes;
#endif
    s_in_render = 1;
    s_rt = s;
    s_rt_depth = d;
    set_viewport(s->pw, s->ph);
}

static void begin_rendering(VkSurf *s, VkDepthBuf *d)
{
    begin_rendering_clear(s,d,NULL,0);
}

/* An image in GENERAL layout, from UNDEFINED; recorded outside rendering. */
static void image_to_general(VkImage img, VkImageAspectFlags aspect)
{
    VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    end_rendering();
    b.srcAccessMask = 0;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    vkCmdPipelineBarrier(s_cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}

/* Multiview uses array views even for one-layer sampled textures, so the
 * same shader can sample a static texture or the matching eye of a target. */
static int make_image_n(uint32_t w, uint32_t h, uint32_t layers, VkFormat fmt,
                        VkImageUsageFlags usage, VkImageAspectFlags aspect,
                        VkImage *img, VkImageView *view, VkDeviceMemory *mem)
{
    VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    VkMemoryRequirements rq;

    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = fmt;
    ci.extent.width = w;
    ci.extent.height = h;
    ci.extent.depth = 1;
    ci.mipLevels = 1;
    ci.arrayLayers = layers;
    if (layers == 6)
        ci.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(s_dev, &ci, NULL, img) != VK_SUCCESS)
        return 0;
    vkGetImageMemoryRequirements(s_dev, *img, &rq);
    *mem = alloc_mem(rq, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!*mem || vkBindImageMemory(s_dev, *img, *mem, 0) != VK_SUCCESS) {
        vkDestroyImage(s_dev, *img, NULL);
        if (*mem) vkFreeMemory(s_dev, *mem, NULL);
        *img = VK_NULL_HANDLE; *mem = VK_NULL_HANDLE;
        return 0;
    }
    vi.image = *img;
    vi.viewType = layers == 6 ? VK_IMAGE_VIEW_TYPE_CUBE :
        s_multiview ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fmt;
    vi.subresourceRange.aspectMask = aspect & ~VK_IMAGE_ASPECT_STENCIL_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = layers;
    if (aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    if (vkCreateImageView(s_dev, &vi, NULL, view) != VK_SUCCESS) {
        vkDestroyImage(s_dev, *img, NULL);
        vkFreeMemory(s_dev, *mem, NULL);
        *img = VK_NULL_HANDLE; *mem = VK_NULL_HANDLE;
        return 0;
    }
    image_to_general(*img, aspect);
    return 1;
}

static int make_image(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                      VkImageAspectFlags aspect, VkImage *img, VkImageView *view,
                      VkDeviceMemory *mem)
{
    return make_image_n(w, h, 1, fmt, usage, aspect, img, view, mem);
}

#ifdef NFSU2_OPENXR
/* One immutable map is shared by both eyes and all equally sized race targets.
 * The center keeps 1x1 shading; only the periphery uses 2x2. HUD and full-screen
 * composites ignore the attachment through their per-draw combiner state. */
static VkImageView xr_rate_attachment(const VkSurf *s)
{
    static VkImage image;
    static VkImageView view;
    static VkDeviceMemory memory;
    static uint32_t width, height;
    if (!s_foveation || s->w != 640 || s->h != 480 || s->aa_sx != 1 || s->aa_sy != 1)
        return VK_NULL_HANDLE;
    uint32_t w = (s->pw + s_rate_texel.width - 1) / s_rate_texel.width;
    uint32_t h = (s->ph + s_rate_texel.height - 1) / s_rate_texel.height;
    if (image && width == w && height == h) return view;
    if (image) garbage_add(image, view, memory);
    image = VK_NULL_HANDLE; view = VK_NULL_HANDLE; memory = VK_NULL_HANDLE;
    if (!make_image(w, h, VK_FORMAT_R8_UINT,
        VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, &image, &view, &memory)) return VK_NULL_HANDLE;
    void *map;
    VkDeviceSize at = ring_alloc((VkDeviceSize)w*h, 4, &map);
    if (!map) {
        garbage_add(image, view, memory);
        image = VK_NULL_HANDLE; view = VK_NULL_HANDLE; memory = VK_NULL_HANDLE;
        return VK_NULL_HANDLE;
    }
    for (uint32_t y=0; y<h; ++y) for (uint32_t x=0; x<w; ++x) {
        float dx = ((x+0.5f)*2.0f/w-1.0f)/0.65f;
        float dy = ((y+0.5f)*2.0f/h-1.0f)/0.65f;
        ((uint8_t *)map)[(size_t)y*w+x] = dx*dx+dy*dy <= 1.0f ? 0 : 5;
    }
    VkBufferImageCopy copy = {0};
    copy.bufferOffset = at;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = (VkExtent3D){w,h,1};
    vkCmdCopyBufferToImage(s_cb, s_f->ring, image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
    width=w; height=h;
    LOGE("XR foveation map %ux%u for %ux%u; central 65%% at 1x1, outer 2x2\n",w,h,s->pw,s->ph);
    return view; /* begin_rendering barriers the upload before its first use */
}

static void xr_set_shading_rate(int world)
{
    static uint32_t generation, render_generation;
    static int previous=-1;
    if (!s_foveation) return;
    if (generation == s_cb_gen && previous == world &&
        (!s_multiview || render_generation == s_render_generation)) return;
    VkExtent2D size = {1,1};
    VkFragmentShadingRateCombinerOpKHR ops[2] = {
        VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
        world ? VK_FRAGMENT_SHADING_RATE_COMBINER_OP_REPLACE_KHR : VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR
    };
    p_shading_rate(s_cb, &size, ops);
    generation=s_cb_gen; previous=world;
    render_generation=s_render_generation;
}
#endif

/* ── Surfaces ─────────────────────────────────────────────────────── */

static VkSurf *surf_find_reference(uint32_t va)
{
    int i;
    for (i = 0; i < VK_MAX_SURF; i++)
        if (s_surf[i].image && s_surf[i].va == va && (s_surf[i].eye == s_eye || s_surf[i].eye == -1))
            return &s_surf[i];
    return NULL;
}

static VkSurf *surf_find(uint32_t va)
{
#ifdef NFSU2_PCVR
    if(s_surface_lookup<0) {
        const char *e=getenv("NFSU2_PCVR_SURFACE_LOOKUP");
        s_surface_lookup=e && *e=='1';
        LOGE("PCVR surface lookup cache %s\n",s_surface_lookup?"on":"off (reference)");
    }
    if(s_surface_lookup && !s_surface_lookup_failed) {
        int slot;
        if(pcvr_surface_lookup_get(&s_surface_lookup_cache,va,s_eye,&slot)) ++s_surface_lookup_hits;
        else {
            VkSurf *found=surf_find_reference(va);
            slot=found?(int)(found-s_surf):-1;
            pcvr_surface_lookup_put(&s_surface_lookup_cache,va,s_eye,slot);
            ++s_surface_lookup_misses;
        }
        VkSurf *found=slot>=0?&s_surf[slot]:NULL;
        if(s_surface_lookup_checked<100000) {
            VkSurf *reference=surf_find_reference(va);
            if(found!=reference) {
                s_surface_lookup_failed=1; s_surface_lookup=0;
                LOGE("PCVR surface lookup mismatch; original search restored\n");
                return reference;
            }
            if(++s_surface_lookup_checked==100000)
                LOGE("PCVR surface lookup verified on 100000 queries (%llu hits, %llu misses)\n",
                     (unsigned long long)s_surface_lookup_hits,(unsigned long long)s_surface_lookup_misses);
        }
        return found;
    }
#endif
    return surf_find_reference(va);
}

static void clear_image_color(VkImage img, float r, float g, float b, float a)
{
    VkClearColorValue c = { { r, g, b, a } };
    VkImageSubresourceRange rg = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                   VK_REMAINING_ARRAY_LAYERS };
    end_rendering();
    vkCmdClearColorImage(s_cb, img, VK_IMAGE_LAYOUT_GENERAL, &c, 1, &rg);
}

static VkSurf *surf_get(uint32_t va, uint32_t w, uint32_t h, uint32_t aa_sx, uint32_t aa_sy)
{
    VkSurf *s = surf_find(va), *victim = NULL;
    int i;

    /* h is the surface clip, not the allocation (NFSU2's split screen clips
     * the back buffer to each player's half): keep the surface while the
     * clip fits, grow it when it does not. Same as nv2a_gl.c. */
    if (s && s->w == w && s->h >= h) {
        s->used = s_frame;
        return s;
    }
    if (s && s->w == w && s->h > h)
        h = s->h;
    if (!s) {
        for (i = 0; i < VK_MAX_SURF; i++) {
            if (!s_surf[i].image) { victim = &s_surf[i]; break; }
            if (!victim || s_surf[i].used < victim->used) victim = &s_surf[i];
        }
        s = victim;
    }
#ifdef NFSU2_PCVR
    /* This is the sole path that changes a color slot's lookup identity or
     * image validity. Invalidate misses too, before creation or replacement,
     * including failure and while the comparison runs in reference mode. */
    pcvr_surface_lookup_reset(&s_surface_lookup_cache);
#endif
    if (s->image) {
        if (s_rt == s) end_rendering(), s_rt = NULL;
        if (s_last == s) s_last = NULL;
#ifdef NFSU2_OPENXR
        for (int eye=0;eye<2;++eye) if(s_last_eye[eye]==s) s_last_eye[eye]=NULL;
#endif
        garbage_add(s->image, s->view, s->mem);
    }
    memset(s, 0, sizeof *s);
    s->va = va; s->w = w; s->h = h; s->used = s_frame;
    s->eye = s_eye;
#ifdef NFSU2_OPENXR
    /* Reflection probes and the rear-view mirror have independent cameras. */
    if (xr_stereo() && ((w==h && w<=256) || (w==256 && h==64))) s->eye=-1;
#endif
    s->aa_sx = aa_sx; s->aa_sy = aa_sy;
    s->layers = s_multiview && s->eye >= 0 ? 2 : 1;
    {
        double k = s_scale, m = (double)(w > h ? w : h);
        if (m * k > (double)s_max_size)
            k = (double)s_max_size / m;
        s->pw = (uint32_t)(w * k + 0.5); s->ph = (uint32_t)(h * k + 0.5);
        if (!s->pw) s->pw = 1;
        if (!s->ph) s->ph = 1;
    }
#ifdef NFSU2_OPENXR
    /* The front end's 2x horizontal AA surface represents a 640x480 menu.
     * Increase its actual raster resolution, retaining all guest coordinates
     * and AA factors. Match the compositor quad to avoid a second resample. */
    if (quest_xr_enabled() && aa_sx==2 && aa_sy==1 && w==1280 && h==480) {
        uint32_t mw,mh;
        quest_xr_menu_extent(&mw,&mh);
        if (mw && mh) {
            double k=(double)s_max_size/(mw>mh?mw:mh);
            if(k>1) k=1;
            s->pw=(uint32_t)(mw*k); s->ph=(uint32_t)(mh*k);
            if(!s_eye) LOGE("XR menu %ux%u (AA %ux%u) stored at %ux%u\n",w,h,aa_sx,aa_sy,s->pw,s->ph);
        }
    }
    if(xr_stereo()) {
        static double eye_scale=-1;
        if(eye_scale<0) {
            const char *e=getenv("NFSU2_VR_RESOLUTION");
            eye_scale=e?strtod(e,NULL):0;
            if(!isfinite(eye_scale) || eye_scale<0.25 || eye_scale>1.5) eye_scale=0;
        }
        if(eye_scale>0 && aa_sx==1 && aa_sy==1 &&
           ((w==640 && h==480) || (w==320 && h==240))) {
            uint32_t ew,eh;
            quest_xr_eye_extent(&ew,&eh);
            if(ew && eh) {
                double k=eye_scale*(double)w/640.0;
                double largest=ew>eh?ew:eh;
                if(largest*k>s_max_size) k=s_max_size/largest;
                s->pw=(uint32_t)(ew*k+0.5); s->ph=(uint32_t)(eh*k+0.5);
                if(!s_eye) LOGE("XR target %ux%u stored at %ux%u (eye scale %.2f)\n",w,h,s->pw,s->ph,eye_scale);
            }
        }
    }
#endif
    if (!make_image_n(s->pw, s->ph, s->layers, VK_FORMAT_B8G8R8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, &s->image, &s->view, &s->mem)) {
        LOGE("surface 0x%08X %ux%u: image creation failed\n", va, w, h);
        memset(s, 0, sizeof *s);
        return NULL;
    }
    clear_image_color(s->image, 0, 0, 0, 1);
    if (s_trace)
        LOGE("surface 0x%08X %ux%u (aa %ux%u, stored %ux%u)\n", va, w, h, aa_sx, aa_sy,
             s->pw, s->ph);
    return s;
}

/* Color grading and the final copy sample the current render target. GENERAL
 * layout alone does not make a sampled-image attachment feedback loop valid.
 * Read a snapshot from before the draw, including for overlapping filtered UVs. */
static VkImageView surface_snapshot(VkSurf *s)
{
    static VkImage image;
    static VkImageView view;
    static VkDeviceMemory memory;
    static uint32_t width,height,layers;
#ifdef NFSU2_OPENXR
    ++s_xr_snapshots;
    s_xr_snapshot_pixels += (uint64_t)s->pw*s->ph*s->layers;
#endif
    end_rendering();
    if(!image || width!=s->pw || height!=s->ph || layers!=s->layers) {
        if(image) garbage_add(image,view,memory);
        image=VK_NULL_HANDLE; view=VK_NULL_HANDLE; memory=VK_NULL_HANDLE;
        if(!make_image_n(s->pw,s->ph,s->layers,VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,&image,&view,&memory)) return VK_NULL_HANDLE;
        width=s->pw; height=s->ph; layers=s->layers;
    }
    barrier_all();
    VkImageCopy copy={0};
    copy.srcSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount=layers;
    copy.dstSubresource=copy.srcSubresource;
    copy.extent.width=width; copy.extent.height=height; copy.extent.depth=1;
    vkCmdCopyImage(s_cb,s->image,VK_IMAGE_LAYOUT_GENERAL,image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
    barrier_all();
    return view;
}

#ifdef NFSU2_OPENXR
/* The original target becomes the sampled input; a spare becomes the target.
 * Keep every border texel, so coverage near sample-center ties is immaterial.
 * All image handles retain their lifetimes and the existing global barriers. */
static VkImageView surface_feedback_swap(VkSurf *s)
{
    VkSurf *spare=&s_feedback_spare;
    end_rendering();
    if(!spare->image || spare->pw!=s->pw || spare->ph!=s->ph || spare->layers!=s->layers) {
        if(spare->image) garbage_add(spare->image,spare->view,spare->mem);
        memset(spare,0,sizeof *spare);
        if(!make_image_n(s->pw,s->ph,s->layers,VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,&spare->image,&spare->view,&spare->mem)) return VK_NULL_HANDLE;
        spare->pw=s->pw; spare->ph=s->ph; spare->layers=s->layers;
    }
    barrier_all();
    VkImageCopy copy[4]={0};
    for(unsigned i=0;i<4;++i) {
        copy[i].srcSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,s->layers};
        copy[i].dstSubresource=copy[i].srcSubresource;
        copy[i].extent=(VkExtent3D){s->pw,1,1};
    }
    copy[1].srcOffset.y=(int32_t)s->ph-1;
    copy[2].srcOffset.y=copy[3].srcOffset.y=1;
    copy[2].extent=copy[3].extent=(VkExtent3D){1,s->ph-2,1};
    copy[3].srcOffset.x=(int32_t)s->pw-1;
    for(unsigned i=0;i<4;++i) copy[i].dstOffset=copy[i].srcOffset;
    vkCmdCopyImage(s_cb,s->image,VK_IMAGE_LAYOUT_GENERAL,spare->image,VK_IMAGE_LAYOUT_GENERAL,4,copy);
    barrier_all();
    VkImage old_image=s->image; s->image=spare->image; spare->image=old_image;
    VkImageView old_view=s->view; s->view=spare->view; spare->view=old_view;
    VkDeviceMemory old_mem=s->mem; s->mem=spare->mem; spare->mem=old_mem;
    ++s_xr_feedback_swaps;
    return old_view;
}
#endif

static VkDepthBuf *depth_get(uint32_t va, uint32_t pw, uint32_t ph, int eye, uint32_t layers)
{
    VkDepthBuf *d, *victim = NULL;
    int i;

    for (i = 0; i < VK_MAX_DEPTH; i++) {
        if (s_depth[i].image && s_depth[i].va == va && s_depth[i].eye == eye &&
            s_depth[i].pw == pw && s_depth[i].ph == ph && s_depth[i].layers == layers) {
            s_depth[i].used = s_frame;
            return &s_depth[i];
        }
        if (!victim || (victim->image && (!s_depth[i].image || s_depth[i].used < victim->used)))
            victim = &s_depth[i];
    }
    d = victim;
    if (d->image) {
        if (s_rt_depth == d) end_rendering(), s_rt = NULL, s_rt_depth = NULL;
        garbage_add(d->image, d->view, d->mem);
    }
    memset(d, 0, sizeof *d);
    d->va = va; d->pw = pw; d->ph = ph; d->used = s_frame;
    d->eye = eye;
    d->layers = layers;
    if (!make_image_n(pw, ph, layers, s_depth_fmt, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                    &d->image, &d->view, &d->mem)) {
        LOGE("depth 0x%08X %ux%u: image creation failed\n", va, pw, ph);
        memset(d, 0, sizeof *d);
        return NULL;
    }
    {
        VkClearDepthStencilValue z = { 1.0f, 0 };
        VkImageSubresourceRange rg = { VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, layers };
        vkCmdClearDepthStencilImage(s_cb, d->image, VK_IMAGE_LAYOUT_GENERAL, &z, 1, &rg);
    }
    d->layout=VK_IMAGE_LAYOUT_GENERAL;
    if (s_trace)
        LOGE("depth 0x%08X %ux%u\n", va, pw, ph);
    return d;
}

/* The target of a draw or clear: surface (created on first use) and the
 * depth buffer of its zeta address. */
static VkSurf *target(const Nv2aSurface *sf, uint32_t zeta_va, VkDepthBuf **dout)
{
    uint32_t bpp = sf->bytes_per_pixel ? sf->bytes_per_pixel : 4;
    uint32_t w = sf->pitch ? sf->pitch / bpp : sf->width;
    uint32_t h = sf->clip_y + sf->height;   /* the clip need not start at 0 */
    VkSurf *s;

    *dout = NULL;
    if (w < sf->clip_x + sf->width) w = sf->clip_x + sf->width;
    if (!w || !h || !sf->color_va || w > 4096 || h > 4096)
        return NULL;
    s = surf_get(sf->color_va, w, h, sf->aa_sx ? sf->aa_sx : 1, sf->aa_sy ? sf->aa_sy : 1);
    if (s && zeta_va)
        *dout = depth_get(zeta_va, s->pw, s->ph, s->eye, s->layers);
    if (s) {
        s_last = s;
#ifdef NFSU2_OPENXR
        s_last_eye[s_eye] = s;
        if (s->layers == 2) s_last_eye[1] = s;
#endif
    }
    return s;
}

/* ── Textures ─────────────────────────────────────────────────────── */

typedef struct {
    uint32_t va, color, w, h, pitch, hash, bytes;
    uint32_t face;              /* cube map: bytes per face; 0 = 2D */
    VkImage image; VkImageView view; VkDeviceMemory mem;
    VkFormat fmt;
    uint32_t used, checked;
} VkTex;

#ifdef NFSU2_PCVR
#define VK_MAX_TEX 2048
static uint32_t s_tex_limit=1024;
#else
#define VK_MAX_TEX 1024
#define s_tex_limit VK_MAX_TEX
#endif
#define TEX_BUCKETS 2048
static VkTex    s_tex[VK_MAX_TEX];
static uint16_t s_tex_head[TEX_BUCKETS], s_tex_next[VK_MAX_TEX];
static uint64_t s_tex_bytes;
static uint32_t *s_decode;
static size_t    s_decode_cap;
static uint32_t s_palette_reg, s_palette_va;
static VkImage  s_dummy_img;
static VkImageView s_dummy_view;
static VkDeviceMemory s_dummy_mem;
/* A transparent black cube, for a cube map stage with nothing to sample. */
static VkImage  s_dummy_cube_img;
static VkImageView s_dummy_cube_view;
static VkDeviceMemory s_dummy_cube_mem;
#ifdef NFSU2_PCVR
/* Opt-in cache-miss diagnostics only; never time ordinary cache lookups. */
static int s_texture_profile = -1;
static struct {
    uint64_t create_ns, fill_ns, account_ns, bc_ns, swizzle_ns, other_ns, max_ns;
    uint32_t created, updated, evicted, color, w, h, layers;
} s_texture_work;
#endif

uint64_t nv2a_gl_texture_bytes(void) { return s_tex_bytes; }
uint32_t nv2a_gl_frame_count(void) { return s_frame; }

static uint32_t tex_bucket(uint32_t va, uint32_t color, uint32_t w, uint32_t h)
{
    uint32_t k = va ^ (color << 24) ^ (w << 12) ^ (h << 4);
    k ^= k >> 15; k *= 0x2C1B3C6Du; k ^= k >> 12;
    return k & (TEX_BUCKETS - 1);
}
static void tex_index_add(VkTex *t)
{
    uint32_t b = tex_bucket(t->va, t->color, t->w, t->h);
    uint16_t i = (uint16_t)(t - s_tex);
    s_tex_next[i] = s_tex_head[b];
    s_tex_head[b] = (uint16_t)(i + 1);
}
static void tex_index_del(VkTex *t)
{
    uint16_t *at = &s_tex_head[tex_bucket(t->va, t->color, t->w, t->h)];
    uint16_t i = (uint16_t)(t - s_tex);
    while (*at) {
        if (*at == i + 1) { *at = s_tex_next[i]; return; }
        at = &s_tex_next[*at - 1];
    }
}

static int tex_size_from_format(uint32_t color)
{
    if (color >= 0x0C && color <= 0x0F)
        return 1;
    switch (color) {
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
    case 0x16: case 0x17: case 0x18: case 0x1B: case 0x1C: case 0x1D:
    case 0x1E: case 0x1F: case 0x20: case 0x24: case 0x25: case 0x26:
    case 0x2E: case 0x2F: case 0x30: case 0x31: case 0x34: case 0x35:
    case 0x36: case 0x37: case 0x40: case 0x41: case 0x43: case 0x44:
    case 0x45: case 0x46:
        return 0;
    default:
        return 1;
    }
}

static uint32_t bytes_hash(uint32_t va, uint32_t bytes)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset() + va;
    uint32_t h = 2166136261u ^ bytes, step, i;
    if (!bytes)
        return h;
    step = bytes / 256;
    if (step < 4) step = 4;
    step &= ~3u;
    for (i = 0; i + 4 <= bytes; i += step) {
        uint32_t w;
        memcpy(&w, mem + i, 4);
        h = (h ^ w) * 16777619u;
    }
    memcpy(&i, mem + bytes - 4, 4);
    return (h ^ i) * 16777619u;
}

static uint32_t tex_bytes(uint32_t color, uint32_t w, uint32_t h, uint32_t pitch)
{
    if (color == 0x0C) return ((w + 3) / 4) * ((h + 3) / 4) * 8;
    if (color == 0x0E || color == 0x0F) return ((w + 3) / 4) * ((h + 3) / 4) * 16;
    if (pitch) return pitch * h;
    return w * h * 4;
}

static uint32_t swizzle_index(uint32_t u, uint32_t v, uint32_t w, uint32_t h)
{
    uint32_t out = 0, bit = 0, mw = w - 1, mh = h - 1, m;
    for (m = 1; mw >= m || mh >= m; m <<= 1) {
        if (mw >= m) { if (u & m) out |= 1u << bit; bit++; }
        if (mh >= m) { if (v & m) out |= 1u << bit; bit++; }
    }
    return out;
}

static int decode_indexed(uint32_t va, uint32_t w, uint32_t h, uint32_t *out)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t entries = 256u >> ((s_palette_reg >> 2) & 3);
    const uint8_t *idx = mem + va;
    uint32_t x, y;
    if (!(s_palette_reg & ~0x3Fu))
        return 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t i = idx[swizzle_index(x, y, w, h)], c;
            if (i >= entries) i = 0;
            memcpy(&c, mem + s_palette_va + i * 4, 4);
            out[y * w + x] = c;
        }
    return 1;
}

#ifdef NFSU2_PCVR
static int pcvr_decode_swizzled(uint32_t va,uint32_t w,uint32_t h,uint32_t color,uint32_t *out)
{
    static int enabled=-1;
    static uint32_t verified[3];
    static uint64_t reference_ns[3],fast_ns[3];
    static uint8_t *snapshot;
    static uint32_t *reference;
    static size_t snapshot_cap,reference_cap;
    if(enabled<0) {
        const char *e=getenv("NFSU2_PCVR_TEXTURE_DECODE");
        enabled=e && *e=='1';
    }
    uint32_t entries=256u>>((s_palette_reg>>2)&3);
    if(!enabled || !pcvr_texture_decode_supported(w,h,color,entries) ||
       (color==0x0b && !(s_palette_reg&~0x3fu))) return 0;
    const uint8_t *mem=(const uint8_t *)xbox_GetMemoryOffset();
    unsigned kind=color==0x06?0:color==0x07?1:2;
    if(verified[kind]>=64)
        return pcvr_texture_decode(out,mem+va,w,h,color,mem+s_palette_va,entries);

    /* Compare both algorithms on one immutable snapshot. Guest writes must
     * not create false mismatches between successive reads during verification. */
    size_t count=(size_t)w*h,bytes=count*(color==0x0b?1:4);
    if(bytes>snapshot_cap) {
        uint8_t *p=realloc(snapshot,bytes);
        if(!p) return 0;
        snapshot=p; snapshot_cap=bytes;
    }
    if(count>reference_cap) {
        uint32_t *p=realloc(reference,count*4);
        if(!p) return 0;
        reference=p; reference_cap=count;
    }
    uint8_t palette[1024];
    memcpy(snapshot,mem+va,bytes);
    if(color==0x0b) memcpy(palette,mem+s_palette_va,entries*4);
    uint64_t start=hz_now();
    for(uint32_t y=0;y<h;++y) for(uint32_t x=0;x<w;++x) {
        uint32_t at=swizzle_index(x,y,w,h),c;
        if(color==0x0b) {
            unsigned i=snapshot[at]; if(i>=entries) i=0;
            memcpy(&c,palette+i*4,4);
        } else {
            memcpy(&c,snapshot+(size_t)at*4,4);
            if(color==0x07) c|=0xff000000u;
        }
        reference[(size_t)y*w+x]=c;
    }
    uint64_t middle=hz_now();
    pcvr_texture_decode(out,snapshot,w,h,color,palette,entries);
    uint64_t end=hz_now();
    if(memcmp(out,reference,count*4)) {
        memcpy(out,reference,count*4);
        enabled=0;
        fprintf(stderr,"[PCVR texture decode] mismatch format=%02X %ux%u; original path restored\n",color,w,h);
    } else {
        reference_ns[kind]+=middle-start; fast_ns[kind]+=end-middle;
        if(++verified[kind]==64)
            fprintf(stderr,"[PCVR texture decode] format=%02X: 64 snapshots pixel-identical; scalar %.3f ms, masked %.3f ms total\n",
                color,reference_ns[kind]/1e6,fast_ns[kind]/1e6);
    }
    return 1;
}
#endif

static void tex_free(VkTex *t)
{
#ifdef NFSU2_PCVR
    if(s_texture_profile>0) ++s_texture_work.evicted;
#endif
    garbage_add(t->image, t->view, t->mem);
    s_tex_bytes -= t->bytes;
    tex_index_del(t);
    memset(t, 0, sizeof *t);
}

static void tex_account(VkTex *t, uint32_t bytes)
{
    static uint64_t budget;
    s_hz_texs++;
    s_hz_tex_bytes += bytes;
    if (!budget) {
        const char *e = getenv("RECOMP_GL_TEX_MB");
        budget = (uint64_t)(e && atoi(e) > 0 ? atoi(e) : 256) << 20;
    }
    s_tex_bytes += (uint64_t)bytes - t->bytes;
    t->bytes = bytes;
    while (s_tex_bytes > budget) {
        VkTex *lru = NULL;
        uint32_t i;
        for (i = 0; i < s_tex_limit; i++) {
            VkTex *c = &s_tex[i];
            if (c->image && c != t && c->used != s_frame && (!lru || c->used < lru->used))
                lru = c;
        }
        if (!lru)
            break;
        tex_free(lru);
    }
}

/* Copy `bytes` at `src` (row length `row_texels`, 0 = tight) into the image. */
static void tex_upload(VkTex *t, const void *src, uint32_t bytes, uint32_t row_texels,
                       uint32_t layer)
{
    void *dst;
    VkDeviceSize at = ring_alloc(bytes, 16, &dst);
    VkBufferImageCopy rg;

    if (!dst)
        return;
    memcpy(dst, src, bytes);
    end_rendering();
    barrier_all();
    memset(&rg, 0, sizeof rg);
    rg.bufferOffset = at;
    rg.bufferRowLength = row_texels;
    rg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rg.imageSubresource.baseArrayLayer = layer;
    rg.imageSubresource.layerCount = 1;
    rg.imageExtent.width = t->w;
    rg.imageExtent.height = t->h;
    rg.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(s_cb, s_f->ring, t->image, VK_IMAGE_LAYOUT_GENERAL, 1, &rg);
}

/* Cube map faces follow each other, each with its mip chain, padded to 128
 * bytes (xemu's NV2A_CUBEMAP_FACE_ALIGNMENT); +X -X +Y -Y +Z -Z, the same
 * order and texel orientation as Vulkan's cube layers. */
static uint32_t cube_face_bytes(uint32_t color, uint32_t w, uint32_t h, uint32_t levels)
{
    uint32_t n = 0, l;

    if (!levels)
        levels = 1;
    for (l = 0; l < levels; l++) {
        n += tex_bytes(color, w, h, 0);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return (n + 127) & ~127u;
}

/* One 2D image (layer 0) or cube face from guest memory at va. */
static void tex_fill(VkTex *t, uint32_t va, uint32_t layer)
{
    const uint8_t *mem = (const uint8_t *)xbox_GetMemoryOffset();
    uint32_t color = t->color, w = t->w, h = t->h, pitch = t->pitch;
    Nv2aTexture nt;

    if (t->fmt != VK_FORMAT_B8G8R8A8_UNORM) {              /* DXT as it is */
        tex_upload(t, mem + va, tex_bytes(color, w, h, 0), 0, layer);
        return;
    }
    if (color == 0x12 && pitch && !(pitch & 3) && pitch / 4 >= w) {
        /* Linear A8R8G8B8 (movie frames): the bytes are B8G8R8A8 already. */
        tex_upload(t, mem + va, pitch * (h - 1) + w * 4, pitch / 4, layer);
        return;
    }
    if ((size_t)w * h > s_decode_cap) {
        free(s_decode);
        s_decode_cap = (size_t)w * h;
        s_decode = (uint32_t *)malloc(s_decode_cap * 4);
    }
    if (!s_decode)
        return;
#ifdef NFSU2_PCVR
    if(pcvr_decode_swizzled(va,w,h,color,s_decode)) {
        /* Exact pixel conversion; upload ordering and cache invalidation stay unchanged. */
    } else
#endif
    if (color == 0x06 || color == 0x07) {
        const uint32_t *src = (const uint32_t *)(mem + va);
        uint32_t x, y, fill = color == 0x07 ? 0xFF000000u : 0;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                s_decode[(size_t)y * w + x] = src[swizzle_index(x, y, w, h)] | fill;
    } else {
        int ok;
        memset(&nt, 0, sizeof nt);
        nt.offset = va; nt.width = w; nt.height = h; nt.pitch = pitch; nt.color = color;
        nt.addr_u = nt.addr_v = 1;
        ok = color == 0x0B ? decode_indexed(va, w, h, s_decode)
                           : nv2a_backend_decode_texture(&nt, s_decode);
        if (!ok) {
            uint32_t k;
            for (k = 0; k < w * h; k++)
                s_decode[k] = 0xFFFF00FFu;
            if (s_trace)
                LOGE("texture 0x%08X format 0x%02X not decodable\n", va, color);
        }
    }
    tex_upload(t, s_decode, w * h * 4, 0, layer);
}

/* face: bytes per cube face (cube_face_bytes), 0 for a 2D texture. */
static VkImageView tex_get(uint32_t va, uint32_t color, uint32_t w, uint32_t h, uint32_t pitch,
                           uint32_t face)
{
    VkTex *t = NULL, *victim = NULL;
    uint32_t i, hash;
    VkFormat fmt = VK_FORMAT_B8G8R8A8_UNORM;
    uint32_t layers = face ? 6 : 1, f, size;

    for (i = s_tex_head[tex_bucket(va, color, w, h)]; i; i = s_tex_next[i - 1]) {
        VkTex *c = &s_tex[i - 1];
        if (c->image && c->va == va && c->color == color && c->w == w && c->h == h
            && c->face == face) {
            t = c;
            break;
        }
    }
    if (t && t->checked == s_frame) {
        t->used = s_frame;
        return t->view;
    }
    hash = bytes_hash(va, face ? face * 6 : tex_bytes(color, w, h, pitch));
    if (color == 0x0B)
        hash ^= bytes_hash(s_palette_va, 1024) * 31u;
    if (t && t->hash == hash) {
        t->used = t->checked = s_frame;
        return t->view;
    }
    /* Time actual uploads only. A cached texture needs neither a timestamp
     * nor a clock-frequency query on every draw that references it. */
    uint64_t upload_start=hz_now();
    uint32_t uploaded_before=s_hz_texs;
#ifdef NFSU2_PCVR
    if(s_texture_profile<0) {
        const char *e=getenv("NFSU2_PCVR_TEXTURE_PROFILE");
        s_texture_profile=e && *e=='1';
    }
    uint64_t fill_start=0,account_start=0;
    int created=0;
#endif
    if (s_tex_changes < 0) {
        const char *e = getenv("RECOMP_TEX_CHANGES");
        s_tex_changes = e && atoi(e) > 0;
    }
    if (t && s_tex_changes)
        fprintf(stderr, "[tex] frame %u: %08X format %02X %ux%u changed\n",
                s_frame, va, color, w, h);
    if (s_have_bc && color == 0x0C) fmt = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    if (s_have_bc && color == 0x0E) fmt = VK_FORMAT_BC2_UNORM_BLOCK;
    if (s_have_bc && color == 0x0F) fmt = VK_FORMAT_BC3_UNORM_BLOCK;
    if (t && (t->fmt != fmt || t->pitch != pitch)) {
        tex_free(t);
        t = NULL;
    }
    if (!t) {
        for (i = 0; i < s_tex_limit; i++) {
            VkTex *c = &s_tex[i];
            if (!c->image) { if (!victim || victim->image) victim = c; }
            else if (!victim || (victim->image && c->used < victim->used)) victim = c;
        }
        t = victim;
        if (t->image)
            tex_free(t);
        t->va = va; t->color = color; t->w = w; t->h = h; t->pitch = pitch; t->fmt = fmt;
        t->face = face;
        VT("texture %08X fmt %02X %ux%u pitch %u%s\n", va, color, w, h, pitch,
           face ? " cube" : "");
        if (!make_image_n(w, h, layers, fmt,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT, &t->image, &t->view, &t->mem)) {
            memset(t, 0, sizeof *t);
            return face ? s_dummy_cube_view : s_dummy_view;
        }
        tex_index_add(t);
#ifdef NFSU2_PCVR
        created=1;
#endif
        if (s_trace)
            LOGE("texture 0x%08X format 0x%02X %ux%u pitch %u%s\n", va, color, w, h, pitch,
                 face ? " cube" : "");
    }
    t->hash = hash;
    t->used = t->checked = s_frame;

#ifdef NFSU2_PCVR
    if(s_texture_profile) fill_start=hz_now();
#endif
    for (f = 0; f < layers; f++)
        tex_fill(t, va + f * face, f);
#ifdef NFSU2_PCVR
    if(s_texture_profile) account_start=hz_now();
#endif
    size = fmt != VK_FORMAT_B8G8R8A8_UNORM ? tex_bytes(color, w, h, 0) : w * h * 4;
    tex_account(t, size * layers);
    if(s_hz_texs!=uploaded_before) {
        uint64_t end=hz_now();
        s_hz_tex_ns+=end-upload_start;
#ifdef NFSU2_PCVR
        if(s_texture_profile) {
            uint64_t fill=account_start-fill_start;
            s_texture_work.create_ns+=fill_start-upload_start;
            s_texture_work.fill_ns+=fill;
            s_texture_work.account_ns+=end-account_start;
            if(created) ++s_texture_work.created; else ++s_texture_work.updated;
            if(fmt!=VK_FORMAT_B8G8R8A8_UNORM) s_texture_work.bc_ns+=fill;
            else if(color==0x06 || color==0x07 || color==0x0b) s_texture_work.swizzle_ns+=fill;
            else s_texture_work.other_ns+=fill;
            if(end-upload_start>s_texture_work.max_ns) {
                s_texture_work.max_ns=end-upload_start;
                s_texture_work.color=color; s_texture_work.w=w; s_texture_work.h=h;
                s_texture_work.layers=layers;
            }
        }
#endif
    }
    return t->view;
}

/* A cube map whose faces the title renders (race car reflections: six
 * 128x128 targets, one per face, every frame). The faces exist only as
 * surface images, so the cube is assembled from them on the GPU, again
 * whenever a face was drawn into since the last copy. */
typedef struct {
    uint32_t va, w, h, face, size;
    VkImage image; VkImageView view; VkDeviceMemory mem;
    VkSurf  *src[6];
    uint32_t gen[6];
    uint32_t used;
} VkCube;
#define VK_MAX_CUBE 4
static VkCube s_cube[VK_MAX_CUBE];

static VkImageView cube_from_surfaces(uint32_t va, uint32_t w, uint32_t h, uint32_t face,
                                      VkSurf *faces[6])
{
    VkCube *c = NULL;
    uint32_t f, size = 0;
    int i, copied = 0;

    for (f = 0; f < 6; f++)
        if (faces[f] && to_stored(w, faces[f]->pw, faces[f]->w) > size)
            size = to_stored(w, faces[f]->pw, faces[f]->w);
    for (i = 0; i < VK_MAX_CUBE; i++)
        if (s_cube[i].image && s_cube[i].va == va && s_cube[i].w == w && s_cube[i].h == h
            && s_cube[i].face == face && s_cube[i].size == size) {
            c = &s_cube[i];
            break;
        }
    if (!c) {
        for (i = 0; i < VK_MAX_CUBE; i++)
            if (!c || !s_cube[i].image || (c->image && s_cube[i].used < c->used))
                c = &s_cube[i];
        if (c->image)
            garbage_add(c->image, c->view, c->mem);
        memset(c, 0, sizeof *c);
        if (!make_image_n(size, size, 6, VK_FORMAT_B8G8R8A8_UNORM,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT, &c->image, &c->view, &c->mem)) {
            memset(c, 0, sizeof *c);
            return s_dummy_cube_view;
        }
        clear_image_color(c->image, 0, 0, 0, 0);
        c->va = va; c->w = w; c->h = h; c->face = face; c->size = size;
        if (s_trace)
            LOGE("cube 0x%08X %ux%u from surfaces (stored %u)\n", va, w, h, size);
    }
    c->used = s_frame;
    for (f = 0; f < 6; f++) {
        VkSurf *sf = faces[f];
        VkImageBlit bl;
        if (!sf || (c->src[f] == sf && c->gen[f] == sf->gen))
            continue;
        if (!copied) {
            end_rendering();
            barrier_all();
            copied = 1;
        }
        memset(&bl, 0, sizeof bl);
        bl.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bl.srcSubresource.layerCount = 1;
        bl.srcOffsets[1].x = (int32_t)to_stored(w, sf->pw, sf->w);
        bl.srcOffsets[1].y = (int32_t)to_stored(h, sf->ph, sf->h);
        bl.srcOffsets[1].z = 1;
        bl.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bl.dstSubresource.baseArrayLayer = f;
        bl.dstSubresource.layerCount = 1;
        bl.dstOffsets[1].x = (int32_t)size;
        bl.dstOffsets[1].y = (int32_t)size;
        bl.dstOffsets[1].z = 1;
        vkCmdBlitImage(s_cb, sf->image, VK_IMAGE_LAYOUT_GENERAL, c->image,
                       VK_IMAGE_LAYOUT_GENERAL, 1, &bl, VK_FILTER_LINEAR);
        c->src[f] = sf;
        c->gen[f] = sf->gen;
    }
    if (copied)
        barrier_all();
    return c->view;
}

/* Samplers: wrap u, wrap v, mag, min. */
typedef struct { uint32_t key; VkSampler s; } SamplerEnt;
static SamplerEnt s_samplers[64];
static int s_nsamplers;

static VkSamplerAddressMode wrap_mode(uint32_t m)
{
    switch (m) {
    case 2:  return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case 3:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case 4:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case 5:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static VkSampler sampler_get(uint32_t key)
{
    VkSamplerCreateInfo ci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    int i;
    for (i = 0; i < s_nsamplers; i++)
        if (s_samplers[i].key == key)
            return s_samplers[i].s;
    ci.addressModeU = wrap_mode(key & 0xF);
    ci.addressModeV = wrap_mode((key >> 8) & 0xF);
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.magFilter = (key & 0x10000) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    ci.minFilter = (key & 0x20000) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.maxLod = 0.0f;
    ci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (s_nsamplers == 64)
        s_nsamplers = 0;                    /* never in practice: 4 x 4 x 4 keys */
    if (vkCreateSampler(s_dev, &ci, NULL, &s_samplers[s_nsamplers].s) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    s_samplers[s_nsamplers].key = key;
    return s_samplers[s_nsamplers++].s;
}

/* A surface kept taller than its clip (surf_get) and sampled as a texture
 * of the clip's size: texcoords cover the w x h texels at the top left, not
 * the whole surface. A texture of the surface's logical or real size keeps
 * the old full-surface mapping (anti-aliased surfaces). */
static void rt_scale(uint32_t w, uint32_t h, uint32_t sw, uint32_t sh,
                     uint32_t aa_sx, uint32_t aa_sy, float scale[2])
{
    uint32_t lw = sw / (aa_sx ? aa_sx : 1), lh = sh / (aa_sy ? aa_sy : 1);

    if (w < lw) scale[0] *= (float)w / (float)lw;
    if (h < lh) scale[1] *= (float)h / (float)lh;
}

/* Stage i: its image view (a surface or a cached texture), sampler and
 * texcoord scale. Records uploads, so call it outside rendering or accept
 * that it ends the pass. */
static void bind_stage(const uint32_t *regs, int i, float scale[2], VkDescriptorImageInfo *out,
                       VkSurf **sampled)
{
    /* Texture mode CUBE_MAP: the shader declares a samplerCube (gl_psh.c),
     * so the descriptor must be a cube view whatever is bound. */
    int cube = ((regs[0x1E70 / 4] >> (5 * i)) & 0x1F) == 3;
    /* RECOMP_VK_CUBE=0: cube stages sample the black dummy (no car
     * reflections, the old look). */
    static int cube_on = -1;
    if (cube_on < 0) {
        const char *e = getenv("RECOMP_VK_CUBE");
        cube_on = !(e && *e == '0');
    }
    uint32_t base = (0x1B00u + (uint32_t)i * 0x40u) / 4;
    uint32_t control0 = regs[base + 3];
    uint32_t format = regs[base + 1];
    uint32_t color = (format >> 8) & 0xFF;
    uint32_t va, w, h, pitch = 0, addr, filter, key;
    VkSurf *rt;

    scale[0] = scale[1] = 1.0f;
    out->imageView = cube ? s_dummy_cube_view : s_dummy_view;
    out->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    out->sampler = sampler_get(0);
    *sampled = NULL;
    if (!(control0 & 0x40000000u) || !regs[base])
        return;
    va = s_batch->tex_va[i];
    if (tex_size_from_format(color)) {
        w = 1u << ((format >> 20) & 0xF);
        h = 1u << ((format >> 24) & 0xF);
    } else {
        uint32_t rect = regs[base + 7];
        w = rect >> 16;
        h = rect & 0xFFFF;
        pitch = regs[base + 4] >> 16;
        if (w) scale[0] = 1.0f / (float)w;
        if (h) scale[1] = 1.0f / (float)h;
    }
    if (!w || !h || w > 4096 || h > 4096)
        return;
    s_palette_reg = regs[base + 8];
    s_palette_va = s_batch->pal_va[i];
    rt = cube ? NULL : surf_find(va);
    if (cube) {
        /* SET_TEXTURE_FORMAT bit 2: CUBEMAP_ENABLE; mip levels in [19:16]. */
        if (cube_on && (format & 4)) {
            uint32_t face = cube_face_bytes(color, w, h, (format >> 16) & 0xF), f;
            VkSurf *faces[6];
            int any = 0;
            for (f = 0; f < 6; f++)
                any |= (faces[f] = surf_find(va + f * face)) != NULL;
            if (any) {
                out->imageView = cube_from_surfaces(va, w, h, face, faces);
            } else {
                out->imageView = tex_get(va, color, w, h, 0, face);
            }
        }
    } else if (rt) {
        out->imageView = rt->view;
        *sampled = rt;
        rt_scale(w, h, rt->w, rt->h, rt->aa_sx, rt->aa_sy, scale);
    } else {
        out->imageView = tex_get(va, color, w, h, pitch, 0);
    }
    addr = regs[base + 2];
    filter = regs[base + 5];
    key = (addr & 0xF0F);
    if (((filter >> 24) & 0xF) == 1) key |= 0x10000;
    if (((filter >> 16) & 0xFF) == 1) key |= 0x20000;
    out->sampler = sampler_get(key);
}

/* ── Shaders and pipelines ────────────────────────────────────────── */

static VkDescriptorSetLayout s_dsl;
static VkPipelineLayout      s_layout;
static VkPipelineCache       s_pcache;

/* Same seven bindings as the ordinary push-descriptor path. A template lets
 * the driver prepare their layout once; every draw still writes ALL bindings,
 * including unchanged images. No descriptor or vertex-input state is skipped. */
typedef struct {
    VkDescriptorBufferInfo buffers[3];
    VkDescriptorImageInfo images[4];
} DrawDescriptors;

#ifdef NFSU2_OPENXR
/* Immutable descriptor sets with dynamic uniform offsets. Every draw binds
 * all seven resources; no push-descriptor state is assumed to survive. Sets
 * belong to one frame ring and are reset only after that slot/chunk's fence. */
struct DescriptorPoolBlock {
    DescriptorPoolBlock *next;
    VkDescriptorPool pool;
    uint32_t used;
};
struct DescriptorEntry {
    DrawDescriptors data;
    VkDescriptorSet set;
    uint32_t next;
};
static uint32_t s_descriptor_hits, s_descriptor_misses;

static void descriptor_frame_reset(void)
{
    for(DescriptorPoolBlock *p=s_f->descriptor_pools;p;p=p->next) {
        if(vkResetDescriptorPool(s_dev,p->pool,0)!=VK_SUCCESS) abort();
        p->used=0;
    }
    s_f->descriptor_current=s_f->descriptor_pools;
    s_f->descriptor_count=0;
    memset(s_f->descriptor_heads,0,sizeof s_f->descriptor_heads);
}

static int descriptor_equal(const DrawDescriptors *a,const DrawDescriptors *b)
{
    for(unsigned i=0;i<3;++i)
        if(a->buffers[i].buffer!=b->buffers[i].buffer || a->buffers[i].range!=b->buffers[i].range) return 0;
    for(unsigned i=0;i<4;++i)
        if(a->images[i].sampler!=b->images[i].sampler || a->images[i].imageView!=b->images[i].imageView ||
           a->images[i].imageLayout!=b->images[i].imageLayout) return 0;
    return 1;
}

static VkDescriptorSet descriptor_allocate(void)
{
    DescriptorPoolBlock **slot=&s_f->descriptor_current;
    DescriptorPoolBlock *previous=NULL;
    if(*slot && (*slot)->used==DESCRIPTOR_POOL_SETS) {
        previous=*slot;
        *slot=previous->next;
    }
    if(!*slot) {
        DescriptorPoolBlock *p=calloc(1,sizeof *p);
        if(!p) return VK_NULL_HANDLE;
        VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,3*DESCRIPTOR_POOL_SETS},
                                      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4*DESCRIPTOR_POOL_SETS}};
        VkDescriptorPoolCreateInfo ci={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        ci.maxSets=DESCRIPTOR_POOL_SETS; ci.poolSizeCount=2; ci.pPoolSizes=sizes;
        if(vkCreateDescriptorPool(s_dev,&ci,NULL,&p->pool)!=VK_SUCCESS) { free(p); return VK_NULL_HANDLE; }
        if(previous) previous->next=p; else s_f->descriptor_pools=p;
        *slot=p;
    }
    VkDescriptorSetAllocateInfo ai={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool=(*slot)->pool; ai.descriptorSetCount=1; ai.pSetLayouts=&s_dsl;
    VkDescriptorSet set=VK_NULL_HANDLE;
    if(vkAllocateDescriptorSets(s_dev,&ai,&set)!=VK_SUCCESS) return VK_NULL_HANDLE;
    ++(*slot)->used;
    return set;
}

static int bind_dynamic_descriptors(const DrawDescriptors *data)
{
    uint64_t hash=1469598103934665603ull;
    uint32_t offsets[3];
    for(unsigned i=0;i<3;++i) {
        /* Range belongs to the immutable set; only the aligned offset varies. */
        if(data->buffers[i].offset>UINT32_MAX) return 0;
        offsets[i]=(uint32_t)data->buffers[i].offset;
        hash=(hash^(uint64_t)(uintptr_t)data->buffers[i].buffer)*1099511628211ull;
        hash=(hash^data->buffers[i].range)*1099511628211ull;
    }
    for(unsigned i=0;i<4;++i) {
        hash=(hash^(uint64_t)(uintptr_t)data->images[i].sampler)*1099511628211ull;
        hash=(hash^(uint64_t)(uintptr_t)data->images[i].imageView)*1099511628211ull;
        hash=(hash^data->images[i].imageLayout)*1099511628211ull;
    }
    uint32_t bucket=(uint32_t)(hash^(hash>>32))&(DESCRIPTOR_BUCKETS-1);
    VkDescriptorSet set=VK_NULL_HANDLE;
    for(uint32_t at=s_f->descriptor_heads[bucket];at;at=s_f->descriptor_entries[at-1].next) {
        DescriptorEntry *entry=&s_f->descriptor_entries[at-1];
        if(descriptor_equal(data,&entry->data)) { set=entry->set; ++s_descriptor_hits; break; }
    }
    if(!set) {
        if(s_f->descriptor_count==s_f->descriptor_capacity) {
            uint32_t capacity=s_f->descriptor_capacity?s_f->descriptor_capacity*2:256;
            DescriptorEntry *entries=realloc(s_f->descriptor_entries,(size_t)capacity*sizeof *entries);
            if(!entries) return 0;
            s_f->descriptor_entries=entries; s_f->descriptor_capacity=capacity;
        }
        set=descriptor_allocate();
        if(!set) return 0;
        DescriptorEntry *entry=&s_f->descriptor_entries[s_f->descriptor_count];
        entry->data=*data; entry->set=set;
        for(unsigned i=0;i<3;++i) entry->data.buffers[i].offset=0;
        VkWriteDescriptorSet writes[7]={0};
        for(unsigned i=0;i<7;++i) {
            writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet=set; writes[i].dstBinding=i; writes[i].descriptorCount=1;
            if(i<2 || i==6) {
                writes[i].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                writes[i].pBufferInfo=&entry->data.buffers[i==6?2:i];
            } else {
                writes[i].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].pImageInfo=&entry->data.images[i-2];
            }
        }
        vkUpdateDescriptorSets(s_dev,7,writes,0,NULL);
        entry->next=s_f->descriptor_heads[bucket];
        s_f->descriptor_heads[bucket]=++s_f->descriptor_count;
        ++s_descriptor_misses;
    }
    vkCmdBindDescriptorSets(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,s_layout,0,1,&set,3,offsets);
    return 1;
}

static void create_push_template(void)
{
    if(s_dynamic_descriptors) return;
    const char *e = getenv("NFSU2_VR_DESCRIPTOR_TEMPLATE");
    if (!quest_xr_enabled() || !e || *e != '1') return;
    p_push_template = (void *)vkGetDeviceProcAddr(s_dev, "vkCmdPushDescriptorSetWithTemplateKHR");
    if (!p_push_template) {
        LOGE("descriptor template unavailable; using full descriptor writes\n");
        return;
    }
    VkDescriptorUpdateTemplateEntry entries[7] = {0};
    for (uint32_t i = 0; i < 7; ++i) {
        entries[i].dstBinding = i;
        entries[i].descriptorCount = 1;
        if (i < 2 || i == 6) {
            entries[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            entries[i].offset = offsetof(DrawDescriptors, buffers) +
                (i == 6 ? 2 : i) * sizeof(VkDescriptorBufferInfo);
            entries[i].stride = sizeof(VkDescriptorBufferInfo);
        } else {
            entries[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            entries[i].offset = offsetof(DrawDescriptors, images) +
                (i - 2) * sizeof(VkDescriptorImageInfo);
            entries[i].stride = sizeof(VkDescriptorImageInfo);
        }
    }
    VkDescriptorUpdateTemplateCreateInfo ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO};
    ci.descriptorUpdateEntryCount = 7;
    ci.pDescriptorUpdateEntries = entries;
    ci.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
    ci.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    ci.pipelineLayout = s_layout;
    ci.set = 0;
    VkResult result = vkCreateDescriptorUpdateTemplate(s_dev, &ci, NULL, &s_push_template);
    if (result != VK_SUCCESS) s_push_template = VK_NULL_HANDLE;
    LOGE("full descriptor template %s (%d); all 7 bindings written per draw\n",
         s_push_template ? "enabled" : "unavailable", (int)result);
}
#endif

typedef struct {
    uint64_t   vkey;
    Nv2aPshKey pkey;
    VkShaderModule vs, fs;
    uint32_t   used;
    uint32_t   hash;
    int        next;
#ifdef NFSU2_PCVR
    int        tracked_mvp;
#endif
} VkProg;

#define VK_MAX_PROG 1024
#define PROG_BUCKETS 1024
static VkProg s_prog[VK_MAX_PROG];
static int s_prog_head[PROG_BUCKETS];
static uint32_t s_prog_made, s_prog_live;

/* This trial preserves both immutable cross-eye uploads and the constant-zero
 * shader path for single draws. It requires exact geometry checks, and never
 * enables the older state cache. */
static int shared_instances(void)
{
#ifdef NFSU2_OPENXR
    static int on=-1;
    if(on<0) {
        const char *enable=getenv("NFSU2_VR_SHARED_INSTANCES");
        const char *inst=getenv("RECOMP_VK_INSTANCE");
        const char *checked=getenv("NFSU2_VR_CHECKED_INSTANCES");
        const char *geometry=getenv("NFSU2_VR_GEOMETRY_REUSE");
        const char *uniform=getenv("NFSU2_VR_UNIFORM_REUSE");
        const char *state=getenv("RECOMP_VK_STATE_CACHE");
        on=enable && *enable=='1' && inst && *inst=='1' &&
           checked && *checked=='1' && geometry && *geometry=='1' &&
           uniform && *uniform=='1' && state && *state=='0';
        LOGE("shared checked instances %s\n",on?"enabled":"off");
    }
    return on;
#else
    return 0;
#endif
}

static int single_instance_shader(void)
{
#ifdef NFSU2_OPENXR
    static int on=-1;
    if(on<0) {
        const char *e=getenv("NFSU2_VR_SINGLE_SHADER"), *inst=getenv("RECOMP_VK_INSTANCE");
        on=e && *e=='1' && inst && *inst=='0';
        LOGE("single-instance shader specialization %s\n",on?"enabled":"off");
    }
    return on;
#else
    return 0;
#endif
}

uint32_t nv2a_gl_program_count(void) { return s_prog_live; }
/* The GL renderer's thread statistics; the Vulkan one records on the
 * executor's thread. */
void nv2a_gl_queue_stats(double secs, int *gl_idle_pct, int *exec_wait_pct)
{
    (void)secs;
    if (gl_idle_pct) *gl_idle_pct = 0;
    if (exec_wait_pct) *exec_wait_pct = 0;
}

typedef struct {
    glslang_stage_t stage;
    const char *src;
    uint32_t *words;
    size_t nwords;
    int ok;
} CompileJob;

static void *compile_thread(void *arg)
{
    CompileJob *j = (CompileJob *)arg;
    glslang_input_t in;
    glslang_shader_t *sh;
    glslang_program_t *pr;

    memset(&in, 0, sizeof in);
    in.language = GLSLANG_SOURCE_GLSL;
    in.stage = j->stage;
    in.client = GLSLANG_CLIENT_VULKAN;
    in.client_version = GLSLANG_TARGET_VULKAN_1_1;
    in.target_language = GLSLANG_TARGET_SPV;
    in.target_language_version = GLSLANG_TARGET_SPV_1_3;
    in.code = j->src;
    in.default_version = 450;
    in.default_profile = GLSLANG_NO_PROFILE;
    in.messages = GLSLANG_MSG_DEFAULT_BIT;
    in.resource = glslang_default_resource();
    sh = glslang_shader_create(&in);
    if (!glslang_shader_preprocess(sh, &in) || !glslang_shader_parse(sh, &in)) {
        LOGE("%s shader failed:\n%s\n", j->stage == GLSLANG_STAGE_VERTEX ? "vertex" : "fragment",
             glslang_shader_get_info_log(sh));
        if (s_trace)
            fprintf(stderr, "%s\n", j->src);
        glslang_shader_delete(sh);
        return NULL;
    }
    pr = glslang_program_create();
    glslang_program_add_shader(pr, sh);
    if (!glslang_program_link(pr, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        LOGE("link failed: %s\n", glslang_program_get_info_log(pr));
    } else {
        glslang_program_SPIRV_generate(pr, j->stage);
        j->nwords = glslang_program_SPIRV_get_size(pr);
        j->words = (uint32_t *)malloc(j->nwords * 4);
        if (j->words) {
            glslang_program_SPIRV_get(pr, j->words);
            j->ok = 1;
        }
    }
    glslang_program_delete(pr);
    glslang_shader_delete(sh);
    return NULL;
}

/* ── Caches across runs ───────────────────────────────────────────
 *
 * A program compiled at first use stalled a race start for ~1 s (glslang,
 * then NIR and NAK inside NVK: ~50 ms each on the console). Three files, next
 * to the NRO (the current directory on Linux); RECOMP_PROG_CACHE=0 disables
 * them all, =<path> moves the program list:
 *   progcache.bin    what each program was built from -- the GL renderer's
 *                    records, shared (the format does not depend on the back end)
 *   vkspirv.bin      glslang's output by hash of the GLSL source
 *   vkpipes.bin      the pipeline variants seen (program + fixed state)
 *   vkpipecache.bin  the driver's VkPipelineCache (NAK binaries), saved while
 *                    running whenever pipelines were added
 * At start-up every recorded program and pipeline is built before the title
 * draws. */
static int s_prewarming;
static int s_cache_off = -1;

static const char *cache_dir(void)
{
#if defined(__SWITCH__)
    return "sdmc:/switch/nfsu2x/";
#else
    return "";
#endif
}

static int cache_path(char *out, size_t cap, const char *name)
{
    if (s_cache_off < 0) {
        const char *e = getenv("RECOMP_PROG_CACHE");
        s_cache_off = e && e[0] == '0' && !e[1];
    }
    if (s_cache_off)
        return 0;
    if (!strcmp(name, "progcache.bin")) {
        const char *e = getenv("RECOMP_PROG_CACHE");
        if (e && *e) {
            snprintf(out, cap, "%s", e);
            return 1;
        }
    }
    snprintf(out, cap, "%s%s", cache_dir(), name);
    return 1;
}

#define SPV_MAGIC 0x31565053u                   /* "SPV1" */
typedef struct { uint64_t hash; uint32_t stage, nwords; uint32_t *words; } SpvEnt;
static SpvEnt *s_spv;
static uint32_t s_nspv, s_spv_cap, s_spv_hits;

static uint64_t text_hash(const char *t, uint32_t stage)
{
    uint64_t h = 1469598103934665603ull ^ stage;
    while (*t)
        h = (h ^ (uint8_t)*t++) * 1099511628211ull;
    return h;
}

static void spv_add(uint64_t hash, uint32_t stage, const uint32_t *w, uint32_t n)
{
    if (s_nspv == s_spv_cap) {
        s_spv_cap = s_spv_cap ? s_spv_cap * 2 : 256;
        s_spv = (SpvEnt *)realloc(s_spv, s_spv_cap * sizeof *s_spv);
    }
    s_spv[s_nspv].hash = hash;
    s_spv[s_nspv].stage = stage;
    s_spv[s_nspv].nwords = n;
    s_spv[s_nspv].words = (uint32_t *)malloc((size_t)n * 4);
    memcpy(s_spv[s_nspv].words, w, (size_t)n * 4);
    s_nspv++;
}

static const SpvEnt *spv_find(uint64_t hash, uint32_t stage)
{
    uint32_t i;
    for (i = 0; i < s_nspv; i++)
        if (s_spv[i].hash == hash && s_spv[i].stage == stage)
            return &s_spv[i];
    return NULL;
}

static void spv_load(void)
{
    char path[256];
    FILE *f;
    uint32_t hdr[4];
    if (!cache_path(path, sizeof path, "vkspirv.bin") || !(f = fopen(path, "rb")))
        return;
    while (fread(hdr, sizeof hdr, 1, f) == 1) {
        uint64_t hash;
        uint32_t *w;
        if (hdr[0] != SPV_MAGIC || hdr[3] > (1u << 22))
            break;
        memcpy(&hash, &hdr[1], 8);
        w = (uint32_t *)malloc((size_t)hdr[3] * 8 + 8);
        if (!w || fread(w, 4, hdr[3], f) != hdr[3]) { free(w); break; }
        /* the stage is in the word after the header */
        spv_add(hash, w[0], w + 1, hdr[3] - 1);
        free(w);
    }
    fclose(f);
}

static void spv_append(uint64_t hash, uint32_t stage, const uint32_t *w, uint32_t n)
{
    char path[256];
    FILE *f;
    uint32_t hdr[4];
    if (!cache_path(path, sizeof path, "vkspirv.bin") || !(f = fopen(path, "ab")))
        return;
    hdr[0] = SPV_MAGIC;
    memcpy(&hdr[1], &hash, 8);
    hdr[3] = n + 1;
    fwrite(hdr, sizeof hdr, 1, f);
    fwrite(&stage, 4, 1, f);
    fwrite(w, 4, n, f);
    fclose(f);
}

/* GLSL to a shader module. glslang's parser recurses deeply: it runs on a
 * thread with a 4 MB stack (the executor's is smaller). */
static VkShaderModule compile(glslang_stage_t stage, const char *src)
{
    CompileJob j = { stage, src, NULL, 0, 0 };
    pthread_t t;
    pthread_attr_t at;
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule m = VK_NULL_HANDLE;

    uint64_t hash = text_hash(src, (uint32_t)stage);
    const SpvEnt *hit = spv_find(hash, (uint32_t)stage);

    if (hit) {
        s_spv_hits++;
        ci.codeSize = (size_t)hit->nwords * 4;
        ci.pCode = hit->words;
        vkCreateShaderModule(s_dev, &ci, NULL, &m);
        return m;
    }
    VT("compile %s (%u bytes of GLSL)\n", stage == GLSLANG_STAGE_VERTEX ? "vs" : "fs",
       (unsigned)strlen(src));
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 4u << 20);
    if (pthread_create(&t, &at, compile_thread, &j) == 0)
        pthread_join(t, NULL);
    else
        compile_thread(&j);
    pthread_attr_destroy(&at);
    if (!j.ok)
        return VK_NULL_HANDLE;
    ci.codeSize = j.nwords * 4;
    ci.pCode = j.words;
    VT("  SPIR-V %u words -> vkCreateShaderModule\n", (unsigned)j.nwords);
    vkCreateShaderModule(s_dev, &ci, NULL, &m);
    spv_add(hash, (uint32_t)stage, j.words, (uint32_t)j.nwords);
    spv_append(hash, (uint32_t)stage, j.words, (uint32_t)j.nwords);
    free(j.words);
    return m;
}

static uint64_t prog_hash(const Nv2aRawBatch *b)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t pc, k;
    for (pc = b->vp_start; pc < b->vp_slots; pc++) {
        for (k = 0; k < 4; k++)
            h = (h ^ b->vp_program[pc][k]) * 1099511628211ull;
        if (b->vp_program[pc][3] & 1)
            break;
    }
    return h ? h : 1;
}

static uint32_t prog_bucket(uint64_t vkey, const Nv2aPshKey *pk)
{
    const uint32_t *w = (const uint32_t *)pk;
    uint64_t h = vkey ^ 1469598103934665603ull;
    size_t i;
    for (i = 0; i < sizeof *pk / 4; i++)
        h = (h ^ w[i]) * 1099511628211ull;
    return (uint32_t)(h ^ (h >> 32)) & (PROG_BUCKETS - 1);
}

static void pipes_forget_prog(int prog);

/* progcache.bin records: nv2a_gl.c's ProgRec, byte for byte. */
#define PROG_REC_MAGIC 0x4350564Eu              /* "NVPC" */
typedef struct {
    uint32_t   magic, xform, vp_start, slots;
    Nv2aPshKey pk;
    uint32_t   prog[136][4];
} ProgRec;

static void prog_rec_fill(ProgRec *rec, const Nv2aRawBatch *b, const Nv2aPshKey *pk)
{
    memset(rec, 0, sizeof *rec);
    rec->magic = PROG_REC_MAGIC;
    rec->xform = b->xform == 2 ? 2 : 1;
    rec->pk = *pk;
    if (rec->xform == 2 && b->vp_program && b->vp_slots <= 136) {
        rec->vp_start = b->vp_start;
        rec->slots = b->vp_slots;
        memcpy(rec->prog, b->vp_program, (size_t)b->vp_slots * 16);
    }
}

static void prog_rec_append(const Nv2aRawBatch *b, const Nv2aPshKey *pk)
{
    char path[256];
    ProgRec rec;
    FILE *f;
    if (s_prewarming || !cache_path(path, sizeof path, "progcache.bin"))
        return;
    prog_rec_fill(&rec, b, pk);
    if ((f = fopen(path, "ab"))) {
        fwrite(&rec, sizeof rec, 1, f);
        fclose(f);
    }
}

/* The batch a record describes (vertex program slots in rec). */
static void prog_rec_batch(const ProgRec *rec, Nv2aRawBatch *bb, uint32_t n)
{
    memset(bb, 0, sizeof *bb);
    bb->xform = rec->xform;
    bb->vp_program = (const uint32_t (*)[4])rec->prog;
    bb->vp_slots = rec->slots ? rec->slots : 136;
    bb->vp_start = rec->vp_start;
    bb->vp_prog_gen = 0x80000000u + n;          /* hash it: not an executor version */
}

static VkProg *prog_get(const Nv2aRawBatch *b, const Nv2aPshKey *pk)
{
    static uint32_t last_gen;
    static uint64_t last_vkey;
    static VkProg *last;
    static char vsrc[320 * 1024], fsrc[64 * 1024];
    uint64_t vkey = 0;
    uint32_t bucket;
    VkProg *p, *victim = NULL;
    int i;
    size_t n;

    if (b->xform == 2) {
        if (b->vp_prog_gen != last_gen || !last_vkey) {
            last_vkey = prog_hash(b);
            last_gen = b->vp_prog_gen;
        }
        vkey = last_vkey;
    }
    if (last && last->vs && last->vkey == vkey && !memcmp(&last->pkey, pk, sizeof *pk)) {
        last->used = s_frame;
        return last;
    }
    bucket = prog_bucket(vkey, pk);
    for (i = s_prog_head[bucket]; i; i = s_prog[i - 1].next) {
        VkProg *c = &s_prog[i - 1];
        if (c->vs && c->vkey == vkey && !memcmp(&c->pkey, pk, sizeof *pk)) {
            c->used = s_frame;
            last = c;
            return c;
        }
    }
    uint64_t compile_start=hz_now();
    for (i = 0; i < VK_MAX_PROG; i++) {
        VkProg *c = &s_prog[i];
        if (!c->vs) { if (!victim || victim->vs) victim = c; }
        else if (!victim || (victim->vs && c->used < victim->used)) victim = c;
    }
    p = victim;
    last = NULL;
    if (p->vs) {
        int *at = &s_prog_head[p->hash], me = (int)(p - s_prog) + 1;
        while (*at) {
            if (*at == me) { *at = p->next; break; }
            at = &s_prog[*at - 1].next;
        }
        pipes_forget_prog((int)(p - s_prog));
        vkDestroyShaderModule(s_dev, p->vs, NULL);
        vkDestroyShaderModule(s_dev, p->fs, NULL);
        s_prog_live--;
    }
    memset(p, 0, sizeof *p);

    n = (size_t)snprintf(vsrc, sizeof vsrc, "%s", nv2a_gl_vsh_prelude());
    if (vkey) {
        int k = nv2a_gl_vsh_program(b->vp_program, b->vp_slots, b->vp_start, vsrc + n, sizeof vsrc - n);
        if (k < 0)
            return NULL;
        n += (size_t)k;
        snprintf(vsrc + n, sizeof vsrc - n, "%s", nv2a_gl_vsh_main_program());
    } else {
        snprintf(vsrc + n, sizeof vsrc - n, "%s", nv2a_gl_vsh_fixed());
    }
    if(single_instance_shader() || shared_instances()) {
        /* Insert after #version; source hashes separate the shader variants.
         * The single-instance variant requires instancing to remain disabled;
         * the shared variant selects its constant base per pipeline. */
        const char *define=shared_instances() ? "#define NV2A_VK_INSTANCE_SPECIALIZE 1\n" :
                                               "#define NV2A_VK_SINGLE_INSTANCE 1\n";
        char *body=strchr(vsrc,'\n');
        size_t length=strlen(vsrc), extra=strlen(define);
        if(!body || length+extra>=sizeof vsrc) return NULL;
        ++body;
        memmove(body+extra,body,strlen(body)+1);
        memcpy(body,define,extra);
    }
    if (nv2a_gl_psh(pk, fsrc, sizeof fsrc) < 0)
        return NULL;
    if (s_multiview) {
        const char define[] = "#define NV2A_VK_MULTIVIEW 1\n";
        char *sources[2] = {vsrc, fsrc};
        size_t capacities[2] = {sizeof vsrc, sizeof fsrc};
        for (int stage=0;stage<2;++stage) {
            char *body=strchr(sources[stage],'\n');
            if (!body || strlen(sources[stage])+sizeof define>capacities[stage]) return NULL;
            ++body;
            memmove(body+sizeof define-1,body,strlen(body)+1);
            memcpy(body,define,sizeof define-1);
        }
    }
    p->vs = compile(GLSLANG_STAGE_VERTEX, vsrc);
    p->fs = compile(GLSLANG_STAGE_FRAGMENT, fsrc);
    if (!p->vs || !p->fs) {
        if (p->vs) vkDestroyShaderModule(s_dev, p->vs, NULL);
        if (p->fs) vkDestroyShaderModule(s_dev, p->fs, NULL);
        memset(p, 0, sizeof *p);
        return NULL;
    }
    s_prog_live++;
    if ((++s_prog_made % 100) == 0)
        LOGE("%u shader programs compiled (%u live)\n", s_prog_made, s_prog_live);
    p->vkey = vkey;
    p->pkey = *pk;
#ifdef NFSU2_PCVR
    /* This classification depends only on the immutable microcode already
     * covered by vkey, not on per-object constants or head pose. */
    p->tracked_mvp = b->xform==2 &&
        quest_program_has_mvp(b->vp_program,b->vp_start,b->vp_slots);
#endif
    p->used = s_frame;
    p->hash = bucket;
    p->next = s_prog_head[bucket];
    s_prog_head[bucket] = (int)(p - s_prog) + 1;
    last = p;
    prog_rec_append(b, pk);
    if(!s_prewarming) {
        ++s_hz_progs;
        s_hz_prog_ns+=hz_now()-compile_start;
    }
    return p;
}

#ifdef NFSU2_OPENXR
static int program_has_tracked_mvp(const VkProg *p,const Nv2aRawBatch *b)
{
#ifdef NFSU2_PCVR
    static unsigned checked;
    static int failed;
    /* Compare real draws before using the cached classification alone. A
     * mismatch retains the original analysis for the remainder of the run. */
    if(checked<100000 || failed) {
        int reference=quest_program_has_mvp(b->vp_program,b->vp_start,b->vp_slots);
        if(reference!=p->tracked_mvp && !failed) {
            failed=1;
            LOGE("PCVR program metadata mismatch; using original analysis\n");
        }
        if(failed) return reference;
        if(++checked==100000)
            LOGE("PCVR program metadata verified on 100000 draws; cached analysis active\n");
    }
    return p->tracked_mvp;
#else
    (void)p;
    return quest_program_has_mvp(b->vp_program,b->vp_start,b->vp_slots);
#endif
}
#endif

typedef struct {
    int      prog;
    uint32_t blend, bsrc, bdst, beq, cmask, depth, topo;
    uint32_t fragment_flags; /* bits 0/1: FS checks; bit 2: instanced VS; bit 3: two views */
} PipeKey;

static int fragment_specialization(void)
{
#ifdef NFSU2_OPENXR
    static int option=-1;
    if(option<0) {
        const char *e=getenv("NFSU2_VR_FRAGMENT_SPECIALIZE");
        option=e && *e=='1';
        LOGE("fragment check specialization %s\n",option?"enabled":"off");
    }
    return option;
#else
    return 0;
#endif
}
#ifdef NFSU2_OPENXR
static uint32_t s_fragment_variants[4];
#endif

typedef struct {
    PipeKey key;
    VkPipeline pipe;
    uint32_t hash;
    int next;
} PipeEnt;

#define VK_MAX_PIPE 4096
#define PIPE_BUCKETS 4096
static PipeEnt s_pipes[VK_MAX_PIPE];
static int s_pipe_head[PIPE_BUCKETS];
static int s_npipes;

static void pipes_forget_prog(int prog)
{
    int i;
    /* Rare (1024 live programs): drop every pipeline using it. */
    for (i = 0; i < s_npipes; i++) {
        PipeEnt *e = &s_pipes[i];
        if (e->pipe && e->key.prog == prog) {
            int *at = &s_pipe_head[e->hash], me = i + 1;
            while (*at) {
                if (*at == me) { *at = e->next; break; }
                at = &s_pipes[*at - 1].next;
            }
            garbage_add_pipe(e->pipe);               /* may still be in flight */
            e->pipe = VK_NULL_HANDLE;
        }
    }
}

static VkBlendFactor blend_factor(uint32_t v)
{
    switch (v) {
    case 0: return VK_BLEND_FACTOR_ZERO;
    case 1: return VK_BLEND_FACTOR_ONE;
    case 0x300: return VK_BLEND_FACTOR_SRC_COLOR;
    case 0x301: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 0x302: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 0x303: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 0x304: return VK_BLEND_FACTOR_DST_ALPHA;
    case 0x305: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case 0x306: return VK_BLEND_FACTOR_DST_COLOR;
    case 0x307: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 0x308: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case 0x8001: return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case 0x8002: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case 0x8003: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case 0x8004: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

static VkBlendOp blend_op(uint32_t v)
{
    switch (v) {
    case 0x800A: return VK_BLEND_OP_SUBTRACT;
    case 0x800B: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case 0x8007: return VK_BLEND_OP_MIN;
    case 0x8008: return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;
    }
}

/* vkpipes.bin: the program by what generated it, and the pipeline's own
 * state. */
#define PIPE_REC_MAGIC 0x4C504B56u              /* "VKPL" */
typedef struct {
    uint32_t   magic, pad;
    uint64_t   vkey;
    Nv2aPshKey pk;
    uint32_t   blend, bsrc, bdst, beq, cmask, depth, topo;
} PipeRec;
static uint32_t s_pipes_unsaved;

static void pipe_rec_append(const PipeKey *k)
{
    char path[256];
    PipeRec rec;
    FILE *f;
    if (s_prewarming || !cache_path(path, sizeof path, "vkpipes.bin"))
        return;
    memset(&rec, 0, sizeof rec);
    rec.magic = PIPE_REC_MAGIC;
    rec.pad = k->fragment_flags; /* formerly zero; preserves the record size */
    rec.vkey = s_prog[k->prog].vkey;
    rec.pk = s_prog[k->prog].pkey;
    rec.blend = k->blend; rec.bsrc = k->bsrc; rec.bdst = k->bdst; rec.beq = k->beq;
    rec.cmask = k->cmask; rec.depth = k->depth; rec.topo = k->topo;
    if ((f = fopen(path, "ab"))) {
        fwrite(&rec, sizeof rec, 1, f);
        fclose(f);
    }
}

static VkPipeline pipe_get(const PipeKey *k)
{
    const uint32_t *w = (const uint32_t *)k;
    uint32_t h = 2166136261u, i;
    int e;
    PipeEnt *pe;
    VkGraphicsPipelineCreateInfo ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkPipelineShaderStageCreateInfo st[2];
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState ba;
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    VkPipelineRenderingCreateInfo rci = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    VkFormat cfmt = VK_FORMAT_B8G8R8A8_UNORM;
    static const VkDynamicState dyn[] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_FRONT_FACE, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
        VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
#ifdef NFSU2_OPENXR
        VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR,
#endif
    };
    VkPipeline pipe = VK_NULL_HANDLE;
    VkProg *p = &s_prog[k->prog];
    const VkSpecializationMapEntry spec_entries[2] = {
        {0, 0, sizeof(VkBool32)}, {1, sizeof(VkBool32), sizeof(VkBool32)}
    };
    VkBool32 spec_values[2] = {!(k->fragment_flags & 1), !(k->fragment_flags & 2)};
    VkSpecializationInfo spec = {2, spec_entries, sizeof spec_values, spec_values};
    const VkSpecializationMapEntry vertex_entry = {2, 0, sizeof(VkBool32)};
    VkBool32 vertex_instances = !!(k->fragment_flags & 4);
    VkSpecializationInfo vertex_spec = {1, &vertex_entry, sizeof vertex_instances, &vertex_instances};

    for (i = 0; i < sizeof *k / 4; i++)
        h = (h ^ w[i]) * 16777619u;
    h &= PIPE_BUCKETS - 1;
    for (e = s_pipe_head[h]; e; e = s_pipes[e - 1].next)
        if (s_pipes[e - 1].pipe && !memcmp(&s_pipes[e - 1].key, k, sizeof *k))
            return s_pipes[e - 1].pipe;

    memset(st, 0, sizeof st);
    st[0].sType = st[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = p->vs;
    st[0].pName = "main";
    if(shared_instances()) st[0].pSpecializationInfo = &vertex_spec;
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = p->fs;
    st[1].pName = "main";
    st[1].pSpecializationInfo = &spec;
    ia.topology = k->topo == 0 ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                : k->topo == 1 ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                               : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    rs.depthClampEnable = s_have_depth_clamp ? VK_TRUE : VK_FALSE;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    memset(&ba, 0, sizeof ba);
    ba.blendEnable = k->blend ? VK_TRUE : VK_FALSE;
    ba.srcColorBlendFactor = ba.srcAlphaBlendFactor = blend_factor(k->bsrc);
    ba.dstColorBlendFactor = ba.dstAlphaBlendFactor = blend_factor(k->bdst);
    ba.colorBlendOp = ba.alphaBlendOp = blend_op(k->beq);
    ba.colorWriteMask = k->cmask;
    cb.attachmentCount = 1;
    cb.pAttachments = &ba;
    dy.dynamicStateCount = (uint32_t)(sizeof dyn / sizeof dyn[0]);
#ifdef NFSU2_OPENXR
    if (!s_foveation) --dy.dynamicStateCount;
    else ci.flags |= VK_PIPELINE_CREATE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
#endif
    dy.pDynamicStates = dyn;
    rci.colorAttachmentCount = 1;
    rci.pColorAttachmentFormats = &cfmt;
    rci.viewMask = k->fragment_flags & 8 ? 3 : 0;
    if (k->depth) {
        rci.depthAttachmentFormat = s_depth_fmt;
        rci.stencilAttachmentFormat = s_depth_fmt;
    }
    ci.pNext = &rci;
    ci.stageCount = 2;
    ci.pStages = st;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = s_layout;
    VT("vkCreateGraphicsPipelines prog %d blend %u cmask %X depth %u topo %u\n", k->prog, k->blend,
       k->cmask, k->depth, k->topo);
#ifdef NFSU2_PCVR
    uint64_t pcvr_pipeline_start=hz_now();
#endif
    VkResult pipeline_result=vkCreateGraphicsPipelines(s_dev, s_pcache, 1, &ci, NULL, &pipe);
#ifdef NFSU2_PCVR
    s_pcvr_pipeline_ns+=hz_now()-pcvr_pipeline_start;
    ++s_pcvr_pipelines;
#endif
    if (pipeline_result != VK_SUCCESS) {
        LOGE("pipeline creation failed\n");
        return VK_NULL_HANDLE;
    }
    if (s_npipes == VK_MAX_PIPE) {
        LOGE("pipeline table full\n");
        return pipe;                          /* leaks; never seen in practice */
    }
    pe = &s_pipes[s_npipes];
    pe->key = *k;
    pe->pipe = pipe;
    pe->hash = h;
    pe->next = s_pipe_head[h];
    s_pipe_head[h] = ++s_npipes;
    s_pipes_unsaved++;
    pipe_rec_append(k);
    return pipe;
}

static VkProg *prog_find(uint64_t vkey, const Nv2aPshKey *pk)
{
    int i;
    for (i = s_prog_head[prog_bucket(vkey, pk)]; i; i = s_prog[i - 1].next) {
        VkProg *c = &s_prog[i - 1];
        if (c->vs && c->vkey == vkey && !memcmp(&c->pkey, pk, sizeof *pk))
            return c;
    }
    return NULL;
}

static void *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    void *d = NULL;
    long n;
    *size = 0;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
        d = malloc((size_t)n);
        if (d && fread(d, 1, (size_t)n, f) == (size_t)n)
            *size = (size_t)n;
        else { free(d); d = NULL; }
    }
    fclose(f);
    return d;
}

static void pcache_save(void)
{
#ifdef NFSU2_PCVR
    uint64_t pcvr_cache_start=hz_now();
#endif
    char path[256], tmp[272];
    size_t n = 0;
    void *d;
    FILE *f;
    if (!s_pcache || !cache_path(path, sizeof path, "vkpipecache.bin"))
        return;
    if (vkGetPipelineCacheData(s_dev, s_pcache, &n, NULL) != VK_SUCCESS || !n)
        return;
    d = malloc(n);
    if (!d || vkGetPipelineCacheData(s_dev, s_pcache, &n, d) != VK_SUCCESS) { free(d); return; }
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if ((f = fopen(tmp, "wb"))) {
        int ok = fwrite(d, 1, n, f) == n;
        fclose(f);
        if (ok) {
            remove(path);                  /* FAT: rename does not replace */
            rename(tmp, path);
        }
    }
    free(d);
    s_pipes_unsaved = 0;
#ifdef NFSU2_PCVR
    s_pcvr_cache_ns+=hz_now()-pcvr_cache_start;
#endif
}

static void prewarm(void)
{
    char path[256];
    FILE *f;
    uint64_t t0 = hz_now();
    uint32_t nprog = 0, npipe = 0, bad = 0;
    ProgRec rec;
    PipeRec pr;

    spv_load();
    s_prewarming = 1;
    if (cache_path(path, sizeof path, "progcache.bin") && (f = fopen(path, "rb"))) {
        while (fread(&rec, sizeof rec, 1, f) == 1) {
            Nv2aRawBatch bb;
            if (rec.magic != PROG_REC_MAGIC || rec.slots > 136) { bad++; break; }
            prog_rec_batch(&rec, &bb, nprog);
            if (prog_get(&bb, &rec.pk))
                nprog++;
        }
        fclose(f);
    }
    if (cache_path(path, sizeof path, "vkpipes.bin") && (f = fopen(path, "rb"))) {
        while (fread(&pr, sizeof pr, 1, f) == 1) {
            VkProg *p;
            PipeKey k;
            if (pr.magic != PIPE_REC_MAGIC) { bad++; break; }
            if (!(p = prog_find(pr.vkey, &pr.pk)))
                continue;
            memset(&k, 0, sizeof k);
            k.prog = (int)(p - s_prog);
            k.blend = pr.blend; k.bsrc = pr.bsrc; k.bdst = pr.bdst; k.beq = pr.beq;
            k.cmask = pr.cmask; k.depth = pr.depth; k.topo = pr.topo;
            k.fragment_flags = fragment_specialization() ? pr.pad & 3u : 0;
            if(shared_instances()) k.fragment_flags |= pr.pad & 4u;
            if(s_multiview) k.fragment_flags |= pr.pad & 8u;
            if (pipe_get(&k))
                npipe++;
        }
        fclose(f);
    }
    s_prewarming = 0;
    fprintf(stderr, "  [VK] prewarm: %u programs (%u SPIR-V from cache), %u pipelines in %.0f ms%s\n",
            nprog, s_spv_hits, npipe, (hz_now() - t0) / 1e6, bad ? " (a file ends in a bad record)" : "");
    if (s_pipes_unsaved)
        pcache_save();
}

/* ── Context ──────────────────────────────────────────────────────── */

static int create_swapchain(void)
{
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR fmts[16];
    VkPresentModeKHR modes[8], mode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t nf = 16, nm = 8, i;
    VkSwapchainCreateInfoKHR ci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };

    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s_pd, s_surface, &caps) != VK_SUCCESS)
        return 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(s_pd, s_surface, &nf, fmts);
    vkGetPhysicalDeviceSurfacePresentModesKHR(s_pd, s_surface, &nm, modes);
    s_sc_format = fmts[0].format;
    for (i = 0; i < nf; i++)
        if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM || fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            s_sc_format = fmts[i].format;
            ci.imageColorSpace = fmts[i].colorSpace;
            break;
        }
    /* The game paces itself (vblank emulation): presenting must not block
     * it. Horizon's immediate mode does not tear (nvnflinger composes). */
    for (i = 0; i < nm; i++)
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = modes[i];
    for (i = 0; i < nm && mode == VK_PRESENT_MODE_FIFO_KHR; i++)
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) mode = modes[i];
    s_sc_extent = caps.currentExtent;
    if (s_sc_extent.width == 0xFFFFFFFFu) {
        s_sc_extent.width = 1280;
        s_sc_extent.height = 720;
    }
    ci.surface = s_surface;
    ci.minImageCount = caps.minImageCount < 3 ? 3 : caps.minImageCount;
    if (caps.maxImageCount && ci.minImageCount > caps.maxImageCount)
        ci.minImageCount = caps.maxImageCount;
    ci.imageFormat = s_sc_format;
    ci.imageExtent = s_sc_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = s_swapchain;
    {
        VkSwapchainKHR sc;
        if (vkCreateSwapchainKHR(s_dev, &ci, NULL, &sc) != VK_SUCCESS)
            return 0;
        if (s_swapchain)
            vkDestroySwapchainKHR(s_dev, s_swapchain, NULL);
        s_swapchain = sc;
    }
    s_sc_count = 8;
    vkGetSwapchainImagesKHR(s_dev, s_swapchain, &s_sc_count, s_sc_images);
    fprintf(stderr, "  [VK] swapchain %ux%u, %u images, format %d, present mode %d\n",
            s_sc_extent.width, s_sc_extent.height, s_sc_count, s_sc_format, mode);
    return 1;
}

static int ready(void)
{
    VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    const char *iext[8];
    uint32_t niext = 0, n, i;
    VkPhysicalDevice pds[8];
    VkPhysicalDeviceProperties pp;
    VkQueueFamilyProperties qf[8];
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT fvi =
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT };
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT feds =
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT };
    VkPhysicalDeviceFeatures have;
    const char *dext[5];
#ifdef NFSU2_OPENXR
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR fsr =
        { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR };
    VkPhysicalDeviceMultiviewFeatures mv = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
#endif
    uint32_t ndext = 0;
    const char *layers[1];
    uint32_t nlayers = 0;

    if (s_state)
        return s_state > 0;
    s_state = -1;
    s_trace = getenv("RECOMP_GL_TRACE") != NULL;
    {
        const char *e = getenv("RECOMP_VK_TRACE");
        s_vtrace = e ? atoi(e) : 0;
    }
    nv2a_shader_vk = 1;
    glslang_initialize_process();

    iext[niext++] = VK_KHR_SURFACE_EXTENSION_NAME;
#if defined(__SWITCH__)
    iext[niext++] = VK_NN_VI_SURFACE_EXTENSION_NAME;
#else
    {
        const char *e = getenv("RECOMP_VK_HEADLESS");
        const char *drv = getenv("SDL_VIDEODRIVER");
        s_headless = (e && *e == '1') || (drv && !strcmp(drv, "offscreen"));
#ifdef NFSU2_OPENXR
        if (quest_xr_enabled()) s_headless = 1; /* OpenXR owns presentation. */
#endif
        if (!s_headless) {
            if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
                s_headless = 1;
            } else {
                s_win = SDL_CreateWindow("NV2A (Vulkan)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                         1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
                if (!s_win) {
                    s_headless = 1;
                } else {
                    unsigned int ne = 0;
                    const char *names[8];
                    SDL_Vulkan_GetInstanceExtensions(s_win, &ne, NULL);
                    if (ne > 6) ne = 6;
                    SDL_Vulkan_GetInstanceExtensions(s_win, &ne, names);
                    niext = 0;
                    for (i = 0; i < ne; i++)
                        iext[niext++] = names[i];
                }
            }
        }
        if (s_headless)
            niext = 0;
    }
    {
        const char *v = getenv("RECOMP_VK_VALIDATION");
        if (v && *v == '1')
            layers[nlayers++] = "VK_LAYER_KHRONOS_validation";
    }
#endif
    ai.pApplicationName = "nfsu2x";
    ai.pEngineName = "xboxrecomp";
    ai.apiVersion = VK_API_VERSION_1_3;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = niext;
    ici.ppEnabledExtensionNames = iext;
    ici.enabledLayerCount = nlayers;
    ici.ppEnabledLayerNames = layers;
    VkResult instance_result;
#ifdef NFSU2_OPENXR
    if (quest_xr_enabled()) instance_result = quest_xr_vk_instance(&ici, &s_inst);
    else
#endif
    instance_result = vkCreateInstance(&ici, NULL, &s_inst);
    if (instance_result != VK_SUCCESS) {
        LOGE("vkCreateInstance failed\n");
        return 0;
    }
    n = 8;
    if (vkEnumeratePhysicalDevices(s_inst, &n, pds) != VK_SUCCESS || !n) {
        LOGE("no Vulkan device\n");
        return 0;
    }
    s_pd = pds[0];
#ifdef NFSU2_OPENXR
    if (quest_xr_enabled()) {
        s_pd = quest_xr_vk_physical(s_inst);
        if (!s_pd) return 0;
    }
#endif
    vkGetPhysicalDeviceProperties(s_pd, &pp);
#ifdef NFSU2_PCVR
    {
        const char *e=getenv("NFSU2_PCVR_TEXTURE_SLOTS");
        /* Leave headroom for surfaces, frame rings and deferred image frees.
         * The existing 256-MB texture-content budget still applies. */
        s_tex_limit=e && atoi(e)==2048 && pp.limits.maxMemoryAllocationCount>=2560 ? 2048 : 1024;
        LOGE("PCVR texture cache: %u entries, device allocation limit %u\n",
            s_tex_limit,pp.limits.maxMemoryAllocationCount);
    }
#endif
#ifdef NFSU2_OPENXR
    s_vertex_offset_limit=pp.limits.maxVertexInputAttributeOffset;
#endif
    fprintf(stderr, "  [VK] %s, Vulkan %u.%u.%u\n", pp.deviceName, VK_API_VERSION_MAJOR(pp.apiVersion),
            VK_API_VERSION_MINOR(pp.apiVersion), VK_API_VERSION_PATCH(pp.apiVersion));
    s_ubo_align = pp.limits.minUniformBufferOffsetAlignment;
    s_ubo_range = pp.limits.maxUniformBufferRange;
    {
        const char *e = getenv("RECOMP_GL_SCALE");
        double k = e ? strtod(e, NULL) : 1.0;
        uint32_t m = pp.limits.maxImageDimension2D;
        if (pp.limits.maxFramebufferWidth < m) m = pp.limits.maxFramebufferWidth;
        if (pp.limits.maxFramebufferHeight < m) m = pp.limits.maxFramebufferHeight;
        s_max_size = m ? m : 4096;
        if (!(k > 0.0)) k = 1.0;                    /* unset, 0 or garbage */
        s_scale = k < 0.5 ? 0.5 : k > 4.0 ? 4.0 : k;
        if (s_scale != 1.0)
            fprintf(stderr, "  [VK] rendering at %gx (RECOMP_GL_SCALE), surfaces up to %u\n",
                    s_scale, s_max_size);
    }
    if (s_ubo_align < 16) s_ubo_align = 16;
    vkGetPhysicalDeviceMemoryProperties(s_pd, &s_memprops);
    vkGetPhysicalDeviceFeatures(s_pd, &have);
    s_have_bc = have.textureCompressionBC;
    s_have_depth_clamp = have.depthClamp;
    {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(s_pd, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
        if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
            s_depth_fmt = VK_FORMAT_D32_SFLOAT_S8_UINT;
        {
            const char *e = getenv("RECOMP_GL_DXT");
            if (e && *e == '0')
                s_have_bc = 0;
        }
    }

    n = 8;
    vkGetPhysicalDeviceQueueFamilyProperties(s_pd, &n, qf);
    for (s_qfam = 0; s_qfam < n; s_qfam++)
        if (qf[s_qfam].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            break;
    if (s_qfam == n) {
        LOGE("no graphics queue\n");
        return 0;
    }
#ifdef NFSU2_OPENXR
    if (quest_xr_enabled()) {
        s_timestamp_bits = qf[s_qfam].timestampValidBits;
        s_timestamp_period = pp.limits.timestampPeriod;
        const char *e = getenv("NFSU2_VR_GPU_WAIT");
        s_xr_gpu_wait = e && *e == '1';
        e=getenv("NFSU2_VR_GPU_PROFILE");
        s_gpu_capture_control=e && *e=='2';
        s_gpu_profile=e && *e=='1';
        LOGE("XR GPU submission: %s, timestamp bits %u\n",
             s_xr_gpu_wait ? "synchronous comparison" : "two frame slots", s_timestamp_bits);
    }
#endif

#if defined(__SWITCH__)
    {
        NWindow *win = nwindowGetDefault();
        VkViSurfaceCreateInfoNN sci = { VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN };
        nwindowSetDimensions(win, 1280, 720);
        sci.window = win;
        if (vkCreateViSurfaceNN(s_inst, &sci, NULL, &s_surface) != VK_SUCCESS) {
            LOGE("vkCreateViSurfaceNN failed\n");
            return 0;
        }
    }
#else
    if (!s_headless && !SDL_Vulkan_CreateSurface(s_win, s_inst, &s_surface)) {
        LOGE("SDL_Vulkan_CreateSurface: %s\n", SDL_GetError());
        s_headless = 1;
    }
#endif

    qci.queueFamilyIndex = s_qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    f13.dynamicRendering = VK_TRUE;
    fvi.vertexInputDynamicState = VK_TRUE;
    feds.extendedDynamicState = VK_TRUE;
    f13.pNext = &fvi;
    f2.pNext = &f13;
    f2.features.textureCompressionBC = s_have_bc ? VK_TRUE : VK_FALSE;
    f2.features.depthClamp = s_have_depth_clamp ? VK_TRUE : VK_FALSE;
    if (!s_headless)
        dext[ndext++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    dext[ndext++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
    dext[ndext++] = VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME;
#ifdef NFSU2_OPENXR
    const char *foveation = getenv("NFSU2_VR_FOVEATION");
    if (xr_stereo() && foveation && *foveation == '1') {
        uint32_t count=0;
        vkEnumerateDeviceExtensionProperties(s_pd, NULL, &count, NULL);
        VkExtensionProperties *extensions=calloc(count, sizeof *extensions);
        int supported=0;
        if (extensions && vkEnumerateDeviceExtensionProperties(s_pd, NULL, &count, extensions)==VK_SUCCESS)
            for(uint32_t j=0;j<count;++j)
                if(!strcmp(extensions[j].extensionName,VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME)) supported=1;
        free(extensions);
        if (supported) {
            VkPhysicalDeviceFeatures2 features={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext=&fsr;
            vkGetPhysicalDeviceFeatures2(s_pd,&features);
            VkPhysicalDeviceFragmentShadingRatePropertiesKHR rates=
                {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR};
            VkPhysicalDeviceProperties2 properties={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            properties.pNext=&rates;
            vkGetPhysicalDeviceProperties2(s_pd,&properties);
            VkFormatProperties format;
            vkGetPhysicalDeviceFormatProperties(s_pd,VK_FORMAT_R8_UINT,&format);
            int rate2=0;
            PFN_vkGetPhysicalDeviceFragmentShadingRatesKHR get_rates=
                (void *)vkGetInstanceProcAddr(s_inst,"vkGetPhysicalDeviceFragmentShadingRatesKHR");
            uint32_t num=0;
            if(get_rates && get_rates(s_pd,&num,NULL)==VK_SUCCESS && num) {
                VkPhysicalDeviceFragmentShadingRateKHR *available=calloc(num,sizeof *available);
                if(available) {
                    for(uint32_t j=0;j<num;++j) available[j].sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_KHR;
                    if(get_rates(s_pd,&num,available)==VK_SUCCESS)
                        for(uint32_t j=0;j<num;++j)
                            if(available[j].fragmentSize.width==2 && available[j].fragmentSize.height==2 &&
                               (available[j].sampleCounts&VK_SAMPLE_COUNT_1_BIT)) rate2=1;
                    free(available);
                }
            }
            s_foveation=fsr.pipelineFragmentShadingRate && fsr.attachmentFragmentShadingRate && rate2 &&
                (format.optimalTilingFeatures&VK_FORMAT_FEATURE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR) &&
                rates.minFragmentShadingRateAttachmentTexelSize.width && rates.minFragmentShadingRateAttachmentTexelSize.height;
            if(s_foveation) {
                s_rate_texel=rates.minFragmentShadingRateAttachmentTexelSize;
                fsr.primitiveFragmentShadingRate=VK_FALSE;
                fvi.pNext=&fsr;
                dext[ndext++]=VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME;
            }
        }
        LOGE("XR fixed foveation %s\n",s_foveation?"enabled":"unsupported, using full shading");
    }
    const char *multiview = getenv("NFSU2_VR_MULTIVIEW");
    if (xr_stereo() && multiview && *multiview=='1') {
        const char *instances=getenv("RECOMP_VK_INSTANCE");
        VkPhysicalDeviceFeatures2 features={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        VkPhysicalDeviceMultiviewProperties limits={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES};
        VkPhysicalDeviceProperties2 properties={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        features.pNext=&mv; properties.pNext=&limits;
        vkGetPhysicalDeviceFeatures2(s_pd,&features);
        vkGetPhysicalDeviceProperties2(s_pd,&properties);
        s_multiview=mv.multiview && limits.maxMultiviewViewCount>=2 &&
            instances && *instances=='0' && !state_cache_enabled();
        if(s_multiview) {
            mv.multiviewGeometryShader=VK_FALSE;
            mv.multiviewTessellationShader=VK_FALSE;
            if(s_foveation) fsr.pNext=&mv; else fvi.pNext=&mv;
        }
        LOGE("XR multiview %s (max views %u); one CPU draw for both eyes\n",
             s_multiview?"enabled":"unavailable or conflicting settings",limits.maxMultiviewViewCount);
    }
#endif
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = ndext;
    dci.ppEnabledExtensionNames = dext;
    (void)feds;
    VkResult device_result;
#ifdef NFSU2_OPENXR
    if (quest_xr_enabled()) device_result = quest_xr_vk_device(s_pd, &dci, &s_dev);
    else
#endif
    device_result = vkCreateDevice(s_pd, &dci, NULL, &s_dev);
    if (device_result != VK_SUCCESS) {
        LOGE("vkCreateDevice failed (needs push descriptors, dynamic vertex input, Vulkan 1.3)\n");
        return 0;
    }
    vkGetDeviceQueue(s_dev, s_qfam, 0, &s_queue);
#define GET(p, name) p = (void *)vkGetDeviceProcAddr(s_dev, name); if (!p) { LOGE("missing %s\n", name); return 0; }
    GET(p_push_desc, "vkCmdPushDescriptorSetKHR");
    GET(p_vertex_input, "vkCmdSetVertexInputEXT");
    GET(p_begin_rendering, "vkCmdBeginRendering");
    GET(p_end_rendering, "vkCmdEndRendering");
    GET(p_cull_mode, "vkCmdSetCullMode");
    GET(p_front_face, "vkCmdSetFrontFace");
    GET(p_topology, "vkCmdSetPrimitiveTopology");
    GET(p_depth_test, "vkCmdSetDepthTestEnable");
    GET(p_depth_write, "vkCmdSetDepthWriteEnable");
    GET(p_depth_op, "vkCmdSetDepthCompareOp");
    GET(p_stencil_test, "vkCmdSetStencilTestEnable");
    GET(p_stencil_op, "vkCmdSetStencilOp");
#ifdef NFSU2_OPENXR
    if(s_foveation) { GET(p_shading_rate,"vkCmdSetFragmentShadingRateKHR"); }
#endif
#undef GET

    if (!s_headless && !create_swapchain()) {
        LOGE("swapchain creation failed; running headless\n");
        s_headless = 1;
    }

    {
        VkDescriptorSetLayoutBinding b[7];
        VkDescriptorSetLayoutCreateInfo lci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        VkPipelineLayoutCreateInfo pli = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        VkPipelineCacheCreateInfo pci = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        memset(b, 0, sizeof b);
#ifdef NFSU2_OPENXR
        const char *dynamic=getenv("NFSU2_VR_DYNAMIC_DESCRIPTORS");
        s_dynamic_descriptors=quest_xr_enabled() && dynamic && *dynamic=='1';
        LOGE("immutable dynamic descriptor sets %s\n",s_dynamic_descriptors?"enabled":"off");
#endif
        for (i = 0; i < 7; i++) {
            b[i].binding = i;
            b[i].descriptorCount = 1;
            b[i].descriptorType = i < 2 || i == 6 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                  : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
#ifdef NFSU2_OPENXR
            if(s_dynamic_descriptors && (i<2 || i==6)) b[i].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
#endif
            b[i].stageFlags = i == 0 || i == 6 ? VK_SHADER_STAGE_VERTEX_BIT
                                               : VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        lci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
#ifdef NFSU2_OPENXR
        if(s_dynamic_descriptors) lci.flags=0;
#endif
        lci.bindingCount = 7;
        lci.pBindings = b;
        if (vkCreateDescriptorSetLayout(s_dev, &lci, NULL, &s_dsl) != VK_SUCCESS)
            return 0;
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &s_dsl;
        if (vkCreatePipelineLayout(s_dev, &pli, NULL, &s_layout) != VK_SUCCESS)
            return 0;
#ifdef NFSU2_OPENXR
        create_push_template();
#endif
        {
            char path[256];
            size_t n = 0;
            void *d = cache_path(path, sizeof path, "vkpipecache.bin") ? read_file(path, &n) : NULL;
            pci.initialDataSize = n;
            pci.pInitialData = d;
            if (vkCreatePipelineCache(s_dev, &pci, NULL, &s_pcache) != VK_SUCCESS) {
                pci.initialDataSize = 0;       /* another driver's or a damaged file */
                pci.pInitialData = NULL;
                vkCreatePipelineCache(s_dev, &pci, NULL, &s_pcache);
            }
            if (n)
                fprintf(stderr, "  [VK] pipeline cache: %u KB from %s\n", (unsigned)(n >> 10), path);
            free(d);
        }
    }

    for (i = 0; i < VK_FRAMES; i++) {
        Frame *f = &s_fr[i];
        VkCommandPoolCreateInfo cpi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        VkMemoryRequirements rq;
        cpi.queueFamilyIndex = s_qfam;
        vkCreateCommandPool(s_dev, &cpi, NULL, &f->pool);
        cai.commandPool = f->pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        vkAllocateCommandBuffers(s_dev, &cai, &f->cb);
        vkCreateFence(s_dev, &fci, NULL, &f->fence);
#ifdef NFSU2_OPENXR
        if (s_timestamp_bits) {
            VkQueryPoolCreateInfo query = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            query.queryType = VK_QUERY_TYPE_TIMESTAMP;
            query.queryCount = s_gpu_profile || s_gpu_capture_control?2+2*XR_GPU_PASS_LIMIT:2;
            if (vkCreateQueryPool(s_dev, &query, NULL, &f->timestamps) != VK_SUCCESS)
                f->timestamps = VK_NULL_HANDLE;
        }
#endif
        vkCreateSemaphore(s_dev, &sci, NULL, &f->acquired);
        vkCreateSemaphore(s_dev, &sci, NULL, &f->rendered);
        bci.size = RING_BYTES;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (vkCreateBuffer(s_dev, &bci, NULL, &f->ring) != VK_SUCCESS)
            return 0;
        vkGetBufferMemoryRequirements(s_dev, f->ring, &rq);
        f->ring_mem = alloc_mem(rq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!f->ring_mem || vkBindBufferMemory(s_dev, f->ring, f->ring_mem, 0) != VK_SUCCESS ||
            vkMapMemory(s_dev, f->ring_mem, 0, RING_BYTES, 0, (void **)&f->ring_ptr) != VK_SUCCESS) {
            LOGE("frame ring allocation failed\n");
            return 0;
        }
    }
    s_fi = 0;
    frame_begin();
    if (!make_image(1, 1, VK_FORMAT_B8G8R8A8_UNORM,
                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT, &s_dummy_img, &s_dummy_view, &s_dummy_mem))
        return 0;
    clear_image_color(s_dummy_img, 0, 0, 0, 0);
    if (!make_image_n(1, 1, 6, VK_FORMAT_B8G8R8A8_UNORM,
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT, &s_dummy_cube_img, &s_dummy_cube_view,
                      &s_dummy_cube_mem))
        return 0;
    clear_image_color(s_dummy_cube_img, 0, 0, 0, 0);
#ifdef NFSU2_OPENXR
    if (quest_xr_enabled() && !quest_xr_start(s_inst, s_pd, s_dev, s_qfam)) return 0;
#endif
    s_state = 1;
    fprintf(stderr, "  [VK] ready%s: BC %s, depth %s, depth clamp %s, UBO align %u\n",
            s_headless ? " (headless)" : "", s_have_bc ? "yes" : "no",
            s_depth_fmt == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : "D32S8",
            s_have_depth_clamp ? "yes" : "no", (unsigned)s_ubo_align);
    prewarm();
#ifdef NFSU2_OPENXR
    if (quest_xr_enabled()) quest_xr_begin();
#endif
    return 1;
}

/* Submit what is recorded and wait for it, then keep recording in the same
 * frame (ring full). */
static void flush_frame_wait(void)
{
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    end_rendering();
    s_rt = NULL;
#ifdef NFSU2_OPENXR
    gpu_timer_end();
    uint64_t t0 = hz_now();
#endif
    vkEndCommandBuffer(s_cb);
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s_cb;
    if (vkQueueSubmit(s_queue, 1, &si, s_f->fence) != VK_SUCCESS) {
        LOGE("mid-frame submission failed\n");
        abort(); /* Do not wait forever on an unsignalled fence. */
    }
    vkWaitForFences(s_dev, 1, &s_f->fence, VK_TRUE, UINT64_MAX);
#ifdef NFSU2_OPENXR
    xr_add_time(&s_xr_wait_ns,hz_now()-t0,1);
    ++s_xr_ring_flushes;
    gpu_timer_collect();
#endif
    vkResetFences(s_dev, 1, &s_f->fence);
    garbage_free(s_f);
#ifdef NFSU2_OPENXR
    descriptor_frame_reset(); /* mid-frame chunk has also finished on GPU */
#endif
    s_f->ring_off = 0;
    vkResetCommandPool(s_dev, s_f->pool, 0);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(s_cb, &bi);
    s_cb_gen++;
    s_dyn_valid = 0;
#ifdef NFSU2_OPENXR
    gpu_timer_begin();
#endif
}

/* ── Render state ─────────────────────────────────────────────────── */

static float byte_f(uint32_t v, int shift) { return (float)((v >> shift) & 0xFF) / 255.0f; }
static void argb_vec4(uint32_t v, float out[4])
{
    out[0] = byte_f(v, 16); out[1] = byte_f(v, 8);
    out[2] = byte_f(v, 0);  out[3] = byte_f(v, 24);
}

static VkCompareOp cmp_op(uint32_t v, uint32_t dflt)
{
    if (!v) v = dflt;
    if (v < 0x200 || v > 0x207) v = dflt;
    return (VkCompareOp)(v - 0x200);
}

static VkStencilOp stencil_op(uint32_t v)
{
    switch (v) {
    case 0x1E01: return VK_STENCIL_OP_REPLACE;
    case 0x1E02: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case 0x1E03: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case 0x150A: return VK_STENCIL_OP_INVERT;
    case 0x8507: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case 0x8508: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default: return VK_STENCIL_OP_KEEP;         /* KEEP, and 0 (never set) */
    }
}

/* Dynamic state for a draw, set only when it changed. */
typedef struct {
    uint32_t cull, cullmode, front, topo;
    uint32_t dtest, dwrite, dop;
    uint32_t stest, sfail, szfail, spass, sop, smask, swrite, sref;
    uint32_t bcolor;
} DynState;
static DynState s_dyn;

static void apply_dynamic(const uint32_t *r, int has_depth, VkPrimitiveTopology topo)
{
    DynState d;
    memset(&d, 0, sizeof d);
    d.cull = r[0x308 / 4] != 0;
    d.cullmode = r[0x39C / 4];
    d.front = r[0x3A0 / 4];
    d.topo = (uint32_t)topo;
    d.dtest = has_depth && r[0x30C / 4];
    d.dwrite = has_depth && r[0x35C / 4];
    d.dop = r[0x354 / 4];
    d.stest = has_depth && r[0x32C / 4];
    if (d.stest) {
        d.sfail = r[0x370 / 4]; d.szfail = r[0x374 / 4]; d.spass = r[0x378 / 4];
        d.sop = r[0x364 / 4]; d.smask = r[0x36C / 4] & 0xFF; d.swrite = r[0x360 / 4] & 0xFF;
        d.sref = r[0x368 / 4] & 0xFF;
    }
    d.bcolor = r[0x34C / 4];
    if (s_dyn_valid && !memcmp(&d, &s_dyn, sizeof d))
        return;
    if (!s_dyn_valid || d.cull != s_dyn.cull || d.cullmode != s_dyn.cullmode) {
        VkCullModeFlags cm = VK_CULL_MODE_NONE;
        if (d.cull)
            cm = d.cullmode == 0x404 ? VK_CULL_MODE_FRONT_BIT
               : d.cullmode == 0x408 ? VK_CULL_MODE_FRONT_AND_BACK : VK_CULL_MODE_BACK_BIT;
        p_cull_mode(s_cb, cm);
    }
    if (!s_dyn_valid || d.front != s_dyn.front)
        /* NV2A winding is as seen on screen, rows top-first -- Vulkan's
         * framebuffer orientation, so CW is CW. nv2a_gl swaps because its
         * window y runs the other way. Swapped here too, every culled draw
         * (29 a frame in the main menu: headlight glass, grille) lost its
         * front faces and kept its back ones. */
        p_front_face(s_cb, d.front == 0x901 ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
    if (!s_dyn_valid || d.topo != s_dyn.topo)
        p_topology(s_cb, topo);
    if (!s_dyn_valid || d.dtest != s_dyn.dtest) p_depth_test(s_cb, d.dtest);
    if (!s_dyn_valid || d.dwrite != s_dyn.dwrite) p_depth_write(s_cb, d.dwrite);
    if (!s_dyn_valid || d.dop != s_dyn.dop) p_depth_op(s_cb, cmp_op(d.dop, 0x203));
    if (!s_dyn_valid || d.stest != s_dyn.stest) p_stencil_test(s_cb, d.stest);
    if (!s_dyn_valid || memcmp(&d.sfail, &s_dyn.sfail, 7 * sizeof(uint32_t))) {
        p_stencil_op(s_cb, VK_STENCIL_FACE_FRONT_AND_BACK, stencil_op(d.sfail), stencil_op(d.spass),
                     stencil_op(d.szfail), cmp_op(d.sop, 0x207));
        vkCmdSetStencilCompareMask(s_cb, VK_STENCIL_FACE_FRONT_AND_BACK, d.smask);
        vkCmdSetStencilWriteMask(s_cb, VK_STENCIL_FACE_FRONT_AND_BACK, d.swrite);
        vkCmdSetStencilReference(s_cb, VK_STENCIL_FACE_FRONT_AND_BACK, d.sref);
    }
    if (!s_dyn_valid || d.bcolor != s_dyn.bcolor) {
        float bc[4];
        argb_vec4(d.bcolor, bc);
        vkCmdSetBlendConstants(s_cb, bc);
    }
    s_dyn = d;
    s_dyn_valid = 1;
}

static void psh_key(const uint32_t *r, Nv2aPshKey *k)
{
    int i;
    memset(k, 0, sizeof *k);
    for (i = 0; i < 8; i++) {
        k->color_icw[i] = r[0xAC0 / 4 + i];
        k->color_ocw[i] = r[0x1E40 / 4 + i];
        k->alpha_icw[i] = r[0x260 / 4 + i];
        k->alpha_ocw[i] = r[0xAA0 / 4 + i];
    }
    k->control = r[0x1E60 / 4];
    k->final0 = r[0x288 / 4];
    k->final1 = r[0x28C / 4];
    k->shader_program = r[0x1E70 / 4];
    k->other_input = r[0x1E78 / 4];
}

static uint32_t zmax_of(const uint32_t *r)
{
    return (((r[0x208 / 4] >> 4) & 0xF) == 1) ? 0xFFFFu : 0xFFFFFFu;
}

/* NV097 primitive -> Vulkan topology and (possibly rewritten) indices. */
static uint32_t *s_prim_idx;
static size_t    s_prim_cap;

static int prim_indices(const Nv2aRawBatch *b, const uint32_t **idx, uint32_t *n,
                        VkPrimitiveTopology *topo, uint32_t *cls)
{
    uint32_t i, m = 0;
    *idx = b->indices;
    *n = b->index_count;
    switch (b->prim) {
    case 1: *topo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; *cls = 0; return 1;
    case 2: *topo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; *cls = 1; return 1;
    case 4: *topo = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; *cls = 1; return 1;
    case 5: *topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; *cls = 2; return 1;
    case 6: case 9: *topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; *cls = 2; return 1;
    case 7: case 10: *topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN; *cls = 2; return 1;
    case 3:                                      /* LINE_LOOP: strip + first */
    case 8: {                                    /* QUADS -> triangles */
        size_t need = b->prim == 3 ? (size_t)b->index_count + 1 : (size_t)b->index_count / 4 * 6;
        if (need > s_prim_cap) {
            free(s_prim_idx);
            s_prim_cap = need + 1024;
            s_prim_idx = (uint32_t *)malloc(s_prim_cap * 4);
        }
        if (!s_prim_idx) { *n = 0; return 0; }
        if (b->prim == 3) {
            if (!b->index_count) { *n = 0; return 0; }
            memcpy(s_prim_idx, b->indices, (size_t)b->index_count * 4);
            s_prim_idx[b->index_count] = b->indices[0];
            m = b->index_count + 1;
            *topo = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; *cls = 1;
        } else {
            for (i = 0; i + 3 < b->index_count; i += 4) {
                const uint32_t *q = b->indices + i;
                s_prim_idx[m++] = q[0]; s_prim_idx[m++] = q[1]; s_prim_idx[m++] = q[2];
                s_prim_idx[m++] = q[0]; s_prim_idx[m++] = q[2]; s_prim_idx[m++] = q[3];
            }
            *topo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; *cls = 2;
        }
        *idx = s_prim_idx;
        *n = m;
        return 1;
    }
    default:
        *n = 0;
        return 0;
    }
}

/* ── Vertices ─────────────────────────────────────────────────────── */

typedef struct { const uint8_t *lo, *hi; VkDeviceSize at; } VRun;

static uint32_t elem_bytes(uint32_t type, uint32_t size)
{
    switch (type) {
    case 0: return 4;
    case 2: return 4 * size;
    case 4: return size;
    default: return 2 * size;
    }
}

static uint32_t vertex_runs(const Nv2aRawBatch *b, uint16_t direct, VRun *run, int *of_run)
{
    uint32_t nv = b->vertex_count, nrun = 0, a, i;
    for (a = 0; a < NV2A_RAW_ATTRS; a++) {
        const uint8_t *lo, *hi;
        if (!(direct & (1u << a)))
            continue;
        lo = (const uint8_t *)((uintptr_t)b->direct[a].ptr & ~(uintptr_t)3);
        hi = b->direct[a].ptr + (size_t)(nv - 1) * b->direct[a].stride
           + elem_bytes(b->direct[a].type, b->direct[a].size);
        for (i = 0; i < nrun; i++)
            if (lo <= run[i].hi && run[i].lo <= hi)
                break;
        if (i == nrun) {
            run[nrun].lo = lo;
            run[nrun].hi = hi;
            nrun++;
        } else {
            if (lo < run[i].lo) run[i].lo = lo;
            if (hi > run[i].hi) run[i].hi = hi;
        }
        of_run[a] = (int)i;
    }
    for (i = 0; i + 1 < nrun; ) {
        uint32_t j;
        for (j = i + 1; j < nrun; j++)
            if (run[j].lo <= run[i].hi && run[i].lo <= run[j].hi)
                break;
        if (j == nrun) { i++; continue; }
        if (run[j].lo < run[i].lo) run[i].lo = run[j].lo;
        if (run[j].hi > run[i].hi) run[i].hi = run[j].hi;
        for (a = 0; a < NV2A_RAW_ATTRS; a++) {
            if (!(direct & (1u << a)))
                continue;
            if (of_run[a] == (int)j) of_run[a] = (int)i;
            else if (of_run[a] == (int)(nrun - 1)) of_run[a] = (int)j;
        }
        run[j] = run[nrun - 1];
        nrun--;
    }
    return nrun;
}

/* The Vulkan vertex format of a direct attribute, or UNDEFINED when the
 * device cannot fetch it (then it is converted to float4 here). */
static VkFormat s_vfmt_cache[8][5];
static int      s_vfmt_known[8][5];

static VkFormat vertex_format(uint32_t type, uint32_t size)
{
    static const VkFormat f_float[5] = { 0, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT,
                                         VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };
    static const VkFormat f_snorm16[5] = { 0, VK_FORMAT_R16_SNORM, VK_FORMAT_R16G16_SNORM,
                                           VK_FORMAT_R16G16B16_SNORM, VK_FORMAT_R16G16B16A16_SNORM };
    static const VkFormat f_unorm8[5] = { 0, VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM,
                                          VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
    static const VkFormat f_sscaled16[5] = { 0, VK_FORMAT_R16_SSCALED, VK_FORMAT_R16G16_SSCALED,
                                             VK_FORMAT_R16G16B16_SSCALED, VK_FORMAT_R16G16B16A16_SSCALED };
    VkFormat f;
    if (size < 1 || size > 4 || type > 7)
        return VK_FORMAT_UNDEFINED;
    if (s_vfmt_known[type][size])
        return s_vfmt_cache[type][size];
    switch (type) {
    case 0: f = VK_FORMAT_B8G8R8A8_UNORM; break;
    case 1: f = f_snorm16[size]; break;
    case 2: f = f_float[size]; break;
    case 4: f = f_unorm8[size]; break;
    case 5: f = f_sscaled16[size]; break;
    default: f = VK_FORMAT_UNDEFINED; break;
    }
    if (f != VK_FORMAT_UNDEFINED) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(s_pd, f, &fp);
        if (!(fp.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) {
            if (s_trace)
                LOGE("vertex format %d (type %u size %u) not supported: converted\n", f, type, size);
            f = VK_FORMAT_UNDEFINED;
        }
    }
    s_vfmt_known[type][size] = 1;
    s_vfmt_cache[type][size] = f;
    return f;
}

/* One attribute of vertex v, as the NV2A reads it, into float4. */
static void attr_to_float4(const Nv2aRawBatch *b, uint32_t a, uint32_t v, float out[4])
{
    const uint8_t *p = b->direct[a].ptr + (size_t)v * b->direct[a].stride;
    uint32_t size = b->direct[a].size, c;
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch (b->direct[a].type) {
    case 0:                                          /* D3DCOLOR: B,G,R,A */
        out[0] = p[2] / 255.0f; out[1] = p[1] / 255.0f; out[2] = p[0] / 255.0f; out[3] = p[3] / 255.0f;
        break;
    case 1:
        for (c = 0; c < size && c < 4; c++) {
            int16_t s; memcpy(&s, p + 2 * c, 2);
            out[c] = s / 32767.0f < -1.0f ? -1.0f : s / 32767.0f;
        }
        break;
    case 2:
        for (c = 0; c < size && c < 4; c++) memcpy(&out[c], p + 4 * c, 4);
        break;
    case 4:
        for (c = 0; c < size && c < 4; c++) out[c] = p[c] / 255.0f;
        break;
    default:
        for (c = 0; c < size && c < 4; c++) { int16_t s; memcpy(&s, p + 2 * c, 2); out[c] = (float)s; }
        break;
    }
}

#ifdef NFSU2_OPENXR
/* Immutable vertex/index bytes for ONE snapshotted draw, shared only with its
 * other eye. A command-buffer generation change means the ring may have been
 * recycled: discard this reference and upload the CPU snapshot normally. All
 * Vulkan vertex/index bindings are still emitted for every draw. */
typedef struct XrGeometry {
    VkVertexInputBindingDescription2EXT bindings[NV2A_RAW_ATTRS];
    VkVertexInputAttributeDescription2EXT attributes[NV2A_RAW_ATTRS];
    VkDeviceSize offsets[NV2A_RAW_ATTRS], vertex_at, vertex_bytes, index_at, constants_at;
    VkBuffer buffer;
    VkIndexType index_type;
    uint32_t generation, index_count, binding_count;
    int valid, constants_valid;
    /* A member keeps its own constants offset but shares the first upload.
     * CPU snapshots remain available if that upload expires before replay. */
    struct XrGeometry *instance_root;
    uint32_t instance_count;
    VkDeviceSize original_offsets[NV2A_RAW_ATTRS];
} XrGeometry;
static XrGeometry *s_xr_geometry;
static const uint32_t *s_xr_immutable_regs;
static const Nv2aRawBatch *s_xr_immutable_batch;
static int s_cpu_instance_mode=-1, s_xr_snapshot_source;
static uint32_t s_cpu_instance_verified, s_cpu_instance_failed;
static int cpu_instance_mode(void)
{
    if(s_cpu_instance_mode<0) {
        const char *e=getenv("NFSU2_VR_INSTANCE_CPU_CHECK");
        s_cpu_instance_mode=shared_instances() && e && (*e=='1' || *e=='3') ? *e-'0' : 0;
        LOGE("CPU snapshot instance comparison mode %d\n",s_cpu_instance_mode);
    }
    return s_cpu_instance_mode;
}
static int s_geometry_mode = -1;
static uint32_t s_geometry_reused, s_geometry_verified, s_geometry_failed, s_geometry_expired;
static uint32_t s_geometry_race_verified;
static uint64_t s_geometry_saved;
static int s_uniform_mode=-1;
static uint32_t s_uniform_reused, s_uniform_verified, s_uniform_failed, s_uniform_race_verified;
static VkDeviceSize s_instance_vertex_offsets[NV2A_RAW_ATTRS];
static uint32_t s_instance_checked, s_instance_rejected;
static uint32_t s_shared_instance_appends;

static int xr_checked_instances(void)
{
    static int on=-1;
    if(on<0) {
        const char *e=getenv("NFSU2_VR_CHECKED_INSTANCES");
        on=e && *e=='1';
    }
    return on;
}

static int xr_uniform_mode(void)
{
    if(s_uniform_mode<0) {
        const char *e=getenv("NFSU2_VR_UNIFORM_REUSE");
        s_uniform_mode=e && *e>='1' && *e<='3' ? *e-'0':0;
        LOGE("stereo constant upload mode %d (1=reuse, 2=verify, 3=verify then reuse)\n",s_uniform_mode);
    }
    return s_uniform_mode;
}

static int xr_geometry_mode(void)
{
    if (s_geometry_mode < 0) {
        const char *e = getenv("NFSU2_VR_GEOMETRY_REUSE");
        s_geometry_mode = e && *e >= '1' && *e <= '3' ? *e - '0' : 0;
        LOGE("stereo geometry upload mode %d (0=original, 1=reuse, 2=compare bytes, 3=verify then reuse)\n", s_geometry_mode);
    }
    return s_geometry_mode;
}

static int xr_geometry_live(void)
{
    return s_xr_geometry && s_eye == 1 && s_xr_geometry->valid &&
        s_xr_geometry->generation == s_cb_gen && s_xr_geometry->buffer == s_f->ring;
}

static void xr_geometry_mismatch(const char *part)
{
    ++s_geometry_failed;
    LOGE("stereo geometry %s mismatch: disabling upload reuse\n", part);
    s_geometry_mode = 0;
    s_xr_geometry->valid = 0;
}
#endif

static int upload_vertices(const Nv2aRawBatch *b)
{
    static uint32_t layout_gen;
    static uint32_t last_binding_count;
    static VkVertexInputBindingDescription2EXT last_bind[NV2A_RAW_ATTRS];
    static VkVertexInputAttributeDescription2EXT last_attr[NV2A_RAW_ATTRS];
    VRun run[NV2A_RAW_ATTRS];
    int of_run[NV2A_RAW_ATTRS];
    uint32_t slot[NV2A_RAW_ATTRS], np = 0, nrun, a, i, v;
    uint32_t nv = b->vertex_count;
    uint32_t binding_count=NV2A_RAW_ATTRS;
    uint16_t direct = 0, conv, cdirect = 0;
    VkDeviceSize bytes = 0, pack_at, const_at, at;
    VkFormat fmt[NV2A_RAW_ATTRS];
    uint8_t *dst;
    VkVertexInputBindingDescription2EXT bind[NV2A_RAW_ATTRS];
    VkVertexInputAttributeDescription2EXT attr[NV2A_RAW_ATTRS];
    VkBuffer bufs[NV2A_RAW_ATTRS];
    VkDeviceSize offs[NV2A_RAW_ATTRS];
    void *map;

    if (!nv)
        return 0;
#ifdef NFSU2_OPENXR
    if (xr_geometry_live() && s_geometry_mode == 1) {
        XrGeometry *g = s_xr_geometry;
        for (a = 0; a < NV2A_RAW_ATTRS; ++a) bufs[a] = g->buffer;
        p_vertex_input(s_cb, g->binding_count, g->bindings, NV2A_RAW_ATTRS, g->attributes);
        s_vertex_layout_state.valid=0;
        vkCmdBindVertexBuffers(s_cb, 0, g->binding_count, bufs, g->offsets);
        s_vertex_bindings_before+=NV2A_RAW_ATTRS; s_vertex_bindings_after+=g->binding_count;
        ++s_layout_sets;
        if(shared_instances()) memcpy(s_instance_vertex_offsets,g->original_offsets,sizeof g->original_offsets);
        return 1;
    }
#endif
    for (a = 0; a < NV2A_RAW_ATTRS; a++) {
        fmt[a] = VK_FORMAT_UNDEFINED;
        if (!(b->attr_direct & (1u << a)))
            continue;
        fmt[a] = vertex_format(b->direct[a].type, b->direct[a].size);
        if (fmt[a] != VK_FORMAT_UNDEFINED)
            direct |= (uint16_t)(1u << a);
        else
            cdirect |= (uint16_t)(1u << a);          /* direct data, converted here */
    }
    conv = b->attr_present & (uint16_t)~direct;
    nrun = vertex_runs(b, direct, run, of_run);
    for (i = 0; i < nrun; i++) {
        run[i].at = bytes;
        bytes += ((VkDeviceSize)(run[i].hi - run[i].lo) + 15) & ~(VkDeviceSize)15;
    }
    for (a = 0; a < NV2A_RAW_ATTRS; a++)
        if (conv & (1u << a))
            slot[np++] = a;
    pack_at = bytes;
    bytes += (VkDeviceSize)nv * np * 16;
    const_at = bytes;
    bytes += NV2A_RAW_ATTRS * 16;
    at = ring_alloc(bytes, 16, &map);
    dst = (uint8_t *)map;
    if (!dst)
        return 0;
    for (i = 0; i < nrun; i++)
        memcpy(dst + run[i].at, run[i].lo, (size_t)(run[i].hi - run[i].lo));
    if (np) {
        float *out = (float *)(dst + pack_at);
        for (v = 0; v < nv; v++)
            for (i = 0; i < np; i++) {
                float *o = out + ((size_t)v * np + i) * 4;
                if (cdirect & (1u << slot[i]))
                    attr_to_float4(b, slot[i], v, o);
                else
                    memcpy(o, b->attrs + ((size_t)v * NV2A_RAW_ATTRS + slot[i]) * 4, 16);
            }
    }
    memcpy(dst + const_at, b->attr_const, NV2A_RAW_ATTRS * 16);

    for (a = 0; a < NV2A_RAW_ATTRS; a++) {
        memset(&bind[a], 0, sizeof bind[a]);
        memset(&attr[a], 0, sizeof attr[a]);
        bind[a].sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT;
        bind[a].binding = a;
        bind[a].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bind[a].divisor = 1;
        attr[a].sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT;
        attr[a].location = a;
        attr[a].binding = a;
        bufs[a] = s_f->ring;
        if (direct & (1u << a)) {
            bind[a].stride = b->direct[a].stride;
            attr[a].format = fmt[a];
            offs[a] = at + run[of_run[a]].at + (VkDeviceSize)(b->direct[a].ptr - run[of_run[a]].lo);
        } else if (conv & (1u << a)) {
            for (i = 0; i < np && slot[i] != a; i++) { }
            bind[a].stride = np * 16;
            attr[a].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            offs[a] = at + pack_at + i * 16;
        } else {
            /* Absent: the title's SET_VERTEX_DATA* value, stride 0. */
            bind[a].stride = 0;
            attr[a].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            offs[a] = at + const_at + a * 16;
        }
    }
#ifdef NFSU2_OPENXR
    if(xr_checked_instances()) memcpy(s_instance_vertex_offsets,offs,sizeof offs);
    if(compact_vertex_bindings())
        binding_count=quest_compact_vertex_bindings(bind,attr,bufs,offs,NV2A_RAW_ATTRS,s_vertex_offset_limit);
    s_vertex_bindings_before+=NV2A_RAW_ATTRS; s_vertex_bindings_after+=binding_count;
    if (s_xr_geometry && !s_eye) {
        XrGeometry *g = s_xr_geometry;
        g->valid = g->constants_valid = 0; /* committed after the uploads */
        g->generation = s_cb_gen; g->buffer = s_f->ring;
        g->binding_count=binding_count;
        g->vertex_at = at; g->vertex_bytes = bytes;
        g->instance_root = NULL; g->instance_count = 1;
        if(shared_instances()) memcpy(g->original_offsets,s_instance_vertex_offsets,sizeof g->original_offsets);
        memcpy(g->bindings, bind, sizeof bind);
        memcpy(g->attributes, attr, sizeof attr);
        memcpy(g->offsets, offs, sizeof offs);
    } else if (xr_geometry_live() && s_geometry_mode >= 2) {
        /* Compare freshly converted right-eye bytes against the left-eye
         * upload, excluding unused alignment padding. Verification renders
         * through the original upload path even if a comparison fails. */
        XrGeometry *g = s_xr_geometry;
        const uint8_t *old = s_f->ring_ptr + g->vertex_at;
        int equal = bytes == g->vertex_bytes && binding_count==g->binding_count &&
            !memcmp(g->bindings, bind, binding_count*sizeof *bind) && !memcmp(g->attributes, attr, sizeof attr);
        for (a = 0; a < binding_count && equal; ++a)
            equal = offs[a] - at == g->offsets[a] - g->vertex_at;
        for (i = 0; i < nrun && equal; ++i)
            equal = !memcmp(old + run[i].at, dst + run[i].at, (size_t)(run[i].hi - run[i].lo));
        if (equal && np) equal = !memcmp(old + pack_at, dst + pack_at, (size_t)nv * np * 16);
        if (equal) equal = !memcmp(old + const_at, dst + const_at, NV2A_RAW_ATTRS * 16);
        if (!equal) xr_geometry_mismatch("vertex");
    }
#endif
    /* Buffer offsets are rebound every draw. The isolated Quest path reuses
     * identical formats only within one pass and one bound pipeline. */
#ifdef NFSU2_OPENXR
    if(s_vertex_layout<0) {
        const char *e=getenv("NFSU2_VR_VERTEX_LAYOUT");
        s_vertex_layout=e && *e=='1';
        LOGE("XR isolated vertex layout reuse: %s\n",s_vertex_layout?"on":"off");
    }
    if(s_vertex_layout && s_multiview && !state_cache_enabled() && !shared_instances()) {
        if(quest_vertex_layout_set(&s_vertex_layout_state,1,s_cb_gen,s_render_generation,
                s_bound_pipe,binding_count,bind,NV2A_RAW_ATTRS,attr)) {
            p_vertex_input(s_cb,binding_count,bind,NV2A_RAW_ATTRS,attr);
            ++s_layout_sets;
        } else ++s_layout_reuses;
    } else {
        s_vertex_layout_state.valid=0;
#endif
    if (!state_cache_enabled() || layout_gen != s_cb_gen || binding_count!=last_binding_count ||
        memcmp(last_bind, bind, sizeof bind) || memcmp(last_attr, attr, sizeof attr)) {
        p_vertex_input(s_cb, binding_count, bind, NV2A_RAW_ATTRS, attr);
        memcpy(last_bind, bind, sizeof bind); memcpy(last_attr, attr, sizeof attr);
        layout_gen = s_cb_gen;
        last_binding_count=binding_count;
        ++s_layout_sets;
    } else ++s_layout_reuses;
#ifdef NFSU2_OPENXR
    }
#endif
    vkCmdBindVertexBuffers(s_cb, 0, binding_count, bufs, offs);
    return 1;
}

/* ── Back end entry points ────────────────────────────────────────── */

/* std140 blocks (gl_vsh.c / gl_psh.c, Vulkan dialect). */
typedef struct {
    float surf[4];
    float m[16];
    float vpoff[4];
    float aa[2];
    int32_t xform;
    int32_t pad;
    float vr_matrix[16];
    float vr_params[4]; /* perspective X/Y, mode, vertical pixel-center bias */
    float vr_clip[4];   /* original HUD clipping rectangle, in guest pixels */
    float vr_depth[4];  /* guest depth A/B, eye/guest B ratio, validity */
    float vr_reflection[4]; /* guest reflection width/height, projective UV enable */
    float vr_matrix_right[16]; /* appended to preserve the existing std140 offsets */
} VsBlock;

#ifdef NFSU2_OPENXR
static FILE *s_xr_trace;
static unsigned s_xr_trace_draw, s_xr_trace_step, s_xr_trace_shots, s_xr_trace_start;
static void dump_surface(VkSurf *s, const char *path);
static void dump_surface_layer(VkSurf *s, const char *path, uint32_t layer);

static void xr_trace_step_capture(void)
{
    static unsigned last_draw;
    if(!s_xr_trace || !s_xr_trace_draw) { last_draw=0; return; }
    if(s_xr_trace_draw==last_draw) return;
    last_draw=s_xr_trace_draw;
    if(s_xr_trace_draw>s_xr_trace_start && s_xr_trace_draw%s_xr_trace_step==0 &&
       s_xr_trace_shots<40 && s_last_eye[0] && s_last_eye[0]->w==640 && s_last_eye[0]->h==480) {
        char path[128];
        snprintf(path,sizeof path,"frames/xr-step-%04u.bmp",s_xr_trace_draw);
        dump_surface(s_last_eye[0],path);
        ++s_xr_trace_shots;
    }
}

/* A one-frame, opt-in diagnostic. A request contains the snapshot stride;
 * normal frames do no draw logging or readback. Consumed only at a flip. */
static void xr_trace_flip(VkSurf *s)
{
    static int enabled=-1;
    if(enabled<0) {
        const char *e=getenv("NFSU2_VR_CAPTURE");
        enabled=e && *e=='1';
    }
    if(!enabled) return;
    if(s_xr_trace) {
        fclose(s_xr_trace); s_xr_trace=NULL;
        if(s) dump_surface(s,"frames/xr-trace-final.bmp");
        if(s_last_eye[1]) dump_surface_layer(s_last_eye[1],"frames/xr-trace-right.bmp",s_multiview?1:0);
        LOGE("XR draw trace complete: %u main draws\n",s_xr_trace_draw);
    }
    if(!s_stereo_frame || !quest_xr_tracking()) return;
    FILE *request=fopen("frames/xr-trace.request","r");
    if(!request) return;
    unsigned stride=100;
    if(fscanf(request,"%u",&stride)!=1 || stride<1) stride=100;
    unsigned start=0;
    (void)fscanf(request,"%u",&start);
    fclose(request);
    remove("frames/xr-trace.request");
    s_xr_trace=fopen("frames/xr-draws.csv","w");
    s_xr_trace_draw=s_xr_trace_shots=0; s_xr_trace_step=stride; s_xr_trace_start=start;
    if(s_xr_trace) fprintf(s_xr_trace,"draw,target,w,h,program,vr,verts,depth,write,blend,src,dst,ps,tex0,tex1,tex2,tex3,rt0,rt1,rt2,rt3,c99x,c99y,c99z,c99w,cmask,full,zeta,depthfunc,stencil,cull,polyoffset,offsetscale,offsetbias,scx,scy,scw,sch,scale_x,scale_y,mvp00,mvp01,mvp02,mvp03,mvp10,mvp11,mvp12,mvp13,mvp20,mvp21,mvp22,mvp23,mvp30,mvp31,mvp32,mvp33,pr00,pr01,pr02,pr03,pr10,pr11,pr12,pr13,pr20,pr21,pr22,pr23,viewport_x,viewport_y,viewport_z,viewport_w\n");
}

/* A full-screen fade/copy is post-processing even when its input lives in
 * guest texture memory rather than an identified Vulkan render target. */
static int xr_fullscreen_quad(const Nv2aRawBatch *b)
{
    /* The game also uses oversized single triangles for full-screen passes. */
    if(b->vertex_count!=3 && b->vertex_count!=4 && b->vertex_count!=6) return 0;
    if(b->xform==1 || (b->xform==2 && b->vp_start!=0 && b->vp_const_count<162)) return 0;
    float lo[2]={1e30f,1e30f},hi[2]={-1e30f,-1e30f};
    for(uint32_t i=0;i<b->vertex_count;++i) {
        float v[4],p[2];
        if(b->attr_direct&1) attr_to_float4(b,0,i,v);
        else if(b->attr_present&1) memcpy(v,b->attrs+(size_t)i*NV2A_RAW_ATTRS*4,sizeof v);
        else return 0;
        if(b->xform==0 || b->vp_start==0) { p[0]=v[0]; p[1]=v[1]; }
        else {
            float c[4]={0};
            for(int row=0;row<4;++row) for(int col=0;col<4;++col) c[row]+=b->vp_consts[96+row][col]*v[col];
            if(fabsf(c[3])<1e-6f) return 0;
            p[0]=c[0]/c[3]*b->vp_consts[160][0]+b->vp_consts[161][0];
            p[1]=c[1]/c[3]*b->vp_consts[160][1]+b->vp_consts[161][1];
        }
        for(int a=0;a<2;++a) {
            if(!isfinite(p[a])) return 0;
            if(p[a]<lo[a]) lo[a]=p[a];
            if(p[a]>hi[a]) hi[a]=p[a];
        }
    }
    return lo[0]<2 && lo[1]<2 && hi[0]>638 && hi[1]>478;
}

/* Full RGBA replacement with no tests/blending and a proven screen-space
 * interior. Everything else, including the alpha-only pass, keeps its copy. */
static int xr_feedback_replace(const Nv2aRawBatch *b, const VkSurf *s,
    const VkDepthBuf *depth, const uint32_t *indices, uint32_t n, VkPrimitiveTopology topo)
{
    if(s_feedback_swap<0) {
        const char *e=getenv("NFSU2_VR_FEEDBACK_SWAP");
        s_feedback_swap=e && *e=='1';
        LOGE("XR feedback target swap %s\n",s_feedback_swap?"on":"off");
    }
    if(!s_feedback_swap || s_feedback_check_active || s_feedback_failed ||
       !s_multiview || depth || b->xform!=2 || b->vp_start!=0 ||
       n!=3 || b->vertex_count!=3 ||
       (topo!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && topo!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP &&
        topo!=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN) ||
       s->w!=640 || s->h!=480 || s->aa_sx!=1 || s->aa_sy!=1 ||
       b->aa_sx!=1.0f || b->aa_sy!=1.0f || !xr_fullscreen_quad(b)) return 0;
    const uint32_t *r=b->regs;
    if(r[0x304/4] || r[0x30c/4] || r[0x32c/4] || r[0x308/4] ||
       (r[0x300/4] && r[0x33c/4]!=0x207) || (r[0x358/4]&0x01010101)!=0x01010101 ||
       (r[0x200/4]&0xffff) || (r[0x204/4]&0xffff) ||
       (r[0x200/4]>>16)<640 || (r[0x204/4]>>16)<480) return 0;
    for(unsigned i=0;i<4;++i) if(((r[0x1e70/4]>>(5*i))&31)==5) return 0; /* shader clip */
    float position[3][4];
    for(unsigned i=0;i<3;++i) {
        float input[16][4];
        if(indices[i]>=b->vertex_count) return 0;
        for(unsigned a=0;a<16;++a) {
            if(b->attr_direct&(1u<<a)) attr_to_float4(b,a,indices[i],input[a]);
            else if(b->attr_present&(1u<<a))
                memcpy(input[a],b->attrs+((size_t)indices[i]*16+a)*4,sizeof input[a]);
            else memcpy(input[a],b->attr_const[a],sizeof input[a]);
        }
        if(!quest_feedback_position(b->vp_program,b->vp_slots,b->vp_consts,b->vp_const_count,input,position[i])) return 0;
    }
    return quest_feedback_covers(position,s->w,s->h,s->pw,s->ph);
}

/* Opt-in capture of actual guest transforms, used to validate stereo camera
 * reconstruction. Bounded by distinct programs, never by draw count. */
static void xr_capture_transform(const Nv2aRawBatch *b)
{
    static int on = -1;
    static uint64_t seen[32];
    static unsigned count;
    if (on < 0) {
        const char *e = getenv("NFSU2_VR_CAPTURE");
        on = e && *e == '1';
    }
    if (!on || !b->xform || count == 32) return;
    uint64_t hash = b->xform == 2 ? prog_hash(b) : 1;
    if(b->vp_const_count>=100 && (fabsf(b->vp_consts[99][0])+fabsf(b->vp_consts[99][1])+fabsf(b->vp_consts[99][2]))>1e-6f)
        hash^=0x7ac18e14fafe3201ull; /* record orthographic and perspective uses */
    for (unsigned i = 0; i < count; ++i) if (seen[i] == hash) return;
    seen[count++] = hash;
    char name[128];
    snprintf(name, sizeof name, "frames/transform-%02u.txt", count);
    FILE *f = fopen(name, "w");
    if (!f) return;
    fprintf(f, "frame %u xform %u surface %ux%u clip %u,%u aa %g,%g start %u\n",
        s_frame, b->xform, b->surface.width, b->surface.height,
        b->surface.clip_x, b->surface.clip_y, b->aa_sx, b->aa_sy, b->vp_start);
    for (unsigned i = 0; i < 4; ++i)
        fprintf(f, "m%u %.9g %.9g %.9g %.9g\n", i, b->composite[4*i],
            b->composite[4*i+1], b->composite[4*i+2], b->composite[4*i+3]);
    fprintf(f, "offset %.9g %.9g %.9g %.9g\n", b->vp_offset[0],b->vp_offset[1],b->vp_offset[2],b->vp_offset[3]);
    if (b->xform == 2) {
        for (unsigned i = b->vp_start; i < b->vp_slots; ++i) {
            const uint32_t *t = b->vp_program[i];
            fprintf(f, "p%u %08x %08x %08x %08x\n", i,t[0],t[1],t[2],t[3]);
            if (t[3] & 1) break;
        }
        for (unsigned i = 0; i < b->vp_const_count; ++i)
            fprintf(f, "c%u %.9g %.9g %.9g %.9g\n", i,b->vp_consts[i][0],
                b->vp_consts[i][1],b->vp_consts[i][2],b->vp_consts[i][3]);
    }
    fclose(f);
    LOGE("XR transform capture: %s\n", name);
}
#endif

typedef struct {
    float tscale[4][4];          /* vec2 array: 16-byte elements */
    float c0[8][4], c1[8][4];
    float fc0[4], fc1[4];
    float fog[4];
    int32_t afunc;
    float aref;
    float pad[2];
} FsBlock;

/* ── Instancing ───────────────────────────────────────────────────────
 *
 * A quarter of a race frame's draws repeat the previous draw with only the
 * transform constants changed (NFSU2's drag start line: ~480 of ~1870) --
 * the same mesh at another matrix. Such a run becomes one instanced draw:
 * the first draw is recorded up to its vkCmdDrawIndexed, which is held
 * back (s_pend); every repeat only appends its 192 constants right behind
 * the previous set in the ring (binding 6, which the vertex shader indexes
 * by gl_InstanceIndex * 192); anything else -- another draw, a clear, a
 * flip -- first issues the held draw with the instance count. Instances
 * are drawn in order, so blending and depth come out as separate draws.
 *
 * "Repeat" means: no register changed since the held draw except the
 * constant and program upload windows and the per-draw methods (executor
 * dirty blocks, compared to a copy), the same program, vertex pointers,
 * formats and count, the same indices, absent-attribute values and
 * texture addresses, and no semaphore release or trap in between
 * (vtx_epoch: vertex bytes the title could have rewritten). Only vertex
 * programs (xform 2) whose attributes are all uploaded as stored.
 * RECOMP_VK_INSTANCE=0 turns it off. */
#define VK_INST_MAX   16                    /* gl_vsh.c: c[192 * 16] */
#define VK_CONST_SIZE (192 * 16)

extern uint8_t *nv2a_pb_reg_dirty(void);
static uint32_t s_regs_seen[0x2000 / 4];    /* registers as of the last draw */
#ifdef NFSU2_OPENXR
static int s_xr_replaying;
static const uint8_t *s_xr_vertex_ids[NV2A_RAW_ATTRS];
#endif

static const uint8_t *vertex_identity(const Nv2aRawBatch *b,uint32_t a)
{
#ifdef NFSU2_OPENXR
    if(s_xr_replaying || s_xr_snapshot_source) return s_xr_vertex_ids[a];
#endif
    return b->direct[a].ptr;
}

/* Did a register a draw depends on change since the last draw? */
static int regs_changed(const uint32_t *r)
{
    uint8_t *dirty = nv2a_pb_reg_dirty();
    uint32_t blk, w;
    int changed = 0;
    int replay_safe=0;
#ifdef NFSU2_OPENXR
    replay_safe=xr_stereo();
#endif

    for (blk = 0; blk < 0x2000 / 64; blk++) {
        if (!replay_safe && !dirty[blk])
            continue;
        if(!replay_safe) dirty[blk] = 0;
        if (blk >= 0xB00 / 64 && blk < 0xC00 / 64)
            continue;                           /* program / constant uploads */
        /* Executor dirty bits describe the live left-eye draw, never a queued
         * right-eye snapshot. Compare complete blocks for stereo replay. */
        if(replay_safe && !memcmp(r+blk*16,s_regs_seen+blk*16,64)) continue;
        for (w = blk * 16; w < blk * 16 + 16; w++) {
            uint32_t m = w * 4;
            if (r[w] == s_regs_seen[w])
                continue;
            s_regs_seen[w] = r[w];
            if (m == 0x0100 || m == 0x17FC || (m >= 0x1800 && m < 0x1820)
             || m == 0x1D6C || m == 0x1D70 || m == 0x1E9C || m == 0x1EA4)
                continue;                       /* NOP, BEGIN_END, draw data, semaphore, load slots */
            changed = 1;
        }
    }
    return changed;
}

static struct {
    int          on;                        /* a draw is held */
    uint32_t     n, count, max;             /* index count, instances so far, room */
    VkDeviceSize cat;                       /* first instance's constants */
    uint32_t     cb_gen;
    uint32_t     prim, vertex_count, index_count;
    uint16_t     attr_present, attr_direct;
    uint32_t     vp_prog_gen, attr_const_gen, vtx_epoch, color_va, zeta_va;
    uint32_t     tex_va[4], pal_va[4];
    float        aa_sx, aa_sy;
    Nv2aSurface  surface;
    struct { const uint8_t *ptr; uint32_t type, size, stride; } direct[NV2A_RAW_ATTRS];
    uint32_t    *idx;
    size_t       idx_cap;
    PipeKey      key;
    VkPipeline   single_pipe, instance_pipe;
#ifdef NFSU2_OPENXR
    int eye;
    uint32_t frame;
    float projection[12];
    VkDeviceSize vertex_offsets[NV2A_RAW_ATTRS];
    float attr_const[NV2A_RAW_ATTRS][4];
    XrGeometry *geometry;
    int shared_constants;
    const uint32_t *immutable_regs;
    const Nv2aRawBatch *snapshot;
#endif
} s_pend;
static int s_inst_on = -1, s_inst_alt;
static uint32_t s_st_draws, s_st_calls, s_st_inst;
static void bind_draw_pipeline(VkPipeline pipe)
{
    if(pipe!=s_bound_pipe || s_bound_pipe_gen!=s_cb_gen) {
        vkCmdBindPipeline(s_cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe);
        s_bound_pipe=pipe; s_bound_pipe_gen=s_cb_gen;
    }
}

#ifdef NFSU2_OPENXR
/* Diagnostic ABBA/BAAB crossover, confined to completed tracked race frames.
 * Both modes use the same shaders, resolution, uploads and descriptor path.
 * Transition frames are discarded; no extra GPU waits are added. PCVR uses
 * longer phases to reduce the influence of individual scheduling stalls. */
static void render_compare_tick(uint64_t now, int race)
{
#ifdef NFSU2_PCVR
    enum { compare_frames=384, compare_warmup=24 };
#else
    enum { compare_frames=120, compare_warmup=12 };
#endif
    static int option=-1, phase=-1;
    static const int modes[8]={0,1,1,0,1,0,0,1};
    static unsigned frames;
    static uint64_t previous, cpu_ns, draws;
    static float samples[compare_frames];
    static double total_ms[2];
    if(option<0) {
        const char *e=getenv("NFSU2_VR_SHARED_COMPARE");
        option=e && *e=='1' && shared_instances();
        e=getenv("NFSU2_VR_BARRIER_COMPARE");
        if(!option && e && *e=='1' && s_multiview && s_inst_on==0) option=2;
        e=getenv("NFSU2_VR_FEEDBACK_COMPARE");
        if(!option && e && *e=='1' && s_multiview && s_inst_on==0) option=3;
        e=getenv("NFSU2_VR_PB_COMPARE");
        if(!option && e && *e=='1' && s_multiview && s_inst_on==0 && !getenv("RECOMP_PB_SCAN")) option=4;
        e=getenv("NFSU2_VR_VERTEX_LAYOUT_COMPARE");
        if(!option && e && *e=='1' && s_multiview && s_inst_on==0 && !state_cache_enabled()) option=5;
        e=getenv("NFSU2_VR_DEPTH_LAYOUT_COMPARE");
        if(!option && e && *e=='1' && s_multiview && s_inst_on==0) option=6;
#ifdef NFSU2_PCVR
        e=getenv("NFSU2_PCVR_PB_DIRECT_COMPARE");
        if(!option && e && *e=='1' && s_multiview && s_inst_on==0) option=7;
        e=getenv("NFSU2_PCVR_SURFACE_LOOKUP_COMPARE");
        if(!option && e && *e=='1' && s_multiview && s_inst_on==0) option=8;
        e=getenv("NFSU2_PCVR_PB_SHADOW_COMPARE");
        if(!option && e && *e=='1' && s_multiview && s_inst_on==0) option=9;
        e=getenv("NFSU2_PCVR_INDICES_COMPARE");
        if(!option && e && *e=='1' && s_multiview && s_inst_on==0) option=10;
        e=getenv("NFSU2_PCVR_INDEX_PACKETS_COMPARE");
        if(!option && e && *e=='1' && s_multiview && s_inst_on==0) option=11;
#endif
    }
    uint64_t frame_cpu=s_compare_cpu_ns;
    uint32_t frame_draws=s_compare_draws;
    s_compare_cpu_ns=0; s_compare_draws=0;
    if(!option || (option==3 && s_feedback_failed)) return;
    if(option==1 && cpu_instance_mode()==3) return; /* byte verification precedes timing */
    const char *name=option==6?"depth-layout":option==5?"vertex-layout":option==4?"pushbuffer":option==3?"feedback":option==2?"barrier":"shared";
    int *setting=option==6?&s_depth_optimal:option==5?&s_vertex_layout:option==4?&s_pb_fast:option==3?&s_feedback_swap:option==2?&s_image_barriers:&s_inst_on;
#ifdef NFSU2_PCVR
    if(option==7) {
        if(!nv2a_pb_data_direct_verified()) return;
        name="direct-packets"; setting=&s_pb_direct_compare;
    }
    if(option==8) {
        if(s_surface_lookup_failed || s_surface_lookup_checked<100000) return;
        name="surface-lookup"; setting=&s_surface_lookup;
    }
    if(option==9) {
        if(!nv2a_pb_data_shadow_verified()) return;
        name="shadow-blocks"; setting=&s_pb_shadow_compare;
    }
    if(option==10) {
        if(!nv2a_pb_index_verified()) return;
        name="index-data"; setting=&s_indices_compare;
    }
    if(option==11) {
        if(!nv2a_pb_index_packet_verified()) return;
        name="index-packets"; setting=&s_index_packets_compare;
    }
#endif
    /* Let the tester settle the car/view before starting. Poll only before
     * measurement; file IO never enters a measured phase. */
    if(phase<0 || phase==8) {
        if(s_frame%60) return;
        const char *path=option==6?"depth-layout-compare.start":option==5?"vertex-layout-compare.start":option==4?"pushbuffer-compare.start":option==3?"feedback-compare.start":option==2?"barrier-compare.start":"shared-compare.start";
#ifdef NFSU2_PCVR
        if(option==7) path="direct-packets-compare.start";
        if(option==8) path="surface-lookup-compare.start";
        if(option==9) path="shadow-blocks-compare.start";
        if(option==10) path="index-data-compare.start";
        if(option==11) path="index-packets-compare.start";
#endif
        FILE *start=fopen(path,"rb");
        if(!start) return;
        fclose(start); remove(path);
        if(option==4 || option==7 || option==9) { nv2a_pb_data_verify(0); LOGE("XR pushbuffer verification disabled before timing\n"); }
        phase=0; previous=0; frames=0; cpu_ns=draws=0;
        total_ms[0]=total_ms[1]=0;
    }
    if(!race || !quest_xr_tracking()) {
        previous=0; frames=0; cpu_ns=draws=0;
        return;
    }
    if(!previous) {
        if(phase<0) phase=0;
        *setting=modes[phase]; previous=now;
        if(option==4) { nv2a_pb_scan_survey(!*setting); nv2a_pb_scan_bulk(*setting); }
#ifdef NFSU2_PCVR
        if(option==7) nv2a_pb_data_direct(*setting);
        if(option==9) nv2a_pb_data_shadow(*setting);
        if(option==10) nv2a_pb_index_mode(*setting);
        if(option==11) nv2a_pb_index_packet_mode(*setting);
#endif
        LOGE("XR %s comparison phase %d/8: optimization %s, %u warmup + %u measured frames\n",
             name,phase+1,*setting?"on":"off",compare_warmup,compare_frames);
        return;
    }
    if(frames>=compare_warmup) {
        samples[frames-compare_warmup]=(float)((now-previous)/1e6);
        cpu_ns+=frame_cpu; draws+=frame_draws;
    }
    previous=now;
    if(++frames==compare_frames+compare_warmup) {
        double sum=0;
        for(unsigned i=0;i<compare_frames;++i) sum+=samples[i];
        qsort(samples,compare_frames,sizeof *samples,frame_time_compare);
        total_ms[modes[phase]]+=sum;
        LOGE("XR %s comparison result: phase %d mode %d fps %.2f p50 %.3f p95 %.3f max %.3f cpu_draw %.3f raw_draws %.1f\n",
             name,phase+1,modes[phase],compare_frames*1000.0/sum,samples[(compare_frames-1)/2],
             samples[(compare_frames*95+99)/100-1],samples[compare_frames-1],
             cpu_ns/(compare_frames*1e6),draws/(double)compare_frames);
        frames=0; cpu_ns=draws=0;
        if(++phase==8) {
            *setting=total_ms[1]<total_ms[0]*0.98;
            LOGE("XR %s comparison complete; optimization %s (off %.2f fps, on %.2f fps; enabling requires >2%% gain)\n",
                 name,*setting?"on":"off",compare_frames*4000.0/total_ms[0],compare_frames*4000.0/total_ms[1]);
        } else {
            *setting=modes[phase];
            LOGE("XR %s comparison phase %d/8: optimization %s\n",name,phase+1,*setting?"on":"off");
        }
        if(option==4) { nv2a_pb_scan_survey(!*setting); nv2a_pb_scan_bulk(*setting); }
#ifdef NFSU2_PCVR
        if(option==7) nv2a_pb_data_direct(*setting);
        if(option==9) nv2a_pb_data_shadow(*setting);
        if(option==10) nv2a_pb_index_mode(*setting);
        if(option==11) nv2a_pb_index_packet_mode(*setting);
#endif
    }
}
#endif

static void pend_flush(void)
{
    if (!s_pend.on)
        return;
    s_pend.on = 0;
    if (s_pend.cb_gen != s_cb_gen)
        return;                                 /* cannot happen: flushes go first */
    /* Select once, after the instance count is known. Both pipelines declare
     * the same dynamic state and layout; no temporary single/instance/single
     * binding sequence is needed. The cache records the actually emitted bind. */
    bind_draw_pipeline(shared_instances() && s_pend.count>1 ? s_pend.instance_pipe : s_pend.single_pipe);
    vkCmdDrawIndexed(s_cb, s_pend.n, s_pend.count, 0, 0, 0);
    s_st_calls++;
}

static int pend_matches(const Nv2aRawBatch *b)
{
    uint32_t a;
#ifdef NFSU2_OPENXR
    if(xr_stereo()) {
        if(s_pend.eye!=s_eye || s_pend.frame!=s_frame) return 0;
        /* Perspective-only instances: the tracked view is constant for this
         * eye/frame, but the game's FOV may change. Exclude HUD/orthographic
         * work even when its original clip rectangle is smaller than 640x480. */
        if(b->vp_const_count<116 ||
           fabsf(b->vp_consts[99][0])+fabsf(b->vp_consts[99][1])+fabsf(b->vp_consts[99][2])<=1e-6f ||
           memcmp(s_pend.projection,&b->vp_consts[113][0],sizeof s_pend.projection)) return 0;
    }
#endif
    if (b->xform != 2 || b->prim != s_pend.prim || b->vertex_count != s_pend.vertex_count
     || b->index_count != s_pend.index_count || b->attr_present != s_pend.attr_present
     || b->attr_direct != s_pend.attr_direct || b->vp_prog_gen != s_pend.vp_prog_gen
     || b->attr_const_gen != s_pend.attr_const_gen || b->vtx_epoch != s_pend.vtx_epoch
     || b->color_va != s_pend.color_va || b->zeta_va != s_pend.zeta_va
     || b->aa_sx != s_pend.aa_sx || b->aa_sy != s_pend.aa_sy
     || memcmp(b->tex_va, s_pend.tex_va, sizeof s_pend.tex_va)
     || memcmp(b->pal_va, s_pend.pal_va, sizeof s_pend.pal_va)
     || memcmp(&b->surface, &s_pend.surface, sizeof s_pend.surface))
        return 0;
    for (a = 0; a < NV2A_RAW_ATTRS; a++)
        if ((b->attr_direct & (1u << a))
         && (vertex_identity(b,a) != s_pend.direct[a].ptr || b->direct[a].type != s_pend.direct[a].type
          || b->direct[a].size != s_pend.direct[a].size || b->direct[a].stride != s_pend.direct[a].stride))
            return 0;
    if(memcmp(b->indices, s_pend.idx, (size_t)b->index_count * 4)) return 0;
#ifdef NFSU2_OPENXR
    if(xr_stereo() && xr_checked_instances()) {
        /* Pointer/epoch identity alone is insufficient evidence for combining
         * deferred draws. Compare against the first draw's immutable GPU upload,
         * not guest memory or executor scratch storage that may have changed.
         * pend_append already checked the command-buffer generation. */
        ++s_instance_checked;
        int equal=!memcmp(b->attr_const,s_pend.attr_const,sizeof s_pend.attr_const);
        int cpu_equal=equal, cpu_check=cpu_instance_mode() && s_pend.snapshot;
        if(cpu_check) {
            QuestInstanceBytes spans[NV2A_RAW_ATTRS]; unsigned count=0;
            for(a=0;a<NV2A_RAW_ATTRS;++a) if(b->attr_direct & (1u<<a)) {
                size_t bytes=(size_t)(b->vertex_count-1)*b->direct[a].stride+
                             elem_bytes(b->direct[a].type,b->direct[a].size);
                spans[count++]=(QuestInstanceBytes){s_pend.snapshot->direct[a].ptr,b->direct[a].ptr,bytes};
            }
            cpu_equal=equal && quest_instance_bytes_equal(spans,count);
        }
        if(!cpu_check || cpu_instance_mode()==3)
        for(a=0;a<NV2A_RAW_ATTRS && equal;++a) if(b->attr_direct & (1u<<a)) {
            size_t bytes=(size_t)(b->vertex_count-1)*b->direct[a].stride+
                         elem_bytes(b->direct[a].type,b->direct[a].size);
            VkDeviceSize at=s_pend.vertex_offsets[a];
            equal=at<=RING_BYTES && bytes<=RING_BYTES-at &&
                  !memcmp(s_f->ring_ptr+at,b->direct[a].ptr,bytes);
        }
        if(cpu_check && cpu_instance_mode()==3) {
            if(cpu_equal!=equal) {
                ++s_cpu_instance_failed; s_cpu_instance_mode=0;
                LOGE("CPU/GPU instance comparison mismatch; CPU path disabled\n");
            } else if(s_stereo_frame && quest_xr_tracking()) ++s_cpu_instance_verified;
        }
        if(cpu_check && cpu_instance_mode()==1) equal=cpu_equal;
        if(!equal) { ++s_instance_rejected; return 0; }
    }
#endif
    return 1;
}

/* Can b be held as the first instance of a run? */
static int pend_eligible(const Nv2aRawBatch *b)
{
    uint32_t a;
    if (!s_inst_on || b->xform != 2 || (b->attr_present & ~b->attr_direct))
        return 0;
    for(a=0;a<4;++a)
        if(b->tex_va[a]==b->surface.color_va && (b->regs[(0x1b0c+a*0x40)/4]&0x40000000u))
            return 0; /* feedback must read the result of each preceding draw */
#ifdef NFSU2_OPENXR
    if(xr_stereo()) {
        if(s_xr_trace) return 0;
        float scales[2];
        if(b->vp_const_count<116 ||
           fabsf(b->vp_consts[99][0])+fabsf(b->vp_consts[99][1])+fabsf(b->vp_consts[99][2])<=1e-6f ||
           !quest_projection_constants(&b->vp_consts[113][0],scales) ||
           !quest_program_has_mvp(b->vp_program,b->vp_start,b->vp_slots)) return 0;
    }
#endif
    for (a = 0; a < NV2A_RAW_ATTRS; a++)
        if ((b->attr_direct & (1u << a))
         && vertex_format(b->direct[a].type, b->direct[a].size) == VK_FORMAT_UNDEFINED)
            return 0;
    return 1;
}

static void pend_hold(const Nv2aRawBatch *b, uint32_t n, VkDeviceSize cat, uint32_t max,
                      const PipeKey *key, VkPipeline pipe)
{
    uint32_t a;
    s_pend.on = 1;
    s_pend.n = n;
    s_pend.count = 1;
    s_pend.max = max;
    s_pend.cat = cat;
    s_pend.cb_gen = s_cb_gen;
    s_pend.prim = b->prim;
    s_pend.vertex_count = b->vertex_count;
    s_pend.index_count = b->index_count;
    s_pend.attr_present = b->attr_present;
    s_pend.attr_direct = b->attr_direct;
    s_pend.vp_prog_gen = b->vp_prog_gen;
    s_pend.attr_const_gen = b->attr_const_gen;
    s_pend.vtx_epoch = b->vtx_epoch;
    s_pend.color_va = b->color_va;
    s_pend.zeta_va = b->zeta_va;
    s_pend.aa_sx = b->aa_sx;
    s_pend.aa_sy = b->aa_sy;
    s_pend.surface = b->surface;
    s_pend.key = *key;
    s_pend.single_pipe = pipe; s_pend.instance_pipe = VK_NULL_HANDLE;
#ifdef NFSU2_OPENXR
    s_pend.geometry = shared_instances() && !s_eye ? s_xr_geometry : NULL;
    s_pend.immutable_regs=s_xr_immutable_regs;
    s_pend.snapshot=s_xr_immutable_batch;
    s_pend.shared_constants = shared_instances() && xr_geometry_live() &&
                              s_xr_geometry->constants_valid;
    s_pend.eye=s_eye; s_pend.frame=s_frame;
    memcpy(s_pend.vertex_offsets,s_instance_vertex_offsets,sizeof s_pend.vertex_offsets);
    memcpy(s_pend.attr_const,b->attr_const,sizeof s_pend.attr_const);
    memset(s_pend.projection,0,sizeof s_pend.projection);
    if(b->vp_const_count>=116) memcpy(s_pend.projection,&b->vp_consts[113][0],sizeof s_pend.projection);
#endif
    memcpy(s_pend.tex_va, b->tex_va, sizeof s_pend.tex_va);
    memcpy(s_pend.pal_va, b->pal_va, sizeof s_pend.pal_va);
    for (a = 0; a < NV2A_RAW_ATTRS; a++) {
        s_pend.direct[a].ptr = vertex_identity(b,a);
        s_pend.direct[a].type = b->direct[a].type;
        s_pend.direct[a].size = b->direct[a].size;
        s_pend.direct[a].stride = b->direct[a].stride;
    }
    if (b->index_count > s_pend.idx_cap) {
        free(s_pend.idx);
        s_pend.idx_cap = b->index_count + 1024;
        s_pend.idx = (uint32_t *)malloc(s_pend.idx_cap * 4);
        if (!s_pend.idx) {
            s_pend.idx_cap = 0;
            s_pend.on = 0;
            bind_draw_pipeline(pipe);
            vkCmdDrawIndexed(s_cb, n, 1, 0, 0, 0);
            s_st_calls++;
            return;
        }
    }
    memcpy(s_pend.idx, b->indices, (size_t)b->index_count * 4);
}

/* One more instance of the held draw, when b is one; 0: draw it normally. */
static int pend_append(const Nv2aRawBatch *b, int changed)
{
    VkDeviceSize at;
    void *map;

    if (!s_pend.on)
        return 0;
    if (changed || s_pend.count >= s_pend.max || s_pend.cb_gen != s_cb_gen || !pend_matches(b))
        return 0;
    int reuse=0;
#ifdef NFSU2_OPENXR
    if(shared_instances() && s_pend.shared_constants) {
        /* Right-eye runs are checked independently. They can only extend
         * within the immutable, contiguous constant range bound by the first
         * draw; no writes into the earlier left-eye range are permitted. */
        if(!xr_geometry_live() || !s_xr_geometry->constants_valid ||
           s_xr_geometry->constants_at!=s_pend.cat+(VkDeviceSize)s_pend.count*VK_CONST_SIZE)
            return 0;
        reuse=1;
    }
#endif
    at = (s_f->ring_off + s_ubo_align - 1) & ~(s_ubo_align - 1);
    if (!reuse && (at != s_pend.cat + (VkDeviceSize)s_pend.count * VK_CONST_SIZE
     || at + VK_CONST_SIZE > RING_BYTES))
        return 0;
    if(shared_instances() && !s_pend.instance_pipe) {
        PipeKey key=s_pend.key; key.fragment_flags|=4;
        s_pend.instance_pipe=pipe_get(&key);
        if(!s_pend.instance_pipe) return 0; /* retain separate original draws */
    }
    if(!reuse) {
        at = ring_alloc(VK_CONST_SIZE, s_ubo_align, &map);
        if (!map) return 0;
        memcpy(map, b->vp_consts, VK_CONST_SIZE);
    }
#ifdef NFSU2_OPENXR
    if(shared_instances() && !s_eye && s_pend.geometry) {
        XrGeometry *root=s_pend.geometry;
        root->instance_count=s_pend.count+1;
        if(s_xr_geometry) {
            *s_xr_geometry=*root;
            s_xr_geometry->instance_root=root;
            s_xr_geometry->constants_at=at;
        }
    }
    if(reuse) ++s_shared_instance_appends;
#endif
    s_pend.count++;
    s_st_inst++;
    return 1;
}

#ifdef NFSU2_OPENXR
static void xr_feedback_verify(const Nv2aRawBatch *b, VkSurf *s);
#endif
static void vk_draw_raw_eye(const Nv2aRawBatch *b)
{
    static uint32_t descriptor_gen;
    static VkDescriptorImageInfo last_images[4];
    VkSurf *s, *sampled[4];
    VkDepthBuf *d;
    VkProg *p;
    Nv2aPshKey pk;
    const uint32_t *r = b->regs;
    const uint32_t *idx;
    uint32_t n, i, cls;
    VkPrimitiveTopology topo;
    DrawDescriptors descriptors;
    VkDescriptorImageInfo *img = descriptors.images;
    float tscale[4][2];
    PipeKey key;
    VkPipeline pipe;
    VkDescriptorBufferInfo *ub = descriptors.buffers;
    VkWriteDescriptorSet wr[7];
    uint32_t inst_room = 0;
    void *map;
#ifdef NFSU2_OPENXR
    int feedback_swapped=0;
#endif

    if (!ready())
        return;
    s_regs = r;
    s_batch = b;
#ifdef NFSU2_OPENXR
    if (quest_xr_enabled() && !s_eye) xr_capture_transform(b);
#endif
    if (s_inst_on < 0) {
        const char *e = getenv("RECOMP_VK_INSTANCE");
        s_inst_on = !(e && *e == '0') && s_ubo_range >= VK_CONST_SIZE
                 && VK_CONST_SIZE % s_ubo_align == 0;
#ifdef NFSU2_OPENXR
        if(xr_stereo() && !xr_checked_instances()) s_inst_on=0;
#endif
        if (s_inst_on && e && *e == '2')
            s_inst_alt = 1;                     /* debug: every other frame */
        fprintf(stderr, "  [VK] instancing %s (uniform range %u, alignment %u)\n",
                s_inst_on ? "on" : "off", s_ubo_range, (unsigned)s_ubo_align);
    }
    s_st_draws++;
#ifdef NFSU2_OPENXR
    ++s_compare_draws;
#endif
    if(s_inst_on) {
        int changed;
#ifdef NFSU2_OPENXR
        if(shared_instances())
            changed=!s_pend.on || !s_pend.immutable_regs ||
                    !quest_instance_regs_equal(s_pend.immutable_regs,r);
        else
#endif
            changed=regs_changed(r);
        if (pend_append(b, changed))
            return;
    }
    pend_flush();
    if (!prim_indices(b, &idx, &n, &topo, &cls) || !n)
        return;
    VT("draw: prim %u idx %u verts %u attrs %04X direct %04X xform %u surf %08X zeta %08X\n", b->prim, n,
       b->vertex_count, b->attr_present, b->attr_direct, b->xform, b->surface.color_va, b->zeta_va);
    s = target(&b->surface, b->zeta_va, &d);
    if (!s)
        return;
#ifdef NFSU2_OPENXR
    if(!s_eye && s_xr_trace && s->va==s_world_color_va && s->w==640 && s->h==480 && !r[0x30c/4]) {
        if(r[0x1e70/4]==0x5e1) dump_surface(s,"frames/xr-before-blur.bmp");
        if(r[0x1e70/4]==0x41e1) dump_surface(s,"frames/xr-before-grade.bmp");
        if(r[0x1e70/4]==1 && b->tex_va[0]!=s->va && r[0x304/4] &&
           r[0x344/4]==1 && r[0x348/4]==1 && xr_fullscreen_quad(b))
            dump_surface(s,"frames/xr-before-lights.bmp");
    }
#endif
    s_drew_any = 1;
    psh_key(r, &pk);
    p = prog_get(b, &pk);
    if (!p)
        return;
    s_hz_draws++;

    /* Textures first: uploads end the rendering pass. */
    for (i = 0; i < 4; i++)
        bind_stage(r, (int)i, tscale[i], &img[i], &sampled[i]);
#ifdef NFSU2_OPENXR
    {
        for(i=0;i<4;++i) {
            uint32_t mode=(pk.shader_program>>(5*i))&31;
            /* Diagnostic only: these modes emit no lookup in gl_psh.c. */
            if(mode==0 || mode==4 || mode==5 || (!i && (mode==15 || mode==16))) {
                if(sampled[i]==s) ++s_xr_unused_feedback;
            }
        }
    }
#endif
    {
        VkImageView snapshot=VK_NULL_HANDLE;
        for(i=0;i<4;++i) if(sampled[i]==s) {
            if(!snapshot) {
#ifdef NFSU2_OPENXR
                if(xr_feedback_replace(b,s,d,idx,n,topo)) {
                    snapshot=surface_feedback_swap(s);
                    feedback_swapped=snapshot!=VK_NULL_HANDLE;
                }
#endif
                if(!snapshot) snapshot=surface_snapshot(s);
            }
            if(!snapshot) return;
            img[i].imageView=snapshot;
        }
    }
    for (i = 0; i < 4; i++)
        if (sampled[i] && sampled[i]->dirty && sampled[i] != s) {
            end_rendering();                 /* next begin_rendering barriers */
            break;
        }

    memset(&key, 0, sizeof key);
    key.prog = (int)(p - s_prog);
    if (r[0x304 / 4]) {
        key.blend = 1; key.bsrc = r[0x344 / 4]; key.bdst = r[0x348 / 4]; key.beq = r[0x350 / 4];
    }
    {
        uint32_t cm = r[0x358 / 4], m = 0;
        if (cm & 0x00010000u) m |= VK_COLOR_COMPONENT_R_BIT;
        if (cm & 0x00000100u) m |= VK_COLOR_COMPONENT_G_BIT;
        if (cm & 0x00000001u) m |= VK_COLOR_COMPONENT_B_BIT;
        if (cm & 0x01000000u) m |= VK_COLOR_COMPONENT_A_BIT;
        key.cmask = m;
    }
    key.depth = d != NULL;
    key.topo = cls;
    {
        /* Everything this draw puts in the ring, reserved now: a flush
         * later in the draw would reuse the ring under its own uniforms. */
        VkDeviceSize need = 2 * 4096 + sizeof(VsBlock) + sizeof(FsBlock) + VK_CONST_SIZE + (VkDeviceSize)n * 4
                          + NV2A_RAW_ATTRS * 16 + 16 * 16;
        uint32_t a;
        for (a = 0; a < NV2A_RAW_ATTRS; a++)
            if (b->attr_present & (1u << a)) {
                uint32_t st = (b->attr_direct & (1u << a)) ? b->direct[a].stride : 0;
                need += (VkDeviceSize)b->vertex_count * (st > 16 ? st : 16) + 16;
            }
        if (s_f->ring_off + need > RING_BYTES) {
            void *tmp;
            if (need > RING_BYTES) return;
            ring_alloc(RING_BYTES, 16, &tmp);       /* forces the flush */
            s_f->ring_off = 0;
        }
    }
    begin_rendering(s, d);
    s->dirty = 1;
    s->gen++;
    apply_dynamic(r, d != NULL, topo);
    set_surface_scissor(r, s);

    /* Uniforms. */
    {
        VsBlock *vb;
        FsBlock *fb;
#ifdef NFSU2_PCVR
        /* HOST_COHERENT does not imply CPU caching. In particular, reading
         * back fields while constructing uniforms in a write-combined upload
         * mapping stalls the CPU on discrete GPUs. Keep every calculation and
         * classification on ordinary RAM, then write the completed blocks. */
        extern int pcvr_cpu_uniforms(void);
        int cpu_uniforms=pcvr_cpu_uniforms();
        VsBlock cpu_vb={0};
        FsBlock cpu_fb;
        void *gpu_vb, *gpu_fb;
#endif
        VkDeviceSize vat = ring_alloc(sizeof(VsBlock), s_ubo_align, &map);
        vb = (VsBlock *)map;
        if (!vb) return;
#ifdef NFSU2_PCVR
        gpu_vb=map;
        if(cpu_uniforms) vb=&cpu_vb;
#endif
        vb->surf[0] = 2.0f / (float)s->w;
        vb->surf[1] = 2.0f / (float)s->h;
        vb->surf[2] = 1.0f / (float)zmax_of(r);
        vb->surf[3] = 0.5f - 0.5f * (float)s->w / (float)s->pw; /* see nv2a_snap */
        memcpy(vb->m, b->composite, sizeof vb->m);
        memcpy(vb->vpoff, b->vp_offset, sizeof vb->vpoff);
        vb->aa[0] = b->aa_sx > 0 ? b->aa_sx : 1.0f;
        vb->aa[1] = b->aa_sy > 0 ? b->aa_sy : 1.0f;
        vb->xform = b->xform == 1 ? 1 : 0;
        memset(vb->vr_matrix, 0, sizeof vb->vr_matrix);
        memset(vb->vr_matrix_right, 0, sizeof vb->vr_matrix_right);
        memset(vb->vr_params, 0, sizeof vb->vr_params);
        memset(vb->vr_depth, 0, sizeof vb->vr_depth);
        memset(vb->vr_reflection, 0, sizeof vb->vr_reflection);
        vb->vr_params[3]=0.5f-0.5f*(float)s->h/(float)s->ph;
        vb->vr_clip[0]=vb->vr_clip[1]=0;
        vb->vr_clip[2]=(float)s->w; vb->vr_clip[3]=(float)s->h;
#ifdef NFSU2_OPENXR
        if (xr_stereo() && quest_xr_tracking() && b->xform == 2 &&
            s->aa_sx == 1 && s->aa_sy == 1 &&
            ((s->w == 640 && s->h == 480) || (s->w == 320 && s->h == 240)) &&
            b->vp_const_count >= 100 &&
            (fabsf(b->vp_consts[99][0])+fabsf(b->vp_consts[99][1])+fabsf(b->vp_consts[99][2]))>1e-6f &&
            program_has_tracked_mvp(p,b) &&
            ((b->vp_const_count>=116 && quest_projection_constants(&b->vp_consts[113][0],vb->vr_params)) ||
             quest_projection_scales(&b->vp_consts[96][0], vb->vr_params))) {
            quest_xr_matrix(s_eye, vb->vr_matrix);
            vb->vr_params[2] = 1;
            if(b->vp_const_count>=116)
                quest_projection_depth(&b->vp_consts[113][0],quest_xr_depth_coefficient(),vb->vr_depth);
            if (s->w == 640) {
                s_stereo_frame = 1;
                if(r[0x30c/4] && r[0x35c/4]) s_world_color_va=s->va;
                s_game_projection[0]=vb->vr_params[0]; s_game_projection[1]=vb->vr_params[1];
                memcpy(s_game_depth,vb->vr_depth,sizeof s_game_depth);
                if (sampled[1] && sampled[1]->w==320 && sampled[1]->h==240 &&
                    ((r[0x1e70/4]>>5)&31)==1 &&
                    quest_program_projects_reflection(b->vp_program,b->vp_start,b->vp_slots)) {
                    vb->vr_reflection[0]=320; vb->vr_reflection[1]=240;
                    vb->vr_reflection[2]=1;
                    static int reported_reflection;
                    if (!reported_reflection) {
                        LOGE("XR road reflection uses per-eye projection and projective UVs\n");
                        reported_reflection=1;
                    }
                }
            }
            static unsigned traces;
            if (!s_eye && traces++ < 12)
                LOGE("XR perspective: frame %u program %u scales %.5f %.5f eye %d\n",
                     s_frame,b->vp_start,vb->vr_params[0],vb->vr_params[1],s_eye);
        }
        /* The front end uses a 2x-AA target and stays on the readable cinema
         * screen. The race HUD uses the captured passthrough program at slot 0.
         * Put its textured elements on a smaller, seated view plane. Render-
         * target composites (bloom etc.) must retain their full-screen UVs. */
        else if (xr_stereo() && quest_xr_tracking() && s_stereo_frame &&
                 s->va==s_world_color_va &&
                 !xr_fullscreen_quad(b) &&
                 (b->xform==0 || (b->xform==2 && (b->vp_start==0 ||
                    (b->vp_const_count>=100 && program_has_tracked_mvp(p,b) &&
                     fabsf(b->vp_consts[99][0])+fabsf(b->vp_consts[99][1])+fabsf(b->vp_consts[99][2])<1e-6f)))) && b->tex_va[0] &&
                 s->aa_sx==1 && s->w==640 && s->h==480 &&
                 (!sampled[0] || (sampled[0]->w==256 && sampled[0]->h==64 &&
                    !r[0x30c/4] && b->vertex_count==4 && r[0x1e70/4]==1)) &&
                 !sampled[1] && !sampled[2] && !sampled[3]) {
            quest_xr_matrix(s_eye,vb->vr_matrix);
            /* CPU-transformed depth-tested particles/shadows still carry
             * perspective w. They need the same eye transform as the meshes. */
            if(r[0x30C/4] && s_game_projection[0]>0 && (b->xform==0 || b->vp_start==0)) {
                vb->vr_params[0]=s_game_projection[0]; vb->vr_params[1]=s_game_projection[1];
                vb->vr_params[2]=1;
                memcpy(vb->vr_depth,s_game_depth,sizeof vb->vr_depth);
            } else vb->vr_params[2]=2;
        }
        if(vb->vr_params[2]>1.5f) {
            uint32_t x0=(r[0x200/4]&0xffff)*s->aa_sx, y0=(r[0x204/4]&0xffff)*s->aa_sy;
            uint32_t x1=x0+(r[0x200/4]>>16)*s->aa_sx, y1=y0+(r[0x204/4]>>16)*s->aa_sy;
            if(x1>s->w) x1=s->w;
            if(y1>s->h) y1=s->h;
            if(x1>x0 && y1>y0) {
                vb->vr_clip[0]=(float)x0; vb->vr_clip[1]=(float)y0;
                vb->vr_clip[2]=(float)x1; vb->vr_clip[3]=(float)y1;
            }
            /* The scissor belongs to the original 2D plane. Clip there in
             * the shader, not against its old location in the eye image. */
            VkRect2D sc={{0,0},{s->pw,s->ph}};
            if(memcmp(&sc,&s_sc_cur,sizeof sc)) {
                vkCmdSetScissor(s_cb,0,1,&sc); s_sc_cur=sc;
            }
        }
        if(s_multiview && vb->vr_params[2]>0) quest_xr_matrix(1,vb->vr_matrix_right);
        xr_set_shading_rate(vb->vr_params[2]==1.0f);
        if(!s_eye && s->aa_sx==1 && s->w==640 && s->h==480)
            ++s_xr_kind[vb->vr_params[2]>0 ? (vb->vr_params[2]>1 ? 4:3) : b->xform<3 ? b->xform:0];
        if(!s_eye && s_xr_trace) {
            if(s->w==640 && s->h==480 && s->aa_sx==1) ++s_xr_trace_draw;
            fprintf(s_xr_trace,"%u,%08x,%u,%u,%u,%.0f,%u,%u,%u,%u,%x,%x,%x,%08x,%08x,%08x,%08x,%u,%u,%u,%u,%.6g,%.6g,%.6g,%.6g,%x,%d,%08x,%x,%u,%u,%u,%08x,%08x",
                s_xr_trace_draw,s->va,s->w,s->h,b->vp_start,vb->vr_params[2],b->vertex_count,
                r[0x30c/4],r[0x35c/4],r[0x304/4],r[0x344/4],r[0x348/4],r[0x1e70/4],
                b->tex_va[0],b->tex_va[1],b->tex_va[2],b->tex_va[3],
                sampled[0]?sampled[0]->w:0,sampled[1]?sampled[1]->w:0,sampled[2]?sampled[2]->w:0,sampled[3]?sampled[3]->w:0,
                b->vp_consts[99][0],b->vp_consts[99][1],b->vp_consts[99][2],b->vp_consts[99][3],r[0x358/4],xr_fullscreen_quad(b),
                b->zeta_va,r[0x354/4],r[0x32c/4],r[0x308/4],r[0x338/4],r[0x384/4],r[0x388/4]);
            fprintf(s_xr_trace,",%d,%d,%u,%u,%.9g,%.9g",s_sc_cur.offset.x,s_sc_cur.offset.y,
                s_sc_cur.extent.width,s_sc_cur.extent.height,vb->vr_params[0],vb->vr_params[1]);
            for(int row=96;row<100;++row) for(int c=0;c<4;++c) fprintf(s_xr_trace,",%.9g",b->vp_consts[row][c]);
            for(int row=113;row<116;++row) for(int c=0;c<4;++c) fprintf(s_xr_trace,",%.9g",b->vp_consts[row][c]);
            for(int c=0;c<4;++c) fprintf(s_xr_trace,",%.9g",b->vp_consts[160][c]);
            fputc('\n',s_xr_trace);
        }
#endif
        ub[0].buffer = s_f->ring;
        ub[0].offset = vat;
        ub[0].range = sizeof(VsBlock);

        {
            VkDeviceSize fat = ring_alloc(sizeof(FsBlock), s_ubo_align, &map);
            fb = (FsBlock *)map;
            if (!fb) return;
#ifdef NFSU2_PCVR
            gpu_fb=map;
            if(cpu_uniforms) fb=&cpu_fb;
#endif
            memset(fb, 0, sizeof *fb);
            for (i = 0; i < 4; i++) {
                fb->tscale[i][0] = tscale[i][0];
                fb->tscale[i][1] = tscale[i][1];
                fb->tscale[i][2] = sampled[i] && sampled[i]->layers==2 ? 1.0f : 0.0f;
            }
            for (i = 0; i < 8; i++) {
                argb_vec4(r[0xA60 / 4 + i], fb->c0[i]);
                argb_vec4(r[0xA80 / 4 + i], fb->c1[i]);
            }
            argb_vec4(r[0x1E20 / 4], fb->fc0);
            argb_vec4(r[0x1E24 / 4], fb->fc1);
            {
                uint32_t fc = r[0x2A8 / 4];
                fb->fog[0] = byte_f(fc, 0); fb->fog[1] = byte_f(fc, 8);
                fb->fog[2] = byte_f(fc, 16); fb->fog[3] = byte_f(fc, 24);
            }
            fb->afunc = r[0x300 / 4] ? (int32_t)r[0x33C / 4] : 0x207;
            fb->aref = (float)(r[0x340 / 4] & 0xFF);
            ub[1].buffer = s_f->ring;
            ub[1].offset = fat;
            ub[1].range = sizeof(FsBlock);
        }
        /* nv2a_clip emits nontrivial vClip only for the tracked HUD plane.
         * All other paths emit vec4(1). Alpha testing stays intact for every
         * function that can reject; texture-mode CLIPPLANE discards are also
         * untouched. No forced early tests (which would change depth writes). */
        if(fragment_specialization()) {
            if(fb->afunc==0x207) key.fragment_flags|=1;
            if(vb->vr_params[2]<=1.5f) key.fragment_flags|=2;
        }
#ifdef NFSU2_PCVR
        if(cpu_uniforms) {
            memcpy(gpu_vb,vb,sizeof *vb);
            memcpy(gpu_fb,fb,sizeof *fb);
        }
#endif
    }
    if(s->layers==2) key.fragment_flags|=8;
    pipe = pipe_get(&key);
    if (!pipe) return;
    if(!shared_instances()) bind_draw_pipeline(pipe);
#ifdef NFSU2_OPENXR
    ++s_fragment_variants[key.fragment_flags & 3];
#endif
    if (!upload_vertices(b))
        return;
#ifdef NFSU2_OPENXR
    if (xr_geometry_live() && s_geometry_mode == 1 && s_xr_geometry->index_count == n) {
        XrGeometry *g = s_xr_geometry;
        vkCmdBindIndexBuffer(s_cb, g->buffer, g->index_at, g->index_type);
        ++s_geometry_reused;
        s_geometry_saved += g->vertex_bytes + (VkDeviceSize)n * (g->index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4);
    } else
#endif
    {
        VkDeviceSize iat;
        VkIndexType index_type = b->vertex_count <= 65536 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        if (b->vertex_count <= 65536) {
            uint16_t *o;
            iat = ring_alloc((VkDeviceSize)n * 2, 4, &map);
            o = (uint16_t *)map;
            if (!o) return;
            for (i = 0; i < n; i++)
                o[i] = (uint16_t)idx[i];
            vkCmdBindIndexBuffer(s_cb, s_f->ring, iat, VK_INDEX_TYPE_UINT16);
        } else {
            iat = ring_alloc((VkDeviceSize)n * 4, 4, &map);
            if (!map) return;
            memcpy(map, idx, (size_t)n * 4);
            vkCmdBindIndexBuffer(s_cb, s_f->ring, iat, VK_INDEX_TYPE_UINT32);
        }
#ifdef NFSU2_OPENXR
        if (s_xr_geometry && !s_eye && s_xr_geometry->generation == s_cb_gen) {
            s_xr_geometry->index_at = iat;
            s_xr_geometry->index_count = n;
            s_xr_geometry->index_type = index_type;
            s_xr_geometry->valid = 1;
        } else if (xr_geometry_live() && s_geometry_mode >= 2) {
            XrGeometry *g = s_xr_geometry;
            if (g->index_count != n || g->index_type != index_type ||
                memcmp(s_f->ring_ptr + g->index_at, map, (size_t)n * (index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4)))
                xr_geometry_mismatch("index");
            else {
                ++s_geometry_verified;
                if (s_stereo_frame && quest_xr_tracking()) ++s_geometry_race_verified;
            }
        } else if (s_xr_geometry && s_eye && s_xr_geometry->valid) ++s_geometry_expired;
#endif
    }
    /* The guest constants are identical for one draw's two eyes. The headset
     * transform lives in VsBlock and is still uploaded separately per eye. */
#ifdef NFSU2_OPENXR
    if (xr_geometry_live() && s_xr_geometry->constants_valid && xr_uniform_mode()==1) {
        ub[2]=(VkDescriptorBufferInfo){s_f->ring,s_xr_geometry->constants_at,VK_CONST_SIZE};
        if(shared_instances()) {
            XrGeometry *g=s_xr_geometry, *root=g->instance_root ? g->instance_root : g;
            inst_room=quest_instance_span(root->constants_at,root->instance_count,
                g->constants_at,VK_CONST_SIZE,RING_BYTES);
            if(!inst_room) inst_room=1;
            ub[2].range=(VkDeviceSize)inst_room*VK_CONST_SIZE;
        }
        ++s_uniform_reused;
    } else
#endif
    {
        /* Constants last: repeats append theirs right behind (pend_append). */
        VkDeviceSize cat = ring_alloc(VK_CONST_SIZE, s_ubo_align, &map), range;
        if (!map) return;
        memcpy(map, b->vp_consts, VK_CONST_SIZE);
#ifdef NFSU2_OPENXR
        if (s_xr_geometry && !s_eye && s_xr_geometry->generation==s_cb_gen) {
            s_xr_geometry->constants_at=cat;
            s_xr_geometry->constants_valid=1;
        } else if (xr_geometry_live() && s_xr_geometry->constants_valid && xr_uniform_mode()>=2) {
            if(memcmp(s_f->ring_ptr+s_xr_geometry->constants_at,map,VK_CONST_SIZE)) {
                ++s_uniform_failed; s_uniform_mode=0;
                LOGE("stereo constant mismatch: disabling uniform reuse\n");
            } else {
                ++s_uniform_verified;
                if(s_stereo_frame && quest_xr_tracking()) ++s_uniform_race_verified;
            }
        }
#endif
        range = (VkDeviceSize)VK_CONST_SIZE * VK_INST_MAX;
        if(single_instance_shader()) range=VK_CONST_SIZE;
        if (range > s_ubo_range) range = s_ubo_range;
        if (cat + range > RING_BYTES) range = RING_BYTES - cat;
        range = range / VK_CONST_SIZE * VK_CONST_SIZE;
        inst_room = (uint32_t)(range / VK_CONST_SIZE);
        ub[2].buffer = s_f->ring;
        ub[2].offset = cat;
        ub[2].range = range;
    }
#ifdef NFSU2_OPENXR
    if(s_dynamic_descriptors) {
        if(!bind_dynamic_descriptors(&descriptors)) {
            LOGE("dynamic descriptor allocation failed\n");
            return;
        }
    } else if (s_push_template && !state_cache_enabled()) {
        p_push_template(s_cb, s_push_template, s_layout, 0, &descriptors);
        s_image_writes += 4;
        ++s_template_draws;
    } else
#endif
    {
        memset(wr, 0, sizeof wr);
        uint32_t write_count = 0;
        for (i = 0; i < 7; i++) {
            if (i >= 2 && i < 6 && state_cache_enabled() && descriptor_gen == s_cb_gen &&
                img[i-2].sampler == last_images[i-2].sampler &&
                img[i-2].imageView == last_images[i-2].imageView &&
                img[i-2].imageLayout == last_images[i-2].imageLayout) {
                ++s_image_reuses;
                continue;
            }
            VkWriteDescriptorSet *w = &wr[write_count++];
            w->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w->dstBinding = i;
            w->descriptorCount = 1;
            if (i < 2 || i == 6) {
                w->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w->pBufferInfo = &ub[i == 6 ? 2 : i];
            } else {
                w->descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w->pImageInfo = &img[i - 2];
                ++s_image_writes;
            }
        }
        p_push_desc(s_cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s_layout, 0, write_count, wr);
        memcpy(last_images, img, sizeof last_images); descriptor_gen = s_cb_gen;
    }

    VT("  vkCmdDrawIndexed %u\n", n);
#ifdef NFSU2_OPENXR
    if(s_f->prof_capture && s_f->prof_open) {
        GpuPassDetail *detail=&s_f->prof_detail[s_f->prof_count];
        if(!detail->draws) detail->first_shader=pk.shader_program;
        ++detail->draws; detail->vertices+=b->vertex_count; detail->indices+=n;
        detail->blend+=key.blend!=0; detail->alpha+=r[0x300/4] && r[0x33c/4]!=0x207;
        s_f->prof_tracked |= s_stereo_frame && quest_xr_tracking();
    }
#endif
    /* The constants were the ring's last allocation, so a repeat's set
     * lands right behind them (pend_append). */
    if (pend_eligible(b) && inst_room > 1) {
        pend_hold(b, n, ub[2].offset, inst_room, &key, pipe);
    } else {
        bind_draw_pipeline(pipe);
        vkCmdDrawIndexed(s_cb, n, 1, 0, 0, 0);
        s_st_calls++;
    }
    if (s_vtrace > 0)
        s_vtrace--;
#ifdef NFSU2_OPENXR
    if(feedback_swapped) xr_feedback_verify(b,s);
#endif
}

#ifdef NFSU2_OPENXR
/* Opt-in diagnostic only: replay this same immutable draw against the original
 * image using the copy path, then compare ALL RGBA bytes in BOTH eyes. The
 * reference result is retained for subsequent game draws. Disable on any
 * mismatch or readback failure; never enter a timed comparison with a failure. */
static void xr_feedback_verify(const Nv2aRawBatch *b, VkSurf *s)
{
    static int option=-1;
    if(option<0) { const char *e=getenv("NFSU2_VR_FEEDBACK_VERIFY"); option=e && *e=='1'; }
    if(!option || s_feedback_check_active || s_feedback_verified>=8) return;
    end_rendering();
    VkImage image=s->image; s->image=s_feedback_spare.image; s_feedback_spare.image=image;
    VkImageView view=s->view; s->view=s_feedback_spare.view; s_feedback_spare.view=view;
    VkDeviceMemory mem=s->mem; s->mem=s_feedback_spare.mem; s_feedback_spare.mem=mem;
    s_feedback_check_active=1;
    vk_draw_raw_eye(b);
    s_feedback_check_active=0;
    end_rendering();
    VkDeviceSize bytes=(VkDeviceSize)s->pw*s->ph*s->layers*4;
    VkBufferCreateInfo ci={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size=bytes*2; ci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    int equal=0;
    if(vkCreateBuffer(s_dev,&ci,NULL,&buffer)==VK_SUCCESS) {
        VkMemoryRequirements rq; vkGetBufferMemoryRequirements(s_dev,buffer,&rq);
        memory=alloc_mem(rq,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if(memory && vkBindBufferMemory(s_dev,buffer,memory,0)==VK_SUCCESS) {
            barrier_all();
            VkBufferImageCopy copy={0};
            copy.imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,s->layers};
            copy.imageExtent=(VkExtent3D){s->pw,s->ph,1};
            vkCmdCopyImageToBuffer(s_cb,s->image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&copy);
            copy.bufferOffset=bytes;
            vkCmdCopyImageToBuffer(s_cb,s_feedback_spare.image,VK_IMAGE_LAYOUT_GENERAL,buffer,1,&copy);
            VkMemoryBarrier host={VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(s_cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
            flush_frame_wait();
            void *mapped;
            if(vkMapMemory(s_dev,memory,0,bytes*2,0,&mapped)==VK_SUCCESS) {
                equal=!memcmp(mapped,(uint8_t *)mapped+bytes,(size_t)bytes);
                if(!equal) {
                    size_t differences=0,first=0;
                    for(size_t i=0;i<(size_t)bytes;++i) if(((uint8_t *)mapped)[i]!=((uint8_t *)mapped)[bytes+i]) {
                        if(!differences) first=i;
                        ++differences;
                    }
                    LOGE("XR feedback verification mismatch: %zu bytes, first %zu, reference %u swap %u\n",
                         differences,first,((uint8_t *)mapped)[first],((uint8_t *)mapped)[bytes+first]);
                }
                vkUnmapMemory(s_dev,memory);
            }
        }
    }
    if(buffer) vkDestroyBuffer(s_dev,buffer,NULL);
    if(memory) vkFreeMemory(s_dev,memory,NULL);
    if(equal) LOGE("XR feedback verification %u/8: %llu RGBA bytes identical, %u layers\n",
                  ++s_feedback_verified,(unsigned long long)bytes,s->layers);
    else {
        s_feedback_failed=1; s_feedback_swap=0;
        LOGE("XR feedback verification failed: target swapping disabled\n");
    }
}

typedef struct XrDrawCopy {
    struct XrDrawCopy *next;
    Nv2aRawBatch batch;
    const uint8_t *vertex_ids[NV2A_RAW_ATTRS];
    XrGeometry *geometry;
} XrDrawCopy;
static XrDrawCopy *s_xr_first, *s_xr_tail;
static size_t s_xr_bytes;
static QuestDrawArena s_xr_arena;

static int xr_shared_probe(const Nv2aSurface *sf)
{
    uint32_t bpp=sf->bytes_per_pixel?sf->bytes_per_pixel:4;
    uint32_t w=sf->pitch?sf->pitch/bpp:sf->width;
    uint32_t h=sf->clip_y+sf->height;
    return (w==h && w<=256) || (w==256 && h==64);
}

/* Finish one render target for the right eye before its results are sampled
 * by another pass. This avoids ending a tile render pass twice per draw on
 * Adreno. Snapshot all guest-backed inputs: the executor reuses them immediately. */
static void xr_flush_draws(void)
{
    const uint32_t *saved_regs=s_regs;
    const Nv2aRawBatch *saved_batch=s_batch;
    pend_flush();
    s_eye=1;
    s_xr_replaying=1;
    while(s_xr_first) {
        XrDrawCopy *node=s_xr_first;
        s_xr_first=node->next;
        memcpy(s_xr_vertex_ids,node->vertex_ids,sizeof s_xr_vertex_ids);
        s_xr_geometry = xr_geometry_mode() && (s_inst_on == 0 || shared_instances()) && !state_cache_enabled() ? node->geometry : NULL;
        s_xr_immutable_regs=node->batch.regs;
        s_xr_immutable_batch=&node->batch;
        uint64_t t0=hz_now();
        vk_draw_raw_eye(&node->batch);
        s_xr_geometry = NULL;
        s_xr_immutable_regs=NULL;
        s_xr_immutable_batch=NULL;
        uint64_t elapsed=hz_now()-t0;
        xr_add_time(&s_xr_draw_ns,elapsed,0); s_compare_cpu_ns+=elapsed;
    }
    pend_flush();
    /* Replayed inputs have now been copied into the fenced Vulkan ring. */
    quest_draw_reset(&s_xr_arena);
    s_xr_replaying=0;
    s_eye=0;
    s_xr_tail=NULL; s_xr_bytes=0;
    s_regs=saved_regs; s_batch=saved_batch;
}

static XrDrawCopy *xr_copy_draw(const Nv2aRawBatch *b)
{
    if(!b->vertex_count) return NULL;
    VRun runs[NV2A_RAW_ATTRS]; int of_run[NV2A_RAW_ATTRS];
    uint32_t nr=vertex_runs(b,b->attr_direct,runs,of_run);
    size_t node_size=(sizeof(XrDrawCopy)+15)&~(size_t)15;
    size_t header=node_size;
    if (xr_geometry_mode()) header += (sizeof(XrGeometry)+15)&~(size_t)15;
    size_t bytes=header+0x2000+(size_t)b->vp_slots*16+VK_CONST_SIZE;
    size_t indices=b->indices?(size_t)b->index_count*4:0;
    size_t attrs=(b->attr_present & ~b->attr_direct)?(size_t)b->vertex_count*NV2A_RAW_ATTRS*16:0;
    bytes+=(indices+15)&~(size_t)15; bytes+=attrs;
    for(uint32_t i=0;i<nr;++i) bytes+=((size_t)(runs[i].hi-runs[i].lo)+15)&~(size_t)15;
    if(s_xr_bytes+bytes>64u*1024*1024) xr_flush_draws();
    uint64_t t0=hz_now();
    XrDrawCopy *node=quest_draw_alloc(&s_xr_arena,bytes);
    if(!node) return NULL;
    node->next=NULL; node->batch=*b;
    node->geometry = header > node_size ? (XrGeometry *)((uint8_t *)node + node_size) : NULL;
    if (node->geometry) {
        node->geometry->valid=node->geometry->constants_valid=0;
        node->geometry->instance_root=NULL; node->geometry->instance_count=1;
    }
    for(uint32_t a=0;a<NV2A_RAW_ATTRS;++a) node->vertex_ids[a]=b->direct[a].ptr;
    uint8_t *dst=(uint8_t *)node+header;
    node->batch.regs=(const uint32_t *)dst; memcpy(dst,b->regs,0x2000); dst+=0x2000;
    node->batch.vp_program=(const uint32_t (*)[4])dst;
    memcpy(dst,b->vp_program,(size_t)b->vp_slots*16); dst+=(size_t)b->vp_slots*16;
    node->batch.vp_consts=(const float (*)[4])dst;
    memset(dst,0,VK_CONST_SIZE);
    memcpy(dst,b->vp_consts,b->vp_const_count<192?(size_t)b->vp_const_count*16:VK_CONST_SIZE);
    dst+=VK_CONST_SIZE;
    if(indices) { node->batch.indices=(const uint32_t *)dst; memcpy(dst,b->indices,indices); dst+=(indices+15)&~(size_t)15; }
    if(attrs) { node->batch.attrs=(const float *)dst; memcpy(dst,b->attrs,attrs); dst+=attrs; }
    for(uint32_t i=0;i<nr;++i) {
        size_t n=(size_t)(runs[i].hi-runs[i].lo);
        memcpy(dst,runs[i].lo,n);
        for(uint32_t a=0;a<NV2A_RAW_ATTRS;++a)
            if((b->attr_direct&(1u<<a)) && of_run[a]==(int)i)
                node->batch.direct[a].ptr=dst+(b->direct[a].ptr-runs[i].lo);
        dst+=(n+15)&~(size_t)15;
    }
    s_xr_bytes+=bytes;
    s_xr_snapshot_ns+=hz_now()-t0;
    return node;
}
#endif

static void vk_draw_raw(const Nv2aRawBatch *b)
{
    if(!ready()) return;
    s_eye=0;
#ifdef NFSU2_OPENXR
    if(s_multiview) {
        uint64_t t0=hz_now();
        vk_draw_raw_eye(b);
        uint64_t elapsed=hz_now()-t0;
        xr_add_time(&s_xr_draw_ns,elapsed,0); s_compare_cpu_ns+=elapsed;
        xr_trace_step_capture();
        return;
    }
    if(xr_stereo()) {
        if(s_xr_tail && (memcmp(&s_xr_tail->batch.surface,&b->surface,sizeof b->surface) ||
                         s_xr_tail->batch.zeta_va!=b->zeta_va)) xr_flush_draws();
        if(xr_shared_probe(&b->surface)) { vk_draw_raw_eye(b); return; }
        XrDrawCopy *copy=xr_copy_draw(b);
        s_xr_geometry = copy && xr_geometry_mode() && (s_inst_on == 0 || shared_instances()) && !state_cache_enabled() ? copy->geometry : NULL;
        s_xr_immutable_regs=copy?copy->batch.regs:NULL;
        s_xr_immutable_batch=copy?&copy->batch:NULL;
        s_xr_snapshot_source=copy && cpu_instance_mode();
        if(s_xr_snapshot_source) memcpy(s_xr_vertex_ids,copy->vertex_ids,sizeof s_xr_vertex_ids);
        uint64_t t0=hz_now();
        vk_draw_raw_eye(s_xr_snapshot_source?&copy->batch:b);
        s_xr_snapshot_source=0;
        s_xr_geometry = NULL;
        s_xr_immutable_regs=NULL;
        s_xr_immutable_batch=NULL;
        s_regs=b->regs; s_batch=b;
        uint64_t elapsed=hz_now()-t0;
        xr_add_time(&s_xr_draw_ns,elapsed,0); s_compare_cpu_ns+=elapsed;
        xr_trace_step_capture();
        if(copy) {
            if(s_xr_tail) s_xr_tail->next=copy; else s_xr_first=copy;
            s_xr_tail=copy;
        } else {
            xr_flush_draws(); s_eye=1; vk_draw_raw_eye(b); s_eye=0;
        }
        return;
    }
#endif
    vk_draw_raw_eye(b);
}

static void vk_clear_eye(const Nv2aSurface *sf, const Nv2aRenderState *rs,
                     uint32_t flags, uint32_t argb, uint32_t zstencil)
{
    VkSurf *s;
    VkDepthBuf *d;
    VkClearAttachment ca[2];
    VkClearRect rect;
    uint32_t nca = 0;

    if (!ready())
        return;
    pend_flush();
    s = target(sf, rs ? rs->zeta_va : 0, &d);
    if (!s)
        return;
    memset(&rect, 0, sizeof rect);
    rect.layerCount = 1;
    rect.rect.extent.width = s->pw;
    rect.rect.extent.height = s->ph;
    if (s_regs) {
        uint32_t hz = s_regs[0x1D98 / 4], vt = s_regs[0x1D9C / 4];
        uint32_t x0 = (hz & 0xFFFF) * s->aa_sx, x1 = ((hz >> 16) + 1) * s->aa_sx;
        uint32_t y0 = (vt & 0xFFFF) * s->aa_sy, y1 = ((vt >> 16) + 1) * s->aa_sy;
        if (x1 > s->w) x1 = s->w;
        if (y1 > s->h) y1 = s->h;
        if (x1 > x0 && y1 > y0) {
            uint32_t px0 = to_stored(x0, s->pw, s->w), py0 = to_stored(y0, s->ph, s->h);
            rect.rect.offset.x = (int32_t)px0;
            rect.rect.offset.y = (int32_t)py0;
            rect.rect.extent.width = to_stored(x1, s->pw, s->w) - px0;
            rect.rect.extent.height = to_stored(y1, s->ph, s->h) - py0;
        }
    }
    if (d && rect.rect.extent.width > d->pw - (uint32_t)rect.rect.offset.x)
        rect.rect.extent.width = d->pw - (uint32_t)rect.rect.offset.x;
    if (d && rect.rect.extent.height > d->ph - (uint32_t)rect.rect.offset.y)
        rect.rect.extent.height = d->ph - (uint32_t)rect.rect.offset.y;
    memset(ca, 0, sizeof ca);
    if ((flags & 0xF0) == 0xF0) {
        ca[nca].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ca[nca].colorAttachment = 0;
        argb_vec4(argb, ca[nca].clearValue.color.float32);
        nca++;
    } else if (flags & 0xF0) {
        static int warned;
        if (!warned++)
            LOGE("clear with a partial colour mask (0x%X) not done yet\n", flags & 0xF0);
    }
    if (d && (flags & 0x3)) {
        uint32_t zmax = s_regs ? zmax_of(s_regs) : 0xFFFFFFu;
        uint32_t z = zmax == 0xFFFFu ? (zstencil & 0xFFFF) : (zstencil >> 8);
        ca[nca].aspectMask = ((flags & 1) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                             ((flags & 2) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
        ca[nca].clearValue.depthStencil.depth = (float)((double)z / (double)zmax);
        ca[nca].clearValue.depthStencil.stencil = zstencil & 0xFF;
        nca++;
    }
    if (!nca)
        return;
    VT("clear %08X %ux%u flags %X rect %d,%d %ux%u depth %p\n", s->va, s->pw, s->ph, flags,
       rect.rect.offset.x, rect.rect.offset.y, rect.rect.extent.width, rect.rect.extent.height, (void *)d);
#ifdef NFSU2_OPENXR
    static int load_clear=-1;
    if(load_clear<0) {
        const char *e=getenv("NFSU2_VR_LOAD_CLEAR");
        load_clear=e && *e=='1';
    }
    if(load_clear && (!s_in_render || s_rt!=s || s_rt_depth!=d) &&
       rect.rect.offset.x==0 && rect.rect.offset.y==0 &&
       rect.rect.extent.width==s->pw && rect.rect.extent.height==s->ph &&
       (!d || (d->pw==s->pw && d->ph==s->ph))) {
        begin_rendering_clear(s,d,ca,nca);
        ++s_xr_load_clears;
    } else
#endif
    {
        begin_rendering(s, d);
        vkCmdClearAttachments(s_cb, nca, ca, 1, &rect);
    }
    VT("  cleared\n");
    s->dirty = 1;
    s->gen++;
}

static void vk_clear(const Nv2aSurface *sf, const Nv2aRenderState *rs,
                     uint32_t flags, uint32_t argb, uint32_t zstencil)
{
#ifdef NFSU2_OPENXR
    xr_flush_draws();
#endif
    s_eye=0;
    vk_clear_eye(sf,rs,flags,argb,zstencil);
#ifdef NFSU2_OPENXR
    if(xr_stereo() && !s_multiview && !xr_shared_probe(sf)) { s_eye=1; vk_clear_eye(sf,rs,flags,argb,zstencil); s_eye=0; }
#endif
}

static void dump_bmp(const char *path, const uint8_t *bgra, uint32_t w, uint32_t h)
{
    FILE *f = fopen(path, "wb");
    uint8_t hdr[54] = { 'B', 'M' };
    uint32_t row = w * 3, pad = (4 - (row & 3)) & 3, size = 54 + (row + pad) * h, y, x;
    if (!f)
        return;
    memcpy(hdr + 2, &size, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);
    for (y = h; y-- > 0; ) {
        const uint8_t *p = bgra + (size_t)y * w * 4;
        for (x = 0; x < w; x++)
            fwrite(p + x * 4, 1, 3, f);
        fwrite("\0\0\0", 1, pad, f);
    }
    fclose(f);
}

/* RECOMP_GL_DUMP: read the surface back (waits for the GPU). */
static void dump_surface_layer(VkSurf *s, const char *path, uint32_t layer);
static void dump_surface(VkSurf *s, const char *path)
{
    dump_surface_layer(s,path,0);
}

static void dump_surface_layer(VkSurf *s, const char *path, uint32_t layer)
{
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkBuffer buf;
    VkDeviceMemory mem;
    VkMemoryRequirements rq;
    VkBufferImageCopy rg;
    void *ptr;

    bci.size = (VkDeviceSize)s->pw * s->ph * 4;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(s_dev, &bci, NULL, &buf) != VK_SUCCESS)
        return;
    vkGetBufferMemoryRequirements(s_dev, buf, &rq);
    mem = alloc_mem(rq, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!mem) { vkDestroyBuffer(s_dev, buf, NULL); return; }
    vkBindBufferMemory(s_dev, buf, mem, 0);
    end_rendering();
    barrier_all();
    memset(&rg, 0, sizeof rg);
    rg.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rg.imageSubresource.layerCount = 1;
    rg.imageSubresource.baseArrayLayer = layer < s->layers ? layer : 0;
    rg.imageExtent.width = s->pw;
    rg.imageExtent.height = s->ph;
    rg.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(s_cb, s->image, VK_IMAGE_LAYOUT_GENERAL, buf, 1, &rg);
    flush_frame_wait();
    if (vkMapMemory(s_dev, mem, 0, bci.size, 0, &ptr) == VK_SUCCESS) {
        dump_bmp(path, (const uint8_t *)ptr, s->pw, s->ph);
        vkUnmapMemory(s_dev, mem);
    }
    vkDestroyBuffer(s_dev, buf, NULL);
    vkFreeMemory(s_dev, mem, NULL);
}

static void vk_flip(void)
{
    static int dump_every = -1;
    static char dump_prefix[256];
#ifdef NFSU2_OPENXR
    xr_flush_draws();
#endif
    VkSurf *s = s_last;
#ifdef NFSU2_OPENXR
    if (xr_stereo()) s=s_last_eye[0];
#endif
    uint32_t idx = 0;
    int have_image = 0;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkPipelineStageFlags ws = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (!ready())
        return;
    pend_flush();
    s_frame++;
    if (s_inst_alt)
        s_inst_on = s_frame & 1;
    {
        static int st = -1;
        static uint32_t st_frame;
        if (st < 0) {
            const char *e = getenv("RECOMP_DRAW_STATS");
            if(!e) e=getenv("RECOMP_VK_DRAW_STATS");
            st = e && *e == '1';
        }
        if (st && s_frame - st_frame >= 300) {
            fprintf(stderr, "[vk] per frame: %u draws -> %u draw calls (%u instances appended)\n",
                    s_st_draws / (s_frame - st_frame), s_st_calls / (s_frame - st_frame),
                    s_st_inst / (s_frame - st_frame));
            s_st_draws = s_st_calls = s_st_inst = 0;
            fprintf(stderr, "[vk] state cache: vertex layouts %u set / %u reused, image descriptors %u written / %u reused\n",
                    s_layout_sets, s_layout_reuses, s_image_writes, s_image_reuses);
#ifdef NFSU2_OPENXR
            if (s_push_template)
                fprintf(stderr, "[vk] full descriptor template: %u draws (all bindings written)\n", s_template_draws);
            s_template_draws = 0;
#endif
            s_layout_sets = s_layout_reuses = s_image_writes = s_image_reuses = 0;
            st_frame = s_frame;
        }
    }
    {
        static uint64_t last_ns, window_ns;
        static int lines;
        uint64_t now = hz_now();
#ifdef NFSU2_OPENXR
        if (quest_xr_enabled()) {
            xr_frame_times(now,s_stereo_frame);
            render_compare_tick(now,s_stereo_frame);
            gpu_capture_tick(s_stereo_frame);
        }
#endif
        if (last_ns) {
            uint64_t dt = now - last_ns;
            if (now - window_ns > 10000000000ull) { window_ns = now; lines = 0; }
            if (dt > 50000000ull && lines++ < 30)
                fprintf(stderr, "[hitch] frame %u: %.0f ms -- %u programs compiled (%.0f ms),"
                        " %u textures %.1f MB (%.0f ms), %u draws\n", s_frame, dt / 1e6,
                        s_hz_progs, s_hz_prog_ns / 1e6, s_hz_texs,
                        s_hz_tex_bytes / 1048576.0, s_hz_tex_ns / 1e6, s_hz_draws);
        }
#ifdef NFSU2_PCVR
        {
            static uint64_t previous[5];
            static uint64_t previous_main_wait,previous_trap_wait;
            extern uint64_t xbox_pcvr_main_wait_ns(void);
            extern uint64_t nv2a_pcvr_trap_wait_ns(void);
            uint64_t main_wait=xbox_pcvr_main_wait_ns(),trap_wait=nv2a_pcvr_trap_wait_ns();
            if(last_ns && now-last_ns>50000000ull && getenv("NFSU2_PCVR_HEALTH"))
                fprintf(stderr,"[PCVR hitch] draw=%.3f fence=%.3f XR begin=%.3f end=%.3f statistics=%.3f main guest-lock wait=%.3f GPU trap wait=%.3f pipelines=%u/%.3f cache=%.3f ms\n",
                    (s_pcvr_phase_ns[0]-previous[0])/1e6,(s_pcvr_phase_ns[1]-previous[1])/1e6,
                    (s_pcvr_phase_ns[2]-previous[2])/1e6,(s_pcvr_phase_ns[3]-previous[3])/1e6,
                    (s_pcvr_phase_ns[4]-previous[4])/1e6,(main_wait-previous_main_wait)/1e6,
                    (trap_wait-previous_trap_wait)/1e6,s_pcvr_pipelines,s_pcvr_pipeline_ns/1e6,s_pcvr_cache_ns/1e6);
            memcpy(previous,s_pcvr_phase_ns,sizeof previous);
            previous_main_wait=main_wait; previous_trap_wait=trap_wait;
            s_pcvr_pipelines=0; s_pcvr_pipeline_ns=0; s_pcvr_cache_ns=0;
        }
        if(s_texture_profile>0) {
            static uint64_t texture_window;
            static unsigned texture_lines;
            if(now-texture_window>10000000000ull) { texture_window=now; texture_lines=0; }
            if(s_hz_tex_ns>2000000 && texture_lines++<24)
                fprintf(stderr,"[PCVR textures] frame=%u total=%.3f create=%.3f fill=%.3f account=%.3f ms; BC=%.3f swizzle=%.3f other=%.3f ms; new=%u changed=%u evicted=%u cache=%.2f MB; slowest=%02X %ux%u layers=%u %.3f ms\n",
                    s_frame,s_hz_tex_ns/1e6,s_texture_work.create_ns/1e6,s_texture_work.fill_ns/1e6,
                    s_texture_work.account_ns/1e6,s_texture_work.bc_ns/1e6,s_texture_work.swizzle_ns/1e6,
                    s_texture_work.other_ns/1e6,s_texture_work.created,s_texture_work.updated,s_texture_work.evicted,
                    s_tex_bytes/1048576.0,s_texture_work.color,s_texture_work.w,s_texture_work.h,
                    s_texture_work.layers,s_texture_work.max_ns/1e6);
            memset(&s_texture_work,0,sizeof s_texture_work);
        }
#endif
        last_ns = now;
        s_hz_progs = s_hz_texs = s_hz_draws = 0;
        s_hz_prog_ns = s_hz_tex_ns = s_hz_tex_bytes = 0;
    }
    {
        static int on = -1;
        static uint32_t last_frame;
        static struct timespec last;
        struct timespec now;
        if (on < 0) {
            const char *e = getenv("RECOMP_FPS_LOG");
            on = e && *e == '1';
            clock_gettime(CLOCK_MONOTONIC, &last);
        }
        if (on) {
            double dt;
            clock_gettime(CLOCK_MONOTONIC, &now);
            dt = (double)(now.tv_sec - last.tv_sec) + (now.tv_nsec - last.tv_nsec) / 1e9;
            if (dt >= 10.0) {
                extern void xbox_vblank_report(double, char *, size_t);
                char vb[128];
                xbox_vblank_report(dt, vb, sizeof vb);
                fprintf(stderr, "[fps] %.1f fps (frame %u), %s\n",
                        (s_frame - last_frame) / dt, s_frame, vb);
                last_frame = s_frame;
                last = now;
            }
        }
    }
    if (dump_every < 0) {
        const char *dd = getenv("RECOMP_GL_DUMP");
        dump_every = 0;
        if (dd && *dd) {
            const char *comma = strchr(dd, ',');
            size_t nn = comma ? (size_t)(comma - dd) : strlen(dd);
            if (nn >= sizeof dump_prefix) nn = sizeof dump_prefix - 1;
            memcpy(dump_prefix, dd, nn);
            dump_prefix[nn] = 0;
            dump_every = comma ? atoi(comma + 1) : 60;
            if (dump_every <= 0) dump_every = 60;
        }
    }
    end_rendering();
    s_rt = NULL;
    s_rt_depth = NULL;
#ifdef NFSU2_OPENXR
    xr_trace_flip(s);
#endif
    if (s && s_drew_any && dump_every && ((s_frame % (uint32_t)dump_every) == 0
                                          || (s_inst_alt && s_frame % (uint32_t)dump_every == 1))) {
        char path[320];
        snprintf(path, sizeof path, "%s%05u.bmp", dump_prefix, s_frame);
        dump_surface(s, path);
#ifdef NFSU2_OPENXR
        if(xr_stereo() && s_last_eye[1]) {
            snprintf(path,sizeof path,"%s%05u-right.bmp",dump_prefix,s_frame);
            dump_surface_layer(s_last_eye[1],path,s_multiview?1:0);
        }
#endif
    }
    barrier_all();

    VT("flip %u: surface %08X drew %d, acquiring\n", s_frame, s ? s->va : 0, s_drew_any);
    if (!s_headless) {
        VkResult rr = vkAcquireNextImageKHR(s_dev, s_swapchain, UINT64_MAX, s_f->acquired,
                                            VK_NULL_HANDLE, &idx);
        if (rr == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(s_dev);
            create_swapchain();
        } else if (rr == VK_SUCCESS || rr == VK_SUBOPTIMAL_KHR) {
            VkImage dst = s_sc_images[idx];
            VT("flip %u: acquired image %u\n", s_frame, idx);
            VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            VkImageSubresourceRange rg = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            VkClearColorValue black = { { 0, 0, 0, 1 } };
            have_image = 1;
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = dst;
            b.subresourceRange = rg;
            vkCmdPipelineBarrier(s_cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, NULL, 0, NULL, 1, &b);
            vkCmdClearColorImage(s_cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &rg);
            if (s && s_drew_any) {
                /* The logical aspect (16:9 for a widescreen title),
                 * letterboxed. Rows are top-first on both sides. */
                float lw = (float)s->w / (float)s->aa_sx, lh = (float)s->h / (float)s->aa_sy;
                float ww = (float)s_sc_extent.width, wh = (float)s_sc_extent.height, scale;
                VkImageBlit bl;
                int dw, dh, dx, dy;
                VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
                if (xbox_video_widescreen() && lw < lh * 1.5f)
                    lw = lh * 16.0f / 9.0f;
                scale = ww / lw < wh / lh ? ww / lw : wh / lh;
                dw = (int)(lw * scale); dh = (int)(lh * scale);
                dx = ((int)ww - dw) / 2; dy = ((int)wh - dh) / 2;
                mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                mb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(s_cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     0, 1, &mb, 0, NULL, 0, NULL);
                memset(&bl, 0, sizeof bl);
                bl.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                bl.srcSubresource.layerCount = 1;
                bl.srcOffsets[1].x = (int32_t)s->pw;
                bl.srcOffsets[1].y = (int32_t)s->ph;
                bl.srcOffsets[1].z = 1;
                bl.dstSubresource = bl.srcSubresource;
                bl.dstOffsets[0].x = dx;
                bl.dstOffsets[0].y = dy;
                bl.dstOffsets[1].x = dx + dw;
                bl.dstOffsets[1].y = dy + dh;
                bl.dstOffsets[1].z = 1;
                vkCmdBlitImage(s_cb, s->image, VK_IMAGE_LAYOUT_GENERAL, dst,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
            }
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = 0;
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(s_cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &b);
        }
    }
    VT("flip %u: submit (image %d)\n", s_frame, have_image);
#ifdef NFSU2_OPENXR
    if (quest_xr_enabled() && s && s_drew_any) {
        VkSurf *right=xr_stereo()?s_last_eye[1]:s;
        quest_xr_copy(s_cb,s->image,right?right->image:s->image,s->pw,s->ph,
                      s_stereo_frame && right && right->pw==s->pw && right->ph==s->ph,
                      s->aa_sx==2 && s->aa_sy==1 && s->w==1280 && s->h==480,s->layers);
    }
    s_stereo_frame=0;
    s_world_color_va=0;
    gpu_timer_end();
#endif
    vkEndCommandBuffer(s_cb);
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s_cb;
    if (have_image) {
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &s_f->acquired;
        si.pWaitDstStageMask = &ws;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &s_f->rendered;
    }
    if (vkQueueSubmit(s_queue, 1, &si, s_f->fence) != VK_SUCCESS) {
        LOGE("vkQueueSubmit failed\n");
        abort();
    }
    s_f->submitted = 1;
#ifdef NFSU2_OPENXR
    if (quest_xr_enabled()) {
        uint64_t t0=hz_now();
        if (s_xr_gpu_wait) {
            vkWaitForFences(s_dev, 1, &s_f->fence, VK_TRUE, UINT64_MAX);
            xr_add_time(&s_xr_wait_ns,hz_now()-t0,1);
        }
        /* All queue/OpenXR access is on this thread. The compositor orders
         * image consumption after the submitted Vulkan commands. Retain the
         * slot fence in frame_begin for ring/command-pool/garbage lifetime. */
        t0=hz_now();
        quest_xr_end();
        xr_add_time(&s_xr_end_ns,hz_now()-t0,3);
        /* All queued eyes are complete here. Verification mode always used
         * fresh uploads; switch only at a frame boundary after real race data. */
        if (s_geometry_mode == 3 && s_geometry_race_verified >= 100000 && !s_geometry_failed) {
            LOGE("XR geometry: %u race draws byte-verified; enabling immutable upload reuse\n", s_geometry_race_verified);
            s_geometry_mode = 1;
        }
        if(s_uniform_mode==3 && s_uniform_race_verified>=100000 && !s_uniform_failed) {
            LOGE("XR constants: %u race draws byte-verified; enabling immutable uniform reuse\n",s_uniform_race_verified);
            s_uniform_mode=1;
        }
        if(s_cpu_instance_mode==3 && s_cpu_instance_verified>=100000 && !s_cpu_instance_failed) {
            s_cpu_instance_mode=1;
            LOGE("CPU snapshot instance checks verified on %u race candidates; GPU rereads disabled\n",s_cpu_instance_verified);
        }
        if(s_frame%120==0) {
#ifdef NFSU2_PCVR
            uint64_t pcvr_stats_start=hz_now();
#endif
            if(compact_vertex_bindings())
                LOGE("XR vertex bindings/frame: %.1f -> %.1f; all attributes retained\n",
                     s_vertex_bindings_before/120.0,s_vertex_bindings_after/120.0);
            s_vertex_bindings_before=s_vertex_bindings_after=0;
            if(s_dynamic_descriptors) {
                LOGE("XR descriptors/frame: %.1f immutable sets reused, %.1f created; every draw binds all resources\n",
                     s_descriptor_hits/120.0,s_descriptor_misses/120.0);
                s_descriptor_hits=s_descriptor_misses=0;
            }
            if(fragment_specialization()) {
                LOGE("XR fragment checks/frame: both %.1f, clip only %.1f, alpha only %.1f, neither %.1f\n",
                     s_fragment_variants[0]/120.0,s_fragment_variants[1]/120.0,
                     s_fragment_variants[2]/120.0,s_fragment_variants[3]/120.0);
            }
            memset(s_fragment_variants,0,sizeof s_fragment_variants);
            if(s_gpu_profile) {
                LOGE("XR GPU passes ms/frame: main %.2f (%u), reflection %.2f (%u), menu %.2f (%u), small %.2f (%u), other %.2f (%u)\n",
                     s_gpu_pass_ns[0]/120e6,s_gpu_pass_count[0],s_gpu_pass_ns[1]/120e6,s_gpu_pass_count[1],
                     s_gpu_pass_ns[2]/120e6,s_gpu_pass_count[2],s_gpu_pass_ns[3]/120e6,s_gpu_pass_count[3],
                     s_gpu_pass_ns[4]/120e6,s_gpu_pass_count[4]);
                memset(s_gpu_pass_ns,0,sizeof s_gpu_pass_ns); memset(s_gpu_pass_count,0,sizeof s_gpu_pass_count);
            }
            LOGE("XR passes/frame: %.1f rendering, %.1f feedback copies (%.1f MB), %.1f unused feedback bindings\n",
                 s_xr_passes/120.0,s_xr_snapshots/120.0,s_xr_snapshot_pixels*4.0/(120.0*1048576.0),s_xr_unused_feedback/120.0);
            s_xr_passes=s_xr_snapshots=s_xr_unused_feedback=0;
            LOGE("XR feedback target swaps/frame: %.1f (one-pixel border preserved)\n",s_xr_feedback_swaps/120.0);
            s_xr_feedback_swaps=0;
            {
                static uint64_t previous_packets,previous_words;
                uint64_t packets,words; nv2a_pb_data_stats(&packets,&words);
                LOGE("XR bulk uploads/frame: %.1f packets, %.1f words\n",
                     (packets-previous_packets)/120.0,(words-previous_words)/120.0);
                previous_packets=packets; previous_words=words;
            }
            s_xr_snapshot_pixels=0;
            LOGE("XR uploads/frame: %.1f constant blocks reused (%.2f MB); %u verified, %u mismatches; %.1f attachment load clears\n",
                 s_uniform_reused/120.0,s_uniform_reused*(double)VK_CONST_SIZE/(120.0*1048576.0),
                 s_uniform_verified,s_uniform_failed,s_xr_load_clears/120.0);
            s_uniform_reused=s_uniform_verified=s_xr_load_clears=0;
            if(xr_checked_instances()) {
                LOGE("XR instance geometry: %u candidates byte-checked, %u rejected\n",s_instance_checked,s_instance_rejected);
                s_instance_checked=s_instance_rejected=0;
                LOGE("XR shared instances: %u right-eye draws appended without reupload\n",s_shared_instance_appends);
                s_shared_instance_appends=0;
            }
            LOGE("XR cost/frame: draw %.2f ms, snapshot %.2f ms, fence %.2f ms, GPU elapsed %.2f ms (%u chunks), XR begin/end %.2f/%.2f ms, ring flushes %u, snapshot arena %.1f MB; batches raw=%u fixed=%u program=%u world=%u HUD=%u\n",
                s_xr_draw_ns/120e6,s_xr_snapshot_ns/120e6,s_xr_wait_ns/120e6,
                s_xr_gpu_ns/120e6,s_xr_gpu_samples,s_xr_begin_ns/120e6,s_xr_end_ns/120e6,
                s_xr_ring_flushes,s_xr_arena.reserved/1048576.0,
                s_xr_kind[0],s_xr_kind[1],s_xr_kind[2],s_xr_kind[3],s_xr_kind[4]);
            s_xr_draw_ns=s_xr_snapshot_ns=s_xr_gpu_ns=s_xr_wait_ns=0;
            if (s_geometry_mode > 0 || s_geometry_failed) {
                LOGE("XR geometry: %u draws reused, %.2f MB/frame upload avoided; %u verified, %u mismatches, %u expired\n",
                    s_geometry_reused, s_geometry_saved/(120.0*1048576.0), s_geometry_verified,
                    s_geometry_failed, s_geometry_expired);
                s_geometry_reused=s_geometry_verified=s_geometry_expired=0;
                s_geometry_saved=0;
            }
            s_xr_begin_ns=s_xr_end_ns=0;
            s_xr_gpu_samples=s_xr_ring_flushes=0;
            memset(s_xr_kind,0,sizeof s_xr_kind);
#ifdef NFSU2_PCVR
            uint64_t pcvr_stats_ns=hz_now()-pcvr_stats_start;
            s_pcvr_phase_ns[4]+=pcvr_stats_ns;
            if(pcvr_stats_ns>5000000 && getenv("NFSU2_PCVR_HEALTH"))
                LOGE("PCVR statistics output took %.3f ms at frame %u\n",pcvr_stats_ns/1e6,s_frame);
#endif
        }
    }
#endif
    if (have_image) {
        VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        VkResult pr;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &s_f->rendered;
        pi.swapchainCount = 1;
        pi.pSwapchains = &s_swapchain;
        pi.pImageIndices = &idx;
        VT("flip %u: vkQueuePresentKHR\n", s_frame);
        pr = vkQueuePresentKHR(s_queue, &pi);
        if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
            vkDeviceWaitIdle(s_dev);
            create_swapchain();
        }
    }
    {
        static uint64_t last_save;
        uint64_t now = hz_now();
        if (s_pipes_unsaved && now - last_save > 20000000000ull) {
            last_save = now;
            pcache_save();
        }
    }
    VT("flip %u: presented, waiting for frame slot\n", s_frame);
    s_fi = (s_fi + 1) % VK_FRAMES;
    frame_begin();
    VT("flip %u: next frame recording\n", s_frame);
#ifdef NFSU2_OPENXR
    if (quest_xr_enabled()) {
        uint64_t t0=hz_now();
        quest_xr_begin();
        xr_add_time(&s_xr_begin_ns,hz_now()-t0,2);
    }
#endif
    s_dyn_valid = 0;
#if !defined(__SWITCH__)
    if (s_win) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) { }
    }
#endif
}

static void vk_draw(const Nv2aSurface *s, const Nv2aBatch *b)
{
    (void)s; (void)b;                        /* draw_raw handles every batch */
}

static const Nv2aBackend s_backend = {
    vk_clear,
    vk_draw,
    vk_flip,
    vk_draw_raw,
    NV2A_BACKEND_RAW_DIRECT,
};

void nv2a_vk_install(void)
{
    nv2a_backend_register(&s_backend);
    fprintf(stderr, "[BOOT] NV2A Vulkan renderer registered\n");
}
