#pragma once
#include "xyz_protocol.h"
#include "xyz_socket.h"
#include <functional>
#include <memory>

namespace xyz {
struct ConnectionProfile { std::string name,identity,claim;bool lan=false; };
struct TransportState {
    LinkState state=LinkState::offline;
    uint32_t vip=0,generation=0,queue_position=0,queue_total=0;
    uint64_t request=0,dropped=0;
    bool races_allowed=false;
    bool lan_available=false;
    std::string notice; // no claim, resume token, device identity or URL
};
struct ReceivedDatagram { uint32_t generation=0;std::vector<uint8_t> bytes;uint32_t departed_ip=0; };
// The only interface shared with guest/render threads consists of copied
// commands, snapshots and bounded datagrams. Every Socket/Session operation,
// including creation and close, belongs to the worker. stop() never joins it.
class Transport {
public:
    static constexpr unsigned roam_queue_limit=512;
    using Factory=std::function<std::unique_ptr<Socket>()>;
    using Clock=std::function<uint64_t()>;
    explicit Transport(Factory factory,Clock clock={});
    ~Transport(); // shutdown only, after gameplay: joins the network worker
    Transport(const Transport&)=delete;
    Transport& operator=(const Transport&)=delete;
    bool start(const ConnectionProfile &profile);
    void stop();
    TransportState snapshot() const;
    bool send(uint32_t generation,const uint8_t *bytes,size_t size);
    bool take(ReceivedDatagram &out);
    bool send_lan(uint32_t generation,const uint8_t *bytes,size_t size);
    bool take_lan(ReceivedDatagram &out);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
