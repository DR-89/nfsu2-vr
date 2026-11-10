#pragma once
#include "quest_vr_panel.h"
#include "xyz_races_api.h"
enum {QUEST_XYZ_RACE_ROWS=6};
typedef struct {
    int open,row,first,held,detail,action,editing,key,bank,creating,form_row,track_choice;
    uint32_t repeat,requested_start,selected_index;
    char password[33],notice[128];
    XYZRaceBrowser view;
    XyzLobbyGame selected;
    XyzRaceInfo selected_info;
    XyzRaceSettings options;
} QuestXYZRaces;
static inline const char *quest_xyz_race_type(unsigned mode){
    return mode==0?"CIRCUIT":mode==1?"SPRINT":mode==2?"DRAG":mode==3?"DRIFT":mode==4?"URL":mode==10?"STREET X":"UNKNOWN";
}
static inline void quest_xyz_races_clear_create(QuestXYZRaces *m){
    volatile char *p=(volatile char*)&m->options;for(size_t i=0;i<sizeof m->options;++i)p[i]=0;
    m->creating=0;
}
static inline void quest_xyz_races_choose_track(QuestXYZRaces *m,unsigned index){
    if(index>=m->view.track_count || index>=XYZ_RACES_TRACK_CHOICES)return;
    m->track_choice=(int)index;const XYZRaceChoice *t=&m->view.tracks[index];XyzRaceSettings *o=&m->options;
    o->mode=t->mode;o->track=t->id;
    if(!t->reverse)o->reverse=0;
    if(o->laps<t->min_laps)o->laps=t->min_laps;
    if(o->laps>t->max_laps)o->laps=t->max_laps;
    if(o->max_players>t->max_players)o->max_players=t->max_players;
    if(o->min_players>o->max_players)o->min_players=o->max_players;
    if(o->mode==3 || o->mode==10)o->catchup=0;
}
static inline void quest_xyz_races_clear_password(QuestXYZRaces *m){
    volatile char *p=m->password;for(size_t i=0;i<sizeof m->password;++i)p[i]=0;
}
static inline const char *quest_xyz_races_keys(int bank){
    return bank==1?"abcdefghijklmnopqrstuvwxyz0123456789_-":bank==2?" !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~":
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
}
static inline int quest_xyz_races_joinable(const QuestXYZRaces *m){
    return m->view.state==XYZ_RACES_READY && m->selected_info.compatible && m->selected.min_players>=2 &&
        m->selected.max_players<=6 && m->selected.players && m->selected.players<m->selected.max_players &&
        !(m->selected.system_flags&0x80000u);
}
static inline void quest_xyz_races_open(QuestXYZRaces *m,int held){
    m->open=1;m->row=m->first=m->detail=m->action=m->editing=0;m->held=held;m->notice[0]=0;quest_xyz_races_clear_password(m);quest_xyz_races_clear_create(m);
}
static inline void quest_xyz_races_update(QuestXYZRaces *m,const XYZRaceBrowser *view){
    unsigned old=m->view.count>XYZ_RACES_PAGE_SIZE?XYZ_RACES_PAGE_SIZE:m->view.count;
    unsigned count=view->count>XYZ_RACES_PAGE_SIZE?XYZ_RACES_PAGE_SIZE:view->count;
    /* Keep a selected command selected while a page is replaced. Otherwise
     * Refresh/Disconnect can turn into an unrelated race under the cursor. */
    if(m->row>=(int)old)m->row=(int)count+m->row-(int)old;
    else if(m->row>=(int)count)m->row=count?(int)count-1:2;
    if(view->epoch!=m->view.epoch || view->state!=XYZ_RACES_READY){m->detail=m->editing=0;quest_xyz_races_clear_password(m);quest_xyz_races_clear_create(m);}
    if(view->state!=m->view.state && (view->state==XYZ_RACES_IN_LOBBY || view->state==XYZ_RACES_RACING))m->row=0;
    m->view=*view;
    int max_first=count>QUEST_XYZ_RACE_ROWS?(int)count-QUEST_XYZ_RACE_ROWS:0;
    if(m->first>max_first)m->first=max_first;
}
static inline void quest_xyz_races_step(QuestXYZRaces *m,uint32_t now,int available,int keys){
    int edge=keys&~m->held;m->held=keys;m->action=0;
    if(!available){m->open=m->detail=m->editing=0;quest_xyz_races_clear_password(m);quest_xyz_races_clear_create(m);return;}
    if(!m->open)return;
    if(edge&QUEST_VR_BACK){if(m->editing)m->editing=0;else if(m->creating)quest_xyz_races_clear_create(m);else if(m->detail)m->detail=0;else m->open=0;quest_xyz_races_clear_password(m);return;}
    if(m->editing){
        if(edge&128)m->bank=(m->bank+1)%(m->editing==2?2:3);
        int nav=keys&15;
        if(nav && ((edge&15) || (int32_t)(now-m->repeat)>=0)){
            m->repeat=now+((edge&15)?350:140);
            if(nav&QUEST_VR_UP)m->key=(m->key+30)%40;
            else if(nav&QUEST_VR_DOWN)m->key=(m->key+10)%40;
            else if(nav&QUEST_VR_LEFT)m->key=(m->key/10)*10+(m->key+9)%10;
            else m->key=(m->key/10)*10+(m->key+1)%10;
        }
        if(edge&QUEST_VR_ACCEPT){
            const char *chars=quest_xyz_races_keys(m->bank);size_t n=strlen(m->password);
            if(m->key<38 && m->key<(int)strlen(chars) && n<(m->editing==2?12u:32u)){m->password[n]=chars[m->key];m->password[n+1]=0;}
            if(m->key==38 && n)m->password[n-1]=0;
            if(m->key==39 && m->editing==2){snprintf(m->options.password,sizeof m->options.password,"%s",m->password);m->editing=0;quest_xyz_races_clear_password(m);}
            else if(m->key==39 && n && quest_xyz_races_joinable(m)){m->action=XYZ_RACES_JOIN;m->editing=0;}
        }return;
    }
    if(m->creating){
        if(m->view.state!=XYZ_RACES_READY || !m->view.track_count){quest_xyz_races_clear_create(m);return;}
        int nav=keys&15,move=0;
        if(nav && ((edge&15) || (int32_t)(now-m->repeat)>=0)){
            m->repeat=now+((edge&15)?350:140);
            if(nav&QUEST_VR_UP)m->form_row=(m->form_row+10)%11;
            else if(nav&QUEST_VR_DOWN)m->form_row=(m->form_row+1)%11;
            else move=nav&QUEST_VR_LEFT?-1:1;
        }
        if(edge&QUEST_VR_ACCEPT)move=1;
        if(!move)return;
        XyzRaceSettings *o=&m->options;const XYZRaceChoice *t=&m->view.tracks[m->track_choice];
        if(m->form_row<2){
            unsigned count=m->view.track_count,index=(unsigned)m->track_choice;
            for(unsigned n=0;n<count;++n){index=(index+count+move)%count;
                if(m->form_row==0?m->view.tracks[index].mode!=o->mode:m->view.tracks[index].mode==o->mode){quest_xyz_races_choose_track(m,index);break;}}
        }else if(m->form_row==2){if(t->reverse)o->reverse=!o->reverse;}
        else if(m->form_row==3)o->laps=t->min_laps+(o->laps-t->min_laps+(t->max_laps-t->min_laps+1)+move)%(t->max_laps-t->min_laps+1);
        else if(m->form_row==4)o->min_players=2+(o->min_players-2+(o->max_players-1)+move)%(o->max_players-1);
        else if(m->form_row==5){o->max_players=2+(o->max_players-2+(t->max_players-1)+move)%(t->max_players-1);if(o->min_players>o->max_players)o->min_players=o->max_players;}
        else if(m->form_row==6)o->collision=!o->collision;
        else if(m->form_row==7){if(o->mode!=3 && o->mode!=10)o->catchup=!o->catchup;}
        else if((edge&QUEST_VR_ACCEPT) && m->form_row==8){m->editing=2;m->key=m->bank=0;snprintf(m->password,sizeof m->password,"%s",o->password);}
        else if((edge&QUEST_VR_ACCEPT) && m->form_row==9)m->action=XYZ_RACES_CREATE;
        else if((edge&QUEST_VR_ACCEPT) && m->form_row==10)quest_xyz_races_clear_create(m);
        return;
    }
    if(m->detail){
        if((edge&QUEST_VR_ACCEPT) && quest_xyz_races_joinable(m)){
            quest_xyz_races_clear_password(m);m->notice[0]=0;
            if(m->selected.system_flags&0x10000u){m->editing=1;m->key=m->bank=0;}
            else m->action=XYZ_RACES_JOIN;
        }return;
    }
    if(m->view.state==XYZ_RACES_IN_LOBBY || m->view.state==XYZ_RACES_RACING){
        int racing=m->view.state==XYZ_RACES_RACING,rows=racing?3:5,nav=keys&15;
        if(m->row>=rows)m->row=0;
        if(nav && ((edge&15) || (int32_t)(now-m->repeat)>=0)){
            m->repeat=now+((edge&15)?350:140);
            if(nav&QUEST_VR_UP)m->row=(m->row+rows-1)%rows;
            if(nav&QUEST_VR_DOWN)m->row=(m->row+1)%rows;
        }
        if(edge&QUEST_VR_ACCEPT){
            if(m->row==rows-1)m->open=0;
            else if(m->row==rows-2)m->action=XYZ_RACES_CLOSE;
            else if(racing || m->row==2)m->action=XYZ_RACES_LEAVE;
            else if(!m->row)m->action=XYZ_RACES_SET_READY;
            else if(m->view.startable)m->action=XYZ_RACES_START;
        }return;
    }
    unsigned count=m->view.count;if(count>XYZ_RACES_PAGE_SIZE)count=XYZ_RACES_PAGE_SIZE;
    int rows=(int)count+6;if(m->row>=rows)m->row=rows-1;
    int nav=keys&15;
    if(nav && ((edge&15) || (int32_t)(now-m->repeat)>=0)){
        m->repeat=now+((edge&15)?350:140);
        if(nav&QUEST_VR_UP)m->row=(m->row+rows-1)%rows;
        else if(nav&QUEST_VR_DOWN)m->row=(m->row+1)%rows;
    }
    if(m->row<(int)count){
        if(m->row<m->first)m->first=m->row;
        if(m->row>=m->first+QUEST_XYZ_RACE_ROWS)m->first=m->row-QUEST_XYZ_RACE_ROWS+1;
    }
    int max_first=count>QUEST_XYZ_RACE_ROWS?(int)count-QUEST_XYZ_RACE_ROWS:0;if(m->first>max_first)m->first=max_first;
    if(!(edge&QUEST_VR_ACCEPT))return;
    if(m->row<(int)count){m->selected=m->view.rows[m->row];m->selected_info=m->view.info[m->row];m->selected_index=(uint32_t)m->row;m->detail=1;m->notice[0]=0;return;}
    int choice=m->row-(int)count;
    if(choice==5){m->open=0;return;}
    if(choice==4){if(m->view.active)m->action=XYZ_RACES_CLOSE;return;}
    if(m->view.state!=XYZ_RACES_READY)return;
    if(choice==3){if(!m->view.track_count)return;quest_xyz_races_clear_create(m);m->creating=1;m->form_row=0;
        m->options.laps=2;m->options.min_players=2;m->options.max_players=6;m->options.unrestricted=1;m->options.collision=m->options.catchup=1;
        quest_xyz_races_choose_track(m,0);return;}
    if(choice==0 && !m->view.start)return;
    if(choice==1 && (count<XYZ_RACES_PAGE_SIZE || m->view.start>INT32_MAX-2*XYZ_RACES_PAGE_SIZE))return;
    m->requested_start=choice==0?(m->view.start>=XYZ_RACES_PAGE_SIZE?m->view.start-XYZ_RACES_PAGE_SIZE:0):
        choice==1?m->view.start+XYZ_RACES_PAGE_SIZE:m->view.start;
    m->action=XYZ_RACES_SEARCH;
}
static inline void quest_xyz_races_panel(uint32_t *pixels,const QuestXYZRaces *m){
    quest_vr_rect(pixels,0,0,1024,768,0xff141b24u);quest_vr_rect(pixels,0,0,1024,7,0xff4da3ffu);
    quest_vr_text(pixels,40,34,5,"RACE BROWSER",0xffffffffu);
    char text[80];
    if(m->editing){
        quest_vr_text(pixels,40,106,3,"LOBBY PASSWORD",0xffc8beb4u);
        size_t n=strlen(m->password);memset(text,'*',n);text[n]=0;quest_vr_text(pixels,40,154,4,text,0xffffd79fu);
        const char *chars=quest_xyz_races_keys(m->bank);
        quest_vr_text(pixels,40,216,3,m->bank==0?"UPPERCASE":m->bank==1?"LOWERCASE":"SYMBOLS",0xffffffffu);
        for(int i=0;i<40;++i){int x=40+(i%10)*94,y=268+(i/10)*68;
            quest_vr_rect(pixels,x,y,82,54,i==m->key?0xff214171u:0xff2b3038u);
            char key[2]={i<38 && i<(int)strlen(chars)?chars[i]:0,0};
            quest_vr_text(pixels,x+12,y+15,3,i==38?"DEL":i==39?"OK":m->bank==2 && !i?"SP":key,0xffffffffu);
        }
        quest_vr_text(pixels,40,592,3,m->editing==2?"Y: CHANGE CASE   B: CANCEL":"Y: CHANGE CASE / SYMBOLS   B: CANCEL",0xffc8beb4u);
        quest_vr_text(pixels,40,636,3,m->editing==2?"OK SAVES PASSWORD. EMPTY ALLOWS EVERYONE.":"OK JOINS THE SELECTED LOBBY",0xffffffffu);return;
    }
    if(m->creating){
        const XyzRaceSettings *o=&m->options;const XYZRaceChoice *t=&m->view.tracks[m->track_choice];
        quest_vr_text(pixels,40,104,3,"CREATE AN UNRANKED RACE",0xffffd79fu);
        for(int row=0;row<11;++row){int y=158+39*row;
            if(row==m->form_row)quest_vr_rect(pixels,24,y-4,976,32,0xff214171u);
            switch(row){
            case 0:snprintf(text,sizeof text,"TYPE: %s",quest_xyz_race_type(o->mode));break;
            case 1:snprintf(text,sizeof text,"MAP: %.44s",o->reverse?t->reverse_name:t->name);break;
            case 2:snprintf(text,sizeof text,"DIRECTION: %s",o->reverse?"REVERSE":t->reverse?"FORWARD":"FORWARD ONLY");break;
            case 3:snprintf(text,sizeof text,"LAPS: %u",o->laps);break;
            case 4:snprintf(text,sizeof text,"MINIMUM PLAYERS: %u",o->min_players);break;
            case 5:snprintf(text,sizeof text,"MAXIMUM PLAYERS: %u",o->max_players);break;
            case 6:snprintf(text,sizeof text,"COLLISION: %s",o->collision?"ON":"OFF");break;
            case 7:snprintf(text,sizeof text,"CATCH-UP: %s",o->mode==3 || o->mode==10?"UNAVAILABLE":o->catchup?"ON":"OFF");break;
            case 8:snprintf(text,sizeof text,"PASSWORD: %s",o->password[0]?"SET":"NONE");break;
            case 9:snprintf(text,sizeof text,"CREATE RACE");break;
            default:snprintf(text,sizeof text,"CANCEL");break;
            }quest_vr_text(pixels,40,y,3,text,0xffffffffu);
        }
        quest_vr_text(pixels,40,624,2,"ALL PERFORMANCE CLASSES. YOUR CAREER IS KEPT.",0xffc8beb4u);
        if(m->notice[0]){snprintf(text,sizeof text,"%.76s",m->notice);quest_vr_text(pixels,40,654,2,text,0xff8888ffu);}
        quest_vr_text(pixels,40,696,3,"LEFT / RIGHT: CHANGE   A: SELECT   B: BACK",0xffffffffu);return;
    }
    if(m->view.state==XYZ_RACES_IN_LOBBY || m->view.state==XYZ_RACES_RACING){
        int racing=m->view.state==XYZ_RACES_RACING;
        quest_vr_text(pixels,40,106,3,racing?"RACE IN PROGRESS":"RACE LOBBY",0xffffd79fu);
        snprintf(text,sizeof text,"%.48s",m->view.race.track_name);quest_vr_text(pixels,40,154,3,text,0xffffffffu);
        for(unsigned i=0;i<m->view.lobby.players && i<6;++i){
            snprintf(text,sizeof text,"%.15s%s  %s",m->view.lobby.player[i],!strcmp(m->view.name,m->view.lobby.player[i])?" (YOU)":"",
                !i?"HOST":m->view.lobby.player_flags[i]&0x08000000u?"READY":"NOT READY");
            quest_vr_text(pixels,40,212+36*i,3,text,0xffffffffu);
        }
        const char *actions[]={m->view.local_ready?"CANCEL READY":"READY", "START RACE", "LEAVE LOBBY", "DISCONNECT", "BACK"};
        const char *race_actions[]={"RETURN TO RACE BROWSER","DISCONNECT","BACK TO RACE"};
        int count=racing?3:5;
        for(int i=0;i<count;++i){int y=448+i*32;
            if(i==m->row)quest_vr_rect(pixels,24,y-3,976,28,0xff214171u);
            quest_vr_text(pixels,40,y,3,racing?race_actions[i]:actions[i],!racing && i==1 && !m->view.startable?0xff6c747fu:0xffffffffu);
        }
        if(m->view.notice[0]){snprintf(text,sizeof text,"%.76s",m->view.notice);quest_vr_text(pixels,40,634,2,text,0xff8888ffu);}
        quest_vr_text(pixels,40,696,3,"STICK / D-PAD: MOVE   A: SELECT   B: BACK",0xffffffffu);return;
    }
    if(m->detail){
        snprintf(text,sizeof text,"%.35s",m->selected.name);quest_vr_text(pixels,40,154,4,text,0xffffd79fu);
        const XyzRaceInfo *info=&m->selected_info;
        snprintf(text,sizeof text,"MAP: %.46s",info->track_name[0]?info->track_name:"NAME UNAVAILABLE");quest_vr_text(pixels,40,230,3,text,0xffffffffu);
        const char *kind=info->mode==0?"CIRCUIT":info->mode==1?"SPRINT":info->mode==2?"DRAG":info->mode==3?"DRIFT":info->mode==4?"URL":info->mode==10?"STREET X":"UNKNOWN RACE TYPE";
        if(info->parsed){
            snprintf(text,sizeof text,"%s - %u LAP%s - %s",kind,info->laps,info->laps==1?"":"S",info->reverse?"REVERSE":"FORWARD");
            quest_vr_text(pixels,40,278,3,text,0xffc8beb4u);
        }
        snprintf(text,sizeof text,"HOST: %.15s",m->selected.host);quest_vr_text(pixels,40,338,3,text,0xffffffffu);
        snprintf(text,sizeof text,"PLAYERS: %u / %u",m->selected.players,m->selected.max_players);quest_vr_text(pixels,40,386,3,text,0xffffffffu);
        quest_vr_text(pixels,40,434,3,m->selected.system_flags&0x10000?"PASSWORD REQUIRED":"NO PASSWORD",0xffc8beb4u);
        quest_vr_text(pixels,40,482,3,m->selected.system_flags&0x80000?"RACE IN PROGRESS":"WAITING FOR PLAYERS",0xffc8beb4u);
        if(!info->compatible)quest_vr_text(pixels,40,550,3,"TRACK OR RULES NOT AVAILABLE IN THIS BUILD",0xff8888ffu);
        if(m->notice[0]){snprintf(text,sizeof text,"%.76s",m->notice);quest_vr_text(pixels,40,610,2,text,0xff8888ffu);}
        quest_vr_text(pixels,40,696,3,quest_xyz_races_joinable(m)?"A: JOIN RACE   B: BACK TO THE LIST":"B: BACK TO THE LIST",0xffffffffu);return;
    }
    const char *states[]={"OFFLINE","CONNECTING","LOADING RACES","CONNECTED","DISCONNECTING","CONNECTION ENDED",
        "JOINING RACE","IN LOBBY","STARTING RACE","RACING","RETURNING"};
    int state=m->view.state;
    snprintf(text,sizeof text,"%s - %.15s",state>=0 && state<11?states[state]:"OFFLINE",m->view.name);
    quest_vr_text(pixels,40,101,3,text,0xffffd79fu);
    unsigned count=m->view.count;if(count>XYZ_RACES_PAGE_SIZE)count=XYZ_RACES_PAGE_SIZE;
    if(!count && state==XYZ_RACES_READY)quest_vr_text(pixels,40,180,3,"NO RACES ON THIS PAGE",0xffc8beb4u);
    for(int line=0;line<QUEST_XYZ_RACE_ROWS && m->first+line<(int)count;++line){
        int i=m->first+line,y=149+50*line;const XyzLobbyGame *g=&m->view.rows[i];
        if(i==m->row)quest_vr_rect(pixels,24,y-4,976,45,0xff214171u);
        const XyzRaceInfo *info=&m->view.info[i];
        snprintf(text,sizeof text,"%.42s",info->track_name[0]?info->track_name:g->name);quest_vr_text(pixels,40,y,3,text,0xffffffffu);
        snprintf(text,sizeof text,"HOST: %.15s / %.38s",g->host,g->name);quest_vr_text(pixels,40,y+22,2,text,0xffc8beb4u);
        snprintf(text,sizeof text,"%u/%u",g->players,g->max_players);quest_vr_text(pixels,870,y,3,text,0xffffffffu);
    }
    const char *actions[]={"PREVIOUS PAGE","NEXT PAGE","REFRESH","CREATE RACE","DISCONNECT","BACK"};
    if(m->view.from_career)actions[4]="RETURN TO FREE ROAM";
    for(int i=0;i<6;++i){
        int y=452+i*29,enabled=i==5 || (i==4?m->view.active:i==3?state==XYZ_RACES_READY && m->view.track_count:state==XYZ_RACES_READY &&
            (i==0?m->view.start>0:i==1?count==XYZ_RACES_PAGE_SIZE && m->view.start<=INT32_MAX-2*XYZ_RACES_PAGE_SIZE:1));
        if(m->row==(int)count+i)quest_vr_rect(pixels,24,y-3,976,27,0xff214171u);
        quest_vr_text(pixels,40,y,3,actions[i],enabled?0xffffffffu:0xff6c747fu);
    }
    snprintf(text,sizeof text,"PAGE %u",m->view.start/XYZ_RACES_PAGE_SIZE+1);quest_vr_text(pixels,770,452,3,text,0xffc8beb4u);
    if(m->view.notice[0]){
        snprintf(text,sizeof text,"%.76s",m->view.notice);quest_vr_text(pixels,40,630,2,text,0xff8888ffu);
        if(strlen(m->view.notice)>76){snprintf(text,sizeof text,"%.51s",m->view.notice+76);quest_vr_text(pixels,40,657,2,text,0xff8888ffu);}
    }
    quest_vr_text(pixels,40,696,3,"STICK / D-PAD: MOVE   A: SELECT   B: BACK",0xffffffffu);
}
