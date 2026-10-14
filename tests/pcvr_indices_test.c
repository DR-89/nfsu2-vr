#include "pcvr_indices.h"
#include <stdio.h>
#include <string.h>
static uint32_t source[65544],reference[65544],actual[65544];
static unsigned cases,failed;
static void check(unsigned alignment,unsigned count,uint32_t base)
{
    uint32_t lo=UINT32_MAX,hi=0,fast_lo,fast_hi;
    const uint32_t *in=source+alignment;
    for(unsigned i=0;i<count;++i) { if(in[i]<lo) lo=in[i]; if(in[i]>hi) hi=in[i]; }
    pcvr_index_range(in,count,&fast_lo,&fast_hi);
    if(lo!=fast_lo || hi!=fast_hi) ++failed;
    size_t bytes=(count+8)*sizeof *actual;
    memset(reference,0xa5,bytes); memset(actual,0xa5,bytes);
    for(unsigned i=0;i<count;++i) reference[3+i]=in[i]-base;
    pcvr_index_rebase(actual+3,in,count,base);
    if(memcmp(reference,actual,bytes)) ++failed;
    ++cases;
}
int main(void)
{
    uint32_t seed=0x2e4163a9u;
    for(unsigned trial=0;trial<12000;++trial) {
        for(unsigned i=0;i<4104;++i) { seed=seed*1664525u+1013904223u; source[i]=seed; }
        unsigned alignment=trial&3,count=trial%4097;
        check(alignment,count,seed);
        if(count) { source[alignment]=0; source[alignment+count-1]=UINT32_MAX; }
        check(alignment,count,0x80000000u);
    }
    for(unsigned pattern=0;pattern<4;++pattern) {
        uint32_t value=pattern==0?0:pattern==1?UINT32_MAX:pattern==2?0x80000000u:0x7fffffffu;
        for(unsigned i=0;i<65544;++i) source[i]=value;
        for(unsigned alignment=0;alignment<4;++alignment) for(unsigned count=0;count<33;++count)
            check(alignment,count,value);
        for(unsigned alignment=0;alignment<4;++alignment) for(unsigned count=65532;count<=65536;++count)
            check(alignment,count,value);
    }
    printf("PCVR index range/rebase: %u cases, %u failures\n",cases,failed);
    return failed?1:0;
}
