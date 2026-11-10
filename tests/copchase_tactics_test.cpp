#include "copchase_tactics.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
static void check(bool ok){if(!ok){std::fprintf(stderr,"CopChase tactics check failed\n");std::abort();}}
int main(){
    auto config=copchase_tactics_defaults();CopChaseTacticsState state;copchase_tactics_init(&state);
    CopChaseTacticsCop cops[]={
        {10,10,-10,1,-2,1,1,0},{5,20,-20,1,2,1,1,0},{60,30,-30,1,3,1,1,0},
        {1,40,-40,1,4,1,-1,0},{2,50,-50,1,5,1,-1,0}
    };
    CopChaseTacticsInput in{100,20,cops,5,0};CopChaseTacticsOutput out;
    check(copchase_tactics_step(&state,&config,&in,&out));
    check(out.role[10]==0 && out.role[5]==1 && out.role[60]==2 && out.role[1]==2 && out.role[2]==1);
    check(out.started && out.sample_used && out.formation==1 && state.ends_ms==4100 && out.side==-1);
    check(out.member[10]==0 && out.member[5]==1 && out.member[60]==2 && out.member[1]==-1);
    in.now_ms=4100;check(copchase_tactics_step(&state,&config,&in,&out));check(!out.ended);
    in.now_ms=4101;check(copchase_tactics_step(&state,&config,&in,&out));
    check(out.ended && !out.formation && state.next_ms==11101 && !out.sample_used);
    in.now_ms=11101;check(copchase_tactics_step(&state,&config,&in,&out));check(!out.started);
    in.now_ms++;in.speed=8;check(copchase_tactics_step(&state,&config,&in,&out));check(!out.started);
    in.speed=8.01f;check(copchase_tactics_step(&state,&config,&in,&out));check(out.started);
    in.speed=3;check(copchase_tactics_step(&state,&config,&in,&out));check(!out.ended);
    in.speed=2.99f;check(copchase_tactics_step(&state,&config,&in,&out));check(out.ended);
    copchase_tactics_init(&state);in.speed=20;in.now_ms=100;in.count=1;
    check(copchase_tactics_step(&state,&config,&in,&out));check(!out.formation && state.next_ms==1100);
    config.heat=3;in.count=5;in.now_ms=1200;in.formation_sample=500;
    check(copchase_tactics_step(&state,&config,&in,&out));check(out.formation==3 && state.ends_ms==6200);
    check(out.member[10]==0 && out.member[5]==-1);
    copchase_tactics_init(&state);config.formations_enabled=0;
    cops[1].forward=30;cops[1].heading_dot=.51f;
    cops[2].forward=30;cops[2].heading_dot=-1;
    cops[3].forward=5;cops[3].distance=39;cops[3].personality_aggression=1.2f;
    cops[4].force_direct=1;
    check(copchase_tactics_step(&state,&config,&in,&out));
    check(out.role[5]==3 && out.role[60]!=0 && out.role[1]==0 && out.role[2]==0);
    config.preset=2;check(copchase_tactics_step(&state,&config,&in,&out));check(out.role[60]==0);
    config.aggression_percent=74;check(copchase_tactics_step(&state,&config,&in,&out));check(out.role[60]!=0);
    const auto before=state;cops[4].slot=cops[3].slot;
    check(!copchase_tactics_step(&state,&config,&in,&out));check(!std::memcmp(&state,&before,sizeof state));
    cops[4].slot=2;in.speed=std::numeric_limits<float>::quiet_NaN();
    check(!copchase_tactics_step(&state,&config,&in,&out));
    check(copchase_tactics_preferred_side(0,0,1)==-1 && copchase_tactics_preferred_side(1,0,0)==1);
    check(copchase_tactics_preferred_side(0,1,17)==1 && copchase_tactics_preferred_side(1,1,18)==-1);
    std::puts("CopChase tactics lifecycle and role checks passed");
}
