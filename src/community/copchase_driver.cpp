#include "copchase_driver.h"
#include <algorithm>
#include <cmath>
namespace {
float f32(float x){volatile float v=x;return v;}
bool finite(float v,float a,float b){return std::isfinite(v) && v>=a && v<=b;}
bool config_ok(const CopChaseDriverConfig &c){return c.preset>=0 && c.preset<=3 && c.heat>=1 && c.heat<=5 &&
    c.aggression_percent>=50 && c.aggression_percent<=200 && finite(c.acceleration,0,200) &&
    finite(c.physics_distance,1,500) && finite(c.reverse_speed,0,30);}
bool pose_ok(const CopChasePose &p){
    for(int k=0;k<2;k++)if(!finite(p.position[k],-20000,20000) || !finite(p.velocity[k],-200,200) ||
        !finite(p.forward[k],-1.1f,1.1f) || !finite(p.left[k],-1.1f,1.1f))return false;
    return true;
}
float dot(float x,float y,const float axis[2]){return f32(f32(y*axis[1])+f32(x*axis[0]));}
float length(float x,float y){return float(std::sqrt(double(f32(f32(y*y)+f32(x*x)))));}
float radius(const CopChaseDriverConfig &c,float player_speed){return c.preset==3 && player_speed>30?40.f:c.physics_distance;}
void profile(const CopChaseDriverConfig &c,float &ram,uint32_t &rest){
    const int h=std::clamp(c.heat+(c.preset==2)+(c.preset==3?4:0),1,5)-1;
    const float aggression=float(c.aggression_percent)/100.f;
    constexpr float r[]={2,4,7,10,13},delay[]={3500,2500,1600,1000,600};
    ram=f32(r[h]*aggression);
    if(c.preset<3)ram=std::min(6.f,ram);
    float wait=f32(delay[h]/aggression);
    if(c.preset<3 && h>=3)wait=f32(wait*.3f);
    rest=uint32_t(wait);
}
void impulse(CopChaseDriverOutput &o,const CopChasePose &cop,float amount){
    o.apply_velocity=1;
    o.velocity_delta[0]=f32(cop.forward[0]*amount);
    o.velocity_delta[1]=f32(cop.forward[1]*amount);
}
}
extern "C" CopChaseDriverConfig copchase_driver_defaults(){return {1,1,100,10,90,6};}
extern "C" int copchase_driver_physics(const CopChaseDriverConfig *c,const CopChasePhysicsInput *i,CopChasePhysicsOutput *o){
    if(!o)return 0;*o={};
    if(!c || !i || !config_ok(*c) || i->physics_mode<0 || i->physics_mode>3 ||
       !finite(i->distance,0,100000) || !finite(i->forward,-100000,100000) || !finite(i->player_speed,0,200))return 0;
    o->forced_physics=i->forced_physics!=0;
    if(i->forced_physics && !i->selected && (uint32_t(i->now_ms-i->last_selected_ms)>20000 ||
       (i->player_valid && i->distance>120))){
        o->forced_physics=0;if(i->physics_mode==1)o->command=COPCHASE_PHYSICS_RESTORE_NATURAL;
        return 1;
    }
    if(!i->physics_enabled || !i->selected || !i->pursuit_active)return 1;
    o->clear_driver_state=1;
    const bool fast=c->preset==3 && i->player_speed>30;
    const float d=i->player_valid?i->distance:0.f;
    const bool keep_ahead=fast && i->player_valid && i->forward>0 && d<220;
    const float r=radius(*c,i->player_speed);
    if(i->physics_mode!=1){
        if(i->physics_mode!=0 && (d<r || keep_ahead)){
            o->command=COPCHASE_PHYSICS_PROMOTE;o->forced_physics=1;
        }
    }else if(!keep_ahead && d>r+(fast?20.f:40.f) &&
             uint32_t(i->now_ms-i->last_relocated_ms)>(fast?1500u:4000u)){
        o->command=COPCHASE_PHYSICS_RESTORE_NATURAL;o->forced_physics=0;
    }
    return 1;
}
extern "C" int copchase_driver_assist(const CopChaseDriverConfig *c,const CopChaseDriverInput *i,CopChaseDriverOutput *o){
    if(!o)return 0;*o={};
    if(!c || !i || !config_ok(*c) || !pose_ok(i->cop) || !pose_ok(i->player) ||
       !finite(i->seconds,0,.25f) || !finite(i->cop_vertical_velocity,-200,200) || !finite(i->up_z,-1.1f,1.1f) ||
       !finite(i->original_road_speed,0,300) || !finite(i->planned.speed,0,300) ||
       !finite(i->planned.target[0],-30000,30000) || !finite(i->planned.target[1],-30000,30000) ||
       i->physics_mode<0 || i->physics_mode>3)return 0;
    o->control=i->planned;o->reverse_until_ms=i->reverse_until_ms;
    if(i->planned.use_road_path)o->control.speed=std::max(i->planned.speed,i->original_road_speed);
    if(i->physics_mode!=1)return 1;
    const auto &cop=i->cop;const auto &player=i->player;
    const float dx=f32(player.position[0]-cop.position[0]),dy=f32(player.position[1]-cop.position[1]);
    const float distance=f32(length(dx,dy)+.01f),alignment=f32(dot(dx,dy,cop.forward)/distance);
    if(i->now_ms<i->reverse_until_ms){
        if(alignment>.6f && uint32_t(i->now_ms-i->reverse_started_ms)>500){
            o->reverse_completed=1;o->reverse_until_ms=0;
        }else{
            o->reverse_active=1;
            const float forward_speed=dot(cop.velocity[0],cop.velocity[1],cop.forward);
            if(forward_speed> -c->reverse_speed){
                const float change=std::min(f32(forward_speed+c->reverse_speed),f32(i->seconds*25.f));
                impulse(*o,cop,-change);
            }
            const float side=dot(dx,dy,cop.left)<0?1.f:-1.f;
            for(int k=0;k<2;k++)o->control.target[k]=f32(f32(f32(side*cop.left[k])*6.f)+f32(f32(cop.forward[k]*6.f)+cop.position[k]));
            o->control.speed=0;o->control.use_road_path=0;return 1;
        }
    }
    if(c->acceleration<=0 || i->seconds<=0 || i->seconds>=.1f)return 1;
    const float cop_speed=float(std::sqrt(double(f32(f32(f32(cop.velocity[0]*cop.velocity[0])+f32(cop.velocity[1]*cop.velocity[1]))+
                                                            f32(i->cop_vertical_velocity*i->cop_vertical_velocity)))));
    const float player_speed=length(player.velocity[0],player.velocity[1]);
    float ram;uint32_t recovery;profile(*c,ram,recovery);
    float desired=o->control.speed;
    if(distance<60)desired=std::min(desired,f32(f32(player_speed+4.f)+ram));
    const bool insane=c->preset==3;
    float align_required=.8f;
    if(insane){desired=f32(player_speed+(distance<40?10.f:40.f));align_required=cop_speed>30?.92f:.6f;}
    else if(distance>40)align_required=.3f;
    if(insane && player_speed>30 && dot(-dx,-dy,player.forward)>5 &&
       f32(f32(cop.forward[0]*player.forward[0])+f32(cop.forward[1]*player.forward[1]))>.7f && i->up_z>.8f){
        const float target=f32(player_speed-3.f);
        if(cop_speed<target)impulse(*o,cop,std::min(f32(target-cop_speed),f32(i->seconds*40.f)));
        return 1;
    }
    if(i->contact_elapsed_ms<recovery || distance<=8 || cop_speed<=(insane?1.f:.5f) ||
       cop_speed>=desired || alignment<=align_required || i->up_z<=.8f)return 1;
    const float acceleration=insane?f32(float(c->heat)*4.f+45.f):f32(float(c->heat-1)*1.5f+c->acceleration);
    const float remaining=f32(desired-cop_speed);
    float change=std::min(remaining,f32(acceleration*i->seconds));
    if(!insane && distance>40)change=std::min(remaining,f32(change*2.f));
    impulse(*o,cop,change);return 1;
}
extern "C" void copchase_recovery_init(CopChaseRecoveryState *s){if(s)*s={};}
extern "C" int copchase_driver_recovery(CopChaseRecoveryState *s,const CopChaseDriverConfig *c,
                                        const CopChaseRecoveryInput *i,CopChaseRecoveryOutput *o){
    if(!o)return 0;*o={};
    if(!s || !c || !i || !config_ok(*c) || !finite(i->distance,0,100000) ||
       !finite(i->cop_speed,0,300) || !finite(i->player_speed,0,200) || !finite(i->target_alignment,-1.5f,1.5f) ||
       i->physics_mode<0 || i->physics_mode>3)return 0;
    if(i->held || uint32_t(i->now_ms-i->last_relocated_ms)<3000)return 1;
    const auto now=i->now_ms;
    if(i->reverse_enabled && i->distance<35 && i->distance>6 && i->target_alignment<-.5f &&
       i->cop_speed<6 && now>uint32_t(s->reverse_until_ms+8000) && !i->arrest_phase){
        s->reverse_until_ms=now+2400;s->reverse_started_ms=now;o->reverse_started=1;
    }
    if(i->cop_speed>4){
        s->stuck_since_ms=0;if(now>uint32_t(s->reverse_until_ms+4000))s->reverse_attempts=0;
        return 1;
    }
    if(i->distance<25 || i->player_speed<6)return 1;
    if(!s->stuck_since_ms){s->stuck_since_ms=now;return 1;}
    const float r=radius(*c,i->player_speed);
    const uint32_t stuck=now-s->stuck_since_ms;
    if(i->distance>=r && i->distance<220 && stuck>2500 && uint32_t(now-i->last_relocated_ms)>6000 &&
       uint32_t(now-s->last_replan_ms)>4000){
        s->last_replan_ms=now;o->replan=1;
        if(i->physics_mode==1 && now>uint32_t(s->reverse_until_ms+1500)){
            s->reverse_until_ms=now+1100;s->reverse_started_ms=now;o->reverse_started=1;
        }
    }
    if(i->distance<r && stuck>1000 && now>uint32_t(s->reverse_until_ms+1500)){
        s->reverse_until_ms=now+1800;s->reverse_started_ms=now;++s->reverse_attempts;o->reverse_started=1;
    }
    if(c->preset==3 && i->backup_enabled && !i->lost_contact && !i->special_motion && stuck>3500)
        o->relocation_requested=1;
    return 1;
}
