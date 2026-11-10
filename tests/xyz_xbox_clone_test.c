#include "xyz_xbox_remote.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
enum { MEM=0x500000,WORLD=0x110000,PLAYER=0x120000,AI=0x121000,CAR=0x122000,
       PREC=0x130000,AREC=0x131000,BASE=0x140000,BODY=0x150000,
       ENTRY=0x160000,LOADER=0x45c4a0,HEAD=LOADER+0x58,DEBUG=0x456048 };
static uint8_t memory[MEM],original[0x940],debug[528];
static unsigned created,destroyed,freed,unloaded,waits,loads,looks;
static int loaded,collision,world_change_in_wait,fail_create;
static unsigned expected_skin=5,expected_number=103;
static uint32_t r32(uint32_t a){uint32_t v;memcpy(&v,memory+a,4);return v;}
static void w32(uint32_t a,uint32_t v){memcpy(memory+a,&v,4);}
static int read_memory(void *ctx,uint32_t a,void *out,size_t n){
    (void)ctx;if(a>MEM || n>MEM-a)return 0;memcpy(out,memory+a,n);return 1;
}
static int write_memory(void *ctx,uint32_t a,const void *b,size_t n){
    (void)ctx;CHECK(a<MEM && n<=MEM-a);
    CHECK((a>=BASE && (uint64_t)a+n<=BASE+0x3000) ||
          (a>=DEBUG && (uint64_t)a+n<=DEBUG+528) ||
          (created && a>=CAR && (uint64_t)a+n<=CAR+0x9e0) ||
          (a==WORLD+4 && n==4) || (a==AI && n==4) || (a==BODY+0x17 && n==1));
    memcpy(memory+a,b,n);return 1;
}
static int guest_call(void *ctx,enum XyzXboxOp op,uint32_t self,uint32_t second,
                       unsigned n,const uint32_t *a,uint32_t *out){
    (void)ctx;*out=0;
    switch(op){
    case XYZ_G_RIDE_SKINS:
        CHECK((self==BASE+0x10 || self==BASE+0x1000 || self==BASE+0x1900) && !second && n==1 && a[0]==expected_skin);
        w32(self+0x584,collision?123:555);w32(self+0x588,556);w32(self+0x58c,557);break;
    case XYZ_G_LOAD: {
        CHECK(self==LOADER && !second && n==3 && (a[0]==BASE+0x10 || a[0]==BASE+0x1000 || a[0]==BASE+0x1900) && !a[1] && !a[2]);
        CHECK(r32(BASE+4)==4 && memory[BASE+1]==expected_number && !memory[BASE+0x900]);
        uint32_t e=ENTRY+loads*0x2000,prev=r32(HEAD+4);++loads;
        w32(prev,e);w32(HEAD+4,e);w32(e,HEAD);w32(e+4,prev);
        w32(e+0x123c,41+loads);*out=41+loads;break;
    }
    case XYZ_G_SERVICE:CHECK(self==LOADER && !second && n==2 && !a[0] && !a[1]);break;
    case XYZ_G_LOADED:CHECK(self==LOADER && n==1 && a[0]>=ENTRY && a[0]<ENTRY+0x10000);*out=(uint32_t)loaded;break;
    case XYZ_G_WAIT:
        CHECK(self==LOADER && n==1 && a[0]>=42 && a[0]<=41+loads && loaded);++waits;
        if(world_change_in_wait)w32(0x4493e0,0);break;
    case XYZ_G_CREATE:
        CHECK(self==BASE+0x960 && second==BASE && n==2 && !a[0] && !a[1]);
        /* Original 0x219000 -> 0x1BF170 requires this initialized engine
         * curve. A zeroed network ride faults in 0x180270 before allocation. */
        CHECK(r32(BASE+0x20+0x55c)==9 && r32(BASE+0x20+0x56c)==0x240000);
        CHECK(r32(BASE+4)==4);if(fail_create)return 0;
        ++created;w32(CAR,WORLD);w32(CAR+4,AI);w32(AI,CAR);w32(WORLD+4,CAR);
        w32(CAR+0x14,BASE);w32(CAR+0x10,BASE+0x10);w32(CAR+0x3c,BODY);
        memset(memory+DEBUG,0x55,528);*out=0xdeadbeef;break; /* EAX deliberately not the car */
    case XYZ_G_DESTROY:
        CHECK(self==CAR && !second && !n && r32(AI)==WORLD && r32(WORLD+4)==AI);
        ++destroyed;memset(memory+DEBUG,0,528);break;
    case XYZ_G_FREE:CHECK(!self && !second && n==1 && a[0]==CAR && destroyed);++freed;break;
    case XYZ_G_UNLOAD: {
        CHECK(self==LOADER && n==1 && a[0]>=42 && a[0]<=41+loads);
        CHECK(a[0]!=42 || !created || freed || !r32(0x4493e0));
        uint32_t e=ENTRY+(a[0]-42)*0x2000;w32(r32(e+4),r32(e));w32(r32(e)+4,r32(e+4));++unloaded;break;
    }
    case XYZ_G_TYPE_HASH:CHECK(!self && n==1 && a[0]==0x12345678);*out=0x200000;break;
    case XYZ_G_TYPE_AVAILABLE:CHECK(!self && n==1 && a[0]<46);*out=1;break;
    case XYZ_G_RIDE_INIT:CHECK(n==4 && !a[0]);w32(self,0);break;
    case XYZ_G_PERF_SET:CHECK(n==1 && a[0]==BASE+0x2300);break;
    case XYZ_G_RIDE_STOCK:CHECK(n==1 && !a[0]);break;
    case XYZ_G_SET_RIDE:
        CHECK(self==CAR && n==1 && (a[0]==BASE+0x1000 || a[0]==BASE+0x1900));++looks;
        memcpy(memory+BASE+0x10,memory+a[0],0x8f0);memset(memory+DEBUG,0x77,528);memory[BODY+0x17]=0;break;
    case XYZ_G_SECTION:CHECK(self==CAR && !n);break;
    default:CHECK(!"unexpected guest call");
    }
    return 1;
}
static void fixture(void){
    memset(memory,0,sizeof memory);created=destroyed=freed=unloaded=waits=loads=looks=0;
    loaded=collision=world_change_in_wait=fail_create=0;
    expected_skin=5;expected_number=103;
    w32(0x4493e0,WORLD);w32(WORLD,PLAYER);w32(WORLD+4,AI);w32(WORLD+0x10,2);
    w32(PLAYER,AI);w32(PLAYER+4,WORLD);w32(PLAYER+0x14,PREC);w32(PLAYER+0x10,PREC+0x10);
    w32(AI,WORLD);w32(AI+4,PLAYER);w32(AI+0x14,AREC);w32(AI+0x10,AREC+0x10);
    memory[AI+0xa]=100;memory[AREC+1]=100;memory[AREC+0x900]=99;
    w32(PREC+4,1);w32(AREC+4,2);w32(PREC+0x10+0x584,123);
    w32(AREC+0x20+0x55c,9);w32(AREC+0x20+0x56c,0x240000);
    w32(HEAD,HEAD);w32(HEAD+4,HEAD);memset(memory+DEBUG,0xa7,528);
    w32(0x45af88,0x200000);w32(0x45af74,0x230000);
    memcpy(original,memory+AREC,sizeof original);memcpy(debug,memory+DEBUG,sizeof debug);
}
static void intact(void){CHECK(!memcmp(original,memory+AREC,sizeof original));CHECK(!memcmp(debug,memory+DEBUG,sizeof debug));CHECK(r32(WORLD+0x10)==2);}
int main(void){
    XyzXboxGuest g={0,read_memory,write_memory,guest_call};XyzXboxArena arena={BASE,0x3000};XyzXboxClone s={0};
    // Real constructor stores IDs >=128 as a sign-extended 16-bit number.
    // Occupied IDs in either half must never be reused by another clone.
    fixture();
    for(unsigned i=0;i<63;++i){
        uint32_t car=0x300000+i*0x2000,rec=car+0x1000,prev=r32(WORLD+4);
        w32(prev,car);w32(car,WORLD);w32(car+4,prev);w32(WORLD+4,car);
        w32(car+0x14,rec);w32(car+0x10,rec+0x10);w32(rec+4,4);
        unsigned number=103+i;memory[car+0xa]=(uint8_t)number;memory[car+0xb]=number>=128?255:0;
    }
    expected_number=166;expected_skin=70;
    CHECK(xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,70,100) && memory[BASE+1]==166);
    CHECK(xyz_xbox_clone_retire(&g,&s));intact();
    fixture();CHECK(!xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,71,100));
    fixture();CHECK(xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,5,100));intact();
    CHECK(!xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,5,200) && loads==1);
    CHECK(xyz_xbox_clone_poll(&g,&s,200)==XYZ_CLONE_LOADING && !waits && !created);
    loaded=1;CHECK(xyz_xbox_clone_poll(&g,&s,300)==XYZ_CLONE_READY && s.car==CAR && created==1);
    CHECK(memory[BODY+0x17]==1);intact();
    CHECK(xyz_xbox_clone_poll(&g,&s,400)==XYZ_CLONE_READY && created==1);
    CHECK(xyz_xbox_clone_retire(&g,&s) && s.stage==XYZ_CLONE_EMPTY && destroyed==1 && freed==1 && unloaded==1);intact();
    CHECK(xyz_xbox_clone_retire(&g,&s) && destroyed==1);
    /* A busy paint slot is rejected before starting any model load. */
    fixture();collision=1;CHECK(!xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,5,100) && !loads);
    CHECK(xyz_xbox_clone_retire(&g,&s));intact();
    /* Aborted loading never calls Wait/Create; the handle is released. */
    fixture();CHECK(xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,5,100));
    CHECK(xyz_xbox_clone_poll(&g,&s,30000101)==XYZ_CLONE_FAILED && !waits && !created);
    CHECK(xyz_xbox_clone_retire(&g,&s) && unloaded==1);intact();
    fixture();CHECK(xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,5,100));
    loaded=world_change_in_wait=1;CHECK(xyz_xbox_clone_poll(&g,&s,200)==XYZ_CLONE_FAILED && !created);
    CHECK(xyz_xbox_clone_retire(&g,&s));
    /* A destroyed world owns the car destruction; never double-free it. */
    fixture();CHECK(xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,5,100));loaded=1;
    CHECK(xyz_xbox_clone_poll(&g,&s,200)==XYZ_CLONE_READY);w32(0x4493e0,0);
    CHECK(xyz_xbox_clone_retire(&g,&s) && !destroyed && !freed && unloaded==1);
    /* Lost ownership and corrupt world/loader chains fail closed. */
    fixture();CHECK(xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,5,100));loaded=1;
    CHECK(xyz_xbox_clone_poll(&g,&s,200)==XYZ_CLONE_READY);w32(CAR+0x14,AREC);
    CHECK(!xyz_xbox_clone_retire(&g,&s) && !destroyed && !unloaded);w32(CAR+0x14,BASE);
    CHECK(xyz_xbox_clone_retire(&g,&s));
    fixture();CHECK(!xyz_xbox_clone_begin(&g,&s,(XyzXboxArena){AREC+0x800,0x3000},WORLD,PLAYER,5,100) && !loads);
    fixture();w32(AI+4,WORLD);CHECK(!xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,5,100) && !loads);
    fixture();CHECK(xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,5,100));w32(ENTRY,ENTRY);
    CHECK(xyz_xbox_clone_poll(&g,&s,200)==XYZ_CLONE_FAILED && !created);
    CHECK(!xyz_xbox_clone_retire(&g,&s) && !unloaded);w32(ENTRY,HEAD);CHECK(xyz_xbox_clone_retire(&g,&s));
    fixture();CHECK(xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,5,100));loaded=fail_create=1;
    CHECK(xyz_xbox_clone_poll(&g,&s,200)==XYZ_CLONE_FAILED && !created);CHECK(xyz_xbox_clone_retire(&g,&s));intact();
    /* Received equipment loads asynchronously, swaps only on readiness,
     * alternates stable buffers and releases each loader reference once. */
    fixture();CHECK(xyz_xbox_clone_begin(&g,&s,arena,WORLD,PLAYER,5,100));loaded=1;
    CHECK(xyz_xbox_clone_poll(&g,&s,200)==XYZ_CLONE_READY);
    rc_carcfg cfg;memset(&cfg,0,sizeof cfg);cfg.type_hash=0x12345678;strcpy(cfg.type_name,"TEST");
    uint8_t wire[RC_CFG_MAX];int bytes=rc_carcfg_encode(&cfg,wire,sizeof wire);CHECK(bytes>0);
    XyzXboxRideResult result;
    CHECK(!xyz_xbox_clone_configure(&g,&s,wire,bytes-1,300,&result) && loads==1);
    CHECK(xyz_xbox_clone_configure(&g,&s,wire,bytes,300,&result) && loads==2 && looks==0);
    loaded=0;CHECK(xyz_xbox_clone_poll(&g,&s,400)==XYZ_CLONE_LOOK_LOADING && looks==0);
    loaded=1;CHECK(xyz_xbox_clone_poll(&g,&s,500)==XYZ_CLONE_READY && looks==1);
    CHECK(s.look_ride==BASE+0x1000 && memory[BODY+0x17]==1 && unloaded==0);intact();
    CHECK(xyz_xbox_clone_configure(&g,&s,wire,bytes,600,&result));
    CHECK(xyz_xbox_clone_poll(&g,&s,700)==XYZ_CLONE_READY && looks==2 && s.look_ride==BASE+0x1900 && unloaded==1);intact();
    CHECK(xyz_xbox_clone_configure(&g,&s,wire,bytes,800,&result));
    CHECK(xyz_xbox_clone_poll(&g,&s,30000801)==XYZ_CLONE_FAILED && looks==2);
    CHECK(xyz_xbox_clone_retire(&g,&s) && unloaded==4 && freed==1);intact();
    /* Network creation uses the received location but preserves the valid
     * bootstrap physics. It remains hidden until its equipment is ready. */
    fixture();float position[3]={123,456,20};
    position[2]=3000;
    CHECK(!xyz_xbox_clone_begin_at(&g,&s,arena,WORLD,PLAYER,5,position,100) && !loads);
    position[2]=20;
    CHECK(xyz_xbox_clone_begin_at(&g,&s,arena,WORLD,PLAYER,5,position,100));
    CHECK(loads==1 && !memcmp(memory+BASE+0x960,position,sizeof position));
    CHECK(!memcmp(memory+BASE+0x20,memory+AREC+0x20,0x380));
    loaded=1;CHECK(xyz_xbox_clone_poll(&g,&s,200)==XYZ_CLONE_READY && loads==1 && !looks && !s.visible);intact();
    CHECK(xyz_xbox_clone_retire(&g,&s) && unloaded==1 && freed==1);
    puts("Xbox owned cars: async loading, unindexed creation, paint isolation, ownership, timeout and teardown passed");
    return 0;
}
