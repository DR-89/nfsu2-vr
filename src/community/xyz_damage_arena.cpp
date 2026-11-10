#include "xyz_damage_arena.h"
#include <map>
#include <algorithm>
#include <cstddef>
namespace {
constexpr uint32_t chunk=4u<<20,limit=32u<<20;
struct Range {uint32_t size,block;bool used;};
std::map<uint32_t,Range> ranges;
uint32_t reserved=0,used=0;
uint32_t (*backing)(uint32_t,uint32_t)=nullptr;
}
extern "C" void xyz_damage_arena_init(uint32_t (*reserve)(uint32_t,uint32_t)){if(!backing)backing=reserve;}
extern "C" uint32_t xyz_damage_arena_available(){return limit-used;}
extern "C" uint32_t xyz_damage_arena_alloc(uint32_t bytes){
    if(!bytes || bytes>chunk || !backing)return 0;
    bytes=(bytes+127u)&~127u;
    for(unsigned attempt=0;attempt<2;attempt++){
        for(auto it=ranges.begin();it!=ranges.end();++it){
            auto& r=it->second;if(r.used || r.size<bytes)continue;
            const auto address=it->first;
            if(r.size>bytes)ranges.emplace(address+bytes,Range{r.size-bytes,r.block,false});
            r.size=bytes;r.used=true;used+=bytes;return address;
        }
        if(reserved==limit)return 0;
        uint32_t address=backing(chunk,4096);
        if(!address || (address&127u) || uint64_t(address)+chunk>0x100000000ull)return 0;
        ranges.emplace(address,Range{chunk,address,false});reserved+=chunk;
    }
    return 0;
}
extern "C" void xyz_damage_arena_free(uint32_t address){
    auto it=ranges.find(address);if(it==ranges.end() || !it->second.used)return;
    auto& r=it->second;r.used=false;used-=r.size;
    auto next=std::next(it);
    if(next!=ranges.end() && !next->second.used && next->second.block==r.block && address+r.size==next->first){r.size+=next->second.size;ranges.erase(next);}
    if(it!=ranges.begin()){
        auto prev=std::prev(it);
        if(!prev->second.used && prev->second.block==r.block && prev->first+prev->second.size==address){prev->second.size+=r.size;ranges.erase(it);}
    }
}
