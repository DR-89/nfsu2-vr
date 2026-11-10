#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace xyz {
constexpr uint32_t hub_ip = 0x0a4d00fd;
constexpr uint16_t roam_port = 47210;
// A full LAN peer list can exceed 64 KiB (the upstream switch has 24000
// places). Keep binary frames at the old cap; text control has its own bound.
constexpr size_t max_message = 1024*1024;
constexpr size_t max_binary_message = 65536;
constexpr size_t max_socket_queue_bytes = 4*1024*1024;
enum class LinkState { offline, connecting, greeting, queued, connected, retry, stopped };
struct Datagram {
    uint8_t kind = 1;
    uint32_t source = 0, destination = 0;
    uint16_t source_port = roam_port, destination_port = roam_port;
    std::vector<uint8_t> payload;
};
std::vector<uint8_t> encode_datagram(const Datagram &d);
bool decode_datagram(const uint8_t *data, size_t size, Datagram &d);
bool service_ip(uint32_t ip);
std::string ip_text(uint32_t ip);
bool parse_ip(const std::string &text, uint32_t &ip);

// Single-thread owned. Transport events are marshalled to this owner; no guest
// memory is touched on a network callback. Credentials are intentionally private.
class Session {
public:
    bool configure(const std::string &name, const std::string &identity, const std::string &claim = {}, bool lan = false);
    bool begin(uint64_t now_ms);
    std::string opened(uint64_t now_ms);
    bool control(const std::string &json, uint64_t now_ms);
    void disconnected(uint16_t code, uint64_t now_ms);
    void stop();
    bool retry_due(uint64_t now_ms) const;
    bool expired(uint64_t now_ms) const;
    std::string heartbeat(uint64_t now_ms);
    std::string path() const;
    LinkState state() const { return state_; }
    uint32_t vip() const { return vip_; }
    uint32_t generation() const { return generation_; }
    uint32_t queue_position() const { return position_; }
    uint32_t queue_total() const { return total_; }
    bool races_allowed() const { return !race_restricted_; }
    bool lan_enabled() const { return lan_; }
    const std::string &notice() const { return notice_; }
private:
    LinkState state_ = LinkState::offline;
    std::string name_, identity_, claim_, token_, ticket_, notice_;
    uint32_t vip_ = 0, generation_ = 0, position_ = 0, total_ = 0;
    uint32_t ping_id_ = 0, last_pong_ = 0, backoff_ = 250;
    uint64_t retry_at_ = 0, last_receive_ = 0, last_ping_ = 0;
    bool race_restricted_ = false;
    bool lan_ = false;
};
}
