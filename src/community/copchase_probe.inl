/* Opt-in diagnostic for one original indexed traffic car. This is not the
 * CopChase gameplay feature. Address/ABI evidence: build/copchase-re/xbox/
 * PSYCHO-DRIVER.md and pursue-xbox.asm. Included on the guest main thread,
 * after guest_call_full, cockpit_ui_read and the community API headers. */
#include "copchase_assets.h"

typedef struct {
    int requested, phase, pending_stop, busy;
    uint32_t world, car, record, driver, control, player_car, index;
    uint32_t saved_physics, saved_734, saved_speed, saved_234, saved_238;
    uint8_t saved_enable;
    float waiting, stable, elapsed, observed, target[2], speed, start[2];
    unsigned driver_calls, heading_calls, logs;
    char model[24];
} CommunityCopChaseProbe;
static CommunityCopChaseProbe community_copchase_probe = { .requested = -1 };

static int community_copchase_probe_enabled(void)
{
    CommunityCopChaseProbe *p = &community_copchase_probe;
    if (p->requested < 0) {
        const char *e = getenv("NFSU2_COPCHASE_AI_PROBE");
        const char *a = getenv("NFSU2_COPCHASE_ASSETS_PROBE");
        const char *s = getenv("RECOMP_PAD_SCRIPT");
        p->requested = e && !strcmp(e,"1") && a && !strcmp(a,"1") && s && *s;
        if (p->requested) fprintf(stderr,"[CopChase AI probe] armed: isolated one-car diagnostic\n");
    }
    return p->requested && copchase_assets_ready();
}

static int community_copchase_probe_offline(void)
{
    XYZOnlineStatus status;
    xyz_online_snapshot(&status);
    return status.state == XYZ_ONLINE_OFFLINE && !xyz_races_active() &&
           !MEM32(0x456290) && xyz_online_fleet_retired();
}

static int community_copchase_probe_context(void)
{
    if (MEM32(0x428e34) != 6 || !community_copchase_probe_offline()) return 0;
    uint32_t mode = MEM32(0x41c338), race = MEM32(0x3cc470);
    /* The Quick Race fixture is deliberately limited to an offline Circuit. */
    return (mode == 1 && race == 0 && !MEM32(0x406960)) || (mode == 2 && race == 1);
}

static int community_copchase_probe_idle(void)
{
    XyzXboxStreaming streaming;
    return xyz_xbox_streaming(cockpit_ui_read,NULL,&streaming) &&
        !streaming.phase && !streaming.car_busy &&
        !MEM32(0x38dbb0) && !MEM32(0x38dbc8) && !MEM32(0x38dbe0) &&
        !MEM8(0x38dba4) && !MEM8(0x38dbbc) && !MEM8(0x38dbd4) && !MEM8(0x3e3ab0);
}

static int community_copchase_probe_model(uint32_t car, char name[24])
{
    static const char *const mixed8[] = {
        "4DR_SEDAN","MINIVAN","COUPE","TAXI02","HATCHBACK","BUS","PICKUP","SUV"
    }; /* Exact active_slots in the validated mixed8 manifest. */
    uint32_t types = MEM32(0x45af88), type = MEM32(car+0xc);
    if (!xyz_xbox_address(types,46u*0x890u) || type < types ||
        type-types >= 46u*0x890u || (type-types)%0x890u || MEM32(type+0x844) != 2)
        return 0;
    if (!cockpit_ui_read(NULL,type,name,24) || !memchr(name,0,24)) return 0;
    for (unsigned i=0;i<sizeof mixed8/sizeof *mixed8;++i)
        if (!strcmp(name,mixed8[i])) return 1;
    return 0;
}

/* World identity and list membership are checked BEFORE dereferencing any
 * remembered car/driver/control. An address range alone is not a lifetime. */
static int community_copchase_probe_car(uint32_t world, uint32_t car,
                                      uint32_t index, uint32_t record,
                                      uint32_t driver, uint32_t control)
{
    if (!world || !car || MEM32(0x4493e0) != world || !xyz_xbox_address(world,0x118) ||
        !xyz_xbox_address(car,0x9e0) ||
        xyz_xbox_find_car(cockpit_ui_read,NULL,world,car,0) != car) return 0;
    uint32_t count = MEM32(world+0x10), first = MEM32(world+0x18);
    if (count > 22 || first >= count || index < first || index >= count ||
        MEM32(world+0x1c+4*index) != car || (int16_t)MEM16(car+8) != (int)index ||
        MEM32(car+0x24) != 4 || MEM32(car+0x14) != record ||
        MEM32(car+0x2c) != driver || MEM32(car+0x30) != control) return 0;
    if (!xyz_xbox_address(record,0x940) || MEM32(record+4) != 3 ||
        !xyz_xbox_address(driver,0x7a8) || !xyz_xbox_address(control,0x460) ||
        MEM32(driver) != 0x341a70 || MEM32(control) != 0x3414e8 ||
        MEM32(driver+4) != car || MEM32(driver+8) != control || MEM32(control+0x220) != car)
        return 0;
    return 1;
}

static int community_copchase_probe_live(void)
{
    CommunityCopChaseProbe *p = &community_copchase_probe;
    return community_copchase_probe_car(p->world,p->car,p->index,p->record,p->driver,p->control);
}

/* Only four audited calls are needed. guest_call_full restores GPRs, x87,
 * SIMD, MMX, flags and TLS; cdecl ChoosePhysics's argument is caller-owned. */
static int community_copchase_probe_call(uint32_t address,uint32_t self,
                                       uint32_t argument,uint32_t *result)
{
    if (address != 0x37550 && address != 0x39b00 && address != 0x1e47d0 && address != 0x1e4a70)
        return 0;
    recomp_func_t function = recomp_lookup_manual(address);
    if (!function) function = recomp_lookup(address);
    if (!function) return 0;
    uint32_t value = guest_call_full(function,self,0,0x18ab7f,1,&argument);
    if (result) *result = value;
    return 1;
}

static int community_copchase_probe_goal(uint32_t kind,uint32_t target)
{
    CommunityCopChaseProbe *p = &community_copchase_probe;
    if (!community_copchase_probe_live()) return 0;
    uint32_t provider = MEM32(p->driver+0x6fc);
    return xyz_xbox_address(provider,0x1c) && MEM32(provider+0x14) == kind &&
           (!target || MEM32(provider+0x18) == target);
}

static int community_copchase_probe_target(void)
{
    CommunityCopChaseProbe *p = &community_copchase_probe;
    uint32_t player = MEM32(0x44940c);
    if (!xyz_xbox_address(player,0x24)) return 0;
    uint32_t car = MEM32(player+4);
    if (!car || car == p->car || xyz_xbox_find_car(cockpit_ui_read,NULL,p->world,car,0) != car ||
        (p->player_car && p->player_car != car)) return 0;
    float x=MEMF(car+0x60), y=MEMF(car+0x64), z=MEMF(car+0x68), speed=MEMF(car+0x430);
    float cx=MEMF(p->car+0x60), cy=MEMF(p->car+0x64), cz=MEMF(p->car+0x68);
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) || fabsf(x)>50000 || fabsf(y)>50000 ||
        !isfinite(cx) || !isfinite(cy) || !isfinite(cz) || !isfinite(speed) || fabsf(speed)>150 ||
        fabsf(z-cz)>15 || (x-cx)*(x-cx)+(y-cy)*(y-cy)>40000) return 0;
    p->player_car=car; p->target[0]=x; p->target[1]=y;
    p->speed=speed+3.f; if (p->speed<0) p->speed=0; if (p->speed>25) p->speed=25;
    return 1;
}

static void community_copchase_probe_stop(const char *reason)
{
    CommunityCopChaseProbe *p = &community_copchase_probe;
    p->pending_stop=1;
    if (!community_copchase_probe_live()) {
        fprintf(stderr,"[CopChase AI probe] abandoned reason=%s: world/car lifetime ended; no stale writes\n",reason);
        p->phase=3; return;
    }
    /* A live car can be released at this frame boundary. Never free an AI
     * behavior from its Update or angle-computation hook. */
    uint32_t natural=UINT32_MAX;
    int ok=community_copchase_probe_call(0x39b00,p->driver,7,NULL);
    if (!community_copchase_probe_live()) { p->phase=3; return; }
    MEM32(p->driver+0x734)=p->saved_734;
    MEM32(p->control+0x230)=p->saved_speed;
    MEM32(p->control+0x234)=p->saved_234;
    MEM32(p->control+0x238)=p->saved_238;
    MEM8(p->control+0x23c)=p->saved_enable;
    ok=community_copchase_probe_call(0x1e47d0,0,p->car,&natural) && ok;
    if (natural!=2 || !community_copchase_probe_live()) ok=0;
    else ok=community_copchase_probe_call(0x1e4a70,p->car,natural,NULL) && ok;
    if (!community_copchase_probe_live()) { p->phase=3; return; }
    ok=ok && MEM32(p->car+0x28)==2 && community_copchase_probe_goal(7,0);
    float dx=MEMF(p->car+0x60)-p->start[0],dy=MEMF(p->car+0x64)-p->start[1];
    fprintf(stderr,"[CopChase AI probe] released reason=%s t=%.3f restored=%d physics=%u goal7=%d driver_calls=%u heading_calls=%u moved2=%.3f\n",
        reason,p->elapsed,ok,MEM32(p->car+0x28),community_copchase_probe_goal(7,0),p->driver_calls,p->heading_calls,dx*dx+dy*dy);
    p->phase=ok ? 2 : 3; p->observed=0;
}

static void community_copchase_probe_frame(void)
{
    CommunityCopChaseProbe *p = &community_copchase_probe;
    if (!community_copchase_probe_enabled() || p->phase==3 || p->busy) return;
    p->busy=1;
    if (p->phase==1 && (!community_copchase_probe_context() || !community_copchase_probe_live() || p->pending_stop)) {
        community_copchase_probe_stop("context/lifetime/target changed"); goto out;
    }
    if (p->phase==2 && (!community_copchase_probe_context() || !community_copchase_probe_live())) {
        fprintf(stderr,"[CopChase AI probe] release observation ended with world/context\n"); p->phase=3; goto out;
    }
    if (!community_copchase_probe_context() || quest_cockpit_ui_paused(cockpit_ui_read,NULL) ||
        !community_copchase_probe_idle()) { p->stable=0; goto out; }
    float dt=MEMF(0x428a0c);
    if (!isfinite(dt) || dt<=0 || dt>.25f) goto out;
    if (p->phase==2) {
        p->observed+=dt;
        if (p->observed>=3.f) {
            fprintf(stderr,"[CopChase AI probe] release observed seconds=%.3f physics=%u goal7=%d active=%u\n",
                p->observed,MEM32(p->car+0x28),community_copchase_probe_goal(7,0),MEM8(p->car+0x550));
            p->phase=3;
        }
        goto out;
    }
    if (p->phase==1) {
        p->elapsed+=dt;
        if (!MEM8(p->car+0x550) || !community_copchase_probe_target() ||
            !community_copchase_probe_goal(5,p->player_car)) {
            community_copchase_probe_stop("inactive or invalid target"); goto out;
        }
        if (p->elapsed>=20.f) { community_copchase_probe_stop("20 simulation seconds"); goto out; }
        if ((unsigned)(p->elapsed/2.f)>p->logs) {
            p->logs=(unsigned)(p->elapsed/2.f);
            fprintf(stderr,"[CopChase AI probe] t=%.3f physics=%u goal5=%d target=(%.2f,%.2f) desired=%.2f actual=%.2f driver=%u heading=%u\n",
                p->elapsed,MEM32(p->car+0x28),community_copchase_probe_goal(5,p->player_car),
                p->target[0],p->target[1],p->speed,MEMF(p->car+0x430),p->driver_calls,p->heading_calls);
        }
        goto out;
    }
    p->waiting+=dt; p->stable+=dt;
    if (p->waiting>120.f) { fprintf(stderr,"[CopChase AI probe] no eligible nearby mixed8 indexed traffic in 120 simulation seconds\n"); p->phase=3; goto out; }
    if (p->stable<3.f) goto out;
    uint32_t world=MEM32(0x4493e0),player=MEM32(0x44940c);
    if (!xyz_xbox_address(world,0x118) || !xyz_xbox_address(player,0x24)) goto out;
    uint32_t player_car=MEM32(player+4),count=MEM32(world+0x10),first=MEM32(world+0x18);
    if (!player_car || count>22 || first>=count ||
        xyz_xbox_find_car(cockpit_ui_read,NULL,world,player_car,0)!=player_car) goto out;
    float px=MEMF(player_car+0x60),py=MEMF(player_car+0x64),pz=MEMF(player_car+0x68);
    if (!isfinite(px) || !isfinite(py) || !isfinite(pz)) goto out;
    float nearest=10000.f;
    for (uint32_t i=first;i<count;++i) {
        uint32_t car=MEM32(world+0x1c+4*i);
        if (!car || car==player_car || xyz_xbox_find_car(cockpit_ui_read,NULL,world,car,0)!=car) continue;
        uint32_t record=MEM32(car+0x14),driver=MEM32(car+0x2c),control=MEM32(car+0x30);
        char name[24];
        if (!community_copchase_probe_car(world,car,i,record,driver,control) || !MEM8(car+0x550) ||
            MEM8(car+0x998) || !community_copchase_probe_model(car,name)) continue;
        float x=MEMF(car+0x60),y=MEMF(car+0x64),z=MEMF(car+0x68),d2=(x-px)*(x-px)+(y-py)*(y-py);
        if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(d2) || fabsf(z-pz)>5 || d2<225 || d2>=nearest) continue;
        nearest=d2; p->world=world; p->car=car; p->index=i; p->record=record; p->driver=driver; p->control=control;
        memcpy(p->model,name,sizeof p->model);
    }
    if (!p->car) goto out;
    p->player_car=player_car;
    if (!community_copchase_probe_target()) { p->car=0; p->player_car=0; goto out; }
    p->saved_physics=MEM32(p->car+0x28); p->saved_734=MEM32(p->driver+0x734);
    p->saved_speed=MEM32(p->control+0x230); p->saved_234=MEM32(p->control+0x234);
    p->saved_238=MEM32(p->control+0x238); p->saved_enable=MEM8(p->control+0x23c);
    p->start[0]=MEMF(p->car+0x60); p->start[1]=MEMF(p->car+0x64);
    p->phase=1;
    if (!community_copchase_probe_call(0x37550,p->driver,p->player_car,NULL) ||
        !community_copchase_probe_goal(5,p->player_car) ||
        !community_copchase_probe_call(0x1e4a70,p->car,1,NULL) ||
        !community_copchase_probe_live() || MEM32(p->car+0x28)!=1) {
        community_copchase_probe_stop("activation failed"); goto out;
    }
    fprintf(stderr,"[CopChase AI probe] started model=%s car=%08x index=%u player=%08x old_physics=%u distance=%.2f native_goal5=1\n",
        p->model,p->car,p->index,p->player_car,p->saved_physics,sqrtf(nearest));
out:
    p->busy=0;
}

static int community_copchase_probe_override(uint32_t control)
{
    CommunityCopChaseProbe *p = &community_copchase_probe;
    if (!community_copchase_probe_enabled() || p->phase!=1 || p->pending_stop || p->busy || control!=p->control) return 0;
    if (!community_copchase_probe_context() || !community_copchase_probe_live() ||
        !MEM8(p->car+0x550) || !community_copchase_probe_target() ||
        !community_copchase_probe_goal(5,p->player_car)) { p->pending_stop=1; return 0; }
    if (quest_cockpit_ui_paused(cockpit_ui_read,NULL) || !community_copchase_probe_idle()) return 0;
    MEMF(control+0x224)=p->target[0]; MEMF(control+0x228)=p->target[1];
    MEMF(control+0x230)=p->speed; MEM8(control+0x23c)=1;
    MEM32(p->driver+0x734)=0;
    return 1;
}

/* Parent wrappers call these BEFORE their respective generated originals.
 * Update's native path helper overwrites XY; Heading reapplies it afterwards. */
static void community_copchase_probe_driver(uint32_t control)
{
    if (community_copchase_probe_override(control)) ++community_copchase_probe.driver_calls;
}
static void community_copchase_probe_heading(uint32_t control)
{
    if (community_copchase_probe_override(control)) ++community_copchase_probe.heading_calls;
}
