/* Short, opt-in local CPU diagnosis. No ETW/admin access required. This is
 * intrusive sampling, not an FPS acceptance capture: each selected thread is
 * suspended only long enough to copy RIP, then resumed before symbol work. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { DWORD id; HANDLE handle; ULONGLONG before, busy; } Thread;
typedef struct { DWORD id; DWORD64 pc; unsigned count; } Sample;
static Sample samples[65536];
static unsigned sample_count;
static ULONGLONG cpu_time(HANDLE thread)
{
    FILETIME create,exit,kernel,user;
    if(!GetThreadTimes(thread,&create,&exit,&kernel,&user)) return 0;
    return ((ULONGLONG)user.dwHighDateTime<<32)|user.dwLowDateTime;
}
static int thread_order(const void *a,const void *b)
{
    ULONGLONG aa=((const Thread *)a)->busy, bb=((const Thread *)b)->busy;
    return aa>bb?-1:aa<bb;
}
static int sample_order(const void *a,const void *b)
{
    const Sample *aa=a,*bb=b;
    if(aa->id!=bb->id) return aa->id<bb->id?-1:1;
    return aa->count>bb->count?-1:aa->count<bb->count;
}
int main(int argc,char **argv)
{
    if(argc!=3) { fprintf(stderr,"Usage: pcvr_cpu_sample PID SECONDS (1..20)\n"); return 2; }
    DWORD pid=strtoul(argv[1],NULL,10), seconds=strtoul(argv[2],NULL,10);
    if(!pid || seconds<1 || seconds>20) return 2;
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
    if(!process) return 1;
    char path[MAX_PATH]; DWORD length=sizeof path;
    if(!QueryFullProcessImageNameA(process,0,path,&length)) { CloseHandle(process); return 1; }
    char *name=strrchr(path,'\\');
    if(!name || _stricmp(name+1,"nfsu2-pcvr.exe")) { CloseHandle(process); return 2; }
    printf("CPU diagnosis: %s PID %lu, %lu seconds; not an FPS benchmark\n",path,pid,seconds);
    *name=0;
    Thread threads[256]; unsigned count=0;
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    THREADENTRY32 entry={sizeof entry};
    if(Thread32First(snapshot,&entry)) do {
        if(entry.th32OwnerProcessID!=pid || count==256) continue;
        HANDLE t=OpenThread(THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
        if(t) threads[count++]=(Thread){entry.th32ThreadID,t,cpu_time(t),0};
    } while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);
    Sleep(500);
    for(unsigned i=0;i<count;++i) threads[i].busy=cpu_time(threads[i].handle)-threads[i].before;
    qsort(threads,count,sizeof *threads,thread_order);
    unsigned selected=count<4?count:4;
    for(unsigned i=0;i<selected;++i)
        printf("Selected thread %lu, %.3f ms user CPU in selection window\n",threads[i].id,threads[i].busy/10000.0);
    ULONGLONG end=GetTickCount64()+seconds*1000;
    while(GetTickCount64()<end) {
        for(unsigned i=0;i<selected;++i) {
            CONTEXT context={0}; context.ContextFlags=CONTEXT_CONTROL;
            int valid=0;
            if(SuspendThread(threads[i].handle)!=(DWORD)-1) {
                valid=GetThreadContext(threads[i].handle,&context)!=0;
                ResumeThread(threads[i].handle);
            }
            if(valid) {
                unsigned j;
                for(j=0;j<sample_count;++j)
                    if(samples[j].id==threads[i].id && samples[j].pc==context.Rip) break;
                if(j<sample_count) ++samples[j].count;
                else if(sample_count<65536) samples[sample_count++]=(Sample){threads[i].id,context.Rip,1};
            }
        }
        Sleep(2);
    }
    for(unsigned i=0;i<count;++i) CloseHandle(threads[i].handle);
    SymSetOptions(SYMOPT_DEFERRED_LOADS|SYMOPT_UNDNAME|SYMOPT_FAIL_CRITICAL_ERRORS);
    int symbols=SymInitialize(process,path,TRUE)!=0;
    qsort(samples,sample_count,sizeof *samples,sample_order);
    for(unsigned i=0;i<sample_count;++i) {
        char storage[sizeof(SYMBOL_INFO)+512]={0}; PSYMBOL_INFO symbol=(PSYMBOL_INFO)storage;
        symbol->SizeOfStruct=sizeof *symbol; symbol->MaxNameLen=511;
        DWORD64 offset=0; IMAGEHLP_MODULE64 module={0}; module.SizeOfStruct=sizeof module;
        if(symbols) SymGetModuleInfo64(process,samples[i].pc,&module);
        if(symbols && SymFromAddr(process,samples[i].pc,&offset,symbol))
            printf("%lu,%u,%016llx,%s!%s+0x%llx\n",samples[i].id,samples[i].count,samples[i].pc,module.ModuleName,symbol->Name,offset);
        else printf("%lu,%u,%016llx,%s+0x%llx\n",samples[i].id,samples[i].count,samples[i].pc,module.ModuleName,samples[i].pc-module.BaseOfImage);
    }
    if(symbols) SymCleanup(process);
    CloseHandle(process); return 0;
}
