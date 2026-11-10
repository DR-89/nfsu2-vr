#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/vr_community_mods.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t ram[0x480000];static uint32_t ecx,esp;
#define MEM32(a) (*(uint32_t *)(ram+(a)))
#define MEM16(a) (*(uint16_t *)(ram+(a)))
#define MEM8(a) (ram[(a)])
static int xyz_xbox_address(uint32_t p,size_t n){return p>=0x10000 && p<sizeof ram && n<=sizeof ram-p;}
static uint32_t recomp_lookup(uint32_t p){return p==0x100 || p==0x104 || p==0x108 || p==0x166470?p:0;}
static unsigned notices,refreshes,package_refreshes;
static uint32_t guest_call_full(uint32_t fn,uint32_t self,uint32_t second,uint32_t ret,unsigned n,const uint32_t *args)
{
    (void)second;(void)ret;
    if(fn==0x100){CHECK(!n);++refreshes;}
    else if(fn==0x104){CHECK(!n);MEM8(self+0x28)=1;}
    else if(fn==0x108){CHECK(n==1 && args);++notices;}
    else {CHECK(fn==0x166470 && n==2 && args[0]==0x92b703b5 && args[1]==0x345678);++package_refreshes;}
    return 0;
}
#include "../src/community/xyz_quickrace_bridge.inl"
static void setup(unsigned mode,unsigned track)
{
    memset(ram,0,sizeof ram);ecx=0x10000;esp=0x11000;
    MEM32(ecx)=0x340000;MEM32(0x34000c)=0x100;MEM32(0x34001c)=0x104;MEM32(0x340040)=0x108;
    MEM32(0x41c338)=2;MEM32(0x428e34)=3;MEM32(0x406d74)=mode;
    uint32_t t=community_quickrace_track();MEM16(t)=track;MEM16(t+2)=10;
    uint32_t h=0x406d74+(mode==12?0x100:0x10+20*mode);MEM8(h)=1;MEM8(h+1)=3;
    MEM32(esp+4)=0x345678;MEM32(esp+8)=0xb5971bf1;notices=refreshes=package_refreshes=0;
}
static void action(unsigned kind,uint32_t message){esp=0x11000;MEM32(esp+8)=message;CHECK(community_quickrace_action(kind));CHECK(esp==0x1100c);}
int main(void)
{
    setup(0,4014);uint32_t t=community_quickrace_track();
    action(0,0xb5971bf1);CHECK(MEM16(t+2)==11 && notices==1 && refreshes==1);
    MEM16(t+2)=127;action(0,0xb5971bf1);CHECK(MEM16(t+2)==1);
    action(0,0x9120409e);CHECK(MEM16(t+2)==127);
    action(1,0xb5971bf1);CHECK(MEM8(0x406d85)==4);
    action(1,0xb5971bf1);CHECK(MEM8(0x406d85)==5);
    action(1,0xb5971bf1);CHECK(MEM8(0x406d85)==0);
    action(2,0xb5971bf1);CHECK(MEM8(t+8) && MEM16(t+2)==5 && MEM8(0x406d85)==5 && package_refreshes==1);
    action(0,0xb5971bf1);CHECK(MEM16(t+2)==6);
    action(2,0);CHECK(MEM16(t+2)==6); /* refresh must not reset KO choices */
    MEM8(0x406d85)=1;action(1,0x9120409e);CHECK(MEM8(0x406d85)==5);
    community_quickrace_unfreeze(ecx);CHECK(MEM8(ecx+0x28)==1);
    for(unsigned track=4401;track<=4402;++track){setup(2,track);t=community_quickrace_track();
        MEM8(t-7)=track==4402?2:3;action(1,0xb5971bf1);CHECK(MEM8(t-7)==0);
        action(1,0x9120409e);CHECK(MEM8(t-7)==(track==4402?2:3));}
    setup(12,4014);t=community_quickrace_track();action(0,0xb5971bf1);CHECK(t==0x406e90 && MEM16(t+2)==11);
    for(unsigned i=0;i<5;++i){setup(0,4014);
        switch(i){case 0:MEM32(0x41c338)=1;break;case 1:MEM32(0x456290)=5;break;
        case 2:MEM32(0x428e34)=6;break;case 3:MEM32(0x406d74)=13;break;case 4:MEM32(ecx)=0;break;}
        CHECK(!community_quickrace_action(0) && esp==0x11000 && !notices && !refreshes);
    }
    puts("Quick Race: ranges, wrap, Drag grids, KO preservation, menu callbacks and offline guards passed");
    return 0;
}
