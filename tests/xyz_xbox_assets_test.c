#include "xyz_xbox_assets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
enum {MEM=0x500000,HEAD=0x3ea7ac,PACK=0x100000,SOLIDPACK=0x101000,TABLE=0x102000,
      SOLIDS=0x103000,HTABLE=0x104000,HSOLIDS=0x105000,HIBER=0x3f07a0,
      PDATA=0x110000,HDATA=0x120000,STRINGS=0x130000,BRANDS=0x140000};
static uint8_t memory[MEM];static unsigned writes,frees,calls;static uint32_t drawing;
static uint32_t u(uint32_t a){uint32_t v;memcpy(&v,memory+a,4);return v;}
static void w(uint32_t a,uint32_t v){memcpy(memory+a,&v,4);}
static unsigned refs(uint32_t a){return memory[a+18]|(unsigned)memory[a+19]<<8;}
static void ref(uint32_t a,unsigned n){memory[a+18]=(uint8_t)n;memory[a+19]=(uint8_t)(n>>8);}
static int read_bytes(void *ctx,uint32_t a,void *b,size_t n){(void)ctx;if(a>MEM || n>MEM-a)return 0;memcpy(b,memory+a,n);return 1;}
static int write_bytes(void *ctx,uint32_t a,const void *b,size_t n){
    (void)ctx;CHECK((a>=TABLE && (uint64_t)a+n<=TABLE+3*24) || (a>=HTABLE && (uint64_t)a+n<=HTABLE+3*24) ||
        (a>=SOLIDS && (uint64_t)a+n<=SOLIDS+3*8));
    ++writes;memcpy(memory+a,b,n);return 1;
}
static int call(void *ctx,enum XyzXboxOp op,uint32_t self,uint32_t second,unsigned n,const uint32_t *a,uint32_t *out) {
    (void)ctx;++calls;*out=0;CHECK(!second);
    if(op==XYZ_G_MODEL_FREE) {
        CHECK(self==0x3ea794 && n==2 && a[0]==PACK && a[1]==TABLE && u(TABLE+20)==PDATA);
        // Native ModelFree tells drawing objects to use the other known solid.
        CHECK(drawing==PDATA+32);drawing=HDATA+32;w(TABLE+20,0);++frees;return 1;
    }
    if(op==XYZ_G_PART_NEXT) {
        CHECK(self==0x45be28 && n==5 && a[0]==0xa6e1b733 && a[1]==0x43 && !a[2] && a[4]==0xffffffff);
        *out=!a[3]?BRANDS:a[3]==BRANDS?BRANDS+14:0;return 1;
    }
    if(op==XYZ_G_PART_NAME){CHECK(n==0 && (self==BRANDS || self==BRANDS+14));*out=BRANDS+128+(self-BRANDS)*8;return 1;}
    CHECK(!"unexpected call");return 0;
}
static void fixture(void) {
    memset(memory,0,sizeof memory);writes=frees=calls=0;drawing=PDATA+32;
    w(HEAD,PACK);w(HEAD+4,PACK);w(PACK,HEAD);w(PACK+4,HEAD);strcpy((char*)memory+PACK+8,"CARS\\BRAKES\\GEOMETRY.BIN");
    w(PACK+0x90,1);w(PACK+0xa0,TABLE);w(PACK+0xa4,3);w(PACK+0xb8,SOLIDPACK);w(SOLIDPACK+0x74,SOLIDS);
    w(HIBER+12,3);w(HIBER+0x78,HTABLE);w(HIBER+0x74,HSOLIDS);
    for(unsigned i=0;i<3;i++) {
        uint32_t e=TABLE+24*i,h=HTABLE+24*i,pd=PDATA+0x1000*i,hd=HDATA+0x1000*i,name=100*(i+1);
        w(e,name);w(e+8,128);w(e+20,pd);ref(e,3+i);
        w(h,name);w(h+8,128);w(h+20,hd);ref(h,5+i);
        w(SOLIDS+8*i,name);w(SOLIDS+8*i+4,pd+32);w(HSOLIDS+8*i,name);w(HSOLIDS+8*i+4,hd+32);
    }
    // Model 0 has two loaded copies. Model 1 has only the pack's copy.
    // Model 2 is busy: it must survive this Wake in HIBERNATING unchanged.
    w(HTABLE+24+20,0);w(HSOLIDS+8+4,0);memory[TABLE+48+17]=0x10;
    w(0x45be28,0x45be28);w(0x45be2c,0x45be28);
    for(unsigned i=0;i<11;i++) {
        uint32_t s=STRINGS+96*i;w(0x42a2ac+4*i,s);sprintf((char*)memory+s,"CARS\\PART_%u\\GEOMETRY.BIN",i);
    }
    strcpy((char*)memory+STRINGS+96*10,"CARS\\AUDIO\\GEOMETRY.bin");
    strcpy((char*)memory+BRANDS+128,"ENKEI");strcpy((char*)memory+BRANDS+128+14*8,"5ZIGEN");
}
// Original Xbox Wake's table-copy and compaction behavior, deliberately
// overwriting the destination reference count and model pointer.
static void original_wake(void) {
    unsigned kept=0,n=u(HIBER+12);
    for(unsigned i=0;i<n;i++) {
        uint32_t h=HTABLE+24*i,name=u(h);int match=-1;
        for(unsigned j=0;j<3;j++)if(u(TABLE+24*j)==name)match=(int)j;
        if(match>=0) {
            memcpy(memory+TABLE+24*match,memory+h,24);
            memcpy(memory+SOLIDS+8*match,memory+HSOLIDS+8*i,8);
        } else {
            memmove(memory+HTABLE+24*kept,memory+h,24);
            memmove(memory+HSOLIDS+8*kept,memory+HSOLIDS+8*i,8);++kept;
        }
    }
    w(HIBER+12,kept);
}
int main(void) {
    XyzXboxGuest g={0,read_bytes,write_bytes,call};XyzXboxWakePlan plan;XyzXboxPackCatalog catalog;
    fixture();CHECK(xyz_xbox_pack_catalog(&g,&catalog) && catalog.count==13 && !writes);
    CHECK(!strcmp(catalog.path[10],"CARS\\AUDIO\\GEOMETRY.bin"));
    CHECK(!strcmp(catalog.path[11],"CARS\\WHEELS\\GEOMETRY_ENKEI.BIN"));
    strcpy((char*)memory+BRANDS+128,"..\\EVIL");CHECK(!xyz_xbox_pack_catalog(&g,&catalog) && !writes);
    fixture();uint32_t pack=0;CHECK(xyz_xbox_pack_find(&g,"CARS\\BRAKES\\GEOMETRY.BIN",&pack)==1 && pack==PACK);
    CHECK(xyz_xbox_pack_find(&g,"missing",&pack)==0 && !pack);
    CHECK(xyz_xbox_wake_prepare(&g,PACK,&plan)==1 && plan.count==3 && !writes && !calls);
    CHECK(plan.fix[0].kind==XYZ_WAKE_FREE && plan.fix[1].kind==XYZ_WAKE_KEEP && plan.fix[2].kind==XYZ_WAKE_HIDE);
    CHECK(xyz_xbox_wake_begin(&g,&plan) && frees==1 && drawing==HDATA+32);
    CHECK(!xyz_xbox_wake_begin(&g,&plan));original_wake();
    CHECK(refs(TABLE)==5 && u(TABLE+24+20)==0 && u(HIBER+12)==1 && u(HTABLE)!=300);
    CHECK(xyz_xbox_wake_finish(&g,&plan));
    CHECK(refs(TABLE)==8 && u(TABLE+20)==HDATA && u(SOLIDS+4)==HDATA+32);
    CHECK(refs(TABLE+24)==4 && u(TABLE+24+20)==PDATA+0x1000 && u(SOLIDS+12)==PDATA+0x1000+32);
    CHECK(u(HTABLE)==300 && refs(HTABLE)==7 && u(HTABLE+20)==HDATA+0x2000 && u(HSOLIDS)==300);
    CHECK(!xyz_xbox_wake_finish(&g,&plan));
    // Busy/no-header/corrupt lists and invalid table data perform no edits.
    fixture();memory[PACK+0x8a]=1;CHECK(xyz_xbox_wake_prepare(&g,PACK,&plan)==0 && !writes && !calls);
    fixture();w(PACK+4,PACK);CHECK(xyz_xbox_wake_prepare(&g,PACK,&plan)==-1 && !writes && !calls);
    fixture();w(HIBER+12,257);CHECK(xyz_xbox_wake_prepare(&g,PACK,&plan)==-1 && !writes && !calls);
    fixture();w(HTABLE+24,100);CHECK(xyz_xbox_wake_prepare(&g,PACK,&plan)==-1 && !writes && !calls);
    fixture();w(TABLE+20,0x83fffff0);CHECK(xyz_xbox_wake_prepare(&g,PACK,&plan)==-1 && !writes && !calls);
    fixture();w(PACK+0xa4,100001);CHECK(xyz_xbox_wake_prepare(&g,PACK,&plan)==-1 && !writes && !calls);
    fixture();CHECK(xyz_xbox_wake_prepare(&g,PACK,&plan)==1);ref(TABLE,4);
    CHECK(!xyz_xbox_wake_begin(&g,&plan) && !writes && !calls);
    // Overflow/unknown solids are protected, never freed or silently wrapped.
    fixture();ref(TABLE,65534);CHECK(xyz_xbox_wake_prepare(&g,PACK,&plan)==1 && plan.fix[0].kind==XYZ_WAKE_HIDE && !writes && !calls);
    fixture();w(SOLIDS+4,PDATA+4096);CHECK(xyz_xbox_wake_prepare(&g,PACK,&plan)==1 && plan.fix[0].kind==XYZ_WAKE_HIDE && !writes && !calls);
    puts("Xbox assets: bounded catalogue, duplicate model transfer, exact reference preservation, busy protection and invalid-data rejection passed");
}
