// Explicit service QA peer, never registered with CTest or started by the game.
// Sends a labelled synthetic trajectory with an owned, captured car config.
// It is not evidence of browser rendering or of another game's physics.
#include "xyz_client.h"
#include "xyz_profiles.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

int main(int argc,char **argv) {
    if((argc!=6 && argc!=8) || std::string(argv[1])!="--connect" || std::string(argv[2]).empty() ||
       (argc==8 && std::string(argv[6])!="--log")) {
        std::puts("Usage: xyz_live_peer --connect ROOM OWNED_CONFIG SECONDS fixed|follow [--log FILE]");return 2;
    }
    if(argc==8 && !std::freopen(argv[7],"w",stdout))return 2;
    char *end=nullptr;long seconds=std::strtol(argv[4],&end,10);
    bool follow=std::string(argv[5])=="follow";
    if(!end || *end || seconds<10 || seconds>900 || (!follow && std::string(argv[5])!="fixed"))return 2;
    std::ifstream file(std::filesystem::u8path(argv[3]),std::ios::binary|std::ios::ate);
    if(!file || file.tellg()<=0 || file.tellg()>RC_CFG_MAX)return 2;
    XyzXboxFrame local{};local.config_size=uint16_t(file.tellg());file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(local.config),local.config_size))return 2;
    rc_carcfg decoded{};if(rc_carcfg_decode(local.config,local.config_size,&decoded)!=RC_OK)return 2;
    auto folder=std::filesystem::temp_directory_path()/"nfsu2-xyz-peer-no-persistence";
    if(std::filesystem::exists(folder))return 2;
    xyz::Profiles profiles(folder);if(!profiles.load())return 2;
    std::string name="VR_Check_"+profiles.identity().substr(0,6);
    uint32_t id=uint32_t(std::strtoul(profiles.identity().substr(0,8).c_str(),nullptr,16));
    if(!id || id==0xffffffffu)id=1;
    xyz::Client client([]{return xyz::make_socket();});
    if(!client.connect({{name,profiles.identity(),{}},argv[2],id}))return 2;
    // Only opaque identity markers are used by Client; this tool has no guest
    // memory and never invokes the vehicle adapter.
    local.status=XYZ_XBOX_READY;local.world=1;local.car=2;local.track=4000;
    float anchor[3]={1695.f,-884.f,24.f};uint32_t anchor_peer=0;
    auto start=std::chrono::steady_clock::now();
    int previous=-1,last_peers=-1;unsigned received=0,configs=0;bool matching=false;
    std::printf("QA peer %s: room=%s duration=%ld; synthetic car, no name claim or saved identity\n",name.c_str(),argv[2],seconds);
    std::fflush(stdout);
    for(;;) {
        auto clock=std::chrono::steady_clock::now();
        auto now=std::chrono::duration_cast<std::chrono::microseconds>(clock.time_since_epoch()).count();
        double elapsed=std::chrono::duration<double>(clock-start).count();if(elapsed>=seconds)break;
        float angle=float(elapsed*.3),heading=angle+1.5707963268f;
        local.state={};local.state.t_us=uint32_t(now);
        local.state.p[0]=anchor[0]+6.f*std::cos(angle);local.state.p[1]=anchor[1]+6.f*std::sin(angle);local.state.p[2]=anchor[2];
        local.state.q[2]=std::sin(heading*.5f);local.state.q[3]=std::cos(heading*.5f);
        local.state.v[0]=-1.8f*std::sin(angle);local.state.v[1]=1.8f*std::cos(angle);local.state.w[2]=.3f;
        local.state.speed=1.8f;local.state.rpm=2000;local.state.gear=1;local.state.throttle=.25f;local.state.steer=.1f;
        const auto &frame=client.frame(local,false,now);
        if(follow) {
            bool present=false;for(const auto &peer:frame.players)if(peer.id==anchor_peer && peer.has_pose)present=true;
            if(!present)for(const auto &peer:frame.players)if(peer.has_pose) {
                std::memcpy(anchor,peer.pose.p,sizeof anchor);anchor_peer=peer.id;
                std::puts("QA trajectory anchored to received peer position");std::fflush(stdout);break;
            }
        }
        for(const auto &peer:frame.players) {
            if(!peer.config.empty()){
                ++configs;matching|=peer.config.size()==local.config_size && !std::memcmp(peer.config.data(),local.config,local.config_size);
            }
            if(peer.has_pose) {
                ++received;
            }
        }
        auto status=client.snapshot();int state=int(status.transport.state);
        if(state!=previous || status.peers!=last_peers) {
            std::printf("QA state=%d peers=%d pose_frames=%u config_frames=%u\n",state,status.peers,received,configs);std::fflush(stdout);
            previous=state;last_peers=status.peers;
        }
        if(status.transport.state==xyz::LinkState::stopped || status.transport.state==xyz::LinkState::queued)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    client.disconnect();
    std::printf("QA complete: received_pose_frames=%u received_config_frames=%u same_config=%d\n",received,configs,matching);
    return received && configs?0:1;
}
