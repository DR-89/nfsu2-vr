#pragma once
#include <stdint.h>

/* Same unsigned sequence comparison as the retail BlockOnTime entry, including
 * wraparound. Never release a wait merely because the command queue is idle. */
static inline int pcvr_fence_reached(uint32_t issued, uint32_t completed, uint32_t target)
{
    return (uint32_t)(issued-target)>=(uint32_t)(issued-completed);
}
