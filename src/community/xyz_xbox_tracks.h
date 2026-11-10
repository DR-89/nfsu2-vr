#pragma once
#include "xyz_xbox_vehicle.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {XYZ_TRACK_LIMIT=512};
typedef struct {
    uint16_t id;
    uint8_t reverse,point_to_point;
    uint32_t region,modes;
    /* Owned data's development label, not a localized player-facing name. */
    char debug_name[32];
} XyzRaceTrack;
/* Guest thread only. Copy the loaded Xbox catalog, using the original online
 * mode filter. Career unlocks do not restrict network tracks, matching the
 * upstream network-race feature. No pointer or track data is retained. */
int xyz_xbox_tracks(XyzXboxRead read,void *context,unsigned mode,
                    XyzRaceTrack *out,unsigned capacity,unsigned *count);
/* Network rule bounds: modes 0 Circuit, 1 Sprint, 2 Drag, 3 Drift,
 * 4 URL, 10 Street X. Drag retains one player per original lane. */
typedef struct {unsigned min_laps,max_laps,max_players;} XyzRaceRules;
int xyz_xbox_track_rules(unsigned mode,const XyzRaceTrack *track,XyzRaceRules *rules);
/* Copy the original game's localized TRACK_NAME / REVERSE_TRACK_NAME text.
 * Reads the relocated language table, never the development track label.
 * Missing reverse labels fall back to the normal name; direction remains a
 * separate race option. Clears out on invalid, missing or overlong text. */
int xyz_xbox_track_name(XyzXboxRead,void *,unsigned id,int reverse,char *out,size_t capacity);
#ifdef __cplusplus
}
#endif
