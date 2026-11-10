#include "xyz_race_info.h"
#include "xyz_xbox_tracks.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x440000);
    uint32_t reject=0;unsigned headers=0;bool changed=false;
    XyzLobbyGame game{};XyzRaceInfo info{};
    void put(uint32_t at,uint32_t v){std::memcpy(ram.data()+at,&v,4);}
    static uint32_t hash(const char *s){uint32_t h=UINT32_MAX;for(;*s;++s)h=h*33+(unsigned char)*s;return h;}
    Fixture(){
        put(0x43d110,0x10000);put(0x43d114,1);
        std::strcpy((char*)ram.data()+0x10000,"DEBUG_FIXTURE");
        uint16_t id=4014;std::memcpy(ram.data()+0x1008a,&id,2);put(0x10094,1);ram[0x10086]=1;
        put(0x3fbc20,2);put(0x3fbc24,0x20000);put(0x3fbc28,0x18000);put(0x3fbc2c,0x17ff0);
        std::strcpy((char*)ram.data()+0x20000,"Harbor Loop");std::strcpy((char*)ram.data()+0x20080,"Harbor Reverse");
        std::array<std::array<uint32_t,2>,2> records{{{{hash("TRACK_NAME_4014"),0x20000}},{{hash("REVERSE_TRACK_NAME_4014"),0x20080}}}};
        std::sort(records.begin(),records.end());std::memcpy(ram.data()+0x18000,records.data(),sizeof records);
        std::strcpy(game.params,"TRACK=4014 DIR=0 LAPS=3");game.custom_flags=0x040000f3;
        game.min_players=2;game.max_players=6;game.players=3;
    }
    static int read(void *context,uint32_t at,void *out,size_t n){auto &f=*(Fixture*)context;
        if(at==f.reject || at>f.ram.size() || n>f.ram.size()-at)return 0;
        std::memcpy(out,f.ram.data()+at,n);
        if(at==0x3fbc20 && f.changed && f.headers++)((uint32_t*)out)[2]+=8;
        return 1;
    }
    int name(unsigned id,int reverse,char *out,size_t cap){return xyz_xbox_track_name(read,this,id,reverse,out,cap);}
    int inspect(){return xyz_xbox_race_info(read,this,&game,&info);}
};
int main(){
    char name[64];
    {Fixture f;CHECK(f.name(4014,0,name,sizeof name) && !std::strcmp(name,"Harbor Loop"));
        CHECK(f.name(4014,1,name,sizeof name) && !std::strcmp(name,"Harbor Reverse"));
        CHECK(!f.name(9999,0,name,sizeof name) && !*name);CHECK(!f.name(0,0,name,sizeof name));
        CHECK(!f.name(65536,0,name,sizeof name));CHECK(!f.name(4014,2,name,sizeof name));
        CHECK(!f.name(4014,0,name,5) && !*name);CHECK(!f.name(4014,0,name,257) && !*name);
    }
    {Fixture f;f.reject=0x20080;CHECK(f.name(4014,1,name,sizeof name) && !std::strcmp(name,"Harbor Loop"));}
    {Fixture f;f.ram[0x20003]='\n';CHECK(!f.name(4014,0,name,sizeof name) && !*name);}
    {Fixture f;std::memset(f.ram.data()+0x20000,'A',64);CHECK(!f.name(4014,0,name,sizeof name) && !*name);}
    {Fixture f;f.changed=true;CHECK(!f.name(4014,0,name,sizeof name) && !*name);}
    for(uint32_t count:{0u,65537u,UINT32_MAX}){Fixture f;f.put(0x3fbc20,count);CHECK(!f.name(4014,0,name,sizeof name));}
    {Fixture f;f.put(0x3fbc28,UINT32_MAX-4);CHECK(!f.name(4014,0,name,sizeof name));}
    {Fixture f;f.put(0x18000,Fixture::hash("TRACK_NAME_4014"));f.put(0x18008,Fixture::hash("TRACK_NAME_4014"));
        CHECK(!f.name(4014,0,name,sizeof name));}
    {Fixture f;CHECK(f.inspect() && f.info.parsed && f.info.compatible && f.info.mode==0 && f.info.track==4014 && f.info.laps==3);
        CHECK(!std::strcmp(f.info.track_name,"Harbor Loop"));
        std::strcpy(f.game.params,"  LAPS=3\nDIR=1\tTRACK=4014\r\n");
        CHECK(f.inspect() && f.info.compatible && f.info.reverse && !std::strcmp(f.info.track_name,"Harbor Reverse"));
    }
    for(const char *params:{"", "TRACK=4014 DIR=0", "TRACK=-1 DIR=0 LAPS=3", "TRACK=4014 DIR=2 LAPS=3",
        "TRACK=4014 DIR=0 LAPS=0", "TRACK=4014 DIR=0 LAPS=11", "TRACK=4014 DIR=0 LAPS=3 LAPS=3",
        "TRACK=42949672960 DIR=0 LAPS=3", "TRACK=4014x DIR=0 LAPS=3", "TRACK=4014 DIR=0 LAPS=3 OTHER=1",
        "TRACK=4014 DIR=0 LAPS=3\001", "TRACK=65536 DIR=0 LAPS=3", "TRACK=4014DIR=0 LAPS=3"}){
        Fixture f;std::strcpy(f.game.params,params);CHECK(!f.inspect() && !f.info.parsed && !f.info.compatible && !f.info.track_name[0]);
    }
    {Fixture f;std::memset(f.game.params,'A',sizeof f.game.params);CHECK(!f.inspect());}
    for(unsigned mask:{0u,0x02000000u,0x0c000000u}){Fixture f;f.game.custom_flags=mask;
        CHECK(f.inspect() && f.info.parsed && !f.info.compatible && f.info.mode==UINT32_MAX && f.info.track_name[0]);}
    for(unsigned i=0;i<6;++i){Fixture f;
        const unsigned flags[]={0x04000000,0x08000000,0x10000000,0x20000000,0x80000000,0x40000000};
        const unsigned modes[]={0,1,2,3,4,10},masks[]={1,4,2,8,0x2000,0x800},laps[]={3,1,1,3,3,4};
        f.game.custom_flags=flags[i];f.put(0x10094,masks[i]);f.game.max_players=i==2?4:6;
        std::snprintf(f.game.params,sizeof f.game.params,"TRACK=4014 DIR=0 LAPS=%u",laps[i]);
        CHECK(f.inspect() && f.info.mode==modes[i] && f.info.compatible);
    }
    for(unsigned i=0;i<5;++i){Fixture f;
        if(i==0)f.game.min_players=1;if(i==1)f.game.max_players=7;if(i==2)f.game.players=7;
        if(i==3)std::strcpy(f.game.params,"TRACK=4015 DIR=0 LAPS=3");
        if(i==4){std::strcpy(f.game.params,"TRACK=4014 DIR=1 LAPS=3");f.ram[0x10086]=0;}
        CHECK(f.inspect() && f.info.parsed && !f.info.compatible);
    }
    std::puts("Race rows: strict parameters, six native modes, local rules, real language labels, direction, malformed tables and bounded reads passed");
}
