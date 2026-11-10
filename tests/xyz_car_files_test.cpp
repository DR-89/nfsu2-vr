#include "xyz_car_files.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace fs=std::filesystem;
static uint32_t hash(const std::string &s){uint32_t h=0xffffffffu;for(unsigned char c:s)h=h*33+c;return h;}
static void write(const fs::path &path,const std::vector<uint8_t> &bytes){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
int main(int argc,char **argv){
    if(argc==2){
        xyz_car_files_init(argv[1]);CHECK(xyz_car_files_available("PEUGOT"));CHECK(xyz_car_files_available("COROLLA"));
        CHECK(xyz_car_files_available("SKYLINE"));CHECK(!xyz_car_files_available("PEUGOT106"));CHECK(!xyz_car_files_available("CORSA"));
        std::puts("xyz_car_files: owned US Xbox archive contains stock cars; missing 106/Corsa rejected");return 0;
    }
    const auto root=fs::current_path()/("xyz-car-files-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(root));CHECK(fs::create_directory(root/"NFSUNDER"));
    const auto dir=root/"NFSUNDER",cars=root/"CARS",car=cars/"PEUGOT106";
    struct Cleanup {fs::path root,dir,cars,car;~Cleanup(){std::error_code ec;xyz_car_files_init(nullptr);
        for(const char *s:{"GEOMETRY.BIN","TEXTURES.BIN","VINYLS.BIN"})fs::remove(car/s,ec);
        fs::remove(car,ec);fs::remove(cars,ec);fs::remove(dir/"ZDIR.BIN",ec);fs::remove(dir/"ZZDATA0.BIN",ec);fs::remove(dir/"ZZDATA10.BIN",ec);fs::remove(dir,ec);fs::remove(root,ec);
    }} cleanup{root,dir,cars,car};
    xyz_car_files_init(root.u8string().c_str());CHECK(!xyz_car_files_available("PEUGOT"));
    CHECK(!xyz_car_files_available("../escape") && !xyz_car_files_available("CARS\\PEUGOT") && !xyz_car_files_available(""));
    std::vector<std::array<uint32_t,6>> entries;
    for(const char *s:{"GEOMETRY.BIN","TEXTURES.BIN","VINYLS.BIN"})entries.push_back({hash(std::string("CARS\\PEUGOT\\")+s),0,1,0,2048,0});
    std::sort(entries.begin(),entries.end());std::vector<uint8_t> bytes;
    for(const auto &entry:entries)for(auto w:entry)for(int i=0;i<4;++i)bytes.push_back(uint8_t(w>>(8*i)));
    write(dir/"ZZDATA0.BIN",std::vector<uint8_t>(4096));write(dir/"ZDIR.BIN",bytes);
    xyz_car_files_init(root.u8string().c_str());CHECK(xyz_car_files_available("PEUGOT"));CHECK(!xyz_car_files_available("PEUGOT106"));
    // An additional legal archive must not invalidate unrelated stock cars.
    auto extra=bytes;extra.insert(extra.begin(),24,0);extra[4]=10;extra[16]=1;
    write(dir/"ZZDATA10.BIN",{1});write(dir/"ZDIR.BIN",extra);
    xyz_car_files_init(root.u8string().c_str());CHECK(xyz_car_files_available("PEUGOT"));
    extra[4]=16;write(dir/"ZDIR.BIN",extra);
    xyz_car_files_init(root.u8string().c_str());CHECK(!xyz_car_files_available("PEUGOT"));
    write(dir/"ZDIR.BIN",bytes);
    // Directory membership alone is insufficient when the data is truncated.
    write(dir/"ZZDATA0.BIN",std::vector<uint8_t>(4095));xyz_car_files_init(root.u8string().c_str());CHECK(!xyz_car_files_available("PEUGOT"));
    write(dir/"ZZDATA0.BIN",std::vector<uint8_t>(4096));bytes.push_back(0);write(dir/"ZDIR.BIN",bytes);
    xyz_car_files_init(root.u8string().c_str());CHECK(!xyz_car_files_available("PEUGOT"));
    // A separately installed complete native pack is detected at next boot.
    CHECK(fs::create_directory(cars));CHECK(fs::create_directory(car));
    for(const char *s:{"GEOMETRY.BIN","TEXTURES.BIN"})write(car/s,{1,2,3});
    xyz_car_files_init(root.u8string().c_str());CHECK(!xyz_car_files_available("PEUGOT106"));
    write(car/"VINYLS.BIN",{1,2,3});xyz_car_files_init(root.u8string().c_str());CHECK(xyz_car_files_available("PEUGOT106"));
    std::puts("xyz_car_files: archive bounds, missing regional cars, path validation and loose packs passed");return 0;
}
