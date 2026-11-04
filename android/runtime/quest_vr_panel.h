#pragma once
#include "quest_vr_settings.h"

#include "quest_bitmap_font.h"

static inline void quest_vr_rect(uint32_t *pixels,int x,int y,int w,int h,uint32_t color)
{
    for(int j=y;j<y+h && j<768;++j)for(int i=x;i<x+w && i<1024;++i)
        if(i>=0 && j>=0)pixels[j*1024+i]=color;
}
static inline void quest_vr_text(uint32_t *pixels,int x,int y,int scale,const char *text,uint32_t color)
{
    for(;*text;++text,x+=6*scale) {
        char c=*text;if(c>='a' && c<='z')c-=32;
        const char *rows=NULL;
        for(unsigned i=0;i<sizeof quest_vr_font/sizeof *quest_vr_font;++i)
            if(quest_vr_font[i].c==c){rows=quest_vr_font[i].rows;break;}
        if(!rows)continue;
        for(int j=0;j<7;++j) {
            char hex[3]={rows[j*2],rows[j*2+1],0};unsigned bits=(unsigned)strtoul(hex,NULL,16);
            for(int i=0;i<5;++i)if(bits&(1u<<(4-i)))quest_vr_rect(pixels,x+i*scale,y+j*scale,scale,scale,color);
        }
    }
}
static inline void quest_vr_panel(uint32_t *pixels,const QuestVRSettings *s,
                                  const QuestVRMenu *m,int saved,int resolution_pending)
{
    quest_vr_rect(pixels,0,0,1024,768,0xff141b24u);
    quest_vr_rect(pixels,0,0,1024,7,0xff4da3ffu);
    if(!m->open) {
        quest_vr_text(pixels,44,26,4,"[X] VR SETTINGS",0xffffffffu);return;
    }
    quest_vr_text(pixels,40,34,5,"VR SETTINGS",0xffffffffu);
    const char *labels[]={"STEERING SENSITIVITY","WHEEL ROTATION","SEAT HEIGHT","SEAT DISTANCE","RENDER RESOLUTION","WHEEL RETURN SPEED","RESET CAR MIRRORS","RESET DEFAULTS","SAVE AND BACK"};
    for(int row=0;row<QUEST_VR_ROWS;++row) {
        int y=112+row*50;char value[64]={0};
        if(row==m->row)quest_vr_rect(pixels,24,y-12,976,48,0xff214171u);
        quest_vr_text(pixels,40,y,3,labels[row],0xffffffffu);
        if(row==0)snprintf(value,sizeof value,"< %d%% >",(int)lrintf(s->sensitivity*100));
        if(row==1)snprintf(value,sizeof value,"< %d DEG >",(int)lrintf(s->rotation));
        if(row==2)snprintf(value,sizeof value,"< %+.0f CM >",s->height*100);
        if(row==3)snprintf(value,sizeof value,"< %+.0f CM >",s->distance*100);
        if(row==4) {
            if(!s->resolution)strcpy(value,"< DEFAULT >");
            else snprintf(value,sizeof value,"< %uX%u >",quest_vr_resolutions[s->resolution][0],quest_vr_resolutions[s->resolution][1]);
        }
        if(row==QUEST_VR_RESET_MIRRORS_ROW && m->mirrors_centered)strcpy(value,"CENTERED");
        if(row==QUEST_VR_RETURN_ROW) {
            int width=(int)lrintf(184*s->return_speed/QUEST_WHEEL_RETURN_MAX);
            quest_vr_rect(pixels,518,y+8,184,5,0xff465566u);
            quest_vr_rect(pixels,518,y+8,width,5,0xff4da3ffu);
            quest_vr_rect(pixels,514+width,y,8,21,0xffffffffu);
            if(s->return_speed>0)snprintf(value,sizeof value,"%.0f DEG/S",s->return_speed);
            else strcpy(value,"OFF");
        }
        quest_vr_text(pixels,row==QUEST_VR_RETURN_ROW?734:620,y,3,value,0xffffd79fu);
    }
    quest_vr_text(pixels,40,568,2,"ROTATION IS TOTAL LOCK-TO-LOCK. HIGHER SENSITIVITY = QUICKER TURNS.",0xffc8beb4u);
    quest_vr_text(pixels,40,600,2,"SEAT: POSITIVE HEIGHT = UP. POSITIVE DISTANCE = FARTHER BACK.",0xffc8beb4u);
    quest_vr_text(pixels,40,632,2,resolution_pending?"RESOLUTION CHANGED: SAVE, THEN RESTART THE GAME.":"RESOLUTION CHANGES TAKE EFFECT ON THE NEXT GAME START.",0xffffd79fu);
    quest_vr_text(pixels,40,686,3,"STICK / D-PAD: SELECT + ADJUST   A: SELECT",0xffffffffu);
    quest_vr_text(pixels,40,726,2,saved<0?"SAVE FAILED - CHECK FREE SPACE. B: BACK":"B: SAVE AND BACK TO PAUSE MENU",saved<0?0xff8888ffu:0xffc8beb4u);
}
