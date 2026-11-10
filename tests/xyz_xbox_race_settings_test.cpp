#include "xyz_xbox_race_settings.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    static constexpr uint32_t profile=0x41c340,manager=0x20000,catalog=0x10000;
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x480000);
    XyzXboxGuest memory{this,read,write,nullptr};XyzLobbyGuest guest{&memory,nullptr};
    XyzXboxLobby lobby{};XyzRaceSettingsLease lease{};XyzXboxFrame frame{};
    XyzRaceSettings options{0,4014,3,2,6,1,1,1,0,0,1,0,"Private_12"};
    std::array<uint8_t,0x4c> before{};uint32_t reject_read=0,reject_write=0;unsigned writes=0;
    void put(uint32_t at,uint32_t v){std::memcpy(ram.data()+at,&v,4);}
    uint32_t word(uint32_t at){uint32_t v;std::memcpy(&v,ram.data()+at,4);return v;}
    uint16_t short_word(uint32_t at){uint16_t v;std::memcpy(&v,ram.data()+at,2);return v;}
    void track(unsigned i,unsigned id,unsigned mask,unsigned reverse=1,unsigned point=0){
        uint8_t *p=ram.data()+catalog+i*0x128;std::strcpy((char*)p,"track fixture");
        uint16_t small=(uint16_t)id;std::memcpy(p+0x8a,&small,2);std::memcpy(p+0x94,&mask,4);
        p[0x86]=(uint8_t)reverse;std::memcpy(p+0x80,&point,4);
    }
    Fixture(){
        lobby.stage=XYZ_LOBBY_READY;lobby.initialized=1;lobby.manager=manager;lobby.protocol=0x30000;lobby.generation=5;
        put(manager+0x44,lobby.protocol);frame.status=XYZ_XBOX_FRONTEND;frame.flow=3;
        put(0x43d110,catalog);put(0x43d114,7);
        track(0,4014,1);track(1,4101,4);track(2,4402,2);track(3,4174,8,0,1);
        track(4,4201,8);track(5,4501,0x800);track(6,4701,0x2000);
        for(unsigned i=0;i<before.size();++i)before[i]=(uint8_t)(i+100);
        std::memcpy(ram.data()+profile,before.data(),before.size());
    }
    static int read(void *context,uint32_t at,void *out,size_t n){auto &f=*(Fixture*)context;
        if(at==f.reject_read || at>f.ram.size() || n>f.ram.size()-at)return 0;
        std::memcpy(out,f.ram.data()+at,n);return 1;}
    static int write(void *context,uint32_t at,const void *in,size_t n){auto &f=*(Fixture*)context;
        if(at==f.reject_write || at>f.ram.size() || n>f.ram.size()-at)return 0;
        std::memcpy(f.ram.data()+at,in,n);++f.writes;return 1;}
    int valid(){return xyz_xbox_race_settings_valid(&guest,&options);}
    int apply(){return xyz_xbox_race_settings_apply(&guest,&lobby,&lease,&frame,&options);}
    int release(){return xyz_xbox_race_settings_release(&guest,&lease,&frame);}
    void restored(){CHECK(!std::memcmp(ram.data()+profile,before.data(),before.size()));
        XyzRaceSettingsLease empty{};CHECK(!std::memcmp(&lease,&empty,sizeof empty));}
};
int main(){
    {Fixture f;CHECK(xyz_xbox_race_settings_join(&f.guest,&f.lobby,&f.lease,&f.frame,&f.options));
        CHECK(!f.writes && !std::memcmp(f.ram.data()+f.profile,f.before.data(),f.before.size()));
        // Retail Join writes only these seven fields after accepting the reply.
        for(auto field:std::vector<std::pair<unsigned,unsigned>>{{8,4},{0x18,1},{0x19,1},{0x1a,1},{0x38,2},{0x3a,2},{0x3c,4}})
            std::memcpy(f.ram.data()+f.profile+field.first,f.lease.written+field.first,field.second);
        CHECK(f.release());f.restored();
    }
    {Fixture f;CHECK(xyz_xbox_race_settings_join(&f.guest,&f.lobby,&f.lease,&f.frame,&f.options));
        CHECK(f.release());f.restored();} // Rejected reply never changed the profile.
    {Fixture f;CHECK(f.valid() && f.apply() && f.lease.captured && f.writes==1);
        CHECK(f.lease.generation==5 && f.lease.protocol==f.lobby.protocol);
        CHECK(f.ram[f.profile]==6 && f.ram[f.profile+1]==2 && f.word(f.profile+4)==0 && f.word(f.profile+8)==0);
        CHECK(f.short_word(f.profile+0x38)==4014 && f.short_word(f.profile+0x3a)==3 && f.word(f.profile+0x3c)==1);
        CHECK(!std::strcmp((char*)f.ram.data()+f.profile+0x1c,"Private_12"));
        CHECK(!std::strcmp((char*)f.ram.data()+f.profile+0x29,"Private_12"));
        for(unsigned i:{2u,3u,0xdu,0x10u,0x17u,0x40u,0x47u,0x49u,0x4bu})CHECK(f.ram[f.profile+i]==f.before[i]);
        const auto snapshot=f.ram;CHECK(!f.apply() && snapshot==f.ram);
        CHECK(f.release());f.restored();unsigned writes=f.writes;CHECK(f.release() && f.writes==writes);
    }
    const unsigned modes[]={0,1,2,3,3,10,4},ids[]={4014,4101,4402,4174,4201,4501,4701},laps[]={2,1,1,1,2,4,2};
    for(unsigned i=0;i<7;++i){Fixture f;f.options.mode=modes[i];f.options.track=ids[i];f.options.laps=laps[i];
        f.options.max_players=i==2?3:6;f.options.reverse=i==3?0:1;f.options.catchup=modes[i]==3 || modes[i]==10?0:1;
        CHECK(f.valid() && f.apply() && f.release());f.restored();
        f.options.laps=laps[i]-1;CHECK(!f.valid());
    }
    for(unsigned i=0;i<22;++i){Fixture f;
        switch(i){
        case 0:f.options.mode=8;break;case 1:f.options.track=9999;break;case 2:f.options.track=65536;break;
        case 3:f.options.min_players=1;break;case 4:f.options.min_players=7;break;case 5:f.options.max_players=7;break;
        case 6:f.options.reverse=2;break;case 7:f.options.collision=2;break;case 8:f.options.catchup=2;break;
        case 9:f.options.ranked=2;break;case 10:f.options.private_game=2;break;case 11:f.options.unrestricted=2;break;
        case 12:f.options.performance=4;break;case 13:f.options.laps=11;break;
        case 14:std::memset(f.options.password,'A',13);break;case 15:std::strcpy(f.options.password,"A\nB");break;
        case 16:std::strcpy(f.options.password,"a=b");break;case 17:f.options.mode=2;f.options.track=4402;f.options.laps=1;break;
        case 18:f.options.mode=3;f.options.track=4174;f.options.laps=1;f.options.catchup=0;break;
        case 19:f.options.mode=10;f.options.track=4501;f.options.laps=4;break;
        case 20:f.options.mode=3;f.options.track=4201;break;case 21:f.options.track=0;break;
        }
        auto original=f.ram;CHECK(!f.valid() && !f.apply() && !f.lease.captured && original==f.ram && !f.writes);
    }
    for(uint32_t gate:{0x3fbbf4u,0x3fbbfcu,0x428e14u,0x4493e0u,0x44940cu,0x456290u,0x456294u,0x4562acu,0x4562b0u}){
        Fixture f;f.put(gate,1);auto original=f.ram;CHECK(!f.apply() && original==f.ram && !f.writes);
    }
    {Fixture f;f.lobby.stage=XYZ_LOBBY_IN_GAME;CHECK(!f.apply());}
    {Fixture f;f.lobby.initialized=0;CHECK(!f.apply());}
    {Fixture f;f.lobby.generation=0;CHECK(!f.apply());}
    {Fixture f;f.put(f.manager+0x44,123);CHECK(!f.apply());}
    {Fixture f;f.frame.flow=6;CHECK(!f.apply());}
    {Fixture f;f.reject_read=f.profile;CHECK(!f.apply() && !f.lease.captured);}
    {Fixture f;f.reject_write=f.profile;CHECK(!f.apply() && f.lease.captured);
        f.reject_write=0;CHECK(f.release());f.restored();}
    {Fixture f;CHECK(f.apply());f.frame.flow=6;auto during=f.ram;CHECK(!f.release() && during==f.ram && f.lease.captured);
        f.frame.flow=3;CHECK(f.release());f.restored();}
    for(bool write:{false,true}){Fixture f;CHECK(f.apply());
        (write?f.reject_write:f.reject_read)=f.profile+0x38;CHECK(!f.release() && f.lease.captured && f.lease.remaining);
        CHECK(!std::memcmp(f.ram.data()+f.profile,f.before.data(),2));
        f.reject_write=f.reject_read=0;CHECK(f.release());f.restored();
    }
    {Fixture f;CHECK(f.apply());f.put(f.profile+8,10);f.put(f.profile+0x40,0x12345678);
        std::strcpy((char*)f.ram.data()+f.profile+0x1c,"OtherOwner");CHECK(f.release());
        CHECK(f.word(f.profile+8)==10 && f.word(f.profile+0x40)==0x12345678);
        CHECK(!std::strcmp((char*)f.ram.data()+f.profile+0x1c,"OtherOwner"));
        CHECK(f.short_word(f.profile+0x38)==(uint16_t)(f.before[0x38]|f.before[0x39]<<8));
    }
    {Fixture f;f.options.password[0]=0;CHECK(f.apply() && !f.ram[f.profile+0x1b]);
        for(unsigned i=0x1c;i<0x38;++i)CHECK(!f.ram[f.profile+i]);CHECK(f.release());f.restored();}
    std::puts("Race settings: stock track rules, frontend ownership, exact retail layout, independent restoration, failures and secret clearing passed");
}
