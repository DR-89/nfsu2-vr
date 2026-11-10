#ifndef XYZ_XBOX_VEHICLE_H
#define XYZ_XBOX_VEHICLE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Windows headers still define the 16-bit keyword `far`; the portable core
 * uses that ordinary identifier for distance hints. Keep both headers intact. */
#ifdef _WIN32
#pragma push_macro("far")
#undef far
#endif
#include "roam_core.h"
#ifdef _WIN32
#pragma pop_macro("far")
#endif

/* US retail XBE only. All access is through the game thread's bounded reader.
 * No guest pointers, callbacks or registers may cross to the socket thread. */
typedef int (*XyzXboxRead)(void *context, uint32_t address, void *out, size_t size);
enum XyzXboxStatus {
    XYZ_XBOX_FRONTEND, XYZ_XBOX_LOADING, XYZ_XBOX_OTHER_RACE,
    XYZ_XBOX_READY, XYZ_XBOX_BAD_WORLD, XYZ_XBOX_BAD_CAR,
    XYZ_XBOX_BAD_POSE, XYZ_XBOX_BAD_RIDE
};
typedef struct {
    uint32_t world, player, car, ride, flow, track, race_type;
    enum XyzXboxStatus status;
    rc_state state;
    uint16_t config_size;
    uint8_t config[RC_CFG_MAX];
} XyzXboxFrame;

int xyz_xbox_address(uint32_t address, size_t size);
/* Host boot only, before any guest/network thread. Match the actual physical
 * window; malformed packets can never alter this bound. */
int xyz_xbox_set_physical_size(uint32_t bytes);
typedef struct {uint32_t phase,car_busy;} XyzXboxStreaming;
/* Read-only scheduling gate: the original track step waits for CarLoader.
 * New remote loads must wait for phase zero; existing loads still progress. */
int xyz_xbox_streaming(XyzXboxRead read,void *context,XyzXboxStreaming *out);
/* A bounded world-list walk, not the 22-entry racing index. The latter omits
 * unindexed cars and must never be enlarged to accommodate online players. */
uint32_t xyz_xbox_find_car(XyzXboxRead read, void *context, uint32_t world,
                          uint32_t car, uint32_t record);
/* Also used to verify a newly built, owned ride before attaching it to a car. */
int xyz_xbox_ride_config(XyzXboxRead read, void *context, uint32_t ride,
                         uint8_t *out, int capacity);
/* Independently reads an existing world-list car; useful for verifying the
 * pose actually applied by the owned-vehicle adapter. */
int xyz_xbox_car_state(XyzXboxRead read,void *context,uint32_t world,uint32_t car,
                       uint32_t time_us,rc_state *out);
enum XyzXboxStatus xyz_xbox_sample(XyzXboxRead read, void *context,
                                  uint32_t time_us, XyzXboxFrame *out);
const char *xyz_xbox_status_text(enum XyzXboxStatus status);

#ifdef __cplusplus
}
#endif
#endif
