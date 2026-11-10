#include "copchase_session.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
namespace {
namespace fs=std::filesystem;
struct Root {fs::path path=fs::temp_directory_path()/("nfsu2-copchase-session-test-"+
    std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    Root(){const bool made=fs::create_directories(path);assert(made);(void)made;}
    ~Root(){assert(fs::equivalent(path.parent_path(),fs::temp_directory_path()) && path.filename().string().find("nfsu2-copchase-session-test-")==0);fs::remove_all(path);}
    std::string name(){return path.u8string();}};
CopChaseSessionFrame frame(uint32_t events=0){CopChaseSessionFrame f{};f.profile_generation=1;f.world_generation=2;f.player_generation=3;
    std::memcpy(f.model_id,"PEUGOT",7);f.events=events;f.phase=events&COPCHASE_STARTED?COPCHASE_PURSUIT:COPCHASE_IDLE;
    f.result.preset=COPCHASE_MOST_WANTED;f.result.heat=4;f.result.fractional_heat=4.5f;f.result.bounty=7000;
    f.result.infractions=3;f.result.cops_damaged=2;f.result.cops_taken_out=1;f.result.cost_to_state=6500;f.result.elapsed_ms=70000;f.result.keep_heat=1;return f;}
CopChaseSessionStatus snap(CopChaseSession *s){CopChaseSessionStatus out{};copchase_session_snapshot(s,&out);return out;}
void ready(CopChaseSession *s){const auto result=copchase_session_wait(s,10000);if(result!=COPCHASE_SESSION_OK){auto v=snap(s);
    std::fprintf(stderr,"wait result=%d blocked=%d queued=%u record_error=%d\n",int(result),v.blocked,v.queued,int(v.record_error));}assert(result==COPCHASE_SESSION_OK);}
void basic_and_restart(){Root r;auto *s=copchase_session_start(r.name().c_str());assert(s);
    auto f=frame();assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_STALE);
    assert(copchase_session_select(s,"career-a",1)==COPCHASE_SESSION_OK);
    assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);ready(s);assert(snap(s).heat_ready && snap(s).retained_heat==1);
    f=frame(COPCHASE_STARTED);assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);
    assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_DUPLICATE);ready(s);auto first=snap(s);assert(first.session_high && first.session_low);
    f=frame(COPCHASE_EVADED);assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);
    assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_DUPLICATE);ready(s);
    assert(snap(s).records.pursuits==1 && snap(s).records.total_bounty==7000 && std::fabs(snap(s).retained_heat-4.05f)<1e-6);
    assert(copchase_session_stop(s)==COPCHASE_SESSION_OK);
    s=copchase_session_start(r.name().c_str());assert(s);assert(copchase_session_select(s,"career-a",2)==COPCHASE_SESSION_OK);
    f=frame();f.profile_generation=2;assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);ready(s);assert(snap(s).records.pursuits==1);
    f.events=COPCHASE_STARTED;f.phase=COPCHASE_PURSUIT;assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);ready(s);
    assert(snap(s).session_high==first.session_high && snap(s).session_low>first.session_low);
    f.events=COPCHASE_BUSTED;f.phase=COPCHASE_IDLE;assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);ready(s);
    assert(snap(s).records.pursuits==2 && snap(s).records.busts==1 && snap(s).records.total_bounty==7000 && snap(s).retained_heat==1);
    assert(copchase_session_stop(s)==COPCHASE_SESSION_OK);
}
void identity_cancel_and_models(){Root r;auto *s=copchase_session_start(r.name().c_str());assert(s);
    assert(copchase_session_select(s,"career-a",1)==COPCHASE_SESSION_OK);auto f=frame(COPCHASE_STARTED);
    assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);f=frame(COPCHASE_EVADED);++f.world_generation;
    assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_STALE);f.events=COPCHASE_CANCELLED;
    assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);ready(s);assert(!snap(s).records.pursuits);
    f=frame(COPCHASE_STARTED);assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);
    f=frame(COPCHASE_EVADED);assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);
    assert(copchase_session_select(s,"career-b",4)==COPCHASE_SESSION_OK);f=frame();f.profile_generation=4;
    assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);ready(s);assert(!snap(s).records.pursuits && snap(s).retained_heat==1);
    assert(copchase_session_select(s,"career-a",5)==COPCHASE_SESSION_OK);f.profile_generation=5;std::memcpy(f.model_id,"GOLF",5);
    assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);ready(s);assert(snap(s).records.pursuits==1 && snap(s).retained_heat==1);
    std::memcpy(f.model_id,"PEUGOT",7);assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);ready(s);assert(snap(s).retained_heat>4);
    assert(copchase_session_select(s,nullptr,6)==COPCHASE_SESSION_OK);ready(s);assert(!snap(s).ready && !snap(s).heat_ready);
    assert(copchase_session_stop(s)==COPCHASE_SESSION_OK);
}
void failed_flush_retry_and_queue(){Root r;auto *s=copchase_session_start(r.name().c_str());assert(s);
    assert(copchase_session_select(s,"career-a",1)==COPCHASE_SESSION_OK);auto f=frame(COPCHASE_STARTED);
    assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);ready(s);const auto temp=r.path/"copchase-career-a.records.new";
    const bool made=fs::create_directory(temp);assert(made);(void)made;f=frame(COPCHASE_EVADED);assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);
    assert(copchase_session_wait(s,10000)==COPCHASE_SESSION_IO && snap(s).blocked && snap(s).queued==1);
    unsigned accepted=0;for(unsigned n=0;n<100;++n){const std::string name="career_"+std::to_string(n);auto result=copchase_session_select(s,name.c_str(),n+2);
        if(result==COPCHASE_SESSION_FULL)break;assert(result==COPCHASE_SESSION_OK);++accepted;}
    assert(accepted==63 && snap(s).queued==64);
    const bool removed=fs::remove(temp);assert(removed);(void)removed;copchase_session_retry(s);ready(s);assert(!snap(s).blocked && !snap(s).queued);
    assert(copchase_session_select(s,"career-a",1000)==COPCHASE_SESSION_OK);ready(s);
    assert(snap(s).records.pursuits==1 && snap(s).records.total_bounty==7000);
    assert(copchase_session_stop(s)==COPCHASE_SESSION_OK);
}
void reservation_failure_and_locks(){Root r;auto *s=copchase_session_start(r.name().c_str());assert(s);
    assert(copchase_session_select(s,"career-a",1)==COPCHASE_SESSION_OK);ready(s);
    auto *other=copchase_session_start(r.name().c_str());assert(other);assert(copchase_session_select(other,"career-b",1)==COPCHASE_SESSION_OK);
    assert(copchase_session_wait(other,10000)==COPCHASE_SESSION_IO);assert(copchase_session_stop(other)==COPCHASE_SESSION_IO);
    const auto temp=r.path/"copchase-session.ids.new";const bool made=fs::create_directory(temp);assert(made);(void)made;
    auto f=frame(COPCHASE_STARTED);assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);
    assert(copchase_session_wait(s,10000)==COPCHASE_SESSION_IO && !snap(s).session_low);
    const bool removed=fs::remove(temp);assert(removed);(void)removed;copchase_session_retry(s);ready(s);assert(snap(s).session_low==2);
    // Reserve-only interrupted pursuits still consume durable IDs.
    const auto old=snap(s);assert(copchase_session_stop(s)==COPCHASE_SESSION_OK);s=copchase_session_start(r.name().c_str());
    assert(copchase_session_select(s,"career-a",1)==COPCHASE_SESSION_OK);assert(copchase_session_publish(s,&f)==COPCHASE_SESSION_OK);ready(s);
    assert(snap(s).session_low>old.session_low && !snap(s).records.pursuits);assert(copchase_session_stop(s)==COPCHASE_SESSION_OK);
    std::ofstream broken(r.path/"copchase-session.ids",std::ios::binary|std::ios::trunc);broken<<"broken";broken.close();
    s=copchase_session_start(r.name().c_str());assert(copchase_session_select(s,"career-a",1)==COPCHASE_SESSION_OK);
    assert(copchase_session_wait(s,10000)==COPCHASE_SESSION_IO);assert(copchase_session_stop(s)==COPCHASE_SESSION_IO);
    assert(fs::file_size(r.path/"copchase-session.ids")==6);
}
}
int main(){assert(!copchase_session_start("relative"));basic_and_restart();identity_cancel_and_models();failed_flush_retry_and_queue();reservation_failure_and_locks();}
