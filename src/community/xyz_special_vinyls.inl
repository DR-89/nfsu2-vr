/* Extra Options' hidden vinyl category, using the Xbox shop's own unlock
 * and price filters. Audited: SetupVinyls 152500, AddCategory 151DF0;
 * Sponsor returns at 15272C, before the original selection is restored.
 * Category/texture availability facts follow MIT nfsu2-web vinylcat.c.
 */
static uint32_t community_special_label;
#define COMMUNITY_SPECIAL_HASH 0x4183832du
static int community_special_drop(const char *name,const char *car)
{
    static const struct {const char *name,*cars;} only[]={
        {"STREET REAPER"," 3000GT CELICA ESCALADE G35 GTO MUSTANGGT RX8 TT "},
        {"JAPAN TUNING"," G35 "},{"DAVID CHOE"," 350Z COROLLA "},
        {"SNOOP DOGG"," MUSTANGGT "},{"SNOOP DOGG2"," ESCALADE "},{"THE DOORS"," GTO "}
    };
    if(!strcmp(name,"DEBUG VINYL"))return 1;
    char pattern[40];snprintf(pattern,sizeof pattern," %s ",car);
    for(unsigned i=0;i<sizeof only/sizeof *only;i++)
        if(!strcmp(name,only[i].name))return strstr(only[i].cars,pattern)==NULL;
    return 0;
}
static int community_special_string(uint32_t at,char *out,unsigned capacity)
{
    if(!xyz_xbox_address(at,capacity))return 0;
    const char *p=(const char*)XBOX_PTR(at);unsigned n=0;
    for(;n+1<capacity && p[n];n++){if((unsigned char)p[n]<32 || (unsigned char)p[n]>126)return 0;out[n]=p[n];}
    out[n]=0;return n && !p[n];
}
static void community_special_category(uint32_t screen)
{
    if(!quest_xr_special_vinyls())return;
    uint32_t front=MEM32(0x3fc1cc),types=MEM32(0x45af88);
    if(!xyz_xbox_address(front,4) || !xyz_xbox_address(MEM32(front),8) || !xyz_xbox_address(screen,0x1e0))return;
    recomp_func_t typefn=recomp_lookup(MEM32(MEM32(front)+4));if(!typefn)return;
    uint32_t car=guest_call_full(typefn,front,0,0x151e89,0,NULL);
    char name[32];if(car>=46 || !xyz_xbox_address(types,46*0x890) || !community_special_string(types+car*0x890,name,sizeof name))return;
    uint32_t slot=0x40,kind=guest_call_full(recomp_lookup(0x1ff290),0,0,0x151e78,1,&slot);
    uint32_t a[5]={car,0x40,0,0xffffffffu,0};
    uint32_t part=guest_call_full(recomp_lookup(0x206da0),0x45be28,0,0x151e9b,4,a);
    uint32_t omit[32];uint8_t original[32];unsigned count=0,drops=0,guard=0;
    for(;part && guard++<4096;){
        if(!xyz_xbox_address(part,14))return;
        if((int32_t)(int8_t)MEM8(part+4)==(int32_t)kind && (MEM8(part+5)&31)==28){
            char caption[32];uint32_t text=guest_call_full(recomp_lookup(0x1ff430),part,0,0x151e9b,0,NULL);
            if(!community_special_string(text,caption,sizeof caption))return;
            if(community_special_drop(caption,name)){if(drops==32)return;omit[drops++]=part;}
            else {if(count==32)return;++count;}
        }
        uint32_t b[5]={part,car,0x40,0,0xffffffffu};
        uint32_t next=guest_call_full(recomp_lookup(0x206dc0),0x45be28,0,0x151f57,5,b);
        if(next==part)return;part=next;
    }
    if(part || !count)return;
    if(!community_special_label){
        uint32_t size=8,label=0;
        if(!community_guest_call(NULL,XYZ_G_ALLOC,0,0,1,&size,&label) || !xyz_xbox_address(label,size))return;
        memcpy((void*)XBOX_PTR(label),"Special",8);community_special_label=label;
    }
    for(unsigned i=0;i<drops;i++){original[i]=MEM8(omit[i]+5);MEM8(omit[i]+5)=(original[i]&0xe0)|31;}
    uint32_t category[3]={28,0x93ec1ce9,COMMUNITY_SPECIAL_HASH};
    guest_call_full(sub_00151DF0_gen,screen,0,0x15272c,3,category);
    for(unsigned i=0;i<drops;i++)MEM8(omit[i]+5)=original[i];
    fprintf(stderr,"[VR special vinyls] car=%s available=%u excluded=%u; original unlock/price filters\n",name,count,drops);
}
