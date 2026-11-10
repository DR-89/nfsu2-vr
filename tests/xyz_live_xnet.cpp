// Explicit service check of Client -> guest socket adapter -> LAN -> WSS.
// Synthetic RAM, temporary identities, private unicast only; not a game race.
#include "xyz_client.h"
#include "xyz_profiles.h"
#include "xyz_guest_memory.h"
#include "xyz_game_api.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <thread>
using namespace xyz;
struct Endpoint {
    bool api=false;
    explicit Endpoint(bool runtime=false):api(runtime){}
    Client client{[]{return make_socket();}};XyzGuestMemory memory;XyzXboxGuest guest=memory.guest();
    uint32_t call(XyzXnetOp op,std::initializer_list<uint32_t> args,uint64_t now){uint32_t result=UINT32_MAX;
        bool handled=api?xyz_online_xnet(&guest,op,1,args.begin(),unsigned(args.size()),now,&result)!=0:
            client.socket_call(guest,op,1,args.begin(),unsigned(args.size()),now,result);
        return handled?result:UINT32_MAX;}
};
int main(int argc,char **argv){
    if(argc!=2 || std::string(argv[1])!="--connect"){std::puts("Use --connect for two temporary session/socket endpoints on nfsu2.xyz; 25 seconds maximum.");return 2;}
    auto folder=std::filesystem::temp_directory_path()/"nfsu2-xyz-xnet-no-persistence";
    if(std::filesystem::exists(folder))return 2;Profiles pa(folder),pb(folder);if(!pa.load() || !pb.load())return 2;
    Endpoint a(true),b;
    auto name="VR_NET_"+pa.identity().substr(0,6);
    if(!xyz_online_connect_races(name.c_str(),"VR_SOCKET_DIAGNOSTIC",pa.identity().c_str(),pa.identity().c_str()) ||
       !b.client.connect({{"VR_NET_"+pb.identity().substr(0,6),pb.identity(),{},true},"VR_SOCKET_DIAGNOSTIC",2}))return 2;
    constexpr uint32_t sa=0x10000,len=0x10040,fd=0x10050,tv=0x100a0,buf=0x12000,udpbuf=0x22010,out=0x23000;
    std::vector<uint8_t> payload(65537),rx_a,rx_b;for(size_t i=0;i<payload.size();++i)payload[i]=uint8_t(i*31+(i>>7));
    XyzGuestMemory::write(&a.memory,buf,payload.data(),payload.size());XyzGuestMemory::write(&b.memory,buf,payload.data(),payload.size());
    uint32_t listener=0,client=0,server=0,ua=0,ub=0;size_t sent_a=0,sent_b=0;
    bool initialized=false,udp_sent=false,udp_received=false,fin_a=false,fin_b=false,eof_a=false,eof_b=false;
    XyzXboxFrame local{};local.status=XYZ_XBOX_OTHER_RACE;
    auto start=std::chrono::steady_clock::now();int result=1;
    while(std::chrono::steady_clock::now()-start<std::chrono::seconds(25)){
        auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        xyz_online_frame(&a.guest,&local,0,now*1000);b.client.frame(local,false,int64_t(now)*1000);
        XYZOnlineStatus as{};xyz_online_snapshot(&as);auto bs=b.client.snapshot().transport;
        if(as.state==XYZ_ONLINE_STOPPED || bs.state==LinkState::stopped || as.state==XYZ_ONLINE_QUEUED || bs.state==LinkState::queued){std::printf("Session check stopped: states=%d/%d\n",as.state,int(bs.state));break;}
        if(!initialized && as.state==XYZ_ONLINE_CONNECTED && bs.lan_available){
            initialized=true;std::puts("Session check: both TLS sessions connected");
            listener=b.call(XYZ_N_SOCKET,{2,1,6},now);b.memory.address(sa,0,9900);
            if(b.call(XYZ_N_BIND,{listener,sa,16},now) || b.call(XYZ_N_LISTEN,{listener,6},now))break;
            ua=a.call(XYZ_N_SOCKET,{2,2,254},now);ub=b.call(XYZ_N_SOCKET,{2,2,254},now);
            a.memory.address(sa,0,3658);b.memory.address(sa,0,3658);
            if(a.call(XYZ_N_BIND,{ua,sa,16},now) || b.call(XYZ_N_BIND,{ub,sa,16},now))break;
            client=a.call(XYZ_N_SOCKET,{2,1,6},now);a.memory.address(sa,bs.vip,9900);
            if(a.call(XYZ_N_CONNECT,{client,sa,16},now)!=UINT32_MAX || a.call(XYZ_N_ERROR,{},now)!=10035)break;
        }
        if(initialized){
            if(!server){auto h=b.call(XYZ_N_ACCEPT,{listener,0,0},now);if(h!=UINT32_MAX)server=h;}
            a.memory.put(fd,1);a.memory.put(fd+4,client);
            bool connected=a.call(XYZ_N_SELECT,{0,0,fd,0,tv},now)==1;
            if(connected && server){
                auto send=[&](Endpoint &e,uint32_t h,size_t &sent){if(sent<payload.size()){
                    auto n=e.call(XYZ_N_SEND,{h,buf+uint32_t(sent),uint32_t(payload.size()-sent),0},now);if(n!=UINT32_MAX)sent+=n;}};
                send(a,client,sent_a);send(b,server,sent_b);
                if(sent_a==payload.size() && !fin_a)fin_a=a.call(XYZ_N_SHUTDOWN,{client,1},now)==0;
                if(sent_b==payload.size() && !fin_b)fin_b=b.call(XYZ_N_SHUTDOWN,{server,1},now)==0;
                if(!udp_sent){a.memory.at(udpbuf)[0]=a.memory.at(udpbuf)[1]=0;std::memcpy(a.memory.at(udpbuf)+2,payload.data(),lan_data_max);
                    a.memory.address(sa,bs.vip,3658);udp_sent=a.call(XYZ_N_SENDTO,{ua,udpbuf,uint32_t(lan_data_max+2),0,sa,16},now)==lan_data_max+2;}
            }
            if(server){
                auto receive=[&](Endpoint &e,uint32_t h,std::vector<uint8_t> &bytes,bool &eof){for(unsigned i=0;i<16 && !eof;++i){
                    auto n=e.call(XYZ_N_RECV,{h,out,8192,0},now);if(n==UINT32_MAX)break;if(!n)eof=true;else bytes.insert(bytes.end(),e.memory.at(out),e.memory.at(out)+n);}};
                receive(a,client,rx_a,eof_a);receive(b,server,rx_b,eof_b);
                if(!udp_received){b.memory.put(len,16);auto n=b.call(XYZ_N_RECVFROM,{ub,out,1500,0,sa,len},now);
                    if(n!=UINT32_MAX)udp_received=n==lan_data_max+2 && !b.memory.at(out)[0] && !b.memory.at(out)[1] &&
                        !std::memcmp(b.memory.at(out)+2,payload.data(),lan_data_max) && b.memory.at(sa)[2]==0x0e && b.memory.at(sa)[3]==0x4a;}
                if(eof_a && eof_b && udp_received){result=rx_a==payload && rx_b==payload?0:1;break;}
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    xyz_online_disconnect();b.client.disconnect();
    std::printf("Session/XNet service check: %s; TCP bytes=%zu/%zu; FIN=%d/%d; UDP254=%d; game C API with synthetic guest RAM, no game race\n",result?"FAIL":"PASS",rx_a.size(),rx_b.size(),eof_a,eof_b,udp_received);
    return result;
}
