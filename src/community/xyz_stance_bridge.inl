#include "xyz_stance.h"
#include "xyz_xbox_stance.h"
static XyzStance community_stance_value;
static uint32_t community_stance_car;
static int community_stance_custom;
static XyzStanceDraw *community_stance_drawing;
static int community_stance_probe=-1;
static void community_stance_frame(void){
    if(community_stance_probe<0){const char *p=getenv("NFSU2_STANCE_PROBE"),*script=getenv("RECOMP_PAD_SCRIPT");community_stance_probe=p && *p=='1' && script && *script;}
    uint32_t car=0,hash=0;char name[32]={0};int stock=0;
    if(MEM32(0x428e34)==6){
        uint32_t player=MEM32(0x44940c),types=MEM32(0x45af88);
        if(xyz_xbox_address(player,0x14) && !MEM32(player+0x10))car=MEM32(player+4);
        uint32_t ride=xyz_xbox_address(car,0x9e0)?MEM32(car+0x10):0;
        if(xyz_xbox_address(ride,0x8f0) && MEM32(ride)<46 && xyz_xbox_address(types,46*0x890)){
            uint32_t type=types+MEM32(ride)*0x890;hash=MEM32(type+0xd0);
            memcpy(name,(const void*)XBOX_PTR(type),31);
            for(unsigned i=0;i<31 && name[i];i++)if((uint8_t)name[i]<32 || (uint8_t)name[i]>126){hash=0;break;}
            stock=35*((MEM8(ride+0x363)!=0)+(MEM8(ride+0x364)!=0));
        }
    }
    community_stance_car=hash?car:0;
    community_stance_custom=xyz_stance_publish(hash,name,stock,&community_stance_value);
    if(hash && community_stance_probe){community_stance_value=(XyzStance){100,150,80,-40,130};community_stance_custom=1;}
}
static void community_stance_begin(XyzStanceDraw *saved){
    memset(saved,0,sizeof *saved);
    if((!xyz_stance_enabled() && community_stance_probe!=1) || MEM32(0x428e34)!=6 || !xyz_xbox_address(ecx,0x2d48) || !xyz_xbox_address(esp,0x34))return;
    uint32_t car=MEM32(ecx);XyzStance value;
    if(car && car==community_stance_car && community_stance_custom)value=community_stance_value;
    else if(!car || !xyz_online_stance(car,&value))return;
    XyzXboxGuest memory={NULL,cockpit_ui_read,community_guest_write,community_guest_call};
    if(xyz_xbox_stance_begin(&memory,ecx,MEM32(esp+0x18),&value,saved)){
        static unsigned frames;
        if(!(frames++%600))fprintf(stderr,"[VR stance] draw car=%08x camber=%d/%d offset=%d/%d width=%d; render-only\n",car,value.fc,value.rc,value.fo,value.ro,value.tw);
    }
}
