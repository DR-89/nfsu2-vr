/* Explicit development check of the production mailbox and browser controller.
 * Uses the same main-menu gate as the VR panel. Never creates/joins a race,
 * changes a profile or supplies a saved identity. */
static void community_races_probe(const XyzXboxFrame *frame,int main_menu,uint64_t now)
{
    static int enabled=-1,phase,finished;
    static uint64_t began,last,stable;
    static uint32_t first_epoch;
    static const char *profile,*device;
    if(enabled<0){const char *e=getenv("NFSU2_XYZ_RACES_UI_PROBE");enabled=e && *e=='1';
        profile=getenv("NFSU2_XYZ_PROBE_PROFILE");device=getenv("NFSU2_XYZ_PROBE_DEVICE");}
    if(!enabled || finished)return;
    if(!began)began=now;
    XYZRaceBrowser view;xyz_races_snapshot(&view);
    if(now-began>180000000ull || view.state==XYZ_RACES_ERROR){
        fprintf(stderr,"[XYZ race browser probe] FAIL phase=%d state=%d main=%d flow=%u notice=%s\n",phase,view.state,main_menu,frame->flow,view.notice);
        xyz_races_close();finished=1;return;
    }
    if(!phase){
        if(!main_menu){stable=0;
            if(now-last>=5000000ull){
                last=now;fprintf(stderr,"[XYZ race browser probe] waiting main=%d flow=%u packages=",main_menu,frame->flow);
                uint32_t frontend=MEM32(0x3fbd08),manager=0,node=0;
                if(frontend && cockpit_ui_read(NULL,frontend+8,&manager,4) && manager)cockpit_ui_read(NULL,manager+0xe4,&node,4);
                for(unsigned i=0;node && i<32;++i){uint32_t fields[4];char name[64];
                    if(!cockpit_ui_read(NULL,node,fields,sizeof fields) || !fields[3] || !cockpit_ui_read(NULL,fields[3],name,sizeof name))break;
                    name[63]=0;fprintf(stderr,"%s%s",i?",":"",name);node=fields[1];}
                fprintf(stderr,"\n");
            }return;
        }
        if(!stable)stable=now;if(now-stable<500000ull)return;
        if(!xyz_races_open("VR_BrowserQA","",profile,device)){
            fprintf(stderr,"[XYZ race browser probe] FAIL opening mailbox\n");finished=1;return;
        }
        xyz_races_snapshot(&view);first_epoch=view.epoch;phase=1;
        fprintf(stderr,"[XYZ race browser probe] actual main menu authorized\n");return;
    }
    if(phase<=3 && view.state==XYZ_RACES_READY){
        uint32_t expected=phase==2?20:0;
        if(view.start!=expected || view.count>20){fprintf(stderr,"[XYZ race browser probe] FAIL page offset\n");xyz_races_close();finished=1;return;}
        unsigned named=0,parsed=0,compatible=0;
        for(unsigned i=0;i<view.count;++i){named+=view.info[i].track_name[0]!=0;parsed+=view.info[i].parsed!=0;compatible+=view.info[i].compatible!=0;}
        fprintf(stderr,"[XYZ race browser probe] page=%d start=%u rows=%u maps=%u parsed=%u compatible=%u\n",phase,view.start,view.count,named,parsed,compatible);
        int accepted=phase<3?xyz_races_search(view.epoch,phase==1?20:0):xyz_races_close();
        if(!accepted){fprintf(stderr,"[XYZ race browser probe] FAIL page command\n");xyz_races_close();finished=1;return;}
        ++phase;return;
    }
    if(phase==4 && !view.active && view.state==XYZ_RACES_OFFLINE){
        if(community_engine_lobby.stage!=XYZ_LOBBY_EMPTY || !xyz_races_open("VR_BrowserQA","",profile,device)){
            fprintf(stderr,"[XYZ race browser probe] FAIL reopen\n");finished=1;return;
        }phase=5;return;
    }
    if(phase==5 && view.state==XYZ_RACES_READY){
        if(view.epoch==first_epoch || view.start || !xyz_races_close()){
            fprintf(stderr,"[XYZ race browser probe] FAIL new session\n");xyz_races_close();finished=1;return;
        }phase=6;return;
    }
    if(phase==6 && !view.active && view.state==XYZ_RACES_OFFLINE){
        fprintf(stderr,"[XYZ race browser probe] %s pages=3 reopen=1 main=%d lobby=%d\n",
            community_engine_lobby.stage==XYZ_LOBBY_EMPTY && main_menu?"PASS":"FAIL",main_menu,community_engine_lobby.stage);
        finished=1;
    }
}
