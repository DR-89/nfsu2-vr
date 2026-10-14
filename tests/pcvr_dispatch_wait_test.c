/* Exercise the production dispatcher against delayed/out-of-order GPU events. */
#include "kernel/kernel.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "pcvr_fence.h"
ptrdiff_t g_xbox_mem_offset;
#define BRIDGE_MEM32(va) (*(volatile uint32_t *)(g_xbox_mem_offset+(va)))
#define BRIDGE_MEM8(va) (*(volatile uint8_t *)(g_xbox_mem_offset+(va)))
#define XBOX_TOTAL_RAM (64u<<20)
static HANDLE ke_shadow_lookup(uint32_t va) { return NULL; }
#include "../xboxrecomp/src/kernel/kernel_dispatch.inc"
static uint8_t test_memory[256];
static volatile LONG completed, wait_result;
static HANDLE wait_entered;
static const uint32_t object = 64;
static int predicate(uint32_t va)
{
    if (va != object) return -1;
    SetEvent(wait_entered);
    return pcvr_fence_reached(32, (uint32_t)InterlockedCompareExchange(&completed,0,0), 32);
}
static DWORD WINAPI waiter(void *unused)
{
    HANDLE host = NULL;
    InterlockedExchange(&wait_result, kdisp_wait(1, &object, &host, 0, 1000));
    return 0;
}
int main(void)
{
    unsigned failures=0;
    g_xbox_mem_offset=(ptrdiff_t)test_memory;
    wait_entered=CreateEvent(NULL,FALSE,FALSE,NULL);
    xbox_wait_completion_hook=predicate;
    test_memory[object]=KDISP_EVENT_SYNCHRONIZE;
    test_memory[object+2]=4;
    /* A NOP from the previous fence must not release the next fence. */
    completed=30; wait_result=-1; BRIDGE_MEM32(object+4)=1;
    HANDLE thread=CreateThread(NULL,0,waiter,NULL,0,NULL);
    WaitForSingleObject(wait_entered,1000);
    if (WaitForSingleObject(thread,30)!=WAIT_TIMEOUT) ++failures;
    InterlockedExchange(&completed,32);
    xbox_notify_wait_completion();
    if (WaitForSingleObject(thread,1000)!=WAIT_OBJECT_0 || wait_result!=STATUS_SUCCESS) ++failures;
    CloseHandle(thread);
    HANDLE host=NULL;
    /* Completion without its NOP event must not hang. */
    BRIDGE_MEM32(object+4)=0;
    if(kdisp_wait(1,&object,&host,0,0)!=STATUS_SUCCESS) ++failures;
    /* A stale signalled event cannot turn a pending timed wait into success. */
    completed=30; BRIDGE_MEM32(object+4)=1;
    if(kdisp_wait(1,&object,&host,0,10)!=STATUS_TIMEOUT) ++failures;
    /* Unrelated events retain normal auto-reset and timeout behavior. */
    const uint32_t other=128;
    test_memory[other]=KDISP_EVENT_SYNCHRONIZE;
    BRIDGE_MEM32(other+4)=1;
    if(kdisp_wait(1,&other,&host,0,0)!=STATUS_SUCCESS || BRIDGE_MEM32(other+4)) ++failures;
    if(kdisp_wait(1,&other,&host,0,0)!=STATUS_TIMEOUT) ++failures;
    CloseHandle(wait_entered);
    printf("PCVR dispatcher completion: 5 scenarios, %u failures\n",failures);
    return failures?1:0;
}
