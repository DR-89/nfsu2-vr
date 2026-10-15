#pragma once
#include <math.h>
#include "quest_wheel_limits.h"
#include <stdint.h>
#include <string.h>

/* Retail car space: +X forward, +Y left, +Z up. Rotation rows are local
 * axes in world space; position is Car+0x50 (the render origin). The camera
 * consumes a row-vector world -> camera matrix, +Y down, +Z forward. */
static inline int quest_cockpit_view(float out[16], const float rotation[12],
                                     const float position[3], const float seat[3])
{
    float axes[3][3], eye[3], result[16]={0};
    for (int a=0;a<3;++a) {
        float length=0;
        for (int c=0;c<3;++c) {
            float v=rotation[a*4+c];
            if (!isfinite(v)) return 0;
            axes[a][c]=v; length+=v*v;
        }
        if (length<0.8f || length>1.2f) return 0;
        length=sqrtf(length);
        for (int c=0;c<3;++c) axes[a][c]/=length;
    }
    for (int a=0;a<3;++a) for (int b=a+1;b<3;++b) {
        float dot=0;
        for (int c=0;c<3;++c) dot+=axes[a][c]*axes[b][c];
        if (fabsf(dot)>0.02f) return 0;
    }
    float determinant=axes[0][0]*(axes[1][1]*axes[2][2]-axes[1][2]*axes[2][1])
        -axes[0][1]*(axes[1][0]*axes[2][2]-axes[1][2]*axes[2][0])
        +axes[0][2]*(axes[1][0]*axes[2][1]-axes[1][1]*axes[2][0]);
    if(determinant<0.98f) return 0;
    for (int c=0;c<3;++c) {
        if (!isfinite(position[c]) || !isfinite(seat[c]) || fabsf(position[c])>1000000 || fabsf(seat[c])>5) return 0;
        eye[c]=position[c];
        for (int a=0;a<3;++a) eye[c]+=seat[a]*axes[a][c];
        result[c*4]=-axes[1][c];
        result[c*4+1]=-axes[2][c];
        result[c*4+2]=axes[0][c];
    }
    for (int a=0;a<3;++a) for (int c=0;c<3;++c) result[12+a]-=eye[c]*result[c*4+a];
    result[15]=1;
    memcpy(out,result,sizeof result);
    return 1;
}

/* Preserve ordinary camera clicks while consuming the recenter chord. There
 * is no cockpit-specific button or long-press action. */
typedef struct { uint32_t pulse; int held, consumed, pulsing; } QuestCameraButton;

/* Driving uses profile slots 0..3, or 0..4 in special retail modes. Insert a
 * native cockpit at the wrap without storing an invalid slot in the profile.
 * 1: enter/consume, 2: exit/run retail, 3: exit/consume, 0: ordinary retail.
 * Remember the entry direction so reversing from cockpit returns to its
 * adjacent view instead of skipping one. */
static inline int quest_cockpit_cycle(unsigned slot,unsigned last,int direction,
                                      int selected,int ready,int *entry_direction)
{
    if(direction!=1 && direction!=-1) return 0;
    if(selected) return direction==*entry_direction ? 2 : 3;
    if(ready && last>=3 && last<=4 && slot<=last &&
       ((direction==1 && slot==last) || (direction==-1 && slot==0))) {
        *entry_direction=direction;
        return 1;
    }
    return 0;
}

/* Column-major rigid wheel rotation about its fitted hub/shaft. The shaft
 * points toward the driver; positive steering is clockwise (right turn). */
static inline void quest_cockpit_wheel(float out[16],const float pivot[3],
                                        const float axis[3],float steering)
{
    if(steering>1) steering=1;
    if(steering< -1) steering=-1;
    /* Half a revolution from center to either lock (360 degrees total). */
    float angle=-steering*QUEST_WHEEL_LOCK_RADIANS, c=cosf(angle),s=sinf(angle),t=1-c;
    float x=axis[0],y=axis[1],z=axis[2];
    const float r[16]={t*x*x+c,t*x*y+s*z,t*x*z-s*y,0,
                       t*x*y-s*z,t*y*y+c,t*y*z+s*x,0,
                       t*x*z+s*y,t*y*z-s*x,t*z*z+c,0,0,0,0,1};
    memcpy(out,r,sizeof r);
    for(int row=0;row<3;++row) {
        out[12+row]=pivot[row];
        for(int col=0;col<3;++col) out[12+row]-=r[col*4+row]*pivot[col];
    }
}
static inline float quest_cockpit_steering_smooth(float current,float target,float seconds)
{
    if(seconds<=0) return current;
    if(seconds>.25f) return target;
    return current+(target-current)*(1-expf(-seconds/.045f));
}
static inline void quest_cockpit_multiply(float out[16],const float a[16],const float b[16])
{
    float r[16];
    for(int col=0;col<4;++col) for(int row=0;row<4;++row) {
        float v=0;
        for(int k=0;k<4;++k) v+=a[k*4+row]*b[col*4+k];
        r[col*4+row]=v;
    }
    memcpy(out,r,sizeof r);
}

/* Keep the existing race information inside the windshield, above the dash.
 * The original 2D clip/scissor and all non-cockpit cameras are unchanged. */
static inline void quest_cockpit_hud(float matrix[16])
{
    for(int r=0;r<4;++r) {
        matrix[12+r]+=matrix[4+r]*.60f;
        matrix[r]*=1.4f/1.8f;
        matrix[4+r]*=.85f/1.35f;
    }
}
static inline int quest_camera_button(QuestCameraButton *s, uint32_t now,int right,int left)
{
    if (right && !s->held) { s->consumed=0; s->held=1; }
    if (left && right) { s->consumed=1; s->pulsing=0; }
    if (!right && s->held) {
        if (!s->consumed) { s->pulse=now; s->pulsing=1; }
        s->held=0;
    }
    if (s->pulsing && now-s->pulse>=100) s->pulsing=0;
    return s->pulsing;
}

typedef struct { QuestCameraButton camera; int chord; } QuestDrivingButtons;
/* Return Xbox ANALOG button bits: White changes camera, Black looks back.
 * Xbox thumb clicks do not trigger those retail actions. Consume a recenter
 * chord until BOTH physical clicks are released. */
static inline uint8_t quest_driving_buttons(QuestDrivingButtons *s,uint32_t now,int left,int right)
{
    if(left && right) s->chord=1;
    if(s->chord) {
        memset(&s->camera,0,sizeof s->camera);
        if(!left && !right) s->chord=0;
        return 0;
    }
    return (quest_camera_button(&s->camera,now,left,0)?0x20:0)|(right?0x10:0);
}
static inline void quest_driving_analog(QuestDrivingButtons *s,uint32_t now,
                                       int left,int right,int cockpit,uint8_t analog[8])
{
    uint8_t actions=quest_driving_buttons(s,now,left,right);
    analog[4]=!cockpit && (actions&0x10)?255:0;
    analog[5]=(actions&0x20)?255:0;
}
