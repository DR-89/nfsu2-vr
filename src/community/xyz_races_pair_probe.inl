/* Explicit two-process QA through the production browser on both sides:
 * copied catalog/create, private password join, ready/start and full return.
 * Only the exact newly owned lobby is selected. The optional finish probe
 * lets the original AI complete the race; it never injects a result or a
 * career change. The launcher provides fresh ephemeral identities. */
static void community_races_pair_probe(const XyzXboxFrame *frame,int main_menu,uint64_t now)
{
    static int enabled=-1,host,phase,finished,passed,browser_guest,career_guest,finish_probe,autopilot,completed;
    static unsigned expected_track=4014;
    static uint64_t began,stable,due,search_due;
    static uint8_t before[0x4c];
    static uint32_t thread,tls_base;
    static const char *profile,*device,*secret,*game;
    static char name[16];
    static float career_position[3];
    if(enabled<0){const char *role=getenv("NFSU2_XYZ_RACES_PAIR_PROBE");host=role && !strcmp(role,"host");
        browser_guest=role && !strcmp(role,"browser-guest");
        career_guest=role && !strcmp(role,"career-guest");
        enabled=host || browser_guest || career_guest || (role && !strcmp(role,"guest"));profile=getenv("NFSU2_XYZ_PROBE_PROFILE");device=getenv("NFSU2_XYZ_PROBE_DEVICE");
        secret=getenv("NFSU2_XYZ_PROBE_PASSWORD");game=getenv("NFSU2_XYZ_PROBE_GAME");
        const char *finish=getenv("NFSU2_XYZ_RACE_FINISH_PROBE");finish_probe=finish && !strcmp(finish,"1");}
    if(!enabled || finished)return;
    if(!began)began=now;
    XYZRaceBrowser browser;xyz_races_snapshot(&browser);
    XYZOnlineStatus online;xyz_online_snapshot(&online);
    if(phase==90)goto cleanup;
    /* Career loading can include the original intro and unlock dialogs. */
    if(now-began>900000000ull)goto failed;
    if(!phase){
        if(!main_menu){stable=0;return;}if(!stable)stable=now;if(now-stable<1000000ull)return;
        if(!profile || strlen(profile)!=32 || !device || strlen(device)!=32 ||
           !secret || strlen(secret)!=12 || strspn(secret,"0123456789abcdef")!=12 ||
           (!host && (!game || !*game || strlen(game)>31)) || online.state!=XYZ_ONLINE_OFFLINE)goto failed;
        memcpy(before,(void*)XBOX_PTR(0x41c340),sizeof before);
        thread=g_fs_base;tls_base=MEM32(g_fs_base+4);if(!tls_base)goto failed;
        snprintf(name,sizeof name,"VRU%c_%08x",host?'H':'G',(uint32_t)now);
        if(career_guest){
            uint32_t args[3]={0x34c7a0,0x34c7f8,0};
            guest_call_full(recomp_lookup(0x16d3a0),0,0,0x0018ab7fu,3,args);
            phase=80;return;
        }
        if(!xyz_races_open(name,"VR_UI_RACE_DIAGNOSTIC",profile,device))goto failed;
        phase=1;began=now;return;
    }
    if(g_fs_base!=thread || MEM32(thread+4)!=tls_base || browser.state==XYZ_RACES_ERROR)goto failed;
    if(phase==80 && community_career_package("UI_StartCareer.fng")){
        uint32_t mask=0;for(unsigned port=0;port<4;++port)if(MEM8(0x427ff8+port)!=UINT8_MAX){mask=1u<<port;break;}
        if(!mask)return;
        guest_call_full(recomp_lookup(0xafbd0),0,0,0x0018ab7fu,1,&mask);phase=81;return;
    }
    if(phase==81 && community_career_package("UI_CareerCrib.fng")){
        uint32_t args[2]={0x9a3b571d,0x34c80c};
        guest_call_full(recomp_lookup(0x166470),0,0,0x0018ab7fu,2,args);phase=82;return;
    }
    if((phase==81 || phase==82) && frame->status==XYZ_XBOX_READY && frame->flow==6 && MEM32(0x41c338)==1){
        /* Match real Pause eligibility: the freshly loaded career's intro
         * must finish before opening a menu. Forcing Pause during NIS can
         * leave that sequence owning cars across the world transition. */
        if(MEM32(0x38dbb0) || MEM32(0x38dbc8) || MEM32(0x38dbe0) ||
           MEM8(0x38dba4) || MEM8(0x38dbbc) || MEM8(0x38dbd4) || MEM8(0x3e3ab0)){due=0;return;}
        if(!due)due=now+3000000;if(now<due || MEM32(0x441b48) || MEM32(0x45c4b4))return;
        memcpy(career_position,frame->state.p,sizeof career_position);
        uint32_t args[2]={0x34d94c,0};guest_call_full(recomp_lookup(0x161190),0,0,0x0018ab7fu,2,args);
        phase=83;stable=0;return;
    }
    if(phase==83){
        if(!quest_cockpit_ui_paused(cockpit_ui_read,NULL)){stable=0;return;}
        if(!stable)stable=now;if(now-stable<1000000)return;
        memcpy(before,(void*)XBOX_PTR(0x41c340),sizeof before);
        if(!xyz_races_open(name,"VR_UI_RACE_DIAGNOSTIC",profile,device))goto failed;
        fprintf(stderr,"[XYZ races pair] guest production browser opened from actual career free roam\n");
        phase=1;began=now;return;
    }
    if(phase==1 && browser.state==XYZ_RACES_READY && now>=search_due){
        if(host){
            XyzRaceSettings options={0};options.track=4014;options.laps=finish_probe?2:3;options.min_players=options.max_players=2;options.unrestricted=1;
            memcpy(options.password,secret,13);
            int accepted=xyz_races_create(browser.epoch,&options);xyz_auth_clear(&options,sizeof options);
            if(!accepted)goto failed;
            fprintf(stderr,"[XYZ races pair] host production create requested catalog=%u\n",browser.track_count);phase=2;return;
        }
        for(unsigned i=0;i<browser.count;++i)if(!strcmp(browser.rows[i].name,game)){
            if(!browser.info[i].compatible || !browser.info[i].track_name[0] ||
               !xyz_races_join(browser.epoch,i,&browser.rows[i],secret))goto failed;
            if(browser_guest)expected_track=browser.info[i].track;
            fprintf(stderr,"[XYZ races pair] guest selected map=%s track=%u\n",browser.info[i].track_name,browser.info[i].track);phase=2;return;
        }
        unsigned next=browser.count==XYZ_RACES_PAGE_SIZE?browser.start+XYZ_RACES_PAGE_SIZE:0;
        if(next>2000 || !xyz_races_search(browser.epoch,next))goto failed;search_due=now+2000000ull;return;
    }
    if(phase==2 && browser.state==XYZ_RACES_IN_LOBBY){
        if(host){
            if(!browser.host)goto failed;
            fprintf(stderr,"[XYZ races pair] host password lobby ready: %s\n",browser.lobby.game_name);phase=3;
        }else{
            if(browser.lobby.players!=2 || browser.host || !xyz_races_ready(browser.epoch,1))goto failed;
            fprintf(stderr,"[XYZ races pair] guest joined and ready requested\n");phase=4;
        }return;
    }
    if(phase==3 && browser.state==XYZ_RACES_IN_LOBBY && browser.lobby.players==2 && browser.startable){
        if(!xyz_races_ready(browser.epoch,1))goto failed;phase=8;return;
    }
    if(phase==8 && browser.state==XYZ_RACES_IN_LOBBY && browser.local_ready && browser.startable){
        if(!xyz_races_start(browser.epoch))goto failed;phase=4;return;
    }
    if(phase==4 && browser.state==XYZ_RACES_RACING && frame->status==XYZ_XBOX_OTHER_RACE && frame->track==expected_track && MEM32(0x456290)==5){
        if(career_guest){
            uint32_t record=MEM32(frame->car+0x14);
            float dx=frame->state.p[0]+224.476f,dy=frame->state.p[1]-83.339f;
            if(!xyz_xbox_address(record,0x940))goto failed;
            fprintf(stderr,"[XYZ races pair] career guest Resort Loop grid distance2=%.3f spawn_override=%u position=(%.3f,%.3f,%.3f)\n",
                dx*dx+dy*dy,MEM8(record+0x91c),frame->state.p[0],frame->state.p[1],frame->state.p[2]);
            /* NetGame may install an explicit synchronized grid spawn after
             * configuration. Check its actual position, not a zero flag. */
            if(dx*dx+dy*dy>10000.f)goto failed;
        }
        fprintf(stderr,"[XYZ races pair] %s production race running track=%u players=%u tls_intact=1\n",host?"host":"guest",frame->track,MEM8(0x4562b0));
        due=now+(host?30000000ull:20000000ull);phase=5;return;
    }
    if(phase==5 && finish_probe){
        uint32_t player=MEM32(0x44940c),race=MEM32(0x449478);
        if(!autopilot && xyz_xbox_address(player,0x238) && MEM32(player+4)==frame->car &&
           xyz_xbox_address(race,0xbf0) && MEM32(race)==4){
            if(career_guest){
                float dx=frame->state.p[0]+224.476f,dy=frame->state.p[1]-83.339f;
                if(dx*dx+dy*dy>10000.f)goto failed;
                fprintf(stderr,"[XYZ races pair] career guest stayed on race grid through countdown\n");
            }
            /* Audited Xbox Player::AutoPilotOn. The ordinary AI drives the
             * whole lap; no teleport, result write or finish injection. */
            guest_call_full(recomp_lookup(0x1eb840),player,0,0x1eb840,0,NULL);autopilot=1;
            fprintf(stderr,"[XYZ race finish] %s original autopilot enabled car=%08x\n",host?"host":"guest",frame->car);
        }
        if(!completed && xyz_xbox_address(race,0xbf0)){
            unsigned n=MEM32(race+0x24);
            if(n==2)for(unsigned i=0;i<n;i++){
                uint32_t racer=MEM32(race+0xbd0+4*i);
                if(xyz_xbox_address(racer,0x20) && MEM32(racer)==frame->car && MEM16(racer+0x10)==1 &&
                   MEM8(racer+0xc)>=1 && MEM8(racer+0xc)<=2){
                    completed=1;due=now+(host?30000000ull:20000000ull);
                    fprintf(stderr,"[XYZ race finish] %s crossed finish position=%u coordinator=%u elapsed=%.2f\n",host?"host":"guest",MEM8(racer+0xc),MEM32(race),(now-began)*.000001);
                }
            }
        }
        if(!completed)return;
    }
    if(phase==5 && now>=due){if(!xyz_races_leave(browser.epoch))goto failed;phase=6;return;}
    if(phase==6 && browser.state==XYZ_RACES_READY && main_menu){
        fprintf(stderr,"[XYZ races pair] %s production browser returned\n",host?"host":"guest");
        if(!xyz_races_close())goto failed;phase=7;return;
    }
    if(phase==7 && !browser.active && browser.state==XYZ_RACES_OFFLINE){
        if(career_guest){
            if(frame->status!=XYZ_XBOX_READY || frame->flow!=6)return;
            float distance2=0;for(unsigned i=0;i<3;++i){float d=frame->state.p[i]-career_position[i];distance2+=d*d;}
            if(distance2>100.f || MEM32(0x41c338)!=1 || community_engine_lobby.stage!=XYZ_LOBBY_EMPTY)goto failed;
            fprintf(stderr,"[XYZ races pair] guest returned to actual career free roam distance2=%.3f lobby=0\n",distance2);
        }
        passed=1;goto cleanup;
    }
    return;
failed:
    fprintf(stderr,"[XYZ races pair] FAIL role=%s phase=%d browser=%d lobby=%d error=%d\n",host?"host":"guest",phase,browser.state,community_engine_lobby.stage,community_engine_lobby.error);
    phase=90;
cleanup:
    if(browser.active){xyz_races_close();phase=90;return;}
    int restored=!memcmp((void*)XBOX_PTR(0x41c340),before,sizeof before);
    int tib_ok=thread==g_fs_base && tls_base && MEM32(thread+4)==tls_base;
    fprintf(stderr,"[XYZ races pair] %s role=%s profile_restored=%d tls_intact=%d lobby=%d\n",passed && restored && tib_ok?"PASS":"FAIL",host?"host":"guest",restored,tib_ok,community_engine_lobby.stage);
    xyz_auth_clear(before,sizeof before);finished=1;
}
