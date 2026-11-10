#include "xyz_xbox_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char memory[0x500000];
static void word(unsigned a,unsigned v){memcpy(memory+a,&v,4);}
static int read_memory(void *ctx,uint32_t a,void *out,size_t n){
    (void)ctx;if((uint64_t)a+n>sizeof memory)return 0;memcpy(out,memory+a,n);return 1;
}
static void block(unsigned a,unsigned next,unsigned prev,unsigned size){
    word(a,next);word(a+4,prev);word(a+8,size);word(a+12,0x44443333);
}
int main(void){
    unsigned pool=0x10000,head=pool+4,rides=0x11000,a=0x20000,b=0x30000;
    word(0x3e3fec,pool);word(0x45afbc,rides);word(rides+0x18,12);word(rides+0x2c,22);
    word(pool+0x1c,0x20000);word(pool+0x20,0xc000);
    word(head,a);word(head+4,b);block(a,b,head,0x4000);block(b,head,a,0x10000);
    word(0x45c50c,b+0x1000);word(0x45c510,0x2000);
    XyzXboxPool p;CHECK(xyz_xbox_pool_read(read_memory,0,&p));
    CHECK(p.size==0x20000 && p.used==0xc000 && p.free==0x14000 && p.largest==0x10000 && p.rides==12 && p.capacity==22);
    CHECK(!xyz_xbox_pool_extra_free(read_memory,0,&p)); // coalesced: retail cannot remove a subrange
    p.extra=b+0xf000;CHECK(!xyz_xbox_pool_extra_free(read_memory,0,&p));
    p.extra=b;p.extra_size=0x10000;CHECK(xyz_xbox_pool_extra_free(read_memory,0,&p));
    word(b+4,b);CHECK(!xyz_xbox_pool_read(read_memory,0,&p));word(b+4,a);
    word(b,a);CHECK(!xyz_xbox_pool_read(read_memory,0,&p));word(b,head);
    word(b+8,UINT32_MAX);CHECK(!xyz_xbox_pool_read(read_memory,0,&p));word(b+8,0x10000);
    word(b+12,0);CHECK(!xyz_xbox_pool_read(read_memory,0,&p));word(b+12,0x44443333);
    word(pool+0x20,0);CHECK(!xyz_xbox_pool_read(read_memory,0,&p));word(pool+0x20,0xc000);
    word(rides+0x18,23);CHECK(!xyz_xbox_pool_read(read_memory,0,&p));word(rides+0x18,12);
    word(head+4,a);CHECK(!xyz_xbox_pool_read(read_memory,0,&p));word(head+4,b);
    CHECK(xyz_xbox_pool_read(read_memory,0,&p));
    puts("Car pool: Xbox accounting, fragmentation, retained blocks and corrupt lists passed");return 0;
}
