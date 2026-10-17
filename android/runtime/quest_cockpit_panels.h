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
static inline unsigned quest_cockpit_mirror_mesh_reference(QuestGhostVertex *out,
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

/* Some side glass is slightly convex. Its complete small material batch is
 * rear-facing and thin, unlike the many differently oriented housing faces. */
static inline unsigned quest_cockpit_curved_glass(QuestGhostVertex *out,
    unsigned capacity,const float (*p)[3],unsigned vertices,const uint32_t *ix,
    unsigned count,const float center[3],int which)
{
    if(!which || count<6 || count>96 || count>capacity || count%3)return 0;
    float lo[3]={1e9f,1e9f,1e9f},hi[3]={-1e9f,-1e9f,-1e9f},axis[3]={0},normal[32][3];
    for(unsigned t=0;t<count;t+=3) {
        for(int j=0;j<3;++j) {
            if(ix[t+j]>=vertices)return 0;const float *q=p[ix[t+j]];
            float x=q[0]-center[0],y=q[1]-center[1],z=q[2]-center[2];
            if(!(which==1?x<-.47f && x>-1.4f:x>.47f && x<1.4f) ||
               y<.45f || y>1.55f || z<-1.15f || z>.25f)return 0;
            for(int a=0;a<3;++a){lo[a]=fminf(lo[a],q[a]);hi[a]=fmaxf(hi[a],q[a]);}
        }
        float a[3],b[3],length=0;
        for(int k=0;k<3;++k){a[k]=p[ix[t+1]][k]-p[ix[t]][k];b[k]=p[ix[t+2]][k]-p[ix[t]][k];}
        for(int k=0;k<3;++k){normal[t/3][k]=a[(k+1)%3]*b[(k+2)%3]-a[(k+2)%3]*b[(k+1)%3];length+=normal[t/3][k]*normal[t/3][k];axis[k]+=normal[t/3][k];}
        length=sqrtf(length);if(length<1e-7f)return 0;
        for(int k=0;k<3;++k)normal[t/3][k]/=length;
        if(normal[t/3][2]<.65f)return 0;
    }
    float length=sqrtf(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);if(length<1e-7f)return 0;
    for(int k=0;k<3;++k)axis[k]/=length;
    for(unsigned t=0;t<count/3;++t) {
        float dot=0;for(int k=0;k<3;++k)dot+=axis[k]*normal[t][k];if(dot<.94f)return 0;
    }
    float plane_min=1e9f,plane_max=-1e9f;
    for(unsigned j=0;j<count;++j) {
        float d=0;for(int k=0;k<3;++k)d+=axis[k]*p[ix[j]][k];plane_min=fminf(plane_min,d);plane_max=fmaxf(plane_max,d);
    }
    float w=hi[0]-lo[0],h=hi[1]-lo[1];
    if(w<.08f || w>.35f || h<.035f || h>.20f || w/h<1.05f || w/h>5.5f || plane_max-plane_min>.02f)return 0;
    for(unsigned j=0;j<count;++j) {
        memcpy(out[j].position,p[ix[j]],12);out[j].position[2]+=.002f;
        out[j].normal[0]=out[j].normal[1]=0;out[j].normal[2]=1;
        float u=(p[ix[j]][0]-lo[0])/w;
        out[j].uv[0]=which==1?u*.55f:.45f+u*.55f;out[j].uv[1]=(hi[1]-p[ix[j]][1])/h;
    }
    return count;
}

/* Other vehicles use different mirror heights, widths and slants. Select a
 * connected planar glass island rather than merging every triangle inside a
 * Peugeot-sized box (which also merges the housing and rejects slanted glass).
 * This bounded search is cached by immutable source geometry in the renderer. */
static inline unsigned quest_cockpit_mirror_mesh(QuestGhostVertex *out,
    unsigned capacity,const float (*p)[3],unsigned vertices,
    const uint32_t *ix,unsigned count,const float center[3],int which)
{
    unsigned reference=quest_cockpit_mirror_mesh_reference(out,capacity,p,vertices,ix,count,center,which);
    if(reference)return reference;
    if(count%3 || count>1536 || which<0 || which>2)return 0;
    unsigned triangles=count/3;unsigned char eligible[512]={0},visited[512]={0};
    float normal[512][3];
    for(unsigned t=0;t<triangles;++t) {
        int inside=1;
        for(int k=0;k<3;++k) {
            unsigned v=ix[3*t+k];if(v>=vertices)return 0;
            float x=p[v][0]-center[0],y=p[v][1]-center[1],z=p[v][2]-center[2];
            /* Small coupes put the glass below the old .94 m cutoff (the
             * Lancer misses it by only 3 mm). Position just bounds the search;
             * the connected planar island and glass dimensions identify it. */
            if(which==0)inside &= fabsf(x)<.30f && y>.70f && y<1.70f && z>-.95f && z<.18f;
            else inside &= (which==1?x<-.47f && x>-1.4f:x>.47f && x<1.4f) &&
                y>.45f && y<1.55f && z>-1.15f && z<.25f;
        }
        if(!inside)continue;
        float a[3],b[3],length=0;
        for(int k=0;k<3;++k){a[k]=p[ix[3*t+1]][k]-p[ix[3*t]][k];b[k]=p[ix[3*t+2]][k]-p[ix[3*t]][k];}
        for(int k=0;k<3;++k){normal[t][k]=a[(k+1)%3]*b[(k+2)%3]-a[(k+2)%3]*b[(k+1)%3];length+=normal[t][k]*normal[t][k];}
        length=sqrtf(length);if(length<1e-7f)continue;
        for(int k=0;k<3;++k)normal[t][k]/=length;
        eligible[t]=fabsf(normal[t][2])>.65f;
    }
    unsigned best[192],best_count=0;float best_area=0;
    for(unsigned seed=0;seed<triangles;++seed)if(eligible[seed] && !visited[seed]) {
        unsigned group[512],used=1;group[0]=seed;visited[seed]=1;
        for(unsigned q=0;q<used;++q)for(unsigned t=0;t<triangles;++t)if(eligible[t] && !visited[t]) {
            float dot=0,plane=0;
            for(int k=0;k<3;++k){dot+=normal[seed][k]*normal[t][k];plane+=normal[seed][k]*(p[ix[3*t]][k]-p[ix[3*seed]][k]);}
            if(fabsf(dot)<.985f || fabsf(plane)>.004f)continue;
            int shared=0;
            for(int a=0;a<3;++a)for(int b=0;b<3;++b) {
                float d=0;for(int k=0;k<3;++k){float v=p[ix[3*t+a]][k]-p[ix[3*group[q]+b]][k];d+=v*v;}
                if(d<1e-10f)shared=1;
            }
            if(shared){visited[t]=1;group[used++]=t;}
        }
        /* A mirror housing contains a larger recessed back plate behind the
         * separate glass draw. Do not cover that whole plate with the image.
         * Generic glass candidates must dominate their material batch. */
        if(used<2 || used*3>capacity || used*3>192)continue;
        float lo[3]={1e9f,1e9f,1e9f},hi[3]={-1e9f,-1e9f,-1e9f};
        for(unsigned j=0;j<used;++j)for(int a=0;a<3;++a)for(int k=0;k<3;++k) {
            float v=p[ix[3*group[j]+a]][k];lo[k]=fminf(lo[k],v);hi[k]=fmaxf(hi[k],v);
        }
        float w=hi[0]-lo[0],h=hi[1]-lo[1];
        if(w<.08f || w>(which?.35f:.42f) || h<.035f || h>.20f || w/h<1.25f || w/h>5.5f)continue;
        /* Large SUV glass can share its material with the shell. Accept only
         * its broad rear-facing plate; small recessed housings stay excluded. */
        if(which && used*4<triangles*3 && !(used>=6 && normal[seed][2]>.85f && w*h>.03f))continue;
        if(w*h>best_area) {
            best_area=w*h;best_count=0;
            for(unsigned j=0;j<used;++j)for(int a=0;a<3;++a)best[best_count++]=ix[3*group[j]+a];
        }
    }
    if(!best_count)return quest_cockpit_curved_glass(out,capacity,p,vertices,ix,count,center,which);
    float left=1e9f,right=-1e9f,bottom=1e9f,top=-1e9f;
    for(unsigned i=0;i<best_count;++i){left=fminf(left,p[best[i]][0]);right=fmaxf(right,p[best[i]][0]);bottom=fminf(bottom,p[best[i]][1]);top=fmaxf(top,p[best[i]][1]);}
    for(unsigned i=0;i<best_count;++i) {
        memcpy(out[i].position,p[best[i]],12);out[i].position[2]+=.002f;
        out[i].normal[0]=out[i].normal[1]=0;out[i].normal[2]=1;
        float u=(p[best[i]][0]-left)/(right-left);
        out[i].uv[0]=which==1?u*.55f:which==2?.45f+u*.55f:.15f+u*.70f;
        out[i].uv[1]=(top-p[best[i]][1])/(top-bottom);
    }
    return best_count;
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
