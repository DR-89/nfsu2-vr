#include "xyz_protocol.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    using namespace xyz;
    uint32_t ip = 0;
    CHECK(parse_ip("10.77.0.253", ip) && ip == hub_ip);
    for (auto bad : {"10.77.0", "10.77.0.256", "10.77.0.1x", "10.77..1", "10.77.0000.1", "-1.77.0.1"}) CHECK(!parse_ip(bad, ip));
    Datagram d; d.source = 0x0a4d0102; d.destination = hub_ip; d.payload = {0x4e, 0x32, 0x46, 0x52};
    const std::vector<uint8_t> expected = {1,0,0,0,10,77,1,2,10,77,0,253,0xb8,0x6a,0xb8,0x6a,0x4e,0x32,0x46,0x52};
    CHECK(encode_datagram(d) == expected);
    Datagram got; CHECK(decode_datagram(expected.data(), expected.size(), got));
    CHECK(got.payload == d.payload && got.source == d.source && got.destination == hub_ip);
    for (size_t n = 0; n <= 16; ++n) CHECK(!decode_datagram(expected.data(), n, got));
    for (size_t i = 0; i < 4; ++i) { auto b = expected; b[i] = 0xff; CHECK(!decode_datagram(b.data(), b.size(), got)); }
    auto b = expected; b[4] = 1; CHECK(!decode_datagram(b.data(), b.size(), got));
    d.payload.resize(1201); CHECK(encode_datagram(d).empty());
    Session s;
    CHECK(!s.configure("x", std::string(32, 'a')));
    CHECK(s.configure("VR_Player", std::string(32, 'a'), std::string(43, 'A')));
    CHECK(s.begin(100));
    auto hello = s.opened(110);
    CHECK(hello.find("\"name\":\"VR_Player\"") != std::string::npos);
    CHECK(hello.find("\"claim\":") != std::string::npos && hello.find("bundle") == std::string::npos);
    CHECK(!s.control("{\"t\":\"welcome\",\"vip\":1,\"token\":\"x\"}", 120));
    CHECK(!s.control("{\"t\":\"welcome\",\"t\":\"queue\"}", 120));
    CHECK(!s.control("{\"t\":\"welcome\"}junk", 120));
    CHECK(!s.control("{\"t\":\"welcome\\u0000junk\"}", 120));
    CHECK(s.control("{\"t\":\"queue\",\"pos\":2,\"of\":9,\"ticket\":\"secret-ticket\"}", 125));
    CHECK(s.state() == LinkState::queued && s.queue_position() == 2);
    s.disconnected(1006, 130); CHECK(!s.retry_due(379) && s.retry_due(380));
    CHECK(s.path() == "/lan?queue&resume" && s.path().find("secret") == std::string::npos);
    CHECK(s.begin(380) && s.opened(390).find("secret-ticket") != std::string::npos);
    CHECK(s.control("{\"t\":\"welcome\",\"vip\":172818690,\"token\":\"secret-resume\",\"mask\":\"255.255.248.0\"}", 400));
    CHECK(s.state() == LinkState::connected && s.vip() == 0x0a4d0102 && s.generation() == 1);
    CHECK(s.heartbeat(500).empty());
    CHECK(s.heartbeat(10390) == "{\"t\":\"ping\",\"id\":1}");
    CHECK(!s.control("{\"t\":\"pong\",\"id\":2}", 10400));
    CHECK(s.control("{\"t\":\"pong\",\"id\":1}", 10400));
    CHECK(!s.control("{\"t\":\"pong\",\"id\":1}", 10500));
    CHECK(!s.expired(55400) && s.expired(55401));
    s.disconnected(1006, 55401); CHECK(s.begin(55651));
    CHECK(s.opened(55652).find("secret-resume") != std::string::npos);
    CHECK(s.control("{\"t\":\"welcome\",\"vip\":172818690,\"token\":\"new-resume\"}", 55660));
    CHECK(s.generation() == 2);
    CHECK(s.control("{\"t\":\"races\",\"msg\":\"restricted\"}", 55661) && !s.races_allowed());
    CHECK(s.control("{\"t\":\"races\",\"lifted\":true}", 55662) && s.races_allowed());
    CHECK(s.control("{\"t\":\"ban\",\"msg\":\"test\"}", 55663));
    s.disconnected(4003, 55664);
    CHECK(s.state() == LinkState::stopped && !s.retry_due(9999999) && !s.begin(9999999) && s.vip() == 0);
    for (uint16_t code : {1008, 4001, 4003, 4004}) {
        CHECK(s.configure("VR_Player", std::string(32, 'b')) && s.begin(1));
        s.disconnected(code, 2); CHECK(s.state() == LinkState::stopped);
    }
    std::puts("xyz_protocol: framing, controls, queue, reconnect, heartbeat and server restrictions passed");
    return 0;
}
