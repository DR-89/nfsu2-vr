#pragma once
#include <stdint.h>

/* Number of complete, contiguous immutable constant sets after this member.
 * The caller separately validates the buffer and its command generation. */
static inline uint32_t quest_instance_span(uint64_t root, uint32_t count,
    uint64_t member, uint64_t stride, uint64_t limit)
{
    if (!count || !stride || member < root || root > limit ||
        count > (limit-root)/stride || (member-root)%stride) return 0;
    uint64_t index=(member-root)/stride;
    return index<count ? count-(uint32_t)index : 0;
}
