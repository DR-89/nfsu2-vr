#ifndef QUEST_COCKPIT_PANELS_H
#define QUEST_COCKPIT_PANELS_H
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "quest_ghost_hands.h"
#include "quest_cockpit_fit.h"

/* The retail car is transformed into seated eye space before head tracking.
 * Only that rigid transform is eligible; roadside meshes and other cars must
 * never become mirrors merely because they happen to be close to the eye. */
static inline int quest_cockpit_own_car(const float *m,float sx,float sy,const float seat[3])
{
    return sx>.1f && sy>.1f && fabsf(m[12]-1)<.002f &&
        fabsf(m[13])+fabsf(m[14])<.002f && fabsf(m[15]+seat[0])<.015f &&
        fabsf(m[3]/sx-seat[1])<.015f &&
        fabsf(m[0])+fabsf(m[2])+fabsf(m[4])+fabsf(m[5])<.008f &&
        fabsf(m[1]/sx+1)<.005f && fabsf(m[6]/sy-1)<.005f;
}

/* Identify the small glass islands in the original body, retaining every
 * original triangle and its angle. Bounds are relative to the car center, so
 * left/right mirrors remain asymmetric relative to the driver's seat. */
static inline unsigned quest_cockpit_mirror_mesh(QuestGhostVertex *out,
    unsigned capacity,const float (*positions)[3],unsigned vertices,
    const uint32_t *indices,unsigned count,const float center[3],int which)
{
    unsigned chosen[192],used=0;
    float lo[3]={1e9f,1e9f,1e9f},hi[3]={-1e9f,-1e9f,-1e9f};
    if(count%3 || which<0 || which>2) return 0;
    for(unsigned t=0;t<count;t+=3) {
        int inside=1;
        for(unsigned k=0;k<3;++k) {
            unsigned v=indices[t+k]; if(v>=vertices) return 0;
            const float *p=positions[v];
            float x=p[0]-center[0],y=p[1]-center[1],z=p[2]-center[2];
            /* Car-relative bounds from the inspected glass. The old
             * eye-relative bounds broke after lowering/moving the seat. */
            if(which==0) inside &= fabsf(x)<.18f && y>1.03f && y<1.24f && z>-.42f && z<-.06f;
            else inside &= (which==1?x<-.60f && x>-1.10f:x>.60f && x<1.10f) &&
                y>.62f && y<.89f && z>-.64f && z<-.22f;
        }
        if(!inside) continue;
        if(used+3>capacity || used+3>192) return 0;
        for(unsigned k=0;k<3;++k) {
            unsigned v=indices[t+k]; chosen[used++]=v;
            for(int a=0;a<3;++a) { if(positions[v][a]<lo[a])lo[a]=positions[v][a]; if(positions[v][a]>hi[a])hi[a]=positions[v][a]; }
        }
    }
    float w=hi[0]-lo[0],h=hi[1]-lo[1],depth=hi[2]-lo[2];
    /* Reject housings, windscreen pieces, wipers and tiny trim. Glass is a
     * thin, broad polygon; never replace the chunky outer mirror housing. */
    if(used<6 || w<.08f || w>(which?.22f:.32f) || h<.035f || h>.13f || depth>.055f || w/h<1.2f) return 0;
    for(unsigned i=0;i<used;++i) {
        const float *p=positions[chosen[i]];
        memcpy(out[i].position,p,12); out[i].position[2]+=.002f;
        out[i].normal[0]=out[i].normal[1]=0;out[i].normal[2]=1;
        float u=(p[0]-lo[0])/w;
        /* Three sectors of the one wide rear camera. The mirror shader flips
         * the rear image once; the existing glass silhouette is unchanged. */
        out[i].uv[0]=which==1?u*.55f:which==2?.45f+u*.55f:.15f+u*.70f;
        out[i].uv[1]=(hi[1]-p[1])/h;
    }
    return used;
}

static inline void quest_cockpit_panel(QuestGhostVertex out[6],float x,float y,
    float z,float w,float h,float x0,float y0,float x1,float y1)
{
    const unsigned corner[6]={0,1,2,0,2,3};
    for(int i=0;i<6;++i) {
        unsigned c=corner[i];int right=c==1||c==2,bottom=c>=2;
        out[i]=(QuestGhostVertex){{x+(right?.5f:-.5f)*w,y+(bottom?-.5f:.5f)*h,z},
            {0,0,1},{(right?x1:x0)/640.0f,(bottom?y1:y0)/480.0f}};
    }
}

/* Complete 640x480 UI on one plane for pause menus and their submenus.
 * Keep the same windshield plane as race information, including both lower
 * corners; these pixels must not be moved onto the map or gauge panels. */
static inline void quest_cockpit_menu_panel(QuestGhostVertex out[6])
{
    quest_cockpit_panel(out,0,.60f,-3,2.8f,1.7f,0,0,640,480);
}
#endif
