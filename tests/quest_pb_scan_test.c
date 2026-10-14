/* Run the real scanner against a synthetic ring/call buffer. Compare each
 * executed method AND the published DMA_GET with inventory on/off. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
extern void nv2a_pb_resync(uint32_t);
extern void nv2a_pb_scan(uint32_t);
extern int nv2a_pb_scan_survey(int);
extern void nv2a_pb_scan_bulk(int);
extern void nv2a_pb_scan_report(void);
static uint8_t *memory;
static uint32_t commands[32][4], ncommands, checksum;
static int recording=1;
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)memory; }
void nv2a_pb_exec_report(void) {}
void nv2a_pb_exec_method(uint32_t channel,uint32_t method,uint32_t parameter) {
    uint32_t get=*(volatile uint32_t *)(memory+0xFD800044u);
    if(recording && ncommands<32) {
        commands[ncommands][0]=channel; commands[ncommands][1]=method;
        commands[ncommands][2]=parameter; commands[ncommands][3]=get;
    }
    ++ncommands; checksum+=method^parameter;
}
uint32_t nv2a_pb_exec_data(uint32_t channel,uint32_t method,int noninc,const uint32_t *data,uint32_t count) {
    /* Scanner boundary stub: accepts the same packet but records every input
     * method so the scalar and packet walks can be compared exactly. */
    for(uint32_t i=0;i<count;++i) nv2a_pb_exec_method(channel,method+(noninc?0:i*4),data[i]);
    return count;
}
static void put(uint32_t address,uint32_t word) { *(uint32_t *)(memory+0x80000000u+address)=word; }
static void scan(void) { nv2a_pb_resync(0x1000); nv2a_pb_scan(0x1024); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
int main(int argc,char **argv) {
    memory=mmap(NULL,1ull<<32,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(memory==MAP_FAILED) { perror("mmap"); return 1; }
    setenv("RECOMP_PB_EXEC","1",1);
    if(argc>1) setenv("RECOMP_PB_SCAN","1",1); else unsetenv("RECOMP_PB_SCAN");
    /* Increasing methods, subroutine with non-increasing inline data,
     * return, both jump encodings, then final increasing method. */
    put(0x1000,(2u<<18)|0x300); put(0x1004,10); put(0x1008,20);
    put(0x100c,0x2002); put(0x1010,0x20003000);
    put(0x2000,0x40000000|(3u<<18)|(2u<<13)|0x1800);
    put(0x2004,30); put(0x2008,40); put(0x200c,50); put(0x2010,0x00020000);
    put(0x3000,0x1019); put(0x1018,(1u<<18)|0x104); put(0x101c,60);
    put(0x1020,0); /* zero-count header */
    nv2a_pb_scan_survey(1); scan();
    const uint32_t expected[6][4]={{0,0x300,10,0x1000},{0,0x304,20,0x1000},
        {2,0x1800,30,0x2000},{2,0x1800,40,0x2000},{2,0x1800,50,0x2000},{0,0x104,60,0x1018}};
    if(ncommands!=6 || memcmp(commands,expected,sizeof expected)) { fprintf(stderr,"Unexpected decode/GET\n"); return 1; }
    ncommands=0;
    int enabled=nv2a_pb_scan_survey(0);
    if(enabled!=(argc>1)) { fprintf(stderr,"Explicit survey override failed\n"); return 1; }
    scan();
    if(ncommands!=6 || memcmp(commands,expected,sizeof expected)) { fprintf(stderr,"Survey changed decode/GET\n"); return 1; }
    nv2a_pb_scan_bulk(1); ncommands=0; scan();
    if(ncommands!=6 || memcmp(commands,expected,sizeof expected)) { fprintf(stderr,"Packet walk changed decode/GET\n"); return 1; }
    nv2a_pb_scan_bulk(0);
    recording=0;
    for(int mode=0;mode<2;++mode) {
        nv2a_pb_scan_survey(mode); ncommands=checksum=0;
        double start=now();
        for(int i=0;i<100000;++i) scan();
        printf("survey %d: %.3f ms, %u methods, checksum %08x\n",mode,(now()-start)*1000,ncommands,checksum);
    }
    nv2a_pb_scan_report();
    puts("Quest pushbuffer inventory toggle: methods/order/parameters/GET/call/return/jumps identical");
    munmap(memory,1ull<<32);
    return 0;
}
