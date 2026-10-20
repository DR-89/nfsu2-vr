#include "../android/runtime/quest_rear_camera.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
typedef struct {uint32_t address,value;} Word;
typedef struct {
    Word words[96];unsigned count,reads,missing,attached,detached;
    uint32_t manager,car,main,rear;int fail_create;
} Guest;
static void put(Guest *g,uint32_t address,uint32_t value)
{
    for(unsigned i=0;i<g->count;++i)if(g->words[i].address==address){g->words[i].value=value;return;}
    CHECK(g->count<96);g->words[g->count++]=(Word){address,value};
}
static int read_guest(void *ctx,uint32_t address,void *out,size_t size)
{
    Guest *g=ctx;++g->reads;CHECK(size==4);
    for(unsigned i=0;i<g->count;++i)if(g->words[i].address==address) {
        memcpy(out,&g->words[i].value,4);return 1;
    }
    ++g->missing;return 0;
}
static void retail_camera(void *ctx,uint32_t manager,int enable)
{
    Guest *g=ctx;CHECK(manager==g->manager);
    if(enable) {
        ++g->attached;if(g->fail_create)return;
        put(g,manager+0x244,g->rear);
        put(g,g->rear,0x003446f0);put(g,g->rear+0x10,3);
        put(g,g->rear+0x1c,0x003efb20);put(g,g->rear+0x2c,g->car);
        put(g,0x003f5c54,g->rear+4);
    } else {
        ++g->detached;put(g,manager+0x244,0);put(g,0x003f5c54,0x003f5c54);
    }
}
static Guest race(unsigned n)
{
    Guest g={0};
    g.manager=0x81000000+n*0x10000;g.car=g.manager+0x1000;
    g.main=g.manager+0x2000;g.rear=g.manager+0x3000;
    put(&g,0x00428e34,6);put(&g,0x0044941c,1);
    put(&g,0x0044940c,g.manager);put(&g,0x00449414,g.manager);
    put(&g,g.manager+4,g.car);put(&g,g.manager+0x10,0);
    put(&g,g.manager+0x18,1);put(&g,g.manager+0x1c,3);
    put(&g,g.manager+0x238,g.main);put(&g,g.manager+0x244,0);
    put(&g,g.main,0x00344880);put(&g,g.main+0x10,1);
    put(&g,g.main+0x1c,0x003ef000);put(&g,g.main+0x2c,g.car);
    put(&g,0x003f5b70,0x003ef000);put(&g,0x003f5b74,g.main+4);
    put(&g,0x003f5c54,0x003f5c54);
    return g;
}
static int update(Guest *g,QuestRearCamera *owned,int selected)
{
    int change=quest_rear_update(owned,read_guest,g,retail_camera,selected);
    CHECK(!g->missing);return change;
}
int main(void)
{
    QuestRearCamera owned={0};
    Guest g=race(0);
    CHECK(!update(&g,&owned,0));CHECK(!g.attached);
    CHECK(update(&g,&owned,1)==1);CHECK(owned.rear_mover==g.rear);
    for(unsigned i=0;i<10000;++i)CHECK(!update(&g,&owned,1));
    CHECK(g.attached==1 && !g.detached); /* No per-frame reallocation. */
    CHECK(update(&g,&owned,0)==-1);CHECK(!owned.rear_mover && g.detached==1);
    CHECK(!update(&g,&owned,0));CHECK(g.detached==1);

    /* Game/profile already owns a mirror; entering/exiting VR never deletes it. */
    g=race(1);retail_camera(&g,g.manager,1);
    CHECK(!update(&g,&owned,1));CHECK(!update(&g,&owned,0));
    CHECK(!owned.rear_mover && !g.detached && g.attached==1);

    /* Restart destroys the rear camera while cockpit selection remains set. */
    g=race(2);CHECK(update(&g,&owned,1)==1);
    put(&g,g.manager+0x244,0);put(&g,0x003f5c54,0x003f5c54);
    g.rear+=0x40;
    CHECK(update(&g,&owned,1)==1);CHECK(g.attached==2 && owned.rear_mover==g.rear);

    /* Next race: old owner's memory is entirely absent, not merely zeroed. */
    Guest next=race(3);
    CHECK(update(&next,&owned,1)==1);
    CHECK(next.attached==1 && !next.detached && !next.missing);
    CHECK(owned.manager==next.manager);

    /* Results/loading invalidate ownership without visiting freed objects. */
    Guest loading={0};put(&loading,0x00428e34,5);
    CHECK(!update(&loading,&owned,1));CHECK(!owned.manager && loading.reads==1);
    /* Allocator may reuse every address for a profile-owned camera. */
    g=race(3);retail_camera(&g,g.manager,1);
    CHECK(!update(&g,&owned,0));CHECK(!g.detached);

    /* Wait until the new race's normal driving camera replaces the intro. */
    g=race(4);put(&g,0x003f5b74,g.main+0x104);
    CHECK(!update(&g,&owned,1));CHECK(!g.attached);
    put(&g,0x003f5b74,g.main+4);
    CHECK(update(&g,&owned,1)==1);
    /* Retail replaces our camera: do not destroy the replacement on exit. */
    g.rear+=0x80;retail_camera(&g,g.manager,1);
    CHECK(!update(&g,&owned,0));CHECK(!g.detached && !owned.manager);

    /* An unrelated rear-view camera owns the list; do not evict it. */
    g=race(5);put(&g,0x003f5c54,0x81234564);
    CHECK(!update(&g,&owned,1));CHECK(!g.attached);
    g=race(6);put(&g,0x00449414,g.manager+0x10000);
    CHECK(!update(&g,&owned,1));CHECK(!g.attached);
    g=race(7);put(&g,g.main+0x2c,g.car+0x1000);
    CHECK(!update(&g,&owned,1));CHECK(!g.attached);
    g=race(8);put(&g,g.manager+0x10,1);
    CHECK(!update(&g,&owned,1));CHECK(!g.attached);
    g=race(9);put(&g,g.main,0x003448d0);
    CHECK(!update(&g,&owned,1));CHECK(!g.attached);
    g=race(10);put(&g,g.manager+0x244,0xaaaaaaaau);
    CHECK(!update(&g,&owned,1));CHECK(!g.attached);

    /* Failed allocation does not claim or later free somebody else's camera. */
    g=race(11);g.fail_create=1;
    CHECK(!update(&g,&owned,1));CHECK(!owned.manager);
    g.fail_create=0;CHECK(update(&g,&owned,1)==1);
    CHECK(update(&g,&owned,0)==-1);
    puts("PASS: 10000 stable frames; restart, next race, loading, address reuse, profile ownership, intro and invalid-owner guards.");
    return 0;
}
