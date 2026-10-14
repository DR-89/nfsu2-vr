#include "pcvr_index_packets.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
static uint32_t input[2056],reference[4104],actual[4104];
static unsigned cases,failed;
static void check(unsigned in_alignment,unsigned out_alignment,unsigned words)
{
    memset(reference,0xa5,sizeof reference); memset(actual,0xa5,sizeof actual);
    for(unsigned i=0;i<words;++i) {
        reference[out_alignment+2*i]=(uint16_t)input[in_alignment+i];
        reference[out_alignment+2*i+1]=(uint16_t)(input[in_alignment+i]>>16);
    }
    pcvr_unpack_indices16(actual+out_alignment,input+in_alignment,words);
    if(memcmp(actual,reference,sizeof actual)) ++failed;
    ++cases;
}
int main(void)
{
    uint32_t seed=0x817253a9u;
    for(unsigned pattern=0;pattern<5;++pattern) {
        for(unsigned i=0;i<2056;++i) {
            seed=seed*1664525u+1013904223u;
            input[i]=pattern==0?seed:pattern==1?0:pattern==2?UINT32_MAX:pattern==3?0x8000ffffu:0xffff0001u;
        }
        for(unsigned in_alignment=0;in_alignment<4;++in_alignment)
            for(unsigned out_alignment=0;out_alignment<4;++out_alignment)
                for(unsigned words=0;words<=2047;++words) check(in_alignment,out_alignment,words);
    }
    /* Guard pages catch reads or writes beyond the published packet, for
     * each SIMD tail including an empty packet at the page boundary. */
    SYSTEM_INFO info; GetSystemInfo(&info);
    uint8_t *src=VirtualAlloc(NULL,info.dwPageSize*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    uint8_t *dst=VirtualAlloc(NULL,info.dwPageSize*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    DWORD old;
    if(!src || !dst || !VirtualProtect(src+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&old) ||
        !VirtualProtect(dst+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&old)) return 2;
    for(unsigned words=0;words<64;++words) {
        uint32_t *in=(uint32_t *)(src+info.dwPageSize)-words;
        uint32_t *out=(uint32_t *)(dst+info.dwPageSize)-words*2;
        for(unsigned i=0;i<words;++i) in[i]=i*0x1234567u;
        pcvr_unpack_indices16(out,in,words);
        for(unsigned i=0;i<words;++i)
            if(out[2*i]!=(uint16_t)in[i] || out[2*i+1]!=(uint16_t)(in[i]>>16)) ++failed;
        ++cases;
    }
    VirtualFree(src,0,MEM_RELEASE); VirtualFree(dst,0,MEM_RELEASE);
    printf("PCVR packed index expansion: %u cases, %u failures\n",cases,failed);
    return failed?1:0;
}
