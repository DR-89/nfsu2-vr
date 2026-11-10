#pragma once
#include "xyz_xbox_remote.h"
#include "xyz_stance.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t render,wheels,ride;
    float places[8],widths[4],rear[2][12];
    int active;
    XyzStance value;
} XyzStanceDraw;
/* Xbox USA 20BC50: ECX=render info, argument 6=wheel matrices.
 * 20E6EF/20E784 use +420 widths/+400 places. Front camber's 1A4EE0
 * return is overridden only inside this owned draw. No physics writes. */
int xyz_xbox_stance_begin(const XyzXboxGuest *g,uint32_t render,uint32_t wheels,const XyzStance *v,XyzStanceDraw *saved);
void xyz_xbox_stance_end(const XyzXboxGuest *g,XyzStanceDraw *saved);
#ifdef __cplusplus
}
#endif
