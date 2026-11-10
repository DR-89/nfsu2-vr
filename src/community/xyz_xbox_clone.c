#include "xyz_xbox_remote.h"
#include "xyz_limits.h"
#include <math.h>
#include <string.h>

enum { WORLD=0x4493e0, LOADER=0x45c4a0, HEAD=LOADER+0x58,
       RECORD_SIZE=0x940, MAX_CARS=XYZ_WORLD_CAR_LIMIT, DEBUG_ARRAYS=0x456048, DEBUG_BYTES=6*22*4 };
static int rd(const XyzXboxGuest *g,uint32_t a,void *b,size_t n) {
    return g && g->read && xyz_xbox_address(a,n) && g->read(g->context,a,b,n);
}
static int wr(const XyzXboxGuest *g,uint32_t a,const void *b,size_t n) {
    return g && g->write && xyz_xbox_address(a,n) && g->write(g->context,a,b,n);
}
static uint32_t word(const uint8_t *b) {
    return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}
static int r32(const XyzXboxGuest *g,uint32_t a,uint32_t *v) {
    uint8_t b[4];if(!rd(g,a,b,4))return 0;*v=word(b);return 1;
}
static int w32(const XyzXboxGuest *g,uint32_t a,uint32_t v) {
    uint8_t b[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)};return wr(g,a,b,4);
}
static int overlaps(XyzXboxArena a,uint32_t address,size_t n) {
    return (uint64_t)a.base<(uint64_t)address+n && (uint64_t)address<(uint64_t)a.base+a.size;
}
static int invoke(const XyzXboxGuest *g,enum XyzXboxOp op,uint32_t self,uint32_t second,
                  unsigned n,const uint32_t *args,uint32_t *out) {
    uint32_t ignored=0;
    return g && g->call && g->call(g->context,op,self,second,n,args,out?out:&ignored);
}
/* Cars outside IndexCars start with index 0. Constructor/destructor and
 * driver/physics setters still write six 22-entry diagnostic arrays. Preserve
 * these around our calls, including the player's index-zero entries. */
static int car_call(const XyzXboxGuest *g,enum XyzXboxOp op,uint32_t self,uint32_t second,
                    unsigned n,const uint32_t *args) {
    uint8_t saved[DEBUG_BYTES];if(!rd(g,DEBUG_ARRAYS,saved,sizeof saved))return 0;
    int ok=invoke(g,op,self,second,n,args,0);
    return wr(g,DEBUG_ARRAYS,saved,sizeof saved) && ok;
}
static int cars(const XyzXboxGuest *g,uint32_t world,uint32_t *list,unsigned *count) {
    uint32_t current,at,prev=world;*count=0;
    if(!r32(g,WORLD,&current) || current!=world || !xyz_xbox_address(world,0x118) ||
       !r32(g,world,&at))return 0;
    for(unsigned i=0;i<MAX_CARS;i++) {
        uint32_t back,next;
        if(at==world)return r32(g,world+4,&back) && back==prev;
        if(!xyz_xbox_address(at,0x9e0) || !r32(g,at+4,&back) || back!=prev ||
           !r32(g,at,&next) || next==at)return 0;
        list[(*count)++]=at;prev=at;at=next;
    }
    return 0;
}
/* -1 means corrupt, 0 means already removed, 1 means present. Never hand an
 * unchecked list entry to the loader's unbounded original implementation. */
static int entry(const XyzXboxGuest *g,uint32_t handle,uint32_t *found) {
    uint32_t at,prev=HEAD;*found=0;if(!r32(g,HEAD,&at))return -1;
    for(unsigned i=0;i<256;i++) {
        uint32_t back,next,h;
        if(at==HEAD)return r32(g,HEAD+4,&back) && back==prev?(*found?1:0):-1;
        if(!xyz_xbox_address(at,0x1240) || !r32(g,at+4,&back) || back!=prev ||
           !r32(g,at,&next) || next==at || !r32(g,at+0x123c,&h))return -1;
        if(h==handle){if(*found)return -1;*found=at;}
        prev=at;at=next;
    }
    return -1;
}
static int begin(const XyzXboxGuest *g,XyzXboxClone *s,XyzXboxArena arena,
                 uint32_t world,uint32_t player,uint32_t skin,uint64_t now,
                 const float position[3]) {
    if(!s || s->stage!=XYZ_CLONE_EMPTY || !g || !g->call || !skin || skin>=XYZ_SKIN_FIRST+XYZ_CARS_MAX ||
       !xyz_xbox_owned(arena,arena.base,0xa00) || overlaps(arena,world,0x118) ||
       overlaps(arena,LOADER,0x60) || overlaps(arena,DEBUG_ARRAYS,DEBUG_BYTES))return 0;
    uint32_t list[MAX_CARS],original=0;unsigned count;uint8_t used[256]={0};int have_player=0;
    if(!cars(g,world,list,&count))return 0;
    for(unsigned i=0;i<count;i++) {
        uint32_t rec,kind;uint8_t number[2];
        if(!r32(g,list[i]+0x14,&rec) || !xyz_xbox_address(rec,RECORD_SIZE) ||
           !r32(g,rec+4,&kind) || !rd(g,list[i]+0xa,number,2))return 0;
        /* Car's constructor 0x1F441F sign-extends the record's byte into
         * its 16-bit number. Normalize both halves of that same namespace. */
        if(number[1]==0 || (number[1]==255 && number[0]>=128))used[number[0]]=1;
        if(!original && kind==2)original=rec;
        if(list[i]==player)have_player=1;
        if(overlaps(arena,rec,RECORD_SIZE) || overlaps(arena,list[i],0x9e0))return 0;
    }
    unsigned number=XYZ_CAR_NUMBER_FIRST;while(number<255 && used[number])++number;
    if(!have_player || !original || number==255)return 0;
    uint8_t record[RECORD_SIZE];if(!rd(g,original,record,sizeof record))return 0;
    record[1]=(uint8_t)number;record[0x900]=0;memset(record+4,0,4);record[4]=4; /* excluded from IndexCars */
    memset(s,0,sizeof *s);s->arena=arena;s->record=arena.base;s->grid=arena.base+0x960;
    s->world=world;s->skin=skin;s->started=now;s->stage=XYZ_CLONE_FAILED;
    if(!wr(g,s->record,record,sizeof record))return 0;
    /* CreateCar runs a physics pass which requires the original ride's live
     * engine tables. A freshly decoded network ride has no such tables.
     * Bootstrap from this owned copy, then configure the network appearance
     * through SetRide after creation. The bootstrap is never displayed. */
    if(!invoke(g,XYZ_G_RIDE_SKINS,s->record+0x10,0,1,&skin,0))return 0;
    uint8_t hashes[12];if(!rd(g,s->record+0x10+0x584,hashes,sizeof hashes))return 0;
    for(unsigned i=0;i<count;i++) {
        uint32_t ride;uint8_t other[12];
        if(!r32(g,list[i]+0x10,&ride) || !rd(g,ride+0x584,other,sizeof other))return 0;
        for(unsigned j=0;j<3;j++)if(word(hashes+4*j) && word(hashes+4*j)==word(other+4*j))return 0;
    }
    float pos[3];
    if(position)memcpy(pos,position,sizeof pos);
    else if(!rd(g,player+0x60,pos,sizeof pos))return 0;
    for(unsigned i=0;i<3;i++)if(!isfinite(pos[i]))return 0;
    if(!position){pos[0]+=6.f;pos[2]+=.5f;}
    uint8_t grid[32]={0};memcpy(grid,pos,sizeof pos);
    if(!wr(g,s->grid,grid,sizeof grid))return 0;
    uint32_t args[3]={s->record+0x10,0,0};
    if(!invoke(g,XYZ_G_LOAD,LOADER,0,3,args,&s->handle) || !s->handle)return 0;
    uint32_t zeros[2]={0};if(!invoke(g,XYZ_G_SERVICE,LOADER,0,2,zeros,0))return 0;
    s->stage=XYZ_CLONE_LOADING;return 1;
}
int xyz_xbox_clone_begin(const XyzXboxGuest *g,XyzXboxClone *s,XyzXboxArena arena,
                         uint32_t world,uint32_t player,uint32_t skin,uint64_t now) {
    return begin(g,s,arena,world,player,skin,now,0);
}
int xyz_xbox_clone_begin_at(const XyzXboxGuest *g,XyzXboxClone *s,XyzXboxArena arena,
                         uint32_t world,uint32_t player,uint32_t skin,
                         const float position[3],uint64_t now) {
    if(!position)return 0;
    for(unsigned i=0;i<3;i++)if(!isfinite(position[i]) || fabsf(position[i])>(i==2?2000.f:20000.f))return 0;
    return begin(g,s,arena,world,player,skin,now,position);
}
static int owned_car(const XyzXboxGuest *g,const XyzXboxClone *s) {
    uint32_t world;
    return s->car && r32(g,WORLD,&world) && world==s->world &&
        xyz_xbox_owned(s->arena,s->record,RECORD_SIZE) &&
        xyz_xbox_find_car(g->read,g->context,world,s->car,s->record)==s->car;
}
static int inert(const XyzXboxGuest *g,XyzXboxClone *s) {
    uint32_t driver,physics,body,zero=0;uint8_t disabled;
    if(!r32(g,s->car+0x24,&driver) || !r32(g,s->car+0x28,&physics) ||
       (driver && !car_call(g,XYZ_G_SET_DRIVER,s->car,0,1,&zero)) ||
       (physics && !car_call(g,XYZ_G_SET_PHYS,s->car,0,1,&zero)) ||
       !r32(g,s->car+0x3c,&body) || !rd(g,body+0x17,&disabled,1))return 0;
    /* Body::Disable 0x19e1c0 increments this byte. Only our new body's
     * disable count is changed, never a player's or stock opponent's. */
    disabled=disabled?disabled:1;return wr(g,body+0x17,&disabled,1);
}
static int release_handle(const XyzXboxGuest *g,uint32_t *handle) {
    if(!*handle)return 1;
    uint32_t e;int present=entry(g,*handle,&e);if(present<0)return 0;
    if(present && !invoke(g,XYZ_G_UNLOAD,LOADER,0,1,handle,0))return 0;
    *handle=0;return 1;
}
int xyz_xbox_clone_configure(const XyzXboxGuest *g,XyzXboxClone *s,
                             const uint8_t *wire,int size,uint64_t now,XyzXboxRideResult *result) {
    if(!s || s->stage!=XYZ_CLONE_READY || !result || !owned_car(g,s) ||
       !xyz_xbox_owned(s->arena,s->arena.base,0x2400))return 0;
    uint32_t first=s->arena.base+0x1000;
    s->pending_ride=s->look_ride==first?first+0x900:first;
    if(!xyz_xbox_build_ride(g,s->arena,s->pending_ride,s->arena.base+0x2300,s->skin,wire,size,result))return 0;
    s->stage=XYZ_CLONE_FAILED;s->started=now;
    uint32_t args[3]={s->pending_ride,0,0},zeros[2]={0};
    if(!invoke(g,XYZ_G_LOAD,LOADER,0,3,args,&s->pending_handle) || !s->pending_handle ||
       !invoke(g,XYZ_G_SERVICE,LOADER,0,2,zeros,0))return 0;
    s->stage=XYZ_CLONE_LOOK_LOADING;return 1;
}
enum XyzXboxCloneStage xyz_xbox_clone_poll(const XyzXboxGuest *g,XyzXboxClone *s,uint64_t now) {
    if(!s)return XYZ_CLONE_FAILED;
    if(s->stage!=XYZ_CLONE_LOADING && s->stage!=XYZ_CLONE_LOOK_LOADING)return s->stage;
    int look=s->stage==XYZ_CLONE_LOOK_LOADING;
    uint32_t handle=look?s->pending_handle:s->handle;
    uint32_t world,e,done;
    if(!r32(g,WORLD,&world) || world!=s->world || now<s->started || now-s->started>30000000 ||
       entry(g,handle,&e)!=1 || !invoke(g,XYZ_G_LOADED,LOADER,0,1,&e,&done))
        return s->stage=XYZ_CLONE_FAILED;
    if(!done)return s->stage;
    /* Wait is used only after Loaded, never as an indefinite per-frame wait. */
    if(!invoke(g,XYZ_G_WAIT,LOADER,0,1,&handle,0))return s->stage=XYZ_CLONE_FAILED;
    if(look) {
        /* SetRide copies into our record, rebuilds body/render data and
         * physics, and must not be called until the loader is finished. */
        if(!owned_car(g,s) || !car_call(g,XYZ_G_SET_RIDE,s->car,0,1,&s->pending_ride) ||
           !inert(g,s) || !release_handle(g,&s->look_handle))return s->stage=XYZ_CLONE_FAILED;
        s->look_handle=s->pending_handle;s->look_ride=s->pending_ride;
        s->pending_handle=s->pending_ride=0;
        return s->stage=XYZ_CLONE_READY;
    }
    uint32_t list[MAX_CARS];unsigned count;
    if(!cars(g,s->world,list,&count))return s->stage=XYZ_CLONE_FAILED;
    uint32_t zeros[2]={0};
    int made=car_call(g,XYZ_G_CREATE,s->grid,s->record,2,zeros);
    s->car=xyz_xbox_find_car(g->read,g->context,s->world,0,s->record);
    if(!made || !s->car)return s->stage=XYZ_CLONE_FAILED;
    if(!inert(g,s))return s->stage=XYZ_CLONE_FAILED;
    s->stage=XYZ_CLONE_READY;
    if(!xyz_xbox_clone_hide(g,s))return s->stage=XYZ_CLONE_FAILED;
    return s->stage;
}
int xyz_xbox_clone_retire(const XyzXboxGuest *g,XyzXboxClone *s) {
    if(!s || !g)return 0;if(s->stage==XYZ_CLONE_EMPTY)return 1;
    uint32_t world;if(!r32(g,WORLD,&world))return 0;
    if(s->car && world==s->world) {
        uint32_t list[MAX_CARS];unsigned count;if(!cars(g,world,list,&count))return 0;
        int exists=0;for(unsigned i=0;i<count;i++)if(list[i]==s->car)exists=1;
        if(exists) {
            uint32_t rec,next,prev;
            if(!r32(g,s->car+0x14,&rec) || rec!=s->record ||
               !xyz_xbox_owned(s->arena,rec,RECORD_SIZE) ||
               !r32(g,s->car,&next) || !r32(g,s->car+4,&prev))return 0;
            /* Exactly the game's world destructor order: unlink, destroy,
             * free. Borrowed cars never pass the record ownership check. */
            if(!w32(g,prev,next) || !w32(g,next+4,prev) ||
               !car_call(g,XYZ_G_DESTROY,s->car,0,0,0) ||
               !invoke(g,XYZ_G_FREE,0,0,1,&s->car,0))return 0;
        }
        s->car=0;
    }
    if(!release_handle(g,&s->pending_handle) || !release_handle(g,&s->look_handle) ||
       !release_handle(g,&s->handle))return 0;
    memset(s,0,sizeof *s);return 1;
}

/* Pose/wheel conventions follow the MIT upstream adapter (vendor/roam/LICENSE),
 * with independently reviewed Xbox layouts. Section may snap the origin on
 * entering loaded track, so restore the received COM/basis and rotated
 * rendering offset after that call instead of flattening airborne cars. */
static int finite_range(float x,float low,float high) {return isfinite(x) && x>=low && x<=high;}
static int valid_pose(const rc_pose *p,float dt) {
    if(!p || !finite_range(dt,0,10) || p->mode<RC_MODE_INTERP || p->mode>RC_MODE_HOLD ||
       !finite_range(p->steer,-1,1) || !finite_range(p->throttle,0,1) || !finite_range(p->brake,0,1) ||
       !finite_range(p->speed,-327.68f,327.67f) || !finite_range(p->rpm,0,65535) || p->gear < -1 || p->gear>8)return 0;
    float vv=0,qq=0;
    for(unsigned i=0;i<3;i++) {
        float bound=i==2?2000.f:20000.f;
        if(!finite_range(p->p[i],-bound,bound) || !finite_range(p->v[i],-200,200) ||
           !finite_range(p->w[i],-32.768f,32.768f))return 0;
        vv+=p->v[i]*p->v[i];
    }
    for(unsigned i=0;i<4;i++) {
        if(!finite_range(p->q[i],-2,2) || (p->ext && !finite_range(p->wz[i],-32.768f,32.768f)))return 0;
        qq+=p->q[i]*p->q[i];
    }
    for(unsigned i=0;i<8;i++)if(p->slipx && !finite_range(p->slip[i],-64,64))return 0;
    return vv<=40000.1f && qq>=.25f && qq<=4.f;
}
static float real(const uint8_t *b) {uint32_t bits=word(b);float f;memcpy(&f,&bits,4);return f;}
static int wf(const XyzXboxGuest *g,uint32_t a,float v) {return wr(g,a,&v,4);}
int xyz_xbox_clone_hide(const XyzXboxGuest *g,XyzXboxClone *s) {
    if(!s || !owned_car(g,s))return 0;
    uint32_t car=s->car;uint8_t inactive=0;float parked[3]={0,0,-123456.f},zero[8]={0};
    if(!inert(g,s) || !wr(g,car+0x550,&inactive,1) ||
       !wr(g,car+0x60,parked,sizeof parked) || !wr(g,car+0x50,parked,sizeof parked) ||
       !wr(g,car+0x70,zero,12) || !wr(g,car+0x110,zero,12) ||
       !wr(g,car+0x42c,zero,8) || !wf(g,car+0x440,0) ||
       !wr(g,car+0x330,zero,16) || !wr(g,car+0x380,zero,16) ||
       !wr(g,car+0x3b4,zero,32) || !w32(g,car+0x460,0) ||
       !car_call(g,XYZ_G_SECTION,car,0,0,0))return 0;
    s->visible=0;return 1;
}
int xyz_xbox_clone_pose(const XyzXboxGuest *g,XyzXboxClone *s,const rc_pose *p,float dt) {
    if(!s || (s->stage!=XYZ_CLONE_READY && s->stage!=XYZ_CLONE_LOOK_LOADING) ||
       !valid_pose(p,dt) || !owned_car(g,s))return 0;
    uint32_t car=s->car,ph,ctl;uint8_t geometry[0xc0];
    if(!r32(g,car+0x20,&ph) || !rd(g,ph,geometry,sizeof geometry) ||
       !r32(g,car+0x30,&ctl) || !xyz_xbox_address(ctl,0x220))return 0;
    float com[3],wheelpos[4][3],radius[4];
    for(unsigned i=0;i<3;i++) {
        com[i]=real(geometry+4*i);if(!finite_range(com[i],-3,3))return 0;
    }
    for(unsigned w=0;w<4;w++) {
        for(unsigned i=0;i<3;i++) {
            wheelpos[w][i]=real(geometry+0x10+0x30*w+4*i);
            if(!finite_range(wheelpos[w][i],i?-2.f:-4.f,i?2.f:4.f))return 0;
        }
        radius[w]=real(geometry+0x20+0x30*w);
        if(!finite_range(radius[w],.05f,2.f))radius[w]=.33f;
    }
    float q[4],basis[3][3],qn=0;
    for(unsigned i=0;i<4;i++)qn+=p->q[i]*p->q[i];
    qn=1.f/rc_sqrtf(qn);for(unsigned i=0;i<4;i++)q[i]=p->q[i]*qn;
    rc_q2m(q,basis[0],basis[1],basis[2]);
    if(!s->visible && !car_call(g,XYZ_G_ACTIVATE,car,0,0,0))return 0;
    if(!inert(g,s))return 0;
    uint8_t headlights=p->hibeam || !(p->bits&RC_BIT_DARK);
    if(!wr(g,car+0x93d,&headlights,1))return 0;
    if(!wr(g,car+0x60,p->p,12) || !wr(g,car+0x70,p->v,12) || !wr(g,car+0x110,p->w,12))return 0;
    uint16_t heading=(uint16_t)(int)(rc_atan2f(basis[0][1],basis[0][0])*10430.378f);
    uint16_t steer=(uint16_t)(int16_t)(p->steer*32767.f);
    if(!wr(g,car+0x40c,&heading,2) || !wf(g,car+0x42c,p->speed) || !wf(g,car+0x430,p->speed) ||
       !wf(g,car+0x440,p->rpm) || !w32(g,car+0x434,(uint32_t)(int32_t)p->gear) ||
       !wf(g,ctl+0x20c,p->throttle) || !wf(g,ctl+0x214,p->brake) ||
       !wf(g,ctl+0x210,(p->bits&RC_BIT_HANDBRAKE)?1.f:0.f) || !wr(g,ctl+0x1e0,&steer,2))return 0;
    uint32_t position=car+0x60;
    if(!invoke(g,XYZ_G_ROAD,car+0x8d8,0,1,&position,0) || !car_call(g,XYZ_G_SECTION,car,0,0,0))return 0;
    if(!wr(g,car+0x60,p->p,12))return 0;
    for(unsigned i=0;i<3;i++)
        if(!wr(g,car+0x90+16*i,basis[i],12) || !wr(g,car+0xd0+16*i,basis[i],12))return 0;
    float origin[3];for(unsigned i=0;i<3;i++)origin[i]=p->p[i]-(com[0]*basis[0][i]+com[1]*basis[1][i]+com[2]*basis[2][i]);
    if(!wr(g,car+0x50,origin,sizeof origin))return 0;
    if(dt>.1f)dt=.1f;
    float wanted=p->steer*3.14159265f;if(wanted>.8f)wanted=.8f;if(wanted<-.8f)wanted=-.8f;
    float k=dt/.08f;if(k>1)k=1;
    float steering=isfinite(s->steer)?s->steer:0;steering+=(wanted-steering)*k;
    float ss=sinf(steering),cs=cosf(steering),spins[4];
    for(unsigned w=0;w<4;w++) {
        float slip=p->slipx?p->slip[2*w]:0,across=p->slipx?p->slip[2*w+1]:0;
        float speed=(p->speed+slip)/radius[w],delta=speed*dt;
        if(!finite_range(delta,-32,32))delta=0;
        float angle=isfinite(s->spin[w])?s->spin[w]:0;
        angle=remainderf(angle-delta,6.28318531f);spins[w]=angle;
        float sa=sinf(angle),ca=cosf(angle),sy=w<2?ss:0,cy=w<2?cs:1;
        float z=wheelpos[w][2];if(p->ext && p->wz[w]>-2 && p->wz[w]<2)z=p->wz[w];
        float wheel[16]={ca*cy,ca*sy,sa,0,-sy,cy,0,0,-sa*cy,-sa*sy,ca,0,wheelpos[w][0],wheelpos[w][1],z,1};
        float brake[16]={cy,sy,0,0,-sy,cy,0,0,0,0,1,0,wheelpos[w][0],wheelpos[w][1],z,1};
        uint8_t grounded=(slip!=0 || across!=0);
        if(!wr(g,car+0x130+0x40*w,wheel,sizeof wheel) || !wr(g,car+0x230+0x40*w,brake,sizeof brake) ||
           !wf(g,car+0x330+4*w,speed) || !wf(g,car+0x380+4*w,z-wheelpos[w][2]) ||
           !wf(g,car+0x3b4+8*w,slip) || !wf(g,car+0x3b8+8*w,across) ||
           !wr(g,car+0x390+w,&grounded,1) || (!grounded && !wf(g,car+0x99c+4*w,0)))return 0;
        if(p->slipx && p->surf[w]<29 && !w32(g,car+0x394+4*w,p->surf[w]))return 0;
    }
    uint16_t wheelsteer=(uint16_t)(int16_t)(steering*10430.378f);
    if(!wr(g,car+0x40e,&wheelsteer,2))return 0;
    memcpy(s->spin,spins,sizeof spins);s->steer=steering;s->visible=1;return 1;
}
