/* Real decoder with a synthetic producer. PUT may stop between header and
 * payload, or between payload words; unpublished memory is poisoned. */
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void nv2a_pb_resync(uint32_t);
extern void nv2a_pb_scan(uint32_t);
extern int nv2a_pb_scan_survey(int);
extern void nv2a_pb_scan_bulk(int);
static uint8_t *memory;
static uint32_t got[32][3], calls;
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)memory; }
void nv2a_pb_exec_report(void) {}
void nv2a_pb_exec_method(uint32_t c,uint32_t m,uint32_t p) {
    if(calls<32) { got[calls][0]=c; got[calls][1]=m; got[calls][2]=p; }
    ++calls;
}
uint32_t nv2a_pb_exec_data(uint32_t c,uint32_t m,int ni,const uint32_t *p,uint32_t n) {
    for(uint32_t i=0;i<n;++i) nv2a_pb_exec_method(c,m+(ni?0:4*i),p[i]);
    return n;
}
static uint32_t *word(uint32_t at) { return (uint32_t *)(memory+0x80000000ull+at); }
int main(void) {
    memory=VirtualAlloc(NULL,1ull<<32,MEM_RESERVE,PAGE_READWRITE);
    if(!memory || !VirtualAlloc(memory+0x80000000ull,64u<<20,MEM_COMMIT,PAGE_READWRITE) ||
        !VirtualAlloc(memory+0xFD800000ull,65536,MEM_COMMIT,PAGE_READWRITE)) return 2;
    _putenv_s("RECOMP_PB_EXEC","1");
    const uint32_t stream[]={ (3u<<18)|0x300,10,20,30, (1u<<18)|0x17fc,0 };
    const uint32_t expected[][3]={{0,0x300,10},{0,0x304,20},{0,0x308,30},{0,0x17fc,0}};
    unsigned failed=0, cases=0;
    for(int bulk=0;bulk<2;++bulk) for(unsigned split=1;split<=6;++split) {
        memset(word(0x1000),0xee,256);
        calls=0; memset(got,0,sizeof got);
        nv2a_pb_scan_survey(0); nv2a_pb_scan_bulk(bulk); nv2a_pb_resync(0x1000);
        memcpy(word(0x1000),stream,split*4);
        nv2a_pb_scan(0x1000+split*4);
        unsigned expected_partial=split<=4?split-1:split==5?3:4;
        int bad=calls!=expected_partial || memcmp(got,expected,expected_partial*sizeof got[0]);
        memcpy(word(0x1000+split*4),stream+split,(6-split)*4);
        nv2a_pb_scan(0x1018);
        bad|=calls!=4 || memcmp(got,expected,sizeof expected)!=0;
        if(bad) { ++failed; fprintf(stderr,"FAIL bulk=%d split=%u calls=%u\n",bulk,split,calls); }
        ++cases;
    }
    /* Suspend inside a CALL, keep non-incrementing methods on subchannel 2,
     * then RETURN and follow both jump encodings back to the ring. */
    for(int bulk=0;bulk<2;++bulk) for(unsigned split=1;split<=4;++split) {
        const uint32_t payload[]={(3u<<18)|(2u<<13)|0x40001800u,30,40,50,0x20000};
        const uint32_t expected_call[][3]={{2,0x1800,30},{2,0x1800,40},{2,0x1800,50},{0,0x104,60}};
        *word(0x1000)=0x2002; *word(0x1004)=0x20003000;
        *word(0x3000)=0x100d; *word(0x100c)=(1u<<18)|0x104; *word(0x1010)=60;
        memset(word(0x2000),0xee,64); memcpy(word(0x2000),payload,split*4);
        calls=0; memset(got,0,sizeof got); nv2a_pb_scan_bulk(bulk); nv2a_pb_resync(0x1000);
        nv2a_pb_scan(0x2000+split*4);
        int bad=calls!=split-1 || memcmp(got,expected_call,(split-1)*sizeof got[0]);
        memcpy(word(0x2000+split*4),payload+split,(5-split)*4);
        nv2a_pb_scan(0x1014);
        bad|=calls!=4 || memcmp(got,expected_call,sizeof expected_call)!=0;
        if(bad) { ++failed; fprintf(stderr,"FAIL call bulk=%d split=%u calls=%u\n",bulk,split,calls); }
        ++cases;
    }
    /* A driver reset discards a partially submitted packet. */
    calls=0; *word(0x1000)=(3u<<18)|0x300;
    nv2a_pb_resync(0x1000); nv2a_pb_scan(0x1004);
    *word(0x3000)=(1u<<18)|0x104; *word(0x3004)=99;
    nv2a_pb_resync(0x3000); nv2a_pb_scan(0x3008);
    if(calls!=1 || got[0][1]!=0x104 || got[0][2]!=99) ++failed;
    ++cases;
    VirtualFree(memory,0,MEM_RELEASE);
    printf("PCVR pushbuffer boundaries: %u cases, %u failures\n",cases,failed);
    return failed?1:0;
}
