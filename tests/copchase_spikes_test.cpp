#include "copchase_spikes.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
static void check(bool v){if(!v){std::puts("CopChase spike check failed");std::abort();}}
int main(){
    CopChaseSpikeState s{};CopChaseSpikeOutput o;CopChaseSpikeInput i{};
    i.forward[0]=i.left[1]=i.strip.forward[0]=i.strip.left[1]=1;
    i.strip.active=1;i.strip.half_width=2;i.strip.position[2]=-.2f;
    i.seconds=.02f;i.driving=1;i.velocity[0]=30;
    for(int w=0;w<4;w++){i.wheel_offset[w][0]=w<2?2.f:-2.f;i.wheel_offset[w][1]=(w&1)?1.f:-1.f;}
    i.position[0]=-3;check(copchase_spikes_step(&s,&i,&o) && !o.flat_count);
    i.position[0]=-1;check(copchase_spikes_step(&s,&i,&o) && o.newly_flat_mask==3 && o.flat_count==2 && o.apply_delta);
    check(o.physical_delta[0]<0 && o.physical_delta[1]==0);
    check(copchase_spikes_step(&s,&i,&o) && !o.newly_flat_mask);
    i.position[0]=3;check(copchase_spikes_step(&s,&i,&o) && o.newly_flat_mask==12 && o.flat_count==4);
    i.strip.active=0;check(copchase_spikes_step(&s,&i,&o) && !s.previous_valid && o.flat_count==4);
    i.driving=0;check(copchase_spikes_step(&s,&i,&o) && !o.apply_delta);
    const auto before=s;i.position[0]=std::numeric_limits<float>::quiet_NaN();
    check(!copchase_spikes_step(&s,&i,&o) && !std::memcmp(&before,&s,sizeof s));
    copchase_spikes_init(&s);check(!s.flat_mask);
    CopChaseSpikeLifecycle strip{};strip.strip.active=1;strip.strip.forward[0]=1;strip.strip.half_width=2;
    CopChaseSpikeLifecycleInput li{};li.now_ms=1500;li.block_active=1;li.settled_count=2;
    li.settled_height[0]=li.settled_height[1]=.3f;li.player_position[0]=-74;li.player_velocity[0]=10;
    CopChaseSpikeLifecycleOutput lo;
    check(copchase_spikes_lifecycle(&strip,&li,&lo) && !lo.height_changed && lo.approaching);
    li.now_ms=1501;check(copchase_spikes_lifecycle(&strip,&li,&lo) && lo.height_changed && !lo.approaching && strip.height_fixed);
    check(std::fabs(strip.strip.position[2]-.12f)<1e-6);
    li.player_position[0]=120;check(copchase_spikes_lifecycle(&strip,&li,&lo) && !lo.removed);
    li.player_position[0]=120.01f;check(copchase_spikes_lifecycle(&strip,&li,&lo) && lo.removed && !strip.strip.active);
    std::puts("CopChase spike intersection and drag checks passed");
}
