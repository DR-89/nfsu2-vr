#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "quest_ghost_hands.h"

/* Cabin coordinates in meters: +X right, +Y up, +Z toward the rear. */
enum { QUEST_MIRROR_CENTER, QUEST_MIRROR_LEFT, QUEST_MIRROR_RIGHT, QUEST_MIRRORS };
typedef struct { float center[3],half[3]; int valid; } QuestMirrorMount;
typedef struct { float yaw,pitch; } QuestMirrorAngle;
/* Mechanical trim around the calibrated rearward view. Reflected rays turn
 * through twice the glass angle. Use the same stops for live and saved angles. */
#define QUEST_MIRROR_YAW_LIMIT .18f
#define QUEST_MIRROR_PITCH_LIMIT .105f
#define QUEST_MIRROR_REAR_FAR 600.f
typedef struct { QuestMirrorAngle inside,outside; } QuestMirrorSetting;
typedef struct {
    int hand,part,blocked[2],rebase;
    uint32_t tick;
    float previous[16],anchor[16],contact[3];
    QuestMirrorAngle start;
} QuestMirrorGrip;
/* Rows of the actual retail rear camera in cabin space, with its position
 * expressed in the same space. Camera axes are right, down, forward. */
typedef struct { float axis[3][3],origin[3]; } QuestMirrorCamera;
/* A reflected eye is in front of the glass. Clip the opaque mirror backs
 * out of its rear capture, otherwise they appear as a bar in the image.
 * Bounds and camera must both be in fitted cabin space. */
static inline float quest_mirror_capture_near(const QuestMirrorCamera *cam,const QuestMirrorMount mounts[3])
{
    float near_z=.18f;
    for(int p=0;p<3;++p)if(mounts[p].valid) {
        float z=0;
        for(int k=0;k<3;++k)
            z+=cam->axis[2][k]*(mounts[p].center[k]-cam->origin[k])+fabsf(cam->axis[2][k])*mounts[p].half[k];
        if(isfinite(z))near_z=fmaxf(near_z,fminf(2.5f,z+.04f));
    }
    return near_z;
}
static inline float quest_mirror_dot(const float a[3],const float b[3])
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static inline int quest_mirror_unit(float v[3])
{
    float n=sqrtf(quest_mirror_dot(v,v));
    if(!isfinite(n) || n<.00001f)return 0;
    for(int k=0;k<3;++k)v[k]/=n;
    return 1;
}
static inline float quest_mirror_bound(float value,float limit)
{ return fmaxf(-limit,fminf(limit,value)); }
static inline QuestMirrorAngle quest_mirror_trim(float yaw,float pitch)
{
    return (QuestMirrorAngle){quest_mirror_bound(yaw,QUEST_MIRROR_YAW_LIMIT),
                              quest_mirror_bound(pitch,QUEST_MIRROR_PITCH_LIMIT)};
}
/* Attenuate the tracked head center around the adjusted seat. Translate
 * both optical eyes equally, preserving IPD and leaving the world view alone. */
static inline void quest_mirror_comfort_eyes(float eyes[2][4],const float seat[3])
{
    float lean[3];
    for(int k=0;k<3;++k)lean[k]=.5f*(eyes[0][k]+eyes[1][k])-seat[k];
    float length=sqrtf(quest_mirror_dot(lean,lean));
    float gain=.70f;
    if(length*gain>.20f)gain=.20f/length;
    for(int i=0;i<2;++i)for(int k=0;k<3;++k)eyes[i][k]+=(gain-1.f)*lean[k];
}
static inline QuestMirrorMount quest_mirror_mount(const QuestGhostVertex *mesh,unsigned count)
{
    QuestMirrorMount m={0};if(!count)return m;
    for(int k=0;k<3;++k) {
        float lo=mesh[0].position[k],hi=lo;
        for(unsigned i=0;i<count;++i) {
            float x=mesh[i].position[k];if(!isfinite(x))return (QuestMirrorMount){0};
            lo=fminf(lo,x);hi=fmaxf(hi,x);
        }
        m.center[k]=(hi+lo)*.5f;m.half[k]=(hi-lo)*.5f;
    }
    m.valid=m.half[0]>.025f && m.half[1]>.012f;
    return m;
}
static inline QuestMirrorAngle quest_mirror_angle(const QuestMirrorSetting *settings,int part)
{
    QuestMirrorAngle a=part==QUEST_MIRROR_CENTER?settings->inside:settings->outside;
    if(part==QUEST_MIRROR_LEFT)a.yaw=-a.yaw;
    return a;
}
static inline void quest_mirror_rotate(float out[3],const float in[3],QuestMirrorAngle a,int inverse)
{
    float cy=cosf(a.yaw),sy=sinf(a.yaw),cp=cosf(a.pitch),sp=sinf(a.pitch);
    float x=in[0],y=in[1],z=in[2];
    if(inverse) {
        float p=sy*x+cy*z;
        out[0]=cy*x-sy*z;out[1]=cp*y+sp*p;out[2]=-sp*y+cp*p;
    } else {
        float p=sp*y+cp*z;
        out[0]=cy*x+sy*p;out[1]=cp*y-sp*z;out[2]=-sy*x+cy*p;
    }
}
static inline void quest_mirror_normal(float out[3],const QuestMirrorMount *m,int part,QuestMirrorAngle a,
                                       const float seat[3])
{
    /* Neutral aim for a driver at the base seat: straight behind in the
     * center and into the adjacent lane on each side. The optical normal
     * bisects the directions to the driver and to that rearward scene. */
    float n[3];for(int k=0;k<3;++k)n[k]=seat[k]-m->center[k];
    float aim[3]={part==1?-.60f:part==2?.60f:0,0,1};
    quest_mirror_unit(n);quest_mirror_unit(aim);
    for(int k=0;k<3;++k)n[k]+=aim[k];
    quest_mirror_unit(n);quest_mirror_rotate(out,n,a,0);
}
static inline void quest_mirror_virtual_eye(float out[3],const float eye[3],const float pivot[3],const float n[3])
{
    float d=2*(quest_mirror_dot(eye,n)-quest_mirror_dot(pivot,n));
    for(int k=0;k<3;++k)out[k]=eye[k]-d*n[k];
}
/* Both retail matrices map world coordinates to right/down/forward. Use
 * a car-centered front view here; the renderer subtracts the fitted seat. */
static inline int quest_mirror_camera(QuestMirrorCamera *cam,const float front[16],const float rear[16])
{
    float world[3];
    for(int i=0;i<3;++i)
        world[i]=-(rear[4*i]*rear[12]+rear[4*i+1]*rear[13]+rear[4*i+2]*rear[14]);
    for(int k=0;k<3;++k) {
        float sign=k?-1.f:1.f;
        cam->origin[k]=sign*(front[12+k]+world[0]*front[k]+world[1]*front[4+k]+world[2]*front[8+k]);
        if(!isfinite(cam->origin[k]) || fabsf(cam->origin[k])>10)return 0;
        for(int j=0;j<3;++j)cam->axis[j][k]=sign*(front[k]*rear[j]+front[4+k]*rear[4+j]+front[8+k]*rear[8+j]);
    }
    for(int j=0;j<3;++j)if(!(fabsf(quest_mirror_dot(cam->axis[j],cam->axis[j])-1)<.015f) ||
        !(fabsf(quest_mirror_dot(cam->axis[j],cam->axis[(j+1)%3]))<.015f))return 0;
    return 1;
}
/* Cabin -> rear clip space, with exactly the retail capture's depth A+B/Z.
 * The added cabin must write the same depth as the body and traffic so both
 * the occlusion and the per-eye mirror reprojection agree. */
static inline int quest_mirror_capture_matrix(float out[16],const QuestMirrorCamera *cam,const float projection[4])
{
    if(!(projection[0]>.01f && projection[1]>.01f && projection[2]>=1.f && projection[3]<0.f))return 0;
    float matrix[16];
    for(int row=0;row<4;++row) {
        int axis=row<2?row:2;
        float scale=row<2?projection[row]:row==2?projection[2]:1.f;
        for(int col=0;col<3;++col)matrix[4*col+row]=scale*cam->axis[axis][col];
        matrix[12+row]=-scale*quest_mirror_dot(cam->axis[axis],cam->origin)+(row==2?projection[3]:0);
    }
    for(int i=0;i<16;++i)if(!isfinite(matrix[i]))return 0;
    memcpy(out,matrix,sizeof matrix);return 1;
}
/* Match the rigid player's shell in the mono rear capture. Guest constants
 * contain transposed clip rows and the pre-Vulkan Y sign. Never classify a
 * nearby peer or a wheel merely by distance from the camera. */
static inline int quest_mirror_own_car(const float m[16],const QuestMirrorCamera *cam,
                                     const float projection[4],const float seat[3])
{
    float origin[3]={seat[1]-cam->origin[0],-seat[2]-cam->origin[1],seat[0]-cam->origin[2]};
    const int axes[3]={2,0,1};const float signs[3]={-1,-1,1};
    const int rows[3]={0,1,3};
    for(int i=0;i<3;++i) {
        int row=rows[i],axis=i;
        float scale=i==0?projection[0]:i==1?-projection[1]:1.f;
        if(!isfinite(scale) || fabsf(scale)<.01f)return 0;
        for(int col=0;col<4;++col) {
            float expected=col<3?signs[col]*cam->axis[axis][axes[col]]:quest_mirror_dot(cam->axis[axis],origin);
            float delta=m[row*4+col]/scale-expected;
            if(!isfinite(delta) || fabsf(delta)>(col==3?.025f:.008f))return 0;
        }
    }
    return 1;
}
static inline void quest_mirror_release(QuestMirrorGrip *g)
{
    g->hand=0;g->blocked[0]=g->blocked[1]=1;g->rebase=1;
}
static inline float quest_mirror_contact_distance(float local[3],const float hand[3],const QuestMirrorMount *m,QuestMirrorAngle a)
{
    float diff[3];for(int k=0;k<3;++k)diff[k]=hand[k]-m->center[k];
    quest_mirror_rotate(local,diff,a,1);
    float squared=0;
    for(int k=0;k<3;++k) {
        float p=quest_mirror_bound(local[k],m->half[k]);
        float d=p-local[k];squared+=d*d;local[k]=p;
    }
    return squared;
}
/* A wrist turn and a push at the grabbed point both rotate the mirror.
 * Keep the lever arm fixed for the grab so tiny changes in the tracked
 * position or in the fitted mount cannot switch the direction of rotation. */
static inline void quest_mirror_grip(QuestMirrorGrip *g,QuestMirrorSetting *settings,
    const QuestMirrorMount mount[3],uint32_t now,const int valid[2],const float pose[2][16],
    const float grip[2],const int busy[2])
{
    for(int h=0;h<2;++h)if(grip[h]<.15f) {
        if(g->hand==h+1)g->hand=0;
        g->blocked[h]=0;
    }
    if(!g->hand) {
        float best=.075f*.075f;
        for(int h=0;h<2;++h) {
            if(busy[h] || !valid[h] || g->blocked[h] || !(grip[h]>=.65f))continue;
            for(int p=0;p<QUEST_MIRRORS;++p)if(mount[p].valid) {
                float contact[3],d=quest_mirror_contact_distance(contact,pose[h]+12,&mount[p],quest_mirror_angle(settings,p));
                if(d<best) {
                    best=d;g->hand=h+1;g->part=p;g->rebase=1;
                    memcpy(g->contact,contact,sizeof contact);
                }
            }
        }
    }
    if(!g->hand)return;
    int h=g->hand-1,p=g->part;
    if(!mount[p].valid || busy[h]) { quest_mirror_release(g);return; }
    if(!isfinite(grip[h])) {
        g->rebase=1;if(now-g->tick>=500)quest_mirror_release(g);return;
    }
    uint32_t elapsed=now-g->tick;
    g->tick=now;
    if(!valid[h]) { g->rebase=1;return; }
    const float *cur=pose[h],*old=g->previous;
    QuestMirrorAngle *a=p==0?&settings->inside:&settings->outside;
    float step[3];for(int k=0;k<3;++k)step[k]=cur[12+k]-old[12+k];
    float trace=0;for(int k=0;k<3;++k)trace+=quest_mirror_dot(old+4*k,cur+4*k);
    if(g->rebase || elapsed>250 || trace<2.4f || quest_mirror_dot(step,step)>.10f*.10f) {
        memcpy(g->anchor,cur,sizeof g->anchor);g->start=quest_mirror_angle(settings,p);
    } else {
        const float *anchor=g->anchor;
        float motion[3],arm[3],spin[3]={0};
        quest_mirror_rotate(arm,g->contact,g->start,0);
        for(int k=0;k<3;++k) {
            motion[k]=cur[12+k]-anchor[12+k];
            int i=(k+1)%3,j=(k+2)%3;
            for(int col=0;col<3;++col)
                spin[k]+=.5f*(anchor[4*col+i]*cur[4*col+j]-anchor[4*col+j]*cur[4*col+i]);
        }
        float leverage=.30f/(quest_mirror_dot(arm,arm)+.085f*.085f);
        float yaw=.45f*spin[1]+leverage*(arm[2]*motion[0]-arm[0]*motion[2]);
        float pitch=.45f*spin[0]+leverage*(arm[1]*motion[2]-arm[2]*motion[1]);
        QuestMirrorAngle target=quest_mirror_trim(g->start.yaw+yaw,g->start.pitch+pitch);
        if(p==QUEST_MIRROR_LEFT)target.yaw=-target.yaw;
        /* Filter toward an absolute angle: no accumulating drift or minimum
         * input step. Keep the same 35 ms response at different frame rates. */
        float gain=-expm1f(-(float)elapsed/35.f);
        a->yaw+=gain*(target.yaw-a->yaw);
        a->pitch+=gain*(target.pitch-a->pitch);
    }
    memcpy(g->previous,cur,16*sizeof(float));g->rebase=0;
}
