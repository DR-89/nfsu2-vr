#include "copchase_state.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

static void require(bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static bool close(double a,double b,double eps=.025){return std::fabs(a-b)<eps;}
struct Fixture {
    CopChaseState state{};
    CopChaseConfig config=copchase_config_defaults();
    CopChaseObservation cop{1,1,0,COPCHASE_VISIBLE,20,0};
    CopChaseInput input{};
    Fixture(){
        copchase_state_init(&state);input.world_generation=input.player_generation=1;
        input.valid_world=input.valid_player=input.allowed_mode=input.driving=input.complete_cop_scan=1;
        input.seconds=.1f;input.speed=31;input.cops=&cop;input.cop_count=1;
    }
    CopChaseOutput tick(){return copchase_state_step(&state,&config,&input);}
    CopChaseOutput run(double seconds){
        unsigned events=0;for(int n=int(std::lround(seconds/input.seconds));n>0;--n)events|=tick().events;
        auto o=state.output;o.events=events;return o;
    }
    void start(){require(run(4).events&COPCHASE_STARTED,"speeding starts after four-second load grace");}
};

static void defaults_and_limits(){
    auto c=copchase_config_defaults();require(c.style==COPCHASE_MOST_WANTED && c.start_heat==1 && c.trigger_speed==30 && c.trigger_distance==60,"package defaults");
    require(c.enabled==1 && c.slow_max_heat==2 && c.slow_max_chasers==3 && c.mw_career_bin==4 &&
            c.max_chasers==4 && c.traffic_cars==20 && c.chasers_scale==1 && c.disable_busted==0 &&
            c.hiding_spots==1 && c.unseen_seconds==7 && c.cooldown_percent==100,"all package configuration defaults");
    c.start_heat=20;c.style=-1;c.slow_max_heat=0;c.slow_max_chasers=20;c.max_chasers=100;c.mw_career_bin=30;
    c.unseen_seconds=std::numeric_limits<float>::quiet_NaN();c.cooldown_percent=-1;
    copchase_config_validate(&c);
    require(c.start_heat==5 && c.style==0 && c.slow_max_heat==1 && c.slow_max_chasers==8 && c.max_chasers==16 && c.mw_career_bin==14,"configuration integer bounds");
    require(c.unseen_seconds==7 && c.cooldown_percent==10,"finite timer configuration");
}
static void cooldown_profile_durations(){
    const float seconds[5]={20,45,75,90,120};
    for(int style=COPCHASE_MOST_WANTED;style<=COPCHASE_INSANE;++style){
        for(int h=1;h<=5;++h){
            Fixture f;f.config.style=style;f.config.start_heat=h;f.config.disable_busted=1;f.start();
            f.run(5);f.cop.visibility=COPCHASE_OCCLUDED;
            require(f.run(7).events&COPCHASE_LOST_SIGHT,"every profile loses sight after seven seconds");
            // Inspect the actual scalar meters using independent table values,
            // not the core's duration helper or an internal heat-profile lookup.
            const double duration=seconds[h-1]*(style==COPCHASE_ULTIMATE?1.2:style==COPCHASE_INSANE?1.5:1);
            require(close(f.state.output.cooldown,7/duration,.001),"verified profile lost-sight credit");
            require(!(f.run(duration-7-.2).events&COPCHASE_EVADED),"cooldown does not finish early");
            auto done=f.run(.2);
            if(!(done.events&COPCHASE_EVADED))std::fprintf(stderr,"cooldown style=%d heat=%d duration=%.6f fill=%.9f\n",style,h,duration,done.cooldown);
            require(done.events&COPCHASE_EVADED,"verified full cooldown elapsed");
        }
    }
}
static void trigger_and_identity(){
    Fixture f;f.cop.visibility=COPCHASE_UNKNOWN;require(!(f.run(10).events&COPCHASE_STARTED),"unknown sight cannot start");
    f.cop.visibility=COPCHASE_OCCLUDED;require(!(f.run(1).events&COPCHASE_STARTED),"occluded cop cannot trigger speeding");
    f.cop.visibility=COPCHASE_VISIBLE;f.input.speed=30;require(!(f.run(1).events&COPCHASE_STARTED),"strict speeding threshold");
    f.input.speed=31;f.cop.distance=60;require(!(f.run(1).events&COPCHASE_STARTED),"strict distance threshold");
    f.cop.distance=59;require(f.tick().events&COPCHASE_STARTED,"confirmed visible cop starts chase");
    f.input.world_generation=2;auto o=f.tick();require(o.events==COPCHASE_CANCELLED && o.phase==COPCHASE_IDLE,"world generation releases pursuit");
    require(!(f.run(3.8).events&COPCHASE_STARTED),"new world has fresh load grace");
    require(f.run(.2).events&COPCHASE_STARTED,"new world can start safely");
    f.input.player_generation=2;o=f.tick();require(o.events==COPCHASE_CANCELLED,"reused player identity cancels");
    f.run(4);f.input.valid_player=0;f.input.paused=1;o=f.tick();require(o.events==COPCHASE_CANCELLED,"invalid player cancels even while paused");
    Fixture ram;ram.input.speed=0;ram.cop.visibility=COPCHASE_OCCLUDED;ram.input.rammed_police=1;
    require(ram.run(4).events&COPCHASE_STARTED,"verified collision can start below speed threshold");
    Fixture nearest;CopChaseObservation pair[]={{1,1,0,COPCHASE_OCCLUDED,10,0},{1,1,0,COPCHASE_VISIBLE,20,0}};
    nearest.input.cops=pair;nearest.input.cop_count=2;
    require(!(nearest.run(5).events&COPCHASE_STARTED),"original speeding trigger tests nearest patrol's sight");
    pair[0].usable=0;require(nearest.tick().events&COPCHASE_STARTED,"nearest eligible visible patrol triggers");
}
static void pause_invalid_and_unknown(){
    Fixture f;f.start();auto before=f.state.output;f.input.paused=1;
    f.run(30);require(f.state.output.pursuit_seconds==before.pursuit_seconds,"pause freezes pursuit time");
    f.input.paused=0;f.input.seconds=std::numeric_limits<float>::infinity();f.tick();
    require(f.state.output.pursuit_seconds==before.pursuit_seconds,"invalid delta rejected");
    f.input.seconds=100;f.tick();require(close(f.state.output.pursuit_seconds,.25,1e-6),"stall cannot fast forward pursuit");
    f.input.seconds=.1f;f.cop.visibility=COPCHASE_UNKNOWN;
    require(!(f.run(30).events&(COPCHASE_LOST_SIGHT|COPCHASE_BUSTED|COPCHASE_EVADED)),"unknown visibility cannot change contact state");
    require(f.state.output.unseen==0 && f.state.output.busted==0,"unknown data holds visibility-dependent meters");
    f.input.complete_cop_scan=0;f.cop.visibility=COPCHASE_OCCLUDED;
    require(!(f.run(30).events&COPCHASE_LOST_SIGHT),"incomplete census does not manufacture escape");
}
static void cooldown_and_reacquire(){
    Fixture f;f.start();f.cop.visibility=COPCHASE_OCCLUDED;
    require(!(f.run(11.8).events&COPCHASE_LOST_SIGHT),"first-five-second sight grace plus unseen timer");
    auto o=f.run(.3);require(o.events&COPCHASE_LOST_SIGHT,"lost sight enters cooldown");
    require(o.phase==COPCHASE_COOLDOWN && close(o.cooldown,.35,.02),"initial lost-sight time credits cooldown");
    f.cop.visibility=COPCHASE_UNKNOWN;float fill=f.state.output.cooldown;f.run(10);
    require(f.state.output.cooldown==fill,"unknown sight does not finish cooldown");
    f.cop.visibility=COPCHASE_VISIBLE;o=f.tick();require(o.events&COPCHASE_SPOTTED,"visible cop resumes pursuit");
    require(o.phase==COPCHASE_PURSUIT && o.cooldown==fill,"reacquisition retains cooldown credit");
    f.run(1);require(close(f.state.output.cooldown,fill-.1,.002),"retained cooldown drains over ten seconds");
    f.cop.visibility=COPCHASE_OCCLUDED;f.run(7.1);require(f.state.output.phase==COPCHASE_COOLDOWN,"can lose sight again");
    o=f.run(13.1);require(o.events&COPCHASE_EVADED,"heat-one cooldown completes");
    require(o.phase==COPCHASE_IDLE && o.retained_heat>=1,"escape emits retained heat without writing a save");
    f.cop.visibility=COPCHASE_VISIBLE;require(!(f.run(9.5).events&COPCHASE_STARTED),"post-escape grace");
    require(f.run(.6).events&COPCHASE_STARTED,"police search resumes after ten seconds");
}
static void arrest_rules(){
    Fixture f;f.start();f.input.speed=0;f.cop.distance=14;
    auto o=f.run(4.9);require(!(o.events&COPCHASE_BUSTED) && close(o.busted,.98,.002),"five-second arrest fill");
    f.cop.vertical_distance=2;f.run(1);require(close(f.state.output.busted,.58,.002),"different elevation drains arrest");
    f.cop.vertical_distance=0;f.cop.selected=0;f.run(1);require(close(f.state.output.busted,.18,.002),"unselected patrol cannot arrest");
    f.cop.selected=1;o=f.run(4.2);require(o.events&COPCHASE_BUSTED,"confirmed nearby selected police arrest");
    require(o.phase==COPCHASE_IDLE && o.retained_heat==0,"bust clears retained heat");
    Fixture disabled;disabled.config.disable_busted=1;disabled.start();disabled.input.speed=0;disabled.cop.distance=1;
    require(!(disabled.run(30).events&COPCHASE_BUSTED) && disabled.state.output.busted==0,"disable busted respected");
}
static void heat_and_hiding(){
    Fixture f;f.start();f.config.disable_busted=1;f.run(119.9);
    require(f.state.output.heat==1,"default MW heat timing below120s");
    require(f.run(.2).events&COPCHASE_HEAT_CHANGED,"default MW heat rises after120s");
    require(f.state.output.heat==2 && f.state.output.max_chasers==5,"heat-two fleet profile");
    Fixture ultimate;ultimate.config.style=COPCHASE_ULTIMATE;ultimate.start();ultimate.run(80.1);
    require(ultimate.state.output.heat==2 && ultimate.state.output.max_chasers==7,"Ultimate1.5x heat time and two more chasers");
    Fixture limited;limited.config.mw_career_bin=14;limited.config.start_heat=2;limited.start();limited.run(1000);
    require(limited.state.output.heat==2,"MW career-bin ceiling");
    Fixture hiding;hiding.start();hiding.run(5);hiding.cop.visibility=COPCHASE_OCCLUDED;hiding.run(7.1);
    hiding.input.speed=0;hiding.input.hiding_spot=1;auto o=hiding.run(2.7);
    require(o.events&COPCHASE_EVADED,"binary heat-one hiding speed5x (not README shorthand3x)");
    Fixture retained;retained.input.retained_heat=3.75f;retained.start();
    require(retained.state.output.heat==3 && close(retained.state.heat_value,3.75,.001),"host-verified per-car heat accepted");
    Fixture slow;slow.config.style=COPCHASE_SLOW;slow.config.start_heat=5;slow.start();
    require(slow.state.output.heat==2 && slow.state.output.max_chasers==3,"Slow heat and chaser caps");
    Fixture insane;insane.config.style=COPCHASE_INSANE;insane.start();
    require(insane.state.output.max_chasers==8,"Insane default custom fleet");
}
int main(){
    defaults_and_limits();trigger_and_identity();pause_invalid_and_unknown();cooldown_and_reacquire();arrest_rules();heat_and_hiding();cooldown_profile_durations();
    std::puts("CopChase state: lifecycle, sight uncertainty, pause, timing, cooldown, arrest, heat and style checks PASS");
}
