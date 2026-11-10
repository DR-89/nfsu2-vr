#include "xyz_skins.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <map>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
constexpr uint32_t base=0x80000000,source=base+0x10000;
std::vector<uint8_t> mem(8*1024*1024);
uint32_t next_address=base+0x100000;unsigned allocs=0,frees=0,inits=0;
bool missing=false,bad_header=false;unsigned unavailable=99;
bool refuse_alloc=false,refuse_free=false;
std::map<uint32_t,uint32_t> allocations;
uint32_t word(uint32_t a){uint32_t v;std::memcpy(&v,&mem.at(a-base),4);return v;}
void put(uint32_t a,uint32_t v){std::memcpy(&mem.at(a-base),&v,4);}
uint32_t hash(const char *s){uint32_t h=UINT32_MAX;for(;*s;++s)h=h*33+uint8_t(*s);return h;}
bool range(uint32_t a,size_t n){return a>=base && n<=mem.size() && uint64_t(a)+n<=uint64_t(base)+mem.size();}
int read(void*,uint32_t a,void *out,size_t n){if(!range(a,n))return 0;std::memcpy(out,&mem[a-base],n);return 1;}
int write(void*,uint32_t a,const void *in,size_t n){
    bool owned=false;for(auto item:allocations)if(a>=item.first && uint64_t(a)+n<=uint64_t(item.first)+item.second)owned=true;
    CHECK(owned && range(a,n));std::memcpy(&mem[a-base],in,n);return 1;
}
uint32_t xbox_ContiguousAlloc(uint32_t bytes,uint32_t alignment){
    CHECK(alignment==4096 && bytes<1100000);if(refuse_alloc)return 0;
    uint32_t out=next_address;next_address+=(bytes+4095)&~4095u;
    CHECK(range(out,bytes));allocations[out]=bytes;++allocs;return out;
}
int xbox_ContiguousFree(uint32_t address){
    if(refuse_free)return 0;
    CHECK(allocations.erase(address)==1);++frees;return 1;
}
#include "../src/community/xyz_paint_bridge.inc"
int call(void*,XyzXboxOp op,uint32_t self,uint32_t second,unsigned n,const uint32_t *a,uint32_t *out){
    CHECK(!second);*out=0;
    if(op==XYZ_G_TEXTURE_FIND){
        CHECK(!self && n==3 && !a[1] && !a[2]);
        const char *names[]={"DUMMY_SKIN1","DUMMY_WHEEL1","DUMMY_SPINNER1"};
        for(unsigned i=0;i<3;i++)if(!missing && i!=unavailable && a[0]==hash(names[i]))*out=source+i*0x10000;
    } else if(op==XYZ_G_PAINT_ALLOC || op==XYZ_G_PAINT_FREE){
        CHECK(!self);return community_paint_memory(op,n,a,out);
    } else if(op==XYZ_G_TEXTURE_INIT){
        CHECK(!n && allocations.count(self));++inits;uint32_t pl=word(self);
        put(pl+0x10,0x40001);put(pl+0x14,bad_header?0:word(self+0x70)&0x7fffffff);
        put(pl+0x24,0x30001);put(pl+0x28,word(self+0x74)&0x7fffffff);
    } else CHECK(!"unexpected operation");
    return 1;
}
void fixture(){
    std::fill(mem.begin(),mem.end(),0);next_address=base+0x100000;allocs=frees=inits=0;allocations.clear();missing=bad_header=false;unavailable=99;
    refuse_alloc=refuse_free=false;std::memset(community_paint_allocations,0,sizeof community_paint_allocations);
    const char *names[]={"DUMMY_SKIN1","DUMMY_WHEEL1","DUMMY_SPINNER1"};
    for(unsigned i=0;i<3;++i){
        uint32_t ti=source+i*0x10000,pl=ti+0x80,px=ti+0x100,pal=px+4096;
        put(ti,pl);put(ti+0x24,hash(names[i]));put(ti+0x38,4096);put(ti+0x3c,1024);put(ti+0x40,4096);
        put(ti+0x44,64|(64<<16));mem[ti-base+0x4a]=8;mem[ti-base+0x4e]=1;
        put(ti+0x68,base+0x500);put(ti+0x6c,ti);put(ti+0x70,px);put(ti+0x74,pal);put(pl+0xc,11);
        for(unsigned j=0;j<5120;++j)mem[px-base+j]=uint8_t(j+i);
    }
}
}
int main(){
    XyzXboxGuest g{nullptr,read,write,call};fixture();xyz::Skins bank;
    const auto original=mem;
    CHECK(!bank.lookup(hash("DUMMY_SKIN7")) && bank.prepare(g,6)==-1 && bank.prepare(g,71)==-1 && !allocs);
    missing=true;CHECK(bank.prepare(g,7)==0 && !allocs);missing=false;
    unavailable=1;CHECK(bank.prepare(g,7)==0 && allocs==1 && !bank.lookup(hash("DUMMY_SKIN7")));
    unavailable=99;CHECK(bank.prepare(g,7)==1 && allocs==3 && inits==3);
    CHECK(bank.prepare(g,7)==1 && allocs==3);
    CHECK(!bank.lookup(hash("DUMMY_SKIN1")) && !bank.lookup(hash("UNRELATED")));
    uint32_t first=bank.lookup(hash("DUMMY_SKIN7"));CHECK(first && word(first+0x68)==0 && word(first+0x6c)==first);
    CHECK(!std::memcmp(&mem[word(first+0x70)-base],&original[word(source+0x70)-base],4096));
    CHECK(!std::memcmp(&mem[word(first+0x74)-base],&original[word(source+0x74)-base],1024));
    CHECK(!std::strcmp(reinterpret_cast<const char*>(&mem[first-base+0xc]),"DUMMY_SKIN7"));
    CHECK(bank.prepare(g,8)==1 && allocs==6);
    uint32_t second=bank.lookup(hash("DUMMY_SKIN8"));CHECK(second && second!=first && word(second+0x70)!=word(first+0x70));
    put(word(first+0x70),0x12345678);CHECK(word(word(second+0x70))!=0x12345678);
    CHECK(!std::memcmp(mem.data(),original.data(),0x100000)); // sources are byte-identical
    fixture();xyz::Skins failed;bad_header=true;
    CHECK(failed.prepare(g,7)==-1 && frees==1 && !failed.lookup(hash("DUMMY_SKIN7")));
    bad_header=false;CHECK(failed.prepare(g,7)==1 && allocs==4);
    fixture();xyz::Skins invalid;mem[source-base+0x4a]=32;
    CHECK(invalid.prepare(g,7)==-1 && !allocs);
    fixture();xyz::Skins bounded;
    refuse_alloc=true;CHECK(bounded.prepare(g,7)==-1 && !allocs && !bounded.lookup(hash("DUMMY_SKIN7")));refuse_alloc=false;
    for(unsigned skin=7;skin<=70;++skin)CHECK(bounded.prepare(g,skin)==1);
    CHECK(allocs==192 && allocations.size()==192);
    uint32_t bytes=4096,out=123,unknown=source;
    CHECK(community_paint_memory(XYZ_G_PAINT_ALLOC,1,&bytes,&out) && !out && allocs==192);
    CHECK(!community_paint_memory(XYZ_G_PAINT_FREE,1,&unknown,&out) && !frees);
    uint32_t owned=community_paint_allocations[0];refuse_free=true;
    CHECK(community_paint_memory(XYZ_G_PAINT_FREE,1,&owned,&out) && !out && !frees && community_paint_allocations[0]==owned);
    refuse_free=false;CHECK(community_paint_memory(XYZ_G_PAINT_FREE,1,&owned,&out) && out==1 && frees==1);
    CHECK(!community_paint_memory(XYZ_G_PAINT_FREE,1,&owned,&out) && frees==1);
    CHECK(community_paint_memory(XYZ_G_PAINT_ALLOC,1,&bytes,&out) && out && allocs==193);
    std::puts("Private paint: independent pixels/palettes, atomic publication, preserved sources, missing templates and invalid GPU header rejection passed");
}
