#include "xyz_protocol.h"
#include "xyz_json.h"
#include <algorithm>
#include <cstdio>

namespace xyz {
static uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
static void put32(uint8_t *p, uint32_t v) { for (int i = 3; i >= 0; --i) { p[i] = uint8_t(v); v >>= 8; } }
bool service_ip(uint32_t ip) { return (ip >> 16) == 0x0a4d && ip != 0x0a4d0000 && ip != 0x0a4dffff; }
std::string ip_text(uint32_t ip) {
    char b[16]; std::snprintf(b, sizeof(b), "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255); return b;
}
bool parse_ip(const std::string &text, uint32_t &ip) {
    uint32_t result = 0; size_t p = 0;
    for (int i = 0; i < 4; ++i) {
        unsigned v = 0, digits = 0;
        while (p < text.size() && text[p] >= '0' && text[p] <= '9') {
            v = v * 10 + unsigned(text[p++] - '0');
            if (++digits > 3 || v > 255) return false;
        }
        if (!digits || (i < 3 && (p == text.size() || text[p++] != '.'))) return false;
        result = (result << 8) | v;
    }
    if (p != text.size()) return false;
    ip = result; return true;
}
static bool datagram_valid(const Datagram &d) {
    if (d.kind != 1 && d.kind != 3 && d.kind != 4) return false;
    if ((!service_ip(d.source) && d.source) || (!service_ip(d.destination) && d.destination != UINT32_MAX)) return false;
    if (!d.source_port || !d.destination_port || d.payload.empty()) return false;
    return d.payload.size() <= (d.kind == 1 ? 1200u : 1472u);
}
std::vector<uint8_t> encode_datagram(const Datagram &d) {
    if (!datagram_valid(d)) return {};
    std::vector<uint8_t> b(16 + d.payload.size(), 0);
    b[0] = d.kind; put32(b.data() + 4, d.source); put32(b.data() + 8, d.destination);
    b[12] = uint8_t(d.source_port >> 8); b[13] = uint8_t(d.source_port);
    b[14] = uint8_t(d.destination_port >> 8); b[15] = uint8_t(d.destination_port);
    std::copy(d.payload.begin(), d.payload.end(), b.begin() + 16); return b;
}
bool decode_datagram(const uint8_t *b, size_t n, Datagram &out) {
    if (!b || n <= 16 || n > 8208 || b[1] || b[2] || b[3]) return false;
    Datagram d; d.kind = b[0]; d.source = be32(b + 4); d.destination = be32(b + 8);
    d.source_port = uint16_t(b[12] << 8 | b[13]); d.destination_port = uint16_t(b[14] << 8 | b[15]);
    d.payload.assign(b + 16, b + n);
    if (!datagram_valid(d) || !service_ip(d.source)) return false;
    out = std::move(d); return true;
}
static bool hex(const std::string &s, size_t n) {
    return s.size() == n && s.find_first_not_of("0123456789abcdef") == std::string::npos;
}
bool Session::configure(const std::string &name, const std::string &identity, const std::string &claim, bool lan) {
    if (name.size() < 3 || name.size() > 15 ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos ||
        !hex(identity, 32) || (!claim.empty() && (claim.size() != 43 ||
        claim.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos))) return false;
    *this = Session(); name_ = name; identity_ = identity; claim_ = claim; lan_ = lan; return true;
}
bool Session::begin(uint64_t now) {
    if (name_.empty() || (state_ != LinkState::offline && !(state_ == LinkState::retry && now >= retry_at_))) return false;
    state_ = LinkState::connecting; last_receive_ = last_ping_ = now; return true;
}
std::string Session::path() const {
    return std::string("/lan?queue") + ((!ticket_.empty() || (vip_ && !token_.empty())) ? "&resume" : "");
}
std::string Session::opened(uint64_t now) {
    if (state_ != LinkState::connecting) return {};
    Json j(cJSON_CreateObject(), cJSON_Delete);
    cJSON_AddStringToObject(j.get(), "t", "hello");
    cJSON_AddStringToObject(j.get(), "name", name_.c_str());
    cJSON_AddStringToObject(j.get(), "id", identity_.c_str());
    // This subscribes to the LAN channel; it does not advertise free-roam
    // lobby capability. The game keeps this off until its race adapter works.
    cJSON_AddNumberToObject(j.get(), "lan", lan_ ? 1 : 0);
    if (!claim_.empty()) cJSON_AddStringToObject(j.get(), "claim", claim_.c_str());
    if (vip_ && !token_.empty()) {
        cJSON_AddStringToObject(j.get(), "vip", ip_text(vip_).c_str());
        cJSON_AddStringToObject(j.get(), "token", token_.c_str());
    }
    if (!ticket_.empty()) cJSON_AddStringToObject(j.get(), "ticket", ticket_.c_str());
    state_ = LinkState::greeting; last_receive_ = last_ping_ = now;
    ping_id_ = last_pong_ = 0; return json_text(j.get());
}
bool Session::control(const std::string &text, uint64_t now) {
    if (state_ != LinkState::greeting && state_ != LinkState::queued && state_ != LinkState::connected) return false;
    auto j = parse_json(text,max_message); if (!j) return false;
    auto type = json_string(j.get(), "t", 24);
    if (type == "welcome") {
        uint32_t vip;
        auto token = json_string(j.get(), "token", 256);
        if (state_ == LinkState::connected || !json_u32(j.get(), "vip", vip) || !service_ip(vip) || vip == hub_ip || token.empty()) return false;
        vip_ = vip; token_ = token; ticket_.clear(); position_ = total_ = 0;
        state_ = LinkState::connected; ++generation_; backoff_ = 250; notice_.clear();
    } else if (type == "queue") {
        uint32_t pos, of;
        if (state_ == LinkState::connected || !json_u32(j.get(), "pos", pos) || !json_u32(j.get(), "of", of) || !pos || pos > of) return false;
        auto ticket = json_string(j.get(), "ticket", 256);
        if (!ticket.empty()) ticket_ = ticket;
        position_ = pos; total_ = of; state_ = LinkState::queued; backoff_ = 250;
    } else if (type == "kick" || type == "ban" || type == "rename") {
        notice_ = json_string(j.get(), "msg", 1024);
        if (notice_.empty()) notice_ = type;
        stop(); // Never reconnect around an operator decision.
    } else if (type == "races") {
        race_restricted_ = !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j.get(), "lifted"));
        notice_ = race_restricted_ ? json_string(j.get(), "msg", 1024) : std::string();
    } else if (type == "pong") {
        uint32_t id;
        if (!json_u32(j.get(), "id", id) || !id || id > ping_id_ || id <= last_pong_) return false;
        last_pong_ = id;
    } else {
        // Other control types are not evidence that our ping reached the server.
        return false;
    }
    last_receive_ = now; return true;
}
void Session::disconnected(uint16_t code, uint64_t now) {
    if (state_ == LinkState::stopped || state_ == LinkState::offline || state_ == LinkState::retry) return;
    if (code == 1002 || code == 1003 || code == 1007 || code == 1008 || code == 1009 || (code >= 4000 && code < 5000)) {
        if (notice_.empty()) notice_ = "Server closed this session";
        stop(); return;
    }
    state_ = LinkState::retry; retry_at_ = now + backoff_;
    backoff_ = std::min(backoff_ * 2, 5000u);
}
void Session::stop() { state_ = LinkState::stopped; token_.clear(); ticket_.clear(); claim_.clear(); vip_ = position_ = total_ = 0; }
bool Session::retry_due(uint64_t now) const { return state_ == LinkState::retry && now >= retry_at_; }
bool Session::expired(uint64_t now) const {
    if (now < last_receive_) return true;
    return (state_ == LinkState::connecting || state_ == LinkState::greeting) ? now - last_receive_ > 20000 :
        (state_ == LinkState::connected || state_ == LinkState::queued) && now - last_receive_ > 45000;
}
std::string Session::heartbeat(uint64_t now) {
    if ((state_ != LinkState::greeting && state_ != LinkState::queued && state_ != LinkState::connected) || now < last_ping_ || now - last_ping_ < 10000) return {};
    last_ping_ = now; return "{\"t\":\"ping\",\"id\":" + std::to_string(++ping_id_) + "}";
}
}
