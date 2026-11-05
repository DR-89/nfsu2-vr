#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>

/* Xbox motor channels describe heavy/light effects, not left/right hands.
 * Both Touch controllers receive the same combined effect. Keep small road
 * signals gentle, while retaining the title's full range for impacts. */
static inline float quest_haptic_amplitude(uint16_t low, uint16_t high)
{
    float a=low/65535.0f, b=high/65535.0f;
    float strength=a>b?a:b;
    return strength*(0.15f+0.85f*strength);
}

static inline float quest_engine_rumble(float rpm,float idle,float limit,float throttle)
{
    if(!isfinite(rpm) || rpm<=0 || !isfinite(idle) || idle<=0 || !isfinite(limit) || limit<=idle)return 0;
    float rev=fminf(1,fmaxf(0,(rpm-idle)/(limit-idle)));
    float load=isfinite(throttle)?fminf(1,fmaxf(0,throttle)):0;
    return .025f*fminf(1,rpm/idle)+rev*(.14f+.14f*load);
}

static inline float quest_mix_rumble(float road,float engine,int allow_engine)
{
    return allow_engine?road+(1-road)*engine:road;
}

typedef struct {
    float current, peak;
    uint32_t report_tick, peak_tick;
    int focused, received;
} QuestHaptics;

static inline void quest_haptics_focus(QuestHaptics *h, int focused)
{
    if(h->focused!=focused) {
        memset(h,0,sizeof *h);
        h->focused=focused;
    }
}

/* Called under the XR input mutex, including unchanged USB reports. A short
 * impact followed by zero between rendered frames must still be felt. */
static inline void quest_haptics_report(QuestHaptics *h, uint32_t now,
                                        uint16_t low, uint16_t high)
{
    if(!h->focused) return;
    float a=quest_haptic_amplitude(low,high);
    if(!h->received || a>h->peak || (uint32_t)(now-h->peak_tick)>=65) {
        h->peak=a; h->peak_tick=now;
    }
    h->current=a; h->report_tick=now; h->received=1;
}

static inline float quest_haptics_sample(QuestHaptics *h, uint32_t now)
{
    if(!h->focused || !h->received) return 0;
    if((uint32_t)(now-h->report_tick)>=250) {
        h->current=h->peak=0; h->received=0;
        return 0;
    }
    float peak=(uint32_t)(now-h->peak_tick)<65?h->peak:0;
    return h->current>peak?h->current:peak;
}
