#include "xyz_client.h"
#include "xyz_guest_memory.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <thread>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace xyz;
struct Wire {
    std::mutex mutex;std::deque<SocketEvent> incoming;std::deque<std::vector<uint8_t>> outgoing;
    std::string hello;std::thread::id guest=std::this_thread::get_id();unsigned created=0,closed=0;
    void text(const char *s){std::lock_guard<std::mutex> l(mutex);SocketEvent e;e.kind=SocketEvent::text;e.data.assign(s,s+std::strlen(s));incoming.push_back(std::move(e));}
};
struct WireSocket:Socket {
    std::shared_ptr<Wire> w;
    explicit WireSocket(std::shared_ptr<Wire> wire):w(wire){owner();std::lock_guard<std::mutex> l(w->mutex);++w->created;}
    void owner(){CHECK(std::this_thread::get_id()!=w->guest);}
    std::string name_info(const std::string &name,const std::string&)override{owner();return "{\"name\":\""+name+"\",\"state\":\"free\"}";}
    bool connect(const std::string&)override{owner();std::lock_guard<std::mutex> l(w->mutex);SocketEvent e;e.kind=SocketEvent::opened;w->incoming.push_back(e);return true;}
    bool send(const uint8_t *b,size_t n,bool text)override{owner();std::lock_guard<std::mutex> l(w->mutex);if(text)w->hello.assign((const char*)b,n);else w->outgoing.emplace_back(b,b+n);return true;}
    bool poll(SocketEvent &e)override{owner();std::lock_guard<std::mutex> l(w->mutex);if(w->incoming.empty())return false;e=std::move(w->incoming.front());w->incoming.pop_front();return true;}
    void close()override{owner();std::lock_guard<std::mutex> l(w->mutex);++w->closed;w->incoming.clear();w->outgoing.clear();w->hello.clear();}
};
int main(){
    constexpr uint32_t mine=0x0a4d0102,other=0x0a4d0103,sa=0x10000,len=0x10040,fd=0x10050,tv=0x100a0,buf=0x12000,out=0x23000;
    auto w=std::make_shared<Wire>();std::atomic<uint64_t> now{1000};
    Client client([w]{return std::make_unique<WireSocket>(w);},[&]{return now.load();});
    XyzGuestMemory memory;auto guest=memory.guest();uint32_t result=0;
    auto routed=[&](XyzXnetOp op,std::initializer_list<uint32_t> a){return client.socket_call(guest,op,7,a.begin(),unsigned(a.size()),now,result);};
    auto call=[&](XyzXnetOp op,std::initializer_list<uint32_t> a){CHECK(routed(op,a));return result;};
    CHECK(!routed(XYZ_N_SOCKET,{2,1,0}) && !w->created);
    ClientProfile profile{{"Session_Test","0123456789abcdef0123456789abcdef",{},true},"PRIVATE",11};
    CHECK(client.connect(profile));
    auto early=call(XYZ_N_SOCKET,{2,1,0});memory.address(sa,other,9900);
    CHECK(call(XYZ_N_CONNECT,{early,sa,16})==UINT32_MAX && call(XYZ_N_ERROR,{})==10050);
    CHECK(!call(XYZ_N_CLOSE,{early}));
    XyzXboxFrame local{};local.status=XYZ_XBOX_OTHER_RACE;
    LanEndpoint peer(123);peer.link(other,1,true);auto listener=peer.listen(9900),udp_peer=peer.udp(3658);CHECK(listener && udp_peer);
    auto pump=[&](bool frame=false){
        now+=10;if(frame)client.frame(local,false,int64_t(now)*1000);
        std::deque<std::vector<uint8_t>> sent;
        {std::lock_guard<std::mutex> l(w->mutex);sent.swap(w->outgoing);}
        for(auto &bytes:sent){Datagram d;CHECK(decode_datagram(bytes.data(),bytes.size(),d));
            if(d.kind==3 || d.kind==4){CHECK(d.destination==other);CHECK(peer.input(d,1));}}
        peer.tick(now);
        {std::lock_guard<std::mutex> l(w->mutex);while(auto *d=peer.output()){
            SocketEvent e;e.kind=SocketEvent::binary;e.data=encode_datagram(*d);w->incoming.push_back(std::move(e));peer.consume_output();}}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    };
    unsigned wait_index=0;
    auto until=[&](auto predicate,bool frames=false){++wait_index;for(unsigned i=0;i<600;++i){if(predicate())return;pump(frames);}
        std::fprintf(stderr,"Timed out at exchange %u, result=%u, state=%d\n",wait_index,result,int(client.snapshot().transport.state));CHECK(false);};
    until([&]{std::lock_guard<std::mutex> l(w->mutex);return !w->hello.empty();},true);
    {std::lock_guard<std::mutex> l(w->mutex);CHECK(w->hello.find("\"lan\":1")!=std::string::npos);}
    w->text("{\"t\":\"welcome\",\"vip\":172818690,\"token\":\"session-fixture\"}");
    until([&]{return client.snapshot().transport.lan_available;},true);auto generation=client.snapshot().transport.generation;
    auto tcp=call(XYZ_N_SOCKET,{2,1,6});CHECK(call(XYZ_N_CONNECT,{tcp,sa,16})==UINT32_MAX && call(XYZ_N_ERROR,{})==10035);
    // From here the native game frame is deliberately paused: socket calls
    // alone must pump reliable data and observe disconnect/restrictions.
    until([&]{memory.put(fd,1);memory.put(fd+4,tcp);return call(XYZ_N_SELECT,{0,0,fd,0,tv})==1;});
    auto accepted=peer.accept(listener);CHECK(accepted);
    std::vector<uint8_t> payload(65537),received;for(size_t i=0;i<payload.size();++i)payload[i]=uint8_t(i*31+(i>>7));
    CHECK(XyzGuestMemory::write(&memory,buf,payload.data(),payload.size()));
    CHECK(call(XYZ_N_SEND,{tcp,buf,uint32_t(payload.size()),0})==payload.size());
    until([&]{uint8_t b[8192];auto r=peer.receive(accepted,b,sizeof b);if(r.error==LanError::ok)received.insert(received.end(),b,b+r.bytes);return received.size()==payload.size();});
    CHECK(received==payload);CHECK(peer.send(accepted,payload.data(),payload.size()).bytes==payload.size());
    received.clear();until([&]{auto n=call(XYZ_N_RECV,{tcp,out,8192,0});if(n!=UINT32_MAX)received.insert(received.end(),memory.at(out),memory.at(out)+n);return received.size()==payload.size();});CHECK(received==payload);
    auto udp=call(XYZ_N_SOCKET,{2,2,254});memory.address(sa,0,3658);CHECK(!call(XYZ_N_BIND,{udp,sa,16}));memory.address(sa,other,3658);
    memory.at(buf)[0]=memory.at(buf)[1]=0;std::memcpy(memory.at(buf)+2,payload.data(),lan_data_max);
    CHECK(call(XYZ_N_SENDTO,{udp,buf,uint32_t(lan_data_max+2),0,sa,16})==lan_data_max+2);
    uint32_t ip=0;uint16_t port=0;std::vector<uint8_t> datagram;
    until([&]{return peer.receive_from(udp_peer,ip,port,datagram).error==LanError::ok;});
    CHECK(ip==mine && port==3658 && datagram==std::vector<uint8_t>(payload.begin(),payload.begin()+lan_data_max));
    CHECK(peer.send_to(udp_peer,mine,3658,datagram.data(),datagram.size()).bytes==datagram.size());memory.put(len,16);
    until([&]{return call(XYZ_N_RECVFROM,{udp,out,1500,0,sa,len})==lan_data_max+2;});
    CHECK(!memory.at(out)[0] && !memory.at(out)[1] && !std::memcmp(memory.at(out)+2,payload.data(),lan_data_max));
    // The worker's trusted departure notice resets the original guest stream.
    w->text("{\"t\":\"delta\",\"gone\":[172818691]}");
    until([&]{return call(XYZ_N_RECV,{tcp,out,8,0})==UINT32_MAX && call(XYZ_N_ERROR,{})==10054;});
    CHECK(!call(XYZ_N_CLOSE,{tcp}));
    w->text("{\"t\":\"races\",\"msg\":\"fixture restriction\"}");
    until([&]{return call(XYZ_N_RECV,{udp,out,8,0})==UINT32_MAX && call(XYZ_N_ERROR,{})==10054;});
    CHECK(!call(XYZ_N_CLOSE,{udp}));
    w->text("{\"t\":\"races\",\"lifted\":true}");
    until([&]{return client.snapshot().transport.races_allowed;},true);
    udp=call(XYZ_N_SOCKET,{2,2,17});memory.address(sa,0,3658);CHECK(!call(XYZ_N_BIND,{udp,sa,16}));
    {std::lock_guard<std::mutex> l(w->mutex);SocketEvent e;e.kind=SocketEvent::closed;e.code=1006;w->incoming.push_back(e);}
    until([&]{return call(XYZ_N_RECV,{udp,out,8,0})==UINT32_MAX && call(XYZ_N_ERROR,{})==10054;});
    until([&]{return client.snapshot().transport.state==LinkState::greeting;},true);
    w->text("{\"t\":\"welcome\",\"vip\":172818690,\"token\":\"resumed-fixture\"}");
    until([&]{return client.snapshot().transport.lan_available;},true);CHECK(client.snapshot().transport.generation!=generation);
    CHECK(call(XYZ_N_RECV,{udp,out,8,0})==UINT32_MAX && call(XYZ_N_ERROR,{})==10054);
    CHECK(!call(XYZ_N_CLOSE,{udp}));udp=call(XYZ_N_SOCKET,{2,2,17});CHECK(!call(XYZ_N_BIND,{udp,sa,16}));
    profile.connection.name="Replacement";CHECK(client.connect(profile));
    CHECK(call(XYZ_N_RECV,{udp,out,8,0})==UINT32_MAX && call(XYZ_N_ERROR,{})==10054);
    CHECK(!call(XYZ_N_CLOSE,{udp}));udp=call(XYZ_N_SOCKET,{2,2,17});
    client.disconnect();CHECK(call(XYZ_N_RECV,{udp,out,8,0})==UINT32_MAX && call(XYZ_N_ERROR,{})==10054);
    CHECK(!call(XYZ_N_CLOSE,{udp}));CHECK(!routed(XYZ_N_SOCKET,{2,1,0}));CHECK(!routed(XYZ_N_ERROR,{}));
    profile.connection.lan=false;CHECK(client.connect(profile));CHECK(!routed(XYZ_N_SOCKET,{2,1,0}));client.abort();
    std::puts("Session sockets: explicit LAN, worker ownership, TCP/UDP254 without game frames, departures, restrictions, same-IP resume, disconnect and retail fallback passed");
}
