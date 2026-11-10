#include "copchase_roads.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

struct CopChaseRoads {
    CopChaseRoadCell cells[65536];
    struct History { uint32_t car,generation,ms; float position[3]; } history[64];
    uint32_t generation,last_ms;
    unsigned count;
};
static_assert(sizeof(CopChaseRoadCell)==32,"road cell layout");
namespace {
bool finite(float x,float limit=90000.f){return std::isfinite(x)&&std::fabs(x)<=limit;}
bool vector_ok(const float *p,unsigned n,float limit=90000.f){
    if(!p)return false;for(unsigned i=0;i<n;++i)if(!finite(p[i],limit))return false;return true;
}
float length(float x,float y){return float(std::sqrt(double(x*x+y*y)));}
bool direction_ok(const float *p){return vector_ok(p,2,1.01f)&&p[0]*p[0]+p[1]*p[1]>=.98f&&p[0]*p[0]+p[1]*p[1]<=1.02f;}
uint32_t key(int x,int y){return (uint32_t(x+0x4000)<<16)|(uint32_t(y-0x8000)&0xffff);}
int grid(float f){return int(std::floor(double(f/6.f)));}
CopChaseRoadCell *lookup(CopChaseRoads *r,int x,int y,bool create){
    const uint32_t k=key(x,y);if(!k)return nullptr;
    const unsigned hash=(k*0x9e3779b1u)>>16;
    for(unsigned i=0;i<64;++i){
        auto &c=r->cells[(hash+i)&65535];
        if(c.key==k)return &c;
        if(!c.key){if(!create||r->count>0xf000)return nullptr;c={};c.key=k;++r->count;return &c;}
    }return nullptr;
}
const CopChaseRoadCell *lookup(const CopChaseRoads *r,int x,int y){return lookup(const_cast<CopChaseRoads *>(r),x,y,false);}
bool read(const CopChaseRoadHost *h,uint32_t a,void *p,uint32_t n){
    return h&&h->read&&a>=0x10000&&n&&a<=UINT32_MAX-n&&h->read(h->context,a,p,n)!=0;
}
template<typename T>bool get(const CopChaseRoadHost *h,uint32_t a,T &v){return read(h,a,&v,sizeof v);}
bool equal(const CopChaseRoadHost *h,uint32_t a,uint32_t b){uint32_t v=0;return get(h,a,v)&&v==b;}
bool identity(const CopChaseRoadHost *h,const CopChaseRoadIdentity *id,bool traffic){
    if(!h||!id||!h->identity||id->slot>=64||!h->identity(h->context,id,traffic?1:0))return false;
    const uint32_t pointers[]={id->car,id->driver,id->control,id->type,id->mover,id->facade,id->body};
    for(uint32_t p:pointers)if(p&&(p<0x10000||p>0xfffff000u||(p&3)))return false;
    if(!id->car||!id->type||!equal(h,id->car+0xc,id->type))return false;
    if(!traffic)return true;
    return id->driver&&id->control&&equal(h,id->car+0x24,4)&&equal(h,id->car+0x2c,id->driver)&&
        equal(h,id->car+0x30,id->control)&&equal(h,id->driver,0x341a70)&&
        equal(h,id->driver+4,id->car)&&equal(h,id->driver+8,id->control)&&
        equal(h,id->control,0x3414e8)&&equal(h,id->control+0x220,id->car);
}
template<typename F>void scan(const CopChaseRoads *r,float x,float y,float radius,F fn){
    const int xmax=grid(x+radius),ymax=grid(y+radius);
    for(int ix=grid(x-radius);ix<=xmax;++ix)for(int iy=grid(y-radius);iy<=ymax;++iy){
        const auto *c=lookup(r,ix,iy);if(!c||!c->samples)continue;
        const float dx=c->position[0]-x,dy=c->position[1]-y;
        if(dy*dy+dx*dx<=radius*radius)fn(*c);
    }
}
void write3(CopChaseRoadStage &s,uint32_t address,const float *v){
    auto &w=s.writes[s.count++];w.address=address;w.size=12;std::memcpy(w.bytes,v,12);
}
}
extern "C" CopChaseRoads *copchase_roads_create(void){return static_cast<CopChaseRoads *>(std::calloc(1,sizeof(CopChaseRoads)));}
extern "C" void copchase_roads_destroy(CopChaseRoads *r){std::free(r);}
extern "C" void copchase_roads_reset(CopChaseRoads *r,uint32_t generation){if(r){std::memset(r,0,sizeof *r);r->generation=generation;}}
extern "C" int copchase_roads_observe(CopChaseRoads *r,const float p[3],const float d[2],uint32_t now){
    if(!r||!vector_ok(p,3)||!direction_ok(d))return 0;
    auto *c=lookup(r,grid(p[0]),grid(p[1]),true);if(!c)return 0;
    if(!c->samples){std::memcpy(c->position,p,12);std::memcpy(c->direction,d,8);c->samples=1;}
    else{
        float dx=d[0],dy=d[1];if(c->direction[1]*dy+c->direction[0]*dx<0){dx=-dx;dy=-dy;}
        const float a=c->samples<20?1.f/float(c->samples+1):.05f;
        dx=(dx-c->direction[0])*a+c->direction[0];dy=(dy-c->direction[1])*a+c->direction[1];
        for(unsigned i=0;i<3;++i)c->position[i]=(p[i]-c->position[i])*a+c->position[i];
        const float norm=length(dx,dy)+1e-6f;c->direction[0]=dx/norm;c->direction[1]=dy/norm;
        if(c->samples<60000)++c->samples;
    }c->last_ms=now;return 1;
}
extern "C" int copchase_roads_cell(const CopChaseRoads *r,int x,int y,CopChaseRoadCell *out){
    if(out)*out={};if(!r||!out||x < -15000||x>15000||y < -15000||y>15000)return 0;
    const auto *c=lookup(r,x,y);if(!c||!c->samples)return 0;*out=*c;return 1;
}
extern "C" int copchase_roads_census(CopChaseRoads *r,const CopChaseRoadHost *h,uint32_t generation,uint32_t now,
                                     const CopChaseRoadCar *cars,unsigned count,CopChaseRoadCensus *out){
    if(out)*out={};if(!r||!h||!out||count>64||(count&&!cars))return 0;
    uint64_t seen=0;for(unsigned i=0;i<count;++i){const unsigned slot=cars[i].identity.slot;
        if(slot>=64||(seen&(uint64_t(1)<<slot)))return 0;seen|=uint64_t(1)<<slot;}
    if(r->generation!=generation)copchase_roads_reset(r,generation);
    out->cells=r->count;if(uint32_t(now-r->last_ms)<250)return 1;r->last_ms=now;out->updated=1;
    for(unsigned i=0;i<count;++i){const auto &car=cars[i];const auto &id=car.identity;
        if(car.disabled||car.held)continue;
        if(id.world_generation!=generation||!identity(h,&id,true)){++out->rejected_cars;continue;}
        ++out->valid_cars;uint8_t n=0;
        if(get(h,id.control+0x10,n)&&n>=2&&n<=20){
            float points[20][4]{};
            if(read(h,id.control+0x20,points,uint32_t(n)*16))for(unsigned j=0;j+1<n;++j){
                const float *a=points[j],*b=points[j+1];if(!vector_ok(a,3)||!vector_ok(b,3))continue;
                const float dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2],len=length(dx,dy);
                if(len<.5f||len>60||std::fabs(dz)>6)continue;++out->path_segments;
                const float d[2]={dx/len,dy/len};for(float t=0;t<len;t+=5){
                    const float p[3]={(t*dx)/len+a[0],(t*dy)/len+a[1],(dz*t)/len+a[2]};
                    out->samples+=copchase_roads_observe(r,p,d,now);
                }
            }
        }
        uint32_t mode=0;float p[3];
        if(car.selected||!get(h,id.car+0x28,mode)||(mode!=2&&mode!=3)||!read(h,id.car+0x50,p,12)||!vector_ok(p,3))continue;
        auto &old=r->history[id.slot];
        if(old.car==id.car&&old.generation==id.identity_generation&&old.ms){
            const uint32_t elapsed=now-old.ms;const float dt=float(elapsed)/1000.f;
            const float dx=p[0]-old.position[0],dy=p[1]-old.position[1],dist=length(dx,dy);
            if(elapsed&&dt<1&&dist>1&&dist/dt<60&&std::fabs(p[2]-old.position[2])<2){
                const float d[2]={dx/dist,dy/dist};const int added=copchase_roads_observe(r,p,d,now);
                out->samples+=added;out->movement_samples+=added;
            }
        }
        old.car=id.car;old.generation=id.identity_generation;old.ms=now;std::memcpy(old.position,p,12);
    }out->cells=r->count;return 1;
}
extern "C" int copchase_roads_query(const CopChaseRoads *r,const CopChaseRoadQuery *in,CopChaseRoadSite *out){
    if(out)*out={};if(!r||!in||!out||!vector_ok(in->position,3,89000)||!vector_ok(in->forward,2,2)||
       !vector_ok(in->velocity,2,300)||!finite(in->speed,300)||in->speed<0)return 0;
    const float fn=length(in->forward[0],in->forward[1])+1e-6f;if(fn<.1f)return 0;
    float travel[2];for(unsigned i=0;i<2;++i)travel[i]=in->speed<=5?in->forward[i]/fn:in->velocity[i]/in->speed;
    if(!vector_ok(travel,2,2)||(!in->debug&&(travel[0]*in->forward[0]+travel[1]*in->forward[1])/fn<.7f))return 0;
    float p[3];std::memcpy(p,in->position,12);float d[2]={travel[0],travel[1]},best=144;
    const CopChaseRoadCell *initial=nullptr;
    scan(r,p[0],p[1],12,[&](const CopChaseRoadCell &c){
        const float dx=c.position[0]-p[0],dy=c.position[1]-p[1],distance=dy*dy+dx*dx;
        if(std::fabs(c.direction[1]*travel[1]+c.direction[0]*travel[0])>=.7f&&std::fabs(c.position[2]-p[2])<=3&&distance<best){best=distance;initial=&c;}
    });
    if(initial){const float sign=travel[1]*initial->direction[1]+travel[0]*initial->direction[0]<=0?-1.f:1.f;
        d[0]=initial->direction[0]*sign;d[1]=initial->direction[1]*sign;}
    float walked=0;unsigned misses=0,snaps=0;const CopChaseRoadCell *site=nullptr;
    const float minimum=in->debug?50.f:140.f,target=in->debug?90.f:190.f,maximum=in->debug?240.f:280.f;
    for(unsigned step=0;step<16&&walked<target;++step){
        const float q[2]={d[0]*20+p[0],d[1]*20+p[1]};best=196;const CopChaseRoadCell *next=nullptr;
        scan(r,q[0],q[1],14,[&](const CopChaseRoadCell &c){
            const float dx=c.position[0]-q[0],dy=c.position[1]-q[1],distance=dy*dy+dx*dx;
            if(std::fabs(c.position[2]-p[2])<=4&&std::fabs(d[0]*c.direction[0]+d[1]*c.direction[1])>=.8f&&distance<best&&
               (c.position[1]-p[1])*d[1]+(c.position[0]-p[0])*d[0]>=8){best=distance;next=&c;}
        });
        if(!next){if(++misses>2)break;walked+=20;p[0]=q[0];p[1]=q[1];continue;}
        const float sign=next->direction[1]*d[1]+next->direction[0]*d[0]<=0?-1.f:1.f;
        walked+=length(next->position[0]-p[0],next->position[1]-p[1]);std::memcpy(p,next->position,12);
        d[0]=next->direction[0]*sign;d[1]=next->direction[1]*sign;misses=0;++snaps;if(walked>=minimum)site=next;
    }
    if(!site||walked>maximum)return 0;
    CopChaseRoadSite result{};std::memcpy(result.centre,site->position,12);std::memcpy(result.forward,d,8);std::memcpy(result.travel,travel,8);
    result.walked=walked;result.snaps=snaps;
    scan(r,site->position[0],site->position[1],22,[&](const CopChaseRoadCell &c){
        const float dx=c.position[0]-site->position[0],dy=c.position[1]-site->position[1],lateral=dy*d[0]-dx*d[1];
        if(std::fabs(d[1]*dy+d[0]*dx)<=15&&std::fabs(lateral)<=14&&std::fabs(d[0]*c.direction[0]+d[1]*c.direction[1])>=.9f&&
           std::fabs(c.position[2]-site->position[2])<=3){result.lane_min=std::min(result.lane_min,lateral);result.lane_max=std::max(result.lane_max,lateral);++result.width_samples;}
    });*out=result;return 1;
}
extern "C" int copchase_roads_wheels(const CopChaseRoadHost *h,const CopChaseRoadIdentity *id,CopChaseRoadWheels *out){
    if(out)*out={};if(!out||!identity(h,id,false))return 0;
    float p[3],basis[3][4];CopChaseRoadWheels result{};
    if(!read(h,id->car+0x50,p,12)||!read(h,id->car+0x90,basis,48)||!vector_ok(p,3))return 0;
    for(unsigned j=0;j<3;++j)if(!vector_ok(basis[j],3,1.01f))return 0;
    for(unsigned i=0;i<4;++i){
        if(!read(h,id->type+0x120+i*0x30,result.local[i],12)||!vector_ok(result.local[i],3,10))return 0;
        for(unsigned j=0;j<3;++j)result.world[i][j]=result.local[i][0]*basis[0][j]+p[j]+result.local[i][1]*basis[1][j]+result.local[i][2]*basis[2][j];
    }*out=result;return 1;
}
extern "C" int copchase_roads_stage_plan(const CopChaseRoadHost *h,const CopChaseRoadIdentity *id,const float p[3],const float d[2],CopChaseRoadStage *out){
    if(out)*out={};if(!out||!vector_ok(p,3)||!direction_ok(d)||!identity(h,id,true))return 0;
    if(!id->mover||!id->facade||!id->body||!equal(h,id->car+0x28,1)||!equal(h,id->car+0x34,id->mover)||
       !equal(h,id->car+0x3c,id->facade)||!equal(h,id->facade,0x35c0f8)||!equal(h,id->facade+0x130,id->car)||
       !equal(h,id->facade+0x68,id->body)||!equal(h,id->mover+0x20,id->body)||!equal(h,id->body,0x35bd2c))return 0;
    CopChaseRoadStage result{};const float rows[3][3]={{d[0],d[1],0},{-d[1],d[0],0},{0,0,1}},zero[3]={0,0,0};
    write3(result,id->body+0x20,p);write3(result,id->body+0x100,p);
    for(unsigned i=0;i<3;++i){write3(result,id->body+0x30+i*16,rows[i]);write3(result,id->body+0xd0+i*16,rows[i]);}
    write3(result,id->car+0x70,zero);write3(result,id->facade+0xc0,zero);
    write3(result,id->body+0xa0,zero);write3(result,id->body+0x70,zero);write3(result,id->body+0x80,zero);
    unsigned char scratch[12];for(unsigned i=0;i<result.count;++i)if(!read(h,result.writes[i].address,scratch,12))return 0;
    *out=result;return 1;
}
extern "C" int copchase_roads_stage(const CopChaseRoadHost *h,const CopChaseRoadIdentity *id,const float p[3],const float d[2]){
    CopChaseRoadStage plan{};return h&&h->write_batch&&copchase_roads_stage_plan(h,id,p,d,&plan)&&h->write_batch(h->context,plan.writes,plan.count);
}
