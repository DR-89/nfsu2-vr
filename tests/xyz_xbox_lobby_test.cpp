#include "xyz_xbox_lobby.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    static constexpr uint32_t manager=0x10000,proto=0x20000,games=0x30000,list=0x38000,arena=0x50000,profile=0x41c340,event=0x40000;
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x480000);
    XyzXboxGuest memory{this,read,write,base_call};XyzLobbyGuest guest{&memory,call};
    XyzXboxLobby state{};XyzXboxFrame frame{};
    int allocations=0,frees=0,inits=0,drops=0,joins=0,updates=0,creates=0,starts=0,idles=0,searches=0,play_releases=0;
    uint32_t sent=0,ready=2;uint32_t reject_write=0,reject_read=0;
    std::string body;std::array<std::array<char,16>,4> original{};
    static constexpr uint32_t offsets[4]={0x58,0x6c,0x94,0xa8};
    Fixture(){frame.status=XYZ_XBOX_FRONTEND;frame.flow=3;
        for(unsigned i=0;i<4;++i){std::snprintf(original[i].data(),16,"Previous_%u",i);std::memcpy(at(profile+offsets[i]),original[i].data(),16);}}
    uint8_t *at(uint32_t a){return ram.data()+a;}
    void put(uint32_t a,uint32_t v){std::memcpy(at(a),&v,4);}
    uint32_t get(uint32_t a){uint32_t v;std::memcpy(&v,at(a),4);return v;}
    static int read(void *v,uint32_t a,void *out,size_t n){auto &f=*static_cast<Fixture*>(v);if(f.reject_read==a || a>f.ram.size() || n>f.ram.size()-a)return 0;std::memcpy(out,f.at(a),n);return 1;}
    static int write(void *v,uint32_t a,const void *in,size_t n){auto &f=*static_cast<Fixture*>(v);if(f.reject_write==a){f.reject_write=0;return 0;}if(a>f.ram.size() || n>f.ram.size()-a)return 0;std::memcpy(f.at(a),in,n);return 1;}
    static int base_call(void *v,XyzXboxOp op,uint32_t,uint32_t,unsigned n,const uint32_t *a,uint32_t *out){
        auto &f=*static_cast<Fixture*>(v);CHECK(n==1);
        if(op==XYZ_G_ALLOC){CHECK(*a==0x3000);++f.allocations;*out=arena;return 1;}
        CHECK(op==XYZ_G_FREE && *a==arena);++f.frees;*out=0;return 1;
    }
    static int call(void *v,XyzLobbyOp op,uint32_t self,unsigned n,const uint32_t *a,uint32_t *out){
        auto &f=*static_cast<Fixture*>(v);*out=0;
        switch(op){
        case XYZ_L_MANAGER:*out=manager;break;
        case XYZ_L_INIT:++f.inits;f.put(manager+0x44,proto);f.put(games+0x1c,UINT32_MAX);f.put(games+8,list);break;
        case XYZ_L_DROP:CHECK(n==1 && *a==0);++f.drops;f.put(manager+0x44,0);break;
        case XYZ_L_RELEASE_PLAY:CHECK(!self && !n);++f.play_releases;break;
        case XYZ_L_GAMES:*out=games;break;
        case XYZ_L_CONNECT:CHECK(n==6 && a[0]==proto && a[2]==20920);CHECK(!std::strcmp((char*)f.at(a[1]),"10.77.0.1"));f.put(proto+8,0x69646c65);break;
        case XYZ_L_UPDATE:CHECK(self==manager);++f.updates;break;
        case XYZ_L_SOCKET_IDLE:CHECK(!self && n==1 && *a==0x46d6d0);++f.idles;break;
        case XYZ_L_REQUEST:CHECK(n==5 && a[0]==proto && a[4]==arena);f.sent=a[1];f.body=(char*)f.at(a[2]);*out=3;break;
        case XYZ_L_FIND:{std::string text=(char*)f.at(a[0]),key=(char*)f.at(a[1]);auto pos=text.find(key+'=');
            while(pos!=std::string::npos && pos && text[pos-1]!=' ' && text[pos-1]!='\n')pos=text.find(key+'=',pos+1);
            if(pos!=std::string::npos)*out=a[0]+uint32_t(pos+key.size()+1);break;}
        case XYZ_L_STRING:{CHECK(n==4);std::string value=(char*)f.at(a[0]);auto end=value.find_first_of(" \r\n");if(end!=std::string::npos)value.resize(end);std::memset(f.at(a[1]),0,a[2]);std::memcpy(f.at(a[1]),value.data(),value.size()<a[2]?value.size():a[2]-1);break;}
        case XYZ_L_JOIN:CHECK(self==games && n==4 && !a[2] && !a[3]);++f.joins;*out=7;break;
        case XYZ_L_SEARCH:
            CHECK(self==games && n==4 && !a[1] && !a[2] && !a[3]);
            CHECK(f.get(a[0]+8)==1 && f.get(a[0]+12)==UINT32_MAX && !f.get(a[0]+16) && !f.get(a[0]+20));
            ++f.searches;f.put(list+0x14,0);f.put(list+0x18,0);*out=10;break;
        case XYZ_L_CREATE:CHECK(self==games && n==5 && a[1]==profile && !a[3] && !a[4]);++f.creates;*out=8;break;
        case XYZ_L_START:CHECK(self==games && n==3 && !a[0] && !a[1] && !a[2]);++f.starts;*out=9;break;
        case XYZ_L_READY:CHECK(self==games && n==1);f.ready=*a;break;
        case XYZ_L_LEAVE:CHECK(self==games && n==2);break;
        default:CHECK(false);
        }return 1;
    }
    bool begin(){return xyz_xbox_lobby_begin(&guest,&state,&frame,"Alpha",3,0x196390,100);}
    void tick(uint64_t now=200,uint32_t generation=3){xyz_xbox_lobby_tick(&guest,&state,generation,now);}
    void reply(uint32_t kind,const char *text,uint32_t code=0){
        put(event,0);put(event+4,0);put(event+8,kind);put(event+12,code);put(event+16,event+32);
        std::strcpy((char*)at(event+32),text);xyz_xbox_lobby_event(&guest,&state,event);
    }
    void login(const char *persona="Alpha1234"){
        CHECK(begin());tick();CHECK(state.stage==XYZ_LOBBY_AUTH && sent==0x61757468 && body=="NAME=Alpha");
        reply(0x61757468,"NAME=Alpha");tick();CHECK(state.stage==XYZ_LOBBY_PERSONA && sent==0x70657273);
        reply(0x70657273,(std::string("PERS=")+persona+" LKEY=temporary").c_str());tick();
    }
    void names_restored(){for(unsigned i=0;i<4;++i)CHECK(!std::memcmp(at(profile+offsets[i]),original[i].data(),16));}
    void play(){
        login();state.stage=XYZ_LOBBY_PLAY_READY;state.play_received=1;
        put(games+0x1c,51);put(games+0x1d8,2);put(games+0x1f0,0x0a4d0221);
        std::strcpy((char*)at(games+0x54),"Host");std::strcpy((char*)at(games+0x1e0),"Host");
        std::strcpy((char*)at(games+0x244),state.name);
    }
};
int main(){
    {Fixture f;XyzLobbyGame game{};CHECK(!xyz_xbox_lobby_current_game(&f.guest,&f.state,&game));
        f.play();std::strcpy((char*)f.at(Fixture::games+0x20),"TestHost");std::strcpy((char*)f.at(Fixture::games+0xa8),"TRACK=4014 DIR=0 LAPS=3");
        f.put(Fixture::games+0x12c,0x040000f3);f.put(Fixture::games+0x134,2);f.put(Fixture::games+0x138,6);
        auto before=f.ram;CHECK(xyz_xbox_lobby_current_game(&f.guest,&f.state,&game) && game.game_id==51 && game.players==2 && game.max_players==6);
        CHECK(!std::strcmp(game.name,"TestHost") && !std::strcmp(game.host,"Host") && f.ram==before);
        f.reject_read=Fixture::games+0x1c;CHECK(!xyz_xbox_lobby_current_game(&f.guest,&f.state,&game) && game.game_id==-1 && !game.name[0]);f.reject_read=0;
        f.put(Fixture::manager+0x44,Fixture::proto+4);CHECK(!xyz_xbox_lobby_current_game(&f.guest,&f.state,&game));f.put(Fixture::manager+0x44,Fixture::proto);
        f.state.stage=XYZ_LOBBY_READY;CHECK(!xyz_xbox_lobby_current_game(&f.guest,&f.state,&game));
    }
    {Fixture f;f.play();xyz_xbox_lobby_close(&f.guest,&f.state);CHECK(f.play_releases==1 && f.drops==1);
        xyz_xbox_lobby_close(&f.guest,&f.state);CHECK(f.play_releases==1);}
    {Fixture f;f.play();f.put(Fixture::manager+0x44,Fixture::proto+4);xyz_xbox_lobby_close(&f.guest,&f.state);
        CHECK(!f.play_releases && !f.drops);}
    {Fixture f;f.play();CHECK(xyz_xbox_lobby_enter_race(&f.guest,&f.state));xyz_xbox_lobby_close(&f.guest,&f.state);
        CHECK(!f.play_releases && f.drops==1);}
    {Fixture f;XyzLobbyGame game{};const uint32_t table=0x39000,node=0x3a000,record=0x3b000;
        CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&game) && game.game_id==-1);f.login();
        f.put(Fixture::list+0x14,1);f.put(Fixture::list+0x18,2);f.put(Fixture::list+0x40,table);
        f.put(table,node);f.put(node,record);f.put(record,123);
        std::strcpy((char*)f.at(record+4),"063.Host");std::strcpy((char*)f.at(record+0x38),"Host");
        std::strcpy((char*)f.at(record+0x8c),"TRACK=4014 LAPS=3");
        f.put(record+0x110,256);f.put(record+0x114,4);f.put(record+0x118,2);f.put(record+0x11c,6);
        f.put(record+0x120,42);f.put(record+0x1bc,3);
        const auto before=f.ram;
        CHECK(xyz_xbox_lobby_game(&f.guest,&f.state,0,&game) && game.game_id==123 && game.players==3 && game.min_players==2 && game.max_players==6);
        CHECK(!std::strcmp(game.name,"063.Host") && !std::strcmp(game.host,"Host") && !std::strcmp(game.params,"TRACK=4014 LAPS=3"));
        CHECK(game.custom_flags==256 && game.system_flags==4 && game.seed==42 && f.ram==before && !f.joins);
        CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,1,&game) && game.game_id==-1 && !game.name[0]);
        CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,UINT32_MAX,&game));
        for(uint32_t address:{Fixture::manager+0x44,Fixture::games+8,Fixture::list+0x14,Fixture::list+0x18,Fixture::list+0x40,table,node,record}){
            f.reject_read=address;CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&game) && game.game_id==-1);f.reject_read=0;
        }
        f.put(Fixture::list+0x18,0);CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&game));
        f.put(Fixture::list+0x18,4097);CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&game));f.put(Fixture::list+0x18,2);
        f.put(Fixture::list+0x40,0xfffffffcu);CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&game));f.put(Fixture::list+0x40,table);
        f.put(record+0x1bc,7);CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&game));f.put(record+0x1bc,3);
        f.put(record+0x118,7);CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&game));f.put(record+0x118,2);
        f.put(record+0x11c,9);CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&game));f.put(record+0x11c,6);
        for(const auto &field:std::array<std::array<unsigned,2>,3>{{{{4,36}},{{0x38,16}},{{0x8c,132}}}}){
            std::vector<uint8_t> old(f.at(record+field[0]),f.at(record+field[0])+field[1]);
            std::memset(f.at(record+field[0]),'X',field[1]);CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&game));
            std::memcpy(f.at(record+field[0]),old.data(),field[1]);
        }
        f.put(Fixture::manager+0x44,Fixture::proto+4);CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&game));f.put(Fixture::manager+0x44,Fixture::proto);
        CHECK(xyz_xbox_lobby_game(&f.guest,&f.state,0,&game));
        XyzLobbyGame selected=game;selected.game_id++;
        CHECK(!xyz_xbox_lobby_join_game(&f.guest,&f.state,0,&selected,"",100));selected=game;selected.name[0]='X';
        CHECK(!xyz_xbox_lobby_join_game(&f.guest,&f.state,0,&selected,"",100));selected=game;
        CHECK(!xyz_xbox_lobby_join_game(&f.guest,&f.state,1,&selected,"",100));
        f.put(record+0x11c,8);CHECK(!xyz_xbox_lobby_join_game(&f.guest,&f.state,0,&selected,"",100));f.put(record+0x11c,6);
        for(auto value:std::array<std::array<uint32_t,2>,4>{{{{0x1bc,6}},{{0x1bc,0}},{{0x118,1}},{{0x114,0x80000}}}}){
            uint32_t old=f.get(record+value[0]);f.put(record+value[0],value[1]);
            CHECK(!xyz_xbox_lobby_join_game(&f.guest,&f.state,0,&selected,"",100));f.put(record+value[0],old);
        }
        f.state.stage=XYZ_LOBBY_IN_GAME;CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&game));
        CHECK(!f.joins && !f.creates && !f.starts);
        f.state.stage=XYZ_LOBBY_READY;
        CHECK(xyz_xbox_lobby_join_game(&f.guest,&f.state,0,&selected,"",100) && f.joins==1);
        f.put(Fixture::games+0x1c,selected.game_id+1);f.tick();
        CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.state.error==XYZ_LOBBY_REJECTED && f.drops==1);f.names_restored();
        f.login();CHECK(xyz_xbox_lobby_join_game(&f.guest,&f.state,0,&selected,"",100));
        f.put(Fixture::games+0x1c,selected.game_id);f.tick();CHECK(f.state.stage==XYZ_LOBBY_IN_GAME);
    }
    {Fixture f;CHECK(!xyz_xbox_lobby_search(&f.guest,&f.state,0,20,100));f.login();
        for(unsigned count:{0u,201u,UINT32_MAX})CHECK(!xyz_xbox_lobby_search(&f.guest,&f.state,0,count,100));
        CHECK(!xyz_xbox_lobby_search(&f.guest,&f.state,INT32_MAX,20,100) && !f.searches);
        f.put(Fixture::list+0x14,20);f.put(Fixture::list+0x18,20);
        CHECK(xyz_xbox_lobby_search(&f.guest,&f.state,20,20,100) && f.state.stage==XYZ_LOBBY_SEARCHING && f.searches==1);
        CHECK(f.get(Fixture::arena+128)==20 && f.get(Fixture::arena+132)==20 && !f.get(Fixture::list+0x14));
        XyzLobbyGame row{};CHECK(!xyz_xbox_lobby_game(&f.guest,&f.state,0,&row));
        CHECK(!xyz_xbox_lobby_search(&f.guest,&f.state,40,20,100) && !xyz_xbox_lobby_join(&f.guest,&f.state,"Name","",100));
        f.reply(0x676a6f69,"COUNT=0");f.tick();CHECK(f.state.stage==XYZ_LOBBY_SEARCHING);
        f.reply(0x67736561,"COUNT=2");f.tick();CHECK(f.state.stage==XYZ_LOBBY_SEARCHING);
        f.put(Fixture::list+0x14,2);f.put(Fixture::list+0x18,2);f.put(Fixture::manager+0x4c,0x60000);
        f.tick();CHECK(f.state.stage==XYZ_LOBBY_SEARCHING);
        f.put(Fixture::manager+0x4c,0);f.tick();CHECK(f.state.stage==XYZ_LOBBY_READY && !f.state.waiting_kind);
        CHECK(xyz_xbox_lobby_search(&f.guest,&f.state,40,20,100));f.reply(0x67736561,"COUNT=0");f.tick();
        CHECK(f.state.stage==XYZ_LOBBY_READY && !f.get(Fixture::list+0x14) && f.searches==2);
        CHECK(xyz_xbox_lobby_search(&f.guest,&f.state,0,200,100));f.reply(0x67736561,"",0x62616463);f.tick();
        CHECK(f.state.stage==XYZ_LOBBY_READY && f.state.error==XYZ_LOBBY_REJECTED && !f.drops);
    }
    for(const char *bad:{"", "COUNT=21", "COUNT=-1", "COUNT=x", "COUNT=2x", "COUNT=4294967296", "COUNT=999999999999999999999999999999999999999"}){
        Fixture f;f.login();CHECK(xyz_xbox_lobby_search(&f.guest,&f.state,0,20,100));f.reply(0x67736561,bad);f.tick();
        CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.state.error==XYZ_LOBBY_REJECTED && f.drops==1);f.names_restored();
    }
    {Fixture f;f.login();CHECK(xyz_xbox_lobby_search(&f.guest,&f.state,0,20,100));f.reply(0x67736561,"COUNT=1");
        f.tick(30000100);CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.state.error==XYZ_LOBBY_TIMEOUT);
        f.reply(0x67736561,"COUNT=0");f.tick();CHECK(f.state.stage==XYZ_LOBBY_FAILED);f.names_restored();
    }
    {Fixture f;f.login();f.put(Fixture::manager+0x44,Fixture::proto+4);
        CHECK(!xyz_xbox_lobby_search(&f.guest,&f.state,0,20,100) && !f.searches);
        f.put(Fixture::manager+0x44,Fixture::proto);f.reject_write=Fixture::arena+128;
        CHECK(!xyz_xbox_lobby_search(&f.guest,&f.state,0,20,100) && !f.searches);
        CHECK(xyz_xbox_lobby_search(&f.guest,&f.state,0,20,100));f.tick(200,4);
        CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.state.error==XYZ_LOBBY_SESSION);f.names_restored();
    }
    {Fixture f;f.put(0x406c98,UINT32_MAX);
        CHECK(!xyz_xbox_lobby_select_controller(&f.guest,&f.state,0));f.login();
        CHECK(!xyz_xbox_lobby_select_controller(&f.guest,&f.state,0));f.state.stage=XYZ_LOBBY_IN_GAME;
        CHECK(!xyz_xbox_lobby_select_controller(&f.guest,&f.state,4) && f.get(0x406c98)==UINT32_MAX);
        f.reject_read=0x427ff8;CHECK(!xyz_xbox_lobby_select_controller(&f.guest,&f.state,0));f.reject_read=0;
        f.at(0x427ff8)[0]=255;
        CHECK(!xyz_xbox_lobby_select_controller(&f.guest,&f.state,0) && f.get(0x406c98)==UINT32_MAX && !f.state.controller_saved);
        f.at(0x427ff8)[0]=65;
        f.reject_read=0x406c98;CHECK(!xyz_xbox_lobby_select_controller(&f.guest,&f.state,0));f.reject_read=0;
        f.reject_write=0x406c98;CHECK(!xyz_xbox_lobby_select_controller(&f.guest,&f.state,0) && !f.state.controller_saved);
        f.put(Fixture::manager+0x44,Fixture::proto+4);CHECK(!xyz_xbox_lobby_select_controller(&f.guest,&f.state,0));
        f.put(Fixture::manager+0x44,Fixture::proto);
        CHECK(xyz_xbox_lobby_select_controller(&f.guest,&f.state,0) && f.get(0x406c98)==0);
        f.at(0x427ff8)[0]=255;
        CHECK(!xyz_xbox_lobby_select_controller(&f.guest,&f.state,0) && f.get(0x406c98)==0);
        CHECK(xyz_xbox_lobby_select_controller(&f.guest,&f.state,2) && f.get(0x406c98)==2);
        xyz_xbox_lobby_close(&f.guest,&f.state);CHECK(f.get(0x406c98)==UINT32_MAX);
    }
    {Fixture f;f.play();f.state.stage=XYZ_LOBBY_IN_GAME;f.put(0x406c98,3);
        CHECK(xyz_xbox_lobby_select_controller(&f.guest,&f.state,0));f.state.stage=XYZ_LOBBY_PLAY_READY;
        CHECK(xyz_xbox_lobby_enter_race(&f.guest,&f.state) && f.get(0x406c98)==0);
        f.tick(500,4);CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.get(0x406c98)==3);
    }
    {Fixture f;f.login();f.state.stage=XYZ_LOBBY_IN_GAME;f.put(0x406c98,3);
        CHECK(xyz_xbox_lobby_select_controller(&f.guest,&f.state,0));f.put(0x406c98,1);
        xyz_xbox_lobby_close(&f.guest,&f.state);CHECK(f.get(0x406c98)==1); // later owner's selection
    }
    {Fixture f;f.frame.status=XYZ_XBOX_READY;CHECK(!f.begin() && !f.inits && !f.allocations);}
    for(uint32_t a:{0x3fbbf4u,0x3fbbfcu,0x428e14u}){Fixture f;f.put(a,1);
        CHECK(!f.begin() && !f.inits && !f.allocations);f.names_restored();
        f.put(a,0);f.reject_read=a;CHECK(!f.begin() && !f.inits && !f.allocations);}
    {Fixture f;f.put(Fixture::manager+0x44,Fixture::proto);CHECK(!f.begin() && !f.inits && !f.allocations);f.names_restored();}
    {Fixture f;f.reject_write=Fixture::profile+0x6c;CHECK(!f.begin());CHECK(f.frees==1 && !f.inits && !f.drops);f.names_restored();}
    {Fixture f;f.login();CHECK(f.state.stage==XYZ_LOBBY_READY);
        for(auto offset:Fixture::offsets)CHECK(!std::strcmp((char*)f.at(Fixture::profile+offset),"Alpha1234"));
        CHECK(xyz_xbox_lobby_owns_callback(&f.state,Fixture::proto,Fixture::arena));
        CHECK(!xyz_xbox_lobby_owns_callback(&f.state,Fixture::proto,Fixture::arena+4));
        f.reply(0x61757468,"NAME=Late",0x62616463);f.tick(90000000);CHECK(f.state.stage==XYZ_LOBBY_READY);
        CHECK(!xyz_xbox_lobby_join(&f.guest,&f.state,"%%%:::::::::::::::%%%%", "%%%%%::::::%%%%",100));
        CHECK(xyz_xbox_lobby_join(&f.guest,&f.state,"Private","wrong",100));
        f.reply(0x676a6f69,"",0x70617373);f.tick();CHECK(f.state.stage==XYZ_LOBBY_READY && f.state.error==XYZ_LOBBY_REJECTED && f.drops==0);
        CHECK(xyz_xbox_lobby_join(&f.guest,&f.state,"Private","correct",100));
        f.put(Fixture::games+0x1c,42);f.put(Fixture::games+0x1d8,2);f.put(Fixture::list+0x14,20);
        std::strcpy((char*)f.at(Fixture::games+0x20),"Private");std::strcpy((char*)f.at(Fixture::games+0x1e0),"Host");std::strcpy((char*)f.at(Fixture::games+0x244),"Alpha1234");
        f.tick();CHECK(f.state.stage==XYZ_LOBBY_IN_GAME);
        CHECK(xyz_xbox_lobby_ready(&f.guest,&f.state,1) && f.ready==1);
        f.put(Fixture::games+0x27c,0x08000000);XyzLobbySnapshot snap{};
        CHECK(xyz_xbox_lobby_snapshot(&f.guest,&f.state,&snap));CHECK(snap.game_id==42 && snap.players==2 && snap.listed_games==20 && snap.player_flags[1]==0x08000000 && !std::strcmp(snap.player[1],"Alpha1234"));
        CHECK(xyz_xbox_lobby_leave(&f.guest,&f.state,100));f.put(Fixture::games+0x1c,UINT32_MAX);f.tick();CHECK(f.state.stage==XYZ_LOBBY_READY);
        f.tick(300,4);CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.state.error==XYZ_LOBBY_SESSION && f.drops==1 && f.frees==1);f.names_restored();
        CHECK(!xyz_xbox_lobby_owns_callback(&f.state,Fixture::proto,Fixture::arena));xyz_xbox_lobby_close(&f.guest,&f.state);CHECK(f.drops==1 && f.frees==1);
    }
    {Fixture f;f.login();CHECK(f.state.stage==XYZ_LOBBY_READY);
        std::strcpy((char*)f.at(Fixture::profile+0x58),"AnotherOwner");xyz_xbox_lobby_close(&f.guest,&f.state);
        CHECK(!std::strcmp((char*)f.at(Fixture::profile+0x58),"AnotherOwner"));}
    {Fixture f;CHECK(f.begin());f.tick();f.reply(0x61757468,"NAME=Alpha");f.tick();
        f.reject_write=Fixture::profile+0x94;f.reply(0x70657273,"PERS=Alpha1234");f.tick();
        CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.drops==1 && f.frees==1);f.names_restored();}
    {Fixture f;f.login();f.put(Fixture::manager+0x44,Fixture::proto+0x4000);f.tick();
        CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.state.error==XYZ_LOBBY_CONTEXT && !f.drops && f.frees==1);f.names_restored();}
    {Fixture f;f.login("ABCDEFGHIJKLMNOPQRST");CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.drops==1 && f.frees==1);f.names_restored();}
    {Fixture f;CHECK(f.begin());f.tick();f.reply(0x61757468,"",0x696d7374);f.tick();CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.state.error==XYZ_LOBBY_REJECTED);f.names_restored();}
    {Fixture f;CHECK(f.begin());f.tick(45000101);CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.state.error==XYZ_LOBBY_TIMEOUT);f.names_restored();}
    {Fixture f;f.login();f.state.stage=XYZ_LOBBY_IN_GAME;f.put(Fixture::games+0x1c,UINT32_MAX);f.tick();CHECK(f.state.stage==XYZ_LOBBY_READY);}
    {Fixture f;f.login();CHECK(!xyz_xbox_lobby_create(&f.guest,&f.state,0,100));
        f.at(Fixture::profile)[0]=6;f.at(Fixture::profile)[1]=2;
        CHECK(!xyz_xbox_lobby_create(&f.guest,&f.state,4,100));
        CHECK(xyz_xbox_lobby_create(&f.guest,&f.state,2,100) && f.creates==1);
        f.reply(0x67637265,"",0x66756c6c);f.tick();CHECK(f.state.stage==XYZ_LOBBY_READY && f.state.error==XYZ_LOBBY_REJECTED);
        CHECK(xyz_xbox_lobby_create(&f.guest,&f.state,2,100));f.put(Fixture::games+0x1c,51);f.tick();CHECK(f.state.stage==XYZ_LOBBY_IN_GAME);
    }
    {Fixture f;f.login();CHECK(!xyz_xbox_lobby_start(&f.guest,&f.state,100));
        f.state.stage=XYZ_LOBBY_IN_GAME;f.put(Fixture::games+0x1c,51);f.put(Fixture::games+0x1d8,2);
        f.put(Fixture::games+0x134,2);
        std::strcpy((char*)f.at(Fixture::games+0x54),"SomeoneElse");
        std::strcpy((char*)f.at(Fixture::games+0x1e0),f.state.name);
        CHECK(!xyz_xbox_lobby_start(&f.guest,&f.state,100));
        std::strcpy((char*)f.at(Fixture::games+0x54),f.state.name);
        CHECK(!xyz_xbox_lobby_start(&f.guest,&f.state,100) && !f.starts);
        f.put(Fixture::games+0x27c,0x08000000);
        CHECK(xyz_xbox_lobby_start(&f.guest,&f.state,100) && f.starts==1);
        CHECK(!xyz_xbox_lobby_start(&f.guest,&f.state,100) && f.starts==1);
        f.reply(0x67737461,"",0x6e65706c);f.tick();CHECK(f.state.stage==XYZ_LOBBY_IN_GAME && f.state.error==XYZ_LOBBY_REJECTED);
        CHECK(xyz_xbox_lobby_start(&f.guest,&f.state,100));f.reply(0x67737461,"");f.tick();
        CHECK(f.state.stage==XYZ_LOBBY_STARTING); // acknowledgement alone is not a started race
        f.state.play_received=1;f.tick(50000000);CHECK(f.state.stage==XYZ_LOBBY_PLAY_READY);
        CHECK(xyz_xbox_lobby_leave(&f.guest,&f.state,50000000) && !f.state.play_received);
        f.put(Fixture::games+0x1c,UINT32_MAX);f.tick(50000001);CHECK(f.state.stage==XYZ_LOBBY_READY);
    }
    {Fixture f;f.login();f.state.stage=XYZ_LOBBY_IN_GAME;f.put(Fixture::games+0x1c,51);
        f.state.play_received=1;f.tick();CHECK(f.state.stage==XYZ_LOBBY_PLAY_READY); // guest receives host's start
    }
    {Fixture f;f.login();constexpr uint32_t resolver=0x42cd78;
        std::strcpy((char*)f.at(resolver+4),"10.77.2.33");f.put(resolver+0x20,3658);f.at(resolver+0x18)[0]=1;
        f.put(Fixture::games+0x1d8,2);f.put(Fixture::games+0x1f0,0x0a4d0221);
        CHECK(!xyz_xbox_lobby_resolve_virtual(&f.guest,&f.state,resolver,0,3658));
        f.state.play_received=1;
        CHECK(!xyz_xbox_lobby_resolve_virtual(&f.guest,&f.state,resolver,1,3658));
        CHECK(!xyz_xbox_lobby_resolve_virtual(&f.guest,&f.state,resolver,0,80));
        CHECK(!xyz_xbox_lobby_resolve_virtual(&f.guest,&f.state,resolver+4,0,3658));
        f.put(Fixture::games+0x1f0,0x0a4d0222);CHECK(!xyz_xbox_lobby_resolve_virtual(&f.guest,&f.state,resolver,0,3658));
        f.put(Fixture::games+0x1f0,0x0a4d0221);CHECK(xyz_xbox_lobby_resolve_virtual(&f.guest,&f.state,resolver,0,3658));
        CHECK(f.get(resolver+0x1c)==0x0a4d0221 && !f.at(resolver+0x18)[0]);
    }
    {Fixture f;f.play();CHECK(xyz_xbox_lobby_enter_race(&f.guest,&f.state));
        CHECK(f.state.stage==XYZ_LOBBY_RACE && !f.state.initialized && !f.state.arena && !f.state.protocol && !f.state.games);
        CHECK(f.drops==1 && f.frees==1 && f.state.race_host==0x0a4d0221);
        for(auto offset:Fixture::offsets)CHECK(!std::strcmp((char*)f.at(Fixture::profile+offset),"Alpha1234"));
        CHECK(!xyz_xbox_lobby_enter_race(&f.guest,&f.state) && f.drops==1 && f.frees==1);
        CHECK(!xyz_xbox_lobby_owns_callback(&f.state,Fixture::proto,Fixture::arena));
        CHECK(!xyz_xbox_lobby_leave(&f.guest,&f.state,100));
        int updates=f.updates,idles=f.idles;f.tick(90000000);
        CHECK(f.state.stage==XYZ_LOBBY_RACE && f.updates==updates && f.idles==idles+1);
        constexpr uint32_t resolver=0x42cd78;
        std::strcpy((char*)f.at(resolver+4),"10.77.2.33");f.put(resolver+0x20,3658);f.at(resolver+0x18)[0]=1;
        std::memset(f.at(Fixture::games),0xcc,0x504); // original record no longer exists
        CHECK(xyz_xbox_lobby_resolve_virtual(&f.guest,&f.state,resolver,0,3658));
        CHECK(f.get(resolver+0x1c)==0x0a4d0221 && !f.at(resolver+0x18)[0]);
        std::strcpy((char*)f.at(resolver+4),"10.77.2.34");
        CHECK(!xyz_xbox_lobby_resolve_virtual(&f.guest,&f.state,resolver,0,3658));
        f.tick(90000001,4);CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.state.error==XYZ_LOBBY_SESSION);
        CHECK(f.drops==1 && f.frees==1 && !f.state.race_host);f.names_restored();
        xyz_xbox_lobby_close(&f.guest,&f.state);CHECK(f.drops==1 && f.frees==1);
    }
    for(unsigned bad=0;bad<8;++bad){Fixture f;f.play();
        switch(bad){
        case 0:f.state.stage=XYZ_LOBBY_IN_GAME;break;
        case 1:f.state.play_received=0;break;
        case 2:f.put(Fixture::manager+0x44,Fixture::proto+4);break;
        case 3:f.put(Fixture::games+0x1d8,7);break;
        case 4:std::strcpy((char*)f.at(Fixture::games+0x54),"DifferentHost");break;
        case 5:std::strcpy((char*)f.at(Fixture::games+0x244),"UnknownPlayer");break;
        case 6:f.put(Fixture::games+0x1f0,0xc0a80101);break;
        case 7:f.reject_read=Fixture::games+0x1f0;break;
        }
        CHECK(!xyz_xbox_lobby_enter_race(&f.guest,&f.state) && !f.drops && !f.frees);
    }
    for(bool unreadable:{false,true}){Fixture f;f.play();CHECK(xyz_xbox_lobby_enter_race(&f.guest,&f.state));
        if(unreadable)f.reject_read=Fixture::manager+0x44;else f.put(Fixture::manager+0x44,Fixture::proto+4);
        int idles=f.idles;f.tick();CHECK(f.state.stage==XYZ_LOBBY_FAILED && f.state.error==XYZ_LOBBY_CONTEXT);
        CHECK(f.drops==1 && f.frees==1 && f.idles==idles);f.names_restored();
    }
    {Fixture f;f.login();uint32_t prepared=123;
        f.put(Fixture::event+8,0x706c6179);f.put(Fixture::event+16,Fixture::event+32);
        const std::string roster="COUNT=2 ADDR0=10.77.0.2 MADDR0= ADDR1=10.77.3.254 MADDR1= PARAMS=original";
        std::strcpy((char*)f.at(Fixture::event+32),roster.c_str());
        CHECK(xyz_xbox_lobby_prepare_play(&f.guest,&f.state,Fixture::event,&prepared));
        CHECK(prepared==Fixture::arena+0x2500 && f.get(prepared+8)==0x706c6179);
        CHECK(std::string((char*)f.at(f.get(prepared+16)))=="MADDR0=10.77.0.2\nMADDR1=10.77.3.254\n"+roster);
        CHECK(!std::strcmp((char*)f.at(Fixture::event+32),roster.c_str()));
        for(const char *bad:{"COUNT=9 ADDR0=10.77.0.2", "COUNT=2 ADDR0=10.77.0.1 ADDR1=10.77.0.2",
            "COUNT=2 ADDR0=10.77.0.2 ADDR1=192.168.0.1", "COUNT=2 ADDR0=10.77.0.2 ADDR1=10.77.255.255",
            "COUNT=2 ADDR0=10.77.0.2 MADDR1=10.77.0.3", "COUNT=2 ADDR0=10.77.0.2 ADDR1=10.77.0.3:3658"}){
            std::strcpy((char*)f.at(Fixture::event+32),bad);prepared=123;
            CHECK(!xyz_xbox_lobby_prepare_play(&f.guest,&f.state,Fixture::event,&prepared) && !prepared);
        }
        std::string too_long=roster+' '+std::string(8190-roster.size(),'x');std::strcpy((char*)f.at(Fixture::event+32),too_long.c_str());
        CHECK(!xyz_xbox_lobby_prepare_play(&f.guest,&f.state,Fixture::event,&prepared));
        f.state.event_error=1;CHECK(!f.drops);f.tick();CHECK(f.drops==1 && f.state.error==XYZ_LOBBY_REJECTED);f.names_restored();
    }
    std::puts("Xbox lobby controller: profile rollback, assigned persona, stale sessions, rejection/retry, create/join, guarded start, race handoff, virtual host resolution and cleanup passed");
}
