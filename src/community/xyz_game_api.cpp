#include "xyz_game_api.h"
#include "xyz_fleet.h"
#include "xyz_xbox_pool.h"
#include "xyz_auth.h"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
void xyz_fleet_probe(xyz::Fleet &,const XyzXboxGuest *,const XyzXboxFrame *,uint64_t);

namespace {
std::atomic<bool> used{false};
std::atomic<unsigned> car_limit{XYZ_CARS_DEFAULT};
bool probing_assets=false; // guest thread only, explicit development diagnostic
struct Runtime {
    xyz::Client client{[]{return xyz::make_socket();}};
    xyz::Fleet fleet;
    std::mutex mutex;
    xyz::FleetStatus cars;
    bool failed=false;
    int logged_state=-1,logged_peers=-1,logged_local=-1;
    xyz::FleetStatus logged_cars;
    uint64_t logged_network=0;
    uint32_t logged_generation=0;
};
Runtime &runtime(){static Runtime r;return r;}
bool identity(const char *s) {
    return s && std::strlen(s)==32 && std::strspn(s,"0123456789abcdef")==32;
}
}
static int connect_profile(const char *name,const char *room,const char *profile,const char *device,const char *claim,bool lan) {
    try {
        if(!name || !room || !identity(profile) || !identity(device) || !xyz_auth_claim_valid(claim))return 0;
        auto &r=runtime();
        {std::lock_guard<std::mutex> lock(r.mutex);if(r.failed)return 0;}
        uint32_t id=2166136261u;
        for(const char *s:{device,profile})for(;*s;++s)id=(id^uint8_t(*s))*16777619u;
        if(!id || id==0xffffffffu)id=1;
        xyz::ClientProfile request{{name,device,claim,lan},room,id};
        struct Clear {std::string &s;~Clear(){xyz_auth_clear(s.data(),s.size());}} clear{request.connection.claim};
        if(!r.client.connect(request))return 0;
        used.store(true,std::memory_order_release);return 1;
    } catch(...){return 0;}
}
extern "C" int xyz_online_connect(const char *name,const char *room,const char *profile,const char *device) {
    return connect_profile(name,room,profile,device,"",false);
}
extern "C" int xyz_online_connect_races(const char *name,const char *room,const char *profile,const char *device) {
    return connect_profile(name,room,profile,device,"",true);
}
extern "C" int xyz_online_connect_claimed(const char *name,const char *room,const char *profile,const char *device,const char *claim) {
    return connect_profile(name,room,profile,device,claim,false);
}
extern "C" int xyz_online_connect_races_claimed(const char *name,const char *room,const char *profile,const char *device,const char *claim) {
    return connect_profile(name,room,profile,device,claim,true);
}
extern "C" int xyz_online_xnet(const XyzXboxGuest *g,XyzXnetOp op,uint32_t thread,const uint32_t *args,unsigned count,uint64_t now,uint32_t *result) {
    if(!g || !result || !used.load(std::memory_order_acquire))return 0;
    try{return runtime().client.socket_call(*g,op,thread,args,count,now,*result);}
    catch(...){runtime().client.abort();*result=UINT32_MAX;return 1;}
}
extern "C" void xyz_online_disconnect(void) {
    if(!used.load(std::memory_order_acquire))return;
    try{runtime().client.disconnect();}catch(...){}
}
extern "C" void xyz_online_snapshot(XYZOnlineStatus *out) {
    if(!out)return;std::memset(out,0,sizeof *out);
    if(!used.load(std::memory_order_acquire))return;
    try {
        auto &r=runtime();auto s=r.client.snapshot();
        out->state=int(s.transport.state);out->peers=s.peers;
        out->queue_position=s.transport.queue_position;out->queue_total=s.transport.queue_total;
        out->generation=s.transport.generation;out->lan_ready=s.transport.lan_available && s.transport.races_allowed;
        out->local_ready=s.local==XYZ_XBOX_READY;
        out->hub=s.roam.hub;out->listed=s.roam.listed;
        std::snprintf(out->name,sizeof out->name,"%s",s.name.c_str());
        std::snprintf(out->room,sizeof out->room,"%s",s.room.c_str());
        std::snprintf(out->notice,sizeof out->notice,"%s",s.transport.notice.c_str());
        std::lock_guard<std::mutex> lock(r.mutex);
        if(out->state==XYZ_ONLINE_CONNECTED){out->visible=int(r.cars.visible);out->loading=int(r.cars.loading);out->failed=int(r.cars.failed);}
        if(r.failed){out->state=XYZ_ONLINE_STOPPED;std::snprintf(out->notice,sizeof out->notice,"Online game adapter stopped");}
    } catch(...) {out->state=XYZ_ONLINE_STOPPED;std::snprintf(out->notice,sizeof out->notice,"Cannot read online status");}
}
extern "C" int xyz_online_needs_frame(void){return used.load(std::memory_order_acquire);}
extern "C" int xyz_online_fleet_retired(void){
    if(!used.load(std::memory_order_acquire))return 1;
    try{return runtime().fleet.retired();}catch(...){return 0;}
}
extern "C" void xyz_online_set_car_limit(unsigned cars){
    car_limit.store(cars<1?1:cars>XYZ_CARS_MAX?XYZ_CARS_MAX:cars,std::memory_order_relaxed);
}
extern "C" uint32_t xyz_online_texture(uint32_t hash){
    // Other textures take the original lookup without constructing a runtime.
    // The three DUMMY name series are contiguous only for one-digit suffixes;
    // compare the complete bounded namespace, including multi-digit slots.
    static const auto hashes=[] {
        std::array<uint32_t,xyz::Skins::count*3> a{};size_t i=0;
        for(auto format:{"DUMMY_SKIN%u","DUMMY_WHEEL%u","DUMMY_SPINNER%u"})
            for(unsigned n=xyz::Skins::first;n<xyz::Skins::first+xyz::Skins::count;++n){
                char name[24];std::snprintf(name,sizeof name,format,n);uint32_t h=UINT32_MAX;
                for(const char *s=name;*s;++s)h=h*33+uint8_t(*s);a[i++]=h;
            }
        std::sort(a.begin(),a.end());return a;
    }();
    if(std::binary_search(hashes.begin(),hashes.end(),hash)){try{return runtime().fleet.texture(hash);}catch(...){return 0;}}
    return 0;
}
extern "C" int xyz_online_stance(uint32_t car,XyzStance *out){
    if(!car || !out || (!probing_assets && !used.load(std::memory_order_acquire)))return 0;
    try{return runtime().fleet.stance(car,*out);}catch(...){return 0;}
}
extern "C" int xyz_online_lights(uint32_t car){
    if(!car || (!probing_assets && !used.load(std::memory_order_acquire)))return 0;
    try{return runtime().fleet.lights(car);}catch(...){return 0;}
}
extern "C" int xyz_online_before_wake(const XyzXboxGuest *g,uint32_t file,XyzXboxWakePlan *p) {
    if(!g || !p || (!probing_assets && !used.load(std::memory_order_acquire)))return 0;
    try{return runtime().fleet.before_wake(*g,file,*p);}catch(...){return 0;}
}
extern "C" void xyz_online_after_wake(const XyzXboxGuest *g,XyzXboxWakePlan *p) {
    if(!g || !p)return;
    try{runtime().fleet.after_wake(*g,*p);}catch(...){runtime().client.abort();}
}
extern "C" void xyz_online_assets_probe(const XyzXboxGuest *g,uint64_t now) {
    if(!g)return;probing_assets=true;
    try {
        static int last=-2;int state=runtime().fleet.assets_probe(*g,now);
        if(state!=last){std::fprintf(stderr,"[XYZ assets] probe=%d\n",state);last=state;}
    } catch(...){std::fprintf(stderr,"[XYZ assets] probe failed\n");}
}
extern "C" void xyz_online_fleet_probe(const XyzXboxGuest *g,const XyzXboxFrame *local,uint64_t now) {
    if(!g || !local || used.load(std::memory_order_acquire))return;
    probing_assets=true;
    try {xyz_fleet_probe(runtime().fleet,g,local,now);}
    catch(...){std::fprintf(stderr,"[XYZ fleet probe] failed after an internal error\n");}
}
extern "C" void xyz_online_frame(const XyzXboxGuest *guest,const XyzXboxFrame *local,int paused,uint64_t now) {
    if(!guest || !local || !used.load(std::memory_order_acquire))return;
    try {
        auto &r=runtime();bool failed;
        {std::lock_guard<std::mutex> lock(r.mutex);failed=r.failed;}
        xyz::ClientFrame empty;
        XyzXboxFrame outgoing=*local;XyzStanceView stance;xyz_stance_snapshot(&stance);
        if(stance.available && stance.enabled && stance.custom && outgoing.config_size){
            rc_carcfg look{};
            if(rc_carcfg_decode(outgoing.config,outgoing.config_size,&look)==RC_OK && look.type_hash==stance.type){
                look.has_stance=1;look.stance_fc=(uint8_t)stance.value.fc;look.stance_rc=(uint8_t)stance.value.rc;
                look.stance_fo=stance.value.fo;look.stance_ro=stance.value.ro;look.stance_tw=(uint8_t)stance.value.tw;
                uint8_t wire[RC_CFG_MAX];int bytes=rc_carcfg_encode(&look,wire,sizeof wire);
                if(bytes>0){std::memcpy(outgoing.config,wire,size_t(bytes));outgoing.config_size=(uint16_t)bytes;}
            }
        }
        const auto &remote=failed?empty:r.client.frame(outgoing,paused!=0,int64_t(now));
        unsigned selected_limit=car_limit.load(std::memory_order_relaxed);
        r.fleet.set_limit(selected_limit);
        auto result=r.fleet.frame(*guest,*local,remote,now);
        auto status=r.client.snapshot();
        if(status.transport.generation && (r.logged_generation!=status.transport.generation || now-r.logged_network>=5000000ull)) {
            const auto &d=status.roam;
            XyzXboxStreaming stream{};XyzXboxPool pool{};
            int stream_ok=xyz_xbox_streaming(guest->read,guest->context,&stream);
            int pool_ok=xyz_xbox_pool_read(guest->read,guest->context,&pool);
            std::fprintf(stderr,"[XYZ loading] stream_valid=%d phase=%u car_busy=%u pool_valid=%d free=%u largest=%u rides=%u/%u\n",
                stream_ok,stream.phase,stream.car_busy,pool_ok,pool.free,pool.largest,pool.rides,pool.capacity);
            std::fprintf(stderr,"[XYZ discovery] generation=%u public=%d hub=%d lists=%u listed=%d candidates=%d configured=%d moving=%d rx=%u bad=%u policy=%u version=%u room=%u unlisted=%u dropped=%llu receive_limit=%d car_limit=%d\n",
                status.transport.generation,status.room.empty(),d.hub,d.hub_lists,d.listed,d.candidates,d.configured,d.moving,
                d.rx,d.bad,d.policy,d.version,d.room,d.unlisted,(unsigned long long)status.transport.dropped,xyz::Client::receive_limit,int(selected_limit));
            r.logged_generation=status.transport.generation;r.logged_network=now;
        }
        if(r.logged_state!=int(status.transport.state) || r.logged_peers!=status.peers || r.logged_local!=int(local->status) ||
           r.logged_cars.visible!=result.visible || r.logged_cars.loading!=result.loading || r.logged_cars.failed!=result.failed) {
            std::fprintf(stderr,"[XYZ online] state=%d generation=%u peers=%d local=%s visible=%u loading=%u failed=%u fallback=%u missing=%u\n",
                int(status.transport.state),remote.generation,status.peers,xyz_xbox_status_text(local->status),
                result.visible,result.loading,result.failed,result.fallbacks,result.missing_parts);
            r.logged_state=int(status.transport.state);r.logged_peers=status.peers;r.logged_local=int(local->status);r.logged_cars=result;
        }
        std::lock_guard<std::mutex> lock(r.mutex);r.cars=result;
    } catch(...) {
        auto &r=runtime();r.client.abort();
        std::fprintf(stderr,"[XYZ online] game adapter stopped after an internal error\n");
        std::lock_guard<std::mutex> lock(r.mutex);r.failed=true;
        // Following frames still attempt owned-car retirement, with no peers.
    }
}
