#pragma once
#include "xyz_xbox_lobby.h"
#include "xyz_xbox_tracks.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    unsigned mode,track,laps,min_players,max_players,reverse;
    unsigned collision,catchup,ranked,private_game,unrestricted,performance;
    /* Retail's password and confirmation fields each hold twelve characters. */
    char password[13];
} XyzRaceSettings;
typedef struct {
    uint32_t generation,protocol;
    unsigned captured,remaining;
    uint8_t previous[0x4c],written[0x4c];
} XyzRaceSettingsLease;
/* Validate against the currently loaded, original network track catalog and
 * lane/lap/direction limits. No writes or retail calls. */
int xyz_xbox_race_settings_valid(const XyzLobbyGuest *,const XyzRaceSettings *);
/* Guest thread only, while the owned lobby is idle in the stable frontend.
 * Preserve every unrelated profile byte. Reuse requires successful release.
 * The caller still invokes the original Create with options.performance. */
int xyz_xbox_race_settings_apply(const XyzLobbyGuest *,const XyzXboxLobby *,
    XyzRaceSettingsLease *,const XyzXboxFrame *,const XyzRaceSettings *);
/* Reserve the seven fields written by the original successful Join callback
 * (0x196410). Does not apply options: retail still decodes the server reply. */
int xyz_xbox_race_settings_join(const XyzLobbyGuest *,const XyzXboxLobby *,
    XyzRaceSettingsLease *,const XyzXboxFrame *,const XyzRaceSettings *);
/* Restore each still-owned field independently, preserving later changes by
 * the game or another owner. A failed read/write retains that field for retry.
 * Call after race teardown and before opening another lobby. No save I/O. */
int xyz_xbox_race_settings_release(const XyzLobbyGuest *,XyzRaceSettingsLease *,const XyzXboxFrame *);
#ifdef __cplusplus
}
#endif
