#include "copchase_driver.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <limits>
static void check(bool ok){if(!ok){std::fprintf(stderr,"CopChase driver check failed\n");std::abort();}}
int main(){
    auto c=copchase_driver_defaults();CopChasePhysicsOutput physics;
    CopChasePhysicsInput p{10000,0,0,1,1,1,0,2,1,89,0,20};
    check(copchase_driver_physics(&c,&p,&physics) && physics.command==COPCHASE_PHYSICS_PROMOTE && physics.forced_physics);
    p.distance=90;check(copchase_driver_physics(&c,&p,&physics) && !physics.command);
    p.physics_mode=1;p.forced_physics=1;p.distance=130;
    check(copchase_driver_physics(&c,&p,&physics) && !physics.command);
    p.distance=130.01f;check(copchase_driver_physics(&c,&p,&physics) && physics.command==COPCHASE_PHYSICS_RESTORE_NATURAL);
    p.selected=0;p.distance=120;p.now_ms=20000;
    check(copchase_driver_physics(&c,&p,&physics) && !physics.command);
    p.now_ms++;check(copchase_driver_physics(&c,&p,&physics) && physics.command==COPCHASE_PHYSICS_RESTORE_NATURAL);
    c.preset=3;p.selected=1;p.physics_mode=2;p.distance=219;p.forward=1;p.player_speed=31;
    check(copchase_driver_physics(&c,&p,&physics) && physics.command==COPCHASE_PHYSICS_PROMOTE);
    p.distance=220;check(copchase_driver_physics(&c,&p,&physics) && !physics.command);
    CopChaseDriverInput i{};i.now_ms=10000;i.contact_elapsed_ms=100000;i.seconds=.02f;i.up_z=1;i.physics_mode=1;
    i.cop={{0,0},{10,0},{1,0},{0,1}};i.player={{50,0},{20,0},{1,0},{0,1}};
    i.planned={{50,0},40,0};CopChaseDriverOutput o;c=copchase_driver_defaults();
    check(copchase_driver_assist(&c,&i,&o) && o.apply_velocity && std::fabs(o.velocity_delta[0]-.4f)<.0001f);
    i.contact_elapsed_ms=3499;check(copchase_driver_assist(&c,&i,&o) && !o.apply_velocity);
    i.contact_elapsed_ms=3500;check(copchase_driver_assist(&c,&i,&o) && o.apply_velocity);
    i.up_z=.8f;check(copchase_driver_assist(&c,&i,&o) && !o.apply_velocity);i.up_z=1;
    i.seconds=.1f;check(copchase_driver_assist(&c,&i,&o) && !o.apply_velocity);i.seconds=.02f;
    i.reverse_until_ms=11000;i.reverse_started_ms=9800;i.player.position[0]=-20;i.player.position[1]=2;
    check(copchase_driver_assist(&c,&i,&o) && o.reverse_active && o.control.speed==0 && o.velocity_delta[0]<0);
    check(o.control.target[0]==6 && o.control.target[1]==-6);
    i.player.position[0]=20;i.reverse_started_ms=9499;
    check(copchase_driver_assist(&c,&i,&o) && o.reverse_completed && !o.reverse_active && !o.reverse_until_ms);
    CopChaseRecoveryState s;copchase_recovery_init(&s);CopChaseRecoveryOutput r;
    CopChaseRecoveryInput ri{10000,0,30,2,20,-1,1,1,0,0,0,1,0};
    check(copchase_driver_recovery(&s,&c,&ri,&r) && r.reverse_started && s.reverse_until_ms==12400);
    check(s.stuck_since_ms==10000);ri.now_ms=14000;ri.target_alignment=1;
    check(copchase_driver_recovery(&s,&c,&ri,&r) && r.reverse_started && s.reverse_attempts==1 && s.reverse_until_ms==15800);
    ri.now_ms=20000;ri.distance=100;
    check(copchase_driver_recovery(&s,&c,&ri,&r) && r.replan && r.reverse_started && s.reverse_until_ms==21100);
    ri.cop_speed=5;ri.now_ms=26000;check(copchase_driver_recovery(&s,&c,&ri,&r) && !s.stuck_since_ms && !s.reverse_attempts);
    const auto before=s;ri.distance=std::numeric_limits<float>::infinity();
    check(!copchase_driver_recovery(&s,&c,&ri,&r) && !std::memcmp(&s,&before,sizeof s));
    std::puts("CopChase physics, assist and recovery checks passed");
}
