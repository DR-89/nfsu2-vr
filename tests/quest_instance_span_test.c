#include "quest_instance_span.h"
#include <stdio.h>

int main(void)
{
    unsigned failed=0, checked=0;
    const uint64_t stride=3072;
    /* Compare the selected constant set for every instance against the
     * original separate draws, including starting partway into a group. */
    for(uint32_t count=1;count<=16;++count) {
        uint64_t root=256*(count+3), end=root+count*stride;
        for(uint32_t member=0;member<count;++member) {
            uint64_t at=root+member*stride;
            uint32_t span=quest_instance_span(root,count,at,stride,end);
            failed+=span!=count-member;
            for(uint32_t instance=0;instance<span;++instance) {
                failed+=(at+instance*stride)!=(root+(member+instance)*stride);
                ++checked;
            }
            failed+=quest_instance_span(root,count,at+1,stride,end)!=0;
            failed+=quest_instance_span(root,count,at,stride,end-1)!=0;
        }
        failed+=quest_instance_span(root,count,root-1,stride,end)!=0;
        failed+=quest_instance_span(root,count,end,stride,end)!=0;
    }
    failed+=quest_instance_span(UINT64_MAX-8,16,UINT64_MAX-8,stride,UINT64_MAX)!=0;
    failed+=quest_instance_span(0,1,0,0,100)!=0;
    failed+=quest_instance_span(0,0,0,stride,100)!=0;
    printf("Quest shared instance constants: %u addresses checked, %u failures\n",checked,failed);
    return failed?1:0;
}
