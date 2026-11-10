#pragma once
#include "xyz_xbox_remote.h"
#include <cstring>
#include <vector>
// Owned synthetic Xbox RAM for socket/session tests. Never maps host pointers.
struct XyzGuestMemory {
    std::vector<uint8_t> bytes=std::vector<uint8_t>(0x40000);
    static int read(void *v,uint32_t a,void *p,size_t n){auto &m=*static_cast<XyzGuestMemory*>(v);if(a<0x10000 || uint64_t(a)-0x10000+n>m.bytes.size())return 0;std::memcpy(p,m.bytes.data()+a-0x10000,n);return 1;}
    static int write(void *v,uint32_t a,const void *p,size_t n){auto &m=*static_cast<XyzGuestMemory*>(v);if(a<0x10000 || uint64_t(a)-0x10000+n>m.bytes.size())return 0;std::memcpy(m.bytes.data()+a-0x10000,p,n);return 1;}
    XyzXboxGuest guest(){return {this,read,write,nullptr};}
    void put(uint32_t a,uint32_t v){uint8_t b[4];for(unsigned i=0;i<4;++i)b[i]=uint8_t(v>>(8*i));write(this,a,b,4);}
    uint32_t get(uint32_t a){uint8_t b[4]={};read(this,a,b,4);return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;}
    void address(uint32_t a,uint32_t ip,uint16_t port){uint8_t b[16]={2,0,uint8_t(port>>8),uint8_t(port)};for(unsigned i=0;i<4;++i)b[4+i]=uint8_t(ip>>(24-8*i));write(this,a,b,16);}
    uint8_t *at(uint32_t a){return bytes.data()+a-0x10000;}
};
