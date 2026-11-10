#pragma once
#include "quest_vr_panel.h"
#include "xyz_profile_api.h"
#include "xyz_game_api.h"

typedef struct {
    int open, row, key, editing, confirming, held, action, index,main_menu,races_active,name_guide,uppercase;
    uint32_t repeat;
    char text[64];
    char guide_notice[96];
    XYZProfileList list;
    XYZOnlineStatus online;
} QuestXYZProfiles;
enum { QUEST_XYZ_REVEAL=128,QUEST_XYZ_ACTIONS=10 };
static inline char quest_xyz_key_char(const QuestXYZProfiles *m,int key) {
    static const char chars[]="abcdefghijklmnopqrstuvwxyz0123456789_-";
    if(key<0 || key>=38)return 0;
    char c=chars[key];return m->uppercase && c>='a' && c<='z'?(char)(c-'a'+'A'):c;
}
static inline int quest_xyz_can_open_website(const QuestXYZProfiles *m) {
    return m->open && m->name_guide && m->main_menu && !m->races_active &&
        m->list.selected>=0 && m->list.selected<m->list.count && m->list.count<=8 &&
        (m->online.state==XYZ_ONLINE_OFFLINE || m->online.state==XYZ_ONLINE_STOPPED);
}
/* One fixed public URL: no name, credential or callback in the browser URL.
 * Recheck eligibility at dispatch, since the connection may have changed. */
static inline int quest_xyz_open_website(QuestXYZProfiles *m,int (*open_url)(const char *)) {
    int ok=quest_xyz_can_open_website(m) && open_url && open_url("https://nfsu2.xyz/")==0;
    snprintf(m->guide_notice,sizeof m->guide_notice,"%s",ok?
        "Browser requested. Finish on the site, then return here.":
        "Could not open browser. Open https://nfsu2.xyz/ yourself.");
    return ok;
}
static inline void quest_xyz_clear_text(QuestXYZProfiles *m) {
    volatile char *p=m->text;for(size_t i=0;i<sizeof m->text;++i)p[i]=0;
}
static inline void quest_xyz_display_text(const QuestXYZProfiles *m,char out[64]) {
    snprintf(out,64,"%s",m->text);
    if(m->editing==XYZ_PROFILE_AUTH && !(m->held&QUEST_XYZ_REVEAL))
        for(size_t i=0;out[i];++i)if(out[i]!='-' && out[i]!='_')out[i]='*';
}
/* Pure menu transitions. A nonzero action is executed once by the host, which
 * refreshes the snapshot and clears it. Holding A never repeats a mutation. */
static inline void quest_xyz_open(QuestXYZProfiles *m,int held) {
    m->open=1;m->row=m->list.selected>=0?m->list.selected:0;
    m->key=m->editing=m->confirming=m->action=m->name_guide=m->uppercase=0;m->held=held;
    m->guide_notice[0]=0;
    quest_xyz_clear_text(m);
}
static inline void quest_xyz_step(QuestXYZProfiles *m,uint32_t now,int paused,int keys) {
    int edge=keys&~m->held;m->held=keys;m->action=0;
    if(!paused) {m->open=0;m->editing=m->confirming=m->name_guide=0;quest_xyz_clear_text(m);return;}
    if(!m->open)return;
    if(edge&QUEST_VR_BACK) {
        if(m->editing || m->confirming || m->name_guide)m->editing=m->confirming=m->name_guide=0;
        else m->open=0;
        quest_xyz_clear_text(m);
        return;
    }
    if(m->editing && (edge&QUEST_VR_OPEN))m->uppercase=!m->uppercase;
    int nav=keys&15;
    if(nav && ((edge&15) || (int32_t)(now-m->repeat)>=0)) {
        m->repeat=now+((edge&15)?350:140);
        if(m->editing) {
            if(nav&QUEST_VR_UP)m->key=(m->key+30)%40;
            else if(nav&QUEST_VR_DOWN)m->key=(m->key+10)%40;
            else if(nav&QUEST_VR_LEFT)m->key=(m->key/10)*10+(m->key+9)%10;
            else m->key=(m->key/10)*10+(m->key+1)%10;
        } else if(m->name_guide) {
            if(nav&QUEST_VR_UP)m->key=(m->key+2)%3;
            else if(nav&QUEST_VR_DOWN)m->key=(m->key+1)%3;
        } else if(m->confirming) m->key=!m->key;
        else {
            int rows=m->list.count+QUEST_XYZ_ACTIONS;
            if(nav&QUEST_VR_UP)m->row=(m->row+rows-1)%rows;
            else if(nav&QUEST_VR_DOWN)m->row=(m->row+1)%rows;
        }
    }
    if(!(edge&QUEST_VR_ACCEPT))return;
    if(m->name_guide) {
        if(m->key==2){m->name_guide=0;return;}
        if(m->key==0){if(quest_xyz_can_open_website(m))m->action=XYZ_PROFILE_WEBSITE;return;}
        int selected=m->list.selected;
        if(selected>=0 && selected<m->list.count && selected<8 && !m->list.auth_busy[selected]){
            m->name_guide=0;m->editing=XYZ_PROFILE_AUTH;quest_xyz_clear_text(m);m->key=m->uppercase=0;
        }
        return;
    }
    if(m->confirming) {
        if(m->key) {m->action=XYZ_PROFILE_DELETE;m->index=m->list.selected;}
        m->confirming=0;return;
    }
    if(m->editing) {
        size_t len=strlen(m->text),max=(m->editing==XYZ_PROFILE_ROOM || m->editing==XYZ_PROFILE_AUTH)?63:15;
        char c=quest_xyz_key_char(m,m->key);
        if(c && len<max) {m->text[len]=c;m->text[len+1]=0;}
        if(m->key==38 && len)m->text[len-1]=0;
        if(m->key==39) {m->action=m->editing;m->index=m->list.selected;m->editing=0;}
        return;
    }
    if(m->row<m->list.count) {m->action=XYZ_PROFILE_SELECT;m->index=m->row;return;}
    int choice=m->row-m->list.count;
    if(choice==9) {m->open=0;quest_xyz_clear_text(m);return;}
    if(choice==6) {m->action=XYZ_PROFILE_DISCONNECT;return;}
    if(choice==0) {m->editing=XYZ_PROFILE_CREATE;quest_xyz_clear_text(m);m->key=m->uppercase=0;return;}
    if(m->list.selected<0)return;
    if(choice==8){m->name_guide=1;m->key=0;m->guide_notice[0]=0;quest_xyz_clear_text(m);return;}
    if(choice==7) {
        if(m->list.auth_busy[m->list.selected])return;
        m->editing=XYZ_PROFILE_AUTH;quest_xyz_clear_text(m);m->key=m->uppercase=0;return;
    }
    if(choice==4) {if(!m->main_menu && !m->races_active){m->action=XYZ_PROFILE_CONNECT;m->index=m->list.selected;}return;}
    if(choice==5) {m->action=XYZ_PROFILE_RACES;m->index=m->list.selected;return;}
    if(choice==3) {m->confirming=1;m->key=0;return;}
    m->editing=choice==1?XYZ_PROFILE_RENAME:XYZ_PROFILE_ROOM;m->key=m->uppercase=0;
    snprintf(m->text,sizeof m->text,"%s",choice==1?m->list.name[m->list.selected]:m->list.room[m->list.selected]);
}
static inline void quest_xyz_panel(uint32_t *pixels,const QuestXYZProfiles *m) {
    quest_vr_rect(pixels,0,0,1024,768,0xff141b24u);
    quest_vr_rect(pixels,0,0,1024,7,0xff4da3ffu);
    quest_vr_text(pixels,40,34,5,"ONLINE PROFILES",0xffffffffu);
    if(m->name_guide) {
        char line[80];int selected=m->list.selected;
        const char *name=selected>=0 && selected<m->list.count && selected<8?m->list.name[selected]:"SELECT A PROFILE";
        quest_vr_text(pixels,40,100,4,"RESERVE / MANAGE NAME",0xffffffffu);
        snprintf(line,sizeof line,"YOUR LOCAL NAME: %.15s",name);quest_vr_text_case(pixels,40,150,3,line,0xffffd79fu);
        static const char *const steps[]={
            "1. OPEN THE OFFICIAL SITE AND SELECT THE ONLINE TAB.",
            "2. SET YOUR NAME AND CHOOSE CLAIM / CHANGE NAME.",
            "3. COMPLETE ITS CHECK AND KEEP YOUR EIGHT-WORD KEYPHRASE.",
            "4. RETURN HERE, LINK THAT KEYPHRASE, THEN CONNECT.",
            "YOUR VR CAREER STAYS ON THIS DEVICE."};
        for(int i=0;i<5;++i)quest_vr_text(pixels,40,210+i*32,2,steps[i],0xffc8beb4u);
        static const char *const choices[]={"OPEN NFSU2.XYZ","LINK KEYPHRASE","BACK"};
        for(int i=0;i<3;++i){int y=420+i*48;
            if(m->key==i)quest_vr_rect(pixels,24,y-5,976,36,0xff214171u);
            quest_vr_text(pixels,40,y,3,choices[i],i==0 && !quest_xyz_can_open_website(m)?0xff808080u:0xffffffffu);
        }
        quest_vr_text(pixels,40,590,2,!m->main_menu?
            "SAVE YOUR CAREER AND RETURN TO THE MAIN MENU TO OPEN THE SITE.":
            quest_xyz_can_open_website(m)?"BROWSER OPENS OUTSIDE THE GAME. RETURN HERE WHEN DONE.":
            "DISCONNECT ONLINE PLAY BEFORE OPENING THE BROWSER.",0xffffd79fu);
        quest_vr_text(pixels,40,622,2,"THE SITE HANDLES THE CLAIM. NO SIGN-IN IS SENT TO THE BROWSER.",0xffc8beb4u);
        snprintf(line,sizeof line,"%.76s",m->guide_notice);quest_vr_text(pixels,40,654,2,line,0xffc8beb4u);
    } else if(m->editing) {
        int auth=m->editing==XYZ_PROFILE_AUTH;
        quest_vr_text(pixels,40,106,3,auth?"EXISTING NFSU2.XYZ KEYPHRASE":m->editing==XYZ_PROFILE_CREATE?"NEW PLAYER NAME":m->editing==XYZ_PROFILE_RENAME?"RENAME PLAYER":"ROOM - EMPTY USES THE PUBLIC WORLD",0xffc8beb4u);
        char shown[64],line[33];quest_xyz_display_text(m,shown);snprintf(line,sizeof line,"%.32s",shown);
        quest_vr_text_case(pixels,40,154,4,line,0xffffd79fu);
        if(strlen(shown)>32)quest_vr_text_case(pixels,40,194,4,shown+32,0xffffd79fu);
        {volatile char *p=shown;for(size_t i=0;i<sizeof shown;++i)p[i]=0;
         p=line;for(size_t i=0;i<sizeof line;++i)p[i]=0;}
        for(int i=0;i<40;++i) {
            int x=40+(i%10)*94,y=268+(i/10)*68;
            quest_vr_rect(pixels,x,y,82,54,i==m->key?0xff214171u:0xff2b3038u);
            char key[2]={quest_xyz_key_char(m,i),0};
            quest_vr_text_case(pixels,x+12,y+15,3,i==38?"DEL":i==39?"OK":key,0xffffffffu);
        }
        quest_vr_text_case(pixels,40,552,2,m->uppercase?"X: Aa - UPPERCASE (ABC)":"X: Aa - lowercase (abc)",0xffffd79fu);
        if(auth) {
            quest_vr_text(pixels,40,576,2,"ENTER YOUR EIGHT WORDS, SEPARATED WITH -.",0xffc8beb4u);
            quest_vr_text(pixels,40,596,2,"HOLD Y TO SHOW. EMPTY + OK REMOVES SAVED SIGN-IN.",0xffc8beb4u);
            quest_vr_text(pixels,40,624,2,"OK SAVES SIGN-IN ON THIS DEVICE. B CANCELS.",0xffc8beb4u);
        } else quest_vr_text(pixels,40,592,2,"CHOOSE OK TO SAVE. B CANCELS THE EDIT.",0xffc8beb4u);
    } else if(m->confirming) {
        quest_vr_text(pixels,40,160,4,"DELETE THIS CONNECTION PROFILE?",0xffffffffu);
        quest_vr_text(pixels,40,230,3,"YOUR GAME CAREER IS KEPT.",0xffc8beb4u);
        quest_vr_rect(pixels,m->key?520:40,350,400,60,0xff214171u);
        quest_vr_text(pixels,60,368,4,"KEEP",0xffffffffu);
        quest_vr_text(pixels,540,368,4,"DELETE",0xffffffffu);
    } else {
        for(int i=0;i<m->list.count+QUEST_XYZ_ACTIONS;++i) {
            int y=104+i*23;
            if(m->row==i)quest_vr_rect(pixels,24,y-3,976,23,0xff214171u);
            static const char *const actions[]={"CREATE PROFILE","RENAME SELECTED","EDIT ROOM","DELETE SELECTED","CONNECT FREE ROAM","BROWSE RACES","DISCONNECT","LINK KEYPHRASE","RESERVE / MANAGE NAME","BACK"};
            const char *label=i<m->list.count?m->list.name[i]:actions[i-m->list.count];
            quest_vr_text_case(pixels,40,y,3,label,(m->main_menu || m->races_active) && i==m->list.count+4?0xff808080u:0xffffffffu);
            if(i==m->list.selected)quest_vr_text(pixels,660,y,3,"SELECTED",0xffffd79fu);
            if(i<m->list.count && (m->list.auth_busy[i] || m->list.auth_linked[i]))
                quest_vr_text(pixels,828,y,3,m->list.auth_busy[i]?"BUSY":"LINKED",0xffffd79fu);
        }
        static const char *const states[]={"OFFLINE","CONNECTING","CONNECTING","IN QUEUE","CONNECTED","RECONNECTING","STOPPED"};
        char line[80];int state=m->online.state;
        snprintf(line,sizeof line,"%s%s%.15s",state>=0 && state<7?states[state]:"OFFLINE",m->online.name[0]?" - ":"",m->online.name);
        quest_vr_text(pixels,40,544,3,line,0xffffd79fu);
        if(state==XYZ_ONLINE_QUEUED)snprintf(line,sizeof line,"QUEUE %u OF %u",m->online.queue_position,m->online.queue_total);
        else if(m->main_menu)snprintf(line,sizeof line,"CHOOSE A PROFILE TO BROWSE ONLINE RACE LOBBIES.");
        else if(state==XYZ_ONLINE_CONNECTED && !m->online.local_ready)snprintf(line,sizeof line,"ENTER CAREER FREE ROAM TO SEE OTHER PLAYERS.");
        else if(state==XYZ_ONLINE_CONNECTED && m->online.failed)snprintf(line,sizeof line,"A REMOTE CAR COULD NOT LOAD. RETRYING.");
        else if(state==XYZ_ONLINE_CONNECTED && m->online.hub==2)snprintf(line,sizeof line,"PLAYER DISCOVERY IS NOT ANSWERING. RECONNECT.");
        else if(state==XYZ_ONLINE_CONNECTED && m->online.hub==0)snprintf(line,sizeof line,"WAITING FOR THE SERVER'S PLAYER LIST.");
        else if(state==XYZ_ONLINE_CONNECTED && !m->online.listed && !m->online.peers)snprintf(line,sizeof line,"SERVER HAS ASSIGNED NO NEARBY PLAYERS YET.");
        else if(state==XYZ_ONLINE_CONNECTED)snprintf(line,sizeof line,"PLAYERS %d - VISIBLE %d - LOADING %d",m->online.peers,m->online.visible,m->online.loading);
        else snprintf(line,sizeof line,"LOCAL PROFILES. NAMES ARE NOT RESERVED.");
        quest_vr_text(pixels,40,578,2,line,0xffc8beb4u);
        quest_vr_text(pixels,40,606,2,m->main_menu?"FREE ROAM: LOAD CAREER, THEN OPEN THIS MENU.":"BROWSE RACES KEEPS YOUR CAREER POSITION FOR THE RETURN.",0xffffd79fu);
        snprintf(line,sizeof line,"%.76s",m->list.auth_notice[0]?m->list.auth_notice:"RESERVED NAME? LINK ITS EXISTING KEYPHRASE BEFORE CONNECTING.");
        quest_vr_text(pixels,40,634,2,line,0xffc8beb4u);
        if(m->list.selected>=0 && m->list.selected<m->list.count) {
            const char *room=m->list.room[m->list.selected];
            snprintf(line,sizeof line,"ROOM: %.63s",*room?room:"PUBLIC (MAIN WORLD)");
            quest_vr_text_case(pixels,40,522,2,line,0xffc8beb4u);
        }
        if(m->online.notice[0]) {snprintf(line,sizeof line,"%.76s",m->online.notice);quest_vr_text(pixels,40,662,2,line,0xff8888ffu);}
    }
    if(m->list.error[0] && !m->name_guide) {
        char line[77];snprintf(line,sizeof line,"%.76s",m->list.error);
        quest_vr_text(pixels,40,662,2,line,0xff8888ffu);
    }
    quest_vr_text(pixels,40,696,3,"STICK / D-PAD: MOVE   A: SELECT   B: BACK",0xffffffffu);
}
