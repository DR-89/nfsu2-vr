#include "xyz_client.h"
#include "xyz_profiles.h"
#include <algorithm>

namespace xyz {
Client::Client(Transport::Factory factory,Transport::Clock clock):factory_(std::move(factory)),clock_(std::move(clock)) {}
bool Client::connect(const ClientProfile &p) {
    Session check;
    if(!p.player_id || p.room.size()>63 || p.room.find('\0')!=std::string::npos ||
       p.connection.name!=online_name(p.connection.name) ||
       !check.configure(p.connection.name,p.connection.identity,p.connection.claim))return false;
    std::lock_guard<std::mutex> lock(mutex_);
    pending_=p;wanted_=true;requested_lan_=p.connection.lan;++request_;
    if(transport_)transport_->stop();
    sockets_.reset();
    status_={};status_.name=p.connection.name;status_.room=p.room;
    status_.transport.state=LinkState::connecting;return true;
}
void Client::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_={};wanted_=requested_lan_=false;++request_;status_={};
    // Only copied state/commands: Socket close still belongs to the worker.
    // Do not wait for a potentially blocked guest loader to see Disconnect.
    if(transport_)transport_->stop();
    sockets_.reset();
}
ClientStatus Client::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);auto result=status_;
    if(transport_ && wanted_ && applied_==request_)result.transport=transport_->snapshot();
    return result;
}
void Client::abort() {
    disconnect();roam_.unbind();frame_={};
}
void Client::pump_lan(uint64_t now) {
    if(!transport_ || !wanted_ || !requested_lan_ || applied_!=request_) {
        sockets_.link(0,0);return;
    }
    auto state=transport_->snapshot();
    auto bind=[&]{bool ready=state.lan_available && state.races_allowed;
        sockets_.link(ready?state.vip:0,ready?state.generation:0);};
    bind();lan_.advance(*transport_,now);
    // A concurrent worker failure or queue overflow must invalidate sockets
    // before the next guest operation, even if no game frame follows it.
    state=transport_->snapshot();bind();
}
bool Client::socket_call(const XyzXboxGuest &guest,XyzXnetOp op,uint32_t thread,
                         const uint32_t *args,unsigned count,uint64_t now,uint32_t &result) {
    std::lock_guard<std::mutex> lock(mutex_);
    pump_lan(now);
    bool handled=sockets_.dispatch(guest,op,thread,args,count,now,wanted_ && requested_lan_,result);
    if(handled)pump_lan(now); // publish writes without waiting for a render frame
    return handled;
}
const ClientFrame &Client::frame(const XyzXboxFrame &local,bool paused,int64_t now) {
    frame_={};if(now<0)return frame_;
    ClientProfile command;bool changed=false,wanted=false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(request_!=applied_){applied_=request_;changed=true;wanted=wanted_;command=std::move(pending_);}
    }
    if(changed) {
        roam_.unbind();previous_=0;
        if(wanted && roam_.configure(command.connection.name,command.room,command.player_id,receive_limit)) {
            // Thread creation can take time. Publish only after it is ready,
            // and recheck the command before allowing any socket creation.
            std::unique_ptr<Transport> fresh;
            if(!transport_)fresh=std::make_unique<Transport>(factory_,clock_);
            std::lock_guard<std::mutex> lock(mutex_);
            if(fresh)transport_=std::move(fresh);
            if(applied_!=request_ || !wanted_)return frame_;
            if(!transport_->start(command.connection)) {
                status_.transport.state=LinkState::stopped;status_.transport.notice="Cannot start network connection";
                return frame_;
            }
        }
    }
    if(!transport_)return frame_;
    {std::lock_guard<std::mutex> lock(mutex_);pump_lan(uint64_t(now)/1000);}
    auto state=transport_->snapshot();
    if(state.generation!=roam_.generation()) {
        roam_.unbind();
        if(state.generation && state.state==LinkState::connected)roam_.bind(state.vip,state.generation);
    }
    if(roam_.generation()) {
        ReceivedDatagram in;
        // Drain at most the queue's full bounded capacity. With 64 peers,
        // a 100 ms frame contains 128 SNAPs plus control/config traffic.
        for(unsigned i=0;i<Transport::roam_queue_limit && transport_->take(in);++i)
            roam_.receive(in.bytes.data(),in.bytes.size(),in.generation,now);
        roam_.frame(local,paused,now);
        std::vector<uint8_t> out;
        for(unsigned i=0;i<Transport::roam_queue_limit && roam_.take(out);++i)transport_->send(roam_.generation(),out.data(),out.size());
        frame_.generation=roam_.generation();
        // A race/garage/loading screen publishes its status but cannot create
        // free-roam cars. Keep the connection so returning needs no handshake.
        if(local.status==XYZ_XBOX_READY) {
            for(int i=0;i<RC_TABLE;++i) {
                const auto *p=roam_.peer(i);if(!p)continue;
                // A direction change is not a disconnect. Preserve an old
                // receive peer's identity/config while its last pose expires,
                // so a brief reassignment can resume the same owned car.
                if((p->hub_way&RC_MARK_TXONLY) && (!p->cfg_len || !p->mo.n))continue;
                RemotePlayer remote;remote.id=p->id;remote.ip=p->ip;
                if(p->dents_n<=RC_DMG_MAX){
                    remote.dents_epoch=p->dents_epoch;remote.dents_first=p->dents_first;
                    remote.dents.assign(p->dent,p->dent+size_t(p->dents_n)*RC_DMG_BYTES);
                }
                remote.listed=roam_.listed_peer(i)!=nullptr;
                remote.status=p->status;remote.lost=p->lost!=0;
                remote.send_only=(p->hub_way&RC_MARK_TXONLY)!=0;
                remote.snap_age_ms=now>=p->last_snap_rx?uint32_t(std::min<int64_t>((now-p->last_snap_rx)/1000,UINT32_MAX)):0;
                if(p->cfg_len>0 && p->cfg_len<=RC_CFG_MAX)remote.config.assign(p->cfg,p->cfg+p->cfg_len);
                remote.has_pose=roam_.pose(i,now,previous_?std::clamp<int64_t>(now-previous_,0,100000):0,remote.pose);
                frame_.players.push_back(std::move(remote));
            }
        }
    }
    previous_=now;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(applied_==request_){status_.transport=std::move(state);status_.peers=roam_.peers();status_.roam=roam_.diagnostics(now);status_.local=local.status;}
        else frame_={}; // a newer render command must not expose old peers
    }
    return frame_;
}
}
