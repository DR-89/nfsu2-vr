#include "copchase_roads.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {
struct Memory {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x10000,0x7b);
    bool valid=true,writable=true;unsigned writes=0,reads=0;
    static bool range(uint32_t a,uint32_t n){return a>=0x10000&&n<=0x10000&&a<=0x20000-n;}
    static int read(void *ctx,uint32_t a,void *out,uint32_t n){
        auto &m=*static_cast<Memory *>(ctx);++m.reads;
        if(!range(a,n))return 0;std::memcpy(out,m.bytes.data()+a-0x10000,n);return 1;
    }
    static int identity(void *ctx,const CopChaseRoadIdentity *id,int){
        return static_cast<Memory *>(ctx)->valid&&id->world_generation==7&&id->identity_generation==3;
    }
    static int write(void *ctx,const CopChaseRoadWrite *w,unsigned count){
        auto &m=*static_cast<Memory *>(ctx);if(!m.writable)return 0;
        for(unsigned i=0;i<count;++i)if(!range(w[i].address,w[i].size)||w[i].size!=12)return 0;
        for(unsigned i=0;i<count;++i){std::memcpy(m.bytes.data()+w[i].address-0x10000,w[i].bytes,w[i].size);++m.writes;}return 1;
    }
    template<typename T>void set(uint32_t a,T v){assert(range(a,sizeof v));std::memcpy(bytes.data()+a-0x10000,&v,sizeof v);}
    template<typename T>T get(uint32_t a){T v;assert(read(this,a,&v,sizeof v));return v;}
};
CopChaseRoadIdentity fixture(Memory &m){
    CopChaseRoadIdentity id{0x11000,0x12000,0x13000,0x14000,0x15000,0x16000,0x17000,7,3,0};
    m.set(id.car+0xc,id.type);m.set(id.car+0x24,4u);m.set(id.car+0x28,2u);
    m.set(id.car+0x2c,id.driver);m.set(id.car+0x30,id.control);m.set(id.driver,0x341a70u);
    m.set(id.driver+4,id.car);m.set(id.driver+8,id.control);m.set(id.control,0x3414e8u);
    m.set(id.control+0x220,id.car);m.set(id.car+0x34,id.mover);m.set(id.car+0x3c,id.facade);
    m.set(id.facade,0x35c0f8u);m.set(id.facade+0x130,id.car);m.set(id.facade+0x68,id.body);
    m.set(id.mover+0x20,id.body);m.set(id.body,0x35bd2cu);
    for(unsigned i=0;i<3;++i){m.set(id.car+0x50+i*4,0.f);for(unsigned j=0;j<3;++j)m.set(id.car+0x90+i*16+j*4,i==j?1.f:0.f);}
    for(unsigned i=0;i<4;++i){m.set(id.type+0x120+i*0x30,i<2?1.5f:-1.5f);m.set(id.type+0x124+i*0x30,i%2?.8f:-.8f);m.set(id.type+0x128+i*0x30,-.3f);}
    m.set(id.control+0x10,uint8_t(20));
    for(unsigned i=0;i<20;++i){m.set(id.control+0x20+i*16,float(i)*10);m.set(id.control+0x24+i*16,0.f);m.set(id.control+0x28+i*16,0.f);}
    return id;
}
void near(float a,float b){assert(std::fabs(a-b)<.0002f);}
}
int main(){
    CopChaseRoads *r=copchase_roads_create();assert(r);copchase_roads_reset(r,7);
    Memory m;const auto id=fixture(m);const CopChaseRoadHost host{&m,Memory::read,Memory::identity,Memory::write};
    CopChaseRoadCar car{id,0,0,0};CopChaseRoadCensus census{};
    assert(copchase_roads_census(r,&host,7,249,&car,1,&census)&&!census.updated);
    assert(copchase_roads_census(r,&host,7,250,&car,1,&census)&&census.updated);
    assert(census.valid_cars==1&&census.path_segments==19&&census.samples==38&&census.cells>0);
    CopChaseRoadCell c{};assert(copchase_roads_cell(r,0,0,&c));near(c.position[0],2.5f);assert(c.samples==2);
    m.set(id.car+0x50,10.f);
    assert(copchase_roads_census(r,&host,7,500,&car,1,&census)&&census.movement_samples==1);
    m.set(id.control+0x10,uint8_t(21));m.set(id.car+0x50,10.f);
    assert(copchase_roads_census(r,&host,7,750,&car,1,&census)&&census.path_segments==0&&census.samples==0);
    m.valid=false;const auto reads=m.reads;assert(copchase_roads_census(r,&host,7,1000,&car,1,&census));
    assert(census.rejected_cars==1&&m.reads==reads);m.valid=true;
    m.set(id.control+0x220,0u);assert(copchase_roads_census(r,&host,7,1250,&car,1,&census)&&census.rejected_cars==1);
    m.set(id.control+0x220,id.car);CopChaseRoadCar duplicates[2]={car,car};
    assert(!copchase_roads_census(r,&host,7,1500,duplicates,2,&census));
    assert(copchase_roads_census(r,&host,8,1500,&car,1,&census)&&census.cells==0&&census.rejected_cars==1);

    copchase_roads_reset(r,7);const float dir[2]={1,0};
    for(int x=0;x<=320;x+=5)for(int y=-6;y<=6;y+=6){const float p[3]={float(x),float(y),3};assert(copchase_roads_observe(r,p,dir,2000));}
    CopChaseRoadQuery query{{0,0,3},{1,0},{20,0},20,0};CopChaseRoadSite site{};
    assert(copchase_roads_query(r,&query,&site));assert(site.centre[0]>=140&&site.centre[0]<220);
    near(site.centre[2],3);near(site.forward[0],1);near(site.lane_min,-6);near(site.lane_max,6);assert(site.width_samples>2);
    query.velocity[0]=-20;assert(!copchase_roads_query(r,&query,&site));query.velocity[0]=20;
    query.position[2]=20;assert(!copchase_roads_query(r,&query,&site));query.position[2]=3;
    query.debug=1;assert(copchase_roads_query(r,&query,&site)&&site.centre[0]<120);
    query.position[0]=std::numeric_limits<float>::quiet_NaN();assert(!copchase_roads_query(r,&query,&site));
    copchase_roads_reset(r,7);query.position[0]=0;assert(!copchase_roads_query(r,&query,&site));
    const float origin[3]={0,0,0},reverse[2]={-1,0};assert(copchase_roads_observe(r,origin,dir,1));assert(copchase_roads_observe(r,origin,reverse,2));
    assert(copchase_roads_cell(r,0,0,&c));near(c.direction[0],1);assert(c.samples==2&&c.last_ms==2);

    CopChaseRoadWheels wheels{};assert(copchase_roads_wheels(&host,&id,&wheels));near(wheels.local[0][0],1.5f);near(wheels.local[2][0],-1.5f);
    near(wheels.world[0][0],11.5f);near(wheels.world[0][1],-.8f);near(wheels.world[0][2],-.3f);
    m.set(id.car+0x90,0.f);m.set(id.car+0x94,1.f);m.set(id.car+0xa0,-1.f);m.set(id.car+0xa4,0.f);
    assert(copchase_roads_wheels(&host,&id,&wheels));near(wheels.world[0][0],10.8f);near(wheels.world[0][1],1.5f);
    m.set(id.type+0x120,std::numeric_limits<float>::infinity());assert(!copchase_roads_wheels(&host,&id,&wheels));m.set(id.type+0x120,1.5f);

    const float target[3]={100,200,3},heading[2]={.6f,.8f};CopChaseRoadStage plan{};
    assert(!copchase_roads_stage_plan(&host,&id,target,heading,&plan)&&!plan.count);m.set(id.car+0x28,1u);
    assert(copchase_roads_stage_plan(&host,&id,target,heading,&plan)&&plan.count==13&&m.writes==0);
    const auto before=m.bytes;m.writable=false;assert(!copchase_roads_stage(&host,&id,target,heading)&&m.bytes==before);m.writable=true;
    assert(copchase_roads_stage(&host,&id,target,heading)&&m.writes==13);
    for(unsigned i=0;i<plan.count;++i)assert(!std::memcmp(m.bytes.data()+plan.writes[i].address-0x10000,plan.writes[i].bytes,12));
    for(unsigned i=0;i<m.bytes.size();++i){bool changed=false;for(unsigned j=0;j<plan.count;++j)
        if(i+0x10000>=plan.writes[j].address&&i+0x10000<plan.writes[j].address+12)changed=true;
        if(!changed)assert(m.bytes[i]==before[i]);}
    near(m.get<float>(id.body+0x20),100);near(m.get<float>(id.body+0x30),.6f);near(m.get<float>(id.body+0x40),-.8f);
    m.set(id.facade+0x130,0u);const unsigned writes=m.writes;assert(!copchase_roads_stage(&host,&id,target,heading)&&m.writes==writes);
    m.set(id.facade+0x130,id.car);m.valid=false;assert(!copchase_roads_stage(&host,&id,target,heading));
    copchase_roads_destroy(r);std::puts("copchase roads: census, query, wheels and transactional stage checks passed");
}
