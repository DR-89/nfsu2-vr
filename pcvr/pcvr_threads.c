/* Read-only diagnosis of a hung PCVR process. Suspend each thread only while
 * copying its context/stack and always resume it before continuing. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if(argc != 2) { fprintf(stderr,"Usage: pcvr_threads PID\n"); return 2; }
    DWORD pid=(DWORD)strtoul(argv[1],NULL,10);
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
    if(!process) { fprintf(stderr,"OpenProcess: %lu\n",GetLastError()); return 1; }
    char executable[MAX_PATH]; DWORD size=MAX_PATH;
    if(!QueryFullProcessImageNameA(process,0,executable,&size)) return 1;
    printf("Process %lu: %s\n",pid,executable);
    char *slash=strrchr(executable,'\\'); if(slash) *slash=0;
    SymSetOptions(SYMOPT_DEFERRED_LOADS|SYMOPT_UNDNAME|SYMOPT_FAIL_CRITICAL_ERRORS);
    if(!SymInitialize(process,executable,TRUE)) {
        fprintf(stderr,"SymInitialize: %lu\n",GetLastError()); CloseHandle(process); return 1;
    }
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    THREADENTRY32 entry={sizeof entry};
    if(Thread32First(snapshot,&entry)) do {
        if(entry.th32OwnerProcessID!=pid) continue;
        HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
        if(!thread) continue;
        CONTEXT context={0}; context.ContextFlags=CONTEXT_FULL;
        DWORD64 pcs[64]; unsigned count=0;
        FILETIME creation,exit,kernel,user;
        GetThreadTimes(thread,&creation,&exit,&kernel,&user);
        if(SuspendThread(thread)!=(DWORD)-1) {
            if(GetThreadContext(thread,&context)) {
                STACKFRAME64 frame={0};
                frame.AddrPC.Offset=context.Rip; frame.AddrPC.Mode=AddrModeFlat;
                frame.AddrStack.Offset=context.Rsp; frame.AddrStack.Mode=AddrModeFlat;
                frame.AddrFrame.Offset=context.Rbp; frame.AddrFrame.Mode=AddrModeFlat;
                pcs[count++]=context.Rip;
                while(count<64 && StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,
                        NULL,SymFunctionTableAccess64,SymGetModuleBase64,NULL)) {
                    if(!frame.AddrPC.Offset) break;
                    pcs[count++]=frame.AddrPC.Offset;
                }
            }
            ResumeThread(thread);
        }
        CloseHandle(thread);
        ULARGE_INTEGER ticks; ticks.LowPart=user.dwLowDateTime; ticks.HighPart=user.dwHighDateTime;
        printf("\nThread %lu, user CPU %.3f seconds\n",entry.th32ThreadID,ticks.QuadPart/1e7);
        for(unsigned i=0;i<count;++i) {
            char storage[sizeof(SYMBOL_INFO)+512]={0}; PSYMBOL_INFO symbol=(PSYMBOL_INFO)storage;
            symbol->SizeOfStruct=sizeof *symbol; symbol->MaxNameLen=511;
            DWORD64 offset=0; IMAGEHLP_MODULE64 module={0}; module.SizeOfStruct=sizeof module;
            SymGetModuleInfo64(process,pcs[i],&module);
            if(SymFromAddr(process,pcs[i],&offset,symbol))
                printf(" %016llx %s!%s+0x%llx\n",pcs[i],module.ModuleName,symbol->Name,offset);
            else printf(" %016llx %s+0x%llx\n",pcs[i],module.ModuleName,pcs[i]-module.BaseOfImage);
        }
    } while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot); SymCleanup(process); CloseHandle(process); return 0;
}
