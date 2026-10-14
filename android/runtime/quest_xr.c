#ifdef __ANDROID__
#define XR_USE_PLATFORM_ANDROID
#include <jni.h>
#endif
#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <SDL.h>
#include <SDL_system.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ANDROID__
#include <unistd.h>
#endif
#include "quest_xr.h"
#include "quest_culling.h"
#include "quest_frame_timing.h"

static XrInstance instance;
static XrSystemId system_id;
static XrSession session;
static XrSpace local_space, view_space;
static XrSessionState state;
static int enabled, running, begun, visible, tracked, recentered, stereo_frame, menu_frame;
static uint64_t frame_number, submitted_frames;
static XrTime display_time;
#ifdef __ANDROID__
static JavaVM *vm;
static jobject activity;
#endif
static VkDevice device;
static XrView views[2];
static XrPosef origin;
static float matrices[2][16];
static pthread_mutex_t culling_lock = PTHREAD_MUTEX_INITIALIZER;
static float culling_planes[6][4];
static int culling_valid;
static uint32_t culling_tick;
static float units_per_meter = 1.0f;
typedef struct {
    XrSwapchain handle;
    XrSwapchainImageVulkan2KHR *images;
    uint32_t width, height, index, count;
    int acquired;
} QuestSwapchain;
static QuestSwapchain eyes[2], cinema;
static PFN_xrCreateVulkanInstanceKHR create_vk_instance;
static PFN_xrGetVulkanGraphicsDevice2KHR get_vk_physical;
static PFN_xrCreateVulkanDeviceKHR create_vk_device;
static PFN_xrPerfSettingsSetPerformanceLevelEXT set_performance;
#ifdef __ANDROID__
static PFN_xrSetAndroidApplicationThreadKHR set_thread;
#endif
static PFN_xrEnumerateDisplayRefreshRatesFB enumerate_rates;
static PFN_xrRequestDisplayRefreshRateFB request_rate;
static PFN_xrGetDisplayRefreshRateFB get_rate;
static uint32_t game_tid;
static XrActionSet actions;
enum { A, B, X, Y, MENU, LCLICK, RCLICK, LTRIGGER, RTRIGGER, LGRIP, RGRIP, LSTICK, RSTICK, ACTION_COUNT };
static XrAction action[ACTION_COUNT];
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t input_buttons;
static uint8_t input_analog[8];
static int16_t input_sticks[4];

static int check(XrResult result, const char *call)
{
    if (XR_SUCCEEDED(result)) return 1;
    char name[XR_MAX_RESULT_STRING_SIZE] = {0};
    if (instance) xrResultToString(instance, result, name);
    fprintf(stderr, "[XR] %s failed: %s (%d)\n", call, name, result);
    return 0;
}
#define XR_OK(call) check((call), #call)

static XrPath path(const char *name)
{
    XrPath result = XR_NULL_PATH;
    XR_OK(xrStringToPath(instance, name, &result));
    return result;
}

static int setup_actions(void)
{
    XrActionSetCreateInfo ci = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(ci.actionSetName, "driving");
    strcpy(ci.localizedActionSetName, "Driving");
    if (!XR_OK(xrCreateActionSet(instance, &ci, &actions))) return 0;
    const char *names[ACTION_COUNT] = {"a", "b", "x", "y", "menu", "left_click", "right_click",
        "left_trigger", "right_trigger", "left_grip", "right_grip", "left_stick", "right_stick"};
    const char *bindings[ACTION_COUNT] = {
        "/user/hand/right/input/a/click", "/user/hand/right/input/b/click",
        "/user/hand/left/input/x/click", "/user/hand/left/input/y/click",
        "/user/hand/left/input/menu/click", "/user/hand/left/input/thumbstick/click",
        "/user/hand/right/input/thumbstick/click", "/user/hand/left/input/trigger/value",
        "/user/hand/right/input/trigger/value", "/user/hand/left/input/squeeze/value",
        "/user/hand/right/input/squeeze/value", "/user/hand/left/input/thumbstick",
        "/user/hand/right/input/thumbstick"};
    XrActionSuggestedBinding suggested[ACTION_COUNT];
    for (int i = 0; i < ACTION_COUNT; ++i) {
        XrActionCreateInfo ai = {XR_TYPE_ACTION_CREATE_INFO};
        strcpy(ai.actionName, names[i]);
        strcpy(ai.localizedActionName, names[i]);
        ai.actionType = i >= LSTICK ? XR_ACTION_TYPE_VECTOR2F_INPUT :
                       i >= LTRIGGER ? XR_ACTION_TYPE_FLOAT_INPUT : XR_ACTION_TYPE_BOOLEAN_INPUT;
        if (!XR_OK(xrCreateAction(actions, &ai, &action[i]))) return 0;
        suggested[i] = (XrActionSuggestedBinding){action[i], path(bindings[i])};
    }
    XrInteractionProfileSuggestedBinding binding = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    binding.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    binding.countSuggestedBindings = ACTION_COUNT;
    binding.suggestedBindings = suggested;
    return XR_OK(xrSuggestInteractionProfileBindings(instance, &binding));
}

int quest_xr_initialize(void)
{
    const char *opt = getenv("NFSU2_VR");
    if (opt && !strcmp(opt, "0")) return 1;
#ifdef __ANDROID__
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    if (!env || (*env)->GetJavaVM(env, &vm) != JNI_OK) return 0;
    jobject local_activity = SDL_AndroidGetActivity();
    activity = (*env)->NewGlobalRef(env, local_activity);
    (*env)->DeleteLocalRef(env, local_activity);
    PFN_xrInitializeLoaderKHR init_loader = NULL;
    if (!XR_OK(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)&init_loader))) return 0;
    XrLoaderInitInfoAndroidKHR loader = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    loader.applicationVM = vm;
    loader.applicationContext = activity;
    if (!XR_OK(init_loader((XrLoaderInitInfoBaseHeaderKHR *)&loader))) return 0;
    const char *extensions[5] = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    uint32_t extension_count=2, available_count=0;
#else
    const char *extensions[5]={XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};
    uint32_t extension_count=1, available_count=0;
#endif
    int have_performance=0, have_thread_settings=0, have_refresh=0;
    if(!XR_OK(xrEnumerateInstanceExtensionProperties(NULL,0,&available_count,NULL))) return 0;
    XrExtensionProperties *available=calloc(available_count,sizeof *available);
    if(!available) return 0;
    for(uint32_t i=0;i<available_count;++i) available[i].type=XR_TYPE_EXTENSION_PROPERTIES;
    if(!XR_OK(xrEnumerateInstanceExtensionProperties(NULL,available_count,&available_count,available))) { free(available); return 0; }
    for(uint32_t i=0;i<available_count;++i) {
        if(!strcmp(available[i].extensionName,XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME)) have_performance=1;
#ifdef __ANDROID__
        if(!strcmp(available[i].extensionName,XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME)) have_thread_settings=1;
#endif
        if(!strcmp(available[i].extensionName,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)) have_refresh=1;
    }
    free(available);
    if(have_performance) extensions[extension_count++]=XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME;
#ifdef __ANDROID__
    if(have_thread_settings) extensions[extension_count++]=XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME;
#endif
    if(have_refresh) extensions[extension_count++]=XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
    XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
#ifdef __ANDROID__
    XrInstanceCreateInfoAndroidKHR android = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android.applicationVM = vm;
    android.applicationActivity = activity;
    ci.next = &android;
    strcpy(ci.applicationInfo.applicationName, "NFSU2 Quest");
#else
    strcpy(ci.applicationInfo.applicationName, "NFSU2 PCVR");
#endif
    strcpy(ci.applicationInfo.engineName, "xboxrecomp");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
    ci.enabledExtensionCount = extension_count;
    ci.enabledExtensionNames = extensions;
    if (!XR_OK(xrCreateInstance(&ci, &instance))) return 0;
    XrInstanceProperties props = {XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(instance, &props);
    fprintf(stderr, "[XR] runtime: %s\n", props.runtimeName);
    XrSystemGetInfo si = {XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!XR_OK(xrGetSystem(instance, &si, &system_id))) return 0;
#define PROC(name, ptr) if (!XR_OK(xrGetInstanceProcAddr(instance, name, (PFN_xrVoidFunction *)&ptr))) return 0
    PROC("xrCreateVulkanInstanceKHR", create_vk_instance);
    PROC("xrGetVulkanGraphicsDevice2KHR", get_vk_physical);
    PROC("xrCreateVulkanDeviceKHR", create_vk_device);
    PFN_xrGetVulkanGraphicsRequirements2KHR requirements;
    PROC("xrGetVulkanGraphicsRequirements2KHR", requirements);
    if(have_performance) PROC("xrPerfSettingsSetPerformanceLevelEXT",set_performance);
#ifdef __ANDROID__
    if(have_thread_settings) PROC("xrSetAndroidApplicationThreadKHR",set_thread);
#endif
    if(have_refresh) {
        PROC("xrEnumerateDisplayRefreshRatesFB",enumerate_rates);
        PROC("xrRequestDisplayRefreshRateFB",request_rate);
        PROC("xrGetDisplayRefreshRateFB",get_rate);
    }
#undef PROC
    XrGraphicsRequirementsVulkan2KHR req = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    if (!XR_OK(requirements(instance, system_id, &req)) || !setup_actions()) return 0;
    const char *scale = getenv("NFSU2_VR_UNITS_PER_METER");
    if (scale && atof(scale) > 0.01 && atof(scale) < 1000) units_per_meter = (float)atof(scale);
    enabled = 1;
    return 1;
}

int quest_xr_enabled(void) { return enabled; }
void quest_xr_game_thread(void) {
#ifdef __ANDROID__
    game_tid=(uint32_t)gettid();
#endif
}
VkResult quest_xr_vk_instance(const VkInstanceCreateInfo *ci, VkInstance *vk)
{
    XrVulkanInstanceCreateInfoKHR info = {XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    info.systemId = system_id;
    info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    info.vulkanCreateInfo = ci;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (!XR_OK(create_vk_instance(instance, &info, vk, &result))) return VK_ERROR_INITIALIZATION_FAILED;
    return result;
}

VkPhysicalDevice quest_xr_vk_physical(VkInstance vk)
{
    XrVulkanGraphicsDeviceGetInfoKHR info = {XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    info.systemId = system_id;
    info.vulkanInstance = vk;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    XR_OK(get_vk_physical(instance, &info, &physical));
    return physical;
}

VkResult quest_xr_vk_device(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, VkDevice *out)
{
    XrVulkanDeviceCreateInfoKHR info = {XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    info.systemId = system_id;
    info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    info.vulkanPhysicalDevice = pd;
    info.vulkanCreateInfo = ci;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (!XR_OK(create_vk_device(instance, &info, out, &result))) return VK_ERROR_INITIALIZATION_FAILED;
    return result;
}

static int create_swapchain(QuestSwapchain *out, int64_t format, uint32_t width, uint32_t height)
{
    XrSwapchainCreateInfo sc = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    sc.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    sc.format = format; sc.width = width; sc.height = height;
    sc.sampleCount = 1; sc.faceCount = 1; sc.arraySize = 1; sc.mipCount = 1;
    if (!XR_OK(xrCreateSwapchain(session, &sc, &out->handle))) return 0;
    out->width = width; out->height = height;
    if (!XR_OK(xrEnumerateSwapchainImages(out->handle, 0, &out->count, NULL))) return 0;
    out->images = calloc(out->count, sizeof *out->images);
    if (!out->images) return 0;
    for (uint32_t j = 0; j < out->count; ++j) out->images[j].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
    uint32_t count;
    return XR_OK(xrEnumerateSwapchainImages(out->handle, out->count, &count,
                (XrSwapchainImageBaseHeader *)out->images));
}

int quest_xr_start(VkInstance vk, VkPhysicalDevice pd, VkDevice dev, uint32_t family)
{
    device = dev;
    XrGraphicsBindingVulkan2KHR binding = {XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    binding.instance = vk; binding.physicalDevice = pd; binding.device = dev;
    binding.queueFamilyIndex = family;
    XrSessionCreateInfo ci = {XR_TYPE_SESSION_CREATE_INFO};
    ci.next = &binding; ci.systemId = system_id;
    if (!XR_OK(xrCreateSession(instance, &ci, &session))) return 0;
#ifdef __ANDROID__
    if(set_thread) {
        if(game_tid) XR_OK(set_thread(session,XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR,game_tid));
        XR_OK(set_thread(session,XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR,(uint32_t)gettid()));
    }
#endif
    XrSessionActionSetsAttachInfo attach = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1; attach.actionSets = &actions;
    if (!XR_OK(xrAttachSessionActionSets(session, &attach))) return 0;
    XrReferenceSpaceCreateInfo space = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    space.poseInReferenceSpace.orientation.w = 1;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!XR_OK(xrCreateReferenceSpace(session, &space, &local_space))) return 0;
    space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!XR_OK(xrCreateReferenceSpace(session, &space, &view_space))) return 0;
    XrViewConfigurationView config[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    uint32_t count = 0;
    if (!XR_OK(xrEnumerateViewConfigurationViews(instance, system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, config)) || count != 2) return 0;
    if (!XR_OK(xrEnumerateSwapchainFormats(session, 0, &count, NULL))) return 0;
    int64_t *formats = calloc(count, sizeof *formats);
    if (!formats) return 0;
    if (!XR_OK(xrEnumerateSwapchainFormats(session, count, &count, formats))) { free(formats); return 0; }
    int64_t format = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (formats[i] == VK_FORMAT_R8G8B8A8_UNORM || formats[i] == VK_FORMAT_B8G8R8A8_UNORM) { format = formats[i]; break; }
    free(formats);
    if (!format) { fprintf(stderr, "[XR] No supported UNORM swapchain format\n"); return 0; }
    for (int eye = 0; eye < 2; ++eye) {
        uint32_t width=config[eye].recommendedImageRectWidth, height=config[eye].recommendedImageRectHeight;
#ifdef NFSU2_PCVR
        const char *w=getenv("NFSU2_VR_EYE_WIDTH"), *h=getenv("NFSU2_VR_EYE_HEIGHT");
        if(w && h && atoi(w)>=256 && atoi(h)>=256 &&
           (uint32_t)atoi(w)<=config[eye].maxImageRectWidth && (uint32_t)atoi(h)<=config[eye].maxImageRectHeight) {
            width=(uint32_t)atoi(w); height=(uint32_t)atoi(h);
        }
#endif
        if (!create_swapchain(&eyes[eye], format, width, height)) return 0;
        views[eye].type = XR_TYPE_VIEW;
        fprintf(stderr, "[XR] eye %d: %ux%u, %u images\n", eye, eyes[eye].width, eyes[eye].height, eyes[eye].count);
    }
    /* A 4:3 menu image goes directly to the quad compositor. Do not squeeze
     * it through an eye-shaped stereo image and resample it a second time. */
    XrSystemProperties properties = {XR_TYPE_SYSTEM_PROPERTIES};
    if (!XR_OK(xrGetSystemProperties(instance, system_id, &properties))) return 0;
    uint32_t unit = 480;
    if (unit > properties.graphicsProperties.maxSwapchainImageWidth / 4)
        unit = properties.graphicsProperties.maxSwapchainImageWidth / 4;
    if (unit > properties.graphicsProperties.maxSwapchainImageHeight / 3)
        unit = properties.graphicsProperties.maxSwapchainImageHeight / 3;
    if (!unit || !create_swapchain(&cinema, format, unit * 4, unit * 3)) return 0;
    fprintf(stderr, "[XR] cinema: %ux%u; menu quad 4.8x3.6 m at 3 m\n", cinema.width, cinema.height);
    return 1;
}

static void multiply(float *out, const float *a, const float *b)
{
    float m[16] = {0};
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k) m[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    memcpy(out, m, sizeof m);
}

static void pose_matrix(float *m, XrPosef p, int inverse)
{
    float x=p.orientation.x, y=p.orientation.y, z=p.orientation.z, w=p.orientation.w;
    float r[16] = {1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w), 0,
        2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w), 0,
        2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y), 0, 0,0,0,1};
    float t[3] = {p.position.x*units_per_meter, p.position.y*units_per_meter, p.position.z*units_per_meter};
    memcpy(m, r, sizeof r);
    if (inverse) {
        for (int c=0;c<3;++c) for(int row=0;row<3;++row) m[c*4+row]=r[row*4+c];
        for(int row=0;row<3;++row) m[12+row]=-(m[row]*t[0]+m[4+row]*t[1]+m[8+row]*t[2]);
    } else for(int row=0;row<3;++row) m[12+row]=t[row];
}

static void update_matrices(void)
{
    float base[16];
    pose_matrix(base, origin, 0);
    for (int eye=0;eye<2;++eye) {
        float view[16], combined[16], p[16]={0};
        pose_matrix(view, views[eye].pose, 1);
        multiply(combined, view, base);
        XrFovf f=views[eye].fov;
        float l=tanf(f.angleLeft), r=tanf(f.angleRight), d=tanf(f.angleDown), u=tanf(f.angleUp);
        /* The captured game projection uses a 0.5-unit near plane. Moving it
         * ten times closer needlessly loses tenfold D24 depth precision. */
        float near_z=0.5f, far_z=10000.0f;
        p[0]=2/(r-l); p[5]=-2/(u-d); p[8]=(r+l)/(r-l); p[9]=-(u+d)/(u-d);
        p[10]=-far_z/(far_z-near_z); p[11]=-1; p[14]=-far_z*near_z/(far_z-near_z);
        multiply(matrices[eye], p, combined);
    }
    float planes[6][4];
    int valid=visible && quest_culling_stereo(planes,matrices[0],matrices[1],0.25f*units_per_meter);
    pthread_mutex_lock(&culling_lock);
    culling_valid=valid;
    if (valid) { memcpy(culling_planes,planes,sizeof planes); culling_tick=SDL_GetTicks(); }
    pthread_mutex_unlock(&culling_lock);
}

int quest_xr_culling_planes(float planes[6][4])
{
    pthread_mutex_lock(&culling_lock);
    int valid=culling_valid && (uint32_t)(SDL_GetTicks()-culling_tick)<500;
    if (valid) memcpy(planes,culling_planes,sizeof culling_planes);
    pthread_mutex_unlock(&culling_lock);
    return valid;
}

static void poll_input(void)
{
    XrActiveActionSet active = {actions, XR_NULL_PATH};
    XrActionsSyncInfo sync = {XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets=1; sync.activeActionSets=&active;
    uint16_t buttons=0; uint8_t analog[8]={0}; int16_t sticks[4]={0};
    if (state == XR_SESSION_STATE_FOCUSED && XR_SUCCEEDED(xrSyncActions(session,&sync))) {
        int pressed[7]={0};
        for(int i=0;i<ACTION_COUNT;++i) {
            XrActionStateGetInfo get={XR_TYPE_ACTION_STATE_GET_INFO}; get.action=action[i];
            if(i < LTRIGGER) {
                XrActionStateBoolean value={XR_TYPE_ACTION_STATE_BOOLEAN};
                if(XR_SUCCEEDED(xrGetActionStateBoolean(session,&get,&value)) && value.isActive) pressed[i]=value.currentState;
            } else if(i < LSTICK) {
                XrActionStateFloat value={XR_TYPE_ACTION_STATE_FLOAT};
                if(XR_SUCCEEDED(xrGetActionStateFloat(session,&get,&value)) && value.isActive) {
                    int index=i==LTRIGGER?6:i==RTRIGGER?7:i==LGRIP?5:4;
                    analog[index]=(uint8_t)(fminf(1,fmaxf(0,value.currentState))*255);
                }
            } else {
                XrActionStateVector2f value={XR_TYPE_ACTION_STATE_VECTOR2F};
                if(XR_SUCCEEDED(xrGetActionStateVector2f(session,&get,&value)) && value.isActive) {
                    int index=(i-LSTICK)*2;
                    sticks[index]=(int16_t)(fminf(1,fmaxf(-1,value.currentState.x))*32767);
                    sticks[index+1]=(int16_t)(fminf(1,fmaxf(-1,value.currentState.y))*32767);
                }
            }
        }
        for(int i=0;i<4;++i) analog[i]=pressed[i]?255:0;
        if(pressed[MENU]) buttons|=0x10;
        if(pressed[LCLICK]) buttons|=0x40;
        if(pressed[RCLICK]) buttons|=0x80;
        /* Right thumbstick doubles as D-pad for menus; left stays analog steering. */
        if(sticks[2]<-16000) buttons|=4; if(sticks[2]>16000) buttons|=8;
        if(sticks[3]<-16000) buttons|=2; if(sticks[3]>16000) buttons|=1;
        static int recenter_held;
        int recenter_now=pressed[LCLICK] && pressed[RCLICK];
        if(recenter_now && !recenter_held) recentered=0;
        recenter_held=recenter_now;
    }
    pthread_mutex_lock(&input_lock);
    input_buttons=buttons; memcpy(input_analog,analog,8); memcpy(input_sticks,sticks,sizeof sticks);
    pthread_mutex_unlock(&input_lock);
}

static void setup_refresh_rate(void)
{
    if (!enumerate_rates || !request_rate || !get_rate) return;
    float requested=(float)quest_refresh_option(getenv("NFSU2_VR_REFRESH_HZ"),getenv("NFSU2_VR_72FPS"));
    uint32_t count=0;
    if (requested && XR_OK(enumerate_rates(session,0,&count,NULL)) && count) {
        float *rates=calloc(count,sizeof *rates);
        if (rates && XR_OK(enumerate_rates(session,count,&count,rates))) {
            int supported=0;
            for(uint32_t i=0;i<count;++i) if(fabsf(rates[i]-requested)<0.01f) supported=1;
            if(supported) XR_OK(request_rate(session,requested));
            else fprintf(stderr,"[XR] %.0f Hz is not listed by the runtime\n",requested);
        }
        free(rates);
    }
    float current=0;
    if(XR_OK(get_rate(session,&current))) fprintf(stderr,"[XR] display refresh %.2f Hz (distinct from game FPS)\n",current);
}

int quest_xr_begin(void)
{
    if (!session || begun) return begun;
    XrEventDataBuffer event={XR_TYPE_EVENT_DATA_BUFFER};
    while(xrPollEvent(instance,&event)==XR_SUCCESS) {
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            state=((XrEventDataSessionStateChanged *)&event)->state;
            fprintf(stderr,"[XR] session state %d\n",state);
            if(state==XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin={XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                running=XR_OK(xrBeginSession(session,&begin));
                if(running) setup_refresh_rate();
                if(running && set_performance) {
                    int cpu=XR_OK(set_performance(session,XR_PERF_SETTINGS_DOMAIN_CPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
                    int gpu=XR_OK(set_performance(session,XR_PERF_SETTINGS_DOMAIN_GPU_EXT,XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
                    fprintf(stderr,"[XR] sustained performance hints: CPU=%d GPU=%d\n",cpu,gpu);
                }
            } else if(state==XR_SESSION_STATE_STOPPING) { xrEndSession(session); running=0; }
            else if(state==XR_SESSION_STATE_EXITING || state==XR_SESSION_STATE_LOSS_PENDING) running=0;
        } else if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) recentered=0;
        else if(event.type==XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB) {
            XrEventDataDisplayRefreshRateChangedFB *rate=(XrEventDataDisplayRefreshRateChangedFB *)&event;
            fprintf(stderr,"[XR] display refresh changed %.2f -> %.2f Hz\n",rate->fromDisplayRefreshRate,rate->toDisplayRefreshRate);
        }
        event=(XrEventDataBuffer){XR_TYPE_EVENT_DATA_BUFFER};
    }
    if(!running) { SDL_Delay(10); return 0; }
    XrFrameWaitInfo wait={XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frame={XR_TYPE_FRAME_STATE};
    if(!XR_OK(xrWaitFrame(session,&wait,&frame))) return 0;
#ifdef NFSU2_PCVR
    static XrDuration previous_period;
    if(frame.predictedDisplayPeriod>0 && frame.predictedDisplayPeriod!=previous_period) {
        fprintf(stderr,"[PCVR] runtime display period %.3f ms (%.2f Hz); independent of game FPS\n",
            frame.predictedDisplayPeriod/1e6,1e9/frame.predictedDisplayPeriod);
        previous_period=frame.predictedDisplayPeriod;
    }
#endif
    XrFrameBeginInfo begin={XR_TYPE_FRAME_BEGIN_INFO};
    if(!XR_OK(xrBeginFrame(session,&begin))) return 0;
    begun=1; visible=frame.shouldRender; display_time=frame.predictedDisplayTime;
    poll_input();
    XrViewLocateInfo locate={XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime=display_time; locate.space=local_space;
    XrViewState view_state={XR_TYPE_VIEW_STATE}; uint32_t count;
    tracked=XR_OK(xrLocateViews(session,&locate,&view_state,2,&count,views)) && count==2 &&
        (view_state.viewStateFlags & (XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT)) ==
        (XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT);
    if(tracked && !recentered) {
        XrSpaceLocation head={XR_TYPE_SPACE_LOCATION};
        if(XR_SUCCEEDED(xrLocateSpace(view_space,local_space,display_time,&head)) &&
           (head.locationFlags & (XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) ==
           (XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            origin=head.pose;
            XrQuaternionf q=head.pose.orientation;
            float yaw=atan2f(2*(q.w*q.y+q.x*q.z),1-2*(q.y*q.y+q.x*q.x));
            origin.orientation=(XrQuaternionf){0,sinf(yaw/2),0,cosf(yaw/2)};
            recentered=1;
        }
    }
    if(tracked && recentered) update_matrices();
    ++frame_number;
    if(frame_number <= 3 || frame_number % 300 == 0)
        fprintf(stderr,"[XR] frame %llu: render=%d tracked=%d recentered=%d flags=0x%llx state=%d\n",
            (unsigned long long)frame_number,visible,tracked,recentered,
            (unsigned long long)view_state.viewStateFlags,state);
    return visible && tracked;
}

void quest_xr_matrix(int eye,float matrix[16]) { memcpy(matrix,matrices[eye],64); }
float quest_xr_depth_coefficient(void) { return -10000.0f*0.5f/(10000.0f-0.5f); }
int quest_xr_tracking(void) { return begun && visible && tracked && recentered; }

void quest_xr_eye_extent(uint32_t *width,uint32_t *height)
{
    *width=eyes[0].width; *height=eyes[0].height;
}

void quest_xr_menu_extent(uint32_t *width,uint32_t *height)
{
    *width=cinema.width; *height=cinema.height;
}

void quest_xr_copy(VkCommandBuffer cb,VkImage left,VkImage right,uint32_t width,uint32_t height,int stereo,int menu,uint32_t source_layers)
{
    stereo_frame=stereo; menu_frame=menu;
    if(!begun || !visible || !tracked || !left) return;
    for(int eye=0;eye<(stereo?2:1);++eye) {
        QuestSwapchain *target=stereo?&eyes[eye]:&cinema;
        XrSwapchainImageAcquireInfo acq={XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(!XR_OK(xrAcquireSwapchainImage(target->handle,&acq,&target->index))) return;
        target->acquired=1;
        XrSwapchainImageWaitInfo wait={XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout=XR_INFINITE_DURATION;
        if(!XR_OK(xrWaitSwapchainImage(target->handle,&wait))) return;
        VkImage image=target->images[target->index].image;
        VkImageMemoryBarrier barrier={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=image;
        barrier.subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkImageBlit blit={0};
        blit.srcSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        blit.dstSubresource=blit.srcSubresource;
        if(source_layers==2) blit.srcSubresource.baseArrayLayer=(uint32_t)eye;
        blit.srcOffsets[1]=(VkOffset3D){width,height,1};
        blit.dstOffsets[1]=(VkOffset3D){target->width,target->height,1};
        vkCmdBlitImage(cb,eye && right?right:left,VK_IMAGE_LAYOUT_GENERAL,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_LINEAR);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&barrier);
    }
}

void quest_xr_end(void)
{
    if(!begun) return;
    int have=stereo_frame?(eyes[0].acquired && eyes[1].acquired):cinema.acquired;
    for(int i=0;i<3;++i) {
        QuestSwapchain *target=i<2?&eyes[i]:&cinema;
        if (target->acquired) {
            XrSwapchainImageReleaseInfo release={XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            XR_OK(xrReleaseSwapchainImage(target->handle,&release)); target->acquired=0;
        }
    }
    XrCompositionLayerProjectionView projection_views[2]={{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    for(int eye=0;eye<2;++eye) {
        projection_views[eye].pose=views[eye].pose; projection_views[eye].fov=views[eye].fov;
        projection_views[eye].subImage.swapchain=eyes[eye].handle;
        projection_views[eye].subImage.imageRect.extent=(XrExtent2Di){eyes[eye].width,eyes[eye].height};
    }
    XrCompositionLayerProjection projection={XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection.space=local_space; projection.viewCount=2; projection.views=projection_views;
    XrCompositionLayerQuad quad={XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.space=local_space; quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain=cinema.handle;
    quad.subImage.imageRect.extent=(XrExtent2Di){cinema.width,cinema.height};
    quad.pose=origin;
    /* A stationary screen for pre-rendered movies and menu bring-up. */
    float yaw=2*atan2f(origin.orientation.y,origin.orientation.w);
    quad.pose.position.x-=sinf(yaw)*3; quad.pose.position.z-=cosf(yaw)*3;
    quad.size=menu_frame?(XrExtent2Df){4.8f,3.6f}:(XrExtent2Df){3.6f,2.7f};
    const XrCompositionLayerBaseHeader *layer=stereo_frame?
        (XrCompositionLayerBaseHeader *)&projection:(XrCompositionLayerBaseHeader *)&quad;
    XrFrameEndInfo end={XR_TYPE_FRAME_END_INFO};
    end.displayTime=display_time; end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount=have && visible && tracked ? 1:0; end.layers=&layer;
    if(XR_OK(xrEndFrame(session,&end)) && end.layerCount) {
        ++submitted_frames;
        if(submitted_frames <= 3 || submitted_frames % 300 == 0)
            fprintf(stderr,"[XR] submitted %llu: %s\n",(unsigned long long)submitted_frames,
                stereo_frame?"stereo projection":"cinema quad");
    }
    begun=0;
}

int quest_xr_input(uint16_t *buttons,uint8_t analog[8],int16_t sticks[4])
{
    if(!enabled) return 0;
    pthread_mutex_lock(&input_lock);
    *buttons=input_buttons; memcpy(analog,input_analog,8); memcpy(sticks,input_sticks,8);
    pthread_mutex_unlock(&input_lock);
    return 1;
}

void quest_xr_shutdown(void)
{
    if(device) vkDeviceWaitIdle(device);
    if(begun) quest_xr_end();
    for(int i=0;i<2;++i) { if(eyes[i].handle) xrDestroySwapchain(eyes[i].handle); free(eyes[i].images); }
    if(cinema.handle) xrDestroySwapchain(cinema.handle);
    free(cinema.images);
    if(view_space) xrDestroySpace(view_space);
    if(local_space) xrDestroySpace(local_space);
    if(session) xrDestroySession(session);
    if(actions) xrDestroyActionSet(actions);
    if(instance) xrDestroyInstance(instance);
#ifdef __ANDROID__
    JNIEnv *env=SDL_AndroidGetJNIEnv();
    if(activity && env) (*env)->DeleteGlobalRef(env,activity);
#endif
    enabled=0;
}
