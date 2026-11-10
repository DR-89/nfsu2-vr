#pragma once
#include "xyz_client.h"
#include "xyz_xbox_remote.h"
#include "xyz_assets.h"
#include "xyz_skins.h"
#include "xyz_stance.h"
#include <array>

namespace xyz {
struct FleetStatus {unsigned visible=0,loading=0,failed=0,fallbacks=0,missing_parts=0;};
// Guest-thread only. A bounded pool of arenas stays alive while any loader
// reference can use it. Creation is serialized to reserve unique racer IDs.
class Fleet {
public:
    explicit Fleet(unsigned capacity=Client::car_limit):limit_(capacity<1?1:capacity>Skins::count?Skins::count:capacity){}
    void set_limit(unsigned capacity){limit_=capacity<1?1:capacity>Skins::count?Skins::count:capacity;}
    bool retired() const {
        for(const auto &s:slots_)if(s.id || s.retiring || s.clone.stage!=XYZ_CLONE_EMPTY || s.clone.car || s.clone.handle || s.clone.look_handle || s.clone.pending_handle)return false;
        return true;
    }
    FleetStatus frame(const XyzXboxGuest &guest,const XyzXboxFrame &local,
                      const ClientFrame &remote,uint64_t now_us);
    int before_wake(const XyzXboxGuest &g,uint32_t file,XyzXboxWakePlan &plan){return assets_.before_wake(g,file,plan);}
    void after_wake(const XyzXboxGuest &g,XyzXboxWakePlan &plan){assets_.after_wake(g,plan);}
    int assets_probe(const XyzXboxGuest &g,uint64_t now){return assets_.prepare(g,now);}
    uint32_t texture(uint32_t hash) const {return skins_.lookup(hash);}
    bool stance(uint32_t car,XyzStance &out) const {
        for(const auto &s:slots_)if(s.clone.car==car && s.clone.visible && !s.retiring && s.has_stance){out=s.stance;return true;}
        return false;
    }
    int lights(uint32_t car) const {
        for(const auto &s:slots_)if(s.clone.car==car && s.clone.visible && !s.retiring)return s.lights;
        return 0;
    }
    void damage(const ClientFrame &remote) const;
private:
    struct Slot {
        XyzXboxArena arena{};
        XyzXboxClone clone{};
        uint32_t id=0,ip=0;
        std::vector<uint8_t> pending,applied;
        XyzXboxRideResult look{};
        bool retiring=false;
        bool has_stance=false;
        XyzStance stance{};
        int lights=0;
        uint64_t retry=0,shown=0;
    };
    static_assert(Client::car_limit<=Skins::count,"Every remote car needs its own paint set");
    std::array<Slot,Skins::count> slots_{};
    unsigned limit_;
    uint32_t generation_=0,world_=0;
    uint64_t previous_=0;
    Assets assets_;
    Skins skins_;
    static bool retire(const XyzXboxGuest &guest,Slot &slot);
};
}
