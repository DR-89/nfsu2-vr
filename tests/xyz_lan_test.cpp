#include "xyz_lan.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace xyz;
constexpr uint32_t a_ip=0x0a4d0102,b_ip=0x0a4d0103;
static void transfer(LanEndpoint &a,LanEndpoint &b,uint32_t generation=1) {
    while(auto d=a.output()) {
        // Round trip through bytes, as a socket would; no shared native struct.
        auto wire=encode_datagram(*d);Datagram decoded;CHECK(decode_datagram(wire.data(),wire.size(),decoded));
        CHECK(b.input(decoded,generation));a.consume_output();
    }
}
static Datagram packet(uint32_t peer,uint16_t sport,uint16_t dport,uint8_t op,uint32_t cid) {
    Datagram d;d.source=peer;d.destination=a_ip;d.source_port=sport;d.destination_port=dport;
    LanPacket p;p.kind=4;p.op=op;p.connection=cid;CHECK(encode_lan(p,d));return d;
}
int main() {
    // Fixed bytes from the documented browser mailbox writer tx_raw: big-
    // endian envelope, little-endian connection id, no native padding.
    const std::vector<uint8_t> syn={4,0,0,0,10,77,1,2,10,77,1,3,0xc0,0,0x26,0xac,1,64,0,0,0x78,0x56,0x34,0x12};
    Datagram d;LanPacket p;CHECK(decode_datagram(syn.data(),syn.size(),d));CHECK(decode_lan(d,p));
    CHECK(p.op==1 && p.connection==0x12345678 && d.destination_port==9900);
    Datagram re=d;CHECK(encode_lan(p,re) && encode_datagram(re)==syn);
    for(size_t n=0;n<8;++n){re.payload.resize(n);CHECK(!decode_lan(re,p));}
    for(unsigned i=2;i<4;++i){re=d;re.payload[i]=1;CHECK(!decode_lan(re,p));}
    re=d;re.payload[0]=6;CHECK(!decode_lan(re,p)); // GONE is trusted control, never peer data
    re=d;re.payload.push_back(1);CHECK(!decode_lan(re,p));
    re=d;std::fill(re.payload.begin()+4,re.payload.end(),0);CHECK(!decode_lan(re,p));
    LanEndpoint a(0x12345677),b(91);a.link(a_ip,1,true);b.link(b_ip,1,true);
    auto listener=b.listen(9900,2);CHECK(listener && !b.listen(9900));
    auto client=a.connect(b_ip,9900,1000);CHECK(client && a.status(client)==LanError::would_block);
    CHECK(encode_datagram(*a.output())==syn);
    auto original_syn=*a.output();transfer(a,b);CHECK(b.input(original_syn,1)); // duplicate SYN must not create another accepted socket
    auto server=b.accept(listener);CHECK(server && !b.accept(listener));
    transfer(b,a);CHECK(a.status(client)==LanError::ok);
    uint8_t small[19];CHECK(a.receive(client,small,sizeof small).error==LanError::would_block);
    // Wrong connection ID / remote port / peer cannot mutate this stream.
    auto wrong=packet(b_ip,9900,a.port(client),3,0x12345679);CHECK(a.input(wrong,1));CHECK(a.status(client)==LanError::ok);
    wrong=packet(b_ip,9901,a.port(client),3,0x12345678);CHECK(a.input(wrong,1));CHECK(a.status(client)==LanError::ok);
    wrong=packet(b_ip+1,9900,a.port(client),3,0x12345678);CHECK(a.input(wrong,1));CHECK(a.status(client)==LanError::ok);
    wrong=packet(b_ip,9900,a.port(client),3,0x12345678);CHECK(!a.input(wrong,2));CHECK(a.status(client)==LanError::ok);
    // More than one transport queue: short writes resume at the returned byte,
    // not at a packet boundary guessed by the caller. FIN follows all bytes.
    std::vector<uint8_t> data(700000),result;for(size_t i=0;i<data.size();++i)data[i]=uint8_t((i*37)^(i>>8));
    auto write=a.send(client,data.data(),data.size());CHECK(write.error==LanError::ok && write.bytes==256*lan_data_max);
    CHECK(a.send(client,data.data()+write.bytes,1).error==LanError::would_block);
    transfer(a,b);auto rest=a.send(client,data.data()+write.bytes,data.size()-write.bytes);CHECK(rest.bytes==data.size()-write.bytes);
    CHECK(a.shutdown_write(client)==LanError::ok);CHECK(a.send(client,data.data(),1).error==LanError::closed);transfer(a,b);
    while(true){auto read=b.receive(server,small,sizeof small);CHECK(read.error==LanError::ok);if(!read.bytes)break;result.insert(result.end(),small,small+read.bytes);}
    CHECK(result==data);
    CHECK(b.send(server,data.data(),19).bytes==19);CHECK(b.shutdown_write(server)==LanError::ok);transfer(b,a);
    CHECK(a.receive(client,small,sizeof small).bytes==19 && !std::memcmp(small,data.data(),19));
    CHECK(a.receive(client,small,sizeof small).bytes==0);CHECK(a.close(client)==LanError::ok && b.close(server)==LanError::ok);
    // UDP shares a numeric port with TCP, preserves zero-length datagrams and
    // message boundaries, and cannot consume free-roam messages.
    auto ua=a.udp(3658),ub=b.udp(3658);CHECK(ua && ub && !a.udp(3658));
    CHECK(a.send_to(ua,b_ip,3658,nullptr,0).error==LanError::ok);
    CHECK(a.send_to(ua,b_ip,3658,data.data(),lan_data_max).bytes==lan_data_max);
    CHECK(a.send_to(ua,b_ip,3658,data.data(),lan_data_max+1).error==LanError::invalid);transfer(a,b);
    std::vector<uint8_t> udp;uint32_t peer=0;uint16_t port=0;
    CHECK(b.receive_from(ub,peer,port,udp).error==LanError::ok && udp.empty() && peer==a_ip && port==3658);
    CHECK(b.receive_from(ub,peer,port,udp).bytes==lan_data_max && std::equal(udp.begin(),udp.end(),data.begin()));
    CHECK(b.receive_from(ub,peer,port,udp).error==LanError::would_block);
    CHECK(a.send_to(ua,b_ip,3660,nullptr,0).error==LanError::ok);transfer(a,b);CHECK(b.output());LanPacket unreach;CHECK(decode_lan(*b.output(),unreach) && unreach.op==1);transfer(b,a);
    CHECK(a.send_to(ua,UINT32_MAX,3660,nullptr,0).error==LanError::ok);transfer(a,b);CHECK(!b.output());
    // Refusal, connect timeout, reset, disconnect and generation replacement.
    client=a.connect(b_ip,9999,2000);transfer(a,b);transfer(b,a);CHECK(a.status(client)==LanError::refused);CHECK(a.close(client)==LanError::ok);
    client=a.connect(b_ip,9900,2000);a.tick(4999);CHECK(a.status(client)==LanError::would_block);a.tick(5000);CHECK(a.status(client)==LanError::timeout);a.close(client);transfer(a,b);
    client=a.connect(b_ip,9900,6000);transfer(a,b);server=b.accept(listener);CHECK(server);transfer(b,a);
    CHECK(a.send(client,data.data(),20).bytes==20);a.peer_left(b_ip);CHECK(a.status(client)==LanError::reset && !a.output());a.close(client);
    b.link(b_ip,2,true);CHECK(b.status(server)==LanError::reset);b.close(server);
    CHECK(!b.input(original_syn,1));CHECK(b.input(original_syn,2));server=b.accept(listener);CHECK(server);b.link(b_ip,2,false);CHECK(b.status(server)==LanError::reset && !b.output());b.close(server);
    CHECK(b.send_to(ub,a_ip,3658,nullptr,0).error==LanError::offline);
    // A full listener rejects new attempts, and closing it resets its pending
    // children. Later opening the port cannot resurrect those connections.
    a.link(a_ip,3,true);b.link(b_ip,3,true);
    auto c1=a.connect(b_ip,9900,7000),c2=a.connect(b_ip,9900,7000),c3=a.connect(b_ip,9900,7000);CHECK(c1 && c2 && c3);
    transfer(a,b,3);transfer(b,a,3);CHECK(a.status(c3)==LanError::refused);
    CHECK(b.close(listener)==LanError::ok);transfer(b,a,3);CHECK(a.status(c1)==LanError::reset && a.status(c2)==LanError::reset);
    a.close(c1);a.close(c2);a.close(c3);
    listener=b.listen(9900);client=a.connect(b_ip,9900,8000);transfer(a,b,3);server=b.accept(listener);transfer(b,a,3);
    // A slow consumer must see a reset, never a truncated successful stream.
    std::vector<uint8_t> burst(250000,73);
    for(int i=0;i<5;++i){CHECK(a.send(client,burst.data(),burst.size()).bytes==burst.size());transfer(a,b,3);}
    CHECK(b.status(server)==LanError::reset);transfer(b,a,3);CHECK(a.status(client)==LanError::reset);
    // Host's lobby client and server on one endpoint, with no public traffic.
    LanEndpoint local(171);local.link(a_ip,1,true);auto ls=local.listen(9900);
    auto lc=local.connect(0x7f000001,9900,9000);CHECK(lc);local.tick(9000);auto rs=local.accept(ls);CHECK(rs && local.status(lc)==LanError::ok && !local.output());
    CHECK(local.send(lc,data.data(),19).bytes==19);CHECK(local.shutdown_write(lc)==LanError::ok);local.tick(9001);
    CHECK(local.receive(rs,small,19).bytes==19 && !std::memcmp(small,data.data(),19));CHECK(local.receive(rs,small,19).bytes==0);
    CHECK(local.send(rs,data.data(),19).bytes==19);local.tick(9002);CHECK(local.receive(lc,small,19).bytes==19 && !local.output());
    auto lu=local.udp(3658);CHECK(local.send_to(lu,0x7f000001,3658,nullptr,0).error==LanError::ok);local.tick(9003);
    CHECK(local.receive_from(lu,peer,port,udp).error==LanError::ok && udp.empty() && peer==a_ip && !local.output());
    // Exhaustion of the reserved control queue fails the link, never leaves
    // an apparently healthy stream whose reset could not be sent.
    for(unsigned i=1;i<=321;++i){auto refusal=packet(b_ip,4000,9999,1,i);local.input(refusal,1);}
    CHECK(local.status(lc)==LanError::reset && !local.output());
    std::puts("LAN: browser wire fixture, streams, backpressure, half-close, UDP, loopback, refusal, stale IDs, generations and bounded overflow passed");
}
