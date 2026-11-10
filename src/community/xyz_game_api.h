#ifndef XYZ_GAME_API_H
#define XYZ_GAME_API_H
#include "xyz_xbox_remote.h"
#include "xyz_xbox_assets.h"
#include "xyz_xnet.h"
#include "xyz_stance.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { XYZ_ONLINE_OFFLINE,XYZ_ONLINE_CONNECTING,XYZ_ONLINE_GREETING,XYZ_ONLINE_QUEUED,
       XYZ_ONLINE_CONNECTED,XYZ_ONLINE_RETRY,XYZ_ONLINE_STOPPED };
typedef struct {
    int state,peers,visible,loading,failed,local_ready,lan_ready;
    unsigned queue_position,queue_total,generation;
    int hub,listed;
    char name[16],room[64],notice[128];
} XYZOnlineStatus;
/* Render-thread calls. Explicit connect is the only way to enable networking.
 * Profile IDs/device IDs stay separate from guest career data. */
int xyz_online_connect(const char *name,const char *room,const char *profile_id,const char *device_id);
int xyz_online_connect_claimed(const char *name,const char *room,const char *profile_id,const char *device_id,const char *claim);
/* Internal race-flow entry: selects LAN transport, without advertising a
 * lobby or enabling unsupported gameplay capabilities. */
int xyz_online_connect_races(const char *name,const char *room,const char *profile_id,const char *device_id);
int xyz_online_connect_races_claimed(const char *name,const char *room,const char *profile_id,const char *device_id,const char *claim);
void xyz_online_disconnect(void);
void xyz_online_snapshot(XYZOnlineStatus *out);
/* Thread-safe setting; applied by the next guest frame, no reconnect. */
void xyz_online_set_car_limit(unsigned cars);
/* Guest-thread calls. The hook must guard re-entry while guest loader calls
 * pump the main loop. No guest addresses are retained by the network worker. */
int xyz_online_needs_frame(void);
/* Guest thread only, after frame. Unlike the UI counts, remains false while
 * disconnected cars still own a loader or world reference. */
int xyz_online_fleet_retired(void);
void xyz_online_frame(const XyzXboxGuest *guest,const XyzXboxFrame *local,int paused,uint64_t now_us);
int xyz_online_xnet(const XyzXboxGuest *guest,enum XyzXnetOp op,uint32_t thread,
                    const uint32_t *args,unsigned count,uint64_t now_ms,uint32_t *result);
/* Guest Wake hook; normal game calls require the same duplicate-model repair
 * as peer-driven calls once a session holds shared pack headers. */
int xyz_online_before_wake(const XyzXboxGuest *guest,uint32_t file,XyzXboxWakePlan *plan);
void xyz_online_after_wake(const XyzXboxGuest *guest,XyzXboxWakePlan *plan);
void xyz_online_assets_probe(const XyzXboxGuest *guest,uint64_t now_us);
uint32_t xyz_online_texture(uint32_t hash);
/* Guest draw thread only; returns copied appearance for a live owned car. */
int xyz_online_stance(uint32_t car,XyzStance *out);
int xyz_online_lights(uint32_t car);
#ifdef __cplusplus
}
#endif
#endif
