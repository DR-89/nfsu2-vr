#pragma once
#include "copchase_fine.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct CopChaseBust CopChaseBust;
typedef struct {
    CopChaseFineIdentity identity;
    uint32_t world,car,director,controller;
    int flow,director_state;
    int hud,root_pause,can_pause,modal_visible;
} CopChaseBustContext;
typedef struct {
    CopChaseFineHost fine;
    /* Checked guest-thread snapshot. can_pause additionally includes native
     * cinematic/stream/network guards; root_pause means the actual active
     * root UI_Pause package with no nested options/confirmation package. */
    int (*context)(void*,CopChaseBustContext*);
} CopChaseBustHost;
enum CopChaseBustPhase {
    COPCHASE_BUST_IDLE,COPCHASE_BUST_WAIT_SAFE,COPCHASE_BUST_WAIT_PAUSE,
    COPCHASE_BUST_READY,COPCHASE_BUST_PAID,COPCHASE_BUST_WAIT_RESUME,
    COPCHASE_BUST_DONE,COPCHASE_BUST_FAILED
};
enum CopChaseBustError {
    COPCHASE_BUST_OK,COPCHASE_BUST_INVALID,COPCHASE_BUST_DUPLICATE,
    COPCHASE_BUST_BUSY,COPCHASE_BUST_STALE,COPCHASE_BUST_TIMEOUT,
    COPCHASE_BUST_NATIVE_IO,COPCHASE_BUST_CONTEXT_LOST,
    COPCHASE_BUST_PAYMENT_IO,COPCHASE_BUST_PAYMENT_UNCERTAIN
};
typedef struct {
    uint64_t token_high,token_low;
    enum CopChaseBustPhase phase;
    enum CopChaseBustError error;
    enum CopChaseFineResult payment_error;
    uint32_t fine,balance,charged;
    int pending,ready,paid,balance_valid,owned_pause;
    int failure_visible,failure_dismissable;
} CopChaseBustStatus;

/* All methods are guest/main-thread operations; no internal UI or locks.
 * The host copies snapshots into its thread-safe VR presentation bridge.
 * Tokens come from the durable session namespace/sequence. Never recreate
 * this object to retry an uncertain charge during the same host session. */
CopChaseBust *copchase_bust_create(const CopChaseBustHost*);
void copchase_bust_destroy(CopChaseBust*);
enum CopChaseBustError copchase_bust_arm(CopChaseBust*,uint64_t token_high,
    uint64_t token_low,uint32_t infractions,uint64_t now_ms);
void copchase_bust_step(CopChaseBust*,uint64_t now_ms);
/* Only an explicit current-token VR confirmation may call this. It charges
 * once while the same owned root pause remains observed. Resume is separate:
 * step waits until context.modal_visible==0, then uses ordinary Pause Resume. */
enum CopChaseBustError copchase_bust_confirm(CopChaseBust*,uint64_t token_high,
    uint64_t token_low,uint64_t now_ms);
/* Explicit failure acknowledgement only. Never charges or sends Resume;
 * leaves the same ordinary root pause under the user's control. An uncertain
 * payment remains permanently latched even after its notice is dismissed. */
enum CopChaseBustError copchase_bust_dismiss(CopChaseBust*,uint64_t token_high,
    uint64_t token_low,uint64_t now_ms);
void copchase_bust_snapshot(const CopChaseBust*,CopChaseBustStatus*);
#ifdef __cplusplus
}
#endif
