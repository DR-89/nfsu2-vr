#pragma once
#include "xyz_xbox_remote.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Audited retail operations, never addresses supplied by a network packet. */
enum XyzLobbyOp {
    XYZ_L_MANAGER,XYZ_L_INIT,XYZ_L_DROP,XYZ_L_UPDATE,XYZ_L_GAMES,
    XYZ_L_CONNECT,XYZ_L_REQUEST,XYZ_L_SUBSCRIBE,XYZ_L_FIND,XYZ_L_STRING,
    XYZ_L_JOIN,XYZ_L_READY,XYZ_L_LEAVE,XYZ_L_CREATE,XYZ_L_SOCKET_IDLE,XYZ_L_START,XYZ_L_SEARCH,XYZ_L_RELEASE_PLAY,XYZ_L_OP_COUNT
};
typedef int (*XyzLobbyCall)(void *,enum XyzLobbyOp,uint32_t,unsigned,const uint32_t *,uint32_t *);
typedef struct {const XyzXboxGuest *memory;XyzLobbyCall call;} XyzLobbyGuest;
enum XyzLobbyStage { XYZ_LOBBY_EMPTY,XYZ_LOBBY_CONNECTING,XYZ_LOBBY_AUTH,
    XYZ_LOBBY_PERSONA,XYZ_LOBBY_READY,XYZ_LOBBY_JOINING,XYZ_LOBBY_IN_GAME,
    XYZ_LOBBY_LEAVING,XYZ_LOBBY_FAILED,XYZ_LOBBY_CREATING,XYZ_LOBBY_STARTING,XYZ_LOBBY_PLAY_READY,XYZ_LOBBY_RACE,XYZ_LOBBY_SEARCHING };
enum XyzLobbyError { XYZ_LOBBY_OK,XYZ_LOBBY_CONTEXT,XYZ_LOBBY_MEMORY,
    XYZ_LOBBY_RETAIL,XYZ_LOBBY_SESSION,XYZ_LOBBY_TIMEOUT,XYZ_LOBBY_REJECTED };
typedef struct {
    uint32_t manager,games,protocol,arena,generation,callback;
    uint32_t reply_kind,reply_code,waiting_kind;
    uint32_t race_host;
    uint32_t search_start,search_count;
    int32_t join_game_id;
    uint64_t deadline;
    enum XyzLobbyStage stage;
    enum XyzLobbyError error;
    int initialized,names_saved,names_written,event_error,play_received;
    int controller_saved;
    uint32_t previous_controller,written_controller;
    char name[16],previous_names[4][16],written_names[4][16],reply[8193];
} XyzXboxLobby;
typedef struct {
    enum XyzLobbyStage stage;enum XyzLobbyError error;uint32_t server_error;
    int32_t game_id;unsigned players,listed_games;
    char persona[16],game_name[36],player[8][16];
    uint32_t player_flags[8];
} XyzLobbySnapshot;
/* A copied browser row, with no guest pointers or authentication fields.
 * PARAMS and flags retain the original game's representation. */
typedef struct {
    int32_t game_id;
    uint32_t players,min_players,max_players,custom_flags,system_flags,seed;
    char name[36],host[16],params[132];
} XyzLobbyGame;
/* Guest thread only. Begin requires the normal frontend and an established
 * LAN session. This binds the REAL game managers, not a second lobby object.
 * Integration with the race start/return flow is still required before UI
 * may expose joining arbitrary public games. */
int xyz_xbox_lobby_begin(const XyzLobbyGuest *,XyzXboxLobby *,const XyzXboxFrame *,
                         const char *name,uint32_t generation,uint32_t callback,uint64_t now);
void xyz_xbox_lobby_tick(const XyzLobbyGuest *,XyzXboxLobby *,uint32_t generation,uint64_t now);
void xyz_xbox_lobby_close(const XyzLobbyGuest *,XyzXboxLobby *);
int xyz_xbox_lobby_owns_callback(const XyzXboxLobby *,uint32_t protocol,uint32_t context);
void xyz_xbox_lobby_event(const XyzLobbyGuest *,XyzXboxLobby *,uint32_t message);
int xyz_xbox_lobby_join(const XyzLobbyGuest *,XyzXboxLobby *,const char *game,const char *password,uint64_t now);
/* Uses the engine's current network race settings at 0x41c340, including
 * privacy/password and track/laps. Performance class is 0..3. The original
 * game generates the lobby name, available in the snapshot after success. */
int xyz_xbox_lobby_create(const XyzLobbyGuest *,XyzXboxLobby *,unsigned performance_class,uint64_t now);
int xyz_xbox_lobby_ready(const XyzLobbyGuest *,XyzXboxLobby *,int ready);
/* The retail Ready event records the accepting logical pad at 0x406c98.
 * Require its original device mapping (0x17b0e0) to be connected; USB port
 * numbers are not logical pad indices. Preserve the selection through race
 * setup and restore it conditionally on close. */
int xyz_xbox_lobby_select_controller(const XyzLobbyGuest *,XyzXboxLobby *,unsigned port);
/* Host only; every other player must be ready. Never kicks an unready player.
 * PLAY_READY means the original play handler populated the race connections;
 * the game flow must still initialize/drive the retail race coordinator. */
int xyz_xbox_lobby_start(const XyzLobbyGuest *,XyzXboxLobby *,uint64_t now);
/* After copying race settings/roster into NetGame, release the original lobby
 * before initializing the race, as retail does. The assigned persona and
 * validated host remain owned until close; tick keeps CommUDP alive. */
int xyz_xbox_lobby_enter_race(const XyzLobbyGuest *,XyzXboxLobby *);
int xyz_xbox_lobby_leave(const XyzLobbyGuest *,XyzXboxLobby *,uint64_t now);
int xyz_xbox_lobby_snapshot(const XyzLobbyGuest *,const XyzXboxLobby *,XyzLobbySnapshot *);
/* Read one displayed row only while the owned lobby is ready. The caller
 * must copy it again before a later selection; list indices are not stable.
 * Failure clears out, and neither a read nor an invalid row joins a game. */
int xyz_xbox_lobby_game(const XyzLobbyGuest *,const XyzXboxLobby *,unsigned index,XyzLobbyGame *out);
/* Copy the room actually joined, including any subsequent host option change. */
int xyz_xbox_lobby_current_game(const XyzLobbyGuest *,const XyzXboxLobby *,XyzLobbyGame *out);
/* Replace the current list using retail search/list ownership. Count is 1..200;
 * start/count must fit the server's signed 32-bit range. SEARCHING disables
 * row reads and joining until the response and all records have arrived. */
int xyz_xbox_lobby_search(const XyzLobbyGuest *,XyzXboxLobby *,unsigned start,unsigned count,uint64_t now);
/* Re-read the selected row and reject a changed identity, full/started game,
 * or an unsupported player count. Confirm the returned identity before IN_GAME. */
int xyz_xbox_lobby_join_game(const XyzLobbyGuest *,XyzXboxLobby *,unsigned index,const XyzLobbyGame *selected,const char *password,uint64_t now);
/* Copy a service play notification into owned scratch memory. The Xbox game
 * connects using the MADDR string, whereas the PC service supplies ADDR and
 * leaves MADDR empty. The returned descriptor is valid until the next guest
 * operation; use synchronously and never mutate the original notification. */
int xyz_xbox_lobby_prepare_play(const XyzLobbyGuest *,XyzXboxLobby *,uint32_t message,uint32_t *prepared);
/* The retail guest coordinator normally resolves an Xbox Live machine ID.
 * An owned virtual-LAN play roster already supplies the host's IPv4 address.
 * Complete only its exact client resolver/slot/port; other resolvers stay retail. */
int xyz_xbox_lobby_resolve_virtual(const XyzLobbyGuest *,XyzXboxLobby *,uint32_t resolver,unsigned slot,uint32_t port);
#ifdef __cplusplus
}
#endif
