/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#if defined(_MSC_VER) && !defined(__clang__)
#define NFSU2_NO_FMA
#define NFSU2_COLD __declspec(noinline)
#define NFSU2_UNUSED
#define __builtin_expect(value, expected) (value)
#else
#define NFSU2_NO_FMA __attribute__((optimize("fp-contract=off")))
#define NFSU2_COLD __attribute__((noinline, cold))
#define NFSU2_UNUSED __attribute__((unused))
#endif
#ifdef NFSU2_PCVR
#include "pcvr_platform.h"
#include "pcvr_fence.h"
#endif
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
#include "quest_xr.h"
#include "vr_community_mods.h"
#include "vr_vinyls.h"
#include "community/xyz_xbox_remote.h"
#include "community/xyz_xbox_pool.h"
#include "community/xyz_car_files.h"
#include "community/xyz_game_api.h"
#include "community/xyz_xnet.h"
#include "quest_haptics.h"
#include "quest_culling.h"
#include "quest_frame_timing.h"
#include "quest_cockpit.h"
#include "quest_cockpit_fit.h"
#include "quest_cockpit_vehicle.h"
#include "quest_cockpit_ui.h"
#include "quest_cockpit_windows.h"
#include "quest_shifter.h"
#include "quest_rear_camera.h"
#include "vr_replay.h"
#include "vr_music.h"
#include "vr_whine.h"
#include "community/xyz_xbox_gps.h"
#include <time.h>
#endif

/* The generated register model (g_eax/g_esp are thread-local there) and the
 * XBOX_PTR/MEM32 accessors; the register names below are its macros. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * The runtime owns them: xbox_kernel defines all three in
 * src/kernel/xbox_memory_layout.c, and recomp_types.h declares them extern.
 * Declare, do not define -- a definition here as well is a duplicate symbol,
 * and a project copied from this template failed to link on all three:
 *
 *   xbox_memory_layout.obj : error LNK2005: g_icall_count already defined
 *                            in recomp_manual.obj
 */
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count;

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

extern ptrdiff_t g_xbox_mem_offset;

/* ── CRT memmove/memcpy ───────────────────────────────────────
 *
 * Two copies of the MSVC CRT memmove are linked (0x002A7EE0 and 0x002A9450;
 * memcpy is the same body). Their tail and backward-copy paths dispatch
 * through UnwindDown-style vectors indexed with a *negative* register -- the
 * table address in the instruction is the end of the table -- which the
 * lifter's forward table reader cannot follow, so those jumps were lowered
 * to indirect tail calls into the middle of the function and failed to
 * resolve: every copy whose length was not a multiple of 4, and every
 * overlapping backward copy, silently lost its tail.
 *
 * Replaced by the host memmove. cdecl (dst, src, count), returns dst, and
 * esi/edi are preserved, as the original restores them. Generated bodies are
 * skipped by tools.recomp --exclude-manual reading this file. */
static void crt_memmove(void)
{
    uint32_t dst = MEM32(esp + 4);
    uint32_t src = MEM32(esp + 8);
    uint32_t n   = MEM32(esp + 12);

    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    eax = dst;
    esp += 4;   /* return address; cdecl, the caller pops the arguments */
}

void sub_002A7EE0(void) { crt_memmove(); }

/* The TrackCarCameraMover applies its final camera at 0x612CD. Intercept
 * the main driving camera and the race director's start-line camera before
 * Camera::SetMatrix computes derived position and history.
 * US retail Car has render origin +0x50 and local-to-world axes +0x90. */
extern void sub_00055C80_gen(void);
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
/* Guest-thread identity, refreshed by the driving camera. Never infer the
 * player's body from its dimensions or from a particular vehicle name. */
static uint32_t cockpit_player_car;
static int community_copchase_music;
static uint32_t restored_mover,restored_car;
/* Guest-thread only. Slot 0..4 indexes the five retail manual replay
 * views, followed by the fitted VR cockpit. This choice is per playback. */
enum { REPLAY_COCKPIT_SLOT=5 };
static uint32_t replay_camera_owner;
static unsigned replay_camera_slot;
static int replay_previous_cockpit;
static int cockpit_entry_direction=1;
extern void sub_001E32E0_gen(void);
/* These are additional calls from the guest thread. Preserve the entire
 * interrupted register file, including SIMD scratch used by the audio engine. */
static uint32_t guest_call_full(void (*function)(void),uint32_t self,uint32_t second_register,
                               uint32_t continuation,unsigned argc,const uint32_t *args)
{
    uint32_t regs[]={eax,ecx,edx,ebx,esi,edi,esp,g_ebp,g_seh_ebp,g_fs_base};
    RecompXmm *xs[]={&g_xmm0,&g_xmm1,&g_xmm2,&g_xmm3,&g_xmm4,&g_xmm5,&g_xmm6,&g_xmm7},x[8];
    RecompMmx *ms[]={&g_mm0,&g_mm1,&g_mm2,&g_mm3,&g_mm4,&g_mm5,&g_mm6,&g_mm7},m[8];
    double fp[8];memcpy(fp,g_fp_stack,sizeof fp);
    int top=g_fp_top,cmp=g_fp_cmp,df=g_df;uint16_t cc=g_fp_cc,cw=g_fp_control_word;
    for(int i=0;i<8;++i){x[i]=*xs[i];m[i]=*ms[i];}
    ecx=self;edx=second_register;
    for(unsigned i=argc;i>0;--i)PUSH32(esp,args[i-1]);
    PUSH32(esp,continuation);function();
    uint32_t result=eax;
    eax=regs[0];ecx=regs[1];edx=regs[2];ebx=regs[3];esi=regs[4];edi=regs[5];esp=regs[6];
    g_ebp=regs[7];g_seh_ebp=regs[8];g_fs_base=regs[9];g_df=df;
    for(int i=0;i<8;++i){*xs[i]=x[i];*ms[i]=m[i];}
    memcpy(g_fp_stack,fp,sizeof fp);g_fp_top=top;g_fp_cmp=cmp;g_fp_cc=cc;g_fp_control_word=cw;
    return result;
}
#include "vr_io_completion.inc"
static uint32_t cockpit_guest_call(void (*function)(void),uint32_t self,uint32_t continuation,int argc,uint32_t arg,uint32_t second)
{
    const uint32_t args[2]={arg,second};
    return guest_call_full(function,self,edx,continuation,(unsigned)argc,args);
}
static unsigned cockpit_last_slot(void)
{
    int type=(int32_t)MEM32(0x003cc460u);
    return (MEM8(0x003cc499u) || MEM32(0x00449464u) || (type>=0x104e && type<=0x1053))?4:3;
}
static void cockpit_restore_camera(uint32_t mover,uint32_t car)
{
    if((restored_mover==mover && restored_car==car) || !quest_xr_cockpit_enabled())return;
    uint32_t manager=MEM32(0x0044940cu);
    if(!xyz_xbox_address(manager,0x248) || MEM32(manager+4)!=car ||
       MEM32(manager+0x238)!=mover || MEM32(manager+0x10)!=0)return;
    unsigned last=cockpit_last_slot(),slot=MEM32(0x00406d34u);
    int choice=quest_xr_camera_choice();
    if(choice<0 && slot<=last)quest_xr_camera_remember((int)slot);
    if(choice>=0) {
        int cockpit=choice>=5;
        unsigned desired=cockpit?(choice==5?last:0):((unsigned)choice<=last?(unsigned)choice:last);
        if(slot!=desired) {
            MEM32(0x00406d34u)=desired;
            /* Direction zero reapplies this valid retail slot to the mover. */
            cockpit_guest_call(sub_001E32E0_gen,manager,0x1f2c6eu,1,0,0);
        }
        if(cockpit)cockpit_entry_direction=choice==5?1:-1;
        quest_xr_cockpit_select(cockpit);
    }
    restored_mover=mover;restored_car=car;
}
static int cockpit_ui_read(void *context,uint32_t address,void *out,size_t size);

static uint32_t cockpit_local_car(void)
{
    uint32_t manager=MEM32(0x44940cu);
    return xyz_xbox_address(manager,0x14) && MEM32(manager+0x10)==0?MEM32(manager+4):0;
}
static VrReplay cockpit_replay;
static uint32_t cockpit_replay_manager(void)
{
    uint32_t manager=MEM32(0x428930u);
    return xyz_xbox_address(manager,0x90)?manager:0;
}
static int cockpit_replay_open(void)
{
    uint32_t owner=MEM32(0x3f9c64u);
    return xyz_xbox_address(owner,0xd0) && MEM32(owner)==0x34d7a4u;
}
static int cockpit_current_race_stage(void)
{
    uint32_t director=MEM32(0x449478u);
    return xyz_xbox_address(director,4)?(int)MEM32(director):0;
}
static int cockpit_replay_camera_context(uint32_t manager)
{
    uint32_t director=MEM32(0x449438u);
    return manager && manager==MEM32(0x449414u) && xyz_xbox_address(manager,0x250) &&
           MEM32(0x428e34u)==6 && xyz_xbox_address(director,4) && MEM32(director)==5 &&
           cockpit_replay_open();
}
static int cockpit_replay_camera_selected(uint32_t manager)
{
    return replay_camera_owner && replay_camera_owner==MEM32(0x3f9c64u) &&
           cockpit_replay_camera_context(manager);
}
static uint32_t cockpit_replay_track_mover(uint32_t manager)
{
    uint32_t mover=MEM32(manager+0x238);
    return xyz_xbox_address(mover,0x60) && MEM32(mover)==0x344880u &&
           MEM32(mover+0x1c)==MEM32(0x3f5b70u)?mover:0;
}
static void cockpit_replay_camera_apply(uint32_t manager,int change)
{
    /* ReplayControl's native camera button (0x1EBA00) uses slots 1..5.
     * Slot zero enables random cuts. Cockpit uses the same hood/bumper
     * mover as driving, with its fitted seat applied at SetMatrix. */
    int seated=replay_camera_slot==REPLAY_COCKPIT_SLOT;
    unsigned native_slot=seated?3:replay_camera_slot+1;
    quest_xr_cockpit_select(seated);
    MEM32(manager+0x21c)=native_slot;
    uint32_t mover=cockpit_replay_track_mover(manager);
    if(change || !mover) {
        if(!xyz_xbox_address(MEM32(manager+4),0x600))return;
        cockpit_guest_call(sub_001EB230,manager,0x1eba5du,1,1,0);
        mover=cockpit_replay_track_mover(manager);
    }
    if(!mover)return;
    uint32_t mode=MEM32(0x3cbfc0u+4*native_slot);
    if(MEM32(mover+0x48)!=mode)
        cockpit_guest_call(sub_00054450,mover,0x1eba7au,1,mode,0);
}
static void cockpit_replay_camera_next(uint32_t manager,unsigned steps)
{
    if(!steps || !cockpit_replay_camera_context(manager))return;
    unsigned count=REPLAY_COCKPIT_SLOT+(quest_xr_cockpit_enabled()?1:0);
    if(!cockpit_replay_camera_selected(manager)) {
        uint32_t mover=cockpit_replay_track_mover(manager);
        replay_previous_cockpit=quest_xr_cockpit_requested();
        /* From a cinematic shot the first grip selects the seat. From a
         * manual view continue after the one actually on screen. */
        replay_camera_slot=REPLAY_COCKPIT_SLOT-1;
        if(cockpit_player_car && replay_previous_cockpit) {
            replay_camera_slot=count-1;
        } else if(mover) {
            for(unsigned i=0;i<REPLAY_COCKPIT_SLOT;++i) {
                if(MEM32(mover+0x4c)==MEM32(0x3cbfc4u+i*4)){replay_camera_slot=i;break;}
            }
        }
        replay_camera_owner=MEM32(0x3f9c64u);
    }
    replay_camera_slot=(replay_camera_slot+steps%count)%count;
    cockpit_replay_camera_apply(manager,1);
    fprintf(stderr,"[Replay] Selected camera %u/%u%s\n",replay_camera_slot+1,count,
            replay_camera_slot==REPLAY_COCKPIT_SLOT?" (cockpit)":"");
}
static void cockpit_replay_camera_end(void)
{
    if(!replay_camera_owner)return;
    replay_camera_owner=0;
    quest_xr_cockpit_select(replay_previous_cockpit);
    restored_mover=restored_car=0;
}
static VrReplayFrame *cockpit_replay_frame(uint32_t manager)
{
    return vr_replay_find(&cockpit_replay,manager,MEM32(manager+0x8c),MEM32(manager+0x78),
                          MEM32(manager+0x84),MEM32(0x3a4c74u));
}
static float cockpit_engine_rumble(void)
{
    uint32_t car=cockpit_local_car();
    if(!xyz_xbox_address(car,0x444))return 0;
    uint32_t spec=MEM32(car+0x20);
    if(!xyz_xbox_address(spec,0x1fc))return 0;
    /* Retail reads idle at 0x177E1 and divides current RPM by this car's
     * rev limit at 0x1E67C6. +0x438 is the processed accelerator. */
    uint32_t control=MEM32(car+0x30);
    float brake=xyz_xbox_address(control,0x218)?MEMF(control+0x214):0;
    quest_xr_telemetry_publish(car,MEMF(car+0x440),MEMF(spec+0x1f8),MEMF(car+0x42c),brake);
    return quest_engine_rumble(MEMF(car+0x440),MEMF(spec+0x1f0),MEMF(spec+0x1f8),MEMF(car+0x438));
}
static int cockpit_grid_gears(uint32_t car)
{
    if(!xyz_xbox_address(car,0x438))return 0;
    uint32_t driver=MEM32(car+0x30),spec=MEM32(car+0x20);
    if(!xyz_xbox_address(driver,0x314) || MEM32(driver+0x2a8)!=car ||
       !xyz_xbox_address(spec,0x1cc))return 0;
    uint32_t mode=MEM32(driver+0x2c0),count=MEM32(spec+0x1c8);
    return (mode==1 || mode==3 || mode==4) && count>=4 && count<=6 &&
           fabsf(MEMF(car+0x430))<1.f?(int)count:0;
}
/* The player/cockpit may become available after the stage broadcast. Refresh
 * from the director that owns the countdown (0x1F78BE) even when the locked
 * human driver stops polling. Change only its gear request, never its brakes. */
static void cockpit_grid_update(void)
{
    if(cockpit_replay_open())return;
    int stage=cockpit_current_race_stage();
    if(!quest_shifter_before_start(stage) && !quest_xr_shifter_grid_car())return;
    uint32_t car=cockpit_local_car();
    int gear=quest_xr_shifter_stage(car,stage,stage,cockpit_grid_gears(car));
    if(gear!=QUEST_SHIFT_NONE)MEM32(MEM32(car+0x30)+0x1e4)=(uint32_t)gear;
}
/* 0x1ED394 broadcasts the new and previous race stages to each car. The
 * human listener unlocks the grid at stage 4 and requests first via
 * 0x1FB52 -> 0x16C96. Keep its brake and launch bookkeeping intact. */
static void cockpit_race_stage(void)
{
    uint32_t car=MEM32(ecx+4),stage=MEM32(esp+4),previous=MEM32(esp+8);
    int gear=QUEST_SHIFT_NONE;
    uint32_t driver=0;
    if(quest_xr_enabled() && car && car==cockpit_local_car() && !cockpit_replay_open()) {
        int count=cockpit_grid_gears(car);
        gear=quest_xr_shifter_stage(car,(int)stage,(int)previous,count);
        if(gear!=QUEST_SHIFT_NONE)driver=MEM32(car+0x30);
    }
    sub_00019D30();
    if(gear>=0)MEM32(driver+0x1e4)=(uint32_t)gear;
}
/* Other grid-release and car-reset calls can request first again between
 * driver ticks (0x4ABE8 / 0x1E5499). Protect the pending physical selection. */
static void cockpit_driver_first(void)
{
    uint32_t driver=ecx;
    sub_00016C70();
    if(MEM32(driver+0x1e4)==QUEST_SHIFT_FIRST && quest_xr_enabled() && !cockpit_replay_open() &&
       quest_xr_shifter_grid_request(MEM32(driver+0x2a8))==QUEST_SHIFT_N)MEM32(driver+0x1e4)=QUEST_SHIFT_N;
}

/* Human-driver vtable slot 0x341604 calls 0x2D800 once per input tick.
 * Retail shift handlers 0x179E0/0x17AD0 clamp against the installed gearbox
 * at Car+0x20 -> +0x1C8 and submit Driver+0x1E4 (R=0, N=1, first=2).
 * Sample after the retail pedal / automatic-reverse processing, before
 * physics reads this request. The gearbox still performs the shift itself. */
static int cockpit_apply_shifter(uint32_t driver,uint32_t car)
{
    if(car!=cockpit_player_car && (car!=cockpit_local_car() ||
       quest_xr_shifter_grid_request(car)==QUEST_SHIFT_NONE))return 0;
    int raw_pedals=0;
    uint32_t spec=MEM32(car+0x20);
    int count=xyz_xbox_address(spec,0x1cc)?(int)MEM32(spec+0x1c8):0;
    int gear=(int)MEM32(car+0x434), mode=(int)MEM32(driver+0x2c0);
    int manual=mode==1 || mode==3 || mode==4;
    int locked=MEM8(driver+0x2b4)?QUEST_DRIVER_GRID_LOCK:QUEST_DRIVER_READY;
    if(MEM32(driver+0x2ac)==~0u || MEM32(driver+0x2b8))locked=QUEST_DRIVER_NO_INPUT;
    int request=quest_xr_shifter_driver(car,count,gear,manual,locked);
    if(manual && locked && (request==QUEST_SHIFT_N || request==QUEST_SHIFT_FIRST) &&
       car==quest_xr_shifter_grid_car())MEM32(driver+0x1e4)=(uint32_t)request;
    if(manual && !locked && count>=4 && count<=6 && request>=0 && request<=count+1) {
        /* Explicit direction: either gear uses the accelerator. In retail
         * 0x17480 the brake near standstill can select reverse and become
         * throttle. Correct that pedal swap while the lever owns direction,
         * retaining the usual forward throttle and race filters otherwise. */
        if(!request || !gear || MEM8(driver+0x2f0) || !MEM32(driver+0x1e4)) {
            raw_pedals=1;
        }
        MEM32(driver+0x1e4)=(uint32_t)request;
    }
    return raw_pedals;
}
static void cockpit_driver_update(void)
{
    uint32_t driver=ecx;
    if(!xyz_xbox_address(driver,0x314) || MEM32(0x428e34u)!=6) {
        sub_0002D800();return;
    }
    uint32_t car=MEM32(driver+0x2a8),manager=cockpit_replay_manager();
    int local=car && car==cockpit_local_car();
    int playing=manager && MEM32(manager);
    VrReplayFrame *frame=local && manager && (playing || MEM32(manager+4))?
                         cockpit_replay_frame(manager):NULL;
    uint32_t gear_before=MEM32(driver+0x1e4);
    if(playing && frame && frame->human_driver)MEM32(driver+0x1e4)=frame->gear_before;
    sub_0002D800();
    int raw_pedals=0;
    if(playing) {
        if(frame && frame->human_driver) {
            MEM32(driver+0x1e4)=frame->gear_after;
            raw_pedals=frame->raw_pedals;
        }
    } else if(car && !cockpit_replay_open()) {
        if(quest_xr_enabled())raw_pedals=cockpit_apply_shifter(driver,car);
        if(frame) {
            frame->human_driver=1;frame->gear_before=gear_before;
            frame->gear_after=MEM32(driver+0x1e4);frame->raw_pedals=(unsigned char)raw_pedals;
        }
    }
    if(raw_pedals) {
        MEM32(driver+0x20c)=MEM32(driver+0x2d4);
        MEM32(driver+0x214)=MEM32(driver+0x2dc);
        MEM32(car+0x438)=MEM32(driver+0x20c);
    }
}
#endif
extern void sub_002535AC_gen(void);
extern void sub_000A1580_gen(void);
void sub_000A1580(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t texture=xyz_online_texture(MEM32(esp+4));
    if(texture){eax=texture;esp+=4;return;}
#endif
    sub_000A1580_gen();
}
void sub_002535AC(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    vr_io_register_completion();
#else
    sub_002535AC_gen();
#endif
}
/* 0x1DA770 records the simulation step on channel 1. Preserve its full
 * tick count across a slow VR frame while retail still owns the stream,
 * compression and snapshots. Both calls are thiscall and pop their args. */
extern void sub_00183120_gen(void);
void sub_00183120(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t manager=ecx;
    if(MEM32(esp)==0x001da7a9u && manager && manager==cockpit_replay_manager() && MEM32(manager+4))
        vr_replay_record(&cockpit_replay,manager,MEM32(manager+0x8c),MEM32(manager+0x78),
                         MEM32(manager+0x84),MEM32(0x3a4c74u),MEM32(esp+4));
#endif
    sub_00183120_gen();
}
extern void sub_001830D0_gen(void);
void sub_001830D0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t manager=ecx,caller=MEM32(esp);
#endif
    sub_001830D0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(!manager || manager!=cockpit_replay_manager() || !MEM32(manager) || MEM32(manager+8))return;
    if(caller==0x001da78eu) {
        VrReplayFrame *frame=cockpit_replay_frame(manager);
        if(frame && (frame->ticks&255u)==eax)eax=frame->ticks;
    } else if(caller==0x001eb1fcu && (uint8_t)eax!=(uint8_t)ebx && cockpit_replay_open()) {
        /* Channel 2 hashes every traffic car's position to 1/65536 of a
         * world unit. A valid packet with simulation drift used to fire
         * race state 12 and dismiss ReplayControl. The stream reader above
         * still checks packet type and exhaustion; real errors go to retail. */
        static int reported;
        if(!reported) {
            fprintf(stderr,"[Replay] Continuing after position drift at frame %u\n",MEM32(0x3a4c74u));
            reported=1;
        }
        eax=(uint8_t)ebx;
    }
#endif
}
void sub_00055C80(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t camera=ecx, mover=edi, target=MEM32(esp+4);
    int main_camera=camera && camera==MEM32(0x003f5b70u);
    if(main_camera) cockpit_player_car=0;
    int available=0, active=0;
    float saved[16];
    int rear_adjusted=0;
    /* Keep the retail rear camera independent of physical mirror trim.
     * The glass shader changes its own reflected rays, never the source. */
    int player_rear=camera==0x003efb20u && MEM32(esp)==0x00059a2eu &&
        xyz_xbox_address(esi,0x30) && MEM32(esi)==0x003446f0u &&
        MEM32(esi+0x1c)==camera && MEM32(esi+0x2c)==cockpit_local_car();
    if(player_rear && cockpit_player_car && quest_xr_cockpit_active() &&
       MEM32(esi+0x2c)==cockpit_player_car && xyz_xbox_address(target,64)) {
        float front[16],rear[16],rotation[12],pos[3],zero[3]={0};
        QuestMirrorCamera source;
        memcpy(rotation,(const void *)XBOX_PTR(cockpit_player_car+0x90),sizeof rotation);
        memcpy(pos,(const void *)XBOX_PTR(cockpit_player_car+0x50),sizeof pos);
        memcpy(rear,(const void *)XBOX_PTR(target),sizeof rear);
        if(quest_cockpit_view(front,rotation,pos,zero) && quest_mirror_camera(&source,front,rear)) {
            /* The mover rewrites these eView 3 fields every tick. Include
             * the outer lanes and allow close traffic behind the glass. */
            MEM16(camera+0xc4)=0x6800; /* 146.25 degrees horizontally */
            MEMF(camera+0xc0)=QUEST_MIRROR_REAR_FAR;
            memcpy(saved,rear,sizeof rear);
            uint32_t info[2]={MEM32(0x3ea638u),MEM32(0x3ea63cu)},masks[2];
            extern uint32_t g_xbox_contig_size;
            quest_cockpit_mirror_masks(cockpit_ui_read,NULL,info,masks,0x80000000u+g_xbox_contig_size);
            quest_xr_mirror_camera(cockpit_player_car,&source,masks);
            MEMF(camera+0xbc)=.18f;
            memcpy((void *)XBOX_PTR(target),rear,sizeof rear);rear_adjusted=1;
        }
    }
    /* Mirrors show the road and traffic; exclude the local shell/interior. */
    if(player_rear)MEM32(camera+0x280)=MEM32(esi+0x2c);
    if(main_camera && (MEM32(0x00428e34u)!=6 || MEM32(esp)!=0x000612d2u)) {
        restored_mover=restored_car=0;
        if(MEM32(0x00428e34u)!=6)quest_xr_cockpit_select(0);
    }
    int driving_mover=main_camera && MEM32(esp)==0x000612d2u &&
        xyz_xbox_address(mover,0x60) && MEM32(mover)==0x00344880u &&
        MEM32(mover+0x10)==1 && MEM32(mover+0x1c)==camera && MEM32(0x00428e34u)==6;
    uint32_t car=0;
    if(driving_mover) {
        static uint32_t last_mover, last_mode=~0u;
        uint32_t mode=MEM32(mover+0x4c);
        if(mover!=last_mover || mode!=last_mode) {
            fprintf(stderr,"[Cockpit] driving camera mover=%08x mode=%u requested=%u\n",
                mover,mode,MEM32(mover+0x48));
            last_mover=mover; last_mode=mode;
        }
        car=MEM32(mover+0x2c);
        if(xyz_xbox_address(car,0x600) && !cockpit_replay_camera_selected(MEM32(0x449414u)))
            cockpit_restore_camera(mover,car);
    }
    /* Retail enters stage 1 for the roll-in (0x1FA8FD), then stages 2/3
     * for the grid/countdown. The cinematic mover can target an opponent.
     * Fit our seat to the local car, independently of transmission or speed.
     * Stage 4 hands the view back to the player's chosen driving camera. */
    int startline=0;
    if(main_camera && MEM32(0x428e34u)==6 && MEM32(0x3cc470u) &&
       quest_xr_enabled() && quest_xr_cockpit_enabled() && !cockpit_replay_open()) {
        int stage=cockpit_current_race_stage();
        if(stage>=1 && stage<=3) {
            car=cockpit_local_car();
            startline=1;
        }
    }
    if(xyz_xbox_address(car,0x600) && xyz_xbox_address(target,64)) {
        float rotation[12], position[3], matrix[16];
        /* Car and type addresses can be reused when changing vehicles. */
        static char fitted_name[33];
        static int fitted_index;
        uint32_t type=MEM32(car+0x0c);
        char name[33]={0};
        if(xyz_xbox_address(type,32))memcpy(name,(const void *)XBOX_PTR(type),32);
        if(strcmp(name,fitted_name)) {
            fitted_index=quest_cockpit_vehicle_index(name);
            memcpy(fitted_name,name,sizeof fitted_name);
            const QuestCockpitVehicle *fit=quest_cockpit_vehicle(fitted_index);
            fprintf(stderr,"[Cockpit] vehicle fit %s: seat %.3f %.3f %.3f, cabin %.3f %.3f %.3f\n",
                fit->name,fit->seat[0],fit->seat[1],fit->seat[2],fit->near_width,fit->far_width,fit->front_depth);
        }
        const QuestCockpitVehicle *fit=quest_cockpit_vehicle(fitted_index);
        uint32_t windows[QUEST_COCKPIT_WINDOWS];
        extern uint32_t g_xbox_contig_size;
        quest_cockpit_window_textures_range(cockpit_ui_read,NULL,car,windows,0x80000000u+g_xbox_contig_size);
        quest_xr_cockpit_vehicle_publish(car,fitted_index,windows);
        memcpy(rotation,(const void *)XBOX_PTR(car+0x90),sizeof rotation);
        memcpy(position,(const void *)XBOX_PTR(car+0x50),sizeof position);
        /* User seat offsets belong to the common XR eye transform.
         * Keep the game's base camera stable for body/mirror identity. */
        available=quest_cockpit_view(matrix,rotation,position,fit->seat);
        if(available && (startline || quest_xr_cockpit_requested())) {
            memcpy(saved,(const void *)XBOX_PTR(target),sizeof saved);
            memcpy((void *)XBOX_PTR(target),matrix,sizeof matrix);
            /* Keep the original body and hood. The fitted cabin asset
             * omits its window pillars to avoid overlapping frames. */
            MEM32(camera+0x280)=0;
            cockpit_player_car=car;
            active=1;
        }
    }
#endif
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(main_camera && MEM32(0x428e34)==6 && xyz_xbox_address(target,64))
        quest_xr_copchase_camera((const float*)XBOX_PTR(target));
#endif
    sub_00055C80_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(active || rear_adjusted) memcpy((void *)XBOX_PTR(target),saved,sizeof saved);
    if(main_camera) quest_xr_cockpit_publish(available,active);
#endif
}

/* CarRenderInfo::Render also hides a high-detail car when the main camera
 * (raised 0.5 m) lies inside its bounds. Clearing Camera+0x280 alone does not
 * bypass that second exclusion: it removes the whole Lancer Evo VIII at our
 * cockpit seat, while shorter shells happen to survive. Keep the retail
 * point/box test for every other caller, car and view. Its only body-hide
 * call is 0x20C27D; the caller has already checked main eView id 1.
 * Running the generated test first preserves its register/x87 side effects. */
extern void sub_0004CA40_gen(void);
void sub_0004CA40(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    int keep_body=0;
    if(MEM32(esp)==0x0020c282u && cockpit_player_car &&
       MEM32(0x00428e34u)==6 && quest_xr_cockpit_active()) {
        uint32_t info=MEM32(cockpit_player_car+0x554);
        keep_body=info && MEM32(esp+4)==info+0x10 && MEM32(esp+8)==info+0x20;
    }
#endif
    sub_0004CA40_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(keep_body && eax) {
        static uint32_t reported_car;
        if(reported_car!=cockpit_player_car) {
            reported_car=cockpit_player_car;
            fprintf(stderr,"[Cockpit] own body retained inside camera bounds: car=%08x\n",cockpit_player_car);
        }
        eax=0;
    }
#endif
}

#if defined(__ANDROID__) || defined(NFSU2_PCVR)
/* Use the game's owner-managed rear-camera mover and normal car tracking.
 * The cockpit matrix hook enables its own shell. The profile may have the
 * mirror disabled, so enabling the render target alone is insufficient. */
static void cockpit_rear_camera(void *context,uint32_t manager,int enable)
{
    (void)context;
    cockpit_guest_call(sub_001E2080,manager,0xe1f0bu,1,enable?1u:0u,0);
}
#endif

/* The driving input at 0x1f2c69 cycles the profile camera through 0,2,4,1
 * (sub_113250 / sub_113200), not the six-slot director at sub_1eba00.
 * Insert cockpit at that profile cycle's wrap without changing save values. */
extern void sub_001E32E0_gen(void);
void sub_001E32E0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t manager=ecx;
    if(quest_xr_enabled() && MEM32(esp)==0x001f2c6eu &&
       xyz_xbox_address(manager,0x250) && MEM32(0x00428e34u)==6) {
        /* A recorded driving camera click belongs to the original race. */
        if(cockpit_replay_camera_selected(manager)) {
            uint32_t mover=cockpit_replay_track_mover(manager);
            eax=mover?MEM32(mover+0x48):0;esp+=8;return;
        }
        uint32_t mover=MEM32(manager+0x238);
        if(xyz_xbox_address(mover,0x60) && MEM32(mover)==0x00344880u &&
           MEM32(mover+0x10)==1 && MEM32(mover+0x1c)==MEM32(0x003f5b70u)) {
            uint32_t player=MEM32(manager+0x10);
            if(player<2) {
                uint32_t slot=MEM32(0x00406d34u+player*40u);
                unsigned last=cockpit_last_slot();
                int direction=(int32_t)MEM32(esp+4);
                int action=quest_cockpit_cycle(slot,last,direction,
                    quest_xr_cockpit_requested(),quest_xr_cockpit_enabled(),&cockpit_entry_direction);
                fprintf(stderr,"[Cockpit] driving camera event slot=%u/%u direction=%d action=%d\n",
                    slot,last,direction,action);
                if(action) quest_xr_cockpit_select(action==1);
                if(action==1 || action==3) {
                    if(player==0)quest_xr_camera_remember(action==1?(direction==1?5:6):(int)slot);
                    eax=MEM32(mover+0x48);
                    esp+=8; return; /* thiscall, one stack argument */
                }
                if(player==0 && (direction==1 || direction==-1)) {
                    sub_001E32E0_gen();
                    quest_xr_camera_remember((int)MEM32(0x00406d34u));return;
                }
            }
        }
    }
#endif
    sub_001E32E0_gen();
}

/* ReplayControl destructor. A new viewer may reuse this address between
 * two guest frame polls. */
extern void sub_000BF140_gen(void);
void sub_000BF140(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(ecx==MEM32(0x3f9c64u)) {
        cockpit_replay_camera_end();
        quest_xr_cockpit_menu_publish(QUEST_UI_MENU,0);
    }
#endif
    sub_000BF140_gen();
}
/* The frontend's own Change Camera action shares the Grip cycle. The
 * automatic director runs as usual until the viewer chooses a camera. */
extern void sub_001EBA00_gen(void);
void sub_001EBA00(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(quest_xr_enabled() && cockpit_replay_camera_context(ecx)) {
        cockpit_replay_camera_next(ecx,1);esp+=4;return;
    }
#endif
    sub_001EBA00_gen();
}
extern void sub_001EBA80_gen(void);
void sub_001EBA80(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(cockpit_replay_camera_selected(ecx)) {
        /* A seek can replace the mover. Reacquire it from the live player
         * each tick, before the game computes this frame's camera pose. */
        cockpit_replay_camera_apply(ecx,0);esp+=4;return;
    }
#endif
    sub_001EBA80_gen();
}

/* Retail eView 3 is the independently updated rear-mirror camera. The normal
 * view selector disables its render target in the hood/bumper camera retained
 * underneath our extra cockpit slot. Enable just that view while driving in
 * the cockpit; the next retail update restores it on leaving the cockpit.
 * Do not change the profile option or invoke the look-back camera. */
extern void sub_000A1800_gen(void);
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
static int cockpit_ui_read(void *context,uint32_t address,void *out,size_t size)
{
    (void)context;
    /* Guest UI and car render data can use the physical-memory window.
     * It is distinct from low virtual memory in this runtime. */
    if(!xyz_xbox_address(address,size))return 0;
    memcpy(out,(const void *)XBOX_PTR(address),size);
    return 1;
}
/* The FE renderer sets its per-package draw gate at 0x129557, then tests
 * it for each object at 0x12953C. Chyron_IG is the in-race EA TRAX banner
 * selected at 0xC67A2. Leave its timer, song events and frontend state live. */
static void cockpit_hud_package_begin(void)
{
    uint32_t renderer=ecx,package=MEM32(esp+4),caller=MEM32(esp);
    sub_00129550();
    if(caller!=0x14311cu || !cockpit_player_car || MEM32(0x428e34u)!=6 ||
       MEM32(package+0x10)!=0x26691d4au || !MEM8(renderer+6))return;
    int ui=quest_xr_cockpit_menu_active();
    if(quest_xr_cockpit_active() && (ui==QUEST_UI_HUD || ui==QUEST_UI_REPLAY))
        MEM8(renderer+6)=0;
}
/* Use the same EA TRAX player as the retail jukebox (0x174A2D). Its
 * in-game volume slider at 0xB4A17 changes exactly mix+0x0C before
 * committing the option block. All guest audio calls stay on this thread. */
/* The retail automatic pause state (5) is forcibly resumed at 0x90289
 * during racing. Use its underlying EA stream pause call instead. Resolve
 * the current music handle exactly as 0x282680 does, including its slot mask.
 * Voice+3 is the actual transport pause flag written at 0x2826E8. */
static uint32_t cockpit_music_voice(uint32_t handle,uint32_t *bank)
{
    if(handle==~0u || !(handle&0xffffffu))return 0;
    for(unsigned i=0;i<4;++i) {
        uint32_t base=0x46b038u+i*0x928u,group=MEM32(base+0x30)&handle;
        if(!MEM32(base+0x34) || !(group&0xf0000000u) || !(group&0x0f000000u))continue;
        for(unsigned slot=0;slot<24;++slot)if(handle&(1u<<slot)) {
            uint32_t voice=MEM32(base+0x58+4*slot);
            if(xyz_xbox_address(voice,0x58)){*bank=base;return voice;}
        }
    }
    return 0;
}

/* The US music bank has one 32-byte "start node" event per EA TRAX song.
 * Only private, still-pending copies of this exact voice command may be
 * replaced. The event templates and other EA sound banks stay engine-owned. */
static int cockpit_radio_event(uint32_t event)
{
    uint32_t header=MEM32(event+12),track=header&0xffu;
    return track<27 && header==(0x10000u|track) && MEM32(event+16)==0xf1000001u &&
           MEM32(event+24)==0x400u && (MEM32(event+28)&~0x01000000u)==(0x00ff0000u+track*3);
}

static int cockpit_radio_start(uint32_t bank,int track)
{
    if(!bank || track<0 || track>=27 || !MEM8(0x46d4e4u) || MEM32(0x46d4e8u))return 0;
    uint32_t sequence=MEM32(bank+0x34),events=MEM32(bank+0x40);
    if(!xyz_xbox_address(sequence,20) || MEM8(sequence+15)<=track ||
       !xyz_xbox_address(events,2u*(track+1)))return 0;
    uint32_t source=sequence+4*(int16_t)MEM16(events+2*track);
    if(!xyz_xbox_address(source,32) || !cockpit_radio_event(source) || MEM8(source+12)!=track)return 0;
    /* Use the sequencer's bank selection, private event allocator and enqueue
     * from 0x282E70. The normal preview stops the channel before enqueuing a
     * non-interrupting event. With a pending start timer that strands the
     * voice at node -1 and blocks every subsequent event (0x284A2B). */
    uint32_t bank_index=(bank-0x46b038u)/0x928u;
    if(!cockpit_guest_call(sub_00284290,0,0x282f01u,1,bank_index,0))return 0;
    uint32_t count=MEM32(bank+0x91c),pending=0;
    int accepted=0;
    for(unsigned i=0;i<count && count<=16;++i) {
        uint32_t event=MEM32(bank+0xdc+4*i);
        if(event<bank+0x11c || event>bank+0x91c-32 || !cockpit_radio_event(event))continue;
        /* Let the sequencer retire superseded starts through its normal
         * cursor/compaction path (0x282DF0), even if all 16 slots are full. */
        if(pending)MEM32(pending+12)|=1u<<22;
        pending=event;
    }
    if(pending || count<16) {
        uint32_t event=pending?pending:cockpit_guest_call(sub_00282AA0,0,0x282f35u,1,source,0);
        if(event>=bank+0x11c && event<=bank+0x91c-32) {
            MEM32(event+12)=MEM32(source+12);
            /* Bit 24 selects the sequencer's immediate track change at
             * 0x2849AC. It stops and starts as one operation on the voice. */
            MEM32(event+28)=MEM32(source+28)|0x01000000u;
            accepted=pending || (int32_t)cockpit_guest_call(sub_00282A10,0,0x282f42u,2,MEM32(bank+0x30),event)==0;
        }
    }
    MEM32(0x46d4e8u)=0;MEM32(0x46d4d8u)=0;
    return accepted;
}
/* Same title and artist pointers as the stock EA TRAX notification, 0xC6862.
 * The 27 records and their strings reside in the retail executable. */
static const QuestRadioSong *cockpit_radio_song(int index)
{
    static int cached=-2;
    static QuestRadioSong song;
    if(index==cached)return &song;
    cached=index;song=(QuestRadioSong){0};
    if(index<0 || index>=27)return &song;
    char *fields[]={song.title,song.artist};
    for(int i=0;i<2;++i) {
        uint32_t addr=MEM32(0x3924c0u+16u*index+4u*i);
        if(!xyz_xbox_address(addr,sizeof song.title))continue;
        const char *src=(const char *)XBOX_PTR(addr);
        size_t len=0;
        while(len<sizeof song.title-1 && src[len])++len;
        if(len>=2 && src[0]=='"' && src[len-1]=='"'){++src;len-=2;}
        memcpy(fields[i],src,len);
    }
    return &song;
}
/* Follow the EA stream's active sound handle (0x2723A7 / 0x275F86).
 * Its channel list at +4 contains the voice IDs used by 0x27577E.
 * The software mixer walks the corresponding 0x5C-byte records at 0x279884. */
static void cockpit_radio_voices(uint32_t stream,uint32_t voices[2])
{
    uint32_t slot=MEM32(stream+0x18),state=MEM32(stream+0x10);
    if(slot>=MEM8(0x46a346u) || !xyz_xbox_address(state,8) || MEM32(0x469ed0u+4*slot)!=state)return;
    uint32_t handle=MEM32(state+4),id=handle&255u,table=MEM32(0x46a4d4u);
    unsigned first=MEM8(0x46a335u),soft=MEM8(0x46a670u);
    uint32_t base=MEM32(0x46a848u);
    if(id>=MEM16(0x46a474u) || !xyz_xbox_address(table,(id+1)*0x90) || !soft || !xyz_xbox_address(base,soft*0x5c))return;
    uint32_t sound=table+id*0x90;
    unsigned channels=MEM8(sound+0x23);
    if(MEM32(sound)!=handle || !MEM8(sound+0x6d) || !channels || channels>2)return;
    for(unsigned ch=0;ch<channels;++ch) {
        unsigned index=MEM16(sound+4+2*ch)-first;
        if(index<soft)voices[ch]=base+index*0x5c;
    }
}

/* At 0x2798D2 the mixer renders one channel into a temporary float buffer
 * (16-bit PCM units). Its per-speaker gains are applied later at 0x279942. */
static void cockpit_radio_channel(recomp_func_t render)
{
    static RECOMP_TLS unsigned depth;
    uint32_t voice=esi,capacity=MEM32(esp+8),dest=MEM32(esp+16);
    ++depth;render();--depth;
    /* The resampler can call the decoder through the same hook. Capture
     * only the outer output at mixer rate; concatenating source-rate and
     * resampled blocks distorts the spectrum and audio diagnostics. */
    if(depth)return;
    uint32_t frames=eax;
    if(frames && frames<=capacity && frames<=4096 && xyz_xbox_address(dest,frames*sizeof(float))) {
        vr_music_stock_samples(voice,(const float *)XBOX_PTR(dest),frames);
        /* Owned headless QA: isolate the EA song from engines and the custom
         * music mixer. No capture without both explicit probe and pad script. */
        static int probe=-1,done;static unsigned used;static uint32_t capture_voice,capture_node;static float samples[48000];
        if(probe<0)probe=getenv("NFSU2_STOCK_AUDIO_PROBE") && getenv("RECOMP_PAD_SCRIPT");
        if(probe && !done && MEM32(0x428e34)==6){
            uint32_t stream=MEM32(0x3e5afc),voices[2]={0};
            if(xyz_xbox_address(stream,0x1a4))cockpit_radio_voices(stream,voices);
            if(voice==voices[0] && voice){
                uint32_t node=MEM32(0x3e5140);
                if(voice!=capture_voice || node!=capture_node){capture_voice=voice;capture_node=node;used=0;}
                unsigned take=frames;if(take>48000-used)take=48000-used;
                memcpy(samples+used,(const void*)XBOX_PTR(dest),take*sizeof(float));used+=take;
                if(used==48000){done=1;const char *path=getenv("NFSU2_STOCK_AUDIO_PROBE");FILE *f=path?fopen(path,"wb"):NULL;
                    if(f){fwrite(samples,sizeof(float),used,f);fclose(f);}
                    fprintf(stderr,"[VR music probe] EA channel=%08x node=%u samples=%u written=%d\n",voice,MEM32(0x3e5140),used,f!=NULL);
                }
            }
        }
    }
}
static void cockpit_radio_resampled(void) { cockpit_radio_channel(sub_0027BA23); }
static void cockpit_radio_decoded(void) { cockpit_radio_channel(sub_0027C5EB); }

static void cockpit_radio(int driving,int replay,int cockpit)
{
    static uint32_t paused_player,owner,owner_handle;
    static uint32_t custom_player,custom_handle,custom_voice;
    static int requested=-1,submitted,last_played=-1;
    uint32_t system=MEM32(0x391f84u),stream=MEM32(0x3e5afcu),player=0;
    if(xyz_xbox_address(system,0x94) && xyz_xbox_address(stream,0x1a4) &&
       MEM32(system+0x38)==0x406ce8u)
        player=MEM32(system+0x90);
    int live=xyz_xbox_address(player,0xd4) && MEM32(player)==0x346744u;
    int race=live && MEM32(0x00428e34u)==6 && (replay || MEM32(system+0x80)!=1);
    uint32_t handle=live?MEM32(stream+0x17c):0;
    uint32_t bank=0,voice=live?cockpit_music_voice(handle,&bank):0;
    /* Both playing states enter the retail pause path at 0x90030. Resume
     * can leave the player in state 3 (0x88831 / 0x8889F). */
    unsigned mode=race?MEM32(player+0xc0):0;
    /* Replay can retain the current track in retail's pause state (5).
     * +0xc4 is the previous transport state saved by 0x8ed5d. */
    unsigned previous=mode==5?MEM32(player+0xc4):0;
    int valid=cockpit_player_car && cockpit && driving && race && voice && (mode==3 || mode==6 || (replay && (previous==3 || previous==6)));
    int paused=voice && MEM8(voice+3);
    if(paused_player && (!race || player!=paused_player)) {
        if(live && player==paused_player && paused==1 && !MEM8(player+0xa6))
            cockpit_guest_call(sub_00282630,0,0x8e997u,2,handle,0);
        paused_player=0;
    }
    float volume=MEMF(0x406cf4u);
    /* Retail's options can also supply exact zero. Keep the song clock
     * running in the cockpit with the same inaudible gain as the VR slider. */
    if(volume==0.f && cockpit && driving && race)MEMF(0x406cf4u)=QUEST_RADIO_SILENT_GAIN;
    float level=isfinite(volume) && volume>QUEST_RADIO_SILENT_GAIN?volume:0.f;
    QuestRadioSong custom_song={0};
    int custom_paused=0;
    int pursuit_music=community_copchase_music && driving && race;
    VrRadioPlayback playback=vr_music_update(driving && race,!pursuit_music && (!voice || paused),level,&custom_song,&custom_paused);
    int custom=playback==VR_RADIO_CUSTOM;
    int mute_original=playback!=VR_RADIO_ORIGINAL || pursuit_music;
    /* Only take ownership of a playing retail music voice. Leave pauses
     * imposed by the frontend or the player to their original owner. */
    if(custom_player && (!mute_original || player!=custom_player || handle!=custom_handle || voice!=custom_voice)) {
        int resume_pending=0;
        if(live && player==custom_player && handle==custom_handle && voice==custom_voice && paused && !paused_player) {
            if(!MEM8(player+0xa6) && MEM32(player+0xc0)!=5) {
                cockpit_guest_call(sub_00282630,0,0x8e997u,2,handle,0);
                paused=MEM8(voice+3)!=0;
            }
            /* Retry after a frontend pause or a busy sequencer. */
            resume_pending=paused;
        }
        if(!resume_pending)custom_player=0;
    }
    if(mute_original && voice && !paused) {
        cockpit_guest_call(sub_00282630,0,0x8e997u,2,handle,1);
        if(MEM8(voice+3)) {custom_player=player;custom_handle=handle;custom_voice=voice;}
    }
    if(!valid || owner!=player || owner_handle!=handle){requested=last_played=-1;submitted=0;}
    owner=valid?player:0;owner_handle=handle;
    /* The polled sample node (+0) can survive a stop. Retail tests the
     * sequencer node (+0x24) for -1 at 0x88C16. Check the live voice too. */
    int playing=-1;
    if(valid && MEM32(stream+0x178)==handle)
        playing=quest_radio_playing(MEM32(0x3e5140u),MEM32(0x3e5164u),MEM16(voice+0x36));
    int spectrum_track=!mute_original && !paused && !paused_player?playing:-1;
    uint32_t voices[2]={0};
    if(spectrum_track>=0)cockpit_radio_voices(stream,voices);
    vr_music_stock_track(spectrum_track,voices[0],voices[1]);
    if(playing>=0)last_played=playing;
    if(submitted && playing==requested){requested=-1;submitted=0;}
    int interactive=valid || (custom && cockpit);
    const QuestRadioSong pursuit_song={"POLICE PURSUIT","VITORINI MODS"};
    QuestRadioCommand cmd=quest_xr_radio_exchange(interactive?cockpit_player_car:0,level,
        pursuit_music?0:(custom?custom_paused:(paused_player!=0 || paused)),
        pursuit_music?&pursuit_song:(custom?&custom_song:cockpit_radio_song(playing)));
    if(!interactive)return;
    if(cmd.action==QUEST_RADIO_VOLUME) {
        if(isfinite(cmd.volume) && cmd.volume>=0 && cmd.volume<=1 && quest_radio_volume_changed(level,cmd.volume)) {
            MEMF(0x406cf4u)=quest_radio_mix_gain(cmd.volume);
            cockpit_guest_call(sub_00076A70,system,0xb4a2du,1,0x406ce8u,0);
        }
    }
    if(pursuit_music)return; /* Volume remains adjustable; restore song transport after pursuit. */
    if(custom) {
        if(cmd.action && cmd.action!=QUEST_RADIO_VOLUME)vr_music_command(cmd.action);
        return;
    }
    int current=requested>=0?requested:(last_played>=0?last_played:(int32_t)MEM32(player+0x14));
    uint32_t playlist=MEM32(player+0x8c);
    if(cmd.action==QUEST_RADIO_PREV || cmd.action==QUEST_RADIO_NEXT) {
        requested=quest_radio_track(current,playlist,cmd.action==QUEST_RADIO_NEXT?1:-1);
        submitted=0;
        if(requested>=0)paused_player=0;
    } else if(cmd.action==QUEST_RADIO_PLAY) {
        if(playing<0) {
            requested=current>=0 && current<27 && (playlist&(1u<<current))?current:quest_radio_track(current,playlist,1);
            submitted=0;paused_player=0;
        } else {
            paused_player=paused_player || paused?0:player;
            cockpit_guest_call(sub_00282630,0,0x8e997u,2,handle,paused_player!=0);
        }
    }
    if(requested>=0 && !submitted && cockpit_radio_start(bank,requested)) {
        if(paused)cockpit_guest_call(sub_00282630,0,0x8e997u,2,handle,0);
        MEM32(player+0x18)=MEM32(player+0x14);MEM32(player+0x14)=(uint32_t)requested;
        cockpit_guest_call(sub_00089230,stream,0x88c82u,1,1,0);
        submitted=1;
    } else if(paused_player && !paused && cmd.action!=QUEST_RADIO_PLAY) {
        cockpit_guest_call(sub_00282630,0,0x8e8f7u,2,handle,1);
    }
}
#endif
void sub_000A1800(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    /* A new race can replace both the owner and its camera without another
     * camera-button event. Reconcile the live registry before selecting views.
     * The helper never follows a pointer retained from a previous race. */
    static QuestRearCamera owned_rear_camera;
    static int was_racing;
    if(quest_xr_enabled()) {
        int racing=MEM32(0x00428e34u)==6;
        if(was_racing && !racing)quest_xr_shifter_stage(0,0,0,0);
        if(racing)cockpit_grid_update();
        was_racing=racing;
        if(!racing || (restored_mover && MEM32(0x003f5b74u)!=restored_mover+4))
            restored_mover=restored_car=0;
        extern uint32_t g_xbox_contig_size;
        int change=quest_rear_update_range(&owned_rear_camera,cockpit_ui_read,NULL,
            cockpit_rear_camera,quest_xr_cockpit_requested() || quest_xr_cockpit_active(),g_xbox_contig_size);
        if(change)
            fprintf(stderr,"[Cockpit] rear-camera lifetime %s: manager=%08x mover=%08x\n",
                change>0?"attached":"released",owned_rear_camera.manager,owned_rear_camera.rear_mover);
    }
#endif
    sub_000A1800_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    /* Preserve the driving-input gate. It remains true inside UI_pause.fng,
     * so it must not select the cockpit UI layout. Publish the actual active
     * frontend separately; the XR thread never reads mutable guest memory. */
    int driving=MEM32(0x00428e34u)==6 && !MEM8(0x0040245cu);
    /* JOY_EVENT_REQUEST_WORLD_MAP (0x1F2E5C) accepts career free roam
     * with no pending frontend request. Director event 18 requires stage 4+. */
    uint32_t map_director=MEM32(0x449438u);
    int world_map=driving && MEM32(0x41c338u)==1 && !MEM32(0x3cc470u) && !MEM8(0x405368u) &&
                  xyz_xbox_address(map_director,4) && (int32_t)MEM32(map_director)>=4;
    quest_xr_driving_publish(driving,world_map,driving && quest_xr_enabled()?cockpit_engine_rumble():0);
    int menu=quest_cockpit_ui_menu(cockpit_ui_read,NULL);
    uint32_t replay=cockpit_replay_open()?MEM32(0x3f9c64u):0;
    unsigned camera_steps=quest_xr_cockpit_menu_publish(menu,replay);
    if(replay!=replay_camera_owner)cockpit_replay_camera_end();
    if(quest_xr_enabled()) {
        /* ReplayControl registers itself at 0xd8434 and clears the pointer
         * in its destructor. Use its own hide/show method and state. */
        uint32_t owner=menu==QUEST_UI_REPLAY?replay:0;
        if(owner && camera_steps)cockpit_replay_camera_next(MEM32(0x449414u),camera_steps);
        if(quest_xr_take_replay_toggle(owner))
            cockpit_guest_call(sub_000BF7C0,owner,0xeeb69u,0,0,0);
        cockpit_radio(MEM32(0x00428e34u)==6 &&
                      (menu==QUEST_UI_REPLAY || (menu==QUEST_UI_HUD && !MEM8(0x0040245cu))),
                      menu==QUEST_UI_REPLAY,quest_xr_cockpit_active());
    }
    quest_xr_pause_publish(MEM32(0x00428e34u)==6?quest_cockpit_ui_paused(cockpit_ui_read,NULL):
        MEM32(0x00428e34u)==3 && quest_cockpit_ui_main(cockpit_ui_read,NULL)?2:0);
    if(quest_xr_cockpit_active() && MEM32(0x00428e34u)==6 &&
       MEM32(0x003f5c14u)==3 && MEM32(0x003f5c50u)==0x003efb20u) {
        uint32_t surface=MEM32(0x003f5c68u);
        if(surface==0x003e8148u) {
            MEM8(0x003f5c18u)=1;
            MEM32(surface+0x10)=1;
            static int reported;
            if(!reported) {
                reported=1;
                fprintf(stderr,"[Cockpit] rear mirror view enabled, camera=%08x mover-link=%08x\n",
                    MEM32(0x003f5c50u),MEM32(0x003f5c54u));
            }
        }
    }
#endif
}

#ifdef NFSU2_PCVR
/* Guest-thread state, published by the matrix builder before world traversal.
 * No renderer-thread access or per-object XR mutex acquisition is needed. */
static struct {
    uint32_t view;
    int valid;
    float planes[6][4];
} pcvr_world_views[32];
static unsigned long pcvr_world_rescued;
static FILE *pcvr_cull_trace;
static unsigned pcvr_cull_trace_count;

/* One primary-camera update interval, requested by the host's file service.
 * Include callers and all views: car-part tests may use an auxiliary view.
 * This diagnostic records decisions only; it never changes their results. */
static void pcvr_cull_trace_frame(void)
{
    extern int pcvr_take_cull_trace(void);
    if (pcvr_cull_trace) {
        fclose(pcvr_cull_trace); pcvr_cull_trace=NULL;
        fprintf(stderr,"[PCVR visibility] CPU trace complete: %u tests\n",pcvr_cull_trace_count);
    }
    if (pcvr_take_cull_trace()) {
        pcvr_cull_trace=fopen("frames/cull-tests.csv","w");
        pcvr_cull_trace_count=0;
        if(pcvr_cull_trace)
            fprintf(pcvr_cull_trace,"kind,caller,view,id,root,result,minx,miny,minz,maxx,maxy,maxz,plane,px,py,pz,pd\n");
    }
}

static void pcvr_cull_trace_box(uint32_t self,uint32_t caller,uint32_t result,
                                const float mn[3],const float mx[3])
{
    if(!pcvr_cull_trace || pcvr_cull_trace_count>=50000) return;
    uint32_t root=MEM32(self);
    int rejected=-1;
    float plane[4]={0};
    if(!result) for(int p=0;p<6;++p) {
        double farthest=MEMF(root+0x14c+16*p);
        for(int c=0;c<3;++c) {
            float n=MEMF(root+0x140+16*p+4*c);
            farthest+=(double)n*(n>=0?mx[c]:mn[c]);
        }
        if(farthest<0) { rejected=p; memcpy(plane,(const void *)XBOX_PTR(root+0x140+16*p),sizeof plane); break; }
    }
    ++pcvr_cull_trace_count;
    fprintf(pcvr_cull_trace,"box,%08x,%08x,%u,%08x,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%.9g,%.9g,%.9g,%.9g\n",
        caller,self,MEM32(self+4),root,result,mn[0],mn[1],mn[2],mx[0],mx[1],mx[2],
        rejected,plane[0],plane[1],plane[2],plane[3]);
}

static int pcvr_world_sphere_retained(uint32_t context, uint32_t point, float radius)
{
    static int enabled=-1;
    if (enabled<0) {
        const char *e=getenv("NFSU2_PCVR_WORLD_VISIBILITY");
        enabled=!(e && *e=='0');
    }
    if (!enabled) return 0;
    uint32_t view=MEM32(context+0x40);
    for (unsigned i=0;i<32;++i) {
        if (pcvr_world_views[i].valid && pcvr_world_views[i].view==view) {
            float p[3]={MEMF(point),MEMF(point+4),MEMF(point+8)};
            if (quest_culling_sphere(pcvr_world_views[i].planes,p,radius)) {
                ++pcvr_world_rescued;
                return 1;
            }
            return 0;
        }
    }
    return 0;
}
#endif

/* Build retail render matrices first, then replace only the main view's CPU
 * frustum. Rendering still needs the unmodified projection for reprojection.
 * The six shared planes also serve the game's sphere and point cull tests. */
extern void sub_000A33F0_gen(void);
void sub_000A33F0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t root = ecx, view = MEM32(esp + 4);
    uint32_t id = MEM32(view + 4), camera = MEM32(view + 0x40);
    /* Retail 0xAEB62 supplies a separate 60 m far override for eView 3.
     * The builder uses this argument for both the projection and all six
     * visibility planes, even when the mover specifies a longer distance. */
    if(MEM32(esp)==0x000aeb81u && view==0x003f5c10u && id==3 && camera==0x003efb20u &&
       cockpit_player_car && quest_xr_cockpit_active())
        MEMF(esp+12)=QUEST_MIRROR_REAR_FAR;
#ifdef NFSU2_PCVR
    if(id==1) pcvr_cull_trace_frame();
    if (id<32) pcvr_world_views[id].valid=0;
#endif
#endif
    sub_000A33F0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    static int enabled = -1, capture, main_id = 1;
    static uint32_t logged;
    if (enabled < 0) {
        const char *vr = getenv("NFSU2_VR"), *stereo = getenv("NFSU2_VR_STEREO");
        const char *cull = getenv("NFSU2_VR_CULL"), *probe = getenv("NFSU2_VR_CULL_CAPTURE");
        const char *select = getenv("NFSU2_VR_CULL_VIEW");
        enabled = !(vr && *vr == '0') && !(stereo && *stereo == '0') &&
                  !(cull && *cull == '0');
        capture = probe && *probe == '1';
        if (select) main_id = atoi(select);
    }
    if (!camera) return;
    if (capture && id < 32 && !(logged & (1u << id))) {
        logged |= 1u << id;
        fprintf(stderr, "[Quest] camera view=%u viewptr=%08x root=%08x camera=%08x near=%g far=%g\n",
                id, view, root, camera, MEMF(view + 0x10), MEMF(view + 0x14));
        char path[96];
        snprintf(path, sizeof path, "frames/camera-%02u.bin", id);
        FILE *f = fopen(path, "wb");
        if (f) {
            uint32_t header[4] = { 0x524d4143, view, root, camera };
            fwrite(header, sizeof header, 1, f);
            fwrite((const void *)XBOX_PTR(root), 0x1a0, 1, f);
            fwrite((const void *)XBOX_PTR(view), 0x70, 1, f);
            fclose(f);
        }
    }
    /* Views 4/6 use the primary camera for reflection/auxiliary passes. Their
     * visibility must cover the same tracked view as the main scene. */
    int primary = id == (uint32_t)main_id;
    if (main_id == 1 && (id == 4 || id == 6) && camera == MEM32(0x003f5b70u)) primary = 1;
    if (enabled && primary) {
        float planes[6][4], world[6][4], matrix[16];
        if (quest_xr_culling_planes(planes)) {
            memcpy(matrix, (const void *)XBOX_PTR(root), sizeof matrix);
            if (quest_culling_world(world, planes, matrix)) {
#ifdef NFSU2_PCVR
                if (id<32) {
                    pcvr_world_views[id].view=view;
                    memcpy(pcvr_world_views[id].planes,world,sizeof world);
                    pcvr_world_views[id].valid=1;
                }
                /* Runtime diagnostic only: isolate CPU frustum rejection from
                 * sector/LOD selection and GPU clipping without changing XR. */
                extern int pcvr_culling_override(void);
                if(pcvr_culling_override()) {
                    memset(world,0,sizeof world);
                    for(int p=0;p<6;++p) world[p][3]=1.0f;
                }
#endif
                memcpy((void *)XBOX_PTR(root + 0x140), world, sizeof world);
#ifdef NFSU2_PCVR
                if (id==1) {
                    static uint32_t previous;
                    uint32_t now=pcvr_counter_milliseconds();
                    if ((uint32_t)(now-previous)>=5000 && pcvr_world_rescued) {
                        fprintf(stderr,"[PCVR visibility] retained %lu world spheres rejected by the untracked camera\n",pcvr_world_rescued);
                        pcvr_world_rescued=0; previous=now;
                    }
                }
#endif
                static int reported;
                if (!reported) {
                    reported = 1;
                    fprintf(stderr, "[Quest] tracked stereo culling active for view %u\n", id);
                }
            }
        }
    }
#endif
}

/* The retail "Rain Drop" pass refracts 32x32 copies of the flat backbuffer
 * onto the camera lens. Those copies do not describe a tracked stereo lens.
 * Keep the separate world-rain pass; omit only this camera overlay in Quest
 * stereo. The original body is retained for cinema/non-Quest and comparison. */
extern void sub_000A6530_gen(void);
void sub_000A6530(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    static int omit = -1;
    if (omit < 0) {
        const char *vr = getenv("NFSU2_VR");
        const char *stereo = getenv("NFSU2_VR_STEREO");
        const char *lens = getenv("NFSU2_VR_LENS_RAIN");
        omit = !(vr && *vr == '0') && !(stereo && *stereo == '0') &&
               !(lens && *lens == '1');
        if (omit) fprintf(stderr, "[Quest] flat camera rain overlay disabled for stereo\n");
    }
    if (omit) {
        esp += 4; /* cdecl void(view): caller removes its argument. */
        return;
    }
#endif
    sub_000A6530_gen();
}

#if defined(__ANDROID__) || defined(NFSU2_PCVR)
/* Bounded snapshots of distinct guest view objects. They identify which
 * frustum belongs to the main camera before any game-side culling changes. */
static void quest_capture_frustum(uint32_t self)
{
    static int enabled = -1;
    static uint32_t seen[16];
    static unsigned count;
    if (enabled < 0) {
        const char *e = getenv("NFSU2_VR_CULL_CAPTURE");
        enabled = e && *e == '1';
    }
    if (!enabled || count == 16) return;
    uint32_t root = MEM32(self);
    for (unsigned i = 0; i < count; ++i) if (seen[i] == root) return;
    seen[count++] = root;
    char path[96];
    snprintf(path, sizeof path, "frames/cull-%08x.bin", root);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t header[4] = { 0x4c4c5543, self, root, 0x1a0 };
    fwrite(header, sizeof header, 1, f);
    fwrite((const void *)XBOX_PTR(root), 0x1a0, 1, f);
    fclose(f);
    fprintf(stderr, "[Quest] captured culling view self=%08x root=%08x\n", self, root);
}
#endif

/* ── Bounding box against the view frustum, sub_0009A330 ─────────────
 *
 * thiscall (this, const float min[3], const float max[3], matrix), ret 12.
 * The box's centre c = (max + min) * K and half-size e = max - c (both
 * stored as floats, as the original does), then for each of six planes
 * (n, d) at [this] + 0x140 + 16k: r = |n|.e, dist = n.c + d; below K2 on
 * dist + r means outside (return 0); below K2 on dist - r means the box
 * straddles that plane. Returns 2 when inside all six, 1 when straddling.
 * With a matrix the box is first transformed by it (sub_0009A250, below).
 *
 * It is the hottest function of NFSU2's main thread in a race (~5% on the
 * console): every object, every view, every frame. Written in C it keeps
 * everything in registers. The arithmetic is the lifted code's -- doubles,
 * in the same order -- so results match it exactly; RECOMP_NATIVE_CHECK=1
 * runs both and reports any difference. RECOMP_NATIVE=0 turns it off. */
extern void sub_0009A330_gen(void);
extern void sub_0009A250_gen(void);

/* sub_0009A250: cdecl (matrix, float min[3], float max[3]) -- the box's
 * corners transformed by a 4x4 row-major matrix, as a box again (Arvo): both
 * start at the translation row, and for each row i and column j the larger
 * of min[i]*M[i][j] and max[i]*M[i][j] goes to max[j], the smaller to min[j].
 * As the original: the max product is rounded to float before the compare
 * (it goes through memory), the sums are kept in extended (here double)
 * precision and stored as floats at the end, and an unordered compare adds
 * the min product to max. */
static void box_transform(uint32_t m, float mn[3], float mx[3])
{
    double hi[3], lo[3];
    int i, j;
    for (j = 0; j < 3; j++)
        hi[j] = lo[j] = (double)MEMF(m + 0x30u + 4u * j);
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            double mm = (double)MEMF(m + 16u * i + 4u * j);
            double e = (double)mn[i] * mm;
            double f = (double)(float)((double)mx[i] * mm);
            if (e < f) { hi[j] += f; lo[j] += e; }
            else       { hi[j] += e; lo[j] += f; }
        }
    for (j = 0; j < 3; j++) {
        mn[j] = (float)lo[j];
        mx[j] = (float)hi[j];
    }
}

static uint32_t frustum_box_native(uint32_t self, uint32_t pmin, uint32_t pmax,
                                   uint32_t matrix, uint32_t *out_ecx, uint32_t *out_edx)
{
    const double K = (double)MEMF(0x003408BCu), K2 = (double)MEMF(0x0033FA8Cu);
    float mn[3] = { MEMF(pmin), MEMF(pmin + 4), MEMF(pmin + 8) };
    float mx[3] = { MEMF(pmax), MEMF(pmax + 4), MEMF(pmax + 8) };
    float ax, ay, az, bx, by, bz;
    if (matrix)
        box_transform(matrix, mn, mx);
    ax = mn[0]; ay = mn[1]; az = mn[2];
    bx = mx[0]; by = mx[1]; bz = mx[2];
    float cx = (float)(((double)bx + (double)ax) * K);
    float cy = (float)(((double)by + (double)ay) * K);
    float cz = (float)(((double)bz + (double)az) * K);
    float ex = (float)((double)bx - (double)cx);
    float ey = (float)((double)by - (double)cy);
    float ez = (float)((double)bz - (double)cz);
    uint32_t planes = MEM32(self) + 0x144u, k;
    int straddle = 0;

    for (k = 0; k < 6; k++) {
        uint32_t p = planes + 16u * k;
        double nx = MEMF(p - 4), ny = MEMF(p), nz = MEMF(p + 4), d = MEMF(p + 8);
        double r = (fabs(ny) * ey + fabs(nz) * ez) + fabs(nx) * ex;
        double dist = ((cy * ny + cz * nz) + cx * nx) + d;
        if (dist + r < K2) {
            *out_ecx = p;
            *out_edx = k + 1;
            return 0;
        }
        if (dist - r < K2)
            straddle = 1;
    }
    *out_ecx = planes + 96u;
    *out_edx = 7;
    return straddle ? 1u : 2u;
}

void sub_0009A330(void)
{
    static int mode = -1;               /* 0 lifted, 1 native, 2 native + check */
    static unsigned long calls, with_matrix, mismatches;
    uint32_t self = ecx, pmin = MEM32(esp + 4), pmax = MEM32(esp + 8);
    uint32_t matrix = MEM32(esp + 12), r, rc, rd;

#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    quest_capture_frustum(self);
#endif
    if (mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
        mode = (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
    }
    calls++;
    with_matrix += matrix != 0;
    if (mode == 0) {
        sub_0009A330_gen();
        return;
    }
    r = frustum_box_native(self, pmin, pmax, matrix, &rc, &rd);
#ifdef NFSU2_PCVR
    if(pcvr_cull_trace) {
        float mn[3]={MEMF(pmin),MEMF(pmin+4),MEMF(pmin+8)};
        float mx[3]={MEMF(pmax),MEMF(pmax+4),MEMF(pmax+8)};
        if(matrix) box_transform(matrix,mn,mx);
        pcvr_cull_trace_box(self,MEM32(esp),r,mn,mx);
    }
#endif
    if (mode == 2) {
        sub_0009A330_gen();             /* pops its own arguments */
        if (eax != r && mismatches++ < 20)
            fprintf(stderr, "[native] sub_0009A330 mismatch: lifted %u native %u "
                    "(box %08X-%08X this %08X)\n", eax, r, pmin, pmax, self);
        if ((calls & 0xFFFFF) == 0)
            fprintf(stderr, "[native] sub_0009A330: %lu calls, %lu with a matrix, "
                    "%lu mismatches\n", calls, with_matrix, mismatches);
        return;
    }
    eax = r;
    ecx = rc;
    edx = rd;
    esp += 16;                          /* return address + three arguments */
}

/* sub_0009A250 on its own (it has other callers): box_transform on guest
 * memory. Returns max (eax), as the original leaves it; cdecl. */
void sub_0009A250(void)
{
    static int mode = -1;
    uint32_t m = MEM32(esp + 4), pmin = MEM32(esp + 8), pmax = MEM32(esp + 12);
    float mn[3], mx[3];
    int j;

    if (mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
        mode = (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
    }
    if (!mode) {
        sub_0009A250_gen();
        return;
    }
    for (j = 0; j < 3; j++) {
        mn[j] = MEMF(pmin + 4u * j);
        mx[j] = MEMF(pmax + 4u * j);
    }
    box_transform(m, mn, mx);
    if (mode == 2) {
        static unsigned long calls, bad;
        sub_0009A250_gen();             /* writes the guest's boxes itself */
        calls++;
        for (j = 0; j < 3; j++)
            if (memcmp(&mn[j], (const void *)XBOX_PTR(pmin + 4u * j), 4)
                || memcmp(&mx[j], (const void *)XBOX_PTR(pmax + 4u * j), 4)) {
                if (bad++ < 20)
                    fprintf(stderr, "[native] sub_0009A250 mismatch at %u: native %g %g,"
                            " lifted %g %g\n", j, mn[j], mx[j],
                            MEMF(pmin + 4u * j), MEMF(pmax + 4u * j));
                break;
            }
        if ((calls & 0xFFFFF) == 0)
            fprintf(stderr, "[native] sub_0009A250: %lu calls, %lu mismatches\n", calls, bad);
        return;
    }
    for (j = 0; j < 3; j++) {
        MEMF(pmin + 4u * j) = mn[j];
        MEMF(pmax + 4u * j) = mx[j];
    }
    eax = pmax;
    ecx = pmin + 12u;
    edx = m + 8u + 48u;
    esp += 4;                           /* cdecl: the caller pops the arguments */
}
void sub_002A9450(void) { crt_memmove(); }

/* ── D3D fence wait, D3D_BlockOnTime (0x002E8F20) ────────────
 *
 * stdcall (fence, flags), ret 8. The stock XDK 5849 D3D keeps its device at
 * 0x002F77A0, reached through the global at 0x002F7798:
 *   +0x2C  write sequence, bumped as fences are inserted
 *   +0x30  pointer to the semaphore the GPU releases as it completes work
 * and this spins until *(+0x30) catches up with the fence it was asked for.
 *
 * Without the pushbuffer executor nothing ever releases the semaphore, so the
 * GPU is reported caught up. With it (RECOMP_PB_EXEC) that would let D3D
 * reuse ring space and vertex memory the executor has not read yet (see the
 * toolkit's d3d8ltcg-device-context.md), so instead the real semaphore is
 * handed to the executor once and the original wait runs against it. */
#define NFSU2_D3D_DEVICE_PTR 0x002F7798u
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);
extern void sub_002E8F20_gen(void);

#ifdef NFSU2_PCVR
static RECOMP_TLS uint32_t pcvr_wait_device, pcvr_wait_target;
int nfsu2_guest_wait_completed(uint32_t object)
{
    uint32_t dev=pcvr_wait_device;
    if(!dev || object!=dev+0x1dccu) return -1;
    uint32_t sem=MEM32(dev+0x30u);
    if(!sem || !pcvr_fence_reached(MEM32(dev+0x2cu),MEM32(sem),pcvr_wait_target)) return 0;
    return 1;
}
#endif

void sub_002E8F20(void)
{
    static int exec = -1;
    uint32_t dev = MEM32(NFSU2_D3D_DEVICE_PTR);
    uint32_t sem = dev ? MEM32(dev + 0x30) : 0;

    if (exec < 0)
        exec = getenv("RECOMP_PB_EXEC") != NULL;
    if (exec) {
        static uint32_t registered;
        if (sem && sem != registered) {
            nv2a_pb_set_semaphore_target(sem);
            fprintf(stderr, "[D3D] GPU semaphore at 0x%08X\n", sem);
            registered = sem;
        }
        if (MEM32(esp) == 0x002E953Cu && MEM32(esp + 12) == 0x000AEDDBu) {
            /* The main loop (sub_000AEA90) calls BlockOnFence on the fence
             * of the frame it just built, right before Present: the whole
             * frame has to be through the GPU before the next one starts,
             * so the game and the pushbuffer executor take turns instead
             * of overlapping (Eden race: game waited ~50% of the time,
             * executor ~20% idle). RECOMP_FRAME_LAG=1 waits for the
             * previous frame's fence instead: one frame in flight, as on a
             * PC. Race frames checked on Linux, no corruption. */
            static int lag = -1;
            static uint32_t prev;
            if (lag < 0) {
                const char *e = getenv("RECOMP_FRAME_LAG");
                lag = e && *e == '1';
                fprintf(stderr, "[D3D] frame fence: %s\n",
                        lag ? "previous frame (RECOMP_FRAME_LAG=1)" : "this frame");
            }
            if (lag) {
                uint32_t mine = MEM32(esp + 4);
                if (prev)
                    MEM32(esp + 4) = prev;
                prev = mine;
            }
        }
#ifdef NFSU2_PCVR
        /* The Xbox patches NOP(5) into an already submitted ring. On the PC,
         * GET can pass that patch before its event is armed. Conversely a
         * delayed NOP can signal the next wait after an earlier wait returned
         * on its completed semaphore. Keep retail submission/patching, but
         * require the real completion predicate for this particular event. */
        uint32_t saved_device=pcvr_wait_device, saved_target=pcvr_wait_target;
        pcvr_wait_device=dev; pcvr_wait_target=MEM32(esp+4);
#endif
        sub_002E8F20_gen();
#ifdef NFSU2_PCVR
        pcvr_wait_device=saved_device; pcvr_wait_target=saved_target;
#endif
        return;
    }
    if (sem)
        MEM32(sem) = MEM32(dev + 0x2C);     /* "GPU" is always caught up */
    eax = 0;
    esp += 4 + 8;                           /* return address, 2 stdcall args */
}

/* ── D3D interrupt handlers at DISPATCH_LEVEL ────────────────
 *
 * D3D services the GPU from its DPC (0x002F2530) and, with the interrupt
 * masked, from its own busy-waits (BlockUntilIdle and friends) on the game
 * thread: both call the vblank handler (0x002F1D80) and the PGRAPH handler
 * (0x002F22F0), which queues flips. On the Xbox the two never interleave --
 * one is a DPC, the other runs with the interrupt off. Here the DPC runs on
 * the kernel timer thread, and lifted code hands over the guest lock at any
 * function entry, so the vblank count could move between D3D computing a
 * flip's target vblank and checking it. D3D retires a flip only when the
 * count *equals* its target, so that flip stayed pending forever and
 * PersistDisplay, which waits for pending flips, never returned: the hang
 * starting a Quick Race or Career. Run at DISPATCH_LEVEL (the dispatch lock,
 * kernel_hal.c), neither can be interrupted by the other.
 *
 * Raising blocks on the lock that the timer thread holds while it waits for
 * the guest lock, so the guest lock is let go for the raise, as a kernel
 * call would; already at DISPATCH (inside the DPC) there is nothing to take. */
extern uint8_t xbox_KfRaiseIrql(uint8_t irql);
extern void xbox_KfLowerIrql(uint8_t irql);
extern uint8_t xbox_CurrentIrql(void);
extern int xbox_gil_suspend(void);
extern void xbox_gil_resume(int depth);
extern void xbox_Nv2aVblankTaken(void);

static uint8_t d3d_isr_enter(void)
{
    uint8_t old = xbox_CurrentIrql();

    if (old < 2) {
        int d = xbox_gil_suspend();
        old = xbox_KfRaiseIrql(2);
        xbox_gil_resume(d);
    }
    return old;
}

static void d3d_isr_leave(uint8_t old)
{
    if (old < 2)
        xbox_KfLowerIrql(old);
}

/* Flips retired by a handler call: D3D's retire index, at +0x1BC of the
 * block both handlers take in ecx (their own reads index the pending-flip
 * slots from it). The pushbuffer executor's FLIP_STALL waits on these, as
 * PGRAPH waits on the retire's PGRAPH_INCREMENT write. */
extern void nv2a_pb_flip_retired(unsigned n);

/* Vblank handler, thiscall. It ends by writing PCRTC_INTR and spinning until
 * PMC_INTR bit 24 drops. Its callers (the DPC, D3D's busy-waits) have read
 * the bits before calling it, so this is the vblank being taken: clear them
 * now, and the spin ends at once instead of holding the guest lock until the
 * runtime's ack thread comes round. */
extern void sub_002F1D80_gen(void);

void sub_002F1D80(void)
{
    uint32_t blk = ecx;
    /* Read only once at DISPATCH: read before, a retire by the other
     * handler (DPC thread vs a D3D busy-wait) in between was counted by
     * both, the executor's flip index ran one off for good, and every
     * FLIP_STALL then waited out its 250 ms (Carbon, console, 3.7 fps). */
    uint8_t old = d3d_isr_enter();
    uint32_t head = MEM32(blk + 0x1BC);

    xbox_Nv2aVblankTaken();
    sub_002F1D80_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);
    d3d_isr_leave(old);
}

/* PGRAPH handler, thiscall, ecx = the device's hardware block (+0 =
 * 0xFD000000). Reads a software-method trap from PGRAPH_INTR / NSOURCE /
 * TRAPPED_ADDR / TRAPPED_DATA, acknowledges it by writing PGRAPH_INTR back,
 * and acts on it: NOP(5) sets the event BlockOnTime sleeps on, a flip trap
 * queues the flip (sub_002F2080). The acknowledge is write-1-to-clear on
 * hardware and a no-op on RAM. Left pending, every turn of a busy-wait took
 * the same trap again (tens of thousands of flips a second, until the flip
 * targets were millions of vblanks ahead); guessed from the handler's
 * PGRAPH_FIFO writes, a trap posted during a call was sometimes dropped (the
 * fence event never set). So the executor posts traps under a lock held here
 * around the call, and a call that found one reports it taken, which clears
 * it (nv2a_pb_exec.c). */
extern void sub_002F22F0_gen(void);
extern void nv2a_pb_trap_lock(void);
extern void nv2a_pb_trap_unlock(void);
extern void nv2a_pb_trap_taken(void);

void sub_002F22F0(void)
{
    volatile uint32_t *nv = (volatile uint32_t *)XBOX_PTR(0xFD000000u);
    uint32_t blk = ecx;
    uint8_t old = d3d_isr_enter();
    uint32_t head = MEM32(blk + 0x1BC);     /* at DISPATCH, as above */
    uint32_t nsource;

    nv2a_pb_trap_lock();
    nsource = nv[0x400108 / 4];
    sub_002F22F0_gen();
    nv2a_pb_flip_retired(MEM32(blk + 0x1BC) - head);   /* a flip queued on its vblank retires at once */
    if (nsource)
        nv2a_pb_trap_taken();
    nv2a_pb_trap_unlock();
    d3d_isr_leave(old);
}

#if defined(__ANDROID__) || defined(NFSU2_PCVR)
static int quest_60fps(void)
{
    static int option=-1;
    if(option<0) { const char *e=getenv("NFSU2_VR_60FPS"); option=e && *e=='1'; }
    return option && quest_xr_enabled();
}
static int quest_xr_paced(void)
{
    /* Refresh-rate selection is independent of game pacing. In particular,
     * REFRESH_HZ=0 keeps the runtime's rate; it must not restore the retail
     * 60 Hz flip wait or the simulation's minimum step during a race. */
    /* Only the race branch uses the adjustable minimum. Frontend/intro
     * timing retains the already tested 60 Hz path. */
    return quest_xr_enabled() && MEM32(0x3A4C50)!=0;
}
#endif

/* Keep the retail flip queue and its fences. The 60 FPS path caps interval
 * 2 to 1; the XR-paced race path requests an immediate flip. Neither
 * changes the guest vblank clock. Callers hold the D3D dispatch lock. */
extern void sub_002F2080_gen(void);
void sub_002F2080(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t slot=esp+4, original=MEM32(slot);
    int xr_paced=quest_xr_paced();
    if(quest_60fps() || xr_paced) {
        uint32_t updated=xr_paced?quest_present_xr(original):quest_present_60hz(original);
        MEM32(slot)=updated;
        static int reported;
        if(updated!=original && !(reported & (xr_paced?2:1))) {
            reported |= xr_paced?2:1;
            fprintf(stderr,"[Quest] presentation: %s (vblank remains 59.94 Hz)\n",
                    xr_paced?"OpenXR paced, immediate retail flip":"60 FPS, flip interval capped at 1");
        }
    }
#endif
    sub_002F2080_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    MEM32(slot)=original;
#endif
}

extern void sub_001890C0_gen(void);
void sub_001890C0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    int xr_paced=quest_xr_paced(), active=quest_60fps() || xr_paced;
    float minimum=MEMF(0x428EE8), elapsed=MEMF(esp+4);
    if(active) MEMF(0x428EE8)=xr_paced?quest_sim_minimum_xr(minimum):quest_sim_minimum_60hz(minimum);
#endif
    sub_001890C0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(active) {
        MEMF(0x428EE8)=minimum;
        static uint64_t start;
        static double input_time,sim_time;
        static unsigned calls;
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
        uint64_t now=(uint64_t)ts.tv_sec*1000000000ull+(uint64_t)ts.tv_nsec;
        if(start) {
            input_time+=elapsed; sim_time+=MEMF(0x428A0C); ++calls;
            if(now-start>=5000000000ull) {
                fprintf(stderr,"[Quest] simulation: %u updates, wall %.3f s, input %.3f s, simulated %.3f s, requested minimum %.4f s\n",
                        calls,(now-start)/1e9,input_time,sim_time,minimum);
                start=0; calls=0; input_time=sim_time=0;
            }
        }
        if(!start) start=now;
    }
#endif
}

#if defined(__ANDROID__) || defined(NFSU2_PCVR)
static int community_adapter_busy;
extern void sub_002194B0_gen(void);
#include "community/xyz_pool_bridge.inc"
static void community_pool_trace(const char *reason,uint32_t caller,uint32_t phase)
{
    uint32_t pool=MEM32(0x3e3fec),rides=MEM32(0x45afbc);
    fprintf(stderr,"[XYZ car pool] %s caller=%08x phase=%u size=%u used=%u free=%u rides=%u/%u loader=%u\n",
        reason,caller,phase,xyz_xbox_address(pool,0x50)?MEM32(pool+0x1c):0,
        xyz_xbox_address(pool,0x50)?MEM32(pool+0x20):0,
        xyz_xbox_address(pool,0x50)?MEM32(pool+0x28):0,
        xyz_xbox_address(rides,0x30)?MEM32(rides+0x18):0,
        xyz_xbox_address(rides,0x30)?MEM32(rides+0x2c):0,MEM32(0x45c4b4));
}
static int community_guest_write(void *context,uint32_t address,const void *data,size_t size)
{
    (void)context;
    if(!xyz_xbox_address(address,size))return 0;
    memcpy((void *)XBOX_PTR(address),data,size);return 1;
}
#include "community/xyz_paint_bridge.inc"
static int community_guest_call(void *context,enum XyzXboxOp op,uint32_t self,uint32_t second,
                                unsigned count,const uint32_t *args,uint32_t *result)
{
    (void)context;
    if(op==XYZ_G_PAINT_ALLOC || op==XYZ_G_PAINT_FREE)return community_paint_memory(op,count,args,result);
    if(op==XYZ_G_POOL_READY && !count && result){*result=community_pool_prepare();return 1;}
    if(op==XYZ_G_TYPE_AVAILABLE && count==1 && args && args[0]<46 && result){
        uint32_t types=MEM32(0x45af88);char name[32];
        if(!xyz_xbox_address(types,46*0x890))return 0;
        memcpy(name,(const void*)XBOX_PTR(types+args[0]*0x890),sizeof name);
        if(!memchr(name,0,sizeof name))return 0;
        *result=xyz_car_files_available(name);return 1;
    }
    static const struct {uint32_t address;unsigned arguments;} calls[XYZ_G_OP_COUNT]={
        {0x0003ed50,1},{0x001ff570,1},{0x001ff6b0,4},{0x00216350,1},
        {0x00207020,0},{0x00207620,1},{0x001b6d90,1},{0x00206d00,5},
        {0x0021ac20,3},{0x0021ac90,2},{0x001ffda0,1},{0x00207e00,1},{0x0021a120,1},
        {0x001f6650,2},{0x001dbe60,1},{0x001e4a70,1},{0x001f10c0,1},
        {0x001e6b40,0},{0x001e6ac0,0},{0x001e48b0,0},{0x0018e780,1},{0x001ca7e0,1},
        {0x000af510,4},{0x000ac200,3},{0x0009f040,1},{0x0009f2e0,1},
        {0x00096e60,2},{0x00206b00,5},{0x001ff430,0},
        {0,0},{0,0},{0x000a1580,3},{0x0009c860,0},{0,0},{0,0}
    };
    if((unsigned)op>=XYZ_G_OP_COUNT || count!=calls[op].arguments ||
       (count && !args) || !result)return 0;
    recomp_func_t function=recomp_lookup_manual(calls[op].address);
    if(!function)function=recomp_lookup(calls[op].address);
    if(!function)return 0;
    *result=guest_call_full(function,self,second,0x0018ab7fu,count,args);
    return 1;
}
static void community_socket_probe(const XyzXboxFrame *sample,uint64_t now)
{
    static int requested=-1,done;
    if(requested<0){const char *e=getenv("NFSU2_XYZ_XNET_PROBE");requested=e && *e=='1';}
    if(!requested || done || (sample->status!=XYZ_XBOX_READY && sample->status!=XYZ_XBOX_OTHER_RACE))return;
    done=1;
    uint32_t size=0x200,arena=0,objects[5]={0},args[6]={0},result=0;
    int stage=0,tcp=0,udp=0;
    if(!community_guest_call(NULL,XYZ_G_ALLOC,0,0,1,&size,&arena) || !xyz_xbox_address(arena,size))return;
    memset((void*)XBOX_PTR(arena),0,size);
    uint8_t *address=(uint8_t*)XBOX_PTR(arena);
    address[0]=2;address[2]=0x26;address[3]=0xac;address[4]=10;address[5]=77;address[6]=1;address[7]=2;
    memcpy((void*)XBOX_PTR(arena+16),address,16);
    MEM8(arena+18)=0x0e;MEM8(arena+19)=0x4a; /* 3658 */
    memcpy((void*)XBOX_PTR(arena+32),(const void*)XBOX_PTR(arena+16),16);MEM8(arena+35)=0x4c; /* 3660 */
    static const uint8_t payload[16]={'X','Y','Z',' ','X','N','E','T',' ','b','r','i','d','g','e','!'};
    memcpy((void*)XBOX_PTR(arena+64),payload,16);
    if(!xyz_xnet_probe_begin(g_fs_base))goto end;
    /* These are the original EA/DirtySock routines. Their direct XNet calls
     * reach the wrappers below, so this checks lifted code, ABI and the UDP
     * protocol-254 prefix, not just the portable fixture. No public socket. */
    for(unsigned i=0;i<2;++i){args[0]=2;args[1]=1;args[2]=0;
        objects[i]=guest_call_full(sub_00293380,0,0,0x0018ab7fu,3,args);
        if(!xyz_xbox_address(objects[i],0x50))goto close;
    }
    stage=1;args[0]=objects[0];args[1]=arena;args[2]=16;
    if(guest_call_full(sub_002934E0,0,0,0x0018ab7fu,3,args))goto close;
    args[1]=2;if(guest_call_full(sub_00293670,0,0,0x0018ab7fu,2,args))goto close;
    stage=2;args[0]=objects[1];args[1]=arena;args[2]=16;
    if(guest_call_full(sub_002935E0,0,0,0x0018ab7fu,3,args))goto close;
    xyz_xnet_probe_tick(now/1000);
    args[0]=objects[1];args[1]=0x73746174u;args[2]=args[3]=0;
    if(guest_call_full(sub_002939F0,0,0,0x0018ab7fu,4,args)!=1)goto close;
    stage=3;MEM32(arena+160)=16;args[0]=objects[0];args[1]=arena+176;args[2]=arena+160;
    objects[2]=guest_call_full(sub_002936D0,0,0,0x0018ab7fu,3,args);
    if(!xyz_xbox_address(objects[2],0x50))goto close;
    args[0]=objects[1];args[1]=arena+64;args[2]=16;args[3]=args[4]=args[5]=0;
    if(guest_call_full(sub_00293790,0,0,0x0018ab7fu,6,args)!=16)goto close;
    xyz_xnet_probe_tick(now/1000);
    args[0]=objects[2];args[1]=arena+96;args[2]=32;
    result=guest_call_full(sub_00293870,0,0,0x0018ab7fu,6,args);
    if(result!=16 || memcmp((const void*)XBOX_PTR(arena+96),payload,16))goto close;
    tcp=1;stage=4;
    for(unsigned i=3;i<5;++i){args[0]=2;args[1]=2;args[2]=0;
        objects[i]=guest_call_full(sub_00293380,0,0,0x0018ab7fu,3,args);
        if(!xyz_xbox_address(objects[i],0x50))goto close;
        args[0]=objects[i];args[1]=arena+(i==3?16:32);args[2]=16;
        if(guest_call_full(sub_002934E0,0,0,0x0018ab7fu,3,args) || MEM32(objects[i]+0x10)!=254)goto close;
    }
    stage=5;args[0]=objects[3];args[1]=arena+64;args[2]=16;args[3]=0;args[4]=arena+32;args[5]=16;
    if(guest_call_full(sub_00293790,0,0,0x0018ab7fu,6,args)!=16)goto close;
    xyz_xnet_probe_tick(now/1000);
    memset((void*)XBOX_PTR(arena+96),0,32);MEM32(arena+160)=16;
    args[0]=objects[4];args[1]=arena+96;args[2]=32;args[3]=0;args[4]=arena+176;args[5]=arena+160;
    result=guest_call_full(sub_00293870,0,0,0x0018ab7fu,6,args);
    udp=result==16 && !memcmp((const void*)XBOX_PTR(arena+96),payload,16) && MEM8(arena+178)==0x0e && MEM8(arena+179)==0x4a;
    stage=6;
close:
    for(unsigned i=0;i<5;++i)if(xyz_xbox_address(objects[i],0x50)){
        args[0]=objects[i];guest_call_full(sub_00293430,0,0,0x0018ab7fu,1,args);
    }
    xyz_xnet_probe_end();
end:
    fprintf(stderr,"[XYZ XNet] original guest SocketOpen/Bind/Connect/Info/Accept/Send/Recv/Close: stage=%d TCP=%d UDP254=%d last=%u; isolated loopback\n",stage,tcp,udp,result);
    community_guest_call(NULL,XYZ_G_FREE,0,0,1,&arena,&result);
}
#include "community/xyz_lobby_bridge.inl"
#include "community/xyz_lobby_probe.inl"
#include "community/xyz_race_link_probe.inl"
#include "community/xyz_races_bridge.inl"
#include "community/xyz_races_probe.inl"
#include "community/xyz_career_probe.inl"
#include "community/xyz_career_mods_probe.inl"
#include "community/xyz_races_pair_probe.inl"
#include "community/xyz_subtitles.inl"
#include "community/xyz_stance_bridge.inl"
#include "community/xyz_lights_bridge.inl"
#include "community/xyz_damage_bridge.inl"
#include "community/copchase_probe.inl"
#include "community/copchase_bridge.inl"
extern void sub_00210E00_gen(void);
#include "community/xyz_texture_lists.inl"
#include "community/xyz_regional_assets.h"
#include "community/xyz_xbox_regional.h"
static int community_regional_ready;
static void community_regional_frame(void){
    if(community_regional_ready || !xyz_regional_available("CORSA") || !xyz_regional_available("PEUGOT106"))return;
    XyzXboxGuest memory={NULL,cockpit_ui_read,community_guest_write,community_guest_call};
    community_regional_ready=xyz_xbox_regional_install(&memory);
    if(community_regional_ready)fprintf(stderr,"[VR regional] original car selection and 28-car AI list %s\n",community_regional_ready>0?"enabled":"rejected: unexpected Xbox tables");
}
#include "community/xyz_race_settings_probe.inl"
static void community_ride_probe(const XyzXboxFrame *sample)
{
    static int requested=-1,done;
    if(requested<0){const char *e=getenv("NFSU2_XYZ_RIDE_PROBE");requested=e && *e=='1';}
    if(!requested || done || !sample->config_size ||
       (sample->status!=XYZ_XBOX_READY && sample->status!=XYZ_XBOX_OTHER_RACE))return;
    done=1; /* guest loads can pump the game; never re-enter this diagnostic */
    XyzXboxGuest guest={NULL,cockpit_ui_read,community_guest_write,community_guest_call};
    uint32_t size=0x1200,allocation=0;
    if(!community_guest_call(NULL,XYZ_G_ALLOC,0,0,1,&size,&allocation) || !xyz_xbox_address(allocation,size)) {
        fprintf(stderr,"[XYZ ride] owned scratch allocation failed\n");return;
    }
    /* Stable for the process lifetime; no game object owns this test ride. */
    XyzXboxArena arena={allocation,size};
    uint32_t ride=(allocation+0xffu)&~0xffu,scratch=ride+0x900;
    XyzXboxRideResult result;
    int ok=xyz_xbox_build_ride(&guest,arena,ride,scratch,5,sample->config,sample->config_size,&result);
    uint8_t rebuilt[RC_CFG_MAX];
    int count=ok?xyz_xbox_ride_config(cockpit_ui_read,NULL,ride,rebuilt,sizeof rebuilt):0;
    int same=count==sample->config_size && !memcmp(rebuilt,sample->config,(size_t)count);
    fprintf(stderr,"[XYZ ride] reconstruction=%s type=%u fallback=%d missing=%u bytes=%d exact=%d owned=%08x\n",
        ok?"built":"failed",result.type,result.fallback_type,result.missing_parts,count,same,ride);
    const char *output=getenv("NFSU2_XYZ_RIDE_PROBE_OUT");
    if(count && output && *output) {
        FILE *file=fopen(output,"wb");
        if(file){fwrite(rebuilt,1,(size_t)count,file);fclose(file);}
    }
}
static void community_visual_probe(const XyzXboxFrame *sample)
{
    static int requested=-1,done;
    if(requested<0){const char *e=getenv("NFSU2_VISUAL_REP_PROBE");requested=e && *e=='1';}
    if(!requested || done || !quest_xr_visual_rep_remover() || !sample->ride ||
       (sample->status!=XYZ_XBOX_READY && sample->status!=XYZ_XBOX_OTHER_RACE))return;
    done=1;
    uint32_t ride=sample->ride,before=MEM32(0x3a37f8u);
    // The existing override branch returns before reading this/ride. Use the
    // actual current stage; never alter career progression for a diagnostic.
    guest_call_full(sub_00130300,0,0,0x0018ab7fu,1,&ride);
    fprintf(stderr,"[Community mods] visual rating probe: override preserved=%d\n",MEM32(0x3a37f8u)==before);
}
static void community_clone_probe(const XyzXboxFrame *sample,uint64_t now)
{
    static int requested=-1,busy,started,reported=-1,finished,configured;
    static XyzXboxClone clone;
    static uint64_t ready,last_motion;
    static uint64_t duration=10000000ull;
    static float base[3],max_position_error,max_rotation_error;
    static unsigned motion_frames,motion_failures,hidden_frames;
    if(requested<0){
        const char *e=getenv("NFSU2_XYZ_CLONE_PROBE");requested=e && *e=='1';
        const char *seconds=getenv("NFSU2_XYZ_CLONE_PROBE_SECONDS");
        if(seconds){long value=strtol(seconds,NULL,10);if(value>=10 && value<=300)duration=(uint64_t)value*1000000ull;}
    }
    if(!requested || busy || finished)return;
    XyzXboxGuest guest={NULL,cockpit_ui_read,community_guest_write,community_guest_call};
    busy=1;
    if(!started && sample->config_size &&
       (sample->status==XYZ_XBOX_READY || sample->status==XYZ_XBOX_OTHER_RACE)) {
        started=1;
        uint32_t size=0x3000,allocation=0;
        if(community_guest_call(NULL,XYZ_G_ALLOC,0,0,1,&size,&allocation) && xyz_xbox_address(allocation,size)) {
            XyzXboxArena arena={allocation,size};
            int ok=xyz_xbox_clone_begin(&guest,&clone,arena,sample->world,sample->car,5,now);
            fprintf(stderr,"[XYZ clone] begin=%d record=%08x handle=%08x world=%08x\n",ok,clone.record,clone.handle,clone.world);
        } else {fprintf(stderr,"[XYZ clone] allocation failed\n");finished=1;}
    }
    if(started && !finished) {
        int stage=xyz_xbox_clone_poll(&guest,&clone,now);
        if(stage!=reported) {
            fprintf(stderr,"[XYZ clone] stage=%d car=%08x indexed=%u\n",stage,clone.car,
                sample->world?MEM32(sample->world+0x10):0);
            reported=stage;
        }
        if(stage==XYZ_CLONE_READY && !configured) {
            uint8_t config[RC_CFG_MAX];int config_size=sample->config_size;
            memcpy(config,sample->config,config_size);
            const char *type=getenv("NFSU2_XYZ_CLONE_TYPE");
            if(type && *type && strlen(type)<32) {
                rc_carcfg decoded;
                if(rc_carcfg_decode(config,config_size,&decoded)==RC_OK) {
                    uint32_t types=MEM32(0x45af88);
                    if(xyz_xbox_address(types,46*0x890))for(unsigned i=0;i<46;++i) {
                        const char *name=(const char *)XBOX_PTR(types+i*0x890);
                        if(strncmp(name,type,32))continue;
                        snprintf(decoded.type_name,sizeof decoded.type_name,"%s",type);
                        decoded.type_hash=MEM32(types+i*0x890+0xd0);decoded.n_parts=0;
                        config_size=rc_carcfg_encode(&decoded,config,sizeof config);break;
                    }
                }
            }
            XyzXboxRideResult result={0};
            community_pool_trace("before clone look",0,0);
            int ok=xyz_xbox_clone_configure(&guest,&clone,config,config_size,now,&result);
            fprintf(stderr,"[XYZ clone] look begin=%d type=%u fallback=%d missing=%u\n",ok,result.type,result.fallback_type,result.missing_parts);
            configured=ok?1:3;
            stage=clone.stage;
            if(!ok)ready=now;
        } else if(stage==XYZ_CLONE_READY && configured==1) {
            uint8_t wire[RC_CFG_MAX];
            int n=xyz_xbox_ride_config(cockpit_ui_read,NULL,MEM32(clone.car+0x10),wire,sizeof wire);
            int exact=n==sample->config_size && !memcmp(wire,sample->config,(size_t)n);
            fprintf(stderr,"[XYZ clone] look applied bytes=%d exact=%d ride=%08x\n",n,exact,MEM32(clone.car+0x10));
            configured=2;ready=now;memcpy(base,sample->state.p,sizeof base);
            community_pool_trace("clone look ready",0,0);
        }
        if(stage==XYZ_CLONE_READY && configured==2 && sample->world==clone.world) {
            float t=(float)(now-ready)/1000000.f;
            if(t>=4.f && t<5.f) {
                if(!xyz_xbox_clone_hide(&guest,&clone))++motion_failures;
                ++hidden_frames;
            } else {
                rc_pose p;memset(&p,0,sizeof p);
                float angle=t*.3f,heading=angle+1.57079633f;
                p.p[0]=base[0]+6.f*cosf(angle);p.p[1]=base[1]+6.f*sinf(angle);p.p[2]=base[2];
                p.q[2]=sinf(heading*.5f);p.q[3]=cosf(heading*.5f);
                p.v[0]=-1.8f*sinf(angle);p.v[1]=1.8f*cosf(angle);p.w[2]=.3f;
                p.mode=RC_MODE_INTERP;p.speed=1.8f;p.rpm=2000;p.gear=1;p.throttle=.25f;p.steer=.1f;
                float dt=last_motion?(float)(now-last_motion)/1000000.f:0;
                rc_state actual;
                int ok=xyz_xbox_clone_pose(&guest,&clone,&p,dt) &&
                    xyz_xbox_car_state(cockpit_ui_read,NULL,clone.world,clone.car,(uint32_t)now,&actual);
                if(ok) {
                    float dot=0;
                    for(unsigned i=0;i<3;i++){float e=fabsf(p.p[i]-actual.p[i]);if(e>max_position_error)max_position_error=e;}
                    for(unsigned i=0;i<4;i++)dot+=p.q[i]*actual.q[i];
                    float error=fabsf(1.f-fabsf(dot));if(error>max_rotation_error)max_rotation_error=error;
                } else ++motion_failures;
                ++motion_frames;
            }
            last_motion=now;
        }
        if(stage==XYZ_CLONE_FAILED || sample->world!=clone.world ||
           (ready && now-ready>=duration)) {
            uint32_t oldcar=clone.car;
            int ok=xyz_xbox_clone_retire(&guest,&clone);
            fprintf(stderr,"[XYZ clone] retire=%d car=%08x remaining=%08x\n",ok,oldcar,
                oldcar && sample->world?xyz_xbox_find_car(cockpit_ui_read,NULL,sample->world,oldcar,0):0);
            fprintf(stderr,"[XYZ clone] motion frames=%u hidden=%u failures=%u max_position_error=%.8f max_rotation_error=%.8f\n",
                motion_frames,hidden_frames,motion_failures,max_position_error,max_rotation_error);
            finished=1;
        }
    }
    busy=0;
}
#endif

/* Original lobby Drop deliberately switches to its "keep friends suspended"
 * branch for a started game (19AEFB), even when called with zero. The XYZ LAN
 * path never initializes the Xbox Live friends manager. Its Suspend method
 * nevertheless calls BuddyApi with a null client, eventually clearing guest
 * 0x1c..0x4353 and erasing the main TIB. Skip only that absent optional client
 * at the audited Drop callsite; existing clients and other callers remain
 * original. No fake client or repaired TLS state is introduced. */
extern void sub_00190210_gen(void);
void sub_00190210(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_engine_lobby.initialized && community_engine_lobby.play_received &&
       ecx==0x42dc50 && xyz_xbox_address(esp,4) && MEM32(esp)==0x19b00e && !MEM32(ecx+0xf8)){
        esp+=4;return;
    }
#endif
    sub_00190210_gen();
}

/* Audited Xbox equivalent of CarLoader::Purge (PC 0x63a1b0). Phase 1 can
 * remove textures from referenced rides. Record pool pressure at its callers
 * without logging any network identity, configuration or credential. */
void sub_002194B0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    static unsigned calls;
    uint32_t caller=MEM32(esp),phase=MEM32(esp+4);
    if(ecx==0x45c4a0 && phase) {
        int handled=community_pool_purge(caller);
        if(handled>=0){eax=(uint32_t)handled;esp+=8;return;}
        ++calls;
        if(calls<=12 || !(calls&(calls-1)))community_pool_trace("retail purge",caller,phase);
    }
#endif
    sub_002194B0_gen();
}
extern void sub_001FFBC0_gen(void);
void sub_001FFBC0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_pool_blocks && ecx==0x45c4a0) {
        XyzXboxPool p;
        if(xyz_xbox_pool_read(cockpit_ui_read,NULL,&p) && MEM32(esp+4)<=p.size){esp+=8;return;}
    }
#endif
    sub_001FFBC0_gen();
}
extern void sub_002199C0_gen(void);
void sub_002199C0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(ecx==0x45c4a0 && !community_pool_release()){esp+=4;return;}
#endif
    sub_002199C0_gen();
}
extern void sub_00050070_gen(void);
void sub_00050070(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_pool_enabled() && MEM32(esp+4)==0x1240 && MEM32(esp+8)==22 && MEM32(esp+12)==0x360b4c) {
        uint32_t stack=esp;MEM32(stack+8)=192;sub_00050070_gen();MEM32(stack+8)=22;
        fprintf(stderr,"[XYZ car pool] ride capacity=192\n");return;
    }
#endif
    sub_00050070_gen();
}

/* Audited Xbox XNet stdcall entry points. Outside a selected native adapter
 * they execute the retail implementation, including its real last error. */
static int community_xnet_call(unsigned operation)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    unsigned count=xyz_xnet_arg_count((enum XyzXnetOp)operation);
    if(count>6 || !xyz_xbox_address(esp,4+4*count))return 0;
    uint32_t args[6],result;for(unsigned i=0;i<count;++i)args[i]=MEM32(esp+4+4*i);
    XyzXboxGuest guest={NULL,cockpit_ui_read,community_guest_write,community_guest_call};
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    uint64_t now=(uint64_t)ts.tv_sec*1000ull+(uint64_t)ts.tv_nsec/1000000;
    if(xyz_xnet_dispatch(&guest,(enum XyzXnetOp)operation,g_fs_base,args,count,now,&result) ||
       xyz_online_xnet(&guest,(enum XyzXnetOp)operation,g_fs_base,args,count,now,&result)){
        eax=result;esp+=4+4*count;return 1;
    }
#else
    (void)operation;
#endif
    return 0;
}
/* One declaration per line: manual_scan.py uses this form to retain the
 * original implementations under their _gen names. */
extern void sub_002FA815_gen(void);
extern void sub_002FA820_gen(void);
extern void sub_002FA82B_gen(void);
extern void sub_002FA836_gen(void);
extern void sub_002FA841_gen(void);
extern void sub_002FA850_gen(void);
extern void sub_002FA85B_gen(void);
extern void sub_002FA866_gen(void);
extern void sub_002FA871_gen(void);
extern void sub_002FA87C_gen(void);
extern void sub_002FA887_gen(void);
extern void sub_002FA892_gen(void);
extern void sub_002FA8BB_gen(void);
extern void sub_002FA8D5_gen(void);
extern void sub_002FA8E4_gen(void);
extern void sub_002FA8FE_gen(void);
extern void sub_00307CC9_gen(void);
void sub_002FA815(void) {if(!community_xnet_call(0))sub_002FA815_gen();}
void sub_002FA820(void) {if(!community_xnet_call(1))sub_002FA820_gen();}
void sub_002FA82B(void) {if(!community_xnet_call(2))sub_002FA82B_gen();}
void sub_002FA836(void) {if(!community_xnet_call(3))sub_002FA836_gen();}
void sub_002FA841(void) {if(!community_xnet_call(4))sub_002FA841_gen();}
void sub_002FA850(void) {if(!community_xnet_call(5))sub_002FA850_gen();}
void sub_002FA85B(void) {if(!community_xnet_call(6))sub_002FA85B_gen();}
void sub_002FA866(void) {if(!community_xnet_call(7))sub_002FA866_gen();}
void sub_002FA871(void) {if(!community_xnet_call(8))sub_002FA871_gen();}
void sub_002FA87C(void) {if(!community_xnet_call(9))sub_002FA87C_gen();}
void sub_002FA887(void) {if(!community_xnet_call(10))sub_002FA887_gen();}
void sub_002FA892(void) {if(!community_xnet_call(11))sub_002FA892_gen();}
void sub_002FA8BB(void) {if(!community_xnet_call(12))sub_002FA8BB_gen();}
void sub_002FA8D5(void) {if(!community_xnet_call(13))sub_002FA8D5_gen();}
void sub_002FA8E4(void) {if(!community_xnet_call(14))sub_002FA8E4_gen();}
void sub_002FA8FE(void) {if(!community_xnet_call(15))sub_002FA8FE_gen();}
void sub_00307CC9(void) {if(!community_xnet_call(16))sub_00307CC9_gen();}

/* Visual Reputation Remover: use the supported Xbox calculation's existing
 * override branch, retaining its complete x87/ABI behavior. Restore the debug
 * override immediately afterward; a saved career is never edited here. */
extern void sub_00151DF0_gen(void);
extern void sub_00204430_gen(void);
extern void sub_000A75D0_gen(void);
extern void sub_000A2CD0_gen(void);
extern void sub_002E80A0_gen(void);
void sub_00204430(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    float before=community_lights_flare_gain;
    community_lights_flare_gain=xyz_xbox_address(ecx,4) && community_lights_state(MEM32(ecx))==2?3.f:1.f;
#endif
    sub_00204430_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    community_lights_flare_gain=before;
#endif
}
void sub_000A75D0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t address=esp+0x10,light=MEM32(esp+8),original=MEM32(address);int changed=0;
    if(community_lights_flare_gain!=1 && xyz_xbox_address(light,12)){
        uint32_t hash=MEM32(light+8);float value=MEMF(address);
        if((hash==0x9db90133 || hash==0xd09091c6) && isfinite(value) && value>=0 && value<=4){
            MEMF(address)=value*community_lights_flare_gain;changed=1;++community_lights_flare_count;
        }
    }
#endif
    sub_000A75D0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(changed)MEM32(address)=original;
#endif
}
void sub_000A2CD0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    float before=community_lights_road_gain;uint32_t matrix=0;float saved[16];
    uint32_t view=MEM32(esp+8);
    community_lights_road_gain=1;
    if(community_lights_car && xyz_xbox_address(view,0x44) && MEM32(view+4)==9){
        community_lights_road_gain=community_lights_mode==1?0:community_lights_mode==2?5:1;
        uint32_t platform=MEM32(view);
        if(community_lights_mode==2 && xyz_xbox_address(platform,0xc0)){
            float v[16],p[16],out[16];
            /* 0xA33F0 transposes all shader matrices with 0x99F70 before
             * publishing them. Calculate in row-vector form, then transpose
             * back for the Xbox shader upload. */
            for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++){
                v[r*4+c]=MEMF(platform+4*(c*4+r));p[r*4+c]=MEMF(platform+0x40+4*(c*4+r));
            }
            if(vr_mod_high_beam(v,p,out)){
                matrix=platform+0x80;memcpy(saved,(const void*)XBOX_PTR(matrix),64);
                for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++)MEMF(matrix+4*(r*4+c))=out[c*4+r];
            }
        }
    }
#endif
    sub_000A2CD0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(matrix)memcpy((void*)XBOX_PTR(matrix),saved,64);
    community_lights_road_gain=before;
#endif
}
void sub_002E80A0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t address=edx;float saved[3];int changed=0;
    if(ecx==0x7a && MEM32(esp)==0xa2e92 && community_lights_road_gain!=1 && xyz_xbox_address(address,16)){
        memcpy(saved,(const void*)XBOX_PTR(address),12);
        if(isfinite(saved[0]) && isfinite(saved[1]) && isfinite(saved[2])){
            for(unsigned i=0;i<3;i++)MEMF(address+4*i)=saved[i]*community_lights_road_gain;
            changed=1;++community_lights_road_count;
        }
    }
#endif
    sub_002E80A0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(changed)memcpy((void*)XBOX_PTR(address),saved,12);
#endif
}
extern void sub_001105C0_gen(void);
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
#include "community/xyz_special_vinyls.inl"
#endif
void sub_00151DF0(void)
{
    uint32_t screen=ecx,caller=MEM32(esp),category=MEM32(esp+4);
    sub_00151DF0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(caller==0x15272c && category==22)community_special_category(screen);
#endif
}
void sub_001105C0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(edx==COMMUNITY_SPECIAL_HASH && community_special_label){eax=community_special_label;esp+=4;return;}
#endif
    sub_001105C0_gen();
}
extern void sub_00130300_gen(void);
extern void sub_0011ED10_gen(void);
extern void sub_001106E0_gen(void);
extern void sub_001197B0_gen(void);
void sub_0011ED10(void)
{
    sub_0011ED10_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    community_subtitle_loaded();
#endif
}
void sub_001106E0(void)
{
    sub_001106E0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    community_subtitle_unloaded();
#endif
}
void sub_001197B0(void)
{
    sub_001197B0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_subtitle_lines)SET_LO8(eax,1);
#endif
}
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
#include "community/xyz_quickrace_bridge.inl"
#include "community/xyz_map_textures.inl"
#endif
extern void sub_000CC2B0_gen(void);
/* LapCounter::Update's sprint/percentage branch also fits a one-lap
 * circuit. Scope the flag to this HUD call; race rules retain their flag. */
extern void sub_000C3F80_gen(void);
void sub_000C3F80(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    int percentage=MEM32(0x3cc47c)==1 && !MEM32(0x3cc474);
    if(percentage)MEM32(0x3cc474)=1;
#endif
    sub_000C3F80_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(percentage && MEM32(0x3cc474)==1)MEM32(0x3cc474)=0;
#endif
}
void sub_000CC2B0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_quickrace_action(0))return;
#endif
    sub_000CC2B0_gen();
}
extern void sub_000CBD40_gen(void);
void sub_000CBD40(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_quickrace_action(1))return;
#endif
    sub_000CBD40_gen();
}
extern void sub_000CC570_gen(void);
void sub_000CC570(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_quickrace_action(2))return;
#endif
    sub_000CC570_gen();
}
extern void sub_000CC320_gen(void);
void sub_000CC320(void)
{
    uint32_t self=ecx;sub_000CC320_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    community_quickrace_unfreeze(self);
#endif
}
extern void sub_000CBE10_gen(void);
void sub_000CBE10(void)
{
    uint32_t self=ecx;sub_000CBE10_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    community_quickrace_unfreeze(self);
#endif
}
extern void sub_00026B10_gen(void);
void sub_00026B10(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t driver=ecx;
#endif
    sub_00026B10_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    int mode=quest_xr_career_catchup();
    static int log=-1;
    if(log<0){const char *s=getenv("NFSU2_CATCHUP_LOG");log=s && !strcmp(s,"1");}
    if((mode || log) && MEM32(0x41c338)==1 && MEM32(0x406960) && !MEM32(0x456290) &&
       xyz_xbox_address(driver,0x774)){
        uint32_t car=MEM32(driver+4),world=MEM32(0x4493e0);
        if(xyz_xbox_address(car,0x46c) && xyz_xbox_find_car(cockpit_ui_read,NULL,world,car,0)==car){
            float pace=MEMF(driver+0x770),boost=MEMF(car+0x468);
            float before_pace=pace,before_boost=boost;
            if(vr_mod_catchup(mode,1,0,&pace,&boost)){
                MEMF(driver+0x770)=pace;MEMF(car+0x468)=boost;
                static int said;if(!said){fprintf(stderr,"[Community mods] career rubberbanding applied mode=%d\n",mode);said=1;}
            }
            if(log){
                static unsigned calls,changed;static float low=9.f,high=-9.f,maximum_boost=-9.f;
                if(before_pace<low)low=before_pace;if(before_pace>high)high=before_pace;
                if(before_boost>maximum_boost)maximum_boost=before_boost;
                changed+=pace!=before_pace || boost!=before_boost;
                if(++calls%120==0){
                    fprintf(stderr,"[Community mods catchup] mode=%d updates=%u changed=%u original_pace=%.6f..%.6f original_boost=%.6f final_pace=%.6f final_boost=%.6f driver=%08x car=%08x\n",
                        mode,calls,changed,low,high,maximum_boost,pace,boost,driver,car);
                    low=9.f;high=maximum_boost=-9.f;
                }
            }
        }
    }
#endif
}
void sub_00130300(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(quest_xr_visual_rep_remover() && MEM32(0x3a37f8u)==0xbf800000u) {
        int stage=(int32_t)MEM32(0x405d28u);
        float stars=vr_mod_visual_rating(stage);
        if(stars>=0) {
            MEMF(0x3a37f8u)=stars;
            sub_00130300_gen();
            MEM32(0x3a37f8u)=0xbf800000u;
            static int last=-1;
            if(last!=stage){fprintf(stderr,"[Community mods] Visual Reputation Remover: stage %d, returned %.1f stars (expected %.1f), override restored\n",stage,g_fp_stack[g_fp_top&7],stars);last=stage;}
            return;
        }
    }
#endif
    sub_00130300_gen();
}
/* Only the rating-box caller gets the upstream mod's "already present"
 * answer. All other package queries retain the original result. */
extern void sub_0013A2A0_gen(void);
void sub_0013A2A0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    int meter=quest_xr_visual_rep_remover() && xyz_xbox_address(esp,8) &&
        MEM32(esp)==0x16242du && MEM32(esp+4)==0x353de0u;
#endif
    sub_0013A2A0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(meter){
        eax|=1u;
        static int said;
        if(!said){fprintf(stderr,"[Community mods] Visual Reputation Remover: rating box suppressed at verified caller\n");said=1;}
    }
#endif
}

/* US Xbox main loop 0x18a8c0: the call at 0x18ab7a immediately follows
 * its two debug no-ops, after WorldDoTimestep and before cameras/rendering.
 * This corresponds to the PC adapter's hook at 0x581475. A camera selector
 * or a physics substep is not a once-per-frame hook. Keep the original call
 * and return values; all guest calls run synchronously on this game thread. */
extern void sub_001FAED0_gen(void);
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
extern void xyz_online_fleet_probe(const XyzXboxGuest *,const XyzXboxFrame *,uint64_t);
#endif
/* Stock frontend Wake has the same overwrite semantics as the PC version.
 * Repair only packs held by the online adapter; without an online session or
 * explicit asset probe this wrapper runs exactly the original function. */
extern void sub_0009F2E0_gen(void);
void sub_0009F2E0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(!community_adapter_busy && xyz_xbox_address(esp,8)) {
        community_adapter_busy=1;
        XyzXboxGuest guest={NULL,cockpit_ui_read,community_guest_write,community_guest_call};
        XyzXboxWakePlan plan;
        int prepared=xyz_online_before_wake(&guest,MEM32(esp+4),&plan);
        sub_0009F2E0_gen();
        if(prepared)xyz_online_after_wake(&guest,&plan);
        community_adapter_busy=0;return;
    }
#endif
    sub_0009F2E0_gen();
}
/* Streaming waits repeatedly call this original pump while the outer frame
 * is blocked. Keep the owned race's DirtySock callbacks moving there too;
 * otherwise a long load fills the bounded LAN departure queue and forces a
 * new service generation. Never advance gameplay or run probe transitions
 * from this nested loader context. Other guest threads and offline loads
 * retain the original behavior. */
extern void sub_001894F0_gen(void);
void sub_001894F0(void)
{
    sub_001894F0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t flow=MEM32(0x428e34);
    if(!community_adapter_busy && community_engine_lobby.stage==XYZ_LOBBY_RACE &&
       g_fs_base==community_engine_thread && (flow==4 || flow==5 || flow==7 || flow==8)){
        static uint64_t last;
        struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
        uint64_t now=(uint64_t)ts.tv_sec*1000000ull+(uint64_t)ts.tv_nsec/1000;
        if(now>=last && now-last<10000ull)return;
        last=now;community_adapter_busy=1;
        XYZOnlineStatus status;xyz_online_snapshot(&status);
        xyz_xbox_lobby_tick(&community_engine_guest,&community_engine_lobby,status.lan_ready?status.generation:0,now);
        if(community_engine_lobby.stage==XYZ_LOBBY_RACE)++community_engine_load_pumps;
        community_adapter_busy=0;
    }
#endif
}
extern void sub_0020BC50_gen(void);
extern void sub_0003ED50_gen(void);
void sub_0003ED50(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t arg=esp+4,bytes=MEM32(arg);
    unsigned count=xyz_xbox_regional_ai_count(community_regional_ready>0,MEM32(esp),esi,ebx,bytes);
    int extended=count!=ebx;
    if(extended){ebx=count;MEM32(arg)=count*4;}
#endif
    sub_0003ED50_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(extended)MEM32(arg)=bytes;
#endif
}
extern void sub_00210E00_gen(void);
void sub_00210E00(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    community_texture_list();
#else
    sub_00210E00_gen();
#endif
}
extern void sub_001AB4C0_gen(void);
void sub_001AB4C0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t body=ecx,impulse=MEM32(esp+4),point=MEM32(esp+8);
    if(xyz_xbox_address(impulse,12)){
        float raw[3];memcpy(raw,(const void*)XBOX_PTR(impulse),sizeof raw);
        copchase_game_collision(community_copchase_game,body,raw);
    }
#endif
    sub_001AB4C0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_damage_ready && xyz_xbox_address(body,0x134) &&
       xyz_xbox_address(MEM32(body),0x54) && MEM32(MEM32(body)+0x50)==0x1AB4C0)dmg_impulse(body,impulse,point);
#endif
}
extern void sub_00217100_gen(void);
void sub_00217100(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_damage_ready)dmg_zero(ecx,MEM32(esp));
#endif
    sub_00217100_gen();
}
extern void sub_00042D50_gen(void);
void sub_00042D50(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t caller=MEM32(esp),part=MEM32(esp+8);
#endif
    sub_00042D50_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_damage_render && part<4 && (caller==0x20BC8A || caller==0x20BCAD || caller==0x20BCD6 || caller==0x20BCFD)){
        int original=eax!=1,opened=xyz_damage_open(community_damage_render,(int)part,original);
        if(opened && !original){eax=0;community_damage_forced|=1u<<part;}
    }
#endif
}
extern void sub_000436D0_gen(void);
void sub_000436D0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t caller=MEM32(esp),part=MEM32(esp+8);
    if(community_damage_render && part<2 && (community_damage_forced&(1u<<part)) && (caller==0x20BD37 || caller==0x20BD6A)){
        g_fp_top=(g_fp_top+7)&7;g_fp_stack[g_fp_top]=1.0;esp+=4;return;
    }
#endif
    sub_000436D0_gen();
}
extern void sub_000ACB30_gen(void);
extern void sub_0009E430_gen(void);
void sub_0009E430(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_copchase_probe_enabled()){
        uint32_t pack=MEM32(esp+8),entry=MEM32(esp+12);
        if(xyz_xbox_address(pack,0xb0) && xyz_xbox_address(entry,24)){
            char name[97];memcpy(name,(const void*)XBOX_PTR(pack+8),96);name[96]=0;
            for(unsigned i=0;i<96 && name[i];i++)if((unsigned char)name[i]<32 || (unsigned char)name[i]>126)name[i]='?';
            fprintf(stderr,"[CopChase stream request] manager=%08x pack=%08x entry=%08x key=%08x offset=%u packed=%u raw=%u flags=%08x buffer=%08x name=%s\n",ecx,pack,entry,MEM32(entry),MEM32(entry+4),MEM32(entry+8),MEM32(entry+12),MEM32(entry+16),MEM32(entry+20),name);
        }
    }
#endif
    sub_0009E430_gen();
}
extern void sub_0017D210_gen(void);
void sub_0017D210(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t source=MEM32(esp+4),destination=MEM32(esp+8),caller=MEM32(esp);
    uint32_t raw=0,packed=0;
    int probe=community_copchase_probe_enabled();
    if(probe && xyz_xbox_address(source,16) && MEM32(source)==0x5a4c444a){raw=MEM32(source+8);packed=MEM32(source+12);}
    if(probe && raw){
        fprintf(stderr,"[CopChase decode begin] source=%08x destination=%08x raw=%u packed=%u caller=%08x\n",source,destination,raw,packed,caller);
        const char *dump=getenv("NFSU2_COPCHASE_DECODE_DUMP");
        if(dump && *dump && packed>=16 && packed<=(8u<<20) && xyz_xbox_address(source,packed)){
            static unsigned serial;
            char path[1024];int n=snprintf(path,sizeof path,"%s/decode-%04u-%08x.bin",dump,serial++,source);
            if(n>0 && (size_t)n<sizeof path){FILE *file=fopen(path,"wb");if(file){fwrite((const void*)XBOX_PTR(source),1,packed,file);fclose(file);}}
        }
    }
#endif
    sub_0017D210_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(probe && raw){
        fprintf(stderr,"[CopChase decode] source=%08x destination=%08x raw=%u packed=%u result=%u caller=%08x\n",source,destination,raw,packed,eax,caller);
    }
#endif
}
extern void sub_000A32D0_gen(void);
void sub_000A32D0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t solid=ecx,chunk=MEM32(esp+4);
#endif
    sub_000A32D0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_copchase_probe_enabled() && xyz_xbox_address(solid,0xc4)){
        static unsigned reports;
        if(reports++<8192){
            char name[33];memcpy(name,(const void*)XBOX_PTR(solid+0xa4),32);name[32]=0;
            for(unsigned i=0;i<32 && name[i];i++)if((unsigned char)name[i]<32 || (unsigned char)name[i]>126)name[i]='?';
            fprintf(stderr,"[CopChase mesh loaded] solid=%08x mesh=%08x hash=%08x chunk=%08x name=%s\n",solid,MEM32(solid),MEM32(solid+0x10),chunk,name);
        }
    }
#endif
}
void sub_000ACB30(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(community_copchase_probe_enabled()){
        uint32_t model=MEM32(esp+4),solid=xyz_xbox_address(model,0x20)?MEM32(model+0x0c):0;
        uint32_t mesh=xyz_xbox_address(solid,0xb0)?MEM32(solid):0;
        if(solid && !xyz_xbox_address(mesh,0x40)){
            static unsigned reports;
            if(reports++<4){
                fprintf(stderr,"[CopChase geometry probe] invalid mesh model=%08x solid=%08x mesh=%08x view=%08x caller=%08x esi=%08x\n",model,solid,mesh,ecx,MEM32(esp),esi);
                fprintf(stderr,"[CopChase geometry probe] model:");for(unsigned i=0;i<0x20;i+=4)fprintf(stderr," %08x",MEM32(model+i));fprintf(stderr,"\n");
                if(xyz_xbox_address(solid,0xb0)){fprintf(stderr,"[CopChase geometry probe] solid:");for(unsigned i=0;i<0xb0;i+=4)fprintf(stderr," %08x",MEM32(solid+i));fprintf(stderr,"\n");}
                fflush(stderr);
            }
        }
    }
    int marker=0;
    if(community_damage_render){
        uint32_t model=MEM32(esp+4),matrix=MEM32(esp+8);
        dmg_body_draw(model,matrix);
        for(unsigned slot=16;slot<=33;slot+=17){
            uint32_t row=community_damage_render+0x17a8+slot*32;
            if(model && (MEM32(row)==model || MEM32(row+16)==model)){dmg_marker_pre((int)slot,model,matrix);marker=1;break;}
        }
    }
#endif
    sub_000ACB30_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(marker)dmg_marker_post();
#endif
}
void sub_0020BC50(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(getenv("NFSU2_VINYL_PROBE") && xyz_xbox_address(ecx,8)){
        static uint32_t seen[80];static unsigned count;
        uint32_t car=MEM32(ecx),ride=MEM32(ecx+4),key=car?car:ecx;
        unsigned i=0;while(i<count && seen[i]!=key)++i;
        if(i==count && count<80 && xyz_xbox_address(ride,0x590)){
            seen[count++]=key;fprintf(stderr,"[VR vinyl draw] car=%08x render=%08x ride=%08x type=%u skin=%08x\n",car,ecx,ride,MEM32(ride),MEM32(ride+0x584));
            unsigned found=0;
            for(unsigned slot=0;slot<63 && found<3;++slot){
                uint32_t model=MEM32(ecx+0x17a8+slot*32);
                if(!xyz_xbox_address(model,0x18))continue;
                uint32_t list=MEM32(model+0x10),n=MEM32(model+0x14);
                if(n>128 || !xyz_xbox_address(list,n*16))continue;
                for(unsigned j=0;j<n && found<3;j++)if(MEM32(list+j*16+4)==MEM32(ride+0x584)){
                    fprintf(stderr,"[VR vinyl binding] car=%08x slot=%u model=%08x replace=%08x texture=%08x\n",car,slot,model,MEM32(list+j*16+4),MEM32(list+j*16+8));++found;
                }
            }
        }
    }
    XyzStanceDraw saved;community_stance_begin(&saved);
    XyzStanceDraw *previous=community_stance_drawing;community_stance_drawing=saved.active?&saved:NULL;
    uint32_t damage_previous=community_damage_render,damage_forced=community_damage_forced;
    uint32_t damage_args=esp+4,damage_wheels[3]={MEM32(esp+0x18),MEM32(esp+0x1c),MEM32(esp+0x20)};
    community_damage_render=community_damage_ready?ecx:0;community_damage_forced=0;
    if(community_damage_render)dmg_render_entry(ecx,damage_args);
#endif
    sub_0020BC50_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    XyzXboxGuest memory={NULL,cockpit_ui_read,community_guest_write,community_guest_call};
    int was_active=saved.active;
    if(community_damage_render)for(unsigned i=0;i<3;i++)MEM32(damage_args+20+i*4)=damage_wheels[i];
    community_damage_render=damage_previous;community_damage_forced=damage_forced;
    xyz_xbox_stance_end(&memory,&saved);community_stance_drawing=previous;
    if(was_active && community_stance_probe==1){
        static unsigned checked;int ok=!memcmp((const void*)XBOX_PTR(saved.render+0x400),saved.places,sizeof saved.places) &&
            !memcmp((const void*)XBOX_PTR(saved.render+0x420),saved.widths,sizeof saved.widths);
        for(unsigned k=0;k<2;k++)ok&=!memcmp((const void*)XBOX_PTR(saved.wheels+(k+2)*64),saved.rear[k],48);
        if(!ok || !(checked++%600))fprintf(stderr,"[VR stance probe] %s original draw completed; places, widths, rear rotations restored; calls=%u\n",ok?"PASS":"FAIL",checked);
    }
#endif
}
extern void sub_001A4EE0_gen(void);
void sub_001A4EE0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    uint32_t ride=ecx-0x10,caller=MEM32(esp);
#endif
    sub_001A4EE0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(caller==0x20e6bd && community_stance_drawing && community_stance_drawing->ride==ride){
        g_fp_stack[g_fp_top&7]=community_stance_drawing->value.fc/70.0;
        static int reported;if(!reported && community_stance_probe==1){reported=1;fprintf(stderr,"[VR stance probe] front camber return %.6f -> %.1f degrees\n",g_fp_stack[g_fp_top&7],community_stance_drawing->value.fc*.1);}
    }
#endif
}
/* CopChase by Vitorini Mods: these two audited boundaries are currently used
 * only by the explicit, offline one-car diagnostic. Native path selection
 * rewrites the steering target between Update and heading computation. */
extern void sub_00030450_gen(void);
void sub_00030450(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    community_copchase_probe_driver(ecx);
    copchase_game_driver(community_copchase_game,ecx);
#endif
    sub_00030450_gen();
}
extern void sub_00015520_gen(void);
void sub_00015520(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    community_copchase_probe_heading(ecx);
    copchase_game_heading(community_copchase_game,ecx);
#endif
    sub_00015520_gen();
}
/* Invalidate before native destructors free storage. Pointer equality alone
 * cannot distinguish recycled Worlds, Players or indexed traffic cars. */
extern void sub_001EF3F0_gen(void);
void sub_001EF3F0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    community_copchase_world_ending();
#endif
    sub_001EF3F0_gen();
}
extern void sub_001EF650_gen(void);
void sub_001EF650(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    community_copchase_world_ending();
#endif
    sub_001EF650_gen();
}
extern void sub_001E18D0_gen(void);
void sub_001E18D0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    ++community_copchase_player_generation;
#endif
    sub_001E18D0_gen();
}
extern void sub_001E1F50_gen(void);
void sub_001E1F50(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    ++community_copchase_player_generation;
#endif
    sub_001E1F50_gen();
}
extern void sub_001E48B0_gen(void);
void sub_001E48B0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(ecx==community_copchase_player_car)++community_copchase_player_generation;
    copchase_game_car_ending(community_copchase_game,ecx);
#endif
    sub_001E48B0_gen();
}
extern void sub_00122330_gen(void);
void sub_00122330(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    /* New/reset profile at the same address is a different identity. The
     * decoder's internal reset is already covered by the load transaction. */
    if(ecx==0x406ca0u && MEM32(esp)!=0x141099u)community_copchase_profile_clear();
#endif
    sub_00122330_gen();
}
extern void sub_001413C0_gen(void);
void sub_001413C0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    /* Only the successful whole-profile file load, not rollback/car import.
     * Bind the actual decoded buffer to successful reads of one save file. */
    CopChaseProfileProof proof={0};int accepted=0;
    int load=ecx==0x406c90u && MEM32(esp)==0xc3758u && MEM32(ecx+0x156a8)!=4;
    if(load){
        community_copchase_profile_clear();
        uint32_t task=MEM32(0x3f9d14),payload=MEM32(esp+4),length=MEM32(esp+8);
        if(xyz_xbox_address(task,0x58) && length==0xd218 && xyz_xbox_address(payload,length) &&
           MEM32(task+0x1c)==payload && MEM32(task+0x54)==length && MEM32(task+0x28)==0x10d &&
           MEM32(task+0x2c)==length && MEM32(task+0x50)==0)
            accepted=copchase_profile_claim((const void*)XBOX_PTR(payload),length,&proof);
    }
#endif
    sub_001413C0_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(load){
        if(accepted && MEM32(0x406cc0)==1)memcpy(community_copchase_profile_id,proof.id,sizeof proof.id);
        fprintf(stderr,"[CopChase profile] decoded save provenance=%s bytes=%zu offset=%llu\n",
            community_copchase_profile_id[0]?"verified":"unknown",proof.bytes,(unsigned long long)proof.file_offset);
    }
#endif
}
void sub_001FAED0(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(MEM32(esp)==0x0018ab7fu) {
        /* CopChase models are substantially larger than retail traffic.
         * Attach the already audited Cars backing block in the frontend,
         * before the track loader can evict a live racer's geometry. */
        if(copchase_assets_ready() && !community_pool_blocks && MEM32(0x428e34)==3 &&
           !MEM32(0x45c4a0+0x14))community_pool_prepare();
        community_map_textures();
        community_stance_frame();
        community_lights_frame();
        community_damage_frame();
        community_copchase_probe_frame();
        community_copchase_frame();
        community_regional_frame();
        int whine_enabled=vr_whine_enabled(),gps_enabled=quest_xr_gps_enabled();
        if(whine_enabled || gps_enabled){
            rc_state sound;uint32_t player=MEM32(0x44940c),world=MEM32(0x4493e0);
            uint32_t car=xyz_xbox_address(player,0x14) && MEM32(player+0x10)==0?MEM32(player+4):0;
            int running=MEM32(0x428e34)==6 && quest_cockpit_ui_menu(cockpit_ui_read,NULL)==QUEST_UI_HUD &&
                xyz_xbox_car_state(cockpit_ui_read,NULL,world,car,0,&sound);
            float master=MEMF(0x406ce8),engine=MEMF(0x406cfc);
            float volume=isfinite(master) && master>=0 && master<=1 && isfinite(engine) && engine>=0 && engine<=1?master*engine:0;
            if(whine_enabled)vr_whine_update(car,running,running?sound.speed:0,running?sound.gear:0,volume,MEMF(0x428a0c));
            if(gps_enabled){
                float metres=-1;int metric=0;
                if(running)xyz_xbox_gps(cockpit_ui_read,NULL,sound.p,&metres,&metric);
                quest_xr_gps_publish(metres,metric);
            }
        }
        static int enabled=-1,reported=-1,captured,assets,fleet_probe;
        static unsigned frames;
        static uint64_t last_report;
        static const char *capture_path;
        if(enabled<0) {
            const char *trace=getenv("NFSU2_XYZ_VEHICLE_TRACE");
            const char *probe=getenv("NFSU2_XYZ_RIDE_PROBE");
            const char *clone=getenv("NFSU2_XYZ_CLONE_PROBE");
            const char *fleet=getenv("NFSU2_XYZ_FLEET_PROBE");fleet_probe=fleet && *fleet=='1';
            const char *asset_probe=getenv("NFSU2_XYZ_ASSET_PROBE");assets=asset_probe && *asset_probe=='1';
            const char *visual_probe=getenv("NFSU2_VISUAL_REP_PROBE");
            const char *xnet_probe=getenv("NFSU2_XYZ_XNET_PROBE");
            const char *lobby_probe=getenv("NFSU2_XYZ_LOBBY_PROBE");
            const char *engine_probe=getenv("NFSU2_XYZ_LOBBY_ENGINE_PROBE");
            const char *race_probe=getenv("NFSU2_XYZ_RACE_LINK_PROBE");
            const char *races_ui_probe=getenv("NFSU2_XYZ_RACES_UI_PROBE");
            capture_path=getenv("NFSU2_XYZ_CAPTURE");
            enabled=assets || (lobby_probe && *lobby_probe=='1') || (engine_probe && *engine_probe=='1') || (xnet_probe && *xnet_probe=='1') || (visual_probe && *visual_probe=='1') || (trace && *trace=='1') || (capture_path && *capture_path) || (probe && *probe=='1') || (clone && *clone=='1');
            enabled|=race_probe && (!strcmp(race_probe,"host") || !strcmp(race_probe,"guest"));
            enabled|=races_ui_probe && *races_ui_probe=='1';
            const char *races_pair=getenv("NFSU2_XYZ_RACES_PAIR_PROBE");
            enabled|=races_pair && (!strcmp(races_pair,"host") || !strcmp(races_pair,"guest") || !strcmp(races_pair,"browser-guest"));
            const char *career_probe=getenv("NFSU2_XYZ_CAREER_PROBE");enabled|=career_probe && *career_probe=='1';
            const char *career_mods=getenv("NFSU2_XYZ_CAREER_MODS_PROBE");enabled|=career_mods && !strcmp(career_mods,"observe");
            const char *settings_probe=getenv("NFSU2_XYZ_RACE_SETTINGS_PROBE");
            enabled|=settings_probe && *settings_probe=='1';
            enabled|=fleet_probe;
        }
        if(!community_adapter_busy && (enabled || xyz_online_needs_frame() || xyz_races_needs_frame())) {
            community_adapter_busy=1;
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
            uint64_t now=(uint64_t)ts.tv_sec*1000000ull+(uint64_t)ts.tv_nsec/1000;
            XyzXboxFrame sample;
            int status=xyz_xbox_sample(cockpit_ui_read,NULL,(uint32_t)now,&sample);
            if(sample.car && sample.car==community_lights_car){sample.state.hibeam=community_lights_mode==2;if(community_lights_mode==1)sample.state.bits|=RC_BIT_DARK;}
            XyzXboxGuest guest={NULL,cockpit_ui_read,community_guest_write,community_guest_call};
            if(assets && (status==XYZ_XBOX_READY || status==XYZ_XBOX_OTHER_RACE))xyz_online_assets_probe(&guest,now);
            if(fleet_probe)xyz_online_fleet_probe(&guest,&sample,now);
            xyz_online_frame(&guest,&sample,quest_cockpit_ui_paused(cockpit_ui_read,NULL),now);
            int race_main_menu=sample.flow==3 && quest_cockpit_ui_main(cockpit_ui_read,NULL);
            community_races_frame(&sample,race_main_menu,quest_cockpit_ui_paused(cockpit_ui_read,NULL),now);
            community_races_probe(&sample,race_main_menu,now);
            community_races_pair_probe(&sample,race_main_menu,now);
            community_career_probe(&sample,race_main_menu,now);
            community_career_mods_probe(&sample,race_main_menu,now);
            community_race_settings_probe(&sample,race_main_menu,now);
            community_ride_probe(&sample);
            community_visual_probe(&sample);
            community_socket_probe(&sample,now);
            community_lobby_probe(&sample,now);
            community_race_link_probe(&sample,now);
            community_clone_probe(&sample,now);
            ++frames;
            if(enabled && (reported!=status || now-last_report>=5000000ull)) {
                fprintf(stderr,"[XYZ vehicle] frames=%u status=%s flow=%u track=%u race=%u world=%08x car=%08x ride=%08x cfg=%u pos=(%.3f,%.3f,%.3f) speed=%.3f rpm=%.1f\n",
                    frames,xyz_xbox_status_text((enum XyzXboxStatus)status),sample.flow,sample.track,sample.race_type,
                    sample.world,sample.car,sample.ride,sample.config_size,
                    sample.state.p[0],sample.state.p[1],sample.state.p[2],sample.state.speed,sample.state.rpm);
                reported=status; last_report=now;
            }
            if(!captured && capture_path && *capture_path && sample.config_size &&
               (status==XYZ_XBOX_READY || status==XYZ_XBOX_OTHER_RACE)) {
                captured=1;
                FILE *file=fopen(capture_path,"wb");
                if(file) {
                    size_t written=fwrite(sample.config,1,sample.config_size,file);
                    int closed=fclose(file);
                    fprintf(stderr,"[XYZ vehicle] wire config capture %s (%u bytes)\n",
                        written==sample.config_size && !closed?"saved":"failed",sample.config_size);
                } else fprintf(stderr,"[XYZ vehicle] wire config capture could not be opened\n");
            }
            community_adapter_busy=0;
        }
    }
#endif
    sub_001FAED0_gen();
}

/* ── Stream read, unlocked precheck (0x00072F90) ─────────────
 *
 * cdecl (stream). Checks a 'STRM' stream's byte count at +8 and reads its
 * buffer descriptor through +0xC -- before taking the stream's lock
 * (sub_00251C41, the call after the reads). The stream thread fills those
 * fields in at the same moment. On the Xbox's one CPU the gap is almost
 * never hit; with guest threads running in parallel, the reader saw a count
 * with +0xC still holding the allocator's 0xAA fill and dereferenced
 * 0xAAAAAAAA -- the crash after Start, nearly every time on a Switch, now
 * and then on a PC. A descriptor that is not a pointer yet means the stream
 * is not ready: answer as the function does with no data (0), and the caller
 * asks again. */
extern void sub_00072F90_gen(void);

void sub_00072F90(void)
{
    uint32_t stream = MEM32(esp + 4);

    if (stream && MEM32(stream + 8)) {
        uint32_t hdr = MEM32(stream);
        uint32_t desc = MEM32(stream + 0xC);
        if (hdr && MEM32(hdr) == 0x4D525453u                /* 'STRM' */
                && (desc < 0x10000u || desc == 0xAAAAAAAAu)) {
            /* Not ready: let the stream thread (which may be waiting for the
             * guest lock) get on with filling it in, then say "no data". */
            recomp_spin_yield();
            eax = 0;
            esp += 4;                   /* return address; the caller pops the argument */
            return;
        }
    }
    sub_00072F90_gen();
}

/* ── VP6 movie frames, sub_002618F0 ──────────────────────────
 *
 * cdecl (decoder, data, size, width, height): decodes one VP6 frame (the
 * payload of an EA MV0K/MV0F chunk) with On2's decoder. The decoder block:
 *   +0x1B0/+0x1B4  picture width/height    +0x1B8/+0x1BC  Y/UV stride
 *   +0x21C/+0x220/+0x224  Y/U/V offsets in a frame buffer
 *   +0x244  buffer being decoded   +0x254  last decoded (sub_0026144D's
 *           output, and the reference) -- swapped after every frame
 * Planes are bottom-up with a 48 (Y) / 24 (UV) pixel border for motion
 * vectors past the edge. The movie player (sub_0025F909) then passes +0x254
 * on to be drawn.
 *
 * Lifted, this is most of the CPU time of a movie on the Switch, so by
 * default FFmpeg decodes the frame (src/movie_vp6.c) into +0x244 and the
 * buffers are swapped as the original does. The border is not filled in:
 * only On2's own motion compensation read it. NFSU2_NATIVE_VP6=0 runs the
 * lifted decoder, =2 runs both and compares the pictures. */
#ifdef NFSU2_NATIVE_VP6
#include "movie_vp6.h"

extern void sub_002618F0_gen(void);

typedef struct {
    uint32_t y, u, v, y_stride, uv_stride, w, h;
} Vp6Planes;

static Vp6Planes vp6_planes(uint32_t dec, uint32_t buf)
{
    Vp6Planes p;
    p.y_stride = MEM32(dec + 0x1B8);
    p.uv_stride = MEM32(dec + 0x1BC);
    p.y = buf + MEM32(dec + 0x21C) + (p.y_stride + 1u) * 48u;
    p.u = buf + MEM32(dec + 0x220) + (p.uv_stride + 1u) * 24u;
    p.v = buf + MEM32(dec + 0x224) + (p.uv_stride + 1u) * 24u;
    p.w = MEM32(dec + 0x1B0);
    p.h = MEM32(dec + 0x1B4);
    return p;
}

/* NFSU2_NATIVE_VP6=2: FFmpeg into a scratch picture, compared with what the
 * lifted decoder left at +0x254. */
static void vp6_check(uint32_t dec, uint32_t data, uint32_t size,double lifted_ms)
{
    static unsigned long frames, bad;
    static double lifted_total,native_total;
    static uint8_t *pic;
    Vp6Planes p = vp6_planes(dec, MEM32(dec + 0x254));
    uint32_t cw = p.w / 2, row, diff = 0;

    if (p.w > 4096 || p.h > 4096)
        return;
    if (!pic && !(pic = malloc(4096u * 4096u * 3u / 2u)))
        return;
    double start=nfsu2_vp6_now_ms();
    if (nfsu2_vp6_decode(dec, (const uint8_t *)XBOX_PTR(data), (int)size,
                         pic, pic + p.w * p.h, pic + p.w * p.h + cw * (p.h / 2),
                         (int)p.w, (int)cw, (int)p.w, (int)p.h) == 0) {
    lifted_total+=lifted_ms;native_total+=nfsu2_vp6_now_ms()-start;
    for (row = 0; row < p.h; row++)
            diff += memcmp(pic + row * p.w, (const void *)XBOX_PTR(p.y + row * p.y_stride), p.w) != 0;
        for (row = 0; row < p.h / 2; row++) {
            diff += memcmp(pic + p.w * p.h + row * cw,
                           (const void *)XBOX_PTR(p.u + row * p.uv_stride), cw) != 0;
            diff += memcmp(pic + p.w * p.h + cw * (p.h / 2) + row * cw,
                           (const void *)XBOX_PTR(p.v + row * p.uv_stride), cw) != 0;
        }
    } else {
        diff = 1;
    }
    frames++;
    if (diff && bad++ < 20)
        fprintf(stderr, "[movie] VP6 check: frame %lu (%u bytes) differs in %u rows\n",
                frames, size, diff);
    if (frames % 300 == 0)
        fprintf(stderr, "[movie] VP6 check: %lu frames, %lu differ; mean lifted %.2f ms, native %.2f ms\n",
                frames,bad,lifted_total/frames,native_total/frames);
}

void sub_002618F0(void)
{
    int mode = nfsu2_vp6_mode();
    uint32_t dec = MEM32(esp + 4), data = MEM32(esp + 8), size = MEM32(esp + 12);
    uint32_t cur;
    Vp6Planes p;

    if (mode != 1) {
        double start=mode==2?nfsu2_vp6_now_ms():0;
        sub_002618F0_gen();
        if (mode == 2 && eax == 0)
            vp6_check(dec, data, size,nfsu2_vp6_now_ms()-start);
        return;
    }
    cur = MEM32(dec + 0x244);
    p = vp6_planes(dec, cur);
    if (nfsu2_vp6_decode(dec, (const uint8_t *)XBOX_PTR(data), (int)size,
                         (uint8_t *)XBOX_PTR(p.y), (uint8_t *)XBOX_PTR(p.u),
                         (uint8_t *)XBOX_PTR(p.v), (int)p.y_stride, (int)p.uv_stride,
                         (int)p.w, (int)p.h) == 0) {
        MEM32(dec + 0x244) = MEM32(dec + 0x254);
        MEM32(dec + 0x254) = cur;
    }
    /* A frame FFmpeg rejects leaves the last picture up. */
    MEM32(dec + 0x1E8) = size;
    MEM32(0x00469D04u) += 1;            /* the decoder's frame counter */
    eax = 0;
    esp += 4;                           /* cdecl: the caller pops the arguments */
}
#else
/* Built without FFmpeg (NFSU2_FFMPEG_DIR): the lifted decoder. */
extern void sub_002618F0_gen(void);
void sub_002618F0(void) { sub_002618F0_gen(); }
#endif

/* ── 4x4 matrix multiply, sub_002213EA ─────────────────────────
 *
 * stdcall (out, a, b), ret 12, eax = out: out = a * b, row-major floats, in
 * SSE (shufps a[i][j] across the row, mulps by row j of b, addps). Every
 * xmm register of the lifted body is thread-local, so each call was ~100
 * TLS accesses (calls on Horizon). sub_000A2EA0 calls it twice per object
 * drawn; at a drag start line (Coastal Express, ~2300 draws a frame) it was
 * 15-18% of the main thread on x86, native -22% main-thread time per frame.
 * Same sums in the same order, ((p0 + p1) + p2) + p3, no FMA contraction;
 * all rows are computed before the store, as out may alias a or b.
 * RECOMP_NATIVE=0 lifted, RECOMP_NATIVE_CHECK=1 both and compare (Linux race:
 * 0 mismatches in 4.4M calls). */
extern void sub_002213EA_gen(void);

NFSU2_NO_FMA
static void mat4_mul(float *o, const float *a, const float *b)
{
    float r[16];
    int i, k;
    for (i = 0; i < 4; i++)
        for (k = 0; k < 4; k++) {
            float s = a[4 * i] * b[k];
            s = s + a[4 * i + 1] * b[4 + k];
            s = s + a[4 * i + 2] * b[8 + k];
            s = s + a[4 * i + 3] * b[12 + k];
            r[4 * i + k] = s;
        }
    memcpy(o, r, sizeof r);
}

void sub_002213EA(void)
{
    static int mode = -1;               /* 0 lifted, 1 native, 2 native + check */
    uint32_t out = MEM32(esp + 4), a = MEM32(esp + 8), b = MEM32(esp + 12);

    if (mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
        mode = (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
    }
    if (mode == 0) {
        sub_002213EA_gen();
        return;
    }
    if (mode == 2) {
        static unsigned long calls, bad;
        float o[16], ma[16], mb[16];
        memcpy(ma, (const void *)XBOX_PTR(a), 64);
        memcpy(mb, (const void *)XBOX_PTR(b), 64);
        mat4_mul(o, ma, mb);
        sub_002213EA_gen();             /* pops its own arguments */
        calls++;
        if (memcmp(o, (const void *)XBOX_PTR(out), 64) && bad++ < 20)
            fprintf(stderr, "[native] sub_002213EA mismatch: out %08X a %08X b %08X\n",
                    out, a, b);
        if ((calls & 0xFFFFF) == 0)
            fprintf(stderr, "[native] sub_002213EA: %lu calls, %lu mismatches\n", calls, bad);
        return;
    }
    mat4_mul((float *)XBOX_PTR(out), (const float *)XBOX_PTR(a), (const float *)XBOX_PTR(b));
    eax = out;
    esp += 16;                          /* return address + three arguments */
}

/* ── Float to integer, sub_002A68EC (_ftol2) ──────────────────
 *
 * MSVC's CRT _ftol2: st(0) truncated to a 64-bit integer in edx:eax, popped.
 * Every (int)float cast in the title calls it (589 sites); the hottest
 * function of the main thread in a Linux race (4.4%). It rounds with fistp
 * (the control word's mode), then corrects towards zero by the sign of the
 * float (x - r): x >= 0 and x - r < 0 -> r - 1; x < 0 (sign of (float)x)
 * and x - r > 0 -> r + 1. r == 0 or the integer indefinite is returned as
 * is. Exact, including ecx, the frame it publishes and the x87 top.
 * RECOMP_NATIVE=0 lifted, RECOMP_NATIVE_CHECK=1 both and compare. */
extern void sub_002A68EC_gen(void);

NFSU2_COLD static int64_t ftol_fist_slow(double x, uint16_t cw)
{
    return recomp_fist(x, cw, 64);
}

static inline int64_t ftol_fist(double x, uint16_t cw)
{
    if (__builtin_expect(((cw >> 10) & 3u) == 0 && fabs(x) < 0x1p63, 1))
        return (int64_t)rint(x);        /* host rounding is never changed: nearest */
    return ftol_fist_slow(x, cw);       /* other modes, NaN, out of range */
}

static inline void ftol2_native(double x, uint32_t *peax, uint32_t *pedx, uint32_t *pecx)
{
    int64_t r = ftol_fist(x, g_fp_control_word);
    uint32_t lo = (uint32_t)r, hi = (uint32_t)((uint64_t)r >> 32), d;
    float fx = (float)x, fd;
    uint32_t sx;

    if (lo == 0 && (hi & 0x7FFFFFFFu) == 0) {   /* 0 or the indefinite */
        *peax = lo;
        *pedx = hi;
        return;
    }
    fd = (float)(x - (double)r);
    memcpy(&d, &fd, 4);
    memcpy(&sx, &fx, 4);
    if (sx & 0x80000000u) {
        d ^= 0x80000000u;
        r += d >= 0x80000001u;          /* carry of d + 0x7FFFFFFF */
    } else {
        r -= d >= 0x80000001u;          /* borrow */
    }
    *peax = (uint32_t)r;
    *pedx = (uint32_t)((uint64_t)r >> 32);
    *pecx = d + 0x7FFFFFFFu;
}

/* Modes other than plain native, out of line so that the hot path below is
 * small enough for LTO to inline into its 581 lifted callers. */
static int s_ftol_mode = -1;            /* 0 lifted, 1 native, 2 native + check */

NFSU2_COLD static void ftol2_slow(void)
{
    double x = g_fp_stack[g_fp_top & 7];
    uint32_t a, d, c = ecx, frame = esp - 4u;

    if (s_ftol_mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *k = getenv("RECOMP_NATIVE_CHECK");
        s_ftol_mode = (e && *e == '0') ? 0 : (k && *k == '1') ? 2 : 1;
    }
    if (s_ftol_mode == 0) {
        sub_002A68EC_gen();
        return;
    }
    ftol2_native(x, &a, &d, &c);
    if (s_ftol_mode == 2) {
        static unsigned long calls, bad;
        int top = (g_fp_top + 1) & 7;
        uint32_t sp = esp + 4u;
        sub_002A68EC_gen();
        calls++;
        if ((eax != a || edx != d || ecx != c || g_fp_top != top || esp != sp
             || g_ebp != frame) && bad++ < 20)
            fprintf(stderr, "[native] sub_002A68EC mismatch for %.17g: lifted %08X:%08X "
                    "ecx %08X, native %08X:%08X ecx %08X\n", x, edx, eax, ecx, d, a, c);
        if ((calls & 0xFFFFFF) == 0)
            fprintf(stderr, "[native] sub_002A68EC: %lu calls, %lu mismatches\n", calls, bad);
        return;
    }
    eax = a;
    edx = d;
    ecx = c;
    g_fp_top = (g_fp_top + 1) & 7;
    g_ebp = g_seh_ebp = frame;
    esp += 4;
}

void sub_002A68EC(void)
{
    uint32_t a, d, c, frame;
    int top;

    if (__builtin_expect(s_ftol_mode != 1, 0)) {
        ftol2_slow();                   /* first call, lifted or check mode */
        return;
    }
    top = g_fp_top;
    c = ecx;
    frame = esp - 4u;
    ftol2_native(g_fp_stack[top & 7], &a, &d, &c);
    eax = a;
    edx = d;
    ecx = c;
    g_fp_top = (top + 1) & 7;
    g_ebp = g_seh_ebp = frame;          /* as its push ebp / mov ebp, esp left them */
    esp += 4;                           /* return address */
}

/* ── Small math leaves of the main thread ─────────────────────
 *
 * Each about 0.3-0.8% of the main thread in a Linux race, more on the
 * console, where every x87 slot and register the lifted bodies touch is
 * thread-local. Exact as the lifted code computes them (x87 values as
 * doubles, rounded to float where the original stores), including the
 * registers, flags and frame they leave behind. RECOMP_NATIVE=0 lifted,
 * RECOMP_NATIVE_CHECK=1 both and compare. */
static int native_mode(void)
{
    const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
    return (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
}

static void native_report(const char *name, unsigned long *calls, unsigned long *bad, int ok,
                          uint32_t a0, uint32_t a1, uint32_t a2)
{
    ++*calls;
    if (!ok && (*bad)++ < 20)
        fprintf(stderr, "[native] %s mismatch: args %08X %08X %08X\n", name, a0, a1, a2);
    if ((*calls & 0xFFFFF) == 0 || *calls == 4096)
        fprintf(stderr, "[native] %s: %lu calls, %lu mismatches\n", name, *calls, *bad);
}

/* sub_0004BC20: cdecl (out, m, v) -- out = v * m, m a row-major 4x4 with
 * the translation in row 3; all three sums before the stores. */
extern void sub_0004BC20_gen(void);

static void xform_point(uint32_t m, uint32_t v, float o[3])
{
    double v0 = MEMF(v), v1 = MEMF(v + 4), v2 = MEMF(v + 8);
    o[0] = (float)(((MEMF(m + 0x20) * v2 + MEMF(m + 0x10) * v1) + MEMF(m + 0x00) * v0)
                   + MEMF(m + 0x30));
    o[1] = (float)(((MEMF(m + 0x24) * v2 + MEMF(m + 0x04) * v0) + MEMF(m + 0x14) * v1)
                   + MEMF(m + 0x34));
    o[2] = (float)(((MEMF(m + 0x28) * v2 + MEMF(m + 0x08) * v0) + MEMF(m + 0x18) * v1)
                   + MEMF(m + 0x38));
}

void sub_0004BC20(void)
{
    static int mode = -1;
    uint32_t out = MEM32(esp + 4), m = MEM32(esp + 8), v = MEM32(esp + 12), e0 = esp;
    float o[3];

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_0004BC20_gen();
        return;
    }
    xform_point(m, v, o);
    if (mode == 2) {
        static unsigned long calls, bad;
        uint32_t sv_edx = edx;
        int top = g_fp_top;
        sub_0004BC20_gen();
        native_report("sub_0004BC20", &calls, &bad,
                      !memcmp(o, (const void *)XBOX_PTR(out), 12) && eax == out && ecx == v
                      && edx == sv_edx && g_fp_top == top && esp == e0 + 4u
                      && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u, out, m, v);
        return;
    }
    memcpy((void *)XBOX_PTR(out), o, 12);
    eax = out;
    ecx = v;
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 4;
}

/* sub_0004B260: cdecl (uint16 angle) -> st(0): a = angle * C, reduced to
 * x in range with a sign s, then s * (x - x^3 D1 + x^5 D2 - x^7 D3 + x^9 D4)
 * (a sine). Also writes the zero-extended angle back to its argument slot
 * and leaves fnstsw's word of the last compare in ax. */
extern void sub_0004B260_gen(void);

static double sine16(uint32_t angle, uint32_t *peax, int top0)
{
    const double a = (double)(int32_t)angle * MEMF(0x0034089Cu);
    double s = MEMF(0x003408ACu), x = a;
    int cmp = RECOMP_FCMP(a, (double)MEMF(0x00343644u));
    uint16_t cc = RECOMP_FCMP_CC(cmp);

    if (!(cc & 0x0100u)) {
        x = a - MEMF(0x00340AFCu);
    } else {
        cmp = RECOMP_FCMP(a, (double)MEMF(0x003408B0u));
        cc = RECOMP_FCMP_CC(cmp);
        if (!(cc & 0x0100u)) {
            x = a - MEMF(0x00343640u);
            s = MEMF(0x003408FCu);
        }
    }
    g_fp_cmp = cmp;
    g_fp_cc = cc;
    *peax = (uint32_t)(uint16_t)((((top0 + 6) & 7u) << 11) | cc);
    double x2 = x * x, x3 = x2 * x, x5 = x3 * x2, x7 = x5 * x2, x9 = x7 * x2;
    double acc = x - x3 * MEMF(0x0034363Cu);
    acc = acc + x5 * MEMF(0x00343638u);
    acc = acc - x7 * MEMF(0x00343634u);
    acc = acc + x9 * MEMF(0x00343630u);
    return acc * s;
}

void sub_0004B260(void)
{
    static int mode = -1;
    uint32_t angle = MEM16(esp + 4), e0 = esp, a;
    int top0 = g_fp_top;
    double r;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_0004B260_gen();
        return;
    }
    r = sine16(angle, &a, top0);
    if (mode == 2) {
        static unsigned long calls, bad;
        int cmp = g_fp_cmp;
        uint16_t cc = g_fp_cc;
        sub_0004B260_gen();
        native_report("sub_0004B260", &calls, &bad,
                      !memcmp(&r, &g_fp_stack[g_fp_top & 7], 8) && g_fp_top == ((top0 + 7) & 7)
                      && eax == a && g_fp_cmp == cmp && g_fp_cc == cc && esp == e0 + 4u
                      && MEM32(e0 + 4u) == angle && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u,
                      angle, 0, 0);
        return;
    }
    MEM32(e0 + 4u) = angle;
    g_fp_top = (top0 + 7) & 7;
    g_fp_stack[g_fp_top] = r;
    eax = a;
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 4;
}

/* sub_001C6900: cdecl (obj, const float p[3], float r) -> int. d = p - c
 * (c at obj+0x60); if d.n (n at obj+0x70) < -r: 0. Otherwise L = |d| - r
 * (as a float) and v = L > r ? A / L * r : A (A at obj+0x80), truncated
 * (cvttss2si, lifted as a cast). Frame aligned to 16; xmm0 is left as its
 * movups of the frame (v, L, caller's frame, return address). */
extern void sub_001C6900_gen(void);

static int32_t sphere_fade(uint32_t obj, uint32_t p, float r, int *pcmp, uint16_t *pcc,
                           float *pv, float *pl, int *vr_retained)
{
    double dx = (double)MEMF(p) - MEMF(obj + 0x60);
    double dy = (double)MEMF(p + 4) - MEMF(obj + 0x64);
    double dz = (double)MEMF(p + 8) - MEMF(obj + 0x68);
    double dot = (dy * MEMF(obj + 0x74) + dz * MEMF(obj + 0x78)) + dx * MEMF(obj + 0x70);
    double nr = -(double)r;
    float l, v;

    *pcmp = RECOMP_FCMP(nr, dot);
    *pcc = RECOMP_FCMP_CC(*pcmp);
    if (!(*pcc & 0x4100u)) {
#ifdef NFSU2_PCVR
        /* sub_001C69C0 has already traversed the world's bounding boxes, but
         * this LOD helper independently rejects the old camera's rear half.
         * Keep its distance/LOD calculation for a sphere visible in VR. */
        *vr_retained=pcvr_world_sphere_retained(obj,p,r);
        if (!*vr_retained)
#endif
            return -1;                  /* outside: returns 0, xmm0 untouched */
    }
    l = (float)(sqrt((dx * dx + dy * dy) + dz * dz) - r);
    *pcmp = RECOMP_FCMP((double)l, (double)r);
    *pcc = RECOMP_FCMP_CC(*pcmp);
    v = (*pcc & 0x4100u) ? MEMF(obj + 0x80)
                         : (float)((double)MEMF(obj + 0x80) / l * r);
    *pv = v;
    *pl = l;
    return 0;
}

void sub_001C6900(void)
{
    static int mode = -1;
    uint32_t e0 = esp, obj = MEM32(esp + 4), p = MEM32(esp + 8), rv = MEM32(esp + 12);
    uint32_t frame = (e0 - 12u) & ~15u, res, sv_edx = edx;
    uint32_t caller_ebp = g_seh_ebp, ret = MEM32(e0);
    float r, v = 0, l = 0;
    int cmp, top = g_fp_top, vr_retained=0;
    uint16_t cc;
    RecompXmm x0;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_001C6900_gen();
        return;
    }
    memcpy(&r, &rv, 4);
    x0 = g_xmm0;
    if (sphere_fade(obj, p, r, &cmp, &cc, &v, &l, &vr_retained) == 0) {
        res = (uint32_t)(int32_t)v;
        x0.f[0] = v;
        x0.f[1] = l;
        x0.u[2] = caller_ebp;
        x0.u[3] = ret;
    } else {
        res = 0;
    }
    if (mode == 2 && !vr_retained) {
        static unsigned long calls, bad;
        sub_001C6900_gen();
        native_report("sub_001C6900", &calls, &bad,
                      eax == res && ecx == obj && edx == sv_edx && g_fp_top == top
                      && g_fp_cmp == cmp && g_fp_cc == cc && !memcmp(&x0, &g_xmm0, 16)
                      && esp == e0 + 4u && g_ebp == frame && g_seh_ebp == frame, obj, p, rv);
        return;
    }
    eax = res;
    ecx = obj;
    g_fp_cmp = cmp;
    g_fp_cc = cc;
    g_xmm0 = x0;
    g_ebp = g_seh_ebp = frame;
    esp += 4;
}

/* sub_000113A0: cdecl (dst, src) -- a 4x4 float matrix copied row by row
 * (each row read before it is written; the middle two of each row through
 * the x87, as doubles). Its argument slot src ends as src[15]'s bits. */
extern void sub_000113A0_gen(void);

void sub_000113A0(void)
{
    static int mode = -1;
    uint32_t e0 = esp, dst = MEM32(esp + 4), src = MEM32(esp + 8), r;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_000113A0_gen();
        return;
    }
    if (mode == 2) {
        static unsigned long calls, bad;
        uint32_t want[16];
        for (r = 0; r < 16; r++) {
            volatile double f = MEMF(src + 4u * r);
            float g = (float)f;
            if ((r & 3) == 1 || (r & 3) == 2)
                memcpy(&want[r], &g, 4);
            else
                want[r] = MEM32(src + 4u * r);
        }
        sub_000113A0_gen();
        native_report("sub_000113A0", &calls, &bad,
                      !memcmp(want, (const void *)XBOX_PTR(dst), 64) && eax == dst
                      && ecx == want[12] && edx == want[15] && MEM32(e0 + 8u) == want[15]
                      && esp == e0 + 4u && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u,
                      dst, src, 0);
        return;
    }
    for (r = 0; r < 64; r += 16) {
        uint32_t w0 = MEM32(src + r), w3 = MEM32(src + r + 12);
        volatile double f1 = MEMF(src + r + 4), f2 = MEMF(src + r + 8);
        MEMF(dst + r + 4) = (float)f1;
        MEM32(dst + r) = w0;
        MEMF(dst + r + 8) = (float)f2;
        MEM32(dst + r + 12) = w3;
    }
    MEM32(e0 + 8u) = MEM32(src + 0x3C);
    eax = dst;
    ecx = MEM32(src + 0x30);
    edx = MEM32(src + 0x3C);
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 4;
}

/* x87 compare as the lifted code records it; with sw, also fnstsw ax. */
static inline uint16_t fcmp_rec(double a, double b, int top, int sw)
{
    g_fp_cmp = RECOMP_FCMP(a, b);
    g_fp_cc = RECOMP_FCMP_CC(g_fp_cmp);
    if (sw)
        eax = (eax & 0xFFFF0000u) | (uint16_t)(((top & 7u) << 11) | g_fp_cc);
    return g_fp_cc;
}

/* sub_001C3430: thiscall (poly, const float p[2]) -> st(0), ret 4. Edge
 * functions e = (b.y - a.y)(p.x - a.x) - (p.y - a.y)(b.x - a.x) over the
 * corners at +0x30/+0x40/+0x50/+0x60 (a fourth edge when [poly+9] == 4);
 * returns their maximum, starting from K (0x3408FC) unless e1 is past it,
 * as soon as it is above Z (0x33FA8C). Each e goes through the argument
 * slot as a float. */
extern void sub_001C3430_gen(void);

static inline double poly_edge(uint32_t o, uint32_t p, uint32_t a, uint32_t b)
{
    return (float)((((double)MEMF(o + b + 4) - MEMF(o + a + 4))
                    * ((double)MEMF(p) - MEMF(o + a)))
                   - (((double)MEMF(p + 4) - MEMF(o + a + 4))
                      * ((double)MEMF(o + b) - MEMF(o + a))));
}

static double poly_max_edge(uint32_t o, uint32_t p, uint32_t slot, int t1)
{
    static const uint8_t edges[3][2] = { { 0x40, 0x50 }, { 0x60, 0x30 }, { 0x50, 0x60 } };
    const double k = MEMF(0x003408FCu), z = MEMF(0x0033FA8Cu);
    float e = (float)poly_edge(o, p, 0x30, 0x40);
    double m;
    int i;

    MEMF(slot) = e;
    if (!(fcmp_rec(k, e, t1, 1) & 0x4100u)) {
        m = k;
    } else {
        if (!(fcmp_rec(e, z, t1, 1) & 0x4100u))
            return e;
        m = e;
    }
    for (i = 0; i < 3; i++) {
        if (i == 2 && MEM8(o + 9) != 4)
            return m;
        e = (float)poly_edge(o, p, edges[i][0], edges[i][1]);
        MEMF(slot) = e;
        if (fcmp_rec(m, e, t1, 1) & 0x4100u)
            m = e;
        if (!(fcmp_rec(m, z, t1, i < 2) & 0x4100u) || i == 2)
            return m;
    }
    return m;
}

void sub_001C3430(void)
{
    static int mode = -1;
    uint32_t e0 = esp, o = ecx, p = MEM32(esp + 4);
    int top0 = g_fp_top, t1 = (top0 + 7) & 7;
    double r;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_001C3430_gen();
        return;
    }
    if (mode == 2) {
        static unsigned long calls, bad;
        uint32_t a0 = eax, a, sl;
        int cmp;
        uint16_t cc;
        r = poly_max_edge(o, p, e0 + 4u, t1);
        a = eax; cmp = g_fp_cmp; cc = g_fp_cc; sl = MEM32(e0 + 4u);
        eax = a0;
        MEM32(e0 + 4u) = p;
        sub_001C3430_gen();
        native_report("sub_001C3430", &calls, &bad,
                      !memcmp(&r, &g_fp_stack[t1], 8) && g_fp_top == t1 && eax == a
                      && g_fp_cmp == cmp && g_fp_cc == cc && MEM32(e0 + 4u) == sl
                      && ecx == o && edx == p && esp == e0 + 8u
                      && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u, o, p, 0);
        return;
    }
    r = poly_max_edge(o, p, e0 + 4u, t1);
    g_fp_top = t1;
    g_fp_stack[t1] = r;
    edx = p;
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 8;                           /* return address + one argument */
}

/* sub_0004C6E0: cdecl (const float p[2], const float s[2], const float q[2],
 * float m) -> 1 when q lies within [p - m, p + s + m] on both axes. */
extern void sub_0004C6E0_gen(void);

void sub_0004C6E0(void)
{
    static int mode = -1;
    uint32_t e0 = esp, p = MEM32(esp + 4), s = MEM32(esp + 8), q = MEM32(esp + 12);
    uint32_t sv_eax = eax, sv_edx = edx, res = 0, a, d = edx;
    double m = MEMF(esp + 16);
    int top = g_fp_top, k, cmp;
    uint16_t cc;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_0004C6E0_gen();
        return;
    }
    for (k = 0; k < 2; k++) {
        if (!(fcmp_rec((double)MEMF(p + 4u * k) - m, MEMF(q + 4u * k), top, 1) & 0x4100u))
            break;
        d = s;
        if ((fcmp_rec(m + MEMF(s + 4u * k), MEMF(q + 4u * k), top, 1) & 0x4500u) == 0x0100u)
            break;
    }
    res = k == 2;
    a = res; cmp = g_fp_cmp; cc = g_fp_cc;
    if (mode == 2) {
        static unsigned long calls, bad;
        eax = sv_eax;
        edx = sv_edx;
        sub_0004C6E0_gen();
        native_report("sub_0004C6E0", &calls, &bad,
                      eax == a && ecx == q && edx == d && g_fp_cmp == cmp && g_fp_cc == cc
                      && g_fp_top == top && esp == e0 + 4u
                      && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u, p, s, q);
        return;
    }
    eax = res;
    ecx = q;
    edx = d;
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 4;
}

/* sub_001C65D0: thiscall (obj, float out[12]), ret 4: three rows of int16
 * (obj+0x2C..0x3C) times S (0x35D678) as floats, each row padded with a
 * zero dword. Its argument slot ends as the last int16 read. */
extern void sub_001C65D0_gen(void);

void sub_001C65D0(void)
{
    static int mode = -1;
    uint32_t e0 = esp, o = ecx, out = MEM32(esp + 4), r;
    const double sc = MEMF(0x0035D678u);
    float v[12];
    int top = g_fp_top;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_001C65D0_gen();
        return;
    }
    for (r = 0; r < 3; r++) {
        uint32_t b = o + 0x2Cu + 6u * r;
        v[4 * r]     = (float)((double)(int16_t)MEM16(b) * sc);
        v[4 * r + 1] = (float)((double)(int16_t)MEM16(b + 2) * sc);
        v[4 * r + 2] = (float)((double)(int16_t)MEM16(b + 4) * sc);
        memset(&v[4 * r + 3], 0, 4);
    }
    if (mode == 2) {
        static unsigned long calls, bad;
        uint32_t sv_esi = esi;
        sub_001C65D0_gen();
        native_report("sub_001C65D0", &calls, &bad,
                      !memcmp(v, (const void *)XBOX_PTR(out), 48) && eax == out
                      && ecx == (uint32_t)(int32_t)(int16_t)MEM16(o + 0x38) && edx == 0
                      && esi == sv_esi && MEM32(e0 + 4u) == ecx && g_fp_top == top
                      && esp == e0 + 8u && g_ebp == e0 - 4u && g_seh_ebp == e0 - 4u,
                      o, out, 0);
        return;
    }
    memcpy((void *)XBOX_PTR(out), v, 48);
    eax = out;
    ecx = (uint32_t)(int32_t)(int16_t)MEM16(o + 0x38);
    edx = 0;
    MEM32(e0 + 4u) = ecx;
    g_ebp = g_seh_ebp = e0 - 4u;
    esp += 8;
}

/* sub_002EEA80: D3D's SSE 4x4 multiply, stdcall (out, a, b), ret 12 --
 * the same sums as sub_002213EA (mat4_mul). Leaves the result rows in
 * xmm2..xmm5 and a[3][2] * b row 2 / a[3][3] * b row 3 in xmm0 / xmm1;
 * eax = a, ecx = out. */
extern void sub_002EEA80_gen(void);

NFSU2_NO_FMA
static void d3d_mat_mul(uint32_t out, uint32_t a, uint32_t b, RecompXmm x[6])
{
    float ma[16], mb[16], o[16];
    int k;
    memcpy(ma, (const void *)XBOX_PTR(a), 64);
    memcpy(mb, (const void *)XBOX_PTR(b), 64);
    mat4_mul(o, ma, mb);
    for (k = 0; k < 4; k++) {
        x[0].f[k] = ma[14] * mb[8 + k];
        x[1].f[k] = ma[15] * mb[12 + k];
    }
    memcpy(&x[2], o, 64);
}

void sub_002EEA80(void)
{
    static int mode = -1;
    uint32_t e0 = esp, out = MEM32(esp + 4), a = MEM32(esp + 8), b = MEM32(esp + 12);
    RecompXmm x[6];

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_002EEA80_gen();
        return;
    }
    d3d_mat_mul(out, a, b, x);
    if (mode == 2) {
        static unsigned long calls, bad;
        sub_002EEA80_gen();
        native_report("sub_002EEA80", &calls, &bad,
                      !memcmp(&x[2], (const void *)XBOX_PTR(out), 64) && eax == a && ecx == out
                      && !memcmp(&x[0], &g_xmm0, 16) && !memcmp(&x[1], &g_xmm1, 16)
                      && !memcmp(&x[2], &g_xmm2, 16) && !memcmp(&x[3], &g_xmm3, 16)
                      && !memcmp(&x[4], &g_xmm4, 16) && !memcmp(&x[5], &g_xmm5, 16)
                      && esp == e0 + 16u, out, a, b);
        return;
    }
    memcpy((void *)XBOX_PTR(out), &x[2], 64);
    g_xmm0 = x[0]; g_xmm1 = x[1]; g_xmm2 = x[2];
    g_xmm3 = x[3]; g_xmm4 = x[4]; g_xmm5 = x[5];
    eax = a;
    ecx = out;
    esp += 16;                          /* return address + three arguments */
}

/* sub_0004B940: cdecl (out, a, b): t = b * a (row-major), each element
 * summed in its own order (as the compiler scheduled it), then copied to
 * out by sub_000113A0 -- whose frame, ecx and edx it leaves behind. */
extern void sub_0004B940_gen(void);

static void mat_mul_ba(const float *A, const float *B, float *t)
{
    t[0] = (float)((((double)A[0] * B[0] + (double)B[3] * A[12]) + (double)B[2] * A[8]) + (double)B[1] * A[4]);
    t[1] = (float)((((double)A[13] * B[3] + (double)A[1] * B[0]) + (double)B[1] * A[5]) + (double)A[9] * B[2]);
    t[2] = (float)((((double)A[10] * B[2] + (double)A[6] * B[1]) + (double)A[2] * B[0]) + (double)B[3] * A[14]);
    t[3] = (float)((((double)A[7] * B[1] + (double)A[15] * B[3]) + (double)B[2] * A[11]) + (double)B[0] * A[3]);
    t[4] = (float)((((double)B[6] * A[8] + (double)A[4] * B[5]) + (double)B[7] * A[12]) + (double)B[4] * A[0]);
    t[5] = (float)((((double)B[6] * A[9] + (double)B[4] * A[1]) + (double)B[5] * A[5]) + (double)B[7] * A[13]);
    t[6] = (float)((((double)B[7] * A[14] + (double)B[6] * A[10]) + (double)A[6] * B[5]) + (double)B[4] * A[2]);
    t[7] = (float)((((double)A[15] * B[7] + (double)B[6] * A[11]) + (double)B[4] * A[3]) + (double)A[7] * B[5]);
    t[8] = (float)((((double)B[10] * A[8] + (double)B[9] * A[4]) + (double)B[11] * A[12]) + (double)B[8] * A[0]);
    t[9] = (float)((((double)B[10] * A[9] + (double)B[8] * A[1]) + (double)B[9] * A[5]) + (double)B[11] * A[13]);
    t[10] = (float)((((double)B[11] * A[14] + (double)B[10] * A[10]) + (double)A[6] * B[9]) + (double)B[8] * A[2]);
    t[11] = (float)((((double)A[15] * B[11] + (double)B[10] * A[11]) + (double)B[8] * A[3]) + (double)A[7] * B[9]);
    t[12] = (float)((((double)B[14] * A[8] + (double)B[13] * A[4]) + (double)B[15] * A[12]) + (double)B[12] * A[0]);
    t[13] = (float)((((double)B[14] * A[9] + (double)B[12] * A[1]) + (double)B[13] * A[5]) + (double)B[15] * A[13]);
    t[14] = (float)((((double)B[15] * A[14] + (double)B[14] * A[10]) + (double)A[6] * B[13]) + (double)B[12] * A[2]);
    t[15] = (float)((((double)A[15] * B[15] + (double)B[14] * A[11]) + (double)B[12] * A[3]) + (double)A[7] * B[13]);
}

void sub_0004B940(void)
{
    static int mode = -1;
    uint32_t e0 = esp, out = MEM32(esp + 4), a = MEM32(esp + 8), b = MEM32(esp + 12);
    uint32_t frame = ((e0 - 12u) & ~15u) - 0x50u, w12, w15;
    float A[16], B[16], t[16];
    int top = g_fp_top;

    if (mode < 0)
        mode = native_mode();
    if (mode == 0) {
        sub_0004B940_gen();
        return;
    }
    memcpy(A, (const void *)XBOX_PTR(a), 64);
    memcpy(B, (const void *)XBOX_PTR(b), 64);
    mat_mul_ba(A, B, t);
    memcpy(&w12, &t[12], 4);
    memcpy(&w15, &t[15], 4);
    if (mode == 2) {
        static unsigned long calls, bad;
        sub_0004B940_gen();
        native_report("sub_0004B940", &calls, &bad,
                      !memcmp(t, (const void *)XBOX_PTR(out), 64) && eax == out && ecx == w12
                      && edx == w15 && g_fp_top == top && esp == e0 + 4u
                      && g_ebp == frame && g_seh_ebp == frame, out, a, b);
        return;
    }
    memcpy((void *)XBOX_PTR(out), t, 64);
    eax = out;
    ecx = w12;
    edx = w15;
    g_ebp = g_seh_ebp = frame;
    esp += 4;
}

/* ── Colour grade table, sub_000A3CA0 ─────────────────────────
 *
 * cdecl (k): rebuilds grade table k (0/1) -- N x N A8R8G8B8 texels, N =
 * [0x39D324], swizzled (masks as sub_00098D70 makes them for N x N x 0),
 * at [0x3E8C18] + 4 * N * N * k -- from its four curve parameters at
 * 0x39D304 + 16k (P0..P3), and caches them at 0x3E8BF0 + 16k. Its callers
 * (sub_000A3FA0) rebuild whenever a parameter changed, which in races is
 * every frame: 2% of the main thread on x86, each texel four divisions and
 * four _ftol2 calls. Texel (i, j): channels from x = i/N (A from P3, B from
 * P1, as floats), y = j/N (R from P0 as a float, the last byte from P2
 * unrounded -- it never leaves the x87 stack), each through
 *   t = (K1 - 2v) * (K1 / P - K3)   (stored as a float)
 *   v < K2 ? v / (t + K1) : (t - v) / (t - K1)   (NaN: the second)
 * times K4, truncated. Exact, as the lifted code computes it (doubles,
 * float rounding where it stores). RECOMP_NATIVE=0 lifted,
 * RECOMP_NATIVE_CHECK=1 both and compare. */
extern void sub_000A3CA0_gen(void);

static inline double grade_curve(double v, double p, double k1, double k2, double k3)
{
    double t = (float)((k1 - (v + v)) * (k1 / p - k3));
    return v < k2 ? v / (t + k1) : (t - v) / (t - k1);
}

static inline uint32_t grade_byte(double v)
{
    uint32_t a, d, c = 0;
    ftol2_native(v, &a, &d, &c);
    return a & 0xFFu;
}

/* Fills out[] (N * N texels at their swizzled offsets) for table k. */
static void grade_table(uint32_t *out, uint32_t k, int32_t n)
{
    const double k1 = MEMF(0x003408ACu), k2 = MEMF(0x003408BCu);
    const double k3 = MEMF(0x00343F00u), k4 = MEMF(0x00343658u);
    const uint32_t pb = 0x0039D304u + 16u * k;
    const double p0 = MEMF(pb), p1 = MEMF(pb + 4), p2 = MEMF(pb + 8), p3 = MEMF(pb + 12);
    const double nf = (float)n;
    uint32_t mu = 0, mv = 0, bit = 1, lvl, us = 0, vs, base = (uint32_t)(n * n) * k;
    int32_t i, j;

    for (lvl = 1;; lvl <<= 1) {         /* sub_00098D70(n, n, 0) */
        int any = 0;
        if (lvl < (uint32_t)n) { mu |= bit; bit <<= 1; any = 1; }
        if (lvl < (uint32_t)n) { mv |= bit; bit <<= 1; any = 1; }
        if (!any)
            break;
    }
    for (i = 0; i < n; i++, us = (us - mu) & mu) {
        double x = (double)i / (double)n;
        uint32_t ca = grade_byte((double)(float)grade_curve((float)x, p3, k1, k2, k3) * k4);
        uint32_t cb = grade_byte((double)(float)grade_curve((float)x, p1, k1, k2, k3) * k4);
        for (j = 0, vs = 0; j < n; j++, vs = (vs - mv) & mv) {
            double y = (double)j / nf;
            uint32_t cr = grade_byte((double)(float)grade_curve((float)y, p0, k1, k2, k3) * k4);
            uint32_t cd = grade_byte(grade_curve(y, p2, k1, k2, k3) * k4);
            out[base + (us | vs)] = ca << 24 | cr << 16 | cb << 8 | cd;
        }
    }
}

void sub_000A3CA0(void)
{
    static int mode = -1;               /* 0 lifted, 1 native, 2 native + check */
    uint32_t k = MEM32(esp + 4), tab = MEM32(0x003E8C18u);
    int32_t n = (int32_t)MEM32(0x0039D324u);
    uint32_t e0 = esp, j;

    if (mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
        mode = (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
    }
    if (mode == 0 || n <= 0 || n > 256) {
        sub_000A3CA0_gen();
        return;
    }
    if (mode == 2) {
        static unsigned long calls, bad;
        size_t cnt = (size_t)n * (size_t)n * (k + 1u);
        uint32_t *mine = malloc(cnt * 4u), sv_ebx = ebx, sv_esi = esi, sv_edi = edi;
        int top = g_fp_top;
        memcpy(mine, (const void *)XBOX_PTR(tab), cnt * 4u);
        grade_table(mine, k, n);
        sub_000A3CA0_gen();
        calls++;
        if ((memcmp(mine, (const void *)XBOX_PTR(tab), cnt * 4u) || g_fp_top != top
             || esp != e0 + 4u || ebx != sv_ebx || esi != sv_esi || edi != sv_edi
             || eax != k << 4 || ecx != MEM32(0x0039D30Cu + 16u * k)
             || edx != MEM32(0x0039D310u + 16u * k) || g_ebp != e0 - 0x5Cu) && bad++ < 20) {
            for (j = 0; j < cnt && mine[j] == MEM32(tab + 4u * j); j++)
                ;
            fprintf(stderr, "[native] sub_000A3CA0 mismatch: table %u, n %d, texel %u "
                    "native %08X lifted %08X, ebp %08X (%08X)\n", k, n, j,
                    j < cnt ? mine[j] : 0, j < cnt ? MEM32(tab + 4u * j) : 0, g_ebp, e0 - 0x5Cu);
        }
        if ((calls & 0x3FF) == 0)
            fprintf(stderr, "[native] sub_000A3CA0: %lu calls, %lu mismatches\n", calls, bad);
        free(mine);
        return;
    }
    grade_table((uint32_t *)XBOX_PTR(tab), k, n);
    for (j = 0; j < 4; j++)
        MEM32(0x003E8BF0u + 16u * k + 4u * j) = MEM32(0x0039D304u + 16u * k + 4u * j);
    eax = k << 4;
    ecx = MEM32(0x0039D30Cu + 16u * k);
    edx = MEM32(0x0039D310u + 16u * k);
    g_ebp = g_seh_ebp = e0 - 0x5Cu;     /* the frame its last _ftol2 call published */
    esp += 4;                           /* cdecl: return address only */
}

/* ── Movie colour conversion, sub_0025ECB4 ───────────────────
 *
 * cdecl (y, u, v, dst, dst_end): one row of a VP6 picture to A8R8G8B8, two
 * pixels per step. MMX table lookups: four words per entry, Y at 0x3D0810,
 * U at 0x3D1010, V at 0x3D1810 (256 x 8 bytes each); pixel = Y[y] + (U[u] +
 * V[v]) with paddw wrap, packuswb to bytes. sub_0025F0B7 calls it for every
 * row of every movie frame, and FFmpeg does not replace it: lifted it was
 * the largest cost of a movie left after the decoder. Exact, including the
 * registers it leaves behind. RECOMP_NATIVE=0 lifted, RECOMP_NATIVE_CHECK=1
 * both and compare. */
extern void sub_0025ECB4_gen(void);

/* Last row written: lies in the movie texture (src/movie_crop.c). */
uint32_t nfsu2_movie_row;

#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

static inline NFSU2_UNUSED uint8_t sat_u8(int16_t w)
{
    return w < 0 ? 0 : w > 255 ? 255 : (uint8_t)w;
}

static void yuv_row_native(uint32_t y, uint32_t u, uint32_t v, uint8_t *out, uint32_t n)
{
    const int16_t *ty = (const int16_t *)XBOX_PTR(0x003D0810u);
    const int16_t *tu = (const int16_t *)XBOX_PTR(0x003D1010u);
    const int16_t *tv = (const int16_t *)XBOX_PTR(0x003D1810u);
    const uint8_t *py = (const uint8_t *)XBOX_PTR(y);
    const uint8_t *pu = (const uint8_t *)XBOX_PTR(u);
    const uint8_t *pv = (const uint8_t *)XBOX_PTR(v);
    uint32_t i;
#if defined(__aarch64__)
    /* vadd wraps like paddw, vqmovun saturates like packuswb */
    for (i = 0; i < n; i++, out += 8) {
        int16x4_t c = vadd_s16(vld1_s16(tu + 4 * pu[i]), vld1_s16(tv + 4 * pv[i]));
        int16x4_t w0 = vadd_s16(vld1_s16(ty + 4 * py[2 * i]), c);
        int16x4_t w1 = vadd_s16(vld1_s16(ty + 4 * py[2 * i + 1]), c);
        vst1_u8(out, vqmovun_s16(vcombine_s16(w0, w1)));
    }
#elif defined(__SSE2__) || defined(_M_X64)
    for (i = 0; i < n; i++, out += 8) {
        __m128i c = _mm_add_epi16(_mm_loadl_epi64((const __m128i *)(tu + 4 * pu[i])),
                                  _mm_loadl_epi64((const __m128i *)(tv + 4 * pv[i])));
        __m128i w = _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(ty + 4 * py[2 * i])),
                                       _mm_loadl_epi64((const __m128i *)(ty + 4 * py[2 * i + 1])));
        w = _mm_add_epi16(w, _mm_unpacklo_epi64(c, c));
        _mm_storel_epi64((__m128i *)out, _mm_packus_epi16(w, w));
    }
#else
    int k;
    for (i = 0; i < n; i++, out += 8) {
        const int16_t *a = tu + 4 * pu[i], *b = tv + 4 * pv[i];
        const int16_t *y0 = ty + 4 * py[2 * i], *y1 = ty + 4 * py[2 * i + 1];
        for (k = 0; k < 4; k++) {
            int16_t c = (int16_t)(a[k] + b[k]);
            out[k] = sat_u8((int16_t)(y0[k] + c));
            out[4 + k] = sat_u8((int16_t)(y1[k] + c));
        }
    }
#endif
}

void sub_0025ECB4(void)
{
    static int mode = -1;               /* 0 lifted, 1 native, 2 native + check */
    uint32_t y = MEM32(esp + 4), u = MEM32(esp + 8), v = MEM32(esp + 12);
    uint32_t dst = MEM32(esp + 16), end = MEM32(esp + 20);
    /* do-while in the original: at least one step, until dst == end */
    uint32_t n = end > dst ? (end - dst) / 8u : 1u, last, k;

    if (mode < 0) {
        const char *e = getenv("RECOMP_NATIVE"), *c = getenv("RECOMP_NATIVE_CHECK");
        mode = (e && *e == '0') ? 0 : (c && *c == '1') ? 2 : 1;
    }
    if (mode == 0 || ((end - dst) & 7u) || n > 4096u) {
        sub_0025ECB4_gen();
        return;
    }
    if (mode == 2) {
        static unsigned long rows, bad;
        static uint8_t buf[4096 * 8];
        yuv_row_native(y, u, v, buf, n);
        sub_0025ECB4_gen();
        rows++;
        if (memcmp(buf, (const void *)XBOX_PTR(dst), n * 8u) && bad++ < 20)
            fprintf(stderr, "[native] sub_0025ECB4 mismatch: row %lu at %08X\n", rows, dst);
        if ((rows & 0xFFFF) == 0)
            fprintf(stderr, "[native] sub_0025ECB4: %lu rows, %lu mismatches\n", rows, bad);
        return;
    }
    nfsu2_movie_row = dst;
    yuv_row_native(y, u, v, (uint8_t *)XBOX_PTR(dst), n);

    /* What the last step leaves in the registers. */
    last = n - 1u;
    {
        const int16_t *ty = (const int16_t *)XBOX_PTR(0x003D0810u);
        const int16_t *tu = (const int16_t *)XBOX_PTR(0x003D1010u);
        const int16_t *tv = (const int16_t *)XBOX_PTR(0x003D1810u);
        const int16_t *a = tu + 4 * MEM8(u + last), *b = tv + 4 * MEM8(v + last);
        const int16_t *y1 = ty + 4 * MEM8(y + 2u * last + 1u);
        for (k = 0; k < 4; k++) {
            mm3.w[k] = b[k];
            mm2.w[k] = (int16_t)(a[k] + b[k]);
            mm1.w[k] = (int16_t)(y1[k] + mm2.w[k]);
        }
        memcpy(&mm0, (const void *)XBOX_PTR(dst + 8u * last), 8);
    }
    eax = 0;
    ecx = y + 2u * n;
    edx = u + n;
    esp += 4;                           /* cdecl: the caller pops the arguments */
}

/* ── Movie file names, sub_00129610 ──────────────────────────
 *
 * cdecl (buf, size, name): snprintf "%sMOVIES\\%s%s" -- the path of a movie
 * with its language suffix (_en.vp6 ...). The last one is kept for
 * src/movie_crop.c. */
extern void sub_00129610_gen(void);

char nfsu2_movie_name[64];

void sub_00129610(void)
{
    uint32_t buf = MEM32(esp + 4), i;

    sub_00129610_gen();
    for (i = 0; i < sizeof nfsu2_movie_name - 1 && MEM8(buf + i); i++)
        nfsu2_movie_name[i] = (char)MEM8(buf + i);
    nfsu2_movie_name[i] = 0;
    fprintf(stderr, "[movie] %s\n", nfsu2_movie_name);
}

/* ── DirectSound DSP command post (0x0032EB65) ───────────────
 *
 * thiscall, ecx = the DSP-side object. Copies a command block into the GP
 * DSP's scratch area, stores the command code at scratch + 0x810, and spins
 * until the DSP zeroes it (this loop does re-read memory). The scratch block
 * is ***(this + 8 + 0x10), the same chain the runtime's RECOMP_DSP_ACK notes
 * describe. There is no DSP, so register the word with the runtime's
 * completion list before running the original body. */
extern int xbox_ApuDspAckWord(uint32_t va);
extern void sub_0032EB65_gen(void);

void sub_0032EB65(void)
{
    uint32_t obj = MEM32(ecx + 8);
    uint32_t blk = obj ? MEM32(obj + 0x10) : 0;
    uint32_t scratch = blk ? MEM32(blk) : 0;

    if (scratch)
        xbox_ApuDspAckWord(scratch + 0x810);
    sub_0032EB65_gen();
}

/* ── DirectSound AC'97 channel reset (0x0033518D) ────────────
 *
 * thiscall, ecx = channel object. Sets RR (bit 1) in the channel's NABM
 * control byte, then waits for the controller to clear it -- except MSVC
 * hoisted the load out of the loop, so the original reads the byte once and
 * spins on the register copy forever unless the bit is already clear at that
 * one read:
 *
 *     mov  byte [eax+0xFEC0010B], 2
 *     mov  cl, [eax+0xFEC0010B]
 *     and  cl, 2
 *   L: test cl, cl
 *     jne  L
 *
 * On hardware the reset has completed by then. The Windows runtime answers it
 * with a write trap on the NABM page; there is no fault handling on POSIX or
 * Switch, so this is the same function with the reset completing at once.
 * Everything else -- the lock taken through sub_0032BFF0/sub_0032C012, the
 * buffer-descriptor base store, the 0xFEC0017C write for channel 1 and the
 * sub_00334F97 re-arm -- is as the original does it. */
void sub_0033518D(void)
{
    uint32_t ebp_saved = g_ebp, frame, chan, nabm;

    PUSH32(esp, ebp_saved);
    frame = esp;
    g_ebp = frame;
    g_seh_ebp = frame;
    esp -= 8;
    MEM32(frame - 4) = 0;
    PUSH32(esp, esi);
    esi = ecx;
    chan = esi;

    ecx = frame - 8;
    PUSH32(esp, 0x003351A1u); RECOMP_ABI_CALL(0x0032BFF0u, sub_0032BFF0);

    nabm = MEM32(MEM32(chan) * 4 + 0x335DE0u);
    MEM8(nabm + 0xFEC0010Bu) = 2;
    MEM8(nabm + 0xFEC0010Bu) &= (uint8_t)~2u;   /* reset complete */
    eax = nabm;
    SET_LO8(ecx, 0);

    MEM32(nabm + 0xFEC00100u) = MEM32(chan + 0x1C);
    if (MEM32(chan) == 1)
        MEM32(0xFEC0017Cu) = MEM32(chan + 0x28);

    PUSH32(esp, 1);
    PUSH32(esp, 1);
    ecx = chan;
    PUSH32(esp, MEM8(chan + 0x24));
    PUSH32(esp, MEM8(chan + 0x25));
    eax = MEM8(chan + 0x25);
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x003351F4u); RECOMP_ABI_CALL(0x00334F97u, sub_00334F97);

    ecx = frame - 8;
    g_ebp = frame;
    g_seh_ebp = frame;
    PUSH32(esp, 0x003351FCu); RECOMP_ABI_CALL(0x0032C012u, sub_0032C012);

    POP32(esp, esi);
    esp = frame;
    POP32(esp, ebp_saved);
    g_ebp = ebp_saved;
    esp += 4;
}

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
/* Registered chunk-loader callback (no direct callers in the supported XBE).
 * Keep the original generated function: the manual dispatcher intercepts the
 * callback, and this direct call deliberately invokes its unchanged parser. */
static void community_load_career(void)
{
    if(quest_xr_hidden_races() && xyz_xbox_address(esp,8)) {
        uint32_t chunk=MEM32(esp+4);
        if(xyz_xbox_address(chunk,8) && MEM32(chunk)==0x80034a10u) {
            uint32_t bytes=MEM32(chunk+4);
            if(bytes<=131064 && xyz_xbox_address(chunk,bytes+8)) {
                int changed=vr_mod_hidden_races((void *)XBOX_PTR(chunk),bytes+8,1);
                if(changed==1)fprintf(stderr,"[Community mods] Show Hidden Races: 33 races revealed; stage caps raised to 8\n");
                else if(changed<0)fprintf(stderr,"[Community mods] Show Hidden Races skipped: unsupported career tables\n");
            }
        }
    }
    sub_0011F370();
}
#endif
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    if(xbox_va==0x00196390u && community_lobby_callback_owner())return community_lobby_callback;
    if(xbox_va==0x00196390u && community_engine_callback_owner())return community_engine_callback;
    if(xbox_va==0x00196410u && community_engine_join_owner())return community_engine_join_callback;
    if(xbox_va==0x00196680u && community_engine_join_owner())return community_engine_search_callback;
    if(xbox_va==0x0018f530u && community_engine_request_owner())return community_engine_request_callback;
    if(xbox_va==0x00198a40u && community_engine_join_owner())return community_engine_game_callback;
    if(xbox_va==0x0011f370u)return community_load_career;
    if(xbox_va==0x0002d800u)return cockpit_driver_update;
    if(xbox_va==0x00019d30u)return cockpit_race_stage;
    if(xbox_va==0x00016c70u)return cockpit_driver_first;
    if(xbox_va==0x00129550u)return cockpit_hud_package_begin;
    if(xbox_va==0x0027ba23u && MEM32(esp)==0x002798d4u)return cockpit_radio_resampled;
    if(xbox_va==0x0027c5ebu && MEM32(esp)==0x002798d4u)return cockpit_radio_decoded;
#endif
    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * Called when RECOMP_ICALL cannot resolve a target address.
 * This usually means one of:
 *   - A vtable dispatch to an address not in the dispatch table
 *   - A function pointer loaded from uninitialized or corrupt memory
 *   - A kernel thunk address that the bridge doesn't handle
 *
 * During early bring-up you will see many of these. Most are harmless
 * (the ICALL macro pops the dummy return address and continues).
 * Focus on the ones that cause crashes or incorrect behavior.
 */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[ICALL] Failed to resolve VA 0x%08X (total calls: %llu)\n",
            va, (unsigned long long)g_icall_count);

    /* Dump last 16 call targets from the ring buffer */
    fprintf(stderr, "  Recent ICALL targets:\n");
    for (int i = 0; i < 16; i++) {
        int idx = (g_icall_trace_idx - 16 + i) & 15;
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/* An indirect call whose target is not code: a null or wild function pointer.
 *
 * Skipping these is right -- calling a data address is worse -- but skipping
 * them *silently* is not. They almost always arrive inside a loop, so the
 * symptom is a hang with no output rather than a diagnosable null vtable call.
 *
 * Rate-limited per address: a spin can produce millions of these, and the
 * useful information is which addresses occur, not how often.
 */
void recomp_icall_not_code_log(uint32_t va)
{
    enum { SLOTS = 16 };
    static uint32_t seen[SLOTS];
    static uint64_t hits[SLOTS];
    static int count;
    int i;

    for (i = 0; i < count; i++)
        if (seen[i] == va)
            break;
    if (i == count) {
        if (count == SLOTS)
            return;
        seen[count] = va;
        hits[count] = 0;
        count++;
    }
    hits[i]++;
    /* Report at 1, 10, 100, 1000 ... rather than once. A single line says a
     * wild pointer was skipped; the progression says it is being skipped in a
     * loop, which is the difference between a curiosity and the reason the
     * title is hung. */
    {
        uint64_t n = hits[i];
        while (n >= 10 && n % 10 == 0)
            n /= 10;
        if (n != 1)
            return;
    }
    fprintf(stderr, "[ICALL] target 0x%08X is not code -- skipped %llu time(s) "
                    "(null or wild function pointer, at call #%llu)\n",
            va, (unsigned long long)hits[i],
            (unsigned long long)g_icall_count);
    fflush(stderr);
}

/* ── Untranslated instructions ───────────────────────────────────────────
 *
 * The lifter emits RECOMP_UNIMPL(text, va) at every instruction it has no
 * translation for, in place of the bare comment it used to leave. The
 * instruction is still a no-op; this only stops the omission being silent.
 * RECOMP_UNIMPL_TRAP=1 aborts at the first hit, at the guest address of the
 * cause rather than wherever the damage surfaces. */
#include <stdlib.h>

void recomp_unimpl(const char *text, uint32_t va)
{
    static int printed;
    const char *trap = getenv("RECOMP_UNIMPL_TRAP");
    int stop = trap && *trap && *trap != '0';

    if (printed < 50 || stop) {
        printed++;
        fprintf(stderr,
                "[UNIMPL] untranslated instruction REACHED: `%s` at 0x%08X"
                " (a no-op; set RECOMP_UNIMPL_TRAP=1 to stop here)\n",
                text, va);
        fflush(stderr);
    }
    if (stop) abort();
}

/* Xbox P8 skin compositor: recipe and source gloss are captured before the
 * original call, then only a host GPU replacement is published. */
extern void sub_00201520_gen(void);
void sub_00201520(void)
{
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    static unsigned vinyl_calls;
    if(getenv("NFSU2_VINYL_PROBE") && vinyl_calls++<12 && xyz_xbox_address(MEM32(esp+4),0x234)){
        uint32_t r=MEM32(esp+4),ti=MEM32(r);
        if(xyz_xbox_address(ti,0x7c))fprintf(stderr,"[VR vinyl probe] caller=%08x recipe=%08x target=%08x size=%ux%u bits=%u mips=%u layers=%u ride=%08x\n",
            MEM32(esp),r,ti,MEM16(ti+0x44),MEM16(ti+0x46),MEM8(ti+0x4a),MEM8(ti+0x4e),MEM32(r+0x1c4),MEM32(r+0x230));
    }
    void *job=MEM32(esp)==0x20931eu?vr_vinyls_begin(cockpit_ui_read,NULL,MEM32(esp+4)):NULL;
#endif
    sub_00201520_gen();
#if defined(__ANDROID__) || defined(NFSU2_PCVR)
    vr_vinyls_finish(job,cockpit_ui_read,NULL,eax!=0);
#endif
}
