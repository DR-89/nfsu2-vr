#include "vr_soundtrack.h"
#include "community/vendor/auth/sha256.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
namespace {
namespace fs=std::filesystem;
struct File {const char *name,*guest,*sha;uint64_t size;};
constexpr File files[]={
    {"ZDIR.BIN","ZDIR.BIN","d97ca4681691163425a0033764eaa5e0c43e1d2355736c8ba5f3e6d7cbf2bef2",32688},
    {"MusicSFX.mus","ZZDATA10.BIN","194745211c1405ae53410cc0b1bd5a2542897d05df4d1ed1db58b2aac79d3f89",1626612096},
    {"MusicSFX.sbk","ZZDATA11.BIN","40261ad0ccab2864e04a98a8118496034206b83567d3c2af7ab202482d6b2bf8",38932},
    {"MusicSFX.mpf","ZZDATA12.BIN","cb3d11a25c5ac1c752aad68bfc045ec72b1df0bb05ef91685d90fae9948b592d",6840}
};
std::array<std::string,4> paths;
bool enabled=false;
bool valid(const fs::path &p,const File &spec){
    if(!fs::is_regular_file(p) || fs::file_size(p)!=spec.size)return false;
    std::ifstream f(p,std::ios::binary);if(!f)return false;
    Nfsu2Sha256 sha;nfsu2_sha256_init(&sha);std::array<char,65536> buffer;uint64_t total=0;
    while(f){f.read(buffer.data(),buffer.size());auto n=f.gcount();if(n>0){total+=n;nfsu2_sha256_update(&sha,buffer.data(),(size_t)n);}}
    if(!f.eof() || total!=spec.size)return false;
    uint8_t digest[32];nfsu2_sha256_final(&sha,digest);char hex[65];
    for(int i=0;i<32;i++)std::snprintf(hex+i*2,3,"%02x",digest[i]);return !std::strcmp(hex,spec.sha);
}
}
extern "C" int vr_soundtrack_start(const char *parent,int requested){
    enabled=false;for(auto &p:paths)p.clear();if(!requested || !parent || !*parent)return 0;
    try{
        const auto base=fs::absolute(fs::u8path(parent)/"SONGS");
        const char *game=std::getenv("NFSU2_GAME_DIR");
        const File retail={"ZDIR.BIN","","254212625c80eb720daf139290b8d19aa159cb6890bebc75d11c2d3da52fb722",32688};
        auto disc=fs::absolute(fs::u8path(game&&*game?game:"game"))/"NFSUNDER";
        if(!valid(disc/"ZDIR.BIN",retail) || fs::exists(disc/"ZZDATA10.BIN") || fs::exists(disc/"ZZDATA11.BIN") || fs::exists(disc/"ZZDATA12.BIN")){
            std::fprintf(stderr,"[VR soundtrack] unsupported disc overlay; original paths retained\n");return 0;
        }
        for(unsigned i=0;i<paths.size();i++){
            auto path=base/files[i].name;
            if(!valid(path,files[i])){std::fprintf(stderr,"[VR soundtrack] missing or invalid %s; original soundtrack retained\n",files[i].name);return 0;}
            paths[i]=path.u8string();
        }
        // The Xbox sector cache also belongs to this disc overlay. Keep it
        // apart from original music and from other isolated game instances.
        const char *runtime=std::getenv("NFSU2_RUNTIME_DIR");
        auto cache=runtime && *runtime?fs::u8path(runtime)/"restored-songs-v1":
            fs::absolute(fs::u8path(game&&*game?game:"game")/"RuntimeCache"/"restored-songs-v1");
        fs::create_directories(cache);
#ifdef _WIN32
        if(_putenv_s("NFSU2_RUNTIME_DIR",cache.u8string().c_str()))return 0;
#else
        if(setenv("NFSU2_RUNTIME_DIR",cache.u8string().c_str(),1))return 0;
#endif
        enabled=true;std::fprintf(stderr,"[VR soundtrack] verified restored songs; original EA TRAX player and controls\n");return 1;
    }catch(...){std::fprintf(stderr,"[VR soundtrack] pack unavailable; original soundtrack retained\n");return 0;}
}
extern "C" const char *vr_soundtrack_redirect(const char *input){
    if(!enabled || !input)return nullptr;
    // Only the CD's exact known archive names. No save, loose file or prefix
    // match can be redirected, even when it contains the same basename.
    char name[128];size_t n=std::strlen(input);if(n>=sizeof name)return nullptr;
    for(size_t i=0;i<=n;i++){char c=input[i];name[i]=c=='/'?'\\':c>='a'&&c<='z'?char(c-32):c;}
    const char *leaf=nullptr;
    for(const char *prefix:{"\\DEVICE\\CDROM0\\NFSUNDER\\","D:\\NFSUNDER\\","\\??\\D:\\NFSUNDER\\"}){
        auto size=std::strlen(prefix);if(!std::strncmp(name,prefix,size)){leaf=name+size;break;}}
    if(!leaf)return nullptr;
    for(unsigned i=0;i<paths.size();i++)if(!std::strcmp(leaf,files[i].guest))return paths[i].c_str();
    return nullptr;
}
