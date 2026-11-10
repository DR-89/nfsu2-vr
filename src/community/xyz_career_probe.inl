/* Opt-in, isolated-save diagnostic. Uses original resume, pause and quit;
 * never fabricates a career or writes a position into it. */
static int community_career_package(const char *expected)
{
    uint32_t front=MEM32(0x3fbd08),manager=0,node=0;
    if(!front || !cockpit_ui_read(NULL,front+8,&manager,4) || !manager || !cockpit_ui_read(NULL,manager+0xe4,&node,4))return 0;
    for(unsigned i=0;node && i<32;++i){uint32_t fields[4];
        if(!cockpit_ui_read(NULL,node,fields,sizeof fields))return 0;
        if(fields[3] && quest_ui_package(cockpit_ui_read,NULL,fields[3],expected))return 1;
        node=fields[1];
    }return 0;
}
static void community_career_probe(const XyzXboxFrame *frame,int main_menu,uint64_t now)
{
    static int enabled=-1,phase,finished,round;static uint64_t began,stable,due;
    static float position[3];static const char *profile,*device;
    if(enabled<0){const char *s=getenv("NFSU2_XYZ_CAREER_PROBE");enabled=s && *s=='1';
        profile=getenv("NFSU2_XYZ_PROBE_PROFILE");device=getenv("NFSU2_XYZ_PROBE_DEVICE");}
    if(!enabled || finished)return;if(!began)began=now;
    XYZRaceBrowser browser;xyz_races_snapshot(&browser);
    if(now-began>900000000ull || browser.state==XYZ_RACES_ERROR){
        fprintf(stderr,"[XYZ career probe] FAIL phase=%d flow=%u mode=%u browser=%d notice=%s\n",phase,frame->flow,MEM32(0x41c338),browser.state,browser.notice);
        xyz_races_close();finished=1;return;
    }
    if(!phase){
        if(!main_menu){stable=0;return;}if(!stable)stable=now;if(now-stable<1000000)return;
        if(!profile || strlen(profile)!=32 || !device || strlen(device)!=32){finished=1;return;}
        uint32_t args[3]={0x34c7a0,0x34c7f8,0};guest_call_full(recomp_lookup(0x16d3a0),0,0,0x0018ab7fu,3,args);
        fprintf(stderr,"[XYZ career probe] original career menu requested\n");phase=8;return;
    }
    if(phase==8 && community_career_package("UI_StartCareer.fng")){
        /* 119700 maps the initiating menu input's bitmask to the actual
         * connected logical port. Headless USB need not be logical port 0. */
        uint32_t mask=0;for(unsigned port=0;port<4;++port)if(MEM8(0x427ff8+port)!=UINT8_MAX){mask=1u<<port;break;}
        if(!mask)return;
        guest_call_full(recomp_lookup(0xafbd0),0,0,0x0018ab7fu,1,&mask);
        fprintf(stderr,"[XYZ career probe] original career resume requested controller_mask=%u\n",mask);phase=1;return;
    }
    if(phase==1 && community_career_package("UI_CareerCrib.fng")){
        uint32_t args[2]={0x9a3b571d,0x34c80c};guest_call_full(recomp_lookup(0x166470),0,0,0x0018ab7fu,2,args);
        fprintf(stderr,"[XYZ career probe] original Explore requested\n");phase=2;return;
    }
    if((phase==1 || phase==2) && frame->status==XYZ_XBOX_READY && frame->flow==6 && MEM32(0x41c338)==1){
        if(!due)due=now+3000000;if(now<due || MEM32(0x441b48) || MEM32(0x45c4b4))return;
        /* Same original package opener as the pause handler at 1F6A28.
         * The outer world coordinator's event numbers are a different enum. */
        uint32_t args[2]={0x34d94c,0};guest_call_full(recomp_lookup(0x161190),0,0,0x0018ab7fu,2,args);
        memcpy(position,frame->state.p,sizeof position);phase=3;stable=0;
        fprintf(stderr,"[XYZ career probe] actual free roam loaded; original pause requested\n");return;
    }
    if(phase==3){
        if(!quest_cockpit_ui_paused(cockpit_ui_read,NULL)){stable=0;return;}
        if(!stable)stable=now;if(now-stable<1000000)return;
        if(!xyz_races_open("VR_CareerQA","VR_CAREER_DIAGNOSTIC",profile,device)){finished=1;return;}
        phase=4;fprintf(stderr,"[XYZ career probe] production browser requested from career\n");return;
    }
    if(phase==4 && main_menu && browser.state==XYZ_RACES_READY){
        if(!browser.from_career || !browser.track_count || !xyz_races_close()){finished=1;return;}
        fprintf(stderr,"[XYZ career probe] production browser ready maps=%u; return requested\n",browser.track_count);phase=5;return;
    }
    if(phase==5 && !browser.active && frame->status==XYZ_XBOX_READY && frame->flow==6){
        float d=0;for(unsigned i=0;i<3;++i){float v=frame->state.p[i]-position[i];d+=v*v;}
        if(d>100.f || MEM32(0x41c338)!=1 || community_engine_lobby.stage!=XYZ_LOBBY_EMPTY){
            fprintf(stderr,"[XYZ career probe] FAIL return distance2=%.3f mode=%u lobby=%d\n",d,MEM32(0x41c338),community_engine_lobby.stage);finished=1;return;
        }
        fprintf(stderr,"[XYZ career probe] round=%d returned track=%u distance2=%.3f lobby=0\n",++round,frame->track,d);
        if(round<2){phase=2;due=now+3000000;return;}
        fprintf(stderr,"[XYZ career probe] PASS career-browser-career twice, maps, position, disconnect\n");finished=1;
    }
}
