// Explicit, bounded virtual-LAN service check. No game, lobby advertisement,
// public broadcast, saved identity, name claim or career data. Not a CTest.
#include "xyz_lan.h"
#include "xyz_transport.h"
#include "xyz_profiles.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

using namespace xyz;
class DiagnosticSocket final:public Socket {
    std::unique_ptr<Socket> socket_=make_socket();size_t largest_text_=0;char label_;
public:
    explicit DiagnosticSocket(char label):label_(label){}
    std::string name_info(const std::string &name,const std::string &claim)override{return socket_->name_info(name,claim);}
    bool connect(const std::string &path)override{return socket_->connect(path);}
    bool send(const uint8_t *p,size_t n,bool text)override{return socket_->send(p,n,text);}
    bool poll(SocketEvent &e)override {
        if(!socket_->poll(e))return false;
        if(e.kind==SocketEvent::text && e.data.size()>largest_text_){largest_text_=e.data.size();std::printf("LAN %c: largest control message %zu bytes\n",label_,largest_text_);}
        if(e.kind==SocketEvent::closed)std::printf("LAN %c: close code %u\n",label_,e.code);
        return true;
    }
    void close()override{socket_->close();}
};
int main(int argc,char **argv) {
    if(argc!=2 || std::string(argv[1])!="--connect") {std::puts("Use --connect for two temporary LAN endpoints on nfsu2.xyz; 25 seconds maximum.");return 2;}
    auto folder=std::filesystem::temp_directory_path()/"nfsu2-xyz-lan-no-persistence";
    if(std::filesystem::exists(folder))return 2;
    Profiles pa(folder),pb(folder);if(!pa.load() || !pb.load())return 2;
    Transport ta([]{return std::make_unique<DiagnosticSocket>('A');}),tb([]{return std::make_unique<DiagnosticSocket>('B');});
    auto seed=[](const Profiles &p){return uint32_t(std::strtoul(p.identity().substr(0,8).c_str(),nullptr,16));};
    LanEndpoint a(seed(pa)),b(seed(pb));
    if(!ta.start({"VR_LAN_"+pa.identity().substr(0,6),pa.identity(),{},true}) ||
       !tb.start({"VR_LAN_"+pb.identity().substr(0,6),pb.identity(),{},true}))return 2;
    auto listener=b.listen(9900),ua=a.udp(3658),ub=b.udp(3658);LanHandle client=0,server=0;
    std::vector<uint8_t> payload(65537),at_a,at_b;for(size_t i=0;i<payload.size();++i)payload[i]=uint8_t(i*31+(i>>7));
    size_t sent_a=0,sent_b=0;bool udp_sent=false,udp_seen=false,fin_a=false,fin_b=false,eof_a=false,eof_b=false;
    auto start=std::chrono::steady_clock::now();int result=1;
    while(std::chrono::steady_clock::now()-start<std::chrono::seconds(25)) {
        auto now=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        auto sa=ta.snapshot(),sb=tb.snapshot();
        if(sa.state==LinkState::stopped || sb.state==LinkState::stopped || sa.state==LinkState::queued || sb.state==LinkState::queued){std::printf("LAN probe stopped: A=%d (%s), B=%d (%s)\n",int(sa.state),sa.notice.c_str(),int(sb.state),sb.notice.c_str());break;}
        a.advance(ta,now);b.advance(tb,now);
        if(!client && sa.lan_available && sb.lan_available){client=a.connect(sb.vip,9900,now);std::puts("LAN probe: both TLS sessions connected");}
        if(!server)server=b.accept(listener);
        if(client && a.status(client)==LanError::ok && server) {
            if(sent_a<payload.size()){auto r=a.send(client,payload.data()+sent_a,payload.size()-sent_a);sent_a+=r.bytes;}
            if(sent_b<payload.size()){auto r=b.send(server,payload.data()+sent_b,payload.size()-sent_b);sent_b+=r.bytes;}
            if(sent_a==payload.size() && !fin_a)fin_a=a.shutdown_write(client)==LanError::ok;
            if(sent_b==payload.size() && !fin_b)fin_b=b.shutdown_write(server)==LanError::ok;
            if(!udp_sent)udp_sent=a.send_to(ua,sb.vip,3658,payload.data(),lan_data_max).error==LanError::ok;
            uint8_t buf[8192];
            auto read=[&](LanEndpoint &ep,LanHandle h,std::vector<uint8_t> &out,bool &eof){
                for(int i=0;i<16;++i){auto r=ep.receive(h,buf,sizeof buf);if(r.error!=LanError::ok)break;if(!r.bytes){eof=true;break;}out.insert(out.end(),buf,buf+r.bytes);}
            };
            read(a,client,at_a,eof_a);read(b,server,at_b,eof_b);
            uint32_t peer;uint16_t port;std::vector<uint8_t> data;
            if(!udp_seen && b.receive_from(ub,peer,port,data).error==LanError::ok)
                udp_seen=peer==sa.vip && port==3658 && data.size()==lan_data_max && std::equal(data.begin(),data.end(),payload.begin());
            if(eof_a && eof_b && udp_seen){result=at_a==payload && at_b==payload?0:1;break;}
        } else if(client && a.status(client)!=LanError::would_block && a.status(client)!=LanError::ok){std::printf("LAN probe: stream failed (%d)\n",int(a.status(client)));break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::printf("LAN service check: %s; TCP bytes A=%zu B=%zu; FIN=%d/%d; UDP=%d; synthetic endpoints, no game race\n",result?"FAIL":"PASS",at_a.size(),at_b.size(),eof_a,eof_b,udp_seen);
    ta.stop();tb.stop();return result;
}
