#include "../src/vr_vinyls.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
using Bytes=std::vector<uint8_t>;
void word(Bytes &b,size_t o,uint32_t v){for(int i=0;i<4;i++)b.at(o+i)=uint8_t(v>>(i*8));}
void half(Bytes &b,size_t o,unsigned v){b.at(o)=v&255;b.at(o+1)=v>>8;}
uint32_t adler(const Bytes &b){uint32_t a=1,s=0;for(auto v:b){a=(a+v)%65521;s=(s+a)%65521;}return s<<16|a;}
Bytes deflate(const Bytes &b){Bytes out;size_t at=0;while(at<b.size()){size_t n=std::min(size_t(65535),b.size()-at);out.push_back(at+n==b.size());out.push_back(n&255);out.push_back(n>>8);out.push_back(~n&255);out.push_back((~n>>8)&255);out.insert(out.end(),b.begin()+at,b.begin()+at+n);at+=n;}return out;}
struct Memory {std::map<uint32_t,Bytes> blocks;};
int read(void *opaque,uint32_t a,void *out,size_t n){auto &b=((Memory*)opaque)->blocks;auto it=b.upper_bound(a);if(it==b.begin())return 0;--it;size_t off=a-it->first;if(off>it->second.size()||n>it->second.size()-off)return 0;memcpy(out,it->second.data()+off,n);return 1;}
int main(){
    namespace fs=std::filesystem;
    auto root=fs::temp_directory_path()/("nfsu2-vinyl-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"HQVINYLS");struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}}cleanup{root};
    Bytes picture(1024*1024*3),mask(1024*1024,64);
    mask[1]=0;
    for(size_t i=0;i<picture.size();i+=3){picture[i]=128;picture[i+1]=64;picture[i+2]=1;}
    auto p=deflate(picture),m=deflate(mask);Bytes pack(64);memcpy(pack.data(),"NHQ1",4);word(pack,4,1);word(pack,8,2);word(pack,12,64);
    word(pack,16,10);word(pack,20,64);word(pack,24,unsigned(p.size()));half(pack,28,1024);pack[30]=3;word(pack,32,adler(picture));word(pack,36,unsigned(picture.size()));
    word(pack,40,20);word(pack,44,unsigned(64+p.size()));word(pack,48,unsigned(m.size()));half(pack,52,1024);pack[54]=1;word(pack,56,adler(mask));word(pack,60,unsigned(mask.size()));
    pack.insert(pack.end(),p.begin(),p.end());pack.insert(pack.end(),m.begin(),m.end());
    {std::ofstream f(root/"HQVINYLS/RX8.nhq",std::ios::binary);f.write((char*)pack.data(),pack.size());}
    Memory mem;auto &b=mem.blocks;
    b[0x45af88]=Bytes(4);word(b[0x45af88],0,0x20000);
    b[0x10000]=Bytes(4);b[0x20000]=Bytes(0x890);memcpy(b[0x20000].data()+0x20,"RX8",4);
    auto &r=b[0x30000]=Bytes(0x234);word(r,0,0x40000);word(r,4,0xff102030);word(r,0x1c4,1);word(r,0x230,0x10000);
    word(r,0x24,10);word(r,0x28,0x50000);word(r,0x2c,0x60000);word(r,0x30,0x80300000);word(r,0x34,0x80400000);
    auto &ti=b[0x40000]=Bytes(0x7c);half(ti,0x44,512);half(ti,0x46,512);ti[0x4a]=8;ti[0x4e]=1;word(ti,0x70,0x80100000);word(ti,0x74,0x80200000);
    b[0x50000]=ti;b[0x50000][0x4a]=0; // On-disc encoding differs from the locked P8 recipe view.
    b[0x60000]=Bytes(0x7c);word(b[0x60000],0x24,20);
    b[0x80100000]=Bytes(512*512);b[0x80200000]=Bytes(1024);word(b[0x80200000],0,0x12345678);
    b[0x80300000]=Bytes(512*512);b[0x80400000]=Bytes(1024);word(b[0x80400000],0,0x80000000);
    std::vector<uint32_t> stock(512*512,0x12345678);VRTextureReplacement out{};
    CHECK(!vr_vinyls_start(root.u8string().c_str(),0));CHECK(!vr_vinyls_begin(read,&mem,0x30000));
    CHECK(vr_vinyls_start(root.u8string().c_str(),1));auto job=vr_vinyls_begin(read,&mem,0x30000);CHECK(job);
    vr_vinyls_finish(job,read,&mem,1);auto revision=vr_vinyls_revision(0x100000);CHECK(revision);
    CHECK(vr_vinyls_find(0x100000,stock.data(),stock.size(),&out));CHECK(out.w==1024&&out.h==1024&&out.bytes==4194304);
    const uint32_t *paint=(const uint32_t*)out.data;
    // A half mask preserves gloss, blends each byte with integer truncation,
    // then converts RGBA to the Vulkan BGRA upload format.
    CHECK(paint[0]==0xbf483018);CHECK(paint[1]==0xff102030);CHECK(paint[1023*1024+1023]==0x12345678);
    // Streaming relocation retains content identity, but unrelated/recycled
    // textures never inherit an old car's paint.
    CHECK(vr_vinyls_revision(0x500000));
    CHECK(vr_vinyls_find(0x500000,stock.data(),stock.size(),&out));
    stock[10]^=1;CHECK(!vr_vinyls_find(0x500000,stock.data(),stock.size(),&out));stock[10]^=1;
    stock[10]^=1;CHECK(!vr_vinyls_find(0x100000,stock.data(),stock.size(),&out));stock[10]^=1;
    word(r,0x44,1);word(r,0x48,0xff0000ff);word(r,0x4c,0x8000ff00);word(r,0x50,0xffffffff);
    job=vr_vinyls_begin(read,&mem,0x30000);CHECK(job&&!vr_vinyls_revision(0x100000));vr_vinyls_finish(job,read,&mem,1);
    CHECK(vr_vinyls_revision(0x100000)>revision);CHECK(vr_vinyls_find(0x100000,stock.data(),stock.size(),&out));
    CHECK(((const uint32_t*)out.data)[0]==0xbf487058); // Native B,G,R weights; weight 1 is discarded.
    word(r,0x24,999);job=vr_vinyls_begin(read,&mem,0x30000);CHECK(job);vr_vinyls_finish(job,read,&mem,1);CHECK(!vr_vinyls_revision(0x100000));
    word(r,0x1c4,9);CHECK(!vr_vinyls_begin(read,&mem,0x30000));word(r,0x1c4,1);word(r,0x24,10);
    job=vr_vinyls_begin(read,&mem,0x30000);CHECK(job);vr_vinyls_finish(job,read,&mem,0);CHECK(!vr_vinyls_revision(0x100000));
    // A new car must not re-upload every already identified relocated skin.
    job=vr_vinyls_begin(read,&mem,0x30000);CHECK(job);vr_vinyls_finish(job,read,&mem,1);
    CHECK(vr_vinyls_find(0x500000,stock.data(),stock.size(),&out));
    auto alias_revision=vr_vinyls_revision(0x500000);
    b[0x80700000]=Bytes(512*512);b[0x80800000]=Bytes(1024);word(b[0x80800000],0,0x87654321);
    word(ti,0x70,0x80700000);word(ti,0x74,0x80800000);
    job=vr_vinyls_begin(read,&mem,0x30000);CHECK(job);vr_vinyls_finish(job,read,&mem,1);
    CHECK(vr_vinyls_revision(0x700000)>alias_revision);
    CHECK(vr_vinyls_revision(0x500000)==alias_revision);
    CHECK(vr_vinyls_find(0x500000,stock.data(),stock.size(),&out));
    word(ti,0x70,0x80100000);word(ti,0x74,0x80200000);
    // Valid index but corrupt compressed payload must retain the retail skin.
    pack.back()^=1;{std::ofstream f(root/"HQVINYLS/RX8.nhq",std::ios::binary|std::ios::trunc);f.write((char*)pack.data(),pack.size());}
    vr_vinyls_start(root.u8string().c_str(),1);job=vr_vinyls_begin(read,&mem,0x30000);CHECK(job);vr_vinyls_finish(job,read,&mem,1);CHECK(!vr_vinyls_revision(0x100000));
    vr_vinyls_start(nullptr,0);puts("PASS HQ vinyl composition, gloss, swatches, repaint ownership and corrupt-pack fallback");return 0;
}
