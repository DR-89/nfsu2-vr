#pragma once
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <string.h>

/* Only input FORMAT state. Vertex buffers/offsets, descriptors and all draw
 * commands remain unconditional. Invalidate at every command-buffer, render
 * pass and pipeline transition: multiview does not preserve dynamic state. */
typedef struct {
    int valid;
    uint32_t command, pass, bindings, attributes;
    VkPipeline pipeline;
    VkVertexInputBindingDescription2EXT binding[16];
    VkVertexInputAttributeDescription2EXT attribute[16];
} QuestVertexLayout;

static inline int quest_vertex_layout_set(QuestVertexLayout *old, int enabled,
    uint32_t command, uint32_t pass, VkPipeline pipeline,
    uint32_t nb, const VkVertexInputBindingDescription2EXT *b,
    uint32_t na, const VkVertexInputAttributeDescription2EXT *a)
{
    if(!enabled || nb>16 || na>16) { old->valid=0; return 1; }
    for(uint32_t i=0;i<nb;++i) if(b[i].pNext) { old->valid=0; return 1; }
    for(uint32_t i=0;i<na;++i) if(a[i].pNext) { old->valid=0; return 1; }
    if(old->valid && old->command==command && old->pass==pass && old->pipeline==pipeline &&
       old->bindings==nb && old->attributes==na &&
       !memcmp(old->binding,b,nb*sizeof *b) && !memcmp(old->attribute,a,na*sizeof *a)) return 0;
    old->valid=1; old->command=command; old->pass=pass; old->pipeline=pipeline;
    old->bindings=nb; old->attributes=na;
    memcpy(old->binding,b,nb*sizeof *b); memcpy(old->attribute,a,na*sizeof *a);
    return 1;
}
