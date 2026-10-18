#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "quest_wheel_limits.h"
typedef struct {
    float pivot[3], axis[3], right[3], up[3], radius;
    float angle, previous[2], anchor[2], limit, sensitivity;
    int grabbed[2], blocked[2], latched, rebase[2];
    uint32_t squeeze_tick[2];
} QuestWheelGrip;

/* NTSC-U's pad filter at 0x001C2350 maps (axis + .5) / 32767.5 through
 * a 24% deadzone and rescales the remaining 76%. Use its inverse ONLY for
 * tracked wheel input; at 360 degrees, an uncorrected wheel otherwise has
 * roughly 43 degrees of dead travel on either side of center. */
static inline float quest_wheel_stick_steering(int16_t stick)
{
    float value=(stick+.5f)/32767.5f;
    return copysignf(fmaxf(0,(fabsf(value)-.24f)/.76f),value);
}
static inline int16_t quest_wheel_encode_steering(float steering)
{
    if(!isfinite(steering) || steering==0) return 0;
    float magnitude=fminf(1,fabsf(steering));
    float axis=copysignf(.24f+.76f*magnitude,steering)*32767.5f-.5f;
    return (int16_t)lrintf(axis);
}

static inline float quest_wheel_dot(const float a[3],const float b[3])
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static inline void quest_wheel_configure(QuestWheelGrip *w,const float pivot[3],const float axis[3],float radius)
{
    memset(w,0,sizeof *w);
    w->limit=QUEST_WHEEL_LOCK_RADIANS;w->sensitivity=1;
    memcpy(w->pivot,pivot,12); memcpy(w->axis,axis,12); w->radius=radius;
    float length=sqrtf(axis[2]*axis[2]+axis[0]*axis[0]);
    w->right[0]=axis[2]/length; w->right[2]=-axis[0]/length;
    w->up[0]=axis[1]*w->right[2];
    w->up[1]=axis[2]*w->right[0]-axis[0]*w->right[2];
    w->up[2]=-axis[1]*w->right[0];
}
static inline void quest_wheel_reset(QuestWheelGrip *w)
{
    for(int h=0;h<2;++h) { w->grabbed[h]=0; w->blocked[h]=1; w->rebase[h]=0; }
    w->latched=0;
}
/* Both hands contribute angular motion in the fitted rim plane. A new hand
 * takes the current angle as its baseline, so grabbing/releasing never jumps.
 * After the first grab, releasing both hands retains the angle until a new
 * grab, deliberate stick takeover, or cockpit/focus/recenter reset.
 * Pose loss, hub crossings and discontinuities suspend angular motion, not
 * the grip. On recovery rebase the angle without replaying unobserved motion.
 * Require a fresh squeeze after focus/recenter loss or prolonged button loss.
 * A nonfinite squeeze means unavailable input, not an observed release. */
static inline int quest_wheel_update(QuestWheelGrip *w,uint32_t now,int active,const int valid[2],
                                    const float position[2][3],const float squeeze[2],float fallback)
{
    if(!active) { quest_wheel_reset(w); return 0; }
    if(!w->latched) w->angle=fmaxf(-1,fminf(1,fallback/w->sensitivity))*w->limit;
    float delta=0; int contributors=0,acquired=0;
    for(int h=0;h<2;++h) {
        if(!isfinite(squeeze[h])) {
            if(w->grabbed[h]) {
                w->rebase[h]=1;
                if((uint32_t)(now-w->squeeze_tick[h])>=500) {
                    w->grabbed[h]=0;w->blocked[h]=1;
                }
            }
            continue;
        }
        w->squeeze_tick[h]=now;
        /* Observe an actual release even when the hand pose is unavailable. */
        if(squeeze[h]<.15f) { w->grabbed[h]=0; w->blocked[h]=0; w->rebase[h]=0; continue; }
        if(w->blocked[h]) continue;
        if(!valid[h]) { if(w->grabbed[h])w->rebase[h]=1; continue; }
        float p[3]; for(int k=0;k<3;++k) p[k]=position[h][k]-w->pivot[k];
        float x=quest_wheel_dot(p,w->right),y=quest_wheel_dot(p,w->up);
        float r=sqrtf(x*x+y*y),depth=quest_wheel_dot(p,w->axis);
        if(!isfinite(r)||!isfinite(depth)||r<.06f) {
            if(w->grabbed[h])w->rebase[h]=1;
            continue;
        }
        float theta=atan2f(y,x);
        if(!w->grabbed[h]) {
            if(squeeze[h]<.65f || fabsf(r-w->radius)>.12f || fabsf(depth)>.14f) continue;
            w->grabbed[h]=1; w->latched=1; w->previous[h]=theta; w->rebase[h]=0;
            acquired|=1<<h;
        } else if(w->rebase[h]) {
            w->previous[h]=theta;w->rebase[h]=0;
        } else {
            float d=theta-w->previous[h];
            if(d>QUEST_WHEEL_PI) d-=2*QUEST_WHEEL_PI;
            if(d< -QUEST_WHEEL_PI) d+=2*QUEST_WHEEL_PI;
            /* Crossing the hub or a tracking jump can flip atan2 abruptly.
             * Keep the grip and discard that delta, then resume from here. */
            if(fabsf(d)<=.8f) { delta-=d; ++contributors; }
            w->previous[h]=theta;
        }
    }
    if(contributors) w->angle=fmaxf(-w->limit,fminf(w->limit,w->angle+delta/contributors));
    /* Capture a wheel-local contact only on acquisition. Rebased tracking,
     * hub crossings, unequal two-hand motion and pushing beyond the steering
     * stop must not slide/teleport a held glove to another part of the rim.
     * Use the final angle so another hand moving in this same sample does
     * not offset the new contact. Release/regrip can choose any rim position. */
    for(int h=0;h<2;++h) if(acquired&(1<<h)) w->anchor[h]=w->previous[h]+w->angle;
    return w->grabbed[0]||w->grabbed[1];
}
/* Neutral/noisy stick input preserves the released wheel angle. A deliberate
 * deflection with both hands off the rim restores ordinary stick steering,
 * including spring-to-center, until the next grab. While grasped, hands win. */
static inline int16_t quest_wheel_input(QuestWheelGrip *w,int active,int16_t stick)
{
    if(active && w->latched && !w->grabbed[0] && !w->grabbed[1] &&
       (stick<=-8192 || stick>=8192)) {
        w->latched=0;
        w->angle=fmaxf(-1,fminf(1,quest_wheel_stick_steering(stick)/w->sensitivity))*w->limit;
    }
    if(!active || !w->latched) return stick;
    float steering=fmaxf(-1,fminf(1,w->angle/w->limit*w->sensitivity));
    return quest_wheel_encode_steering(steering);
}
/* A grasped palm follows the actual rendered wheel, including its smoothing.
 * Local +Y points toward the fingertips, +Z is the back of the hand. */
static inline void quest_wheel_hand_matrix(const QuestWheelGrip *w,int hand,float steering,float out[16])
{
    float theta=w->anchor[hand]-steering*QUEST_WHEEL_LOCK_RADIANS;
    float c=cosf(theta),s=sinf(theta),radial[3],tangent[3];
    for(int k=0;k<3;++k) { radial[k]=c*w->right[k]+s*w->up[k]; tangent[k]=-s*w->right[k]+c*w->up[k]; }
    memset(out,0,64); out[15]=1;
    for(int k=0;k<3;++k) {
        /* Wrist toward the driver's body, fingers forward around the rim,
         * back of hand radially outward. Canonical glove is a right hand. */
        out[k]=tangent[k]*(hand?-1:1); out[4+k]=-w->axis[k]; out[8+k]=radial[k];
        out[12+k]=w->pivot[k]+radial[k]*(w->radius+.027f)+w->axis[k]*.020f;
    }
}
