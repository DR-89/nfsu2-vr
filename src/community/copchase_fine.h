#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Original v0.3.2 cash confirmation, adapted to the verified Xbox money
 * object405364/get111C50/add111C40. Host must call only after explicit payment
 * confirmation on the main game thread. This helper never accesses saves. */
typedef struct { uint64_t profile,world,player; int valid,offline,career; } CopChaseFineIdentity;
typedef struct {
    void *user;
    int (*identity)(void*,CopChaseFineIdentity*);
    int (*read32)(void*,uint32_t,uint32_t*);
    /* Return value means the call completed. It is distinct from EAX. */
    int (*call)(void*,uint32_t fn,uint32_t self,unsigned argc,const uint32_t*,uint32_t *eax);
} CopChaseFineHost;
enum CopChaseFineResult { COPCHASE_FINE_OK,COPCHASE_FINE_DUPLICATE,COPCHASE_FINE_INVALID,
    COPCHASE_FINE_STALE,COPCHASE_FINE_IO,COPCHASE_FINE_UNCERTAIN };
typedef struct {
    uint64_t token_high,token_low;
    CopChaseFineIdentity identity;
    uint32_t fine,charged,before,after;
    int armed,attempted,paid,abandoned;
} CopChaseFine;
/* One pending modal at a time. Tokens use the session worker's fixed durable
 * namespace(high) and increasing sequence(low). Old tokens cannot be rearmed;
 * an uncertain native call permanently latches this pending modal. Keep this
 * object for the host session; do not reset it to retry an uncertain charge. */
enum CopChaseFineResult copchase_fine_arm(CopChaseFine*,const CopChaseFineHost*,
    uint64_t token_high,uint64_t token_low,uint32_t infractions);
enum CopChaseFineResult copchase_fine_confirm(CopChaseFine*,const CopChaseFineHost*);
/* Context/modal cancelled BEFORE any mutating native call. Keeps the token
 * watermark so the abandoned event cannot be rearmed. Never clears uncertainty. */
enum CopChaseFineResult copchase_fine_abandon_unattempted(CopChaseFine*);
#ifdef __cplusplus
}
#endif
