#include "pcvr_surface_lookup.h"
#include <stdio.h>

typedef struct { uint32_t address; int eye, live; } Surface;
static Surface surfaces[64];
static PcvrSurfaceLookup cache;
static unsigned queries, failures, hits, misses;
static int reference(uint32_t address,int eye)
{
    for(int i=0;i<64;++i)
        if(surfaces[i].live && surfaces[i].address==address &&
           (surfaces[i].eye==eye || surfaces[i].eye==-1)) return i;
    return -1;
}
static void query(uint32_t address,int eye)
{
    int slot;
    if(pcvr_surface_lookup_get(&cache,address,eye,&slot)) ++hits;
    else { ++misses; slot=reference(address,eye); pcvr_surface_lookup_put(&cache,address,eye,slot); }
    ++queries;
    if(slot!=reference(address,eye)) ++failures;
}
static void change(unsigned slot,uint32_t address,int eye,int live)
{
    pcvr_surface_lookup_reset(&cache);
    surfaces[slot]=(Surface){address,eye,live};
}
int main(void)
{
    /* A cached miss must not hide a newly created surface. Shared and
     * eye-specific entries retain the reference's first-slot precedence. */
    query(0,0); query(0,0); change(7,0,1,1); query(0,0); query(0,1);
    change(6,0,-1,1); query(0,0); query(0,1);
    change(5,0,0,1); query(0,0); query(0,1);
    change(5,0,0,0); query(0,0); query(0,1);
    change(6,0,-1,0); query(0,0); query(0,1);
    change(7,0,1,0); query(0,1);
    uint32_t seed=0x7312f945u;
    for(unsigned step=0;step<20000;++step) {
        seed=seed*1664525u+1013904223u;
        unsigned slot=(seed>>16)&63;
        uint32_t address=((seed>>8)&255)*0x800u; /* deliberate hash collisions */
        int eye=(int)((seed>>26)%3)-1;
        change(slot,address,eye,(seed>>3)&1);
        for(int n=0;n<8;++n) {
            query(address,n&1); query(surfaces[(slot+n)&63].address,n&1);
            query(0x80000000u+address,n&1);
        }
        /* Replacing image/view handles without changing lookup identity
         * needs no invalidation; replay precisely the same lookup. */
        query(address,0); query(address,0);
    }
    printf("PCVR surface lookups: %u queries, %u hits, %u misses, %u failures\n",
        queries,hits,misses,failures);
    return failures || !hits || !misses;
}
