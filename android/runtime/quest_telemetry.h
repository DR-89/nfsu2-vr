#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quest_bitmap_font.h"
enum { QUEST_TELEMETRY_SAMPLES=120,QUEST_TELEMETRY_WIDTH=320,QUEST_TELEMETRY_HEIGHT=180 };
typedef struct {
    float values[QUEST_TELEMETRY_SAMPLES][3],speed,rpm,accel,brake;
    uint32_t car,tick,sample_tick;unsigned head,count;
} QuestTelemetry;
static inline void quest_telemetry_sample(QuestTelemetry *s,uint32_t now,uint32_t car,float rpm,float limit,float speed,float brake)
{
    if(!car || !isfinite(rpm) || !isfinite(limit) || limit<=0 || !isfinite(speed) || !isfinite(brake)) {
        memset(s,0,sizeof *s);return;
    }
    if(car!=s->car || now-s->tick>250){memset(s,0,sizeof *s);s->car=car;s->tick=now;s->speed=speed;}
    if(s->count && now-s->sample_tick<50){s->tick=now;return;}
    uint32_t dt=now-s->sample_tick;
    float acceleration=s->count && dt>0?(speed-s->speed)*1000.f/dt:0;
    s->accel=fmaxf(-12,fminf(12,acceleration));s->speed=speed;
    s->rpm=rpm;s->brake=fmaxf(0,fminf(1,brake));s->tick=s->sample_tick=now;
    float *p=s->values[s->head];p[0]=fmaxf(0,fminf(1,rpm/limit));p[1]=.5f+s->accel/24.f;p[2]=s->brake;
    s->head=(s->head+1)%QUEST_TELEMETRY_SAMPLES;if(s->count<QUEST_TELEMETRY_SAMPLES)++s->count;
}
static inline void quest_telemetry_pixel(uint8_t *pixels,int x,int y,uint32_t rgb)
{
    if(x<0 || x>=320 || y<0 || y>=180)return;
    uint8_t *p=pixels+4*(y*320+x);p[0]=(uint8_t)(rgb>>16);p[1]=(uint8_t)(rgb>>8);p[2]=(uint8_t)rgb;p[3]=255;
}
static inline void quest_telemetry_text(uint8_t *p,int x,int y,const char *text,uint32_t rgb)
{
    for(;*text;++text,x+=6)for(unsigned c=0;c<sizeof quest_vr_font/sizeof *quest_vr_font;++c)if(quest_vr_font[c].c==*text) {
        for(int row=0;row<7;++row){char hex[]={quest_vr_font[c].rows[row*2],quest_vr_font[c].rows[row*2+1],0};
            unsigned bits=(unsigned)strtoul(hex,0,16);for(int col=0;col<5;++col)if(bits&(1u<<(4-col)))quest_telemetry_pixel(p,x+col,y+row,rgb);}
        break;
    }
}
static inline void quest_telemetry_render(const QuestTelemetry *s,uint32_t now,uint8_t *pixels)
{
    for(int y=0;y<180;++y)for(int x=0;x<320;++x)quest_telemetry_pixel(pixels,x,y,0x07121C);
    int live=s->car && s->count && now-s->tick<250;
    quest_telemetry_text(pixels,12,8,live?"LIVE TELEMETRY / 6 SECONDS":"TELEMETRY / NO LIVE DATA",0xDBEFF7);
    const uint32_t colors[]={0xFF6854,0x51DFAC,0x599FFF};
    for(int channel=0;channel<3;++channel) {
        int top=35+channel*48;char label[64];
        if(channel==0)snprintf(label,sizeof label,"RPM %.0f",live?s->rpm:0);
        if(channel==1)snprintf(label,sizeof label,"ACCEL %+.1f M/S2",live?s->accel:0);
        if(channel==2)snprintf(label,sizeof label,"BRAKE %.0f%%",live?s->brake*100:0);
        quest_telemetry_text(pixels,12,top-11,label,colors[channel]);
        for(int x=12;x<308;++x)for(int y=0;y<3;++y)quest_telemetry_pixel(pixels,x,top+y*14,0x203643);
        for(int x=12;x<308;x+=49)for(int y=0;y<=28;++y)quest_telemetry_pixel(pixels,x,top+y,0x172C39);
        if(!live)continue;
        int previous=-1;
        for(int x=0;x<296;++x) {
            unsigned age=(unsigned)((295-x)*(QUEST_TELEMETRY_SAMPLES-1)/295);
            if(age>=s->count)continue;
            unsigned index=(s->head+QUEST_TELEMETRY_SAMPLES-1-age)%QUEST_TELEMETRY_SAMPLES;
            int y=top+28-(int)lrintf(s->values[index][channel]*28);
            int from=previous<0?y:previous;for(int k=from<y?from:y;k<=(from>y?from:y);++k)quest_telemetry_pixel(pixels,x+12,k,colors[channel]);
            previous=y;
        }
    }
    quest_telemetry_text(pixels,12,171,"PAGE: VIDEO / MUSIC <-> TELEMETRY",0x8FA8B8);
}
