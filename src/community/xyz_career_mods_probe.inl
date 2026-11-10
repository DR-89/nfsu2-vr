/* Explicit isolated-career diagnostic. Opens the original career/Explore
 * menus, waits for NIS to release the car, then observes navigation state.
 * The explicit race mode starts an already available stock career race using
 * the original frontend race-launch path after a normal world unload.
 * No service connection or fabricated progression. */
static void community_career_mods_probe(const XyzXboxFrame *frame,int main_menu,uint64_t now)
{
    static int enabled=-1,phase,finished,race_requested,driving,finish;static uint64_t began,stable,last;
    static XyzXboxCareer career;static uint32_t race_id;
    if(enabled<0){const char *s=getenv("NFSU2_XYZ_CAREER_MODS_PROBE");enabled=!s?0:!strcmp(s,"observe")?1:!strcmp(s,"race")?2:0;
        s=getenv("NFSU2_XYZ_CAREER_MODS_FINISH");finish=s && !strcmp(s,"1");}
    if(!enabled || finished)return;if(!began)began=now;
    if(now-began>900000000ull || MEM32(0x456290)){
        fprintf(stderr,"[XYZ career mods] STOP phase=%d flow=%u mode=%u\n",phase,frame->flow,MEM32(0x41c338));finished=1;return;
    }
    if(!phase){
        if(!main_menu){stable=0;return;}if(!stable)stable=now;if(now-stable<1000000ull)return;
        uint32_t args[3]={0x34c7a0,0x34c7f8,0};guest_call_full(recomp_lookup(0x16d3a0),0,0,0x0018ab7fu,3,args);
        phase=1;fprintf(stderr,"[XYZ career mods] original career requested\n");return;
    }
    if(phase==1 && community_career_package("UI_StartCareer.fng")){
        uint32_t mask=0;for(unsigned port=0;port<4;++port)if(MEM8(0x427ff8+port)!=UINT8_MAX){mask=1u<<port;break;}
        if(!mask)return;guest_call_full(recomp_lookup(0xafbd0),0,0,0x0018ab7fu,1,&mask);phase=2;return;
    }
    if(phase==2 && community_career_package("UI_CareerCrib.fng")){
        uint32_t args[2]={0x9a3b571d,0x34c80c};guest_call_full(recomp_lookup(0x166470),0,0,0x0018ab7fu,2,args);phase=3;return;
    }
    if((phase==2 || phase==3) && frame->status==XYZ_XBOX_READY && frame->flow==6 && MEM32(0x41c338)==1){
        if(MEM32(0x38dbb0) || MEM32(0x38dbc8) || MEM32(0x38dbe0) || MEM8(0x38dba4) ||
           MEM8(0x38dbbc) || MEM8(0x38dbd4) || MEM8(0x3e3ab0) || MEM32(0x441b48) || MEM32(0x45c4b4)){stable=0;return;}
        if(!stable)stable=now;if(now-stable<3000000ull)return;
        phase=4;fprintf(stderr,"[XYZ career mods] actual Explore ready after cinematics\n");
    }
    if(phase>=4 && now-last>=1000000ull){
        last=now;float distance=-1;int metric=0;
        int gps=xyz_xbox_gps(cockpit_ui_read,NULL,frame->state.p,&distance,&metric);
        fprintf(stderr,"[XYZ career mods] flow=%u mode=%u event=%u gps=%d distance=%.3f metric=%d target=(%.3f,%.3f) position=(%.3f,%.3f,%.3f) menu=%d\n",
            frame->flow,MEM32(0x41c338),MEM32(0x406960),gps,distance,metric,MEMF(0x3fbdd0),MEMF(0x3fbdd4),
            frame->state.p[0],frame->state.p[1],frame->state.p[2],quest_cockpit_ui_menu(cockpit_ui_read,NULL));
    }
    if(phase==4 && enabled==2 && !race_requested){
        if(frame->status!=XYZ_XBOX_READY || frame->flow!=6 || MEM32(0x41c338)!=1 || MEM32(0x406960) ||
           quest_cockpit_ui_menu(cockpit_ui_read,NULL) || MEM32(0x38dbb0) || MEM32(0x38dbc8) || MEM32(0x38dbe0) ||
           MEM8(0x38dba4) || MEM8(0x38dbbc) || MEM8(0x38dbd4) || MEM8(0x3e3ab0) || MEM32(0x441b48) || MEM32(0x45c4b4)){
            stable=0;return;
        }
        if(!stable)stable=now;if(now-stable<5000000ull)return;
        uint32_t table=MEM32(0x3fbc30),count=MEM32(0x3fbc34),event=0;
        if(count!=224 || !xyz_xbox_address(table,224*136))return;
        /* 134D70 populates this original list with the current stage's
         * available events. Do not pick directly from the full race table. */
        for(unsigned i=0;i<64;++i){
            uint32_t candidate=MEM32(0x4053f8+i*4);
            if(candidate<table || candidate-table>=224*136 || (candidate-table)%136)continue;
            if(MEM8(candidate+0x37)!=MEM32(0x405d28) || MEM8(candidate+0xd)==1 || MEM8(candidate+0x34)==2)continue;
            if(MEM8(candidate+0xf)>1)continue; /* Circuit or Sprint only. */
            if(!event)event=candidate;
            if(MEM32(candidate+8)==0xca207584u){event=candidate;break;}
        }
        if(!event){
            float distance;int metric;uint32_t player=MEM32(0x44940c);
            if(!driving && xyz_xbox_address(player,0x24) && xyz_xbox_gps(cockpit_ui_read,NULL,frame->state.p,&distance,&metric)){
                guest_call_full(recomp_lookup(0x1eb840),player,0,0x0018ab7fu,0,NULL);driving=1;
                fprintf(stderr,"[XYZ career mods] original AutoPilotOn requested while waiting for available races\n");
            }
            return;
        }
        if(driving){uint32_t player=MEM32(0x44940c);
            if(!xyz_xbox_address(player,0x24))return;
            guest_call_full(recomp_lookup(0x1eb940),player,0,0x0018ab7fu,0,NULL);driving=0;
        }
        race_requested=1;race_id=MEM32(event+8);
        fprintf(stderr,"[XYZ career mods] available race selected event=%08x id=%08x kind=%u stage=%u\n",
            event,MEM32(event+8),MEM8(event+0xf),MEM32(0x405d28));
        uint32_t args[2]={0x34d94c,0};guest_call_full(recomp_lookup(0x161190),0,0,0x0018ab7fu,2,args);
        phase=5;stable=0;return;
    }
    if(phase==5){
        int paused=quest_cockpit_ui_paused(cockpit_ui_read,NULL);
        if(career.stage==XYZ_CAREER_IDLE && !xyz_xbox_career_begin(&community_career_guest,&career,frame,paused))return;
        if(!xyz_xbox_career_step(&community_career_guest,&career,frame,paused,main_menu,1))return;
        uint32_t event=guest_call_full(recomp_lookup(0x110880),0,0,0x0018ab7fu,1,&race_id);
        uint32_t table=MEM32(0x3fbc30);
        if(MEM32(0x3fbc34)!=224 || event<table || event-table>=224*136 || (event-table)%136 || !xyz_xbox_address(event,136)){
            fprintf(stderr,"[XYZ career mods] STOP: selected race no longer available\n");finished=1;return;
        }
        /* Same career frontend branch as F45B0 at F4799..F47BC, using
         * the already loaded career, controller and original race record. */
        MEM32(0x41c338)=1;
        guest_call_full(recomp_lookup(0x113fa0),0x406c90,0,0x0018ab7fu,0,NULL);
        guest_call_full(recomp_lookup(0x14d3d0),0x3fe230,0,0x0018ab7fu,1,&event);
        guest_call_full(recomp_lookup(0x13f050),0,0,0x0018ab7fu,1,&event);
        guest_call_full(recomp_lookup(0x182230),0x428e10,0,0x0018ab7fu,0,NULL);
        phase=6;stable=0;fprintf(stderr,"[XYZ career mods] original career frontend race launch requested id=%08x\n",race_id);
    }
    if(phase==6 && finish && !driving){
        if(frame->flow!=6 || frame->status!=XYZ_XBOX_OTHER_RACE || !frame->race_type ||
           quest_cockpit_ui_menu(cockpit_ui_read,NULL) || MEM32(0x38dbb0) || MEM32(0x38dbc8) || MEM32(0x38dbe0) ||
           MEM8(0x38dba4) || MEM8(0x38dbbc) || MEM8(0x38dbd4) || MEM8(0x3e3ab0) || MEM32(0x441b48) || MEM32(0x45c4b4)){
            stable=0;return;
        }
        if(!stable)stable=now;if(now-stable<30000000ull)return;
        uint32_t player=MEM32(0x44940c);if(!xyz_xbox_address(player,0x24))return;
        guest_call_full(recomp_lookup(0x1eb840),player,0,0x0018ab7fu,0,NULL);driving=2;
        fprintf(stderr,"[XYZ career mods] original AutoPilotOn requested in career race (no position/result writes)\n");
    }
}
