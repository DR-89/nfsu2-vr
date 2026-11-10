#include "copchase_records.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>
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
constexpr size_t header_size=128,record_size=192,max_records=16384,max_cars=256;
using Bytes=std::array<uint8_t,record_size>;
using Session=std::pair<uint64_t,uint64_t>;
namespace fs=std::filesystem;
bool id_ok(const char *id){
    if(!id || !*id)return false;
    for(unsigned n=0;n<=64;++n){const unsigned char c=static_cast<unsigned char>(id[n]);if(!c)return n>0;
        if(n==64 || !((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'))return false;
    }return false;
}
void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8));}
void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=uint8_t(v>>(i*8));}
uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(i*8);return v;}
uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(i*8);return v;}
uint32_t checksum(const uint8_t *p,size_t n){uint32_t c=~0u;
    while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;
}
bool valid(const CopChaseSettlement &s){return (s.session_high || s.session_low) &&
    (s.event==COPCHASE_EVADED || s.event==COPCHASE_BUSTED) && s.preset<=3 && s.heat>=1 && s.heat<=5 &&
    std::isfinite(s.fractional_heat) && s.fractional_heat>=0 && s.fractional_heat<6 &&
    s.bounty<=INT32_MAX && s.infractions<=1000000 && s.cops_damaged<=1000000 &&
    s.cops_taken_out<=1000000 && s.cost_to_state<=INT32_MAX && s.elapsed_ms<=UINT32_MAX &&
    (s.keep_heat==0 || s.keep_heat==1) && id_ok(s.car_id);
}
Bytes encode(const CopChaseSettlement &s){Bytes b{};put32(b.data(),0x31525645u);put64(b.data()+4,s.session_high);
    put64(b.data()+12,s.session_low);put32(b.data()+20,s.event);put32(b.data()+24,s.preset);put32(b.data()+28,s.heat);
    uint32_t heat;std::memcpy(&heat,&s.fractional_heat,4);put32(b.data()+32,heat);put32(b.data()+36,s.bounty);
    put32(b.data()+40,s.infractions);put32(b.data()+44,s.cops_damaged);put32(b.data()+48,s.cops_taken_out);
    put32(b.data()+52,s.cost_to_state);put64(b.data()+56,s.elapsed_ms);put32(b.data()+64,uint32_t(s.keep_heat));
    std::memcpy(b.data()+68,s.car_id,std::strlen(s.car_id));put32(b.data()+188,checksum(b.data(),188));return b;
}
bool decode(const Bytes &b,CopChaseSettlement &s){
    if(get32(b.data())!=0x31525645u || get32(b.data()+188)!=checksum(b.data(),188))return false;
    s={};s.session_high=get64(b.data()+4);s.session_low=get64(b.data()+12);s.event=get32(b.data()+20);
    s.preset=get32(b.data()+24);s.heat=get32(b.data()+28);uint32_t heat=get32(b.data()+32);std::memcpy(&s.fractional_heat,&heat,4);
    s.bounty=get32(b.data()+36);s.infractions=get32(b.data()+40);s.cops_damaged=get32(b.data()+44);
    s.cops_taken_out=get32(b.data()+48);s.cost_to_state=get32(b.data()+52);s.elapsed_ms=get64(b.data()+56);
    s.keep_heat=int(get32(b.data()+64));std::memcpy(s.car_id,b.data()+68,65);
    // Canonical encoding rejects unterminated IDs, junk trailing data and
    // unknown extension bytes instead of silently accepting a new schema.
    return valid(s) && encode(s)==b;
}
float retained(const CopChaseSettlement &s){
    if(s.event==COPCHASE_BUSTED)return 1;
    if(s.preset==COPCHASE_INSANE)return float(s.heat);
    const float h=s.fractional_heat>=s.heat && s.fractional_heat<s.heat+1?s.fractional_heat:float(s.heat);
    return std::max(1.f,h*.9f); // ASI10028600 HeatF, then truncated integer Heat
}
FILE *open_file(const fs::path &p,bool write){
#ifdef _WIN32
    FILE *f=nullptr;_wfopen_s(&f,p.c_str(),write?L"wb":L"rb");return f;
#else
    return std::fopen(p.c_str(),write?"wb":"rb");
#endif
}
bool sync_file(FILE *f){if(std::fflush(f))return false;
#ifdef _WIN32
    return _commit(_fileno(f))==0;
#else
    return fsync(fileno(f))==0;
#endif
}
}
struct CopChaseRecords {
    std::mutex mutex,io_mutex;fs::path root,path,temp,lock_path;std::string profile;
    std::vector<Bytes> events;std::map<Session,size_t> sessions;std::map<std::string,float> cars;
    CopChaseRecordsStatus status{};
#ifdef _WIN32
    HANDLE lock=INVALID_HANDLE_VALUE;
#else
    int lock=-1;
#endif
    ~CopChaseRecords(){
#ifdef _WIN32
        if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);
#else
        if(lock>=0){flock(lock,LOCK_UN);::close(lock);}
#endif
    }
    CopChaseRecordResult acquire(){
#ifdef _WIN32
        lock=CreateFileW(lock_path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        if(lock!=INVALID_HANDLE_VALUE)return COPCHASE_RECORD_OK;const DWORD error=GetLastError();
        return error==ERROR_SHARING_VIOLATION || error==ERROR_LOCK_VIOLATION?COPCHASE_RECORD_LOCKED:COPCHASE_RECORD_IO;
#else
        lock=::open(lock_path.c_str(),O_CREAT|O_RDWR,0600);if(lock<0)return COPCHASE_RECORD_IO;
        if(flock(lock,LOCK_EX|LOCK_NB)==0)return COPCHASE_RECORD_OK;
        return errno==EWOULDBLOCK || errno==EAGAIN?COPCHASE_RECORD_LOCKED:COPCHASE_RECORD_IO;
#endif
    }
    CopChaseRecordResult apply(const CopChaseSettlement &s){
        if(!valid(s))return COPCHASE_RECORD_INVALID;
        const Session id{s.session_high,s.session_low};const Bytes b=encode(s);auto seen=sessions.find(id);
        if(seen!=sessions.end())return events[seen->second]==b?COPCHASE_RECORD_DUPLICATE:COPCHASE_RECORD_CONFLICT;
        if(events.size()>=max_records || (s.keep_heat && !cars.count(s.car_id) && cars.size()>=max_cars))return COPCHASE_RECORD_FULL;
        // Stage all potentially allocating work before modifying totals.
        const size_t index=events.size();events.push_back(b);
        try{sessions.emplace(id,index);if(s.keep_heat)cars.emplace(s.car_id,1.f);}
        catch(...){sessions.erase(id);events.pop_back();throw;}
        if(s.keep_heat)cars.find(s.car_id)->second=retained(s);
        ++status.pursuits;if(s.event==COPCHASE_EVADED){++status.escapes;status.total_bounty+=s.bounty;}else ++status.busts;
        status.last=s;status.last_retained_heat=s.keep_heat?retained(s):0;
        status.last_fine_quote=s.event==COPCHASE_BUSTED?std::max(1u,s.infractions)*215u:0;status.dirty=1;
        return COPCHASE_RECORD_OK;
    }
};

extern "C" CopChaseRecords *copchase_records_open(const char *directory,const char *profile,CopChaseRecordResult *result){
    auto fail=[&](CopChaseRecordResult r){if(result)*result=r;return static_cast<CopChaseRecords*>(nullptr);};
    if(!directory || !*directory || !id_ok(profile))return fail(COPCHASE_RECORD_INVALID);
    CopChaseRecords *r=nullptr;
    try{
        const fs::path root=fs::u8path(directory);if(!root.is_absolute())return fail(COPCHASE_RECORD_INVALID);
        std::error_code ec;fs::create_directories(root,ec);if(ec)return fail(COPCHASE_RECORD_IO);
        r=new CopChaseRecords;r->root=root;r->profile=profile;r->path=root/("copchase-"+r->profile+".records");
        r->temp=r->path;r->temp+=".new";r->lock_path=r->path;r->lock_path+=".lock";
        const auto acquired=r->acquire();if(acquired!=COPCHASE_RECORD_OK){delete r;return fail(acquired);}
        const bool exists=fs::exists(r->path,ec);if(ec){delete r;return fail(COPCHASE_RECORD_IO);}
        if(exists){
            const auto size=fs::file_size(r->path,ec);
            if(ec || size<header_size || size>header_size+max_records*record_size || (size-header_size)%record_size){delete r;return fail(COPCHASE_RECORD_CORRUPT);}
            std::unique_ptr<FILE,decltype(&std::fclose)> file(open_file(r->path,false),&std::fclose);
            FILE *f=file.get();if(!f){delete r;return fail(COPCHASE_RECORD_IO);}
            std::array<uint8_t,header_size> h{};bool ok=std::fread(h.data(),1,h.size(),f)==h.size();
            const char magic[8]={'C','C','R','E','C','0','1',0};
            ok=ok && !std::memcmp(h.data(),magic,8) && get32(h.data()+8)==1 &&
                get32(h.data()+12)==(size-header_size)/record_size && get32(h.data()+124)==checksum(h.data(),124) &&
                std::memchr(h.data()+16,0,65) && std::strcmp(reinterpret_cast<const char*>(h.data()+16),profile)==0;
            const size_t count=(size-header_size)/record_size;
            for(size_t i=0;ok && i<count;++i){Bytes b{};CopChaseSettlement s{};
                ok=std::fread(b.data(),1,b.size(),f)==b.size() && decode(b,s) && r->apply(s)==COPCHASE_RECORD_OK;
            }
            ok=ok && !std::ferror(f);file.reset();
            if(!ok){delete r;return fail(COPCHASE_RECORD_CORRUPT);}
            r->status.durable_revision=r->events.size();r->status.dirty=0;
        }
        if(result)*result=COPCHASE_RECORD_OK;return r;
    }catch(...){delete r;return fail(COPCHASE_RECORD_IO);}
}
extern "C" CopChaseRecordResult copchase_records_apply(CopChaseRecords *r,const CopChaseSettlement *s){
    if(!r || !s)return COPCHASE_RECORD_INVALID;
    try{std::lock_guard<std::mutex> lock(r->mutex);return r->apply(*s);}catch(...){return COPCHASE_RECORD_IO;}
}
extern "C" CopChaseRecordResult copchase_records_flush(CopChaseRecords *r){
    if(!r)return COPCHASE_RECORD_INVALID;
    try{
        std::lock_guard<std::mutex> io(r->io_mutex);std::vector<Bytes> events;
        {std::lock_guard<std::mutex> lock(r->mutex);if(!r->status.dirty)return COPCHASE_RECORD_OK;events=r->events;}
        std::array<uint8_t,header_size> h{};const char magic[8]={'C','C','R','E','C','0','1',0};std::memcpy(h.data(),magic,8);
        put32(h.data()+8,1);put32(h.data()+12,uint32_t(events.size()));std::memcpy(h.data()+16,r->profile.data(),r->profile.size());
        put32(h.data()+124,checksum(h.data(),124));
        std::unique_ptr<FILE,decltype(&std::fclose)> file(open_file(r->temp,true),&std::fclose);
        FILE *f=file.get();if(!f)return COPCHASE_RECORD_IO;
        bool ok=std::fwrite(h.data(),1,h.size(),f)==h.size();
        for(const auto &b:events)if(ok)ok=std::fwrite(b.data(),1,b.size(),f)==b.size();
        ok=ok && sync_file(f);if(std::fclose(file.release()))ok=false;if(!ok)return COPCHASE_RECORD_IO;
#ifdef _WIN32
        if(!MoveFileExW(r->temp.c_str(),r->path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return COPCHASE_RECORD_IO;
#else
        if(::rename(r->temp.c_str(),r->path.c_str()))return COPCHASE_RECORD_IO;
        int directory=::open(r->root.c_str(),O_RDONLY|O_DIRECTORY);
        if(directory<0)return COPCHASE_RECORD_IO;const int synced=fsync(directory);::close(directory);
        if(synced)return COPCHASE_RECORD_IO;
#endif
        {std::lock_guard<std::mutex> lock(r->mutex);r->status.durable_revision=events.size();r->status.dirty=r->events.size()!=events.size();}
        return COPCHASE_RECORD_OK;
    }catch(...){return COPCHASE_RECORD_IO;}
}
extern "C" void copchase_records_snapshot(CopChaseRecords *r,CopChaseRecordsStatus *out){
    if(!r || !out)return;std::lock_guard<std::mutex> lock(r->mutex);*out=r->status;
}
extern "C" int copchase_records_heat(CopChaseRecords *r,const char *id,float *heat){
    if(!r || !heat || !id_ok(id))return 0;std::lock_guard<std::mutex> lock(r->mutex);
    auto found=r->cars.find(id);*heat=found==r->cars.end()?1.f:found->second;return 1;
}
extern "C" void copchase_records_close(CopChaseRecords *r){delete r;}
