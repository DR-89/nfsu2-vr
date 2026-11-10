#include "copchase_roadblock.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static void check(bool x){if(!x){std::puts("CopChase roadblock check failed");std::abort();}}
int main(){
    CopChaseRoadblockPlanInput i{};CopChaseRoadblockPlan o;
    i.heat=5;i.spikes_enabled=1;i.forward[0]=i.travel_direction[0]=1;
    i.centre[0]=200;i.lane_min=-4;i.lane_max=4;i.candidate_count=4;
    for(unsigned n=0;n<4;n++){auto &c=i.candidates[n];c.id=n;c.eligible=1;c.position[0]=-100-float(n);}
    check(copchase_roadblock_plan(&i,&o) && o.count==2 && o.across_count==2 && o.strip.active);
    check(std::fabs(o.width-11.4f)<1e-5 && o.strip.half_width==1.9f && o.random_used==7);
    i.heavy=1;check(copchase_roadblock_plan(&i,&o) && o.count==4 && o.across_count==2 && !o.strip.active);
    check(o.cars[2].position[0]==209 && o.cars[3].position[0]==209);
    i.heavy=0;i.lane_min=-1;i.lane_max=1;
    check(copchase_roadblock_plan(&i,&o) && o.strip.active && o.strip.position[0]==193 && o.strip.half_width>4);
    i.candidates[0].eligible=0;i.candidates[1].eligible=0;i.candidates[2].eligible=0;
    check(!copchase_roadblock_plan(&i,&o) && !o.count);
    CopChaseRoadblockSchedule s{};CopChaseRoadblockScheduleOutput so;
    CopChaseRoadblockScheduleInput si{};si.now_ms=10000;si.pursuit_epoch=1;si.preset=1;si.heat=5;
    si.active=si.enabled=1;si.speed=20;si.chance_multiplier=1;si.insane_roll_ms=15000;
    check(copchase_roadblock_schedule(&s,&si,&so) && !so.request && s.next_roll_ms==18000 && s.next_attempt_ms==13000);
    si.now_ms=19000;check(copchase_roadblock_schedule(&s,&si,&so) && so.request);
    copchase_roadblock_attempted(&s,si.now_ms,0);check(s.roll_succeeded && s.next_attempt_ms==22000);
    si.now_ms=22000;check(copchase_roadblock_schedule(&s,&si,&so) && !so.request);
    si.now_ms++;check(copchase_roadblock_schedule(&s,&si,&so) && so.request);
    copchase_roadblock_attempted(&s,si.now_ms,1);check(!s.roll_succeeded);
    CopChaseRoadblockLifecycle life{};life.active=1;life.forward[0]=1;life.created_ms=100;
    CopChaseRoadblockLifecycleInput li{1000,1,0,{0,0},0};CopChaseRoadblockRelease release;
    check(copchase_roadblock_lifecycle(&life,&li,&release) && !release);
    li.now_ms=3500;check(copchase_roadblock_lifecycle(&life,&li,&release) && !release);
    li.now_ms++;check(copchase_roadblock_lifecycle(&life,&li,&release) && release==COPCHASE_BLOCK_STOPPED);
    life.active=1;li.speed=20;li.player_position[0]=15;check(copchase_roadblock_lifecycle(&life,&li,&release) && !release);
    li.player_position[0]=15.01f;check(copchase_roadblock_lifecycle(&life,&li,&release) && release==COPCHASE_BLOCK_PASSED);
    std::puts("CopChase roadblock planning and lifecycle checks passed");
}
