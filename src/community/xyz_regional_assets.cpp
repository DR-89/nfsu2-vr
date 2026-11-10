#include "xyz_regional_assets.h"
#include "copchase_assets.h"
#include "../vr_soundtrack.h"
#include "vendor/auth/sha256.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
namespace {
namespace fs=std::filesystem;
struct File {const char *name,*sha;uint64_t size;};
// Private PC 1.2 assets converted to the audited Xbox layout. All six files
// must match before the original regional restrictions may be lifted.
constexpr File files[]={
    {"PEUGOT106/GEOMETRY.BIN","2a54a9cafbd9066b5bcce8702816040524dab71f22d04ca827c122c04c429353",6831220},
    {"PEUGOT106/TEXTURES.BIN","cb399b415bd4efd2c96cf7dd1fa5a9ed2b29c59a33626b91cb4c0f6430a2fed0",372222},
    {"PEUGOT106/VINYLS.BIN","a193478c8bc41d592d37e26b34d9419456948c28214de88621717e00ee173f70",22200815},
    {"CORSA/GEOMETRY.BIN","cc6333ff32afce0a055145ed9e0e1e4872bf0c4c07cea338416305f63a3449bc",7076100},
    {"CORSA/TEXTURES.BIN","cb0cc95e803ba2a91255d978f3d11c57c715a4334cf273017ea59d596311620f",343294},
    {"CORSA/VINYLS.BIN","2344eaa2e63962b0d7df8ecbd98aeeb0edd98d3c579413e7e16ac90c00ac7afd",22436946}
};
std::array<std::string,6> paths;
bool active=false;
bool valid(const fs::path& path,const File& spec){
    if(!*spec.sha || !fs::is_regular_file(path) || fs::file_size(path)!=spec.size)return false;
    std::ifstream f(path,std::ios::binary);if(!f)return false;
    Nfsu2Sha256 sha;nfsu2_sha256_init(&sha);std::array<char,65536> buffer;uint64_t total=0;
    while(f){f.read(buffer.data(),buffer.size());auto n=f.gcount();if(n>0){total+=n;nfsu2_sha256_update(&sha,buffer.data(),size_t(n));}}
    if(!f.eof() || total!=spec.size)return false;
    uint8_t digest[32];char hex[65];nfsu2_sha256_final(&sha,digest);
    for(unsigned i=0;i<32;i++)std::snprintf(hex+i*2,3,"%02x",digest[i]);return !std::strcmp(hex,spec.sha);
}
}
extern "C" int xyz_regional_start(const char *parent,int enabled){
    active=false;for(auto& p:paths)p.clear();if(!parent || !*parent || !enabled)return 0;
    try{
        auto base=fs::absolute(fs::u8path(parent)/"REGIONAL"/"CARS");
        for(unsigned i=0;i<paths.size();i++){
            auto path=base/files[i].name;
            if(!valid(path,files[i])){std::fprintf(stderr,"[VR regional] missing or invalid %s; regional cars remain unavailable\n",files[i].name);return 0;}
            paths[i]=path.u8string();
        }
        active=true;std::fprintf(stderr,"[VR regional] validated native Corsa and Peugeot 106 geometry, textures and vinyls\n");return 1;
    }catch(...){std::fprintf(stderr,"[VR regional] optional pack unavailable\n");return 0;}
}
extern "C" int xyz_regional_available(const char *model){return active && model && (!std::strcmp(model,"CORSA") || !std::strcmp(model,"PEUGOT106"));}
extern "C" const char *xyz_regional_redirect(const char *input){
    if(!active || !input)return nullptr;
    char name[160];size_t n=std::strlen(input);if(n>=sizeof name)return nullptr;
    for(size_t i=0;i<=n;i++){char c=input[i];name[i]=c=='\\'?'/':c>='a'&&c<='z'?char(c-32):c;}
    const char *leaf=nullptr;
    for(const char *prefix:{"/DEVICE/CDROM0/CARS/","D:/CARS/","/??/D:/CARS/"}){
        auto length=std::strlen(prefix);if(!std::strncmp(name,prefix,length)){leaf=name+length;break;}}
    if(!leaf)return nullptr;
    for(unsigned i=0;i<paths.size();i++)if(!std::strcmp(leaf,files[i].name))return paths[i].c_str();
    return nullptr;
}
extern "C" const char *xyz_community_redirect(const char *path){
    const char *copchase=copchase_assets_redirect(path);if(copchase)return copchase;
    const char *regional=xyz_regional_redirect(path);return regional?regional:vr_soundtrack_redirect(path);
}
