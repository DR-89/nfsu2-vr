#include "quest_xyz_profiles.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static void press(QuestXYZProfiles *m,int keys) { quest_xyz_step(m,1,1,0);quest_xyz_step(m,2,1,keys); }
static int browser_calls,browser_result,browser_wrong_url;
static int open_browser(const char *url){
    ++browser_calls;browser_wrong_url|=strcmp(url,"https://nfsu2.xyz/")!=0;return browser_result;
}
int main(int argc,char **argv) {
    QuestXYZProfiles m={0};m.list.selected=-1;
    quest_xyz_open(&m,QUEST_VR_ACCEPT);
    quest_xyz_step(&m,1,1,QUEST_VR_ACCEPT);CHECK(!m.editing);
    press(&m,QUEST_VR_ACCEPT);CHECK(m.editing==XYZ_PROFILE_CREATE);
    press(&m,QUEST_VR_ACCEPT);CHECK(!strcmp(m.text,"A"));
    quest_xyz_step(&m,800,1,QUEST_VR_ACCEPT);CHECK(!strcmp(m.text,"A"));
    m.key=39;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_PROFILE_CREATE && !m.editing);
    m.list.count=1;m.list.selected=0;strcpy(m.list.name[0],"Driver");m.row=0;
    press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_PROFILE_SELECT && m.index==0);
    m.row=2;press(&m,QUEST_VR_ACCEPT);CHECK(m.editing==XYZ_PROFILE_RENAME && !strcmp(m.text,"Driver"));
    m.key=38;press(&m,QUEST_VR_ACCEPT);CHECK(!strcmp(m.text,"Drive"));
    press(&m,QUEST_VR_BACK);CHECK(!m.editing && !m.action && m.open);
    m.row=4;press(&m,QUEST_VR_ACCEPT);CHECK(m.confirming && m.key==0);
    press(&m,QUEST_VR_ACCEPT);CHECK(!m.confirming && !m.action); // Keep is the default.
    press(&m,QUEST_VR_ACCEPT);press(&m,QUEST_VR_RIGHT);press(&m,QUEST_VR_ACCEPT);
    CHECK(m.action==XYZ_PROFILE_DELETE && m.index==0);
    m.row=3;press(&m,QUEST_VR_ACCEPT);CHECK(m.editing==XYZ_PROFILE_ROOM);
    m.text[0]=0;m.key=39;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_PROFILE_ROOM && !m.text[0]);
    m.row=5;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_PROFILE_CONNECT && m.index==0);
    quest_xyz_step(&m,900,1,QUEST_VR_ACCEPT);CHECK(!m.action); // holding cannot reconnect repeatedly
    m.main_menu=1;press(&m,QUEST_VR_ACCEPT);CHECK(!m.action);
    m.row=6;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_PROFILE_RACES && m.index==0);
    m.main_menu=0;
    press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_PROFILE_RACES && m.index==0);
    m.row=7;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_PROFILE_DISCONNECT);
    m.list.selected=0;m.row=8;press(&m,QUEST_VR_ACCEPT);CHECK(m.editing==XYZ_PROFILE_AUTH && !m.text[0]);
    strcpy(m.text,"ACID-ACORN-ACRE-ACTS-AFAR-AFFIX-AGED-AGENT");
    char display[64];quest_xyz_display_text(&m,display);CHECK(!strcmp(display,"****-*****-****-****-****-*****-****-*****"));
    quest_xyz_step(&m,1000,1,QUEST_XYZ_REVEAL);quest_xyz_display_text(&m,display);CHECK(!strcmp(display,m.text));
    quest_xyz_step(&m,1001,1,0);quest_xyz_display_text(&m,display);CHECK(!strstr(display,"ACID"));
    size_t phrase_length=strlen(m.text);
    m.key=0;press(&m,QUEST_VR_ACCEPT);CHECK(strlen(m.text)==phrase_length+1); // exceeds ordinary name limit
    press(&m,QUEST_VR_BACK);CHECK(m.open && !m.editing);
    for(size_t i=0;i<sizeof m.text;++i)CHECK(!m.text[i]);
    press(&m,QUEST_VR_ACCEPT);m.key=39;press(&m,QUEST_VR_ACCEPT);
    CHECK(m.action==XYZ_PROFILE_AUTH && m.index==0 && !m.text[0]); // explicit empty save unlinks
    m.list.auth_busy[0]=1;press(&m,QUEST_VR_ACCEPT);CHECK(!m.editing && !m.action);m.list.auth_busy[0]=0;
    press(&m,QUEST_VR_ACCEPT);strcpy(m.text,"test input");quest_xyz_step(&m,3,0,0);CHECK(!m.open && !m.text[0]);
    quest_xyz_open(&m,0);m.row=9;press(&m,QUEST_VR_ACCEPT);CHECK(m.name_guide && m.open);
    press(&m,QUEST_VR_ACCEPT);CHECK(!m.action && !quest_xyz_open_website(&m,open_browser) && !browser_calls);
    m.main_menu=1;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_PROFILE_WEBSITE);
    CHECK(quest_xyz_open_website(&m,open_browser) && browser_calls==1 && !browser_wrong_url);
    quest_xyz_step(&m,900,1,QUEST_VR_ACCEPT);CHECK(!m.action); // a held button never opens extra browser tabs
    for(int state=XYZ_ONLINE_CONNECTING;state<=XYZ_ONLINE_RETRY;++state){
        m.online.state=state;press(&m,QUEST_VR_ACCEPT);CHECK(!m.action);
        CHECK(!quest_xyz_open_website(&m,open_browser) && browser_calls==1);
    }
    m.online.state=XYZ_ONLINE_OFFLINE;m.races_active=1;
    CHECK(!quest_xyz_open_website(&m,open_browser) && browser_calls==1);m.races_active=0;
    browser_result=-1;CHECK(!quest_xyz_open_website(&m,open_browser) && browser_calls==2 && strstr(m.guide_notice,"Could not open"));
    m.list.selected=-1;CHECK(!quest_xyz_open_website(&m,open_browser) && browser_calls==2);m.list.selected=0;
    press(&m,QUEST_VR_DOWN);press(&m,QUEST_VR_ACCEPT);CHECK(!m.name_guide && m.editing==XYZ_PROFILE_AUTH && !m.text[0]);
    press(&m,QUEST_VR_BACK);m.row=9;press(&m,QUEST_VR_ACCEPT);CHECK(m.name_guide);
    press(&m,QUEST_VR_BACK);CHECK(m.open && !m.name_guide && !m.action);
    m.row=10;press(&m,QUEST_VR_ACCEPT);CHECK(!m.open);
    quest_xyz_open(&m,0);
    m.list.selected=-1;m.row=5;press(&m,QUEST_VR_ACCEPT);CHECK(!m.action);
    m.row=7;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_PROFILE_DISCONNECT);
    quest_xyz_step(&m,3,0,0);CHECK(!m.open);
    QuestVRMenu menu={0};QuestVRSettings settings=quest_vr_defaults();menu.open=1;menu.row=QUEST_VR_PROFILES_ROW;
    CHECK(quest_vr_menu_step(&menu,&settings,1,1,QUEST_VR_ACCEPT)==(4|QUEST_VR_OPEN_PROFILES));
    CHECK(quest_vr_menu_step(&menu,&settings,2,1,QUEST_VR_ACCEPT)==4);
    for(int page=1;page<argc && page<=6;++page) {
        uint32_t *pixels=malloc(1024*768*4);CHECK(pixels);
        memset(&m,0,sizeof m);m.open=1;m.list.count=8;m.list.selected=0;m.row=8;
        for(int i=0;i<8;++i)snprintf(m.list.name[i],16,"VR_DRIVER_%d",i+1);
        if(page==2) {m.editing=XYZ_PROFILE_CREATE;m.key=39;strcpy(m.text,"VR_DRIVER");}
        if(page==3) {m.confirming=1;m.key=0;}
        if(page==4) {m.editing=XYZ_PROFILE_AUTH;m.key=39;strcpy(m.text,"ACID-ACORN-ACRE-ACTS-AFAR-AFFIX-AGED-AGENT");}
        if(page>=5){m.name_guide=1;m.main_menu=page==5;strcpy(m.guide_notice,"Browser requested. Finish on the site, then return here.");}
        quest_xyz_panel(pixels,&m);
        FILE *f=fopen(argv[page],"wb");CHECK(f);fprintf(f,"P6\n1024 768\n255\n");
        for(int i=0;i<1024*768;++i){unsigned char rgb[3]={(pixels[i]>>16)&255,(pixels[i]>>8)&255,pixels[i]&255};fwrite(rgb,1,3,f);}
        fclose(f);free(pixels);
    }
    puts("Online profile UI: controller keyboard, selection, cancellation and confirmed deletion passed");return 0;
}
