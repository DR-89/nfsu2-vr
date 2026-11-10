#include "xyz_fleet.h"
#include "xyz_damage.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace xyz {
void Fleet::damage(const ClientFrame &remote) const {
    if(!xyz_damage_enabled())return;
    for(size_t k=0;k<slots_.size();++k){
        const auto &s=slots_[k];const RemotePlayer *p=nullptr;
        if(!s.retiring && s.clone.visible)for(const auto &v:remote.players)if(v.id==s.id && v.ip==s.ip){p=&v;break;}
        if(p && p->dents.size()<=RC_DMG_MAX*RC_DMG_BYTES && p->dents.size()%RC_DMG_BYTES==0)
            dmg_net_car(int(k),s.clone.car,s.id,p->dents_epoch,p->dents_first,int(p->dents.size()/RC_DMG_BYTES),p->dents.data());
        else dmg_net_car(int(k),0,0,0,0,0,nullptr);
    }
}
bool Fleet::retire(const XyzXboxGuest &g,Slot &s) {
    s.retiring=true;
    if(!xyz_xbox_clone_retire(&g,&s.clone))return false;
    auto arena=s.arena;auto retry=s.retry;s={};s.arena=arena;s.retry=retry;return true;
}
FleetStatus Fleet::frame(const XyzXboxGuest &g,const XyzXboxFrame &local,const ClientFrame &remote,uint64_t now) {
    struct DamageAtEnd {const Fleet &fleet;const ClientFrame &remote;~DamageAtEnd(){fleet.damage(remote);}} damage_at_end{*this,remote};
    FleetStatus status;
    assets_.maintain(g);
    const bool ready=local.status==XYZ_XBOX_READY && remote.generation && local.world && local.car;
    bool reset=!ready || generation_!=remote.generation || world_!=local.world;
    generation_=ready?remote.generation:0;world_=ready?local.world:0;
    float dt=previous_ && now>=previous_?float(std::min<uint64_t>(now-previous_,100000))*.000001f:0;
    previous_=now;
    auto find=[&](const Slot &s)->const RemotePlayer* {
        if(!ready || !s.id)return nullptr;
        for(const auto &p:remote.players)if(p.id==s.id && p.ip==s.ip)return &p;
        return nullptr;
    };
    bool blocked=false,creating=false;
    for(auto &s:slots_) {
        bool excess=size_t(&s-slots_.data())>=limit_;
        if(reset || excess || s.retiring || (s.id && !find(s))) {
            if(s.id && !s.retiring)std::fprintf(stderr,"[XYZ car] slot=%zu retire=%s\n",size_t(&s-slots_.data()),reset?"world/session":excess?"setting":"peer-left");
            if(!retire(g,s)){++status.failed;blocked=true;}
        }
    }
    // A failed retirement retains ownership and its arena. Do not introduce
    // any more cars while the old world/loader cannot be verified.
    if(!ready || blocked)return status;
    XyzXboxStreaming stream{};
    const bool may_start=xyz_xbox_streaming(g.read,g.context,&stream) && !stream.phase && !stream.car_busy;
    // CarLoader is shared with the player's car. An equipment load consumes
    // the same pool as the initial constructor load; neither may overlap a
    // second load. Keep one quiet frame after completion before starting it.
    creating=std::any_of(slots_.begin(),slots_.end(),[](const Slot &s){
        return s.clone.stage==XYZ_CLONE_LOADING || s.clone.stage==XYZ_CLONE_LOOK_LOADING;
    });
    for(auto &s:slots_) {
        auto p=find(s);if(!p)continue;
        auto before=s.clone.stage;
        auto stage=xyz_xbox_clone_poll(&g,&s.clone,now);
        if(stage!=before)std::fprintf(stderr,"[XYZ car] slot=%zu stage=%d->%d type=%u elapsed_ms=%llu\n",
            size_t(&s-slots_.data()),int(before),int(stage),s.look.type,
            (unsigned long long)(now>=s.clone.started?(now-s.clone.started)/1000:0));
        if(stage==XYZ_CLONE_FAILED) {
            ++status.failed;s.retry=now+5000000;
            if(!retire(g,s))blocked=true;
            continue;
        }
        if(before==XYZ_CLONE_LOOK_LOADING && stage==XYZ_CLONE_READY) {
            s.applied=std::move(s.pending);s.pending.clear();
            rc_carcfg look{};s.has_stance=rc_carcfg_decode(s.applied.data(),int(s.applied.size()),&look)==RC_OK && look.has_stance;
            if(s.has_stance){s.stance={(int16_t)look.stance_fc,(int16_t)look.stance_rc,look.stance_fo,look.stance_ro,(int16_t)look.stance_tw};xyz_stance_clamp(&s.stance);}
        }
        if(stage==XYZ_CLONE_READY && !p->config.empty() && p->config!=s.applied) {
            // This is the same peer. Keep its current model moving while the
            // replacement loads; never hide it merely for an equipment update.
            // A shop visit can put shared models back into HIBERNATING after
            // the first car was created. Wait for their ownership repair on
            // equipment changes too, before asking the loader for new parts.
            int resources=creating || !may_start?0:assets_.prepare(g,now);
            if(resources<=0){if(resources<0)++status.failed;else ++status.loading;}
            else if(xyz_xbox_clone_configure(&g,&s.clone,p->config.data(),int(p->config.size()),now,&s.look)) {
                s.pending=p->config;stage=s.clone.stage;creating=true;
                std::fprintf(stderr,"[XYZ car] slot=%zu load type=%u fallback=%d missing=%u\n",
                    size_t(&s-slots_.data()),s.look.type,s.look.fallback_type,s.look.missing_parts);
            } else {
                ++status.failed;s.retry=now+5000000;if(!retire(g,s))blocked=true;continue;
            }
        }
        if((stage==XYZ_CLONE_READY || stage==XYZ_CLONE_LOOK_LOADING) && !s.applied.empty() && p->has_pose) {
            if(xyz_xbox_clone_pose(&g,&s.clone,&p->pose,dt)){
                s.lights=p->pose.hibeam?2:(p->pose.bits&RC_BIT_DARK)?1:0;
                ++status.visible;if(!s.shown)s.shown=now;
            }
            else {s.retiring=true;++status.failed;}
        } else {
            if(s.clone.visible){
                std::fprintf(stderr,"[XYZ car] slot=%zu hide pose=%d status=%u age_ms=%u listed=%d send_only=%d lost=%d stage=%d\n",
                    size_t(&s-slots_.data()),int(p->has_pose),unsigned(p->status),p->snap_age_ms,
                    int(p->listed),int(p->send_only),int(p->lost),int(stage));
                if(!xyz_xbox_clone_hide(&g,&s.clone)){s.retiring=true;++status.failed;}
            }
        }
        if(stage==XYZ_CLONE_LOADING || stage==XYZ_CLONE_LOOK_LOADING)++status.loading;
        status.fallbacks+=s.look.fallback_type!=0;status.missing_parts+=s.look.missing_parts;
    }
    if(blocked || creating)return status;
    // At most one constructor load per frame and only after the previous car
    // exists in the world list. Pending cars cannot accidentally share ID 103.
    // Load approaching peers first. Keep assigned cars unless a candidate is
    // substantially nearer, so small rank changes cannot swap them each list.
    auto rank=[&](const RemotePlayer &p){
        if(!p.has_pose)return 1e9f;
        float dx=p.pose.p[0]-local.state.p[0],dy=p.pose.p[1]-local.state.p[1];
        float vx=p.pose.v[0]-local.state.v[0],vy=p.pose.v[1]-local.state.v[1];
        float best=dx*dx+dy*dy;
        for(float t:{1.f,4.f}){float x=dx+vx*t,y=dy+vy*t;best=std::min(best,x*x+y*y);}
        return std::isfinite(best)?std::sqrt(best):1e9f;
    };
    std::vector<const RemotePlayer*> candidates;
    for(const auto &p:remote.players) {
        if(!p.id || !p.listed || !p.has_pose || p.config.empty() || p.config.size()>RC_CFG_MAX)continue;
        bool exists=false;for(const auto &s:slots_)if(s.id==p.id && s.ip==p.ip)exists=true;
        if(exists)continue;
        candidates.push_back(&p);
    }
    std::stable_sort(candidates.begin(),candidates.end(),[&](auto a,auto b){return rank(*a)<rank(*b);});
    const auto end=slots_.begin()+limit_;
    for(const auto *candidate:candidates) {
        const auto &p=*candidate;
        auto it=std::find_if(slots_.begin(),end,[&](const Slot &s){return !s.id && !s.retiring && now>=s.retry;});
        if(it==end) {
            float far=0;auto victim=end;
            for(auto s=slots_.begin();s!=end;++s) {
                auto peer=find(*s);if(!peer || s->retiring || s->clone.stage!=XYZ_CLONE_READY)continue;
                // A better predicted encounter must not remove a car still
                // alongside the player. Hold visible nearby cars, and give a
                // newly shown car at least five seconds before rank eviction.
                // Real departures and invalid poses still follow normal cleanup.
                if(s->clone.visible && peer->has_pose){
                    float dx=peer->pose.p[0]-local.state.p[0],dy=peer->pose.p[1]-local.state.p[1];
                    if(dx*dx+dy*dy<=250.f*250.f || now<s->shown || now-s->shown<5000000)continue;
                }
                float distance=rank(*peer);
                if(distance>far){far=distance;victim=s;}
            }
            float next=rank(p);
            if(victim==end || !(next<far*.6f && far-next>50.f))break;
            if(!may_start || assets_.prepare(g,now)<=0)break;
            bool was_visible=victim->clone.visible!=0;
            if(!retire(g,*victim)){++status.failed;break;}
            if(was_visible && status.visible)--status.visible;
            std::fprintf(stderr,"[XYZ car] slot=%zu retire=nearer-peer rank=%.1f next=%.1f\n",size_t(&*victim-slots_.data()),far,next);
            it=victim;
        }
        if(!may_start){++status.loading;break;}
        auto &s=*it;
        int resources=assets_.prepare(g,now);
        if(resources<=0){if(resources<0)++status.failed;else ++status.loading;break;}
        const uint32_t skin=Skins::first+uint32_t(&s-slots_.data());
        int paint=skins_.prepare(g,skin);
        if(paint<=0){if(paint<0){++status.failed;s.retry=now+5000000;}else ++status.loading;break;}
        if(!s.arena.base) {
            uint32_t bytes=0x3000,address=0;
            if(!g.call || !g.call(g.context,XYZ_G_ALLOC,0,0,1,&bytes,&address) || !xyz_xbox_address(address,bytes)) {
                ++status.failed;s.retry=now+5000000;break;
            }
            bool overlap=false;
            for(const auto &other:slots_)if(other.arena.base && uint64_t(address)<uint64_t(other.arena.base)+other.arena.size &&
                uint64_t(other.arena.base)<uint64_t(address)+bytes)overlap=true;
            if(overlap){++status.failed;s.retry=now+5000000;break;}
            s.arena={address,bytes};
        }
        bool started=xyz_xbox_clone_begin_at(&g,&s.clone,s.arena,local.world,local.car,skin,p.pose.p,now)!=0;
        if(!started && !retire(g,s))blocked=true;
        if(started){s.id=p.id;s.ip=p.ip;++status.loading;}
        else {++status.failed;s.retry=now+5000000;}
        break;
    }
    return status;
}
}
