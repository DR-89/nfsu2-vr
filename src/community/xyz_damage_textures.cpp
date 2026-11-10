#include "xyz_damage_textures.h"
#include "vendor/auth/sha256.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
namespace {
std::filesystem::path path;
struct Texture{uint32_t hash=0,info=0;};std::array<Texture,4> textures;
uint32_t u32(const uint8_t *p){uint32_t v;std::memcpy(&v,p,4);return v;}
uint16_t u16(const uint8_t *p){uint16_t v;std::memcpy(&v,p,2);return v;}
void put(void *p,uint32_t v){std::memcpy(p,&v,4);}
uint32_t hash(const char *s){uint32_t h=UINT32_MAX;while(*s)h=h*33+uint8_t(*s++);return h;}
struct Span{size_t at=0,size=0;};
bool chunks(const std::vector<uint8_t>& b,size_t begin,size_t end,std::array<Span,4>& out,int depth=0){
    if(depth>3 || begin>end || end>b.size())return false;
    for(size_t p=begin;p<end;){
        if(end-p<8)return false;uint32_t kind=u32(&b[p]),n=u32(&b[p+4]);size_t data=p+8;
        if(n>end-data)return false;
        if(kind==0xB3310000 || kind==0xB3320000){if(!chunks(b,data,data+n,out,depth+1))return false;}
        else {int slot=kind==0x33310002?0:kind==0x33310004?1:kind==0x33310005?2:kind==0x33320002?3:-1;
            if(slot>=0){if(out[slot].at)return false;out[slot]={data,n};}}
        p=data+n;
    }
    return true;
}
}
extern "C" void xyz_damage_assets_start(const char *parent){
    path.clear();if(parent && *parent)path=std::filesystem::u8path(parent)/"TEXPACKS"/"crash-scratches.tpk";
}
extern "C" uint32_t xyz_damage_texture(uint32_t h){for(const auto& t:textures)if(t.info && t.hash==h)return t.info;return 0;}
extern "C" int xyz_damage_textures_load(const XyzDamageHost *host){
    if(textures[0].info)return 1;
    if(path.empty() || !host || !host->alloc || !host->map || !host->free || !host->call || !host->valid)return 0;
    try{
        std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return 0;
        const auto length=f.tellg();if(length<8 || length>2*1024*1024)return 0;
        std::vector<uint8_t> b(static_cast<size_t>(length));f.seekg(0);if(!f.read(reinterpret_cast<char*>(b.data()),b.size()))return 0;
        uint8_t digest[32];char hex[65];nfsu2_sha256(b.data(),b.size(),digest);
        for(unsigned i=0;i<32;i++)std::snprintf(hex+i*2,3,"%02x",digest[i]);
        if(std::strcmp(hex,"9e76c6dbea1205f346e82028d74ab1ecad610014502ca27bb1db1fb227d2ecc4"))return 0;
        if(u32(b.data())!=0xB3300000 || u32(b.data()+4)!=b.size()-8)return 0;
        std::array<Span,4> spans{};if(!chunks(b,8,b.size(),spans) || spans[0].size!=32 || spans[1].size!=4*0x7c || spans[2].size!=4*0x20 || !spans[3].at)return 0;
        const size_t pixels=(spans[3].at+127)&~size_t(127),end=spans[3].at+spans[3].size;
        if(pixels>end)return 0;
        // Validate all inputs before allocating, including names, every mip,
        // source offsets and the DXT5 format. Never give a PC platform header to Xbox.
        for(unsigned i=0;i<4;i++){
            const uint8_t *ti=&b[spans[1].at+i*0x7c],*pl=&b[spans[2].at+i*0x20];char name[24];
            std::snprintf(name,sizeof name,"DAMAGE_SCRATCH%u",i+1);
            unsigned w=u16(ti+0x44),h=u16(ti+0x46),levels=ti[0x4e];uint32_t offset=u32(ti+0x30),size=u32(ti+0x38);
            if(u32(ti+0x24)!=hash(name) || u32(&b[spans[0].at+i*8])!=hash(name) || std::memcmp(ti+0xc,name,std::strlen(name)+1) ||
               !w || !h || w>1024 || h>1024 || (w&(w-1)) || (h&(h-1)) || !levels || levels>11 || u32(pl+0x14)!=0x35545844 ||
               u32(ti+0x3c)!=0 || offset>end-pixels || size>end-pixels-offset)return 0;
            size_t need=0;for(unsigned m=0;m<levels;m++){need+=((w+3)/4)*((h+3)/4)*16;w=w>1?w/2:1;h=h>1?h/2:1;}
            if(size!=need)return 0;
        }
        std::array<Texture,4> made{};std::array<uint32_t,4> allocations{};
        auto fail=[&](){for(auto a:allocations)if(a)host->free(a);return 0;};
        for(unsigned i=0;i<4;i++){
            const uint8_t *source=&b[spans[1].at+i*0x7c];uint32_t size=u32(source+0x38),bytes=256+size,a=host->alloc(bytes);
            if(!a)return fail();allocations[i]=a;if(!host->valid(a,bytes))return fail();
            auto *dst=static_cast<uint8_t*>(host->map(a));std::memset(dst,0,256);std::memcpy(dst,source,0x7c);
            const uint32_t platform=a+128,data=a+256;
            put(dst,platform);put(dst+4,0);put(dst+8,0);put(dst+0x2c,0);put(dst+0x68,0);put(dst+0x6c,a);
            put(dst+0x70,data);put(dst+0x74,0);put(dst+0x78,0);put(dst+128+0xc,0x0f);
            std::memcpy(dst+256,&b[pixels+u32(source+0x30)],size);
            host->call(0x9C860,a,0,nullptr,nullptr);
            if((u32(dst+128+0x10)&0x70000)!=0x40000 || u32(dst+128+0x14)!=(data&0x7fffffff))return fail();
            made[i]={u32(source+0x24),a};
        }
        textures=made;
        std::fprintf(stderr,"[damage] four FRS Mods scratch textures initialized as native Xbox DXT5\n");return 1;
    }catch(...){return 0;}
}
