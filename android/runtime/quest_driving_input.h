#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int driving; uint32_t action_tick; int action_armed,action,menu_direction; uint32_t menu_tick; } QuestShiftInput;

/* One dominant direction with hysteresis. VR thumbsticks need not reach
 * the old 55% throw, and small diagonal wobble must not emit two menu keys. */
static inline int quest_stick_direction(int x,int y,int previous)
{
    const int on=11500,off=6500;
    if(abs(x)<off && abs(y)<off)return 0;
    if(abs(x)<on && abs(y)<on)return previous;
    return abs(y)>=abs(x)?(y>0?1:2):(x<0?4:8);
}

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
 * Right-stick directions send career actions, never gear buttons. */
static inline void quest_driving_input(QuestShiftInput *s,uint32_t now,int driving,
                                       uint16_t *buttons,uint8_t analog[8],int16_t sticks[4])
{
    int x=sticks[2],y=sticks[3];
    if(!driving) {
        if(s->driving)memset(s,0,sizeof *s);
        if(abs(sticks[0])+abs(sticks[1])>abs(x)+abs(y)){x=sticks[0];y=sticks[1];}
        int direction=*buttons&15;
        if(!direction)direction=quest_stick_direction(x,y,s->menu_direction);
        /* Stretch quick flicks/D-pad taps across the guest USB poll. Holding
         * still reaches retail auto-repeat. Consume the source axis so the
         * same gesture cannot also arrive as an analog navigation event. */
        if(direction && direction!=s->menu_direction) {
            s->menu_direction=direction;s->menu_tick=now;
        } else if(!direction && now-s->menu_tick>=100)s->menu_direction=0;
        *buttons=(*buttons&~15u)|(uint16_t)s->menu_direction;
        memset(sticks,0,4*sizeof *sticks);
        return;
    }
    if(!s->driving) { memset(s,0,sizeof *s);s->driving=1; }
    uint8_t a=analog[0];analog[0]=analog[1];analog[1]=a;
    /* Xbox D-pad up challenges a rival / enters an event or shop; down
     * opens career status. Preserve left=SMS and right=event shortcuts.
     * One pulse per deflection; both axes must center before another. */
    int action_released=s->action && (uint32_t)(now-s->action_tick)>=100;
    if(action_released)s->action=0;
    if(abs(x)<6500 && abs(y)<6500)s->action_armed=1;
    int direction=quest_stick_direction(x,y,0);
    if(!action_released && !s->action && s->action_armed && direction) {
        s->action=direction==4?8:direction==8?1:direction;
        s->action_tick=now;s->action_armed=0;
    }
    *buttons|=(uint16_t)s->action;
    /* The retail right axis can also apply throttle/brake. Consume it, so
     * a career action cannot brake or accelerate the car. */
    sticks[2]=sticks[3]=0;
}
