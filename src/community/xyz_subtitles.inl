#include "../vr_subtitles.h"
static uint32_t community_subtitle_allocation;
static unsigned community_subtitle_lines;
/* 11ED10 relocates the original language table. Only the English subtitle
 * entries get separate owned storage; the original file and other text stay
 * intact. 1106E0 unloads that table before its chunk is freed. */
static void community_subtitle_loaded(void)
{
    if(!eax || community_subtitle_allocation)return;
    size_t bytes=0;unsigned count=0;const uint8_t *pack=vr_subtitles_pack(&bytes,&count);
    if(!pack || !count)return;
    uint32_t table=MEM32(0x3fbc28),n=MEM32(0x3fbc20);
    if(!n || n>65536 || !xyz_xbox_address(table,(size_t)n*8))return;
    uint32_t english=guest_call_full(recomp_lookup(0x1105c0),0,0x45a6a9ce,0x0018ab7fu,0,NULL);
    const char prompt[]="Please press START to begin";
    if(!xyz_xbox_address(english,sizeof prompt) || memcmp((const void*)XBOX_PTR(english),prompt,sizeof prompt))return;
    uint32_t allocation=0,length=(uint32_t)bytes;
    if(!community_guest_call(NULL,XYZ_G_ALLOC,0,0,1,&length,&allocation) || !xyz_xbox_address(allocation,bytes))return;
    memcpy((void*)XBOX_PTR(allocation),pack,bytes);
    for(unsigned k=0;k<count;k++){
        uint32_t hash,offset;memcpy(&hash,pack+12+8*k,4);memcpy(&offset,pack+16+8*k,4);
        unsigned lo=0,hi=n;
        while(lo<hi){unsigned mid=lo+(hi-lo)/2;if(MEM32(table+8*mid)<hash)lo=mid+1;else hi=mid;}
        if(lo<n && MEM32(table+8*lo)==hash){MEM32(table+8*lo+4)=allocation+offset;++community_subtitle_lines;}
    }
    community_subtitle_allocation=allocation;
    fprintf(stderr,"[VR subtitles] %u English movie texts installed; original player timing/layout\n",community_subtitle_lines);
}
static void community_subtitle_unloaded(void)
{
    if(eax && community_subtitle_allocation){
        uint32_t result=0,allocation=community_subtitle_allocation;community_subtitle_allocation=0;community_subtitle_lines=0;
        community_guest_call(NULL,XYZ_G_FREE,0,0,1,&allocation,&result);
    }
}
