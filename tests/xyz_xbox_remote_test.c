#include "xyz_xbox_remote.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
enum { MEM=0x500000,BASE=0x100000,RIDE=BASE+0x100,SCRATCH=BASE+0xa00,
       TYPES=0x110000,OWNERS=0x140000,WRONG=0x142000,RIGHT=0x142100,STOCK=0x142200 };
static uint8_t memory[MEM];
static unsigned writes,calls,lookups;static int fail_call;
static uint32_t hashed_type=2,missing_type=UINT32_MAX;static int missing_all;
static uint32_t rd32(uint32_t a) { uint32_t v;memcpy(&v,memory+a,4);return v; }
static void wr32(uint32_t a,uint32_t v) { memcpy(memory+a,&v,4); }
static int read_memory(void *ctx,uint32_t a,void *out,size_t n) {
    (void)ctx;if(a>MEM || n>MEM-a)return 0;memcpy(out,memory+a,n);return 1;
}
static int write_memory(void *ctx,uint32_t a,const void *data,size_t n) {
    (void)ctx;CHECK(a>=BASE && (uint64_t)a+n<=BASE+0x1200);++writes;memcpy(memory+a,data,n);return 1;
}
static int guest_call(void *ctx,enum XyzXboxOp op,uint32_t self,uint32_t second,unsigned n,const uint32_t *a,uint32_t *out) {
    (void)ctx;CHECK(!second);++calls;*out=0;if(fail_call && calls==(unsigned)fail_call)return 0;
    switch(op) {
    case XYZ_G_TYPE_AVAILABLE:CHECK(!self && n==1 && a[0]<46);*out=!missing_all && a[0]!=missing_type;break;
    case XYZ_G_TYPE_HASH: CHECK(!self && n==1);*out=a[0]==0x87654321?TYPES+hashed_type*0x890:0;break;
    case XYZ_G_RIDE_INIT:
        CHECK(self==RIDE && n==4 && a[0]==2 && !a[1] && a[2]==2 && a[3]==3);
        wr32(RIDE,2);memory[RIDE+5]=2;memory[RIDE+6]=3;memory[RIDE+7]=1;break;
    case XYZ_G_PERF_SET:
        CHECK(self==RIDE+0x2f0 && n==1 && a[0]==SCRATCH);
        CHECK(!rd32(SCRATCH) && !memory[SCRATCH+4]);
        for(unsigned i=0;i<68;i++)memory[RIDE+0x340+i]=(memory[SCRATCH+5+(i>>3)]>>(7-(i&7)))&1;
        break;
    case XYZ_G_RIDE_STOCK:
        CHECK(self==RIDE && n==1 && !a[0]);
        for(unsigned i=0;i<RC_PART_SLOTS;i++)wr32(RIDE+0x590+4*i,STOCK);break;
    case XYZ_G_GET_PART:
        CHECK(self==0x45be28 && n==5 && a[0]==2 && a[4]==0xffffffffu);++lookups;
        if(a[1]==3) {CHECK(a[2]==0x1234abcd);*out=a[3]==0?WRONG:a[3]==WRONG?RIGHT:0;}
        else CHECK(a[1]==169);
        break;
    case XYZ_G_RIDE_PARTS:CHECK(self==RIDE && !n);break;
    case XYZ_G_RIDE_SKINS:CHECK(self==RIDE && n==1 && a[0]==5);wr32(RIDE+0x584,0xabcdef01);break;
    default:CHECK(!"unexpected operation (physics/loader/car must not run here)");
    }
    return 1;
}
static int fixture(uint8_t *wire) {
    memset(memory,0,sizeof memory);writes=calls=lookups=0;fail_call=0;
    hashed_type=2;missing_type=UINT32_MAX;missing_all=0;
    wr32(0x45af88,TYPES);wr32(0x45af74,OWNERS);memcpy(memory+TYPES+2*0x890,"TESTCAR",8);
    wr32(TYPES+2*0x890+0xd0,0x87654321);
    wr32(WRONG,0x1234abcd);memory[WRONG+7]=1;wr32(OWNERS+4,111);
    wr32(RIGHT,0x1234abcd);memory[RIGHT+7]=2;wr32(OWNERS+8,222);
    wr32(STOCK,0x98765432);memory[STOCK+7]=3;wr32(OWNERS+12,333);
    rc_carcfg c;memset(&c,0,sizeof c);c.type_hash=0x87654321;strcpy(c.type_name,"TESTCAR");
    c.ride_b[0]=1;c.ride_b[1]=2;c.ride_b[2]=3;c.ride_b[3]=4;
    c.has_perf=1;c.perf[0]=0x80;c.perf[1]=0x40;c.perf[2]=0x80;c.perf[10]=0x10;
    for(int i=0;i<41;i++)c.perf[11+i]=(uint8_t)(i+17);
    c.n_parts=2;c.part[0].slot=3;c.part[0].key=0x1234abcd;c.part[0].owner=222;
    c.part[1].slot=169;c.part[1].key=0xdeadbeef;c.part[1].owner=444;
    int n=rc_carcfg_encode(&c,wire,RC_CFG_MAX);CHECK(n>0);return n;
}
int main(void) {
    XyzXboxGuest guest={0,read_memory,write_memory,guest_call};XyzXboxArena arena={BASE,0x1200};
    XyzXboxRideResult result;uint8_t wire[RC_CFG_MAX],again[RC_CFG_MAX];int n=fixture(wire);
    CHECK(xyz_xbox_build_ride(&guest,arena,RIDE,SCRATCH,5,wire,n,&result));
    CHECK(result.type==2 && !result.fallback_type && result.missing_parts==1 && lookups==3);
    CHECK(rd32(RIDE+0x590+3*4)==RIGHT && rd32(RIDE+0x590+169*4)==STOCK && !rd32(RIDE+0x590));
    CHECK(memory[RIDE+4]==1 && memory[RIDE+7]==4);
    CHECK(rd32(RIDE+0x318)==1 && rd32(RIDE+0x318+9*4)==1 && !rd32(RIDE+0x31c));
    CHECK(memory[RIDE+0x340]==1 && memory[RIDE+0x340+67]==1 && !memory[RIDE+0x341]);
    CHECK(memory[RIDE+0x398]==17 && memory[RIDE+0x398+40]==57 && rd32(RIDE+0x584)==0xabcdef01);
    int size=xyz_xbox_ride_config(read_memory,0,RIDE,again,sizeof again);rc_carcfg decoded;
    CHECK(size>0 && rc_carcfg_decode(again,size,&decoded)==RC_OK);
    CHECK(decoded.n_parts==2 && decoded.part[0].owner==222 && decoded.part[1].key==0x98765432);
    /* Every malformed record is rejected before even entering a game call. */
    for(int cut=0;cut<n;cut++) {
        fixture(wire);CHECK(!xyz_xbox_build_ride(&guest,arena,RIDE,SCRATCH,5,wire,cut,&result));CHECK(!writes && !calls);
    }
    fixture(wire);CHECK(!xyz_xbox_build_ride(&guest,arena,RIDE,RIDE+8,5,wire,n,&result) && !calls && !writes);
    CHECK(!xyz_xbox_build_ride(&guest,arena,RIDE-0x1000,SCRATCH,5,wire,n,&result) && !calls && !writes);
    CHECK(!xyz_xbox_owned((XyzXboxArena){0xfffffff0,0x1200},0xfffffff0,32));
    CHECK(!xyz_xbox_owned(arena,RIDE,(size_t)-1));
    fixture(wire);fail_call=4;CHECK(!xyz_xbox_build_ride(&guest,arena,RIDE,SCRATCH,5,wire,n,&result));
    /* A wrong owner's same numeric key must never be silently attached. */
    fixture(wire);wr32(OWNERS+8,999);CHECK(xyz_xbox_build_ride(&guest,arena,RIDE,SCRATCH,5,wire,n,&result));
    CHECK(result.missing_parts==2 && rd32(RIDE+0x590+3*4)==STOCK);
    /* Unknown type uses the reviewed stock name-prefix fallback, reported to
     * the caller; it does not apply foreign type-specific parts. */
    fixture(wire);wire[1]^=1;
    CHECK(xyz_xbox_build_ride(&guest,arena,RIDE,SCRATCH,5,wire,n,&result) && result.fallback_type && !lookups);
    /* A real hash for an uninstalled regional car must choose a loadable
     * stock replacement before any loader request or foreign part lookup. */
    fixture(wire);hashed_type=6;missing_type=6;
    memcpy(memory+TYPES+2*0x890,"PEUGOT\0",8);memcpy(memory+TYPES+6*0x890,"PEUGOT106",10);
    CHECK(xyz_xbox_build_ride(&guest,arena,RIDE,SCRATCH,5,wire,n,&result));
    CHECK(result.type==2 && result.fallback_type && !lookups);
    fixture(wire);missing_all=1;
    CHECK(!xyz_xbox_build_ride(&guest,arena,RIDE,SCRATCH,5,wire,n,&result) && !writes);
    puts("Xbox remote ride: bounded writes, validated records, part owners, fallback and guest-call failures passed");
    return 0;
}
