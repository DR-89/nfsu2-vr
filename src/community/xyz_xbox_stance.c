#include "xyz_xbox_stance.h"
#include <math.h>
#include <string.h>
static int rd(const XyzXboxGuest *g,uint32_t a,void *p,size_t n){return g && g->read && xyz_xbox_address(a,n) && g->read(g->context,a,p,n);}
static int wr(const XyzXboxGuest *g,uint32_t a,const void *p,size_t n){return g && g->write && xyz_xbox_address(a,n) && g->write(g->context,a,p,n);}
int xyz_xbox_stance_begin(const XyzXboxGuest *g,uint32_t render,uint32_t wheels,const XyzStance *v,XyzStanceDraw *s){
    if(!s || !xyz_stance_valid(v))return 0;
    memset(s,0,sizeof *s);
    if(!xyz_xbox_address(render,0x2d48) || !xyz_xbox_address(wheels,256) ||
       !rd(g,render+4,&s->ride,4) || !xyz_xbox_address(s->ride,0x8f0) ||
       !rd(g,render+0x400,s->places,sizeof s->places) || !rd(g,render+0x420,s->widths,sizeof s->widths))return 0;
    for(unsigned k=0;k<8;k++)if(!isfinite(s->places[k]) || fabsf(s->places[k])>10)return 0;
    for(unsigned k=0;k<4;k++)if(!isfinite(s->widths[k]) || s->widths[k]<0 || s->widths[k]>2)return 0;
    for(unsigned k=0;k<2;k++){
        if(!rd(g,wheels+(k+2)*64,s->rear[k],48))return 0;
        for(unsigned j=0;j<12;j++)if(!isfinite(s->rear[k][j]) || fabsf(s->rear[k][j])>10)return 0;
    }
    s->render=render;s->wheels=wheels;s->value=*v;s->active=1;
    float places[8],widths[4],rear[12];
    for(unsigned k=0;k<8;k++){float offset=(k<4?v->fo:v->ro)*.001f;places[k]=s->places[k]+(s->places[k]<0?-offset:offset);}
    for(unsigned k=0;k<4;k++)widths[k]=s->widths[k]*v->tw*.01f;
    if(!wr(g,render+0x400,places,sizeof places) || !wr(g,render+0x420,widths,sizeof widths))goto failed;
    for(unsigned k=0;k<2;k++){
        memcpy(rear,s->rear[k],sizeof rear);float a=v->rc*.001745329252f*(k?1.f:-1.f),cs=cosf(a),sn=sinf(a);
        for(unsigned row=0;row<3;row++){float y=rear[row*4+1],z=rear[row*4+2];rear[row*4+1]=y*cs-z*sn;rear[row*4+2]=y*sn+z*cs;}
        if(!wr(g,wheels+(k+2)*64,rear,sizeof rear))goto failed;
    }
    return 1;
failed:xyz_xbox_stance_end(g,s);return 0;
}
void xyz_xbox_stance_end(const XyzXboxGuest *g,XyzStanceDraw *s){
    if(!s || !s->active)return;
    wr(g,s->render+0x400,s->places,sizeof s->places);wr(g,s->render+0x420,s->widths,sizeof s->widths);
    for(unsigned k=0;k<2;k++)wr(g,s->wheels+(k+2)*64,s->rear[k],48);
    s->active=0;
}
