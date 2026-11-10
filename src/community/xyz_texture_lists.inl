/* Xbox 210E00 is the counterpart of PC 631E20. Both have two lists of
 * 69 names, counts +228/+22C, and the same erroneous full-list increment.
 * Count the non-decal names first, temporarily omit only excess decal
 * pointers, and restore the complete ride after constructing the list. */
static void community_texture_engine(uint32_t list,uint32_t ride){
    uint32_t types=MEM32(0x45af88),type=MEM32(ride);
    if(type>=46 || !xyz_xbox_address(types,46*0x890) || MEM32(types+type*0x890+0x844)==2)return;
    char name[40];memcpy(name,(const void*)XBOX_PTR(types+type*0x890+0x20),31);name[31]=0;
    size_t n=strlen(name);if(!n || n>31)return;memcpy(name+n,"_ENGINE",8);
    uint32_t hash=UINT32_MAX;for(const unsigned char *p=(const unsigned char*)name;*p;p++)hash=hash*33+*p;
    for(unsigned set=0;set<2;set++){
        uint32_t count=MEM32(list+0x228+set*4),at=list+set*0x114;if(count>=69)continue;
        unsigned i=0;for(;i<count;i++)if(MEM32(at+4*i)==hash)break;
        if(i==count){MEM32(at+4*count)=hash;MEM32(list+0x228+set*4)=count+1;}
    }
    static uint8_t noted[46];if(!noted[type]){noted[type]=1;fprintf(stderr,"[damage] streamed engine texture %s\n",name);}
}
static void community_texture_list(void){
    uint32_t list=MEM32(esp+4),ride=MEM32(esp+8),args[4],zones[7],seen[56],off[56],values[56];
    if(!xyz_xbox_address(list,0x2fc) || !xyz_xbox_address(ride,0x8f0)){sub_00210E00_gen();return;}
    int engine=community_damage_ready;unsigned count=0,removed=0;
    for(unsigned z=0;z<7;z++){
        zones[z]=MEM32(ride+0x660+z*4);if(!zones[z])continue;
        for(unsigned i=0;i<8;i++){
            uint32_t part=MEM32(ride+0x714+z*32+i*4);unsigned k=0;
            while(k<count && seen[k]!=part)++k;if(part && k==count)seen[count++]=part;
        }
    }
    if(count){
        for(unsigned i=0;i<4;i++)args[i]=MEM32(esp+4+i*4);
        for(unsigned z=0;z<7;z++)MEM32(ride+0x660+z*4)=0;
        guest_call_full(sub_00210E00_gen,ecx,edx,MEM32(esp),4,args);
        for(unsigned z=0;z<7;z++)MEM32(ride+0x660+z*4)=zones[z];
        uint32_t others=MEM32(list+0x228),second=MEM32(list+0x22c);if(second>others)others=second;
        if(engine && others<UINT32_MAX)++others;
        uint32_t room=others<69?69-others:0;unsigned kept=0;
        if(count>room)for(unsigned z=0;z<7;z++)if(zones[z])for(unsigned i=0;i<8;i++){
            uint32_t at=ride+0x714+z*32+i*4,part=MEM32(at);if(!part)continue;
            unsigned k=0;while(k<kept && seen[k]!=part)++k;if(k<kept)continue;
            if(kept<room)seen[kept++]=part;
            else {off[removed]=at;values[removed++]=part;MEM32(at)=0;}
        }
    }
    sub_00210E00_gen();
    for(unsigned i=0;i<removed;i++)MEM32(off[i])=values[i];
    for(unsigned i=0;i<2;i++)if(MEM32(list+0x228+i*4)>69)MEM32(list+0x228+i*4)=69;
    if(engine)community_texture_engine(list,ride);
    if(removed)fprintf(stderr,"[XYZ texture list] omitted %u excess decals from bounded loader list; ride restored\n",removed);
}
