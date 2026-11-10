#ifndef XYZ_XNET_H
#define XYZ_XNET_H
#include "xyz_xbox_remote.h"
#ifdef __cplusplus
extern "C" {
#endif
enum XyzXnetOp {XYZ_N_SOCKET,XYZ_N_CLOSE,XYZ_N_SHUTDOWN,XYZ_N_IOCTL,XYZ_N_SETOPT,
 XYZ_N_SOCKNAME,XYZ_N_PEERNAME,XYZ_N_BIND,XYZ_N_CONNECT,XYZ_N_LISTEN,XYZ_N_ACCEPT,
 XYZ_N_SELECT,XYZ_N_RECV,XYZ_N_RECVFROM,XYZ_N_SEND,XYZ_N_SENDTO,XYZ_N_ERROR,XYZ_N_COUNT};
unsigned xyz_xnet_arg_count(enum XyzXnetOp op);
// Dormant unless a verified adapter has selected an endpoint. Guest code only;
// no network callback ever reads guest memory. Returns zero to use retail XNet.
int xyz_xnet_dispatch(const XyzXboxGuest *guest,enum XyzXnetOp op,uint32_t thread,
                     const uint32_t *args,unsigned count,uint64_t now_ms,uint32_t *result);
int xyz_xnet_probe_begin(uint32_t thread);
void xyz_xnet_probe_tick(uint64_t now_ms);
void xyz_xnet_probe_end(void);
#ifdef __cplusplus
}
#include "xyz_lan.h"
#include <memory>
namespace xyz {
class XnetSockets {
public:
    XnetSockets(LanEndpoint &endpoint,uint32_t vip);
    ~XnetSockets();
    XnetSockets(const XnetSockets&)=delete;
    XnetSockets& operator=(const XnetSockets&)=delete;
    // Session changes invalidate every existing handle. Keep their tombstones
    // until guest Close, so a stale virtual handle never reaches retail XNet.
    void reset();
    void link(uint32_t vip,uint32_t generation);
    // Route by handle ownership, not just the current online mode: sockets
    // opened by retail code before joining must continue to use retail XNet.
    bool dispatch(const XyzXboxGuest &guest,XyzXnetOp op,uint32_t thread,
                  const uint32_t *args,unsigned count,uint64_t now_ms,
                  bool create,uint32_t &result);
    uint32_t call(const XyzXboxGuest &guest,XyzXnetOp op,uint32_t thread,
                  const uint32_t *args,unsigned count,uint64_t now_ms);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
#endif
#endif
