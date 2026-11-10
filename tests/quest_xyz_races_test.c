#include "quest_xyz_races.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void press(QuestXYZRaces *m,int key){quest_xyz_races_step(m,1,1,0);quest_xyz_races_step(m,2,1,key);}
int main(int argc,char **argv){
    QuestXYZRaces m={0};m.view.active=1;m.view.state=XYZ_RACES_READY;m.view.count=20;m.view.epoch=1;strcpy(m.view.name,"VR_TESTER");
    for(int i=0;i<20;++i){snprintf(m.view.rows[i].name,36,"RACE %02d",i+1);strcpy(m.view.rows[i].host,"VR_DRIVER");m.view.rows[i].players=2;m.view.rows[i].min_players=2;m.view.rows[i].max_players=6;}
    for(int i=0;i<20;++i){XyzRaceInfo *info=&m.view.info[i];info->parsed=info->compatible=1;info->mode=0;info->track=4014;info->laps=3;
        snprintf(info->track_name,sizeof info->track_name,"HARBOR LOOP %02d",i+1);}
    quest_xyz_races_open(&m,QUEST_VR_ACCEPT);quest_xyz_races_step(&m,1,1,QUEST_VR_ACCEPT);CHECK(!m.detail);
    press(&m,QUEST_VR_ACCEPT);CHECK(m.detail && !m.action && !strcmp(m.selected.name,"RACE 01"));
    CHECK(m.selected_info.compatible && m.selected_info.laps==3 && !strcmp(m.selected_info.track_name,"HARBOR LOOP 01"));
    press(&m,QUEST_VR_BACK);CHECK(m.open && !m.detail);
    m.row=19;press(&m,QUEST_VR_DOWN);CHECK(m.row==20);press(&m,QUEST_VR_ACCEPT);CHECK(!m.action);
    m.row=21;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_SEARCH && m.requested_start==20);
    quest_xyz_races_step(&m,1000,1,QUEST_VR_ACCEPT);CHECK(!m.action);
    XYZRaceBrowser page=m.view;page.state=XYZ_RACES_SEARCHING;page.count=0;
    quest_xyz_races_update(&m,&page);CHECK(m.row==1);press(&m,QUEST_VR_ACCEPT);CHECK(!m.action);
    page.state=XYZ_RACES_READY;page.start=20;page.count=3;quest_xyz_races_update(&m,&page);CHECK(m.row==4);
    press(&m,QUEST_VR_ACCEPT);CHECK(!m.action); // no next page after a short result
    m.row=3;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_SEARCH && !m.requested_start);
    m.row=5;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_SEARCH && m.requested_start==20);
    m.row=7;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_CLOSE);
    m.row=0;press(&m,QUEST_VR_ACCEPT);CHECK(m.detail);
    page.active=0;page.state=XYZ_RACES_ERROR;page.count=0;quest_xyz_races_update(&m,&page);CHECK(!m.detail && m.row==2);
    m.row=3;press(&m,QUEST_VR_ACCEPT);CHECK(!m.action);
    m.row=5;press(&m,QUEST_VR_ACCEPT);CHECK(!m.open && !m.action);
    quest_xyz_races_open(&m,0);quest_xyz_races_step(&m,1,0,QUEST_VR_ACCEPT);CHECK(!m.open && !m.action);
    page.active=1;page.state=XYZ_RACES_READY;page.start=0;page.count=20;quest_xyz_races_update(&m,&page);quest_xyz_races_open(&m,0);
    for(int i=0;i<20;++i)press(&m,QUEST_VR_DOWN);CHECK(m.row==20 && m.first==14);
    m.row=25;press(&m,QUEST_VR_DOWN);CHECK(!m.row && !m.first);
    page.start=INT32_MAX-20;quest_xyz_races_update(&m,&page);m.row=21;press(&m,QUEST_VR_ACCEPT);CHECK(!m.action);
    m.view.start=0;m.first=m.row=0;m.selected=m.view.rows[0];m.selected_info=m.view.info[0];
    m.detail=1;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_JOIN && !m.password[0]);
    m.selected.system_flags=0x80000;press(&m,QUEST_VR_ACCEPT);CHECK(!m.action && !m.editing);
    m.selected.system_flags=0x10000;press(&m,QUEST_VR_ACCEPT);CHECK(m.editing && !m.action);
    press(&m,QUEST_VR_ACCEPT);CHECK(!strcmp(m.password,"A"));press(&m,128);press(&m,QUEST_VR_ACCEPT);CHECK(!strcmp(m.password,"Aa"));
    press(&m,128);press(&m,QUEST_VR_ACCEPT);CHECK(!strcmp(m.password,"Aa "));
    m.key=38;press(&m,QUEST_VR_ACCEPT);CHECK(!strcmp(m.password,"Aa"));m.key=39;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_JOIN && !m.editing);
    page=m.view;page.state=XYZ_RACES_JOINING;quest_xyz_races_update(&m,&page);CHECK(!m.password[0] && !m.detail);
    page.state=XYZ_RACES_IN_LOBBY;quest_xyz_races_update(&m,&page);CHECK(!m.row);
    press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_SET_READY);m.row=1;press(&m,QUEST_VR_ACCEPT);CHECK(!m.action);
    m.view.startable=1;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_START);
    m.row=2;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_LEAVE);m.row=3;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_CLOSE);
    page.state=XYZ_RACES_RACING;quest_xyz_races_update(&m,&page);press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_LEAVE);
    m.row=2;press(&m,QUEST_VR_ACCEPT);CHECK(!m.open && !m.action);
    page.state=XYZ_RACES_READY;quest_xyz_races_update(&m,&page);quest_xyz_races_open(&m,0);m.selected=m.view.rows[0];m.selected_info=m.view.info[0];
    m.view.track_count=2;m.view.tracks[0]=(XYZRaceChoice){0,4014,1,1,10,6,"RESORT LOOP","RESORT LOOP REVERSE"};
    m.view.tracks[1]=(XYZRaceChoice){2,4201,0,1,1,4,"BAYVIEW BRIDGE","BAYVIEW BRIDGE"};
    m.row=(int)m.view.count+3;press(&m,QUEST_VR_ACCEPT);CHECK(m.creating && m.options.track==4014);
    press(&m,QUEST_VR_RIGHT);CHECK(m.options.mode==2 && m.options.track==4201 && m.options.laps==1 && m.options.max_players==4);
    m.form_row=2;press(&m,QUEST_VR_ACCEPT);CHECK(!m.options.reverse);
    m.form_row=8;press(&m,QUEST_VR_ACCEPT);CHECK(m.editing==2);press(&m,QUEST_VR_ACCEPT);press(&m,128);press(&m,QUEST_VR_ACCEPT);
    CHECK(!strcmp(m.password,"Aa"));m.key=39;press(&m,QUEST_VR_ACCEPT);CHECK(!m.editing && !strcmp(m.options.password,"Aa") && !m.password[0]);
    m.form_row=9;press(&m,QUEST_VR_ACCEPT);CHECK(m.action==XYZ_RACES_CREATE && m.options.track==4201);
    page=m.view;page.state=XYZ_RACES_JOINING;quest_xyz_races_update(&m,&page);CHECK(!m.creating && !m.options.password[0]);
    page.state=XYZ_RACES_READY;quest_xyz_races_update(&m,&page);
    for(int p=1;p<argc && p<=6;++p){
        uint32_t *pixels=malloc(1024*768*4);CHECK(pixels);m.detail=p==2;
        if(p==3){m.view.active=0;m.view.state=XYZ_RACES_ERROR;m.view.count=0;m.row=5;strcpy(m.view.notice,"The race connection ended. Reopen the browser to reconnect.");}
        if(p==4){m.view.active=1;m.view.state=XYZ_RACES_IN_LOBBY;m.row=0;m.view.local_ready=1;m.view.lobby.players=6;strcpy(m.view.race.track_name,"HARBOR LOOP");m.view.notice[0]=0;
            for(unsigned i=0;i<6;++i){snprintf(m.view.lobby.player[i],16,"DRIVER_%u",i);m.view.lobby.player_flags[i]=i%2?0:0x08000000;}}
        if(p==5){m.editing=1;m.bank=1;m.key=0;strcpy(m.password,"secret");}
        if(p==6){m.editing=0;m.view.state=XYZ_RACES_READY;m.creating=1;m.form_row=1;m.options.min_players=2;m.options.max_players=6;m.options.laps=3;quest_xyz_races_choose_track(&m,0);}
        quest_xyz_races_panel(pixels,&m);FILE *f=fopen(argv[p],"wb");CHECK(f);fprintf(f,"P6\n1024 768\n255\n");
        for(int i=0;i<1024*768;++i){unsigned char rgb[3]={(pixels[i]>>16)&255,(pixels[i]>>8)&255,pixels[i]&255};fwrite(rgb,1,3,f);}fclose(f);free(pixels);
    }
    puts("Race browser UI: pagination, copied details, asynchronous refresh selection, held input and context loss passed");return 0;
}
