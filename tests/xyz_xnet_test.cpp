#include "xyz_xnet.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace xyz;
struct Memory {
    std::vector<uint8_t> bytes=std::vector<uint8_t>(0x10000);uint32_t reject=0;
    static int rd(void *v,uint32_t a,void *p,size_t n){auto &m=*static_cast<Memory*>(v);if(a<0x10000 || a-0x10000+n>m.bytes.size())return 0;std::memcpy(p,m.bytes.data()+a-0x10000,n);return 1;}
    static int wr(void *v,uint32_t a,const void *p,size_t n){auto &m=*static_cast<Memory*>(v);if(a==m.reject || a<0x10000 || a-0x10000+n>m.bytes.size())return 0;std::memcpy(m.bytes.data()+a-0x10000,p,n);return 1;}
    void put(uint32_t a,uint32_t v){uint8_t b[4];for(int i=0;i<4;++i)b[i]=uint8_t(v>>(8*i));CHECK(wr(this,a,b,4));}
    uint32_t get(uint32_t a){uint8_t b[4];CHECK(rd(this,a,b,4));return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;}
    void address(uint32_t a,uint32_t ip,uint16_t port){uint8_t b[16]={2,0,uint8_t(port>>8),uint8_t(port)};for(unsigned i=0;i<4;++i)b[4+i]=uint8_t(ip>>(24-8*i));CHECK(wr(this,a,b,16));}
};
int main(){
    constexpr uint32_t vip=0x0a4d0102,sa=0x10000,sb=0x10020,buf=0x10100,out=0x10200,len=0x10040,fd=0x10050,tv=0x100a0;
    Memory m;XyzXboxGuest g{&m,Memory::rd,Memory::wr,nullptr};LanEndpoint lan(17);lan.link(vip,1,true);XnetSockets sockets(lan,vip);
    uint32_t thread=1;
    auto call=[&](XyzXnetOp op,std::initializer_list<uint32_t> args){return sockets.call(g,op,thread,args.begin(),unsigned(args.size()),1000);};
    auto error=[&]{return call(XYZ_N_ERROR,{});};
    CHECK(call(XYZ_N_SOCKET,{23,1,0})==UINT32_MAX && error()==10047);thread=2;CHECK(error()==0);thread=1;CHECK(error()==10047);
    CHECK(call(XYZ_N_SOCKET,{2,1,254})==UINT32_MAX && error()==10045);
    auto listener=call(XYZ_N_SOCKET,{2,1,0}),client=call(XYZ_N_SOCKET,{2,1,6});CHECK(listener!=UINT32_MAX && client!=UINT32_MAX);
    m.address(sa,0,9900);m.address(sb,0x7f000001,9900);
    CHECK(!call(XYZ_N_BIND,{listener,sa,16}));CHECK(!call(XYZ_N_LISTEN,{listener,6}));
    m.put(len,1);CHECK(!call(XYZ_N_IOCTL,{client,0x8004667e,len}));
    CHECK(call(XYZ_N_CONNECT,{client,sb,16})==UINT32_MAX && error()==10035);
    m.put(fd,1);m.put(fd+4,client);CHECK(call(XYZ_N_SELECT,{0,0,fd,0,tv})==0 && m.get(fd)==0);
    lan.tick(1000);m.put(fd,1);m.put(fd+4,client);CHECK(call(XYZ_N_SELECT,{0,0,fd,0,tv})==1 && m.get(fd)==1 && m.get(fd+4)==client);
    m.put(len,16);auto accepted=call(XYZ_N_ACCEPT,{listener,sb,len});CHECK(accepted!=UINT32_MAX && m.get(len)==16);
    CHECK(call(XYZ_N_ACCEPT,{listener,0,0})==UINT32_MAX && error()==10035);
    m.put(len,16);CHECK(!call(XYZ_N_PEERNAME,{client,sb,len}));CHECK(m.get(sb)==0xac260002);
    const uint8_t data[]={4,8,15,16,23,42};CHECK(Memory::wr(&m,buf,data,sizeof data));
    CHECK(call(XYZ_N_SEND,{client,buf,sizeof data,0})==sizeof data);lan.tick(1001);
    m.put(len,0);CHECK(!call(XYZ_N_IOCTL,{accepted,0x4004667f,len}) && m.get(len)==sizeof data);
    CHECK(call(XYZ_N_RECV,{accepted,out,sizeof data,2})==sizeof data);
    CHECK(call(XYZ_N_RECV,{accepted,0xfffffff0,6,0})==UINT32_MAX && error()==10014);
    m.reject=out;CHECK(call(XYZ_N_RECV,{accepted,out,6,0})==UINT32_MAX && error()==10014);m.reject=0;
    CHECK(call(XYZ_N_RECV,{accepted,out,3,0})==3 && !std::memcmp(&m.bytes[out-0x10000],data,3));
    CHECK(call(XYZ_N_RECV,{accepted,out,6,0})==3 && !std::memcmp(&m.bytes[out-0x10000],data+3,3));
    CHECK(call(XYZ_N_RECV,{accepted,out,6,0})==UINT32_MAX && error()==10035);
    CHECK(!call(XYZ_N_SHUTDOWN,{client,1}));lan.tick(1002);CHECK(call(XYZ_N_RECV,{accepted,out,6,0})==0);
    CHECK(call(XYZ_N_SEND,{client,buf,6,0})==UINT32_MAX && error()==10058);
    CHECK(call(XYZ_N_SEND,{accepted,buf,6,0})==6);lan.tick(1003);CHECK(call(XYZ_N_RECV,{client,out,6,0})==6);
    CHECK(!call(XYZ_N_CLOSE,{accepted}));CHECK(!call(XYZ_N_CLOSE,{client}));CHECK(!call(XYZ_N_CLOSE,{listener}));
    CHECK(call(XYZ_N_CLOSE,{client})==UINT32_MAX && error()==10038);
    lan.tick(1003);CHECK(!lan.output());
    // Native protocol 254 carries two zero bytes. The virtual LAN never does.
    auto udp=call(XYZ_N_SOCKET,{2,2,254});m.address(sa,0,3658);CHECK(!call(XYZ_N_BIND,{udp,sa,16}));
    auto raw=lan.udp(3660);CHECK(raw);m.address(sb,vip,3660);
    const uint8_t prefix[]={0,0,4,8,15,16,23,42};CHECK(Memory::wr(&m,buf,prefix,8));
    CHECK(call(XYZ_N_SENDTO,{udp,buf,8,0,sb,16})==8);
    CHECK(lan.output());LanPacket packet;CHECK(decode_lan(*lan.output(),packet) && packet.data==std::vector<uint8_t>(data,data+6));
    lan.tick(1004);uint32_t ip;uint16_t port;std::vector<uint8_t> datagram;CHECK(lan.receive_from(raw,ip,port,datagram).bytes==6 && port==3658);
    CHECK(lan.send_to(raw,vip,3658,data,6).bytes==6);lan.tick(1005);
    CHECK(!call(XYZ_N_IOCTL,{udp,0x4004667f,len}) && m.get(len)==8);
    m.put(len,16);CHECK(call(XYZ_N_RECVFROM,{udp,out,8,2,sa,len})==8 && m.get(len)==16);
    CHECK(!std::memcmp(&m.bytes[out-0x10000],prefix,8));
    CHECK(call(XYZ_N_RECVFROM,{udp,out,4,0,sa,len})==UINT32_MAX && error()==10040);
    CHECK(call(XYZ_N_RECV,{udp,out,8,0})==UINT32_MAX && error()==10035);
    // A connected UDP socket accepts only its selected peer/port.
    CHECK(!call(XYZ_N_CONNECT,{udp,sb,16}));auto wrong=lan.udp(3661);CHECK(lan.send_to(wrong,vip,3658,data,6).bytes==6);
    CHECK(lan.send_to(raw,vip,3658,data,6).bytes==6);lan.tick(1006);
    CHECK(call(XYZ_N_RECV,{udp,out,8,0})==8);CHECK(call(XYZ_N_RECV,{udp,out,8,0})==UINT32_MAX && error()==10035);
    CHECK(!call(XYZ_N_SHUTDOWN,{udp,2}));CHECK(call(XYZ_N_SEND,{udp,buf,8,0})==UINT32_MAX && error()==10058);
    CHECK(call(XYZ_N_RECV,{udp,out,8,0})==UINT32_MAX && error()==10058);CHECK(!call(XYZ_N_CLOSE,{udp}));
    m.put(fd,65);CHECK(call(XYZ_N_SELECT,{0,fd,0,0,tv})==UINT32_MAX && error()==10014);
    m.put(fd,1);m.put(fd+4,0x6effffff);CHECK(call(XYZ_N_SELECT,{0,fd,0,0,tv})==UINT32_MAX && error()==10038);
    m.put(tv,1);CHECK(call(XYZ_N_SELECT,{0,fd,0,0,tv})==UINT32_MAX && error()==10045);
    // Runtime dispatch preserves retail handles and last-error ownership.
    // Once a virtual handle is exposed, reset/close must never send it back
    // into the Xbox implementation, including after the session is disabled.
    {
        LanEndpoint session_lan(123);XnetSockets session(session_lan,0);uint32_t r=0;
        auto dispatch=[&](XyzXnetOp op,std::initializer_list<uint32_t> a,bool enabled=true){return session.dispatch(g,op,7,a.begin(),unsigned(a.size()),2000,enabled,r);};
        CHECK(!dispatch(XYZ_N_SOCKET,{2,2,0},false));
        CHECK(dispatch(XYZ_N_SOCKET,{2,2,0}));auto old=r;CHECK(old!=UINT32_MAX);
        session.link(vip,10);m.address(sa,0,3658);CHECK(dispatch(XYZ_N_BIND,{old,sa,16}) && r==0);
        CHECK(!dispatch(XYZ_N_CLOSE,{0x123400}));CHECK(!dispatch(XYZ_N_ERROR,{}));
        CHECK(dispatch(XYZ_N_SEND,{old,buf,8,0}) && r==UINT32_MAX);
        CHECK(dispatch(XYZ_N_ERROR,{}) && r==10057);
        m.put(tv,0);m.put(fd,2);m.put(fd+4,old);m.put(fd+8,0x123400);
        CHECK(dispatch(XYZ_N_SELECT,{0,0,fd,0,tv}) && r==UINT32_MAX);
        CHECK(dispatch(XYZ_N_ERROR,{}) && r==10022 && m.get(fd)==2);
        session.link(vip,11); // same address, different service generation
        CHECK(dispatch(XYZ_N_RECV,{old,out,8,0}) && r==UINT32_MAX);
        CHECK(dispatch(XYZ_N_ERROR,{}) && r==10054);
        CHECK(dispatch(XYZ_N_SOCKET,{2,2,0}));auto fresh=r;CHECK(fresh!=old);
        CHECK(dispatch(XYZ_N_BIND,{fresh,sa,16}) && r==0); // old port released
        session.reset();CHECK(dispatch(XYZ_N_CLOSE,{old},false) && r==0);
        CHECK(dispatch(XYZ_N_CLOSE,{old},false) && r==UINT32_MAX);
        CHECK(dispatch(XYZ_N_ERROR,{},false) && r==10038);
        CHECK(dispatch(XYZ_N_CLOSE,{fresh},false) && r==0);
        CHECK(!dispatch(XYZ_N_SOCKET,{2,2,0},false));CHECK(!dispatch(XYZ_N_ERROR,{},false));
    }
    // No default hook and no cross-thread capture during the guest probe.
    uint32_t result=0,args[3]={2,1,0};CHECK(!xyz_xnet_dispatch(&g,XYZ_N_SOCKET,1,args,3,0,&result));
    CHECK(xyz_xnet_probe_begin(1));CHECK(!xyz_xnet_dispatch(&g,XYZ_N_SOCKET,2,args,3,0,&result));
    CHECK(xyz_xnet_dispatch(&g,XYZ_N_SOCKET,1,args,3,0,&result) && result!=UINT32_MAX);xyz_xnet_probe_end();
    std::puts("XNet: guest memory, per-thread errors, ABI, readiness, peek, half-close, UDP254, session generations and retail-handle isolation passed");
}
