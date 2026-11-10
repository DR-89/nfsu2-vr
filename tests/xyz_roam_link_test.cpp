#include "xyz_roam_link.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
using namespace xyz;
constexpr uint32_t ip_a=0x0a4d0102,ip_b=0x0a4d0103;
static std::vector<uint8_t> hub_list(uint32_t to,uint32_t peer_ip,uint32_t peer_id,uint16_t port=roam_port,uint8_t mark=0) {
    rc_msg m{}; m.h.type=RC_MSG_PEERS; m.h.sender=0xffffffffu;
    m.u.peers.n=1; m.u.peers.e[0].ip=peer_ip; m.u.peers.e[0].port=port; m.u.peers.e[0].id=peer_id;
    if(mark){m.h.flags|=RC_HF_CREW;m.u.peers.crew[0]=mark;}
    uint8_t bytes[RC_MAX_DGRAM]; int n=rc_encode(&m,bytes,sizeof bytes); CHECK(n>0);
    Datagram d; d.source=hub_ip; d.destination=to; d.payload.assign(bytes,bytes+n);
    return encode_datagram(d);
}
static XyzXboxFrame car(float x) {
    XyzXboxFrame f{}; f.status=XYZ_XBOX_READY; f.world=0x100000; f.car=0x102000;
    f.state.p[0]=x; f.state.q[3]=1; f.state.rpm=3000; f.state.speed=10; f.state.v[0]=10; f.state.gear=2;
    rc_carcfg c{}; c.type_hash=0x12345678; std::strcpy(c.type_name,"TESTCAR"); c.has_perf=1;
    c.n_parts=RC_PART_SLOTS; c.perf[0]=0x80; c.perf[51]=77;
    c.has_stance=1;c.stance_fc=150;c.stance_rc=23;c.stance_fo=-40;c.stance_ro=80;c.stance_tw=130;
    for(int i=0;i<RC_PART_SLOTS;i++) { c.part[i].slot=(uint8_t)i; c.part[i].key=10000+i; c.part[i].owner=42; }
    int n=rc_carcfg_encode(&c,f.config,sizeof f.config); CHECK(n>RC_CFG_FRAG); f.config_size=(uint16_t)n;
    return f;
}
static unsigned snapshots,configs,hellos;
static void coop_endpoint() {
    // The real core on the browser's second-player port; no rewritten packets
    // or same-port assumptions may hide a wrong endpoint in the adapter.
    RoamLink vr;CHECK(vr.configure("VR_Player","",11,2) && vr.bind(ip_a,1));
    auto core=std::make_unique<rc_session>();std::deque<std::vector<uint8_t>> pending;
    rc_config c{};c.self_id=22;c.port=47211;c.max_players=3;c.links=2;c.hub_ip=hub_ip;c.hub_port=47211;
    std::strcpy(c.name,"Browser_2");rc_io io{};io.user=&pending;
    io.send=[](void *ctx,uint32_t ip,uint16_t port,const uint8_t *b,int n){
        Datagram d;d.source=ip_b;d.destination=ip;d.source_port=47211;d.destination_port=port;d.payload.assign(b,b+n);
        static_cast<std::deque<std::vector<uint8_t>>*>(ctx)->push_back(encode_datagram(d));
    };
    rc_session_init(core.get(),&c,&io);auto local=car(10),remote=car(40);
    auto list=hub_list(ip_a,ip_b,22,47211),back=hub_list(ip_b,ip_a,11);
    Datagram hub;CHECK(decode_datagram(back.data(),back.size(),hub));
    rc_set_cfg(core.get(),remote.config,remote.config_size,1000000);rc_set_status(core.get(),RC_ST_ROAM);
    for(int i=0;i<100;++i) {
        int64_t now=1000000+50000ll*i;
        if(i%10==0) {CHECK(vr.receive(list.data(),list.size(),1,now));rc_receive(core.get(),hub_ip,47211,hub.payload.data(),int(hub.payload.size()),now);}
        vr.frame(local,false,now);remote.state.t_us=uint32_t(now);rc_push_state(core.get(),&remote.state,now);rc_tick(core.get(),now);
        std::vector<uint8_t> wire;
        while(vr.take(wire)) {Datagram d;CHECK(decode_datagram(wire.data(),wire.size(),d));
            if(d.destination==ip_b){CHECK(d.destination_port==47211);rc_receive(core.get(),d.source,d.source_port,d.payload.data(),int(d.payload.size()),now);}}
        while(!pending.empty()) {wire=std::move(pending.front());pending.pop_front();Datagram d;CHECK(decode_datagram(wire.data(),wire.size(),d));
            if(d.destination==ip_a)CHECK(vr.receive(wire.data(),wire.size(),1,now));}
    }
    CHECK(vr.peers()==1 && rc_established(core.get())==1);
    auto p=vr.listed_peer(0);CHECK(p && p->port==47211 && p->cfg_len==remote.config_size);
    rc_carcfg look{};CHECK(rc_carcfg_decode(p->cfg,p->cfg_len,&look)==RC_OK);
    CHECK(look.has_stance && look.stance_fc==150 && look.stance_rc==23 && look.stance_fo==-40 && look.stance_ro==80 && look.stance_tw==130);
    rc_pose pose{};CHECK(vr.pose(0,5950000,50000,pose));
}
static bool route(RoamLink &from,RoamLink &to,int64_t now,bool send_snapshots=true) {
    bool any=false; std::vector<uint8_t> wire;
    while(from.take(wire)) {
        any=true; Datagram d; CHECK(decode_datagram(wire.data(),wire.size(),d));
        CHECK(d.kind==1 && d.source_port==roam_port && d.destination_port==roam_port);
        rc_msg m{}; CHECK(rc_decode(d.payload.data(),(int)d.payload.size(),&m)==RC_OK);
        if(m.h.type==RC_MSG_HELLO) {
            ++hellos; CHECK(!(m.h.flags&(RC_HF_DUELS|RC_HF_LOBBY|RC_HF_DRIFT|RC_HF_SAY|RC_HF_DENTS)));
            CHECK(m.u.hello.platform==0 && m.u.hello.core_ver==RC_CORE_VER);
        }
        if(m.h.type==RC_MSG_SNAP) ++snapshots;
        if(m.h.type==RC_MSG_SNAP && !send_snapshots)continue;
        if(m.h.type==RC_MSG_CFG) ++configs;
        if(d.destination!=hub_ip) CHECK(to.receive(wire.data(),wire.size(),9,now));
    }
    return any;
}
static void visibility_grace() {
    RoamLink a,b;CHECK(a.configure("Grace_A","VR_TEST",11,6) && b.configure("Grace_B","VR_TEST",22,6));
    CHECK(a.bind(ip_a,9) && b.bind(ip_b,9));
    auto fa=car(10),fb=car(30);auto ha=hub_list(ip_a,ip_b,22),hb=hub_list(ip_b,ip_a,11);
    for(int i=0;i<100;i++) {
        int64_t now=1000000+i*50000;
        if(i%10==0){CHECK(a.receive(ha.data(),ha.size(),9,now));CHECK(b.receive(hb.data(),hb.size(),9,now));}
        a.frame(fa,i>=40,now);b.frame(fb,false,now);
        for(int k=0;k<8;k++){bool x=route(a,b,now,i<=40),y=route(b,a,now);if(!x && !y)break;CHECK(k<7);}
    }
    auto p=b.peer(0);rc_pose pose{};
    CHECK(p && p->status==RC_ST_PAUSED && 5950000-p->last_snap_rx>2000000);
    CHECK(b.pose(0,5950000,50000,pose) && pose.speed==0 && pose.v[0]==0);
    // Omit the peer from one hub list. It is no longer a new-slot candidate,
    // but the existing car remains until the core's 2.5-second grace ends.
    auto missing=hub_list(ip_b,0x0a4d0110,33);
    CHECK(b.receive(missing.data(),missing.size(),9,6000000));b.frame(fb,false,6000000);
    CHECK(b.peer(0) && !b.listed_peer(0) && b.pose(0,6000000,50000,pose));
    CHECK(b.receive(hb.data(),hb.size(),9,6500000));b.frame(fb,false,6500000);
    CHECK(b.listed_peer(0) && b.pose(0,6500000,50000,pose));
    // Genuine stale traffic/lost peers still disappear, even when paused.
    b.frame(fb,false,10000000);CHECK(!b.pose(0,10000000,50000,pose));
    b.unbind();CHECK(!b.pose(0,10000000,50000,pose));
}
static void career_start_visibility() {
    RoamLink a,b;CHECK(a.configure("Spawn_A","VR_TEST",11,6) && b.configure("Spawn_B","VR_TEST",22,6));
    CHECK(a.bind(ip_a,9) && b.bind(ip_b,9));
    auto fa=car(10),fb=car(30);auto ha=hub_list(ip_a,ip_b,22),hb=hub_list(ip_b,ip_a,11);
    fb.state.p[0]=1705.3f;fb.state.p[1]=-883.6f;fb.state.p[2]=24.1f;
    std::fill_n(fa.state.v,3,0.f);fa.state.speed=0;
    // At spawn, near its boundary, clear of it, back inside, and above it.
    const float offsets[][3]={{0,0,0},{4,0,0},{6,0,0},{0,0,0},{0,0,6}};
    for(int phase=0;phase<5;++phase){
        fa.state.p[0]=1695.3f+offsets[phase][0];fa.state.p[1]=-883.6f+offsets[phase][1];fa.state.p[2]=24.1f+offsets[phase][2];
        int64_t now=0;
        for(int i=0;i<60;++i){
            now=1000000+(phase*60+i)*50000ll;
            if(i%10==0){CHECK(a.receive(ha.data(),ha.size(),9,now));CHECK(b.receive(hb.data(),hb.size(),9,now));}
            a.frame(fa,false,now);b.frame(fb,false,now);
            for(int k=0;k<8;++k){bool x=route(a,b,now),y=route(b,a,now);if(!x && !y)break;CHECK(k<7);}
            rc_pose displayed{};b.pose(0,now,50000,displayed);
        }
        auto peer=b.peer(0);CHECK(peer && !peer->lost && peer->mo.n && peer->cfg_len && peer->status==RC_ST_ROAM && now-peer->last_snap_rx<=50000);
        rc_pose pose{};CHECK(b.pose(0,now,0,pose)==(phase==2 || phase==4));
        CHECK(b.peers()==1 && b.listed_peer(0));
    }
}
int main() {
    coop_endpoint();
    visibility_grace();
    career_start_visibility();
    RoamLink a,b;
    CHECK(!a.configure("","",1,4));
    CHECK(!a.configure("Test_A","",0,4)); CHECK(!a.configure("Test_A","",1,65));
    CHECK(a.configure("Test_A","VR_TEST",11,4) && b.configure("Test_B","VR_TEST",22,4));
    CHECK(!a.bind(hub_ip,9) && !a.bind(ip_a,0)); CHECK(a.bind(ip_a,9) && b.bind(ip_b,9));
    CHECK(a.diagnostics(1000000).hub==0 && !a.diagnostics(1000000).hub_lists);
    auto fa=car(15),fb=car(45);
    auto ha=hub_list(ip_a,ip_b,22),hb=hub_list(ip_b,ip_a,11);
    for(int i=0;i<140;i++) {
        int64_t now=1000000+i*50000;
        if(i%10==0) { CHECK(a.receive(ha.data(),ha.size(),9,now)); CHECK(b.receive(hb.data(),hb.size(),9,now)); }
        fa.state.p[0]=15.f+i*0.5f; fb.state.p[0]=45.f+i*0.5f;
        a.frame(fa,false,now); b.frame(fb,false,now);
        for(int k=0;k<8;k++) { bool x=route(a,b,now),y=route(b,a,now); if(!x && !y) break; CHECK(k<7); }
    }
    CHECK(a.peers()==1 && b.peers()==1 && snapshots>100 && configs>=4 && hellos>2);
    auto diag=a.diagnostics(7950000);
    CHECK(diag.hub==1 && diag.hub_lists==14 && diag.listed==1 && diag.configured==1 && diag.moving==1 && !diag.bad);
    CHECK(a.diagnostics(18000000).hub==2);
    const rc_peer *pa=a.peer(0),*pb=b.peer(0); CHECK(pa && pb && pa->id==22 && pb->id==11);
    CHECK(pb->cfg_len==fa.config_size && !std::memcmp(pb->cfg,fa.config,fa.config_size));
    CHECK(pa->cfg_len==fb.config_size && !std::memcmp(pa->cfg,fb.config,fb.config_size));
    rc_pose p{}; CHECK(b.pose(0,7950000,50000,p) && std::isfinite(p.p[0]) && p.gear==2);
    CHECK(!b.pose(0,11000000,50000,p));
    // A hub direction change keeps only already received pose data, bounded
    // by the ordinary two-second timeout. TX-only SNAPs remain rejected by
    // the unchanged core policy, and the peer is never a new-slot candidate.
    auto tx=hub_list(ip_b,ip_a,11,roam_port,RC_MARK_TXONLY);
    CHECK(b.receive(tx.data(),tx.size(),9,7975000));
    CHECK(b.peer(0) && !b.listed_peer(0) && b.pose(0,7975000,25000,p));
    auto last=b.peer(0)->last_snap_rx;auto policy=b.diagnostics(7975000).policy;
    a.frame(fa,false,8050000);route(a,b,8050000);
    CHECK(b.peer(0)->last_snap_rx==last && b.diagnostics(8050000).policy>policy);
    CHECK(!b.pose(0,last+2000001,50000,p));
    CHECK(b.receive(hb.data(),hb.size(),9,8050001));
    CHECK(!a.receive(ha.data(),ha.size(),8,8000000));
    CHECK(!a.receive(hb.data(),hb.size(),9,8000000));
    auto wrong=ha; wrong[14]=1; CHECK(!a.receive(wrong.data(),wrong.size(),9,8000000));
    wrong=ha; wrong[0]=3; CHECK(!a.receive(wrong.data(),wrong.size(),9,8000000));
    CHECK(!a.receive(ha.data(),15,9,8000000));
    fa.state.throttle=1;fa.state.brake=.5f;
    a.frame(fa,true,8100000); route(a,b,8100000); route(b,a,8100000);
    pb=b.peer(0); CHECK(pb && pb->status==RC_ST_PAUSED && pb->mo.n);
    bool saw_paused=false;
    for(int i=0;i<pb->mo.n;i++) if(pb->mo.s[i].t_us==8100000) {
        saw_paused=true; CHECK(pb->mo.s[i].speed==0 && pb->mo.s[i].v[0]==0 && pb->mo.s[i].rpm==0 && pb->mo.s[i].throttle==0 && pb->mo.s[i].brake==0);
    }
    CHECK(saw_paused);
    CHECK(b.pose(0,8100000,50000,p) && p.speed==0 && p.rpm==0 && p.v[0]==0 && p.throttle==0);
    fa.status=XYZ_XBOX_FRONTEND; a.frame(fa,false,8150000); route(a,b,8150000); route(b,a,8150000);
    CHECK(!b.pose(0,8150000,50000,p));
    /* Disconnect invalidates every peer, outgoing datagram and old socket
     * generation even when the service later assigns the same virtual IP. */
    a.unbind(); CHECK(!a.peers() && !a.peer(0));
    CHECK(!a.diagnostics(18000000).hub_lists && !a.diagnostics(18000000).listed);
    std::vector<uint8_t> wire; CHECK(!a.take(wire)); CHECK(a.bind(ip_a,10));
    CHECK(!a.receive(ha.data(),ha.size(),9,9000000) && !a.peers());
    CHECK(a.receive(ha.data(),ha.size(),10,9000000));
    /* Queue is bounded when its transport stalls; discovery cannot escape
     * the service's virtual network. */
    fa.status=XYZ_XBOX_READY;
    for(int i=0;i<1000;i++) a.frame(fa,false,10000000+i*1000000ll);
    CHECK(a.dropped()>0); unsigned queued=0; while(a.take(wire)) ++queued; CHECK(queued==512);
    b.leave(); CHECK(!b.peers()); unsigned byes=0;
    while(b.take(wire)) { Datagram d; CHECK(decode_datagram(wire.data(),wire.size(),d)); rc_msg m{};
        CHECK(rc_decode(d.payload.data(),(int)d.payload.size(),&m)==RC_OK); if(m.h.type==RC_MSG_BYE) ++byes; }
    CHECK(byes>=3);
    std::puts("Roam link: hub routing, fragmented cars, motion, pause, stale generations and bounded queues passed");
}
