#pragma once
#include "copchase_session.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Before guest threads: canonicalize the configured game/save root. */
int copchase_runtime_start(void);
void copchase_runtime_stop(void);
/* Immutable until stop. External mod records, separate from original UDATA. */
const char *copchase_runtime_records_path(void);
/* Borrowed for the guest-main-thread lifetime; stop only after its join. */
CopChaseSession *copchase_runtime_session(void);
#ifdef __cplusplus
}
#endif
