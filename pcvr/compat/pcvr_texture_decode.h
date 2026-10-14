#ifndef PCVR_TEXTURE_DECODE_H
#define PCVR_TEXTURE_DECODE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../../xboxrecomp/src/d3d/d3d8_swizzle.h"

/* Only the three formats with dedicated Vulkan decoders are supported.
 * Keep unsupported dimensions/formats on their existing path. */
static inline int pcvr_texture_decode_supported(uint32_t w,uint32_t h,
                                                uint32_t color,uint32_t entries)
{
    return w && h && w<=4096 && h<=4096 && !(w&(w-1)) && !(h&(h-1)) &&
        (color==0x06 || color==0x07 ||
         (color==0x0b && (entries==32 || entries==64 || entries==128 || entries==256)));
}

static inline int pcvr_texture_decode(uint32_t *dst,const uint8_t *src,
    uint32_t w,uint32_t h,uint32_t color,const uint8_t *palette,uint32_t entries)
{
    if(!pcvr_texture_decode_supported(w,h,color,entries) ||
       !dst || !src || (color==0x0b && !palette)) return 0;
    uint32_t mx,my,oy=0;
    xbox_swizzle_masks(w,h,&mx,&my);
    for(uint32_t y=0;y<h;++y) {
        uint32_t ox=0;
        uint32_t *row=dst+(size_t)y*w;
        if(color==0x0b) {
            for(uint32_t x=0;x<w;++x) {
                uint32_t i=src[oy|ox];
                if(i>=entries) i=0;
                memcpy(row+x,palette+4*i,4);
                ox=(ox-mx)&mx;
            }
        } else {
            uint32_t fill=color==0x07?0xff000000u:0;
            for(uint32_t x=0;x<w;++x) {
                uint32_t c;
                memcpy(&c,src+4*(size_t)(oy|ox),4);
                row[x]=c|fill;
                ox=(ox-mx)&mx;
            }
        }
        oy=(oy-my)&my;
    }
    return 1;
}
#endif
