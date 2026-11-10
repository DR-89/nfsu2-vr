#pragma once
#include "xyz_xbox_remote.h"
#include "xyz_limits.h"
#include <array>
#include <mutex>
namespace xyz {
// Guest-thread construction; lookup may also run on a guest loader thread.
// Private texture allocations stay alive for the guest process, independently
// of streamed car packs. Allocated on demand, up to the selected car count.
class Skins {
public:
    static constexpr unsigned first=XYZ_SKIN_FIRST,count=XYZ_CARS_MAX;
    int prepare(const XyzXboxGuest &,unsigned skin);
    uint32_t lookup(uint32_t hash) const;
private:
    struct Texture {uint32_t hash=0,info=0,allocation=0,bytes=0;};
    struct Slot {bool ready=false;std::array<Texture,3> textures{};};
    std::array<Slot,count> slots_{};
    mutable std::mutex mutex_;
    static int create(const XyzXboxGuest &,unsigned skin,unsigned part,Texture &);
};
}
