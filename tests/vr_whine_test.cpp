#include "../src/vr_whine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::vector<int16_t> input(960),output(960);
static void wav(const std::filesystem::path &p,unsigned period){
    std::vector<unsigned char>b(44+2048);auto w16=[&](unsigned a,unsigned v){b[a]=v;b[a+1]=v>>8;};
    auto w32=[&](unsigned a,unsigned v){w16(a,v);w16(a+2,v>>16);};
    std::copy_n("RIFF",4,b.begin());w32(4,unsigned(b.size()-8));std::copy_n("WAVEfmt ",8,b.begin()+8);
    w32(16,16);w16(20,1);w16(22,1);w32(24,48000);w32(28,96000);w16(32,2);w16(34,16);
    std::copy_n("data",4,b.begin()+36);w32(40,2048);
    for(unsigned i=0;i<1024;++i)w16(44+i*2,uint16_t(int16_t(std::sin(i*6.2831853/period)*16000)));
    std::ofstream f(p,std::ios::binary);CHECK(f.write(reinterpret_cast<const char*>(b.data()),b.size()));
}
static double run(unsigned blocks,int gear,float volume,int running=1){
    double power=0;
    for(unsigned i=0;i<blocks;++i){vr_whine_update(1,running,30,gear,volume,.01f);
        std::fill(output.begin(),output.end(),0);vr_whine_mix(input.data(),output.data(),int(output.size()),48000);
        power=0;for(size_t n=0;n<output.size();n+=2){CHECK(output[n]==output[n+1]);power+=double(output[n])*output[n];}}
    return power;
}
int main(int argc,char **argv){
    auto path=std::filesystem::temp_directory_path()/std::filesystem::path("nfsu2-whine-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    bool generated=argc==1;
    if(generated){auto dir=path/"MODPACKS"/"WHINE";std::filesystem::create_directories(dir);
        wav(dir/"car_whine.wav",64);wav(dir/"car_whine_2.wav",32);wav(dir/"car_whine_reverse.wav",128);
    }else path=std::filesystem::u8path(argv[1]);
    CHECK(vr_whine_start(path.u8string().c_str()) && vr_whine_available());
    std::fill(output.begin(),output.end(),123);CHECK(!vr_whine_mix(input.data(),output.data(),960,48000));CHECK(output[0]==123);
    vr_whine_enable(1);double steady=run(80,3,1);CHECK(steady>1000);
    double shifting=run(20,4,1);CHECK(shifting<steady*.05);CHECK(run(70,4,1)>1000);
    CHECK(run(160,4,0)==0);CHECK(run(80,0,1)>1000);CHECK(run(160,0,1,0)==0);
    CHECK(run(80,3,1)>1000);vr_whine_enable(0);CHECK(run(160,3,1)==0);
    vr_whine_enable(1);CHECK(run(80,3,1)>1000);std::this_thread::sleep_for(std::chrono::milliseconds(300));
    for(unsigned i=0;i<160;++i)vr_whine_mix(input.data(),output.data(),960,48000);
    CHECK(std::all_of(output.begin(),output.end(),[](int16_t n){return n==0;}));
    std::fill(output.begin(),output.end(),234);CHECK(!vr_whine_mix(input.data(),output.data(),959,48000) && output[0]==234);
    if(generated){
        auto dir=path/"MODPACKS"/"WHINE";
        for(const char *name:{"car_whine.wav","car_whine_2.wav","car_whine_reverse.wav"})std::filesystem::remove(dir/name);
        std::filesystem::remove(dir);std::filesystem::remove(path/"MODPACKS");std::filesystem::remove(path);
    }
    std::puts("Transmission whine: sound loading, stereo, upshift, reverse, volume, pause, disable and stale fade passed");return 0;
}
