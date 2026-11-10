#pragma once
#include "xyz_xbox_remote.h"
#ifdef __cplusplus
extern "C" {
#endif
enum XyzCareerStage {XYZ_CAREER_IDLE,XYZ_CAREER_DRAINING,XYZ_CAREER_UNLOADING,XYZ_CAREER_BROWSER,XYZ_CAREER_RESUMING};
enum XyzCareerOp {XYZ_C_QUIT,XYZ_C_MAIN,XYZ_C_EXPLORE};
typedef int (*XyzCareerCall)(void *,enum XyzCareerOp);
typedef struct {const XyzXboxGuest *memory;XyzCareerCall call;} XyzCareerGuest;
typedef struct {
    enum XyzCareerStage stage;
    uint32_t world,car,controller,slot;
    uint8_t position[18],position_captured,main_requested;
} XyzXboxCareer;
/* Only an explicitly requested, paused career free roam may transfer. No save
 * files are written. Original Pause::Quit stores position inside the career. */
int xyz_xbox_career_begin(const XyzCareerGuest *,XyzXboxCareer *,const XyzXboxFrame *,int paused);
int xyz_xbox_career_step(const XyzCareerGuest *,XyzXboxCareer *,const XyzXboxFrame *,int paused,int main_menu,int retired);
/* Returns 1 after cancellation or completed original Explore load; 0 while
 * waiting. Never unloads live remote cars or overwrites a different career. */
int xyz_xbox_career_resume(const XyzCareerGuest *,XyzXboxCareer *,const XyzXboxFrame *,int main_menu);
#ifdef __cplusplus
}
#endif
