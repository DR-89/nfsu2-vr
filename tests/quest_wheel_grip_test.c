#include "../android/runtime/quest_wheel_grip.h"
#include "../android/runtime/quest_ghost_hands.h"
#include "../android/runtime/quest_glove_asset.h"
#include "../android/runtime/quest_cockpit.h"
#include "../android/runtime/quest_driving_input.h"
#include "quest_steering_reference.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1;} } while(0)
static void location(const QuestWheelGrip *w,float theta,float out[3])
{ for(int k=0;k<3;++k) out[k]=w->pivot[k]+w->radius*(cosf(theta)*w->right[k]+sinf(theta)*w->up[k]); }
static int fixed_contact(const QuestWheelGrip *w,int hand,float rendered,const float contact[16])
{
    float glove[16],inverse[16],relative[16];
    quest_wheel_hand_matrix(w,hand,rendered,glove);
    quest_cockpit_wheel(inverse,w->pivot,w->axis,-rendered);
    quest_cockpit_multiply(relative,inverse,glove);
    for(int k=0;k<16;++k) if(fabsf(relative[k]-contact[k])>.00002f) return 0;
    return 1;
}
static int held_contact_test(void)
{
    const float pivot[3]={.01f,-.23f,-.34f},axis[3]={0,.6f,.8f};
    const int rates[]={30,60,72,90,120};
    for(int rate=0;rate<5;++rate)for(int mask=1;mask<=3;++mask)for(int affected=0;affected<2;++affected) {
        if(!(mask&(1<<affected)))continue;
        QuestWheelGrip w;quest_wheel_configure(&w,pivot,axis,.185f);
        int valid[2]={1,1};float p[2][3],squeeze[2]={(mask&1)!=0,(mask&2)!=0};
        float theta[2]={.7f,-.9f},contact[2][16],rendered=.35f;uint32_t tick=100;
        for(int h=0;h<2;++h)location(&w,theta[h],p[h]);
        CHECK(quest_wheel_update(&w,tick,1,valid,p,squeeze,rendered));
        for(int h=0;h<2;++h)quest_wheel_hand_matrix(&w,h,0,contact[h]);
        for(int phase=0;phase<45;++phase) {
            int other=1-affected;
            if(phase==0) { theta[affected]-=.15f;theta[other]-=.23f; }
            if(phase==1) { valid[affected]=0;theta[other]-=.1f; }
            if(phase==2) { valid[affected]=1;theta[affected]+=1.1f; }
            if(phase==3) theta[affected]+=1.0f; /* rejected tracking jump */
            if(phase==5) theta[affected]+=QUEST_WHEEL_PI; /* opposite side of hub */
            if(phase==6) squeeze[affected]=NAN;
            if(phase==7) { squeeze[affected]=1;theta[affected]+=.3f; }
            if(phase==8 || phase==9) { theta[affected]-=.12f;theta[other]+=.08f; }
            if(phase>=10) { theta[0]-=.25f;theta[1]-=.25f; } /* continue past full lock */
            for(int h=0;h<2;++h)location(&w,theta[h],p[h]);
            if(phase==4)memcpy(p[affected],pivot,sizeof pivot);
            CHECK(quest_wheel_update(&w,tick+=1000/rates[rate],1,valid,p,squeeze,0));
            rendered=quest_cockpit_steering_smooth(rendered,w.angle/QUEST_WHEEL_LOCK_RADIANS,1.0f/rates[rate]);
            for(int h=0;h<2;++h)if(mask&(1<<h)) {
                CHECK(w.grabbed[h]);
                CHECK(fixed_contact(&w,h,rendered,contact[h]));
            }
        }
        CHECK(fabsf(w.angle-QUEST_WHEEL_LOCK_RADIANS)<.00001f);
        /* A real release permits a new point anywhere on the rim. Acquiring
         * while the other hand moves must use the resulting wheel angle. */
        squeeze[affected]=0;quest_wheel_update(&w,tick+=14,1,valid,p,squeeze,0);
        theta[affected]=2.4f;theta[1-affected]-=.15f;squeeze[affected]=1;
        for(int h=0;h<2;++h)location(&w,theta[h],p[h]);
        CHECK(quest_wheel_update(&w,tick+=14,1,valid,p,squeeze,0));
        float glove[16];quest_wheel_hand_matrix(&w,affected,w.angle/QUEST_WHEEL_LOCK_RADIANS,glove);
        for(int k=0;k<3;++k) {
            float expected=pivot[k]+(p[affected][k]-pivot[k])*(.212f/.185f)+.020f*axis[k];
            CHECK(fabsf(glove[12+k]-expected)<.00001f);
        }
        CHECK(!fixed_contact(&w,affected,w.angle/QUEST_WHEEL_LOCK_RADIANS,contact[affected]));
        if(mask==3)CHECK(fixed_contact(&w,1-affected,rendered,contact[1-affected]));
    }
    return 0;
}
static int steering_response_test(void)
{
    const float pivot[3]={0},axis[3]={0,.6f,.8f};
    /* Reproduce the old failure: even a 40-degree hand turn was swallowed
     * by the game's pad deadzone at the default 360-degree wheel range. */
    CHECK(retail_steering((int16_t)lrintf(40.0f/180*32767))==0);
    for(int degrees=180;degrees<=1080;degrees+=90)for(int gain=1;gain<=6;++gain) {
        QuestWheelGrip w;quest_wheel_configure(&w,pivot,axis,.185f);
        w.limit=degrees*QUEST_WHEEL_PI/360;w.sensitivity=gain*.5f;
        w.latched=1;w.grabbed[0]=1;
        float previous=-1;
        for(int step=-1000;step<=1000;++step) {
            float fraction=step/1000.0f;
            w.angle=w.limit*fraction;
            int16_t packet=quest_wheel_input(&w,1,0);
            float response=retail_steering(packet);
            float expected=fmaxf(-1,fminf(1,fraction*w.sensitivity));
            CHECK(fabsf(response-expected)<.00003f && response>=previous-.000001f);
            CHECK(w.angle==w.limit*fraction); /* compensation cannot move hands */
            if(step==0)CHECK(packet==0);
            previous=response;
        }
    }
    /* Pose -> angular motion -> packet -> original game filter, for either
     * hand and both hands. A one-degree turn must already steer both ways. */
    for(int mask=1;mask<=3;++mask)for(int direction=-1;direction<=1;direction+=2) {
        QuestWheelGrip w;quest_wheel_configure(&w,pivot,axis,.185f);
        int valid[2]={1,1};float p[2][3],squeeze[2]={(mask&1)!=0,(mask&2)!=0};
        location(&w,.7f,p[0]);location(&w,2.4f,p[1]);
        CHECK(quest_wheel_update(&w,100,1,valid,p,squeeze,0));
        for(int degree=1;degree<=5;++degree) {
            float travel=direction*degree*QUEST_WHEEL_PI/180;
            location(&w,.7f-travel,p[0]);location(&w,2.4f-travel,p[1]);
            CHECK(quest_wheel_update(&w,100+degree*14,1,valid,p,squeeze,0));
            float response=retail_steering(quest_wheel_input(&w,1,0));
            CHECK(response*direction>0 && fabsf(response-direction*degree/180.0f)<.00003f);
        }
    }
    /* Preserve every physical stick value and its response when switching
     * to a held wheel, then releasing it. Center/drift must stay centered. */
    for(int raw=-32768;raw<=32767;++raw) {
        QuestWheelGrip w;quest_wheel_configure(&w,pivot,axis,.185f);
        int valid[2]={1,1};float p[2][3],squeeze[2]={1,0};
        location(&w,0,p[0]);location(&w,2,p[1]);
        CHECK(quest_wheel_input(&w,1,(int16_t)raw)==raw);
        float effective=quest_wheel_stick_steering((int16_t)raw);
        CHECK(fabsf(effective-retail_steering((int16_t)raw))<.000001f);
        CHECK(quest_wheel_update(&w,100,1,valid,p,squeeze,effective));
        int16_t packet=quest_wheel_input(&w,1,0);
        CHECK(fabsf(retail_steering(packet)-retail_steering((int16_t)raw))<.00003f);
        squeeze[0]=0;CHECK(!quest_wheel_update(&w,114,1,valid,p,squeeze,0));
        CHECK(quest_wheel_input(&w,1,0)==packet);
        CHECK(quest_wheel_input(&w,0,(int16_t)raw)==raw);
    }
    puts("Steering: 24% retail deadzone reproduced; 132066 range/gain samples, small hand turns and all 65536 stick transitions passed");
    return 0;
}
int main(int argc,char **argv)
{
    CHECK(!held_contact_test());
    CHECK(!steering_response_test());
    uint32_t test_tick=0;
    /* Real Xbox packets: gameplay face-button swap, discrete gear shifts,
     * no throttle/brake axis leakage, and unchanged menu navigation. */
    for(unsigned frame_ms=8;frame_ms<=40;frame_ms+=8) {
        QuestShiftInput shift={0};int up=0,down=0;uint8_t prev_up=0,prev_down=0;
        for(unsigned t=0;t<2400;t+=frame_ms) {
            int driving=t>=200 && t<2000;
            int y=t<300?25000:t<400?0:t<900?25000:t<1000?0:t<1400?-25000:t<1500?0:t<1900?25000:0;
            uint16_t digital=0x10;uint8_t a[8]={255,0,0,0,0,255,123,231};
            int16_t axes[4]={-1234,5678,0,(int16_t)y};
            quest_driving_input(&shift,0xfffffc00u+t,driving,&digital,a,axes); /* timer wrap */
            CHECK(a[0]==(driving?0:255) && a[1]==(driving?255:0));
            CHECK(a[5]==255 && a[6]==123 && a[7]==231 && axes[0]==-1234 && axes[1]==5678);
            if(driving) {
                CHECK(digital==0x10 && axes[2]==0 && axes[3]==0);
                if(t<400)CHECK(!a[2] && !a[3]); /* held on entry is not a shift */
                up+=a[2] && !prev_up;down+=a[3] && !prev_down;
            } else CHECK(digital==(y>16000?0x11:0x10) && axes[3]==y && !a[2] && !a[3]);
            prev_up=a[2];prev_down=a[3];
        }
        CHECK(up==2 && down==1);
    }
    QuestShiftInput shift={0};
    for(int mode=0;mode<2;++mode)for(int face=0;face<4;++face) {
        uint16_t digital=0;uint8_t a[8]={0};a[0]=(face&1)?255:0;a[1]=(face&2)?255:0;
        int16_t axes[4]={0};quest_driving_input(&shift,10,mode,&digital,a,axes);
        CHECK(a[mode?1:0]==((face&1)?255:0) && a[mode?0:1]==((face&2)?255:0));
    }
    for(int i=0;i<100;++i) { /* neutral noise / sideways deflection cannot shift */
        uint16_t digital=0;uint8_t a[8]={0};int16_t axes[4]={0,0,30000,(i%2)?19000:-19000};
        quest_driving_input(&shift,i*20,1,&digital,a,axes);CHECK(!a[2] && !a[3] && (!digital || digital==1));
    }
    QuestDrivingButtons b={0};
    CHECK(quest_driving_buttons(&b,0,1,0)==0x10); /* left is held rear view */
    CHECK(quest_driving_buttons(&b,300,1,0)==0x10);
    CHECK(!quest_driving_buttons(&b,400,0,0));
    CHECK(!quest_driving_buttons(&b,500,0,1));
    CHECK(quest_driving_buttons(&b,600,0,0)==0x20); /* right camera pulse */
    CHECK(quest_driving_buttons(&b,650,0,0)==0x20);
    CHECK(!quest_driving_buttons(&b,700,0,0));
    CHECK(!quest_driving_buttons(&b,800,1,1));
    CHECK(!quest_driving_buttons(&b,810,0,1)); /* partial chord release is consumed */
    CHECK(!quest_driving_buttons(&b,820,1,0));
    CHECK(!quest_driving_buttons(&b,830,0,0));
    CHECK(quest_driving_buttons(&b,840,1,0)==0x10);
    /* Check the actual Xbox packet, not just internal action flags. Grips
     * must never supply White/Black; camera must remain usable in the cabin. */
    const unsigned click_cases[][5]={ /* time, left, right, White, Black */
        {0,0,0,0,0},{10,0,1,0,0},{30,0,0,255,0},{100,0,0,255,0},
        {130,0,0,0,0},{200,1,0,0,255},{300,1,0,0,255},
        {310,1,1,0,0},{320,0,1,0,0},{330,1,0,0,0},{340,0,0,0,0},
        {400,0,1,0,0},{420,0,0,255,0},{520,0,0,0,0},
        {600,0,1,0,0},{650,1,1,0,0},{680,0,0,0,0},{700,1,0,0,255},{800,0,0,0,0}
    };
    for(int cabin=0;cabin<2;++cabin) {
        QuestDrivingButtons controls={0};
        for(unsigned i=0;i<sizeof click_cases/sizeof click_cases[0];++i) {
            uint8_t packet[8]={17,31,49,63,200,210,127,255};
            const unsigned *c=click_cases[i];
            quest_driving_analog(&controls,c[0],c[1],c[2],cabin,packet);
            CHECK(packet[5]==c[3] && packet[4]==(cabin?0:c[4]));
            CHECK(packet[0]==17 && packet[1]==31 && packet[2]==49 && packet[3]==63);
            CHECK(packet[6]==127 && packet[7]==255);
        }
    }
    /* Follow complete rings through the Xbox White rising edge, including
     * exit from the added cockpit slot, at low and high render rates. */
    for(unsigned frame_ms=8;frame_ms<=40;frame_ms+=8) {
        QuestDrivingButtons controls={0};
        unsigned slot=0,expected=0,events=0;int cabin=0,entry=0;uint8_t prior=0;
        for(unsigned click=0;click<20;++click) {
            for(unsigned t=0;t<320;t+=frame_ms) {
                uint8_t packet[8]={0};
                quest_driving_analog(&controls,click*320+t,0,t<40,cabin,packet);
                if(packet[5] && !prior) {
                    int action=quest_cockpit_cycle(slot,3,1,cabin,1,&entry);
                    if(action==0 || action==2) slot=(slot+1)%4;
                    cabin=action==1;expected=(expected+1)%5;++events;
                    CHECK((cabin?4:slot)==expected);
                }
                prior=packet[5];
            }
        }
        CHECK(events==20 && !cabin && slot==0);
    }
    const float pivot[3]={.000155f,-.212722f,-.328760f};
    const float axis[3]={-.00175054f,.28863694f,.95743702f};
    QuestWheelGrip w;
    int valid[2]={1,1}; float p[2][3],g[2]={0,0};
    for(int fps=30;fps<=120;fps+=6) for(int hands=1;hands<=2;++hands) {
        quest_wheel_configure(&w,pivot,axis,.185f);
        location(&w,QUEST_WHEEL_PI,p[0]); location(&w,0,p[1]);
        g[0]=g[1]=0; quest_wheel_update(&w,test_tick+=14,1,valid,p,g,.2f);
        g[0]=1; g[1]=hands==2;
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,.2f));
        CHECK(fabsf(w.angle-.2f*QUEST_WHEEL_LOCK_RADIANS)<.00001f);
        /* Turn right 90 degrees in one second: same result at every rate. */
        for(int frame=1;frame<=fps;++frame) {
            float theta=-(float)frame/fps*QUEST_WHEEL_PI*.5f;
            location(&w,QUEST_WHEEL_PI+theta,p[0]); location(&w,theta,p[1]);
            CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,.2f));
        }
        CHECK(fabsf(w.angle-(.2f*QUEST_WHEEL_LOCK_RADIANS+.5f*QUEST_WHEEL_PI))<.00001f);
        float before=w.angle;
        g[1]=1; CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
        CHECK(fabsf(w.angle-before)<.00001f); /* second-hand pickup: no jump */
        g[0]=0; CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
        CHECK(fabsf(w.angle-before)<.00001f); /* release one: no jump */
        g[1]=0; CHECK(!quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
    }
    quest_wheel_configure(&w,pivot,axis,.185f);
    location(&w,3.10f,p[0]); location(&w,0,p[1]);
    g[0]=1;g[1]=0; CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
    location(&w,-3.10f,p[0]); CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
    CHECK(fabsf(w.angle+.0831853f)<.00001f); /* wrap does not reverse steering */
    float before_loss=w.angle;
    valid[0]=0; CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
    location(&w,1.0f,p[0]);valid[0]=1;
    CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0)); /* still held; recovery does not jump */
    CHECK(w.angle==before_loss && !w.blocked[0]);
    g[0]=0; quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0);
    g[0]=1; CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
    CHECK(!quest_wheel_update(&w,test_tick+=14,0,valid,p,g,0)); /* cameras/menu/focus disable grabbing */
    CHECK(!quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
    g[0]=0; quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0); g[0]=1;
    p[0][2]-=1; CHECK(!quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0)); /* cannot grab across the cabin */
    location(&w,0,p[0]); CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
    float before_jump=w.angle;
    location(&w,1.0f,p[0]); CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
    CHECK(w.angle==before_jump && !w.blocked[0]); /* discard teleport delta, keep grip */
    quest_wheel_configure(&w,pivot,axis,.185f);
    location(&w,0,p[0]); CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,.95f));
    location(&w,-.5f,p[0]); CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
    CHECK(fabsf(w.angle-QUEST_WHEEL_PI)<.00001f); /* stop at 180 degrees */
    location(&w,-.4f,p[0]); CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
    CHECK(fabsf(w.angle-(QUEST_WHEEL_LOCK_RADIANS-.1f))<.00001f); /* no dead travel returning from lock */
    for(int hand=0;hand<2;++hand) for(int n=-360;n<=360;++n) {
        w.anchor[hand]=hand?0:QUEST_WHEEL_PI;
        float m[16],offset[3]; quest_wheel_hand_matrix(&w,hand,n/360.0f,m);
        for(int k=0;k<16;++k) CHECK(isfinite(m[k]));
        for(int k=0;k<3;++k) offset[k]=m[12+k]-pivot[k]-.020f*axis[k];
        CHECK(fabsf(sqrtf(quest_wheel_dot(offset,offset))-(.185f+.027f))<.00001f);
        for(int col=0;col<3;++col) CHECK(fabsf(quest_wheel_dot(m+4*col,m+4*col)-1)<.00001f);
        CHECK(quest_wheel_dot(m+4,axis)<-.999f); /* wrist (-Y) points toward the driver */
    }
    /* Crossed hands are allowed at every rim angle, with independent anchors.
     * A hand-over-hand transfer must not recenter or reverse the wheel. */
    for(int slot=0;slot<24;++slot) {
        quest_wheel_configure(&w,pivot,axis,.185f);
        float theta[2]={slot*QUEST_WHEEL_PI/12,(slot+7)*QUEST_WHEEL_PI/12};
        for(int h=0;h<2;++h) location(&w,theta[h],p[h]);
        g[0]=g[1]=1; CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,.35f));
        CHECK(w.grabbed[0] && w.grabbed[1]);
        theta[0]-=.15f;theta[1]-=.25f;
        for(int h=0;h<2;++h) location(&w,theta[h],p[h]);
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
        CHECK(fabsf(w.angle-(.35f*QUEST_WHEEL_LOCK_RADIANS+.20f))<.00001f);
        for(int h=0;h<2;++h) {
            float m[16];quest_wheel_hand_matrix(&w,h,w.angle/QUEST_WHEEL_LOCK_RADIANS,m);
            /* Each palm retains its acquisition point on the rotating wheel,
             * even if the two controllers move by slightly different amounts. */
            float attached[3];location(&w,theta[h]+(h?.25f:.15f)-.20f,attached);
            for(int k=0;k<3;++k) {
                float expected=pivot[k]+(attached[k]-pivot[k])*(.212f/.185f)+.020f*axis[k];
                CHECK(fabsf(m[12+k]-expected)<.00001f);
            }
        }
        float before=w.angle;g[0]=0;quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0);
        location(&w,theta[1]+.4f,p[0]);g[0]=1;
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0) && w.grabbed[0]);
        CHECK(fabsf(w.angle-before)<.00001f);
    }
    /* Traverse the entire 360-degree range in both directions, crossing
     * atan2's seam and transferring hands four times. Physical motion
     * must accumulate rather than wrapping the steering command to center. */
    for(int direction=-1;direction<=1;direction+=2) {
        quest_wheel_configure(&w,pivot,axis,.185f);
        g[0]=1;g[1]=0;
        location(&w,0,p[0]);location(&w,QUEST_WHEEL_PI,p[1]);
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,(float)-direction));
        for(int step=1;step<=180;++step) {
            float travel=step*(2*QUEST_WHEEL_PI/180),theta=-direction*travel;
            location(&w,theta,p[0]);location(&w,theta+QUEST_WHEEL_PI,p[1]);
            CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
            float expected=direction*(-QUEST_WHEEL_PI+travel);
            CHECK(fabsf(w.angle-expected)<.00004f);
            if(step%45==0) {
                float before=w.angle;int next=(step/45)%2;
                g[0]=g[1]=1;CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
                g[1-next]=0;CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
                CHECK(fabsf(w.angle-before)<.00001f);
            }
        }
        CHECK(fabsf(w.angle/QUEST_WHEEL_LOCK_RADIANS-direction)<.00001f);
        CHECK(quest_wheel_input(&w,1,0)==(direction<0?-32768:32767));
        /* Returning from either lock responds immediately. */
        float theta=-direction*(2*QUEST_WHEEL_PI-.1f);
        location(&w,theta,p[0]);location(&w,theta+QUEST_WHEEL_PI,p[1]);
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
        CHECK(fabsf(w.angle-direction*(QUEST_WHEEL_LOCK_RADIANS-.1f))<.00004f);
    }
    /* Releasing BOTH hands must retain the actual game input, including the
     * release frame and many later neutral-stick frames. Regrabbing another
     * part of the rim resumes from that angle, never the fallback stick. */
    for(int turn=-4;turn<=4;++turn) {
        quest_wheel_configure(&w,pivot,axis,.185f);
        valid[0]=valid[1]=1;g[0]=1;g[1]=0;
        location(&w,.4f,p[0]);location(&w,-1.1f,p[1]);
        CHECK(quest_wheel_input(&w,1,1234)==1234); /* stick before first grab */
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,turn*.25f));
        float retained=w.angle;int16_t command=quest_wheel_input(&w,1,0);
        if(turn==4) CHECK(command==32767);
        if(turn==-4) CHECK(command==-32768);
        g[0]=g[1]=0;
        for(int frame=0;frame<240;++frame) {
            location(&w,frame*.07f,p[0]);location(&w,-frame*.11f,p[1]);
            CHECK(!quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
            CHECK(w.angle==retained && w.latched && !w.grabbed[0] && !w.grabbed[1]);
            CHECK(quest_wheel_input(&w,1,0)==command);
            CHECK(quest_wheel_input(&w,1,(frame%2)?4096:-4096)==command); /* drift does not take over */
        }
        /* Free-hand tracking loss cannot recenter the retained steering. */
        valid[0]=valid[1]=0;CHECK(!quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
        CHECK(quest_wheel_input(&w,1,0)==command);
        valid[0]=valid[1]=1;quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0);
        location(&w,2.3f,p[1]);g[1]=1;
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
        CHECK(w.angle==retained && quest_wheel_input(&w,1,0)==command);
        float motion=turn>=0?.15f:-.15f;
        location(&w,2.3f+motion,p[1]);CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
        CHECK(fabsf(w.angle-(retained-motion))<.00001f);
        CHECK(quest_wheel_input(&w,0,1234)==1234); /* no override outside cockpit */
        quest_wheel_reset(&w);
        CHECK(!w.latched && quest_wheel_input(&w,1,1234)==1234);
        CHECK(!quest_wheel_update(&w,test_tick+=14,1,valid,p,g,.1f)); /* fresh squeeze required */
        g[1]=0;quest_wheel_update(&w,test_tick+=14,1,valid,p,g,.1f);g[1]=1;
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,.1f));
        CHECK(fabsf(w.angle-.1f*QUEST_WHEEL_LOCK_RADIANS)<.00001f);
        CHECK(!quest_wheel_update(&w,test_tick+=14,0,valid,p,g,0));
        CHECK(!w.latched && quest_wheel_input(&w,1,1234)==1234);
    }
    /* Hand -> held angle -> stick -> hand, repeatedly without changing camera.
     * Check both directions, either hand and two hands. Holding either grip
     * wins over the stick; after release, deliberate stick use must unlock it. */
    for(int mask=1;mask<=3;++mask)for(int direction=-1;direction<=1;direction+=2) {
        quest_wheel_configure(&w,pivot,axis,.185f);
        for(int cycle=0;cycle<4;++cycle) {
            valid[0]=valid[1]=1;g[0]=g[1]=0;
            location(&w,.4f,p[0]);location(&w,2.3f,p[1]);
            quest_wheel_update(&w,test_tick+=14,1,valid,p,g,.25f*direction);
            g[0]=(mask&1)!=0;g[1]=(mask&2)!=0;
            CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,.25f*direction));
            CHECK(fabsf(w.angle-direction*QUEST_WHEEL_PI*.25f)<.00001f);
            int16_t held=quest_wheel_input(&w,1,0);
            CHECK(quest_wheel_input(&w,1,-direction*20000)==held && w.latched);
            if(mask==3) { /* one remaining hand still has control */
                g[0]=0;CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
                CHECK(quest_wheel_input(&w,1,-direction*20000)==held && w.latched);
            }
            g[0]=g[1]=0;CHECK(!quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
            CHECK(quest_wheel_input(&w,1,0)==held && w.latched);
            CHECK(quest_wheel_input(&w,1,direction*8191)==held && w.latched);
            CHECK(quest_wheel_input(&w,1,direction*20000)==direction*20000 && !w.latched);
            CHECK(quest_wheel_input(&w,1,-direction*16000)==-direction*16000);
            CHECK(quest_wheel_input(&w,1,0)==0); /* ordinary stick recenters */
            /* Regrabbing after stick steering uses its current angle, not
             * the older hand-held angle, then retains that angle on release. */
            CHECK(!quest_wheel_update(&w,test_tick+=14,1,valid,p,g,-.5f*direction));
            g[0]=1;CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,-.5f*direction));
            CHECK(fabsf(w.angle+direction*QUEST_WHEEL_PI*.5f)<.00001f);
            g[0]=0;CHECK(!quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
            int16_t reheld=quest_wheel_input(&w,1,0);
            CHECK(fabsf(retail_steering(reheld)+direction*.5f)<.00003f);
            CHECK(quest_wheel_input(&w,1,direction*20000)==direction*20000 && !w.latched);
        }
    }
    /* Reproduce the reported diagonal pass THROUGH the hub. atan2 changes
     * by pi across the center even though this is not a wheel rotation.
     * Keep the physical grip for the entire crossing, then resume steering. */
    for(int hand=0;hand<2;++hand)for(int fps=30;fps<=120;fps+=30)for(int diagonal=0;diagonal<4;++diagonal) {
        quest_wheel_configure(&w,pivot,axis,.185f);
        valid[0]=valid[1]=1;g[0]=g[1]=0;g[hand]=1;
        float theta=(diagonal+.5f)*QUEST_WHEEL_PI*.5f;
        location(&w,theta,p[hand]);location(&w,0,p[1-hand]);
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,.3f));
        float held=w.angle;
        for(int frame=1;frame<=fps;++frame) {
            float across=1-2.0f*frame/fps;
            for(int k=0;k<3;++k) p[hand][k]=pivot[k]+across*w.radius*(cosf(theta)*w.right[k]+sinf(theta)*w.up[k]);
            CHECK(quest_wheel_update(&w,test_tick+=1000/fps,1,valid,p,g,0));
            CHECK(w.grabbed[hand] && !w.blocked[hand] && fabsf(w.angle-held)<.00001f);
        }
        location(&w,theta+QUEST_WHEEL_PI+.1f,p[hand]);
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
        CHECK(fabsf(w.angle-(held-.1f))<.00001f);
        /* A pose outage with a positively held button retains ownership;
         * recovery at another position rebases once without a steering jump. */
        held=w.angle;valid[hand]=0;
        for(int frame=0;frame<100;++frame) {
            CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
            CHECK(w.angle==held && w.grabbed[hand]);
        }
        valid[hand]=1;location(&w,theta,p[hand]);
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0) && w.angle==held);
        location(&w,theta+.1f,p[hand]);g[hand]=.2f; /* relaxed squeeze is still held */
        CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
        CHECK(fabsf(w.angle-(held-.1f))<.00001f);
        /* A real release still works without tracking, including stick takeover. */
        valid[hand]=0;g[hand]=0;
        CHECK(!quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
        CHECK(!w.grabbed[hand] && !w.blocked[hand]);
        CHECK(quest_wheel_input(&w,1,20000)==20000 && !w.latched);
    }
    /* Unknown button samples are not releases; bound disconnected-controller
     * retention to 500 ms, including the 32-bit clock wrapping. */
    quest_wheel_configure(&w,pivot,axis,.185f);test_tick=0xffffff00u;
    valid[0]=valid[1]=1;g[0]=1;g[1]=0;location(&w,0,p[0]);
    CHECK(quest_wheel_update(&w,test_tick,1,valid,p,g,.4f));
    float held=w.angle;g[0]=NAN;
    CHECK(quest_wheel_update(&w,test_tick+250,1,valid,p,g,0));
    CHECK(quest_wheel_update(&w,test_tick+499,1,valid,p,g,0) && w.angle==held);
    g[0]=1;location(&w,1.5f,p[0]);
    CHECK(quest_wheel_update(&w,test_tick+499,1,valid,p,g,0) && w.angle==held);
    g[0]=NAN;
    CHECK(!quest_wheel_update(&w,test_tick+999,1,valid,p,g,0) && w.blocked[0]);
    g[0]=1;CHECK(!quest_wheel_update(&w,test_tick+1000,1,valid,p,g,0));
    g[0]=0;valid[0]=0;CHECK(!quest_wheel_update(&w,test_tick+1010,1,valid,p,g,0));
    g[0]=1;valid[0]=1;CHECK(quest_wheel_update(&w,test_tick+1020,1,valid,p,g,0));
    /* The tracked hand continues steering without its contribution being
     * diluted by an occluded second hand; recovery does not countersteer. */
    quest_wheel_configure(&w,pivot,axis,.185f);test_tick=2000;
    g[0]=g[1]=1;location(&w,0,p[0]);location(&w,QUEST_WHEEL_PI,p[1]);
    CHECK(quest_wheel_update(&w,test_tick,1,valid,p,g,0));
    valid[0]=0;location(&w,QUEST_WHEEL_PI+.1f,p[1]);
    CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0));
    CHECK(w.grabbed[0] && w.grabbed[1] && fabsf(w.angle+.1f)<.00001f);
    held=w.angle;valid[0]=1;location(&w,1.0f,p[0]);
    CHECK(quest_wheel_update(&w,test_tick+=14,1,valid,p,g,0) && w.angle==held);
    QuestGhostMesh *mesh=malloc(sizeof *mesh); CHECK(mesh);
    FILE *dump=argc>1?fopen(argv[1],"wb"):NULL; CHECK(argc<=1 || dump);
    for(int closed=0;closed<2;++closed) {
        quest_ghost_mesh(mesh,closed);
        CHECK(mesh->count==4320 && mesh->count<QUEST_GHOST_MAX_VERTICES);
        for(unsigned i=0;i<mesh->count;++i) {
            QuestGhostVertex *v=&mesh->vertices[i];
            for(int k=0;k<3;++k) CHECK(isfinite(v->position[k]) && fabsf(v->position[k])<.15f);
            CHECK(fabsf(quest_wheel_dot(v->normal,v->normal)-1)<.00001f);
        }
        if(dump) { fwrite(&mesh->count,sizeof mesh->count,1,dump); fwrite(mesh->vertices,sizeof mesh->vertices[0],mesh->count,dump); }
    }
    if(dump) fclose(dump); free(mesh);
    if(argc>2) {
        FILE *f=fopen(argv[2],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size>0&&size<40000000);rewind(f);
        unsigned char *data=malloc((size_t)size);CHECK(data);CHECK(fread(data,1,size,f)==(size_t)size);fclose(f);
        QuestGloveAsset glove;CHECK(quest_glove_asset(&glove,data,size));
        CHECK(!quest_glove_asset(&glove,data,size-1));
        for(int offset=4;offset<32;offset+=4) {
            unsigned char saved[4];memcpy(saved,data+offset,4);memset(data+offset,255,4);
            CHECK(!quest_glove_asset(&glove,data,size));memcpy(data+offset,saved,4);
        }
        float bad=NAN;memcpy(data+32,&bad,4);CHECK(!quest_glove_asset(&glove,data,size));
        free(data);
    }
    puts("Driving buttons, one/two-hand steering, grip transitions, tracking loss and ghost mesh checks passed");
    return 0;
}
