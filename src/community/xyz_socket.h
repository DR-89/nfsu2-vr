#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xyz {
struct SocketEvent {
    enum Kind { opened, text, binary, closed } kind = closed;
    std::vector<uint8_t> data;
    uint16_t code = 1006;
};
// The owner is a network worker, never the render or guest thread. Received
// Binary messages are bounded to 64 KiB, text to 1 MiB (LAN peer lists), and
// the queue to 256 events / 4 MiB. Neither credentials nor peer lists are logged.
class Socket {
public:
    virtual ~Socket() = default;
    // Worker-only, read-only HTTPS /names. Empty means unavailable. Only the
    // fixed service origin (or explicit loopback test seam) is permitted.
    virtual std::string name_info(const std::string &name,const std::string &claim) = 0;
    virtual bool connect(const std::string &path) = 0;
    virtual bool send(const uint8_t *data, size_t size, bool text) = 0;
    virtual bool poll(SocketEvent &event) = 0;
    virtual void close() = 0;
};
std::unique_ptr<Socket> make_socket();
#ifdef _WIN32
// Only loopback plaintext is available to local protocol tests; the production
// constructor always uses nfsu2.xyz:443 with the Windows certificate verifier.
std::unique_ptr<Socket> make_loopback_test_socket(uint16_t port);
#endif
}
