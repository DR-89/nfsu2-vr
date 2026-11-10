#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Startup, before the game and audio threads: MODPACKS/WHINE below parent. */
int vr_whine_start(const char *parent);
void vr_whine_enable(int enabled);
int vr_whine_enabled(void);
int vr_whine_available(void);
/* Guest thread. Engine telemetry is copied; no guest pointers reach audio. */
void vr_whine_update(uint32_t car,int running,float speed,int gear,float volume,float dt);
/* Audio thread, stereo PCM. Returns 0 without touching out when silent. */
int vr_whine_mix(const int16_t *game,int16_t *out,int samples,unsigned rate);
#ifdef __cplusplus
}
#endif
