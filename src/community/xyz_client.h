#pragma once
#include "xyz_roam_link.h"
#include "xyz_transport.h"
#include "xyz_xnet.h"
#include "xyz_limits.h"
#include <mutex>

namespace xyz {
// Copied on the game thread; never contains guest pointers or core pointers.
struct RemotePlayer {
    uint32_t id=0,ip=0;
    std::vector<uint8_t> config;
    rc_pose pose{};
    bool has_pose=false;
    bool listed=true; // false during the core's bounded hub-removal grace
    uint8_t status=0;
    uint32_t snap_age_ms=0;
    bool send_only=false,lost=false;
    uint8_t dents_epoch=0;
    uint16_t dents_first=0;
    std::vector<uint8_t> dents;
};
struct ClientProfile {
    ConnectionProfile connection;
    std::string room;
    uint32_t player_id=0;
};
struct ClientStatus {
    TransportState transport;
    std::string name,room;
    int peers=0;
    RoamDiagnostics roam;
    XyzXboxStatus local=XYZ_XBOX_FRONTEND;
};
struct ClientFrame {
    uint32_t generation=0;
    std::vector<RemotePlayer> players;
};
class Client {
public:
    explicit Client(Transport::Factory factory,Transport::Clock clock={});
    // Render-thread commands. No network or game work is done by these calls.
    bool connect(const ClientProfile &profile);
    void disconnect();
    ClientStatus snapshot() const;
    // Sole owner of the core, called by the guest main loop. A new generation
    // invalidates all previously owned remote cars, including a same-IP resume.
    const ClientFrame &frame(const XyzXboxFrame &local,bool paused,int64_t now_us);
    void abort(); // guest thread only: stop the worker even after frame failure
    // Guest socket entry points. Only copied LAN packets reach the worker.
    // A false result delegates a retail handle to the original Xbox routine.
    bool socket_call(const XyzXboxGuest &guest,XyzXnetOp op,uint32_t thread,
                     const uint32_t *args,unsigned count,uint64_t now_ms,uint32_t &result);
    static constexpr int car_limit=XYZ_CARS_DEFAULT;
    // Keep a wider neighbourhood connected while the six visible cars are
    // retained locally. Asking for only six makes the hub stop sending an
    // owned car whenever another predicted encounter takes its link.
    static constexpr int receive_limit=RC_MAX_PEERS;
private:
    Transport::Factory factory_;
    Transport::Clock clock_;
    std::unique_ptr<Transport> transport_;
    RoamLink roam_;
    LanEndpoint lan_{0x58595a31};
    XnetSockets sockets_{lan_,0};
    mutable std::mutex mutex_;
    uint64_t request_=0,applied_=0;
    bool wanted_=false,requested_lan_=false;
    ClientProfile pending_;
    ClientStatus status_;
    ClientFrame frame_;
    int64_t previous_=0;
    void pump_lan(uint64_t now_ms); // mutex_ held; no guest callbacks
};
}
