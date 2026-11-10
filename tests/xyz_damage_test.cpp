/* The real port runs against a bounded synthetic guest image. Exercise the
 * Xbox 32-byte format, private mesh ownership, malformed input and retirement. */
#include "../src/community/xyz_damage.cpp"
#include "../src/community/xyz_damage_arena.h"
#include "../src/community/xyz_roam_link.h"
#include <array>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
namespace {
std::vector<uint8_t> ram(48u<<20);
uint32_t backingNext=8u<<20;
void *mapTest(uint32_t a){return a<ram.size()?ram.data()+a:nullptr;}
uint32_t addressTest(const void *p){return p?uint32_t(static_cast<const uint8_t*>(p)-ram.data()):0;}
int validTest(uint32_t a,uint32_t n){return a>=0x10000 && uint64_t(a)+n<=ram.size();}
uint32_t reserveTest(uint32_t n,uint32_t align){auto a=(backingNext+align-1)&~(align-1);if(uint64_t(a)+n>ram.size())return 0;backingNext=a+n;return a;}
uint32_t allocTest(uint32_t n){uint32_t a=xyz_damage_arena_alloc(n);if(a)memset(mapTest(a),0,n);return a;}
uint32_t callTest(uint32_t fn,uint32_t,int,const uint32_t*,float *f){if(f)*f=0;return fn==0xA0B00?0x130000:0;}
void word(uint32_t a,uint32_t v){memcpy(mapTest(a),&v,4);}
}
static int networkDamage(){
    using namespace xyz;
    constexpr uint32_t ipA=0x0a4d0102,ipB=0x0a4d0103,player=0x150000,car=0x151000,ri=0x110000;
    word(0x44940c,player);word(player+4,car);word(car+0x554,ri);
    Dent dent;dent.point={1.8f,.1f,.6f};dent.direction={-1,0,0};dent.depth=.12f;
    NetRecord(G(ri),dent,30);g_mine.lastDent=NowMs()-500;g_mine.lastPub=NowMs()-2000;NetFrame();
    uint8_t epoch,record[RC_DMG_MAX*RC_DMG_BYTES];uint16_t first;
    CHECK(dmg_net_mine(&epoch,&first,record,RC_DMG_MAX)==1 && first==0);
    Dent decoded;float magnitude;CHECK(NetDecode(record,decoded,magnitude,{-4,-2,-2},{4,2,3}));
    uint8_t bad[RC_DMG_BYTES]{};CHECK(!NetDecode(bad,decoded,magnitude,{-4,-2,-2},{4,2,3}));
    RoamLink a;CHECK(a.configure("DamageVR","",11,6) && a.bind(ipA,1));
    auto b=std::make_unique<rc_session>();std::deque<std::vector<uint8_t>> queue;
    rc_config cfg{};cfg.self_id=22;cfg.port=roam_port;cfg.max_players=7;cfg.links=6;cfg.hub_ip=hub_ip;cfg.hub_port=roam_port;cfg.dents=1;
    std::strcpy(cfg.name,"DamageBrowser");rc_io io{};io.user=&queue;
    io.send=[](void *ctx,uint32_t ip,uint16_t port,const uint8_t *bytes,int n){
        Datagram d;d.source=0x0a4d0103;d.destination=ip;d.destination_port=port;d.payload.assign(bytes,bytes+n);
        static_cast<std::deque<std::vector<uint8_t>>*>(ctx)->push_back(encode_datagram(d));
    };
    rc_session_init(b.get(),&cfg,&io);
    XyzXboxFrame local{};local.status=XYZ_XBOX_READY;local.car=car;local.world=0x152000;local.state.q[3]=1;local.state.p[0]=100;
    rc_carcfg look{};look.type_hash=0x12345678;std::strcpy(look.type_name,"TESTCAR");
    local.config_size=(uint16_t)rc_carcfg_encode(&look,local.config,sizeof local.config);CHECK(local.config_size>0);
    rc_set_cfg(b.get(),local.config,local.config_size,1000000);rc_set_status(b.get(),RC_ST_ROAM);
    unsigned announced=0,packets=0;
    for(int phase=0;phase<2;phase++){
        if(phase){NetReset(G(ri));g_mine.lastDent=NowMs()-500;g_mine.lastPub=NowMs()-2000;NetFrame();}
        rc_set_dents(b.get(),uint8_t(7+phase),0,phase?0:1,record,1000000+phase*5000000ll);
        for(int i=0;i<100;i++){
            int64_t now=1000000+(phase*100+i)*50000ll;
            if(i%10==0)for(int side=0;side<2;side++){
                rc_msg m{};m.h.type=RC_MSG_PEERS;m.h.sender=UINT32_MAX;m.u.peers.n=1;
                m.u.peers.e[0].ip=side?ipA:ipB;m.u.peers.e[0].id=side?11:22;m.u.peers.e[0].port=roam_port;
                uint8_t bytes[RC_MAX_DGRAM];int n=rc_encode(&m,bytes,sizeof bytes);CHECK(n>0);
                if(side)rc_receive(b.get(),hub_ip,roam_port,bytes,n,now);
                else {Datagram d;d.source=hub_ip;d.destination=ipA;d.payload.assign(bytes,bytes+n);auto wire=encode_datagram(d);CHECK(a.receive(wire.data(),wire.size(),1,now));}
            }
            a.frame(local,false,now);rc_state remote=local.state;remote.p[0]=110;remote.t_us=(uint32_t)now;rc_push_state(b.get(),&remote,now);rc_tick(b.get(),now);
            std::vector<uint8_t> wire;
            while(a.take(wire)){
                Datagram d;CHECK(decode_datagram(wire.data(),wire.size(),d));rc_msg m{};CHECK(rc_decode(d.payload.data(),int(d.payload.size()),&m)==RC_OK);
                if(m.h.type==RC_MSG_HELLO && d.destination==ipB){CHECK(m.h.flags&RC_HF_DENTS);announced++;}
                if(m.h.type==RC_MSG_DENTS)packets++;
                if(d.destination==ipB)rc_receive(b.get(),d.source,d.source_port,d.payload.data(),int(d.payload.size()),now);
            }
            while(!queue.empty()){
                wire=std::move(queue.front());queue.pop_front();Datagram d;CHECK(decode_datagram(wire.data(),wire.size(),d));
                if(d.destination==ipA)CHECK(a.receive(wire.data(),wire.size(),1,now));
            }
        }
        const rc_peer *remote=a.peer(0),*received=nullptr;
        for(const auto& p:b->peer)if(p.id==11 && p.state!=RC_PS_FREE)received=&p;
        CHECK(remote && received && remote->dents_epoch==7+phase && remote->dents_n==(phase?0:1));
        CHECK(received->dents_epoch==uint8_t(epoch+phase) && received->dents_n==(phase?0:1));
        if(!phase){CHECK(!std::memcmp(remote->dent,record,RC_DMG_BYTES));CHECK(!std::memcmp(received->dent,record,RC_DMG_BYTES));}
    }
    CHECK(announced && packets);word(0x44940c,0);
    return 0;
}
int main(){
    xyz_damage_arena_init(reserveTest);
    XyzDamageHost host{mapTest,addressTest,validTest,allocTest,xyz_damage_arena_free,xyz_damage_arena_available,callTest,nullptr};
    CHECK(xyz_damage_start(&host,0));
    constexpr uint32_t model=0x100000,solid=0x101000,mesh=0x102000,verts=0x103000,strips=0x104000,indices=0x105000,ri=0x110000;
    word(model+12,solid);word(solid,mesh);word(solid+0x2c,0x106000);word(solid+0x3c,0x107000);
    ram[solid+0x19]=1;ram[solid+0x1a]=1;
    word(0x106000,0x12345);word(0x106004,0x140000);word(0x107000,0x23456);word(0x107004,0x130000);
    word(mesh,0x12340001);At<uint16_t>(G(mesh),4)=0x4180;At<uint16_t>(G(mesh),6)=3;
    At<uint16_t>(G(mesh),8)=1;At<uint16_t>(G(mesh),10)=32;
    word(mesh+12,strips);word(mesh+16,indices);word(mesh+20,6);word(mesh+24,verts);
    XboxVertex v[3]={{{0,0,0.5f},{0.1f,0.2f},{1,0,0}},{{0,0.1f,0.5f},{0.3f,0.4f},{1,0,0}},{{0,0,0.6f},{0.5f,0.6f},{1,0,0}}};
    memcpy(G(verts),v,sizeof v);uint16_t tri[3]={0,1,2};memcpy(G(indices),tri,6);
    word(strips+0x18,3);word(strips+0x20,0x14180);word(strips+0x24,0);
    ram[strips+0x1d]=0xA5;ram[strips+0x2d]=0xB6;
    word(ri+0x17a8+5*32,model);
    const auto originalVertices=std::vector<uint8_t>(G(verts),G(verts)+sizeof v);
    const auto originalSolid=std::vector<uint8_t>(G(solid),G(solid)+kSolidSize);
    TessMesh t;CHECK(ReadSource(G(model),t));CHECK(t.verts.size()==3 && t.strips[0].size()==3);
    CHECK(t.verts[0].uv[0]==0.1f && t.verts[0].nrm[0]==1 && t.verts[0].colour==UINT32_MAX);
    word(strips+0x24,3);CHECK(!ReadSource(G(model),t));word(strips+0x24,0);
    At<uint16_t>(G(indices),0)=3;CHECK(!ReadSource(G(model),t));At<uint16_t>(G(indices),0)=0;
    At<uint16_t>(G(mesh),10)=36;CHECK(!ReadSource(G(model),t));At<uint16_t>(G(mesh),10)=32;
    const uint32_t before=xyz_damage_arena_available();
    Clone *c=MakeClone(G(model),G(ri),5,0,1);CHECK(c && c->count==3 && Live(c));
    CHECK(R32(A(c->mesh))==0x12340001 && At<uint16_t>(c->mesh,4)==0x4180);
    CHECK(At<uint16_t>(c->mesh,kMeshStride)==32 && At<uint16_t>(c->mesh,kMeshCount)==6);
    CHECK(R32(A(c->mesh)+kMeshVerts)==c->vb && R32(A(c->mesh)+kMeshVB+4)==c->vb);
    CHECK(At<uint32_t>(c->strips,kStripIndexPtr)==0 && c->strips[0x1d]==0xA5 && c->strips[0x2d]==0xB6);
    CHECK(!memcmp(G(c->vb),v,sizeof v));
    CarState state;Dent dent;dent.point={0,0,0.5f};dent.direction={-1,0,0};dent.depth=0.1f;
    CHECK(DentCar(G(ri),state,dent)>0);CHECK(Upload(c));
    CHECK(((XboxVertex*)G(c->vb))[0].pos[0]<-0.01f);
    CHECK(!memcmp(G(verts),originalVertices.data(),originalVertices.size()));
    CHECK(!memcmp(G(solid),originalSolid.data(),originalSolid.size()));
    // A scratch strip must keep the byte-sized material fields' neighbours.
    c->overlayTex[0]=1;c->scratchLm=1;c->scratch.assign(c->count,0.1f);
    RebuildOverlay(c);CHECK(c->drawStrips==2);
    CHECK(At<uint32_t>(c->strips+kStripSize,kStripIndexPtr)==3);
    CHECK(c->strips[kStripSize+0x1d]==0xA5 && c->strips[kStripSize+0x2d]==0xB6);
    Retire(c,true);CHECK(R32(model+12)==solid && g_clones.empty() && g_retired.size()==1);
    xyz_damage_frame(0);CHECK(g_retired.size()==1);
    xyz_damage_frame(0);CHECK(g_retired.empty() && xyz_damage_arena_available()==before);
    // Coalescing and unknown/double frees cannot corrupt the arena accounting.
    auto a=allocTest(1024),b=allocTest(2048);CHECK(a && b && a!=b);
    xyz_damage_arena_free(a+1);xyz_damage_arena_free(a);xyz_damage_arena_free(a);xyz_damage_arena_free(b);
    CHECK(xyz_damage_arena_available()==before);
    CHECK(!xyz_damage_arena_alloc((4u<<20)+1));
    auto big=allocTest(4u<<20);CHECK(big);xyz_damage_arena_free(big);
    CHECK(xyz_damage_arena_available()==before);
    CHECK(networkDamage()==0);
    puts("PASS Xbox damage: vertices, strips, private ownership, dents, scratches, retirement, arena and bidirectional damage/repair transport");
}
