#pragma once
#include <math.h>
#include <stdint.h>

/* Retail cars place a crossed, two-card driver proxy at their own seated
 * head position. From inside it the cards become large opaque bars. Match
 * the complete topology and geometry, not a texture address or car name.
 * Call only for the player's rigid shell in the tracked cockpit world pass;
 * other cameras, other drivers, mirrors and the original body stay intact.
 * Positions use retail car coordinates: forward, left, up, homogeneous W. */
static inline int quest_cockpit_driver_cards(const float p[8][4],
                                            const uint32_t ix[12])
{
    float lo[3]={1e9f,1e9f,1e9f},hi[3]={-1e9f,-1e9f,-1e9f};
    for(int v=0;v<8;++v) {
        if(!isfinite(p[v][3]) || fabsf(p[v][3]-1)>.001f)return 0;
        for(int k=0;k<3;++k) {
            if(!isfinite(p[v][k]))return 0;
            lo[k]=fminf(lo[k],p[v][k]);hi[k]=fmaxf(hi[k],p[v][k]);
        }
    }
    /* Identify the complete crossed-card mesh independently of the seat's
     * location. The stock A3/Skyline put it on the opposite side; Navigator
     * is higher, and Lancer/Peugeot fall outside the old Peugeot-era window.
     * A position window cannot distinguish these from legitimate trim.
     * Keep only broad finite car-space bounds, then require the specific
     * two-plane topology, upper-body size, orthogonality and shared center. */
    for(int k=0;k<3;++k)if(fabsf(lo[k])>5 || fabsf(hi[k])>5)return 0;
    for(int k=0;k<3;++k)if(hi[k]-lo[k]<.18f || hi[k]-lo[k]>.75f)return 0;
    if(hi[2]-lo[2]<.35f || hi[2]-lo[2]>.55f)return 0;
    float normal[2][3],plane[2];unsigned masks[2]={0},groups[2]={0};int used=0;
    for(int t=0;t<4;++t) {
        for(int k=0;k<3;++k)if(ix[t*3+k]>=8)return 0;
        const float *a=p[ix[t*3]],*b=p[ix[t*3+1]],*c=p[ix[t*3+2]];
        float u[3],v[3],n[3],length=0;
        for(int k=0;k<3;++k){u[k]=b[k]-a[k];v[k]=c[k]-a[k];}
        for(int k=0;k<3;++k){n[k]=u[(k+1)%3]*v[(k+2)%3]-u[(k+2)%3]*v[(k+1)%3];length+=n[k]*n[k];}
        length=sqrtf(length);if(length<.01f)return 0;
        for(int k=0;k<3;++k)n[k]/=length;
        if(fabsf(n[2])>.05f)return 0; /* both cards must be vertical */
        int group=-1;
        for(int g=0;g<used;++g) {
            float dot=0,offset=-plane[g];
            for(int k=0;k<3;++k){dot+=n[k]*normal[g][k];offset+=a[k]*normal[g][k];}
            if(fabsf(dot)>.999f && fabsf(offset)<.002f)group=g;
        }
        if(group<0) {
            if(used==2)return 0;
            group=used++;plane[group]=0;
            for(int k=0;k<3;++k){normal[group][k]=n[k];plane[group]+=n[k]*a[k];}
        }
        ++groups[group];for(int k=0;k<3;++k)masks[group]|=1u<<ix[t*3+k];
    }
    if(used!=2 || groups[0]!=2 || groups[1]!=2 ||
       (masks[0]&masks[1]) || (masks[0]|masks[1])!=255)return 0;
    float dot=0;for(int k=0;k<3;++k)dot+=normal[0][k]*normal[1][k];
    if(fabsf(dot)>.1f)return 0; /* crossed cards, not neighboring trim */
    for(int g=0;g<2;++g) {
        unsigned vertices=0;float offset=-plane[g];
        for(int k=0;k<3;++k)offset+=normal[g][k]*(lo[k]+hi[k])*.5f;
        if(fabsf(offset)>.02f)return 0; /* planes cross at their centers */
        for(int v=0;v<8;++v)if(masks[g]&(1u<<v)) {
            ++vertices;
            if(fminf(fabsf(p[v][2]-lo[2]),fabsf(p[v][2]-hi[2]))>.002f)return 0;
        }
        if(vertices!=4)return 0;
    }
    return 1;
}
