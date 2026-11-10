#include "xyz_skins.h"
#include <cstdio>
#include <cstring>
#include <vector>
namespace xyz {
namespace {
constexpr uint32_t ti_size=0x7c,platform_size=0x3c;
uint32_t word(const uint8_t *b){uint32_t v;std::memcpy(&v,b,4);return v;}
void put(uint8_t *b,uint32_t v){std::memcpy(b,&v,4);}
uint32_t hash_name(const char *s){uint32_t h=UINT32_MAX;for(;*s;++s)h=h*33+uint8_t(*s);return h;}
bool read(const XyzXboxGuest &g,uint32_t a,void *out,size_t n){return g.read && xyz_xbox_address(a,n) && g.read(g.context,a,out,n);}
bool call(const XyzXboxGuest &g,XyzXboxOp op,uint32_t self,unsigned n,const uint32_t *a,uint32_t &out){return g.call && g.call(g.context,op,self,0,n,a,&out);}
}
int Skins::create(const XyzXboxGuest &g,unsigned skin,unsigned part,Texture &out){
    static const char *const names[]={"DUMMY_SKIN%u","DUMMY_WHEEL%u","DUMMY_SPINNER%u"};
    uint8_t info[ti_size],platform[platform_size];uint32_t source=0;
    char name[24];std::snprintf(name,sizeof name,names[part],skin);
    const uint32_t hash=hash_name(name);
    auto error=[&](const char *reason,uint32_t detail=0){
        std::fprintf(stderr,"[XYZ paint] slot=%u part=%u failed=%s source=%08x detail=%08x\n",skin,part,reason,source,detail);return -1;
    };
    // Never replace an existing game's texture, including a texture mod.
    uint32_t args[3]={hash,0,0},existing=0;
    if(!call(g,XYZ_G_TEXTURE_FIND,0,3,args,existing))return error("texture-find");
    if(existing)return error("existing-texture",existing);
    for(unsigned n=1;n<=6 && !source;++n){
        std::snprintf(name,sizeof name,names[part],n);args[0]=hash_name(name);
        uint32_t candidate=0;
        if(!call(g,XYZ_G_TEXTURE_FIND,0,3,args,candidate))return error("template-find");
        if(candidate && read(g,candidate,info,sizeof info) && read(g,word(info),platform,sizeof platform))source=candidate;
    }
    if(!source)return 0; // The local car's paint pack has not arrived yet.
    unsigned w=unsigned(info[0x44])|unsigned(info[0x45])<<8;
    unsigned h=unsigned(info[0x46])|unsigned(info[0x47])<<8;
    uint32_t pixels=word(info+0x38),palette=word(info+0x3c);
    // Audited US Xbox dummy textures are one-mip P8, with a 256-entry palette.
    // Other texture encodings need a separate port, never guessed GPU headers.
    if(!w || !h || w>1024 || h>1024 || (w&(w-1)) || (h&(h-1)) ||
       info[0x4a]!=8 || info[0x4e]!=1 || word(platform+0xc)!=11 ||
       pixels!=w*h || word(info+0x40)!=pixels || palette!=1024 || word(info+0x2c)){
        std::fprintf(stderr,"[XYZ paint] template=%08x size=%ux%u format=%u mips=%u platform=%u pixels=%u mip0=%u palette=%u alias=%08x\n",
            source,w,h,info[0x4a],info[0x4e],word(platform+0xc),pixels,word(info+0x40),palette,word(info+0x2c));
        return error("template-layout");
    }
    std::vector<uint8_t> data(pixels+palette);
    if(!read(g,word(info+0x70),data.data(),pixels) ||
       !read(g,word(info+0x74),data.data()+pixels,palette))return error("template-data");
    uint32_t bytes=ti_size+platform_size+127+pixels+palette,address=0;
    if(!call(g,XYZ_G_PAINT_ALLOC,0,1,&bytes,address) || !address)return error("allocate");
    auto fail=[&](const char *reason){uint32_t ignored=0;call(g,XYZ_G_PAINT_FREE,0,1,&address,ignored);return error(reason,address);};
    // The original Xbox header builder masks the high bit to get a physical
    // address. Require actual RAM alias storage, not an arbitrary guest heap.
    if(address<0x80000000u || !xyz_xbox_address(address,bytes) || !g.write)return error("allocation-range",address);
    auto overlaps=[&](uint32_t a,uint32_t n){return uint64_t(address)<uint64_t(a)+n && uint64_t(a)<uint64_t(address)+bytes;};
    if(overlaps(source,ti_size) || overlaps(word(info),platform_size) ||
       overlaps(word(info+0x70),pixels) || overlaps(word(info+0x74),palette))return error("allocation-alias",address);
    uint32_t pl=address+ti_size,px=(pl+platform_size+127)&~127u,pal=px+pixels;
    put(info,pl);put(info+0x24,hash);put(info+0x68,0);put(info+0x6c,address);
    put(info+0x70,px);put(info+0x74,pal);put(info+0x78,0);
    std::snprintf(name,sizeof name,names[part],skin);
    std::memset(info+0xc,0,24);std::memcpy(info+0xc,name,std::strlen(name));
    // 0x9C860 builds the embedded Xbox texture/palette headers (+10/+24).
    // It does not allocate host Direct3D objects or touch the source texture.
    std::memset(platform+0x10,0,platform_size-0x10);
    if(!g.write(g.context,address,info,sizeof info) || !g.write(g.context,pl,platform,sizeof platform) ||
       !g.write(g.context,px,data.data(),data.size()))return fail("write");
    uint32_t ignored=0;
    if(!call(g,XYZ_G_TEXTURE_INIT,address,0,nullptr,ignored) || !read(g,pl,platform,sizeof platform) ||
       (word(platform+0x10)&0x70000)!=0x40000 || word(platform+0x14)!=(px&0x7fffffff) ||
       (word(platform+0x24)&0x70000)!=0x30000 || word(platform+0x28)!=(pal&0x7fffffff))return fail("header-init");
    out={hash,address,address,bytes};return 1;
}
int Skins::prepare(const XyzXboxGuest &g,unsigned skin){
    if(skin<first || skin>=first+count)return -1;
    auto &s=slots_[skin-first];
    {std::lock_guard<std::mutex> lock(mutex_);if(s.ready)return 1;}
    for(unsigned i=0;i<s.textures.size();++i)if(!s.textures[i].info){int result=create(g,skin,i,s.textures[i]);if(result<=0)return result;}
    {std::lock_guard<std::mutex> lock(mutex_);s.ready=true;}
    unsigned bytes=0;for(const auto &t:s.textures)bytes+=t.bytes;
    std::fprintf(stderr,"[XYZ paint] slot=%u private textures=3 bytes=%u\n",skin,bytes);return 1;
}
uint32_t Skins::lookup(uint32_t hash) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for(const auto &s:slots_)if(s.ready)for(const auto &t:s.textures)if(t.hash==hash)return t.info;
    return 0;
}
}
