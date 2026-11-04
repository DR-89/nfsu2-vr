#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

/* NCPK v2: 64-byte LE header (eight uint32, pivot/axis float3, two reserved
 * uint32), interleaved float3 position,
 * float3 normal, float2 UV vertices, uint32 indices, then RGBA8 atlas.
 * NCPK v3 uses the reserved words for the lever's first vertex and count.
 * v4 adds a 64-byte pedal block before the vertex data. */
typedef struct { uint32_t first,count;float hinge[3]; } QuestCockpitPedal;
typedef struct {
    uint32_t vertices, indices, width, height;
    uint32_t wheel_first, wheel_count;
    uint32_t shifter_first, shifter_count;
    QuestCockpitPedal pedals[3]; /* accelerator, brake, clutch */
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
    if(!p || size<64 || memcmp(p,"NCPK",4)) return 0;
    QuestCockpitAsset a={0};
    size_t header_size=64;
    a.vertices=quest_cockpit_u32(p+8); a.indices=quest_cockpit_u32(p+12);
    a.shifter_first=quest_cockpit_u32(p+56); a.shifter_count=quest_cockpit_u32(p+60);
    switch(quest_cockpit_u32(p+4)) {
    case 2: if(a.shifter_first || a.shifter_count)return 0; break;
    case 4:
        header_size=128;
        if(size<header_size || quest_cockpit_u32(p+124))return 0;
        for(int i=0;i<3;++i) {
            const unsigned char *record=p+64+20*i;
            QuestCockpitPedal *pedal=&a.pedals[i];
            pedal->first=quest_cockpit_u32(record);pedal->count=quest_cockpit_u32(record+4);
            if(!pedal->count || pedal->first>=a.vertices || pedal->count>a.vertices-pedal->first ||
               (i && pedal->first!=a.pedals[i-1].first+a.pedals[i-1].count))return 0;
            memcpy(pedal->hinge,record+8,12);
            for(int k=0;k<3;++k)if(!isfinite(pedal->hinge[k]) || fabsf(pedal->hinge[k])>5)return 0;
        }
        if(a.shifter_first>=a.pedals[0].first || a.shifter_count>a.pedals[0].first-a.shifter_first)return 0;
        /* fall through */
    case 3: if(!a.shifter_count || a.shifter_first>=a.vertices ||
               a.shifter_count>a.vertices-a.shifter_first)return 0; break;
    default: return 0;
    }
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
    if(size!=header_size+vs+is+ts) return 0;
    a.vertex_data=p+header_size; a.index_data=a.vertex_data+vs; a.pixels=a.index_data+is;
    for(uint32_t i=0;i<a.indices;++i) if(quest_cockpit_u32(a.index_data+4*i)>=a.vertices) return 0;
    for(uint32_t i=0;i<a.vertices;++i) {
        float v[8]; memcpy(v,a.vertex_data+32*i,sizeof v);
        for(int j=0;j<8;++j) if(!isfinite(v[j]) || fabsf(v[j])>20) return 0;
        if(v[6]<0 || v[6]>1 || v[7]<0 || v[7]>1) return 0;
    }
    *out=a; return 1;
}
