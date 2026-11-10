#include "xyz_stance.h"
#include "xyz_xbox_stance.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <vector>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::array<uint8_t,0x10000> memory;
static int fail_write;
static int read(void*,uint32_t at,void *out,size_t n){if(at<0x10000 || uint64_t(at)+n>0x20000)return 0;std::memcpy(out,memory.data()+at-0x10000,n);return 1;}
static int write(void*,uint32_t at,const void *in,size_t n){
    CHECK((at==0x10400 && n==32) || (at==0x10420 && n==16) || ((at==0x14080 || at==0x140c0) && n==48));
    if(fail_write && !--fail_write)return 0;
    std::memcpy(memory.data()+at-0x10000,in,n);return 1;
}
static void put(uint32_t at,const void *p,size_t n){std::memcpy(memory.data()+at-0x10000,p,n);}
int main(int argc,char **argv){
    auto folder=std::filesystem::temp_directory_path()/std::filesystem::path("nfsu2-stance-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);xyz_stance_init(folder.u8string().c_str());
    XyzStance v;XyzStanceView view;
    CHECK(!xyz_stance_publish(123,"RX8",35,&v) && v.fc==35 && v.tw==100);
    CHECK(xyz_stance_edit(123,0,1) && xyz_stance_enabled());
    CHECK(!xyz_stance_edit(456,2,1));CHECK(xyz_stance_edit(123,2,1));
    CHECK(xyz_stance_publish(123,"RX8",35,&v) && v.fc==40);
    for(int i=0;i<99;i++)CHECK(xyz_stance_edit(123,3,1));
    CHECK(xyz_stance_publish(123,"RX8",35,&v) && v.rc==150);
    CHECK(!xyz_stance_publish(456,"SUPRA",70,&v) && v.fc==70);
    CHECK(xyz_stance_edit(456,4,-1));xyz_stance_init(folder.u8string().c_str());
    CHECK(xyz_stance_publish(123,"RX8",35,&v) && v.fc==40 && v.rc==150 && v.fo==0);
    CHECK(xyz_stance_publish(456,"SUPRA",70,&v) && v.fo==-1 && v.fc==70);
    CHECK(xyz_stance_edit(456,7,1));CHECK(!xyz_stance_publish(456,"SUPRA",70,&v));
    CHECK(xyz_stance_publish(123,"RX8",35,&v));
    // A corrupt file cannot activate a partially loaded table or wrapped int16 values.
    {std::ofstream f(folder/"xyz-stance.txt");f<<"NVS1 1\n0000007b 65536 0 0 0 100\n";}
    xyz_stance_init(folder.u8string().c_str());CHECK(!xyz_stance_enabled() && !xyz_stance_publish(123,"RX8",35,&v));
    // Storage failure leaves the previous in-memory choice active.
    auto invalid=folder/"not-a-directory";{std::ofstream f(invalid);f<<"x";}
    xyz_stance_init(invalid.u8string().c_str());xyz_stance_publish(123,"RX8",35,&v);
    CHECK(!xyz_stance_edit(123,0,1) && !xyz_stance_enabled());xyz_stance_snapshot(&view);CHECK(view.save_failed);
    uint32_t ride=0x15000;put(0x10004,&ride,4);
    float places[]={-1,-1,1,1,1,1,-1,-1},widths[]={.2f,.3f,.4f,.5f};
    put(0x10400,places,sizeof places);put(0x10420,widths,sizeof widths);
    for(unsigned k=0;k<4;k++){float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,7,8,9,1};put(0x14000+64*k,matrix,sizeof matrix);}
    auto before=memory;XyzXboxGuest guest{nullptr,read,write,nullptr};XyzStanceDraw saved;
    v={100,150,80,-40,130};CHECK(xyz_xbox_stance_begin(&guest,0x10000,0x14000,&v,&saved));
    float x;read(nullptr,0x10400,&x,4);CHECK(std::fabs(x+1.08f)<1e-6f);
    read(nullptr,0x10410,&x,4);CHECK(std::fabs(x-.96f)<1e-6f);
    read(nullptr,0x10420,&x,4);CHECK(std::fabs(x-.26f)<1e-6f);
    read(nullptr,0x14098,&x,4);CHECK(std::fabs(x+std::sin(15*3.14159265f/180))<1e-6f);
    // Other car data, front transforms and translations are untouched, including at extreme values.
    CHECK(!std::memcmp(memory.data(),before.data(),0x400));CHECK(!std::memcmp(memory.data()+0x4000,before.data()+0x4000,128));
    CHECK(!std::memcmp(memory.data()+0x40b0,before.data()+0x40b0,16));
    xyz_xbox_stance_end(&guest,&saved);CHECK(memory==before && !saved.active);
    fail_write=3;CHECK(!xyz_xbox_stance_begin(&guest,0x10000,0x14000,&v,&saved) && memory==before);
    v.fc=-1;CHECK(!xyz_xbox_stance_begin(&guest,0x10000,0x14000,&v,&saved) && memory==before);
    // Only this test's unique temporary directory is removed.
    std::filesystem::remove_all(folder);
    std::puts("Stance: per-model atomic persistence, invalid data, stale selection, bounded render edits and complete restoration passed");
}
