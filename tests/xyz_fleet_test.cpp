#include "xyz_fleet.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <cstring>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace xyz;
// Resource scheduling and actual model repair have their own asset tests.
static int resources=1;
static int paint=1;
int xyz::Assets::prepare(const XyzXboxGuest&,uint64_t){return resources;}
void xyz::Assets::maintain(const XyzXboxGuest&){}
int xyz::Skins::prepare(const XyzXboxGuest&,unsigned skin){CHECK(skin>=7 && skin<=70);return paint;}
// Exercise scheduling/ownership with a controlled asynchronous loader. The
// separate clone tests execute the real primitives against bounded guest RAM.
static unsigned allocations,begins,creates,looks,moves,hides,retires,pending,look_pending;
static bool loaded,refuse_retire;
static uint32_t stream_phase,car_busy;static bool bad_read;
static int read_stream(void*,uint32_t address,void *out,size_t size){
    CHECK(size==4 && (address==0x441b48 || address==0x45c4b4));if(bad_read)return 0;
    uint32_t value=address==0x441b48?stream_phase:car_busy;std::memcpy(out,&value,4);return 1;
}
static std::set<uint32_t> live,skins;
static int call(void*,XyzXboxOp op,uint32_t,uint32_t,unsigned n,const uint32_t *a,uint32_t *out) {
    CHECK(op==XYZ_G_ALLOC && n==1 && a[0]==0x3000);*out=0x100000+allocations++*0x3000;return 1;
}
extern "C" int xyz_xbox_clone_begin(const XyzXboxGuest*,XyzXboxClone *s,XyzXboxArena a,uint32_t w,uint32_t,uint32_t skin,uint64_t now) {
    CHECK(s->stage==XYZ_CLONE_EMPTY && !pending && !look_pending);
    s->arena=a;s->world=w;s->skin=skin;s->stage=XYZ_CLONE_FAILED;
    if(skin<5)return 0;CHECK(!skins.count(skin));skins.insert(skin);
    s->stage=XYZ_CLONE_LOADING;s->started=now;++begins;++pending;return 1;
}
extern "C" int xyz_xbox_clone_begin_at(const XyzXboxGuest *g,XyzXboxClone *s,XyzXboxArena a,uint32_t w,uint32_t player,uint32_t skin,
                   const float *position,uint64_t now) {
    CHECK(position);
    return xyz_xbox_clone_begin(g,s,a,w,player,skin,now);
}
extern "C" XyzXboxCloneStage xyz_xbox_clone_poll(const XyzXboxGuest*,XyzXboxClone *s,uint64_t) {
    if(loaded && s->stage==XYZ_CLONE_LOADING){--pending;++creates;s->car=s->arena.base+0x900;s->stage=XYZ_CLONE_READY;live.insert(s->car);}
    if(loaded && s->stage==XYZ_CLONE_LOOK_LOADING){s->stage=XYZ_CLONE_READY;--look_pending;}
    return s->stage;
}
extern "C" int xyz_xbox_clone_configure(const XyzXboxGuest*,XyzXboxClone *s,const uint8_t*,int,uint64_t,XyzXboxRideResult *r) {
    CHECK(s->stage==XYZ_CLONE_READY && live.count(s->car) && !pending && !look_pending);
    ++looks;++look_pending;s->stage=XYZ_CLONE_LOOK_LOADING;*r={};return 1;
}
extern "C" int xyz_xbox_clone_pose(const XyzXboxGuest*,XyzXboxClone *s,const rc_pose*,float dt) {
    CHECK((s->stage==XYZ_CLONE_READY || s->stage==XYZ_CLONE_LOOK_LOADING) && live.count(s->car) && dt>=0 && dt<=.100001f);++moves;s->visible=1;return 1;
}
extern "C" int xyz_xbox_clone_hide(const XyzXboxGuest*,XyzXboxClone *s){CHECK(live.count(s->car));++hides;s->visible=0;return 1;}
extern "C" int xyz_xbox_clone_retire(const XyzXboxGuest*,XyzXboxClone *s) {
    if(s->stage==XYZ_CLONE_EMPTY)return 1;
    if(refuse_retire)return 0;
    if(s->stage==XYZ_CLONE_LOADING)--pending;
    if(s->stage==XYZ_CLONE_LOOK_LOADING)--look_pending;
    if(s->car){CHECK(live.erase(s->car)==1);++retires;}
    skins.erase(s->skin);*s={};return 1;
}
int main() {
    Fleet fleet(2);XyzXboxGuest g{};g.call=call;g.read=read_stream;
    XyzXboxFrame local{};local.status=XYZ_XBOX_READY;local.world=0x200000;local.car=0x210000;
    ClientFrame remote;remote.generation=1;
    RemotePlayer a;a.id=11;a.ip=0x0a4d0102;a.config={1,2,3};a.has_pose=true;
    RemotePlayer b=a;b.id=22;b.ip++;
    remote.players={a,b};uint64_t now=1000000;
    auto step=[&]{now+=50000;return fleet.frame(g,local,remote,now);};
    // City streaming, the original car loader and unreadable state each
    // defer new peer requests without touching resource packs or arenas.
    for(stream_phase=1;stream_phase<=7;++stream_phase)CHECK(step().loading==1 && !begins && !allocations);
    stream_phase=0;car_busy=1;CHECK(step().loading==1 && !begins && !allocations);
    car_busy=0;bad_read=true;CHECK(step().loading==1 && !begins && !allocations);bad_read=false;
    resources=0;CHECK(step().loading==1 && !begins && !allocations);
    resources=-1;CHECK(step().failed==1 && !begins && !allocations);
    resources=1;
    paint=0;CHECK(step().loading==1 && !begins && !allocations);paint=1;
    CHECK(step().loading==1 && begins==1 && pending==1 && allocations==1);
    for(int i=0;i<10;i++)step();CHECK(begins==1 && !creates);
    stream_phase=2;loaded=true;step();CHECK(creates==1 && begins==1 && !pending && !looks);
    for(int i=0;i<5;++i)step();CHECK(!looks && begins==1);stream_phase=0;
    step();CHECK(looks==1 && look_pending==1 && begins==1 && !moves);
    loaded=false;for(int i=0;i<10;++i)CHECK(step().visible==0);
    CHECK(look_pending==1 && begins==1 && !moves);
    loaded=true;for(int i=0;i<12;++i)step();
    CHECK(step().visible==2 && creates==2 && !pending && !look_pending && allocations==2 && moves>=2 && skins.size()==2);
    remote.players[0].pose.hibeam=1;remote.players[1].pose.bits=RC_BIT_DARK;
    CHECK(step().visible==2 && fleet.lights(0x100900)==2 && fleet.lights(0x103900)==1 && fleet.lights(0x765432)==0);
    remote.players[0].pose.hibeam=0;remote.players[1].pose.bits=0;
    CHECK(step().visible==2 && fleet.lights(0x100900)==0 && fleet.lights(0x103900)==0);
    unsigned oldlooks=looks;for(int i=0;i<5;i++)CHECK(step().visible==2);CHECK(looks==oldlooks);
    remote.players[0].config={4,5};loaded=false;
    stream_phase=4;CHECK(step().loading==1 && hides==0 && looks==oldlooks);stream_phase=0;
    resources=0;CHECK(step().loading==1 && hides==0 && looks==oldlooks);
    resources=-1;CHECK(step().failed==1 && hides==0 && looks==oldlooks);
    resources=1;
    CHECK(step().loading==1 && hides==0 && looks==oldlooks+1);
    for(int i=0;i<3;i++)CHECK(step().visible==2);CHECK(looks==oldlooks+1);
    loaded=true;CHECK(step().visible==2);
    remote.players[0].has_pose=false;CHECK(step().visible==1 && hides==1);
    CHECK(step().visible==1 && hides==1); // hidden once, no repeated guest calls
    remote.players[0].has_pose=true;CHECK(step().visible==2);
    remote.players[0].listed=false;unsigned previous_begins=begins;
    for(int i=0;i<10;i++)CHECK(step().visible==2);
    CHECK(!retires && begins==previous_begins);remote.players[0].listed=true;
    // Disconnected/reconnected with the same virtual addresses still destroys
    // both old cars. Their arenas are reused only after successful retirement.
    ++remote.generation;loaded=false;step();CHECK(retires==2 && live.empty() && pending==1 && allocations==2);
    remote.players.clear();step();CHECK(!pending && live.empty());
    remote.players={a,b};loaded=true;for(int i=0;i<8;i++)step();CHECK(live.size()==2);
    // Corrupt ownership/loader state cannot release its arena or start more cars.
    refuse_retire=true;remote.players.clear();auto failed=step();CHECK(failed.failed==2 && live.size()==2);
    unsigned wasbegins=begins;remote.players={a,b};step();CHECK(begins==wasbegins);
    refuse_retire=false;for(int i=0;i<8;i++)step();CHECK(live.size()==2 && allocations==2);
    local.status=XYZ_XBOX_OTHER_RACE;step();CHECK(live.empty() && !pending);
    local.status=XYZ_XBOX_READY;for(int i=0;i<8;i++)step();CHECK(live.size()==2);
    local.world+=0x10000;step();CHECK(live.empty() && pending==1 && allocations==2);
    remote.generation=0;step();CHECK(live.empty() && !pending && skins.empty());
    // Extra network candidates do not churn the two rendered slots at a small
    // distance crossover. A much nearer listed peer does replace the farthest.
    remote.generation=5;a.pose.p[0]=100;b.pose.p[0]=120;remote.players={a,b};
    for(int i=0;i<8;i++)step();CHECK(live.size()==2);
    RemotePlayer c=a;c.id=33;c.ip+=5;c.pose.p[0]=110;
    previous_begins=begins;remote.players.push_back(c);
    for(int i=0;i<8;i++)CHECK(step().visible==2);
    CHECK(begins==previous_begins);
    remote.players[2].pose.p[0]=10;remote.players[2].listed=false;
    for(int i=0;i<8;i++)CHECK(step().visible==2);CHECK(begins==previous_begins);
    remote.players[2].listed=true;
    // A much closer newcomer cannot remove an already visible nearby car.
    for(int i=0;i<120;i++)CHECK(step().visible==2);
    CHECK(begins==previous_begins);
    remote.players[1].pose.p[0]=400;
    step();CHECK(begins==previous_begins+1 && pending==1);
    for(int i=0;i<8;i++)step();CHECK(live.size()==2 && begins==previous_begins+1);
    // Newly shown cars get a minimum lifetime even outside the nearby zone.
    remote.players[2].pose.p[0]=400;
    RemotePlayer d=a;d.id=44;d.ip+=6;d.pose.p[0]=10;remote.players.push_back(d);
    for(int i=0;i<40;i++)CHECK(step().visible==2);
    CHECK(begins==previous_begins+1);
    for(int i=0;i<100;i++)step();CHECK(live.size()==2 && begins==previous_begins+2);
    remote.generation=0;step();CHECK(live.empty() && !pending && skins.empty());
    // All 64 incoming peers coexist with independent skins and arenas. A
    // complete equipment update remains serialized without hiding any car.
    Fleet full(64);remote.generation=6;remote.players.clear();
    for(unsigned i=0;i<64;++i){auto peer=a;peer.id=100+i;peer.ip+=i;remote.players.push_back(peer);}
    auto full_step=[&]{now+=50000;return full.frame(g,local,remote,now);};
    unsigned base_allocations=allocations,base_creates=creates,base_hides=hides;
    for(int i=0;i<400;++i)CHECK(!full_step().failed);
    CHECK(full_step().visible==64 && live.size()==64 && skins.size()==64 && *skins.begin()==7 && *skins.rbegin()==70);
    CHECK(allocations==base_allocations+64 && creates==base_creates+64 && !pending && !look_pending);
    for(auto &peer:remote.players)peer.config={9,8,7};
    unsigned base_looks=looks;
    for(int i=0;i<400;++i){auto result=full_step();CHECK(!result.failed && result.visible==64);}
    CHECK(looks==base_looks+64 && hides==base_hides && !pending && !look_pending);
    full.set_limit(6);CHECK(full_step().visible==6 && live.size()==6 && skins.size()==6);
    full.set_limit(64);for(int i=0;i<400;++i)CHECK(!full_step().failed);
    CHECK(full_step().visible==64 && allocations==base_allocations+64); // reuse every owned arena
    remote.players.erase(remote.players.begin()+2);CHECK(full_step().visible==63 && live.size()==63);
    remote.generation=0;full_step();CHECK(live.empty() && skins.empty() && !pending && !look_pending);
    std::puts("Fleet: 64 cars, live 64-to-6-to-64 setting, stable equipment, world changes, bounded arenas and failed retirement passed");
}
