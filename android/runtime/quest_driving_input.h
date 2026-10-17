#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t tick; int driving,armed,pulse; uint32_t action_tick; int action_armed,action; } QuestShiftInput;

/* Xbox retail layout: A handbrake, B nitrous, X upshift, Y downshift.
 * Touch A/B swap only in gameplay. Keep menu buttons and D-pad navigation.
 * A shift is one 100-ms button pulse, rearmed only near stick center. */
static inline void quest_driving_input(QuestShiftInput *s,uint32_t now,int driving,
                                       uint16_t *buttons,uint8_t analog[8],int16_t sticks[4])
{
    int x=sticks[2],y=sticks[3];
    if(!driving) {
        memset(s,0,sizeof *s);
        if(x< -16000)*buttons|=4; if(x>16000)*buttons|=8;
        if(y< -16000)*buttons|=2; if(y>16000)*buttons|=1;
        return;
    }
    if(!s->driving) { memset(s,0,sizeof *s);s->driving=1; }
    uint8_t a=analog[0];analog[0]=analog[1];analog[1]=a;
    int released=s->pulse && (uint32_t)(now-s->tick)>=100;
    if(released) s->pulse=0;
    if(abs(y)<8192) s->armed=1;
    if(!released && !s->pulse && s->armed && abs(y)>18000 && abs(y)>abs(x)) {
        s->pulse=y>0?1:-1;s->tick=now;s->armed=0;
    }
    if(s->pulse) analog[s->pulse>0?2:3]=255;
    /* Xbox career actions: D-pad right opens SMS, up enters an event/shop.
     * One pulse per horizontal deflection, with dominant-axis selection to
     * avoid shifting and opening SMS at the same time. */
    int action_released=s->action && (uint32_t)(now-s->action_tick)>=100;
    if(action_released)s->action=0;
    if(abs(x)<8192)s->action_armed=1;
    if(!action_released && !s->action && s->action_armed && abs(x)>18000 && abs(x)>abs(y)) {
        s->action=x<0?8:1;s->action_tick=now;s->action_armed=0;
    }
    *buttons|=(uint16_t)s->action;
    /* The retail right axis can also apply throttle/brake. Consume it, so
     * a downshift cannot brake and an upshift cannot accelerate the car. */
    sticks[2]=sticks[3]=0;
}
