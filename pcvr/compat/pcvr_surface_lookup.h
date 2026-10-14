#pragma once
#include <stdint.h>
#include <string.h>

/* Memoize only the result of a surface-address lookup, including misses.
 * Slots refer to the caller's stable surface table, never Vulkan handles.
 * Reset before changing any slot's address, eye or image validity. */
#define PCVR_SURFACE_LOOKUP_SIZE 128
typedef struct {
    uint32_t address;
    int eye, slot, valid;
} PcvrSurfaceLookupEntry;
typedef struct { PcvrSurfaceLookupEntry entries[PCVR_SURFACE_LOOKUP_SIZE]; } PcvrSurfaceLookup;

static inline uint32_t pcvr_surface_lookup_bucket(uint32_t address,int eye)
{
    uint32_t h=(address>>4)^(address>>16)^((uint32_t)eye*0x9e3779b9u);
    return h&(PCVR_SURFACE_LOOKUP_SIZE-1);
}
static inline void pcvr_surface_lookup_reset(PcvrSurfaceLookup *cache)
{
    memset(cache,0,sizeof *cache);
}
static inline int pcvr_surface_lookup_get(const PcvrSurfaceLookup *cache,uint32_t address,int eye,int *slot)
{
    const PcvrSurfaceLookupEntry *e=&cache->entries[pcvr_surface_lookup_bucket(address,eye)];
    if(!e->valid || e->address!=address || e->eye!=eye) return 0;
    *slot=e->slot;
    return 1;
}
static inline void pcvr_surface_lookup_put(PcvrSurfaceLookup *cache,uint32_t address,int eye,int slot)
{
    PcvrSurfaceLookupEntry *e=&cache->entries[pcvr_surface_lookup_bucket(address,eye)];
    e->address=address; e->eye=eye; e->slot=slot; e->valid=1;
}
