#include "xyz_auth_store.h"
#include "xyz_credentials.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <future>
#include <iterator>
#include <mutex>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
using namespace std::chrono_literals;
namespace fs=std::filesystem;
// Non-platform builds test the worker via an injected fixture, never plaintext
// fallback storage in the product. Windows separately exercises real DPAPI.
#if !defined(_WIN32) && !defined(__ANDROID__)
namespace xyz { bool credential_crypt(const std::string&,const std::vector<uint8_t>&,bool,std::vector<uint8_t>&){return false;} }
#endif
static const std::string id="0123456789abcdef0123456789abcdef";
static const std::string other="1123456789abcdef0123456789abcdef";
static const char *phrase="acid-acorn-acre-acts-afar-affix-aged-agent";
static const char *expected="Ec0iIUolQixwifl_FxyyfA9X-foBclMDwN_l-F2BSCw";
static bool idle(xyz::AuthStore &store){
    auto until=std::chrono::steady_clock::now()+10s;
    while(store.status(id).busy && std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(2ms);
    return !store.status(id).busy;
}
static std::string read(const fs::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct Fixture {
    std::atomic<bool> fail{false};std::mutex mutex;std::condition_variable wake;
    bool gate=false,entered=false,release=false;
    bool crypt(const std::string &profile,const std::vector<uint8_t> &in,bool encrypt,std::vector<uint8_t> &out){
        out.clear();if(fail)return false;
        if(encrypt){std::unique_lock<std::mutex> lock(mutex);if(gate){entered=true;wake.notify_all();if(!wake.wait_for(lock,5s,[&]{return release;}))return false;}}
        if(encrypt){out.assign(profile.begin(),profile.end());for(auto c:in)out.push_back(c^0xa5);return true;}
        if(in.size()<profile.size() || !std::equal(profile.begin(),profile.end(),in.begin()))return false;
        for(size_t i=profile.size();i<in.size();++i)out.push_back(in[i]^0xa5);return true;
    }
};
int main(){
    const auto root=fs::current_path()/("xyz-auth-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(root));
    const auto file=root/("xyz-auth-"+id+".bin"),second=root/("xyz-auth-"+other+".bin");
    // Delete only this test's known outputs, then its empty directory.
    struct Cleanup {fs::path root,a,b;~Cleanup(){std::error_code ec;fs::remove(a,ec);fs::remove(b,ec);fs::remove(root,ec);}} cleanup{root,file,second};
    Fixture fixture;auto crypt=[&](const auto &p,const auto &in,bool enc,auto &out){return fixture.crypt(p,in,enc,out);};
    std::string token;
    {
        xyz::AuthStore store(root,{id},crypt);CHECK(idle(store));CHECK(store.token(id,token) && token.empty());
        CHECK(!store.link("../escape",phrase));CHECK(!store.link(id,nullptr));
        CHECK(!store.link(id,std::string(513,'a').c_str()));CHECK(!fs::exists(file));
        CHECK(store.link(id,phrase));CHECK(idle(store));CHECK(store.status(id).linked);
        CHECK(store.token(id,token) && token==expected);
        const auto original=read(file);CHECK(!original.empty());CHECK(original.find(phrase)==std::string::npos && original.find(expected)==std::string::npos);
        CHECK(store.link(id,"invalid phrase"));CHECK(idle(store));CHECK(store.token(id,token) && token==expected);CHECK(read(file)==original);
        fixture.fail=true;CHECK(store.link(id,phrase));CHECK(idle(store));CHECK(store.token(id,token) && token==expected);CHECK(read(file)==original);fixture.fail=false;
    }
    {
        xyz::AuthStore store(root,{id},crypt);CHECK(idle(store));CHECK(store.token(id,token) && token==expected);
        CHECK(store.link(id,""));CHECK(idle(store));CHECK(!store.status(id).linked && !fs::exists(file));
        CHECK(store.token(id,token) && token.empty());
    }
    // Corrupt stored credentials block connection instead of silently falling
    // back to an unclaimed name. Explicit removal recovers the profile.
    {std::ofstream out(file,std::ios::binary);out<<"corrupt";}
    {
        xyz::AuthStore store(root,{id},crypt);CHECK(idle(store));CHECK(!store.token(id,token));
        store.forget(id);CHECK(idle(store));CHECK(store.token(id,token) && token.empty());CHECK(!fs::exists(file));
    }
    // An in-flight link followed by delete and immediate shutdown must never
    // leave a credential behind. Duplicate deletion requests coalesce.
    fixture.gate=true;
    auto store=std::make_unique<xyz::AuthStore>(root,std::vector<std::string>{},crypt);
    CHECK(store->link(id,phrase));
    bool entered;
    {std::unique_lock<std::mutex> lock(fixture.mutex);entered=fixture.wake.wait_for(lock,5s,[&]{return fixture.entered;});}
    for(int i=0;i<100;++i)store->forget(id);
    CHECK(!store->token(id,token));
    auto closed=std::async(std::launch::async,[s=std::move(store)]()mutable{s.reset();});
    {std::lock_guard<std::mutex> lock(fixture.mutex);fixture.release=true;}fixture.wake.notify_all();
    closed.get();CHECK(entered);CHECK(!fs::exists(file));
#ifdef _WIN32
    // Real OS protection must survive store restart, bind to the profile ID,
    // reject tampering, and keep the secret out of its on-disk representation.
    {
        xyz::AuthStore protected_store(root,{});CHECK(protected_store.link(id,phrase));CHECK(idle(protected_store));
        CHECK(protected_store.token(id,token) && token==expected);
    }
    const auto bytes=read(file);CHECK(bytes.find(phrase)==std::string::npos && bytes.find(expected)==std::string::npos);
    std::vector<uint8_t> blob(bytes.begin(),bytes.end()),plain;
    CHECK(!xyz::credential_crypt(other,blob,false,plain));blob.back()^=1;CHECK(!xyz::credential_crypt(id,blob,false,plain));
    {
        xyz::AuthStore protected_store(root,{id});CHECK(idle(protected_store));CHECK(protected_store.token(id,token) && token==expected);
        protected_store.forget(id);CHECK(idle(protected_store));CHECK(!fs::exists(file));
    }
    std::puts("xyz_auth_store: worker, persistence, failure recovery, delete/shutdown ordering and Windows DPAPI passed");
#else
    std::puts("xyz_auth_store: worker, persistence, failure recovery and delete/shutdown ordering passed (injected protection fixture)");
#endif
    return 0;
}
