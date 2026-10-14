#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const uint8_t *held, *current;
    size_t bytes;
} QuestInstanceBytes;

/* Interleaved attributes often cover the same byte ranges. A successful
 * larger comparison proves a contained range only when both relative byte
 * displacements agree. This is exact equality, not a hash or pointer test. */
static inline int quest_instance_bytes_equal(const QuestInstanceBytes *spans, unsigned count)
{
    for(unsigned i=0;i<count;++i) {
        unsigned j;
        for(j=0;j<i;++j) {
            uintptr_t a=(uintptr_t)spans[i].held, b=(uintptr_t)spans[i].current;
            uintptr_t pa=(uintptr_t)spans[j].held, pb=(uintptr_t)spans[j].current;
            if(a>=pa && b>=pb && a-pa==b-pb && a-pa<=spans[j].bytes &&
               spans[i].bytes<=spans[j].bytes-(a-pa)) break;
        }
        if(j==i && spans[i].bytes && memcmp(spans[i].held,spans[i].current,spans[i].bytes)) return 0;
    }
    return 1;
}
