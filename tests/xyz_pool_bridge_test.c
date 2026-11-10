#include "xyz_xbox_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t low[0x500000],physical[64u<<20];
static unsigned allocations,purges;
static int unused_available,allocation_fails;
enum {POOL=0x10000,RIDES=0x11000,GENERAL=0x12000,GENERAL_FREE=0x20000,FREE=0x100000,HEAD=POOL+4};
static void *pointer(uint32_t a,size_t n){
    if((uint64_t)a+n<=sizeof low)return low+a;
    if(a>=0x84000000u && (uint64_t)a+n<=0x84000000ull+sizeof physical)return physical+a-0x84000000u;
    return NULL;
}
#define MEM32(a) (*(uint32_t *)pointer((a),4))
static int cockpit_ui_read(void *ctx,uint32_t a,void *out,size_t n){
    (void)ctx;void *p=pointer(a,n);if(!p)return 0;memcpy(out,p,n);return 1;
}
static volatile unsigned marker;
static void sub_0004F340(void){marker=1;}
static void sub_002194B0_gen(void){marker=2;}
static void sub_00219310(void){marker=3;}
static uint32_t guest_call_full(void (*fn)(void),uint32_t self,uint32_t second,uint32_t ret,
                                unsigned n,const uint32_t *args){
    (void)second;(void)ret;
    if(fn==sub_002194B0_gen){CHECK(self==0x45c4a0 && n==1 && !args[0]);++purges;
        if(unused_available){--unused_available;--MEM32(RIDES+0x18);return 1;}return 0;}
    CHECK(fn==sub_0004F340 && n==3 && (args[0]==0 || args[0]==4));
    uint32_t pool=args[0]==4?POOL:GENERAL,head=pool+4;
    uint32_t at=args[1],size=args[2],tail=MEM32(head+4);
    MEM32(pool+0x1c)+=size;
    if(tail!=head && tail+MEM32(tail+8)==at)MEM32(tail+8)+=size;
    else {MEM32(at)=head;MEM32(at+4)=tail;MEM32(at+8)=size;MEM32(at+12)=0x44443333;
        MEM32(tail)=at;MEM32(head+4)=at;}
    return 0;
}
uint32_t xbox_ContiguousAlloc(uint32_t size,uint32_t align){
    CHECK(size==(16u<<20) && align==4096 && allocations<4);
    if(allocation_fails)return 0;
    return 0x84000000u+(allocations++<<24);
}
#include "../src/community/xyz_pool_bridge.inc"
static void fixture(void){
    memset(low,0,sizeof low);memset(physical,0,sizeof physical);
    allocations=purges=community_pool_blocks=0;community_pool_pointer=0;
    community_heap_blocks=community_heap_pointer=0;
    unused_available=allocation_fails=0;
    MEM32(0x3e3fec)=POOL;MEM32(0x45afbc)=RIDES;MEM32(RIDES+0x18)=10;MEM32(RIDES+0x2c)=64;
    MEM32(POOL+0x1c)=1u<<20;MEM32(HEAD)=MEM32(HEAD+4)=FREE;
    MEM32(FREE)=MEM32(FREE+4)=HEAD;MEM32(FREE+8)=1u<<20;MEM32(FREE+12)=0x44443333;
    MEM32(0x3e3fdc)=GENERAL;MEM32(GENERAL+0x1c)=4u<<20;
    MEM32(GENERAL+4)=MEM32(GENERAL+8)=GENERAL_FREE;
    MEM32(GENERAL_FREE)=MEM32(GENERAL_FREE+4)=GENERAL+4;MEM32(GENERAL_FREE+8)=4u<<20;MEM32(GENERAL_FREE+12)=0x44443333;
    MEM32(0x45c4f8)=MEM32(0x45c4fc)=0x45c4f8;
}
int main(void){
#ifdef _WIN32
    _putenv_s("NFSU2_XYZ_POOL_MB","128");
#else
    setenv("NFSU2_XYZ_POOL_MB","128",1);
#endif
    CHECK(xyz_xbox_set_physical_size(128u<<20));fixture();
    CHECK(community_pool_prepare() && allocations==1 && community_pool_blocks==1);
    CHECK(community_pool_prepare() && allocations==1);
    unused_available=1;CHECK(community_pool_purge(0x2195bc)==1 && allocations==1 && purges==1);
    CHECK(community_pool_purge(0x21a1ec)==0 && allocations==1); // ride-slot pressure cannot consume byte blocks
    CHECK(community_pool_purge(0x2195bc)==1 && allocations==2);
    CHECK(community_pool_purge(0x2195bc)==1 && allocations==3);
    CHECK(community_pool_purge(0x2195bc)==0 && allocations==3); // never destructive fallback at the cap
    MEM32(RIDES+0x18)=63;unused_available=4;
    CHECK(community_pool_prepare() && MEM32(RIDES+0x18)==61 && allocations==3);
    MEM32(0x45c50c)=FREE;MEM32(0x45c510)=1u<<20;
    CHECK(community_pool_release()); // retail can remove this exact free block
    MEM32(0x45c50c)=0x85000000u;MEM32(0x45c510)=16u<<20;
    CHECK(!community_pool_release()); // coalesced: retail silently refuses this subrange
    MEM32(0x45c4f8)=0;CHECK(!community_pool_release()); // corrupt loader list is never handed to retail cleanup
    fixture();allocation_fails=1;CHECK(!community_pool_prepare() && !community_pool_blocks);
    fixture();MEM32(FREE+4)=FREE;CHECK(!community_pool_prepare() && !allocations);
    fixture();CHECK(community_heap_prepare() && !allocations);
    for(unsigned i=0;i<4;++i){
        MEM32(GENERAL+4)=MEM32(GENERAL+8)=GENERAL_FREE;
        MEM32(GENERAL_FREE)=MEM32(GENERAL_FREE+4)=GENERAL+4;MEM32(GENERAL_FREE+8)=65536;
        MEM32(GENERAL+0x20)=MEM32(GENERAL+0x1c)-65536;
        CHECK(community_heap_prepare() && allocations==i+1 && community_heap_blocks==i+1);
    }
    MEM32(GENERAL+4)=MEM32(GENERAL+8)=GENERAL_FREE;
    MEM32(GENERAL_FREE)=MEM32(GENERAL_FREE+4)=GENERAL+4;MEM32(GENERAL_FREE+8)=65536;
    MEM32(GENERAL+0x20)=MEM32(GENERAL+0x1c)-65536;
    CHECK(!community_heap_prepare() && allocations==4);
    fixture();MEM32(GENERAL_FREE+4)=0;CHECK(!community_pool_prepare() && !allocations);
    puts("Car/general pools: checked bounded growth, unused-only reclamation, exhaustion and safe release passed");
    return 0;
}
