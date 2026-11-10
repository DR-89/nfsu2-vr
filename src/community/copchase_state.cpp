#include "copchase_state.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
int bound(int v,int lo,int hi){return std::max(lo,std::min(hi,v));}
float bound(float v,float lo,float hi,float fallback){return std::isfinite(v)?std::max(lo,std::min(hi,v)):fallback;}
struct Heat {int chasers;float cooldown,bust_kmh,sight,hiding_bonus;};
// ASI 10061e28 + heat*30h, fields +0,+4,+8,+1c,+20.
constexpr Heat heat[5]={{4,20,9,151,4},{5,45,12,151,5},{7,75,19,151,6},
                        {8,90,20,151,7},{10,120,25,151,8}};
// ASI 10061fd2 + heat*30 + MwCareerBin*2. Values are seconds.
constexpr unsigned short escalation[4][15]={
    {120,120,120,120,120,120,120,120,120,120,120,180,240,360,600},
    {120,120,120,120,120,120,120,180,240,240,360,480,600,600,600},
    {120,120,120,120,120,180,240,300,600,600,600,600,600,600,600},
    {120,240,360,480,600,600,600,600,600,600,600,600,600,600,600}};
constexpr float score_threshold[4]={40,100,185,300};
int limit(const CopChaseConfig &c){
    if(c.style==COPCHASE_SLOW)return c.slow_max_heat;
    if(c.style==COPCHASE_MOST_WANTED || c.style==COPCHASE_ULTIMATE)
        return c.mw_career_bin<5?5:c.mw_career_bin<9?4:c.mw_career_bin<13?3:2;
    return 5;
}
float duration(const CopChaseConfig &c,int level){
    float scale=c.style==COPCHASE_ULTIMATE?1.2f:c.style==COPCHASE_INSANE?1.5f:1;
    return std::max(c.unseen_seconds+1,heat[level-1].cooldown*c.cooldown_percent*.01f*scale);
}
int chasers(const CopChaseConfig &c,int level){
    // 10003820 sets 100789e8 for Insane; 10016890 then uses the tuning
    // float at 100760a0 (default 8), instead of the heat profile count.
    int count=c.style==COPCHASE_INSANE?8:heat[level-1].chasers+(c.style==COPCHASE_ULTIMATE?2:0);
    if(c.chasers_scale && c.traffic_cars>20)count=std::max(count,int(std::sqrt(c.traffic_cars/20.f)*count+.5f));
    count=std::min(16,std::max(c.max_chasers,count));
    return c.style==COPCHASE_SLOW?std::min(c.slow_max_chasers,count):count;
}
void clear(CopChaseState &s,uint32_t events){
    std::memset(&s,0,sizeof s);s.output.heat=1;s.heat_value=1;s.output.events=events;
}
void finish(CopChaseState &s,uint32_t event){
    s.output.events|=event;s.output.phase=COPCHASE_IDLE;s.output.hiding=0;
    s.output.retained_heat=event==COPCHASE_EVADED?float(s.heat_value):0;
    s.output.cooldown=0;s.output.unseen=0;s.unseen_seconds=0;s.cooldown_value=0;
    s.grace_seconds=event==COPCHASE_BUSTED?22:10;
}
struct Seen {
    bool unknown=false,trigger=false,active=false,reacquire=false,bust=false;
    float nearest=std::numeric_limits<float>::infinity();
};
Seen observations(const CopChaseInput &in,const CopChaseConfig &c,int level,bool hiding){
    Seen s;s.unknown=!in.complete_cop_scan;
    float nearest_patrol=std::numeric_limits<float>::infinity();
    if(in.cop_count>COPCHASE_OBSERVATION_LIMIT || (in.cop_count && !in.cops)){s.unknown=true;return s;}
    for(unsigned i=0;i<in.cop_count;++i){
        const auto &o=in.cops[i];if(!o.usable)continue;
        if(!std::isfinite(o.distance) || o.distance<0 || !std::isfinite(o.vertical_distance) ||
           o.visibility<COPCHASE_UNKNOWN || o.visibility>COPCHASE_VISIBLE){s.unknown=true;continue;}
        if(o.visibility==COPCHASE_UNKNOWN){s.unknown=true;continue;}
        bool visible=o.visibility==COPCHASE_VISIBLE && o.distance<heat[level-1].sight && (!hiding || o.distance<=20);
        if(o.distance<nearest_patrol){nearest_patrol=o.distance;s.trigger=visible && o.distance<c.trigger_distance;}
        if((o.selected && visible) || (o.arrest_hold && o.distance<60))s.active=true;
        // The original cooldown treats a confirmed cop within 12m as spotting
        // even when the ray is blocked. Unknown rays never take that shortcut.
        if(visible || o.distance<12)s.reacquire=true;
        if(o.selected){
            s.nearest=std::min(s.nearest,o.distance);
            if(visible && o.distance<15 && std::fabs(o.vertical_distance)<1.5f)s.bust=true;
        }
    }
    return s;
}
void advance(CopChaseState &s,const CopChaseConfig &c,const CopChaseInput &in,double dt){
    auto &o=s.output;s.world_seconds+=dt;s.grace_seconds=std::max(0.0,s.grace_seconds-dt);
    const bool hiding=c.hiding_spots && in.hiding_spot && in.speed<3;
    Seen seen=observations(in,c,o.heat,o.phase==COPCHASE_COOLDOWN && hiding);
    if(o.phase==COPCHASE_IDLE){
        if(s.world_seconds+1e-7<4 || s.grace_seconds>1e-7 || seen.unknown)return;
        if(!in.rammed_police && !(seen.trigger && in.speed>c.trigger_speed))return;
        o.phase=COPCHASE_PURSUIT;o.events|=COPCHASE_STARTED;
        int initial=c.start_heat;if(c.style==COPCHASE_SLOW)initial=std::min(initial,c.slow_max_heat);
        float retained=bound(in.retained_heat,0,5,0);
        s.heat_value=std::max(double(initial),double(retained));
        if(c.style==COPCHASE_SLOW)s.heat_value=std::min(s.heat_value,double(c.slow_max_heat));
        o.heat=bound(int(s.heat_value),1,5);o.busted=o.unseen=o.cooldown=o.heat_fraction=0;
        o.retained_heat=0;o.bounty=0;o.pursuit_seconds=o.chase_seconds=0;s.unseen_seconds=0;
        s.cooldown_value=s.busted_value=0;
        return;
    }
    o.pursuit_seconds+=dt;
    if(o.phase==COPCHASE_COOLDOWN){
        o.hiding=hiding;
        if(seen.unknown)return;
        if(seen.reacquire){
            o.phase=COPCHASE_PURSUIT;o.events|=COPCHASE_SPOTTED;o.hiding=0;
            o.unseen=0;s.unseen_seconds=0;return;
        }
        const float bonus=heat[o.heat-1].hiding_bonus;
        double speed=hiding?1+bonus*(c.style==COPCHASE_ULTIMATE?.75:c.style==COPCHASE_INSANE?.5:1):1;
        s.cooldown_value=std::min(1.0,s.cooldown_value+dt*speed/duration(c,o.heat));
        o.cooldown=float(s.cooldown_value);
        if(o.cooldown>=1-1e-6)finish(s,COPCHASE_EVADED);
        return;
    }
    o.chase_seconds+=dt;
    const uint64_t bounty=uint64_t(in.incident_bounty)+uint64_t(o.chase_seconds/10)*1000;
    o.bounty=uint32_t(std::min<uint64_t>(bounty,UINT32_MAX));
    const int ceiling=std::max(o.heat,limit(c));
    int next=o.heat;
    if(c.style==COPCHASE_MOST_WANTED || c.style==COPCHASE_ULTIMATE){
        if(o.heat<ceiling)s.heat_value+=dt*(c.style==COPCHASE_ULTIMATE?1.5:1)/escalation[o.heat-1][c.mw_career_bin];
        next=std::min(ceiling,int(s.heat_value+1e-9));
    }else{
        double score=o.chase_seconds+o.bounty/800.0;
        next=1;while(next<ceiling && score>=score_threshold[next-1])++next;
        next=std::max(o.heat,next);s.heat_value=next;
        if(next<ceiling){
            double low=next==1?0:score_threshold[next-2];
            s.heat_value+=std::max(0.0,std::min(1.0,(score-low)/(score_threshold[next-1]-low)));
        }
    }
    next=bound(next,1,5);if(next>o.heat){o.heat=next;o.events|=COPCHASE_HEAT_CHANGED;}
    o.heat_fraction=o.heat>=ceiling?1:float(s.heat_value-int(s.heat_value));
    if(seen.unknown)return;
    bool bust=seen.bust && in.speed<heat[o.heat-1].bust_kmh/3.6f;
    if(c.style==COPCHASE_INSANE){
        bust=seen.nearest<35 && in.speed<heat[o.heat-1].bust_kmh*.447f && o.pursuit_seconds>5;
        s.busted_value+=bust && !c.disable_busted?dt/(in.speed<3?3.5:8):
                         -dt/(in.speed<=20 && seen.nearest<=60?20:6);
    }else s.busted_value+=bust && !c.disable_busted?dt/5:-dt*.4;
    s.busted_value=std::max(0.0,std::min(1.0,s.busted_value));o.busted=float(s.busted_value);
    if(o.pursuit_seconds<5 || seen.active)s.unseen_seconds=0;
    else s.unseen_seconds+=dt;
    o.unseen=float(s.unseen_seconds/c.unseen_seconds);
    s.cooldown_value=std::max(0.0,s.cooldown_value-dt/10);o.cooldown=float(s.cooldown_value);
    if(o.busted>=1-1e-6){finish(s,COPCHASE_BUSTED);return;}
    if(o.unseen>=1-1e-6){
        o.phase=COPCHASE_COOLDOWN;o.events|=COPCHASE_LOST_SIGHT;
        o.busted=o.unseen=0;o.hiding=0;s.busted_value=0;
        s.cooldown_value=std::max(s.cooldown_value,double(c.unseen_seconds)/duration(c,o.heat));
        o.cooldown=float(s.cooldown_value);
    }
}
}
extern "C" CopChaseConfig copchase_config_defaults(void){
    return {1,COPCHASE_MOST_WANTED,1,2,3,4,4,20,1,0,1,60,30,7,100};
}
extern "C" void copchase_config_validate(CopChaseConfig *c){
    if(!c)return;
    c->enabled=c->enabled!=0;c->style=bound(c->style,0,3);c->start_heat=bound(c->start_heat,1,5);
    c->slow_max_heat=bound(c->slow_max_heat,1,5);c->slow_max_chasers=bound(c->slow_max_chasers,1,8);
    c->mw_career_bin=bound(c->mw_career_bin,0,14);c->max_chasers=bound(c->max_chasers,1,16);
    c->traffic_cars=bound(c->traffic_cars,0,100);c->chasers_scale=c->chasers_scale!=0;
    c->disable_busted=c->disable_busted!=0;c->hiding_spots=c->hiding_spots!=0;
    c->trigger_distance=bound(c->trigger_distance,1,300,60);c->trigger_speed=bound(c->trigger_speed,0,100,30);
    c->unseen_seconds=bound(c->unseen_seconds,1,120,7);
    c->cooldown_percent=bound(c->cooldown_percent,10,1000,100);
}
extern "C" void copchase_state_init(CopChaseState *s){if(s)clear(*s,0);}
extern "C" CopChaseOutput copchase_state_step(CopChaseState *s,const CopChaseConfig *cfg,const CopChaseInput *in){
    if(!s)return {};s->output.events=0;
    CopChaseConfig c=cfg?*cfg:copchase_config_defaults();copchase_config_validate(&c);
    if(!in || !c.enabled || !in->valid_world || !in->valid_player || !in->allowed_mode ||
       !in->world_generation || !in->player_generation){
        clear(*s,s->output.phase==COPCHASE_IDLE?0:COPCHASE_CANCELLED);return s->output;
    }
    if(!s->identity_valid || s->world_generation!=in->world_generation || s->player_generation!=in->player_generation){
        clear(*s,s->output.phase==COPCHASE_IDLE?0:COPCHASE_CANCELLED);
        s->world_generation=in->world_generation;s->player_generation=in->player_generation;s->identity_valid=1;
    }
    if(!in->paused && in->driving && std::isfinite(in->seconds) && in->seconds>0 &&
       std::isfinite(in->speed) && in->speed>=0){
        double remain=std::min(double(in->seconds),.25);
        while(remain>1e-8){double dt=std::min(remain,.1);advance(*s,c,*in,dt);remain-=dt;}
    }
    s->output.max_chasers=chasers(c,s->output.heat);return s->output;
}
