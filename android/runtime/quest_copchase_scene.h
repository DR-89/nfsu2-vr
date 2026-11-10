#pragma once
#include "quest_copchase.h"
#include <math.h>
#include <string.h>
typedef struct {float position[3],color[3];} QuestCopChaseVertex;
enum {QUEST_COPCHASE_SCENE_VERTICES=1536};
/* Native VR rendition: physical strip span with a dark base, yellow ends
 * and raised steel spikes. Positions use the gameplay strip's exact axes. */
static inline void quest_copchase_scene_point(float out[3],const QuestCopChaseScene *s,float across,float along,float height)
{
    out[0]=s->position[0]+s->left[0]*across+s->forward[0]*along;
    out[1]=s->position[1]+s->left[1]*across+s->forward[1]*along;
    out[2]=s->position[2]+height;
}
static inline void quest_copchase_scene_triangle(QuestCopChaseVertex *out,const QuestCopChaseScene *s,
                                                const float a[3],const float b[3],const float c[3],const float color[3])
{
    const float *p[3]={a,b,c};for(int i=0;i<3;++i){quest_copchase_scene_point(out[i].position,s,p[i][0],p[i][1],p[i][2]);memcpy(out[i].color,color,12);}
}
static inline unsigned quest_copchase_scene_mesh(QuestCopChaseVertex *out,const QuestCopChaseScene *s)
{
    if(!out || !s || !s->active || !isfinite(s->half_width) || s->half_width<.1f || s->half_width>16)return 0;
    for(int k=0;k<3;++k)if(!isfinite(s->position[k]) || fabsf(s->position[k])>90000)return 0;
    for(int k=0;k<2;++k)if(!isfinite(s->forward[k]) || !isfinite(s->left[k]))return 0;
    float f=s->forward[0]*s->forward[0]+s->forward[1]*s->forward[1];
    float l=s->left[0]*s->left[0]+s->left[1]*s->left[1];
    if(fabsf(f-1)>.02f || fabsf(l-1)>.02f || fabsf(s->forward[0]*s->left[0]+s->forward[1]*s->left[1])>.02f)return 0;
    unsigned n=0;const float dark[3]={.08f,.085f,.09f},yellow[3]={.78f,.52f,.06f},steel[3]={.63f,.68f,.73f};
    for(int section=0;section<3;++section){
        float lo=section==0?-s->half_width:section==1?-s->half_width-.18f:s->half_width;
        float hi=section==0?s->half_width:section==1?-s->half_width:s->half_width+.18f;
        float a[3]={lo,-.14f,.025f},b[3]={hi,-.14f,.025f},c[3]={hi,.14f,.025f},d[3]={lo,.14f,.025f};
        const float *color=section?yellow:dark;
        quest_copchase_scene_triangle(out+n,s,a,b,c,color);n+=3;quest_copchase_scene_triangle(out+n,s,a,c,d,color);n+=3;
    }
    unsigned spikes=(unsigned)ceilf(s->half_width*4);if(spikes>64)spikes=64;
    for(unsigned i=0;i<spikes;++i){
        float x=-s->half_width+((float)i+.5f)*(2*s->half_width/(float)spikes);
        float p[5][3]={{x-.055f,-.08f,.025f},{x+.055f,-.08f,.025f},{x+.055f,.08f,.025f},{x-.055f,.08f,.025f},{x,0,.15f}};
        for(unsigned face=0;face<4;++face){quest_copchase_scene_triangle(out+n,s,p[face],p[(face+1)%4],p[4],steel);n+=3;}
    }
    return n;
}
/* Retail view is right/down/forward; OpenXR camera is right/up/back. */
static inline int quest_copchase_scene_matrix(float out[16],const float eye[16],const float view[16])
{
    for(int i=0;i<16;++i)if(!isfinite(eye[i]) || !isfinite(view[i]))return 0;
    if(fabsf(view[3])+fabsf(view[7])+fabsf(view[11])>.001f || fabsf(view[15]-1)>.001f)return 0;
    float converted[16];memcpy(converted,view,sizeof converted);
    for(int c=0;c<4;++c){converted[c*4+1]=-converted[c*4+1];converted[c*4+2]=-converted[c*4+2];}
    float m[16]={0};for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)m[c*4+r]+=eye[k*4+r]*converted[c*4+k];
    memcpy(out,m,sizeof m);return 1;
}
