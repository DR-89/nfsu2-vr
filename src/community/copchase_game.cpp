#include "copchase_game.h"
#include "copchase_steering.h"
#include "copchase_contact.h"
#include "copchase_tactics.h"
#include "copchase_driver.h"
#include "copchase_roadblock.h"
#include "copchase_incidents.h"
#include "copchase_markers.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <new>

namespace {
constexpr unsigned kIndices=22,kMaximum=10,kListLimit=256;
constexpr uint32_t kMissing=COPCHASE_GAME_NO_BUST_PRESENTATION|COPCHASE_GAME_NO_CASH|
    COPCHASE_GAME_NO_PERSISTENCE|COPCHASE_GAME_NO_HUD|COPCHASE_GAME_NO_AUDIO;
struct Identity {uint32_t car=0,record=0,driver=0,control=0,index=0,type=0;unsigned model_class=0;};
struct Pose {CopChasePose xy{};float z=0;};
struct World {uint32_t address=0,player=0,count=0,first=0;std::array<uint32_t,kIndices> indexed{};
    std::array<uint32_t,kListLimit> list{};unsigned listed=0;};
struct Slot {
    Identity id{};bool active=false,pending=false,planned=false,los_valid=false,release_failed=false;
    uint32_t saved_734=0,saved_79c=0,saved_speed=0,saved_timer=0,saved_command=0;
    uint8_t saved_enable=0;
    uint32_t contact_ms=0;double los_seconds=0;CopChaseVisibility visibility=COPCHASE_UNKNOWN;
    int preferred_side=1;CopChasePersonality personality{1,0,1,1};CopChaseSteeringOutput plan{};
    CopChaseRecoveryState recovery{};
    bool held=false;unsigned stage_frames=0;uint32_t serial=0;
    float stage_position[3]{},stage_heading[2]{};
    uint32_t last_damage_ms=0;
};
bool same(const Identity &a,const Identity &b){return a.car==b.car && a.record==b.record &&
    a.driver==b.driver && a.control==b.control && a.index==b.index && a.type==b.type && a.model_class==b.model_class;}
bool finite(float f,float limit){return std::isfinite(f) && std::fabs(f)<=limit;}
float bounded(float f,float lo,float hi,float def){return std::isfinite(f)?std::clamp(f,lo,hi):def;}
bool contains(const World &w,uint32_t car){return car &&
    std::find(w.list.begin(),w.list.begin()+w.listed,car)!=w.list.begin()+w.listed;}
}
struct CopChaseGame {
    CopChaseGameHost host{};std::mutex mutex;CopChaseGameConfig config{};
    CopChaseGameConfig applied{};CopChaseGameStatus status{},published{};
    CopChaseState state{};std::array<Slot,kIndices> slots{};
    CopChaseTacticsState tactics_state{};CopChaseTacticsOutput tactics{};
    uint64_t world_generation=0,player_generation=0;uint32_t world=0,player=0;
    uint64_t race_token=0;
    uint32_t scratch=0,random=1;unsigned los_cursor=0;bool ready=false,busy=false,release_pending=false,fault=false;
    double seconds=0,sample_seconds=0;float last_dt=0,last_position[2]{},filtered[2]{};bool sampled=false;
    CopChaseRoads *roads=nullptr;uint32_t road_generation=1,identity_serial=0,pursuit_epoch=0;
    uint32_t block_members=0;
    CopChaseRoadblockSchedule road_schedule{};CopChaseRoadblockLifecycle road_block{};
    CopChaseSpikeLifecycle spike_lifecycle{};CopChaseSpikeState spikes{};
    CopChaseIncidents incidents{};CopChaseMarkers markers{};uint32_t markers_ms=0;bool markers_sampled=false;
    struct Collision {uint64_t world_generation=0,player_generation=0;uint32_t car=0,record=0,driver=0,control=0,index=0,serial=0,ms=0;float strength=0;};
    std::array<Collision,32> collisions{};unsigned collision_count=0;int pending_ram=-1;uint32_t pending_ram_serial=0;
    uint32_t models_ms=0;bool models_sampled=false;
    template<typename T> bool read(uint32_t a,T &v){return bytes(a,&v,sizeof v);}
    bool bytes(uint32_t a,void *p,uint32_t n){return a && n && a<=UINT32_MAX-n && host.read(host.context,a,p,n)!=0;}
    template<typename T> bool write(uint32_t a,const T &v){return a && a<=UINT32_MAX-sizeof v &&
        host.write(host.context,a,&v,sizeof v)!=0;}
    bool zero(uint32_t a,uint32_t n){uint8_t z[0x400]{};return n<=sizeof z && host.write(host.context,a,z,n)!=0;}
    bool call(uint32_t fn,uint32_t self,unsigned n,const uint32_t *args,uint32_t *value=nullptr){
        static const struct{uint32_t fn;unsigned n;} allowed[]={{0x37550,1},{0x39b00,1},
            {0x1e47d0,1},{0x1e4a70,1},{0x1d13f0,0},{0x1d14a0,0},{0x1d6e70,2},{0x1d8440,5},
            {0x1f10c0,1},{0x1ffda0,1},{0x207e00,1}};
        bool ok=false;for(auto a:allowed)if(a.fn==fn && a.n==n)ok=true;
        uint32_t result=0;ok=ok && host.call(host.context,fn,self,n,args,&result)!=0;
        if(value)*value=result;return ok;
    }
    bool guards(CopChaseGameGuards &g){g={};return host.guards(host.context,&g)!=0;}
    bool context(const CopChaseGameGuards &g){uint32_t flow=0,mode=0,race=0,event=0,net=0;
        if(!g.offline || !g.races_inactive || !g.fleet_retired || !read(0x428e34,flow) || flow!=6 ||
           !read(0x41c338,mode) || !read(0x3cc470,race) || !read(0x406960,event) || event || !read(0x456290,net) || net)return false;
        if(!g.pursuit_race_token)return mode==1 && !race;
        if(mode!=2 || race!=1 || g.pursuit_race_kind!=1 || g.pursuit_race_start_heat<1 || g.pursuit_race_start_heat>5 ||
           g.pursuit_race_style<0 || g.pursuit_race_style>3 || !g.pursuit_race_track || g.pursuit_race_track==4000 ||
           g.pursuit_race_track==4801 || !g.pursuit_race_world || !g.pursuit_race_player || !g.pursuit_race_manager ||
           g.pursuit_race_manager>UINT32_MAX-0xbe8)return false;
        uint32_t selected=0,track=0,manager=0,state=0,director=0,current_world=0,p=0,car=0,row=0,count=0;
        if(!read(0x406d74,selected) || selected!=1 || !read(0x3cc460,track) || track!=g.pursuit_race_track ||
           !read(0x449478,manager) || manager!=g.pursuit_race_manager || !read(manager,state) || state!=4 ||
           !read(0x449438,director) || !director || !read(director,state) || (state!=4 && !(g.paused && state==6)) ||
           !read(0x4493e0,current_world) || current_world!=g.pursuit_race_world || !read(0x44940c,p) ||
           !p || p>UINT32_MAX-0x20 || !read(p+4,car) || car!=g.pursuit_race_player || !read(p+0x1c,row) || !row ||
           !read(manager+0x24,count) || !count || count>6)return false;
        unsigned matches=0;for(unsigned i=0;i<count;++i){uint32_t r=0;if(!read(manager+0xbd0+i*4,r))return false;matches+=r==row;}
        return matches==1;
    }
    bool idle(const CopChaseGameGuards &g){
        if(!g.streaming_idle)return false;
        for(uint32_t a:{0x38dbb0u,0x38dbc8u,0x38dbe0u}){uint32_t v=1;if(!read(a,v) || v)return false;}
        for(uint32_t a:{0x38dba4u,0x38dbbcu,0x38dbd4u,0x3e3ab0u}){uint8_t v=1;if(!read(a,v) || v)return false;}
        return true;
    }
    bool world_read(World &w){
        w={};uint32_t p=0,next=0,prev=0,tail=0;
        if(!read(0x4493e0,w.address) || !w.address || w.address>UINT32_MAX-0x118u || !read(w.address,next) || !read(w.address+4,tail) ||
            !read(w.address+0x10,w.count) || w.count>kIndices || !read(w.address+0x18,w.first) ||
            w.first>w.count || !bytes(w.address+0x1c,w.indexed.data(),kIndices*4))return false;
        prev=w.address;
        while(next!=w.address){
            if(!next || next>UINT32_MAX-0x9e0u || w.listed==kListLimit || contains(w,next))return false;
            uint32_t back=0,after=0;if(!read(next+4,back) || back!=prev || !read(next,after))return false;
            w.list[w.listed++]=next;prev=next;next=after;
        }
        if(tail!=prev)return false;
        // A valid world can outlive its Player during unload; retain the world
        // census so live owned traffic can still be released at this boundary.
        if(read(0x44940c,p) && p && p<=UINT32_MAX-0x24u)read(p+4,w.player);
        if(!contains(w,w.player))w.player=0;
        return true;
    }
    bool model_type(uint32_t type,unsigned &model_class){
        // Verified mixed8 asset mapping in the supplied mod's slot table.
        static const char *const names[]={"4DR_SEDAN","MINIVAN","COUPE","TAXI02","HATCHBACK","BUS","PICKUP","SUV"};
        uint32_t types=0,kind=0;char name[24]{};
        if(!read(0x45af88,types) || !types || types>UINT32_MAX-46u*0x890u || type<types ||
           type-types>=46u*0x890u || (type-types)%0x890u || !read(type+0x844,kind) || kind!=2 ||
           !bytes(type,name,sizeof name) || !std::memchr(name,0,sizeof name))return false;
        for(unsigned i=0;i<8;++i)if(!std::strcmp(names[i],name)){model_class=i;return true;}return false;
    }
    bool model(uint32_t car,uint32_t &type,unsigned &model_class){return read(car+0xc,type) && model_type(type,model_class);}
    bool heat_available(const Slot &s)const{
        // Source10007030 excludes a model before its first nonzero heat
        // weight. This gate does not pretend to rebuild the population.
        static constexpr unsigned first_heat[]={0,2,3,3,4,5,3,4};
        return s.id.model_class<8 && unsigned(state.output.heat)>=first_heat[s.id.model_class];
    }
    struct TrafficRide {uint32_t address=0,type=0,handle=0,loaded=0,refs=0,uses=0,assigned=0;uint8_t skin=0;unsigned model_class=8;};
    bool traffic_rides(std::array<TrafficRide,15> &rides,unsigned &count){
        // TrafficManager constructor2A0A0 and caller2E0BB0; native list900.
        constexpr uint32_t head=0x3db0d0;count=0;uint32_t next=0,tail=0,previous=head,types=0;
        if(!read(head,next) || !read(head+4,tail) || !read(0x45af88,types) || !types || types>UINT32_MAX-46u*0x890u)return false;
        while(next!=head){
            if(!next || next>UINT32_MAX-0x920 || count==rides.size())return false;
            for(unsigned i=0;i<count;++i)if(rides[i].address==next)return false;
            TrafficRide r;uint32_t after=0,back=0;r.address=next;
            if(!read(next,after) || !read(next+4,back) || back!=previous || !read(next+0x10,r.type) || r.type>=46 ||
               !read(next+0x17,r.skin) || !read(next+0x8f8,r.handle) || !read(next+0x900,r.loaded) ||
               !read(next+0x908,r.refs) || r.refs>kIndices || !read(next+0x90c,r.uses) || r.uses>=1000000 ||
               !read(next+0x910,r.assigned) || r.assigned>kIndices)return false;
            model_type(types+r.type*0x890,r.model_class);rides[count++]=r;previous=next;next=after;
        }
        uint32_t first=0,last=0;return tail==previous && read(head,first) && first==(count?rides[0].address:head) && read(head+4,last) && last==tail;
    }
    bool loaded_entry(uint32_t handle,uint32_t &entry){
        constexpr uint32_t head=0x45c4f8;entry=0;uint32_t next=0,tail=0,previous=head;std::array<uint32_t,256> seen{};unsigned count=0;
        if(!handle || !read(head,next) || !read(head+4,tail))return false;
        while(next!=head){uint32_t after=0,back=0,h=0;
            if(!next || next>UINT32_MAX-0x1240 || count==seen.size() ||
               std::find(seen.begin(),seen.begin()+count,next)!=seen.begin()+count ||
               !read(next,after) || !read(next+4,back) || back!=previous || !read(next+0x123c,h))return false;
            if(h==handle){if(entry)return false;entry=next;}seen[count++]=next;previous=next;next=after;
        }
        return tail==previous && entry;
    }
    bool model_words(const uint32_t *addresses,const uint32_t *values,unsigned count){
        CopChaseRoadWrite changes[6]{};if(!host.write_batch || !count || count>6)return false;
        for(unsigned i=0;i<count;++i){uint32_t before=0;if(!read(addresses[i],before))return false;
            changes[i].address=addresses[i];changes[i].size=4;std::memcpy(changes[i].bytes,values+i,4);}
        return host.write_batch(host.context,changes,count)!=0;
    }
    bool reassign_model(Slot &s,const World &w,const TrafficRide &target){
        if(s.active || s.held || s.pending || s.release_failed || !live(w,s) || !enabled_car(s) || !goal(s,7,0))return false;
        uint32_t mode=0,types=0,ride=0,entry=0,done=0;uint8_t skin=0;
        if(!read(s.id.car+0x28,mode) || mode!=2 || !read(0x45af88,types) || !read(s.id.car+0x10,ride) ||
           ride!=s.id.record+0x10 || !read(ride+7,skin) || !target.loaded || target.loaded==0x7fffffff ||
           !loaded_entry(target.handle,entry) || !call(0x1ffda0,0x45c4a0,1,&entry,&done) || !done ||
           !call(0x207e00,0x45c4a0,1,&target.handle))return false;
        // Reacquire all identities after native calls; never carry a cached
        // ride/body reference across a yield or a changed world.
        CopChaseGameGuards current;World after;std::array<TrafficRide,15> rides{};unsigned count=0;
        if(!guards(current) || !current.assets_ready || current.settlement_pending || current.paused || !context(current) || !idle(current) ||
           current.pursuit_race_token!=race_token ||
           current.world_generation!=world_generation || current.player_generation!=player_generation ||
           !world_read(after) || after.address!=world || after.player!=player || !live(after,s) || !traffic_rides(rides,count))return false;
        const TrafficRide *old=nullptr,*next=nullptr;const uint32_t old_type=(s.id.type-types)/0x890;
        for(unsigned i=0;i<count;++i){const auto &r=rides[i];if(r.type==old_type && r.skin==skin){if(old)return false;old=&r;}
            if(r.address==target.address)next=&r;}
        if(!old || !next || old==next || next->type!=target.type || next->handle!=target.handle || !next->loaded ||
           next->loaded==0x7fffffff || !old->refs || !old->assigned || next->refs>=kIndices || next->assigned>=kIndices)return false;
        const uint32_t pre_address[]={next->address+0x908,next->address+0x90c,next->address+0x910,s.id.record+0x908};
        const uint32_t pre_value[]={next->refs+1,next->uses+1,next->assigned+1,next->type};
        const uint32_t post_address[]={old->address+0x908,old->address+0x910};
        const uint32_t post_value[]={old->refs-1,old->assigned-1};
        // Both batches' ranges have already been read from the fresh bounded
        // list. SetRide copies the complete ride and rebuilds its physical and
        // render bodies; no car, index, driver or control is replaced. Retain
        // the old ride reference until success, and protect the new one first.
        // Final counts match the source, but a failed call cannot expose either
        // possibly-referenced model to native eviction; at most one transaction
        // remains pinned until normal world teardown because faults latch.
        if(!model_words(pre_address,pre_value,4)){++status.failures;fault=true;return false;}
        const uint32_t new_ride=next->address+0x10;Identity changed;
        bool ok=call(0x1f10c0,s.id.car,1,&new_ride) && world_read(after) && after.address==world && after.player==player &&
            identity(after,s.id.index,changed) && changed.car==s.id.car && changed.record==s.id.record &&
            changed.driver==s.id.driver && changed.control==s.id.control && changed.type==types+next->type*0x890 &&
            changed.model_class==next->model_class && read(s.id.car+0x28,mode) && mode==2 && goal(s,7,0);
        // A reported native failure may have mutated storage. Latch it rather
        // than guessing a rollback or reusing a stale identity next frame.
        if(!ok){++status.failures;fault=true;return false;}
        s.id=changed;s.planned=s.los_valid=false;s.recovery={};
        if(!model_words(post_address,post_value,2)){++status.failures;fault=true;return false;}
        ++status.model_reassignments;return true;
    }
    void models_frame(const World &w,const Pose &target,const Pose *poses,const bool *eligible){
        if(!applied.heat_models || !host.write_batch || fault || (models_sampled && uint32_t(now_ms()-models_ms)<600))return;
        models_sampled=true;models_ms=now_ms();
        // Original10016960/10018350: weighted population, greatest deficit,
        // >=.75 car imbalance, at most two changes per600ms. The native port
        // only chooses already-loaded rides; loading/eviction remains retail.
        static constexpr uint8_t weights[8][6]={{6,6,4,2,1,0},{0,0,3,1,0,0},{0,0,0,5,3,1},{0,0,0,1,4,2},
            {0,0,0,0,1,6},{0,0,0,0,0,2},{0,0,0,2,0,0},{0,0,0,0,2,2}};
        const unsigned heat=state.output.phase==COPCHASE_IDLE?0u:unsigned(std::clamp(state.output.heat,1,5));
        unsigned counts[8]{},total=0,sum=0;for(unsigned m=0;m<8;++m)sum+=weights[m][heat];
        for(const auto &s:slots)if(s.id.car && s.id.model_class<8 && live(w,s)){++counts[s.id.model_class];++total;}
        if(!sum || !total)return;
        for(unsigned change=0;change<2 && !fault;++change){
            std::array<TrafficRide,15> rides{};unsigned count=0;if(!traffic_rides(rides,count))return;
            const TrafficRide *wanted=nullptr;float deficit=-1e9f;
            for(unsigned model=0;model<8;++model)if(weights[model][heat]){
                const TrafficRide *available=nullptr;for(unsigned i=0;i<count;++i)if(rides[i].model_class==model &&
                    rides[i].handle && rides[i].loaded && rides[i].loaded!=0x7fffffff){available=&rides[i];break;}
                const float need=float(total)*float(weights[model][heat])/float(sum)-float(counts[model]);
                if(available && deficit<need){deficit=need;wanted=available;}
            }
            if(!wanted || deficit<.75f)return;int chosen=-1;float best=-1e9f;
            for(unsigned i=0;i<kIndices;++i){const auto &s=slots[i];if(!eligible[i] || s.active || s.held || s.pending || s.release_failed ||
                incidents.cops[i].wrecked || s.id.model_class==wanted->model_class)continue;
                const unsigned m=s.id.model_class;const float excess=weights[m][heat]?
                    float(counts[m])-float(total)*float(weights[m][heat])/float(sum):99.f;
                if(excess<.75f)continue;
                const float dx=poses[i].xy.position[0]-target.xy.position[0],dy=poses[i].xy.position[1]-target.xy.position[1],dz=poses[i].z-target.z;
                const float distance=std::sqrt(dx*dx+dy*dy+dz*dz),ahead=dx*target.xy.forward[0]+dy*target.xy.forward[1];
                if(distance<=120 && (distance<=40 || ahead>=-25))continue;
                const float score=excess*100+distance;if(best<score){best=score;chosen=int(i);}
            }
            if(chosen<0)return;auto &s=slots[unsigned(chosen)];const unsigned old=s.id.model_class;
            if(!reassign_model(s,w,*wanted))return;--counts[old];++counts[s.id.model_class];
        }
    }
    bool identity(const World &w,unsigned i,Identity &id){
        id={};if(i<w.first || i>=w.count)return false;uint32_t c=w.indexed[i],kind=0,v=0;
        int16_t index=-1;if(!contains(w,c) || c==w.player || !read(c+8,index) || index!=int(i) ||
           !read(c+0x24,kind) || kind!=4 || !read(c+0x14,id.record) || !id.record || id.record>UINT32_MAX-0x940u ||
           !read(c+0x2c,id.driver) || !id.driver || id.driver>UINT32_MAX-0x7a8u ||
           !read(c+0x30,id.control) || !id.control || id.control>UINT32_MAX-0x460u ||
           !read(id.record+4,kind) || kind!=3 || !read(id.driver,v) || v!=0x341a70 ||
           !read(id.control,v) || v!=0x3414e8 || !read(id.driver+4,v) || v!=c ||
           !read(id.driver+8,v) || v!=id.control || !read(id.control+0x220,v) || v!=c || !model(c,id.type,id.model_class))return false;
        id.car=c;id.index=i;return true;
    }
    bool live(const World &w,const Slot &s){Identity current;return w.address==world &&
        identity(w,s.id.index,current) && same(current,s.id);}
    bool enabled_car(const Slot &s){uint8_t active=0,excluded=1;return read(s.id.car+0x550,active) && active &&
        read(s.id.car+0x998,excluded) && !excluded;}
    bool goal(const Slot &s,uint32_t kind,uint32_t target){uint32_t p=0,k=0,t=0;return
        read(s.id.driver+0x6fc,p) && p && p<=UINT32_MAX-0x1cu && read(p+0x14,k) && k==kind &&
        (!target || (read(p+0x18,t) && t==target));}
    bool pose(uint32_t car,Pose &p){
        // ASI uses render origin +50, velocity +70, and basis +90/+a0.
        if(!bytes(car+0x50,p.xy.position,8) || !read(car+0x58,p.z) || !bytes(car+0x70,p.xy.velocity,8) ||
           !bytes(car+0x90,p.xy.forward,8) || !bytes(car+0xa0,p.xy.left,8))return false;
        for(unsigned k=0;k<2;++k)if(!finite(p.xy.position[k],20000) || !finite(p.xy.velocity[k],200) ||
            !finite(p.xy.forward[k],1.01f) || !finite(p.xy.left[k],1.01f))return false;
        const float f=p.xy.forward[0]*p.xy.forward[0]+p.xy.forward[1]*p.xy.forward[1];
        const float l=p.xy.left[0]*p.xy.left[0]+p.xy.left[1]*p.xy.left[1];
        const float dot=p.xy.forward[0]*p.xy.left[0]+p.xy.forward[1]*p.xy.left[1];
        return finite(p.z,20000) && f>.5f && f<1.5f && l>.5f && l<1.5f && std::fabs(dot)<.1f;
    }
    uint32_t rand32(){random^=random<<13;random^=random>>17;random^=random<<5;return random;}
    uint32_t now_ms()const{return uint32_t(std::fmod(std::floor(seconds*1000),4294967296.0));}
    CopChaseDriverConfig driver_config()const{
        auto c=copchase_driver_defaults();c.preset=applied.pursuit.style;c.heat=state.output.heat;
        c.aggression_percent=applied.aggression_percent;return c;
    }
    bool impulse(const Slot &s,const float delta[3],float delta_limit=25){
        uint32_t mode=0,facade=0,body=0,physical=0,v=0;float velocity[3];
        if(!read(s.id.car+0x28,mode) || mode!=1 || !read(s.id.car+0x3c,facade) ||
           !facade || facade>UINT32_MAX-0x134u || !read(facade,v) || v!=0x35c0f8 ||
           !read(facade+0x130,v) || v!=s.id.car || !read(facade+0x68,body) ||
           !body || body>UINT32_MAX-0x7cu || !read(body,v) || v!=0x35bd2c ||
           !read(s.id.car+0x34,physical) || !physical || physical>UINT32_MAX-0x24u ||
           !read(physical+0x20,v) || v!=body || !bytes(body+0x70,velocity,sizeof velocity))return false;
        for(unsigned k=0;k<3;++k){if(!finite(velocity[k],300) || !finite(delta[k],delta_limit))return false;
            velocity[k]+=delta[k];if(!finite(velocity[k],300))return false;}
        return write(body+0x70,velocity);
    }
    void abandon(Slot &s){if(s.active)++status.abandoned;if(s.id.index<kIndices)tactics_state.member[s.id.index]=-1;s={};}
    void clear_identity(){for(auto &s:slots)abandon(s);world=player=0;world_generation=player_generation=race_token=0;
        ready=false;release_pending=false;sampled=false;filtered[0]=filtered[1]=0;sample_seconds=0;
        copchase_tactics_init(&tactics_state);tactics={};
        if(!++road_generation)++road_generation;copchase_roads_reset(roads,road_generation);
        road_block={};block_members=0;spike_lifecycle={};spikes={};road_schedule={};status.road_cells=0;
        copchase_incidents_init(&incidents);markers={};markers_sampled=false;collision_count=0;pending_ram=-1;models_sampled=false;}
    void publish(){status.pursuit=state.output;status.filtered_speed=std::hypot(filtered[0],filtered[1]);
        status.active_cars=0;for(const auto &s:slots)status.active_cars+=s.active?1u:0u;
        status.held_cars=status.staging_cars=0;for(const auto &s:slots){status.held_cars+=s.held?1u:0u;status.staging_cars+=s.stage_frames?1u:0u;}
        status.flat_tyre_mask=spikes.flat_mask;status.spike_strip=spike_lifecycle.strip;status.roadblock_active=road_block.active;
        status.infractions=incidents.infractions;status.infraction_mask=incidents.infraction_mask;
        status.cops_damaged=incidents.cops_damaged;status.cops_taken_out=incidents.cops_taken_out;
        status.cost_to_state=incidents.cost_to_state;status.incident_bounty=incidents.incident_bounty;
        status.hiding_known=markers.result==COPCHASE_MARKERS_VALID;status.hiding_markers=markers.count;
        status.release_pending=release_pending;status.missing_features=kMissing;
        if(!roads || !host.write_batch)status.missing_features|=COPCHASE_GAME_NO_ROADBLOCKS|COPCHASE_GAME_NO_SPIKES;
        std::lock_guard<std::mutex> lock(mutex);status.pending_events|=published.pending_events;
        status.pending_road_events|=published.pending_road_events;
        status.pending_incident_events|=published.pending_incident_events;
        published=status;status.pending_events=status.pending_road_events=status.pending_incident_events=0;}
    void cancel(){auto out=copchase_state_step(&state,&applied.pursuit,nullptr);status.pending_events|=out.events;ready=false;
        copchase_tactics_init(&tactics_state);tactics={};
        road_block.active=0;block_members=0;spike_lifecycle={};spikes={};status.tyre_scrape=0;
        collision_count=0;pending_ram=-1;markers={};markers_sampled=false;}
    bool release(Slot &s,const World &w){
        s.planned=false;s.pending=true;if(!s.active){s.pending=false;return true;}
        if(!live(w,s)){abandon(s);return true;}
        // Another owner replacing the provider ends our authority too.
        if(!goal(s,s.held?7u:5u,s.held?0u:player)){abandon(s);return true;}
        if(s.release_failed)return false;
        uint32_t seven=7,natural=0;bool ok=call(0x39b00,s.id.driver,1,&seven);
        World after;if(!world_read(after) || !live(after,s)){abandon(s);return true;}
        bool restored=write(s.id.driver+0x734,s.saved_734) && write(s.id.driver+0x79c,s.saved_79c) &&
            write(s.id.control+0x230,s.saved_speed) && write(s.id.control+0x234,s.saved_timer) &&
            write(s.id.control+0x238,s.saved_command) && write(s.id.control+0x23c,s.saved_enable);
        ok=call(0x1e47d0,0,1,&s.id.car,&natural) && ok;
        if(natural==2 && world_read(after) && live(after,s))ok=call(0x1e4a70,s.id.car,1,&natural) && ok;
        else ok=false;
        uint32_t physics=0;ok=ok && restored && read(s.id.car+0x28,physics) && physics==2 && goal(s,7,0);
        if(ok)++status.releases;else{++status.failures;fault=true;}
        // A failed native call is latched, never retried blindly each frame.
        // Keep reporting an unreleased owned provider, rather than claiming
        // active_cars==0 while it still targets the player.
        s.release_failed=!ok && goal(s,s.held?7u:5u,s.held?0u:player);s.active=s.release_failed;s.pending=s.release_failed;
        if(!s.active){s.held=false;s.stage_frames=0;}return ok;
    }
    bool activate(Slot &s,const World &w){
        if(!live(w,s) || !enabled_car(s))return false;
        uint32_t physics=0;if(!read(s.id.car+0x28,physics) || physics!=2 ||
           !read(s.id.driver+0x734,s.saved_734) || !read(s.id.driver+0x79c,s.saved_79c) ||
           !read(s.id.control+0x230,s.saved_speed) || !read(s.id.control+0x234,s.saved_timer) ||
           !read(s.id.control+0x238,s.saved_command) || !read(s.id.control+0x23c,s.saved_enable))return false;
        // Mark before mutation so partial activation has a release path.
        s.active=true;uint32_t one=1;bool ok=call(0x37550,s.id.driver,1,&player);
        World after;ok=ok && world_read(after) && live(after,s) && goal(s,5,player);
        ok=ok && call(0x1e4a70,s.id.car,1,&one) && world_read(after) && live(after,s) &&
            read(s.id.car+0x28,physics) && physics==1;
        if(!ok){++status.failures;fault=true;release(s,w);return false;}
        s.pending=false;s.planned=false;s.recovery={};++status.activations;return true;
    }
    CopChaseVisibility los(const Pose &a,const Pose &b){
        ++status.los_queries;
        if(!scratch){scratch=host.alloc(host.context,0x470);if(!scratch || scratch>UINT32_MAX-0x470u || (scratch&15u)){
            if(scratch)host.free(host.context,scratch);scratch=0;return COPCHASE_UNKNOWN;}}
        if(!zero(scratch,0x400) || !call(0x1d13f0,scratch,0,nullptr))return COPCHASE_UNKNOWN;
        const uint32_t from=scratch+0x400,to=from+16,normal=to+16,hit=normal+16,at=hit+16;
        bool known=true,blocked=false;uint32_t tree=0,result=0;
        const float dx=a.xy.position[0]-b.xy.position[0],dy=a.xy.position[1]-b.xy.position[1],dz=a.z-b.z;
        const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        const int count=std::max(1,int(std::ceil(distance/10.f)));
        // Original ASI 1002d400 adds DAT100768c0=1.0m to BOTH render origins.
        float start[4]={a.xy.position[0],a.xy.position[1],a.z+1.f,0};
        auto position=[&](float f,float (&v)[4]){v[0]=a.xy.position[0]+(b.xy.position[0]-a.xy.position[0])*f;
            v[1]=a.xy.position[1]+(b.xy.position[1]-a.xy.position[1])*f;v[2]=a.z+(b.z-a.z)*f+1.f;v[3]=0;};
        auto section=[&](const float (&v)[4]){const uint32_t args[]={at,0};return write(at,v) &&
            call(0x1d6e70,scratch,2,args) && read(scratch+8,tree) && tree;};
        known=section(start);
        for(int i=1;known && !blocked && i<=count;++i){
            float finish[4],middle[4];position(float(i)/float(count),finish);position((float(i)-.5f)/float(count),middle);
            // The native long-ray helper silently skips unloaded sections.
            // Check both ends and each <=10m segment's section before using
            // its collision result; missing sections remain UNKNOWN.
            known=section(finish) && section(middle) && write(from,start) && write(to,finish) && zero(normal,32);
            const uint32_t args[]={from,to,normal,hit,1};
            known=known && call(0x1d8440,scratch,5,args,&result) && read(scratch+8,tree) && tree;
            if(known)blocked=(result&255)!=0;std::memcpy(start,finish,sizeof start);
        }
        if(!call(0x1d14a0,scratch,0,nullptr)){known=false;fault=true;++status.failures;}
        return !known?COPCHASE_UNKNOWN:blocked?COPCHASE_OCCLUDED:COPCHASE_VISIBLE;
    }
    static int road_read(void *ctx,uint32_t a,void *p,uint32_t n){return static_cast<CopChaseGame *>(ctx)->bytes(a,p,n);}
    static int road_identity(void *ctx,const CopChaseRoadIdentity *id,int traffic){
        auto &g=*static_cast<CopChaseGame *>(ctx);CopChaseGameGuards guards;World w;
        if(!id || id->world_generation!=g.road_generation || !g.guards(guards) || guards.paused || guards.settlement_pending ||
           !guards.assets_ready || guards.pursuit_race_token!=g.race_token || guards.world_generation!=g.world_generation || guards.player_generation!=g.player_generation ||
           !g.context(guards) || !g.idle(guards) || !g.world_read(w) || w.address!=g.world || w.player!=g.player)return 0;
        if(!traffic){uint32_t type=0;return id->car==g.player && contains(w,id->car) &&
            g.read(id->car+0xc,type) && type==id->type;}
        if(id->slot>=kIndices)return 0;const auto &s=g.slots[id->slot];
        return s.serial==id->identity_generation && s.id.car==id->car && s.id.driver==id->driver &&
            s.id.control==id->control && s.id.type==id->type && g.live(w,s) && g.enabled_car(s);
    }
    static int road_write(void *ctx,const CopChaseRoadWrite *writes,unsigned count){
        auto &g=*static_cast<CopChaseGame *>(ctx);return g.host.write_batch && g.host.write_batch(g.host.context,writes,count);
    }
    CopChaseRoadHost road_host(){return {this,road_read,road_identity,road_write};}
    bool road_id(const Slot &s,CopChaseRoadIdentity &id,bool physical=false){
        id={};id.car=s.id.car;id.driver=s.id.driver;id.control=s.id.control;id.type=s.id.type;
        id.slot=s.id.index;id.world_generation=road_generation;id.identity_generation=s.serial;
        if(!physical)return true;
        return read(id.car+0x34,id.mover) && read(id.car+0x3c,id.facade) && id.facade &&
            id.facade<=UINT32_MAX-0x134u && read(id.facade+0x68,id.body);
    }
    void road_census(){
        if(!roads)return;CopChaseRoadCar cars[kIndices]{};unsigned count=0;
        for(const auto &s:slots)if(s.id.car){auto &c=cars[count++];road_id(s,c.identity);
            c.selected=s.active&&!s.held;c.disabled=!enabled_car(s)||incidents.cops[s.id.index].wrecked;c.held=s.held;}
        auto h=road_host();CopChaseRoadCensus result{};
        if(copchase_roads_census(roads,&h,road_generation,now_ms(),cars,count,&result))status.road_cells=result.cells;
    }
    bool hold(Slot &s,const World &w,const CopChaseRoadblockCar &planned){
        if(s.active && !release(s,w))return false;
        uint32_t physics=0;if(!live(w,s) || !enabled_car(s) || !goal(s,7,0) ||
           !read(s.id.car+0x28,physics) || physics!=2 ||
           !read(s.id.driver+0x734,s.saved_734) || !read(s.id.driver+0x79c,s.saved_79c) ||
           !read(s.id.control+0x230,s.saved_speed) || !read(s.id.control+0x234,s.saved_timer) ||
           !read(s.id.control+0x238,s.saved_command) || !read(s.id.control+0x23c,s.saved_enable))return false;
        s.active=s.held=true;s.pending=s.planned=false;s.stage_frames=8;s.recovery={};
        std::memcpy(s.stage_position,planned.position,12);std::memcpy(s.stage_heading,planned.heading,8);
        uint32_t one=1;World after;
        if(!call(0x1e4a70,s.id.car,1,&one) || !world_read(after) || !live(after,s) || !goal(s,7,0) ||
           !read(s.id.car+0x28,physics) || physics!=1){++status.failures;fault=true;release(s,w);return false;}
        return true;
    }
    void release_block(const World &w){
        const bool existed=road_block.active || std::any_of(slots.begin(),slots.end(),[](const Slot &s){return s.held;});
        road_block.active=0;block_members=0;
        for(auto &s:slots)if(s.held)release(s,w);
        if(existed){++status.roadblocks_released;status.pending_road_events|=COPCHASE_ROADBLOCK_RELEASED;}
        road_schedule.next_attempt_ms=now_ms()+20000;
    }
    bool road_attempt(const World &w,const Pose &target,const Pose *poses,const bool *eligible){
        CopChaseRoadQuery query{};std::memcpy(query.position,target.xy.position,8);query.position[2]=target.z;
        std::memcpy(query.forward,target.xy.forward,8);std::memcpy(query.velocity,filtered,8);
        query.speed=std::hypot(filtered[0],filtered[1]);CopChaseRoadSite site{};
        if(!copchase_roads_query(roads,&query,&site))return false;
        CopChaseRoadblockPlanInput input{};input.heat=state.output.heat;input.spikes_enabled=applied.spikes_enabled&&applied.spikes_visible;
        input.heavy=applied.heavy_roadblocks;std::memcpy(input.player_position,target.xy.position,8);
        std::memcpy(input.travel_direction,site.travel,8);std::memcpy(input.centre,site.centre,12);std::memcpy(input.forward,site.forward,8);
        input.lane_min=site.lane_min;input.lane_max=site.lane_max;input.candidate_count=kIndices;
        for(unsigned i=0;i<kIndices;++i){const auto &s=slots[i];auto &c=input.candidates[i];uint32_t mode=0;
            c.id=i;std::memcpy(c.position,poses[i].xy.position,8);c.selected=s.active&&!s.held;c.held=s.held;
            c.disabled=s.pending||s.release_failed;c.eligible=eligible[i] && live(w,s) &&
                read(s.id.car+0x28,mode) && (s.active?(mode==1 && goal(s,5,player)):(mode==2 && goal(s,7,0)));}
        const uint32_t old_random=random;for(auto &v:input.random)v=rand32()%32768;
        CopChaseRoadblockPlan plan{};const bool success=copchase_roadblock_plan(&input,&plan)!=0;
        random=old_random;for(unsigned i=0;i<plan.random_used;++i)rand32();if(!success)return false;
        for(unsigned i=0;i<plan.count;++i)if(!hold(slots[plan.cars[i].id],w,plan.cars[i])){release_block(w);return false;}
        road_block={};road_block.active=1;road_block.created_ms=now_ms();
        block_members=0;for(unsigned i=0;i<plan.count;++i)block_members|=1u<<plan.cars[i].id;
        std::memcpy(road_block.centre,plan.centre,12);std::memcpy(road_block.forward,plan.forward,8);
        spike_lifecycle={};spike_lifecycle.strip=plan.strip;spike_lifecycle.created_ms=now_ms();
        return true;
    }
    void roads_frame(const World &w,const Pose &target,const Pose *poses,const bool *eligible,float dt){
        const bool pursuit=state.output.phase==COPCHASE_PURSUIT;bool deployed=false;
        if(!applied.roadblocks_enabled || !pursuit){if(road_block.active)release_block(w);}
        else if(road_block.active){CopChaseRoadblockLifecycleInput in{};in.now_ms=now_ms();in.pursuit_active=1;
            std::memcpy(in.player_position,target.xy.position,8);in.speed=std::hypot(filtered[0],filtered[1]);
            for(unsigned i=0;i<kIndices;++i)if((block_members&(1u<<i)) && slots[i].last_damage_ms && uint32_t(now_ms()-slots[i].last_damage_ms)<1000)in.recent_ram=1;
            CopChaseRoadblockRelease reason=COPCHASE_BLOCK_KEEP;
            if(copchase_roadblock_lifecycle(&road_block,&in,&reason) && reason!=COPCHASE_BLOCK_KEEP)release_block(w);}
        const bool held_live=std::any_of(slots.begin(),slots.end(),[](const Slot &s){return s.held;});
        if(road_block.active && !held_live)release_block(w);
        if(roads && host.write_batch && applied.roadblocks_enabled && !fault){
            CopChaseRoadblockScheduleInput in{};in.now_ms=now_ms();in.pursuit_epoch=pursuit_epoch;
            in.preset=applied.pursuit.style;in.heat=state.output.heat;in.active=pursuit;in.enabled=1;
            in.block_active=road_block.active;in.strip_active=spike_lifecycle.strip.active;in.arrest_phase=state.output.busted>0;
            in.speed=std::hypot(filtered[0],filtered[1]);in.chance_multiplier=applied.roadblock_chance_multiplier;in.insane_roll_ms=15000;
            const uint32_t old_random=random;for(auto &v:in.random)v=rand32()%32768;
            CopChaseRoadblockScheduleOutput out{};copchase_roadblock_schedule(&road_schedule,&in,&out);
            random=old_random;for(unsigned i=0;i<out.random_used;++i)rand32();
            if(out.request){deployed=road_attempt(w,target,poses,eligible);copchase_roadblock_attempted(&road_schedule,now_ms(),deployed);}
        }
        auto host_adapter=road_host();
        for(auto &s:slots)if(s.held && s.stage_frames && !fault){CopChaseRoadIdentity id;
            if(road_id(s,id,true) && copchase_roads_stage(&host_adapter,&id,s.stage_position,s.stage_heading)){
                --s.stage_frames;++status.road_stage_frames;
            }else{++status.failures;fault=true;release_block(w);break;}}
        if(fault){spike_lifecycle={};status.tyre_scrape=0;return;}
        if(deployed){++status.roadblocks_deployed;status.pending_road_events|=COPCHASE_ROADBLOCK_DEPLOYED;}
        if(!applied.spikes_enabled || !applied.roadblocks_enabled){spike_lifecycle={};spikes={};status.tyre_scrape=0;return;}
        if(!applied.spikes_visible){spike_lifecycle.strip.active=0;spikes.previous_valid=0;status.tyre_scrape=0;return;}
        CopChaseSpikeLifecycleInput life{};life.now_ms=now_ms();life.block_active=road_block.active;
        std::memcpy(life.player_position,target.xy.position,8);std::memcpy(life.player_velocity,target.xy.velocity,8);
        for(const auto &s:slots)if(s.held && !s.stage_frames && life.settled_count<4){Pose p;uint32_t mode=0;
            if(live(w,s) && read(s.id.car+0x28,mode) && mode==1 && pose(s.id.car,p))life.settled_height[life.settled_count++]=p.z;}
        CopChaseSpikeLifecycleOutput effect{};copchase_spikes_lifecycle(&spike_lifecycle,&life,&effect);
        if(effect.approaching)status.pending_road_events|=COPCHASE_SPIKES_APPROACHING;
        if(effect.removed)status.pending_road_events|=COPCHASE_SPIKES_REMOVED;
        CopChaseRoadIdentity player_id{};player_id.car=player;player_id.world_generation=road_generation;
        if(!read(player+0xc,player_id.type))return;CopChaseRoadWheels wheels{};
        if(!copchase_roads_wheels(&host_adapter,&player_id,&wheels)){status.tyre_scrape=0;return;}
        CopChaseSpikeInput in{};in.seconds=dt;in.driving=1;in.strip=spike_lifecycle.strip;
        std::memcpy(in.position,target.xy.position,8);in.position[2]=target.z;std::memcpy(in.forward,target.xy.forward,8);
        std::memcpy(in.left,target.xy.left,8);std::memcpy(in.velocity,target.xy.velocity,8);
        for(unsigned i=0;i<4;++i)std::memcpy(in.wheel_offset[i],wheels.local[i],8);
        CopChaseSpikeOutput result{};if(!copchase_spikes_step(&spikes,&in,&result))return;
        status.tyre_scrape=result.scrape_active;
        if(result.newly_flat_mask){status.pending_road_events|=COPCHASE_TIRES_SPIKED;
            for(unsigned i=0;i<4;++i)status.spike_hits+=(result.newly_flat_mask>>i)&1u;}
        if(result.apply_delta){Slot validated_player{};validated_player.id.car=player;
            if(impulse(validated_player,result.physical_delta,75))++status.spike_impulses;}
    }
    uint64_t incident_identity(const Slot &s)const{return (uint64_t(road_generation)<<32)|s.serial;}
    void incident_result(const CopChaseIncidentOutput &out){
        status.pending_incident_events|=out.new_infractions;
        if(out.damaged_slot>=0){slots[unsigned(out.damaged_slot)].last_damage_ms=now_ms();status.pending_incident_events|=COPCHASE_INCIDENT_DAMAGED;}
        if(out.taken_out_slot>=0)status.pending_incident_events|=COPCHASE_INCIDENT_TAKEN_OUT;
        if(out.trigger_slot>=0){pending_ram=out.trigger_slot;pending_ram_serial=slots[unsigned(pending_ram)].serial;
            status.pending_incident_events|=COPCHASE_INCIDENT_RAM_TRIGGER;}
    }
    bool collision_capture(uint32_t facade,const float raw[3]){
        if(busy || fault || release_pending || !applied.pursuit.enabled || !raw || !facade || facade>UINT32_MAX-0x134u)return false;
        for(unsigned i=0;i<3;++i)if(!finite(raw[i],1000000))return false;
        const float strength=float(std::sqrt(double(raw[0]*raw[0]+raw[1]*raw[1]+raw[2]*raw[2])));
        if(!finite(strength,1000000) || strength<=1)return false;
        CopChaseGameGuards guards;World w;uint32_t v=0,car=0,body=0,mover=0,mode=0;
        if(!this->guards(guards) || !guards.assets_ready || guards.paused || guards.settlement_pending || guards.world_generation!=world_generation ||
           guards.player_generation!=player_generation || guards.pursuit_race_token!=race_token || !context(guards) || !idle(guards) || !world_read(w) ||
           w.address!=world || w.player!=player || !read(facade,v) || v!=0x35c0f8 ||
           !read(facade+0x130,car) || car==player || !contains(w,car) || !read(car+0x3c,v) || v!=facade ||
           !read(car+0x28,mode) || mode!=1 || !read(facade+0x68,body) || !body || body>UINT32_MAX-0x300u ||
           !read(body,v) || v!=0x35bd2c || !read(car+0x34,mover) || !mover || mover>UINT32_MAX-0x24u ||
           !read(mover+0x20,v) || v!=body)return false;
        int16_t index=-1;Collision event{};uint32_t type=0,types=0;
        if(!read(car+8,index) || index<int(w.first) || index>=int(w.count) || w.indexed[unsigned(index)]!=car ||
           !read(car+0x24,v) || v!=4 || !read(car+0x14,event.record) || !event.record || event.record>UINT32_MAX-0x940u ||
           !read(event.record+4,v) || v!=3 || !read(car+0x2c,event.driver) || !event.driver || event.driver>UINT32_MAX-0x7a8u ||
           !read(car+0x30,event.control) || !event.control || event.control>UINT32_MAX-0x460u ||
           !read(event.driver,v) || v!=0x341a70 || !read(event.control,v) || v!=0x3414e8 ||
           !read(event.driver+4,v) || v!=car || !read(event.driver+8,v) || v!=event.control ||
           !read(event.control+0x220,v) || v!=car || !read(car+0xc,type) || !read(0x45af88,types) ||
           !types || types>UINT32_MAX-46u*0x890u || type<types || type-types>=46u*0x890u || (type-types)%0x890u ||
           !read(type+0x844,v) || v!=2)return false;
        if(collision_count==collisions.size()){++status.collision_dropped;return false;}
        event.car=car;event.index=unsigned(index);event.world_generation=world_generation;event.player_generation=player_generation;
        event.ms=now_ms();event.strength=strength;const auto &s=slots[event.index];
        if(s.id.car==car && live(w,s))event.serial=s.serial;
        collisions[collision_count++]=event;return true;
    }
    void incidents_frame(const World &w,const Pose &target,Pose *poses,bool *eligible,CopChaseObservation *obs){
        auto incident_config=copchase_incident_defaults();CopChaseWitness witnesses[kIndices]{};unsigned count=0;
        for(unsigned i=0;i<kIndices;++i)if(eligible[i]){auto &witness=witnesses[count++];witness.slot=int(i);witness.usable=1;
            witness.excluded=incidents.cops[i].wrecked;std::memcpy(witness.position,poses[i].xy.position,8);witness.position[2]=poses[i].z;}
        for(unsigned n=0;n<collision_count;++n){const auto &event=collisions[n];uint32_t v=0;Pose hit;
            if(event.world_generation!=world_generation || event.player_generation!=player_generation || event.index>=w.count ||
               w.indexed[event.index]!=event.car || !contains(w,event.car) ||
               !read(event.car+0x14,v) || v!=event.record || !read(event.car+0x2c,v) || v!=event.driver ||
               !read(event.car+0x30,v) || v!=event.control || !pose(event.car,hit)){++status.collision_dropped;continue;}
            const auto &s=slots[event.index];const bool registered=s.id.car==event.car && live(w,s);
            if((registered && event.serial!=s.serial) || (!registered && event.serial)){++status.collision_dropped;continue;}
            CopChaseCollision in{};in.now_ms=now_ms();in.event_ms=event.ms;in.valid=in.player_valid=1;
            in.pursuit=state.output.phase!=COPCHASE_IDLE;in.target_is_traffic=1;in.target_slot=registered?int(event.index):-1;
            in.target_identity=registered?incident_identity(s):0;in.cross_slot=-1;in.pending_trigger=pending_ram;in.strength=event.strength;
            std::memcpy(in.player_position,target.xy.position,8);in.player_position[2]=target.z;std::memcpy(in.player_velocity,filtered,8);
            std::memcpy(in.target_position,hit.xy.position,8);in.target_position[2]=hit.z;in.witnesses=witnesses;in.witness_count=count;
            const auto result=copchase_incidents_collision(&incidents,&incident_config,&in);if(result.accepted)++status.collision_events;
            else ++status.collision_dropped;incident_result(result);
        }
        collision_count=0;
        for(unsigned i=0;i<kIndices;++i){auto &s=slots[i];if(!s.id.car)continue;
            if(incidents.cops[i].wrecked){if(s.active)release(s,w);eligible[i]=false;obs[i].usable=obs[i].selected=0;continue;}
            float up=1;if(eligible[i] && read(s.id.car+0xb8,up)){
                const auto result=copchase_incidents_rollover(&incidents,&incident_config,i,incident_identity(s),
                    state.output.phase!=COPCHASE_IDLE,s.active&&!s.held,0,up);
                if(result.taken_out_slot>=0)status.pending_incident_events|=COPCHASE_INCIDENT_ROLLOVER;
                incident_result(result);
            }
        }
        if(pending_ram>=0 && (slots[unsigned(pending_ram)].serial!=pending_ram_serial || !slots[unsigned(pending_ram)].id.car))pending_ram=-1;
        if(!markers_sampled || uint32_t(now_ms()-markers_ms)>2000){
            CopChaseMarkerReader reader{this,road_read};copchase_markers_collect(&reader,&markers);markers_ms=now_ms();markers_sampled=true;}
    }
    bool hold_hook(Slot &s,const Pose &cop){
        float target[2]={cop.xy.position[0]+cop.xy.forward[0]*5,cop.xy.position[1]+cop.xy.forward[1]*5};
        const float speed=0;const uint32_t zero=0;
        const bool ok=write(s.id.control+0x224,target) && write(s.id.control+0x230,speed) && write(s.id.driver+0x734,zero);
        if(ok){std::memcpy(s.plan.target,target,8);s.plan.speed=0;s.planned=true;}
        else{s.pending=true;s.planned=false;++status.failures;}return ok;
    }
    bool hook(uint32_t control,World &w,Slot *&slot,Pose &cop,Pose &target){
        if(!ready || busy || release_pending || fault)return false;
        CopChaseGameGuards g;
        if(!guards(g) || !g.assets_ready || !context(g) || !g.world_generation ||
           g.world_generation!=world_generation || g.player_generation!=player_generation || g.pursuit_race_token!=race_token){release_pending=true;ready=false;return false;}
        if(g.paused || g.settlement_pending || !idle(g))return false;
        if(!world_read(w) || w.address!=world || w.player!=player){release_pending=true;ready=false;return false;}
        for(auto &s:slots)if(s.active && s.id.control==control){
            slot=&s;if(s.pending || incidents.cops[s.id.index].wrecked || !live(w,s) || !enabled_car(s) || !goal(s,s.held?7u:5u,s.held?0u:player) ||
                !pose(s.id.car,cop) || !pose(player,target)){s.pending=true;s.planned=false;return false;}
            target.xy.velocity[0]=filtered[0];target.xy.velocity[1]=filtered[1];return true;
        }
        return false;
    }
};

extern "C" CopChaseGameConfig copchase_game_defaults(void){
    CopChaseGameConfig c{};c.pursuit=copchase_config_defaults();c.pursuit.enabled=0;c.maximum_vehicles=10;
    c.aggression_percent=100;c.catchup_speed=50;c.direct_range=110;c.maximum_speed=75;c.speed_bonus=14;c.random_seed=1;
    c.roadblocks_enabled=c.spikes_enabled=c.heat_models=1;c.roadblock_chance_multiplier=1;return c;
}
extern "C" CopChaseGame *copchase_game_create(const CopChaseGameHost *host){
    if(!host || !host->read || !host->write || !host->call || !host->alloc || !host->free || !host->guards)return nullptr;
    auto *g=new(std::nothrow) CopChaseGame;if(!g)return nullptr;g->host=*host;g->config=copchase_game_defaults();
    g->applied=g->config;copchase_state_init(&g->state);copchase_tactics_init(&g->tactics_state);
    copchase_incidents_init(&g->incidents);
    g->roads=copchase_roads_create();copchase_roads_reset(g->roads,g->road_generation);
    g->status.blocked=COPCHASE_GAME_DISABLED;g->publish();return g;
}
extern "C" void copchase_game_configure(CopChaseGame *g,const CopChaseGameConfig *config){
    if(!g || !config)return;auto c=*config;copchase_config_validate(&c.pursuit);
    c.maximum_vehicles=std::clamp(c.maximum_vehicles,1,int(kMaximum));c.aggression_percent=std::clamp(c.aggression_percent,50,200);
    c.catchup_speed=bounded(c.catchup_speed,0,150,50);c.direct_range=bounded(c.direct_range,1,500,110);
    c.maximum_speed=bounded(c.maximum_speed,1,150,75);c.speed_bonus=bounded(c.speed_bonus,0,50,14);if(!c.random_seed)c.random_seed=1;
    c.retained_heat=bounded(c.retained_heat,0,5,0);
    c.roadblocks_enabled=!!c.roadblocks_enabled;c.spikes_enabled=!!c.spikes_enabled;c.heavy_roadblocks=!!c.heavy_roadblocks;
    c.spikes_visible=!!c.spikes_visible;
    c.heat_models=!!c.heat_models;
    c.roadblock_chance_multiplier=bounded(c.roadblock_chance_multiplier,0,10,1);
    std::lock_guard<std::mutex> lock(g->mutex);g->config=c;
}
extern "C" void copchase_game_frame(CopChaseGame *g,float dt){
    if(!g || g->busy)return;g->busy=true;g->ready=false;
    const int previous_style=g->applied.pursuit.style;
    {std::lock_guard<std::mutex> lock(g->mutex);g->applied=g->config;}
    CopChaseGameGuards guards{};World w{};Pose player{};
    const bool guarded=g->guards(guards),world_ok=guarded && g->world_read(w);
    if(!guarded || !guards.world_generation || (g->world_generation &&
       (guards.world_generation!=g->world_generation || !world_ok || w.address!=g->world))){
        g->cancel();g->clear_identity();
    }
    const bool player_ok=world_ok && w.player && guards.player_generation && g->pose(w.player,player);
    const bool context_ok=guarded && g->context(guards),idle=guarded && g->idle(guards);
    if(context_ok && guards.pursuit_race_token){
        g->applied.pursuit.enabled=1;g->applied.pursuit.start_heat=guards.pursuit_race_start_heat;
        g->applied.pursuit.style=guards.pursuit_race_style;g->applied.retained_heat=0;
        copchase_config_validate(&g->applied.pursuit);
    }
    const bool permitted=g->applied.pursuit.enabled && guards.assets_ready && context_ok && player_ok;
    const bool player_changed=g->player && (w.player!=g->player || guards.player_generation!=g->player_generation);
    const bool race_changed=g->world && g->race_token!=guards.pursuit_race_token;
    const bool preset_changed=g->world && previous_style!=g->applied.pursuit.style;
    if(!permitted || player_changed || race_changed || preset_changed || g->release_pending || g->fault){
        g->cancel();g->release_pending=true;
        if(world_ok && w.address==g->world && guards.world_generation==g->world_generation && idle){
            for(auto &s:g->slots)g->release(s,w);
            g->release_pending=std::any_of(g->slots.begin(),g->slots.end(),[](const Slot &s){return s.active;});
        }
        if(!g->world || !world_ok)g->release_pending=false;
        if((player_changed || race_changed || preset_changed) && !g->release_pending){g->clear_identity();}
    }
    g->status.registered_cars=g->status.visible_cars=g->status.unknown_cars=0;
    if(!g->applied.pursuit.enabled)g->status.blocked=COPCHASE_GAME_DISABLED;
    else if(!guards.assets_ready)g->status.blocked=COPCHASE_GAME_ASSETS;
    else if(!context_ok)g->status.blocked=COPCHASE_GAME_CONTEXT;
    else if(!world_ok || !player_ok || !guards.world_generation)g->status.blocked=COPCHASE_GAME_IDENTITY;
    else if(g->fault)g->status.blocked=COPCHASE_GAME_FAULT;
    else if(guards.settlement_pending)g->status.blocked=COPCHASE_GAME_SETTLEMENT;
    else if(guards.paused)g->status.blocked=COPCHASE_GAME_PAUSED;
    else if(!idle)g->status.blocked=COPCHASE_GAME_STREAMING;
    else if(!std::isfinite(dt) || dt<=0 || dt>.25f)g->status.blocked=COPCHASE_GAME_TIME;
    else g->status.blocked=COPCHASE_GAME_READY;
    if(g->status.blocked!=COPCHASE_GAME_READY || g->release_pending){
        g->status.collision_dropped+=g->collision_count;g->collision_count=0;
        for(auto &s:g->slots){s.planned=false;s.los_valid=false;}g->status.tyre_scrape=0;
        if(guards.settlement_pending){g->sampled=false;g->sample_seconds=0;g->pending_ram=-1;}
        if(!idle){g->sampled=false;g->sample_seconds=0;g->filtered[0]=g->filtered[1]=0;}
        g->publish();g->busy=false;return;
    }
    if(!g->world){g->world=w.address;g->world_generation=guards.world_generation;g->player=w.player;
        g->player_generation=guards.player_generation;g->race_token=guards.pursuit_race_token;g->random=g->applied.random_seed;}
    g->last_dt=dt;g->seconds+=dt;g->sample_seconds+=dt;
    if(!g->sampled){std::memcpy(g->last_position,player.xy.position,sizeof g->last_position);g->sample_seconds=0;g->sampled=true;}
    else if(g->sample_seconds>=.1){
        const float vx=float((player.xy.position[0]-g->last_position[0])/g->sample_seconds);
        const float vy=float((player.xy.position[1]-g->last_position[1])/g->sample_seconds);
        if(vx*vx+vy*vy<22500.f){g->filtered[0]=(g->filtered[0]+vx)*.5f;g->filtered[1]=(g->filtered[1]+vy)*.5f;}
        std::memcpy(g->last_position,player.xy.position,sizeof g->last_position);g->sample_seconds=0;
    }
    CopChaseObservation obs[kIndices]{};Pose poses[kIndices]{};bool eligible[kIndices]{};bool complete_scan=true;
    for(unsigned i=0;i<kIndices;++i){auto &s=g->slots[i];Identity id;
        if(!g->identity(w,i,id)){g->abandon(s);continue;}
        if(!same(s.id,id)){g->abandon(s);s.id=id;s.contact_ms=g->now_ms()-100000u;
            if(!++g->identity_serial)++g->identity_serial;s.serial=g->identity_serial;
            uint16_t samples[4];for(auto &v:samples)v=uint16_t(g->rand32()%1000);s.personality=copchase_personality(samples);
            s.preferred_side=copchase_tactics_preferred_side(i,1,g->rand32());}
        copchase_incidents_bind(&g->incidents,i,g->incident_identity(s));
        if(g->incidents.cops[i].wrecked){if(s.active)g->release(s,w);continue;}
        if(!g->enabled_car(s)){if(s.active)g->release(s,w);continue;}
        if(!g->pose(s.id.car,poses[i])){if(s.active)g->release(s,w);complete_scan=false;
            obs[i]={1,0,0,COPCHASE_UNKNOWN,0,0};++g->status.unknown_cars;continue;}
        if(s.active && (s.pending || !g->goal(s,s.held?7u:5u,s.held?0u:g->player))){g->release(s,w);continue;}
        ++g->status.registered_cars;eligible[i]=true;
        const float dx=player.xy.position[0]-poses[i].xy.position[0],dy=player.xy.position[1]-poses[i].xy.position[1];
        const float dz=player.z-poses[i].z,d=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(d>=151){s.visibility=COPCHASE_OCCLUDED;s.los_seconds=g->seconds;s.los_valid=true;}
        obs[i]={1,s.active&&!s.held?1:0,0,COPCHASE_UNKNOWN,d,dz};s.planned=false;
        // Physical control stays in the audited one-car probe's 200m envelope.
        if(s.active && !s.held && (d>200 || std::fabs(dz)>15)){g->release(s,w);obs[i].selected=0;}
    }
    // Spread native topology work over frames with round-robin fairness. The
    // budget scales with simulation dt to refresh the census within200ms;
    // never use an expired (>250ms) ray as known visibility. Missing data or
    // a slow host therefore holds the state instead of fabricating sightings.
    unsigned budget=std::max(1u,unsigned(std::ceil(g->status.registered_cars*dt/.2f)));
    const unsigned cursor=g->los_cursor;
    for(unsigned n=0;n<kIndices && budget && !g->fault;++n){unsigned i=(cursor+n)%kIndices;auto &s=g->slots[i];
        if(!eligible[i] || obs[i].distance>=151 || (s.los_valid && g->seconds-s.los_seconds<.25))continue;
        s.visibility=g->los(poses[i],player);s.los_seconds=g->seconds;s.los_valid=true;
        g->los_cursor=(i+1)%kIndices;--budget;
    }
    for(unsigned i=0;i<kIndices;++i)if(eligible[i]){const auto &s=g->slots[i];
        obs[i].visibility=s.los_valid && g->seconds-s.los_seconds<.25?s.visibility:COPCHASE_UNKNOWN;
        if(obs[i].visibility==COPCHASE_VISIBLE)++g->status.visible_cars;
        if(obs[i].visibility==COPCHASE_UNKNOWN)++g->status.unknown_cars;
    }
    g->incidents_frame(w,player,poses,eligible,obs);
    CopChaseInput input{};input.world_generation=g->world_generation;input.player_generation=g->player_generation;
    input.valid_world=input.valid_player=input.allowed_mode=input.driving=1;input.complete_cop_scan=complete_scan;
    input.seconds=dt;input.speed=std::hypot(g->filtered[0],g->filtered[1]);input.cops=obs;input.cop_count=kIndices;
    input.retained_heat=g->applied.retained_heat;
    input.rammed_police=g->pending_ram>=0;input.incident_bounty=g->incidents.incident_bounty;
    const bool pursuit_race_start=g->race_token && guards.pursuit_race_force_start;
    float nearest_marker=0;input.hiding_spot=copchase_markers_hiding(&g->markers,player.xy.position,12,&nearest_marker);
    g->road_census();
    auto out=copchase_state_step(&g->state,&g->applied.pursuit,&input);g->status.pending_events|=out.events;
    if(out.events&COPCHASE_STARTED){copchase_tactics_init(&g->tactics_state);if(!++g->pursuit_epoch)++g->pursuit_epoch;
        copchase_incidents_begin(&g->incidents);g->pending_ram=-1;}
    g->status.pending_incident_events|=copchase_incidents_tick(&g->incidents,out.phase!=COPCHASE_IDLE,0,input.speed,out.busted);
    g->roads_frame(w,player,poses,eligible,dt);
    if(out.phase!=COPCHASE_PURSUIT || g->fault){for(auto &s:g->slots)g->release(s,w);
        copchase_tactics_init(&g->tactics_state);g->tactics={};}
    else{
        unsigned active=0,limit=unsigned(std::min(out.max_chasers,g->applied.maximum_vehicles));
        for(auto &s:g->slots)if(s.active&&!s.held){if(active>=limit)g->release(s,w);else ++active;}
        std::array<unsigned,kIndices> order{};for(unsigned i=0;i<kIndices;++i)order[i]=i;
        std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return obs[a].distance<obs[b].distance;});
        for(unsigned i:order){auto &s=g->slots[i];if(active>=limit || g->fault)break;
            if(!eligible[i] || !g->heat_available(s) || s.active ||
                (obs[i].visibility==COPCHASE_UNKNOWN && !(pursuit_race_start && (out.events&COPCHASE_STARTED))) || obs[i].distance>200 ||
                std::fabs(obs[i].vertical_distance)>15)continue;
            if(g->activate(s,w))++active;
        }
        CopChaseTacticsCop cars[kMaximum]{};unsigned count=0;
        for(unsigned i:order)if(g->slots[i].active && !g->slots[i].held && count<kMaximum){
            const auto &cop=poses[i].xy;const float dx=cop.position[0]-player.xy.position[0],dy=cop.position[1]-player.xy.position[1];
            cars[count++]={i,obs[i].distance,dx*player.xy.forward[0]+dy*player.xy.forward[1],
                cop.forward[0]*player.xy.forward[0]+cop.forward[1]*player.xy.forward[1],
                dx*player.xy.left[0]+dy*player.xy.left[1],g->slots[i].personality.aggression,g->slots[i].preferred_side,0};
        }
        auto tc=copchase_tactics_defaults();tc.preset=g->applied.pursuit.style;tc.heat=out.heat;tc.aggression_percent=g->applied.aggression_percent;
        const uint32_t saved_random=g->random;
        CopChaseTacticsInput ti{g->now_ms(),input.speed,cars,count,uint16_t(g->rand32()%1000)};
        if(!copchase_tactics_step(&g->tactics_state,&tc,&ti,&g->tactics)){g->tactics={};++g->status.failures;}
        if(!g->tactics.sample_used)g->random=saved_random;
        for(unsigned i:order)if(g->slots[i].active && !g->slots[i].held){auto &s=g->slots[i];
            const auto &cop=poses[i].xy;float vertical=0;uint32_t mode=0;
            if(!g->read(s.id.car+0x78,vertical) || !finite(vertical,200) || !g->read(s.id.car+0x28,mode))continue;
            const float dx=player.xy.position[0]-cop.position[0],dy=player.xy.position[1]-cop.position[1];
            CopChaseRecoveryInput ri{};ri.now_ms=g->now_ms();ri.last_relocated_ms=ri.now_ms-100000u;
            ri.distance=obs[i].distance;ri.cop_speed=std::sqrt(cop.velocity[0]*cop.velocity[0]+cop.velocity[1]*cop.velocity[1]+vertical*vertical);
            ri.player_speed=input.speed;ri.target_alignment=(dx*cop.forward[0]+dy*cop.forward[1])/(ri.distance+.01f);
            ri.physics_mode=int(mode);ri.reverse_enabled=1;ri.arrest_phase=out.busted>0;
            CopChaseRecoveryOutput recovery{};auto dc=g->driver_config();
            if(!copchase_driver_recovery(&s.recovery,&dc,&ri,&recovery))continue;
            if(recovery.reverse_started)++g->status.reversals;
            if(recovery.replan){
                bool ok=g->call(0x37550,s.id.driver,1,&g->player);World refreshed;
                ok=ok && g->world_read(refreshed) && g->live(refreshed,s) && g->goal(s,5,g->player);
                if(ok)++g->status.replans;else{s.pending=true;s.planned=false;++g->status.failures;}
            }
        }
    }
    g->models_frame(w,player,poses,eligible);
    if(g->fault){g->status.blocked=COPCHASE_GAME_FAULT;
        g->release_pending=std::any_of(g->slots.begin(),g->slots.end(),[](const Slot &s){return s.active;});}
    g->ready=!g->fault && out.phase==COPCHASE_PURSUIT;g->publish();g->busy=false;
}
extern "C" int copchase_game_driver(CopChaseGame *g,uint32_t control){
    if(!g)return 0;World w;Slot *s=nullptr;Pose cop,target;if(!g->hook(control,w,s,cop,target))return 0;
    if(s->held){const bool ok=g->hold_hook(*s,cop);if(ok)++g->status.driver_overrides;return ok;}
    CopChaseContactOutput contact{};
    if(!copchase_contact_update(&cop.xy,&target.xy,g->now_ms(),s->contact_ms,&contact))return 0;
    s->contact_ms=contact.last_contact_ms;if(contact.refreshed)++g->status.contacts;
    auto input=copchase_steering_defaults();input.preset=g->applied.pursuit.style;input.heat=g->state.output.heat;
    input.aggression_percent=g->applied.aggression_percent;input.personality=s->personality;
    const unsigned slot=s->id.index;input.role=g->tactics.role[slot];
    if(g->tactics.formation && g->tactics.member[slot]>=0){input.formation=g->tactics.formation;
        input.formation_slot=g->tactics.member[slot];input.formation_side=g->tactics.side;input.formation_pressure=g->tactics.pressure;}
    input.since_contact_ms=float(std::min(contact.elapsed_ms,1000000000u));
    input.smoothed_speed=std::hypot(g->filtered[0],g->filtered[1]);input.catchup_speed=g->applied.catchup_speed;
    input.direct_range=g->applied.direct_range;input.maximum_speed=g->applied.maximum_speed;input.speed_bonus=g->applied.speed_bonus;
    if(!copchase_steering(&input,&cop.xy,&target.xy,&s->plan)){s->planned=false;return 0;}
    CopChaseDriverInput di{};di.now_ms=g->now_ms();di.reverse_until_ms=s->recovery.reverse_until_ms;
    di.reverse_started_ms=s->recovery.reverse_started_ms;di.contact_elapsed_ms=contact.elapsed_ms;
    di.seconds=g->last_dt;di.cop=cop.xy;di.player=target.xy;di.planned=s->plan;
    uint32_t mode=0;auto dc=g->driver_config();CopChaseDriverOutput assist{};
    if(g->read(s->id.car+0x28,mode) && g->read(s->id.car+0x78,di.cop_vertical_velocity) &&
       g->read(s->id.car+0xb8,di.up_z) && g->read(control+0x230,di.original_road_speed)){
        di.physics_mode=int(mode);
        if(copchase_driver_assist(&dc,&di,&assist)){
            s->plan=assist.control;s->recovery.reverse_until_ms=assist.reverse_until_ms;
            if(assist.apply_velocity && g->impulse(*s,assist.velocity_delta))++g->status.impulses;
        }
    }
    s->planned=!s->plan.use_road_path;
    bool ok=true;if(s->planned)ok=g->write(control+0x224,s->plan.target);
    float speed=s->plan.speed,old=0;if(s->plan.use_road_path && g->read(control+0x230,old) && std::isfinite(old))speed=std::max(speed,old);
    uint32_t zero=0;ok=g->write(control+0x230,speed) && g->write(s->id.driver+0x734,zero) && ok;
    if(!ok){s->pending=true;s->planned=false;++g->status.failures;return 0;}
    ++g->status.driver_overrides;return 1;
}
extern "C" int copchase_game_heading(CopChaseGame *g,uint32_t control){
    if(!g)return 0;World w;Slot *s=nullptr;Pose cop,target;if(!g->hook(control,w,s,cop,target) || !s->planned)return 0;
    // ASI 1001c4f0 restores XY only after native path update, not desired speed.
    if(!g->write(control+0x224,s->plan.target)){s->pending=true;s->planned=false;++g->status.failures;return 0;}
    ++g->status.heading_overrides;return 1;
}
extern "C" int copchase_game_collision(CopChaseGame *g,uint32_t body,const float raw[3]){
    return g&&g->collision_capture(body,raw);
}
extern "C" void copchase_game_snapshot(CopChaseGame *g,CopChaseGameStatus *out,int take){
    if(!g || !out)return;std::lock_guard<std::mutex> lock(g->mutex);*out=g->published;
    if(take)g->published.pending_events=g->published.pending_road_events=g->published.pending_incident_events=0;
}
extern "C" void copchase_game_world_ending(CopChaseGame *g){if(!g)return;g->cancel();g->clear_identity();
    g->status.blocked=COPCHASE_GAME_IDENTITY;g->status.registered_cars=g->status.visible_cars=g->status.unknown_cars=0;g->publish();}
extern "C" void copchase_game_car_ending(CopChaseGame *g,uint32_t car){
    if(!g || !car)return;
    for(auto &s:g->slots)if(s.id.car==car)g->abandon(s);
    for(unsigned i=0;i<g->collision_count;)if(g->collisions[i].car==car){
        g->collisions[i]=g->collisions[--g->collision_count];++g->status.collision_dropped;
    }else ++i;
    if(g->player==car){g->ready=false;g->release_pending=true;}
    g->status.registered_cars=g->status.visible_cars=g->status.unknown_cars=0;g->publish();
}
extern "C" void copchase_game_destroy(CopChaseGame *g){if(!g)return;if(g->scratch)g->host.free(g->host.context,g->scratch);
    copchase_roads_destroy(g->roads);delete g;}
