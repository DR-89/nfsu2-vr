#include "xyz_xbox_race.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    static constexpr uint32_t manager=0x10000,proto=0x20000,games=0x30000,arena=0x50000,net=0x456290;
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x480000);
    XyzXboxGuest memory{this,read,write,base_call};XyzLobbyGuest guest{&memory,lobby_call};
    XyzRaceGuest race_guest{&guest,race_call};XyzXboxLobby lobby{};XyzXboxRace race{};XyzXboxFrame frame{};
    uint32_t reject_read=0,reject_write=0;int reject_op=-1,incomplete_reset=0,drops=0,frees=0;
    std::vector<XyzRaceOp> calls;std::array<uint8_t,0x120> baseline{};std::array<uint8_t,12> profile{};
    uint8_t *at(uint32_t a){return ram.data()+a;}
    void put(uint32_t a,uint32_t v){std::memcpy(at(a),&v,4);}
    uint32_t get(uint32_t a){uint32_t v;std::memcpy(&v,at(a),4);return v;}
    Fixture(bool host=true,unsigned players=2){
        frame.status=XYZ_XBOX_FRONTEND;frame.flow=3;
        lobby.initialized=lobby.play_received=1;lobby.stage=XYZ_LOBBY_PLAY_READY;lobby.generation=7;
        lobby.manager=manager;lobby.protocol=proto;lobby.games=games;lobby.arena=arena;
        std::strcpy(lobby.name,host?"Host":"Guest");put(manager+0x44,proto);
        put(games+0x1c,51);put(games+0x1d8,players);put(games+0x13c,42);put(games+0x1f0,0x0a4d0203);
        std::strcpy((char*)at(games+0x54),"Host");
        for(unsigned i=0;i<players;++i)std::snprintf((char*)at(games+0x1e0+0x64*i),16,i==0?"Host":i==1?"Guest":"Player%u",i);
        put(0x3a5320,players-1);put(0x42a48c,host?1:0);put(0x41c338,2);put(0x406c98,2);
        std::memset(at(0x427ff8),255,4);at(0x427ffa)[0]=65;
        for(unsigned i=0x24;i<baseline.size();++i)baseline[i]=(uint8_t)i;
        std::memcpy(at(net),baseline.data(),baseline.size());at(0x3a53bc)[0]=17;
        for(unsigned i=0;i<profile.size();++i)profile[i]=(uint8_t)(i+50);
        std::memcpy(at(0x41c38c),profile.data(),profile.size());
    }
    static int read(void *v,uint32_t a,void *out,size_t n){auto &f=*(Fixture*)v;
        if(f.reject_read==a || a>f.ram.size() || n>f.ram.size()-a)return 0;std::memcpy(out,f.at(a),n);return 1;}
    static int write(void *v,uint32_t a,const void *in,size_t n){auto &f=*(Fixture*)v;
        if(f.reject_write==a){f.reject_write=0;return 0;}if(a>f.ram.size() || n>f.ram.size()-a)return 0;std::memcpy(f.at(a),in,n);return 1;}
    static int base_call(void *v,XyzXboxOp op,uint32_t,uint32_t,unsigned n,const uint32_t *a,uint32_t *out){
        auto &f=*(Fixture*)v;CHECK(op==XYZ_G_FREE && n==1 && *a==arena);++f.frees;*out=0;return 1;}
    static int lobby_call(void *v,XyzLobbyOp op,uint32_t self,unsigned n,const uint32_t *a,uint32_t *out){
        auto &f=*(Fixture*)v;CHECK(op==XYZ_L_DROP && !self && n==1 && !*a);++f.drops;f.put(manager+0x44,0);*out=0;return 1;}
    static int race_call(void *v,XyzRaceOp op,uint32_t self,unsigned n,const uint32_t *a,uint32_t *out){
        auto &f=*(Fixture*)v;*out=0;if(f.reject_op==int(op))return 0;f.calls.push_back(op);
        CHECK(self==(op==XYZ_R_PROFILE?0x41c340:op==XYZ_R_RETURN?0x428e10:op==XYZ_R_RELEASE_PLAY || op==XYZ_R_RETURN_SCREEN?0:net));
        switch(op){
        case XYZ_R_NAMES:CHECK(n==1);std::memcpy(f.at(net+0x64),f.at(*a),96);break;
        case XYZ_R_BEGIN:CHECK(!n);f.put(net,1);break;
        case XYZ_R_CONFIGURE:CHECK(n==1 && *a==f.race.players && f.lobby.initialized && f.get(net)==1);break;
        case XYZ_R_PROFILE:CHECK(n==1 && *a==3 && f.lobby.stage==XYZ_LOBBY_RACE && f.drops==1 && f.frees==1);
            f.at(0x41c38c)[0]=0;f.put(0x41c394,f.get(0x41c390));f.put(0x41c390,*a);break;
        case XYZ_R_INIT:CHECK(!n && f.get(net)==1 && f.get(0x41c338)==8 && f.get(0x41c390)==3);
            f.put(net,2);f.put(net+4,0x70000);f.at(net+0x20)[0]=(uint8_t)f.race.players;
            for(unsigned i=0;i<f.race.players;++i)f.put(net+8+i*4,0x70000+0x100*i);
            std::strcpy((char*)f.at(0x42cd7c),"10.77.2.3");f.put(0x42cd98,3658);f.at(0x42cd90)[0]=1;break;
        case XYZ_R_STOP:CHECK(!n);break;
        case XYZ_R_RESET:CHECK(n==1 && *a==1);if(!f.incomplete_reset)std::memset(f.at(net),0,0x22);break;
        case XYZ_R_RETURN:CHECK(!n && !f.get(0x428e14) && f.get(0x3fbd0c)==0x34c7a0);f.put(0x428e14,0x18b1a0);break;
        case XYZ_R_RETURN_SCREEN:CHECK(n==1 && *a==0x34c7a0);f.put(0x3fbd0c,*a);break;
        case XYZ_R_RELEASE_PLAY:CHECK(!n && !f.get(net+4));break;
        default:CHECK(false);
        }return 1;
    }
    bool begin(){return xyz_xbox_race_begin(&race_guest,&race,&lobby,&frame)!=0;}
    bool close(){return xyz_xbox_race_close(&race_guest,&race,&frame)!=0;}
    void world(){frame.flow=6;frame.status=XYZ_XBOX_OTHER_RACE;frame.world=0x80000;frame.car=0x81000;put(0x4493e0,0x80000);put(0x44940c,0x81000);}
    void frontend(){frame.flow=3;frame.status=XYZ_XBOX_FRONTEND;frame.world=frame.car=0;put(0x4493e0,0);put(0x44940c,0);put(0x428e14,0);}
    void restored(){CHECK(!std::memcmp(at(net),baseline.data(),baseline.size()) && get(0x41c338)==2 && at(0x3a53bc)[0]==17);
        CHECK(!std::memcmp(at(0x41c38c),profile.data(),profile.size()));}
};
int main(){
    {Fixture f;CHECK(f.begin());f.world();CHECK(xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame));f.frontend();f.put(0x41c338,2);
        CHECK(!f.close());std::memset(f.at(Fixture::net),0,0x22);CHECK(f.close() && f.get(0x41c338)==2);f.restored();}
    {Fixture f;CHECK(f.begin());f.world();CHECK(xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame));f.frontend();f.put(0x41c338,2);
        std::memset(f.at(Fixture::net),0,0x22);f.put(0x3fbd0c,0x23456);CHECK(!f.close());}
    {Fixture f;f.put(0x3fbd0c,0x12345);CHECK(f.begin());f.world();f.reject_op=XYZ_R_RETURN_SCREEN;
        CHECK(!xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame) && !f.get(0x428e14));f.reject_op=-1;
        CHECK(xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame) && f.get(0x3fbd0c)==0x34c7a0);
        f.frontend();f.reject_read=0x3fbd0c;CHECK(!f.close() && f.get(0x41c338)==8);f.reject_read=0;
        CHECK(f.close() && f.get(0x3fbd0c)==0x12345);f.restored();}
    {Fixture f;CHECK(f.begin());f.world();CHECK(xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame));
        f.put(0x3fbd0c,0x23456);f.frontend();CHECK(f.close() && f.get(0x3fbd0c)==0x23456);}
    {Fixture f;CHECK(f.begin());f.world();f.frame.flow=5;f.frame.status=XYZ_XBOX_FRONTEND;
        auto calls=f.calls.size();CHECK(!xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame) && f.calls.size()==calls && !f.get(0x428e14));
        f.frame.flow=4;CHECK(!xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame));
        f.frame.flow=6;f.frame.car=0;CHECK(!xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame));f.frontend();CHECK(f.close());}
    for(bool host:{false,true})for(unsigned players:{2u,6u}){Fixture f(host,players);
        CHECK(f.begin() && f.race.stage==XYZ_RACE_ACTIVE && f.race.players==players && f.race.host==(unsigned)host);
        CHECK(f.race.generation==7 && f.lobby.stage==XYZ_LOBBY_RACE && !f.lobby.arena && f.get(Fixture::net+0xc4)==42);
        CHECK(!std::strcmp((char*)f.at(Fixture::net+0x64),"Host") && !std::strcmp((char*)f.at(Fixture::net+0x74),"Guest"));
        CHECK(f.calls==std::vector<XyzRaceOp>({XYZ_R_NAMES,XYZ_R_BEGIN,XYZ_R_CONFIGURE,XYZ_R_PROFILE,XYZ_R_INIT}));
        if(!host)CHECK(f.get(0x42cd94)==0x0a4d0203 && !f.at(0x42cd90)[0]);
        CHECK(!f.begin());f.world();const auto before=f.ram;CHECK(!f.close() && f.ram==before);
        f.put(0x428e14,123);CHECK(!xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame) && f.get(0x428e14)==123);
        f.put(0x428e14,0);CHECK(xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame) && f.get(0x428e14)==0x18b1a0);
        const auto count=f.calls.size();CHECK(xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame) && f.calls.size()==count);
        CHECK(!f.close());f.frontend();CHECK(f.close() && f.race.stage==XYZ_RACE_CLOSED && !f.race.captured);f.restored();
        CHECK(f.calls[f.calls.size()-3]==XYZ_R_STOP && f.calls[f.calls.size()-2]==XYZ_R_RESET && f.calls.back()==XYZ_R_RELEASE_PLAY);
        const auto done=f.calls.size();CHECK(f.close() && f.calls.size()==done);
    }
    for(uint32_t address:{0x3fbbf4u,0x3fbbfcu,0x428e14u,0x4493e0u,0x44940cu,0x456290u,0x456294u,0x456298u,0x4562acu}){
        Fixture f;f.put(address,1);const auto before=f.ram;CHECK(!f.begin() && f.calls.empty() && f.ram==before && !f.race.captured);}
    for(uint32_t address:{Fixture::manager+0x44,0x3a5320u,0x42a48cu,0x406c98u,0x427ffau,Fixture::games+0x13c,0x41c338u,0x41c38cu,0x3a53bcu}){
        Fixture f;f.reject_read=address;const auto before=f.ram;CHECK(!f.begin() && f.calls.empty() && f.ram==before && !f.race.captured);}
    for(auto change:std::array<std::array<uint32_t,2>,6>{{{{Fixture::manager+0x44,123}},{{0x3a5320,0}},{{0x42a48c,0}},{{0x406c98,4}},{{Fixture::games+0x1d8,7}},{{Fixture::games+0x1d8,1}}}}){
        Fixture f;f.put(change[0],change[1]);CHECK(!f.begin() && f.calls.empty() && !f.race.captured);}
    {Fixture f;f.at(0x427ffa)[0]=255;CHECK(!f.begin() && f.calls.empty());}
    {Fixture f;std::strcpy((char*)f.at(Fixture::games+0x244),"Host");CHECK(!f.begin() && f.calls.empty());}
    {Fixture f(false);std::strcpy(f.lobby.name,"Unknown");CHECK(!f.begin() && f.calls.empty());}
    for(XyzRaceOp op:{XYZ_R_NAMES,XYZ_R_BEGIN,XYZ_R_CONFIGURE,XYZ_R_PROFILE,XYZ_R_INIT}){
        Fixture f;f.reject_op=op;CHECK(!f.begin() && f.race.captured && f.race.error==XYZ_LOBBY_RETAIL);
        f.reject_op=-1;CHECK(f.close());f.restored();
    }
    for(uint32_t address:{Fixture::arena+0x2800,Fixture::net+0xc4,0x41c338u}){
        Fixture f;f.reject_write=address;CHECK(!f.begin() && f.race.captured && f.race.error==XYZ_LOBBY_MEMORY);
        CHECK(f.close());f.restored();
    }
    {Fixture f;CHECK(f.begin());f.put(0x41c338,2);const auto before=f.ram;CHECK(!f.close() && before==f.ram);
        f.put(0x41c338,8);f.incomplete_reset=1;CHECK(!f.close() && f.race.begun && f.get(Fixture::net+4)==0x70000);
        f.incomplete_reset=0;CHECK(f.close());f.restored();}
    {Fixture f;CHECK(f.begin());f.put(0x41c390,9);CHECK(f.close() && f.get(0x41c390)==9);}
    for(uint32_t address:{Fixture::net,0x3a53bcu,0x41c38cu,0x41c338u}){
        Fixture f;CHECK(f.begin());f.reject_write=address;CHECK(!f.close() && f.race.captured);CHECK(f.close());f.restored();}
    for(XyzRaceOp op:{XYZ_R_STOP,XYZ_R_RESET,XYZ_R_RELEASE_PLAY}){Fixture f;CHECK(f.begin());f.reject_op=op;CHECK(!f.close() && f.race.captured);
        f.reject_op=-1;CHECK(f.close());f.restored();}
    {Fixture f(false);f.reject_read=0x42cd7c;CHECK(!f.begin() && f.race.coordinator && f.race.stage==XYZ_RACE_PREPARING);
        f.reject_read=0;f.world();CHECK(!f.close());CHECK(xyz_xbox_race_return(&f.race_guest,&f.race,&f.frame));
        f.frontend();CHECK(f.close());f.restored();}
    {Fixture f;CHECK(f.begin());f.reject_read=0x41c38c;CHECK(!f.close() && f.race.captured);
        f.reject_read=0;CHECK(f.close());f.restored();}
    std::puts("Xbox race handoff: owned roster, retail ordering, deferred return, partial failures and conditional restoration passed");
}
