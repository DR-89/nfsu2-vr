// Explicit, bounded read-only public-hub diagnostic. Publishes MENUS only:
// no car, position, race, claim, chat or saved identity. Not part of CTest.
#include "xyz_transport.h"
#include "xyz_profiles.h"
extern "C" {
#include "roam_core.h"
}
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
using namespace xyz;
struct Link { Transport *transport;uint32_t vip,generation; };
static void send(void *ctx,uint32_t ip,uint16_t port,const uint8_t *bytes,int n) {
    auto &l=*static_cast<Link*>(ctx);Datagram d;d.source=l.vip;d.destination=ip;d.destination_port=port;
    d.payload.assign(bytes,bytes+n);auto wire=encode_datagram(d);
    l.transport->send(l.generation,wire.data(),wire.size());
}
int main(int argc,char **argv) {
    if((argc!=2 && argc!=4) || std::strcmp(argv[1],"--connect") ||
       (argc==4 && (std::strcmp(argv[2],"--name") || online_name(argv[3])!=argv[3])))return 2;
    auto path=std::filesystem::temp_directory_path()/"nfsu2-xyz-discovery-no-persistence";
    if(std::filesystem::exists(path))return 2;
    Profiles profiles(path);if(!profiles.load())return 2;
    auto name="VR_Diag_"+profiles.identity().substr(0,6);
    if(argc==4)name=argv[3]; // explicit selected profile name; still no claim
    Transport transport([]{return make_socket();});
    if(!transport.start({name,profiles.identity(),{}}))return 2;
    auto core=std::make_unique<rc_session>();Link link{&transport,0,0};
    auto start=std::chrono::steady_clock::now();int previous=-1;int64_t last=0;uint32_t map=0;
    while(std::chrono::steady_clock::now()-start<std::chrono::seconds(25)) {
        int64_t now=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        auto s=transport.snapshot();
        if(previous!=int(s.state)){previous=int(s.state);std::printf("Discovery transport=%d\n",previous);std::fflush(stdout);}
        if(s.state==LinkState::stopped || s.state==LinkState::queued)break;
        if(s.generation && s.generation!=link.generation) {
            link.vip=s.vip;link.generation=s.generation;rc_config cfg{};
            cfg.self_id=0x56520000u|uint32_t(std::stoul(profiles.identity().substr(0,4),nullptr,16));
            cfg.port=roam_port;cfg.hub_ip=hub_ip;cfg.hub_port=roam_port;cfg.links=2;cfg.max_players=3;
            std::snprintf(cfg.name,sizeof cfg.name,"%s",name.c_str());rc_io io{};io.user=&link;io.send=send;
            rc_session_init(core.get(),&cfg,&io);
        }
        if(s.generation) {
            ReceivedDatagram in;while(transport.take(in)) {
                Datagram d;if(in.generation==link.generation && decode_datagram(in.bytes.data(),in.bytes.size(),d))
                    rc_receive(core.get(),d.source,d.source_port,d.payload.data(),int(d.payload.size()),now);
            }
            rc_tick(core.get(),now);
            if(now-last>=3000000) {
                last=now;rc_ask_map(core.get(),now,16);
                std::printf("Discovery lists=%u rx=%u bad=%u version=%u dropped=%llu map_generation=%u map_total=%u\n",
                    core->hub_lists,core->rx,core->rx_bad,core->rx_version,(unsigned long long)s.dropped,core->map_gen,core->map_total);
                std::fflush(stdout);
            }
            map=core->map_gen;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if(link.generation)rc_shutdown(core.get(),0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));transport.stop();
    return core->hub_lists && map?0:1;
}
