#include "pcvr_texture_decode.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned cases,failed;
static uint64_t pixels;
/* Independent original Vulkan address calculation. */
static uint32_t reference_offset(uint32_t u,uint32_t v,uint32_t w,uint32_t h)
{
    uint32_t out=0,bit=0,mw=w-1,mh=h-1;
    for(uint32_t m=1;mw>=m || mh>=m;m<<=1) {
        if(mw>=m) { if(u&m) out|=1u<<bit; ++bit; }
        if(mh>=m) { if(v&m) out|=1u<<bit; ++bit; }
    }
    return out;
}
static void check(uint32_t w,uint32_t h,uint32_t color,uint32_t entries,unsigned align)
{
    size_t count=(size_t)w*h,bytes=count*(color==0x0b?1:4);
    uint8_t *storage=malloc(bytes+8),*src=storage+align,pal[1028];
    uint32_t *out=malloc((count+2)*4);
    if(!storage || !out) exit(2);
    uint32_t seed=0x31508aefu;
    for(size_t i=0;i<bytes;++i) { seed=seed*1664525u+1013904223u; src[i]=(uint8_t)(seed>>24); }
    for(unsigned i=0;i<sizeof pal;++i) pal[i]=(uint8_t)(i*71u+(i>>2));
    out[0]=out[count+1]=0xa5a5c3c3u;
    int ok=pcvr_texture_decode(out+1,src,w,h,color,pal+align,entries);
    unsigned bad=!ok;
    for(uint32_t y=0;y<h;++y) for(uint32_t x=0;x<w;++x) {
        uint32_t at=reference_offset(x,y,w,h),value;
        if(color==0x0b) {
            unsigned i=src[at]; if(i>=entries) i=0;
            memcpy(&value,pal+align+i*4,4);
        } else { memcpy(&value,src+4*(size_t)at,4); if(color==0x07) value|=0xff000000u; }
        if(out[1+(size_t)y*w+x]!=value) bad=1;
    }
    if(out[0]!=0xa5a5c3c3u || out[count+1]!=0xa5a5c3c3u) bad=1;
    if(bad) { ++failed; printf("FAIL %ux%u color=%02X entries=%u align=%u\n",w,h,color,entries,align); }
    ++cases; pixels+=count;
    free(storage); free(out);
}
static void guarded(uint32_t w,uint32_t h,uint32_t color,uint32_t entries)
{
    size_t count=(size_t)w*h,bytes=count*(color==0x0b?1:4);
    SYSTEM_INFO si; GetSystemInfo(&si); size_t page=si.dwPageSize;
    size_t sb=(bytes+page-1)&~(page-1),db=(count*4+page-1)&~(page-1);
    uint8_t *s=VirtualAlloc(NULL,sb+page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    uint8_t *d=VirtualAlloc(NULL,db+page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    uint8_t *p=VirtualAlloc(NULL,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!s || !d || !p) exit(2);
    DWORD old;
    if(!VirtualProtect(s+sb,page,PAGE_NOACCESS,&old) ||
       !VirtualProtect(d+db,page,PAGE_NOACCESS,&old) ||
       !VirtualProtect(p+page,page,PAGE_NOACCESS,&old)) exit(2);
    memset(s+sb-bytes,0xff,bytes); memset(p+page-entries*4,0x5a,entries*4);
    uint32_t *out=(uint32_t *)(d+db-count*4);
    if(!pcvr_texture_decode(out,s+sb-bytes,w,h,color,p+page-entries*4,entries)) ++failed;
    uint32_t expected=color==0x0b?0x5a5a5a5au:UINT32_MAX;
    for(size_t i=0;i<count;++i) if(out[i]!=expected) { ++failed; break; }
    ++cases; pixels+=count;
    VirtualFree(s,0,MEM_RELEASE); VirtualFree(d,0,MEM_RELEASE); VirtualFree(p,0,MEM_RELEASE);
}
int main(void)
{
    for(unsigned lw=0;lw<=10;++lw) for(unsigned lh=0;lh<=10;++lh) {
        unsigned align=(lw+lh)&3;
        check(1u<<lw,1u<<lh,0x06,0,align);
        check(1u<<lw,1u<<lh,0x07,0,align);
        for(unsigned entries=32;entries<=256;entries*=2)
            check(1u<<lw,1u<<lh,0x0b,entries,align);
    }
    for(unsigned lh=0;lh<=12;++lh) {
        check(4096,1u<<lh,0x0b,256,1);
        check(1u<<lh,4096,0x07,0,3);
    }
    for(unsigned n=0;n<=9;++n) {
        guarded(1u<<n,1,0x06,256); guarded(1,1u<<n,0x07,256);
        for(unsigned entries=32;entries<=256;entries*=2) guarded(1u<<n,2,0x0b,entries);
    }
    uint32_t out=0x12345678; uint8_t in[1024]={0};
    const uint32_t invalid[][4]={{0,1,6,0},{1,0,6,0},{3,2,6,0},{2,3,6,0},
        {8192,1,6,0},{1,8192,6,0},{1,1,0x0c,0},{1,1,0x0b,0},{1,1,0x0b,512}};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        const uint32_t *v=invalid[i];
        if(pcvr_texture_decode(&out,in,v[0],v[1],v[2],in,v[3]) || out!=0x12345678) ++failed;
        ++cases;
    }
    if(pcvr_texture_decode(&out,in,1,1,0x0b,NULL,32) || out!=0x12345678) ++failed;
    ++cases;
    printf("PCVR texture decode: %u cases, %llu pixels, %u failures\n",cases,(unsigned long long)pixels,failed);
    return failed?1:0;
}
