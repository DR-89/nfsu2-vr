#include "../android/runtime/quest_copchase_panel.h"
#include <assert.h>
#include <stdlib.h>
int main(void)
{
    QuestCopChaseConfirm m={0};QuestCopChaseBust s={0};s.active=s.ready=1;s.token_high=12;s.token_low=1;
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    assert(!quest_copchase_confirm(&m,&s,1,0));
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_BACK));
    assert(quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    assert(!quest_copchase_confirm(&m,&s,1,0));
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    ++s.token_low;assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    assert(!quest_copchase_confirm(&m,&s,1,0));s.error=2;
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    s.error=0;s.ready=0;assert(!quest_copchase_confirm(&m,&s,1,0));
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    s.ready=1;assert(!quest_copchase_confirm(&m,&s,0,0));
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    s.error=1;assert(!quest_copchase_confirm(&m,&s,1,0));
    assert(quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    assert(!quest_copchase_confirm(&m,&s,1,0));
    assert(quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));s.error=0;
    s.failed=s.uncertain=1;s.error=2;s.ready=0;
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    assert(!quest_copchase_confirm(&m,&s,1,0));
    assert(quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    assert(!quest_copchase_confirm(&m,&s,1,0));
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    /* A changed failure on the same token requires a fresh release too. */
    s.uncertain=0;s.paid=1;
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    assert(!quest_copchase_confirm(&m,&s,1,0));
    assert(quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));
    s.active=0;assert(!quest_copchase_confirm(&m,&s,1,0));
    assert(!quest_copchase_confirm(&m,&s,1,QUEST_VR_ACCEPT));s.active=1;
    uint32_t *p=calloc(1024u*768u+2,sizeof *p);assert(p);p[0]=p[1024u*768u+1]=0xaabbccdd;
    s.fine=215;s.bounty=12000;s.seconds=245;s.infractions=1;
    quest_copchase_panel(p+1,&s,0);assert(p[0]==0xaabbccdd && p[1024u*768u+1]==0xaabbccdd);
    s.paid=0;s.uncertain=1;quest_copchase_panel(p+1,&s,0);
    assert(p[0]==0xaabbccdd && p[1024u*768u+1]==0xaabbccdd);
    unsigned changed=0;for(unsigned i=1;i<=1024u*768u;++i)changed+=p[i]!=0xff141b24u;
    assert(changed>10000);free(p);return 0;
}
