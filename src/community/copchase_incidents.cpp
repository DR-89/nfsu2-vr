#include "copchase_incidents.h"
#include <algorithm>
#include <cmath>
namespace {
bool vec(const float *v,unsigned n){for(unsigned i=0;i<n;++i)if(!std::isfinite(v[i]) || std::fabs(v[i])>1e7f)return false;return true;}
bool config(const CopChaseIncidentConfig &c){return (c.cop_damage==0 || c.cop_damage==1) &&
    std::isfinite(c.damage_threshold) && c.damage_threshold>=0 && c.damage_threshold<=1000 &&
    std::isfinite(c.damage_scale) && c.damage_scale>=0 && c.damage_scale<=1 &&
    c.hit_bounty<=1000000 && c.taken_out_bounty<=1000000 && c.cost_per_hit<=1000000 && c.cost_per_taken_out<=1000000;}
float square2(float x,float y){const volatile float xx=x*x,yy=y*y;return xx+yy;}
float len2(float x,float y){return float(std::sqrt(double(square2(x,y))));}
float len3(float x,float y,float z){const volatile float xy=square2(x,y),zz=z*z;return float(std::sqrt(double(xy+zz)));}
uint32_t add(uint32_t &v,uint32_t n){const uint32_t delta=uint32_t(std::min<uint64_t>(n,uint64_t(INT32_MAX)-v));v+=delta;return delta;}
uint32_t infraction(CopChaseIncidents &s,unsigned kind){const uint32_t bit=1u<<kind;if(s.infraction_mask&bit)return 0;
    s.infraction_mask|=bit;++s.infractions;return bit;}
CopChaseIncidentOutput output(){CopChaseIncidentOutput o{};o.trigger_slot=o.damaged_slot=o.taken_out_slot=-1;return o;}
void taken(CopChaseIncidents &s,const CopChaseIncidentConfig &c,unsigned slot,bool score,CopChaseIncidentOutput &o){
    add(s.cops_taken_out,1);o.taken_out_slot=int(slot);if(score){o.bounty_added+=add(s.incident_bounty,c.taken_out_bounty);o.cost_added+=add(s.cost_to_state,c.cost_per_taken_out);}}
}
extern "C" CopChaseIncidentConfig copchase_incident_defaults(void){return {1,6.f,.012f,500,5000,250,6000};}
extern "C" void copchase_incidents_init(CopChaseIncidents *s){if(s){*s={};for(auto &c:s->cops)c.health=1;}}
extern "C" void copchase_incidents_begin(CopChaseIncidents *s){if(!s)return;
    s->infractions=s->infraction_mask=s->cost_to_state=s->cops_damaged=s->cops_taken_out=s->incident_bounty=s->assaults=0;
    s->busted_peak=0;for(auto &c:s->cops)c.damaged=0;}
extern "C" int copchase_incidents_bind(CopChaseIncidents *s,unsigned slot,uint64_t id){if(!s || slot>=64 || !id)return 0;
    auto &c=s->cops[slot];if(c.identity!=id){c={};c.identity=id;c.health=1;}return 1;}
extern "C" uint32_t copchase_incidents_tick(CopChaseIncidents *s,int pursuit,int paused,float speed,float busted){
    if(!s || paused || !std::isfinite(speed) || speed<0 || speed>1000 || !std::isfinite(busted) || busted<0 || busted>10)return 0;
    uint32_t fresh=0;if(pursuit){if(speed>29.f)fresh|=infraction(*s,COPCHASE_SPEEDING);
        if(speed>44.7f)fresh|=infraction(*s,COPCHASE_RECKLESS);s->busted_peak=std::max(s->busted_peak,busted);
        if(s->busted_peak<=.5f || busted>=.05f)return fresh;fresh|=infraction(*s,COPCHASE_RESISTING);}
    s->busted_peak=0;return fresh;
}
extern "C" CopChaseIncidentOutput copchase_incidents_collision(CopChaseIncidents *s,const CopChaseIncidentConfig *c,const CopChaseCollision *in){
    auto o=output();if(!s || !c || !in || !config(*c) || !in->valid || in->paused || !in->player_valid ||
        !std::isfinite(in->strength) || in->strength<=1 || in->strength>1e6f || uint32_t(in->now_ms-in->event_ms)>1000 ||
        !vec(in->player_position,3) || !vec(in->target_position,3) || !vec(in->player_velocity,2) || in->witness_count>64 ||
        (in->witness_count && !in->witnesses) || in->target_slot< -1 || in->target_slot>=64)return o;
    if(in->target_is_player)return o; // Optional player damage is a separate port.
    const int slot=in->target_slot;CopChaseIncidentCop *cop=nullptr;
    if(slot>=0){cop=&s->cops[slot];if(!in->target_identity || cop->identity!=in->target_identity)return o;}
    const float dx=in->target_position[0]-in->player_position[0],dy=in->target_position[1]-in->player_position[1],dz=in->target_position[2]-in->player_position[2];
    const float distance=len3(dx,dy,dz);const bool by_player=in->strength>2.5f && distance<7.5f;o.accepted=1;
    if(cop && by_player && !in->pursuit && !cop->wrecked && in->pending_trigger<0){o.trigger_slot=slot;o.trigger_reason=1;}
    if(!cop && by_player && in->strength>6.f && in->target_is_traffic){
        if(in->pursuit)o.new_infractions|=infraction(*s,COPCHASE_HIT_AND_RUN);
        else if(in->pending_trigger<0){float nearest=60;for(unsigned n=0;n<in->witness_count;++n){const auto &w=in->witnesses[n];
            if(!w.usable || w.excluded || w.slot<0 || w.slot>=64 || s->cops[w.slot].wrecked || !vec(w.position,3))continue;
            const float d=len3(w.position[0]-in->player_position[0],w.position[1]-in->player_position[1],w.position[2]-in->player_position[2]);
            if(d<nearest){nearest=d;o.trigger_slot=w.slot;o.trigger_reason=2;}}}
    }
    if(!cop || !c->cop_damage || cop->wrecked || !by_player)return o;
    float damage=(in->strength-c->damage_threshold)*c->damage_scale;if(!(damage>0))return o;
    if(slot==in->cross_slot)damage*=.35f;cop->health-=damage;o.damaged_slot=slot;o.health=std::max(0.f,cop->health);
    const float speed=len2(in->player_velocity[0],in->player_velocity[1]);
    const volatile float vx=in->player_velocity[0]*dx,vy=in->player_velocity[1]*dy;
    const float forward=(vx+vy)/(len2(dx,dy)+.01f);
    if(speed>4.f && speed*.3f<forward && in->pursuit && uint32_t(in->now_ms-cop->last_assault_ms)>1500){
        cop->last_assault_ms=in->now_ms;o.bounty_added+=add(s->incident_bounty,c->hit_bounty);add(s->assaults,1);
        o.cost_added+=add(s->cost_to_state,c->cost_per_hit);if(!cop->damaged){cop->damaged=1;add(s->cops_damaged,1);}
        o.new_infractions|=infraction(*s,COPCHASE_ASSAULT);
    }
    if(cop->health<=0){cop->health=0;cop->wrecked=1;taken(*s,*c,unsigned(slot),in->pursuit!=0,o);}
    return o;
}
extern "C" CopChaseIncidentOutput copchase_incidents_rollover(CopChaseIncidents *s,const CopChaseIncidentConfig *c,unsigned slot,uint64_t id,
    int pursuit,int selected,int paused,float up_z){auto o=output();if(!s || !c || !config(*c) || slot>=64 || !id ||
        s->cops[slot].identity!=id || paused || !selected || !std::isfinite(up_z) || std::fabs(up_z)>1.01f)return o;
    auto &cop=s->cops[slot];if(!pursuit){cop.rollover_counted=0;return o;}
    o.accepted=1;if(up_z<.25f && !cop.rollover_counted){cop.rollover_counted=1;taken(*s,*c,slot,true,o);}return o;
}
extern "C" int copchase_incidents_hiding(const float *xy,const float (*points)[2],unsigned count,float radius,float *nearest){
    if(nearest)*nearest=1e9f;if(!xy || !nearest || !vec(xy,2) || count>64 || (count && !points) ||
        !std::isfinite(radius) || radius<=0 || radius>1000)return 0;
    float squared=1e18f;for(unsigned i=0;i<count;++i){if(!vec(points[i],2))return 0;const float dx=xy[0]-points[i][0],dy=xy[1]-points[i][1];
        squared=std::min(squared,square2(dx,dy));}*nearest=float(std::sqrt(double(squared)));return *nearest<radius;
}
