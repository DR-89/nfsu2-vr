#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Validate a prepared optional SONGS pack before any guest threads start.
 * Returns 1 only when the complete pack passes SHA-256. Never edits disc data. */
int vr_soundtrack_start(const char *parent,int enabled);
/* Kernel path hook. Immutable UTF-8 absolute filenames, or NULL for retail. */
const char *vr_soundtrack_redirect(const char *xbox_path);
#ifdef __cplusplus
}
#endif
