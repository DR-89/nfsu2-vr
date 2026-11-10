#include "xyz_assets.h"
#include <cstdio>
#include <cstring>

namespace xyz {
static bool read(const XyzXboxGuest &g,uint32_t a,void *out,size_t n){return g.read && xyz_xbox_address(a,n) && g.read(g.context,a,out,n);}
static bool call(const XyzXboxGuest &g,XyzXboxOp op,unsigned n,const uint32_t *args,uint32_t &out){return g.call && g.call(g.context,op,0,0,n,args,&out);}
bool Assets::fingerprint(const XyzXboxGuest &g,uint64_t &hash) {
    uint32_t n,table;if(!read(g,0x3f07ac,&n,4) || n>XYZ_HIBER_CAP || !read(g,0x3f0818,&table,4))return false;
    uint8_t bytes[XYZ_HIBER_CAP*24];if(n && !read(g,table,bytes,n*24))return false;
    hash=14695981039346656037ull;
    for(unsigned i=0;i<n*24;i++)hash=(hash^bytes[i])*1099511628211ull;
    return true;
}
bool Assets::owned(const XyzXboxGuest &g,unsigned i,uint32_t &pack) {
    uint32_t refs;
    return i<loaded_ && xyz_xbox_pack_find(&g,catalog_.path[i],&pack)==1 && pack==packs_[i].pointer &&
        read(g,pack+0xb4,&refs,4) && refs>0;
}
void Assets::maintain(const XyzXboxGuest &g) {
    if(!started_ || failed_ || loaded_!=catalog_.count)return;
    uint64_t hash;
    if(!fingerprint(g,hash)){failed_=true;return;}
    if(hash!=fingerprint_){fingerprint_=hash;scan_=0;dirty_=true;deferred_=false;}
    if(!dirty_)return;
    // At most one pack's repair each frame. Heavy resource work is never done
    // by render input or the socket worker.
    if(scan_>=loaded_){scan_=0;dirty_=deferred_;deferred_=false;if(!dirty_)return;}
    uint32_t pack;
    if(!owned(g,scan_,pack)){failed_=true;return;}
    XyzXboxWakePlan plan;int result=xyz_xbox_wake_prepare(&g,pack,&plan);
    if(result<0){failed_=true;return;}
    if(!result)deferred_=true;
    else if(plan.matches) {
        for(unsigned i=0;i<plan.count;i++)if(plan.fix[i].kind==XYZ_WAKE_HIDE)deferred_=true;
        if(!xyz_xbox_wake_begin(&g,&plan)){failed_=true;return;}
        uint32_t ignored;
        // The host's re-entry guard skips the external Wake wrapper here;
        // this call already has its complete begin/finish repair around it.
        if(!call(g,XYZ_G_PACK_WAKE,1,&packs_[scan_].file,ignored) || !xyz_xbox_wake_finish(&g,&plan)) {failed_=true;return;}
        if(!fingerprint(g,fingerprint_)){failed_=true;return;}
    }
    ++scan_;
    if(scan_==loaded_ && !deferred_)dirty_=false;
}
int Assets::prepare(const XyzXboxGuest &g,uint64_t now) {
    if(failed_)return -1;
    uint32_t room=0;
    if(!call(g,XYZ_G_POOL_READY,0,nullptr,room)){failed_=true;return -1;}
    if(!room)return 0;
    if(!started_) {
        started_=true;started_at_=now;
        if(!xyz_xbox_pack_catalog(&g,&catalog_) || !catalog_.count){failed_=true;return -1;}
        uint32_t bytes=XYZ_PACK_CAP*XYZ_PACK_PATH,address=0;
        if(!call(g,XYZ_G_ALLOC,1,&bytes,address) || !xyz_xbox_address(address,bytes)) {failed_=true;return -1;}
        arena_={address,bytes};
        if(!g.write || !g.write(g.context,address,catalog_.path,sizeof catalog_.path)){failed_=true;return -1;}
        for(unsigned i=0;i<catalog_.count;i++)packs_[i].file=address+i*XYZ_PACK_PATH;
        std::fprintf(stderr,"[XYZ assets] catalogue=%u shared and rim packs\n",catalog_.count);
    }
    if(loaded_<catalog_.count) {
        uint32_t pack=0,before=0,after=0,ok=0;int found=xyz_xbox_pack_find(&g,catalog_.path[loaded_],&pack);
        if(found<0 || (found && (!read(g,pack+0xb4,&before,4) || before==0xffffffffu))) {failed_=true;return -1;}
        uint32_t args[4]={packs_[loaded_].file,0,0,0};
        if(!call(g,XYZ_G_PACK_LOAD,4,args,ok) || !ok || xyz_xbox_pack_find(&g,catalog_.path[loaded_],&pack)!=1 ||
           !read(g,pack+0xb4,&after,4) || after!=before+1) {failed_=true;return -1;}
        packs_[loaded_++].pointer=pack;return 0;
    }
    maintain(g);
    if(failed_)return -1;
    if(dirty_) {
        if(!initial_ready_ && (now<started_at_ || now-started_at_>30000000)){failed_=true;std::fprintf(stderr,"[XYZ assets] header/ownership repair timed out\n");return -1;}
        return 0;
    }
    if(!initial_ready_)std::fprintf(stderr,"[XYZ assets] ready=%u shared and rim packs\n",loaded_);
    initial_ready_=true;return 1;
}
int Assets::before_wake(const XyzXboxGuest &g,uint32_t file,XyzXboxWakePlan &plan) {
    // A later failure must not disable repairs for already held references.
    if(!started_ || !loaded_)return 0;
    char path[128];
    unsigned n=0;for(;n<sizeof path;n++){if(!read(g,file+n,path+n,1))return 0;if(!path[n])break;}
    if(n==sizeof path)return 0;
    for(unsigned i=0;i<loaded_;i++) {
        if(std::strcmp(path,catalog_.path[i]))continue;
        uint32_t pack,ignored;if(!owned(g,i,pack)){failed_=true;return 0;}
        // Stock Wake already waits for this exact pack. Complete that wait
        // before inspecting model entries, under the host's re-entry guard.
        if(!xyz_xbox_pack_idle(&g,pack) && !call(g,XYZ_G_PACK_WAIT,1,&packs_[i].file,ignored)){failed_=true;return 0;}
        int result=xyz_xbox_wake_prepare(&g,pack,&plan);
        if(result!=1 || !xyz_xbox_wake_begin(&g,&plan)){failed_=true;return 0;}
        return 1;
    }
    return 0;
}
void Assets::after_wake(const XyzXboxGuest &g,XyzXboxWakePlan &plan) {
    if(!xyz_xbox_wake_finish(&g,&plan)){failed_=true;return;}
    // The next game frame checks any other packs sharing hibernated models.
    fingerprint_=0;dirty_=true;scan_=0;
}
}
