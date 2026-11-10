#include "copchase_steering.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace {
float length(float x,float y){return std::sqrt(x*x+y*y);}
float dot(float x,float y,const float a[2]){return x*a[0]+y*a[1];}
bool pose_ok(const CopChasePose &p){
    for(int k=0;k<2;k++)if(!std::isfinite(p.position[k]) || std::fabs(p.position[k])>20000 ||
        !std::isfinite(p.velocity[k]) || std::fabs(p.velocity[k])>200 ||
        !std::isfinite(p.forward[k]) || !std::isfinite(p.left[k]))return false;
    const float f=dot(p.forward[0],p.forward[1],p.forward),l=dot(p.left[0],p.left[1],p.left);
    return f>.5f && f<1.5f && l>.5f && l<1.5f &&
        std::fabs(dot(p.forward[0],p.forward[1],p.left))<.1f;
}
bool bounded(float f,float lo,float hi){return std::isfinite(f) && f>=lo && f<=hi;}
void aim(CopChaseSteeringOutput &o,const CopChasePose &p,float lead,float forward,float side){
    for(int k=0;k<2;k++)o.target[k]=p.position[k]+p.velocity[k]*lead+p.forward[k]*forward+p.left[k]*side;
}
// ASI 100038c0: do not charge past a nearby target, or accelerate into a
// tight turn. Applied after every role/formation and the stationary box-in.
void cap_turn(CopChaseSteeringOutput &o,const CopChasePose &cop,float speed){
    const float dx=o.target[0]-cop.position[0],dy=o.target[1]-cop.position[1],d=length(dx,dy);
    if(d>=.01f){
        o.speed=std::min(o.speed,d*.6f+speed+4);
        if(dot(dx,dy,cop.forward)/d<.2f && d<90)o.speed=std::min(o.speed,speed+7);
    }
    o.speed=std::max(0.f,o.speed);
}
}
extern "C" CopChaseSteeringInput copchase_steering_defaults(){
    CopChaseSteeringInput i{};i.preset=1;i.heat=1;i.aggression_percent=100;
    i.formation_side=1;i.catchup_speed=50;i.direct_range=110;i.maximum_speed=75;i.speed_bonus=14;
    i.since_contact_ms=100000;i.personality={1,0,1,1};return i;
}
extern "C" CopChasePersonality copchase_personality(const uint16_t s[4]){
    if(!s)return {1,0,1,1};
    return {.4f+float(s[0]%1000)*1.3f/999.f,-2.4f+float(s[1]%1000)*4.8f/999.f,
        .93f+float(s[2]%1000)*.15f/999.f,.7f+float(s[3]%1000)*.7f/999.f};
}
extern "C" int copchase_steering(const CopChaseSteeringInput *input,const CopChasePose *c,
                                 const CopChasePose *p,CopChaseSteeringOutput *out){
    if(!out)return 0;*out={};
    if(!input || !c || !p || !pose_ok(*c) || !pose_ok(*p))return 0;
    const auto &i=*input;const auto &s=i.personality;
    if(i.preset<0 || i.preset>3 || i.heat<1 || i.heat>5 || i.role<0 || i.role>3 ||
        i.aggression_percent<50 || i.aggression_percent>200 || i.formation<0 || i.formation>4 ||
        i.formation_slot<0 || i.formation_slot>15 || (i.formation_side!=1 && i.formation_side!=-1) ||
        !bounded(i.formation_pressure,0,1) || !bounded(i.since_contact_ms,0,1e9f) ||
        !bounded(i.catchup_speed,0,150) || !bounded(i.direct_range,1,500) ||
        !bounded(i.maximum_speed,1,150) || !bounded(i.speed_bonus,0,50) ||
        !bounded(i.smoothed_speed,0,200) || !bounded(s.lead,.4f,1.701f) ||
        !bounded(s.lateral,-2.401f,2.401f) || !bounded(s.speed,.929f,1.081f) ||
        !bounded(s.aggression,.699f,1.401f))return 0;
    const bool insane=i.preset==3;
    const float dx=p->position[0]-c->position[0],dy=p->position[1]-c->position[1];
    const float d=length(dx,dy),v=length(p->velocity[0],p->velocity[1]);
    const int h=std::clamp(i.heat+(i.preset==2?1:0)+(insane?4:0),1,5)-1;
    // ASI 10061f8c..10061fec, profile adjusted by 10003250.
    static constexpr float extra[]={2,4,7,10,13},gap[]={7,3,0,0,0};
    static constexpr float side[]={3.2f,2.6f,2,1.6f,1.2f},rest[]={3500,2500,1600,1000,600};
    const float aggression=float(i.aggression_percent)/100.f;
    float ram=extra[h]*aggression;
    if(!insane)ram=std::min(ram,6.f);
    ram*=s.aggression;
    float following=gap[h]/aggression*(2-s.aggression);
    float contact_delay=rest[h]/aggression;
    if(!insane && h>=3)contact_delay*=.3f;
    const float ram_side=std::max(1.f,3.2f-(3.2f-side[h])*aggression);
    bool head_on=h>=3;
    if(aggression<.75f)head_on=false;else if(aggression>=1.5f && h>=2)head_on=true;
    if(i.cross){ram*=1.6f;following=0;}
    const bool recovering=uint32_t(i.since_contact_ms)<uint32_t(contact_delay);
    float maximum=insane?130.f:i.maximum_speed;
    if(i.cross)maximum*=1.12f;
    if(!insane)maximum=std::min(maximum,i.catchup_speed*(i.preset==2?1.2f:1.f)+5);
    static constexpr float mph[]={58,68,78,88,96};
    float speed=std::max(mph[i.heat-1]*.447f,i.smoothed_speed*1.15f+i.speed_bonus+(i.heat-1)*3);
    if(insane)speed=v*1.3f+i.heat*4+i.speed_bonus;
    if(dot(dx,dy,p->forward)>35)speed+=(d-35)*(insane?.6f:.3f);
    speed*=s.speed;if(i.cross)speed*=1.15f;
    out->use_road_path=d>(insane && v>30?45.f:i.direct_range);
    out->speed=speed;
    if(v<3){
        const float angle=float(i.role)*1.5708f;
        aim(*out,*p,0,std::cos(angle)*4.5f,std::sin(angle)*4.5f);
        const float target_distance=length(out->target[0]-c->position[0],out->target[1]-c->position[1]);
        if(target_distance<=30)out->speed=target_distance<=6?3.f:12.f;
        cap_turn(*out,*c,v);return 1;
    }
    const float along=-dot(dx,dy,p->forward),across=-dot(dx,dy,p->left);
    if(insane && along>25 && dot(c->forward[0],c->forward[1],p->forward)<-.5f &&
        (!head_on || along>=70 || std::fabs(across)>=7)){
        for(int k=0;k<2;k++)out->target[k]=c->position[k]+c->forward[k]*40;
        out->speed=v>30?30.f:20.f;out->use_road_path=0;return 1;
    }
    if(insane && v>30 && along>25 && dot(c->forward[0],c->forward[1],p->forward)>.5f){
        aim(*out,*p,0,along+45,across);out->speed=v*.95f;out->use_road_path=0;return 1;
    }
    if(i.formation && v>6){
        float f=0,l=0;const float pressure=i.formation_pressure;
        switch(i.formation){
        case 1: f=18-pressure*6;l=float(i.formation_slot-1)*3.6f;
            if(along>f-6)out->speed=(1-pressure*.15f)*v;break;
        case 2:{
            static constexpr float forward_sign[]={1,-1,0,0},side_sign[]={0,0,1,-1};
            const unsigned slot=unsigned(i.formation_slot)&3;
            f=(9-pressure*3)*forward_sign[slot];l=(3.9f-pressure*.9f)*side_sign[slot];
            if(slot==0 && along>2)out->speed=(1-pressure*.1f)*v;
            else if(std::fabs(along-f)<6)out->speed=v+1;break;}
        case 3: f=-1.8f;l=float(i.formation_side)*1.1f;
            if(std::fabs(along+1.8f)<8)out->speed=pressure*4+v+4;break;
        case 4: f=float(i.formation_slot)*3.5f-2;l=(2.4f-pressure*.6f)*i.formation_side;
            if(std::fabs(along-f)<8)out->speed=v+1.5f;break;
        }
        aim(*out,*p,.25f,f,l);out->speed=std::min(out->speed,maximum+10);
        out->use_road_path=0;cap_turn(*out,*c,v);return 1;
    }
    const float lead=std::min(2.f,d/(v+20)*s.lead);
    const float lateral=s.lateral*(d<=14?.25f:1.f);
    aim(*out,*p,lead,0,lateral);
    if(i.role==0){
        if(recovering){aim(*out,*p,.3f,-following-9,0);out->speed=std::max(0.f,v-2);}
        else if(following<=0){if(d<14)out->speed=ram+v;}
        else{
            const float error=-along-following;
            aim(*out,*p,.3f,-following,0);
            if(error<=15)out->speed=v+std::min(ram,std::max(-3.f,error*.4f));
        }
    }else if(i.role==1 || i.role==2){
        const float side_offset=(d>=12 || recovering?3.5f:ram_side)*(i.role==1?1.f:-1.f);
        for(int k=0;k<2;k++)out->target[k]+=p->left[k]*side_offset+p->forward[k]*1.5f;
        if(d<12)out->speed=v+(recovering?0:ram*.3f+1);
    }else{
        for(int k=0;k<2;k++)out->target[k]+=p->forward[k]*22;
        out->speed+=8;
    }
    out->speed=std::min(out->speed,maximum+10);cap_turn(*out,*c,v);return 1;
}
