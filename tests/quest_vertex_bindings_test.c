#include "quest_vertex_bindings.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint32_t seed=0x37ed10u;
static uint32_t random_u32(void) { seed=seed*1664525u+1013904223u; return seed; }
int main(void)
{
    unsigned failed=0, reduced=0;
    /* Different buffers, reversed/large offsets, stride-zero constants,
     * vertex/instance rates, and mixed divisors. Compare actual fetch
     * addresses before/after, not just the expected number of groups. */
    for(unsigned test=0;test<10000;++test) {
        VkVertexInputBindingDescription2EXT b[16]={0}, original[16];
        VkVertexInputAttributeDescription2EXT a[16]={0};
        VkDeviceSize offsets[16], old_offsets[16];
        VkBuffer buffers[16], old_buffers[16];
        uint32_t limit=test%2?2047:15;
        for(unsigned i=0;i<16;++i) {
            b[i].sType=VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT;
            b[i].binding=i; b[i].stride=(random_u32()%5)*16;
            b[i].inputRate=(VkVertexInputRate)(random_u32()%2);
            b[i].divisor=random_u32()%3+1;
            a[i].sType=VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT;
            a[i].location=i; a[i].binding=i; a[i].format=VK_FORMAT_R32G32B32A32_SFLOAT;
            offsets[i]=(random_u32()%257)*16;
            buffers[i]=(VkBuffer)(uintptr_t)(random_u32()%3+1);
        }
        memcpy(original,b,sizeof b); memcpy(old_offsets,offsets,sizeof offsets);
        memcpy(old_buffers,buffers,sizeof buffers);
        uint32_t count=quest_compact_vertex_bindings(b,a,buffers,offsets,16,limit);
        reduced+=count<16;
        if(count<1 || count>16) { ++failed; continue; }
        for(unsigned i=0;i<16;++i) {
            uint32_t slot=a[i].binding;
            if(slot>=count || a[i].location!=i || a[i].offset>limit ||
               a[i].format!=VK_FORMAT_R32G32B32A32_SFLOAT || buffers[slot]!=old_buffers[i] ||
               b[slot].stride!=original[i].stride || b[slot].inputRate!=original[i].inputRate ||
               b[slot].divisor!=original[i].divisor) { ++failed; continue; }
            for(unsigned vertex=0;vertex<100;vertex+=33)
                if(old_offsets[i]+(VkDeviceSize)vertex*original[i].stride !=
                   offsets[slot]+a[i].offset+(VkDeviceSize)vertex*b[slot].stride) ++failed;
        }
    }
    if(!reduced) ++failed;
    printf("Quest vertex binding checks: %u failures, %u layouts reduced\n",failed,reduced);
    return failed?1:0;
}
