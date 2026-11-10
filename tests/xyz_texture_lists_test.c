#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t ram[0x600000/4],esp=0x11000,ecx,edx;
#define MEM32(a) ram[(uint32_t)(a)/4]
#define XBOX_PTR(a) ((uint8_t*)ram+(uint32_t)(a))
static int community_damage_ready,base[2],calls;
static int xyz_xbox_address(uint32_t a,uint32_t n){return a>=0x10000 && (uint64_t)a+n<=sizeof ram;}
static void sub_00210E00_gen(void){
    uint32_t list=MEM32(esp+4),ride=MEM32(esp+8);++calls;
    memset(XBOX_PTR(list),0,0x2fc);
    for(int set=0;set<2;set++){
        uint32_t at=list+set*0x114,count=base[set];CHECK(count<69);
        for(unsigned i=0;i<count;i++)MEM32(at+i*4)=10000+i;
        for(unsigned z=0;z<7;z++)if(MEM32(ride+0x660+z*4))for(unsigned i=0;i<8;i++){
            uint32_t part=MEM32(ride+0x714+z*32+i*4);if(!part)continue;
            unsigned j=0;while(j<count && MEM32(at+j*4)!=part)j++;
            if(j==count){CHECK(count<69);MEM32(at+count++*4)=part;}
        }
        MEM32(list+0x228+set*4)=count;
    }
    MEM32(esp+16)=0xBADBAD;esp+=4;ecx=0xDEAD;edx=0xBEEF;
}
static uint32_t guest_call_full(void(*fn)(void),uint32_t self,uint32_t second,uint32_t ret,unsigned n,const uint32_t *args){
    uint32_t oldEsp=esp,oldEcx=ecx,oldEdx=edx;esp=0x12000;ecx=self;edx=second;MEM32(esp)=ret;
    for(unsigned i=0;i<n;i++)MEM32(esp+4+i*4)=args[i];fn();esp=oldEsp;ecx=oldEcx;edx=oldEdx;return 0;
}
#include "../src/community/xyz_texture_lists.inl"
int main(void){
    const uint32_t ride=0x20000,list=0x24000,types=0x30000;
    MEM32(0x45af88)=types;strcpy((char*)XBOX_PTR(types+0x20),"PEUGEOT");
    for(int engine=0;engine<2;engine++)for(int count=0;count<=56;count++)for(int repeated=0;repeated<2;repeated++){
        memset(XBOX_PTR(ride),0,0x8f0);for(int z=0;z<7;z++)MEM32(ride+0x660+z*4)=1;
        for(int i=0;i<count;i++)MEM32(ride+0x714+i*4)=500+(repeated?i%8:i);
        uint8_t saved[0x8f0];memcpy(saved,XBOX_PTR(ride),sizeof saved);
        base[0]=18;base[1]=25;community_damage_ready=engine;calls=0;
        MEM32(list-4)=MEM32(list+0x2fc)=0xABCDEF12;
        esp=0x11000;MEM32(esp)=0x123456;MEM32(esp+4)=list;MEM32(esp+8)=ride;MEM32(esp+12)=1;MEM32(esp+16)=2;
        community_texture_list();
        CHECK(esp==0x11004 && ecx==0xDEAD && edx==0xBEEF && calls==(count?2:1));
        CHECK(!memcmp(saved,XBOX_PTR(ride),sizeof saved));
        CHECK(MEM32(list-4)==0xABCDEF12 && MEM32(list+0x2fc)==0xABCDEF12);
        int distinct=repeated && count>8?8:count,kept=distinct<44-engine?distinct:44-engine;
        CHECK(MEM32(list+0x228)==18+kept+engine && MEM32(list+0x22c)==25+kept+engine);
    }
    puts("PASS texture lists: 228 bounded decal cases, duplicate names, both lists, engine, registers and complete ride restoration");
    return 0;
}
