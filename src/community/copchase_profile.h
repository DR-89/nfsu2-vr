#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Read-only provenance for the exact buffer accepted by the native profile
 * decoder. No payload pointer is dereferenced and no filesystem is accessed.
 * The host supplies a canonical absolute title root and actual opened-file
 * paths (GetFinalPathNameByHandle/realpath), not user input or display names. */
enum { COPCHASE_PROFILE_SEGMENTS = 64, COPCHASE_PROFILE_MAX_AGE_MS = 30000 };
typedef struct {
    char id[65]; /* title + 12-hex UID + 128-bit normalized filename digest */
    char container_uid[13]; /* Separately exposed; ID stability excludes rename. */
    uint64_t file_offset;
    size_t bytes;
} CopChaseProfileProof;
typedef struct CopChaseProfileTracker CopChaseProfileTracker;

/* Explicit-clock pure tracker API. Caller serializes access. Startup accepts
 * exactly an absolute .../UDATA/4541005a root, with Windows long-path forms.
 * An invalid root returns NULL. No canonicalization or I/O is performed. */
CopChaseProfileTracker *copchase_profile_tracker_create(const char *canonical_title_root);
void copchase_profile_tracker_destroy(CopChaseProfileTracker *);
void copchase_profile_tracker_reset(CopChaseProfileTracker *);
void copchase_profile_tracker_observe(CopChaseProfileTracker *, void *handle,
    const char *resolved_utf8_path, void *buffer, size_t bytes, int64_t offset, uint64_t now_ms);
/* Consumes all intersecting provenance on success OR failure. Only complete
 * same-file/same-handle coverage with contiguous file offsets is accepted.
 * SaveMeta.xbx/SaveImage.xbx and ambiguous paths are never profile sources. */
int copchase_profile_tracker_claim(CopChaseProfileTracker *, const void *buffer,
    size_t bytes, uint64_t now_ms, CopChaseProfileProof *proof);

/* Thread-safe default tracker for kernel/native hooks. Startup/reset are
 * called outside the read observer. A claim is only a candidate identity:
 * the host commits it after its independently verified decoder succeeds. */
int copchase_profile_start(const char *canonical_title_root);
void copchase_profile_reset(void);
void copchase_profile_stop(void);
void copchase_profile_observe(void *handle, const char *resolved_utf8_path,
    void *buffer, size_t bytes, int64_t offset);
int copchase_profile_claim(const void *buffer, size_t bytes, CopChaseProfileProof *proof);
#ifdef __cplusplus
}
#endif
