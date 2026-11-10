#include "../src/vr_texpacks.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace fs=std::filesystem;
static void put(std::vector<uint8_t>&b,size_t at,uint32_t v){for(unsigned i=0;i<4;i++)b[at+i]=(uint8_t)(v>>(8*i));}
static void save(const fs::path &p,const std::vector<uint8_t>&b){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write((char*)b.data(),b.size());}
static std::vector<uint8_t> read(const fs::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char **argv){
    auto folder=fs::temp_directory_path()/"nfsu2-texture-pack-test";fs::create_directories(folder/"TEXPACKS");
    auto path=folder/"TEXPACKS/uhud.pak";std::array<uint32_t,16> original;original.fill(0xffff0000);
    VRTextureReplacement out{};
    CHECK(vr_texpacks_start(folder.u8string().c_str(),0,0)==0);
    CHECK(!vr_texpacks_find(original.data(),64,4,4,21,1,&out));
    std::vector<uint8_t> b(128);memcpy(b.data(),"NTXP",4);put(b,4,1);put(b,8,1);
    put(b,16,0xfa07caa5);put(b,20,0xec7d1fe1);put(b,24,4|(4<<16));put(b,28,21);
    put(b,32,8|(8<<16));put(b,36,0x35545844);put(b,40,1);put(b,44,64);put(b,48,64);
    for(unsigned i=0;i<4;i++){b[64+16*i]=255;b[64+16*i+9]=0xf8;}
    save(path,b);CHECK(vr_texpacks_start(folder.u8string().c_str(),1,0)==1);
    CHECK(vr_texpacks_shape(4,4,21)&&!vr_texpacks_shape(8,4,21));
    CHECK(vr_texpacks_find(original.data(),64,4,4,21,1,&out));
    CHECK(out.w==8&&out.h==8&&out.bytes==64&&out.format==0x35545844&&!memcmp(out.data,b.data()+64,64));
    CHECK(vr_texpacks_find(original.data(),64,4,4,21,0,&out)&&out.bytes==256&&out.format==21);
    for(unsigned i=0;i<64;i++)CHECK(((const uint32_t*)out.data)[i]==0xffff0000);
    original[0]^=1;CHECK(!vr_texpacks_find(original.data(),64,4,4,21,0,&out));original[0]^=1;
    CHECK(!vr_texpacks_find(original.data(),63,4,4,21,0,&out));
    const uint8_t deflated[]={251,207,0,5,63,32,212,127,18,249,0};
    b.resize(64+sizeof deflated);put(b,4,2);put(b,48,sizeof deflated);memcpy(b.data()+64,deflated,sizeof deflated);
    save(path,b);CHECK(vr_texpacks_start(folder.u8string().c_str(),1,0)==1);
    CHECK(vr_texpacks_find(original.data(),64,4,4,21,0,&out));
    for(unsigned i=0;i<64;i++)CHECK(((const uint32_t*)out.data)[i]==0xffff0000);
    b.back()^=0x80;save(path,b);CHECK(vr_texpacks_start(folder.u8string().c_str(),1,0)==1);
    // A definitely truncated stream is rejected even when its index is valid.
    put(b,48,1);save(path,b);CHECK(vr_texpacks_start(folder.u8string().c_str(),1,0)==1);
    CHECK(!vr_texpacks_find(original.data(),64,4,4,21,0,&out));
    put(b,44,0xfffffff0);save(path,b);CHECK(!vr_texpacks_start(folder.u8string().c_str(),1,0));
    put(b,44,64);put(b,8,0xffffffff);save(path,b);CHECK(!vr_texpacks_start(folder.u8string().c_str(),1,0));
    vr_texpacks_start(nullptr,0,0);fs::remove(path);fs::remove(folder/"TEXPACKS");fs::remove(folder);
    if(argc==3){
        CHECK(vr_texpacks_start(argv[1],1,1)==2);unsigned count=0;CHECK(vr_texpacks_map_edits(&count)&&count==88);
        auto source=read(argv[2]);CHECK(source.size()>=0x300+262144);
        CHECK(vr_texpacks_find(source.data()+0x300,262144,512,512,0x33545844,1,&out));
        CHECK(out.w==2048&&out.h==2048&&out.bytes==4194304&&out.format==0x35545844);
        // Independent zlib output is compared by the caller's fixture audit.
        auto output=fs::path(argv[2]);output+=".replacement.bc3";save(output,std::vector<uint8_t>((const uint8_t*)out.data,(const uint8_t*)out.data+out.bytes));
    }
    puts("Texture packs: v1/v2, hashes, dimensions, BC3/CPU decode, invalid extents and truncation passed.");return 0;
}
