#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Startup before guest/render threads. Original files and guest dimensions
 * remain unchanged. Lookup and its scratch storage belong to Vulkan only. */
int vr_texpacks_start(const char *parent,int hud,int detailed);
int vr_texpacks_shape(unsigned w,unsigned h,unsigned format);
typedef struct {unsigned w,h,format;size_t bytes;const void *data;} VRTextureReplacement;
int vr_texpacks_find(const void *level,size_t bytes,unsigned w,unsigned h,unsigned format,int bc,VRTextureReplacement *out);
typedef struct {uint16_t track,offset;float value;} VRTrackMapEdit;
const VRTrackMapEdit *vr_texpacks_map_edits(unsigned *count);
#ifdef __cplusplus
}
#endif
