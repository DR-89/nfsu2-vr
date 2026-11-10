#pragma once
#include "quest_vr_panel.h"
#include "../../src/community/xyz_stance.h"
typedef struct {
    int open,row,held,action,step;
    uint32_t repeat;
    XyzStanceView view;
} QuestStanceMenu;
static inline void quest_stance_step(QuestStanceMenu *m,uint32_t now,int paused,int keys){
    m->action=-1;m->step=0;int edge=keys&~m->held;m->held=keys;
    if(!paused || (edge&QUEST_VR_BACK)){m->open=0;return;}
    int nav=keys&15;
    if(nav && ((edge&15) || (int32_t)(now-m->repeat)>=0)){
        m->repeat=now+((edge&15)?350:140);
        if(nav&QUEST_VR_UP)m->row=(m->row+8)%9;
        else if(nav&QUEST_VR_DOWN)m->row=(m->row+1)%9;
        else if(m->row<7){m->action=m->row;m->step=nav&QUEST_VR_RIGHT?1:-1;}
    }
    if(edge&QUEST_VR_ACCEPT){
        if(m->row==8)m->open=0;
        else if(m->row<2 || m->row==7){m->action=m->row;m->step=1;}
    }
}
static inline void quest_stance_panel(uint32_t *pixels,const QuestStanceMenu *m){
    quest_vr_rect(pixels,0,0,1024,768,0xff141b24u);quest_vr_rect(pixels,0,0,1024,7,0xff4da3ffu);
    quest_vr_text(pixels,40,34,5,"STANCE",0xffffffffu);
    quest_vr_text(pixels,40,88,3,m->view.available?m->view.name:"LOAD A CAR IN FREE ROAM OR A RACE",0xffffd79fu);
    const char *labels[]={"STANCE MOD","THIS VEHICLE MODEL","FRONT CAMBER","REAR CAMBER","FRONT OFFSET","REAR OFFSET","TIRE WIDTH","RESET THIS MODEL","BACK"};
    for(int row=0;row<9;row++){
        int y=144+row*43;char value[48]={0};const XyzStance *v=&m->view.value;
        if(row==m->row)quest_vr_rect(pixels,24,y-5,976,31,0xff214171u);
        if(!row)snprintf(value,sizeof value,"< %s >",m->view.enabled?"ON":"OFF");
        if(row==1)snprintf(value,sizeof value,"< %s >",m->view.custom?"CUSTOM":"ORIGINAL");
        if(row==2 || row==3)snprintf(value,sizeof value,"< %.1f DEG >",(row==2?v->fc:v->rc)*.1f);
        if(row==4 || row==5)snprintf(value,sizeof value,"< %+d MM >",row==4?v->fo:v->ro);
        if(row==6)snprintf(value,sizeof value,"< %d%% >",v->tw);
        uint32_t color=!row || row==8 || m->view.available?0xffffffffu:0xff999999u;
        quest_vr_text(pixels,40,y,3,labels[row],color);quest_vr_text(pixels,650,y,3,value,color);
    }
    quest_vr_text(pixels,40,556,3,"SAVED PER MODEL. VISUAL CHANGES ONLY.",0xffc8beb4u);
    quest_vr_text(pixels,40,596,3,"ONLINE PLAYERS SEE YOUR STANCE IN FREE ROAM.",0xffc8beb4u);
    quest_vr_text(pixels,40,650,2,"BASED ON ARCHIE'S STANCE MOD. OTHER PLAYERS' STANCE IS SHOWN WHEN ON.",0xffc8beb4u);
    quest_vr_text(pixels,40,690,3,"STICK / D-PAD: SELECT + ADJUST   A: SELECT",0xffffffffu);
    quest_vr_text(pixels,40,730,2,m->view.save_failed?"SAVE FAILED. YOUR PREVIOUS SETTINGS ARE STILL ACTIVE.":"B: BACK TO COMMUNITY MODS",m->view.save_failed?0xff8888ffu:0xffc8beb4u);
}
