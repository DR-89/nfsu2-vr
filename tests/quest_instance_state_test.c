#include "quest_instance_state.h"
#include <stdio.h>

static uint32_t rng=0x45381u;
static uint32_t next(void) { rng=rng*1664525u+1013904223u; return rng; }
static void canonicalize(uint32_t *r)
{
    const unsigned methods[]={0x100,0x17fc,0x1d6c,0x1d70,0x1e9c,0x1ea4};
    for(unsigned i=0;i<sizeof methods/sizeof methods[0];++i) r[methods[i]/4]=0;
    for(unsigned i=0xb00/4;i<0xc00/4;++i) r[i]=0;
    for(unsigned i=0x1800/4;i<0x1820/4;++i) r[i]=0;
}
int main(void)
{
    uint32_t a[2048],b[2048],ca[2048],cb[2048];
    unsigned failed=0;
    /* Mutate every register separately, then mixed random mutations. Compare
     * the fast ranges to independently canonicalized complete register files.
     * Repeated calls may not mutate either source snapshot. */
    for(unsigned test=0;test<12048;++test) {
        for(unsigned i=0;i<2048;++i) a[i]=b[i]=next();
        if(test<2048) b[test]^=0x80000001u;
        else for(unsigned i=0;i<1+test%31;++i) b[next()%2048]^=next()|1;
        memcpy(ca,a,sizeof a); memcpy(cb,b,sizeof b);
        canonicalize(ca); canonicalize(cb);
        int expected=memcmp(ca,cb,sizeof ca)==0;
        failed+=quest_instance_regs_equal(a,b)!=expected;
        failed+=quest_instance_regs_equal(b,a)!=expected;
        failed+=!quest_instance_regs_equal(a,a);
    }
    printf("Quest instance state: 12048 register mutations, %u failures\n",failed);
    return failed?1:0;
}
