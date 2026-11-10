#include "xyz_xbox_pool.h"
#include <string.h>
static int words(XyzXboxRead read,void *ctx,uint32_t a,uint32_t *out,size_t n) {
    return read && xyz_xbox_address(a,n*4) && read(ctx,a,out,n*4);
}
static int free_list(XyzXboxRead read,void *ctx,uint32_t pool,uint32_t *total,uint32_t *largest,
                     uint32_t extra,uint32_t size,int *contained) {
    uint32_t head=pool+4,links[2],prev=head,previous_end=0;
    *total=*largest=0;*contained=0;
    if(!words(read,ctx,head,links,2))return 0;
    uint32_t at=links[0],tail=links[1];
    for(unsigned n=0;n<65536;++n) {
        if(at==head)return prev==tail;
        uint32_t b[4];
        if(!words(read,ctx,at,b,4) || b[1]!=prev || b[2]<16 || b[3]!=0x44443333u ||
           !xyz_xbox_address(at,b[2]) || (prev!=head && at<previous_end) || UINT32_MAX-*total<b[2])return 0;
        *total+=b[2];if(b[2]>*largest)*largest=b[2];
        if(extra && at==extra && size==b[2])*contained=1;
        previous_end=at+b[2];prev=at;at=b[0];
    }
    return 0;
}
int xyz_xbox_heap_read(XyzXboxRead read,void *ctx,unsigned slot,XyzXboxPool *out) {
    if(!out)return 0;memset(out,0,sizeof *out);
    if(slot!=0 && slot!=4)return 0;
    XyzXboxPool p={0};uint32_t values[4];
    int contained;
    if(!words(read,ctx,0x3e3fdc+slot*4,&p.pointer,1) || !words(read,ctx,p.pointer+0x1c,values,4))return 0;
    p.size=values[0];p.used=values[1];
    if(p.used>p.size || !free_list(read,ctx,p.pointer,&p.free,&p.largest,0,0,&contained) ||
       p.free>p.size || p.used!=p.size-p.free)return 0;
    *out=p;return 1;
}
int xyz_xbox_pool_read(XyzXboxRead read,void *ctx,XyzXboxPool *out) {
    if(!out)return 0;memset(out,0,sizeof *out);
    XyzXboxPool p={0};uint32_t rides;
    if(!xyz_xbox_heap_read(read,ctx,4,&p) || !words(read,ctx,0x45afbc,&rides,1) ||
       !words(read,ctx,rides+0x18,&p.rides,1) || !words(read,ctx,rides+0x2c,&p.capacity,1) ||
       !p.capacity || p.capacity>256 || p.rides>p.capacity ||
       !words(read,ctx,0x45c50c,&p.extra,1) || !words(read,ctx,0x45c510,&p.extra_size,1))return 0;
    if((p.extra==0)!=(p.extra_size==0) || (p.extra && !xyz_xbox_address(p.extra,p.extra_size)))return 0;
    *out=p;return 1;
}
int xyz_xbox_pool_extra_free(XyzXboxRead read,void *ctx,const XyzXboxPool *pool) {
    uint32_t total,largest;int contained;
    if(!pool)return 0;if(!pool->extra)return 1;
    return free_list(read,ctx,pool->pointer,&total,&largest,pool->extra,pool->extra_size,&contained) && contained;
}
