#pragma once
#include "xyz_xbox_lobby.h"
#ifdef __cplusplus
extern "C" {
#endif
enum XyzRaceOp {
    XYZ_R_NAMES,XYZ_R_BEGIN,XYZ_R_CONFIGURE,XYZ_R_PROFILE,XYZ_R_INIT,
    XYZ_R_STOP,XYZ_R_RESET,XYZ_R_RETURN,XYZ_R_RELEASE_PLAY,XYZ_R_RETURN_SCREEN,XYZ_R_OP_COUNT
};
typedef int (*XyzRaceCall)(void *,enum XyzRaceOp,uint32_t,unsigned,const uint32_t *,uint32_t *);
typedef struct {const XyzLobbyGuest *lobby;XyzRaceCall call;} XyzRaceGuest;
enum XyzRaceStage {XYZ_RACE_EMPTY,XYZ_RACE_PREPARING,XYZ_RACE_ACTIVE,XYZ_RACE_RETURNING,XYZ_RACE_CLOSED};
typedef struct {
    enum XyzRaceStage stage;
    enum XyzLobbyError error;
    uint32_t generation,players,host,previous_mode;
    uint32_t previous_screen;
    int screen_written;
    int captured,mode_written,begun,coordinator,profile_written,lobby_released,play_released;
    uint8_t previous_netgame[0x120],previous_tick,previous_profile[12],written_profile[12];
} XyzXboxRace;
/* Guest main thread only. Zero-initialize for each handoff, reusing storage
 * only after close succeeds. A real PLAY_READY roster and a connected selected
 * controller are required. Copies roster/seed, selects network race mode,
 * releases the lobby, then initializes retail NetGame. Does not select a
 * track, fabricate readiness, move cars, or mark a race as finished.
 * If preparation fails after taking ownership, close must still be called. */
int xyz_xbox_race_begin(const XyzRaceGuest *,XyzXboxRace *,XyzXboxLobby *,const XyzXboxFrame *);
/* Queue retail's return at the normal frame boundary. Never overwrite a
 * different pending transition or unload the world inside this call. */
int xyz_xbox_race_return(const XyzRaceGuest *,XyzXboxRace *,const XyzXboxFrame *);
/* Only succeeds in a stable frontend with no world/local player. Stops and
 * resets the owned coordinator before restoring the previous empty NetGame.
 * A caller must keep pumping the session/loader until this can succeed, then
 * close its lobby/session. Failed reads leave ownership available for retry. */
int xyz_xbox_race_close(const XyzRaceGuest *,XyzXboxRace *,const XyzXboxFrame *);
#ifdef __cplusplus
}
#endif
