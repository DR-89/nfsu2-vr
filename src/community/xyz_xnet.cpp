#include "xyz_xnet.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <mutex>

extern "C" unsigned xyz_xnet_arg_count(XyzXnetOp op) {
    static const unsigned counts[]={3,1,2,3,5,3,3,3,3,2,3,5,4,6,4,6,0};
    return unsigned(op)<XYZ_N_COUNT?counts[op]:UINT32_MAX;
}
namespace xyz {
namespace {
constexpr uint32_t bad_socket=UINT32_MAX;
enum Error { fault=10014,invalid=10022,too_many=10024,would_block=10035,not_socket=10038,
 message_size=10040,not_supported=10045,bad_family=10047,in_use=10048,bad_address=10049,
 network_down=10050,connection_reset=10054,no_buffers=10055,already_connected=10056,not_connected=10057,
 shut_down=10058,timed_out=10060,refused=10061 };
uint32_t error_of(LanError error) {
    switch(error){case LanError::ok:return 0;case LanError::would_block:return would_block;
    case LanError::offline:return network_down;case LanError::refused:return refused;
    case LanError::reset:return connection_reset;case LanError::timeout:return timed_out;
    case LanError::closed:return shut_down;case LanError::no_buffer:return no_buffers;default:return invalid;}
}
bool read(const XyzXboxGuest &g,uint32_t address,void *data,size_t size) {
    return (!size || (g.read && xyz_xbox_address(address,size) && g.read(g.context,address,data,size)));
}
bool write(const XyzXboxGuest &g,uint32_t address,const void *data,size_t size) {
    return (!size || (g.write && xyz_xbox_address(address,size) && g.write(g.context,address,data,size)));
}
uint32_t le32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void put32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;++i)p[i]=uint8_t(x>>(8*i));}
bool get32(const XyzXboxGuest &g,uint32_t address,uint32_t &x){uint8_t b[4];if(!read(g,address,b,4))return false;x=le32(b);return true;}
bool set32(const XyzXboxGuest &g,uint32_t address,uint32_t x){uint8_t b[4];put32(b,x);return write(g,address,b,4);}
bool address(const XyzXboxGuest &g,uint32_t pointer,uint32_t size,uint32_t &ip,uint16_t &port) {
    uint8_t b[16];if(size<16 || !read(g,pointer,b,16) || b[0]!=2 || b[1])return false;
    ip=uint32_t(b[4])<<24|uint32_t(b[5])<<16|uint32_t(b[6])<<8|b[7];port=uint16_t(b[2]<<8|b[3]);return true;
}
bool address_out(const XyzXboxGuest &g,uint32_t pointer,uint32_t size_pointer,uint32_t ip,uint16_t port) {
    uint32_t cap=0;if(!get32(g,size_pointer,cap) || cap<16)return false;
    uint8_t b[16]={2,0,uint8_t(port>>8),uint8_t(port)};
    for(unsigned i=0;i<4;++i)b[4+i]=uint8_t(ip>>(24-8*i));
    return write(g,pointer,b,16) && set32(g,size_pointer,16);
}
}
struct XnetSockets::Impl {
    struct Socket {bool udp=false,bound=false,listening=false,nonblocking=false,broadcast=false,abortive=false,read_closed=false,write_closed=false;
        uint32_t protocol=0,peer=0,failure=0;uint16_t port=0,remote_port=0;LanHandle endpoint=0;};
    LanEndpoint &lan;uint32_t vip,generation,next=0;
    std::map<uint32_t,Socket> sockets;
    std::map<uint32_t,uint32_t> errors;
    std::map<uint32_t,bool> virtual_error;
    Impl(LanEndpoint &endpoint,uint32_t address):lan(endpoint),vip(address),generation(address?1:0){}
    uint32_t fail(uint32_t thread,uint32_t error){if(errors.size()<64 || errors.count(thread))errors[thread]=error;return bad_socket;}
    uint32_t add(Socket socket){if(sockets.size()>=64 || next>=0xffffff)return 0;uint32_t handle=0x6e000000u|++next;sockets.emplace(handle,socket);return handle;}
    bool port_used(uint16_t port,bool udp,uint32_t except)const {
        if(!port)return false;for(const auto &s:sockets)if(s.first!=except && !s.second.failure && s.second.udp==udp && s.second.bound && s.second.port==port)return true;return false;
    }
    bool udp_bound(Socket &s){if(!s.endpoint){s.endpoint=lan.udp(s.port);if(!s.endpoint)return false;s.port=lan.port(s.endpoint);s.bound=true;}return true;}
};
XnetSockets::XnetSockets(LanEndpoint &endpoint,uint32_t vip):impl_(std::make_unique<Impl>(endpoint,vip)){}
XnetSockets::~XnetSockets(){for(auto &s:impl_->sockets)if(s.second.endpoint)impl_->lan.close(s.second.endpoint,true);}
void XnetSockets::reset() {
    auto &p=*impl_;p.lan.link(0,0,false);
    for(auto &entry:p.sockets){auto &s=entry.second;
        if(s.endpoint)p.lan.close(s.endpoint,true);
        s.endpoint=0;s.failure=connection_reset;
    }
    p.vip=p.generation=0;
}
void XnetSockets::link(uint32_t vip,uint32_t generation) {
    auto &p=*impl_;if(!generation || !service_ip(vip) || vip==hub_ip)vip=generation=0;
    if(p.generation && (p.generation!=generation || p.vip!=vip))this->reset();
    p.vip=vip;p.generation=generation;p.lan.link(vip,generation,generation!=0);
}
bool XnetSockets::dispatch(const XyzXboxGuest &g,XyzXnetOp op,uint32_t thread,const uint32_t *a,unsigned count,uint64_t now,bool create,uint32_t &result) {
    auto &p=*impl_;auto own=[](uint32_t h){return (h&0xff000000u)==0x6e000000u;};
    bool route=false,mixed=false;uint32_t malformed=0;
    if(xyz_xnet_arg_count(op)!=count || (count && !a)) {
        if(!create)return false;route=true;malformed=invalid;
    } else if(op==XYZ_N_ERROR) {
        auto it=p.virtual_error.find(thread);route=it!=p.virtual_error.end() && it->second;
    } else if(op==XYZ_N_SOCKET)route=create;
    else if(op==XYZ_N_SELECT) {
        bool retail=false;
        for(unsigned k=1;k<4;++k)if(a[k]){
            uint32_t n=0;if(!get32(g,a[k],n) || n>64){malformed=fault;continue;}
            for(unsigned i=0;i<n;++i){uint32_t h=0;if(!get32(g,a[k]+4+4*i,h)){malformed=fault;break;}
                if(own(h))route=true;else retail=true;
            }
        }
        mixed=route && retail;
        if(create && !retail)route=true;
    } else route=own(a[0]);
    if(op!=XYZ_N_ERROR && (p.virtual_error.size()<64 || p.virtual_error.count(thread)))p.virtual_error[thread]=route;
    if(!route)return false;
    try{result=malformed?p.fail(thread,malformed):mixed?p.fail(thread,invalid):call(g,op,thread,a,count,now);}
    catch(...){result=p.fail(thread,no_buffers);}
    return true;
}
uint32_t XnetSockets::call(const XyzXboxGuest &g,XyzXnetOp op,uint32_t thread,const uint32_t *a,unsigned count,uint64_t now) {
    auto &p=*impl_;auto fail=[&](uint32_t error){return p.fail(thread,error);};
    if(xyz_xnet_arg_count(op)!=count || (count && !a))return fail(invalid);
    if(op==XYZ_N_ERROR){auto e=p.errors.find(thread);return e==p.errors.end()?0:e->second;}
    if(op==XYZ_N_SOCKET) {
        if(a[0]!=2)return fail(bad_family);
        if(a[1]!=1 && a[1]!=2)return fail(not_supported);
        if((a[1]==1 && a[2]!=0 && a[2]!=6) || (a[1]==2 && a[2]!=0 && a[2]!=17 && a[2]!=254))return fail(not_supported);
        Impl::Socket s;s.udp=a[1]==2;s.protocol=a[2];auto h=p.add(s);return h?h:fail(too_many);
    }
    if(op==XYZ_N_SELECT) {
        // The audited DirtySock poll is nonblocking. Do not turn a nonzero
        // timeout into a render/game-thread sleep or pretend it elapsed.
        uint8_t timeout[8];if(a[4] && (!read(g,a[4],timeout,8)))return fail(fault);
        if(!a[4] || le32(timeout) || le32(timeout+4))return fail(not_supported);
        if(!a[1] && !a[2] && !a[3])return fail(invalid);
        std::array<std::vector<uint32_t>,3> sets;uint32_t total=0;
        for(unsigned k=0;k<3;++k)if(a[k+1]) {
            uint32_t n=0;if(!get32(g,a[k+1],n) || n>64)return fail(fault);
            for(unsigned i=0;i<n;++i){uint32_t h;if(!get32(g,a[k+1]+4+i*4,h))return fail(fault);
                auto it=p.sockets.find(h);if(it==p.sockets.end())return fail(not_socket);
                auto &s=it->second;auto r=p.lan.ready(s.endpoint);
                if(s.failure)r.read=r.except=true;
                if((k==0 && (s.read_closed || r.read)) || (k==1 && r.write) || (k==2 && r.except)){sets[k].push_back(h);++total;}
            }
        }
        for(unsigned k=0;k<3;++k)if(a[k+1]) {
            std::vector<uint8_t> b(4+sets[k].size()*4);put32(b.data(),uint32_t(sets[k].size()));
            for(size_t i=0;i<sets[k].size();++i)put32(b.data()+4+i*4,sets[k][i]);
            if(!write(g,a[k+1],b.data(),b.size()))return fail(fault);
        }return total;
    }
    auto it=p.sockets.find(a[0]);if(it==p.sockets.end())return fail(not_socket);auto &s=it->second;
    if(s.failure && op!=XYZ_N_CLOSE)return fail(s.failure);
    switch(op) {
    case XYZ_N_CLOSE: {
        auto e=s.endpoint?p.lan.close(s.endpoint,s.abortive):LanError::ok;if(e!=LanError::ok)return fail(error_of(e));
        p.sockets.erase(it);return 0;
    }
    case XYZ_N_IOCTL: {
        uint32_t value;if(!get32(g,a[2],value))return fail(fault);
        if(a[1]==0x8004667eu){s.nonblocking=value!=0;return 0;}
        if(a[1]==0x4004667fu){auto r=p.lan.ready(s.endpoint);uint32_t n=uint32_t(r.bytes);if(s.udp && s.protocol==254 && r.read)n+=2;return set32(g,a[2],n)?0:fail(fault);}
        return fail(not_supported);
    }
    case XYZ_N_SETOPT: {
        uint8_t b[4];if(a[4]!=4 || !read(g,a[3],b,4))return fail(fault);auto value=le32(b);
        if(a[1]==0xffff) {
            if(a[2]==0x20){s.broadcast=value!=0;return 0;}
            if(a[2]==0x80){s.abortive=(b[0] || b[1]) && !b[2] && !b[3];return 0;}
            if((a[2]==4 || a[2]==8) && !value)return 0;
        }
        if(a[1]==6 && a[2]==1 && !s.udp)return 0;
        return fail(not_supported);
    }
    case XYZ_N_BIND: {
        if(s.bound || s.endpoint)return fail(invalid);
        uint32_t ip;uint16_t port;if(!address(g,a[1],a[2],ip,port))return fail(fault);
        if(ip && ip!=p.vip && (ip>>24)!=127)return fail(bad_address);
        if(p.port_used(port,s.udp,a[0]))return fail(in_use);
        s.port=port;if(s.udp && !p.udp_bound(s))return fail(in_use);s.bound=true;return 0;
    }
    case XYZ_N_CONNECT: {
        if(!p.vip)return fail(network_down);
        uint32_t peer;uint16_t port;if(!address(g,a[1],a[2],peer,port))return fail(fault);
        if((peer>>24)==127)peer=p.vip;
        if(!service_ip(peer) || peer==hub_ip || !port)return fail(bad_address);
        if(s.listening)return fail(invalid);
        if(s.udp){if(!p.udp_bound(s))return fail(no_buffers);s.peer=peer;s.remote_port=port;return 0;}
        if(s.endpoint){auto status=p.lan.status(s.endpoint);return fail(status==LanError::ok?already_connected:error_of(status));}
        s.endpoint=p.lan.connect(peer,port,now,s.port);if(!s.endpoint)return fail(no_buffers);
        s.peer=peer;s.remote_port=port;s.port=p.lan.port(s.endpoint);s.bound=true;return fail(would_block);
    }
    case XYZ_N_LISTEN: {
        if(s.udp || !s.bound || !s.port)return fail(invalid);
        if(s.listening)return 0;if(s.endpoint)return fail(invalid);
        s.endpoint=p.lan.listen(s.port,std::max(1u,std::min(a[1],32u)));if(!s.endpoint)return fail(in_use);s.listening=true;return 0;
    }
    case XYZ_N_ACCEPT: {
        if(!s.listening)return fail(invalid);if(p.sockets.size()>=64)return fail(too_many);
        uint32_t size=0;if(a[1] && (!get32(g,a[2],size) || size<16 || !xyz_xbox_address(a[1],16)))return fail(fault);
        auto child=p.lan.accept(s.endpoint);if(!child)return fail(would_block);
        auto remote=p.lan.ready(child);Impl::Socket accepted=s;accepted.endpoint=child;accepted.listening=false;accepted.peer=remote.peer;accepted.remote_port=remote.remote_port;
        if(a[1] && !address_out(g,a[1],a[2],remote.peer,remote.remote_port)){p.lan.close(child,true);return fail(fault);}
        auto h=p.add(accepted);if(!h){p.lan.close(child,true);return fail(too_many);}return h;
    }
    case XYZ_N_SOCKNAME:case XYZ_N_PEERNAME: {
        if(op==XYZ_N_PEERNAME && (!s.peer || (!s.udp && p.lan.status(s.endpoint)!=LanError::ok)))return fail(not_connected);
        auto ip=op==XYZ_N_SOCKNAME?p.vip:s.peer;auto port=op==XYZ_N_SOCKNAME?s.port:s.remote_port;
        return address_out(g,a[1],a[2],ip,port)?0:fail(fault);
    }
    case XYZ_N_SHUTDOWN: {
        if(a[1]>2)return fail(invalid);if(!s.endpoint)return fail(not_connected);
        if(a[1]!=0 && !s.udp){auto e=p.lan.shutdown_write(s.endpoint);if(e!=LanError::ok)return fail(error_of(e));}
        if(a[1]!=0)s.write_closed=true;
        if(a[1]!=1)s.read_closed=true;return 0;
    }
    case XYZ_N_SEND:case XYZ_N_SENDTO: {
        if(!p.vip)return fail(network_down);
        if(s.write_closed)return fail(shut_down);
        if(a[2]>1024*1024 || a[3])return fail(a[3]?not_supported:message_size);
        uint32_t peer=s.peer;uint16_t port=s.remote_port;
        if(op==XYZ_N_SENDTO && a[4] && !address(g,a[4],a[5],peer,port))return fail(fault);
        if((peer>>24)==127)peer=p.vip;
        if(s.udp && (!port || (!service_ip(peer) && peer!=UINT32_MAX) || peer==hub_ip))return fail(not_connected);
        if(peer==UINT32_MAX && !s.broadcast)return fail(10013);
        std::vector<uint8_t> bytes(a[2]);if(!read(g,a[1],bytes.data(),bytes.size()))return fail(fault);
        if(s.udp) {
            if(!p.udp_bound(s))return fail(no_buffers);
            size_t prefix=s.protocol==254?2:0;
            if(bytes.size()<prefix || (prefix && (bytes[0] || bytes[1])))return fail(invalid);
            auto size=bytes.size()-prefix;if(size>lan_data_max)return fail(message_size);
            auto result=p.lan.send_to(s.endpoint,peer,port,size?bytes.data()+prefix:nullptr,size);
            return result.error==LanError::ok?a[2]:fail(error_of(result.error));
        }
        if(!s.endpoint || s.listening)return fail(not_connected);
        auto result=p.lan.send(s.endpoint,bytes.data(),bytes.size());return result.error==LanError::ok?uint32_t(result.bytes):fail(error_of(result.error));
    }
    case XYZ_N_RECV:case XYZ_N_RECVFROM: {
        if(!p.vip)return fail(network_down);
        if(a[2]>1024*1024 || (a[3]&~2u))return fail(a[3]&~2u?not_supported:message_size);
        if(s.read_closed)return fail(shut_down);
        uint32_t cap=0;if(op==XYZ_N_RECVFROM && a[4] && (!get32(g,a[5],cap) || cap<16 || !xyz_xbox_address(a[4],16)))return fail(fault);
        if(s.udp) {
            if(!p.udp_bound(s))return fail(no_buffers);
            std::vector<uint8_t> bytes;uint32_t peer=0;uint16_t port=0;
            LanResult result;
            for(unsigned i=0;i<256;++i){
                result=p.lan.receive_from(s.endpoint,peer,port,bytes,true);
                if(result.error!=LanError::ok)return fail(error_of(result.error));
                if(!s.peer || (peer==s.peer && port==s.remote_port))break;
                p.lan.receive_from(s.endpoint,peer,port,bytes);
                if(i==255)return fail(would_block);
            }
            if(s.protocol==254)bytes.insert(bytes.begin(),2,0);
            auto n=std::min<size_t>(a[2],bytes.size());if(!write(g,a[1],bytes.data(),n))return fail(fault);
            if(op==XYZ_N_RECVFROM && a[4] && !address_out(g,a[4],a[5],peer,port))return fail(fault);
            if(!(a[3]&2)){std::vector<uint8_t> discard;p.lan.receive_from(s.endpoint,peer,port,discard);}
            return bytes.size()>a[2]?fail(message_size):uint32_t(n);
        }
        if(!s.endpoint || s.listening)return fail(not_connected);
        std::vector<uint8_t> bytes(a[2]);auto result=p.lan.receive(s.endpoint,bytes.data(),bytes.size(),true);
        if(result.error!=LanError::ok)return fail(error_of(result.error));
        if(!write(g,a[1],bytes.data(),result.bytes))return fail(fault);
        if(op==XYZ_N_RECVFROM && a[4] && !address_out(g,a[4],a[5],s.peer,s.remote_port))return fail(fault);
        if(!(a[3]&2))p.lan.receive(s.endpoint,bytes.data(),result.bytes);
        return uint32_t(result.bytes);
    }
    default:return fail(not_supported);
    }
}
}

namespace {
// No connection is created here. This isolated endpoint is selected only by
// the explicit guest-code diagnostic; normal gameplay still uses retail XNet.
struct Probe {uint32_t thread;xyz::LanEndpoint lan{0x584e4554};xyz::XnetSockets sockets{lan,0x0a4d0102};explicit Probe(uint32_t t):thread(t){lan.link(0x0a4d0102,1,true);}};
std::mutex probe_mutex;
std::unique_ptr<Probe> probe;
}
extern "C" int xyz_xnet_probe_begin(uint32_t thread) {
    try{std::lock_guard<std::mutex> lock(probe_mutex);if(probe)return 0;probe=std::make_unique<Probe>(thread);return 1;}catch(...){return 0;}
}
extern "C" void xyz_xnet_probe_tick(uint64_t now) {std::lock_guard<std::mutex> lock(probe_mutex);if(probe)probe->lan.tick(now);}
extern "C" void xyz_xnet_probe_end(void) {std::lock_guard<std::mutex> lock(probe_mutex);probe.reset();}
extern "C" int xyz_xnet_dispatch(const XyzXboxGuest *g,XyzXnetOp op,uint32_t thread,const uint32_t *args,unsigned count,uint64_t now,uint32_t *result) {
    std::lock_guard<std::mutex> lock(probe_mutex);if(!probe || probe->thread!=thread || !g || !result)return 0;
    try{*result=probe->sockets.call(*g,op,thread,args,count,now);}catch(...){*result=UINT32_MAX;}return 1;
}
