#include "copchase_fine.h"
#include <cassert>
struct Mock {
    CopChaseFineIdentity id{1,2,3,1,1,1};uint32_t cash=1000;unsigned writes=0;
    bool fail_after=false,fail_read=false,swap_get=false,change_cash=false;
    static int identity(void *p,CopChaseFineIdentity *i){*i=static_cast<Mock*>(p)->id;return 1;}
    static int read(void *p,uint32_t a,uint32_t *v){auto &m=*static_cast<Mock*>(p);assert(a==0x405364);*v=m.cash;return !m.fail_read;}
    static int call(void *p,uint32_t fn,uint32_t self,unsigned n,const uint32_t *args,uint32_t *eax){
        auto &m=*static_cast<Mock*>(p);assert(self==0x405364);
        if(fn==0x111c50){assert(!n && !args);*eax=m.cash;if(m.swap_get)++m.id.profile;if(m.change_cash)++m.cash;return 1;}
        assert(fn==0x111c40 && n==1 && args);m.cash+=args[0];++m.writes;*eax=0;return !m.fail_after;
    }
    CopChaseFineHost host(){return {this,identity,read,call};}
};
int main(){
    Mock m;auto h=m.host();CopChaseFine f{};
    assert(copchase_fine_arm(&f,&h,1,1,3)==COPCHASE_FINE_OK && f.fine==645);
    assert(copchase_fine_confirm(&f,&h)==COPCHASE_FINE_OK && m.cash==355 && m.writes==1);
    assert(copchase_fine_confirm(&f,&h)==COPCHASE_FINE_DUPLICATE && m.writes==1);
    assert(copchase_fine_arm(&f,&h,1,1,3)==COPCHASE_FINE_DUPLICATE);
    assert(copchase_fine_arm(&f,&h,1,1,4)==COPCHASE_FINE_INVALID);
    assert(copchase_fine_arm(&f,&h,1,2,3)==COPCHASE_FINE_OK);
    assert(copchase_fine_confirm(&f,&h)==COPCHASE_FINE_OK && m.cash==0 && f.charged==355);
    assert(copchase_fine_arm(&f,&h,1,3,0)==COPCHASE_FINE_OK && f.fine==215);
    assert(copchase_fine_confirm(&f,&h)==COPCHASE_FINE_OK && m.writes==2);
    assert(copchase_fine_arm(&f,&h,1,1,3)==COPCHASE_FINE_DUPLICATE && m.writes==2);
    for(unsigned which=0;which<6;++which){Mock a;auto ah=a.host();CopChaseFine af{};
        assert(copchase_fine_arm(&af,&ah,9,which+1,0)==COPCHASE_FINE_OK);
        if(which==0)++a.id.profile;if(which==1)++a.id.world;if(which==2)++a.id.player;
        if(which==3)a.id.offline=0;if(which==4)a.id.career=0;if(which==5)a.id.valid=0;
        assert(copchase_fine_confirm(&af,&ah)==COPCHASE_FINE_STALE && a.cash==1000 && !a.writes);}
    for(unsigned which=0;which<4;++which){Mock a;auto ah=a.host();CopChaseFine af{};
        assert(copchase_fine_arm(&af,&ah,10,which+1,0)==COPCHASE_FINE_OK);
        if(which==0)a.fail_read=true;if(which==1)a.swap_get=true;if(which==2)a.change_cash=true;if(which==3)a.cash=0xffffffffu;
        const auto result=copchase_fine_confirm(&af,&ah);assert((result==COPCHASE_FINE_IO || result==COPCHASE_FINE_STALE) && !a.writes);}
    Mock a;auto ah=a.host();CopChaseFine af{};assert(copchase_fine_arm(&af,&ah,20,1,0)==COPCHASE_FINE_OK);a.fail_after=true;
    assert(copchase_fine_confirm(&af,&ah)==COPCHASE_FINE_UNCERTAIN && a.cash==785 && a.writes==1);
    assert(copchase_fine_confirm(&af,&ah)==COPCHASE_FINE_UNCERTAIN && a.writes==1);
    assert(copchase_fine_arm(&af,&ah,20,2,0)==COPCHASE_FINE_INVALID);
    assert(copchase_fine_abandon_unattempted(&af)==COPCHASE_FINE_UNCERTAIN);
    Mock b;auto bh=b.host();CopChaseFine bf{};assert(copchase_fine_arm(&bf,&bh,30,1,0)==COPCHASE_FINE_OK);
    assert(copchase_fine_abandon_unattempted(&bf)==COPCHASE_FINE_OK && !bf.paid);
    assert(copchase_fine_confirm(&bf,&bh)==COPCHASE_FINE_STALE && !b.writes);
    assert(copchase_fine_arm(&bf,&bh,30,1,0)==COPCHASE_FINE_DUPLICATE);
    assert(copchase_fine_arm(&bf,&bh,30,2,0)==COPCHASE_FINE_OK);
    assert(copchase_fine_confirm(&bf,&bh)==COPCHASE_FINE_OK && b.writes==1);
}
