#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Optional locally prepared <parent>/COPCHASE mixed8 asset overlay.
 * Call only before guest threads, after vr_soundtrack_start. requested is the
 * startup VR setting. A diagnostic additionally requires the exact probe flag
 * and a nonempty pad script; diagnostic pack variants are never normal mounts.
 * No pursuit behavior is enabled by mounting these resources. */
int copchase_assets_start(const char *parent, int requested);
int copchase_assets_ready(void);
/* Absolute UTF-8 paths for the two exact CD archive names and the isolated
 * Partition1/CACHE tree. NULL means unclaimed; an empty string rejects an
 * unsafe claimed cache path. Cache results last until this thread's next
 * cache redirect; the path layer copies them immediately. */
const char *copchase_assets_redirect(const char *path);
#ifdef __cplusplus
}
#endif
