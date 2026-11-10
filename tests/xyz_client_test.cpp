#include "xyz_client.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <thread>
#include <array>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace xyz;
struct Wire {
    std::mutex mutex;std::deque<SocketEvent> events;std::deque<std::vector<uint8_t>> outgoing;
    unsigned created=0,hellos=0,closed=0;std::thread::id game=std::this_thread::get_id();
    void push(SocketEvent e){std::lock_guard<std::mutex> l(mutex);events.push_back(std::move(e));}
    void control(const char *s){SocketEvent e;e.kind=SocketEvent::text;e.data.assign(s,s+std::strlen(s));push(e);}
    void packet(std::vector<uint8_t> v){SocketEvent e;e.kind=SocketEvent::binary;e.data=std::move(v);push(std::move(e));}
};
class SocketFixture:public Socket {
    std::shared_ptr<Wire> w;
    void owner(){CHECK(std::this_thread::get_id()!=w->game);}
public:
    explicit SocketFixture(std::shared_ptr<Wire> wire):w(std::move(wire)){owner();std::lock_guard<std::mutex> l(w->mutex);++w->created;}
    std::string name_info(const std::string &name,const std::string&)override{owner();return "{\"name\":\""+name+"\",\"state\":\"free\"}";}
    bool connect(const std::string&)override{owner();SocketEvent e;e.kind=SocketEvent::opened;w->push(e);return true;}
    bool send(const uint8_t *b,size_t n,bool text)override {
        owner();std::lock_guard<std::mutex> l(w->mutex);
        if(text)++w->hellos;else w->outgoing.emplace_back(b,b+n);return true;
    }
    bool poll(SocketEvent &e)override{owner();std::lock_guard<std::mutex> l(w->mutex);if(w->events.empty())return false;e=std::move(w->events.front());w->events.pop_front();return true;}
    void close()override{owner();std::lock_guard<std::mutex> l(w->mutex);++w->closed;w->events.clear();w->outgoing.clear();}
};
constexpr uint32_t mine=0x0a4d0102,other=0x0a4d0103;
static std::vector<uint8_t> hub(uint32_t to,uint32_t ip,uint32_t id,uint8_t mark=0) {
    rc_msg m{};m.h.type=RC_MSG_PEERS;m.h.sender=0xffffffff;m.u.peers.n=1;
    m.u.peers.e[0].ip=ip;m.u.peers.e[0].port=roam_port;m.u.peers.e[0].id=id;
    if(mark){m.h.flags|=RC_HF_CREW;m.u.peers.crew[0]=mark;}
    uint8_t b[RC_MAX_DGRAM];int n=rc_encode(&m,b,sizeof b);CHECK(n>0);
    Datagram d;d.source=hub_ip;d.destination=to;d.payload.assign(b,b+n);return encode_datagram(d);
}
static XyzXboxFrame car(float x) {
    XyzXboxFrame f{};f.status=XYZ_XBOX_READY;f.world=0x100000;f.car=0x110000;
    f.state.q[3]=1;f.state.p[0]=x;f.state.rpm=4000;f.state.speed=10;f.state.v[0]=10;f.state.gear=2;
    rc_carcfg c{};c.type_hash=123;std::strcpy(c.type_name,"TEST");c.n_parts=RC_PART_SLOTS;
    for(int i=0;i<RC_PART_SLOTS;i++){c.part[i].slot=(uint8_t)i;c.part[i].key=100+i;c.part[i].owner=55;}
    f.config_size=(uint16_t)rc_carcfg_encode(&c,f.config,sizeof f.config);CHECK(f.config_size>RC_CFG_FRAG);return f;
}
static void crowded_room() {
    // Exercise the production client/worker/core together at ten game frames
    // per second, with 64 independently moving, fragmented-config peers.
    // The hub changes list order, but never removes these peers.
    constexpr unsigned count=64;
    auto w=std::make_shared<Wire>();std::atomic<uint64_t> ms{1000};int64_t now=1000000;
    Client client([w]{return std::make_unique<SocketFixture>(w);},[&]{return ms.load();});
    ClientProfile profile{{"Crowd_VR","0123456789abcdef0123456789abcdef",{}},"CROWD",11};
    CHECK(client.connect(profile));auto local=car(10);
    auto wait_worker=[&] {
        auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        for(;;){
            bool empty;{std::lock_guard<std::mutex> l(w->mutex);empty=w->events.empty();}
            if(empty)break;
            CHECK(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // poll removes the final event just before Transport accepts it.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    };
    client.frame(local,false,now);
    for(unsigned i=0;i<1000;++i){
        bool greeted;{std::lock_guard<std::mutex> l(w->mutex);greeted=w->hellos!=0;}
        if(greeted)break;CHECK(i<999);std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    w->control("{\"t\":\"welcome\",\"vip\":172818690,\"token\":\"fixture-crowd-token\"}");wait_worker();
    std::array<std::unique_ptr<RoamLink>,count> peers;
    std::array<XyzXboxFrame,count> cars;std::array<float,count> first{},last{};
    for(unsigned i=0;i<count;++i){
        peers[i]=std::make_unique<RoamLink>();
        CHECK(peers[i]->configure("Peer_"+std::to_string(i),"CROWD",100+i,1) && peers[i]->bind(0x0a4d0200+i,1));
        cars[i]=car(40.f+i*3.f);
    }
    bool advertised=false;unsigned complete=0;
    for(unsigned frame=0;frame<180;++frame){
        for(unsigned sub=0;sub<2;++sub){
            now+=50000;ms=uint64_t(now)/1000;
            if(frame%5==0 && !sub){
                rc_msg list{};list.h.type=RC_MSG_PEERS;list.h.sender=0xffffffff;list.u.peers.n=count;
                for(unsigned i=0;i<count;++i){unsigned p=(i+frame)%count;
                    list.u.peers.e[i].ip=0x0a4d0200+p;list.u.peers.e[i].port=roam_port;list.u.peers.e[i].id=100+p;
                    auto back=hub(0x0a4d0200+p,mine,11);CHECK(peers[p]->receive(back.data(),back.size(),1,now));}
                uint8_t data[RC_MAX_DGRAM];int n=rc_encode(&list,data,sizeof data);CHECK(n>0);
                Datagram d;d.source=hub_ip;d.destination=mine;d.payload.assign(data,data+n);w->packet(encode_datagram(d));
            }
            for(unsigned i=0;i<count;++i){
                cars[i].state.p[0]=40.f+i*3.f+float(now-1000000)*.00001f;
                peers[i]->frame(cars[i],false,now);std::vector<uint8_t> wire;
                while(peers[i]->take(wire)){Datagram d;CHECK(decode_datagram(wire.data(),wire.size(),d));
                    if(d.destination==mine)w->packet(std::move(wire));}
            }
        }
        local.state.p[0]=10.f+float(now-1000000)*.00001f;
        wait_worker();const auto &view=client.frame(local,false,now);
        CHECK(view.generation && !client.snapshot().transport.dropped);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::deque<std::vector<uint8_t>> sent;{std::lock_guard<std::mutex> l(w->mutex);sent.swap(w->outgoing);}
        for(auto &wire:sent){Datagram d;CHECK(decode_datagram(wire.data(),wire.size(),d));
            if(d.destination==hub_ip){rc_msg m{};CHECK(rc_decode(d.payload.data(),int(d.payload.size()),&m)==RC_OK);
                if(m.h.type==RC_MSG_HELLO){CHECK(m.u.hello.links==count);advertised=true;}}
            else {CHECK(d.destination>=0x0a4d0200 && d.destination<0x0a4d0200+count);
                CHECK(peers[d.destination-0x0a4d0200]->receive(wire.data(),wire.size(),1,now));}}
        if(frame>=60){
            CHECK(view.players.size()==count);
            for(const auto &p:view.players){
                CHECK(p.id>=100 && p.id<100+count && p.has_pose && p.listed && !p.lost && !p.send_only);
                CHECK(p.config.size()==cars[p.id-100].config_size && p.snap_age_ms<=100);
                if(frame==60)first[p.id-100]=p.pose.p[0];else CHECK(p.pose.p[0]>last[p.id-100]);
                last[p.id-100]=p.pose.p[0];
            }
            ++complete;
        }
    }
    CHECK(advertised && complete==120);
    for(unsigned i=0;i<count;++i)CHECK(last[i]-first[i]>115.f);
    CHECK(client.snapshot().roam.configured==count && client.snapshot().roam.moving==count);
    client.disconnect();CHECK(client.frame(local,false,now+100000).players.empty());
    std::puts("Client crowd: 64 independent cars, 20 Hz network / 10 Hz game, reordered hub lists, continuous motion and zero queue drops passed");
}
int main() {
    crowded_room();
    auto w=std::make_shared<Wire>();std::atomic<uint64_t> ms{1000};int64_t now=1000000;
    Client client([w]{return std::make_unique<SocketFixture>(w);},[&]{return ms.load();});
    auto local=car(10),foreign=car(40);RoamLink peer;CHECK(peer.configure("Peer","ROOM",22,2) && peer.bind(other,1));
    ClientProfile profile{{"VR_Driver","0123456789abcdef0123456789abcdef",{}},"ROOM",11};
    client.frame(local,false,now);CHECK(!w->created);
    CHECK(client.connect(profile) && client.snapshot().transport.state==LinkState::connecting && !w->created);
    auto tick=[&](bool paused=false)->const ClientFrame& {now+=50000;ms=(uint64_t)now/1000;auto &f=client.frame(local,paused,now);std::this_thread::sleep_for(std::chrono::milliseconds(2));return f;};
    auto wait=[&](auto predicate){for(int i=0;i<1000 && !predicate();i++)tick();CHECK(predicate());};
    wait([&]{std::lock_guard<std::mutex> l(w->mutex);return w->hellos>0;});
    w->control("{\"t\":\"queue\",\"pos\":1,\"of\":2,\"ticket\":\"fixture-ticket\"}");
    wait([&]{return client.snapshot().transport.state==LinkState::queued;});CHECK(tick().players.empty());
    w->control("{\"t\":\"welcome\",\"vip\":172818690,\"token\":\"fixture-resume-token\"}");
    wait([&]{return client.snapshot().transport.state==LinkState::connected;});uint32_t generation=tick().generation;CHECK(generation);
    auto ha=hub(mine,other,22),hb=hub(other,mine,11);
    auto exchange=[&](bool paused=false) {
        w->packet(ha);CHECK(peer.receive(hb.data(),hb.size(),1,now));peer.frame(foreign,false,now);tick(paused);
        std::deque<std::vector<uint8_t>> sent;
        {std::lock_guard<std::mutex> l(w->mutex);sent.swap(w->outgoing);}
        for(auto &v:sent){Datagram d;CHECK(decode_datagram(v.data(),v.size(),d));if(d.destination==other)CHECK(peer.receive(v.data(),v.size(),1,now));}
        std::vector<uint8_t> v;while(peer.take(v)){Datagram d;CHECK(decode_datagram(v.data(),v.size(),d));if(d.destination==mine)w->packet(std::move(v));}
    };
    for(int i=0;i<130;i++)exchange();
    const auto &f=tick();CHECK(f.players.size()==1 && f.players[0].id==22 && f.players[0].has_pose);
    CHECK(f.players[0].config.size()==foreign.config_size && !std::memcmp(f.players[0].config.data(),foreign.config,foreign.config_size));
    CHECK(peer.peers()==1 && peer.peer(0)->cfg_len==local.config_size);
    w->packet(hub(mine,other,22,RC_MARK_TXONLY));tick();
    const auto &kept=tick();CHECK(kept.players.size()==1 && kept.players[0].id==22 && kept.players[0].has_pose && !kept.players[0].listed);
    for(int i=0;i<10;i++)exchange(true);CHECK(peer.peer(0)->status==RC_ST_PAUSED);
    local.status=XYZ_XBOX_OTHER_RACE;for(int i=0;i<10;i++)exchange();
    CHECK(tick().players.empty() && peer.peer(0)->status==RC_ST_EVENT);
    local.status=XYZ_XBOX_READY;for(int i=0;i<10;i++)exchange();CHECK(!tick().players.empty());
    SocketEvent closed;closed.kind=SocketEvent::closed;closed.code=1006;w->push(closed);
    wait([&]{return client.snapshot().transport.state==LinkState::retry;});CHECK(!tick().generation && tick().players.empty());
    wait([&]{return client.snapshot().transport.state==LinkState::greeting;});
    w->control("{\"t\":\"welcome\",\"vip\":172818690,\"token\":\"fixture-new-token\"}");
    wait([&]{return client.snapshot().transport.state==LinkState::connected;});CHECK(tick().generation!=generation && tick().players.empty());
    // Disconnect reaches the worker even while no game frame is running.
    unsigned before_close;{std::lock_guard<std::mutex> l(w->mutex);before_close=w->closed;}
    client.disconnect();
    bool closed_without_frame=false;
    for(int i=0;i<1000;i++) {
        {std::lock_guard<std::mutex> l(w->mutex);closed_without_frame=w->closed>before_close;}
        if(closed_without_frame)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(closed_without_frame && client.snapshot().transport.state==LinkState::offline);
    // Latest command wins before the game frame; no connection for the canceled profile.
    client.connect(profile);client.disconnect();CHECK(client.snapshot().transport.state==LinkState::offline);
    CHECK(!tick().generation && tick().players.empty());
    CHECK(client.connect(profile));tick();client.abort();CHECK(client.snapshot().transport.state==LinkState::offline);
    CHECK(!tick().generation && tick().players.empty());
    profile.player_id=0;CHECK(!client.connect(profile));
    std::puts("Client: explicit join, queue, real core exchange, fragmented cars, pause/event, resume and disconnect passed");
}
