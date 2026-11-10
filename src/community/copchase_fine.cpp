#include "copchase_fine.h"
#include <algorithm>
namespace {
bool valid(const CopChaseFineIdentity &i){return i.valid && i.offline && i.career && i.profile && i.world && i.player;}
bool same(const CopChaseFineIdentity &a,const CopChaseFineIdentity &b){return valid(a) && valid(b) &&
    a.profile==b.profile && a.world==b.world && a.player==b.player;}
bool host(const CopChaseFineHost *h){return h && h->identity && h->read32 && h->call;}
}
extern "C" CopChaseFineResult copchase_fine_arm(CopChaseFine *f,const CopChaseFineHost *h,
    uint64_t high,uint64_t low,uint32_t infractions){
    if(!f || !host(h) || !(high|low) || infractions>1000000)return COPCHASE_FINE_INVALID;
    const uint32_t fine=std::max(1u,infractions)*215u;
    if(f->armed && f->token_high==high && f->token_low==low)
        return f->fine==fine?COPCHASE_FINE_DUPLICATE:COPCHASE_FINE_INVALID;
    if(f->armed && f->token_high!=high)return COPCHASE_FINE_INVALID;
    if(f->armed && low<f->token_low)return COPCHASE_FINE_DUPLICATE;
    if(f->armed && !f->paid && !f->abandoned)return COPCHASE_FINE_INVALID;
    CopChaseFineIdentity i{};if(!h->identity(h->user,&i) || !valid(i))return COPCHASE_FINE_STALE;
    *f={};f->armed=1;f->identity=i;f->token_high=high;f->token_low=low;f->fine=fine;return COPCHASE_FINE_OK;
}
extern "C" CopChaseFineResult copchase_fine_confirm(CopChaseFine *f,const CopChaseFineHost *h){
    if(!f || !f->armed || !host(h))return COPCHASE_FINE_INVALID;
    if(f->abandoned)return COPCHASE_FINE_STALE;
    if(f->attempted)return f->paid?COPCHASE_FINE_DUPLICATE:COPCHASE_FINE_UNCERTAIN;
    CopChaseFineIdentity identity{};if(!h->identity(h->user,&identity) || !same(identity,f->identity))return COPCHASE_FINE_STALE;
    uint32_t raw=0,balance=0;if(!h->read32(h->user,0x405364,&raw) || raw>INT32_MAX)return COPCHASE_FINE_IO;
    if(!h->call(h->user,0x111c50,0x405364,0,nullptr,&balance) || balance!=raw)return COPCHASE_FINE_IO;
    // Revalidate immediately before the only mutation; no stale captured cash.
    uint32_t current=0;if(!h->identity(h->user,&identity) || !same(identity,f->identity))return COPCHASE_FINE_STALE;
    if(!h->read32(h->user,0x405364,&current) || current!=balance)return COPCHASE_FINE_IO;
    f->before=balance;f->charged=std::min(balance,f->fine);f->after=balance-f->charged;
    f->attempted=1; // Latch BEFORE entering guest code: failure may be post-write.
    if(f->charged){uint32_t ignored=0,delta=0u-f->charged;
        if(!h->call(h->user,0x111c40,0x405364,1,&delta,&ignored))return COPCHASE_FINE_UNCERTAIN;}
    if(!h->read32(h->user,0x405364,&current) || current!=f->after)return COPCHASE_FINE_UNCERTAIN;
    if(!h->identity(h->user,&identity) || !same(identity,f->identity))return COPCHASE_FINE_UNCERTAIN;
    f->paid=1;return COPCHASE_FINE_OK;
}
extern "C" CopChaseFineResult copchase_fine_abandon_unattempted(CopChaseFine *f){
    if(!f || !f->armed)return COPCHASE_FINE_INVALID;
    if(f->paid || f->abandoned)return COPCHASE_FINE_DUPLICATE;
    if(f->attempted)return COPCHASE_FINE_UNCERTAIN;
    f->abandoned=1;return COPCHASE_FINE_OK;
}
