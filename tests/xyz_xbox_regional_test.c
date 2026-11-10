#include "xyz_xbox_regional.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint8_t ram[0x600000];static uint32_t fail_address;
static int read_test(void *unused,uint32_t a,void *out,size_t n){(void)unused;if((uint64_t)a+n>sizeof ram)return 0;memcpy(out,ram+a,n);return 1;}
static int write_test(void *unused,uint32_t a,const void *in,size_t n){(void)unused;if(a==fail_address || (uint64_t)a+n>sizeof ram)return 0;memcpy(ram+a,in,n);return 1;}
static void word(uint32_t a,uint32_t v){memcpy(ram+a,&v,4);}
static uint32_t get(uint32_t a){uint32_t v;memcpy(&v,ram+a,4);return v;}
int main(void){
    XyzXboxGuest g={0,read_test,write_test,0};CHECK(xyz_xbox_regional_install(&g)==0);
    word(0x45af88,0x100000);word(0x3a1270,7);word(0x3a1274,6);ram[0x3a040c]=5;ram[0x3a040d]=18;
    strcpy((char*)ram+0x100000+6*0x890,"PEUGOT106");strcpy((char*)ram+0x100000+7*0x890,"CORSA");
    word(0x3a1278,5);word(0x3a0410,0x160f131b);
    fail_address=0x3a040e;CHECK(xyz_xbox_regional_install(&g)==-1 && get(0x3a1270)==7 && get(0x3a1274)==6);
    fail_address=0;ram[0x3a040e]=99;CHECK(xyz_xbox_regional_install(&g)==-1 && get(0x3a1270)==7);ram[0x3a040e]=0;
    CHECK(xyz_xbox_regional_install(&g)==1 && get(0x3a1270)==UINT32_MAX && get(0x3a1274)==UINT32_MAX);
    CHECK(ram[0x3a040e]==7 && ram[0x3a040f]==6 && get(0x3a1278)==5 && get(0x3a0410)==0x160f131b);
    CHECK(xyz_xbox_regional_install(&g)==-1);
    CHECK(xyz_xbox_regional_ai_count(1,0x10f8e6,0x3a03f4,26,104)==28);
    CHECK(xyz_xbox_regional_ai_count(0,0x10f8e6,0x3a03f4,26,104)==26);
    CHECK(xyz_xbox_regional_ai_count(1,0x10f8e7,0x3a03f4,26,104)==26);
    CHECK(xyz_xbox_regional_ai_count(1,0x10f8e6,0x3a0410,26,104)==26);
    CHECK(xyz_xbox_regional_ai_count(1,0x10f8e6,0x3a03f4,3,12)==3);
    puts("PASS regional tables: loaded types, exact version, rollback, neighbouring lists and scoped AI allocation");return 0;
}
