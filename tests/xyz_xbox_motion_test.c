#include "xyz_xbox_remote.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define NEAR(a,b) CHECK(fabsf((a)-(b))<.0001f)
enum { MEM=0x500000,WORLD=0x110000,CAR=0x120000,REC=0x130000,PHYS=0x140000,
       CTL=0x150000,BODY=0x160000,DEBUG=0x456048 };
static uint8_t memory[MEM];static unsigned writes,calls,activations,sections,roads;
static uint32_t r32(uint32_t a){uint32_t v;memcpy(&v,memory+a,4);return v;}
static void w32(uint32_t a,uint32_t v){memcpy(memory+a,&v,4);}
static float rf(uint32_t a){float v;memcpy(&v,memory+a,4);return v;}
static void wf(uint32_t a,float v){memcpy(memory+a,&v,4);}
static int rd(void *ctx,uint32_t a,void *out,size_t n){
    (void)ctx;if(a>MEM || n>MEM-a)return 0;memcpy(out,memory+a,n);return 1;
}
static int wr(void *ctx,uint32_t a,const void *b,size_t n){
    (void)ctx;CHECK(a<MEM && n<=MEM-a);
    CHECK((a>=CAR && (uint64_t)a+n<=CAR+0x9e0) || (a>=CTL && (uint64_t)a+n<=CTL+0x220) ||
          (a>=DEBUG && (uint64_t)a+n<=DEBUG+528) || (a==BODY+0x17 && n==1));
    memcpy(memory+a,b,n);++writes;return 1;
}
static int call(void *ctx,enum XyzXboxOp op,uint32_t self,uint32_t second,
                 unsigned n,const uint32_t *a,uint32_t *out){
    (void)ctx;CHECK(!second);++calls;*out=0;
    switch(op){
    case XYZ_G_ACTIVATE:
        CHECK(self==CAR && !n);++activations;memory[CAR+0x550]=1;
        w32(CAR+0x24,3);w32(CAR+0x28,4);memory[BODY+0x17]=0;break;
    case XYZ_G_SET_DRIVER:CHECK(self==CAR && n==1 && !a[0]);w32(CAR+0x24,0);break;
    case XYZ_G_SET_PHYS:CHECK(self==CAR && n==1 && !a[0]);w32(CAR+0x28,0);break;
    case XYZ_G_ROAD:CHECK(self==CAR+0x8d8 && n==1 && a[0]==CAR+0x60);++roads;return 1;
    case XYZ_G_SECTION:
        CHECK(self==CAR && !n);++sections;
        /* Entering a streamed section calls BodyPos, which can snap both
         * COM and orientation to the road. The received pose must survive. */
        if(rf(CAR+0x68)!=-123456.f){wf(CAR+0x68,999);memset(memory+CAR+0x90,0,12);wf(CAR+0x50,999);}
        break;
    default:CHECK(!"unexpected guest operation");
    }
    memset(memory+DEBUG,0xee,528);return 1;
}
static XyzXboxClone fixture(rc_pose *p){
    memset(memory,0,sizeof memory);writes=calls=activations=sections=roads=0;
    w32(0x4493e0,WORLD);w32(WORLD,CAR);w32(WORLD+4,CAR);w32(CAR,WORLD);w32(CAR+4,WORLD);
    w32(CAR+0x14,REC);w32(CAR+0x20,PHYS);w32(CAR+0x30,CTL);w32(CAR+0x3c,BODY);w32(REC+4,4);
    wf(PHYS,.3f);wf(PHYS+8,.3f);
    for(unsigned i=0;i<4;i++) {
        wf(PHYS+0x10+0x30*i,i<2?1.f:-1.f);wf(PHYS+0x14+0x30*i,(i==0||i==3)?.7f:-.7f);
        wf(PHYS+0x18+0x30*i,-.25f);wf(PHYS+0x20+0x30*i,.5f);
    }
    memset(memory+DEBUG,0xa7,528);memset(p,0,sizeof *p);
    p->p[0]=10;p->p[1]=20;p->p[2]=30;p->q[2]=p->q[3]=.707106781f;
    p->v[1]=1;p->speed=1;p->rpm=800;p->gear=1;p->mode=RC_MODE_INTERP;
    XyzXboxClone s={0};s.arena=(XyzXboxArena){REC,0x3000};s.record=REC;s.world=WORLD;s.car=CAR;s.stage=XYZ_CLONE_READY;
    return s;
}
static void debug_intact(void){for(unsigned i=0;i<528;i++)CHECK(memory[DEBUG+i]==0xa7);}
int main(void){
    XyzXboxGuest g={0,rd,wr,call};rc_pose p;XyzXboxClone s=fixture(&p);
    CHECK(xyz_xbox_clone_pose(&g,&s,&p,.1f));debug_intact();
    CHECK(activations==1 && roads==1 && sections==1 && s.visible && memory[BODY+0x17]);
    CHECK(memory[CAR+0x93d]==1);
    p.bits|=RC_BIT_DARK;CHECK(xyz_xbox_clone_pose(&g,&s,&p,0) && !memory[CAR+0x93d]);
    p.hibeam=1;CHECK(xyz_xbox_clone_pose(&g,&s,&p,0) && memory[CAR+0x93d]);
    p.hibeam=0;p.bits&=~RC_BIT_DARK;CHECK(xyz_xbox_clone_pose(&g,&s,&p,0) && memory[CAR+0x93d]);
    CHECK(!r32(CAR+0x24) && !r32(CAR+0x28));NEAR(rf(CAR+0x60),10);NEAR(rf(CAR+0x68),30);
    NEAR(rf(CAR+0x50),10);NEAR(rf(CAR+0x54),19.7f);NEAR(rf(CAR+0x58),29.7f);
    NEAR(rf(CAR+0x90),0);NEAR(rf(CAR+0x94),1);NEAR(rf(CAR+0xa0),-1);NEAR(rf(CAR+0xe0),-1);
    NEAR(rf(CAR+0x130),cosf(.2f));NEAR(rf(CAR+0x138),-sinf(.2f));NEAR(rf(CAR+0x168),-.25f);
    NEAR(rf(CAR+0x330),2);NEAR(rf(CAR+0x380),0);CHECK(r32(CAR+0x434)==1);
    p.steer=.2f;p.throttle=.75f;p.brake=.5f;p.bits=RC_BIT_HANDBRAKE;
    CHECK(xyz_xbox_clone_pose(&g,&s,&p,.1f));CHECK(activations==1);NEAR(rf(CTL+0x210),1);NEAR(rf(CTL+0x20c),.75f);
    NEAR(rf(CAR+0x234),sinf(.2f*3.14159265f));NEAR(rf(CAR+0x2b4),0); /* rear caliper does not steer */
    p.ext=p.slipx=1;p.wz[0]=.2f;p.slip[0]=3;p.surf[0]=7;
    CHECK(xyz_xbox_clone_pose(&g,&s,&p,0));NEAR(rf(CAR+0x168),.2f);NEAR(rf(CAR+0x380),.45f);
    NEAR(rf(CAR+0x330),8);NEAR(rf(CAR+0x3b4),3);CHECK(r32(CAR+0x394)==7 && memory[CAR+0x390]);
    p.slipx=0;p.ext=0;p.speed=0;float spin=s.spin[0];
    CHECK(xyz_xbox_clone_pose(&g,&s,&p,.1f));NEAR(s.spin[0],spin);NEAR(rf(CAR+0x3b4),0);NEAR(rf(CAR+0x168),-.25f);
    CHECK(xyz_xbox_clone_hide(&g,&s));CHECK(!s.visible && !memory[CAR+0x550]);NEAR(rf(CAR+0x68),-123456);
    NEAR(rf(CAR+0x70),0);NEAR(rf(CAR+0x330),0);NEAR(rf(CAR+0x440),0);debug_intact();
    CHECK(xyz_xbox_clone_pose(&g,&s,&p,.1f) && activations==2);
    /* Every consumed floating input rejects NaN before any guest call/write. */
    float *fields[]={&p.p[0],&p.p[1],&p.p[2],&p.q[0],&p.q[1],&p.q[2],&p.q[3],
        &p.v[0],&p.v[1],&p.v[2],&p.w[0],&p.w[1],&p.w[2],&p.steer,&p.throttle,&p.brake,&p.speed,&p.rpm,
        &p.wz[0],&p.wz[1],&p.wz[2],&p.wz[3],&p.slip[0],&p.slip[1],&p.slip[2],&p.slip[3],&p.slip[4],&p.slip[5],&p.slip[6],&p.slip[7]};
    for(unsigned i=0;i<sizeof fields/sizeof fields[0];i++) {
        s=fixture(&p);p.ext=p.slipx=1;*fields[i]=NAN;
        CHECK(!xyz_xbox_clone_pose(&g,&s,&p,.1f) && !writes && !calls);
    }
    s=fixture(&p);p.q[2]=p.q[3]=0;CHECK(!xyz_xbox_clone_pose(&g,&s,&p,.1f) && !writes);
    s=fixture(&p);CHECK(!xyz_xbox_clone_pose(&g,&s,&p,NAN) && !writes);
    s=fixture(&p);p.v[0]=200;p.v[1]=200;CHECK(!xyz_xbox_clone_pose(&g,&s,&p,.1f) && !writes);
    s=fixture(&p);wf(PHYS,NAN);CHECK(!xyz_xbox_clone_pose(&g,&s,&p,.1f) && !writes);
    s=fixture(&p);w32(CAR+0x14,REC+0x100);CHECK(!xyz_xbox_clone_pose(&g,&s,&p,.1f) && !writes && !calls);
    s=fixture(&p);w32(0x4493e0,0);CHECK(!xyz_xbox_clone_hide(&g,&s) && !writes && !calls);
    puts("Xbox motion: COM rotation, section snap, wheel/caliper animation, hide/show and malformed-pose rejection passed");
    return 0;
}
