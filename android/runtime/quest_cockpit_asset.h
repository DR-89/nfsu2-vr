#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

/* NCPK v2: 64-byte LE header (eight uint32, pivot/axis float3, two reserved
 * uint32), interleaved float3 position,
 * float3 normal, float2 UV vertices, uint32 indices, then RGBA8 atlas.
 * Assets are converted offline; no archive/parser/texture decode in a frame. */
typedef struct {
    uint32_t vertices, indices, width, height;
    uint32_t wheel_first, wheel_count;
    float wheel_pivot[3], wheel_axis[3];
    const unsigned char *vertex_data, *index_data, *pixels;
} QuestCockpitAsset;
static inline uint32_t quest_cockpit_u32(const unsigned char *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline int quest_cockpit_asset(QuestCockpitAsset *out, const void *bytes, size_t size)
{
    const unsigned char *p=(const unsigned char *)bytes;
    if(!p || size<64 || memcmp(p,"NCPK",4) || quest_cockpit_u32(p+4)!=2 ||
       quest_cockpit_u32(p+56) || quest_cockpit_u32(p+60)) return 0;
    QuestCockpitAsset a={0};
    a.vertices=quest_cockpit_u32(p+8); a.indices=quest_cockpit_u32(p+12);
    a.width=quest_cockpit_u32(p+16); a.height=quest_cockpit_u32(p+20);
    a.wheel_first=quest_cockpit_u32(p+24); a.wheel_count=quest_cockpit_u32(p+28);
    memcpy(a.wheel_pivot,p+32,12); memcpy(a.wheel_axis,p+44,12);
    if(!a.vertices || a.vertices>250000 || !a.indices || a.indices>750000 || a.indices%3 ||
       !a.width || !a.height || a.width>4096 || a.height>4096) return 0;
    if(!a.wheel_first || a.wheel_first>=a.indices || a.wheel_first%3 || !a.wheel_count ||
       a.wheel_count!=a.indices-a.wheel_first || a.wheel_count%3) return 0;
    float axis_length=0;
    for(int j=0;j<3;++j) {
        if(!isfinite(a.wheel_pivot[j]) || fabsf(a.wheel_pivot[j])>5 ||
           !isfinite(a.wheel_axis[j]) || fabsf(a.wheel_axis[j])>1.001f) return 0;
        axis_length+=a.wheel_axis[j]*a.wheel_axis[j];
    }
    if(fabsf(axis_length-1)>0.001f) return 0;
    size_t vs=(size_t)a.vertices*32, is=(size_t)a.indices*4, ts=(size_t)a.width*a.height*4;
    if(size!=64+vs+is+ts) return 0;
    a.vertex_data=p+64; a.index_data=p+64+vs; a.pixels=p+64+vs+is;
    for(uint32_t i=0;i<a.indices;++i) if(quest_cockpit_u32(a.index_data+4*i)>=a.vertices) return 0;
    for(uint32_t i=0;i<a.vertices;++i) {
        float v[8]; memcpy(v,a.vertex_data+32*i,sizeof v);
        for(int j=0;j<8;++j) if(!isfinite(v[j]) || fabsf(v[j])>20) return 0;
        if(v[6]<0 || v[6]>1 || v[7]<0 || v[7]>1) return 0;
    }
    *out=a; return 1;
}
