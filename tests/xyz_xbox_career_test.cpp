#include "xyz_xbox_career.h"
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x480000);
    XyzXboxGuest memory{this,read,write,nullptr};XyzCareerGuest guest{&memory,call};XyzXboxCareer state{};XyzXboxFrame frame{};
    unsigned quits=0,menus=0,resumes=0;bool paused=true,main=false,retired=false,fail_position=false,fail_main=false;
    void put(uint32_t at,uint32_t v){std::memcpy(ram.data()+at,&v,4);}
    Fixture(){frame.status=XYZ_XBOX_READY;frame.flow=6;frame.world=0x10000;frame.car=0x20000;
        put(0x41c338,1);put(0x4493e0,frame.world);put(0x44940c,frame.car);put(0x406d2c,2);}
    static int read(void *c,uint32_t at,void *out,size_t n){auto &f=*(Fixture*)c;
        if(at==0x406980 && f.fail_position)return 0;
        if(at>f.ram.size() || n>f.ram.size()-at)return 0;std::memcpy(out,f.ram.data()+at,n);return 1;}
    static int write(void *c,uint32_t at,const void *in,size_t n){auto &f=*(Fixture*)c;
        if(at>f.ram.size() || n>f.ram.size()-at)return 0;std::memcpy(f.ram.data()+at,in,n);return 1;}
    static int call(void *c,XyzCareerOp op){auto &f=*(Fixture*)c;
        if(op==XYZ_C_QUIT){++f.quits;f.ram[0x406974]=1;for(unsigned i=0;i<18;++i)f.ram[0x406980+i]=(uint8_t)(i+1);}
        else if(op==XYZ_C_MAIN){if(f.fail_main)return 0;++f.menus;f.put(0x3fbd0c,0x34c7a0);}
        else if(op==XYZ_C_EXPLORE)++f.resumes;else return 0;return 1;}
    int begin(){return xyz_xbox_career_begin(&guest,&state,&frame,paused);}
    int step(){return xyz_xbox_career_step(&guest,&state,&frame,paused,main,retired);}
    int resume(){return xyz_xbox_career_resume(&guest,&state,&frame,main);}
    void frontend(){frame={};frame.status=XYZ_XBOX_FRONTEND;frame.flow=3;main=true;paused=false;
        put(0x4493e0,0);put(0x44940c,0);put(0x41c338,2);}
};
int main(){
    {Fixture f;CHECK(f.begin());CHECK(!f.step() && !f.quits);f.retired=true;CHECK(!f.step() && f.quits==1 && f.menus==1);
        CHECK(!f.step() && f.quits==1);f.frontend();CHECK(f.step() && f.state.stage==XYZ_CAREER_BROWSER);
        f.put(0x406d2c,3);CHECK(!f.resume() && !f.resumes);f.put(0x406d2c,2);
        f.ram[0x406980]=99;CHECK(!f.resume() && !f.resumes);f.ram[0x406980]=1;
        CHECK(!f.resume() && f.resumes==1 && f.state.stage==XYZ_CAREER_RESUMING);
        CHECK(!f.resume() && f.resumes==1);f.frame.status=XYZ_XBOX_READY;f.frame.flow=6;f.frame.world=0x30000;f.frame.car=0x40000;
        CHECK(f.resume() && f.state.stage==XYZ_CAREER_IDLE);
    }
    {Fixture f;CHECK(f.begin() && f.resume() && !f.quits && !f.resumes);}
    {Fixture f;CHECK(f.begin());f.retired=true;f.fail_position=true;
        CHECK(!f.step() && f.quits==1 && !f.menus);CHECK(!f.step() && f.quits==1);
        f.fail_position=false;f.fail_main=true;CHECK(!f.step() && f.quits==1 && !f.menus);
        f.fail_main=false;CHECK(!f.resume() && f.quits==1 && f.menus==1);
        f.frontend();CHECK(!f.resume() && f.resumes==1);
    }
    {Fixture f;f.put(0x45c4b4,1);CHECK(f.begin());f.retired=true;CHECK(!f.step() && !f.quits);
        f.put(0x45c4b4,0);CHECK(!f.step() && f.quits==1);}
    for(unsigned i=0;i<7;++i){Fixture f;
        switch(i){case 0:f.paused=false;break;case 1:f.frame.race_type=1;break;case 2:f.put(0x41c338,8);break;
        case 3:f.put(0x456290,1);break;case 4:f.put(0x428e14,1);break;case 5:f.put(0x406c98,4);break;case 6:f.ram[0x427ff8]=255;break;}
        CHECK(!f.begin() && !f.quits && !f.resumes);
    }
    for(uint32_t at:{0x38dbb0u,0x38dbc8u,0x38dbe0u,0x38dba4u,0x38dbbcu,0x38dbd4u,0x3e3ab0u}){
        Fixture f;f.ram[at]=1;CHECK(!f.begin() && !f.quits);
        f.ram[at]=0;CHECK(f.begin());f.retired=true;f.ram[at]=1;
        CHECK(!f.step() && !f.quits);f.ram[at]=0;CHECK(!f.step() && f.quits==1);
    }
    puts("Career handoff: pause ownership, retirement barrier, original quit, saved-position guard and single resume passed");
}
