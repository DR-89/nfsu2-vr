#include "xyz_transport.h"
#include "xyz_lan.h"
#include "xyz_json.h"
#include "xyz_names.h"
#include "xyz_auth.h"
#include <algorithm>
#include <bitset>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

namespace xyz {
static void clear_profile(ConnectionProfile &p){
    xyz_auth_clear(p.claim.data(),p.claim.size());p={};
}
struct Transport::Impl {
    Factory factory;Clock clock;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread thread;
    bool quitting=false,wanted=false,failed=false;
    uint64_t request=0;
    uint32_t generation=0;
    ConnectionProfile pending;
    TransportState state;
    std::deque<ReceivedDatagram> incoming,incoming_lan,outgoing;
    // The service broadcasts departures for its entire /16. Only peers
    // with reliable traffic can own a stream in our endpoint. Tracking them
    // keeps unrelated lobby churn out of the bounded reliable queue while
    // the guest is busy unloading/loading a world.
    std::bitset<65536> reliable_peers;
    static uint64_t steady_ms() {
        return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    Impl(Factory f,Clock c):factory(std::move(f)),clock(c?std::move(c):Clock(steady_ms)) {
        // Start after every shared field has been initialized.
        thread=std::thread([this]{run();});
    }
    ~Impl() {
        {std::lock_guard<std::mutex> lock(mutex);quitting=true;clear_profile(pending);}
        wake.notify_all();thread.join();
    }
    bool current(uint64_t epoch) {
        std::lock_guard<std::mutex> lock(mutex);return !quitting && epoch==request;
    }
    void publish(uint64_t epoch,const Session &session,bool fresh=false,const std::string *notice=nullptr) {
        std::lock_guard<std::mutex> lock(mutex);if(quitting || epoch!=request)return;
        if(fresh){if(!++generation)++generation;incoming.clear();incoming_lan.clear();outgoing.clear();reliable_peers.reset();}
        state.state=session.state();state.vip=session.state()==LinkState::connected?session.vip():0;
        state.generation=state.vip?generation:0;
        state.queue_position=session.queue_position();state.queue_total=session.queue_total();
        state.races_allowed=state.vip && session.races_allowed();state.notice=notice?*notice:session.notice();
        state.lan_available=state.vip && session.lan_enabled();
        if(!state.vip){incoming.clear();incoming_lan.clear();outgoing.clear();reliable_peers.reset();}
        else if(!state.races_allowed){
            incoming_lan.clear();reliable_peers.reset();
            outgoing.erase(std::remove_if(outgoing.begin(),outgoing.end(),[](const ReceivedDatagram &d){return !d.bytes.empty() && d.bytes[0]!=1;}),outgoing.end());
        }
    }
    bool receive(uint64_t epoch,const Session &session,const std::vector<uint8_t> &bytes) {
        Datagram d;LanPacket packet;
        bool valid=session.state()==LinkState::connected && decode_datagram(bytes.data(),bytes.size(),d) && d.source!=session.vip();
        // The hub names each peer's endpoint. Browser co-op player two uses
        // 47211, and clients may choose another source port. Our own receive
        // port stays fixed; the core enforces the hub's peer list.
        bool roam=valid && d.kind==1 && d.destination==session.vip() && d.destination_port==roam_port;
        bool lan=valid && session.lan_enabled() && session.races_allowed() && d.source!=hub_ip &&
            (d.destination==session.vip() || (d.kind==3 && d.destination==UINT32_MAX)) && decode_lan(d,packet);
        std::lock_guard<std::mutex> lock(mutex);if(quitting || epoch!=request)return true;
        if(!roam && !lan){++state.dropped;return true;}
        auto &queue=lan?incoming_lan:incoming;
        if(queue.size()>=(lan?256:Transport::roam_queue_limit)){++state.dropped;return !lan || d.kind!=4;}
        if(lan && d.kind==4)reliable_peers.set(d.source&0xffff);
        queue.push_back({generation,bytes});return true;
    }
    bool departures(uint64_t epoch,const Session &session,const std::string &text) {
        if(!session.lan_enabled() || !session.races_allowed() || session.state()!=LinkState::connected)return true;
        auto j=parse_json(text,max_message);if(!j || json_string(j.get(),"t",24)!="delta")return true;
        const auto *gone=cJSON_GetObjectItemCaseSensitive(j.get(),"gone");
        if(!cJSON_IsArray(gone))return true;
        std::vector<uint32_t> ips;
        for(const auto *v=gone->child;v;v=v->next){
            if(!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) || v->valuedouble<0 || v->valuedouble>UINT32_MAX || std::floor(v->valuedouble)!=v->valuedouble)return true;
            auto ip=static_cast<uint32_t>(v->valuedouble);if(!service_ip(ip) || ip==hub_ip)return false;ips.push_back(ip);
        }
        std::lock_guard<std::mutex> lock(mutex);if(quitting || epoch!=request)return true;
        // A missed departure can leave a reliable connection attached to a
        // reused address. Reconnect the link rather than silently lose it.
        for(auto ip:ips){
            if(!reliable_peers.test(ip&0xffff))continue;
            if(incoming_lan.size()>=256){
                std::fprintf(stderr,"[XYZ transport] LAN departure capacity: queued=%zu limit=256\n",incoming_lan.size());
                return false;
            }
            incoming_lan.push_back({generation,{},ip});reliable_peers.reset(ip&0xffff);
        }
        return true;
    }
    bool next(uint64_t epoch,ReceivedDatagram &out) {
        std::lock_guard<std::mutex> lock(mutex);
        if(quitting || epoch!=request || state.state!=LinkState::connected || outgoing.empty())return false;
        out=std::move(outgoing.front());outgoing.pop_front();return true;
    }
    void run() noexcept {
        std::unique_ptr<Socket> socket;
        Session session;uint64_t active=0;bool enabled=false;ConnectionProfile profile;
        struct Clear {ConnectionProfile &p;~Clear(){clear_profile(p);}} clear{profile};
        try {
            for(;;) {
                bool changed=false;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    auto ready=[&]{return quitting || request!=active || !outgoing.empty();};
                    if(enabled)wake.wait_for(lock,std::chrono::milliseconds(5),ready);
                    else wake.wait(lock,ready);
                    if(quitting)break;
                    if(request!=active){active=request;enabled=wanted;clear_profile(profile);profile=std::move(pending);changed=true;}
                }
                if(changed) {
                    if(socket){socket->close();socket.reset();}
                    session=Session();
                    if(!enabled){session.stop();continue;}
                    if(!session.configure(profile.name,profile.identity,profile.claim,profile.lan)){session.stop();enabled=false;publish(active,session);continue;}
                }
                if(!enabled || !current(active))continue;
                uint64_t now=clock();
                if(session.state()==LinkState::offline || session.retry_due(now)) {
                    if(socket){socket->close();socket.reset();}
                    if(!current(active))continue;
                    {
                        socket=factory?factory():nullptr;
                        if(socket){
                            auto decision=check_name_reply(socket->name_info(profile.name,profile.claim),profile.name,!profile.claim.empty());
                            if(!current(active))continue;
                            if(!decision.allowed){
                                session.stop();publish(active,session,false,&decision.notice);enabled=false;socket->close();socket.reset();
                                clear_profile(profile);continue;
                            }
                        }
                        // The socket handshake timeout starts after HTTP,
                        // so a slow name lookup cannot expire a fresh socket.
                        if(!session.begin(clock()))continue;
                        publish(active,session);
                        if(!socket || !socket->connect(session.path()))session.disconnected(1006,clock());
                    }
                }
                auto fail=[&](uint16_t code,const char *reason){
                    // Fixed local reasons only: no server payload, identity,
                    // claim or URL enters the diagnostic log.
                    std::fprintf(stderr,"[XYZ transport] connection lost: %s code=%u generation=%u\n",reason,unsigned(code),generation);
                    session.disconnected(code,clock());publish(active,session);
                    if(socket){socket->close();socket.reset();}
                };
                SocketEvent event;
                for(unsigned i=0;socket && i<64 && current(active) && socket->poll(event);i++) {
                    now=clock();
                    if(event.kind==SocketEvent::opened) {
                        auto hello=session.opened(now);
                        if(hello.empty() || !socket->send(reinterpret_cast<const uint8_t*>(hello.data()),hello.size(),true)){fail(1006,"greeting send failed");break;}
                    } else if(event.kind==SocketEvent::text) {
                        auto before=session.state();
                        std::string control(event.data.begin(),event.data.end());
                        session.control(control,now);
                        publish(active,session,before!=LinkState::connected && session.state()==LinkState::connected);
                        if(!departures(active,session,control)){fail(1006,"LAN departure queue overflow");break;}
                        if(session.state()==LinkState::stopped){socket->close();socket.reset();break;}
                    } else if(event.kind==SocketEvent::binary){if(!receive(active,session,event.data)){fail(1006,"reliable LAN receive queue overflow");break;}}
                    else {fail(event.code,"socket closed");break;}
                }
                if(!current(active))continue;
                now=clock();
                if(session.expired(now)){fail(1006,"heartbeat timeout");continue;}
                if(socket) {
                    auto ping=session.heartbeat(now);
                    if(!ping.empty() && !socket->send(reinterpret_cast<const uint8_t*>(ping.data()),ping.size(),true)){fail(1006,"heartbeat send failed");continue;}
                    ReceivedDatagram message;
                    for(unsigned i=0;i<32 && current(active) && next(active,message);i++) {
                        if(message.generation!=generation)continue;
                        if(!socket->send(message.bytes.data(),message.bytes.size(),false)){fail(1006,"datagram send failed");break;}
                    }
                }
                publish(active,session);
                if(session.state()==LinkState::stopped)enabled=false;
            }
        } catch(...) {
            // Error strings from TLS/JNI/allocators may include endpoint data;
            // never forward them (or credentials) into the render snapshot.
            std::lock_guard<std::mutex> lock(mutex);
            failed=true;
            state.state=LinkState::stopped;state.vip=state.generation=0;state.queue_position=state.queue_total=0;
            state.races_allowed=state.lan_available=false;
            state.notice="Network worker stopped";incoming.clear();incoming_lan.clear();outgoing.clear();reliable_peers.reset();
        }
        if(socket)socket->close();
    }
};
Transport::Transport(Factory factory,Clock clock):impl_(std::make_unique<Impl>(std::move(factory),std::move(clock))) {}
Transport::~Transport()=default;
bool Transport::start(const ConnectionProfile &profile) {
    Session check;if(!check.configure(profile.name,profile.identity,profile.claim,profile.lan))return false;
    auto &p=*impl_;
    {std::lock_guard<std::mutex> lock(p.mutex);if(p.failed || p.quitting)return false;clear_profile(p.pending);p.pending=profile;p.wanted=true;++p.request;
        p.state={};p.state.state=LinkState::connecting;p.state.request=p.request;p.incoming.clear();p.incoming_lan.clear();p.outgoing.clear();p.reliable_peers.reset();}
    p.wake.notify_all();return true;
}
void Transport::stop() {
    auto &p=*impl_;
    {std::lock_guard<std::mutex> lock(p.mutex);clear_profile(p.pending);p.wanted=false;++p.request;p.state={};p.state.request=p.request;p.incoming.clear();p.incoming_lan.clear();p.outgoing.clear();p.reliable_peers.reset();}
    p.wake.notify_all();
}
TransportState Transport::snapshot() const {auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);return p.state;}
bool Transport::send(uint32_t generation,const uint8_t *bytes,size_t size) {
    Datagram d;if(!decode_datagram(bytes,size,d) || d.kind!=1 || d.source_port!=roam_port)return false;
    auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
    if(p.state.state!=LinkState::connected || !generation || generation!=p.state.generation ||
       d.source!=p.state.vip || d.destination==p.state.vip)return false;
    if(p.outgoing.size()>=Transport::roam_queue_limit){++p.state.dropped;return false;}
    p.outgoing.push_back({generation,std::vector<uint8_t>(bytes,bytes+size)});p.wake.notify_all();return true;
}
bool Transport::take(ReceivedDatagram &out) {
    auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);if(p.incoming.empty())return false;
    out=std::move(p.incoming.front());p.incoming.pop_front();return true;
}
bool Transport::send_lan(uint32_t generation,const uint8_t *bytes,size_t size) {
    Datagram d;LanPacket packet;if(!decode_datagram(bytes,size,d) || !decode_lan(d,packet))return false;
    auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);
    if(!p.state.lan_available || !p.state.races_allowed || !generation || generation!=p.state.generation ||
       d.source!=p.state.vip || d.destination==p.state.vip || d.destination==hub_ip || (d.kind==4 && d.destination==UINT32_MAX))return false;
    if(p.outgoing.size()>=256)return false;
    if(d.kind==4)p.reliable_peers.set(d.destination&0xffff);
    p.outgoing.push_back({generation,std::vector<uint8_t>(bytes,bytes+size)});p.wake.notify_all();return true;
}
bool Transport::take_lan(ReceivedDatagram &out) {
    auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);if(p.incoming_lan.empty())return false;
    out=std::move(p.incoming_lan.front());p.incoming_lan.pop_front();return true;
}
}
