#ifndef XYZ_XBOX_ASSETS_H
#define XYZ_XBOX_ASSETS_H
#include "xyz_xbox_remote.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { XYZ_PACK_CAP=48,XYZ_PACK_PATH=96,XYZ_HIBER_CAP=256 };
typedef struct { unsigned count;char path[XYZ_PACK_CAP][XYZ_PACK_PATH]; } XyzXboxPackCatalog;
/* Bounded reads of the game's own catalogue/part names. Network-provided
 * strings are never interpreted as filenames or passed to a guest loader. */
int xyz_xbox_pack_catalog(const XyzXboxGuest *guest,XyzXboxPackCatalog *catalog);
/* -1 invalid list, 0 absent, 1 found. A read-only search, unlike PackFind's
 * mutation of the streamer's LRU list. */
int xyz_xbox_pack_find(const XyzXboxGuest *guest,const char *path,uint32_t *pack);
int xyz_xbox_pack_idle(const XyzXboxGuest *guest,uint32_t pack);

enum { XYZ_WAKE_FREE=1,XYZ_WAKE_KEEP,XYZ_WAKE_HIDE };
typedef struct {
    uint32_t name,entry,hiber,solid,temporary;
    uint8_t original[24],hiber_original[24];
    uint16_t loads;
    uint8_t kind;
} XyzXboxWakeFix;
typedef struct {
    uint32_t pack,table,solids,hiber_table,hiber_solids,hiber_count;
    unsigned count,matches;
    int begun;
    XyzXboxWakeFix fix[XYZ_HIBER_CAP];
} XyzXboxWakePlan;
/* The original Wake copies HIBERNATING over a pack entry, losing any second
 * copy and its references. Prepare validates a complete repair before writes;
 * begin transfers drawing cars through ModelFree or protects an unsafe entry;
 * finish preserves counts/entries after the ORIGINAL guest Wake has run.
 * The caller must keep the guest main loop non-reentrant across this sequence.
 * 1 prepared, 0 busy, -1 invalid. No calls/writes during prepare. */
int xyz_xbox_wake_prepare(const XyzXboxGuest *guest,uint32_t pack,XyzXboxWakePlan *plan);
int xyz_xbox_wake_begin(const XyzXboxGuest *guest,XyzXboxWakePlan *plan);
int xyz_xbox_wake_finish(const XyzXboxGuest *guest,XyzXboxWakePlan *plan);
#ifdef __cplusplus
}
#endif
#endif
