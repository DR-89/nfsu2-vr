#pragma once
#include "copchase_state.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* External mod records for the native adaptation of CopChase v0.3.2 by
 * Vitorini Mods. NEVER opens or modifies an original game save or game cash.
 * Profile/car IDs MUST be independently verified stable host identities.
 * Do not substitute a transient guest pointer, display name, or online name.
 * IDs are 1..64 ASCII letters/digits/_/-. Root is an absolute mod-data path. */
typedef struct CopChaseRecords CopChaseRecords;
enum CopChaseRecordResult {
    COPCHASE_RECORD_OK,COPCHASE_RECORD_DUPLICATE,COPCHASE_RECORD_CONFLICT,
    COPCHASE_RECORD_INVALID,COPCHASE_RECORD_FULL,COPCHASE_RECORD_IO,
    COPCHASE_RECORD_CORRUPT,COPCHASE_RECORD_LOCKED
};
typedef struct {
    uint64_t session_high,session_low; /* unique, durable 128-bit pursuit ID */
    uint32_t event,preset,heat;        /* exactly EVADED or BUSTED; heat1..5 */
    float fractional_heat;            /* current raw heat, before settlement */
    uint32_t bounty,infractions,cops_damaged,cops_taken_out,cost_to_state;
    uint64_t elapsed_ms;
    int keep_heat;
    char car_id[65];
} CopChaseSettlement;
typedef struct {
    uint64_t total_bounty,durable_revision;
    uint32_t pursuits,escapes,busts;
    int dirty;
    CopChaseSettlement last;
    float last_retained_heat;
    /* Quote only. No charge, refund or cash award is performed. */
    uint32_t last_fine_quote;
} CopChaseRecordsStatus;
/* Synchronous filesystem work: call on a host IO worker, never audio/render
 * callbacks. Holds an OS lock until close; a second writer returns LOCKED. */
CopChaseRecords *copchase_records_open(const char *absolute_mod_directory,
                                      const char *verified_profile_id,
                                      enum CopChaseRecordResult *result);
/* Pure in-memory update, safe to call on the main thread. Exactly-once IDs
 * survive flush/reopen. Repeating the same ID with different data conflicts.
 * Bounty increases only after EVADED, never BUSTED or cancellation. */
enum CopChaseRecordResult copchase_records_apply(CopChaseRecords*,const CopChaseSettlement*);
/* Explicit synchronous durable commit on the host IO worker. Atomic replace
 * of this module's own file. Failure leaves dirty=true; an error after rename
 * may have installed the complete new version but not confirmed directory
 * durability. Reopen/retry is idempotent. Concurrent apply goes in next flush. */
enum CopChaseRecordResult copchase_records_flush(CopChaseRecords*);
void copchase_records_snapshot(CopChaseRecords*,CopChaseRecordsStatus*);
/* Unrecorded car returns heat1. Returns0 for an invalid request. */
int copchase_records_heat(CopChaseRecords*,const char *verified_car_id,float *heat);
/* No implicit flush: caller handles flush errors before close. No calls may
 * race close, including a still-running flush. */
void copchase_records_close(CopChaseRecords*);
#ifdef __cplusplus
}
#endif
