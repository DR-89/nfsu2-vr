#include <SDL.h>
#include "quest_xr.h"
#include "game_data.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <io.h>
#include <windows.h>
#include "../xboxrecomp/src/apu/apu_xaudio2.h"

static HANDLE pcvr_service_stop, pcvr_service_thread;
static volatile LONG pcvr_capture_requested;
static volatile LONG pcvr_cull_all;
static volatile LONG pcvr_cull_trace_requested;
static volatile LONG pcvr_cpu_uniforms_enabled=1;
int pcvr_culling_override(void) { return (int)InterlockedCompareExchange(&pcvr_cull_all,0,0); }
int pcvr_take_cull_trace(void) { return (int)InterlockedExchange(&pcvr_cull_trace_requested,0); }
int pcvr_cpu_uniforms(void) { return (int)InterlockedCompareExchange(&pcvr_cpu_uniforms_enabled,0,0); }

/* Write directly to files rather than a launcher-owned anonymous pipe. A
 * blocked pipe reader also stalls concurrent fprintf calls on the CRT stream
 * lock, even when only this service calls fflush. Keep the streams buffered
 * and perform routine flushes and trigger-file lookups off the frame path. */
static DWORD WINAPI pcvr_service(void *unused)
{
    (void)unused;
    const int health=getenv("NFSU2_PCVR_HEALTH")!=NULL;
    const char *capture_env=getenv("NFSU2_VR_CAPTURE");
    const int capture=capture_env && *capture_env=='1';
    unsigned ticks=0;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    double worst_flush_ms=0;
    uint64_t previous_reads=0,previous_bytes=0,previous_underruns=0;
    while(WaitForSingleObject(pcvr_service_stop,250)==WAIT_TIMEOUT) {
        if(health && GetFileAttributesA("pcvr-uniforms.start")!=INVALID_FILE_ATTRIBUTES) {
            FILE *control=fopen("pcvr-uniforms.start","rb");
            int mode=0;
            if(control) {
                int read=fscanf(control,"%d",&mode); fclose(control);
                if(read==1 && (mode==0 || mode==1) && DeleteFileA("pcvr-uniforms.start")) {
                    InterlockedExchange(&pcvr_cpu_uniforms_enabled,mode);
                    fprintf(stderr,"[PCVR] CPU uniform construction: %s\n",mode?"on":"off (reference)");
                }
            }
        }
        if(capture && GetFileAttributesA("frames/cull-trace.request")!=INVALID_FILE_ATTRIBUTES &&
           DeleteFileA("frames/cull-trace.request"))
            InterlockedExchange(&pcvr_cull_trace_requested,1);
        if(health && GetFileAttributesA("pcvr-culling.start")!=INVALID_FILE_ATTRIBUTES) {
            FILE *control=fopen("pcvr-culling.start","rb");
            int mode=0;
            if(control) {
                int read=fscanf(control,"%d",&mode); fclose(control);
                if(read==1 && (mode==0 || mode==1) && DeleteFileA("pcvr-culling.start")) {
                    InterlockedExchange(&pcvr_cull_all,mode);
                    fprintf(stderr,"[PCVR] primary frustum diagnostic: %s\n",mode?"retain all candidates":"tracked stereo planes");
                }
            }
        }
        if(!InterlockedCompareExchange(&pcvr_capture_requested,0,0) &&
           GetFileAttributesA("benchmark.start")!=INVALID_FILE_ATTRIBUTES &&
           DeleteFileA("benchmark.start"))
            InterlockedExchange(&pcvr_capture_requested,1);
        if(health && ++ticks%20==0) {
            extern void xbox_io_get_stats(uint64_t out[5],char *last,int last_size);
            extern int mcpx_apu_pacing_stats(char *buf,int cap);
            uint64_t io[5]; char pacing[320]; Xa2Stats audio;
            xbox_io_get_stats(io,NULL,0);
            xa2_get_stats(&audio);
            mcpx_apu_pacing_stats(pacing,sizeof pacing);
            fprintf(stderr,"[PCVR health] IO: %llu reads, %.2f MB, max %.3f ms; audio: %llu underruns; log flush max %.3f ms; APU: %s\n",
                (unsigned long long)(io[2]-previous_reads),(io[3]-previous_bytes)/1048576.0,io[4]/1000.0,
                (unsigned long long)(audio.underruns-previous_underruns),worst_flush_ms,pacing);
            previous_reads=io[2]; previous_bytes=io[3]; previous_underruns=audio.underruns;
            worst_flush_ms=0;
        }
        LARGE_INTEGER flush_begin,flush_end;
        if(health) QueryPerformanceCounter(&flush_begin);
        fflush(stdout); fflush(stderr);
        if(health) {
            QueryPerformanceCounter(&flush_end);
            double ms=(flush_end.QuadPart-flush_begin.QuadPart)*1000.0/frequency.QuadPart;
            if(ms>worst_flush_ms) worst_flush_ms=ms;
        }
    }
    fflush(stdout); fflush(stderr);
    return 0;
}

static int pcvr_service_start(void)
{
    static char out_buffer[16384], err_buffer[65536];
    const wchar_t *out_path=_wgetenv(L"NFSU2_PCVR_STDOUT");
    const wchar_t *err_path=_wgetenv(L"NFSU2_PCVR_STDERR");
    if(out_path && *out_path) {
        if(!_wfreopen(out_path,L"w",stdout)) return 0;
        SetStdHandle(STD_OUTPUT_HANDLE,(HANDLE)_get_osfhandle(_fileno(stdout)));
    }
    if(err_path && *err_path) {
        if(!_wfreopen(err_path,L"w",stderr)) return 0;
        SetStdHandle(STD_ERROR_HANDLE,(HANDLE)_get_osfhandle(_fileno(stderr)));
    }
    setvbuf(stdout,out_buffer,_IOFBF,sizeof out_buffer);
    setvbuf(stderr,err_buffer,_IOFBF,sizeof err_buffer);
    fprintf(stderr,"[PCVR] log transport: stdout=%lu stderr=%lu (disk=1, pipe=3)\n",
        GetFileType((HANDLE)_get_osfhandle(_fileno(stdout))),
        GetFileType((HANDLE)_get_osfhandle(_fileno(stderr))));
    const char *uniforms=getenv("NFSU2_PCVR_CPU_UNIFORMS");
    pcvr_cpu_uniforms_enabled=!(uniforms && *uniforms=='0');
    fprintf(stderr,"[PCVR] CPU uniform construction: %s\n",pcvr_cpu_uniforms_enabled?"on":"off (reference)");
    pcvr_service_stop=CreateEventA(NULL,TRUE,FALSE,NULL);
    if(!pcvr_service_stop) return 0;
    pcvr_service_thread=CreateThread(NULL,0,pcvr_service,NULL,0,NULL);
    if(!pcvr_service_thread) { CloseHandle(pcvr_service_stop); return 0; }
    return 1;
}

static void pcvr_service_end(void)
{
    SetEvent(pcvr_service_stop);
    WaitForSingleObject(pcvr_service_thread,INFINITE);
    CloseHandle(pcvr_service_thread); CloseHandle(pcvr_service_stop);
}

/* Buffer the timed samples in RAM. The trigger and final CSV are outside the
 * measured interval; inactive tracking aborts instead of hiding the gap. */
void pcvr_benchmark_frame(uint64_t now,int tracked,const uint64_t phases[5],
                          const uint64_t work[4],uint32_t draws)
{
    /* Reuse existing cumulative phase clocks: no new timer queries or file
     * writes in the measured interval. These counters belong to the renderer. */
    static struct { double ms,phase[5],work[4]; uint32_t draws; } samples[32768];
    static uint64_t previous;
    static uint64_t previous_phases[5];
    static double elapsed;
    static unsigned count;
    static int recording;
    if(recording) {
        if(!tracked) {
            recording=0; previous=0;
            fprintf(stderr,"[PCVR] benchmark aborted: race or headset tracking inactive\n");
            return;
        }
        double ms=(now-previous)/1e6;
        samples[count].ms=ms;
        for(unsigned p=0;p<5;++p) samples[count].phase[p]=(phases[p]-previous_phases[p])/1e6;
        for(unsigned p=0;p<4;++p) samples[count].work[p]=work[p]/1e6;
        samples[count].draws=draws;
        memcpy(previous_phases,phases,sizeof previous_phases);
        ++count; elapsed+=ms; previous=now;
        if(elapsed>=45000.0 || count==32768) {
            FILE *out=fopen("benchmark.csv","wb");
            if(out) {
                fprintf(out,"frame,frame_ms,draw_ms,fence_ms,xr_begin_ms,xr_end_ms,statistics_ms,program_compile_ms,texture_upload_ms,pipeline_create_ms,pipeline_cache_ms,draws\n");
                for(unsigned i=0;i<count;++i)
                    fprintf(out,"%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u\n",i+1,samples[i].ms,
                        samples[i].phase[0],samples[i].phase[1],samples[i].phase[2],
                        samples[i].phase[3],samples[i].phase[4],samples[i].work[0],
                        samples[i].work[1],samples[i].work[2],samples[i].work[3],samples[i].draws);
                int failed=ferror(out); failed|=fclose(out)!=0;
                fprintf(stderr,"[PCVR] benchmark %s: %u game frames, %.3f seconds, %.3f FPS; benchmark.csv\n",
                    failed?"write failed":"complete",count,elapsed/1000.0,count*1000.0/elapsed);
            } else fprintf(stderr,"[PCVR] benchmark write failed\n");
            recording=0; previous=0;
        }
    } else if(tracked && InterlockedExchange(&pcvr_capture_requested,0)) {
        count=0; elapsed=0; previous=now; recording=1;
        memcpy(previous_phases,phases,sizeof previous_phases);
        fprintf(stderr,"[PCVR] benchmark started: 45 seconds of actual game frame intervals\n");
    }
}
/* Windows schedules the guest; the kernel's GIL still serializes guest code. */
void xbox_guest_pin(int interrupt) { (void)interrupt; }
void xbox_nx_retag_thread(void *entry) { (void)entry; }
void xbox_nx_raise_host_thread(void) { }
/* A failed headless renderer used to leave a black console with game audio.
 * Report it and offer an app-only restart; never require a PC reboot. */
void pcvr_vr_failure(const char *reason)
{
    fprintf(stderr,"[PCVR] VR session unavailable: %s\n",reason);fflush(stderr);
    int retry=MessageBoxA(NULL,reason,"NFSU2 PCVR - headset connection",
                         MB_RETRYCANCEL|MB_ICONERROR|MB_SETFOREGROUND);
    if(retry==IDRETRY) {
        wchar_t executable[32768],command[32768];
        DWORD n=GetModuleFileNameW(NULL,executable,32768);
        const wchar_t *original=GetCommandLineW();
        if(n && n<32768 && wcslen(original)<32768) {
            wcscpy(command,original);
            STARTUPINFOW start={0};PROCESS_INFORMATION process={0};start.cb=sizeof start;
            _putenv_s("NFSU2_PCVR_RESTART_DELAY","1");
            if(CreateProcessW(executable,command,NULL,NULL,FALSE,0,NULL,NULL,&start,&process)) {
                CloseHandle(process.hThread);CloseHandle(process.hProcess);
            } else MessageBoxA(NULL,"Could not restart the game. Please launch Start-PCVR.cmd again.","NFSU2 PCVR",MB_OK|MB_ICONERROR);
        }
    }
    /* Guest workers cannot safely resume after a lost OpenXR device. */
    ExitProcess(1);
}
int pcvr_run_game(int (*entry)(void))
{
    if(getenv("NFSU2_PCVR_RESTART_DELAY")) { _putenv_s("NFSU2_PCVR_RESTART_DELAY","");Sleep(1500); }
    int ready=pcvr_prepare_game_data();
    if(ready<=0) return ready<0?1:0;
    atexit(pcvr_release_game_data);
    extern int (*xbox_wait_completion_hook)(uint32_t);
    extern int nfsu2_guest_wait_completed(uint32_t);
    xbox_wait_completion_hook=nfsu2_guest_wait_completed;
    if(!pcvr_service_start()) {
        fprintf(stderr,"[PCVR] Cannot start log/benchmark service\n");
        fflush(stderr); return 1;
    }
    SDL_SetMainReady();
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER)!=0) {
        fprintf(stderr,"[PCVR] SDL initialization failed: %s\n",SDL_GetError());
        pcvr_service_end(); return 1;
    }
    while(!quest_xr_initialize()) {
        fprintf(stderr,"[PCVR] OpenXR initialization failed. Connect the headset in Virtual Desktop.\n");
        quest_xr_shutdown();
        if(MessageBoxA(NULL,"The OpenXR headset is not ready. Connect Virtual Desktop (VDXR), Quest Link or SteamVR, then select Retry. No PC restart is needed.",
            "NFSU2 PCVR - connect headset",MB_RETRYCANCEL|MB_ICONINFORMATION|MB_SETFOREGROUND)!=IDRETRY) {
            SDL_Quit();pcvr_service_end();return 1;
        }
    }
    /* Match quest_entry.c: this executor test uses the original flat Xbox
     * viewport and can discard whole batches before the tracked VR shader.
     * Keep the game's tracked frustum and Vulkan rasterizer culling active. */
    if(quest_xr_enabled()) {
        _putenv_s("RECOMP_CULL","0");
        fprintf(stderr,"[PCVR] legacy screen-space batch culling disabled for tracked VR\n");
    }
    quest_xr_game_thread();
    int result=entry();
    quest_xr_shutdown(); SDL_Quit(); pcvr_service_end(); return result;
}
