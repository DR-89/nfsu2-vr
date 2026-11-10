#include "copchase_incidents.h"
#include <cassert>
#include <cmath>
#include <limits>
CopChaseCollision sample(){CopChaseCollision i{};i.valid=i.player_valid=1;i.now_ms=10000;i.event_ms=10000;
    i.target_slot=2;i.cross_slot=-1;i.pending_trigger=-1;i.target_identity=123;i.strength=10;i.target_position[0]=5;i.player_velocity[0]=10;return i;}
int main(){CopChaseIncidents s;copchase_incidents_init(&s);auto c=copchase_incident_defaults();assert(c.cost_per_hit==250 && c.cost_per_taken_out==6000);
    assert(copchase_incidents_bind(&s,2,123));auto i=sample();auto out=copchase_incidents_collision(&s,&c,&i);
    assert(out.trigger_slot==2 && out.trigger_reason==1 && out.damaged_slot==2 && !out.new_infractions && !s.cost_to_state);
    assert(std::fabs(s.cops[2].health-.952f)<1e-6);copchase_incidents_begin(&s);i.pursuit=1;
    out=copchase_incidents_collision(&s,&c,&i);assert(out.trigger_slot==-1 && out.bounty_added==500 && out.cost_added==250);
    assert(out.new_infractions==(1u<<COPCHASE_ASSAULT) && s.cops_damaged==1 && s.infractions==1);
    i.now_ms=i.event_ms=11500;out=copchase_incidents_collision(&s,&c,&i);assert(!out.bounty_added);
    ++i.now_ms;++i.event_ms;out=copchase_incidents_collision(&s,&c,&i);assert(out.bounty_added==500 && !out.new_infractions && s.cops_damaged==1);
    i.strength=1000;out=copchase_incidents_collision(&s,&c,&i);assert(out.taken_out_slot==2 && out.bounty_added==5000 && out.cost_added==6000 && s.cops[2].wrecked);
    out=copchase_incidents_collision(&s,&c,&i);assert(out.damaged_slot==-1 && s.cops_taken_out==1);
    assert(copchase_incidents_bind(&s,2,124));i.target_identity=123;out=copchase_incidents_collision(&s,&c,&i);assert(!out.accepted);
    copchase_incidents_init(&s);assert(copchase_incidents_tick(&s,1,0,29,.5)==0);
    assert(copchase_incidents_tick(&s,1,0,44.7f,.5001f)==1);
    assert(copchase_incidents_tick(&s,1,0,44.71f,.05f)==2);
    assert(copchase_incidents_tick(&s,1,0,0,.049f)==16 && s.infractions==3);
    assert(!copchase_incidents_tick(&s,1,0,100,0));copchase_incidents_begin(&s);assert(!s.infractions);
    CopChaseWitness witnesses[3]={{0,1,0,{60,0,0}},{1,1,1,{5,0,0}},{3,1,0,{59,0,0}}};
    i=sample();i.target_slot=-1;i.target_identity=0;i.target_is_traffic=1;i.witnesses=witnesses;i.witness_count=3;
    out=copchase_incidents_collision(&s,&c,&i);assert(out.trigger_slot==3 && out.trigger_reason==2);
    i.pursuit=1;out=copchase_incidents_collision(&s,&c,&i);assert(out.new_infractions==8);
    i.paused=1;const auto before=s;out=copchase_incidents_collision(&s,&c,&i);assert(!out.accepted && s.infraction_mask==before.infraction_mask);
    i=sample();copchase_incidents_bind(&s,2,123);i.target_position[0]=7.5f;out=copchase_incidents_collision(&s,&c,&i);assert(out.trigger_slot==-1 && out.damaged_slot==-1);
    i.target_position[0]=5;i.strength=2.5f;out=copchase_incidents_collision(&s,&c,&i);assert(out.trigger_slot==-1);
    i.strength=10;i.event_ms=i.now_ms-1001;assert(!copchase_incidents_collision(&s,&c,&i).accepted);
    i.event_ms=i.now_ms-1000;assert(copchase_incidents_collision(&s,&c,&i).accepted);
    i.event_ms=i.now_ms;i.strength=std::numeric_limits<float>::quiet_NaN();assert(!copchase_incidents_collision(&s,&c,&i).accepted);
    out=copchase_incidents_rollover(&s,&c,2,123,1,1,0,.25f);assert(out.taken_out_slot==-1);
    out=copchase_incidents_rollover(&s,&c,2,123,1,1,0,.249f);assert(out.taken_out_slot==2 && out.bounty_added==5000);
    assert(copchase_incidents_rollover(&s,&c,2,123,1,1,0,.1f).taken_out_slot==-1);
    const float xy[2]={0,0},markers[2][2]={{12,0},{0,14}};float nearest=0;
    assert(!copchase_incidents_hiding(xy,markers,2,12,&nearest) && nearest==12);
    assert(copchase_incidents_hiding(xy,markers,2,12.001f,&nearest));
    assert(!copchase_incidents_hiding(xy,nullptr,0,12,&nearest) && nearest==1e9f);
}
