#pragma once
#include "quest_vr_settings.h"

/* Small original 5x7 bitmap alphabet; no runtime font or retail UI assets.
 * Render only on menu changes, not for each game frame. */
static const struct { char c; const char *rows; } quest_vr_font[]={
 {'A',"0E11111F111111"},{'B',"1E11111E11111E"},{'C',"0F10101010100F"},
 {'D',"1E11111111111E"},{'E',"1F10101E10101F"},{'F',"1F10101E101010"},
 {'G',"0F10101711110F"},{'H',"1111111F111111"},{'I',"0E04040404040E"},
 {'J',"0702020212120C"},{'K',"11121418141211"},{'L',"1010101010101F"},
 {'M',"111B1515111111"},{'N',"11191513111111"},{'O',"0E11111111110E"},
 {'P',"1E11111E101010"},{'Q',"0E11111115120D"},{'R',"1E11111E141211"},
 {'S',"0F10100E01011E"},{'T',"1F040404040404"},{'U',"1111111111110E"},
 {'V',"11111111110A04"},{'W',"11111115151B11"},{'X',"11110A040A1111"},
 {'Y',"11110A04040404"},{'Z',"1F01020408101F"},
 {'0',"0E11131519110E"},{'1',"040C040404040E"},{'2',"0E11010204081F"},
 {'3',"1E01010601011E"},{'4',"02060A121F0202"},{'5',"1F10101E01011E"},
 {'6',"0E10101E11110E"},{'7',"1F010204080808"},{'8',"0E11110E11110E"},
 {'9',"0E11110F01010E"},{'.',"00000000000C0C"},{'-',"0000001F000000"},
 {'+',"0004041F040400"},{':',"000C0C000C0C00"},{'/',"01010204081010"},
 {'<',"01020408040201"},{'>',"10080402040810"},{'%',"191A0204080B13"},
 {'[',"0E08080808080E"},{']',"0E02020202020E"}
};
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
    const char *labels[]={"STEERING SENSITIVITY","WHEEL ROTATION","SEAT HEIGHT","SEAT DISTANCE","RENDER RESOLUTION","RESET DEFAULTS","SAVE AND BACK"};
    for(int row=0;row<QUEST_VR_ROWS;++row) {
        int y=112+row*62;char value[64]={0};
        if(row==m->row)quest_vr_rect(pixels,24,y-12,976,52,0xff214171u);
        quest_vr_text(pixels,40,y,3,labels[row],0xffffffffu);
        if(row==0)snprintf(value,sizeof value,"< %d%% >",(int)lrintf(s->sensitivity*100));
        if(row==1)snprintf(value,sizeof value,"< %d DEG >",(int)lrintf(s->rotation));
        if(row==2)snprintf(value,sizeof value,"< %+.0f CM >",s->height*100);
        if(row==3)snprintf(value,sizeof value,"< %+.0f CM >",s->distance*100);
        if(row==4) {
            if(!s->resolution)strcpy(value,"< DEFAULT >");
            else snprintf(value,sizeof value,"< %uX%u >",quest_vr_resolutions[s->resolution][0],quest_vr_resolutions[s->resolution][1]);
        }
        quest_vr_text(pixels,620,y,3,value,0xffffd79fu);
    }
    quest_vr_text(pixels,40,568,2,"ROTATION IS TOTAL LOCK-TO-LOCK. HIGHER SENSITIVITY = QUICKER TURNS.",0xffc8beb4u);
    quest_vr_text(pixels,40,600,2,"SEAT: POSITIVE HEIGHT = UP. POSITIVE DISTANCE = FARTHER BACK.",0xffc8beb4u);
    quest_vr_text(pixels,40,632,2,resolution_pending?"RESOLUTION CHANGED: SAVE, THEN RESTART THE GAME.":"RESOLUTION CHANGES TAKE EFFECT ON THE NEXT GAME START.",0xffffd79fu);
    quest_vr_text(pixels,40,686,3,"STICK / D-PAD: SELECT + ADJUST   A: SELECT",0xffffffffu);
    quest_vr_text(pixels,40,726,2,saved<0?"SAVE FAILED - CHECK FREE SPACE. B: BACK":"B: SAVE AND BACK TO PAUSE MENU",saved<0?0xff8888ffu:0xffc8beb4u);
}
