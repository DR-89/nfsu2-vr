/* Explicit two-device diagnostic. Each full game owns a distinct production
 * session/VIP. Original Create/Join and NetGameUtil/NetGameLink exchange private
 * test payloads. Optional RACE_START_PROBE also starts this private unranked
 * lobby and checks the original coordinator. WORLD_PROBE also loads Circuit
 * and requests the original return flow; it does not complete a ranked race. */
static void community_race_probe_motion(const XyzXboxFrame *sample,int host,uint64_t now)
{
    /* Observe original cars only. Input comes through the ordinary USB pad
     * script; this check never writes positions, velocities or race clocks. */
    uint32_t car=MEM32(sample->world);
    for(unsigned i=0;i<32 && car!=sample->world;++i){
        if(!xyz_xbox_address(car,0x9e0))return;
        rc_state state;
        if(xyz_xbox_car_state(cockpit_ui_read,NULL,sample->world,car,(uint32_t)now,&state))
            fprintf(stderr,"[XYZ race motion] role=%s clock=%u slot=%u local=%d p=(%.3f,%.3f,%.3f) speed=%.3f\n",
                host?"host":"guest",MEM32(0x428a00),MEM16(car+0xa),car==sample->car,
                state.p[0],state.p[1],state.p[2],state.speed);
        car=MEM32(car);
    }
}
static void community_race_link_probe(const XyzXboxFrame *sample,uint64_t now)
{
    static int requested=-1,host,started,finished,action,received,sent,announced,request_start,request_world,request_countdown,countdown,request_motion;
    static int last_net=-1,last_players=-1;
    static uint64_t frontend_since,start,hold_until,next_world_log,last_tick,next_motion,cleanup_until;
    static uint32_t utility,link,previous_host,previous_mode;
    static uint8_t previous_profile[0xc0];
    static XyzRaceSettingsLease settings_lease;
    static int saved;
    static char name[16];
    const char *secret=getenv("NFSU2_XYZ_PROBE_PASSWORD"),*game=getenv("NFSU2_XYZ_PROBE_GAME");
    /* The isolated harness provides 24 random hex characters. Both peers use
     * the first twelve in retail's separate password/confirmation fields. */
    char password[13]={0};if(secret && strlen(secret)==24)memcpy(password,secret,12);
    if(requested<0){const char *e=getenv("NFSU2_XYZ_RACE_LINK_PROBE");
        host=e && !strcmp(e,"host");requested=host || (e && !strcmp(e,"guest"));
        e=getenv("NFSU2_XYZ_RACE_START_PROBE");request_start=e && !strcmp(e,"1");
        e=getenv("NFSU2_XYZ_RACE_MOTION_PROBE");request_motion=e && !strcmp(e,"1");
        e=getenv("NFSU2_XYZ_RACE_COUNTDOWN_PROBE");request_countdown=request_motion || (e && !strcmp(e,"1"));
        e=getenv("NFSU2_XYZ_RACE_WORLD_PROBE");request_world=request_countdown || (e && !strcmp(e,"1"));request_start|=request_world;}
    if(!requested || finished)return;
    uint32_t args[4]={0},result;
    XYZOnlineStatus status;xyz_online_snapshot(&status);
    if(started && last_tick && now-last_tick>2000000ull)
        fprintf(stderr,"[XYZ race link] frame gap %.3f s; flow=%u session=%u expected=%u\n",
            (now-last_tick)/1000000.0,sample->flow,status.generation,community_engine_lobby.generation);
    last_tick=now;
    if(action==90){
        if(xyz_xbox_race_close(&community_race_guest,&community_engine_race,sample))goto done;
        xyz_xbox_race_return(&community_race_guest,&community_engine_race,sample);return;
    }
    if(!started){
        if(sample->status!=XYZ_XBOX_FRONTEND || sample->flow!=3 || MEM32(0x3fbbf4) || MEM32(0x3fbbfc) || MEM32(0x428e14)){
            frontend_since=0;return;
        }
        if(!frontend_since)frontend_since=now;
        if(now-frontend_since<20000000ull)return;
        const char *profile=getenv("NFSU2_XYZ_PROBE_PROFILE"),*device=getenv("NFSU2_XYZ_PROBE_DEVICE");
        if(status.state!=XYZ_ONLINE_OFFLINE || !profile || !device || !secret || strlen(secret)!=24 ||
           strspn(secret,"0123456789abcdef")!=24 || (!host && (!game || !*game || strlen(game)>31))){finished=1;return;}
        snprintf(name,sizeof name,"VRL%c_%08x",host?'H':'G',(uint32_t)now);
        if(!xyz_online_connect_races(name,"VR_LINK_DIAGNOSTIC",profile,device)){finished=1;return;}
        start=now;started=1;
        fprintf(stderr,"[XYZ race link] role=%s temporary session, private lobby, start_probe=%d\n",host?"host":"guest",request_start);
    }
    if(now-start>(request_world?300000000ull:180000000ull) || status.state==XYZ_ONLINE_STOPPED || status.state==XYZ_ONLINE_QUEUED)goto done;
    if(status.state!=XYZ_ONLINE_CONNECTED || !status.lan_ready)return;
    if(!saved){
        memcpy(previous_profile,(void*)XBOX_PTR(0x41c340),sizeof previous_profile);previous_host=MEM32(0x42a48c);previous_mode=MEM32(0x41c338);saved=1;
        if(!xyz_xbox_lobby_begin(&community_engine_guest,&community_engine_lobby,sample,name,status.generation,0x196390,now))goto done;
    }
    xyz_xbox_lobby_tick(&community_engine_guest,&community_engine_lobby,status.generation,now);
    if(community_engine_lobby.stage==XYZ_LOBBY_FAILED)goto done;
    if(!action && community_engine_lobby.stage==XYZ_LOBBY_READY){
        if(host){
            /* Original network settings: MAXSIZE, MINSIZE, Circuit, any class,
             * unranked, private/password protected, three laps on the same
             * track 4014 already verified in offline Circuit on both builds. */
            XyzRaceSettings settings={0};settings.track=4014;settings.laps=3;
            settings.min_players=settings.max_players=2;settings.unrestricted=settings.private_game=1;
            memcpy(settings.password,password,sizeof settings.password);
            if(!xyz_xbox_race_settings_apply(&community_engine_guest,&community_engine_lobby,&settings_lease,sample,&settings))goto done;
            if(!xyz_xbox_lobby_create(&community_engine_guest,&community_engine_lobby,0,now))goto done;
        } else if(!xyz_xbox_lobby_join(&community_engine_guest,&community_engine_lobby,game,password,now))goto done;
        action=1;
    }
    if(action==1 && community_engine_lobby.stage==XYZ_LOBBY_IN_GAME){
        XyzLobbySnapshot roster;
        if(!xyz_xbox_lobby_snapshot(&community_engine_guest,&community_engine_lobby,&roster))goto done;
        if(host && !announced){fprintf(stderr,"[XYZ race link] private lobby ready: %s\n",roster.game_name);announced=1;}
        if(roster.players!=2)return;
        unsigned other=strcmp(roster.player[0],community_engine_lobby.name)?0:1;
        uint32_t peer=MEM32(community_engine_lobby.games+0x1f0+other*0x64);
        if((peer>>16)!=0x0a4d || peer==0x0a4d0001 || !peer)goto done;
        char address[20];snprintf(address,sizeof address,"%u.%u.%u.%u",peer>>24,(peer>>16)&255,(peer>>8)&255,peer&255);
        memcpy((void*)XBOX_PTR(community_engine_lobby.arena+0x2800),address,strlen(address)+1);
        args[0]=community_engine_lobby.arena+0x2800;args[1]=3658;args[2]=host?1:0;
        utility=community_lobby_call(0x18e5a0,3,args);
        if(!xyz_xbox_address(utility,0xbc))goto done;
        fprintf(stderr,"[XYZ race link] original NetGameUtil open role=%s peer=%s\n",host?"listen":"connect",address);action=2;
    }
    if(action==2){
        args[0]=utility;link=community_lobby_call(0x18e670,1,args);
        if(!link)return;
        if(!xyz_xbox_address(link,0x1cc))goto done;
        fprintf(stderr,"[XYZ race link] original NetGameLink connected\n");action=3;
    }
    if(action==3){
        /* The retail utility sets mwid=100 (0x18e601), including room for
         * the link's type/sync trailer. Match its actual game-packet limit. */
        uint8_t payload[100];for(unsigned i=0;i<sizeof payload;++i)payload[i]=(uint8_t)(i*29+(host?73:151));
        uint32_t arena=community_engine_lobby.arena;
        if(!sent){
            memcpy((void*)XBOX_PTR(arena+0x2800),payload,sizeof payload);
            args[0]=link;args[1]=arena+0x2800;args[2]=sizeof payload;args[3]=1;
            if((int32_t)community_lobby_call(0x18e710,4,args)>0)sent=1;
        }
        args[0]=link;args[1]=arena+0x2900;args[2]=arena+0x2c00;
        result=community_lobby_call(0x18e6d0,3,args);
        if(result&255){
            if(MEM16(arena+0x2904)!=sizeof payload || !MEM8(arena+0x2c00))goto done;
            for(unsigned i=0;i<sizeof payload;++i)if(MEM8(arena+0x2908+i)!=(uint8_t)(i*29+(host?151:73)))goto done;
            received=1;
        }
        if(sent && received){fprintf(stderr,"[XYZ race link] reliable payload: PASS bytes=%u role=%s\n",(unsigned)sizeof payload,host?"host":"guest");hold_until=now+3000000ull;action=4;}
    }
    if(action==4){
        args[0]=link;args[1]=community_engine_lobby.arena+0x2900;args[2]=community_engine_lobby.arena+0x2c00;
        community_lobby_call(0x18e6d0,3,args); /* flush acknowledgement/keepalive */
        if(now<hold_until)return;
        if(request_start){
            args[0]=link;community_lobby_call(0x294b10,1,args);link=0;
            args[0]=utility;community_lobby_call(0x294400,1,args);utility=0;
            /* Select an actually connected logical pad, as the retail Ready
             * event does from its input mask. USB PAD1 need not be logical
             * index zero; choosing a disconnected index opens the original
             * controller dialog and prevents the human driver from binding. */
            unsigned controller=0;
            while(controller<4 && MEM8(0x427ff8+controller)==UINT8_MAX)++controller;
            if(!xyz_xbox_lobby_select_controller(&community_engine_guest,&community_engine_lobby,controller))goto done;
            fprintf(stderr,"[XYZ race start] connected logical controller=%u device=%u\n",controller,MEM8(0x427ff8+controller));
            if(!xyz_xbox_lobby_ready(&community_engine_guest,&community_engine_lobby,1))goto done;
            action=10;
        } else {
        if(host){XyzLobbySnapshot roster;
            if(!xyz_xbox_lobby_snapshot(&community_engine_guest,&community_engine_lobby,&roster))goto done;
            if(roster.players==2)return;
            if(roster.players!=1)goto done;
        }
        if(!xyz_xbox_lobby_leave(&community_engine_guest,&community_engine_lobby,now))goto done;action=5;
        }
    }
    if(action==10){
        XyzLobbySnapshot roster;
        if(!xyz_xbox_lobby_snapshot(&community_engine_guest,&community_engine_lobby,&roster) || roster.players!=2)goto done;
        if(host){
            if(!(roster.player_flags[1]&0x08000000u))return;
            if(!xyz_xbox_lobby_start(&community_engine_guest,&community_engine_lobby,now))goto done;
            fprintf(stderr,"[XYZ race start] original host Start queued\n");
        }
        action=11;
    }
    if(action==11 && community_engine_lobby.stage==XYZ_LOBBY_PLAY_READY){
        fprintf(stderr,"[XYZ race start] original play handled; peer count=1\n");
        if(!xyz_xbox_race_begin(&community_race_guest,&community_engine_race,&community_engine_lobby,sample))goto done;
        fprintf(stderr,"[XYZ race start] original lobby released before race initialization\n");
        action=12;hold_until=now+30000000ull;
    }
    if(action==12){
        int net=(int)MEM32(host?0x42a714:0x42a6f4),players=MEM8(0x4562b0);
        if(net!=last_net || players!=last_players){
            fprintf(stderr,"[XYZ race start] coordinator role=%s state=%d players=%d game=%u peers=%u\n",
                host?"host":"guest",net,players,MEM32(0x456290),MEM32(0x42a480));last_net=net;last_players=players;
        }
        if(players==2 && (host?MEM32(0x42a480)==1:net>=2)){
            fprintf(stderr,"[XYZ race start] original coordinator two players: PASS role=%s\n",host?"host":"guest");
            action=request_world?20:13;hold_until=now+(request_world?90000000ull:5000000ull);
        } else if(now>=hold_until)goto done;
    }
    if(action>=20 && action<=22 && now>=next_world_log){
        fprintf(stderr,"[XYZ race world] role=%s action=%d flow=%u track=%u type=%u game=%u players=%u sample=%d car=%08x loader_idle=%u\n",
            host?"host":"guest",action,sample->flow,MEM32(0x3cc460),MEM32(0x3cc470),MEM32(0x456290),MEM8(0x4562b0),sample->status,sample->car,community_engine_load_pumps);
        next_world_log=now+5000000ull;
        if(request_countdown && action==21){
            fprintf(stderr,"[XYZ race countdown] role=%s game=%u start_clock=%08x wait_clock=%u clock=%u online_error=%u\n",
                host?"host":"guest",MEM32(0x456290),MEM32(0x4562b4),MEM32(0x4562ec),MEM32(0x428a00),MEM8(0x42a4e0));
            for(unsigned i=0;i<6;++i){uint32_t player=MEM32(0x456298+4*i);
                if(player && xyz_xbox_address(player,0x28))fprintf(stderr,"[XYZ race countdown] slot=%u local=%d state=%u id=%u\n",
                    i,player==MEM32(0x456294),MEM32(player),MEM8(player+4));
            }
        }
    }
    if(action==20){
        if(sample->status==XYZ_XBOX_OTHER_RACE && sample->track==4014 && sample->race_type==1 && MEM8(0x4562b0)==2){
            fprintf(stderr,"[XYZ race world] loaded Circuit and valid local car: PASS role=%s\n",host?"host":"guest");
            action=21;hold_until=now+(request_countdown?90000000ull:20000000ull);
        } else if(now>=hold_until)goto done;
    }
    if(action==21 && request_countdown && !countdown){
        if(MEM32(0x456290)==5 && sample->status==XYZ_XBOX_OTHER_RACE && MEM8(0x4562b0)==2){
            fprintf(stderr,"[XYZ race countdown] original race running: PASS role=%s\n",host?"host":"guest");
            countdown=1;hold_until=now+(request_motion?65000000ull:20000000ull);
        } else if(now<hold_until)return;
        else {
            /* Preserve ordinary world cleanup on a failed readiness barrier. */
            fprintf(stderr,"[XYZ race countdown] readiness timeout role=%s\n",host?"host":"guest");
        }
    }
    if(action==21 && countdown && request_motion && now>=next_motion && MEM32(0x456290)==5 && sample->status==XYZ_XBOX_OTHER_RACE){
        community_race_probe_motion(sample,host,now);next_motion=now+100000ull;
    }
    if(action==21 && now>=hold_until){
        /* Schedule the retail world-to-frontend transition at its normal main
         * loop boundary; never run the heavy asset teardown inside this hook. */
        if(!xyz_xbox_race_return(&community_race_guest,&community_engine_race,sample))goto done;
        fprintf(stderr,"[XYZ race world] original return requested role=%s\n",host?"host":"guest");
        action=22;hold_until=now+60000000ull;
    }
    if(action==22){
        if(sample->flow==3){
            fprintf(stderr,"[XYZ race world] original frontend return: PASS role=%s\n",host?"host":"guest");
            action=13;hold_until=now;
        } else if(now>=hold_until)goto done;
    }
    if(action==13 && now>=hold_until){
        /* Flow 3 can precede the last queued frontend/movie transition.
         * Keep normal cleanup pending until the shared ownership gate clears. */
        if(!xyz_xbox_race_close(&community_race_guest,&community_engine_race,sample)){
            if(!cleanup_until)cleanup_until=now+30000000ull;
            if(now>=cleanup_until)goto done;
            return;
        }
        if(community_engine_lobby.stage==XYZ_LOBBY_RACE){action=6;goto done;}
        if(MEM32(community_engine_lobby.games+0x1c)==UINT32_MAX){action=6;goto done;}
        if(!xyz_xbox_lobby_leave(&community_engine_guest,&community_engine_lobby,now))goto done;action=5;
    }
    if(action==5 && community_engine_lobby.stage==XYZ_LOBBY_READY){action=6;goto done;}
    return;
done:
    if(community_engine_race.captured && !xyz_xbox_race_close(&community_race_guest,&community_engine_race,sample)){
        fprintf(stderr,"[XYZ race link] deferring owned cleanup until frontend; flow=%u race_error=%d\n",sample->flow,community_engine_race.error);
        xyz_xbox_race_return(&community_race_guest,&community_engine_race,sample);action=90;return;
    }
    if(!xyz_xbox_race_settings_release(&community_engine_guest,&settings_lease,sample)){
        fprintf(stderr,"[XYZ race link] waiting for owned settings cleanup\n");action=90;return;
    }
    fprintf(stderr,"[XYZ race link] %s role=%s action=%d lobby=%d error=%d sent=%d received=%d session=%u elapsed=%.3f\n",
        action==6 && (!request_countdown || countdown)?"PASS":"FAIL",host?"host":"guest",action,community_engine_lobby.stage,community_engine_lobby.error,sent,received,status.generation,(now-start)/1000000.0);
    if(link){args[0]=link;community_lobby_call(0x294b10,1,args);link=0;}
    if(utility){args[0]=utility;community_lobby_call(0x294400,1,args);utility=0;}
    xyz_xbox_lobby_close(&community_engine_guest,&community_engine_lobby);
    args[0]=0x46d6d0;community_lobby_call(0x2932b0,1,args);
    if(saved){memcpy((void*)XBOX_PTR(0x41c340),previous_profile,sizeof previous_profile);MEM32(0x42a48c)=previous_host;MEM32(0x41c338)=previous_mode;}
    xyz_online_disconnect();finished=1;
}
