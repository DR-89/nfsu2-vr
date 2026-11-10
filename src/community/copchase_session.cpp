#include "copchase_session.h"
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace {
namespace fs=std::filesystem;
constexpr size_t capacity=64;
bool id_ok(const char *s){if(!s || !*s)return false;for(size_t n=0;n<=64;++n){unsigned char c=static_cast<unsigned char>(s[n]);
    if(!c)return n>0;if(n==64 || !((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'))return false;}return false;}
void copy(char (&out)[65],const std::string &s){std::memset(out,0,65);std::memcpy(out,s.data(),s.size());}
bool metrics(const CopChaseSettlement &s){return s.preset<=3 && s.heat>=1 && s.heat<=5 &&
    std::isfinite(s.fractional_heat) && s.fractional_heat>=0 && s.fractional_heat<6 && s.bounty<=INT32_MAX &&
    s.infractions<=1000000 && s.cops_damaged<=1000000 && s.cops_taken_out<=1000000 && s.cost_to_state<=INT32_MAX &&
    s.elapsed_ms<=UINT32_MAX && (s.keep_heat==0 || s.keep_heat==1);}
void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=uint8_t(v>>(i*8));}
uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(i*8);return v;}
uint64_t crc(const uint8_t *p){uint64_t c=14695981039346656037ull;for(unsigned i=0;i<24;++i)c=(c^p[i])*1099511628211ull;return c;}
FILE *file(const fs::path &p,bool write){
#ifdef _WIN32
    FILE *f=nullptr;_wfopen_s(&f,p.c_str(),write?L"wb":L"rb");return f;
#else
    return std::fopen(p.c_str(),write?"wb":"rb");
#endif
}
bool entropy(uint64_t &value){
#ifdef _WIN32
    HMODULE lib=LoadLibraryW(L"advapi32.dll");if(!lib)return false;
    using Random=BOOLEAN(APIENTRY*)(void*,ULONG);auto fn=reinterpret_cast<Random>(GetProcAddress(lib,"SystemFunction036"));
    const bool ok=fn && fn(&value,sizeof value);FreeLibrary(lib);return ok && value;
#else
    int fd=::open("/dev/urandom",O_RDONLY);if(fd<0)return false;size_t done=0;
    while(done<sizeof value){const auto n=::read(fd,reinterpret_cast<char*>(&value)+done,sizeof value-done);if(n<=0)break;done+=size_t(n);}
    ::close(fd);return done==sizeof value && value;
#endif
}
struct Sequence {
    fs::path root,path,temp;uint64_t high=0,low=0;bool opened=false;
#ifdef _WIN32
    HANDLE lock=INVALID_HANDLE_VALUE;
#else
    int lock=-1;
#endif
    ~Sequence(){
#ifdef _WIN32
        if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);
#else
        if(lock>=0){flock(lock,LOCK_UN);::close(lock);}
#endif
    }
    bool flush(){std::array<uint8_t,32> bytes{};std::memcpy(bytes.data(),"CCSID01",7);
        put64(bytes.data()+8,high);put64(bytes.data()+16,low);put64(bytes.data()+24,crc(bytes.data()));
        FILE *f=file(temp,true);if(!f)return false;bool ok=std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size() && !std::fflush(f);
#ifdef _WIN32
        if(ok)ok=_commit(_fileno(f))==0;
#else
        if(ok)ok=fsync(fileno(f))==0;
#endif
        if(std::fclose(f))ok=false;if(!ok)return false;
#ifdef _WIN32
        return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        if(::rename(temp.c_str(),path.c_str()))return false;int fd=::open(root.c_str(),O_RDONLY|O_DIRECTORY);
        if(fd<0)return false;const bool synced=fsync(fd)==0;::close(fd);return synced;
#endif
    }
    bool open(const fs::path &directory){if(opened)return true;root=directory;path=root/"copchase-session.ids";temp=path;temp+=".new";
        std::error_code ec;fs::create_directories(root,ec);if(ec)return false;fs::path lp=path;lp+=".lock";
#ifdef _WIN32
        if(lock==INVALID_HANDLE_VALUE)lock=CreateFileW(lp.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        if(lock==INVALID_HANDLE_VALUE)return false;
#else
        if(lock<0){lock=::open(lp.c_str(),O_CREAT|O_RDWR,0600);if(lock<0)return false;
            if(flock(lock,LOCK_EX|LOCK_NB)){::close(lock);lock=-1;return false;}}
#endif
        bool exists=fs::exists(path,ec);if(ec)return false;
        if(exists){if(fs::file_size(path,ec)!=32 || ec)return false;std::array<uint8_t,32> b{};
            FILE *f=file(path,false);if(!f)return false;const bool ok=std::fread(b.data(),1,b.size(),f)==b.size() && !std::ferror(f);std::fclose(f);
            if(!ok || std::memcmp(b.data(),"CCSID01",8) || get64(b.data()+24)!=crc(b.data()))return false;
            high=get64(b.data()+8);low=get64(b.data()+16);if(!high)return false;
        }else{if(!entropy(high))return false;low=0;if(!flush())return false;}
        opened=true;return true;
    }
    bool reserve(uint64_t &h,uint64_t &l){if(!opened || low==UINT64_MAX)return false;
        ++low; // A failed attempt consumes a sequence number; never reuse it.
        if(!flush())return false;h=high;l=low;return true;}
};
enum Kind { Select,Model,Start,Finish,Cancel };
struct Job {Kind kind=Select;std::string profile,model;uint64_t generation=0,ticket=0;CopChaseSettlement settlement{};};
}
struct CopChaseSession {
    std::string root,desired,model;uint64_t generation=0,ticket=0,world=0,player=0;bool active=false,finished=false;
    std::mutex mutex;std::condition_variable cv;std::deque<Job> queue;bool stopping=false,retry=false,busy=false;
    CopChaseSessionStatus status{};std::thread worker;
    // Worker-only state below.
    Sequence sequence;CopChaseRecords *records=nullptr;std::string selected,selected_model;uint64_t selected_generation=0,worker_ticket=0,reserved_ticket=0,high=0,low=0;
    void reflect(){CopChaseRecordsStatus r{};float heat=1;if(records){copchase_records_snapshot(records,&r);
            if(!selected_model.empty())copchase_records_heat(records,selected_model.c_str(),&heat);}
        std::lock_guard<std::mutex> guard(mutex);status.records=r;
        if(selected==desired && selected_generation==generation){copy(status.profile_id,selected);copy(status.model_id,selected_model);
            status.profile_generation=selected_generation;status.retained_heat=heat;status.heat_ready=records && !selected_model.empty();
            status.ready=records!=nullptr;
            if(reserved_ticket==ticket){status.session_high=high;status.session_low=low;}}
    }
    bool process(const Job &j,CopChaseRecordResult &error){error=COPCHASE_RECORD_OK;
        if(!sequence.open(fs::u8path(root))){error=COPCHASE_RECORD_IO;return false;}
        if(j.kind==Select){if(records){error=copchase_records_flush(records);if(error!=COPCHASE_RECORD_OK)return false;
                copchase_records_close(records);records=nullptr;}
            selected=j.profile;selected_generation=j.generation;selected_model.clear();worker_ticket=reserved_ticket=high=low=0;
            if(!selected.empty()){records=copchase_records_open(root.c_str(),selected.c_str(),&error);if(!records)return false;}
            reflect();return true;}
        if(j.profile!=selected || j.generation!=selected_generation || !records){error=COPCHASE_RECORD_INVALID;return false;}
        selected_model=j.model;
        if(j.kind==Model){reflect();return true;}
        if(j.kind==Cancel){worker_ticket=reserved_ticket=high=low=0;reflect();return true;}
        if(j.kind==Start){if(!sequence.reserve(high,low)){error=COPCHASE_RECORD_IO;return false;}worker_ticket=reserved_ticket=j.ticket;reflect();return true;}
        if(j.ticket!=worker_ticket || !(high|low)){error=COPCHASE_RECORD_INVALID;return false;}
        auto result=j.settlement;result.session_high=high;result.session_low=low;copy(result.car_id,j.model);
        error=copchase_records_apply(records,&result);
        if(error!=COPCHASE_RECORD_OK && error!=COPCHASE_RECORD_DUPLICATE)return false;
        error=copchase_records_flush(records);if(error!=COPCHASE_RECORD_OK)return false;
        worker_ticket=0;reflect();return true;
    }
    void run(){for(;;){Job job;{
                std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[&]{return stopping || (!queue.empty() && (!status.blocked || retry));});
                if(stopping && (queue.empty() || status.blocked))break;
                retry=false;busy=true;job=queue.front();}
            CopChaseRecordResult error=COPCHASE_RECORD_IO;bool ok=false;
            try{ok=process(job,error);}catch(...){error=COPCHASE_RECORD_IO;}
            {std::lock_guard<std::mutex> lock(mutex);busy=false;
                if(ok){queue.pop_front();status.blocked=0;status.error=COPCHASE_SESSION_OK;status.record_error=COPCHASE_RECORD_OK;}
                else{status.blocked=1;status.error=COPCHASE_SESSION_IO;status.record_error=error;}cv.notify_all();}
        }
        if(records){copchase_records_close(records);records=nullptr;}
    }
};
extern "C" CopChaseSession *copchase_session_start(const char *root){
    if(!root || !*root)return nullptr;try{if(!fs::u8path(root).is_absolute())return nullptr;
        std::unique_ptr<CopChaseSession> s(new CopChaseSession);s->root=root;s->status.retained_heat=1;
        auto *p=s.get();s->worker=std::thread([p]{p->run();});return s.release();}catch(...){return nullptr;}
}
extern "C" CopChaseSessionResult copchase_session_select(CopChaseSession *s,const char *profile,uint64_t gen){
    if(!s || (profile && *profile && (!id_ok(profile) || !gen)))return COPCHASE_SESSION_INVALID;
    try{const std::string id=profile?profile:"";std::lock_guard<std::mutex> lock(s->mutex);if(s->stopping)return COPCHASE_SESSION_STOPPED;
        if(s->desired==id && s->generation==gen)return COPCHASE_SESSION_DUPLICATE;
        if(s->queue.size()>=capacity)return COPCHASE_SESSION_FULL;
        Job job;job.kind=Select;job.profile=id;job.generation=gen;s->queue.push_back(job);
        s->desired=id;s->generation=gen;s->model.clear();s->active=false;s->finished=false;s->status.heat_ready=0;s->status.ready=0;
        s->status.session_high=s->status.session_low=0;s->cv.notify_all();return COPCHASE_SESSION_OK;
    }catch(...){return COPCHASE_SESSION_IO;}
}
extern "C" CopChaseSessionResult copchase_session_publish(CopChaseSession *s,const CopChaseSessionFrame *f){
    if(!s || !f || !id_ok(f->model_id) || !f->profile_generation || !f->world_generation || !f->player_generation || f->phase>COPCHASE_COOLDOWN)
        return COPCHASE_SESSION_INVALID;
    const uint32_t terminal=f->events&(COPCHASE_EVADED|COPCHASE_BUSTED),start=f->events&COPCHASE_STARTED,cancel=f->events&COPCHASE_CANCELLED;
    if((terminal==(COPCHASE_EVADED|COPCHASE_BUSTED)) || (start && (terminal || cancel)) || (cancel && terminal) ||
       (start && f->phase!=COPCHASE_PURSUIT) || (terminal && (f->phase!=COPCHASE_IDLE || !metrics(f->result))))return COPCHASE_SESSION_INVALID;
    try{std::lock_guard<std::mutex> lock(s->mutex);if(s->stopping)return COPCHASE_SESSION_STOPPED;
        if(s->desired.empty() || s->generation!=f->profile_generation)return COPCHASE_SESSION_STALE;
        const std::string model=f->model_id;const bool identity=s->active && (s->world!=f->world_generation || s->player!=f->player_generation || s->model!=model);
        if(identity && !cancel)return COPCHASE_SESSION_STALE;
        Job job;job.profile=s->desired;job.generation=s->generation;job.model=model;job.ticket=s->ticket;
        if(start){if(s->active)return COPCHASE_SESSION_DUPLICATE;if(s->ticket==UINT64_MAX)return COPCHASE_SESSION_FULL;job.kind=Start;job.ticket=s->ticket+1;}
        else if(terminal){if(!s->active)return s->finished?COPCHASE_SESSION_DUPLICATE:COPCHASE_SESSION_STALE;job.kind=Finish;job.settlement=f->result;job.settlement.event=terminal;}
        else if(cancel){if(!s->active)return COPCHASE_SESSION_DUPLICATE;job.kind=Cancel;}
        else{if(s->model==model)return COPCHASE_SESSION_DUPLICATE;job.kind=Model;}
        if(s->queue.size()>=capacity)return COPCHASE_SESSION_FULL;s->queue.push_back(job);
        if(start){s->active=true;s->finished=false;s->ticket=job.ticket;s->world=f->world_generation;s->player=f->player_generation;
            s->status.session_high=s->status.session_low=0;}
        if(terminal || cancel){s->active=false;s->finished=terminal!=0;}
        if(cancel)s->status.session_high=s->status.session_low=0;
        if(s->model!=model)s->status.heat_ready=0;s->model=model;s->cv.notify_all();return COPCHASE_SESSION_OK;
    }catch(...){return COPCHASE_SESSION_IO;}
}
extern "C" void copchase_session_snapshot(CopChaseSession *s,CopChaseSessionStatus *out){if(!out)return;*out={};if(!s)return;
    std::lock_guard<std::mutex> lock(s->mutex);*out=s->status;out->active=s->active;out->queued=unsigned(s->queue.size());
    if(s->desired!=out->profile_id || s->generation!=out->profile_generation || s->model!=out->model_id)out->heat_ready=0;
}
extern "C" CopChaseSessionResult copchase_session_wait(CopChaseSession *s,unsigned timeout){if(!s)return COPCHASE_SESSION_INVALID;
    std::unique_lock<std::mutex> lock(s->mutex);if(!s->cv.wait_for(lock,std::chrono::milliseconds(timeout),[&]{return s->status.blocked || (s->queue.empty() && !s->busy);}))return COPCHASE_SESSION_FULL;
    return s->status.blocked?COPCHASE_SESSION_IO:COPCHASE_SESSION_OK;
}
extern "C" void copchase_session_retry(CopChaseSession *s){if(!s)return;std::lock_guard<std::mutex> lock(s->mutex);
    if(s->status.blocked){s->status.blocked=0;s->retry=true;s->cv.notify_all();}}
extern "C" CopChaseSessionResult copchase_session_stop(CopChaseSession *s){if(!s)return COPCHASE_SESSION_INVALID;
    {std::lock_guard<std::mutex> lock(s->mutex);s->stopping=true;s->cv.notify_all();}s->worker.join();
    const auto result=s->queue.empty()?COPCHASE_SESSION_OK:COPCHASE_SESSION_IO;delete s;return result;
}
