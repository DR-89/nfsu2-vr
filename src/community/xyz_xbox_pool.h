#ifndef XYZ_XBOX_POOL_H
#define XYZ_XBOX_POOL_H
#include "xyz_xbox_vehicle.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t pointer,size,used,free,largest,rides,capacity,extra,extra_size;
} XyzXboxPool;
/* Validates both the pool's accounting and its complete bounded free list.
 * The size/used offsets differ from the PC executable. */
int xyz_xbox_pool_read(XyzXboxRead read,void *context,XyzXboxPool *out);
/* General heap (0) or Cars (4); same checked accounting, no ride metadata. */
int xyz_xbox_heap_read(XyzXboxRead read,void *context,unsigned slot,XyzXboxPool *out);
/* The retail remove operation accepts only one exact free block. Even a
 * larger coalesced free block must stay: retail would silently fail to unlink
 * it, then free its backing memory while the pool still points into it. */
int xyz_xbox_pool_extra_free(XyzXboxRead read,void *context,const XyzXboxPool *pool);
#ifdef __cplusplus
}
#endif
#endif
