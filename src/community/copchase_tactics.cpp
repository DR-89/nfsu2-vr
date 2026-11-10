#include "copchase_tactics.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace {
// ASI100616c0..1006175f: weights, rolling/box duration, pressure, idle gap.
struct Profile { float weight[4],duration,pressure,gap; };
constexpr Profile profiles[]={
    {{100,0,0,0},4,.5f,7},{{0,100,0,0},5,.6f,6},
    {{20,25,60,40},6,.8f,5},{{50,50,50,30},7,.9f,4},
    {{50,50,50,50},8,1,3}
};
bool bounded(float f,float low,float high){return std::isfinite(f) && f>=low && f<=high;}
bool valid(const CopChaseTacticsState &s,const CopChaseTacticsConfig &c,const CopChaseTacticsInput &i){
    if(c.preset<0 || c.preset>3 || c.heat<1 || c.heat>5 || c.aggression_percent<50 ||
       c.aggression_percent>200 || c.formations_enabled< -1 || c.formations_enabled>1 ||
       c.formation_heat_boost< -1 || c.formation_heat_boost>4 ||
       !bounded(c.duration_multiplier,0,100) || !bounded(c.gap_multiplier,0,100) ||
       !bounded(i.speed,0,200) || i.count>COPCHASE_TACTICS_CHASERS || (!i.cops && i.count) ||
       s.formation<0 || s.formation>4 || (s.side!=1 && s.side!=-1))return false;
    bool seen[COPCHASE_TACTICS_SLOTS]{};
    for(unsigned n=0;n<i.count;n++){
        const auto &p=i.cops[n];
        if(p.slot>=COPCHASE_TACTICS_SLOTS || seen[p.slot] || !bounded(p.distance,0,100000) ||
           !bounded(p.forward,-100000,100000) || !bounded(p.lateral,-100000,100000) ||
           !bounded(p.heading_dot,-1.5f,1.5f) || !bounded(p.personality_aggression,.699f,1.401f) ||
           (p.preferred_side!=1 && p.preferred_side!=-1) || (p.force_direct!=0 && p.force_direct!=1))return false;
        seen[p.slot]=true;
    }
    for(auto member:s.member)if(member< -1 || member>=COPCHASE_TACTICS_CHASERS)return false;
    return true;
}
uint32_t milliseconds(float seconds,float multiplier){return uint32_t((seconds*1000.f)*multiplier);}
}
extern "C" CopChaseTacticsConfig copchase_tactics_defaults(){return {1,1,100,-1,-1,1,1};}
extern "C" void copchase_tactics_init(CopChaseTacticsState *s){
    if(!s)return;
    *s={};s->side=1;std::memset(s->member,-1,sizeof s->member);
}
extern "C" int copchase_tactics_preferred_side(unsigned slot,int varied,uint32_t sample){
    return ((varied?sample:slot)&1u)?1:-1;
}
extern "C" int copchase_tactics_step(CopChaseTacticsState *s,const CopChaseTacticsConfig *cfg,
                                    const CopChaseTacticsInput *in,CopChaseTacticsOutput *out){
    if(!out)return 0;
    *out={};std::memset(out->member,-1,sizeof out->member);
    if(!s || !cfg || !in || !valid(*s,*cfg,*in))return 0;
    const auto &c=*cfg;const auto &i=*in;
    const int boost=c.formation_heat_boost<0?(c.preset==3?4:0):c.formation_heat_boost;
    const auto &profile=profiles[std::clamp(c.heat+boost,1,5)-1];
    const int steering_heat=std::clamp(c.heat+boost+(c.preset==2),1,5);
    const float aggression=float(c.aggression_percent)/100.f;
    bool head_on=steering_heat>=4;
    if(aggression<.75f)head_on=false;
    else if(aggression>=1.5f && steering_heat>=3)head_on=true;
    int direct=-1;float nearest=1e9f;
    for(unsigned n=0;n<i.count;n++)if(i.cops[n].forward<25 && i.cops[n].distance<nearest){
        direct=int(n);nearest=i.cops[n].distance;
    }
    if(direct<0)for(unsigned n=0;n<i.count;n++)if(i.cops[n].distance<nearest){
        direct=int(n);nearest=i.cops[n].distance;
    }
    unsigned sides[2]{};
    for(unsigned n=0;n<i.count;n++){
        const auto &p=i.cops[n];
        if(int(n)==direct || p.force_direct || (p.forward>25 && p.heading_dot<-.3f && head_on))continue;
        if(c.heat>=2 && p.forward>25 && p.heading_dot>.5f){out->role[p.slot]=3;continue;}
        if(p.personality_aggression>1.15f && c.heat>=3 && p.forward<10 && p.distance<40)continue;
        unsigned side=p.preferred_side<=0?1u:0u;
        if(sides[side^1u]<sides[side])side^=1u;
        ++sides[side];out->role[p.slot]=uint8_t(side+1);
    }
    const bool enabled=c.formations_enabled<0?c.preset!=0:c.formations_enabled!=0;
    if(enabled){
        if(s->formation && (i.now_ms>s->ends_ms || i.speed<3)){
            s->formation=0;std::memset(s->member,-1,sizeof s->member);
            s->next_ms=i.now_ms+milliseconds(profile.gap,c.gap_multiplier);out->ended=1;
        }
        if(!s->formation && i.now_ms>s->next_ms && i.speed>8){
            unsigned nearby=0;
            for(unsigned n=0;n<i.count;n++)if(i.cops[n].distance<70)++nearby;
            float sum=0;for(float weight:profile.weight)sum+=weight;
            if(nearby<2 || sum<=0)s->next_ms=i.now_ms+1000;
            else{
                float sample=(float(i.formation_sample%1000)/1000.f)*sum;
                int formation=1;
                while(formation<4 && !(sample<profile.weight[formation-1])){
                    sample-=profile.weight[formation-1];++formation;
                }
                const unsigned wanted=formation==1?3u:formation==2?4u:formation==3?1u:3u;
                std::memset(s->member,-1,sizeof s->member);
                int lead=-1;
                for(unsigned rank=0;rank<std::min(wanted,nearby);rank++){
                    int best=-1;float closest=1e9f;
                    for(unsigned n=0;n<i.count;n++){
                        const auto &p=i.cops[n];
                        if(p.distance<70 && s->member[p.slot]<0 && p.distance<closest){best=int(n);closest=p.distance;}
                    }
                    if(best>=0){s->member[i.cops[best].slot]=int8_t(rank);if(!rank)lead=best;}
                }
                s->side=lead<0 || i.cops[lead].lateral>=0?1:-1;
                s->formation=formation;
                s->ends_ms=i.now_ms+milliseconds(formation<=2?profile.duration:5.f,c.duration_multiplier);
                out->started=1;out->sample_used=1;
            }
        }
    }
    out->formation=s->formation;out->side=s->side;out->pressure=profile.pressure;
    std::memcpy(out->member,s->member,sizeof s->member);
    return 1;
}
