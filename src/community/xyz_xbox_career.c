#include "xyz_xbox_career.h"
#include <string.h>
static int read_value(const XyzCareerGuest *g,uint32_t at,void *out,size_t n){
    return g && g->memory && g->memory->read && xyz_xbox_address(at,n) && g->memory->read(g->memory->context,at,out,n);
}
static int word_is(const XyzCareerGuest *g,uint32_t at,uint32_t expected){uint32_t v;return read_value(g,at,&v,4) && v==expected;}
static int call(const XyzCareerGuest *g,enum XyzCareerOp op){return g && g->call && g->call(g->memory->context,op);}
static int transition_clear(const XyzCareerGuest *g){
    const uint32_t gates[]={0x3fbbf4,0x3fbbfc,0x428e14};
    for(unsigned i=0;i<sizeof gates/sizeof gates[0];++i)if(!word_is(g,gates[i],0))return 0;
    uint8_t net[0x21];if(!read_value(g,0x456290,net,sizeof net))return 0;
    for(unsigned i=0;i<sizeof net;++i)if(net[i])return 0;
    return 1;
}
static int idle(const XyzCareerGuest *g){return transition_clear(g) && word_is(g,0x441b48,0) && word_is(g,0x45c4b4,0);}
static int cinematics_idle(const XyzCareerGuest *g){
    /* Retail pause eligibility uses the intro/outro/story handles (3F540,
     * 3F620, 3F760). A native overlay must also wait for their pending loads.
     * Otherwise the old career sequence can reposition the next race's car. */
    const uint32_t handles[]={0x38dbb0,0x38dbc8,0x38dbe0};
    const uint32_t loading[]={0x38dba4,0x38dbbc,0x38dbd4,0x3e3ab0};
    for(unsigned i=0;i<3;++i)if(!word_is(g,handles[i],0))return 0;
    for(unsigned i=0;i<4;++i){uint8_t v;if(!read_value(g,loading[i],&v,1) || v)return 0;}
    return 1;
}
static int menu(const XyzCareerGuest *g,const XyzXboxFrame *f,int main_menu){
    return main_menu && f && f->flow==3 && f->status==XYZ_XBOX_FRONTEND && !f->world && !f->car &&
        word_is(g,0x4493e0,0) && word_is(g,0x44940c,0) && idle(g);
}
int xyz_xbox_career_begin(const XyzCareerGuest *g,XyzXboxCareer *s,const XyzXboxFrame *f,int paused){
    XyzXboxCareer next={0};uint8_t device;
    if(!s || s->stage!=XYZ_CAREER_IDLE || !paused || !f || f->status!=XYZ_XBOX_READY || f->flow!=6 ||
       f->race_type || !f->world || !f->car || !word_is(g,0x41c338,1) || !transition_clear(g) || !cinematics_idle(g) ||
       !read_value(g,0x406c98,&next.controller,4) || next.controller>3 ||
       !read_value(g,0x427ff8+next.controller,&device,1) || device==UINT8_MAX ||
       !read_value(g,0x406d2c,&next.slot,4))return 0;
    next.world=f->world;next.car=f->car;next.stage=XYZ_CAREER_DRAINING;*s=next;return 1;
}
int xyz_xbox_career_step(const XyzCareerGuest *g,XyzXboxCareer *s,const XyzXboxFrame *f,int paused,int main_menu,int retired){
    if(!s || !f)return 0;
    if(s->stage==XYZ_CAREER_DRAINING){
        if(!retired || !paused || f->status!=XYZ_XBOX_READY || f->world!=s->world || f->car!=s->car ||
           !word_is(g,0x41c338,1) || !idle(g) || !cinematics_idle(g))return 0;
        if(!call(g,XYZ_C_QUIT))return 0;
        // Quit has queued the original world transition. Do not dispatch twice.
        s->stage=XYZ_CAREER_UNLOADING;
    }
    if(s->stage==XYZ_CAREER_UNLOADING){
        if(!s->position_captured){
            if(!read_value(g,0x406980,s->position,sizeof s->position))return 0;
            s->position_captured=1;
        }
        if(!s->main_requested){
            if(!call(g,XYZ_C_MAIN))return 0;
            s->main_requested=1;
        }
        if(menu(g,f,main_menu) && word_is(g,0x41c338,2))s->stage=XYZ_CAREER_BROWSER;
    }
    return s->stage==XYZ_CAREER_BROWSER;
}
int xyz_xbox_career_resume(const XyzCareerGuest *g,XyzXboxCareer *s,const XyzXboxFrame *f,int main_menu){
    if(!s || !f)return 0;
    if(s->stage==XYZ_CAREER_IDLE)return 1;
    if(s->stage==XYZ_CAREER_DRAINING){memset(s,0,sizeof *s);return 1;}
    if(s->stage==XYZ_CAREER_UNLOADING)xyz_xbox_career_step(g,s,f,0,main_menu,1);
    if(s->stage==XYZ_CAREER_BROWSER){
        uint8_t saved,position[18],device;
        if(!menu(g,f,main_menu) || !word_is(g,0x41c338,2) || !word_is(g,0x406d2c,s->slot) ||
           !word_is(g,0x406c98,s->controller) || !read_value(g,0x427ff8+s->controller,&device,1) || device==UINT8_MAX ||
           !read_value(g,0x406974,&saved,1) || !saved || !read_value(g,0x406980,position,sizeof position) ||
           memcmp(position,s->position,sizeof position))return 0;
        // This is the same loaded career that Pause::Quit suspended. Explore
        // uses its saved position, controller and performance; the storage
        // selection must also still match the suspended profile.
        uint32_t mode=1;
        if(!g->memory->write || !g->memory->write(g->memory->context,0x41c338,&mode,4))return 0;
        if(!call(g,XYZ_C_EXPLORE)){mode=2;g->memory->write(g->memory->context,0x41c338,&mode,4);return 0;}
        s->stage=XYZ_CAREER_RESUMING;return 0;
    }
    if(s->stage==XYZ_CAREER_RESUMING && f->flow==6 && f->status==XYZ_XBOX_READY && !f->race_type && f->world && f->car && word_is(g,0x41c338,1)){
        memset(s,0,sizeof *s);return 1;
    }
    return 0;
}
