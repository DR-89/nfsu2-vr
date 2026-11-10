#include "copchase_steering.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
static void check(bool ok){if(!ok){std::fprintf(stderr,"CopChase steering check failed\n");std::abort();}}
static bool close(float a,float b){return std::fabs(a-b)<.003f;}
static CopChasePose rotate(CopChasePose p,float a){
    for(float *v:{p.position,p.velocity,p.forward,p.left}){
        float x=v[0],y=v[1];v[0]=std::cos(a)*x-std::sin(a)*y;v[1]=std::sin(a)*x+std::cos(a)*y;
    }return p;
}
int main(){
    CopChasePose player={{0,0},{20,0},{1,0},{0,1}},cop={{-30,0},{10,0},{1,0},{0,1}};
    auto i=copchase_steering_defaults();i.smoothed_speed=20;
    CopChaseSteeringOutput out{};
    check(copchase_steering(&i,&cop,&player,&out));
    // Heat1 follows 7m behind a .3-second lead; the approach speed is then
    // bounded by distance, independent of the ordinary road target.
    check(close(out.target[0],-1) && close(out.target[1],0) && !out.use_road_path);
    check(out.speed>20 && out.speed<50);
    player.velocity[0]=0;i.smoothed_speed=0;i.role=0;
    check(copchase_steering(&i,&cop,&player,&out));check(close(out.target[0],4.5f) && close(out.target[1],0));
    cop.position[0]=4;cop.position[1]=0;
    check(copchase_steering(&i,&cop,&player,&out));check(close(out.speed,3));
    unsigned cases=0;
    for(int preset=0;preset<4;preset++)for(int heat=1;heat<=5;heat++)for(int role=0;role<4;role++)
    for(int formation=0;formation<=4;formation++)for(int speed=0;speed<=80;speed+=20)
    for(int distance=10;distance<=170;distance+=40){
        player={{0,0},{float(speed),0},{1,0},{0,1}};cop={{-float(distance),5},{10,0},{1,0},{0,1}};
        i=copchase_steering_defaults();i.preset=preset;i.heat=heat;i.role=role;i.formation=formation;
        i.formation_slot=role;i.formation_pressure=.6f;i.smoothed_speed=float(speed);
        check(copchase_steering(&i,&cop,&player,&out));
        check(std::isfinite(out.speed) && out.speed>=0 && out.speed<=150);
        const auto cp=rotate(cop,.37f),pp=rotate(player,.37f);CopChaseSteeringOutput turned{};
        check(copchase_steering(&i,&cp,&pp,&turned));
        check(close(turned.speed,out.speed) && turned.use_road_path==out.use_road_path);
        check(close(turned.target[0],std::cos(.37f)*out.target[0]-std::sin(.37f)*out.target[1]));
        check(close(turned.target[1],std::sin(.37f)*out.target[0]+std::cos(.37f)*out.target[1]));++cases;
    }
    cop.position[0]=std::numeric_limits<float>::quiet_NaN();out.speed=123;
    check(!copchase_steering(&i,&cop,&player,&out) && out.speed==0 && out.target[0]==0);
    cop={{-30,0},{0,0},{1,0},{0,1}};i.personality.speed=std::numeric_limits<float>::infinity();
    check(!copchase_steering(&i,&cop,&player,&out));
    i=copchase_steering_defaults();i.role=4;check(!copchase_steering(&i,&cop,&player,&out));
    uint16_t values[4]={0,999,500,999};const auto personality=copchase_personality(values);
    check(close(personality.lead,.4f) && close(personality.lateral,2.4f) && close(personality.aggression,1.4f));
    std::printf("CopChase steering: %u role/heat/formation/rotation cases and input boundaries passed\n",cases);
}
