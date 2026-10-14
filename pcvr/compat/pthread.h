/* Only the small thread/mutex subset used by the shared Vulkan/OpenXR code. */
#pragma once
#include <windows.h>
#include <process.h>
#include <stdlib.h>
typedef HANDLE pthread_t;
typedef size_t pthread_attr_t;
typedef SRWLOCK pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
static inline int pthread_mutex_lock(pthread_mutex_t *m) { AcquireSRWLockExclusive(m); return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { ReleaseSRWLockExclusive(m); return 0; }
static inline int pthread_attr_init(pthread_attr_t *a) { *a=0; return 0; }
static inline int pthread_attr_setstacksize(pthread_attr_t *a,size_t size) { *a=size; return 0; }
static inline int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
typedef struct { void *(*entry)(void *); void *arg; } PcvrThreadStart;
static unsigned __stdcall pcvr_thread_entry(void *arg)
{
    PcvrThreadStart start=*(PcvrThreadStart *)arg;
    free(arg); start.entry(start.arg); return 0;
}
static inline int pthread_create(pthread_t *out,const pthread_attr_t *attr,void *(*entry)(void *),void *arg)
{
    PcvrThreadStart *start=malloc(sizeof *start);
    if(!start) return 1;
    *start=(PcvrThreadStart){entry,arg};
    *out=(HANDLE)_beginthreadex(NULL,attr?(unsigned)*attr:0,pcvr_thread_entry,start,0,NULL);
    if(!*out) { free(start); return 1; }
    return 0;
}
static inline int pthread_join(pthread_t thread,void **result)
{
    (void)result;
    DWORD status=WaitForSingleObject(thread,INFINITE);
    CloseHandle(thread); return status==WAIT_OBJECT_0?0:1;
}
