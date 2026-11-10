#include "copchase_record_flow.h"
#include <array>
#include <cstring>
#include <new>

namespace {
bool id(const char *s) {
    if(!s || !*s)return false;
    for(unsigned i=0;i<=64;++i){const unsigned char c=static_cast<unsigned char>(s[i]);
        if(!c)return true;
        if(i==64 || !((c>='a' && c<='z') || (c>='A' && c<='Z') ||
           (c>='0' && c<='9') || c=='_' || c=='-'))return false;
    }return false;
}
bool same(const CopChaseSessionFrame &a,const CopChaseSessionFrame &b) {
    return a.profile_generation==b.profile_generation && a.world_generation==b.world_generation &&
        a.player_generation==b.player_generation && !std::strcmp(a.model_id,b.model_id);
}
bool accepted(CopChaseSessionResult r) {return r==COPCHASE_SESSION_OK || r==COPCHASE_SESSION_DUPLICATE;}
}
struct CopChaseRecordFlow {
    CopChaseRecordFlowHost host{};
    std::array<CopChaseSessionFrame,4> pending{};
    unsigned head=0,count=0;
    char selected[65]{};
    uint64_t selected_generation=0;
    CopChaseSessionFrame owner{};
    bool active=false,terminal_pending=false,holding=false;
    CopChaseSessionResult error=COPCHASE_SESSION_OK;
    bool push(const CopChaseSessionFrame &f) {
        if(count==pending.size()){error=COPCHASE_SESSION_FULL;return false;}
        pending[(head+count)%pending.size()]=f;++count;return true;
    }
    bool pump() {
        if(error!=COPCHASE_SESSION_OK)return false;
        while(count){const auto result=host.publish(host.user,&pending[head]);
            if(result==COPCHASE_SESSION_FULL)return false;
            if(!accepted(result)){error=result;return false;}
            head=(head+1)%pending.size();--count;
        }return true;
    }
    void cancel() {
        if(!active)return;
        auto f=owner;f.phase=COPCHASE_IDLE;f.events=COPCHASE_CANCELLED;f.result={};
        if(push(f))active=false;
    }
};
extern "C" CopChaseRecordFlow *copchase_record_flow_create(const CopChaseRecordFlowHost *h) {
    if(!h || !h->select || !h->publish || !h->snapshot)return nullptr;
    auto *f=new(std::nothrow) CopChaseRecordFlow;if(f)f->host=*h;return f;
}
extern "C" CopChaseRecordFlow *copchase_record_flow_for_session(CopChaseSession *session) {
    if(!session)return nullptr;
    const CopChaseRecordFlowHost h{session,
        [](void *s,const char *id,uint64_t gen){return copchase_session_select(static_cast<CopChaseSession*>(s),id,gen);},
        [](void *s,const CopChaseSessionFrame *f){return copchase_session_publish(static_cast<CopChaseSession*>(s),f);},
        [](void *s,CopChaseSessionStatus *out){copchase_session_snapshot(static_cast<CopChaseSession*>(s),out);}};
    return copchase_record_flow_create(&h);
}
extern "C" void copchase_record_flow_destroy(CopChaseRecordFlow *f) {delete f;}
extern "C" int copchase_record_flow_before(CopChaseRecordFlow *f,const char *profile,
    uint64_t generation,uint64_t world,uint64_t player,const char *model,
    CopChaseSessionFrame *frame,CopChaseSessionStatus *status) {
    if(!frame || !status)return 0;*frame={};*status={};if(!f)return 0;f->holding=false;
    const bool profile_valid=id(profile) && generation;
    const bool valid=profile_valid && id(model) && world && player;
    if(valid){frame->profile_generation=generation;frame->world_generation=world;
        frame->player_generation=player;std::memcpy(frame->model_id,model,std::strlen(model)+1);}
    // Cancellation is queued even while START is waiting for worker capacity.
    // Selection never overtakes either event, including a rapid profile reload.
    if(f->active && (!valid || !same(f->owner,*frame) || std::strcmp(f->selected,profile)))f->cancel();
    if(!f->pump()){f->holding=valid && f->error==COPCHASE_SESSION_OK;return 0;}
    const char *selection=profile_valid?profile:"";
    if(f->selected_generation!=generation || std::strcmp(f->selected,selection)){
        const auto result=f->host.select(f->host.user,selection,generation);
        if(result==COPCHASE_SESSION_FULL){f->holding=valid;return 0;}
        if(!accepted(result)){f->error=result;return 0;}
        std::memcpy(f->selected,selection,std::strlen(selection)+1);f->selected_generation=generation;
    }
    if(!valid)return 0;
    const auto result=f->host.publish(f->host.user,frame);
    if(result==COPCHASE_SESSION_FULL){f->holding=true;return 0;}
    if(!accepted(result)){f->error=result;return 0;}
    f->host.snapshot(f->host.user,status);
    // Queue removal happens after the worker commits and reflects the exact
    // terminal result. Until then the previous model heat is stale, even if
    // ready/heat_ready are still true. Never authorize a new pursuit with it.
    if(f->terminal_pending && !status->blocked && !status->queued)f->terminal_pending=false;
    const bool ready=!f->terminal_pending && status->ready && status->heat_ready && !status->blocked &&
        status->profile_generation==generation && !std::strcmp(status->profile_id,profile) &&
        !std::strcmp(status->model_id,model);
    f->holding=!ready;return ready;
}
extern "C" void copchase_record_flow_after(CopChaseRecordFlow *f,const CopChaseSessionFrame *frame) {
    if(!f || !frame || f->error!=COPCHASE_SESSION_OK)return;
    const unsigned events=frame->events&(COPCHASE_STARTED|COPCHASE_CANCELLED|COPCHASE_EVADED|COPCHASE_BUSTED);
    if(!events)return;
    if(events&(events-1)){f->error=COPCHASE_SESSION_INVALID;return;}
    if(events==COPCHASE_CANCELLED)f->cancel();
    else if(events==COPCHASE_STARTED){
        if(!id(frame->model_id) || !frame->profile_generation || !frame->world_generation ||
           !frame->player_generation || frame->profile_generation!=f->selected_generation || !*f->selected){
            f->error=COPCHASE_SESSION_STALE;return;
        }
        if(f->active){if(!same(f->owner,*frame))f->error=COPCHASE_SESSION_STALE;return;}
        auto start=*frame;start.events=COPCHASE_STARTED;start.phase=COPCHASE_PURSUIT;
        if(f->push(start)){f->owner=start;f->active=true;}
    }else if(f->active){
        // A terminal from a new identity cannot settle the old pursuit. A
        // before-step cancellation owns that transition and wins over stale UI.
        if(!same(f->owner,*frame)){f->cancel();}
        else {auto finish=*frame;finish.events=events;finish.phase=COPCHASE_IDLE;
            if(f->push(finish)){f->active=false;f->terminal_pending=true;}}
    }
    f->pump();
    if(f->error==COPCHASE_SESSION_OK && (f->count || f->terminal_pending))f->holding=true;
}
extern "C" CopChaseSessionResult copchase_record_flow_error(const CopChaseRecordFlow *f) {
    return f?f->error:COPCHASE_SESSION_INVALID;
}
extern "C" unsigned copchase_record_flow_pending(const CopChaseRecordFlow *f) {return f?f->count:0;}
extern "C" int copchase_record_flow_holding(const CopChaseRecordFlow *f) {return f && f->holding;}
