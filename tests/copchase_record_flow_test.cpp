#include "copchase_record_flow.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>
static unsigned checks;
static void check(bool value,const char *label){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
struct Backend {
    bool full=false,select_full=false,blocked=false,active=false;
    uint64_t ticket=0,settled=0;
    CopChaseSessionStatus view{};
    std::vector<std::string> accepted;
    std::vector<CopChaseSessionFrame> events;
    static CopChaseSessionResult select(void *p,const char *id,uint64_t gen){auto &b=*static_cast<Backend*>(p);
        if(b.select_full)return COPCHASE_SESSION_FULL;
        b.accepted.push_back(std::string("select:")+id);b.view={};std::memcpy(b.view.profile_id,id,std::strlen(id)+1);
        b.view.profile_generation=gen;b.view.ready=*id!=0;b.active=false;return COPCHASE_SESSION_OK;}
    static CopChaseSessionResult publish(void *p,const CopChaseSessionFrame *f){auto &b=*static_cast<Backend*>(p);
        if(b.full)return COPCHASE_SESSION_FULL;
        if(f->profile_generation!=b.view.profile_generation || !b.view.profile_id[0])return COPCHASE_SESSION_STALE;
        if(f->events&COPCHASE_STARTED){if(b.active)return COPCHASE_SESSION_DUPLICATE;b.active=true;++b.ticket;b.accepted.push_back("start");}
        else if(f->events&COPCHASE_CANCELLED){if(!b.active)return COPCHASE_SESSION_DUPLICATE;b.active=false;b.accepted.push_back("cancel");}
        else if(f->events&(COPCHASE_EVADED|COPCHASE_BUSTED)){if(!b.active)return COPCHASE_SESSION_STALE;
            b.active=false;b.settled=b.ticket;b.accepted.push_back("finish");}
        else {std::memcpy(b.view.model_id,f->model_id,std::strlen(f->model_id)+1);b.view.heat_ready=1;return COPCHASE_SESSION_DUPLICATE;}
        b.events.push_back(*f);return COPCHASE_SESSION_OK;}
    static void snapshot(void *p,CopChaseSessionStatus *out){auto &b=*static_cast<Backend*>(p);*out=b.view;out->blocked=b.blocked;}
};
struct Fixture {
    Backend backend;
    CopChaseRecordFlow *flow;
    CopChaseSessionFrame frame{};
    CopChaseSessionStatus status{};
    Fixture(){CopChaseRecordFlowHost h{&backend,Backend::select,Backend::publish,Backend::snapshot};
        flow=copchase_record_flow_create(&h);check(flow!=nullptr,"create coordinator");}
    ~Fixture(){copchase_record_flow_destroy(flow);}
    bool before(const char *profile="profile_a",uint64_t gen=1,uint64_t world=1,uint64_t player=1,const char *model="FOCUS"){
        return copchase_record_flow_before(flow,profile,gen,world,player,model,&frame,&status)!=0;}
    void event(uint32_t e){frame.events=e;frame.phase=e==COPCHASE_STARTED?COPCHASE_PURSUIT:COPCHASE_IDLE;
        frame.result={};frame.result.heat=2;frame.result.fractional_heat=2.25f;frame.result.bounty=1234;frame.result.keep_heat=1;
        copchase_record_flow_after(flow,&frame);}
};
int main(){
    check(!copchase_record_flow_create(nullptr),"missing callbacks rejected");
    check(!copchase_record_flow_for_session(nullptr),"missing session rejected");
    {
        Fixture f;check(f.before(),"initial profile/model ready");f.backend.full=true;f.event(COPCHASE_STARTED);
        check(copchase_record_flow_pending(f.flow)==1 && !f.backend.active,"FULL has not reserved a session");
        check(!f.before(),"pending START holds gameplay");
        check(copchase_record_flow_holding(f.flow),"queue pressure requests a state-preserving game hold");
        // This is the root bridge's actual failure path: the disabled game
        // emits CANCEL even if the new frame had no model/identity at all.
        f.frame={};f.event(COPCHASE_CANCELLED);
        check(copchase_record_flow_pending(f.flow)==2,"CANCEL retained after blocked START");
        f.event(COPCHASE_CANCELLED);check(copchase_record_flow_pending(f.flow)==2,"duplicate cancellation does not grow queue");
        f.backend.full=false;check(f.before(),"drained cancellation permits gameplay again");
        check(!copchase_record_flow_holding(f.flow),"recovered queue releases game hold");
        check(!f.backend.active && f.backend.events.size()==2,"START then CANCEL delivered exactly once");
        check(f.backend.events[0].events==COPCHASE_STARTED && f.backend.events[1].events==COPCHASE_CANCELLED,"exact event ordering");
        check(!std::strcmp(f.backend.events[1].model_id,"FOCUS") && f.backend.events[1].world_generation==1,"cancellation retains START identity");
        f.event(COPCHASE_STARTED);check(f.backend.ticket==2 && f.backend.active,"next pursuit allocates a new ticket");
        f.event(COPCHASE_EVADED);check(f.backend.settled==2 && !f.backend.active,"terminal settles the second ticket only");
        check(copchase_record_flow_error(f.flow)==COPCHASE_SESSION_OK,"recovered pressure does not latch error");
    }
    {
        Fixture f;check(f.before(),"profile change fixture ready");f.backend.full=true;f.event(COPCHASE_STARTED);
        check(!f.before("profile_b",2,4,8,"COROLLA"),"new profile waits behind pending events");
        check(copchase_record_flow_pending(f.flow)==2 && !std::strcmp(f.backend.view.profile_id,"profile_a"),"old selection preserved until START/CANCEL drain");
        f.backend.full=false;check(f.before("profile_b",2,4,8,"COROLLA"),"new profile selected after old cancellation");
        check(f.backend.accepted.size()==4 && f.backend.accepted[1]=="start" && f.backend.accepted[2]=="cancel" &&
              f.backend.accepted[3]=="select:profile_b","profile select cannot overtake old lifecycle events");
        f.event(COPCHASE_STARTED);f.event(COPCHASE_BUSTED);
        check(f.backend.settled==2 && f.backend.events.back().profile_generation==2,"new profile result gets its own ticket and identity");
    }
    {
        Fixture f;check(f.before(),"terminal pressure fixture ready");f.event(COPCHASE_STARTED);f.backend.full=true;f.event(COPCHASE_EVADED);
        check(copchase_record_flow_pending(f.flow)==1,"terminal pending");
        check(!f.before("profile_b",2,2,2,"COROLLA"),"profile selection blocked by old terminal");
        f.frame={};f.event(COPCHASE_CANCELLED);check(copchase_record_flow_pending(f.flow)==1,"later cancellation cannot overwrite completed result");
        f.backend.full=false;check(f.before("profile_b",2,2,2,"COROLLA"),"terminal drains before profile select");
        check(f.backend.settled==1 && f.backend.events.back().events==COPCHASE_EVADED && f.backend.events.back().result.bounty==1234,"exact completed payload preserved");
        check(f.backend.accepted[2]=="finish" && f.backend.accepted[3]=="select:profile_b","settlement precedes selection");
    }
    {
        Fixture f;check(f.before(),"world change fixture ready");f.event(COPCHASE_STARTED);f.backend.full=true;
        check(!f.before("profile_a",1,2,3,"COROLLA"),"world/model change queues old cancellation");
        check(copchase_record_flow_pending(f.flow)==1,"one owned cancellation only");
        check(!f.before("profile_a",1,3,4,"CIVIC"),"further identity changes remain bounded");
        check(copchase_record_flow_pending(f.flow)==1,"rapid new identities cannot grow lifecycle queue");
        f.backend.full=false;check(f.before("profile_a",1,3,4,"CIVIC"),"latest model selected after cancellation");
        check(!std::strcmp(f.backend.events.back().model_id,"FOCUS") && f.backend.events.back().world_generation==1,"cancellation still targets original car/world");
        f.event(COPCHASE_STARTED);f.event(COPCHASE_CANCELLED);check(f.backend.ticket==2 && !f.backend.active,"new world has separate lifecycle");
    }
    {
        Fixture f;check(f.before(),"terminal durability fixture ready");f.event(COPCHASE_STARTED);
        f.event(COPCHASE_EVADED);f.backend.view.queued=1;
        check(!f.before() && copchase_record_flow_holding(f.flow),"accepted but uncommitted terminal holds despite old ready heat");
        check(f.status.ready && f.status.heat_ready,"test specifically covers stale ready flags");
        f.backend.view.queued=0;f.backend.view.retained_heat=3.6f;
        check(f.before() && !copchase_record_flow_holding(f.flow) && f.status.retained_heat==3.6f,"committed terminal exposes new heat and releases hold");
        f.event(COPCHASE_STARTED);f.backend.view.queued=1;
        check(f.before() && !copchase_record_flow_holding(f.flow),"accepted START may continue current pursuit while token is persisted");
    }
    {
        Fixture f;f.backend.select_full=true;check(!f.before(),"FULL selection retried without starting gameplay");
        check(!f.backend.view.ready && !copchase_record_flow_pending(f.flow),"unaccepted selection has no phantom owner");
        f.backend.select_full=false;check(f.before(),"selection retry ready");f.event(COPCHASE_STARTED);
        check(!f.before("",2,0,0,""),"missing profile cancels and clears selection");
        check(!copchase_record_flow_holding(f.flow),"identity loss is cancellation, not a resumable hold");
        check(!f.backend.active && !f.backend.view.profile_id[0],"invalidated save identity cannot retain active pursuit");
        check(!f.before(nullptr,3,0,0,nullptr),"null identity safely clears");
        check(copchase_record_flow_error(f.flow)==COPCHASE_SESSION_OK,"normal context loss is not a records fault");
    }
    {
        Fixture f;check(f.before(),"blocked worker fixture ready");f.event(COPCHASE_STARTED);f.backend.blocked=true;
        check(!f.before(),"worker IO hold disables new gameplay");f.backend.full=true;f.event(COPCHASE_CANCELLED);
        check(copchase_record_flow_pending(f.flow)==1,"worker hold preserves cancellation");
        f.backend.blocked=f.backend.full=false;check(f.before(),"worker recovery drains cancellation");
        check(!f.backend.active,"recovery cannot leave session active");
    }
    {
        Fixture f;check(f.before(),"stale terminal fixture ready");f.event(COPCHASE_STARTED);
        ++f.frame.player_generation;f.event(COPCHASE_BUSTED);
        check(!f.backend.active && !f.backend.settled && f.backend.events.back().events==COPCHASE_CANCELLED,"wrong player terminal cancels without settlement");
    }
    {
        // Exercise the production session callback adapter and real worker as
        // well as the deterministic FULL backend. Only a unique temp tree is written.
        namespace fs=std::filesystem;
        auto root=fs::temp_directory_path()/("copchase-record-flow-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        auto *session=copchase_session_start(root.u8string().c_str());check(session!=nullptr,"real session starts");
        auto *flow=copchase_record_flow_for_session(session);check(flow!=nullptr,"production session adapter");
        CopChaseSessionFrame frame{};CopChaseSessionStatus status{};
        copchase_record_flow_before(flow,"actual_profile",1,1,1,"FOCUS",&frame,&status);
        check(copchase_session_wait(session,5000)==COPCHASE_SESSION_OK,"real profile/model commands drain");
        check(copchase_record_flow_before(flow,"actual_profile",1,1,1,"FOCUS",&frame,&status),"real worker authorizes retained heat");
        frame.events=COPCHASE_STARTED;copchase_record_flow_after(flow,&frame);
        check(copchase_session_wait(session,5000)==COPCHASE_SESSION_OK,"real token durable");
        copchase_session_snapshot(session,&status);const uint64_t first=status.session_low;
        check(status.active && first && status.session_high,"real sequence reserved");
        frame.events=COPCHASE_CANCELLED;copchase_record_flow_after(flow,&frame);
        frame.events=COPCHASE_STARTED;copchase_record_flow_after(flow,&frame);
        frame.events=COPCHASE_EVADED;frame.result.heat=3;frame.result.fractional_heat=3.25f;frame.result.bounty=456;frame.result.keep_heat=1;
        copchase_record_flow_after(flow,&frame);
        check(copchase_session_wait(session,5000)==COPCHASE_SESSION_OK,"real ordered cancellation/new pursuit/result drain");
        copchase_session_snapshot(session,&status);
        check(!status.active && status.records.escapes==1 && status.records.total_bounty==456,"only completed real pursuit recorded");
        check(status.records.last.session_low==first+1 && status.retained_heat>2.92f && status.retained_heat<2.93f,"new durable token and original retained-heat calculation");
        copchase_record_flow_destroy(flow);check(copchase_session_stop(session)==COPCHASE_SESSION_OK,"real worker clean shutdown");
        fs::remove_all(root);
    }
    std::printf("CopChase record flow: %u checks passed\n",checks);
}
