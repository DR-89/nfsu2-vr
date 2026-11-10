#pragma once
#include "xyz_xbox_lobby.h"
#include "xyz_race_info.h"
#include "xyz_xbox_race_settings.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {XYZ_RACES_OFFLINE,XYZ_RACES_CONNECTING,XYZ_RACES_SEARCHING,XYZ_RACES_READY,XYZ_RACES_CLOSING,XYZ_RACES_ERROR,
      XYZ_RACES_JOINING,XYZ_RACES_IN_LOBBY,XYZ_RACES_STARTING,XYZ_RACES_RACING,XYZ_RACES_RETURNING};
enum {XYZ_RACES_OPEN=1,XYZ_RACES_SEARCH,XYZ_RACES_CLOSE,XYZ_RACES_JOIN,XYZ_RACES_SET_READY,XYZ_RACES_START,XYZ_RACES_LEAVE,XYZ_RACES_CREATE,XYZ_RACES_PAGE_SIZE=20,XYZ_RACES_TRACK_CHOICES=128};
typedef struct {
    unsigned mode,id,reverse,min_laps,max_laps,max_players;
    char name[64],reverse_name[64];
} XYZRaceChoice;
typedef struct {
    uint32_t epoch,start,count;
    int state,active,from_career;
    char name[16],notice[128];
    XyzLobbyGame rows[XYZ_RACES_PAGE_SIZE];
    XyzRaceInfo info[XYZ_RACES_PAGE_SIZE];
    XyzLobbySnapshot lobby;
    XyzRaceInfo race;
    int host,local_ready,startable;
    unsigned track_count;
    XYZRaceChoice tracks[XYZ_RACES_TRACK_CHOICES];
} XYZRaceBrowser;
/* Render-thread requests copy strings; guest pointers never cross threads. */
int xyz_races_open(const char *name,const char *room,const char *profile,const char *device);
int xyz_races_open_claimed(const char *name,const char *room,const char *profile,const char *device,const char *claim);
int xyz_races_search(uint32_t epoch,uint32_t start);
/* A copied selection is checked against the current page and again on the
 * guest thread. Passwords are request-only and never published to readers. */
int xyz_races_join(uint32_t epoch,unsigned index,const XyzLobbyGame *,const char *password);
int xyz_races_ready(uint32_t epoch,int ready);
int xyz_races_start(uint32_t epoch);
int xyz_races_leave(uint32_t epoch);
int xyz_races_create(uint32_t epoch,const XyzRaceSettings *);
int xyz_races_close(void);
int xyz_races_active(void);
void xyz_races_snapshot(XYZRaceBrowser *);
/* Guest-thread mailbox. Identity is private and never part of the UI snapshot. */
typedef struct {
    uint32_t epoch,start,index;int action,ready;
    char name[16],room[64],profile[33],device[33],claim[44],password[33];
    XyzLobbyGame selected;
    XyzRaceSettings options;
} XYZRaceRequest;
int xyz_races_needs_frame(void);
int xyz_races_take(XYZRaceRequest *);
void xyz_races_publish(const XYZRaceBrowser *);
#ifdef __cplusplus
}
#endif
