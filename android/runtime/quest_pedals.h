#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    float travel[3]; /* accelerator, brake, clutch, 0..1 */
    uint32_t tick,shift_tick,epoch;
    int active,gear,pulse;
} QuestPedals;

/* The triggers are absolute positions. The clutch follows the H lever
 * through neutral and completes a short stroke on a direct or sequential
 * shift, including a shift that completes between consecutive XR frames. */
static inline void quest_pedals_update(QuestPedals *p,uint32_t now,int driving,
    uint8_t gas,uint8_t brake,uint32_t epoch,int gear,int moving,int selected)
{
    if(!driving) { memset(p,0,sizeof *p);return; }
    int fresh=!p->active || epoch!=p->epoch || now-p->tick>=500;
    if(fresh) { p->pulse=0;p->travel[2]=0; }
    float dt=fresh?0.f:fminf(.10f,.001f*(now-p->tick));
    p->travel[0]=gas/255.f;p->travel[1]=brake/255.f;
    if(gear<0) { p->pulse=0;p->travel[2]=0; }
    else {
        if(selected || (!fresh && p->gear>=0 && p->gear!=gear)) {
            p->shift_tick=now;p->pulse=1;
        }
        if(now-p->shift_tick>=130)p->pulse=0;
        p->travel[2]=moving || p->pulse?fminf(1,p->travel[2]+dt/.055f):
                                          fmaxf(0,p->travel[2]-dt/.18f);
    }
    p->active=1;p->epoch=epoch;p->gear=gear;p->tick=now;
}
