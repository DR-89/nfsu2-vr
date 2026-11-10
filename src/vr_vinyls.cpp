#include "vr_vinyls.h"
#include "community/vendor/texpacks/inflate.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {
constexpr unsigned side=1024,game=512;
using Bytes=std::vector<uint8_t>;
uint32_t u32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
unsigned u16(const uint8_t *p){return p[0]|unsigned(p[1])<<8;}
uint32_t adler(const Bytes &b){uint32_t a=1,s=0;size_t at=0;while(at<b.size()){size_t end=std::min(at+5552,b.size());while(at<end){a+=b[at++];s+=a;}a%=65521;s%=65521;}return s<<16|a;}
uint64_t hash(const void *v,size_t n){auto p=(const uint8_t*)v;uint64_t h=14695981039346656037ull;while(n--)h=(h^*p++)*1099511628211ull;return h;}
uint32_t bgra(uint32_t rgba){return (rgba&0xff00ff00u)|(rgba&255u)<<16|(rgba>>16&255u);}
bool name_ok(const char *s){size_t n=0;for(;s[n]&&n<16;n++)if((s[n]<'A'||s[n]>'Z')&&(s[n]<'0'||s[n]>'9'))return false;return n>0&&n<16;}
struct Row {uint32_t key,off,bytes,checksum;unsigned channels;};
struct Pack {
    std::ifstream file;uint64_t size=0;std::vector<Row> rows;
    bool read(uint32_t off,void *p,size_t n){if(off>size||n>size-off)return false;file.clear();file.seekg(off);return bool(file.read((char*)p,n));}
    bool open(const std::filesystem::path &path){
        file.open(path,std::ios::binary);if(!file)return false;file.seekg(0,std::ios::end);auto end=file.tellg();
        if(end<40||end>512*1024*1024)return false;size=uint64_t(end);
        uint8_t h[16];if(!read(0,h,16)||memcmp(h,"NHQ1",4)||u32(h+4)!=1)return false;
        unsigned n=u32(h+8),start=u32(h+12);if(!n||n>4096||start!=16+24*n)return false;
        Bytes index(24*n);if(!read(16,index.data(),index.size()))return false;
        uint64_t previous_end=start;
        for(unsigned i=0;i<n;i++){
            auto p=index.data()+24*i;Row r{u32(p),u32(p+4),u32(p+8),u32(p+16),p[14]};
            if((i&&r.key<=rows.back().key)||u16(p+12)!=side||(r.channels!=1&&r.channels!=3)||p[15]||
                u32(p+20)!=side*side*r.channels||r.off<previous_end||r.off>size||!r.bytes||r.bytes>4*1024*1024||r.bytes>size-r.off)return false;
            previous_end=uint64_t(r.off)+r.bytes;rows.push_back(r);
        }
        return true;
    }
    bool picture(uint32_t key,Bytes &out,unsigned &channels){
        auto it=std::lower_bound(rows.begin(),rows.end(),key,[](const Row &r,uint32_t k){return r.key<k;});
        if(it==rows.end()||it->key!=key)return false;Bytes encoded(it->bytes);out.resize(side*side*it->channels);
        if(!read(it->off,encoded.data(),encoded.size())||!inflate_raw(encoded.data(),encoded.size(),out.data(),out.size())||adler(out)!=it->checksum)return false;
        channels=it->channels;return true;
    }
};
struct Layer {uint32_t picture,mask,col[3];bool remap;Bytes alpha;};
struct Job {uint32_t target,physical,base;char car[16];std::vector<Layer> layers;};
struct Skin {uint64_t serial,fingerprint;std::shared_ptr<std::vector<uint32_t>> pixels;bool shown=false,warned=false;};
bool active=false;std::filesystem::path folder;
std::mutex paint_mutex,skin_mutex;
std::map<std::string,std::unique_ptr<Pack>> packs;
std::map<uint32_t,Skin> skins;uint64_t serial=0;unsigned logged=0;
std::map<uint32_t,uint32_t> aliases;
thread_local std::shared_ptr<std::vector<uint32_t>> render_hold;
void fallback(const char *car,const char *reason){
    if(logged++<40)fprintf(stderr,"[VR vinyls] %s: %s; original skin\n",car,reason);
}
void *reject(const char *reason){std::lock_guard<std::mutex> lock(paint_mutex);fallback("recipe",reason);return nullptr;}
struct Memory {
    VRVinylRead read;void *context;
    bool get(uint32_t a,void *out,size_t n){return a&&n<=4*1024*1024&&uint64_t(a)+n<=UINT32_MAX&&read&&read(context,a,out,n);}
    uint32_t word(uint32_t a){uint8_t b[4];return get(a,b,4)?u32(b):0;}
};
unsigned swizzle(unsigned x,unsigned y){unsigned r=0;for(unsigned i=0;i<9;i++)r|=((x>>i)&1)<<(i*2)|((y>>i)&1)<<(i*2+1);return r;}
bool original(Memory &m,uint32_t ti,std::vector<uint32_t> &out,uint32_t &physical){
    uint8_t info[0x7c];if(!m.get(ti,info,sizeof info)||u16(info+0x44)!=game||u16(info+0x46)!=game||info[0x4a]!=8||info[0x4e]!=1)return false;
    Bytes pixels(game*game);std::array<uint32_t,256> palette;
    uint32_t px=u32(info+0x70),pal=u32(info+0x74);if(!(px&0x80000000u)||!m.get(px,pixels.data(),pixels.size())||!m.get(pal,palette.data(),sizeof palette))return false;
    physical=px&0x7fffffffu;out.resize(game*game);
    for(unsigned y=0;y<game;y++)for(unsigned x=0;x<game;x++)out[y*game+x]=palette[pixels[swizzle(x,y)]];
    return true;
}
uint8_t alpha_at(const Bytes &plane,unsigned x,unsigned y){
    // Bilinear sampling of the game's gloss plane, matching the browser mod.
    int sx=int(x/2),sy=int(y/2),dx=(x&1)?1:-1,dy=(y&1)?1:-1;
    unsigned nx=unsigned(std::clamp(sx+dx,0,int(game)-1)),ny=unsigned(std::clamp(sy+dy,0,int(game)-1));
    return uint8_t((9*plane[sy*game+sx]+3*plane[sy*game+nx]+3*plane[ny*game+sx]+plane[ny*game+nx]+8)/16);
}
bool compose(const Job &job,Pack &pack,const std::vector<uint32_t> &original,std::vector<uint32_t> &result){
    result.assign(side*side,job.base);
    for(const auto &layer:job.layers){
        Bytes picture,mask;unsigned pc=0,mc=0;
        if(!pack.picture(layer.picture,picture,pc)||!pack.picture(layer.mask,mask,mc))return false;
        for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++){
            size_t at=size_t(y)*side+x;unsigned m=mask[at*mc];if(!m)continue;
            unsigned s[4]={picture[at*pc],picture[at*pc+(pc==3?1:0)],picture[at*pc+(pc==3?2:0)],255};
            if(layer.remap){unsigned w[3]={s[0],s[1],s[2]};std::fill(std::begin(s),std::end(s),0);
                for(unsigned k=0;k<3;k++)if(w[k]>=2){for(unsigned c=0;c<3;c++)s[c]+=((layer.col[k]>>(8*c))&255)*w[k]/255;
                    s[3]=std::max(s[3],(layer.col[k]>>24)*w[k]/255);}
                for(auto &v:s)v=std::min(v,255u);
            }else s[3]=alpha_at(layer.alpha,x,y);
            uint32_t d=result[at],v=0;for(unsigned c=0;c<4;c++)v|=(m>127?s[c]:(s[c]*m/128+((d>>(8*c))&255)*(128-m)/128))<<(8*c);
            result[at]=v;
        }
    }
    for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++)result[y*side+x]=y>=1016&&x>=968?original[(y/2)*game+x/2]:bgra(result[y*side+x]);
    return true;
}
}
extern "C" int vr_vinyls_start(const char *parent,int enabled){
    active=false;packs.clear();skins.clear();aliases.clear();render_hold.reset();serial=0;logged=0;
    if(!parent||!*parent||!enabled)return 0;
    try{folder=std::filesystem::u8path(parent)/"HQVINYLS";active=std::filesystem::is_directory(folder);
        fprintf(stderr,"[VR vinyls] %s\n",active?"1024 vinyls enabled; original skins retained as fallback":"optional packs unavailable; original skins");
    }catch(...){active=false;}return active;
}
extern "C" void *vr_vinyls_begin(VRVinylRead read,void *context,uint32_t recipe){
    if(!active)return nullptr;
    try{
        Memory m{read,context};uint8_t r[0x234],info[0x7c];if(!m.get(recipe,r,sizeof r))return reject("recipe memory");
        auto job=std::make_unique<Job>();job->target=u32(r);job->base=bgra(u32(r+4));
        if(!m.get(job->target,info,sizeof info)||u16(info+0x44)!=game||u16(info+0x46)!=game||info[0x4a]!=8||info[0x4e]!=1)return reject("target layout");
        job->physical=u32(info+0x70)&0x7fffffffu;
        {std::lock_guard<std::mutex> lock(skin_mutex);skins.erase(job->physical);}
        uint32_t n=u32(r+0x1c4),ride=u32(r+0x230),type=m.word(ride),types=m.word(0x45af88);
        if(!n||n>8||!ride||!types||type>=46||!m.get(types+type*0x890+0x20,job->car,16)||!name_ok(job->car))return reject("car or layers");
        for(unsigned i=0;i<n;i++){
            auto p=r+0x24+i*0x34;Layer layer{};layer.picture=u32(p);layer.mask=m.word(u32(p+8)+0x24);layer.remap=u32(p+0x20)!=0;
            if(!layer.picture||!layer.mask)return reject("layer hashes");
            // Xbox P8 recipes already contain BGRA (0x2017ab copies the base
            // straight to the palette). Its remap weights are B,G,R, read at
            // 0x201856..0x20186e. HQV1 pictures and this compositor use R,G,B.
            for(unsigned k=0;k<3;k++)layer.col[k]=bgra(u32(p+0x24+(2-k)*4));
            if(!layer.remap){
                uint32_t ti=u32(p+4);uint8_t li[0x7c];if(!m.get(ti,li,sizeof li)||u16(li+0x44)!=game||u16(li+0x46)!=game)return reject("layer dimensions");
                layer.alpha.resize(game*game);
                // This is 0x201520's locked P8 view, regardless of the source
                // TextureInfo's on-disc encoding. The original reads one
                // index at 0x201825 and its palette entry at 0x20182f.
                Bytes indices(game*game);std::array<uint32_t,256> palette;
                if(!m.get(u32(p+0xc),indices.data(),indices.size())||!m.get(u32(p+0x10),palette.data(),sizeof palette))return reject("P8 layer memory");
                for(size_t k=0;k<indices.size();k++)layer.alpha[k]=uint8_t(palette[indices[k]]>>24);
            }
            job->layers.push_back(std::move(layer));
        }
        return job.release();
    }catch(...){return nullptr;}
}
extern "C" void vr_vinyls_finish(void *opaque,VRVinylRead read,void *context,int success){
    std::unique_ptr<Job> job((Job*)opaque);if(!job||!success)return;
    try{
        std::lock_guard<std::mutex> painting(paint_mutex);
        auto it=packs.find(job->car);if(it==packs.end()){
            auto pack=std::make_unique<Pack>();if(!pack->open(folder/(std::string(job->car)+".nhq")))pack.reset();
            it=packs.emplace(job->car,std::move(pack)).first;
        }
        if(!it->second){fallback(job->car,"pack unavailable or invalid");return;}
        Memory m{read,context};std::vector<uint32_t> stock;uint32_t physical=0;
        if(!original(m,job->target,stock,physical)||physical!=job->physical){fallback(job->car,"paint target changed");return;}
        auto pixels=std::make_shared<std::vector<uint32_t>>();
        if(!compose(*job,*it->second,stock,*pixels)){fallback(job->car,"picture unavailable or invalid");return;}
        Skin skin{++serial,hash(stock.data(),stock.size()*4),std::move(pixels)};
        {std::lock_guard<std::mutex> lock(skin_mutex);skins[physical]=std::move(skin);
            if(skins.size()>72){auto oldest=std::min_element(skins.begin(),skins.end(),[](auto &a,auto &b){return a.second.serial<b.second.serial;});skins.erase(oldest);}}
        if(logged++<40)fprintf(stderr,"[VR vinyls] %s skin=%08x layers=%zu: painted at 1024\n",job->car,physical,job->layers.size());
    }catch(...){}
}
extern "C" uint64_t vr_vinyls_revision(uint32_t physical){
    if(!active)return 0;std::lock_guard<std::mutex> lock(skin_mutex);
    // The retail streaming loader may relocate a painted P8 texture before
    // its first race draw. Unknown addresses must be checked against complete
    // decoded contents after a new paint; an address alone is not identity.
    auto it=skins.find(physical);
    if(it!=skins.end())return it->second.serial;
    auto alias=aliases.find(physical);
    if(alias!=aliases.end()){
        it=skins.find(alias->second);
        if(it!=skins.end())return it->second.serial;
    }
    return skins.empty()?0:serial;
}
extern "C" int vr_vinyls_find(uint32_t physical,const uint32_t *original,size_t pixels,VRTextureReplacement *out){
    if(!active||!out||!original||pixels!=game*game)return 0;
    std::lock_guard<std::mutex> lock(skin_mutex);auto it=skins.find(physical);
    const uint64_t fingerprint=hash(original,pixels*4);
    if(it!=skins.end() && it->second.fingerprint!=fingerprint){
        if(!it->second.warned){it->second.warned=true;fprintf(stderr,"[VR vinyls] skin=%08x original pixels changed; original texture retained\n",physical);}
        return 0;
    }
    if(it==skins.end()){
        for(auto candidate=skins.begin();candidate!=skins.end();++candidate)if(candidate->second.fingerprint==fingerprint){
            // Two equal low-resolution skins can conceivably have different
            // high-resolution artwork. Never choose arbitrarily in that case.
            if(it!=skins.end() && *it->second.pixels!=*candidate->second.pixels)return 0;
            it=candidate;
        }
        if(it==skins.end())return 0;
        if(aliases.size()>512)aliases.clear();
        aliases[physical]=it->first;
    }
    if(!it->second.shown){it->second.shown=true;fprintf(stderr,"[VR vinyls] Vulkan replacement skin=%08x source=%08x revision=%llu\n",physical,it->first,(unsigned long long)it->second.serial);}
    render_hold=it->second.pixels;*out={side,side,21,side*side*4,render_hold->data()};return 1;
}
