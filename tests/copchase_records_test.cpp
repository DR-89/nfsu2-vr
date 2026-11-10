#include "copchase_records.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs=std::filesystem;
void set_id(char (&out)[65],const char *value){const size_t n=std::strlen(value);assert(n<65);std::memset(out,0,65);std::memcpy(out,value,n);}
struct Root {
    fs::path parent=fs::temp_directory_path(),path;
    Root(){path=parent/("nfsu2-copchase-record-test-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        assert(!fs::exists(path));assert(fs::create_directories(path));}
    ~Root(){assert(fs::equivalent(path.parent_path(),parent) && path.filename().string().find("nfsu2-copchase-record-test-")==0);fs::remove_all(path);}
    std::string name()const{return path.u8string();}
    fs::path file(const char *id)const{return path/(std::string("copchase-")+id+".records");}
};
CopChaseSettlement event(uint64_t id,uint32_t type=COPCHASE_EVADED){CopChaseSettlement s{};
    s.session_high=0xaabbccddeeff0011ull;s.session_low=id;s.event=type;s.preset=COPCHASE_MOST_WANTED;s.heat=4;s.fractional_heat=4.5f;
    s.bounty=7000;s.infractions=3;s.cops_damaged=2;s.cops_taken_out=1;s.cost_to_state=6500;s.elapsed_ms=70000;s.keep_heat=1;
    set_id(s.car_id,"ride-0001");return s;}
CopChaseRecords *open(const Root &root,const char *id="career-a"){CopChaseRecordResult result=COPCHASE_RECORD_INVALID;
    auto *r=copchase_records_open(root.name().c_str(),id,&result);assert(r && result==COPCHASE_RECORD_OK);return r;}
CopChaseRecordsStatus status(CopChaseRecords *r){CopChaseRecordsStatus s{};copchase_records_snapshot(r,&s);return s;}
float heat(CopChaseRecords *r,const char *id="ride-0001"){float h=0;assert(copchase_records_heat(r,id,&h));return h;}
std::vector<char> bytes(const fs::path &p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
void write(const fs::path &p,const std::vector<char> &b){std::ofstream f(p,std::ios::binary|std::ios::trunc);assert(f);f.write(b.data(),std::streamsize(b.size()));assert(f);}
void heat_bounty_fines(){Root root;auto *r=open(root);assert(heat(r)==1 && !status(r).pursuits);
    auto a=event(1);assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK);
    assert(std::fabs(heat(r)-4.05f)<1e-6 && status(r).total_bounty==7000 && status(r).last_fine_quote==0);
    auto bust=event(2,COPCHASE_BUSTED);assert(copchase_records_apply(r,&bust)==COPCHASE_RECORD_OK);
    assert(heat(r)==1 && status(r).total_bounty==7000 && status(r).last_fine_quote==645);
    bust=event(3,COPCHASE_BUSTED);bust.infractions=0;assert(copchase_records_apply(r,&bust)==COPCHASE_RECORD_OK);
    assert(status(r).last_fine_quote==215);
    a=event(4);a.preset=COPCHASE_INSANE;a.heat=5;a.fractional_heat=5.4f;assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK);
    assert(heat(r)==5);a=event(5);a.keep_heat=0;assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK);
    assert(heat(r)==5 && status(r).last_retained_heat==0 && status(r).total_bounty==21000);
    a=event(6);a.heat=3;a.fractional_heat=0;assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK);
    assert(std::fabs(heat(r)-2.7f)<1e-6);
    a=event(7);a.heat=1;a.fractional_heat=1;assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK && heat(r)==1);
    assert(status(r).pursuits==7 && status(r).escapes==5 && status(r).busts==2);
    copchase_records_close(r);
}
void identity_and_reopen(){Root root;auto *r=open(root);auto a=event(1);assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK);
    auto duplicate=a;assert(copchase_records_apply(r,&duplicate)==COPCHASE_RECORD_DUPLICATE);
    duplicate.bounty++;assert(copchase_records_apply(r,&duplicate)==COPCHASE_RECORD_CONFLICT);
    duplicate=a;duplicate.event=COPCHASE_BUSTED;assert(copchase_records_apply(r,&duplicate)==COPCHASE_RECORD_CONFLICT);
    assert(status(r).pursuits==1 && status(r).total_bounty==7000 && status(r).dirty);
    CopChaseRecordResult error;auto *other=copchase_records_open(root.name().c_str(),"career-a",&error);
    assert(!other && error==COPCHASE_RECORD_LOCKED);
    other=open(root,"career-b");assert(!status(other).total_bounty && heat(other)==1);
    assert(copchase_records_apply(other,&a)==COPCHASE_RECORD_OK && copchase_records_flush(other)==COPCHASE_RECORD_OK);
    copchase_records_close(other);
    auto b=event(2);set_id(b.car_id,"ride-0002");b.preset=COPCHASE_INSANE;b.heat=5;
    assert(copchase_records_apply(r,&b)==COPCHASE_RECORD_OK);assert(heat(r,"ride-0002")==5);
    assert(copchase_records_flush(r)==COPCHASE_RECORD_OK);assert(!status(r).dirty && status(r).durable_revision==2);
    auto saved=bytes(root.file("career-a"));copchase_records_close(r);r=open(root);
    assert(status(r).total_bounty==14000 && status(r).pursuits==2 && !status(r).dirty && heat(r,"ride-0002")==5);
    assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_DUPLICATE && copchase_records_apply(r,&duplicate)==COPCHASE_RECORD_CONFLICT);
    assert(copchase_records_flush(r)==COPCHASE_RECORD_OK && bytes(root.file("career-a"))==saved);
    copchase_records_close(r);
}
void invalid_and_corrupt(){Root root;CopChaseRecordResult error;
    assert(!copchase_records_open("relative", "career",&error) && error==COPCHASE_RECORD_INVALID);
    for(auto id:{"", "../career", "career/name", "a.b", "a b"})
        assert(!copchase_records_open(root.name().c_str(),id,&error) && error==COPCHASE_RECORD_INVALID);
    auto *r=open(root);for(unsigned n=0;n<11;++n){auto a=event(1);
        switch(n){case 0:a.session_low=a.session_high=0;break;case 1:a.event=COPCHASE_EVADED|COPCHASE_CANCELLED;break;
        case 2:a.heat=6;break;case 3:a.preset=4;break;case 4:a.fractional_heat=std::numeric_limits<float>::quiet_NaN();break;
        case 5:a.bounty=UINT32_MAX;break;case 6:a.infractions=1000001;break;case 7:a.elapsed_ms=UINT64_MAX;break;
        case 8:a.keep_heat=2;break;case 9:std::memset(a.car_id,'x',sizeof a.car_id);break;case 10:set_id(a.car_id,"../ride");break;}
        assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_INVALID);}
    assert(!status(r).pursuits);auto a=event(1);assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK);
    assert(copchase_records_flush(r)==COPCHASE_RECORD_OK);copchase_records_close(r);auto good=bytes(root.file("career-a"));
    auto broken=good;broken.back()^=1;write(root.file("career-a"),broken);
    assert(!copchase_records_open(root.name().c_str(),"career-a",&error) && error==COPCHASE_RECORD_CORRUPT);
    assert(bytes(root.file("career-a"))==broken);write(root.file("career-a"),good);
    write(root.file("career-c"),good);assert(!copchase_records_open(root.name().c_str(),"career-c",&error) && error==COPCHASE_RECORD_CORRUPT);
    broken=good;broken.resize(broken.size()-1);write(root.file("career-a"),broken);
    assert(!copchase_records_open(root.name().c_str(),"career-a",&error) && error==COPCHASE_RECORD_CORRUPT);
}
void failed_commit_preserves_old(){Root root;auto *r=open(root);auto a=event(1);
    assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK && copchase_records_flush(r)==COPCHASE_RECORD_OK);
    const auto old=bytes(root.file("career-a"));a=event(2);assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK);
    auto temp=root.file("career-a");temp+=".new";assert(fs::create_directory(temp));
    assert(copchase_records_flush(r)==COPCHASE_RECORD_IO);assert(bytes(root.file("career-a"))==old);
    assert(status(r).dirty && status(r).durable_revision==1);assert(fs::remove(temp));
    assert(copchase_records_flush(r)==COPCHASE_RECORD_OK);copchase_records_close(r);r=open(root);
    assert(status(r).total_bounty==14000 && status(r).durable_revision==2);copchase_records_close(r);
}
void concurrency_and_bounds(){Root root;auto *r=open(root);std::thread io([&]{for(unsigned i=0;i<15;++i)
        assert(copchase_records_flush(r)==COPCHASE_RECORD_OK);});
    for(unsigned i=1;i<=200;++i){auto a=event(i);a.bounty=1;assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK);}
    io.join();assert(copchase_records_flush(r)==COPCHASE_RECORD_OK);copchase_records_close(r);r=open(root);
    assert(status(r).total_bounty==200 && status(r).pursuits==200 && !status(r).dirty);
    // Per-profile car map has an explicit bound; no silent eviction can make
    // another car inherit heat or destroy exact-once session history.
    for(unsigned i=1;i<=255;++i){auto a=event(200+i);std::snprintf(a.car_id,sizeof a.car_id,"ride-%u",i);
        assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK);}
    auto a=event(1000);set_id(a.car_id,"one-car-too-many");assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_FULL);
    assert(status(r).pursuits==455);copchase_records_close(r);
}
void session_bound(){Root root;auto *r=open(root);auto a=event(1);a.keep_heat=0;a.bounty=0;
    for(unsigned i=1;i<=16384;++i){a.session_low=i;assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_OK);}
    a.session_low=16385;assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_FULL);
    a.session_low=1;assert(copchase_records_apply(r,&a)==COPCHASE_RECORD_DUPLICATE);
    assert(copchase_records_flush(r)==COPCHASE_RECORD_OK);copchase_records_close(r);r=open(root);
    assert(status(r).pursuits==16384 && copchase_records_apply(r,&a)==COPCHASE_RECORD_DUPLICATE);copchase_records_close(r);
}
}
int main(){heat_bounty_fines();identity_and_reopen();invalid_and_corrupt();failed_commit_preserves_old();concurrency_and_bounds();session_bound();
    std::puts("copchase_records PASS: verified heat/fine/bounty, deduplication/reopen, profile/car isolation, corrupt/invalid input, failed commit, concurrency/caps");}
