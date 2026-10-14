#include "pcvr_fence.h"
#include <stdio.h>
int main(void)
{
    const uint32_t cases[][4]={
        {0x16a95,0x16a93,0x16a85,1}, /* captured Windows hang */
        {0x16a95,0x16a93,0x16a94,0},
        {0x16a95,0x16a93,0x16a93,1},
        {0x16a95,0x16a93,0x16a95,0},
        {10,10,10,1},{10,9,10,0},
        {3,1,0xffffffffu,1},{3,0xffffffffu,1,0},
        {0,0xffffffffu,0,0},{0,0,0xffffffffu,1}};
    int failures=0;
    for(unsigned i=0;i<sizeof cases/sizeof *cases;++i)
        if(pcvr_fence_reached(cases[i][0],cases[i][1],cases[i][2])!=(int)cases[i][3]) ++failures;
    printf("PCVR completed-fence checks: %d failures\n",failures);
    return failures!=0;
}
