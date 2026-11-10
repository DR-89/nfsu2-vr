#include "copchase_bust.h"
#include <cstdio>
#include <cstdlib>

static unsigned checks;
static void check(bool value,const char *why) {
    ++checks; if (!value) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); }
}
struct Mock {
    CopChaseBustContext c{{1,2,3,1,1,1},0x10000,0x20000,0x30000,0,6,4,1,0,1,0};
    uint32_t cash=1000,pending=0,disabled=0,devices=0xffffff00,previous=4;
    unsigned pauses=0,resumes=0,charges=0;
    bool fail_context=false,fail_cash=false,fail_pause=false,fail_resume=false,fail_charge_after=false;
    bool nested_on_get=false,swap_on_identity=false;
    static int context(void *user,CopChaseBustContext *out) { auto &m=*static_cast<Mock*>(user); *out=m.c; return !m.fail_context; }
    static int identity(void *user,CopChaseFineIdentity *out) {
        auto &m=*static_cast<Mock*>(user);
        if (m.swap_on_identity) { ++m.c.identity.profile; m.swap_on_identity=false; }
        *out=m.c.identity; return 1;
    }
    static int read(void *user,uint32_t address,uint32_t *out) {
        auto &m=*static_cast<Mock*>(user);
        switch (address) {
        case 0x449438:*out=m.c.director;return 1;
        case 0x30000:*out=uint32_t(m.c.director_state);return 1;
        case 0x30024:*out=m.previous;return 1;
        case 0x405368:*out=m.pending;return 1;
        case 0x40245c:*out=m.disabled;return 1;
        case 0x20008:*out=m.c.controller;return 1;
        case 0x427ff8:*out=m.devices;return 1;
        case 0x405364:*out=m.cash;return !m.fail_cash;
        default:return 0;
        }
    }
    static int call(void *user,uint32_t fn,uint32_t self,unsigned argc,const uint32_t *args,uint32_t *eax) {
        auto &m=*static_cast<Mock*>(user); *eax=0;
        switch (fn) {
        case 0x1f2b50:
            check(self==m.c.car && argc==3 && args[0]==m.c.controller && args[1]==0xbd && args[2]==0,"original pause ABI");
            ++m.pauses;m.pending=1;return !m.fail_pause;
        case 0x1e45f0:
            check(self==m.c.director && argc==1 && args[0]==1,"native saved-state Resume event ABI");
            ++m.resumes;return !m.fail_resume;
        case 0x111c50:
            check(self==0x405364 && !argc && !args,"native balance getter ABI");
            *eax=m.cash;if(m.nested_on_get)m.c.root_pause=0;return 1;
        case 0x111c40:
            check(self==0x405364 && argc==1,"native fine subtraction ABI");
            ++m.charges;m.cash+=args[0];return !m.fail_charge_after;
        default:check(false,"unexpected guest mutation");return 0;
        }
    }
    CopChaseBustHost host() { return {{this,identity,read,call},context}; }
    void opened() { c.hud=0;c.root_pause=1;c.director_state=6;c.modal_visible=1;pending=0; }
    void resumed() { c.hud=1;c.root_pause=0;c.director_state=4;c.modal_visible=0;pending=0; }
};
struct Test {
    Mock m; CopChaseBust *b;
    Test() { auto h=m.host();b=copchase_bust_create(&h);check(b!=nullptr,"create controller"); }
    ~Test() { copchase_bust_destroy(b); }
    CopChaseBustStatus state() { CopChaseBustStatus s;copchase_bust_snapshot(b,&s);return s; }
    void ready(uint64_t token=1,uint32_t infractions=3,uint64_t now=10) {
        check(copchase_bust_arm(b,99,token,infractions,now)==COPCHASE_BUST_OK,"arm durable bust event");
        copchase_bust_step(b,now+1);check(m.pauses>0 && state().phase==COPCHASE_BUST_WAIT_PAUSE,"request original pause once");
        m.opened();copchase_bust_step(b,now+2);
        check(state().ready && state().owned_pause && state().phase==COPCHASE_BUST_READY,"actual root pause grants modal ownership");
    }
};
int main() {
    check(!copchase_bust_create(nullptr),"invalid callback set rejected");
    {
        Test t;t.ready();auto s=t.state();check(s.fine==645 && s.balance==1000 && s.balance_valid && !s.paid,"live modal quote and balance");
        check(!t.m.charges && !t.m.resumes,"no charge or resume before confirmation");
        check(copchase_bust_confirm(t.b,99,2,13)==COPCHASE_BUST_INVALID,"stale VR token rejected");
        check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_OK,"explicit current-token payment");
        check(t.m.cash==355 && t.m.charges==1 && t.state().paid && !t.state().ready,"one native payment");
        check(copchase_bust_confirm(t.b,99,1,14)==COPCHASE_BUST_DUPLICATE && t.m.charges==1,"duplicate UI input never charges twice");
        copchase_bust_step(t.b,15);check(!t.m.resumes,"paid panel closes before guest resume");
        t.m.c.modal_visible=0;copchase_bust_step(t.b,16);check(t.m.resumes==1,"owned root resumed once");
        copchase_bust_step(t.b,17);check(t.m.resumes==1,"queued resume not repeated");
        t.m.resumed();copchase_bust_step(t.b,18);
        check(t.state().phase==COPCHASE_BUST_DONE && !t.state().pending && !t.state().owned_pause,"observe genuine driving before completion");
        check(copchase_bust_arm(t.b,99,1,3,19)==COPCHASE_BUST_DUPLICATE,"old durable event cannot rearm");
        check(copchase_bust_arm(t.b,100,2,3,19)==COPCHASE_BUST_INVALID,"durable namespace cannot change mid-session");
        t.ready(2,3,20);check(copchase_bust_confirm(t.b,99,2,23)==COPCHASE_BUST_OK,"next distinct bust allowed");
        check(t.m.cash==0 && t.m.charges==2 && t.state().charged==355,"fine clamps to current balance");
    }
    for (unsigned gate=0;gate<8;++gate) {
        Test t;
        if(gate==0)t.m.c.can_pause=0;if(gate==1)t.m.c.hud=0;if(gate==2)t.m.opened();
        if(gate==3)t.m.pending=1;if(gate==4)t.m.disabled=1;if(gate==5)t.m.devices=0xffffffff;
        if(gate==6)t.m.c.director_state=5;if(gate==7)t.m.c.modal_visible=1;
        check(copchase_bust_arm(t.b,99,1,0,10)==COPCHASE_BUST_OK,"defer bust while original pause gate closed");
        copchase_bust_step(t.b,11);check(!t.m.pauses && !t.state().owned_pause,"pre-existing pause/dialog/gate never taken over");
        copchase_bust_step(t.b,5011);check(t.state().error==COPCHASE_BUST_TIMEOUT && !t.m.resumes && !t.m.charges,"blocked gate timeout performs no mutation");
    }
    {
        Test t;check(copchase_bust_arm(t.b,99,1,0,10)==COPCHASE_BUST_OK,"arm before pause timeout");
        check(copchase_bust_confirm(t.b,99,1,10)==COPCHASE_BUST_BUSY,"early confirmation rejected");
        copchase_bust_step(t.b,11);copchase_bust_step(t.b,5012);
        check(t.m.pauses==1 && t.state().error==COPCHASE_BUST_TIMEOUT && !t.m.resumes && !t.m.charges,"unobserved pause cannot authorize payment or resume");
    }
    for (unsigned which=0;which<9;++which) {
        Test t;t.ready();
        if(which==0)++t.m.c.identity.profile;if(which==1)++t.m.c.identity.world;if(which==2)++t.m.c.identity.player;
        if(which==3)++t.m.c.car;if(which==4)++t.m.c.director;if(which==5)t.m.c.identity.offline=0;
        if(which==6)t.m.c.root_pause=0;if(which==7)t.m.fail_context=true;if(which==8)t.m.c.controller=1;
        check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_CONTEXT_LOST,"stale identity or nested dialog cannot pay");
        copchase_bust_step(t.b,14);check(!t.m.charges && !t.m.resumes && !t.state().owned_pause,"stale modal never blindly resumes");
    }
    for (unsigned which=0;which<2;++which) {
        Test t;t.ready();if(which==0)t.m.nested_on_get=true;else t.m.swap_on_identity=true;
        check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_STALE,"context revalidated inside fine transaction");
        check(!t.m.charges && !t.m.resumes,"changed context inside getter/identity cannot charge");
    }
    {
        Test t;t.ready();t.m.fail_cash=true;
        check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_PAYMENT_IO && !t.m.charges,"pre-mutation read failure leaves payment unattempted");
        copchase_bust_step(t.b,14);check(!t.m.charges,"no automatic payment retry");
        t.m.fail_cash=false;
        check(copchase_bust_confirm(t.b,99,1,15)==COPCHASE_BUST_OK && t.m.charges==1,"second explicit confirmation may retry pre-mutation IO");
    }
    {
        Test t;t.ready();t.m.fail_cash=true;
        check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_PAYMENT_IO,"prepare abandoned unattempted fine");
        t.m.resumed();copchase_bust_step(t.b,14);check(t.state().phase==COPCHASE_BUST_FAILED,"lost root aborts pending unattempted modal");
        t.m.fail_cash=false;t.ready(2,0,20);
        check(copchase_bust_confirm(t.b,99,2,23)==COPCHASE_BUST_OK && t.m.charges==1,"new token after abandoned pre-mutation work stays safe");
    }
    {
        Test t;t.ready();t.m.fail_charge_after=true;
        check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_PAYMENT_UNCERTAIN,"post-write failure latches uncertain payment");
        check(t.m.cash==355 && t.m.charges==1,"uncertain charge may already have occurred");
        check(copchase_bust_confirm(t.b,99,1,14)==COPCHASE_BUST_PAYMENT_UNCERTAIN,"uncertain payment never retried");
        t.m.resumed();check(copchase_bust_arm(t.b,99,2,3,15)==COPCHASE_BUST_BUSY,"uncertain latch survives later token");
        copchase_bust_step(t.b,16);check(t.m.charges==1 && !t.m.resumes,"uncertain transaction never automatically resumes");
    }
    {
        Test t;t.m.fail_pause=true;check(copchase_bust_arm(t.b,99,1,0,10)==COPCHASE_BUST_OK,"arm pause-call failure");
        copchase_bust_step(t.b,11);t.m.opened();copchase_bust_step(t.b,12);
        check(t.state().error==COPCHASE_BUST_NATIVE_IO && !t.state().owned_pause && !t.m.charges,"failed call completion cannot infer pause ownership");
    }
    {
        Test t;t.ready();check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_OK,"pay before failed resume");
        t.m.fail_resume=true;t.m.c.modal_visible=0;copchase_bust_step(t.b,14);copchase_bust_step(t.b,15);
        check(t.state().paid && t.state().error==COPCHASE_BUST_NATIVE_IO && t.m.resumes==1 && t.m.charges==1,"failed resume does not repeat payment or event");
    }
    {
        Test t;t.ready();copchase_bust_step(t.b,9);check(t.state().error==COPCHASE_BUST_STALE && !t.m.charges,"clock reversal invalidates modal ownership");
    }
    {
        Test t;t.m.cash=0;t.ready();check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_OK && !t.m.charges && t.state().paid,"zero balance confirms without subtract call");
    }
    {
        Test t;t.ready();t.m.fail_charge_after=true;
        check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_PAYMENT_UNCERTAIN,"prepare uncertain notice");
        check(t.state().failure_visible && t.state().failure_dismissable && !t.state().owned_pause && !t.state().ready,"uncertain error displayed without payment or resume ownership");
        check(copchase_bust_dismiss(t.b,99,2,14)==COPCHASE_BUST_INVALID,"wrong token cannot dismiss failure");
        check(copchase_bust_dismiss(t.b,99,1,14)==COPCHASE_BUST_OK,"explicit failure acknowledgement");
        check(!t.state().failure_visible && !t.state().failure_dismissable && t.m.c.root_pause,"dismiss leaves ordinary pause visible");
        copchase_bust_step(t.b,15);check(t.m.charges==1 && !t.m.resumes,"failure dismissal neither pays nor resumes");
        check(copchase_bust_confirm(t.b,99,1,16)==COPCHASE_BUST_PAYMENT_UNCERTAIN,"dismissal cannot reset uncertain-charge latch");
        check(copchase_bust_arm(t.b,99,2,3,17)==COPCHASE_BUST_BUSY,"new token cannot bypass dismissed uncertain charge");
    }
    {
        Test t;t.ready();check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_OK,"prepare paid resume failure");
        t.m.fail_resume=true;t.m.c.modal_visible=0;copchase_bust_step(t.b,14);
        check(t.state().failure_visible && t.state().paid,"failed resume displays confirmed payment notice");
        check(copchase_bust_dismiss(t.b,99,1,15)==COPCHASE_BUST_OK,"paid failure acknowledged without retrying resume");
        copchase_bust_step(t.b,16);check(t.m.charges==1 && t.m.resumes==1 && t.m.c.root_pause,"user retains ordinary pause after paid failure");
    }
    {
        Test t;t.ready();t.m.c.root_pause=0;copchase_bust_step(t.b,13);
        check(!t.state().failure_visible && !t.state().failure_dismissable,"nested dialog hides failure notice");
        check(copchase_bust_dismiss(t.b,99,1,14)==COPCHASE_BUST_CONTEXT_LOST,"cannot dismiss through nested dialog");
        t.m.c.root_pause=1;copchase_bust_step(t.b,15);
        check(t.state().failure_visible,"same root returning reveals deferred failure");
        check(copchase_bust_arm(t.b,99,2,1,16)==COPCHASE_BUST_BUSY,"visible old error cannot be overwritten by new result");
        ++t.m.c.identity.profile;copchase_bust_step(t.b,17);
        check(!t.state().failure_visible,"new profile clears old notice");
        check(copchase_bust_dismiss(t.b,99,1,18)==COPCHASE_BUST_BUSY && !t.m.charges && !t.m.resumes,"stale profile notice cannot invoke any guest mutation");
    }
    {
        Test t;t.ready();t.m.previous=5;
        check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_CONTEXT_LOST,"saved director state must still be driving before payment");
        check(!t.m.charges && !t.m.resumes,"foreign previous state never charged or restored");
    }
    {
        Test t;t.ready();check(copchase_bust_confirm(t.b,99,1,13)==COPCHASE_BUST_OK,"pay before changed saved state");
        t.m.previous=0;t.m.c.modal_visible=0;copchase_bust_step(t.b,14);
        check(!t.m.resumes && t.state().error==COPCHASE_BUST_CONTEXT_LOST,"changed saved state cannot restart race or resume wrong context");
    }
    {
        Test t;t.m.c.director=UINT32_MAX-0x23;
        check(copchase_bust_arm(t.b,99,1,3,10)==COPCHASE_BUST_STALE,"director saved-state address cannot wrap");
    }
    std::printf("CopChase owned bust modal: %u checks passed\n",checks);
}
