#include "xyz_races_api.h"
#include <cstdio>
#include <cstring>
#include <climits>
#include <atomic>
#include <thread>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
    const char *id="1234567890abcdef1234567890abcdef";
    const char *claim="Ec0iIUolQixwifl_FxyyfA9X-foBclMDwN_l-F2BSCw";
    XYZRaceBrowser v{},out{};XYZRaceRequest r{};
    CHECK(!xyz_races_active() && !xyz_races_needs_frame() && !xyz_races_take(&r));
    CHECK(!xyz_races_open("x","",id,id));CHECK(!xyz_races_open("Driver!","",id,id));
    CHECK(!xyz_races_open("Driver","","bad",id));CHECK(!xyz_races_open("Driver",nullptr,id,id));
    CHECK(xyz_races_open("Driver","VR_TEST",id,id));
    xyz_races_snapshot(&v);CHECK(v.active && v.epoch && v.state==XYZ_RACES_CONNECTING);
    CHECK(!xyz_races_open("Other","",id,id));CHECK(!xyz_races_search(v.epoch,0));
    XYZRaceBrowser stale=v;stale.state=XYZ_RACES_READY;
    xyz_races_publish(&stale);xyz_races_snapshot(&out);CHECK(out.state==XYZ_RACES_CONNECTING);
    CHECK(xyz_races_take(&r) && r.action==XYZ_RACES_OPEN && r.epoch==v.epoch);
    CHECK(!std::strcmp(r.name,"Driver") && !std::strcmp(r.room,"VR_TEST") && !std::strcmp(r.device,id));
    CHECK(!xyz_races_take(&r) && !r.action && !r.device[0]);
    v.state=XYZ_RACES_READY;v.count=20;xyz_races_publish(&v);
    CHECK(!xyz_races_search(v.epoch+1,20));CHECK(!xyz_races_search(v.epoch,INT_MAX-19));
    CHECK(xyz_races_search(v.epoch,20));CHECK(!xyz_races_search(v.epoch,40));
    xyz_races_snapshot(&out);CHECK(out.state==XYZ_RACES_SEARCHING && !out.count);
    CHECK(xyz_races_close());CHECK(xyz_races_take(&r) && r.action==XYZ_RACES_CLOSE && !r.profile[0]);
    xyz_races_publish(&v);xyz_races_snapshot(&out);CHECK(out.state==XYZ_RACES_CLOSING);
    v.active=0;v.state=XYZ_RACES_OFFLINE;v.count=0;xyz_races_publish(&v);
    CHECK(!xyz_races_active() && !xyz_races_needs_frame() && !xyz_races_close());
    CHECK(xyz_races_open("Other","",id,id));CHECK(xyz_races_take(&r));
    xyz_races_snapshot(&out);CHECK(out.epoch!=v.epoch);
    xyz_races_publish(&v);xyz_races_snapshot(&v);CHECK(v.active && v.state==XYZ_RACES_CONNECTING);
    // Snapshot readers must never see half a page copied by the guest thread.
    v.state=XYZ_RACES_READY;v.count=20;xyz_races_publish(&v);
    std::atomic<bool> done{false},bad{false};
    std::thread reader([&]{while(!done){XYZRaceBrowser s;xyz_races_snapshot(&s);
        for(unsigned i=0;i<s.count;++i)if(s.rows[i].seed!=s.start)bad=true;}});
    for(unsigned n=1;n<=2000;++n){v.start=n;for(auto &g:v.rows)g.seed=n;xyz_races_publish(&v);}
    done=true;reader.join();CHECK(!bad);
    CHECK(xyz_races_close() && xyz_races_take(&r));v.active=0;v.state=XYZ_RACES_ERROR;v.count=0;xyz_races_publish(&v);
    CHECK(!xyz_races_active());
    // Closing before Open is consumed must erase the copied identity.
    CHECK(xyz_races_open("Driver","",id,id));xyz_races_snapshot(&v);CHECK(xyz_races_close());
    CHECK(xyz_races_take(&r) && r.action==XYZ_RACES_CLOSE && !r.name[0] && !r.device[0]);
    v.active=0;v.state=XYZ_RACES_OFFLINE;xyz_races_publish(&v);CHECK(!xyz_races_needs_frame());
    CHECK(!xyz_races_open_claimed("Driver","",id,id,"invalid"));
    CHECK(xyz_races_open_claimed("Driver","",id,id,claim));xyz_races_snapshot(&v);
    CHECK(xyz_races_take(&r) && !std::strcmp(r.claim,claim));
    CHECK(!xyz_races_take(&r) && !r.claim[0]);
    CHECK(xyz_races_close());CHECK(xyz_races_take(&r) && !r.claim[0]);
    v.active=0;v.state=XYZ_RACES_OFFLINE;xyz_races_publish(&v);
    CHECK(xyz_races_open_claimed("Driver","",id,id,claim));xyz_races_snapshot(&v);CHECK(xyz_races_close());
    CHECK(xyz_races_take(&r) && !r.claim[0]);v.active=0;v.state=XYZ_RACES_OFFLINE;xyz_races_publish(&v);
    CHECK(xyz_races_open("Driver","",id,id));CHECK(xyz_races_take(&r));xyz_races_snapshot(&v);
    v.state=XYZ_RACES_READY;v.count=1;v.rows[0]={};v.rows[0].game_id=7;std::strcpy(v.rows[0].name,"Example");
    v.rows[0].players=1;v.rows[0].min_players=2;v.rows[0].max_players=6;v.info[0].compatible=1;xyz_races_publish(&v);
    XyzLobbyGame selected=v.rows[0];selected.game_id=8;
    CHECK(!xyz_races_join(v.epoch,0,&selected,""));CHECK(!xyz_races_join(v.epoch+1,0,&v.rows[0],""));
    CHECK(!xyz_races_join(v.epoch,1,&v.rows[0],""));CHECK(!xyz_races_join(v.epoch,0,&v.rows[0],"bad\n"));
    v.rows[0].system_flags=0x10000;xyz_races_publish(&v);CHECK(!xyz_races_join(v.epoch,0,&v.rows[0],""));
    CHECK(xyz_races_join(v.epoch,0,&v.rows[0],"aA-123"));CHECK(!xyz_races_join(v.epoch,0,&v.rows[0],"aA-123"));
    CHECK(xyz_races_close() && xyz_races_take(&r) && !r.password[0] && r.action==XYZ_RACES_CLOSE);
    v.active=0;v.count=0;v.state=XYZ_RACES_OFFLINE;xyz_races_publish(&v);
    CHECK(xyz_races_open("Driver","",id,id));CHECK(xyz_races_take(&r));xyz_races_snapshot(&v);
    v.state=XYZ_RACES_READY;v.count=1;v.rows[0]=selected;v.rows[0].game_id=7;v.info[0].compatible=1;xyz_races_publish(&v);
    CHECK(xyz_races_join(v.epoch,0,&v.rows[0],"secret"));CHECK(xyz_races_take(&r) && r.action==XYZ_RACES_JOIN && !strcmp(r.password,"secret"));
    CHECK(!xyz_races_take(&r) && !r.password[0]);v.count=0;v.state=XYZ_RACES_IN_LOBBY;xyz_races_publish(&v);
    CHECK(!xyz_races_ready(v.epoch,2));CHECK(!xyz_races_ready(v.epoch+1,1));CHECK(!xyz_races_start(v.epoch));
    CHECK(xyz_races_ready(v.epoch,1));CHECK(!xyz_races_leave(v.epoch));CHECK(xyz_races_take(&r) && r.ready==1);
    v.host=1;v.startable=1;xyz_races_publish(&v);CHECK(xyz_races_start(v.epoch));CHECK(xyz_races_take(&r) && r.action==XYZ_RACES_START);
    v.state=XYZ_RACES_RACING;xyz_races_publish(&v);CHECK(!xyz_races_ready(v.epoch,0));CHECK(xyz_races_leave(v.epoch));
    CHECK(xyz_races_take(&r) && r.action==XYZ_RACES_LEAVE);CHECK(xyz_races_close() && xyz_races_take(&r));
    v.active=0;v.state=XYZ_RACES_OFFLINE;xyz_races_publish(&v);
    CHECK(xyz_races_open("Driver","",id,id));CHECK(xyz_races_take(&r));xyz_races_snapshot(&v);
    v.state=XYZ_RACES_READY;v.track_count=1;v.tracks[0]={};v.tracks[0].mode=2;v.tracks[0].id=4201;
    v.tracks[0].min_laps=v.tracks[0].max_laps=1;v.tracks[0].max_players=4;xyz_races_publish(&v);
    XyzRaceSettings options{};options.mode=2;options.track=4201;options.laps=1;options.min_players=2;options.max_players=4;
    strcpy(options.password,"Ab_123");CHECK(!xyz_races_create(v.epoch+1,&options));options.laps=2;CHECK(!xyz_races_create(v.epoch,&options));
    options.laps=1;options.max_players=6;CHECK(!xyz_races_create(v.epoch,&options));options.max_players=4;
    options.reverse=1;CHECK(!xyz_races_create(v.epoch,&options));options.reverse=0;
    CHECK(xyz_races_create(v.epoch,&options));CHECK(!xyz_races_create(v.epoch,&options));
    CHECK(xyz_races_take(&r) && r.action==XYZ_RACES_CREATE && !strcmp(r.options.password,"Ab_123"));
    CHECK(!xyz_races_take(&r) && !r.options.password[0]);v.state=XYZ_RACES_READY;xyz_races_publish(&v);
    CHECK(xyz_races_create(v.epoch,&options) && xyz_races_close());CHECK(xyz_races_take(&r) && r.action==XYZ_RACES_CLOSE && !r.options.password[0]);
    v.active=0;v.state=XYZ_RACES_OFFLINE;xyz_races_publish(&v);
    puts("Race mailbox: copied rules, bounds, epochs, password clearing and atomic page snapshots passed");return 0;
}
