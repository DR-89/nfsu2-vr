#include "copchase_contact.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
static void need(bool value,const char *message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static CopChasePose pose(float x,float y,float vx,float vy){CopChasePose p{};p.position[0]=x;p.position[1]=y;p.velocity[0]=vx;p.velocity[1]=vy;return p;}
static CopChaseContactOutput update(CopChasePose c,CopChasePose p,uint32_t elapsed,uint32_t now=10000){
    CopChaseContactOutput out;need(copchase_contact_update(&c,&p,now,now-elapsed,&out)==1,"finite snapshot accepted");return out;
}
int main(){
    auto c=pose(96,-200,30,0),p=pose(100,-200,20,0);
    auto out=update(c,p,100000);
    need(out.refreshed && out.elapsed_ms==0 && out.last_contact_ms==10000,"observed ASI adapter regression refreshes before steering");
    need(out.distance==4 && std::fabs(out.closing_speed-40/4.01f)<1e-6,"source distance and closing speed");
    for(unsigned ms:{0u,1u,399u,400u})need(!update(c,p,ms).refreshed,"strict 400 millisecond guard");
    need(update(c,p,401).refreshed,"401 milliseconds eligible");
    need(!update(c,p,400,20).refreshed && update(c,p,401,20).refreshed,"unsigned clock wrap keeps exact boundary");
    need(update(c,p,UINT32_MAX,20).refreshed,"unsigned elapsed is not a signed comparison");
    c.position[0]=94.5f;need(!update(c,p,1000).refreshed,"distance5.5 excludes contact");
    c.position[0]=std::nextafter(94.5f,100.f);need(update(c,p,1000).refreshed,"distance immediately below5.5 eligible");
    c=pose(96,-200,20,0);need(!update(c,p,1000).refreshed,"same velocity is not closing");
    c.velocity[0]=10;need(!update(c,p,1000).refreshed,"separating vehicles do not refresh");
    c.velocity[0]=20;c.velocity[1]=100;need(!update(c,p,1000).refreshed,"lateral velocity alone is not closing");
    c=pose(100,-204,0,10);p=pose(100,-200,0,0);need(update(c,p,1000).refreshed,"Y-axis equivalent");
    c=p;need(!update(c,p,1000).refreshed,"coincident positions have zero dot without division by zero");
    // Manufacture the exact 2.5 single-precision quotient, then adjacent
    // float velocities. Position1 gives the same float1.01 denominator.
    c=pose(0,0,2.5f*1.01f,0);p=pose(1,0,0,0);out=update(c,p,1000);
    need(out.closing_speed==2.5f && !out.refreshed,"strict2.5 closing-speed boundary");
    c.velocity[0]=std::nextafter(c.velocity[0],100.f);need(update(c,p,1000).refreshed,"adjacent float above closing threshold");
    c.velocity[0]=std::nextafter(2.5f*1.01f,0.f);need(!update(c,p,1000).refreshed,"adjacent float below closing threshold");
    c.position[0]=std::numeric_limits<float>::quiet_NaN();out={};
    need(!copchase_contact_update(&c,&p,10000,5000,&out) && !out.refreshed && out.last_contact_ms==5000 && out.elapsed_ms==5000,"invalid snapshot preserves contact state");
    need(!copchase_contact_update(nullptr,&p,0,UINT32_MAX,&out) && out.elapsed_ms==1,"null snapshot preserves wrap-safe elapsed");
    need(!copchase_contact_update(&p,&p,0,0,nullptr),"null output rejected");
    std::puts("CopChase contact: source thresholds, closing direction, clock wrap and invalid-state checks PASS");
}
