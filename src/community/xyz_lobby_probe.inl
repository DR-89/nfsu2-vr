/* Included by recomp_manual.c. Explicit diagnostic of the actual lifted
 * Xbox lobby code over the production TLS session. No chat, room, race start,
 * result report, name claim or saved profile. Two owned clients share one VIP. */
typedef struct {
    uint32_t lobby,arena,list,kind,code,previous,wanted;
    int stage;
    char name[16],reply[8193],game[8193];
} CommunityLobbyProbe;
static CommunityLobbyProbe community_lobby_probes[2];

static uint32_t community_lobby_call(uint32_t address,unsigned count,const uint32_t *args)
{
    recomp_func_t fn=recomp_lookup_manual(address);
    if(!fn)fn=recomp_lookup(address);
    return fn?guest_call_full(fn,0,0,0x0018ab7fu,count,args):UINT32_MAX;
}
static CommunityLobbyProbe *community_lobby_callback_owner(void)
{
    if(!xyz_xbox_address(esp,16))return NULL;
    uint32_t index=MEM32(esp+12);
    if(index<1 || index>2)return NULL;
    CommunityLobbyProbe *p=&community_lobby_probes[index-1];
    return p->lobby && MEM32(esp+4)==p->lobby?p:NULL;
}
static void community_lobby_callback(void)
{
    CommunityLobbyProbe *p=community_lobby_callback_owner();
    uint32_t message=MEM32(esp+8);
    if(p && xyz_xbox_address(message,20)) {
        uint32_t kind=MEM32(message+8),data=MEM32(message+16);
        if(kind==0x67616d65u || kind==p->wanted) {
            char *out=kind==0x67616d65u?p->game:p->reply;
            if(kind!=0x67616d65u){p->kind=kind;p->code=MEM32(message+12);}
            size_t n=0;
            if(data)for(;n<8192 && xyz_xbox_address(data+(uint32_t)n,1);++n){out[n]=(char)MEM8(data+n);if(!out[n])break;}
            out[n]=0;
        }
    }
    eax=0;esp+=4; /* cdecl callback; three arguments remain for its caller */
}
static uint32_t community_lobby_field(CommunityLobbyProbe *p,const char *text,const char *key)
{
    size_t size=strlen(text);
    if(size>8192 || strlen(key)>31)return 0;
    memcpy((void*)XBOX_PTR(p->arena+512),text,size+1);
    memcpy((void*)XBOX_PTR(p->arena+128),key,strlen(key)+1);
    uint32_t args[2]={p->arena+512,p->arena+128};
    uint32_t value=community_lobby_call(0x0028af50u,2,args);
    return value>=p->arena+512 && value<p->arena+512+size?value:0;
}
static int32_t community_lobby_number(CommunityLobbyProbe *p,const char *text,const char *key)
{
    uint32_t field=community_lobby_field(p,text,key);
    uint32_t args[2]={field,UINT32_MAX};
    return field?(int32_t)community_lobby_call(0x0028ba20u,2,args):-1;
}
static int community_lobby_send(CommunityLobbyProbe *p,uint32_t kind,const char *body)
{
    if(strlen(body)>383)return 0;
    memcpy((void*)XBOX_PTR(p->arena+128),body,strlen(body)+1);
    p->kind=p->code=0;p->wanted=kind;p->reply[0]=0;
    uint32_t args[5]={p->lobby,kind,p->arena+128,0x00196390u,(uint32_t)(p-community_lobby_probes)+1};
    return (int32_t)community_lobby_call(0x0028f750u,5,args)>=0;
}
static int community_lobby_reply(const CommunityLobbyProbe *p,uint32_t kind)
{
    return p->kind==kind?(p->code?-1:1):0;
}
static void community_lobby_probe(const XyzXboxFrame *sample,uint64_t now)
{
    static int requested=-1,started,finished,action,listed=-1,engine,search_page;
    static uint64_t start,frontend_since;
    static uint8_t previous_profile[0xc0];
    static int profile_saved;
    static char game_name[40],password[33];
    static int32_t game_id;
    if(requested<0){const char *e=getenv("NFSU2_XYZ_LOBBY_PROBE");requested=e && *e=='1';
        e=getenv("NFSU2_XYZ_LOBBY_ENGINE_PROBE");engine=e && *e=='1';requested|=engine;}
    if(!requested || finished)return;
    uint32_t args[6]={0},result=0;
    char body[384];
    if(!started) {
        if(engine){
            if(sample->status!=XYZ_XBOX_FRONTEND || sample->flow!=3)return;
            if(!frontend_since)frontend_since=now;
            if(now-frontend_since<20000000ull)return;
        } else if(sample->status!=XYZ_XBOX_OTHER_RACE)return;
        XYZOnlineStatus status;xyz_online_snapshot(&status);
        const char *profile=getenv("NFSU2_XYZ_PROBE_PROFILE"),*device=getenv("NFSU2_XYZ_PROBE_DEVICE");
        if(status.state!=XYZ_ONLINE_OFFLINE || !profile || !device){finished=1;return;}
        for(unsigned i=0;i<2;++i)snprintf(community_lobby_probes[i].name,16,"VRQ_%08x_%u",(uint32_t)now,i);
        if(!xyz_online_connect_races(community_lobby_probes[0].name,"VR_LOBBY_DIAGNOSTIC",profile,device)){finished=1;return;}
        memcpy(password,profile,32);password[32]=0;
        snprintf(game_name,sizeof game_name,"VRQA_%08x",(uint32_t)now);
        start=now;started=1;
        fprintf(stderr,"[XYZ lobby] retail protocol probe: engine=%d, temporary clients, private lobby, no race\n",engine);
    }
    if(now-start>45000000ull)goto done;
    XYZOnlineStatus status;xyz_online_snapshot(&status);
    if(status.state==XYZ_ONLINE_STOPPED || status.state==XYZ_ONLINE_QUEUED)goto done;
    if(status.state!=XYZ_ONLINE_CONNECTED)return;
    for(unsigned i=0;i<2;++i) {
        CommunityLobbyProbe *p=&community_lobby_probes[i];
        if(engine && !i){
            if(!p->stage){
                if(!status.lan_ready)return;
                memcpy(previous_profile,(void*)XBOX_PTR(0x41c340),sizeof previous_profile);profile_saved=1;
                if(!xyz_xbox_lobby_begin(&community_engine_guest,&community_engine_lobby,sample,p->name,status.generation,0x196390,now))goto done;
                p->arena=community_engine_lobby.arena;p->lobby=community_engine_lobby.protocol;
                p->list=MEM32(community_engine_lobby.games+8);p->stage=2;
            }
            xyz_xbox_lobby_tick(&community_engine_guest,&community_engine_lobby,status.lan_ready?status.generation:0,now);
            if(community_engine_lobby.stage==XYZ_LOBBY_FAILED)goto done;
            p->stage=community_engine_lobby.stage>=XYZ_LOBBY_READY?5:2;
            continue;
        }
        if(!p->stage) {
            uint32_t size=0x3000;
            if(!community_guest_call(NULL,XYZ_G_ALLOC,0,0,1,&size,&p->arena) || !xyz_xbox_address(p->arena,size)){
                p->arena=0;goto done;
            }
            memset((void*)XBOX_PTR(p->arena),0,size);
            memcpy((void*)XBOX_PTR(p->arena),"10.77.0.1",10);
            args[0]=args[1]=args[2]=0;p->lobby=community_lobby_call(0x00290040u,3,args);
            if(!xyz_xbox_address(p->lobby,0x1988)){p->lobby=0;goto done;}
            args[0]=p->lobby;args[1]=4;args[2]=0x00196390u;args[3]=i+1;
            if((int32_t)community_lobby_call(0x0028f4a0u,4,args)<0)goto done;
            p->list=community_lobby_call(0x0028f550u,4,args);
            if(!xyz_xbox_address(p->list,24))goto done;
            args[0]=p->lobby;args[1]=p->arena;args[2]=20920;args[3]=args[4]=0;args[5]=30000;
            if((int32_t)community_lobby_call(0x002901b0u,6,args)<0)goto done;
            p->stage=2;
        }
        args[0]=p->lobby;community_lobby_call(0x002905c0u,1,args);
        uint32_t state=MEM32(p->lobby+8);
        if(state!=p->previous){fprintf(stderr,"[XYZ lobby] client=%u state=%08x stage=%d\n",i,state,p->stage);p->previous=state;}
        if(state==0x6f66666cu)goto done;
        if(p->stage==2 && state==0x69646c65u) {
            snprintf(body,sizeof body,"NAME=%s",p->name);
            if(!community_lobby_send(p,0x61757468u,body))goto done;
            p->stage=3;
        } else if(p->stage==3 && community_lobby_reply(p,0x61757468u)) {
            if(p->code)goto done;
            snprintf(body,sizeof body,"PERS=%s",p->name);
            if(!community_lobby_send(p,0x70657273u,body))goto done;
            p->stage=4;
        } else if(p->stage==4 && community_lobby_reply(p,0x70657273u)) {
            if(p->code || !community_lobby_field(p,p->reply,"LKEY") ||
               memcmp((void*)XBOX_PTR(p->lobby+0xd60),p->name,strlen(p->name)+1))goto done;
            p->stage=5;
        }
    }
    CommunityLobbyProbe *a=&community_lobby_probes[0],*b=&community_lobby_probes[1];
    CommunityLobbyProbe *host=engine?b:a,*guest=engine?a:b;
    if(a->stage!=5 || b->stage!=5)return;
    if(action==0) {
        if(engine){if(!xyz_xbox_lobby_search(&community_engine_guest,&community_engine_lobby,0,20,now))goto done;}
        else if(!community_lobby_send(a,0x67736561u,"START=0 COUNT=20"))goto done;
        action=1;
    } else if(action==1 && (engine?community_engine_lobby.stage==XYZ_LOBBY_READY:community_lobby_reply(a,0x67736561u))) {
        if(engine?community_engine_lobby.error!=XYZ_LOBBY_OK:a->code)goto done;
        listed=community_lobby_number(a,engine?community_engine_lobby.reply:a->reply,"COUNT");
        if(listed<0 || listed>20)goto done;
        if(engine)a->list=MEM32(community_engine_lobby.games+8);
        /* Wait for every +gam record to reach the original list container. */
        if((int)MEM32(a->list+0x14)!=listed)return;
        if(engine){
            for(int i=0;i<listed;++i){XyzLobbyGame row;
                if(!xyz_xbox_lobby_game(&community_engine_guest,&community_engine_lobby,(unsigned)i,&row))goto done;
            }
            XyzLobbyGame end;
            if(xyz_xbox_lobby_game(&community_engine_guest,&community_engine_lobby,(unsigned)listed,&end))goto done;
            if(!search_page)fprintf(stderr,"[XYZ lobby] bounded browser rows: PASS count=%d\n",listed);
            fprintf(stderr,"[XYZ lobby] browser page: PASS start=%u count=%d limit=%u\n",
                community_engine_lobby.search_start,listed,community_engine_lobby.search_count);
            if(search_page==2 && listed!=0)goto done;
            if(search_page<3){
                const unsigned offsets[4]={0,20,2147483627u,0},limits[4]={20,20,20,5};
                ++search_page;
                if(!xyz_xbox_lobby_search(&community_engine_guest,&community_engine_lobby,offsets[search_page],limits[search_page],now))goto done;
                return;
            }
            fprintf(stderr,"[XYZ lobby] browser pagination: PASS pages=4; empty page and first-page refresh\n");
        }
        snprintf(body,sizeof body,"NAME=%s PASS=%s CUSTFLAGS=256 SYSFLAGS=0 MAXSIZE=2 MINSIZE=2",game_name,password);
        if(!community_lobby_send(host,0x67637265u,body))goto done;
        action=2;
    } else if(action==2 && community_lobby_reply(host,0x67637265u)) {
        game_id=community_lobby_number(host,host->reply,"IDENT");
        if(host->code || game_id<=0 || community_lobby_number(host,host->reply,"COUNT")!=1)goto done;
        snprintf(body,sizeof body,"NAME=%s PASS=%s",game_name,password);
        if(engine){
            if(!xyz_xbox_lobby_join(&community_engine_guest,&community_engine_lobby,game_name,"wrong-password",now))goto done;
            action=20;return;
        }
        else if(!community_lobby_send(guest,0x676a6f69u,body))goto done;
        action=3;
    } else if(action==20 && community_engine_lobby.stage==XYZ_LOBBY_READY) {
        if(community_engine_lobby.error!=XYZ_LOBBY_REJECTED || community_engine_lobby.reply_code!=0x70617373u)goto done;
        fprintf(stderr,"[XYZ lobby engine] wrong password rejected; retrying same session\n");
        if(!xyz_xbox_lobby_join(&community_engine_guest,&community_engine_lobby,game_name,password,now))goto done;
        action=3;
    } else if(action==3 && (engine?community_engine_lobby.stage==XYZ_LOBBY_IN_GAME:community_lobby_reply(guest,0x676a6f69u))) {
        if(engine){XyzLobbySnapshot roster;
            if(!xyz_xbox_lobby_snapshot(&community_engine_guest,&community_engine_lobby,&roster) || roster.game_id!=game_id || roster.players!=2 ||
               strcmp(roster.player[1],community_engine_lobby.name) || !xyz_xbox_lobby_ready(&community_engine_guest,&community_engine_lobby,1))goto done;
        } else {
            if(guest->code || community_lobby_number(guest,guest->reply,"IDENT")!=game_id || community_lobby_number(guest,guest->reply,"COUNT")!=2)goto done;
            if(!community_lobby_send(guest,0x67736574u,"USERFLAGS=134217728"))goto done;
        }
        action=4;
    } else if(action==4 && (engine || community_lobby_reply(guest,0x67736574u))) {
        if(!engine && guest->code)goto done;
        /* The host must receive the guest's readiness as an asynchronous game
         * notification, not merely the guest's own request acknowledgement. */
        if(community_lobby_number(host,host->game,"IDENT")!=game_id ||
           community_lobby_number(host,host->game,"COUNT")!=2 ||
           community_lobby_number(host,host->game,"OPFLAG1")!=134217728)return;
        if(engine){XyzLobbySnapshot roster;
            if(!xyz_xbox_lobby_snapshot(&community_engine_guest,&community_engine_lobby,&roster) || roster.player_flags[1]!=134217728)return;
            /* Decode the real service roster through the play-address adapter
             * and retail GameRecord/IPv4 parser, in scratch memory only. This
             * does not dispatch a play event or start a race. */
            memcpy((void*)XBOX_PTR(host->arena+512),host->game,strlen(host->game)+1);
            uint32_t message[5]={0,4,0x706c6179u,0,host->arena+512},prepared=0;
            memcpy((void*)XBOX_PTR(host->arena+0x2500),message,sizeof message);
            if(!xyz_xbox_lobby_prepare_play(&community_engine_guest,&community_engine_lobby,host->arena+0x2500,&prepared))goto done;
            uint32_t record=host->arena+0x2600;
            args[0]=record;args[1]=MEM32(prepared+16);community_lobby_call(0x28fa50,2,args);
            if(MEM32(record+0x1bc)!=2)goto done;
            for(unsigned j=0;j<2;++j){
                args[0]=host->arena+0x2d00;args[1]=args[0]+4;args[2]=args[0]+8;args[3]=record+0x1dc+j*0x64;
                if(community_lobby_call(0x2967f0,4,args)!=1 || MEM32(args[0])!=MEM32(record+0x1d4+j*0x64))goto done;
            }
            fprintf(stderr,"[XYZ lobby engine] service addresses through original GameRecord/IPv4 parser: PASS players=2; no play dispatched\n");
            if(!xyz_xbox_lobby_leave(&community_engine_guest,&community_engine_lobby,now))goto done;
        } else if(!community_lobby_send(guest,0x676c6561u,""))goto done;
        action=5;
    } else if(action==5 && (engine?community_engine_lobby.stage==XYZ_LOBBY_READY:community_lobby_reply(guest,0x676c6561u))) {
        if(!engine && guest->code)goto done;
        if(community_lobby_number(host,host->game,"COUNT")!=1)return;
        if(!community_lobby_send(host,0x6764656cu,""))goto done;
        action=6;
    } else if(action==6 && community_lobby_reply(host,0x6764656cu)) {
        if(host->code)goto done;
        action=7;goto done;
    }
    return;
done:
    fprintf(stderr,"[XYZ lobby] retail login/list/create/join/ready/leave/delete: %s action=%d listed=%d clients=%d/%d codes=%08x/%08x; private lobby, no race\n",
        action==7?"PASS":"FAIL",action,listed,community_lobby_probes[0].stage,community_lobby_probes[1].stage,
        community_lobby_probes[0].code,community_lobby_probes[1].code);
    for(unsigned i=0;i<2;++i){CommunityLobbyProbe *p=&community_lobby_probes[i];
        if(engine && !i){
            fprintf(stderr,"[XYZ lobby engine] status=%d error=%d\n",community_engine_lobby.stage,community_engine_lobby.error);
            uint32_t manager=community_engine_lobby.manager;
            xyz_xbox_lobby_close(&community_engine_guest,&community_engine_lobby);p->lobby=p->arena=0;
            int names_restored=profile_saved;
            const unsigned offsets[4]={0x58,0x6c,0x94,0xa8};
            for(unsigned j=0;j<4 && names_restored;++j)
                names_restored=!memcmp((void*)XBOX_PTR(0x41c340+offsets[j]),previous_profile+offsets[j],16);
            fprintf(stderr,"[XYZ lobby engine] cleanup names=%d protocol=%08x arena=%08x\n",
                names_restored,manager?MEM32(manager+0x44):0,community_engine_lobby.arena);
            if(profile_saved)memcpy((void*)XBOX_PTR(0x41c340),previous_profile,sizeof previous_profile);
            continue;
        }
        if(p->lobby){args[0]=p->lobby;community_lobby_call(0x00290150u,1,args);p->lobby=0;}
        if(p->arena){community_guest_call(NULL,XYZ_G_FREE,0,0,1,&p->arena,&result);p->arena=0;}
    }
    xyz_online_disconnect();memset(password,0,sizeof password);finished=1;
}
