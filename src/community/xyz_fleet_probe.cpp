// Explicit local stress probe. No socket, account, server or save writes.
#include "xyz_fleet.h"
#include "xyz_xbox_pool.h"
#include "xyz_damage.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
static int player_textures(const XyzXboxGuest &g,const XyzXboxFrame &sample) {
    auto word=[&](uint32_t a,uint32_t &v){return xyz_xbox_address(a,4) && g.read(g.context,a,&v,4);};
    uint32_t type,at,previous=0x45c4f8;unsigned total=0;uint8_t skin[12];
    if(!word(sample.ride,type) || !word(previous,at) ||
       !xyz_xbox_address(sample.ride+0x584,sizeof skin) || !g.read(g.context,sample.ride+0x584,skin,sizeof skin))return -1;
    for(unsigned n=0;n<256;++n) {
        if(at==0x45c4f8)return int(total);
        uint32_t next,back,refs,kind,models,count;
        if(!xyz_xbox_address(at,0x1240) || !word(at,next) || !word(at+4,back) || back!=previous ||
           !word(at+8,refs) || !word(at+0x20,kind) || !word(at+0x95c,models) ||
           !word(models+0x318,count) || count>69)return -1;
        uint8_t other[12];
        if(refs && kind==type && g.read(g.context,at+0x20+0x584,other,sizeof other) &&
           !std::memcmp(skin,other,sizeof skin))total+=count;
        previous=at;at=next;
    }
    return -1;
}
void xyz_fleet_probe(xyz::Fleet &fleet,const XyzXboxGuest *g,const XyzXboxFrame *sample,uint64_t now) {
    static bool started=false,finished=false;
    static uint64_t start,last;
    static uint32_t world;
    static float position[3];
    static std::vector<std::vector<uint8_t>> configs;
    static unsigned ready_frames,failures,changes,last_phase=~0u;
    static unsigned cars=xyz::Client::car_limit,phase_seconds=12,duration=72;
    if(!g || !sample || finished)return;
    if(!started) {
        if(sample->status!=XYZ_XBOX_READY && sample->status!=XYZ_XBOX_OTHER_RACE)return;
        const char *requested=std::getenv("NFSU2_XYZ_FLEET_CARS");
        if(requested){char *end=nullptr;unsigned long n=std::strtoul(requested,&end,10);
            if(end && !*end && n>=1 && n<=XYZ_CARS_MAX)cars=unsigned(n);}
        fleet.set_limit(cars);
        if(cars>6){phase_seconds=60;duration=240;}
        uint32_t types=0;rc_carcfg car{};
        if(rc_carcfg_decode(sample->config,sample->config_size,&car)!=RC_OK ||
           !g->read(g->context,0x45af88,&types,4) || !xyz_xbox_address(types,46*0x890))return;
        const char *wanted[]={"SKYLINE","SUPRA","HUMMER","ESCALADE","RX7","RX8","3000GT","GTO"};
        const char *regional=std::getenv("NFSU2_XYZ_FLEET_REGIONAL_PROBE");
        if(regional && *regional=='1'){wanted[0]="PEUGOT106";wanted[1]="CORSA";wanted[4]="PEUGOT106";wanted[5]="CORSA";}
        for(auto name:wanted)for(unsigned i=0;i<46;++i) {
            char actual[32];uint32_t hash;
            if(!g->read(g->context,types+i*0x890,actual,sizeof actual) || std::strncmp(actual,name,32) ||
               !g->read(g->context,types+i*0x890+0xd0,&hash,4))continue;
            std::snprintf(car.type_name,sizeof car.type_name,"%s",name);car.type_hash=hash;car.n_parts=0;
            const char *stance=std::getenv("NFSU2_STANCE_PROBE");
            if(stance && *stance=='1'){car.has_stance=1;car.stance_fc=75;car.stance_rc=125;car.stance_fo=30;car.stance_ro=50;car.stance_tw=110;}
            std::vector<uint8_t> wire(RC_CFG_MAX);int n=rc_carcfg_encode(&car,wire.data(),int(wire.size()));
            if(n>0){wire.resize(size_t(n));configs.push_back(std::move(wire));}break;
        }
        started=true;start=now;world=sample->world;std::memcpy(position,sample->state.p,sizeof position);
        if(configs.size()<2){std::fprintf(stderr,"[XYZ fleet probe] insufficient model fixtures\n");finished=true;return;}
        std::fprintf(stderr,"[XYZ fleet probe] started models=%zu cars=%u duration=%u seconds; local only\n",configs.size(),cars,duration);
    }
    float t=float(now-start)*.000001f;
    xyz::ClientFrame remote;
    XyzXboxFrame local=*sample;local.status=XYZ_XBOX_READY;
    unsigned phase=unsigned(t)/phase_seconds;
    if(t<duration && sample->world==world) {
        remote.generation=1;
        for(unsigned i=0;i<cars;++i) {
            xyz::RemotePlayer p;p.id=i+1;p.ip=0x0a4d0102+i;p.config=configs[(phase*2+i)%configs.size()];p.has_pose=true;
            float angle=t*.3f+i*6.283185f/cars;
            float radius=cars>6?30.f:6.f;
            p.pose.p[0]=position[0]+radius*std::cos(angle);
            p.pose.p[1]=position[1]+radius*std::sin(angle);p.pose.p[2]=position[2];
            p.pose.q[3]=1;p.pose.mode=RC_MODE_INTERP;p.pose.rpm=1500;p.pose.gear=1;
            const char *damage=std::getenv("NFSU2_DAMAGE_FLEET_PROBE");
            if(damage && *damage=='1' && xyz_damage_enabled()){
                p.dents_epoch=uint8_t(1+phase);
                if(phase%3!=2){
                    // One valid wire-format front dent; a second side dent
                    // in the next phase, followed by a repair. Local QA only.
                    const uint8_t front[RC_DMG_BYTES]={0x08,0x07,0,0,0x58,0x02,0x81,0,0,255,57};
                    const uint8_t side[RC_DMG_BYTES]={0x90,0x01,0x84,0x03,0x58,0x02,0,0x81,0,255,57};
                    p.dents.assign(front,front+RC_DMG_BYTES);
                    if(phase%3==1)p.dents.insert(p.dents.end(),side,side+RC_DMG_BYTES);
                }
            }
            remote.players.push_back(std::move(p));
        }
    }
    auto result=fleet.frame(*g,local,remote,now);
    ready_frames+=result.visible==cars;failures+=result.failed;
    if(phase!=last_phase){last_phase=phase;++changes;}
    if(!last || now-last>=5000000 || remote.players.empty()) {
        XyzXboxPool pool{};int valid=xyz_xbox_pool_read(g->read,g->context,&pool);
        std::fprintf(stderr,"[XYZ fleet probe] t=%.1f phase=%u visible=%u loading=%u failed=%u pool_valid=%d size=%u used=%u largest=%u rides=%u/%u player_textures=%d\n",
            t,phase,result.visible,result.loading,result.failed,valid,pool.size,pool.used,pool.largest,pool.rides,pool.capacity,player_textures(*g,*sample));last=now;
        if(xyz_damage_enabled()){XyzDamageStats st{};xyz_damage_stats(&st);
            std::fprintf(stderr,"[XYZ damage fleet] t=%.1f clones=%u retired=%u cars=%u failures=%u drawn=%u overlays=%u loose=%u fallen=%u debris=%u\n",t,st.clones,st.retired,st.cars,st.failures,st.drawn,st.overlays,st.loose,st.fallen,st.debris);}
    }
    if(remote.players.empty()) {
        finished=true;
        std::fprintf(stderr,"[XYZ fleet probe] finished ready_frames=%u failures=%u phases=%u retired=%d\n",ready_frames,failures,changes,!result.failed);
    }
}
