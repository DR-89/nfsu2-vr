#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "quest_ghost_hands.h"
typedef struct {
    uint32_t vertices,width,height;
    const unsigned char *geometry,*pixels;
} QuestGloveAsset;
/* V1 stores matching open/grip vertices. Keep the index from the open
 * glove and curl the other fingers with the authored grip geometry. The
 * blend falls inside the finger webs; the index pad supplies the hit point. */
static inline void quest_glove_point_pose(QuestGhostVertex *out,const QuestGhostVertex *open,
    const QuestGhostVertex *grip,unsigned count,float tip[3])
{
    tip[0]=tip[2]=0;tip[1]=0;
    for(unsigned i=0;i<count;++i) {
        float x=open[i].position[0];
        float w=fmaxf(0,fminf(1,fminf((x+.065f)/.011f,(-.009f-x)/.008f)));
        out[i]=grip[i];
        for(int j=0;j<3;++j) {
            out[i].position[j]+=w*(open[i].position[j]-grip[i].position[j]);
            out[i].normal[j]+=w*(open[i].normal[j]-grip[i].normal[j]);
        }
        if(w==1 && out[i].position[1]>tip[1])memcpy(tip,out[i].position,3*sizeof(float));
    }
}
static inline uint32_t quest_glove_u32(const unsigned char *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static inline int quest_glove_asset(QuestGloveAsset *out,const void *bytes,size_t size)
{
    const unsigned char *p=bytes;
    if(!p || size<32 || memcmp(p,"NGHV",4) || quest_glove_u32(p+4)!=1 ||
       quest_glove_u32(p+20) || quest_glove_u32(p+24) || quest_glove_u32(p+28)) return 0;
    QuestGloveAsset a={0};a.vertices=quest_glove_u32(p+8);a.width=quest_glove_u32(p+12);a.height=quest_glove_u32(p+16);
    if(!a.vertices || a.vertices>60000 || a.vertices%3 || !a.width || !a.height || a.width>2048 || a.height>2048) return 0;
    size_t geometry=(size_t)a.vertices*32*2,texture=(size_t)a.width*a.height*4;
    if(size!=32+geometry+texture) return 0;
    a.geometry=p+32;a.pixels=a.geometry+geometry;
    for(size_t i=0;i<(size_t)a.vertices*2;++i) {
        float v[8];memcpy(v,a.geometry+i*32,32);
        for(int k=0;k<8;++k) if(!isfinite(v[k])) return 0;
        for(int k=0;k<3;++k) if(fabsf(v[k])>.5f) return 0;
        float n=v[3]*v[3]+v[4]*v[4]+v[5]*v[5];
        if(n<.5f || n>1.5f || v[6]<0 || v[6]>1 || v[7]<0 || v[7]>1) return 0;
    }
    *out=a;return 1;
}
