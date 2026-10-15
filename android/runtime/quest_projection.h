#pragma once
#include <math.h>
#include <stdint.h>

/* The retail shell can be much closer than the road camera's 0.5-unit
 * near plane. Use a short world near plane only while seated inside it. */
#define QUEST_COCKPIT_WORLD_NEAR_M 0.08f
#define QUEST_WORLD_FAR 10000.0f
static inline void quest_projection_set_depth(float matrix[16],float near_z,float far_z)
{
    matrix[10]=-far_z/(far_z-near_z);
    matrix[11]=-1;
    matrix[14]=-far_z*near_z/(far_z-near_z);
}

/* The NTSC-U road material projects the 320x240 scene reflection through oT1.
 * Recognize its screen-coordinate producer and final projective write, not
 * merely slot 36 (program upload slots can be reused). */
static inline int quest_program_projects_reflection(const uint32_t (*program)[4], uint32_t start, uint32_t slots)
{
    if (start > slots || slots-start < 26) return 0;
    const uint32_t *a=program[start+23], *b=program[start+25];
    return a[1]==0x00544015 && a[2]==0xc4aa186c && a[3]==0x2c400ff8 &&
           b[1]==0x00400015 && b[2]==0x45ff886c && b[3]==0x2070c851;
}

/* Captured game projection rows c[113..115], independent of a model's scale.
 * Prefer these to extracting P from P*V*M for non-uniformly scaled objects. */
static inline int quest_projection_constants(const float rows[12], float scale[2])
{
    for(int i=0;i<12;++i) if(!isfinite(rows[i])) return 0;
    const int zeros[]={1,2,3,4,6,7,8,9};
    for(unsigned i=0;i<sizeof zeros/sizeof zeros[0];++i)
        if(fabsf(rows[zeros[i]])>1e-5f) return 0;
    if(rows[10]<0.9f || rows[10]>1.1f || fabsf(rows[11])<1e-5f) return 0;
    scale[0]=fabsf(rows[0]); scale[1]=fabsf(rows[5]);
    return scale[0]>0.1f && scale[0]<20 && scale[1]>0.1f && scale[1]<20;
}

/* Preserve depth-only offsets applied by the guest shader (decals and layered
 * car surfaces). Reconstructing XYZ from clip W otherwise silently drops them.
 * With the same camera, this makes the new projected depth equal the guest's. */
static inline int quest_projection_depth(const float rows[12], float eye_b, float depth[4])
{
    float scales[2];
    if(!quest_projection_constants(rows,scales) || !isfinite(eye_b) || eye_b>=0 || rows[11]>=0) return 0;
    depth[0]=rows[10]; depth[1]=rows[11]; depth[2]=eye_b/rows[11]; depth[3]=1;
    return 1;
}

/* Recover the symmetric perspective scales from P*V*M without assuming an
 * FOV. Orthographic, skewed and non-finite matrices are deliberately rejected.
 * Translation is irrelevant to the scale; rigid/uniformly scaled models work. */
static inline int quest_projection_scales(const float m[16], float scale[2])
{
    float ww=0, xw=0, yw=0, xx=0, yy=0, xy=0;
    for(int i=0;i<16;++i) if(!isfinite(m[i])) return 0;
    for(int i=0;i<3;++i) {
        ww+=m[12+i]*m[12+i]; xw+=m[i]*m[12+i]; yw+=m[4+i]*m[12+i];
        xx+=m[i]*m[i]; yy+=m[4+i]*m[4+i]; xy+=m[i]*m[4+i];
    }
    if(ww<1e-10f || xx<1e-10f || yy<1e-10f) return 0;
    /* This path is for the retail game's centered perspective. An asymmetric
     * guest camera needs its center recovered too, rather than silently lost. */
    if(fabsf(xw)>0.02f*sqrtf(xx*ww) || fabsf(yw)>0.02f*sqrtf(yy*ww) ||
       fabsf(xy)>0.02f*sqrtf(xx*yy)) return 0;
    scale[0]=sqrtf(xx/ww); scale[1]=sqrtf(yy/ww);
    return scale[0]>0.1f && scale[0]<20 && scale[1]>0.1f && scale[1]<20;
}

/* The captured NTSC-U shaders write clip oPos using DP4 with c[96..99],
 * then the XDK viewport tail divides by w. Verify all four rows in the actual
 * microcode before treating these constants as the camera transform. */
static inline int quest_program_has_mvp(const uint32_t (*program)[4], uint32_t start, uint32_t slots)
{
    unsigned rows=0;
    for(uint32_t pc=start;pc<slots;++pc) {
        const uint32_t *t=program[pc];
        unsigned mac=(t[1]>>21)&15, ci=(t[1]>>13)&255;
        unsigned amux=(t[2]>>26)&3, bmux=(t[2]>>11)&3;
        unsigned cmux=amux==3?0:1;
        unsigned swizzle=cmux ? (t[2]>>17)&255 : t[1]&255;
        unsigned negate=cmux ? (t[2]>>25)&1 : (t[1]>>8)&1;
        if(mac==7 && ci>=96 && ci<=99 && !(t[3]&2) &&
           ((amux==3) != (bmux==3)) && swizzle==0x1b && !negate) {
            unsigned mask=0;
            if(((t[3]>>20)&15)==12) mask|=(t[3]>>24)&15;
            if((t[3]&0x800) && !(t[3]&4) && ((t[3]>>3)&255)==0) mask|=(t[3]>>12)&15;
            if(mask & (8u>>(ci-96))) rows|=1u<<(ci-96);
        }
        if(t[3]&1) break;
    }
    return rows==15;
}
