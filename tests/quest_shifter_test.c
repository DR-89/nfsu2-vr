#include "../android/runtime/quest_shifter.h"
#include "../android/runtime/quest_pedals.h"
#include <stdio.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"Failed line %d: %s\n",__LINE__,#x); return 1; } } while(0)

/* Independent expected positions, in units of rail pitch and lever throw.
 * Array order is the retail ID order R,N,1,2,3,4,5,6. */
static const int gate5[][2]={{1,1},{0,0},{-1,-1},{-1,1},{0,-1},{0,1},{1,-1}};
static const int gate6[][2]={{2,1},{0,0},{-1,-1},{-1,1},{0,-1},{0,1},{1,-1},{1,1}};

static int shifts_between_slots(int count,int vehicle,int hand)
{
    const int (*gate)[2]=count==6?gate6:gate5;
    for(int from=0;from<count+2;++from)for(int to=0;to<count+2;++to) {
        QuestShifter s;quest_shifter_reset(&s);
        int valid[2]={1,1},wheel[2]={0,0};wheel[1-hand]=1;
        float squeeze[2]={0,0},pos[2][3]={{0}},center[3];
        quest_shifter_knob(&s,vehicle,center);
        memcpy(pos[hand],center,sizeof center);
        pos[hand][0]+=gate[from][0]*QUEST_SHIFT_PITCH+.012f;
        pos[hand][2]+=gate[from][1]*QUEST_SHIFT_THROW+.008f;
        CHECK(quest_shifter_update(&s,0,vehicle,count,from,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE);
        squeeze[0]=squeeze[1]=1;
        CHECK(quest_shifter_update(&s,10,vehicle,count,from,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE);
        CHECK(s.hand==hand+1 && s.gear==from);
        pos[hand][2]=center[2]+.008f;
        CHECK(quest_shifter_update(&s,20,vehicle,count,from,valid,pos,squeeze,wheel)==
            (from==QUEST_SHIFT_N?QUEST_SHIFT_NONE:QUEST_SHIFT_N));
        /* Slide in samples through all intermediate lanes without shifting. */
        for(int i=1;i<=6;++i) {
            float lane=gate[from][0]+(gate[to][0]-gate[from][0])*i/6.f;
            pos[hand][0]=center[0]+lane*QUEST_SHIFT_PITCH+.012f;
            CHECK(quest_shifter_update(&s,20+i,vehicle,count,QUEST_SHIFT_N,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE);
        }
        pos[hand][2]+=gate[to][1]*QUEST_SHIFT_THROW;
        CHECK(quest_shifter_update(&s,35,vehicle,count,QUEST_SHIFT_N,valid,pos,squeeze,wheel)==
            (to==QUEST_SHIFT_N?QUEST_SHIFT_NONE:to));
        CHECK(s.gear==to);
        CHECK(quest_shifter_update(&s,45,vehicle,count,to,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE);
        valid[hand]=0;squeeze[hand]=0;
        CHECK(quest_shifter_update(&s,55,vehicle,count,to,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE);
        CHECK(!s.hand && s.gear==to);
    }
    return 0;
}

int main(void)
{
    QuestPedals pedals={0};
    for(int i=0;i<256;++i) {
        quest_pedals_update(&pedals,i,1,i,255-i,1,2,0,0);
        CHECK(fabsf(pedals.travel[0]-i/255.f)<.00001f);
        CHECK(fabsf(pedals.travel[1]-(255-i)/255.f)<.00001f && pedals.travel[2]==0);
    }
    /* Hold neutral through a manual shift, seat the next gear and leave
     * the hand resting on the lever. The clutch must return on its own. */
    for(unsigned t=270;t<=470;t+=10)quest_pedals_update(&pedals,t,1,90,190,1,1,1,t==270);
    CHECK(pedals.travel[2]==1);
    for(unsigned t=480;t<=850;t+=10)quest_pedals_update(&pedals,t,1,90,190,1,4,0,t==480);
    CHECK(pedals.travel[2]==0);
    quest_pedals_update(&pedals,866,1,0,255,1,5,0,0); /* sequential shift */
    CHECK(pedals.travel[2]>0 && pedals.travel[0]==0 && pedals.travel[1]==1);
    quest_pedals_update(&pedals,880,1,255,255,1,-1,0,0); /* automatic */
    CHECK(pedals.travel[2]==0 && pedals.travel[0]==1 && pedals.travel[1]==1);
    quest_pedals_update(&pedals,900,0,255,255,1,3,1,1);
    CHECK(!pedals.travel[0] && !pedals.travel[1] && !pedals.travel[2]);
    puts("Pedals passed all trigger levels, independent inputs and timed clutch engagement/release.");
    for(int count=4;count<=6;++count)for(int hand=0;hand<2;++hand) {
        CHECK(!shifts_between_slots(count,quest_cockpit_vehicle_index("PEUGOT"),hand));
        CHECK(!shifts_between_slots(count,quest_cockpit_vehicle_index("SKYLINE"),hand));
    }
    QuestShifter s;quest_shifter_reset(&s);
    s.first_only=1;
    CHECK(quest_shifter_move(&s,0,-QUEST_SHIFT_THROW,6)==QUEST_SHIFT_NONE && s.gear==QUEST_SHIFT_N);
    CHECK(quest_shifter_move(&s,-QUEST_SHIFT_PITCH,QUEST_SHIFT_THROW,6)==QUEST_SHIFT_NONE && s.gear==QUEST_SHIFT_N);
    CHECK(quest_shifter_move(&s,-QUEST_SHIFT_PITCH,-.058f,6)==QUEST_SHIFT_NONE && s.gear==QUEST_SHIFT_N);
    CHECK(quest_shifter_move(&s,-QUEST_SHIFT_PITCH,-QUEST_SHIFT_THROW,6)==QUEST_SHIFT_FIRST);
    quest_shifter_reset(&s);
    /* Far-right forward is a blind end on a six-speed. Halfway between
     * lanes is also blocked, and lateral pressure cannot change an engaged gear. */
    CHECK(quest_shifter_move(&s,2*QUEST_SHIFT_PITCH,-QUEST_SHIFT_THROW,6)==QUEST_SHIFT_NONE && s.z==0);
    CHECK(quest_shifter_move(&s,.5f*QUEST_SHIFT_PITCH,QUEST_SHIFT_THROW,6)==QUEST_SHIFT_NONE && s.z==0);
    CHECK(quest_shifter_move(&s,0,-.030f,6)==QUEST_SHIFT_NONE);
    CHECK(quest_shifter_move(&s,-QUEST_SHIFT_PITCH,-.040f,6)==QUEST_SHIFT_NONE && s.x==0);
    CHECK(quest_shifter_move(&s,-QUEST_SHIFT_PITCH,-QUEST_SHIFT_THROW,6)==4);
    quest_shifter_detent(&s,2,6);
    CHECK(quest_shifter_move(&s,QUEST_SHIFT_PITCH,-QUEST_SHIFT_THROW,6)==QUEST_SHIFT_NONE);
    CHECK(s.x==-QUEST_SHIFT_PITCH && s.gear==2);

    float pos[2][3]={{0}},squeeze[2]={1,0};
    int valid[2]={1,1},wheel[2]={0};
    quest_shifter_reset(&s);quest_shifter_knob(&s,0,pos[0]);
    quest_shifter_update(&s,0,0,6,1,valid,pos,squeeze,wheel);CHECK(!s.hand);
    squeeze[0]=0;quest_shifter_update(&s,5,0,6,1,valid,pos,squeeze,wheel);
    pos[0][1]+=.15f;squeeze[0]=1;
    quest_shifter_update(&s,10,0,6,1,valid,pos,squeeze,wheel);CHECK(!s.hand);
    pos[0][1]-=.15f;wheel[0]=1;
    quest_shifter_update(&s,15,0,6,1,valid,pos,squeeze,wheel);CHECK(!s.hand);
    wheel[0]=0;quest_shifter_update(&s,20,0,6,1,valid,pos,squeeze,wheel);CHECK(s.hand==1);
    valid[0]=0;pos[0][2]+=.10f;
    quest_shifter_update(&s,25,0,6,1,valid,pos,squeeze,wheel);CHECK(s.hand==1);
    valid[0]=1;CHECK(quest_shifter_update(&s,30,0,6,1,valid,pos,squeeze,wheel)==QUEST_SHIFT_NONE && s.z==0);
    squeeze[0]=NAN;quest_shifter_update(&s,540,0,6,1,valid,pos,squeeze,wheel);CHECK(!s.hand && s.blocked[0]);
    puts("H shifter passed all slot pairs, driver sides, either hand, neutral and tracking resets.");
    return 0;
}
