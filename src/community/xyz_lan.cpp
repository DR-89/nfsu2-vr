#include "xyz_lan.h"
#include "xyz_transport.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <map>

namespace xyz {
static bool valid_lan(const LanPacket &p) {
    if(p.data.size()>lan_data_max)return false;
    if(p.kind==3)return p.connection==0 && (p.op==0 || (p.op==1 && p.data.empty()));
    if(p.kind!=4 || !p.connection || p.op<1 || p.op>5)return false;
    return p.op==4 ? !p.data.empty() : p.data.empty();
}
bool decode_lan(const Datagram &d,LanPacket &out) {
    const auto &b=d.payload;
    if(b.size()<lan_header_size || b.size()>lan_header_size+lan_data_max || b[2] || b[3])return false;
    LanPacket p;p.kind=d.kind;p.op=b[0];p.ttl=b[1];
    p.connection=uint32_t(b[4]) | uint32_t(b[5])<<8 | uint32_t(b[6])<<16 | uint32_t(b[7])<<24;
    p.data.assign(b.begin()+lan_header_size,b.end());
    if(!valid_lan(p))return false;
    out=std::move(p);return true;
}
bool encode_lan(const LanPacket &p,Datagram &d) {
    if(!valid_lan(p))return false;
    d.kind=p.kind;d.payload.assign(lan_header_size+p.data.size(),0);
    d.payload[0]=p.op;d.payload[1]=p.ttl;
    for(unsigned i=0;i<4;++i)d.payload[4+i]=uint8_t(p.connection>>(8*i));
    std::copy(p.data.begin(),p.data.end(),d.payload.begin()+lan_header_size);return true;
}
struct LanEndpoint::Impl {
    static constexpr size_t sockets_max=64,tx_data_max=256,tx_max=320,rx_stream_max=1024*1024,rx_total_max=8*1024*1024,rx_udp_max=256;
    enum Type { udp_socket,listener,stream };
    struct Udp {uint32_t peer;uint16_t port;std::vector<uint8_t> data;};
    struct Socket {
        Type type=stream;LanError error=LanError::ok;
        uint16_t port=0,remote_port=0;uint32_t peer=0,cid=0;
        uint64_t started=0;unsigned backlog=0;LanHandle parent=0;
        bool read_fin=false,write_fin=false;
        std::deque<uint8_t> received;
        std::deque<Udp> datagrams;
        std::deque<LanHandle> accepted;
    };
    uint32_t vip=0,generation=0,cid,next_handle=0;
    uint16_t next_port=49152;
    size_t rx_bytes=0;
    bool fatal=false;
    std::map<LanHandle,Socket> sockets;
    std::deque<Datagram> tx;
    explicit Impl(uint32_t seed):cid(seed?seed:1){}
    Socket *get(LanHandle h){auto p=sockets.find(h);return p==sockets.end()?nullptr:&p->second;}
    bool used(uint16_t port,bool tcp)const {
        for(const auto &s:sockets)if(s.second.port==port && (s.second.type!=udp_socket)==tcp)return true;
        return false;
    }
    uint16_t bind(uint16_t p,bool tcp) {
        if(p)return used(p,tcp)?0:p;
        for(unsigned i=0;i<16383;++i){auto candidate=next_port;if(++next_port==65535)next_port=49152;if(!used(candidate,tcp))return candidate;}
        return 0;
    }
    LanHandle add(Socket s) {
        if(sockets.size()>=sockets_max || next_handle==UINT32_MAX)return 0;
        auto h=++next_handle;sockets.emplace(h,std::move(s));return h;
    }
    void clear_rx(Socket &s) {
        rx_bytes-=s.received.size();s.received.clear();
        for(const auto &d:s.datagrams)rx_bytes-=d.data.size();s.datagrams.clear();
    }
    void fail(Socket &s,LanError error) {clear_rx(s);s.error=error;s.read_fin=s.write_fin=true;}
    void break_link() {
        fatal=true;tx.clear();
        for(auto &entry:sockets)if(entry.second.type==stream)fail(entry.second,LanError::reset);
    }
    void reap() {
        for(auto it=sockets.begin();it!=sockets.end();) {
            auto &s=it->second;
            if(s.parent && s.error!=LanError::ok) {
                if(auto *parent=get(s.parent)){auto &a=parent->accepted;a.erase(std::remove(a.begin(),a.end(),it->first),a.end());}
                clear_rx(s);it=sockets.erase(it);
            }else ++it;
        }
    }
    bool emit(uint8_t kind,uint8_t op,uint32_t peer,uint16_t local,uint16_t remote,uint32_t conn,const uint8_t *data=nullptr,size_t n=0) {
        const bool control=kind==4 && op!=4;
        if(!vip || fatal || tx.size()>=(control?tx_max:tx_data_max))return false;
        Datagram d;d.source=vip;d.destination=peer;d.source_port=local;d.destination_port=remote;
        LanPacket p;p.kind=kind;p.op=op;p.connection=conn;if(n)p.data.assign(data,data+n);
        if(!encode_lan(p,d))return false;
        tx.push_back(std::move(d));return true;
    }
    bool control(Socket &s,uint8_t op){return emit(4,op,s.peer,s.port,s.remote_port,s.cid);}
    void discard_tx(const Socket &s) {
        tx.erase(std::remove_if(tx.begin(),tx.end(),[&](const Datagram &d){
            LanPacket p;return d.destination==s.peer && d.source_port==s.port && d.destination_port==s.remote_port && decode_lan(d,p) && p.connection==s.cid;
        }),tx.end());
    }
};
LanEndpoint::LanEndpoint(uint32_t seed):impl_(std::make_unique<Impl>(seed)){}
LanEndpoint::~LanEndpoint()=default;
void LanEndpoint::link(uint32_t vip,uint32_t generation,bool allowed) {
    auto &p=*impl_;if(!allowed || !generation || !service_ip(vip) || vip==hub_ip)vip=generation=0;
    if(p.vip==vip && p.generation==generation)return;
    p.tx.clear();p.vip=vip;p.generation=generation;p.fatal=false;
    for(auto &entry:p.sockets){auto &s=entry.second;if(s.type==Impl::stream)p.fail(s,LanError::reset);else p.clear_rx(s);}
    // Unaccepted connections belong to their listener and cannot leak slots.
    for(auto it=p.sockets.begin();it!=p.sockets.end();)if(it->second.parent)it=p.sockets.erase(it);else{it->second.accepted.clear();++it;}
}
void LanEndpoint::advance(Transport &t,uint64_t now) {
    auto state=t.snapshot();link(state.vip,state.generation,state.lan_available && state.races_allowed);
    ReceivedDatagram in;
    for(unsigned i=0;i<256 && t.take_lan(in);++i) {
        if(in.generation!=impl_->generation)continue;
        if(in.departed_ip)peer_left(in.departed_ip);
        else {Datagram d;if(decode_datagram(in.bytes.data(),in.bytes.size(),d))input(d,in.generation);}
    }
    tick(now);
    if(impl_->fatal){t.stop();link(0,0,false);return;}
    for(unsigned i=0;i<256 && output();++i){auto bytes=encode_datagram(*output());if(!t.send_lan(impl_->generation,bytes.data(),bytes.size()))break;consume_output();}
}
void LanEndpoint::tick(uint64_t now) {
    auto &p=*impl_;
    // A host's lobby client connects to its own server. Keep loopback inside
    // this endpoint: the public switch must never see these frames.
    for(unsigned i=0;i<512 && !p.fatal;++i) {
        auto it=std::find_if(p.tx.begin(),p.tx.end(),[&](const Datagram &d){return d.destination==p.vip;});
        if(it==p.tx.end())break;
        auto d=std::move(*it);p.tx.erase(it);input_impl(d,p.generation,true);
    }
    for(auto &entry:p.sockets){auto &s=entry.second;if(s.type==Impl::stream && s.error==LanError::would_block && (now<s.started || now-s.started>=3000)){
        p.discard_tx(s);p.control(s,3);p.fail(s,LanError::timeout);
    }}
    p.reap();
}
void LanEndpoint::peer_left(uint32_t ip) {
    auto &p=*impl_;
    for(auto &entry:p.sockets){auto &s=entry.second;if(s.type==Impl::stream && s.peer==ip){p.discard_tx(s);p.fail(s,LanError::reset);}}
    p.reap();
}
LanHandle LanEndpoint::udp(uint16_t port) {
    auto &p=*impl_;Impl::Socket s;s.type=Impl::udp_socket;s.port=p.bind(port,false);return s.port?p.add(std::move(s)):0;
}
LanHandle LanEndpoint::listen(uint16_t port,unsigned backlog) {
    auto &p=*impl_;if(!port || !backlog || backlog>32)return 0;
    Impl::Socket s;s.type=Impl::listener;s.port=p.bind(port,true);s.backlog=backlog;return s.port?p.add(std::move(s)):0;
}
LanHandle LanEndpoint::connect(uint32_t peer,uint16_t port,uint64_t now,uint16_t local) {
    auto &p=*impl_;if((peer>>24)==127)peer=p.vip;
    if(!p.vip || !service_ip(peer) || peer==hub_ip || !port || p.sockets.size()>=Impl::sockets_max)return 0;
    Impl::Socket s;s.port=p.bind(local,true);if(!s.port)return 0;
    s.peer=peer;s.remote_port=port;s.started=now;s.error=LanError::would_block;
    if(!++p.cid)++p.cid;s.cid=p.cid;
    if(p.next_handle==UINT32_MAX || !p.control(s,1))return 0;
    return p.add(std::move(s));
}
LanHandle LanEndpoint::accept(LanHandle handle) {
    auto &p=*impl_;auto *s=p.get(handle);if(!s || s->type!=Impl::listener)return 0;
    while(!s->accepted.empty()) {auto h=s->accepted.front();s->accepted.pop_front();auto *c=p.get(h);
        if(c && c->error==LanError::ok){c->parent=0;return h;}
        if(c){p.clear_rx(*c);p.sockets.erase(h);}
    }return 0;
}
LanError LanEndpoint::status(LanHandle h)const {auto *s=impl_->get(h);return s?s->error:LanError::invalid;}
uint16_t LanEndpoint::port(LanHandle h)const {auto *s=impl_->get(h);return s?s->port:0;}
LanReady LanEndpoint::ready(LanHandle h)const {
    auto &p=*impl_;auto *s=p.get(h);LanReady r;if(!s)return r;
    r.peer=s->peer;r.remote_port=s->remote_port;
    r.except=s->error!=LanError::ok && s->error!=LanError::would_block;
    if(s->type==Impl::listener)r.read=!s->accepted.empty();
    else if(s->type==Impl::udp_socket){r.read=!s->datagrams.empty();r.bytes=r.read?s->datagrams.front().data.size():0;r.write=p.vip && !p.fatal && p.tx.size()<Impl::tx_data_max;}
    else {r.read=!s->received.empty() || s->read_fin || r.except;r.bytes=s->received.size();r.write=p.vip && !p.fatal && s->error==LanError::ok && !s->write_fin && p.tx.size()<Impl::tx_data_max;}
    return r;
}
LanResult LanEndpoint::send(LanHandle h,const uint8_t *data,size_t n) {
    auto &p=*impl_;auto *s=p.get(h);if(!s || s->type!=Impl::stream || (n && !data))return {LanError::invalid,0};
    if(s->error!=LanError::ok)return {s->error,0};if(s->write_fin)return {LanError::closed,0};if(!p.vip)return {LanError::offline,0};
    size_t sent=0;while(sent<n){auto count=std::min(n-sent,lan_data_max);if(!p.emit(4,4,s->peer,s->port,s->remote_port,s->cid,data+sent,count))break;sent+=count;}
    return {sent || !n?LanError::ok:LanError::would_block,sent};
}
LanResult LanEndpoint::receive(LanHandle h,uint8_t *data,size_t cap,bool peek) {
    auto &p=*impl_;auto *s=p.get(h);if(!s || s->type!=Impl::stream || (cap && !data))return {LanError::invalid,0};
    if(s->error!=LanError::ok)return {s->error,0};
    if(s->received.empty())return {s->read_fin || !cap?LanError::ok:LanError::would_block,0};
    auto n=std::min(cap,s->received.size());
    if(peek)std::copy_n(s->received.begin(),n,data);
    else {for(size_t i=0;i<n;++i){data[i]=s->received.front();s->received.pop_front();}p.rx_bytes-=n;}
    return {LanError::ok,n};
}
LanResult LanEndpoint::send_to(LanHandle h,uint32_t peer,uint16_t port,const uint8_t *data,size_t n) {
    auto &p=*impl_;if((peer>>24)==127)peer=p.vip;
    auto *s=p.get(h);if(!s || s->type!=Impl::udp_socket || (n && !data) || n>lan_data_max || !port ||
        (!service_ip(peer) && peer!=UINT32_MAX) || peer==hub_ip)return {LanError::invalid,0};
    if(!p.vip)return {LanError::offline,0};
    return p.emit(3,0,peer,s->port,port,0,data,n)?LanResult{LanError::ok,n}:LanResult{LanError::would_block,0};
}
LanResult LanEndpoint::receive_from(LanHandle h,uint32_t &peer,uint16_t &port,std::vector<uint8_t> &data,bool peek) {
    auto &p=*impl_;auto *s=p.get(h);if(!s || s->type!=Impl::udp_socket)return {LanError::invalid,0};
    if(s->error!=LanError::ok){auto e=s->error;s->error=LanError::ok;return {e,0};}
    if(s->datagrams.empty())return {LanError::would_block,0};
    if(peek){const auto &d=s->datagrams.front();peer=d.peer;port=d.port;data=d.data;}
    else {auto d=std::move(s->datagrams.front());s->datagrams.pop_front();peer=d.peer;port=d.port;data=std::move(d.data);p.rx_bytes-=data.size();}
    return {LanError::ok,data.size()};
}
LanError LanEndpoint::shutdown_write(LanHandle h) {
    auto &p=*impl_;auto *s=p.get(h);if(!s || s->type!=Impl::stream)return LanError::invalid;
    if(s->error!=LanError::ok)return s->error;if(s->write_fin)return LanError::ok;
    if(!p.control(*s,5))return LanError::would_block;s->write_fin=true;return LanError::ok;
}
LanError LanEndpoint::close(LanHandle h,bool abortive) {
    auto &p=*impl_;auto *s=p.get(h);if(!s)return LanError::invalid;
    if(s->type==Impl::listener) {
        // Keep the handle alive if a control frame cannot yet be queued.
        while(!s->accepted.empty()){auto child=s->accepted.front();auto result=close(child,true);if(result==LanError::would_block)return result;s->accepted.pop_front();}
    }else if(s->type==Impl::stream && (s->error==LanError::ok || s->error==LanError::would_block)) {
        bool reset=abortive || s->error==LanError::would_block || !s->received.empty();
        if(reset)p.discard_tx(*s);
        if(p.vip && (reset || !s->write_fin) && !p.control(*s,reset?3:5))return LanError::would_block;
    }
    p.clear_rx(*s);p.sockets.erase(h);return LanError::ok;
}
bool LanEndpoint::input(const Datagram &d,uint32_t gen) {
    return input_impl(d,gen,false);
}
bool LanEndpoint::input_impl(const Datagram &d,uint32_t gen,bool local) {
    auto &p=*impl_;LanPacket packet;
    if(!p.vip || p.fatal || gen!=p.generation || !service_ip(d.source) || (d.source==p.vip && !local) || d.source==hub_ip || !d.source_port || !d.destination_port ||
       (d.destination!=p.vip && d.destination!=UINT32_MAX) || !decode_lan(d,packet))return false;
    if(d.kind==3) {
        bool bound=false;
        for(auto &entry:p.sockets){auto &s=entry.second;if(s.type!=Impl::udp_socket || s.port!=d.destination_port)continue;bound=true;
            // Unreachable replies are not socket-wide errors: an unconnected
            // UDP socket may have sent to many peers. Its normal reads continue.
            if(packet.op==1)continue;
            if(s.datagrams.size()<Impl::rx_udp_max && p.rx_bytes+packet.data.size()<=Impl::rx_total_max){p.rx_bytes+=packet.data.size();s.datagrams.push_back({d.source,d.source_port,packet.data});}
        }
        if(!bound && packet.op==0 && d.destination!=UINT32_MAX)p.emit(3,1,d.source,d.destination_port,d.source_port,0);
        return true;
    }
    if(d.destination!=p.vip)return false;
    Impl::Socket *s=nullptr;
    for(auto &entry:p.sockets){auto &candidate=entry.second;if(candidate.type==Impl::stream && candidate.peer==d.source && candidate.remote_port==d.source_port && candidate.port==d.destination_port && candidate.cid==packet.connection){s=&candidate;break;}}
    auto reset=[&]{if(!p.emit(4,3,d.source,d.destination_port,d.source_port,packet.connection))p.break_link();};
    switch(packet.op) {
    case 1: {
        if(s)return true;
        Impl::Socket *listener=nullptr;LanHandle parent=0;
        for(auto &entry:p.sockets)if(entry.second.type==Impl::listener && entry.second.port==d.destination_port){listener=&entry.second;parent=entry.first;break;}
        if(!listener || listener->accepted.size()>=listener->backlog || p.sockets.size()>=Impl::sockets_max || p.next_handle==UINT32_MAX){reset();return true;}
        Impl::Socket child;child.port=d.destination_port;child.peer=d.source;child.remote_port=d.source_port;child.cid=packet.connection;child.parent=parent;
        if(!p.control(child,2)){p.break_link();return false;}
        auto h=p.add(std::move(child));listener->accepted.push_back(h);return true;
    }
    case 2: if(s && s->error==LanError::would_block)s->error=LanError::ok;else if(!s || s->error!=LanError::ok)reset();break;
    case 3: if(s){p.discard_tx(*s);p.fail(*s,s->error==LanError::would_block?LanError::refused:LanError::reset);}break;
    case 4:
        if(!s || s->error!=LanError::ok){reset();break;}
        if(s->read_fin)break;
        if(s->received.size()+packet.data.size()>Impl::rx_stream_max || p.rx_bytes+packet.data.size()>Impl::rx_total_max){
            p.discard_tx(*s);reset();p.fail(*s,LanError::reset);break;
        }
        s->received.insert(s->received.end(),packet.data.begin(),packet.data.end());p.rx_bytes+=packet.data.size();break;
    case 5: if(s && s->error==LanError::ok)s->read_fin=true;break;
    }
    return true;
}
const Datagram *LanEndpoint::output()const {return impl_->fatal || impl_->tx.empty()?nullptr:&impl_->tx.front();}
void LanEndpoint::consume_output(){if(!impl_->tx.empty())impl_->tx.pop_front();}
size_t LanEndpoint::queued()const{return impl_->tx.size();}
}
