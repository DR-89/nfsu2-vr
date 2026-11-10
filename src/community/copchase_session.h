#pragma once
#include "copchase_records.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct CopChaseSession CopChaseSession;
enum CopChaseSessionResult { COPCHASE_SESSION_OK,COPCHASE_SESSION_DUPLICATE,
    COPCHASE_SESSION_INVALID,COPCHASE_SESSION_STALE,COPCHASE_SESSION_FULL,
    COPCHASE_SESSION_IO,COPCHASE_SESSION_STOPPED };
typedef struct {
    uint64_t profile_generation,world_generation,player_generation;
    char model_id[65];
    uint32_t phase,events;
    /* Worker replaces session IDs/event/car_id. The other settlement fields
     * must already be validated measurements, not guessed incident counts. */
    CopChaseSettlement result;
} CopChaseSessionFrame;
typedef struct {
    uint64_t profile_generation,session_high,session_low;
    char profile_id[65],model_id[65];
    float retained_heat;
    int heat_ready,ready,active,blocked;
    unsigned queued;
    enum CopChaseSessionResult error;
    enum CopChaseRecordResult record_error;
    CopChaseRecordsStatus records;
} CopChaseSessionStatus;
/* Starts worker. No records filesystem IO happens on the caller. Root must
 * be absolute. Worker owns a root-wide sequence lock and records handles. */
CopChaseSession *copchase_session_start(const char *absolute_records_root);
/* IDs must come from verified load provenance. NULL/empty clears identity;
 * generations distinguish reloads of the same profile. Selection cancels any
 * unfinished pursuit; queued completed settlements retain their old profile. */
enum CopChaseSessionResult copchase_session_select(CopChaseSession*,const char *profile_id,uint64_t generation);
/* Ordered bounded queue. On FULL caller must retry the SAME event. A duplicate
 * START/terminal does not allocate or settle twice. Identity changes cancel
 * unfinished pursuit, never transfer it to another world/profile/model. */
enum CopChaseSessionResult copchase_session_publish(CopChaseSession*,const CopChaseSessionFrame*);
void copchase_session_snapshot(CopChaseSession*,CopChaseSessionStatus*);
/* Host IO/test thread only: wait for accepted commands or first IO failure. */
enum CopChaseSessionResult copchase_session_wait(CopChaseSession*,unsigned timeout_ms);
/* Explicitly retry a failed command. A failed flush retains its exact token
 * and payload; it cannot duplicate bounty. Queueing still bounded when blocked. */
void copchase_session_retry(CopChaseSession*);
/* Drain once, join, close. Returns IO when pending accepted work could not be
 * committed. Call wait/retry BEFORE stop if the host can recover the error.
 * No concurrent calls may race stop. This destroys the session. */
enum CopChaseSessionResult copchase_session_stop(CopChaseSession*);
#ifdef __cplusplus
}
#endif
