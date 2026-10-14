#include "pcvr_bulk_shadow.h"
#include <stdio.h>
static uint32_t original[2048],reference[2048],actual[2048],data[2048];
static uint8_t dirty[128],reference_dirty[128],actual_dirty[128];
static unsigned cases,failed;
static void check(uint32_t first,int noninc,uint32_t count)
{
    memcpy(reference,original,sizeof original); memcpy(actual,original,sizeof original);
    memcpy(reference_dirty,dirty,sizeof dirty); memcpy(actual_dirty,dirty,sizeof dirty);
    for(uint32_t i=0;i<count;++i) {
        uint32_t slot=first+(noninc?0:i);
        if(reference[slot]!=data[i]) { reference[slot]=data[i]; reference_dirty[slot>>4]=1; }
    }
    pcvr_bulk_shadow(actual,actual_dirty,first,noninc,data,count);
    ++cases;
    if(memcmp(reference,actual,sizeof actual) || memcmp(reference_dirty,actual_dirty,sizeof actual_dirty)) ++failed;
}
int main(void)
{
    /* Every offset across multiple dirty blocks, unchanged and changing. */
    for(unsigned offset=0;offset<32;++offset) for(unsigned count=0;count<=32;++count) {
        memset(data,0,sizeof data); check(offset,0,count);
        for(unsigned i=0;i<count;++i) data[i]=i+1;
        check(offset,0,count);
        memset(dirty,1,sizeof dirty); check(offset,0,count); memset(dirty,0,sizeof dirty);
    }
    /* Repeated register writes, including restoration to the original and
     * the largest supported packet. Preserve unrelated words/dirty blocks. */
    for(unsigned count=2;count<=2047;++count) {
        memset(data,0,count*sizeof *data); data[count/2]=0xdeadbeefu;
        check(2047,1,count); data[count-1]=0; check(2047,1,count);
    }
    uint32_t seed=0x31415926u;
    for(unsigned trial=0;trial<20000;++trial) {
        for(unsigned i=0;i<2048;++i) { seed=seed*1664525u+1013904223u; original[i]=seed; data[i]=seed^(trial&1); }
        for(unsigned i=0;i<128;++i) dirty[i]=(uint8_t)((seed+i)%3==0?0:(seed+i)%3==1?1:0x80);
        unsigned count=trial%32+1, first=(seed>>8)%(2049-count);
        check(first,0,count);
        unsigned repeated=trial%2047+1;
        for(unsigned i=0;i<repeated;++i) data[i]=original[first];
        if(trial&1) data[repeated/2]^=0x80000000u;
        check(first,1,repeated);
    }
    printf("PCVR bulk shadow: %u cases, %u failures\n",cases,failed);
    return failed?1:0;
}
