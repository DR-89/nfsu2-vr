#pragma once
#include "xyz_protocol.h"
#include <cstddef>
#include <memory>

namespace xyz {
// Wire contract: runtime/web/net_proto.h and net_lan.c in the pinned upstream.
// This is its stream protocol over an ordered WebSocket, not host TCP packets.
constexpr size_t lan_header_size=8, lan_data_max=1464;
enum class LanOp : uint8_t { udp=0, syn=1, synack=2, reset=3, data=4, fin=5 };
struct LanPacket {
    uint8_t kind=3,op=0,ttl=64;
    uint32_t connection=0;
    std::vector<uint8_t> data;
};
bool decode_lan(const Datagram &datagram,LanPacket &packet);
bool encode_lan(const LanPacket &packet,Datagram &datagram);

enum class LanError { ok, would_block, invalid, offline, refused, reset, timeout, no_buffer, closed };
struct LanResult { LanError error=LanError::ok;size_t bytes=0; };
struct LanReady {bool read=false,write=false,except=false;size_t bytes=0;uint32_t peer=0;uint16_t remote_port=0;};
using LanHandle=uint32_t;
class Transport;
// Single game/network-adapter owner. No guest pointers or OS sockets. All calls
// are nonblocking; TCP writes may be short and UDP datagrams remain atomic.
class LanEndpoint {
public:
    explicit LanEndpoint(uint32_t connection_seed);
    ~LanEndpoint();
    LanEndpoint(const LanEndpoint&)=delete;
    LanEndpoint& operator=(const LanEndpoint&)=delete;
    void link(uint32_t vip,uint32_t generation,bool allowed);
    void advance(Transport &transport,uint64_t now_ms);
    void tick(uint64_t now_ms);
    void peer_left(uint32_t vip);
    LanHandle udp(uint16_t port=0);
    LanHandle listen(uint16_t port,unsigned backlog=6);
    LanHandle connect(uint32_t peer,uint16_t port,uint64_t now_ms,uint16_t local_port=0);
    LanHandle accept(LanHandle listener);
    LanError status(LanHandle handle) const;
    uint16_t port(LanHandle handle) const;
    LanReady ready(LanHandle handle) const;
    LanResult send(LanHandle handle,const uint8_t *data,size_t size);
    LanResult receive(LanHandle handle,uint8_t *data,size_t capacity,bool peek=false);
    LanResult send_to(LanHandle handle,uint32_t peer,uint16_t port,const uint8_t *data,size_t size);
    LanResult receive_from(LanHandle handle,uint32_t &peer,uint16_t &port,std::vector<uint8_t> &data,bool peek=false);
    LanError shutdown_write(LanHandle handle);
    LanError close(LanHandle handle,bool abortive=false);
    bool input(const Datagram &datagram,uint32_t generation);
    // Retain the front frame until the transport has accepted it. Removing a
    // frame on a failed send would silently corrupt a reliable stream.
    const Datagram *output() const;
    void consume_output();
    size_t queued() const;
private:
    bool input_impl(const Datagram &datagram,uint32_t generation,bool local);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
