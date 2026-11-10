#include "xyz_roam_link.h"
#include "xyz_profiles.h"
#include "xyz_damage.h"
#include <algorithm>
#include <cstring>

namespace xyz {
RoamLink::RoamLink():core_(std::make_unique<rc_session>()) {}
bool RoamLink::configure(const std::string &name,const std::string &room,uint32_t id,int cars) {
    if(!id || name.empty() || name!=online_name(name) || room.size()>63 || room.find('\0')!=std::string::npos || cars<1 || cars>RC_MAX_PEERS) return false;
    unbind(); config_={};
    config_.self_id=id; config_.port=roam_port; config_.room=rc_room_hash(room.c_str());
    std::memcpy(config_.name,name.c_str(),name.size()+1);
    // No platform code is assigned to this native Xbox port by protocol 17.
    // Zero is decoded as unknown. Never pretend Quest is the PC ASI client.
    config_.platform=0; config_.max_players=cars+1; config_.links=cars;
    config_.rate_hz=20; config_.snap_states=1; config_.hub_ip=hub_ip; config_.hub_port=roam_port;
    config_.dents=xyz_damage_enabled();
    // The raw WebSocket tunnel needs no LAN broadcast discovery. Dents are
    // advertised only after the native renderer has initialized successfully.
    configured_=true; return true;
}
bool RoamLink::bind(uint32_t ip,uint32_t generation) {
    if(!configured_ || !generation || !service_ip(ip) || ip==hub_ip) return false;
    unbind(); vip_=ip; generation_=generation;
    rc_io io{}; io.user=this; io.send=send;
    rc_session_init(core_.get(),&config_,&io); return true;
}
void RoamLink::unbind() {
    vip_=generation_=last_world_=last_car_=0; have_car_=false;
    outgoing_.clear(); std::memset(core_.get(),0,sizeof *core_);
}
void RoamLink::leave() {
    if(vip_) rc_shutdown(core_.get(),0);
    vip_=0; have_car_=false;
    // Keep the bounded BYEs for the transport to drain before closing.
}
void RoamLink::send(void *user,uint32_t ip,uint16_t port,const uint8_t *data,int size) {
    auto &self=*static_cast<RoamLink *>(user);
    if(!self.vip_ || !service_ip(ip) || ip==self.vip_ || !port || size<=0 || size>RC_MAX_DGRAM) return;
    if(self.outgoing_.size()>=512) { ++self.dropped_; return; }
    Datagram d; d.source=self.vip_; d.destination=ip; d.destination_port=port; d.payload.assign(data,data+size);
    auto wire=encode_datagram(d); if(!wire.empty()) self.outgoing_.push_back(std::move(wire));
}
void RoamLink::frame(const XyzXboxFrame &local,bool paused,int64_t now) {
    if(!vip_ || now<0) return;
    bool ready=local.status==XYZ_XBOX_READY && local.config_size>0 && local.config_size<=RC_CFG_MAX;
    if(ready) {
        // A bad/partial look must not enter the core's fragment sender.
        if(core_->cfg_len!=local.config_size || std::memcmp(core_->cfg,local.config,local.config_size)) {
            rc_carcfg decoded{};
            if(rc_carcfg_decode(local.config,local.config_size,&decoded)!=RC_OK) ready=false;
            else rc_set_cfg(core_.get(),local.config,local.config_size,now);
        }
    }
    uint8_t status=ready?(paused?RC_ST_PAUSED:RC_ST_ROAM):
        local.status==XYZ_XBOX_FRONTEND?RC_ST_MENUS:local.status==XYZ_XBOX_OTHER_RACE?RC_ST_EVENT:RC_ST_LOADING;
    rc_set_status(core_.get(),status);
    if(ready) {
        if(!have_car_ || local.world!=last_world_ || local.car!=last_car_) rc_bump_epoch(core_.get());
        last_world_=local.world; last_car_=local.car;
        rc_state state=local.state;
        state.t_us=static_cast<uint32_t>(now);
        if(paused) {
            std::fill_n(state.v,3,0.f); std::fill_n(state.w,3,0.f);
            state.speed=state.rpm=state.throttle=state.brake=0;state.nos=0;
            std::fill_n(state.slip,8,0.f);
        }
        rc_push_state(core_.get(),&state,now);
        if(config_.dents && xyz_damage_enabled()){
            uint8_t epoch=0,dents[RC_DMG_MAX*RC_DMG_BYTES];uint16_t first=0;
            int count=dmg_net_mine(&epoch,&first,dents,RC_DMG_MAX);
            if(count>=0 && count<=RC_DMG_MAX)rc_set_dents(core_.get(),epoch,first,count,dents,now);
        }
    }
    have_car_=ready; rc_tick(core_.get(),now);
}
bool RoamLink::receive(const uint8_t *data,size_t size,uint32_t generation,int64_t now) {
    if(!vip_ || generation!=generation_ || now<0) return false;
    Datagram d;
    if(!decode_datagram(data,size,d) || d.kind!=1 || d.destination!=vip_ || d.source==vip_ ||
       d.destination_port!=roam_port) return false;
    rc_receive(core_.get(),d.source,d.source_port,d.payload.data(),static_cast<int>(d.payload.size()),now);
    return true;
}
bool RoamLink::take(std::vector<uint8_t> &data) {
    if(outgoing_.empty()) return false;
    data=std::move(outgoing_.front()); outgoing_.pop_front(); return true;
}
int RoamLink::peers() const { return vip_?rc_established(core_.get()):0; }
RoamDiagnostics RoamLink::diagnostics(int64_t now) const {
    RoamDiagnostics d;if(!vip_ || now<0)return d;
    const auto &s=*core_;
    d.hub_lists=s.hub_lists;d.rx=s.rx;d.bad=s.rx_bad;d.policy=s.rx_policy;
    d.version=s.rx_version;d.room=s.rx_room;d.unlisted=s.rx_unlisted;
    // 0: waiting for the first list; 1: a recent answer; 2: silent.
    auto heard=s.hub_heard_us?s.hub_heard_us:s.hub_since;
    d.hub=heard && now>=heard && now-heard>=10000000?2:s.hub_heard_us?1:0;
    for(int i=0;i<RC_TABLE;++i) {
        const auto &p=s.peer[i];if(p.state==RC_PS_FREE)continue;
        if(p.state==RC_PS_CANDIDATE)++d.candidates;
        if(!s.hub_heard_us || p.hub_listed_us!=s.hub_heard_us || (p.hub_way&RC_MARK_TXONLY))continue;
        ++d.listed;
        if(p.state==RC_PS_EST && p.cfg_len)++d.configured;
        if(p.state==RC_PS_EST && p.mo.n && now>=p.last_snap_rx && now-p.last_snap_rx<=2000000)++d.moving;
    }
    return d;
}
const rc_peer *RoamLink::peer(int index) const {
    return vip_ && index>=0 && index<RC_TABLE && core_->peer[index].state==RC_PS_EST?&core_->peer[index]:nullptr;
}
const rc_peer *RoamLink::listed_peer(int index) const {
    auto p=peer(index);
    return p && rc_peer_listed(core_.get(),index) && !(p->hub_way&RC_MARK_TXONLY)?p:nullptr;
}
bool RoamLink::pose(int index,int64_t now,int64_t dt,rc_pose &out) {
    auto p=peer(index);
    // The core retains a formerly listed peer for its bounded hub grace.
    // Existing cars may still use that peer; only new assignments require a
    // current listing. Paused browser tabs still announce/ping but stop SNAPs.
    if(!p || p->lost || (p->status!=RC_ST_ROAM && p->status!=RC_ST_PAUSED) ||
       !p->cfg_len || !p->mo.n || now<p->last_snap_rx ||
       ((p->status!=RC_ST_PAUSED || (p->hub_way&RC_MARK_TXONLY)) && now-p->last_snap_rx>2000000)) return false;
    if(!rc_interp_eval(&core_->peer[index].mo,now,std::clamp<int64_t>(dt,0,100000),100000,250000,&out))return false;
    // Match the browser client's at_spawn rule: new careers share this exact
    // airport start. Loading/idle players stacked there are not visible cars.
    // Keep their connection/config so driving out resumes the normal pose.
    const float sx=out.p[0]-1695.3f,sy=out.p[1]+883.6f,sz=out.p[2]-24.1f;
    if(sx*sx+sy*sy+sz*sz<25.f)return false;
    // Keep a paused peer's pose while disabling wheel/effect motion even
    // during the interpolation delay or when its last packet held throttle.
    if(p->status==RC_ST_PAUSED) {
        std::fill_n(out.v,3,0.f);std::fill_n(out.w,3,0.f);std::fill_n(out.slip,8,0.f);
        out.speed=out.rpm=out.throttle=out.brake=0;out.nos=0;
        out.eng=out.eng_acc=out.eng_dcl=out.eng_base=0;
    }
    return true;
}
}
