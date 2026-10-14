#include "quest_frame_timing.h"
#include <math.h>
#include <stdio.h>

int main(void)
{
    int failed=0;
    if(quest_refresh_option(NULL,"1")!=72) ++failed;
    if(quest_refresh_option(NULL,NULL)!=0) ++failed;
    if(quest_refresh_option("120","1")!=120) ++failed;
    if(quest_refresh_option("90",NULL)!=90) ++failed;
    if(quest_refresh_option("80",NULL)!=80) ++failed;
    if(quest_refresh_option("72",NULL)!=72) ++failed;
    if(quest_refresh_option("120junk","1")!=0) ++failed;
    if(quest_refresh_option("0","1")!=0) ++failed;
    for (uint32_t kind=0;kind<32;++kind) for (uint32_t interval=0;interval<8;++interval)
        for (uint32_t late=0;late<2;++late) {
            uint32_t original=0x89abc000u | (late<<8) | (interval<<5) | kind;
            uint32_t updated=quest_present_60hz(original);
            uint32_t expected=kind==1 && interval>1 ? 1:interval;
            if (((updated>>5)&7)!=expected || (updated&~224u)!=(original&~224u)) ++failed;
            updated=quest_present_xr(original);
            if (kind==1) {
                if ((updated & 224u) || !(updated & 256u) ||
                    (updated & ~480u)!=(original & ~480u)) ++failed;
            } else if (updated!=original) ++failed;
        }
    if (quest_sim_minimum_60hz(1.0f/30.0f)!=1.0f/60.0f) ++failed;
    if (quest_sim_minimum_60hz(1.0f/120.0f)!=1.0f/120.0f) ++failed;
    if (quest_sim_minimum_60hz(0)!=0) ++failed;
    if (!isnan(quest_sim_minimum_60hz(NAN))) ++failed;
    if (quest_sim_minimum_xr(1.0f/30.0f)!=0) ++failed;
    if (quest_sim_minimum_xr(1.0f/60.0f)!=0) ++failed;
    if (quest_sim_minimum_xr(1.0f/120.0f)!=0) ++failed;
    if (quest_sim_minimum_xr(0)!=0) ++failed;
    if (!isnan(quest_sim_minimum_xr(NAN))) ++failed;
    /* Alternating early/late frames must not accelerate a 72 or 120 Hz race. */
    double wall=0,sim=0;
    for(unsigned rate=72;rate<=120;rate+=48) {
    for(unsigned i=0;i<7200;++i) {
        float dt=1.0f/rate+(i&1?0.002f:-0.002f);
        wall+=dt; sim+=fmaxf(dt,quest_sim_minimum_xr(1.0f/30.0f));
    }
    if(fabs(wall-sim)>0.0001) ++failed;
    }
    printf("Quest frame timing checks: %d failures\n",failed);
    return failed?1:0;
}
