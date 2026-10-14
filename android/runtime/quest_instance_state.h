#pragma once
#include <stdint.h>
#include <string.h>

/* Exact render-state comparison against the held draw's immutable snapshot.
 * Ignore only the same non-render-state methods as the original instance
 * matcher: NOP, upload window, BEGIN_END/draw data, semaphore and load slots.
 * No dirty bits or remembered state from the opposite eye are involved. */
static inline int quest_instance_regs_equal(const uint32_t *a, const uint32_t *b)
{
    static const uint16_t ranges[][2]={
        {0x0000,0x0100}, {0x0104,0x0b00}, {0x0c00,0x17fc},
        {0x1820,0x1d6c}, {0x1d74,0x1e9c}, {0x1ea0,0x1ea4}, {0x1ea8,0x2000}
    };
    for(unsigned i=0;i<sizeof ranges/sizeof ranges[0];++i)
        if(memcmp(a+ranges[i][0]/4,b+ranges[i][0]/4,ranges[i][1]-ranges[i][0])) return 0;
    return 1;
}
