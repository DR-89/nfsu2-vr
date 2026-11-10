#pragma once
#include "xyz_xbox_lobby.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    unsigned parsed,compatible,mode,track,laps,reverse;
    char track_name[64];
} XyzRaceInfo;
/* Decode a copied service row, then resolve the local game's track/rule/name
 * data. No joins, writes or retail callbacks. A parsed row can still describe
 * a track or mode the local game cannot race; compatible reports that apart. */
int xyz_xbox_race_info(XyzXboxRead,void *,const XyzLobbyGame *,XyzRaceInfo *);
#ifdef __cplusplus
}
#endif
