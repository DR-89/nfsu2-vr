#include "xyz_car_files.h"
#include "xyz_regional_assets.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace {
namespace fs=std::filesystem;
uint32_t word(const uint8_t *b){return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;}
uint32_t hash(const std::string &s){uint32_t h=0xffffffffu;for(unsigned char c:s)h=h*33+c;return h;}
bool regular(const fs::path &p,uintmax_t &size){std::error_code ec;if(!fs::is_regular_file(p,ec) || ec)return false;size=fs::file_size(p,ec);return !ec && size>0;}
struct Files {
    fs::path root;std::unordered_set<uint32_t> archived;std::unordered_map<std::string,bool> checked;
    explicit Files(fs::path path):root(std::move(path)){
        auto directory=root/"NFSUNDER";uintmax_t size=0;
        if(!regular(directory/"ZDIR.BIN",size) || size>24*65536 || size%24)return;
        std::vector<uint8_t> data(static_cast<size_t>(size));std::ifstream file(directory/"ZDIR.BIN",std::ios::binary);
        if(!file.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size())))return;
        // The Xbox archive manager opens slots 0..15 (183A80). Optional
        // sound/texture overlays may use the six slots beyond the retail ten.
        std::array<uintmax_t,16> archives{};
        for(size_t i=0;i<archives.size();++i)regular(directory/("ZZDATA"+std::to_string(i)+".BIN"),archives[i]);
        uint32_t previous=0;std::unordered_set<uint32_t> valid;
        for(size_t offset=0;offset<data.size();offset+=24){
            const auto b=data.data()+offset;uint32_t key=word(b),index=word(b+4),sector=word(b+8),bytes=word(b+16);
            if(key<previous || index>=archives.size())return;previous=key;
            if(bytes && uint64_t(sector)*2048+bytes<=archives[index])valid.insert(key);
        }
        archived=std::move(valid);
    }
    bool available(const std::string &name){
        auto old=checked.find(name);if(old!=checked.end())return old->second;
        bool ok=true;
        for(const char *file:{"GEOMETRY.BIN","TEXTURES.BIN","VINYLS.BIN"}){
            uintmax_t size;auto path=root/"CARS"/name/file;
            if(!regular(path,size) && !archived.count(hash("CARS\\"+name+"\\"+file))){ok=false;break;}
        }
        checked.emplace(name,ok);return ok;
    }
};
std::unique_ptr<Files> files;
}
extern "C" void xyz_car_files_init(const char *directory){
    files.reset();try{if(directory && *directory)files=std::make_unique<Files>(fs::u8path(directory));}catch(...){}
}
extern "C" int xyz_car_files_available(const char *name){
    if(xyz_regional_available(name))return 1;
    if(!files || !name)return 0;
    try{
        unsigned n=0;for(;n<32 && name[n];++n){unsigned char c=name[n];if(!((c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_'))return 0;}
        return n>0 && n<32 && files->available(std::string(name,n));
    }catch(...){return 0;}
}
