/* Xbox USA audited counterparts of lightkey.c: +93D gates the lamps,
 * 204430 names the render-info, A75D0 draws an individual flare. A2CD0
 * uploads the road projector, 2E80A0/constant 122 its RGB strength.
 * Settings control our lamps; the existing protocol carries other players'. */
static uint32_t community_lights_car;
static int community_lights_mode,community_lights_dark,community_lights_probe=-1;
static float community_lights_flare_gain=1,community_lights_road_gain=1;
static unsigned community_lights_road_count,community_lights_flare_count;
static void community_lights_frame(void)
{
    if(community_lights_probe<0){const char *p=getenv("NFSU2_LIGHTS_PROBE"),*script=getenv("RECOMP_PAD_SCRIPT");community_lights_probe=p && *p=='1' && script && *script;}
    uint32_t player=MEM32(0x44940c),world=MEM32(0x4493e0);
    uint32_t car=MEM32(0x428e34)==6 && xyz_xbox_address(player,0x14) && !MEM32(player+0x10)?MEM32(player+4):0;
    if(!car || xyz_xbox_find_car(cockpit_ui_read,NULL,world,car,0)!=car){community_lights_car=0;community_lights_dark=0;return;}
    if(community_lights_car!=car){community_lights_car=car;community_lights_dark=0;}
    int mode=quest_xr_headlights();
    if(community_lights_probe){
        static uint64_t start;static int before=-1;
        struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);uint64_t now=(uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
        if(!start)start=now;mode=(int)((now-start)/5000%3);
        if(mode!=before){before=mode;fprintf(stderr,"[VR lights probe] mode=%d road=%u flares=%u\n",mode,community_lights_road_count,community_lights_flare_count);}
    }
    community_lights_mode=mode;
    if(mode==1){MEM8(car+0x93d)=0;community_lights_dark=1;}
    else if(mode==2 || community_lights_dark){MEM8(car+0x93d)=1;community_lights_dark=0;}
}
static int community_lights_state(uint32_t car)
{
    if(!car)return 0;
    if(car==community_lights_car)return community_lights_mode;
    return xyz_online_lights(car);
}
