#pragma once
#include "copchase_session.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CopChaseRecordFlow CopChaseRecordFlow;
/* Serialized guest-thread handoff. Callbacks enqueue/snapshot only, never wait
 * for filesystem IO. Exposed callbacks permit deterministic backpressure QA. */
typedef struct {
    void *user;
    enum CopChaseSessionResult (*select)(void*,const char*,uint64_t);
    enum CopChaseSessionResult (*publish)(void*,const CopChaseSessionFrame*);
    void (*snapshot)(void*,CopChaseSessionStatus*);
} CopChaseRecordFlowHost;
CopChaseRecordFlow *copchase_record_flow_create(const CopChaseRecordFlowHost*);
CopChaseRecordFlow *copchase_record_flow_for_session(CopChaseSession*);
void copchase_record_flow_destroy(CopChaseRecordFlow*);
/* Forms the current identity even while returning not ready. A missing
 * profile/model cancels the previous pursuit before any later selection.
 * Return 1 only when new gameplay is permitted with the returned heat. */
int copchase_record_flow_before(CopChaseRecordFlow*,const char *profile_id,
    uint64_t profile_generation,uint64_t world_generation,uint64_t player_generation,
    const char *model_id,CopChaseSessionFrame*,CopChaseSessionStatus*);
/* Always call after the gameplay step, including when before returned zero.
 * CANCEL is bound to the START owner, not an empty/new current frame. */
void copchase_record_flow_after(CopChaseRecordFlow*,const CopChaseSessionFrame*);
enum CopChaseSessionResult copchase_record_flow_error(const CopChaseRecordFlow*);
unsigned copchase_record_flow_pending(const CopChaseRecordFlow*);
/* Recoverable readiness/queue/terminal durability hold. Feed the game guard
 * settlement_pending instead of disabling pursuit and resetting its state. */
int copchase_record_flow_holding(const CopChaseRecordFlow*);
#ifdef __cplusplus
}
#endif
