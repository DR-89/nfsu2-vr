#ifndef XYZ_XBOX_REMOTE_H
#define XYZ_XBOX_REMOTE_H
#include "xyz_xbox_vehicle.h"
#ifdef __cplusplus
extern "C" {
#endif

/* No received packet contains a guest address or function. The adapter can
 * request only these audited operations, on the guest main-loop thread. */
enum XyzXboxOp {
    XYZ_G_ALLOC, XYZ_G_TYPE_HASH, XYZ_G_RIDE_INIT, XYZ_G_RIDE_STOCK,
    XYZ_G_RIDE_PARTS, XYZ_G_RIDE_SKINS, XYZ_G_PERF_SET, XYZ_G_GET_PART,
    XYZ_G_LOAD, XYZ_G_SERVICE, XYZ_G_LOADED, XYZ_G_WAIT, XYZ_G_UNLOAD,
    XYZ_G_CREATE, XYZ_G_SET_DRIVER, XYZ_G_SET_PHYS, XYZ_G_SET_RIDE,
    XYZ_G_ACTIVATE, XYZ_G_SECTION, XYZ_G_DESTROY, XYZ_G_FREE, XYZ_G_ROAD,
    XYZ_G_PACK_LOAD,XYZ_G_PACK_UNLOAD,XYZ_G_PACK_WAIT,XYZ_G_PACK_WAKE,
    XYZ_G_MODEL_FREE,XYZ_G_PART_NEXT,XYZ_G_PART_NAME,XYZ_G_POOL_READY,XYZ_G_TYPE_AVAILABLE,
    XYZ_G_TEXTURE_FIND,XYZ_G_TEXTURE_INIT,XYZ_G_PAINT_ALLOC,XYZ_G_PAINT_FREE,XYZ_G_OP_COUNT
};
typedef int (*XyzXboxWrite)(void *,uint32_t,const void *,size_t);
typedef int (*XyzXboxCall)(void *,enum XyzXboxOp,uint32_t,uint32_t,
                          unsigned,const uint32_t *,uint32_t *);
typedef struct {
    void *context;
    XyzXboxRead read;
    XyzXboxWrite write;
    XyzXboxCall call;
} XyzXboxGuest;
typedef struct { uint32_t base,size; } XyzXboxArena;
typedef struct {
    uint32_t type;
    unsigned missing_parts;
    int fallback_type;
} XyzXboxRideResult;

int xyz_xbox_owned(XyzXboxArena arena,uint32_t address,size_t size);
/* All writes stay in the supplied allocation. Call only on a ride which no
 * loader/car is currently using. Failure leaves that owned ride unusable;
 * nothing borrowed (especially a player's career ride) is written. */
int xyz_xbox_build_ride(const XyzXboxGuest *guest,XyzXboxArena arena,
                        uint32_t ride,uint32_t scratch,uint32_t skin,
                        const uint8_t *wire,int size,XyzXboxRideResult *result);

enum XyzXboxCloneStage { XYZ_CLONE_EMPTY, XYZ_CLONE_LOADING, XYZ_CLONE_READY, XYZ_CLONE_FAILED, XYZ_CLONE_LOOK_LOADING };
typedef struct {
    XyzXboxArena arena;
    uint32_t record,grid,world,car,handle;
    uint32_t skin,look_ride,look_handle,pending_ride,pending_handle;
    uint64_t started;
    float spin[4],steer;
    int visible;
    enum XyzXboxCloneStage stage;
} XyzXboxClone;
/* The arena remains alive until retire succeeds, including after a failed
 * load. Each clone owns its record; stock opponents are never lent/stolen.
 * This primitive does not publish network readiness or enter a server. */
int xyz_xbox_clone_begin(const XyzXboxGuest *guest,XyzXboxClone *clone,
                         XyzXboxArena arena,uint32_t world,uint32_t player,
                         uint32_t skin,uint64_t now);
/* Use the received location for the constructor grid. CreateCar still needs
 * a valid original physics record; configure the received appearance after
 * polling completes and keep the bootstrap hidden until then. */
int xyz_xbox_clone_begin_at(const XyzXboxGuest *guest,XyzXboxClone *clone,
                         XyzXboxArena arena,uint32_t world,uint32_t player,uint32_t skin,
                         const float position[3],uint64_t now);
enum XyzXboxCloneStage xyz_xbox_clone_poll(const XyzXboxGuest *guest,
                                           XyzXboxClone *clone,uint64_t now);
int xyz_xbox_clone_retire(const XyzXboxGuest *guest,XyzXboxClone *clone);
/* A second asynchronous load; replaces the owned car only after models are
 * ready. Requires at least 0x2400 arena bytes for two stable ride buffers. */
int xyz_xbox_clone_configure(const XyzXboxGuest *guest,XyzXboxClone *clone,
                             const uint8_t *wire,int size,uint64_t now,
                             XyzXboxRideResult *result);
/* Validated interpolated COM pose; only owned world-list cars are writable.
 * Does not enable collisions, inject controls into the player or advance AI. */
int xyz_xbox_clone_pose(const XyzXboxGuest *guest,XyzXboxClone *clone,
                        const rc_pose *pose,float dt);
int xyz_xbox_clone_hide(const XyzXboxGuest *guest,XyzXboxClone *clone);

#ifdef __cplusplus
}
#endif
#endif
