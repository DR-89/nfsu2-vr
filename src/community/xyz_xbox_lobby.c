#include "xyz_xbox_lobby.h"
#include <stdio.h>
#include <string.h>

enum { PROFILE=0x41c340,ARENA_SIZE=0x3000 };
static const uint32_t name_offsets[4]={0x58,0x6c,0x94,0xa8};
static int read_bytes(const XyzLobbyGuest *g,uint32_t at,void *out,size_t size){
    return g && g->memory && g->memory->read && xyz_xbox_address(at,size) && g->memory->read(g->memory->context,at,out,size);
}
static int write_bytes(const XyzLobbyGuest *g,uint32_t at,const void *data,size_t size){
    return g && g->memory && g->memory->write && xyz_xbox_address(at,size) && g->memory->write(g->memory->context,at,data,size);
}
static uint32_t word(const XyzLobbyGuest *g,uint32_t at){uint32_t v=0;read_bytes(g,at,&v,4);return v;}
static int invoke(const XyzLobbyGuest *g,enum XyzLobbyOp op,uint32_t self,unsigned count,const uint32_t *args,uint32_t *out){
    return g && g->memory && g->call && g->call(g->memory->context,op,self,count,args,out);
}
static int name_valid(const char *name){
    if(!name)return 0;size_t n=0;
    for(;n<16 && name[n];++n){unsigned char c=(unsigned char)name[n];
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-'))return 0;}
    return n>=3 && n<=15;
}
static int names(const XyzLobbyGuest *g,XyzXboxLobby *s,const char *name){
    char value[16]={0};memcpy(value,name,strlen(name));
    /* Record each field before the first write; an allocation/init failure
     * must not leave a partially overwritten offline profile. */
    if(!s->names_saved){for(unsigned i=0;i<4;++i)if(!read_bytes(g,PROFILE+name_offsets[i],s->previous_names[i],16))return 0;s->names_saved=1;}
    for(unsigned i=0;i<4;++i){
        if(!write_bytes(g,PROFILE+name_offsets[i],value,16))return 0;
        memcpy(s->written_names[i],value,16);s->names_written|=1<<i;
    }
    return 1;
}
static void dispose(const XyzLobbyGuest *g,XyzXboxLobby *s){
    uint32_t result=0,zero=0;
    if(s->initialized && (!s->protocol || word(g,s->manager+0x44)==s->protocol)){
        if(s->play_received)invoke(g,XYZ_L_RELEASE_PLAY,0,0,NULL,&result);
        invoke(g,XYZ_L_DROP,0,1,&zero,&result);
    }
    s->protocol=s->manager=s->games=0;s->initialized=0;
    if(s->names_saved){for(unsigned i=0;i<4;++i){char current[16];
        if((s->names_written&(1<<i)) && read_bytes(g,PROFILE+name_offsets[i],current,16) &&
           !memcmp(current,s->written_names[i],16))write_bytes(g,PROFILE+name_offsets[i],s->previous_names[i],16);
    }}
    s->names_saved=s->names_written=0;
    if(s->controller_saved){uint32_t current;
        if(read_bytes(g,0x406c98,&current,4) && current==s->written_controller)
            write_bytes(g,0x406c98,&s->previous_controller,4);
        s->controller_saved=0;
    }
    if(s->arena && g && g->memory && g->memory->call)
        g->memory->call(g->memory->context,XYZ_G_FREE,0,0,1,&s->arena,&result);
    s->arena=0;s->generation=0;s->reply_kind=s->waiting_kind=s->race_host=0;
    memset(s->reply,0,sizeof s->reply);
}
static void fail(const XyzLobbyGuest *g,XyzXboxLobby *s,enum XyzLobbyError error){
    s->error=error;dispose(g,s);s->stage=XYZ_LOBBY_FAILED;
}
void xyz_xbox_lobby_close(const XyzLobbyGuest *g,XyzXboxLobby *s){if(!s)return;dispose(g,s);memset(s,0,sizeof *s);}
static int send_request(const XyzLobbyGuest *g,XyzXboxLobby *s,uint32_t kind,const char *body){
    size_t n=strlen(body);if(n>383 || !write_bytes(g,s->arena+128,body,n+1))return 0;
    uint32_t a[5]={s->protocol,kind,s->arena+128,s->callback,s->arena},result=0;
    s->reply_kind=s->reply_code=0;s->waiting_kind=kind;s->reply[0]=0;
    return invoke(g,XYZ_L_REQUEST,0,5,a,&result) && (int32_t)result>=0;
}
int xyz_xbox_lobby_begin(const XyzLobbyGuest *g,XyzXboxLobby *s,const XyzXboxFrame *frame,
                         const char *name,uint32_t generation,uint32_t callback,uint64_t now){
    if(!s || !frame || frame->status!=XYZ_XBOX_FRONTEND || frame->flow!=3 || !generation || !callback || !name_valid(name) ||
       (s->stage!=XYZ_LOBBY_EMPTY && s->stage!=XYZ_LOBBY_FAILED) || s->arena)return 0;
    /* Flow 3 includes startup/career movies. A race must not take over their
     * framebuffer or pending frontend transition before retail releases it. */
    const uint32_t gates[3]={0x3fbbf4,0x3fbbfc,0x428e14};
    for(unsigned i=0;i<3;++i){uint32_t value;
        if(!read_bytes(g,gates[i],&value,4) || value)return 0;
    }
    memset(s,0,sizeof *s);uint32_t result=0,zero=0;
    if(!invoke(g,XYZ_L_MANAGER,0,0,NULL,&s->manager) || !xyz_xbox_address(s->manager,0xa4) || word(g,s->manager+0x44)){
        s->manager=0;s->stage=XYZ_LOBBY_FAILED;s->error=XYZ_LOBBY_CONTEXT;return 0;
    }
    uint32_t size=ARENA_SIZE;
    if(!g->memory->call || !g->memory->call(g->memory->context,XYZ_G_ALLOC,0,0,1,&size,&s->arena) || !xyz_xbox_address(s->arena,size)){
        s->arena=0;fail(g,s,XYZ_LOBBY_MEMORY);return 0;
    }
    s->callback=callback;s->generation=generation;memcpy(s->name,name,strlen(name)+1);
    if(!names(g,s,name)){fail(g,s,XYZ_LOBBY_MEMORY);return 0;}
    s->initialized=1;
    if(!invoke(g,XYZ_L_INIT,0,0,NULL,&result) || (int32_t)result<0 ||
       !invoke(g,XYZ_L_GAMES,0,1,&zero,&s->games) || !xyz_xbox_address(s->games,0x504)){
        fail(g,s,XYZ_LOBBY_RETAIL);return 0;
    }
    s->protocol=word(g,s->manager+0x44);
    if(!xyz_xbox_address(s->protocol,0x1988) || !write_bytes(g,s->arena,"10.77.0.1",10)){
        fail(g,s,XYZ_LOBBY_MEMORY);return 0;
    }
    uint32_t a[6]={s->protocol,s->arena,20920,0,0,30000};
    if(!invoke(g,XYZ_L_CONNECT,0,6,a,&result) || (int32_t)result<0){fail(g,s,XYZ_LOBBY_RETAIL);return 0;}
    s->stage=XYZ_LOBBY_CONNECTING;s->deadline=now+45000000ull;return 1;
}
int xyz_xbox_lobby_owns_callback(const XyzXboxLobby *s,uint32_t protocol,uint32_t context){
    return s && s->arena && s->protocol==protocol && s->arena==context;
}
int xyz_xbox_lobby_select_controller(const XyzLobbyGuest *g,XyzXboxLobby *s,unsigned port){
    uint32_t protocol,current;uint8_t device;
    if(!s || port>3 || s->stage!=XYZ_LOBBY_IN_GAME || !s->initialized || !s->protocol ||
       !read_bytes(g,s->manager+0x44,&protocol,4) || protocol!=s->protocol ||
       !read_bytes(g,0x427ff8+port,&device,1) || device==UINT8_MAX ||
       !read_bytes(g,0x406c98,&current,4))return 0;
    uint32_t selected=port;
    if(!write_bytes(g,0x406c98,&selected,4))return 0;
    if(!s->controller_saved)s->previous_controller=current;
    s->controller_saved=1;s->written_controller=selected;return 1;
}
void xyz_xbox_lobby_event(const XyzLobbyGuest *g,XyzXboxLobby *s,uint32_t message){
    uint32_t m[5];if(!s || !read_bytes(g,message,m,sizeof m) || m[2]!=s->waiting_kind)return;
    s->reply_kind=m[2];s->reply_code=m[3];size_t n=0;
    for(;m[4] && n<sizeof s->reply-1;++n){
        if(!read_bytes(g,m[4]+(uint32_t)n,s->reply+n,1) || !s->reply[n])break;
    }
    s->reply[n]=0;
}
static int persona_reply(const XyzLobbyGuest *g,XyzXboxLobby *s){
    size_t n=strlen(s->reply);uint32_t result=0;
    if(!write_bytes(g,s->arena+512,s->reply,n+1) || !write_bytes(g,s->arena+128,"PERS",5))return 0;
    uint32_t a[4]={s->arena+512,s->arena+128,0,0};
    if(!invoke(g,XYZ_L_FIND,0,2,a,&result) || result<s->arena+512 || result>=s->arena+512+n)return 0;
    a[0]=result;a[1]=s->arena+0x2400;a[2]=32;a[3]=0;
    if(!invoke(g,XYZ_L_STRING,0,4,a,&result))return 0;
    char name[32];if(!read_bytes(g,s->arena+0x2400,name,32) || !memchr(name,0,32) || !name_valid(name))return 0;
    /* A claimed/occupied name may produce a legitimate server-assigned
     * variant. The engine must identify itself by THAT name in every roster. */
    if(!names(g,s,name))return 0;
    memset(s->name,0,sizeof s->name);memcpy(s->name,name,strlen(name));return 1;
}
static int text_field(const XyzLobbyGuest *,XyzXboxLobby *,size_t,const char *,char [32]);
static int search_complete(const XyzLobbyGuest *g,XyzXboxLobby *s){
    char text[32];size_t length=strlen(s->reply);uint32_t expected=0,list,shown,stored,pending;
    if(!write_bytes(g,s->arena+512,s->reply,length+1) || !text_field(g,s,length,"COUNT",text) || !text[0])return -1;
    for(const char *p=text;*p;++p){
        if(*p<'0' || *p>'9' || expected>200)return -1;
        expected=expected*10+(unsigned)(*p-'0');
    }
    if(expected>s->search_count || !read_bytes(g,s->games+8,&list,4) || !xyz_xbox_address(list,0x44) ||
       !read_bytes(g,list+0x14,&shown,4) || !read_bytes(g,list+0x18,&stored,4) ||
       shown>stored || stored>4096 || !read_bytes(g,s->manager+0x4c,&pending,4))return -1;
    /* Retail finishes its queued request only when every +gam is installed.
     * A zero-count page completes through the search reply itself. */
    return !pending && shown==expected;
}
void xyz_xbox_lobby_tick(const XyzLobbyGuest *g,XyzXboxLobby *s,uint32_t generation,uint64_t now){
    if(!s || (!s->initialized && s->stage!=XYZ_LOBBY_RACE))return;
    if(!generation || generation!=s->generation){fail(g,s,XYZ_LOBBY_SESSION);return;}
    uint32_t protocol,result=0;
    if(!read_bytes(g,s->manager+0x44,&protocol,4) || protocol!=s->protocol){fail(g,s,XYZ_LOBBY_CONTEXT);return;}
    if(s->initialized && !invoke(g,XYZ_L_UPDATE,s->manager,0,NULL,&result)){fail(g,s,XYZ_LOBBY_RETAIL);return;}
    /* Legacy Xbox Live startup normally supplies the DirtySock idle thread.
     * Our explicit session instead pumps its original bounded idle callback
     * on the guest thread, including CommUDP timers and deferred socket frees. */
    uint32_t sockets=0x46d6d0;
    if(!invoke(g,XYZ_L_SOCKET_IDLE,0,1,&sockets,&result)){fail(g,s,XYZ_LOBBY_RETAIL);return;}
    if(s->stage==XYZ_LOBBY_RACE)return;
    /* Callback rejection is deferred until the retail dispatcher returns.
     * Destroying its lobby inside the callback would invalidate its stack. */
    if(s->event_error){fail(g,s,XYZ_LOBBY_REJECTED);return;}
    uint32_t state=word(g,s->protocol+8);char body[64];
    if(state==0x6f66666cu){fail(g,s,XYZ_LOBBY_SESSION);return;}
    if(s->stage==XYZ_LOBBY_CONNECTING && state==0x69646c65u){
        uint8_t active=1;
        if(!write_bytes(g,s->manager+0x48,&active,1)){fail(g,s,XYZ_LOBBY_MEMORY);return;}
        snprintf(body,sizeof body,"NAME=%s",s->name);
        if(!send_request(g,s,0x61757468u,body)){fail(g,s,XYZ_LOBBY_RETAIL);return;}
        s->stage=XYZ_LOBBY_AUTH;
    } else if(s->stage==XYZ_LOBBY_AUTH && s->reply_kind==0x61757468u){
        if(s->reply_code){fail(g,s,XYZ_LOBBY_REJECTED);return;}
        snprintf(body,sizeof body,"PERS=%s",s->name);
        if(!send_request(g,s,0x70657273u,body)){fail(g,s,XYZ_LOBBY_RETAIL);return;}
        s->stage=XYZ_LOBBY_PERSONA;
    } else if(s->stage==XYZ_LOBBY_PERSONA && s->reply_kind==0x70657273u){
        if(s->reply_code){fail(g,s,XYZ_LOBBY_REJECTED);return;}
        if(!persona_reply(g,s)){fail(g,s,XYZ_LOBBY_MEMORY);return;}
        s->stage=XYZ_LOBBY_READY;s->waiting_kind=0;s->reply[0]=0;
    } else if(s->stage==XYZ_LOBBY_SEARCHING && s->reply_kind==0x67736561u){
        if(s->reply_code){s->stage=XYZ_LOBBY_READY;s->error=XYZ_LOBBY_REJECTED;s->waiting_kind=0;}
        else {
            int complete=search_complete(g,s);
            if(complete<0){fail(g,s,XYZ_LOBBY_REJECTED);return;}
            if(complete){s->stage=XYZ_LOBBY_READY;s->waiting_kind=0;}
        }
    } else if((s->stage==XYZ_LOBBY_JOINING || s->stage==XYZ_LOBBY_CREATING) &&
              s->reply_kind==s->waiting_kind && s->reply_code){
        s->stage=XYZ_LOBBY_READY;s->error=XYZ_LOBBY_REJECTED;s->waiting_kind=0;
    } else if((s->stage==XYZ_LOBBY_JOINING || s->stage==XYZ_LOBBY_CREATING) && (int32_t)word(g,s->games+0x1c)>=0){
        if(s->stage==XYZ_LOBBY_JOINING && s->join_game_id>=0 && word(g,s->games+0x1c)!=(uint32_t)s->join_game_id){
            fail(g,s,XYZ_LOBBY_REJECTED);return;
        }
        s->stage=XYZ_LOBBY_IN_GAME;
    } else if(s->stage==XYZ_LOBBY_STARTING && s->reply_kind==s->waiting_kind && s->reply_code){
        s->stage=XYZ_LOBBY_IN_GAME;s->error=XYZ_LOBBY_REJECTED;s->waiting_kind=0;
    } else if(s->stage==XYZ_LOBBY_LEAVING && word(g,s->games+0x1c)==UINT32_MAX){
        s->stage=XYZ_LOBBY_READY;
    } else if(s->stage==XYZ_LOBBY_IN_GAME && word(g,s->games+0x1c)==UINT32_MAX){
        /* The host deleted the lobby or the service removed this player. */
        s->stage=XYZ_LOBBY_READY;
    }
    if(s->play_received && (s->stage==XYZ_LOBBY_IN_GAME || s->stage==XYZ_LOBBY_STARTING)){
        s->stage=XYZ_LOBBY_PLAY_READY;s->waiting_kind=0;
    }
    if(s->stage!=XYZ_LOBBY_READY && s->stage!=XYZ_LOBBY_IN_GAME && s->stage!=XYZ_LOBBY_PLAY_READY &&
       now>=s->deadline)fail(g,s,XYZ_LOBBY_TIMEOUT);
}
int xyz_xbox_lobby_create(const XyzLobbyGuest *g,XyzXboxLobby *s,unsigned performance,uint64_t now){
    uint8_t settings[0x4c];
    if(!s || s->stage!=XYZ_LOBBY_READY || performance>3 || !read_bytes(g,PROFILE,settings,sizeof settings) ||
       settings[1]<2 || settings[0]<settings[1] || settings[0]>6 || !memchr(settings+0x1c,0,28))return 0;
    if(!write_bytes(g,s->arena+128,s->name,strlen(s->name)+1))return 0;
    uint32_t a[5]={s->arena+128,PROFILE,performance,0,0},result=0;
    if(!invoke(g,XYZ_L_CREATE,s->games,5,a,&result) || (int32_t)result<0)return 0;
    s->reply_kind=s->reply_code=0;s->waiting_kind=0x67637265u;s->error=XYZ_LOBBY_OK;
    s->play_received=0;
    s->stage=XYZ_LOBBY_CREATING;s->deadline=now+30000000ull;return 1;
}
int xyz_xbox_lobby_join(const XyzLobbyGuest *g,XyzXboxLobby *s,const char *game,const char *password,uint64_t now){
    if(!s || s->stage!=XYZ_LOBBY_READY || !game || !*game || strlen(game)>31 || !password || strlen(password)>32)return 0;
    /* The retail builder has 96 bytes for both TagFields. Account for quoted
     * spaces and percent escaping before giving it user-entered values. */
    size_t encoded=16;
    for(unsigned i=0;i<2;++i){const unsigned char *p=(const unsigned char *)(i?password:game);int spaces=0;
        for(;*p;++p){spaces|=*p==' ';encoded+=*p<32 || *p==127 || strchr("=\":%^",*p)?3:1;}
        if(spaces)encoded+=2;
    }
    if(encoded>95)return 0;
    if(!write_bytes(g,s->arena+128,game,strlen(game)+1) || !write_bytes(g,s->arena+192,password,strlen(password)+1))return 0;
    uint32_t a[4]={s->arena+128,s->arena+192,0,0},result=0;
    if(!invoke(g,XYZ_L_JOIN,s->games,4,a,&result) || (int32_t)result<0)return 0;
    s->reply_kind=s->reply_code=0;s->waiting_kind=0x676a6f69u;s->error=XYZ_LOBBY_OK;
    s->play_received=0;s->join_game_id=-1;
    s->stage=XYZ_LOBBY_JOINING;s->deadline=now+30000000ull;return 1;
}
int xyz_xbox_lobby_search(const XyzLobbyGuest *g,XyzXboxLobby *s,unsigned start,unsigned count,uint64_t now){
    uint32_t protocol,result=0,settings[6]={0};
    if(!s || s->stage!=XYZ_LOBBY_READY || !s->initialized || !s->protocol || !s->arena ||
       !count || count>200 || start>(uint32_t)INT32_MAX-count ||
       !read_bytes(g,s->manager+0x44,&protocol,4) || protocol!=s->protocol)return 0;
    /* Search settings: start, count, any performance, any race type; ordinary
     * non-quick search. Original 0x19b560 destroys/recreates the display list,
     * clears its ping records and queues the request with 0x196680. */
    settings[0]=start;settings[1]=count;settings[2]=1;settings[3]=UINT32_MAX;
    if(!write_bytes(g,s->arena+128,settings,sizeof settings))return 0;
    uint32_t args[4]={s->arena+128,0,0,0};
    if(!invoke(g,XYZ_L_SEARCH,s->games,4,args,&result) || (int32_t)result<0)return 0;
    s->search_start=start;s->search_count=count;s->reply_kind=s->reply_code=0;s->reply[0]=0;
    s->waiting_kind=0x67736561u;s->error=XYZ_LOBBY_OK;
    s->stage=XYZ_LOBBY_SEARCHING;s->deadline=now+30000000ull;return 1;
}
int xyz_xbox_lobby_join_game(const XyzLobbyGuest *g,XyzXboxLobby *s,unsigned index,const XyzLobbyGame *selected,const char *password,uint64_t now){
    XyzLobbyGame current;
    if(!selected || !memchr(selected->name,0,sizeof selected->name) ||
       !xyz_xbox_lobby_game(g,s,index,&current) || current.game_id!=selected->game_id || strcmp(current.name,selected->name) ||
       current.min_players<2 || current.max_players>6 || !current.players || current.players>=current.max_players ||
       (current.system_flags&0x80000u) || !xyz_xbox_lobby_join(g,s,current.name,password,now))return 0;
    s->join_game_id=current.game_id;return 1;
}
int xyz_xbox_lobby_ready(const XyzLobbyGuest *g,XyzXboxLobby *s,int ready){
    if(!s || s->stage!=XYZ_LOBBY_IN_GAME)return 0;
    uint32_t value=ready!=0,result=0;
    return invoke(g,XYZ_L_READY,s->games,1,&value,&result) && (int32_t)result>=0;
}
int xyz_xbox_lobby_start(const XyzLobbyGuest *g,XyzXboxLobby *s,uint64_t now){
    char host[16];XyzLobbySnapshot roster;
    if(!s || s->stage!=XYZ_LOBBY_IN_GAME || !xyz_xbox_lobby_snapshot(g,s,&roster) || roster.game_id<0 ||
       roster.players<2 || roster.players>6 || !read_bytes(g,s->games+0x54,host,sizeof host) ||
       !memchr(host,0,sizeof host) || strcmp(host,s->name) ||
       roster.players<word(g,s->games+0x134))return 0;
    /* The retail roster keeps the host first. Reject a contradictory record
     * instead of accidentally starting for the wrong owner. */
    if(strcmp(roster.player[0],s->name))return 0;
    for(unsigned i=1;i<roster.players;++i)if(!(roster.player_flags[i]&0x08000000u))return 0;
    uint32_t a[3]={0,0,0},result=0;
    if(!invoke(g,XYZ_L_START,s->games,3,a,&result) || (int32_t)result<0)return 0;
    s->reply_kind=s->reply_code=0;s->waiting_kind=0x67737461u;s->error=XYZ_LOBBY_OK;
    s->stage=XYZ_LOBBY_STARTING;s->deadline=now+30000000ull;return 1;
}
int xyz_xbox_lobby_leave(const XyzLobbyGuest *g,XyzXboxLobby *s,uint64_t now){
    if(!s || (s->stage!=XYZ_LOBBY_IN_GAME && s->stage!=XYZ_LOBBY_PLAY_READY))return 0;
    uint32_t a[2]={0,0},result=0;
    if(!invoke(g,XYZ_L_LEAVE,s->games,2,a,&result) || (int32_t)result<0)return 0;
    s->play_received=0;s->stage=XYZ_LOBBY_LEAVING;s->deadline=now+30000000ull;return 1;
}
int xyz_xbox_lobby_snapshot(const XyzLobbyGuest *g,const XyzXboxLobby *s,XyzLobbySnapshot *out){
    if(!s || !out)return 0;memset(out,0,sizeof *out);out->game_id=-1;
    out->stage=s->stage;out->error=s->error;out->server_error=s->reply_code;memcpy(out->persona,s->name,16);
    if(!s->initialized)return 1;
    uint32_t list=word(g,s->games+8);if(xyz_xbox_address(list,24))out->listed_games=word(g,list+0x14);
    out->game_id=(int32_t)word(g,s->games+0x1c);
    if(out->game_id<0)return 1;
    out->players=word(g,s->games+0x1d8);if(out->players>8)return 0;
    if(!read_bytes(g,s->games+0x20,out->game_name,36))return 0;out->game_name[35]=0;
    for(unsigned i=0;i<out->players;++i){
        if(!read_bytes(g,s->games+0x1e0+i*0x64,out->player[i],16))return 0;
        out->player[i][15]=0;out->player_flags[i]=word(g,s->games+0x218+i*0x64);
    }
    return 1;
}
static int decode_game(const uint8_t bytes[0x1c0],XyzLobbyGame *out){
    XyzLobbyGame value={0};
    memcpy(&value.game_id,bytes,4);memcpy(value.name,bytes+4,sizeof value.name);
    memcpy(value.host,bytes+0x38,sizeof value.host);memcpy(value.params,bytes+0x8c,sizeof value.params);
    memcpy(&value.custom_flags,bytes+0x110,4);memcpy(&value.system_flags,bytes+0x114,4);
    memcpy(&value.min_players,bytes+0x118,4);memcpy(&value.max_players,bytes+0x11c,4);
    memcpy(&value.seed,bytes+0x120,4);memcpy(&value.players,bytes+0x1bc,4);
    if(value.game_id<0 || !value.name[0] || !memchr(value.name,0,sizeof value.name) ||
       !memchr(value.host,0,sizeof value.host) || !memchr(value.params,0,sizeof value.params) ||
       value.players>value.max_players || value.min_players>value.max_players || value.max_players>8)return 0;
    *out=value;return 1;
}
int xyz_xbox_lobby_current_game(const XyzLobbyGuest *g,const XyzXboxLobby *s,XyzLobbyGame *out){
    uint8_t bytes[0x1c0];uint32_t protocol;
    if(!out)return 0;memset(out,0,sizeof *out);out->game_id=-1;
    if(!s || !s->initialized || !s->protocol ||
       (s->stage!=XYZ_LOBBY_IN_GAME && s->stage!=XYZ_LOBBY_STARTING && s->stage!=XYZ_LOBBY_PLAY_READY) ||
       !read_bytes(g,s->manager+0x44,&protocol,4) || protocol!=s->protocol ||
       !read_bytes(g,s->games+0x1c,bytes,sizeof bytes))return 0;
    return decode_game(bytes,out);
}
int xyz_xbox_lobby_game(const XyzLobbyGuest *g,const XyzXboxLobby *s,unsigned index,XyzLobbyGame *out){
    uint32_t protocol,list,shown,stored,table,node,record;uint8_t bytes[0x1c0];
    if(!out)return 0;memset(out,0,sizeof *out);out->game_id=-1;
    if(!s || s->stage!=XYZ_LOBBY_READY || !s->initialized || !s->protocol ||
       !read_bytes(g,s->manager+0x44,&protocol,4) || protocol!=s->protocol ||
       !read_bytes(g,s->games+8,&list,4) || !xyz_xbox_address(list,0x44) ||
       !read_bytes(g,list+0x14,&shown,4) || !read_bytes(g,list+0x18,&stored,4) ||
       shown>stored || index>=shown || stored>4096 ||
       !read_bytes(g,list+0x40,&table,4) || !xyz_xbox_address(table,4*((size_t)index+1)) ||
       !read_bytes(g,table+4*index,&node,4) || !read_bytes(g,node,&record,4) ||
       !read_bytes(g,record,bytes,sizeof bytes))return 0;
    /* Same two indirections as retail ListDispListIndex (0x2872b0), then
     * the GameRecord fields decoded by 0x28fa50. Never expose its AUTH. */
    return decode_game(bytes,out);
}
static int text_field(const XyzLobbyGuest *g,XyzXboxLobby *s,size_t length,const char *key,char out[32]){
    uint32_t result=0,a[4]={s->arena+512,s->arena+128,0,0};
    if(!write_bytes(g,a[1],key,strlen(key)+1) || !invoke(g,XYZ_L_FIND,0,2,a,&result) ||
       result<s->arena+512 || result>=s->arena+512+length)return 0;
    a[0]=result;a[1]=s->arena+0x2400;a[2]=32;a[3]=0;
    return invoke(g,XYZ_L_STRING,0,4,a,&result) && read_bytes(g,a[1],out,32) && memchr(out,0,32)!=NULL;
}
static int player_ip(const char *text,unsigned octets[4]){
    const char *p=text;
    for(unsigned i=0;i<4;++i){
        unsigned value=0,digits=0;
        while(*p>='0' && *p<='9'){
            value=value*10+(unsigned)(*p++-'0');
            if(++digits>3 || value>255)return 0;
        }
        if(!digits || (i<3?*p++!='.':*p!=0))return 0;
        octets[i]=value;
    }
    return octets[0]==10 && octets[1]==77 && (octets[2] || octets[3]>1) &&
        !(octets[2]==255 && octets[3]==255);
}
int xyz_xbox_lobby_enter_race(const XyzLobbyGuest *g,XyzXboxLobby *s){
    XyzLobbySnapshot roster;char host[16];uint32_t protocol,ip;unsigned local=0;
    if(!s || s->stage!=XYZ_LOBBY_PLAY_READY || !s->initialized || !s->play_received || !s->arena ||
       !read_bytes(g,s->manager+0x44,&protocol,4) || protocol!=s->protocol ||
       !xyz_xbox_lobby_snapshot(g,s,&roster) || roster.game_id<0 || roster.players<2 || roster.players>6 ||
       !read_bytes(g,s->games+0x54,host,sizeof host) || !memchr(host,0,sizeof host) ||
       strcmp(host,roster.player[0]) || !read_bytes(g,s->games+0x1f0,&ip,4) ||
       (ip>>16)!=0x0a4d || ip<=0x0a4d0001 || ip==0x0a4dffff)return 0;
    for(unsigned i=0;i<roster.players;++i){
        if(!name_valid(roster.player[i]))return 0;
        local+=!strcmp(roster.player[i],s->name);
    }
    if(local!=1)return 0;
    /* Retail 0xf8880 drops the lobby before NetGame::Init. A started server
     * race survives its lobby sockets closing; its CommUDP links are separate.
     * Keep only the already validated host for the client's pending lookup. */
    uint32_t zero=0,result=0;
    if(!invoke(g,XYZ_L_DROP,0,1,&zero,&result))return 0;
    if(!read_bytes(g,s->manager+0x44,&protocol,4) || protocol){fail(g,s,XYZ_LOBBY_RETAIL);return 0;}
    s->initialized=0;s->protocol=s->games=0;s->race_host=ip;
    if(g->memory->call)g->memory->call(g->memory->context,XYZ_G_FREE,0,0,1,&s->arena,&result);
    s->arena=0;s->reply_kind=s->reply_code=s->waiting_kind=0;s->deadline=0;
    memset(s->reply,0,sizeof s->reply);s->stage=XYZ_LOBBY_RACE;return 1;
}
int xyz_xbox_lobby_resolve_virtual(const XyzLobbyGuest *g,XyzXboxLobby *s,uint32_t resolver,unsigned slot,uint32_t port){
    char address[20];unsigned ip[4];uint32_t protocol,host;
    if(!s || (!s->initialized && s->stage!=XYZ_LOBBY_RACE) || !s->play_received || resolver!=0x42cd78 || slot || port!=3658 ||
       !read_bytes(g,s->manager+0x44,&protocol,4) || protocol!=s->protocol ||
       !read_bytes(g,resolver+4,address,sizeof address) || !memchr(address,0,sizeof address) || !player_ip(address,ip))return 0;
    if(s->stage==XYZ_LOBBY_RACE)host=s->race_host;
    else {
        uint32_t count;
        if(!read_bytes(g,s->games+0x1d8,&count,4) || count<2 || count>6 || !read_bytes(g,s->games+0x1f0,&host,4))return 0;
    }
    uint32_t value=(ip[0]<<24)|(ip[1]<<16)|(ip[2]<<8)|ip[3];
    if(host!=value || word(g,resolver+0x20)!=port)return 0;
    uint8_t ready=0;
    return write_bytes(g,resolver+0x1c,&value,4) && write_bytes(g,resolver+0x18,&ready,1);
}
int xyz_xbox_lobby_prepare_play(const XyzLobbyGuest *g,XyzXboxLobby *s,uint32_t message,uint32_t *prepared){
    uint32_t m[5];char source[8193],body[8193],value[32];size_t size=0,used=0;
    if(prepared)*prepared=0;
    if(!s || !s->initialized || !s->arena || !prepared || !read_bytes(g,message,m,sizeof m) ||
       m[2]!=0x706c6179u || m[3] || !m[4])return 0;
    for(;size<sizeof source;++size){
        if(!read_bytes(g,m[4]+(uint32_t)size,source+size,1))return 0;
        if(!source[size])break;
    }
    if(size==sizeof source || !write_bytes(g,s->arena+512,source,size+1) ||
       !text_field(g,s,size,"COUNT",value) || value[0]<'2' || value[0]>'6' || value[1])return 0;
    unsigned count=(unsigned)(value[0]-'0');
    for(unsigned i=0;i<count;++i){
        char key[16];unsigned octets[4];snprintf(key,sizeof key,"ADDR%u",i);
        if(!text_field(g,s,size,key,value) || !player_ip(value,octets))return 0;
        int n=snprintf(body+used,sizeof body-used,"MADDR%u=%u.%u.%u.%u\n",i,octets[0],octets[1],octets[2],octets[3]);
        if(n<0 || (size_t)n>=sizeof body-used)return 0;used+=(size_t)n;
    }
    /* TagFieldFind takes the first occurrence. Preserve every service field
     * verbatim after the six-or-fewer adapted connection strings. */
    if(size>=sizeof body-used)return 0;
    memcpy(body+used,source,size+1);
    if(!write_bytes(g,s->arena+512,body,used+size+1))return 0;
    m[4]=s->arena+512;
    if(!write_bytes(g,s->arena+0x2500,m,sizeof m))return 0;
    *prepared=s->arena+0x2500;return 1;
}
