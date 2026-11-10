#include "copchase_roadblock.h"
#include <algorithm>
#include <cmath>
namespace {
float f32(float a){volatile float x=a;return x;}
bool finite(float x,float a,float b){return std::isfinite(x) && x>=a && x<=b;}
bool axis_ok(const float a[2]){const float n=a[0]*a[0]+a[1]*a[1];return finite(a[0],-1.1f,1.1f) && finite(a[1],-1.1f,1.1f) && n>.98f && n<1.02f;}
float dot(float x,float y,const float a[2]){return f32(f32(x*a[0])+f32(y*a[1]));}
float length(float x,float y){return float(std::sqrt(double(f32(f32(x*x)+f32(y*y)))));}
constexpr int chance[]={0,10,25,30,40},spike_chance[]={0,0,0,70,80};
}
extern "C" int copchase_roadblock_plan(const CopChaseRoadblockPlanInput *i,CopChaseRoadblockPlan *o){
    if(!o)return 0;*o={};
    if(!i || i->heat<1 || i->heat>5 || i->candidate_count>64 || !axis_ok(i->travel_direction) || !axis_ok(i->forward) ||
       !finite(i->lane_min,-14,0) || !finite(i->lane_max,0,14))return 0;
    for(float p:i->centre)if(!finite(p,-20000,20000))return 0;
    for(float p:i->player_position)if(!finite(p,-20000,20000))return 0;
    uint64_t seen=0;
    for(unsigned n=0;n<i->candidate_count;n++){
        const auto &c=i->candidates[n];if(c.id>=64 || (seen&(uint64_t(1)<<c.id)))return 0;seen|=uint64_t(1)<<c.id;
        for(float p:c.position)if(!finite(p,-20000,20000))return 0;
        if(n && c.id<=i->candidates[n-1].id)return 0;
    }
    for(uint32_t r:i->random)if(r>32767)return 0;
    CopChaseRoadblockPlan out{};unsigned rng=0;auto sample=[&](){return i->random[rng++];};
    float low=i->lane_min,high=i->lane_max;
    if(f32(high-low)<3){low=f32(low-1.8f);high=f32(high+1.8f);}
    low=f32(low-1.7f);high=f32(high+1.7f);
    const float width=f32(high-low);const bool narrow=width<9.5f;
    const bool spikes=!i->heavy && i->spikes_enabled && (i->debug || sample()%100<unsigned(spike_chance[i->heat-1]));
    const unsigned requested_across=width>12?3:2,requested=i->heavy?4:requested_across;
    if(!narrow)(void)sample(); // Original draws an initial gap, then redraws after selection.
    unsigned selected[4]{};
    for(unsigned pass=0;pass<3 && out.count<requested;pass++)for(unsigned n=0;n<i->candidate_count && out.count<requested;n++){
        const auto &c=i->candidates[n];
        if(!c.eligible || c.disabled || c.model_pending || c.held || (c.selected && pass<2))continue;
        bool duplicate=false;for(unsigned k=0;k<out.count;k++)duplicate|=selected[k]==n;if(duplicate)continue;
        const float dx=f32(c.position[0]-i->player_position[0]),dy=f32(c.position[1]-i->player_position[1]);
        const float ahead=dot(dx,dy,i->travel_direction),distance=length(dx,dy);
        if((pass==0 && (ahead< -60 || distance>300)) ||
           (pass==1 && (ahead<0 || distance>120 || i->debug)) ||
           (pass==2 && c.selected && ahead< -120))selected[out.count++]=n;
    }
    if(out.count<2)return 0;
    out.across_count=std::min(out.count,requested_across);
    const unsigned extra=out.count-out.across_count;
    const unsigned gap=narrow?~0u:sample()%(out.across_count-1)+1;
    const float gap_width=narrow?0.f:3.f,nominal=f32(f32(width-gap_width)/float(out.across_count));
    float extent=0,angle=0;
    for(int degrees=0;degrees<=40;degrees+=5){
        angle=f32(float(degrees)*.0174533f);
        extent=f32(f32(float(std::sin(double(angle)))*1.9f)+f32(float(std::cos(double(angle)))*4.9f));
        if(extent<=f32(nominal+.4f))break;
    }
    const float spacing=std::max(nominal,f32(extent-.3f));
    const float midpoint=f32(f32(high+low)*.5f);
    float cursor=f32(midpoint-f32(f32(float(out.across_count)*spacing)+gap_width)*.5f);
    float left[2]={-i->forward[1],i->forward[0]};
    out.width=width;out.angle_radians=angle;
    for(int k=0;k<3;k++)out.centre[k]=i->centre[k];
    for(int k=0;k<2;k++)out.forward[k]=i->forward[k];
    auto strip=[&](float forward_offset,float lateral,float half_width){
        out.strip.active=1;out.strip.half_width=half_width;
        for(int k=0;k<2;k++){
            out.strip.forward[k]=i->forward[k];out.strip.left[k]=left[k];
            out.strip.position[k]=f32(f32(f32(forward_offset*i->forward[k])+i->centre[k])+f32(lateral*left[k]));
        }
        out.strip.position[2]=f32(i->centre[2]-.22f);
    };
    if(spikes && narrow)strip(-7,midpoint,f32(f32(width*.5f)+.4f));
    for(unsigned n=0;n<=out.across_count;n++){
        if(n==gap){if(spikes)strip(0,f32(f32(gap_width*.5f)+cursor),f32(f32(gap_width*.5f)+.4f));cursor=f32(cursor+gap_width);}
        if(n==out.across_count)break;
        auto &car=out.cars[n];const auto &candidate=i->candidates[selected[n]];car.id=candidate.id;car.release_chaser=candidate.selected;
        const float lateral=f32(f32(spacing*.5f)+cursor);cursor=f32(cursor+spacing);
        const float noisy=f32(f32(float(int(sample()%11)-5)*.01f)+((n&1)?angle:-angle));
        const float sine=float(std::sin(double(noisy))),cosine=float(std::cos(double(noisy)));
        const bool flip=(sample()&1)!=0;
        for(int k=0;k<2;k++){
            car.position[k]=f32(f32(lateral*left[k])+i->centre[k]);
            car.heading[k]=f32(f32(cosine*left[k])+f32(sine*i->forward[k]));if(flip)car.heading[k]=-car.heading[k];
        }
        car.position[2]=f32(i->centre[2]+.35f);
    }
    for(unsigned n=0;n<extra;n++){
        auto &car=out.cars[out.across_count+n];const auto &candidate=i->candidates[selected[out.across_count+n]];
        car.id=candidate.id;car.release_chaser=candidate.selected;
        const float lateral=f32(f32(f32(float(n+1)*width)/float(extra+1))+low);
        const float noisy=f32(float(int(sample()%21)-10)*.01f),sine=float(std::sin(double(noisy))),cosine=float(std::cos(double(noisy)));
        const bool flip=(sample()&1)!=0;
        for(int k=0;k<2;k++){
            car.position[k]=f32(f32(f32(i->forward[k]*9.f)+i->centre[k])+f32(lateral*left[k]));
            car.heading[k]=f32(f32(cosine*left[k])+f32(sine*i->forward[k]));if(flip)car.heading[k]=-car.heading[k];
        }
        car.position[2]=f32(i->centre[2]+.35f);
    }
    out.random_used=rng;*o=out;return 1;
}
extern "C" void copchase_roadblock_schedule_init(CopChaseRoadblockSchedule *s){if(s)*s={};}
extern "C" int copchase_roadblock_schedule(CopChaseRoadblockSchedule *s,const CopChaseRoadblockScheduleInput *i,CopChaseRoadblockScheduleOutput *o){
    if(!o)return 0;*o={};
    if(!s || !i || i->preset<0 || i->preset>3 || i->heat<1 || i->heat>5 || !finite(i->speed,0,200) ||
       !finite(i->chance_multiplier,0,10) || !finite(i->insane_roll_ms,100,120000))return 0;
    for(uint32_t r:i->random)if(r>32767)return 0;
    if(!i->active || !i->enabled || i->block_active || i->strip_active || i->arrest_phase ||
       !chance[i->heat-1] || i->speed<=12 || i->now_ms<=s->next_attempt_ms)return 1;
    auto sample=[&](){return i->random[o->random_used++];};
    if(s->pursuit_epoch!=i->pursuit_epoch){
        s->pursuit_epoch=i->pursuit_epoch;s->roll_succeeded=0;
        s->next_roll_ms=i->preset<3?i->now_ms+8000+sample()%4000:0;
    }
    if(s->next_roll_ms<i->now_ms){
        s->next_roll_ms=i->now_ms+(i->preset<3?8000+sample()%4000:uint32_t(i->insane_roll_ms));
        const float threshold=f32(f32(float(chance[i->heat-1])*i->chance_multiplier)*(i->preset==2?1.3f:1.f));
        s->roll_succeeded=float(sample()%100)<threshold;
    }
    if(s->roll_succeeded)o->request=1;else s->next_attempt_ms=i->now_ms+3000;
    return 1;
}
extern "C" void copchase_roadblock_attempted(CopChaseRoadblockSchedule *s,uint32_t now,int succeeded){
    if(!s)return;if(succeeded)s->roll_succeeded=0;else s->next_attempt_ms=now+3000;
}
extern "C" int copchase_roadblock_lifecycle(CopChaseRoadblockLifecycle *s,const CopChaseRoadblockLifecycleInput *i,enum CopChaseRoadblockRelease *o){
    if(!o)return 0;*o=COPCHASE_BLOCK_KEEP;
    if(!s || !i || !finite(i->speed,0,200) || !axis_ok(s->forward))return 0;
    for(int k=0;k<2;k++)if(!finite(s->centre[k],-20000,20000) || !finite(i->player_position[k],-20000,20000))return 0;
    if(!s->active)return 1;
    const float dx=f32(i->player_position[0]-s->centre[0]),dy=f32(i->player_position[1]-s->centre[1]);
    if(length(dx,dy)>=18 || i->speed>=3)s->stopped_ms=0;else if(!s->stopped_ms)s->stopped_ms=i->now_ms;
    if(!i->pursuit_active)*o=COPCHASE_BLOCK_PURSUIT_OVER;
    else if(dot(dx,dy,s->forward)>15)*o=COPCHASE_BLOCK_PASSED;
    else if(s->stopped_ms && uint32_t(i->now_ms-s->stopped_ms)>2500)*o=COPCHASE_BLOCK_STOPPED;
    else if(uint32_t(i->now_ms-s->created_ms)>60000)*o=COPCHASE_BLOCK_TIMEOUT;
    else if(i->recent_ram)*o=COPCHASE_BLOCK_RAMMED;
    if(*o!=COPCHASE_BLOCK_KEEP)s->active=0;
    return 1;
}
