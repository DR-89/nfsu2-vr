/* Included by recomp_manual.c: audited guest functions, guest-thread only. */
extern uint32_t xbox_ContiguousAlloc(uint32_t size,uint32_t alignment);
static unsigned community_pool_blocks;
static uint32_t community_pool_pointer;
static unsigned community_heap_blocks;
static uint32_t community_heap_pointer;
static int community_pool_enabled(void) {
    static int enabled=-1;
    if(enabled<0){const char *e=getenv("NFSU2_XYZ_POOL_MB");enabled=!e || !*e || !strcmp(e,"128");}
    return enabled;
}
static int community_pool_grow(void) {
    XyzXboxPool before,after;
    if(!community_pool_enabled() || community_pool_blocks>=3 ||
       !xyz_xbox_pool_read(cockpit_ui_read,NULL,&before) ||
       (community_pool_pointer && community_pool_pointer!=before.pointer))return 0;
    const uint32_t size=16u<<20;
    uint32_t mem=xbox_ContiguousAlloc(size,4096);
    if(!mem)return 0;
    uint32_t args[3]={4,mem,size};
    guest_call_full(sub_0004F340,0,0,0x18ab7f,3,args);
    /* The block now belongs to Cars for the process lifetime. Never return
     * backing pages while any texture/model or free-list link can use them. */
    ++community_pool_blocks;community_pool_pointer=before.pointer;
    int ok=xyz_xbox_pool_read(cockpit_ui_read,NULL,&after) && after.pointer==before.pointer &&
        after.size==before.size+size && after.used==before.used;
    fprintf(stderr,"[XYZ car pool] added=%u MB blocks=%u verified=%d\n",size>>20,community_pool_blocks,ok);
    return ok;
}
static int community_heap_prepare(void) {
    XyzXboxPool before,after;
    if(!xyz_xbox_heap_read(cockpit_ui_read,NULL,0,&before) ||
       (community_heap_pointer && community_heap_pointer!=before.pointer))return 0;
    if(before.free>=(2u<<20) && before.largest>=(1u<<20))return 1;
    if(community_heap_blocks>=4)return 0;
    const uint32_t bytes=16u<<20;uint32_t address=xbox_ContiguousAlloc(bytes,4096);
    if(!address)return 0;
    uint32_t args[3]={0,address,bytes};
    guest_call_full(sub_0004F340,0,0,0x18ab7f,3,args);
    // The general heap, like Cars, owns this block for the process lifetime.
    // Never change retail's low RAM size or free an attached backing block.
    ++community_heap_blocks;community_heap_pointer=before.pointer;
    int ok=xyz_xbox_heap_read(cockpit_ui_read,NULL,0,&after) && after.pointer==before.pointer &&
        after.size==before.size+bytes && after.used==before.used;
    fprintf(stderr,"[XYZ general heap] added=%u MB blocks=%u verified=%d\n",bytes>>20,community_heap_blocks,ok);
    return ok;
}
static int community_pool_prepare(void) {
    if(!community_pool_enabled())return 1;
    if(!community_heap_prepare())return 0;
    if(!community_pool_blocks && !community_pool_grow())return 0;
    XyzXboxPool p;
    for(unsigned n=0;n<64;++n) {
        if(!xyz_xbox_pool_read(cockpit_ui_read,NULL,&p) || p.pointer!=community_pool_pointer)return 0;
        if(p.rides+2<p.capacity && p.free>=(4u<<20) && p.largest>=(4u<<20))return 1;
        uint32_t zero=0;
        if(!guest_call_full(sub_002194B0_gen,0x45c4a0,0,0x18ab7f,1,&zero))break;
    }
    if(p.rides+2>=p.capacity)return 0;
    /* No new load is allowed to force reclamation of a player's textures.
     * The retail step hook below can add space if fragmentation changes later. */
    return community_pool_grow();
}
static int community_pool_purge(uint32_t caller) {
    if(!community_pool_blocks)return -1;
    uint32_t zero=0;
    if(guest_call_full(sub_002194B0_gen,0x45c4a0,0,0x18ab7f,1,&zero))return 1;
    // The ride-slot wait needs a free entry, not more byte storage.
    if(caller==0x21a1ec)return 0;
    return community_pool_grow();
}
static int community_pool_release(void) {
    if(!community_pool_blocks)return 1;
    uint32_t head=0x45c4f8,at=MEM32(head),previous=head,entries[256];unsigned count=0;
    // Match retail's unused-entry cleanup, with checked, bounded ownership.
    while(at!=head && count<256) {
        if(!xyz_xbox_address(at,0x1240) || MEM32(at+4)!=previous)return 0;
        entries[count++]=at;previous=at;at=MEM32(at);
    }
    if(at!=head || MEM32(head+4)!=previous)return 0;
    for(unsigned n=0;n<count;++n) {
        uint32_t args[2]={entries[n],1};
        guest_call_full(sub_00219310,0x45c4a0,0,0x18ab7f,2,args);
    }
    XyzXboxPool p;
    if(at!=head || !xyz_xbox_pool_read(cockpit_ui_read,NULL,&p))return 0;
    if(xyz_xbox_pool_extra_free(cockpit_ui_read,NULL,&p))return 1;
    fprintf(stderr,"[XYZ car pool] retained track block=%u KB; references remain\n",p.extra_size>>10);
    return 0;
}
