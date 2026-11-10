#pragma once
#include <stddef.h>
#include <stdint.h>
#include "vr_texpacks.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Startup before guest/render threads; packs are optional. */
int vr_vinyls_start(const char *parent,int enabled);
typedef int (*VRVinylRead)(void *,uint32_t,void *,size_t);
/* Original Xbox paint call runs between begin and finish. No guest writes. */
void *vr_vinyls_begin(VRVinylRead read,void *context,uint32_t recipe);
void vr_vinyls_finish(void *job,VRVinylRead read,void *context,int success);
/* Vulkan thread. A revision changes even when the original 512 skin did not. */
uint64_t vr_vinyls_revision(uint32_t physical);
int vr_vinyls_find(uint32_t physical,const uint32_t *original,size_t pixels,VRTextureReplacement *out);
#ifdef __cplusplus
}
#endif
