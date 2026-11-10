#include "xyz_xbox_race.h"
#include <string.h>

enum {NETGAME=0x456290,MODE=0x41c338,PROFILE_STATE=0x41c38c,TICK=0x3a53bc};
static const XyzXboxGuest *memory(const XyzRaceGuest *g){return g && g->lobby?g->lobby->memory:NULL;}
static int read_bytes(const XyzRaceGuest *g,uint32_t at,void *out,size_t size){
    const XyzXboxGuest *m=memory(g);
    return m && m->read && xyz_xbox_address(at,size) && m->read(m->context,at,out,size);
}
static int write_bytes(const XyzRaceGuest *g,uint32_t at,const void *in,size_t size){
    const XyzXboxGuest *m=memory(g);
    return m && m->write && xyz_xbox_address(at,size) && m->write(m->context,at,in,size);
}
static int call(const XyzRaceGuest *g,enum XyzRaceOp op,uint32_t self,unsigned count,const uint32_t *args){
    uint32_t result=0;const XyzXboxGuest *m=memory(g);
    return m && g->call && g->call(m->context,op,self,count,args,&result);
}
static int error(XyzXboxRace *s,enum XyzLobbyError e){s->error=e;return 0;}
static int frontend(const XyzRaceGuest *g,const XyzXboxFrame *frame){
    if(!frame || frame->status!=XYZ_XBOX_FRONTEND || frame->flow!=3)return 0;
    const uint32_t gates[]={0x3fbbf4,0x3fbbfc,0x428e14,0x4493e0,0x44940c};
    for(unsigned i=0;i<sizeof gates/sizeof gates[0];++i){uint32_t value;
        if(!read_bytes(g,gates[i],&value,4) || value)return 0;
    }
    return 1;
}
static int empty_netgame(const XyzRaceGuest *g){
    uint32_t words[8];uint8_t players;
    if(!read_bytes(g,NETGAME,words,sizeof words) || !read_bytes(g,NETGAME+0x20,&players,1) || players)return 0;
    for(unsigned i=0;i<8;++i)if(words[i])return 0;
    return 1;
}
int xyz_xbox_race_begin(const XyzRaceGuest *g,XyzXboxRace *s,XyzXboxLobby *lobby,const XyzXboxFrame *frame){
    XyzLobbySnapshot roster;uint32_t protocol,peers,host,controller,seed;uint8_t device;
    if(!s || s->stage!=XYZ_RACE_EMPTY || !lobby || !lobby->initialized || !lobby->play_received ||
       lobby->stage!=XYZ_LOBBY_PLAY_READY || !lobby->generation || !frontend(g,frame) || !empty_netgame(g) ||
       !read_bytes(g,lobby->manager+0x44,&protocol,4) || !protocol || protocol!=lobby->protocol ||
       !xyz_xbox_lobby_snapshot(g->lobby,lobby,&roster) || roster.game_id<0 || roster.players<2 || roster.players>6 ||
       !read_bytes(g,0x3a5320,&peers,4) || peers!=roster.players-1 ||
       !read_bytes(g,0x42a48c,&host,4) || !!host!=!strcmp(roster.player[0],lobby->name) ||
       !read_bytes(g,0x406c98,&controller,4) || controller>3 ||
       !read_bytes(g,0x427ff8+controller,&device,1) || device==UINT8_MAX ||
       !read_bytes(g,lobby->games+0x13c,&seed,4))return 0;
    unsigned local=0;
    for(unsigned i=0;i<roster.players;++i){
        if(!roster.player[i][0])return 0;
        local+=!strcmp(roster.player[i],lobby->name);
        for(unsigned j=0;j<i;++j)if(!strcmp(roster.player[i],roster.player[j]))return 0;
    }
    if(local!=1 || !read_bytes(g,NETGAME,s->previous_netgame,sizeof s->previous_netgame) ||
       !read_bytes(g,TICK,&s->previous_tick,1) || !read_bytes(g,MODE,&s->previous_mode,4) ||
       !read_bytes(g,PROFILE_STATE,s->previous_profile,sizeof s->previous_profile))return 0;
    s->captured=1;s->stage=XYZ_RACE_PREPARING;s->error=XYZ_LOBBY_OK;
    s->generation=lobby->generation;s->players=roster.players;s->host=!!host;
    char names[6][16]={{0}};memcpy(names,roster.player,roster.players*16);
    uint32_t address=lobby->arena+0x2800,mode=8,profile=3;
    if(!write_bytes(g,address,names,sizeof names))return error(s,XYZ_LOBBY_MEMORY);
    if(!call(g,XYZ_R_NAMES,NETGAME,1,&address))return error(s,XYZ_LOBBY_RETAIL);
    if(!write_bytes(g,NETGAME+0xc4,&seed,4) || !write_bytes(g,MODE,&mode,4))return error(s,XYZ_LOBBY_MEMORY);
    s->mode_written=1;
    if(!call(g,XYZ_R_BEGIN,NETGAME,0,NULL))return error(s,XYZ_LOBBY_RETAIL);
    s->begun=1;
    if(!call(g,XYZ_R_CONFIGURE,NETGAME,1,&s->players) || !xyz_xbox_lobby_enter_race(g->lobby,lobby))return error(s,XYZ_LOBBY_RETAIL);
    s->lobby_released=1;
    /* Mark the owned fields before calling retail so a failed subsequent
     * read still leaves a precise conditional rollback value. */
    memcpy(s->written_profile,s->previous_profile,12);
    s->written_profile[0]=0;
    memcpy(s->written_profile+8,s->previous_profile+4,4);memcpy(s->written_profile+4,&profile,4);
    s->profile_written=1;
    if(!call(g,XYZ_R_PROFILE,0x41c340,1,&profile))return error(s,XYZ_LOBBY_RETAIL);
    s->coordinator=1;
    if(!call(g,XYZ_R_INIT,NETGAME,0,NULL))return error(s,XYZ_LOBBY_RETAIL);
    if(!s->host && !xyz_xbox_lobby_resolve_virtual(g->lobby,lobby,0x42cd78,0,3658))return error(s,XYZ_LOBBY_RETAIL);
    s->stage=XYZ_RACE_ACTIVE;return 1;
}
int xyz_xbox_race_return(const XyzRaceGuest *g,XyzXboxRace *s,const XyzXboxFrame *frame){
    uint32_t mode,pending;
    if(!s || !s->captured || !frame || (s->stage!=XYZ_RACE_ACTIVE && s->stage!=XYZ_RACE_RETURNING &&
       !(s->stage==XYZ_RACE_PREPARING && s->coordinator)) ||
       !read_bytes(g,MODE,&mode,4) || mode!=8 || !read_bytes(g,0x428e14,&pending,4))return 0;
    if(s->stage==XYZ_RACE_RETURNING)return 1;
    if(frontend(g,frame)){s->stage=XYZ_RACE_RETURNING;return 1;}
    /* The generic frame sampler also calls loader flows 4/5 "frontend".
     * Returning during partial world/audio construction tears down resources
     * which are not live yet. Wait for the loaded driving world, or for the
     * stable frontend above; callers keep pumping while cleanup is pending. */
    if(frame->flow!=6 || !frame->world || !frame->car || (pending && pending!=0x18b1a0))return 0;
    /* Retail Pause::Quit first selects its next frontend package (0xc1b20,
     * 0x113cb0, 0x118be0). Omitting it replays the startup logos. Our native
     * browser lives on UI_Main, so select that audited stock string instead
     * of the retired Xbox Live UI. Restore only our still-owned selector. */
    uint32_t main_screen=0x34c7a0;
    if(!s->screen_written){
        if(!read_bytes(g,0x3fbd0c,&s->previous_screen,4))return error(s,XYZ_LOBBY_MEMORY);
        s->screen_written=1;
    }
    if(!call(g,XYZ_R_RETURN_SCREEN,0,1,&main_screen))return error(s,XYZ_LOBBY_RETAIL);
    if(!pending && !call(g,XYZ_R_RETURN,0x428e10,0,NULL))return error(s,XYZ_LOBBY_RETAIL);
    s->stage=XYZ_RACE_RETURNING;return 1;
}
int xyz_xbox_race_close(const XyzRaceGuest *g,XyzXboxRace *s,const XyzXboxFrame *frame){
    uint32_t mode,one=1;uint8_t profile[12];
    if(!s)return 0;
    if(s->stage==XYZ_RACE_EMPTY || s->stage==XYZ_RACE_CLOSED)return 1;
    if(!s->captured || !frontend(g,frame) || !read_bytes(g,MODE,&mode,4))return 0;
    if(mode!=(s->mode_written?8:s->previous_mode)){
        uint32_t screen;
        /* UI_Main's original constructor (0xf2bb0) sets mode 2 while
         * rebuilding the frontend. That legitimate return has already
         * emptied NetGame; it must not strand our lease waiting for mode 8.
         * Accept only our requested main-menu return and preserve its mode. */
        if(s->stage!=XYZ_RACE_RETURNING || mode!=2 || !s->screen_written ||
           !read_bytes(g,0x3fbd0c,&screen,4) || screen!=0x34c7a0 || !empty_netgame(g))return 0;
    }
    if(s->coordinator){if(!call(g,XYZ_R_STOP,NETGAME,0,NULL))return error(s,XYZ_LOBBY_RETAIL);s->coordinator=0;}
    if(s->begun && !call(g,XYZ_R_RESET,NETGAME,1,&one))return error(s,XYZ_LOBBY_RETAIL);
    /* Never restore a baseline over live retail players or failed teardown. */
    if(!empty_netgame(g))return error(s,XYZ_LOBBY_CONTEXT);
    s->begun=0;
    /* PLAY creates connection records even if initialization never starts.
     * Stop can skip their disposal when its coordinator is still idle. */
    if(s->lobby_released && !s->play_released){
        if(!call(g,XYZ_R_RELEASE_PLAY,0,0,NULL))return error(s,XYZ_LOBBY_RETAIL);
        s->play_released=1;
    }
    if(!write_bytes(g,NETGAME,s->previous_netgame,sizeof s->previous_netgame) ||
       !write_bytes(g,TICK,&s->previous_tick,1))return error(s,XYZ_LOBBY_MEMORY);
    if(s->profile_written){
        if(!read_bytes(g,PROFILE_STATE,profile,sizeof profile))return error(s,XYZ_LOBBY_MEMORY);
        if(!memcmp(profile,s->written_profile,sizeof profile) &&
           !write_bytes(g,PROFILE_STATE,s->previous_profile,sizeof profile))return error(s,XYZ_LOBBY_MEMORY);
    }
    if(s->screen_written){uint32_t screen;
        if(!read_bytes(g,0x3fbd0c,&screen,4))return error(s,XYZ_LOBBY_MEMORY);
        if(screen==0x34c7a0 && !write_bytes(g,0x3fbd0c,&s->previous_screen,4))return error(s,XYZ_LOBBY_MEMORY);
        s->screen_written=0;
    }
    if(s->mode_written && mode==8 && !write_bytes(g,MODE,&s->previous_mode,4))return error(s,XYZ_LOBBY_MEMORY);
    s->captured=0;s->stage=XYZ_RACE_CLOSED;return 1;
}
