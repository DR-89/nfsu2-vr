#include "xyz_races_api.h"
#include "xyz_auth.h"
#include <mutex>
#include <cstring>
#include <cstdio>
#include <climits>
namespace {
std::mutex mutex;
XYZRaceBrowser view{};
XYZRaceRequest pending{};
uint32_t sequence=0;
bool identity(const char *text){return text && std::strlen(text)==32 && std::strspn(text,"0123456789abcdef")==32;}
bool name_valid(const char *text){return text && std::strlen(text)>=3 && std::strlen(text)<=15 &&
    std::strspn(text,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")==std::strlen(text);}
}
extern "C" int xyz_races_open(const char *name,const char *room,const char *profile,const char *device){
    return xyz_races_open_claimed(name,room,profile,device,"");
}
extern "C" int xyz_races_open_claimed(const char *name,const char *room,const char *profile,const char *device,const char *claim){
    if(!name_valid(name) || !room || std::strlen(room)>63 || !identity(profile) || !identity(device) || !xyz_auth_claim_valid(claim))return 0;
    std::lock_guard<std::mutex> lock(mutex);
    if(view.active || pending.action)return 0;
    if(!++sequence)++sequence;
    pending={};pending.epoch=sequence;pending.action=XYZ_RACES_OPEN;
    std::snprintf(pending.name,sizeof pending.name,"%s",name);std::snprintf(pending.room,sizeof pending.room,"%s",room);
    std::memcpy(pending.profile,profile,33);std::memcpy(pending.device,device,33);
    std::snprintf(pending.claim,sizeof pending.claim,"%s",claim);
    view={};view.epoch=sequence;view.active=1;view.state=XYZ_RACES_CONNECTING;
    std::snprintf(view.name,sizeof view.name,"%s",name);return 1;
}
extern "C" int xyz_races_search(uint32_t epoch,uint32_t start){
    std::lock_guard<std::mutex> lock(mutex);
    if(!view.active || view.state!=XYZ_RACES_READY || pending.action || epoch!=view.epoch || start>INT_MAX-XYZ_RACES_PAGE_SIZE)return 0;
    pending={};pending.epoch=epoch;pending.start=start;pending.action=XYZ_RACES_SEARCH;
    view.state=XYZ_RACES_SEARCHING;view.count=0;std::memset(view.rows,0,sizeof view.rows);std::memset(view.info,0,sizeof view.info);return 1;
}
extern "C" int xyz_races_join(uint32_t epoch,unsigned index,const XyzLobbyGame *selected,const char *password){
    if(!selected || !password || std::strlen(password)>32)return 0;
    for(const unsigned char *p=(const unsigned char *)password;*p;++p)if(*p<32 || *p==127)return 0;
    std::lock_guard<std::mutex> lock(mutex);
    if(!view.active || view.state!=XYZ_RACES_READY || pending.action || epoch!=view.epoch || index>=view.count ||
       index>=XYZ_RACES_PAGE_SIZE || !view.info[index].compatible)return 0;
    const auto &row=view.rows[index];
    if(std::memcmp(&row,selected,sizeof row) || row.min_players<2 || row.max_players>6 ||
       !row.players || row.players>=row.max_players || (row.system_flags&0x80000u) ||
       ((row.system_flags&0x10000u) && !*password))return 0;
    pending={};pending.epoch=epoch;pending.index=index;pending.selected=row;pending.action=XYZ_RACES_JOIN;
    std::snprintf(pending.password,sizeof pending.password,"%s",password);
    view.state=XYZ_RACES_JOINING;view.notice[0]=0;return 1;
}
extern "C" int xyz_races_ready(uint32_t epoch,int ready){
    std::lock_guard<std::mutex> lock(mutex);
    if(!view.active || view.state!=XYZ_RACES_IN_LOBBY || pending.action || epoch!=view.epoch || (ready!=0 && ready!=1))return 0;
    pending={};pending.epoch=epoch;pending.action=XYZ_RACES_SET_READY;pending.ready=ready;return 1;
}
extern "C" int xyz_races_start(uint32_t epoch){
    std::lock_guard<std::mutex> lock(mutex);
    if(!view.active || view.state!=XYZ_RACES_IN_LOBBY || !view.host || !view.startable || pending.action || epoch!=view.epoch)return 0;
    pending={};pending.epoch=epoch;pending.action=XYZ_RACES_START;view.state=XYZ_RACES_STARTING;return 1;
}
extern "C" int xyz_races_leave(uint32_t epoch){
    std::lock_guard<std::mutex> lock(mutex);
    if(!view.active || (view.state!=XYZ_RACES_IN_LOBBY && view.state!=XYZ_RACES_RACING) || pending.action || epoch!=view.epoch)return 0;
    pending={};pending.epoch=epoch;pending.action=XYZ_RACES_LEAVE;view.state=XYZ_RACES_RETURNING;return 1;
}
extern "C" int xyz_races_close(void){
    std::lock_guard<std::mutex> lock(mutex);
    if(!view.active)return 0;
    xyz_auth_clear(&pending,sizeof pending);
    pending={};pending.epoch=view.epoch;pending.action=XYZ_RACES_CLOSE;
    view.state=XYZ_RACES_CLOSING;view.count=0;std::memset(view.rows,0,sizeof view.rows);std::memset(view.info,0,sizeof view.info);return 1;
}
extern "C" int xyz_races_create(uint32_t epoch,const XyzRaceSettings *o){
    if(!o || o->min_players<2 || o->min_players>o->max_players || o->max_players>6 ||
       o->reverse>1 || o->collision>1 || o->catchup>1 || o->ranked>1 || o->private_game>1 ||
       o->unrestricted>1 || o->performance>3 || !std::memchr(o->password,0,sizeof o->password))return 0;
    for(const unsigned char *p=(const unsigned char*)o->password;*p;++p)
        if(!((*p>='A' && *p<='Z') || (*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='_' || *p=='-'))return 0;
    std::lock_guard<std::mutex> lock(mutex);
    if(!view.active || view.state!=XYZ_RACES_READY || pending.action || epoch!=view.epoch)return 0;
    bool valid=false;
    for(unsigned i=0;i<view.track_count;++i){const auto &t=view.tracks[i];
        if(t.mode==o->mode && t.id==o->track && o->laps>=t.min_laps && o->laps<=t.max_laps &&
           o->max_players<=t.max_players && (!o->reverse || t.reverse) &&
           (!(o->mode==3 || o->mode==10) || !o->catchup)){valid=true;break;}
    }
    if(!valid)return 0;
    pending={};pending.epoch=epoch;pending.action=XYZ_RACES_CREATE;pending.options=*o;
    view.state=XYZ_RACES_JOINING;view.notice[0]=0;return 1;
}
extern "C" int xyz_races_active(void){std::lock_guard<std::mutex> lock(mutex);return view.active;}
extern "C" int xyz_races_needs_frame(void){std::lock_guard<std::mutex> lock(mutex);return view.active || pending.action;}
extern "C" void xyz_races_snapshot(XYZRaceBrowser *out){if(!out)return;std::lock_guard<std::mutex> lock(mutex);*out=view;}
extern "C" int xyz_races_take(XYZRaceRequest *out){
    if(!out)return 0;std::lock_guard<std::mutex> lock(mutex);*out=pending;
    xyz_auth_clear(&pending,sizeof pending);return out->action!=0;
}
extern "C" void xyz_races_publish(const XYZRaceBrowser *in){
    if(!in || in->count>XYZ_RACES_PAGE_SIZE || in->track_count>XYZ_RACES_TRACK_CHOICES || in->lobby.players>6 || in->state<XYZ_RACES_OFFLINE || in->state>XYZ_RACES_RETURNING)return;
    std::lock_guard<std::mutex> lock(mutex);
    if(!view.active || in->epoch!=view.epoch || pending.action)return;
    /* Close remains dominant after its request has been consumed, too. */
    if(view.state==XYZ_RACES_CLOSING && in->active && in->state!=XYZ_RACES_CLOSING)return;
    view=*in;view.name[15]=0;view.notice[127]=0;
}
