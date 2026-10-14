#pragma once
#include <vulkan/vulkan.h>

/* Input: one binding per attribute, in attribute order, offset zero.
 * Share bindings only when buffer/stride/rate/divisor match and the byte
 * displacement fits the device limit. Every attribute and fetch address is
 * retained; this changes neither geometry nor shader input locations. */
static inline uint32_t quest_compact_vertex_bindings(
    VkVertexInputBindingDescription2EXT *bindings,
    VkVertexInputAttributeDescription2EXT *attributes,
    VkBuffer *buffers, VkDeviceSize *offsets, uint32_t count, uint32_t max_offset)
{
    uint32_t used=0;
    for(uint32_t i=0;i<count;++i) {
        VkVertexInputBindingDescription2EXT current=bindings[i];
        VkBuffer buffer=buffers[i];
        VkDeviceSize offset=offsets[i];
        uint32_t j;
        for(j=0;j<used;++j) {
            if(buffers[j]==buffer && bindings[j].stride==current.stride &&
               bindings[j].inputRate==current.inputRate && bindings[j].divisor==current.divisor &&
               offset>=offsets[j] && offset-offsets[j]<=max_offset) break;
        }
        if(j==used) {
            bindings[j]=current; bindings[j].binding=j;
            buffers[j]=buffer; offsets[j]=offset; ++used;
        }
        attributes[i].binding=j;
        attributes[i].offset=(uint32_t)(offset-offsets[j]);
    }
    return used;
}
