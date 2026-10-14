/* Internal dispatcher implementation, included by the bridge and its
 * Windows synchronization regression test. Requires BRIDGE_MEM8/32. */
#define KDISP_EVENT_NOTIFICATION  0
#define KDISP_EVENT_SYNCHRONIZE   1
#define KDISP_SEMAPHORE           5

static CRITICAL_SECTION   g_kdisp_cs;
static CONDITION_VARIABLE g_kdisp_cv;
static volatile LONG      g_kdisp_state;   /* 0 = not yet, 1 = busy, 2 = ready */

/* Optional title completion predicate, installed before guest threads start.
 * -1: unrelated object, 0: pending, 1: complete. For a recognized wait the
 * predicate is authoritative, including when the event is signalled: an old
 * GPU NOP notification can arrive after an earlier predicate-based release.
 * It must only read state: invoked while the dispatcher-object lock is held. */
int (*xbox_wait_completion_hook)(uint32_t object);
static volatile LONG g_completion_waiters;

static void kdisp_init(void)
{
    if (g_kdisp_state == 2)
        return;
    if (InterlockedCompareExchange(&g_kdisp_state, 1, 0) == 0) {
        InitializeCriticalSection(&g_kdisp_cs);
        InitializeConditionVariable(&g_kdisp_cv);
        InterlockedExchange(&g_kdisp_state, 2);
    }
    while (g_kdisp_state != 2)
        Sleep(0);
}

/* Completion is stored before this notification. A registered waiter holds
 * g_kdisp_cs while checking its predicate, so the producer cannot signal
 * between that check and the condition-variable sleep. */
void xbox_notify_wait_completion(void)
{
    if (!InterlockedCompareExchange(&g_completion_waiters, 0, 0)) return;
    EnterCriticalSection(&g_kdisp_cs);
    WakeAllConditionVariable(&g_kdisp_cv);
    LeaveCriticalSection(&g_kdisp_cs);
}

/* A guest VA whose header reads as an event or semaphore, and which the
 * kernel has no host object for. The Size check is what keeps an arbitrary
 * pointer -- a handle token, a stale value -- from qualifying. */
static int kdisp_is_guest(uint32_t va)
{
    uint8_t type, size;

    if (va < 0x10000 || va > XBOX_TOTAL_RAM - 0x14)
        return 0;
    if (ke_shadow_lookup(va))
        return 0;
    type = BRIDGE_MEM8(va);
    size = BRIDGE_MEM8(va + 2);
    if (type == KDISP_EVENT_NOTIFICATION || type == KDISP_EVENT_SYNCHRONIZE)
        return size == 4;
    if (type == KDISP_SEMAPHORE)
        return size == 5;
    return 0;
}

/* Take the object if it is signalled. Caller holds g_kdisp_cs. */
static int kdisp_try_acquire(uint32_t va, int consume)
{
    int32_t state = (int32_t)BRIDGE_MEM32(va + 4);

    if (state <= 0)
        return 0;
    if (consume) {
        uint8_t type = BRIDGE_MEM8(va);
        if (type == KDISP_EVENT_SYNCHRONIZE)
            BRIDGE_MEM32(va + 4) = 0;
        else if (type == KDISP_SEMAPHORE)
            BRIDGE_MEM32(va + 4) = (uint32_t)(state - 1);
    }
    return 1;
}

static DWORD kdisp_timeout_ms(uint32_t timeout_va)
{
    int64_t t;

    if (!timeout_va)
        return INFINITE;
    t = (int64_t)((uint64_t)BRIDGE_MEM32(timeout_va)
                  | ((uint64_t)BRIDGE_MEM32(timeout_va + 4) << 32));
    if (t == 0)
        return 0;
    if (t < 0) {
        int64_t ms = -t / 10000;
        return ms ? (ms >= INFINITE ? INFINITE - 1 : (DWORD)ms) : 1;
    }
    /* Absolute system time: nothing on the Xbox side waits on these in
     * practice; treat as "soon" rather than guess at a clock origin. */
    return 1;
}

/* Wait on a mix of guest objects and host handles.
 *
 * Guest objects are polled under the lock and woken by the condition
 * variable; host handles are polled with a zero-timeout wait. The sleep is
 * capped so a host handle, or a SignalState the title stored itself, is
 * noticed within a few milliseconds even though neither signals the CV. */
static NTSTATUS kdisp_wait(uint32_t count, const uint32_t *vas,
                           const HANDLE *hosts, int wait_all, DWORD ms)
{
    DWORD start = GetTickCount();
    uint32_t i;

    kdisp_init();
    EnterCriticalSection(&g_kdisp_cs);
    if (count == 1 && vas[0] && xbox_wait_completion_hook &&
        xbox_wait_completion_hook(vas[0]) >= 0) {
        NTSTATUS result = STATUS_TIMEOUT;
        InterlockedIncrement(&g_completion_waiters);
        for (;;) {
            int complete = xbox_wait_completion_hook(vas[0]);
            int signalled = kdisp_try_acquire(vas[0], 1);
            if (complete > 0) { result = STATUS_SUCCESS; break; }
            if (signalled) {
                static unsigned stale;
                if (++stale <= 8 || stale % 1024 == 0)
                    fprintf(stderr, "[PCVR] ignored early D3D event while fence pending (%u)\n", stale);
            }
            DWORD slice = 4;
            if (ms != INFINITE) {
                DWORD spent = GetTickCount() - start;
                if (spent >= ms) break;
                if (ms - spent < slice) slice = ms - spent;
            }
            SleepConditionVariableCS(&g_kdisp_cv, &g_kdisp_cs, slice);
        }
        InterlockedDecrement(&g_completion_waiters);
        LeaveCriticalSection(&g_kdisp_cs);
        return result;
    }
    for (;;) {
        if (wait_all) {
            uint32_t ready = 0;
            for (i = 0; i < count; i++) {
                if (vas[i] ? kdisp_try_acquire(vas[i], 0)
                           : WaitForSingleObject(hosts[i], 0) == WAIT_OBJECT_0)
                    ready++;
            }
            if (ready == count) {
                for (i = 0; i < count; i++)
                    if (vas[i])
                        kdisp_try_acquire(vas[i], 1);
                LeaveCriticalSection(&g_kdisp_cs);
                return STATUS_SUCCESS;
            }
        } else {
            for (i = 0; i < count; i++) {
                if (vas[i] ? kdisp_try_acquire(vas[i], 1)
                           : WaitForSingleObject(hosts[i], 0) == WAIT_OBJECT_0) {
                    LeaveCriticalSection(&g_kdisp_cs);
                    return (NTSTATUS)(STATUS_SUCCESS + i);
                }
            }
        }

        {
            DWORD slice = 4;
            if (ms != INFINITE) {
                DWORD spent = GetTickCount() - start;
                if (spent >= ms) {
                    LeaveCriticalSection(&g_kdisp_cs);
                    return STATUS_TIMEOUT;
                }
                if (ms - spent < slice)
                    slice = ms - spent;
            }
            SleepConditionVariableCS(&g_kdisp_cv, &g_kdisp_cs, slice);
        }
    }
}

/* Set SignalState and wake everyone; returns the previous state. */
static int32_t kdisp_signal(uint32_t va, int32_t add, int set_to_one)
{
    int32_t prev;

    kdisp_init();
    EnterCriticalSection(&g_kdisp_cs);
    prev = (int32_t)BRIDGE_MEM32(va + 4);
    BRIDGE_MEM32(va + 4) = (uint32_t)(set_to_one ? 1 : prev + add);
    WakeAllConditionVariable(&g_kdisp_cv);
    LeaveCriticalSection(&g_kdisp_cs);
    return prev;
}

static int32_t kdisp_reset(uint32_t va)
{
    int32_t prev;

    kdisp_init();
    EnterCriticalSection(&g_kdisp_cs);
    prev = (int32_t)BRIDGE_MEM32(va + 4);
    BRIDGE_MEM32(va + 4) = 0;
    LeaveCriticalSection(&g_kdisp_cs);
    return prev;
}

