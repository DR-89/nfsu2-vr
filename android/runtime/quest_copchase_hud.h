#pragma once
#include <string.h>
#include "quest_gps.h"
#include "quest_copchase.h"
enum { QUEST_COPCHASE_LINES=3, QUEST_COPCHASE_TEXT=32,
       QUEST_COPCHASE_VERTICES=QUEST_COPCHASE_LINES*(QUEST_COPCHASE_TEXT+1)*6 };
static inline void quest_copchase_text(char text[QUEST_COPCHASE_LINES][QUEST_COPCHASE_TEXT],const QuestCopChaseHUD *s)
{
    memset(text,0,QUEST_COPCHASE_LINES*QUEST_COPCHASE_TEXT);
    if(!s || (s->phase!=1 && s->phase!=2 && !s->notice) || s->heat<1 || s->heat>5 ||
       s->cops<0 || s->cops>10 || !isfinite(s->busted) || !isfinite(s->cooldown))return;
    if(s->notice==1)strcpy(text[0],"PURSUIT EVADED");
    else if(s->notice==2)strcpy(text[0],"BUSTED");
    else snprintf(text[0],QUEST_COPCHASE_TEXT,"HEAT %d   POLICE %d",s->heat,s->cops);
    if(s->notice)snprintf(text[1],QUEST_COPCHASE_TEXT,"TIME %u:%02u",s->seconds/60,s->seconds%60);
    else if(s->phase==2)snprintf(text[1],QUEST_COPCHASE_TEXT,"COOLDOWN %d%%",(int)(fminf(1,fmaxf(0,s->cooldown))*100));
    else snprintf(text[1],QUEST_COPCHASE_TEXT,"BUST %d%%",(int)(fminf(1,fmaxf(0,s->busted))*100));
    snprintf(text[2],QUEST_COPCHASE_TEXT,"BOUNTY %u",s->bounty);
}
/* Follow the saved map position above its circle; GPS remains below it. */
static inline unsigned quest_copchase_vertices(QuestGhostVertex *out,const QuestRadioFont *font,const QuestCopChaseHUD *state)
{
    char text[QUEST_COPCHASE_LINES][QUEST_COPCHASE_TEXT];quest_copchase_text(text,state);
    QuestGhostVertex *next=out;
    for(unsigned line=0;line<QUEST_COPCHASE_LINES;++line){
        size_t n=strlen(text[line]);if(!n)continue;QuestGhostVertex *begin=next;
        const float advance=.009f,height=.013f;float p[3]={0,0,0};
        quest_control_quad(&next,p,n*advance+.012f,height+.008f,QUEST_KEY_FACE,NULL);
        p[0]=-(float)(n-1)*advance*.5f;
        for(size_t i=0;i<n;++i,p[0]+=advance)
            quest_control_quad(&next,p,advance*.83f,height,QUEST_KEY_TEXT,font->bits[(unsigned char)text[line][i]]);
        for(QuestGhostVertex *v=begin;v<next;++v){
            float down=v->position[2];v->position[1]=QUEST_MINIMAP_RADIUS+.062f-line*.022f-down;
            v->position[2]=v->normal[0]==QUEST_KEY_TEXT?.002f:.001f;
        }
    }
    return (unsigned)(next-out);
}
