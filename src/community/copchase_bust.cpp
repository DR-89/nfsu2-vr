#include "copchase_bust.h"
#include <algorithm>
#include <new>

namespace {
constexpr uint64_t transition_timeout = 5000;
bool valid(const CopChaseBustContext &c) {
    const auto &i = c.identity;
    return i.valid && i.offline && i.career && i.profile && i.world && i.player &&
        c.world && c.car && c.car <= UINT32_MAX-8 && c.director && c.director <= UINT32_MAX-0x24 && c.controller < 4 && c.flow == 6;
}
bool same(const CopChaseBustContext &a,const CopChaseBustContext &b) {
    return valid(a) && valid(b) && a.identity.profile == b.identity.profile &&
        a.identity.world == b.identity.world && a.identity.player == b.identity.player &&
        a.world == b.world && a.car == b.car && a.director == b.director && a.controller == b.controller;
}
bool active(CopChaseBustPhase phase) {
    return phase >= COPCHASE_BUST_WAIT_SAFE && phase <= COPCHASE_BUST_WAIT_RESUME;
}
}
struct CopChaseBust {
    CopChaseBustHost host{};
    CopChaseBustContext owner{};
    CopChaseBustStatus status{};
    CopChaseFine payment{};
    uint32_t infractions = 0;
    uint64_t stage_time = 0,last_time = 0;
    bool clock_valid = false,latched = false,failure_pending = false;

    bool context(CopChaseBustContext &out) {
        return host.context(host.fine.user,&out) && valid(out);
    }
    void fail(CopChaseBustError error) {
        if (payment.armed && !payment.attempted) copchase_fine_abandon_unattempted(&payment);
        failure_pending = failure_pending || status.owned_pause;
        status.error = error; status.phase = COPCHASE_BUST_FAILED;
        status.pending = status.ready = status.owned_pause = 0;
        refresh_failure();
    }
    bool clock(uint64_t now) {
        if (clock_valid && now < last_time) { fail(COPCHASE_BUST_STALE); return false; }
        clock_valid = true; last_time = now; return true;
    }
    bool read(uint32_t address,uint32_t &value) { return host.fine.read32(host.fine.user,address,&value) != 0; }
    bool pause_gate(const CopChaseBustContext &c) {
        if (!c.can_pause || !c.hud || c.root_pause || c.modal_visible || c.director_state != 4) return false;
        uint32_t director=0,state=0,pending=0,disabled=0,controller=0,devices=0;
        return read(0x449438,director) && director == c.director && read(director,state) && state == 4 &&
            read(0x405368,pending) && !(pending & 255) && read(0x40245c,disabled) && !(disabled & 255) &&
            read(c.car+8,controller) && controller == c.controller &&
            read(0x427ff8,devices) && ((devices >> (c.controller*8)) & 255) != 255;
    }
    bool root(const CopChaseBustContext &c) {
        uint32_t director=0,state=0,previous=0;
        return same(owner,c) && c.root_pause && !c.hud && c.director_state == 6 &&
            read(0x449438,director) && director == c.director && read(director,state) && state == 6 &&
            read(director+0x24,previous) && previous == 4;
    }
    void refresh_failure() {
        status.failure_visible = status.failure_dismissable = 0;
        if(!failure_pending)return;
        CopChaseBustContext current{};
        if(!context(current))return; // No proof to draw over an unknown UI.
        if(!same(owner,current) || (current.hud && !current.root_pause && current.director_state==4)){
            failure_pending=false;return;
        }
        if(root(current))status.failure_visible=status.failure_dismissable=1;
    }
    void balance() {
        uint32_t value=0;
        status.balance_valid = read(0x405364,value) && value <= INT32_MAX;
        status.balance = status.balance_valid ? value : 0;
    }
    bool call(uint32_t fn,uint32_t self,unsigned argc,const uint32_t *args) {
        uint32_t ignored=0; return host.fine.call(host.fine.user,fn,self,argc,args,&ignored) != 0;
    }
    static int payment_identity(void *user,CopChaseFineIdentity *out) {
        auto &b = *static_cast<CopChaseBust*>(user); CopChaseBustContext context{};
        if (!b.context(context) || !b.root(context) || !b.host.fine.identity(b.host.fine.user,out)) return 0;
        const auto &id = b.owner.identity;
        return out->valid && out->offline && out->career && out->profile == id.profile &&
            out->world == id.world && out->player == id.player;
    }
    static int payment_read(void *user,uint32_t address,uint32_t *value) {
        auto &b = *static_cast<CopChaseBust*>(user); return b.host.fine.read32(b.host.fine.user,address,value);
    }
    static int payment_call(void *user,uint32_t fn,uint32_t self,unsigned argc,const uint32_t *args,uint32_t *eax) {
        auto &b = *static_cast<CopChaseBust*>(user); return b.host.fine.call(b.host.fine.user,fn,self,argc,args,eax);
    }
    CopChaseFineHost payment_host() { return {this,payment_identity,payment_read,payment_call}; }
};

extern "C" CopChaseBust *copchase_bust_create(const CopChaseBustHost *host) {
    if (!host || !host->context || !host->fine.identity || !host->fine.read32 || !host->fine.call) return nullptr;
    auto *b = new(std::nothrow) CopChaseBust;
    if (b) b->host = *host;
    return b;
}
extern "C" void copchase_bust_destroy(CopChaseBust *b) { delete b; }
extern "C" CopChaseBustError copchase_bust_arm(CopChaseBust *b,uint64_t high,uint64_t low,
    uint32_t infractions,uint64_t now) {
    if (!b || !(high|low) || infractions > 1000000) return COPCHASE_BUST_INVALID;
    if (b->status.token_high == high && b->status.token_low == low)
        return b->infractions == infractions ? COPCHASE_BUST_DUPLICATE : COPCHASE_BUST_INVALID;
    if ((b->status.token_high|b->status.token_low) && b->status.token_high != high) return COPCHASE_BUST_INVALID;
    if (low < b->status.token_low) return COPCHASE_BUST_DUPLICATE;
    if(b->status.phase==COPCHASE_BUST_FAILED)b->refresh_failure();
    if (active(b->status.phase) || b->latched || b->failure_pending) return COPCHASE_BUST_BUSY;
    CopChaseBustContext context{};
    if (!b->context(context) || !b->clock(now)) return COPCHASE_BUST_STALE;
    // Payment is armed only after explicit confirmation. A prior uncertain
    // mutation permanently blocks new arms; unattempted work cannot charge.
    if (b->payment.armed && !b->payment.paid && !b->payment.abandoned) return COPCHASE_BUST_BUSY;
    b->owner = context; b->infractions = infractions; b->stage_time = now;
    b->status = {}; b->status.token_high = high; b->status.token_low = low;
    b->status.fine = std::max(1u,infractions)*215;
    b->status.phase = COPCHASE_BUST_WAIT_SAFE; b->status.pending = 1;
    return COPCHASE_BUST_OK;
}
extern "C" void copchase_bust_step(CopChaseBust *b,uint64_t now) {
    if(!b)return;
    if(b->status.phase==COPCHASE_BUST_FAILED){if(b->clock(now))b->refresh_failure();return;}
    if (!active(b->status.phase) || !b->clock(now)) return;
    CopChaseBustContext current{};
    if (!b->context(current) || !same(b->owner,current)) { b->fail(COPCHASE_BUST_STALE); return; }
    const bool timeout = now - b->stage_time > transition_timeout;
    switch (b->status.phase) {
    case COPCHASE_BUST_WAIT_SAFE:
        if (timeout) { b->fail(COPCHASE_BUST_TIMEOUT); return; }
        // A pre-existing user pause or dialog is never taken over.
        if (!b->pause_gate(current)) return;
        b->status.phase = COPCHASE_BUST_WAIT_PAUSE; b->stage_time = now;
        { const uint32_t args[]{current.controller,0xbd,0};
          if (!b->call(0x1f2b50,current.car,3,args)) b->fail(COPCHASE_BUST_NATIVE_IO); }
        return;
    case COPCHASE_BUST_WAIT_PAUSE:
        if (timeout) { b->fail(COPCHASE_BUST_TIMEOUT); return; }
        if (b->root(current)) {
            b->status.phase = COPCHASE_BUST_READY; b->status.ready = b->status.owned_pause = 1;
            b->stage_time = now; b->balance();
        }
        return;
    case COPCHASE_BUST_READY:
        if (!b->root(current)) { b->fail(COPCHASE_BUST_CONTEXT_LOST); return; }
        b->balance(); return; // User confirmation has no inactivity timeout.
    case COPCHASE_BUST_PAID:
        if (!b->root(current)) { b->fail(COPCHASE_BUST_CONTEXT_LOST); return; }
        if (current.modal_visible) return;
        b->status.phase = COPCHASE_BUST_WAIT_RESUME; b->stage_time = now;
        { const uint32_t args[]{1};
          if (!b->call(0x1e45f0,current.director,1,args)) b->fail(COPCHASE_BUST_NATIVE_IO); }
        return;
    case COPCHASE_BUST_WAIT_RESUME:
        if (current.hud && !current.root_pause && current.director_state == 4) {
            b->status.phase = COPCHASE_BUST_DONE; b->status.pending = b->status.owned_pause = 0; return;
        }
        if (timeout) { b->fail(COPCHASE_BUST_TIMEOUT); return; }
        // While the ordinary event is queued, root pause may remain visible.
        // A nested dialog never triggers another resume or cash operation.
        if (!b->root(current)) b->fail(COPCHASE_BUST_CONTEXT_LOST);
        return;
    default: return;
    }
}
extern "C" CopChaseBustError copchase_bust_confirm(CopChaseBust *b,uint64_t high,uint64_t low,uint64_t now) {
    if (!b || b->status.token_high != high || b->status.token_low != low) return COPCHASE_BUST_INVALID;
    if (b->status.paid) return COPCHASE_BUST_DUPLICATE;
    if (b->latched) return COPCHASE_BUST_PAYMENT_UNCERTAIN;
    if (b->status.phase != COPCHASE_BUST_READY || !b->status.ready || !b->status.owned_pause) return COPCHASE_BUST_BUSY;
    if (!b->clock(now)) return COPCHASE_BUST_STALE;
    CopChaseBustContext current{};
    if (!b->context(current) || !b->root(current)) { b->fail(COPCHASE_BUST_CONTEXT_LOST); return b->status.error; }
    const auto payment_host = b->payment_host();
    auto result = copchase_fine_arm(&b->payment,&payment_host,high,low,b->infractions);
    if (result == COPCHASE_FINE_OK || result == COPCHASE_FINE_DUPLICATE)
        result = copchase_fine_confirm(&b->payment,&payment_host);
    b->status.payment_error = result;
    b->status.charged = b->payment.charged;
    if (result == COPCHASE_FINE_OK || (result == COPCHASE_FINE_DUPLICATE && b->payment.paid)) {
        b->status.paid = 1; b->status.ready = 0; b->status.error = COPCHASE_BUST_OK;
        b->status.balance = b->payment.after; b->status.balance_valid = 1;
        b->status.phase = COPCHASE_BUST_PAID; b->stage_time = now;
        return COPCHASE_BUST_OK;
    }
    if (result == COPCHASE_FINE_UNCERTAIN || b->payment.attempted) {
        b->latched = true; b->fail(COPCHASE_BUST_PAYMENT_UNCERTAIN); return b->status.error;
    }
    if (result == COPCHASE_FINE_STALE) { b->fail(COPCHASE_BUST_STALE); return b->status.error; }
    // Pre-mutation I/O failure can be retried by another explicit confirmation
    // while this same owned root pause remains; no automatic payment retries.
    b->status.error = COPCHASE_BUST_PAYMENT_IO; return b->status.error;
}
extern "C" void copchase_bust_snapshot(const CopChaseBust *b,CopChaseBustStatus *out) {
    if (out) *out = b ? b->status : CopChaseBustStatus{};
}
extern "C" CopChaseBustError copchase_bust_dismiss(CopChaseBust *b,uint64_t high,uint64_t low,uint64_t now) {
    if(!b || b->status.token_high!=high || b->status.token_low!=low)return COPCHASE_BUST_INVALID;
    if(b->status.phase!=COPCHASE_BUST_FAILED || !b->failure_pending)return COPCHASE_BUST_BUSY;
    if(!b->clock(now))return COPCHASE_BUST_STALE;
    b->refresh_failure();if(!b->status.failure_dismissable)return COPCHASE_BUST_CONTEXT_LOST;
    b->failure_pending=false;b->status.failure_visible=b->status.failure_dismissable=0;
    return COPCHASE_BUST_OK;
}
