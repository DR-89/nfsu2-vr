#include "xyz_game_api.h"
#include "xyz_races_api.h"
#include "xyz_xbox_race.h"
#include "xyz_xbox_career.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static XyzXboxLobby community_engine_lobby;
static XyzLobbyGuest community_engine_guest;
static XyzXboxRace community_engine_race;
static XyzRaceGuest community_race_guest;
static XyzCareerGuest community_career_guest;
int xyz_online_fleet_retired(void){return 1;}
static XyzXboxGuest memory;
static unsigned joins,ready_calls,starts,leaves,race_begins,race_returns,race_closes;
static unsigned creates,settings_applied,settings_restored;
static int allow_create=1;
static int allow_join=1,allow_pad=1,reject_join,room_ready,room_host,allow_race=1,changed_rules;
static int read_memory(void *c,uint32_t at,void *out,size_t n){
    (void)c;
    if(at>=0x427ff8 && at<0x427ffc && n==1){*(uint8_t*)out=allow_pad && at==0x427ff8?0:255;return 1;}
    if(at==0x43d110 && n==8){uint32_t h[2]={0x10000,1};memcpy(out,h,8);return 1;}
    if(at==0x10000 && n<=0x128){uint8_t b[0x128]={0};uint16_t id=4014;uint32_t mode=1;
        strcpy((char*)b,"fixture");memcpy(b+0x8a,&id,2);memcpy(b+0x94,&mode,4);b[0x86]=1;memcpy(out,b,n);return 1;}
    return 0;
}
static XYZOnlineStatus online;
static unsigned connections,disconnects,begins,searches,closes,start;
static int allow_connect=1,allow_begin=1,allow_search=1,allow_rows=1;
int xyz_online_connect_races_claimed(const char *n,const char *r,const char *p,const char *d,const char *claim){
    (void)n;(void)r;(void)p;(void)d;CHECK(claim && !*claim);++connections;online.state=XYZ_ONLINE_CONNECTING;return allow_connect;
}
void xyz_online_disconnect(void){++disconnects;memset(&online,0,sizeof online);}
void xyz_online_snapshot(XYZOnlineStatus *s){*s=online;}
int xyz_xbox_lobby_begin(const XyzLobbyGuest *g,XyzXboxLobby *s,const XyzXboxFrame *f,const char *n,uint32_t gen,uint32_t cb,uint64_t now){
    (void)g;(void)f;(void)n;(void)cb;(void)now;++begins;if(!allow_begin)return 0;
    s->stage=XYZ_LOBBY_CONNECTING;s->generation=gen;return 1;
}
void xyz_xbox_lobby_tick(const XyzLobbyGuest *g,XyzXboxLobby *s,uint32_t gen,uint64_t now){
    (void)g;(void)now;if(s->generation!=gen){s->stage=XYZ_LOBBY_FAILED;return;}
    if(s->stage==XYZ_LOBBY_CONNECTING || s->stage==XYZ_LOBBY_SEARCHING)s->stage=XYZ_LOBBY_READY;
    else if(s->stage==XYZ_LOBBY_JOINING || s->stage==XYZ_LOBBY_CREATING){s->stage=reject_join?XYZ_LOBBY_READY:XYZ_LOBBY_IN_GAME;s->error=reject_join?XYZ_LOBBY_REJECTED:XYZ_LOBBY_OK;s->reply_code=0x66756c6c;}
    else if(s->stage==XYZ_LOBBY_LEAVING)s->stage=XYZ_LOBBY_READY;
    else if(s->stage==XYZ_LOBBY_STARTING)s->stage=XYZ_LOBBY_PLAY_READY;
}
void xyz_xbox_lobby_close(const XyzLobbyGuest *g,XyzXboxLobby *s){(void)g;++closes;memset(s,0,sizeof *s);}
int xyz_xbox_lobby_search(const XyzLobbyGuest *g,XyzXboxLobby *s,unsigned offset,unsigned count,uint64_t now){
    (void)g;(void)now;++searches;start=offset;if(!allow_search || count!=20)return 0;s->stage=XYZ_LOBBY_SEARCHING;s->error=XYZ_LOBBY_OK;return 1;
}
int xyz_xbox_lobby_snapshot(const XyzLobbyGuest *g,const XyzXboxLobby *s,XyzLobbySnapshot *out){
    (void)g;memset(out,0,sizeof *out);out->listed_games=start?3:20;strcpy(out->persona,"AssignedName");
    if(s->stage==XYZ_LOBBY_IN_GAME || s->stage==XYZ_LOBBY_STARTING || s->stage==XYZ_LOBBY_PLAY_READY){
        out->players=2;strcpy(out->player[room_host?0:1],"AssignedName");strcpy(out->player[room_host?1:0],"Other");
        out->player_flags[0]=out->player_flags[1]=room_ready?0x08000000:0;
    }return 1;
}
int xyz_xbox_lobby_game(const XyzLobbyGuest *g,const XyzXboxLobby *s,unsigned index,XyzLobbyGame *out){
    (void)g;(void)s;memset(out,0,sizeof *out);out->game_id=(int32_t)(start+index);strcpy(out->name,"Example");
    strcpy(out->params,changed_rules?"TRACK=4015 DIR=0 LAPS=3":"TRACK=4014 DIR=0 LAPS=3");
    out->custom_flags=0x040000f3;out->players=1;out->min_players=2;out->max_players=6;return allow_rows;
}
int xyz_xbox_lobby_current_game(const XyzLobbyGuest *g,const XyzXboxLobby *s,XyzLobbyGame *out){return xyz_xbox_lobby_game(g,s,0,out);}
int xyz_xbox_lobby_join_game(const XyzLobbyGuest *g,XyzXboxLobby *s,unsigned i,const XyzLobbyGame *selected,const char *password,uint64_t now){
    (void)g;(void)i;(void)selected;(void)password;(void)now;++joins;if(!allow_join)return 0;s->stage=XYZ_LOBBY_JOINING;return 1;
}
int xyz_xbox_lobby_select_controller(const XyzLobbyGuest *g,XyzXboxLobby *s,unsigned p){(void)g;(void)s;return !p && allow_pad;}
int xyz_xbox_lobby_ready(const XyzLobbyGuest *g,XyzXboxLobby *s,int r){(void)g;(void)s;++ready_calls;room_ready=r;return 1;}
int xyz_xbox_lobby_start(const XyzLobbyGuest *g,XyzXboxLobby *s,uint64_t now){(void)g;(void)now;++starts;if(!room_host || !room_ready)return 0;s->stage=XYZ_LOBBY_STARTING;return 1;}
int xyz_xbox_lobby_leave(const XyzLobbyGuest *g,XyzXboxLobby *s,uint64_t now){(void)g;(void)now;++leaves;s->stage=XYZ_LOBBY_LEAVING;return 1;}
int xyz_xbox_race_begin(const XyzRaceGuest *g,XyzXboxRace *r,XyzXboxLobby *s,const XyzXboxFrame *f){
    (void)g;(void)f;++race_begins;r->captured=1;r->stage=XYZ_RACE_ACTIVE;s->stage=XYZ_LOBBY_RACE;return allow_race;
}
int xyz_xbox_race_return(const XyzRaceGuest *g,XyzXboxRace *r,const XyzXboxFrame *f){(void)g;(void)r;(void)f;++race_returns;return 1;}
int xyz_xbox_race_close(const XyzRaceGuest *g,XyzXboxRace *r,const XyzXboxFrame *f){
    (void)g;if(f->world || f->flow!=3)return 0;++race_closes;r->captured=0;r->stage=XYZ_RACE_CLOSED;return 1;
}
int xyz_xbox_race_settings_apply(const XyzLobbyGuest *g,const XyzXboxLobby *l,XyzRaceSettingsLease *s,const XyzXboxFrame *f,const XyzRaceSettings *o){
    (void)g;CHECK(l->stage==XYZ_LOBBY_READY && f->flow==3 && !s->captured && o->track==4014);
    s->captured=1;++settings_applied;return 1;
}
int xyz_xbox_race_settings_join(const XyzLobbyGuest *g,const XyzXboxLobby *l,XyzRaceSettingsLease *s,const XyzXboxFrame *f,const XyzRaceSettings *o){
    (void)g;CHECK(l->stage==XYZ_LOBBY_READY && f->flow==3 && !s->captured && o->track==4014);
    s->captured=1;return 1;
}
int xyz_xbox_race_settings_release(const XyzLobbyGuest *g,XyzRaceSettingsLease *s,const XyzXboxFrame *f){
    (void)g;if(!s->captured)return 1;if(f->flow!=3 || f->world)return 0;s->captured=0;++settings_restored;return 1;
}
int xyz_xbox_lobby_create(const XyzLobbyGuest *g,XyzXboxLobby *s,unsigned performance,uint64_t tick){
    (void)g;(void)performance;(void)tick;++creates;if(!allow_create)return 0;s->stage=XYZ_LOBBY_CREATING;room_host=1;return 1;
}
#include "xyz_races_bridge.inl"
static XyzXboxFrame frame;
static XYZRaceBrowser view;
static uint64_t now=1;
static const char *id="1234567890abcdef1234567890abcdef";
static void step(int main){community_races_frame(&frame,main,0,now++);xyz_races_snapshot(&view);}
static int open_browser(void){return xyz_races_open("Driver","",id,id);}
static void connected(void){online.state=XYZ_ONLINE_CONNECTED;online.lan_ready=1;online.generation=1;}
int main(void){
    memory.read=read_memory;community_engine_guest.memory=&memory;
    frame.flow=3;frame.status=XYZ_XBOX_FRONTEND;
    CHECK(open_browser());step(0);CHECK(view.state==XYZ_RACES_ERROR && !view.active && !connections && !closes);
    CHECK(open_browser() && xyz_races_close());step(1);CHECK(view.state==XYZ_RACES_OFFLINE && !connections);
    community_engine_lobby.stage=XYZ_LOBBY_IN_GAME;
    CHECK(open_browser());step(1);CHECK(view.state==XYZ_RACES_ERROR && !connections && !closes);
    memset(&community_engine_lobby,0,sizeof community_engine_lobby);
    CHECK(open_browser());step(1);CHECK(connections==1 && view.state==XYZ_RACES_CONNECTING);
    online.state=XYZ_ONLINE_QUEUED;online.queue_position=2;online.queue_total=3;step(1);CHECK(strstr(view.notice,"2 of 3") && !begins);
    now+=120000000;step(1);CHECK(view.active && view.state==XYZ_RACES_CONNECTING && !disconnects);
    connected();step(1);CHECK(view.state==XYZ_RACES_SEARCHING && begins==1 && searches==1);
    step(1);CHECK(view.state==XYZ_RACES_READY && view.count==20 && !strcmp(view.name,"AssignedName"));
    CHECK(xyz_races_search(view.epoch,20));step(1);CHECK(view.state==XYZ_RACES_SEARCHING && start==20);
    step(1);CHECK(view.state==XYZ_RACES_READY && view.start==20 && view.count==3 && view.rows[2].game_id==22);
    CHECK(xyz_races_search(view.epoch,0) && xyz_races_close());step(1);
    CHECK(!view.active && view.state==XYZ_RACES_OFFLINE && closes==1 && disconnects==1 && searches==2);
    CHECK(open_browser());step(1);connected();step(1);step(1);CHECK(view.state==XYZ_RACES_READY);
    online.generation=2;step(1);CHECK(!view.active && view.state==XYZ_RACES_ERROR && closes==2 && disconnects==2);
    CHECK(open_browser());step(1);step(0);CHECK(view.state==XYZ_RACES_OFFLINE && closes==3 && disconnects==3);
    CHECK(open_browser());step(1);now+=45000001;step(1);CHECK(view.state==XYZ_RACES_ERROR && closes==4 && disconnects==4);
    allow_begin=0;CHECK(open_browser());step(1);connected();step(1);CHECK(!view.active && view.state==XYZ_RACES_ERROR);allow_begin=1;
    allow_rows=0;CHECK(open_browser());step(1);connected();step(1);step(1);CHECK(!view.active && view.state==XYZ_RACES_ERROR);allow_rows=1;
    allow_search=0;CHECK(open_browser());step(1);connected();step(1);CHECK(!view.active && view.state==XYZ_RACES_ERROR);allow_search=1;
    allow_connect=0;unsigned old=disconnects;CHECK(open_browser());step(1);CHECK(view.state==XYZ_RACES_ERROR && !view.active && disconnects==old);
    allow_connect=1;
    CHECK(open_browser());step(1);online.state=XYZ_ONLINE_STOPPED;
    strcpy(online.notice,"Name reserved. LINK KEYPHRASE or choose another player name.");
    step(1);CHECK(!view.active && view.state==XYZ_RACES_ERROR && strstr(view.notice,"Name reserved."));
    CHECK(open_browser());step(1);connected();step(1);step(1);CHECK(view.info[0].compatible);
    CHECK(xyz_races_join(view.epoch,0,&view.rows[0],""));changed_rules=1;step(1);CHECK(!joins && view.state==XYZ_RACES_READY && strstr(view.notice,"changed"));changed_rules=0;
    step(1);reject_join=1;CHECK(xyz_races_join(view.epoch,0,&view.rows[0],""));step(1);CHECK(joins==1 && view.state==XYZ_RACES_JOINING);
    step(1);CHECK(view.state==XYZ_RACES_SEARCHING && strstr(view.notice,"busy"));step(1);reject_join=0;
    CHECK(xyz_races_join(view.epoch,0,&view.rows[0],""));step(1);step(1);CHECK(view.state==XYZ_RACES_IN_LOBBY && view.lobby.players==2 && !view.host);
    CHECK(!xyz_races_start(view.epoch));allow_pad=0;CHECK(xyz_races_ready(view.epoch,1));step(1);CHECK(!ready_calls && strstr(view.notice,"controller"));
    allow_pad=1;CHECK(xyz_races_ready(view.epoch,1));step(1);CHECK(ready_calls==1 && view.local_ready);
    CHECK(xyz_races_leave(view.epoch));step(1);CHECK(leaves==1 && view.state==XYZ_RACES_RETURNING);step(1);step(1);CHECK(view.state==XYZ_RACES_READY);
    room_host=1;CHECK(xyz_races_join(view.epoch,0,&view.rows[0],""));step(1);step(1);CHECK(view.host && view.startable);
    CHECK(xyz_races_start(view.epoch));step(1);CHECK(starts==1 && view.state==XYZ_RACES_STARTING);step(1);CHECK(race_begins==1 && view.state==XYZ_RACES_RACING);
    frame.flow=5;frame.status=XYZ_XBOX_FRONTEND;step(0);CHECK(view.state==XYZ_RACES_RACING && !race_returns && !race_closes);
    frame.flow=6;frame.status=XYZ_XBOX_OTHER_RACE;frame.world=0x10000;frame.car=0x20000;step(0);CHECK(view.state==XYZ_RACES_RACING && view.active);
    old=disconnects;CHECK(xyz_races_close());step(0);CHECK(view.state==XYZ_RACES_CLOSING && disconnects==old && race_returns);
    step(0);CHECK(view.active && disconnects==old);frame.flow=3;frame.status=XYZ_XBOX_FRONTEND;frame.world=frame.car=0;step(1);
    CHECK(!view.active && disconnects==old+1 && race_closes==1 && !community_engine_race.captured);
    CHECK(open_browser());step(1);connected();step(1);step(1);CHECK(xyz_races_join(view.epoch,0,&view.rows[0],""));step(1);step(1);
    CHECK(xyz_races_start(view.epoch));step(1);step(1);frame.flow=6;frame.status=XYZ_XBOX_OTHER_RACE;frame.world=0x10000;frame.car=0x20000;step(0);
    CHECK(xyz_races_leave(view.epoch));step(0);CHECK(view.state==XYZ_RACES_RETURNING && view.active);
    frame.flow=3;frame.status=XYZ_XBOX_FRONTEND;frame.world=frame.car=0;step(1);step(1);step(1);CHECK(view.state==XYZ_RACES_READY && view.active && race_closes==2);
    CHECK(xyz_races_close());step(1);CHECK(!view.active);
    CHECK(open_browser());step(1);connected();step(1);step(1);CHECK(view.track_count==1 && view.tracks[0].id==4014);
    CHECK(settings_restored==4);settings_restored=0;
    XyzRaceSettings options={0};options.track=4014;options.laps=2;options.min_players=2;options.max_players=4;options.unrestricted=1;
    strcpy(options.password,"private");allow_create=0;CHECK(xyz_races_create(view.epoch,&options));step(1);
    CHECK(view.state==XYZ_RACES_READY && creates==1 && settings_applied==1 && settings_restored==1);
    allow_create=1;CHECK(xyz_races_create(view.epoch,&options));step(1);step(1);CHECK(view.state==XYZ_RACES_IN_LOBBY && view.host && settings_restored==1);
    CHECK(xyz_races_leave(view.epoch));step(1);step(1);step(1);CHECK(view.state==XYZ_RACES_READY && settings_restored==2);
    CHECK(xyz_races_create(view.epoch,&options));step(1);step(1);CHECK(xyz_races_close());step(1);CHECK(!view.active && settings_restored==3);
    puts("Race browser: owned join, changed rules, rejection, ready, start, race return, deferred disconnect and reconnect passed");return 0;
}
