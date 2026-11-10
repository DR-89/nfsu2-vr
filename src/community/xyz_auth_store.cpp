#include "xyz_auth_store.h"
#include "xyz_auth.h"
#include "xyz_credentials.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <deque>
#include <unordered_map>
#include <fstream>
#include <chrono>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
namespace xyz {
static bool valid_id(const std::string &s){return s.size()==32 && s.find_first_not_of("0123456789abcdef")==std::string::npos;}
static bool valid_token(const std::string &s){return s.size()==43 && s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")==std::string::npos;}
template<class T>static void wipe(T &s){volatile char *p=reinterpret_cast<volatile char*>(s.data());for(size_t n=s.size()*sizeof(*s.data());n--;) *p++=0;s.clear();}
static bool save_blob(const std::filesystem::path &file,const std::vector<uint8_t> &bytes){
    std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);if(ec)return false;
    auto temporary=file;temporary+=std::string(".tmp.")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    bool ok=false,created=false;
#ifdef _WIN32
    HANDLE h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h!=INVALID_HANDLE_VALUE){created=true;DWORD n=0;ok=WriteFile(h,bytes.data(),DWORD(bytes.size()),&n,nullptr) && n==bytes.size() && FlushFileBuffers(h);CloseHandle(h);
        if(ok)ok=MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;}
#else
    int fd=open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
    if(fd>=0){created=true;size_t n=0;while(n<bytes.size()){auto got=write(fd,bytes.data()+n,bytes.size()-n);if(got<0 && errno==EINTR)continue;if(got<=0)break;n+=size_t(got);}
        ok=n==bytes.size() && fsync(fd)==0;if(close(fd))ok=false;if(ok)ok=rename(temporary.c_str(),file.c_str())==0;}
#endif
    if(created && !ok)std::filesystem::remove(temporary,ec);return ok;
}
struct AuthStore::Impl {
    struct Entry {uint64_t revision=0;bool busy=false,failed=false;std::string token,notice;};
    struct Job {std::string id,phrase;uint64_t revision;int action;}; // 0 load, 1 link, 2 forget
    std::filesystem::path directory;Crypt crypt;mutable std::mutex mutex;std::condition_variable wake;
    std::unordered_map<std::string,Entry> entries;std::deque<Job> jobs;bool quit=false;std::thread thread;
    Impl(std::filesystem::path path,const std::vector<std::string> &ids,Crypt fn):directory(std::move(path)),crypt(fn?std::move(fn):Crypt(credential_crypt)){
        for(const auto &id:ids)if(valid_id(id)){entries[id].busy=true;jobs.push_back({id,{},0,0});}
        thread=std::thread([this]{run();});
    }
    // Finish accepted writes/deletions before shutdown. In particular, dropping
    // a queued forget after an in-flight link would resurrect the saved token.
    ~Impl(){ {std::lock_guard<std::mutex> lock(mutex);quit=true;}wake.notify_all();thread.join();for(auto &e:entries)wipe(e.second.token);}
    void run() noexcept {
        for(;;){
            Job job;
            {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return quit || !jobs.empty();});if(jobs.empty())return;job=std::move(jobs.front());jobs.pop_front();}
            bool ok=false;std::string token,notice;std::vector<uint8_t> raw,blob;
            try {
                auto path=directory/("xyz-auth-"+job.id+".bin");std::error_code ec;
                if(job.action==0){
                    bool exists=std::filesystem::exists(path,ec);
                    if(!ec && !exists)ok=true;
                    else if(!ec){auto n=std::filesystem::file_size(path,ec);
                        if(!ec && n>0 && n<=4096){blob.resize(size_t(n));std::ifstream in(path,std::ios::binary);
                            if(in.read(reinterpret_cast<char*>(blob.data()),std::streamsize(n)) && crypt(job.id,blob,false,raw)){
                                token.assign(raw.begin(),raw.end());ok=valid_token(token);}}}
                    if(!ok)notice="Saved sign-in could not be opened. Link your keyphrase again.";
                } else if(job.action==2){std::filesystem::remove(path,ec);ok=!ec;notice=ok?"Saved sign-in removed.":"Could not remove saved sign-in.";
                } else {
                    char derived[44]={0};
                    struct ClearDerived {char *p;~ClearDerived(){xyz_auth_clear(p,44);}} clear_derived{derived};
                    ok=xyz_auth_token(job.phrase.c_str(),derived,sizeof derived)!=0;
                    if(ok)token=derived;xyz_auth_clear(derived,sizeof derived);wipe(job.phrase);
                    if(!ok)notice="Enter the eight words of your existing nfsu2.xyz keyphrase.";
                    else {raw.assign(token.begin(),token.end());ok=crypt(job.id,raw,true,blob) && !blob.empty() && blob.size()<=4096 && save_blob(path,blob);
                        notice=ok?"Sign-in saved. Connect again to use it.":"Could not protect or save sign-in. Previous sign-in kept.";}
                }
            } catch(...){ok=false;notice="Could not process sign-in. Previous sign-in kept.";}
            wipe(job.phrase);wipe(raw);wipe(blob);
            {std::lock_guard<std::mutex> lock(mutex);auto it=entries.find(job.id);
                if(it!=entries.end() && it->second.revision==job.revision){auto &entry=it->second;
                    if(ok){wipe(entry.token);entry.token=std::move(token);}entry.failed=!ok && entry.token.empty();entry.busy=false;entry.notice=notice;}}
            wipe(token);
        }
    }
};
AuthStore::AuthStore(std::filesystem::path path,const std::vector<std::string> &ids,Crypt crypt):impl_(std::make_unique<Impl>(std::move(path),ids,std::move(crypt))){}
AuthStore::~AuthStore()=default;
bool AuthStore::link(const std::string &id,const char *phrase){
    if(!valid_id(id) || !phrase)return false;size_t n=0;while(n<513 && phrase[n])++n;if(n>512)return false;
    auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);auto &entry=p.entries[id];
    if(p.quit || entry.busy || p.jobs.size()>=16)return false;
    entry.busy=true;entry.notice="Processing sign-in...";++entry.revision;
    p.jobs.push_back({id,phrase,entry.revision,n?1:2});p.wake.notify_one();return true;
}
void AuthStore::forget(const std::string &id){
    if(!valid_id(id))return;auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);auto &entry=p.entries[id];
    if(p.quit)return;
    // A deletion supersedes pending operations for this profile, including
    // duplicate deletes. The in-flight job is followed by this final deletion.
    for(auto it=p.jobs.begin();it!=p.jobs.end();){
        if(it->id==id){wipe(it->phrase);it=p.jobs.erase(it);}else ++it;
    }
    wipe(entry.token);++entry.revision;entry.busy=true;entry.failed=false;
    p.jobs.push_back({id,{},entry.revision,2});p.wake.notify_one();
}
AuthStatus AuthStore::status(const std::string &id)const{
    auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);auto it=p.entries.find(id);if(it==p.entries.end())return {};
    const auto &e=it->second;return {e.busy,!e.token.empty(),e.notice};
}
bool AuthStore::token(const std::string &id,std::string &out)const{
    wipe(out);auto &p=*impl_;std::lock_guard<std::mutex> lock(p.mutex);auto it=p.entries.find(id);if(it==p.entries.end())return true;
    const auto &e=it->second;if(e.busy || e.failed)return false;out=e.token;return true;
}
}
