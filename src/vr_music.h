#pragma once
#include <stdint.h>
#include "quest_radio_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/* parent is a UTF-8 filesystem path. Owns the optional Music subdirectory. */
void vr_music_start(const char *parent);
void vr_music_stop(void);
void vr_music_set_source(int source);
/* The guest owns the EA music voice. QUIET keeps it muted outside playback
 * (e.g. a menu) while a playable custom library is selected exclusively. */
typedef enum { VR_RADIO_ORIGINAL, VR_RADIO_CUSTOM, VR_RADIO_QUIET } VrRadioPlayback;
VrRadioPlayback vr_music_update(int driving,int can_mix,float volume,QuestRadioSong *song,int *paused);
void vr_music_command(int action);
int vr_music_mix(const int16_t *game,int16_t *out,int samples);
enum { VR_VIDEO_WIDTH=320, VR_VIDEO_HEIGHT=180, VR_VIDEO_BYTES=VR_VIDEO_WIDTH*VR_VIDEO_HEIGHT*4 };
/* Returns zero when no video frame is due. With pixels != NULL, copies the
 * current RGBA frame into VR_VIDEO_BYTES of caller-owned storage. */
uint64_t vr_music_video_frame(uint8_t *pixels);
/* EA software mixer voice addresses for the playing song. A zero right
 * voice denotes mono. Pass -1,0,0 on pause, stop or source change. */
void vr_music_stock_track(int track,uint32_t left,uint32_t right);
void vr_music_stock_samples(uint32_t voice,const float *mono,unsigned frames);
/* Render thread only. Both eyes share the spectrum and its beat envelope. */
float vr_music_spectrum(float levels[QUEST_RADIO_BANDS]);
#ifdef __cplusplus
}
#endif
