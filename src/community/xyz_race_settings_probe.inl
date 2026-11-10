/* Opt-in original-engine verification. Six private, unranked lobbies, no
 * opponent joins, race starts, reports, saved identities or career writes. */
static void community_race_settings_probe(const XyzXboxFrame *frame,int main_menu,uint64_t now)
{
    static int enabled=-1,phase,finished,owned,passed;
    static unsigned index;
    static uint64_t began,stable,next_create;
    static XyzRaceSettingsLease lease;
    static XyzRaceSettings options;
    static uint8_t before[0x4c];
    static const char *profile,*device;
    static const unsigned modes[]={0,1,2,3,4,10};
    if(enabled<0){const char *e=getenv("NFSU2_XYZ_RACE_SETTINGS_PROBE");enabled=e && !strcmp(e,"1");
        profile=getenv("NFSU2_XYZ_PROBE_PROFILE");device=getenv("NFSU2_XYZ_PROBE_DEVICE");}
    if(!enabled || finished)return;
    if(!began)began=now;
    XYZOnlineStatus status;xyz_online_snapshot(&status);
    if(phase==90)goto cleanup;
    if(now-began>180000000ull)goto failed;
    if(!phase){
        if(!main_menu || frame->status!=XYZ_XBOX_FRONTEND){stable=0;return;}
        if(!stable)stable=now;if(now-stable<500000ull)return;
        if(status.state!=XYZ_ONLINE_OFFLINE || community_engine_lobby.stage!=XYZ_LOBBY_EMPTY ||
           !profile || strlen(profile)!=32 || !device || strlen(device)!=32)goto failed;
        memcpy(before,(void*)XBOX_PTR(0x41c340),sizeof before);
        if(!xyz_online_connect_races("VR_SettingsQA","VR_SETTINGS_DIAGNOSTIC",profile,device))goto failed;
        owned=1;phase=1;began=now;
        fprintf(stderr,"[XYZ race settings probe] private session requested\n");return;
    }
    if(status.state==XYZ_ONLINE_STOPPED || status.state==XYZ_ONLINE_OFFLINE || status.state==XYZ_ONLINE_RETRY)goto failed;
    if(status.state!=XYZ_ONLINE_CONNECTED || !status.lan_ready)return;
    if(community_engine_lobby.stage==XYZ_LOBBY_EMPTY &&
       !xyz_xbox_lobby_begin(&community_engine_guest,&community_engine_lobby,frame,"VR_SettingsQA",status.generation,0x196390,now))goto failed;
    xyz_xbox_lobby_tick(&community_engine_guest,&community_engine_lobby,status.generation,now);
    if(community_engine_lobby.stage==XYZ_LOBBY_FAILED)goto failed;
    if(phase==2 && community_engine_lobby.stage==XYZ_LOBBY_READY && community_engine_lobby.error!=XYZ_LOBBY_OK)goto failed;
    if(phase==1 && community_engine_lobby.stage==XYZ_LOBBY_READY){
        if(now<next_create)return;
        XyzRaceTrack tracks[XYZ_TRACK_LIMIT];unsigned count=0;XyzRaceRules rules;
        if(!xyz_xbox_tracks(cockpit_ui_read,NULL,modes[index],tracks,XYZ_TRACK_LIMIT,&count) || !count ||
           !xyz_xbox_track_rules(modes[index],tracks,&rules))goto failed;
        memset(&options,0,sizeof options);options.mode=modes[index];options.track=tracks[0].id;
        options.laps=rules.min_laps;options.min_players=2;options.max_players=rules.max_players;
        options.reverse=(index&1) && tracks[0].reverse;options.collision=index&1;
        options.catchup=options.mode!=3 && options.mode!=10;options.unrestricted=options.private_game=1;
        memcpy(options.password,profile,12);
        if(!xyz_xbox_race_settings_apply(&community_engine_guest,&community_engine_lobby,&lease,frame,&options) ||
           !xyz_xbox_lobby_create(&community_engine_guest,&community_engine_lobby,options.performance,now))goto failed;
        phase=2;return;
    }
    if(phase==2 && community_engine_lobby.stage==XYZ_LOBBY_IN_GAME){
        XyzLobbySnapshot room;char params[132];uint32_t flags,min_players,max_players;
        uint32_t games=community_engine_lobby.games;
        if(!xyz_xbox_lobby_snapshot(&community_engine_guest,&community_engine_lobby,&room) || room.players!=1 ||
           !cockpit_ui_read(NULL,games+0xa8,params,sizeof params) || !memchr(params,0,sizeof params) ||
           !cockpit_ui_read(NULL,games+0x12c,&flags,4) || !cockpit_ui_read(NULL,games+0x134,&min_players,4) ||
           !cockpit_ui_read(NULL,games+0x138,&max_players,4))goto failed;
        XyzLobbyGame game={0};XyzRaceInfo info;
        memcpy(game.params,params,sizeof params);game.custom_flags=flags;
        game.min_players=min_players;game.max_players=max_players;game.players=1;
        if(!xyz_xbox_race_info(cockpit_ui_read,NULL,&game,&info) || !info.compatible || !info.track_name[0] || info.mode!=options.mode ||
           info.track!=options.track || info.laps!=options.laps || info.reverse!=options.reverse ||
           min_players!=options.min_players || max_players!=options.max_players ||
           (flags&0x103)!=(0x100u|options.collision|(options.catchup<<1)))goto failed;
        fprintf(stderr,"[XYZ race settings probe] verified mode=%u track=%u laps=%u direction=%u players=%u/%u flags=%08x map=%s\n",
            options.mode,options.track,options.laps,options.reverse,min_players,max_players,flags,info.track_name);
        if(!xyz_xbox_lobby_leave(&community_engine_guest,&community_engine_lobby,now))goto failed;
        phase=3;return;
    }
    if(phase==3 && community_engine_lobby.stage==XYZ_LOBBY_READY){
        if(!xyz_xbox_race_settings_release(&community_engine_guest,&lease,frame) ||
           memcmp((void*)XBOX_PTR(0x41c340),before,sizeof before))goto failed;
        if(++index<sizeof modes/sizeof modes[0]){phase=1;next_create=now+2500000ull;return;}
        passed=1;goto cleanup;
    }
    return;
failed:
    fprintf(stderr,"[XYZ race settings probe] FAIL phase=%d index=%u lobby=%d error=%d server=%08x\n",
        phase,index,community_engine_lobby.stage,community_engine_lobby.error,community_engine_lobby.reply_code);phase=90;
cleanup:
    if(!xyz_xbox_race_settings_release(&community_engine_guest,&lease,frame)){phase=90;return;}
    if(owned){xyz_xbox_lobby_close(&community_engine_guest,&community_engine_lobby);xyz_online_disconnect();owned=0;}
    int restored=!memcmp((void*)XBOX_PTR(0x41c340),before,sizeof before);
    xyz_auth_clear(&options,sizeof options);xyz_auth_clear(before,sizeof before);
    fprintf(stderr,"[XYZ race settings probe] %s lobbies=%u profile_restored=%d\n",passed && restored?"PASS":"FAIL",index,restored);
    finished=1;
}
