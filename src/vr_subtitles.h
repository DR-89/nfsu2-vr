#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Startup-only, opt-in English text replacement. The original movie player
 * retains its timing, font, layout, skipping and stereo screen placement. */
int vr_subtitles_start(const char *parent,int enabled);
const uint8_t *vr_subtitles_pack(size_t *size,unsigned *count);
#ifdef __cplusplus
}
#endif
