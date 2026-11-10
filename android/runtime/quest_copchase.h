#pragma once
#include <stdint.h>
typedef struct {
    int phase,heat,cops,notice; /* notice: 1 escaped, 2 busted */
    float busted,cooldown;
    uint32_t bounty,seconds;
} QuestCopChaseHUD;
typedef struct {
    uint64_t token_high,token_low;
    uint32_t fine,charged,bounty,seconds,infractions;
    int active,ready,paid,error;
    int failed,uncertain; /* Failure A acknowledges only; never pay or resume. */
} QuestCopChaseBust;
typedef struct {
    int active,enabled;
    float position[3],forward[2],left[2],half_width;
} QuestCopChaseScene;
