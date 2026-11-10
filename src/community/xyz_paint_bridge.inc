/* Private GPU paint storage: guest-thread only, independent of bMalloc's
 * small general-purpose pool. Never free a game-owned or unknown address. */
extern uint32_t xbox_ContiguousAlloc(uint32_t size,uint32_t alignment);
extern int xbox_ContiguousFree(uint32_t address);
#include "xyz_limits.h"
static uint32_t community_paint_allocations[XYZ_CARS_MAX*3];
static int community_paint_memory(enum XyzXboxOp op,unsigned count,const uint32_t *args,uint32_t *result) {
    if(count!=1 || !args || !result)return 0;
    *result=0;
    if(op==XYZ_G_PAINT_ALLOC) {
        if(!args[0] || args[0]>(1u<<20)+4096)return 0;
        for(unsigned i=0;i<XYZ_CARS_MAX*3;++i)if(!community_paint_allocations[i]) {
            uint32_t address=xbox_ContiguousAlloc(args[0],4096);
            if(!address)return 1;
            if(address<0x80000000u || !xyz_xbox_address(address,args[0]))return 0;
            for(unsigned j=0;j<XYZ_CARS_MAX*3;++j)if(community_paint_allocations[j]==address)return 0;
            community_paint_allocations[i]=address;*result=address;return 1;
        }
        return 1;
    }
    if(op==XYZ_G_PAINT_FREE) {
        if(!args[0])return 0;
        for(unsigned i=0;i<XYZ_CARS_MAX*3;++i)if(community_paint_allocations[i]==args[0]) {
            if(xbox_ContiguousFree(args[0])){community_paint_allocations[i]=0;*result=1;}
            return 1;
        }
    }
    return 0;
}
