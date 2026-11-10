#include "copchase_game.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
constexpr uint32_t W=0x600000,P=0x605000,C=0x610000,R=0x700000,D=0x800000,
    CT=0x900000,PR=0xa00000,T=0xb00000,S=0xd00000;
constexpr uint32_t stride=0x1000;
struct Write {uint32_t address,size;};
struct Native {uint32_t function,self,argument;};
struct Fixture {
    std::vector<uint8_t> mem=std::vector<uint8_t>(0xf00000);
    std::vector<Write> writes;std::vector<Native> calls;
    CopChaseGameGuards guard{1,1,1,1,1,1,0,1};
    CopChaseGame *game=nullptr;CopChaseGameConfig cfg=copchase_game_defaults();
    unsigned cops=12;bool obstruction=false,missing=false,missing_midpoint=false,fail_ctor=false,fail_ray=false;
    bool fail_physics=false,bad_release=false,fail_release=false;unsigned allocations=0,frees=0,ctors=0,dtors=0,rays=0;
    bool road_mode=false,fail_batch=false;bool staged[22]{};unsigned batches=0;
    bool model_mode=false,model_loaded=true,fail_setride=false;unsigned model_batches=0;
    template<typename V> V get(uint32_t a){assert(a+sizeof(V)<mem.size());V v;std::memcpy(&v,mem.data()+a,sizeof v);return v;}
    template<typename V> void put(uint32_t a,const V &v){assert(a+sizeof(V)<mem.size());std::memcpy(mem.data()+a,&v,sizeof v);}
    static int read(void *p,uint32_t a,void *v,uint32_t n){auto &f=*(Fixture*)p;if(!a || uint64_t(a)+n>f.mem.size())return 0;
        std::memcpy(v,f.mem.data()+a,n);return 1;}
    static int write(void *p,uint32_t a,const void *v,uint32_t n){auto &f=*(Fixture*)p;
        if(!a || uint64_t(a)+n>f.mem.size())return 0;f.writes.push_back({a,n});std::memcpy(f.mem.data()+a,v,n);return 1;}
    static int guards(void *p,CopChaseGameGuards *g){*g=((Fixture*)p)->guard;return 1;}
    static int batch(void *p,const CopChaseRoadWrite *writes,unsigned count){
        auto &f=*(Fixture*)p;if(f.fail_batch)return 0;
        const bool words=(count==2 || count==4) && writes[0].size==4;if(words && !f.model_mode)return 0;
        for(unsigned i=0;i<count;++i)if(!writes[i].address || uint64_t(writes[i].address)+writes[i].size>f.mem.size() || writes[i].size!=(words?4u:12u))return 0;
        for(unsigned i=0;i<count;++i){write(p,writes[i].address,writes[i].bytes,writes[i].size);
            for(unsigned slot=1;slot<=f.cops;++slot)if(writes[i].address==0xc00000+slot*stride+0x420)f.staged[slot]=true;}
        if(words)++f.model_batches;else ++f.batches;return 1;
    }
    static uint32_t alloc(void *p,uint32_t n){auto &f=*(Fixture*)p;assert(n==0x470);++f.allocations;return S;}
    static void free(void *p,uint32_t a){auto &f=*(Fixture*)p;assert(a==S);++f.frees;}
    static int call(void *p,uint32_t fn,uint32_t self,unsigned n,const uint32_t *a,uint32_t *eax){
        auto &f=*(Fixture*)p;f.calls.push_back({fn,self,n?a[0]:0});*eax=0;
        if(fn==0x37550){assert(n==1 && self>=D && self<D+22*stride && a[0]==C);
            unsigned i=(self-D)/stride;f.put(self+0x6fc,PR+i*stride);f.put(PR+i*stride+0x14,5u);f.put(PR+i*stride+0x18,a[0]);return 1;}
        if(fn==0x39b00){assert(n==1 && a[0]==7 && self>=D && self<D+22*stride);
            if(f.fail_release)return 0;
            unsigned i=(self-D)/stride;f.put(PR+i*stride+0x14,f.bad_release?8u:7u);return 1;}
        if(fn==0x1e47d0){assert(n==1 && !self && a[0]>=C && a[0]<C+22*stride);*eax=2;return 1;}
        if(fn==0x1e4a70){assert(n==1 && (a[0]==1 || a[0]==2));if(f.fail_physics && a[0]==1)return 0;
            f.put(self+0x28,a[0]);if(a[0]==2 && self>=C && self<C+22*stride)f.staged[(self-C)/stride]=false;return 1;}
        if(fn==0x1d13f0){assert(self==S && !n);++f.ctors;return !f.fail_ctor;}
        if(fn==0x1d14a0){assert(self==S && !n);++f.dtors;return 1;}
        if(fn==0x1d6e70){assert(self==S && n==2 && a[1]==0);float x=f.get<float>(a[0]);
            bool unavailable=f.missing || (f.missing_midpoint && std::fabs(x-(f.get<float>(C+0x50)-5.f))<.1f);
            f.put(S+8,unavailable?0u:0xe00000u);return 1;}
        if(fn==0x1d8440){assert(self==S && n==5 && a[4]==1);++f.rays;*eax=f.obstruction?1:0;return !f.fail_ray;}
        if(fn==0x1ffda0){assert(f.model_mode && self==0x45c4a0 && n==1 && a[0]>=0xe20000);*eax=f.model_loaded;return 1;}
        if(fn==0x207e00){assert(f.model_mode && self==0x45c4a0 && n==1 && a[0]>=100 && a[0]<108 && f.model_loaded);return 1;}
        if(fn==0x1f10c0){assert(f.model_mode && n==1 && self>C && self<C+22*stride && a[0]>=0xd10000 && a[0]<0xd18000);
            if(f.fail_setride)return 0;const auto rec=f.get<uint32_t>(self+0x14);std::memcpy(f.mem.data()+rec+0x10,f.mem.data()+a[0],0x8f0);
            f.put(rec+0x908,f.get<uint32_t>(a[0]));f.put(self+0xc,T+f.get<uint32_t>(a[0])*0x890);return 1;}
        assert(false);return 0;
    }
    explicit Fixture(unsigned n=12):cops(n){
        put(0x4493e0,W);put(0x44940c,P);put(P+4,C);put(0x428e34,6u);put(0x41c338,1u);
        put(0x3cc470,0u);put(0x406960,0u);put(0x456290,0u);put(0x45af88,T);
        std::memcpy(mem.data()+T,"4DR_SEDAN",10);put(T+0x844,2u);
        put(W,C);put(W+4,C+cops*stride);put(W+0x10,cops+1);put(W+0x18,1u);
        for(unsigned i=0;i<=cops;++i){uint32_t car=C+i*stride;
            put(car,i==cops?W:car+stride);put(car+4,i?car-stride:W);put(car+8,int16_t(i));put(car+0xc,T);
            put(car+0x14,R+i*stride);put(R+i*stride+4,i?3u:1u);put(car+0x24,i?4u:1u);put(car+0x28,i?2u:1u);
            put(car+0x2c,D+i*stride);put(car+0x30,CT+i*stride);put(W+0x1c+i*4,car);
            put(D+i*stride,0x341a70u);put(D+i*stride+4,car);put(D+i*stride+8,CT+i*stride);
            put(D+i*stride+0x6fc,PR+i*stride);put(PR+i*stride+0x14,7u);
            put(D+i*stride+0x734,42u);put(D+i*stride+0x79c,24.f);
            put(CT+i*stride,0x3414e8u);put(CT+i*stride+0x220,car);put(CT+i*stride+0x230,12.f);
            put(CT+i*stride+0x234,4.f);put(CT+i*stride+0x238,5.f);put(CT+i*stride+0x23c,uint8_t(1));
            put(car+0x50,100.f-float(i)*10);put(car+0x54,-200.f);put(car+0x58,5.f);
            put(car+0x70,40.f);put(car+0x74,0.f);put(car+0x90,1.f);put(car+0x94,0.f);put(car+0xa0,0.f);put(car+0xa4,1.f);
            put(car+0xb8,1.f);
            put(car+0x430,40.f);put(car+0x550,uint8_t(1));put(car+0x998,uint8_t(0));
        }
        CopChaseGameHost host{this,read,write,call,alloc,free,guards};game=copchase_game_create(&host);assert(game);
    }
    ~Fixture(){copchase_game_world_ending(game);copchase_game_destroy(game);assert(frees==allocations);}
    void enable(int heat=1,int preset=1,int max=10){cfg.pursuit.enabled=1;cfg.pursuit.start_heat=heat;
        cfg.pursuit.style=preset;cfg.maximum_vehicles=max;copchase_game_configure(game,&cfg);}
    void tick(float dt=.1f,bool move=true){if(move && std::isfinite(dt))for(unsigned i=0;i<=cops;++i){
        if(road_mode && staged[i]){for(unsigned k=0;k<3;++k)put(C+i*stride+0x50+k*4,get<float>(0xc00000+i*stride+0x420+k*4));}
        else put(C+i*stride+0x50,get<float>(C+i*stride+0x50)+40.f*dt);}copchase_game_frame(game,dt);}
    void road_fixture(bool visible=true){
        copchase_game_world_ending(game);copchase_game_destroy(game);road_mode=true;
        CopChaseGameHost host{this,read,write,call,alloc,free,guards,batch};game=copchase_game_create(&host);assert(game);
        for(unsigned i=0;i<=cops;++i){const uint32_t car=C+i*stride,mover=0xc00000+i*stride,facade=mover+0x100,body=mover+0x400;
            put(car+0x34,mover);put(mover+0x20,body);put(car+0x3c,facade);put(facade,0x35c0f8u);put(facade+0x130,car);
            put(facade+0x68,body);put(body,0x35bd2cu);put(body+0x70,40.f);put(car+0xb8,1.f);
            if(i>1)put(car+0x50,100.f-200.f-float(i)*20);
            put(CT+i*stride+0x10,uint8_t(20));
            for(unsigned q=0;q<20;++q){put(CT+i*stride+0x20+q*16,float(q)*50);put(CT+i*stride+0x24+q*16,-200.f);put(CT+i*stride+0x28+q*16,5.f);}
        }
        for(unsigned i=0;i<4;++i){put(T+0x120+i*0x30,i<2?1.5f:-1.5f);put(T+0x124+i*0x30,i%2?.8f:-.8f);put(T+0x128+i*0x30,-.3f);}
        cfg.spikes_visible=visible;cfg.roadblock_chance_multiplier=10;cfg.random_seed=3;enable(5,COPCHASE_INSANE,1);
    }
    uint32_t physical(unsigned i){const uint32_t car=C+i*stride,mover=0xc00000+i*stride,facade=mover+0x100,body=mover+0x400;
        put(car+0x34,mover);put(mover+0x20,body);put(car+0x3c,facade);put(facade,0x35c0f8u);put(facade+0x130,car);
        put(facade+0x68,body);put(body,0x35bd2cu);put(body+0x70,40.f);put(car+0x28,1u);return facade;}
    void model_fixture(){
        assert(cops==8);copchase_game_world_ending(game);copchase_game_destroy(game);model_mode=true;
        CopChaseGameHost host{this,read,write,call,alloc,free,guards,batch};game=copchase_game_create(&host);assert(game);
        static const char *const names[]={"4DR_SEDAN","MINIVAN","COUPE","TAXI02","HATCHBACK","BUS","PICKUP","SUV"};
        constexpr uint32_t head=0x3db0d0,loader=0x45c4f8;
        put(head,0xd10000u);put(head+4,0xd17000u);put(loader,0xe20000u);put(loader+4,0xe20000u+7*0x1300);
        for(unsigned i=0;i<8;++i){const uint32_t type=T+i*0x890,r=0xd10000+i*stride,e=0xe20000+i*0x1300,c=C+(i+1)*stride;
            std::memcpy(mem.data()+type,names[i],std::strlen(names[i])+1);put(type+0x844,2u);put(c+0xc,type);
            put(c+0x10,R+(i+1)*stride+0x10);put(R+(i+1)*stride+0x10,i);put(R+(i+1)*stride+0x908,i);
            if(i)put(c+0x50,100.f-200.f-float(i)*20);
            put(r,i==7?head:r+stride);put(r+4,i?r-stride:head);put(r+0x10,i);put(r+0x8f8,100u+i);
            put(r+0x900,1000u);put(r+0x908,1u);put(r+0x90c,1u);put(r+0x910,1u);
            put(e,i==7?loader:e+0x1300);put(e+4,i?e-0x1300:loader);put(e+0x123c,100u+i);
        }
        cfg.heat_models=0;start(5,COPCHASE_MOST_WANTED,1);assert(status().active_cars==1);
        cfg.heat_models=1;copchase_game_configure(game,&cfg);calls.clear();writes.clear();
    }
    void pursuit_race(){
        constexpr uint32_t manager=0xe50000,director=0xe60000,row=0xe70000;
        guard.pursuit_race_token=123;guard.pursuit_race_world=W;guard.pursuit_race_player=C;
        guard.pursuit_race_manager=manager;guard.pursuit_race_track=4201;guard.pursuit_race_kind=1;
        guard.pursuit_race_start_heat=3;guard.pursuit_race_style=COPCHASE_MOST_WANTED;guard.pursuit_race_force_start=0;
        put(0x41c338,2u);put(0x406d74,1u);put(0x3cc460,4201u);put(0x3cc470,1u);
        put(0x449478,manager);put(manager,4u);put(manager+0x24,1u);put(manager+0xbd0,row);
        put(0x449438,director);put(director,4u);put(P+0x1c,row);
        cfg.pursuit.enabled=0;cfg.pursuit.style=COPCHASE_SLOW;cfg.retained_heat=5;copchase_game_configure(game,&cfg);
    }
    void run(unsigned n){for(unsigned i=0;i<n;++i)tick();}
    CopChaseGameStatus status(int take=0){CopChaseGameStatus out{};copchase_game_snapshot(game,&out,take);return out;}
    void start(int heat=1,int preset=1,int max=10){enable(heat,preset,max);run(45);assert(status().pursuit.phase==COPCHASE_PURSUIT);}
    void audit_writes(){for(auto w:writes){bool allowed=w.address>=S && w.address+w.size<=S+0x470;
        for(unsigned i=1;i<=cops;++i){uint32_t d=D+i*stride,c=CT+i*stride;
            allowed|=(w.address==d+0x734 || w.address==d+0x79c) && w.size==4;
            allowed|=w.address==c+0x224 && w.size==8;
            allowed|=(w.address==c+0x230 || w.address==c+0x234 || w.address==c+0x238) && w.size==4;
            allowed|=w.address==c+0x23c && w.size==1;
        }assert(allowed);}}
};
void disabled_and_defaults(){Fixture f;f.run(50);assert(!f.status().active_cars);assert(!f.status().pursuit.phase);
    assert(f.status().blocked==COPCHASE_GAME_DISABLED);assert(f.calls.empty() && f.writes.empty());
    assert(f.cfg.maximum_vehicles==10 && f.cfg.aggression_percent==100 && !f.cfg.pursuit.enabled);}
void selection_and_hooks(){Fixture f;f.start();auto s=f.status();assert(s.active_cars==4 && s.activations==4 && s.visible_cars==12);
    assert(s.pending_events&COPCHASE_STARTED);assert(f.status(1).pending_events&COPCHASE_STARTED);assert(!f.status().pending_events);
    uint32_t control=CT+stride;assert(copchase_game_driver(f.game,control));float x=f.get<float>(control+0x224);
    f.put(control+0x224,999.f);f.put(control+0x230,6.f);assert(copchase_game_heading(f.game,control));
    assert(f.get<float>(control+0x224)==x && f.get<float>(control+0x230)==6.f);
    assert(!copchase_game_driver(f.game,CT));assert(!copchase_game_heading(f.game,CT+12*stride));
    f.cfg.pursuit.enabled=0;copchase_game_configure(f.game,&f.cfg);f.tick();s=f.status();assert(!s.active_cars && s.releases==4);
    assert(s.pending_events&COPCHASE_CANCELLED);for(unsigned i=1;i<=4;++i){assert(f.get<uint32_t>(C+i*stride+0x28)==2);
        assert(f.get<uint32_t>(PR+i*stride+0x14)==7 && f.get<uint32_t>(D+i*stride+0x734)==42);
        assert(f.get<float>(CT+i*stride+0x230)==12.f);}f.audit_writes();assert(f.ctors==f.dtors);
}
void caps_and_presets(){for(int preset=0;preset<4;++preset){Fixture f;f.start(5,preset,99);
    unsigned expected=preset==0?3:preset==3?8:10;assert(f.status().active_cars==expected);
    for(unsigned i=1;i<=expected;++i)assert(copchase_game_driver(f.game,CT+i*stride));f.audit_writes();}
    Fixture f;f.start(5,1,1);assert(f.status().active_cars==1);f.cfg.maximum_vehicles=3;
    copchase_game_configure(f.game,&f.cfg);f.tick();assert(f.status().active_cars==3);
}
void model_heat_selection(){
    static const char *const names[]={"4DR_SEDAN","MINIVAN","COUPE","TAXI02","HATCHBACK","BUS","PICKUP","SUV"};
    static const int first_heat[]={0,2,3,3,4,5,3,4};
    for(int heat=1;heat<=5;++heat){Fixture f(8);unsigned expected=0;
        for(unsigned i=0;i<8;++i){const uint32_t type=T+i*0x890;std::memcpy(f.mem.data()+type,names[i],std::strlen(names[i])+1);
            f.put(type+0x844,2u);f.put(C+(i+1)*stride+0xc,type);expected+=heat>=first_heat[i];}
        f.start(heat,COPCHASE_MOST_WANTED);assert(f.status().registered_cars==8 && f.status().active_cars==expected);
        for(unsigned i=0;i<8;++i)assert((f.get<uint32_t>(PR+(i+1)*stride+0x14)==5)==(heat>=first_heat[i]));
        // Source excludes future classes from new pursuit assignments; all
        // indexed traffic remains untouched and can retain its road behavior.
        assert(f.get<uint32_t>(W+0x10)==9);f.audit_writes();}
}
void model_reassignment(){
    {Fixture f(8);f.model_fixture();const auto original_driver=f.get<uint32_t>(C+7*stride+0x2c);f.tick(.1f,false);auto s=f.status();
        assert(s.model_reassignments==2 && !s.failures && f.model_batches==4 && s.active_cars==1);
        assert(f.get<uint32_t>(C+7*stride+0xc)==T+4*0x890 && f.get<uint32_t>(C+2*stride+0xc)==T+4*0x890);
        assert(f.get<uint32_t>(0xd14000+0x908)==3 && f.get<uint32_t>(0xd14000+0x90c)==3 && f.get<uint32_t>(0xd14000+0x910)==3);
        assert(!f.get<uint32_t>(0xd16000+0x908) && !f.get<uint32_t>(0xd11000+0x910));
        assert(f.get<uint32_t>(C+7*stride+0x2c)==original_driver && f.get<uint32_t>(C+7*stride+0x28)==2 && f.get<uint32_t>(W+0x10)==9);
        for(unsigned i=0;i<5;++i)f.tick(.1f,false);assert(f.status().model_reassignments==2);}
    for(unsigned gate=0;gate<6;++gate){Fixture f(8);f.model_fixture();
        if(gate==0)f.guard.settlement_pending=1;if(gate==1)f.model_loaded=false;
        if(gate==2)f.put(0xd14000+4,0u);if(gate==3)f.put(0xe20000+4,0u);
        if(gate==4)for(unsigned i=1;i<=f.cops;++i)f.put(C+i*stride+0x50,f.get<float>(C+0x50)+float(i));
        if(gate==5)f.put(0xd16000+0x908,0u);
        f.tick(.1f,false);assert(!f.status().model_reassignments && !f.model_batches);
        for(auto n:f.calls)assert(n.function!=0x1f10c0);}
    {Fixture f(8);f.model_fixture();f.fail_setride=true;f.tick(.1f,false);
        assert(!f.status().model_reassignments && f.status().failures==1 && f.status().blocked==COPCHASE_GAME_FAULT);
        // Neither the original nor a possibly-partly-rebuilt target can be
        // evicted after an uncertain native failure; both refs stay pinned.
        assert(f.get<uint32_t>(0xd16000+0x908)==1 && f.get<uint32_t>(0xd14000+0x908)==2);
        unsigned before=0;for(auto n:f.calls)before+=n.function==0x1f10c0;f.run(3);unsigned after=0;
        for(auto n:f.calls)after+=n.function==0x1f10c0;assert(before==1 && after==1);}
}
void pauses_and_stream(){Fixture f;f.start();auto before=f.status();f.calls.clear();f.writes.clear();f.guard.paused=1;
    f.run(30);assert(f.status().pursuit.chase_seconds==before.pursuit.chase_seconds && f.status().active_cars==4);
    assert(!copchase_game_driver(f.game,CT+stride) && !copchase_game_heading(f.game,CT+stride));assert(f.calls.empty() && f.writes.empty());
    f.guard.paused=0;f.guard.streaming_idle=0;f.run(10);assert(f.status().pursuit.chase_seconds==before.pursuit.chase_seconds);
    f.guard.offline=0;f.tick();assert(f.status().release_pending && f.status().active_cars==4);
    assert(f.calls.empty() && f.writes.empty());f.guard.streaming_idle=1;f.tick();assert(!f.status().active_cars && f.status().releases==4);
}
void settlement_hold(){
    {Fixture f;f.start();const auto facade=f.physical(1);const float hit[3]={20,0,0};
        f.put(C+stride+0x50,f.get<float>(C+0x50)+3);assert(copchase_game_collision(f.game,facade,hit));
        f.tick(.05f,false);const auto before=f.status(1);assert(before.cops_damaged==1);
        assert(copchase_game_collision(f.game,facade,hit));f.calls.clear();f.writes.clear();f.guard.settlement_pending=1;
        // The guard is live even before the next main frame.
        assert(!copchase_game_driver(f.game,CT+stride) && !copchase_game_heading(f.game,CT+stride));
        assert(!copchase_game_collision(f.game,facade,hit));f.run(30);const auto held=f.status();
        assert(held.blocked==COPCHASE_GAME_SETTLEMENT && held.active_cars==before.active_cars && !held.release_pending);
        assert(held.pursuit.phase==before.pursuit.phase && held.pursuit.heat==before.pursuit.heat &&
            held.pursuit.chase_seconds==before.pursuit.chase_seconds && held.pursuit.bounty==before.pursuit.bounty);
        assert(held.cops_damaged==before.cops_damaged && held.cost_to_state==before.cost_to_state &&
            held.collision_events==before.collision_events && held.collision_dropped==before.collision_dropped+1);
        assert(!held.pending_events && f.calls.empty() && f.writes.empty());
        f.guard.settlement_pending=0;f.tick(.05f,false);assert(f.status().blocked==COPCHASE_GAME_READY &&
            f.status().pursuit.chase_seconds>before.pursuit.chase_seconds && f.status().active_cars==before.active_cars);
        f.guard.settlement_pending=1;f.guard.offline=0;f.tick(.05f,false);
        assert(f.status().blocked==COPCHASE_GAME_CONTEXT && !f.status().active_cars && f.status().releases==4);}
    {Fixture f;f.start();f.guard.settlement_pending=1;++f.guard.world_generation;f.calls.clear();f.writes.clear();f.tick();
        assert(!f.status().active_cars && f.status().abandoned==4 && f.calls.empty() && f.writes.empty());}
}
void owned_pursuit_race(){
    {Fixture f;f.pursuit_race();f.missing=true;f.run(45);assert(f.status().pursuit.phase==COPCHASE_IDLE);
        f.guard.pursuit_race_force_start=1;f.tick();auto s=f.status();
        assert(s.blocked==COPCHASE_GAME_READY && s.pursuit.phase==COPCHASE_PURSUIT && s.pursuit.heat==3 && s.active_cars>0);
        assert(s.pending_events&COPCHASE_STARTED);assert(!(s.pending_incident_events&COPCHASE_INCIDENT_RAM_TRIGGER));
        const auto active=s.active_cars;f.guard.paused=1;f.put(0xe60000,6u);f.calls.clear();f.writes.clear();f.run(10);
        assert(f.status().blocked==COPCHASE_GAME_PAUSED && f.status().active_cars==active && f.calls.empty() && f.writes.empty());
        f.guard.paused=0;f.put(0xe60000,4u);f.guard.pursuit_race_force_start=0;f.tick();
        f.guard.pursuit_race_token=124;assert(!copchase_game_driver(f.game,CT+stride));f.tick();
        assert(!f.status().active_cars && f.status().releases==active && (f.status().pending_events&COPCHASE_CANCELLED));}
    for(unsigned gate=0;gate<9;++gate){Fixture f;f.pursuit_race();f.guard.pursuit_race_force_start=1;
        if(gate==0)f.guard.pursuit_race_token=0;if(gate==1)f.put(0x406d74,0u);if(gate==2)f.put(0x3cc460,4202u);
        if(gate==3)f.put(0x3cc470,3u);if(gate==4)f.put(0x449478,0u);if(gate==5)f.put(0xe50000+0xbd0,0u);
        if(gate==6)f.guard.pursuit_race_world=W+0x100;if(gate==7)f.guard.assets_ready=0;if(gate==8)f.put(0xe60000,7u);
        f.run(50);assert(!f.status().active_cars && f.status().pursuit.phase==COPCHASE_IDLE && f.calls.empty() && f.writes.empty());}
}
void uncertain_and_blocked_los(){
    for(unsigned mode=0;mode<5;++mode){Fixture f;f.enable();if(mode==0)f.missing=true;if(mode==1)f.fail_ctor=true;
        if(mode==2)f.fail_ray=true;if(mode==3)f.obstruction=true;if(mode==4)f.missing_midpoint=true;
        f.run(60);assert(f.status().pursuit.phase==COPCHASE_IDLE && !f.status().active_cars);
        if(mode!=3)assert(f.status().unknown_cars>0);else assert(!f.status().visible_cars);
        if(mode!=1)assert(f.ctors==f.dtors);}
    Fixture f;f.start();f.missing=true;f.run(100);assert(f.status().pursuit.phase==COPCHASE_PURSUIT);
    assert(!f.status().pursuit.cooldown && f.status().unknown_cars==12);
}
void lifecycle(){
    {Fixture f;f.start();f.calls.clear();f.writes.clear();++f.guard.world_generation;
        f.tick();assert(f.status().abandoned==4 && !f.status().active_cars);
        for(auto c:f.calls)assert(c.function!=0x39b00 && c.function!=0x1e4a70);f.audit_writes();}
    {Fixture f;f.start();f.calls.clear();f.writes.clear();copchase_game_world_ending(f.game);
        assert(f.calls.empty() && f.writes.empty() && f.status().abandoned==4 && !f.status().active_cars);}
    {Fixture f;f.start();++f.guard.player_generation;f.tick();assert(f.status().releases==4 && !f.status().active_cars);}
    {Fixture f;f.start();f.put(P+4,0u);f.tick();assert(f.status().releases==4 && !f.status().active_cars);}
    {Fixture f;f.start();f.put(C+stride+0x30,CT+20*stride);f.calls.clear();f.writes.clear();f.tick();
        assert(f.status().abandoned==1);for(auto w:f.writes)assert(w.address!=D+stride+0x734 && w.address!=CT+stride+0x230);}
    {Fixture f;f.start();f.put(W+4,0u);f.calls.clear();f.writes.clear();f.tick();
        assert(f.calls.empty() && f.writes.empty() && !f.status().active_cars);}
    {Fixture f;f.start();f.calls.clear();f.writes.clear();copchase_game_car_ending(f.game,C+stride);
        assert(f.status().active_cars==3 && f.calls.empty() && f.writes.empty());
        assert(!copchase_game_driver(f.game,CT+stride));
        // A newly constructed traffic car happens to reuse every address.
        f.put(D+stride+0x734,77u);f.put(C+stride+0x28,2u);f.put(PR+stride+0x14,7u);f.tick();
        assert(f.status().active_cars==4 && f.status().activations==5);
        f.cfg.pursuit.enabled=0;copchase_game_configure(f.game,&f.cfg);f.tick();assert(f.get<uint32_t>(D+stride+0x734)==77);}
}
void context_gates(){for(unsigned gate=0;gate<10;++gate){Fixture f;f.enable();
    if(gate==0)f.guard.assets_ready=0;if(gate==1)f.guard.offline=0;if(gate==2)f.guard.races_inactive=0;
    if(gate==3)f.guard.fleet_retired=0;if(gate==4)f.put(0x428e34,7u);if(gate==5)f.put(0x41c338,2u);
    if(gate==6)f.put(0x3cc470,1u);if(gate==7)f.put(0x406960,1u);if(gate==8)f.put(0x456290,1u);
    if(gate==9)f.guard.world_generation=0;f.run(50);assert(!f.status().active_cars && f.calls.empty() && f.writes.empty());}
    Fixture f;f.start();f.put(0x3cc470,1u);f.tick();assert(f.status().releases==4 && !f.status().active_cars);
}
void road_path_and_contact(){Fixture f;f.start(1,COPCHASE_SLOW);f.cfg.direct_range=1;copchase_game_configure(f.game,&f.cfg);f.tick();
    uint32_t ctrl=CT+stride;f.put(ctrl+0x230,100.f);f.put(ctrl+0x224,123.f);
    assert(copchase_game_driver(f.game,ctrl));assert(!copchase_game_heading(f.game,ctrl));
    assert(f.get<float>(ctrl+0x224)==123.f && f.get<float>(ctrl+0x230)==100.f);
    f.cfg.direct_range=110;copchase_game_configure(f.game,&f.cfg);f.tick();
    f.put(C+stride+0x50,f.get<float>(C+0x50)-3);f.put(C+stride+0x70,50.f);
    assert(copchase_game_driver(f.game,ctrl));f.tick();assert(f.status().contacts==1);
    assert(copchase_game_driver(f.game,ctrl));f.tick();assert(f.status().contacts==1);f.audit_writes();
}
void malformed_and_faults(){
    {Fixture f;f.enable();f.put(W+0x10,23u);f.run(50);assert(f.calls.empty() && f.writes.empty());}
    {Fixture f;f.enable();f.put(C+0x50,std::numeric_limits<float>::quiet_NaN());f.run(50);assert(f.calls.empty() && f.writes.empty());}
    {Fixture f;f.start();double t=f.status().pursuit.chase_seconds;f.calls.clear();f.writes.clear();
        for(float dt:{0.f,-.1f,.3f,std::numeric_limits<float>::quiet_NaN()})f.tick(dt,false);
        assert(f.status().pursuit.chase_seconds==t && f.calls.empty() && f.writes.empty());}
    {Fixture f;f.enable();f.fail_physics=true;f.run(45);assert(!f.status().active_cars && f.status().failures>0);
        assert(f.get<uint32_t>(C+stride+0x28)==2 && f.get<uint32_t>(PR+stride+0x14)==7);}
    {Fixture f;f.start();f.bad_release=true;f.cfg.pursuit.enabled=0;copchase_game_configure(f.game,&f.cfg);f.tick();
        assert(f.status().failures==4);auto calls=f.calls.size();f.tick();assert(f.calls.size()==calls);}
    {Fixture f;f.start();f.fail_release=true;f.cfg.pursuit.enabled=0;copchase_game_configure(f.game,&f.cfg);f.tick();
        assert(f.status().failures==4 && f.status().active_cars==4 && f.status().release_pending);
        auto calls=f.calls.size();f.tick();assert(f.calls.size()==calls && f.status().active_cars==4);}
    {Fixture f;f.start();f.cfg.pursuit.style=COPCHASE_SLOW;copchase_game_configure(f.game,&f.cfg);f.tick();
        assert(!f.status().active_cars && f.status().releases==4 && f.status().pursuit.phase==COPCHASE_IDLE);}
    {Fixture f;f.start();f.put(C+stride+0x50,std::numeric_limits<float>::quiet_NaN());f.obstruction=true;f.run(200);
        assert(f.status().pursuit.phase==COPCHASE_PURSUIT && f.status().unknown_cars>0);}
}
void cooldown_releases(){Fixture f;f.start();for(unsigned i=1;i<=f.cops;++i)f.put(C+i*stride+0x50,f.get<float>(C+i*stride+0x50)-10.f);
    f.obstruction=true;f.run(130);
    assert(f.status().pursuit.phase==COPCHASE_COOLDOWN && !f.status().active_cars && f.status().releases==4);
    f.obstruction=false;f.run(4);assert(f.status().pursuit.phase==COPCHASE_PURSUIT && f.status().active_cars==4);
    assert(f.status().pending_events&COPCHASE_SPOTTED);f.audit_writes();
}
void physical_assistance(){
    Fixture f;f.start();const uint32_t car=C+stride,control=CT+stride,facade=0xc00000,body=0xc01000,physical=0xc02000;
    f.put(car+0x3c,facade);f.put(facade,0x35c0f8u);f.put(facade+0x130,car);f.put(facade+0x68,body);
    f.put(car+0x34,physical);f.put(physical+0x20,body);f.put(body,0x35bd2cu);
    f.put(body+0x70,5.f);f.put(body+0x74,0.f);f.put(body+0x78,0.f);
    f.put(car+0x70,5.f);f.put(car+0xb8,1.f);f.tick(.033f);
    assert(copchase_game_driver(f.game,control));assert(f.get<float>(body+0x70)>5.f);
    assert(f.get<float>(car+0x70)==5.f);f.tick(.033f);assert(f.status().impulses==1);
    const float old=f.get<float>(body+0x70);f.put(facade+0x130,C);f.writes.clear();
    assert(copchase_game_driver(f.game,control));assert(f.get<float>(body+0x70)==old);f.audit_writes();
    f.put(facade+0x130,car);f.put(physical+0x20,body+0x100);f.writes.clear();
    assert(copchase_game_driver(f.game,control));assert(f.get<float>(body+0x70)==old);f.audit_writes();
    f.put(physical+0x20,body);f.put(body+0x70,std::numeric_limits<float>::quiet_NaN());f.writes.clear();
    assert(copchase_game_driver(f.game,control));assert(std::isnan(f.get<float>(body+0x70)));f.audit_writes();
}
void roadblocks_and_spikes(){
    Fixture f(20);f.road_fixture();
    for(unsigned n=0;n<100 && !f.status().roadblocks_deployed;++n)f.tick();
    auto s=f.status();assert(s.roadblocks_deployed==1 && s.road_cells>20 && s.held_cars>=2);
    assert(s.road_stage_frames==s.held_cars && s.staging_cars==s.held_cars && f.batches==s.held_cars);
    assert(s.pending_road_events&COPCHASE_ROADBLOCK_DEPLOYED);assert(!(s.missing_features&COPCHASE_GAME_NO_ROADBLOCKS));
    unsigned held=0;for(unsigned i=1;i<=f.cops;++i)if(f.staged[i]){held=i;break;}assert(held);
    const uint32_t car=C+held*stride,ctrl=CT+held*stride,body=0xc00000+held*stride+0x400;
    f.tick();assert(copchase_game_driver(f.game,ctrl));assert(f.get<float>(ctrl+0x230)==0 && f.get<uint32_t>(D+held*stride+0x734)==0);
    f.put(ctrl+0x224,999.f);assert(copchase_game_heading(f.game,ctrl));assert(f.get<float>(ctrl+0x224)!=999.f);
    assert(f.get<float>(body+0x70)==0);const auto stage_count=f.status().road_stage_frames;
    f.guard.paused=1;f.run(10);assert(f.status().road_stage_frames==stage_count);f.guard.paused=0;
    f.run(6);s=f.status();assert(s.staging_cars==0 && s.road_stage_frames==s.held_cars*8);
    assert(s.spike_strip.active);const auto strip=s.spike_strip;
    // Cross a real planned strip with all four verified descriptor wheels.
    // A <=6m step preserves the original crossing-history condition.
    f.put(C+0x50,strip.position[0]-2.9f);f.put(C+0x54,strip.position[1]);f.put(C+0x58,strip.position[2]+.2f);
    f.tick(.05f,false);f.put(C+0x50,strip.position[0]+2.9f);f.tick(.05f,false);s=f.status();
    assert(s.flat_tyre_mask==15 && s.spike_hits==4 && s.spike_impulses>0 && s.tyre_scrape);
    assert(s.pending_road_events&COPCHASE_TIRES_SPIKED);assert(f.get<float>(0xc00470)<40.f);
    assert(f.status(1).pending_road_events);assert(!f.status().pending_road_events);
    f.put(C+0x50,f.get<float>(car+0x50)+30.f);f.tick(.05f,false);s=f.status();
    assert(!s.held_cars && !s.roadblock_active && s.roadblocks_released==1);
    assert(f.get<uint32_t>(car+0x28)==2 && f.get<uint32_t>(PR+held*stride+0x14)==7);
    f.cfg.pursuit.enabled=0;copchase_game_configure(f.game,&f.cfg);f.tick(.05f,false);
    assert(!f.status().active_cars && !f.status().spike_strip.active && !f.status().flat_tyre_mask);
}
void roadblock_guards(){
    {Fixture f(20);f.road_fixture(false);for(unsigned n=0;n<100 && !f.status().roadblocks_deployed;++n)f.tick();
        assert(f.status().held_cars>=2 && !f.status().spike_strip.active && !f.status().flat_tyre_mask);}
    {Fixture f(20);f.road_fixture();f.fail_batch=true;f.run(60);
        assert(f.status().failures>0 && !f.batches && !f.status().held_cars && !f.status().spike_strip.active);}
    {Fixture f(20);f.road_fixture();for(unsigned i=1;i<=f.cops;++i)f.put(CT+i*stride+0x10,uint8_t(21));
        // Observed movement is behind the player; malformed paths cannot make a site ahead.
        f.run(100);assert(!f.status().roadblocks_deployed && !f.batches);}
    {Fixture f(20);f.road_fixture();for(unsigned n=0;n<100 && !f.status().roadblocks_deployed;++n)f.tick();
        f.calls.clear();f.writes.clear();copchase_game_world_ending(f.game);
        assert(f.calls.empty() && f.writes.empty() && !f.status().held_cars && !f.status().spike_strip.active);}
}
void raw_incidents(){
    Fixture f;f.start(1,COPCHASE_MOST_WANTED,1);const auto facade=f.physical(1);const float hit[3]={20,0,0},fatal[3]={100,0,0};
    f.put(C+stride+0x50,f.get<float>(C+0x50)+3);const auto writes=f.writes.size(),calls=f.calls.size();
    assert(copchase_game_collision(f.game,facade,hit));assert(f.writes.size()==writes && f.calls.size()==calls);
    f.tick(.05f,false);auto s=f.status();assert(s.collision_events==1 && s.cops_damaged==1 && s.incident_bounty==500 && s.cost_to_state==250);
    assert(s.infraction_mask&COPCHASE_INCIDENT_ASSAULT);assert(s.pending_incident_events&COPCHASE_INCIDENT_DAMAGED);
    assert(copchase_game_collision(f.game,facade,fatal));f.tick(.05f,false);s=f.status();
    assert(s.cops_taken_out==1 && s.incident_bounty==5500 && s.cost_to_state==6250);
    assert(f.get<uint32_t>(PR+stride+0x14)==7 && f.get<uint32_t>(C+stride+0x28)==2);
    assert(!copchase_game_driver(f.game,CT+stride));f.tick();assert(f.get<uint32_t>(PR+stride+0x14)==7);
    assert(f.status(1).pending_incident_events);assert(!f.status().pending_incident_events);
    const unsigned active=2;const auto other=f.physical(active);f.put(other+0x130,C);
    assert(!copchase_game_collision(f.game,other,hit));f.put(other+0x130,C+active*stride);
    f.guard.paused=1;assert(!copchase_game_collision(f.game,other,hit));f.guard.paused=0;
    const float bad[3]={std::numeric_limits<float>::infinity(),0,0};assert(!copchase_game_collision(f.game,other,bad));
    const float small[3]={2,0,0};const uint64_t dropped=f.status().collision_dropped,events=f.status().collision_events;
    for(unsigned i=0;i<32;++i)assert(copchase_game_collision(f.game,other,small));assert(!copchase_game_collision(f.game,other,small));
    f.tick(.05f,false);assert(f.status().collision_events==events+32 && f.status().collision_dropped==dropped+1);
    assert(copchase_game_collision(f.game,other,hit));f.calls.clear();f.writes.clear();copchase_game_car_ending(f.game,C+active*stride);
    assert(f.calls.empty() && f.writes.empty());f.tick(.05f,false);assert(f.status().collision_events==events+32);
    assert(!(f.status().missing_features&COPCHASE_GAME_NO_RAM_TRIGGER));
}
void ram_trigger_and_markers(){
    {Fixture f;f.enable();for(unsigned i=0;i<45;++i)f.tick(.1f,false);
        assert(f.status().pursuit.phase==COPCHASE_IDLE);const auto facade=f.physical(1);f.put(C+stride+0x50,f.get<float>(C+0x50)+3);
        const float impulse[3]={20,0,0};assert(copchase_game_collision(f.game,facade,impulse));f.tick(.1f,false);
        assert(f.status().pursuit.phase==COPCHASE_PURSUIT && (f.status().pending_incident_events&COPCHASE_INCIDENT_RAM_TRIGGER));}
    {Fixture f;const uint32_t list=0xe10000,node=0xe11000,descriptor=0xe12000;
        f.put(0x3f9d3c,list);f.put(list,node);f.put(list+4,node);f.put(node,list);f.put(node+4,list);
        f.put(node+0x1c,descriptor);f.put(descriptor+4,6u);f.put(node+0xc,100.f);f.put(node+0x10,-200.f);
        f.enable();f.tick(.1f,false);assert(f.status().hiding_known && f.status().hiding_markers==1);
        f.put(node+4,0u);for(unsigned i=0;i<22;++i)f.tick(.1f,false);
        assert(!f.status().hiding_known && !f.status().hiding_markers);
        copchase_game_world_ending(f.game);assert(!f.status().hiding_known);}
}
}
int main(){disabled_and_defaults();selection_and_hooks();caps_and_presets();model_heat_selection();model_reassignment();pauses_and_stream();settlement_hold();owned_pursuit_race();uncertain_and_blocked_los();
    lifecycle();context_gates();road_path_and_contact();malformed_and_faults();cooldown_releases();physical_assistance();roadblocks_and_spikes();roadblock_guards();raw_incidents();ram_trigger_and_markers();
    std::puts("copchase_game fixture PASS: defaults, 4 presets, caps, hooks/roadpath/contact, pause/stream, LOS, lifetimes, context, faults, cooldown");}
