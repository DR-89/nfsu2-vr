#pragma once
#include "quest_vr_panel.h"
#include "quest_copchase.h"
typedef struct {uint64_t high,low;int held,released,submitted,failed,error,uncertain;} QuestCopChaseConfirm;
/* A held button from the final driving frame cannot pay a newly opened fine. */
static inline int quest_copchase_confirm(QuestCopChaseConfirm *m,const QuestCopChaseBust *s,int paused,int keys)
{
    if(!s->active || !paused){memset(m,0,sizeof *m);return 0;}
    if(m->high!=s->token_high || m->low!=s->token_low || m->failed!=s->failed ||
       m->error!=s->error || m->uncertain!=s->uncertain){
        memset(m,0,sizeof *m);m->high=s->token_high;m->low=s->token_low;m->held=keys;
        m->failed=s->failed;m->error=s->error;m->uncertain=s->uncertain;
    }
    int edge=keys&~m->held;m->held=keys;
    if(!(keys&QUEST_VR_ACCEPT))m->released=1;
    if(s->error==1 && !(keys&QUEST_VR_ACCEPT))m->submitted=0;
    if((s->failed || (s->ready && !s->paid && s->error<2)) && m->released && !m->submitted && (edge&QUEST_VR_ACCEPT)){
        m->submitted=1;return 1;
    }
    return 0;
}
static inline void quest_copchase_panel(uint32_t *pixels,const QuestCopChaseBust *s,int submitted)
{
    quest_vr_rect(pixels,0,0,1024,768,0xff141b24u);quest_vr_rect(pixels,0,0,1024,7,0xff6868ffu);
    quest_vr_text(pixels,40,36,6,"BUSTED",0xffffffffu);
    quest_vr_text(pixels,40,105,3,"COPCHASE BY VITORINI MODS",0xffc8beb4u);
    char line[80];
    snprintf(line,sizeof line,"PURSUIT TIME   %u:%02u",s->seconds/60,s->seconds%60);
    quest_vr_text(pixels,40,195,4,line,0xffffffffu);
    snprintf(line,sizeof line,"BOUNTY LOST    %u",s->bounty);quest_vr_text(pixels,40,260,4,line,0xffffffffu);
    snprintf(line,sizeof line,"INFRACTIONS    %u",s->infractions);quest_vr_text(pixels,40,325,4,line,0xffffffffu);
    snprintf(line,sizeof line,"FINE           $%u",s->fine);quest_vr_text(pixels,40,415,5,line,0xffffd79fu);
    quest_vr_text(pixels,40,482,3,"THE FINE IS LIMITED TO YOUR CURRENT CASH.",0xffc8beb4u);
    quest_vr_rect(pixels,24,570,976,66,0xff214171u);
    const char *action=s->failed?"[A] RETURN TO PAUSE MENU":s->error==1?"PAYMENT FAILED - [A] RETRY":s->error?"PAYMENT UNAVAILABLE":s->paid?"PAID - RETURNING TO FREE ROAM":
        submitted?"PROCESSING PAYMENT...":s->ready?"[A] PAY FINE AND CONTINUE":"PREPARING RESULT...";
    quest_vr_text(pixels,40,590,4,action,0xffffffffu);
    if(s->failed){
        const char *message=s->uncertain?"PAYMENT STATUS UNKNOWN. DO NOT RETRY.":s->paid?
            "FINE PAID. RESUME FROM THE PAUSE MENU.":"NO PAYMENT WAS MADE. USE THE PAUSE MENU.";
        quest_vr_text(pixels,40,690,3,message,0xff8888ffu);
    }else if(s->error==1)quest_vr_text(pixels,40,690,3,"YOUR CASH HAS NOT BEEN CHANGED.",0xff8888ffu);
}
