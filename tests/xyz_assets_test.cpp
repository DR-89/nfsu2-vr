#include "xyz_assets.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace xyz;
static const char *names[]={"CARS\\BRAKES\\GEOMETRY.BIN","CARS\\WHEELS\\GEOMETRY_ENKEI.BIN"};
static uint8_t arena[XYZ_PACK_CAP*XYZ_PACK_PATH],hiber[24];
static unsigned allocations,loads,wakes,begins,finishes,waits,refs[2];
static bool busy[2],need[2],bad_catalog,pool_ready=true;
constexpr uint32_t base=0x200000,table=0x1d0000;
static unsigned index(uint32_t p){CHECK(p==0x100000 || p==0x101000);return (p-0x100000)/0x1000;}
static int read(void*,uint32_t a,void *b,size_t n) {
    uint32_t value=0;
    if(a==0x3f07ac && n==4)value=1;
    else if(a==0x3f0818 && n==4)value=table;
    else if(a==0x1000b4 && n==4)value=refs[0];
    else if(a==0x1010b4 && n==4)value=refs[1];
    else if(a==table && n==24){std::memcpy(b,hiber,n);return 1;}
    else if(a>=base && uint64_t(a)+n<=base+sizeof arena){std::memcpy(b,arena+a-base,n);return 1;}
    else return 0;
    std::memcpy(b,&value,4);return 1;
}
static int write(void*,uint32_t a,const void *b,size_t n){CHECK(a==base && n==sizeof arena);std::memcpy(arena,b,n);return 1;}
static int call(void*,XyzXboxOp op,uint32_t self,uint32_t second,unsigned n,const uint32_t *a,uint32_t *out) {
    CHECK(!self && !second);*out=0;
    if(op==XYZ_G_POOL_READY){CHECK(!n && !a);*out=pool_ready;return 1;}
    if(op==XYZ_G_ALLOC){CHECK(n==1 && a[0]==sizeof arena);*out=base;++allocations;return 1;}
    CHECK(a[0]>=base && a[0]<base+2*XYZ_PACK_PATH);unsigned i=(a[0]-base)/XYZ_PACK_PATH;
    CHECK(!std::strcmp((char*)arena+a[0]-base,names[i]));
    if(op==XYZ_G_PACK_LOAD){CHECK(n==4 && !a[1] && !a[2] && !a[3]);++refs[i];++loads;*out=1;return 1;}
    if(op==XYZ_G_PACK_WAKE){CHECK(n==1 && !busy[i]);++wakes;need[i]=false;return 1;}
    if(op==XYZ_G_PACK_WAIT){CHECK(n==1);++waits;busy[i]=false;return 1;}
    CHECK(!"unexpected operation");return 0;
}
extern "C" int xyz_xbox_pack_catalog(const XyzXboxGuest*,XyzXboxPackCatalog *c) {
    *c={};if(bad_catalog)return 0;c->count=2;for(unsigned i=0;i<2;i++)std::strcpy(c->path[i],names[i]);return 1;
}
extern "C" int xyz_xbox_pack_find(const XyzXboxGuest*,const char *name,uint32_t *p){for(unsigned i=0;i<2;i++)if(!std::strcmp(name,names[i])){*p=0x100000+0x1000*i;return 1;}*p=0;return 0;}
extern "C" int xyz_xbox_pack_idle(const XyzXboxGuest*,uint32_t p){return !busy[index(p)];}
extern "C" int xyz_xbox_wake_prepare(const XyzXboxGuest*,uint32_t pack,XyzXboxWakePlan *p){*p={};p->pack=pack;p->matches=need[index(pack)];return !busy[index(pack)];}
extern "C" int xyz_xbox_wake_begin(const XyzXboxGuest*,XyzXboxWakePlan *p){CHECK(!p->begun);p->begun=1;++begins;return 1;}
extern "C" int xyz_xbox_wake_finish(const XyzXboxGuest*,XyzXboxWakePlan *p){CHECK(p->begun);p->begun=0;++finishes;return 1;}
static void reset(){allocations=loads=wakes=begins=finishes=waits=0;refs[0]=refs[1]=1;busy[0]=busy[1]=bad_catalog=false;need[0]=need[1]=true;std::memset(hiber,0,sizeof hiber);}
int main() {
    XyzXboxGuest g{};g.read=read;g.write=write;g.call=call;
    reset();Assets assets;CHECK(!assets.started());assets.maintain(g);CHECK(!loads);
    pool_ready=false;CHECK(assets.prepare(g,900000)==0 && !loads && !allocations);
    pool_ready=true;
    CHECK(assets.prepare(g,1000000)==0 && loads==1 && allocations==1);
    CHECK(assets.prepare(g,1050000)==0 && loads==2 && refs[0]==2 && refs[1]==2);
    busy[1]=true;CHECK(assets.prepare(g,1100000)==0 && wakes==1);
    CHECK(assets.prepare(g,1150000)==0 && wakes==1);
    busy[1]=false;
    for(int i=0;i<4;i++)assets.prepare(g,1200000+i*50000);
    CHECK(assets.prepare(g,1500000)==1 && wakes==2 && begins==2 && finishes==2);
    for(int i=0;i<20;i++)CHECK(assets.prepare(g,2000000+i*50000)==1);
    CHECK(loads==2 && allocations==1 && wakes==2); // cached references, no repeated loads
    ++hiber[0];need[0]=true;CHECK(assets.prepare(g,90000000)==0);
    CHECK(assets.prepare(g,90050000)==1 && wakes==3); // later garage visits don't hit the initial timeout
    XyzXboxWakePlan plan;busy[0]=true;need[0]=true;
    CHECK(assets.before_wake(g,base,plan) && waits==1 && !busy[0]);
    uint32_t file=base,ignored;CHECK(call(nullptr,XYZ_G_PACK_WAKE,0,0,1,&file,&ignored));assets.after_wake(g,plan);
    CHECK(begins==4 && finishes==4 && refs[0]==2 && refs[1]==2);
    reset();bad_catalog=true;Assets invalid;CHECK(invalid.prepare(g,1000000)==-1 && !loads && !allocations);
    reset();Assets timeout;timeout.prepare(g,1000000);timeout.prepare(g,1050000);busy[0]=true;
    CHECK(timeout.prepare(g,32000000)==-1);
    // Even a later failed load/timeout keeps stock Wake repairs for held packs.
    need[0]=true;CHECK(timeout.before_wake(g,base,plan) && waits==1);
    CHECK(call(nullptr,XYZ_G_PACK_WAKE,0,0,1,&file,&ignored));timeout.after_wake(g,plan);
    std::puts("Assets: bounded asynchronous header loads, persistent references, stock Wake guard, timeout and later garage revisits passed");
}
