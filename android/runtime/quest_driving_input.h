#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t tick; int driving,armed,pulse; uint32_t action_tick; int action_armed,action; } QuestShiftInput;

/* Tap B/Circle for the VR timeline. A deliberate hold still reaches the
 * retail replay controls. Arm on release when entering or regaining focus. */
typedef struct { uint32_t since;int armed,pressed; } QuestReplayButton;
static inline int quest_replay_button(QuestReplayButton *b,int active,uint32_t now,uint8_t *button)
{
    if(!active){*b=(QuestReplayButton){0};return 0;}
    if(*button<128) {
        int toggle=b->pressed && now-b->since<500;
        b->pressed=0;b->armed=1;return toggle;
    }
    if(b->armed && !b->pressed){b->pressed=1;b->since=now;}
    if(!b->pressed || now-b->since<500)*button=0;
    return 0;
}

/* A complete right-stick click advances one replay view. A source change,
 * tracking loss, or the two-stick recenter chord requires a fresh release. */
typedef struct { int source,armed,down; } QuestReplayClick;
static inline int quest_replay_click(QuestReplayClick *c,int source,int left,int right)
{
    if(c->source!=source)*c=(QuestReplayClick){.source=source};
    if(!source || left) {c->armed=0;c->down=0;return 0;}
    if(right) {c->down=c->armed;return 0;}
    int advance=c->down;
    c->armed=1;c->down=0;
    return advance;
}

/* Steam Link can reserve Touch Menu for the dashboard. During driving only,
 * defer Touch Y until a tap or hold can be distinguished. Latch ownership
 * across the resulting pause so the held Y never leaks into the menu. */
typedef struct { uint32_t down_at,pulse_at; int phase,pulse; } QuestPauseGesture;
enum { QUEST_Y_IDLE, QUEST_Y_TIMING, QUEST_Y_MENU, QUEST_Y_RELEASE };
enum { QUEST_Y_TAP=1, QUEST_Y_START=2 };
static inline int quest_steamvr_y(QuestPauseGesture *s,uint32_t now,int valid,int driving,int down)
{
    if(!valid) { s->phase=QUEST_Y_RELEASE;s->pulse=0;return 0; }
    if(now-s->pulse_at>=(s->pulse==QUEST_Y_START?200u:100u) || (s->pulse==QUEST_Y_TAP && !driving))s->pulse=0;
    if(s->phase==QUEST_Y_IDLE && down) {
        s->phase=driving?QUEST_Y_TIMING:QUEST_Y_MENU;s->down_at=now;
    }
    if(s->phase==QUEST_Y_TIMING) {
        if(!driving)s->phase=QUEST_Y_RELEASE;
        else if(now-s->down_at>=650 || !down) {
            s->pulse=now-s->down_at>=650?QUEST_Y_START:QUEST_Y_TAP;
            s->pulse_at=now;s->phase=QUEST_Y_RELEASE;
        }
    }
    if(s->phase==QUEST_Y_MENU && driving)s->phase=QUEST_Y_RELEASE;
    int result=s->pulse;
    if(s->phase==QUEST_Y_MENU && down)result|=QUEST_Y_TAP;
    if(!down)s->phase=QUEST_Y_IDLE;
    return result;
}

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
