#pragma once
#include <stdint.h>
#include <string.h>

/* Update CPU shadow registers for bounded data-only packets. Dirty bytes
 * latch changes per 16 words. No GPU method or synchronization is executed.
 * Caller guarantees the input does not alias the register array. */
static inline void pcvr_bulk_shadow(uint32_t *registers,uint8_t *dirty,
    uint32_t first,int noninc,const uint32_t *data,uint32_t count)
{
    if(!count) return;
    if(noninc) {
        /* The final value alone is insufficient: intermediate writes can
         * change a clean register and then restore its original value. */
        uint8_t *changed=&dirty[first>>4];
        if(*changed!=1) {
            uint32_t original=registers[first];
            for(uint32_t i=0;i<count;++i) if(data[i]!=original) { *changed=1; break; }
        }
        registers[first]=data[count-1];
        return;
    }
    while(count) {
        uint32_t words=16-(first&15);
        if(words>count) words=count;
        uint8_t *changed=&dirty[first>>4];
        if(*changed!=1 && memcmp(registers+first,data,words*sizeof *data)) *changed=1;
        memcpy(registers+first,data,words*sizeof *data);
        first+=words; data+=words; count-=words;
    }
}
