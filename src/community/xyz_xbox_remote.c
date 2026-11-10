#include "xyz_xbox_remote.h"
#include <string.h>

/* Ride reconstruction follows the MIT upstream roam_game.c build_ride
 * algorithm, using independently reviewed US Xbox calls. License and pinned
 * source: vendor/roam/LICENSE and README.md. */
enum { TYPES=0x45af88, OWNERS=0x45af74, PART_DB=0x45be28,
       TYPE_COUNT=46, TYPE_BYTES=0x890, RIDE_BYTES=0x8f0 };
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int read_bytes(const XyzXboxGuest *g,uint32_t a,void *out,size_t n) {
    return g->read && xyz_xbox_address(a,n) && g->read(g->context,a,out,n);
}
static int read32(const XyzXboxGuest *g,uint32_t a,uint32_t *out) {
    uint8_t b[4]; if(!read_bytes(g,a,b,4))return 0; *out=u32(b);return 1;
}
int xyz_xbox_owned(XyzXboxArena arena,uint32_t a,size_t n) {
    return arena.size && xyz_xbox_address(arena.base,arena.size) &&
        a>=arena.base && n<=arena.size && (uint64_t)a+n<=(uint64_t)arena.base+arena.size;
}
static int write_bytes(const XyzXboxGuest *g,XyzXboxArena arena,uint32_t a,const void *data,size_t n) {
    return g->write && xyz_xbox_owned(arena,a,n) && g->write(g->context,a,data,n);
}
static int write32(const XyzXboxGuest *g,XyzXboxArena arena,uint32_t a,uint32_t v) {
    uint8_t b[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)};
    return write_bytes(g,arena,a,b,4);
}
static int call(const XyzXboxGuest *g,enum XyzXboxOp op,uint32_t self,unsigned n,const uint32_t *a,uint32_t *out) {
    uint32_t ignored=0;return g->call && g->call(g->context,op,self,0,n,a,out?out:&ignored);
}
static int call1(const XyzXboxGuest *g,enum XyzXboxOp op,uint32_t self,uint32_t a) {
    return call(g,op,self,1,&a,0);
}
static int prefix_match(const uint8_t *a,const char *b) {
    for(int i=0;i<4;i++) {
        unsigned x=a[i],y=(unsigned char)b[i];
        if(!x || !y) return 0;
        if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;
        if(x!=y)return 0;
    }
    return 1;
}
static int find_part(const XyzXboxGuest *g,uint32_t owners,uint32_t type,uint32_t slot,
                     uint32_t key,uint32_t owner,uint32_t *found) {
    uint32_t previous=0; *found=0;
    for(int i=0;i<6;i++) {
        uint32_t args[5]={type,slot,key,previous,0xffffffffu},part;
        if(!call(g,XYZ_G_GET_PART,PART_DB,5,args,&part))return 0;
        if(!part)return 1;
        uint8_t b[14];uint32_t actual;
        if(part==previous || !read_bytes(g,part,b,sizeof b) || !read32(g,owners+4u*b[7],&actual))return 0;
        if(u32(b)==key && actual==owner){*found=part;return 1;}
        previous=part;
    }
    return 1;
}
int xyz_xbox_build_ride(const XyzXboxGuest *g,XyzXboxArena arena,
                        uint32_t ride,uint32_t scratch,uint32_t skin,
                        const uint8_t *wire,int size,XyzXboxRideResult *result) {
    if(!result)return 0;memset(result,0,sizeof *result);
    if(!g || !g->read || !g->write || !g->call || !wire || !skin || skin>255 ||
       !xyz_xbox_owned(arena,ride,RIDE_BYTES) || !xyz_xbox_owned(arena,scratch,16) ||
       ((uint64_t)scratch<(uint64_t)ride+RIDE_BYTES && (uint64_t)ride<(uint64_t)scratch+16))return 0;
    rc_carcfg c;
    /* The whole record is validated before the first write or guest call. */
    if(rc_carcfg_decode(wire,size,&c)!=RC_OK)return 0;
    uint32_t types,owners,type_address,type=UINT32_MAX,available=0;
    if(!read32(g,TYPES,&types) || !read32(g,OWNERS,&owners) ||
       !xyz_xbox_address(types,TYPE_COUNT*TYPE_BYTES) || !xyz_xbox_address(owners,256*4) ||
       !call(g,XYZ_G_TYPE_HASH,0,1,&c.type_hash,&type_address))return 0;
    if(type_address>=types && (uint64_t)type_address<(uint64_t)types+TYPE_COUNT*TYPE_BYTES &&
       (type_address-types)%TYPE_BYTES==0) type=(type_address-types)/TYPE_BYTES;
    else {
        result->fallback_type=1;
        for(uint32_t i=0;i<TYPE_COUNT;i++) {
            uint8_t name[4];if(!read_bytes(g,types+i*TYPE_BYTES,name,sizeof name))return 0;
            if(prefix_match(name,c.type_name)){
                if(!call(g,XYZ_G_TYPE_AVAILABLE,0,1,&i,&available))return 0;
                if(available){type=i;break;}
            }
        }
    }
    if(type!=UINT32_MAX && !call(g,XYZ_G_TYPE_AVAILABLE,0,1,&type,&available))return 0;
    if(type==UINT32_MAX || !available){
        /* US tables still contain EU-only 106/Corsa records without their
         * files. A valid hash therefore does not imply a loadable model.
         * Never submit a missing car to the shared asynchronous loader. */
        result->fallback_type=1;type=UINT32_MAX;
        static const char *fallbacks[]={"PEUGOT","COROLLA","FOCUS"};
        for(unsigned f=0;f<sizeof fallbacks/sizeof fallbacks[0] && type==UINT32_MAX;f++)
            for(uint32_t i=0;i<TYPE_COUNT;i++){
                char name[32];if(!read_bytes(g,types+i*TYPE_BYTES,name,sizeof name))return 0;
                if(!memchr(name,0,sizeof name) || strcmp(name,fallbacks[f]))continue;
                if(!call(g,XYZ_G_TYPE_AVAILABLE,0,1,&i,&available))return 0;
                if(available){type=i;break;}
            }
        if(type==UINT32_MAX)return 0;
    }
    result->type=type;
    uint8_t zero[RIDE_BYTES]={0};
    if(!write_bytes(g,arena,ride,zero,sizeof zero))return 0;
    uint32_t args[4]={type,0,c.ride_b[1],c.ride_b[2]};
    if(!call(g,XYZ_G_RIDE_INIT,ride,4,args,0))return 0;
    uint8_t perf[RC_PERF_BYTES];
    if(c.has_perf)memcpy(perf,c.perf,sizeof perf);
    else {memset(perf,0,11);memset(perf+11,100,41);}
    for(int i=0;i<10;i++)
        if(!write32(g,arena,ride+0x318+4u*i,(perf[i>>3]>>(7-(i&7)))&1u))return 0;
    uint8_t packed[16]={0};memcpy(packed+5,perf+2,9);
    if(!write_bytes(g,arena,scratch,packed,sizeof packed) ||
       !call1(g,XYZ_G_PERF_SET,ride+0x2f0,scratch) ||
       !write_bytes(g,arena,ride+0x398,perf+11,41) ||
       !call1(g,XYZ_G_RIDE_STOCK,ride,0))return 0;
    if(!result->fallback_type && c.n_parts) {
        uint32_t wanted[RC_PART_SLOTS]={0};uint8_t named[RC_PART_SLOTS]={0};
        for(unsigned i=0;i<c.n_parts;i++) {
            unsigned slot=c.part[i].slot;named[slot]=1;
            if(!find_part(g,owners,type,slot,c.part[i].key,c.part[i].owner,&wanted[slot]))return 0;
            if(!wanted[slot])result->missing_parts++;
        }
        for(unsigned slot=0;slot<RC_PART_SLOTS;slot++)
            if((wanted[slot] || !named[slot]) &&
               !write32(g,arena,ride+0x590+4u*slot,wanted[slot]))return 0;
        if(!write_bytes(g,arena,ride+4,c.ride_b,4) ||
           !call(g,XYZ_G_RIDE_PARTS,ride,0,0,0))return 0;
    }
    return call1(g,XYZ_G_RIDE_SKINS,ride,skin);
}
