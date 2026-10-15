#include "../android/runtime/quest_haptics.h"
#include <stdio.h>
#include <math.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1;} } while(0)
int main(void)
{
    CHECK(quest_haptic_amplitude(0,0)==0);
    CHECK(quest_haptic_amplitude(65535,0)==1);
    CHECK(quest_haptic_amplitude(0,65535)==1);
    CHECK(quest_haptic_amplitude(12000,9000)<.08f);
    float previous=0;
    for(unsigned i=0;i<=65535;++i) {
        float a=quest_haptic_amplitude((uint16_t)i,0);
        CHECK(isfinite(a) && a>=previous && a<=1); previous=a;
    }
    /* A 4-ms collision between render frames survives at 30/72/120 FPS.
     * A zero report then stops it within a bounded 65-ms envelope. */
    const unsigned intervals[]={33,14,8};
    for(unsigned i=0;i<3;++i) {
        QuestHaptics h={0}; quest_haptics_focus(&h,1);
        quest_haptics_report(&h,1,65535,12000);
        quest_haptics_report(&h,5,0,0);
        CHECK(quest_haptics_sample(&h,intervals[i])==1);
        CHECK(quest_haptics_sample(&h,67)==0);
    }
    QuestHaptics h={0}; quest_haptics_focus(&h,1);
    /* Same-strength reports keep rough-road feedback alive; missing reports
     * expire. This also exercises the USB deduplication heartbeat contract. */
    for(unsigned now=0;now<=2000;now+=16) {
        quest_haptics_report(&h,now,12000,9000);
        float a=quest_haptics_sample(&h,now);
        CHECK(a>0 && a<.08f);
    }
    CHECK(quest_haptics_sample(&h,2250)==0);
    quest_haptics_report(&h,2300,60000,0);
    quest_haptics_focus(&h,0);
    quest_haptics_report(&h,2301,65535,65535);
    CHECK(quest_haptics_sample(&h,2302)==0);
    quest_haptics_focus(&h,1);
    CHECK(quest_haptics_sample(&h,2303)==0); /* no replay after focus returns */
    quest_haptics_report(&h,0xfffffff0u,65535,0);
    CHECK(quest_haptics_sample(&h,0x10)==1);
    CHECK(quest_haptics_sample(&h,0x100)==0); /* timer wrap */
    puts("Touch haptics: strength range, short impacts, road heartbeat, timeout and focus checks passed");
    return 0;
}
