#include "quest_instance_bytes.h"
#include <stdio.h>

static uint32_t rng=0x6179823u;
static uint32_t next(void) { rng=rng*1664525u+1013904223u; return rng; }
int main(void)
{
    uint8_t a[4096],b[4096];
    unsigned failed=0;
    for(unsigned test=0;test<10000;++test) {
        QuestInstanceBytes spans[16];
        for(unsigned i=0;i<4096;++i) a[i]=b[i]=(uint8_t)next();
        for(unsigned i=0;i<16;++i) {
            unsigned offset=test%3 ? i*4 : next()%2048;
            unsigned size=test%3 ? 1024-i*4 : next()%2048;
            spans[i]=(QuestInstanceBytes){a+offset,b+offset,size};
            if(test%7==0 && i%3==0) spans[i].current+=4; /* unequal mappings */
        }
        if(test%2) b[next()%4096]^=0x81;
        int expected=1;
        for(unsigned i=0;i<16;++i)
            if(memcmp(spans[i].held,spans[i].current,spans[i].bytes)) expected=0;
        failed+=quest_instance_bytes_equal(spans,16)!=expected;
        /* Permutation cannot change equality, even with overlaps/nesting. */
        for(unsigned i=0;i<8;++i) {
            QuestInstanceBytes swap=spans[i]; spans[i]=spans[15-i]; spans[15-i]=swap;
        }
        failed+=quest_instance_bytes_equal(spans,16)!=expected;
    }
    printf("Quest instance bytes: 10000 mixed layouts, %u failures\n",failed);
    return failed?1:0;
}
