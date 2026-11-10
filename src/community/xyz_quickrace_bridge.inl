/* Xbox Extra Options: original option controllers and their notifications.
 * Only offline Quick Race gets the larger ranges. No career/LAN fields or
 * asset files are changed. Audited Xbox functions: CBD40, CC2B0, CC570;
 * getters 113400/113420/113610 establish the settings layout below. */
static uint32_t community_quickrace_track(void)
{
    if(MEM32(0x41c338)!=2 || MEM32(0x428e34)!=3 || MEM32(0x456290))return 0;
    uint32_t mode=MEM32(0x406d74);
    if(mode>12)return 0;
    return 0x406d74+(mode==12?0x11c:0x18+20*mode);
}
static int community_quickrace_object(uint32_t self)
{
    if(!xyz_xbox_address(self,0x34))return 0;
    uint32_t v=MEM32(self);
    return xyz_xbox_address(v,0x44) && recomp_lookup(MEM32(v+0xc)) &&
        recomp_lookup(MEM32(v+0x1c)) && recomp_lookup(MEM32(v+0x40));
}
static void community_quickrace_notify(uint32_t self,uint32_t message)
{
    guest_call_full(recomp_lookup(MEM32(MEM32(self)+0x40)),self,0,0xcc307,1,&message);
    guest_call_full(recomp_lookup(MEM32(MEM32(self)+0xc)),self,0,0xcc30e,0,NULL);
}
static int community_quickrace_action(unsigned kind)
{
    uint32_t track=community_quickrace_track(),self=ecx;
    if(!track || !xyz_xbox_address(esp,12) || !community_quickrace_object(self))return 0;
    uint32_t message=MEM32(esp+8),package=MEM32(esp+4);
    int left=message==0x9120409e,right=message==0xb5971bf1;
    uint32_t mode=MEM32(0x406d74),humans_at=0x406d74+(mode==12?0x100:0x10+20*mode);
    unsigned humans=MEM8(humans_at),top=vr_mod_qr_top(mode,MEM16(track),humans);
    if(humans<1 || humans>4 || !top)return 0;
    if(kind==0){
        if(left || right)MEM16(track+2)=(uint16_t)vr_mod_qr_step(MEM16(track+2),1,127,right);
        community_quickrace_notify(self,message);
    }else if(kind==1){
        if(left || right){
            MEM8(humans_at+1)=(uint8_t)vr_mod_qr_step(MEM8(humans_at+1),MEM8(track+8)?1:0,top,right);
            community_quickrace_notify(self,message);
        }
    }else if(kind==2){
        if(left || right){
            MEM8(track+8)=!MEM8(track+8);
            if(MEM8(track+8)){MEM16(track+2)=5;MEM8(humans_at+1)=(uint8_t)top;}
        }
        uint32_t args[2]={0x92b703b5,package};
        guest_call_full(recomp_lookup(0x166470),0,0,0xcc5e6,2,args);
        community_quickrace_notify(self,message);
    }else return 0;
    esp+=12;return 1; /* original controllers: ret 8 */
}
static void community_quickrace_unfreeze(uint32_t self)
{
    uint32_t track=community_quickrace_track();
    if(track && MEM8(track+8) && community_quickrace_object(self) && !MEM8(self+0x28))
        guest_call_full(recomp_lookup(MEM32(MEM32(self)+0x1c)),self,0,0xcc380,0,NULL);
}
