#include "vr_texpacks.h"
#include "community/vendor/texpacks/inflate.h"
#include "../xboxrecomp/src/d3d/d3d8_swizzle.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>
namespace {
constexpr unsigned ARGB=21,DXT1=0x31545844,DXT3=0x33545844,DXT5=0x35545844;
uint32_t u32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
unsigned u16(const uint8_t *p){return p[0]|unsigned(p[1])<<8;}
size_t level_bytes(unsigned w,unsigned h,unsigned fmt){
    if(!w||!h||w>4096||h>4096)return 0;
    return fmt==ARGB?size_t(w)*h*4:fmt==DXT1?size_t((w+3)/4)*((h+3)/4)*8:
        fmt==DXT3||fmt==DXT5?size_t((w+3)/4)*((h+3)/4)*16:0;
}
uint64_t shape(unsigned w,unsigned h,unsigned f){return uint64_t(w)|(uint64_t(h)<<16)|(uint64_t(f)<<32);}
struct Entry {unsigned crc,adler,ow,oh,ofmt,w,h,fmt,levels,off,size,base,bsize;size_t decoded;bool shown=false;};
struct Pack {
    std::ifstream file;uint64_t size=0;unsigned version=0;std::vector<Entry> entries;
    bool read(unsigned off,void *p,size_t n){
        if(off>size||n>size-off)return false;file.clear();file.seekg(off);return bool(file.read((char*)p,n));
    }
    bool open(const std::filesystem::path &path){
        file.open(path,std::ios::binary);if(!file)return false;file.seekg(0,std::ios::end);auto end=file.tellg();
        if(end<16||end>512*1024*1024)return false;size=(uint64_t)end;
        uint8_t h[16];if(!read(0,h,16)||memcmp(h,"NTXP",4)||u32(h+12))return false;
        version=u32(h+4);unsigned n=u32(h+8);if((version!=1&&version!=2)||!n||n>8192)return false;
        size_t index=16+n*(version==2?48:40);std::vector<uint8_t>b(index);if(!read(0,b.data(),b.size()))return false;
        for(unsigned i=0;i<n;i++){
            const auto p=b.data()+16+i*40;Entry e{};e.crc=u32(p);e.adler=u32(p+4);e.ow=u16(p+8);e.oh=u16(p+10);e.ofmt=u32(p+12);
            e.w=u16(p+16);e.h=u16(p+18);e.fmt=u32(p+20);e.levels=u32(p+24);e.off=u32(p+28);e.size=u32(p+32);
            if(!level_bytes(e.ow,e.oh,e.ofmt)||!level_bytes(e.w,e.h,e.fmt)||!e.levels||e.levels>13)return false;
            unsigned w=e.w,h=e.h;for(unsigned l=0;l<e.levels;l++){e.decoded+=level_bytes(w,h,e.fmt);w=std::max(w/2,1u);h=std::max(h/2,1u);}
            if(e.decoded>64*1024*1024||e.off<index||!e.size||e.off>size||e.size>size-e.off)return false;
            if(version==1&&e.size!=e.decoded)return false;
            if(version==2){const auto q=b.data()+16+n*40+i*8;e.base=u32(q);e.bsize=u32(q+4);
                if(bool(e.base)!=bool(e.bsize)||(e.base&&(e.base<index||e.base>size||e.bsize>size-e.base)))return false;}
            if(!entries.empty()&&std::pair(e.crc,e.adler)<std::pair(entries.back().crc,entries.back().adler))return false;
            entries.push_back(e);
        }
        for(const auto &e:entries)if(e.base){
            auto it=std::find_if(entries.begin(),entries.end(),[&](const Entry &v){return v.off==e.base&&v.size==e.bsize&&!v.base&&v.decoded==e.decoded;});
            if(it==entries.end())return false;
        }
        return true;
    }
};
std::vector<std::unique_ptr<Pack>> packs;std::set<uint64_t> shapes;
std::vector<uint8_t> packed,decoded,base;std::vector<uint32_t> pixels;
std::vector<VRTrackMapEdit> edits;
std::array<uint32_t,256> crc_table;
uint32_t crc32(const uint8_t *p,size_t n){uint32_t c=~0u;while(n--)c=crc_table[(c^*p++)&255]^(c>>8);return ~c;}
uint32_t adler32(const uint8_t *p,size_t n){uint32_t a=1,b=0;while(n){size_t k=std::min(n,size_t(5552));n-=k;while(k--){a+=*p++;b+=a;}a%=65521;b%=65521;}return b<<16|a;}
bool unpack(Pack &p,unsigned at,unsigned n,std::vector<uint8_t> &out,size_t size){
    packed.resize(n);out.resize(size);return p.read(at,packed.data(),n)&&inflate_raw(packed.data(),n,out.data(),size);
}
void track_edits(const std::filesystem::path &path){
    const std::pair<const char*,unsigned> fields[]={{"TrackMapCalibrationOffsetX",0xac},{"TrackMapCalibrationOffsetY",0xb0},
        {"TrackMapCalibrationZoomIn",0xc0},{"MenuMapZoomOffsetX",0x118},{"MenuMapZoomOffsetY",0x11c},{"MenuMapZoomWidth",0x120}};
    std::ifstream f(path);std::string line;unsigned lines=0;
    while(std::getline(f,line)){
        if(++lines>1024||line.size()>256){edits.clear();return;}if(line.empty()||line[0]=='#')continue;
        unsigned track;char field[48],extra;float value;
        if(sscanf(line.c_str(),"%u %47s %f %c",&track,field,&value,&extra)!=3||track<4000||track>4801||!std::isfinite(value)||std::abs(value)>100000){edits.clear();return;}
        auto found=std::find_if(std::begin(fields),std::end(fields),[&](auto &v){return !strcmp(field,v.first);});
        if(found==std::end(fields)){edits.clear();return;}
        edits.push_back({(uint16_t)track,(uint16_t)found->second,value});
    }
}
}
extern "C" int vr_texpacks_start(const char *parent,int hud,int detailed){
    packs.clear();shapes.clear();edits.clear();
    if(!parent||!*parent)return 0;
    try {
        for(unsigned i=0;i<256;i++){uint32_t c=i;for(int k=0;k<8;k++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));crc_table[i]=c;}
        const char *names[]={"uhud","detailed"};
        for(unsigned i=0;i<2;i++)if(i?detailed:hud){auto p=std::make_unique<Pack>();
            auto folder=std::filesystem::u8path(parent)/"TEXPACKS";
            if(!p->open(folder/(std::string(names[i])+".pak"))){fprintf(stderr,"[VR textures] %s unavailable or invalid; original textures\n",names[i]);continue;}
            if(i){track_edits(folder/"detailed.tracks");if(edits.empty()){
                fprintf(stderr,"[VR textures] detailed map calibration missing or invalid; original map\n");continue;}}
            for(const auto &e:p->entries)shapes.insert(shape(e.ow,e.oh,e.ofmt));
            fprintf(stderr,"[VR textures] %s: %zu validated replacements\n",names[i],p->entries.size());
            packs.push_back(std::move(p));
        }
        return (int)packs.size();
    }catch(...){packs.clear();shapes.clear();edits.clear();return 0;}
}
extern "C" int vr_texpacks_shape(unsigned w,unsigned h,unsigned format){return shapes.count(shape(w,h,format))!=0;}
extern "C" const VRTrackMapEdit *vr_texpacks_map_edits(unsigned *count){if(count)*count=(unsigned)edits.size();return edits.data();}
extern "C" int vr_texpacks_find(const void *level,size_t bytes,unsigned w,unsigned h,unsigned format,int bc,VRTextureReplacement *out){
    if(!out)return 0;*out={};if(!level||bytes!=level_bytes(w,h,format)||!vr_texpacks_shape(w,h,format))return 0;
    try {
        auto crc=crc32((const uint8_t*)level,bytes),adler=adler32((const uint8_t*)level,bytes);
        for(auto &p:packs){auto it=std::lower_bound(p->entries.begin(),p->entries.end(),std::pair(crc,adler),[](const Entry &e,auto k){return std::pair(e.crc,e.adler)<k;});
            for(;it!=p->entries.end()&&it->crc==crc&&it->adler==adler;++it){auto &e=*it;if(e.ow!=w||e.oh!=h||e.ofmt!=format)continue;
                if(p->version==1){decoded.resize(e.decoded);if(!p->read(e.off,decoded.data(),decoded.size()))return 0;}
                else {if(!unpack(*p,e.off,e.size,decoded,e.decoded))return 0;if(e.base){if(!unpack(*p,e.base,e.bsize,base,e.decoded))return 0;
                    for(size_t j=0;j<decoded.size();j++)decoded[j]^=base[j];}}
                out->w=e.w;out->h=e.h;out->format=e.fmt;out->bytes=level_bytes(e.w,e.h,e.fmt);out->data=decoded.data();
                if(!bc&&e.fmt!=ARGB){pixels.resize(size_t(e.w)*e.h);unsigned color=e.fmt==DXT1?0x0c:e.fmt==DXT3?0x0e:0x0f;
                    for(unsigned y=0;y<e.h;y++)for(unsigned x=0;x<e.w;x++)if(!d3d8_dxt_decode_texel(decoded.data(),color,x,y,e.w,&pixels[size_t(y)*e.w+x]))return 0;
                    out->data=pixels.data();out->format=ARGB;out->bytes=pixels.size()*4;}
                if(!e.shown){fprintf(stderr,"[VR textures] replaced %ux%u -> %ux%u key=%08x/%08x\n",w,h,e.w,e.h,crc,adler);e.shown=true;}
                return 1;
            }
        }
    }catch(...){*out={};}return 0;
}
