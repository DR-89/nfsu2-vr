#pragma once
#ifdef __cplusplus
extern "C" {
#endif
enum { XYZ_PROFILE_CREATE = 1, XYZ_PROFILE_SELECT, XYZ_PROFILE_RENAME, XYZ_PROFILE_ROOM, XYZ_PROFILE_DELETE,
       XYZ_PROFILE_CONNECT,XYZ_PROFILE_DISCONNECT,XYZ_PROFILE_RACES,XYZ_PROFILE_AUTH,
       XYZ_PROFILE_WEBSITE }; /* WEBSITE is handled by the native UI host. */
typedef struct {
    int count, selected;
    char name[8][16], room[8][64];
    char error[128];
    int auth_busy[8],auth_linked[8];
    char auth_notice[128];
} XYZProfileList;
/* Render-thread API. Files are separate from all game careers and settings. */
void xyz_profiles_init(const char *directory_utf8);
void xyz_profiles_snapshot(XYZProfileList *out);
int xyz_profiles_action(int action, int index, const char *text);
#ifdef __cplusplus
}
#endif
