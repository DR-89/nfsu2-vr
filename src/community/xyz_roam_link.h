#pragma once
#include "xyz_protocol.h"
#include "xyz_xbox_vehicle.h"
#include <deque>
#include <memory>

namespace xyz {
struct RoamDiagnostics {
    uint32_t hub_lists=0,rx=0,bad=0,policy=0,version=0,room=0,unlisted=0;
    int hub=0,listed=0,candidates=0,configured=0,moving=0;
};
// Owned by the guest's main-loop hook. Only bounded, copied datagrams cross to
// the transport worker; its callbacks never touch rc_session or guest memory.
class RoamLink {
public:
    RoamLink();
    RoamLink(const RoamLink &) = delete;
    RoamLink &operator=(const RoamLink &) = delete;
    bool configure(const std::string &name, const std::string &room, uint32_t player_id, int cars);
    bool bind(uint32_t virtual_ip, uint32_t generation);
    void unbind();
    void leave();
    void frame(const XyzXboxFrame &local, bool paused, int64_t now_us);
    bool receive(const uint8_t *data, size_t size, uint32_t generation, int64_t now_us);
    bool take(std::vector<uint8_t> &data);
    int peers() const;
    RoamDiagnostics diagnostics(int64_t now_us) const;
    // Pointers are valid only on this owner thread, until its next operation.
    const rc_peer *peer(int index) const;
    const rc_peer *listed_peer(int index) const;
    bool pose(int index, int64_t now_us, int64_t frame_us, rc_pose &out);
    uint64_t dropped() const { return dropped_; }
    uint32_t generation() const { return generation_; }
private:
    static void send(void *user, uint32_t ip, uint16_t port, const uint8_t *data, int size);
    std::unique_ptr<rc_session> core_;
    rc_config config_{};
    uint32_t vip_=0, generation_=0, last_world_=0, last_car_=0;
    bool configured_=false, have_car_=false;
    std::deque<std::vector<uint8_t>> outgoing_;
    uint64_t dropped_=0;
};
}
