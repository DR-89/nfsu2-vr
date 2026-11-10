#pragma once
#include "xyz_xbox_assets.h"
#include <array>

namespace xyz {
// Guest-thread ownership. One extra reference to each shared header is cached
// for the process, as upstream does. No model data is pinned by these header
// references. Car destruction releases its own model references normally.
class Assets {
public:
    int prepare(const XyzXboxGuest &g,uint64_t now); // -1 failed, 0 loading, 1 ready
    void maintain(const XyzXboxGuest &g);
    bool started() const{return started_;}
    int before_wake(const XyzXboxGuest &g,uint32_t file,XyzXboxWakePlan &plan);
    void after_wake(const XyzXboxGuest &g,XyzXboxWakePlan &plan);
private:
    struct Pack {uint32_t file=0,pointer=0;};
    XyzXboxPackCatalog catalog_{};
    XyzXboxArena arena_{};
    std::array<Pack,XYZ_PACK_CAP> packs_{};
    unsigned loaded_=0,scan_=0;
    uint64_t started_at_=0,fingerprint_=0;
    bool started_=false,failed_=false,dirty_=true,deferred_=false,initial_ready_=false;
    bool fingerprint(const XyzXboxGuest &g,uint64_t &value);
    bool owned(const XyzXboxGuest &g,unsigned index,uint32_t &pack);
};
}
