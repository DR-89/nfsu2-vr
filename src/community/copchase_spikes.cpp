#include "copchase_spikes.h"
#include <algorithm>
#include <cmath>
namespace {
float f32(float a){volatile float x=a;return x;}
bool finite(float x,float limit){return std::isfinite(x) && std::fabs(x)<=limit;}
bool input_ok(const CopChaseSpikeInput &i){
    if(!finite(i.seconds,.25f) || i.seconds<0)return false;
    for(int k=0;k<3;k++)if(!finite(i.position[k],20000) || !finite(i.strip.position[k],20000))return false;
    for(int k=0;k<2;k++){
        if(!finite(i.forward[k],1.1f) || !finite(i.left[k],1.1f) || !finite(i.velocity[k],200) ||
           !finite(i.strip.forward[k],1.1f) || !finite(i.strip.left[k],1.1f))return false;
        for(int w=0;w<4;w++)if(!finite(i.wheel_offset[w][k],10))return false;
    }
    return finite(i.strip.half_width,30) && (!i.strip.active || i.strip.half_width>0);
}
float dot(float x,float y,const float axis[2]){return f32(f32(x*axis[0])+f32(y*axis[1]));}
}
extern "C" void copchase_spikes_init(CopChaseSpikeState *s){if(s)*s={};}
extern "C" int copchase_spikes_step(CopChaseSpikeState *s,const CopChaseSpikeInput *i,CopChaseSpikeOutput *o){
    if(!o)return 0;*o={};
    if(!s || !i || !input_ok(*i) || (s->flat_mask&~15u))return 0;
    for(float p:s->previous_longitudinal)if(!finite(p,60000))return 0;
    if(!i->strip.active)s->previous_valid=0;
    else {
        for(unsigned w=0;w<4;w++){
            float diff[2];
            for(int k=0;k<2;k++)diff[k]=f32(f32(f32(f32(i->wheel_offset[w][0]*i->forward[k])+i->position[k])+
                                                               f32(i->wheel_offset[w][1]*i->left[k]))-i->strip.position[k]);
            const float longitudinal=dot(diff[0],diff[1],i->strip.forward);
            const float lateral=dot(diff[0],diff[1],i->strip.left);
            const bool crossing=s->previous_valid && ((s->previous_longitudinal[w]>0)!=(longitudinal>0)) &&
                                std::fabs(f32(s->previous_longitudinal[w]-longitudinal))<6;
            if(std::fabs(lateral)<i->strip.half_width &&
               std::fabs(f32(f32(i->position[2]-.2f)-i->strip.position[2]))<2 &&
               (std::fabs(longitudinal)<.3f || crossing) && !(s->flat_mask&(1u<<w))){
                s->flat_mask=uint8_t(s->flat_mask|(1u<<w));o->newly_flat_mask|=1u<<w;
            }
            s->previous_longitudinal[w]=longitudinal;
        }
        s->previous_valid=1;
    }
    for(unsigned w=0;w<4;w++)o->flat_count+=(s->flat_mask>>w)&1u;
    o->speed=float(std::sqrt(double(f32(f32(i->velocity[0]*i->velocity[0])+f32(i->velocity[1]*i->velocity[1])))));
    if(i->seconds<=0 || i->seconds>.1f || !i->driving)return 1;
    if(i->control_locked || i->arrest_phase){
        if(o->speed>.05f){o->apply_delta=1;for(int k=0;k<2;k++)o->physical_delta[k]=f32(-i->velocity[k]*.5f);}
        return 1;
    }
    o->scrape_active=o->flat_count!=0;
    if(!o->flat_count || o->speed<.5f)return 1;
    constexpr float caps[]={0,42,32,24,17};
    float deceleration=f32(float(o->flat_count)*.9f);
    if(o->speed>caps[o->flat_count])deceleration=f32(deceleration+f32(f32(o->speed-caps[o->flat_count])*10.f));
    const float drag=std::min(f32(o->speed*.3f),f32(deceleration*i->seconds));
    float bias=0;
    for(unsigned w=0;w<4;w++)if(s->flat_mask&(1u<<w))
        bias=f32(bias+f32((i->wheel_offset[w][0]>0?1.4f:1.f)*(i->wheel_offset[w][1]>0?1.f:-1.f)));
    const float pull=f32(f32(f32(bias*.8f)*i->seconds)*std::min(2.f,f32(o->speed/15.f)));
    for(int k=0;k<2;k++)o->physical_delta[k]=f32(f32(pull*i->left[k])-f32(f32(i->velocity[k]/o->speed)*drag));
    o->apply_delta=1;return 1;
}
extern "C" int copchase_spikes_lifecycle(CopChaseSpikeLifecycle *s,const CopChaseSpikeLifecycleInput *i,CopChaseSpikeLifecycleOutput *o){
    if(!o)return 0;*o={};
    if(!s || !i || i->settled_count>4)return 0;
    for(float v:s->strip.position)if(!finite(v,20000))return 0;
    for(int k=0;k<2;k++)if(!finite(s->strip.forward[k],1.1f) || !finite(i->player_position[k],20000) || !finite(i->player_velocity[k],200))return 0;
    for(unsigned n=0;n<i->settled_count;n++)if(!finite(i->settled_height[n],20000))return 0;
    if(!s->strip.active){s->near_announced=0;return 1;}
    if(!s->height_fixed && i->block_active && uint32_t(i->now_ms-s->created_ms)>1500){
        float height=0;for(unsigned n=0;n<i->settled_count;n++)height=f32(height+i->settled_height[n]);
        if(i->settled_count){
            height=f32(f32(height/float(i->settled_count))-.18f);
            if(std::fabs(f32(height-s->strip.position[2]))<.6f){s->strip.position[2]=height;o->height_changed=1;}
        }
        s->height_fixed=1;
    }
    const float dx=f32(i->player_position[0]-s->strip.position[0]),dy=f32(i->player_position[1]-s->strip.position[1]);
    const float distance2=f32(f32(dx*dx)+f32(dy*dy));
    if(distance2>62500 || dot(dx,dy,s->strip.forward)>120){
        s->strip.active=0;s->near_announced=0;o->removed=1;return 1;
    }
    if(!s->near_announced && distance2<5625 && dot(-dx,-dy,i->player_velocity)>0){
        s->near_announced=1;o->approaching=1;
    }
    return 1;
}
