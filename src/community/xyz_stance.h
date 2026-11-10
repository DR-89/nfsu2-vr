#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { int16_t fc,rc,fo,ro,tw; } XyzStance;
typedef struct {
    uint32_t type,revision;
    int enabled,custom,available,save_failed;
    char name[32];
    XyzStance value;
} XyzStanceView;
int xyz_stance_valid(const XyzStance *value);
void xyz_stance_clamp(XyzStance *value);
/* Host preferences, separate from game saves. Choices are per vehicle model. */
void xyz_stance_init(const char *directory);
void xyz_stance_snapshot(XyzStanceView *out);
/* Render thread: the expected model prevents an edit crossing a car change.
 * Rows: 0 enable, 1 this model, 2..6 values, 7 reset. Persist before publishing. */
int xyz_stance_edit(uint32_t expected_type,int row,int step);
/* Guest thread: copy only, no guest pointers retained by the preference store. */
int xyz_stance_publish(uint32_t type,const char *name,int stock_camber,XyzStance *out);
int xyz_stance_enabled(void);
#ifdef __cplusplus
}
#endif
