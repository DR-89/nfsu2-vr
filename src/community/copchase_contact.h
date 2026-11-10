#pragma once
#include "copchase_steering.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* CopChase v0.3.2 by Vitorini Mods, ASI10026d46..10026dc5. This is the
 * planner's close/closing-velocity contact estimate, not collision detection.
 * Use the same filtered player XY velocity as the steering planner. */
typedef struct {
    uint32_t last_contact_ms,elapsed_ms;
    int refreshed;
    float distance,closing_speed;
} CopChaseContactOutput;
/* Input/output timestamps are unsigned 32-bit milliseconds, including clock
 * wrap. Caller owns the per-car timestamp and may increment its diagnostic
 * contact counter when refreshed==1. The helper performs no guest IO.
 * Returns zero for invalid snapshots; preserves timestamp/elapsed output and
 * never reports a contact on failure. Output may not alias either pose.
 * The original reads GetTickCount again after this step: a caller sampling
 * another clock value should recompute elapsed from the returned timestamp. */
int copchase_contact_update(const CopChasePose *cop,const CopChasePose *player,
                            uint32_t now_ms,uint32_t last_contact_ms,
                            CopChaseContactOutput *output);
#ifdef __cplusplus
}
#endif
