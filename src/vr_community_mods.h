#pragma once
#include <stddef.h>
#include <stdint.h>
#include <math.h>

/* Row-vector headlight projector: view * pitch(+5 degrees) * projection.
 * Keep the eye/cockpit camera and every non-headlight view unchanged. */
static inline int vr_mod_high_beam(const float view[16],const float projection[16],float out[16])
{
    if(!view || !projection || !out)return 0;
    for(unsigned i=0;i<16;i++)if(!isfinite(view[i]) || !isfinite(projection[i]))return 0;
    const float c=.9961946981f,s=.08715574275f;
    float v[16],result[16];
    for(unsigned r=0;r<4;r++){
        v[r*4]=view[r*4];v[r*4+1]=view[r*4+1]*c-view[r*4+2]*s;
        v[r*4+2]=view[r*4+1]*s+view[r*4+2]*c;v[r*4+3]=view[r*4+3];
    }
    for(unsigned r=0;r<4;r++)for(unsigned k=0;k<4;k++){
        float n=0;for(unsigned j=0;j<4;j++)n+=v[r*4+j]*projection[j*4+k];
        if(!isfinite(n))return 0;result[r*4+k]=n;
    }
    for(unsigned i=0;i<16;i++)out[i]=result[i];return 1;
}

/* Extra Options' Quick Race ranges; Drag retains its grid capacity. */
static inline unsigned vr_mod_qr_top(unsigned mode,unsigned track,unsigned humans)
{
    unsigned grid=mode==2?(track==4402?3u:4u):6u;
    return humans<grid?grid-humans:0;
}
static inline unsigned vr_mod_qr_step(unsigned value,unsigned lo,unsigned hi,int right)
{
    if(hi<lo)return lo;
    if(value<lo || value>hi)return right?lo:hi;
    return right?(value==hi?lo:value+1):(value==lo?hi:value-1);
}

/* NFSU2.xyz's career catch-up choices. Invalid telemetry leaves retail intact. */
static inline int vr_mod_catchup(int mode,int career,int network,float *pace,float *boost)
{
    if(!career || network || mode<1 || mode>2 || !pace || !boost || !isfinite(*pace) || !isfinite(*boost))return 0;
    int changed=0;
    if((mode==2 && *pace!=1.f) || (mode==1 && *pace>1.f)){*pace=1.f;changed=1;}
    if(*boost!=1.f){*boost=1.f;changed=1;}
    return changed;
}

/* Visual Reputation Remover's stage values (thelink2012, MIT;
 * community/vendor/visualrep/LICENSE). Invalid Xbox stage values
 * leave the original calculation active instead of granting progression. */
static inline float vr_mod_visual_rating(int stage)
{
    static const float stars[]={0.f,1.f,3.f,5.5f,8.f,10.f,10.f};
    return stage>=0 && stage<7?stars[stage]:-1.f;
}

/* Xbox USA career loader 0x0011F370: 0x80034A10 contains 0x34A11
 * (224 races, 0x88 bytes) and 0x34A18 (6 stages, 0x50 bytes).
 * Verified against both retail archive copies and the PC v1.2 tables.
 * Hashes identify the entire unmodified tables, not guessed memory offsets.
 * This applies Show Hidden Races' documented data edits to a loaded copy;
 * neither the disc/archive nor a saved career is written by this helper.
 */
static inline uint32_t vr_mod_u32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static inline uint64_t vr_mod_hash(const uint8_t *p,size_t n)
{
    uint64_t h=UINT64_C(14695981039346656037);
    for(size_t i=0;i<n;++i)h=(h^p[i])*UINT64_C(1099511628211);
    return h;
}
/* -1: unsupported/malformed (no writes); 0: disabled; 1: patched; 2: already patched.
 * Validate both complete tables before making any change. */
static inline int vr_mod_hidden_races(void *buffer,size_t size,int enabled)
{
    if(!enabled)return 0;
    if(!buffer || size<8 || size>131072)return -1;
    uint8_t *p=(uint8_t *)buffer,*races=NULL,*stages=NULL;
    if(vr_mod_u32(p)!=0x80034a10u || vr_mod_u32(p+4)!=size-8)return -1;
    size_t offset=8;
    while(offset<size) {
        if(size-offset<8)return -1;
        uint32_t id=vr_mod_u32(p+offset),n=vr_mod_u32(p+offset+4);
        if(n>size-offset-8)return -1;
        if(id==0x34a11u) {
            if(races || n!=224u*136)return -1;
            races=p+offset+8;
        }
        if(id==0x34a18u) {
            if(stages || n!=6u*80)return -1;
            stages=p+offset+8;
        }
        offset+=8u+n;
    }
    if(!races || !stages)return -1;
    uint64_t rh=vr_mod_hash(races,224u*136),sh=vr_mod_hash(stages,6u*80);
    if(rh==UINT64_C(0x78d6c71ac7e9d629) && sh==UINT64_C(0x0454036740ece7ce))return 2;
    if(rh!=UINT64_C(0x90885f20f972e18a) || sh!=UINT64_C(0xf5a927ec755a75c9))return -1;
    for(unsigned i=0;i<224;++i)races[i*136+127]=0;
    for(unsigned i=0;i<6;++i)for(unsigned k=48;k<53;++k)
        if(stages[i*80+k]<8)stages[i*80+k]=8;
    return 1;
}
