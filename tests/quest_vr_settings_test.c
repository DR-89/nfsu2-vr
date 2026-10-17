#include "../android/runtime/quest_vr_settings.h"
#include "../android/runtime/quest_vr_panel.h"
#include "../android/runtime/quest_wheel_grip.h"
#include "../android/runtime/quest_cockpit.h"
#include "../android/runtime/quest_driving_input.h"
#include "../android/runtime/quest_cockpit_panels.h"
#include "../android/runtime/quest_culling.h"
#include "../android/runtime/quest_projection.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static float point_component(const float m[16],const float p[3],int row)
{ return m[row]*p[0]+m[4+row]*p[1]+m[8+row]*p[2]+m[12+row]; }
static int seat_mirror_test(void)
{
    unsigned cases=0;
    /* Compare with an independently positioned physical camera. Cover every
     * 1-cm menu setting, stereo eyes and tilted/turned head orientations. */
    for(int h=-35;h<=35;++h)for(int d=-40;d<=40;++d)for(int pose=0;pose<3;++pose) {
        float height=h*.01f,distance=d*.01f,yaw=pose*.7f,pitch=(pose-1)*.35f;
        float sy=sinf(yaw),cy=cosf(yaw),sp=sinf(pitch),cp=cosf(pitch);
        float axes[3][3]={{cy,0,-sy},{sy*sp,cp,cy*sp},{sy*cp,-sp,cy*cp}};
        float clips[2][16],planes[6][4],points[2][3];
        for(int eye=0;eye<2;++eye) {
            float physical[3]={eye?.032f:-.032f,.07f,.03f};
            float relocated[3]={physical[0],physical[1]+height,physical[2]+distance};
            float view[16]={0},reference[16]={0};view[15]=reference[15]=1;
            for(int row=0;row<3;++row)for(int col=0;col<3;++col) {
                view[col*4+row]=reference[col*4+row]=axes[row][col];
                view[12+row]-=axes[row][col]*physical[col];
                reference[12+row]-=axes[row][col]*relocated[col];
            }
            quest_cockpit_shift_eye(view,height,distance);
            const float mirrors[3][3]={{.35f,.20f,-.45f},{-.48f,-.12f,-.6f},{1.18f,-.12f,-.6f}};
            for(int mirror=0;mirror<3;++mirror)for(int row=0;row<4;++row)
                CHECK(fabsf(point_component(view,mirrors[mirror],row)-point_component(reference,mirrors[mirror],row))<2e-6f);
            /* A tracked free hand is expressed in shifted cabin coordinates,
             * so moving the seat must not move it relative to the head. */
            float hand[3]={.2f,.1f+height,-.4f+distance},base_hand[3]={.2f,.1f,-.4f};
            float original[16];memcpy(original,reference,sizeof original);
            quest_cockpit_shift_eye(original,-height,-distance);
            for(int row=0;row<4;++row)CHECK(fabsf(point_component(view,hand,row)-point_component(original,base_hand,row))<2e-6f);
            float projection[16]={0};projection[0]=1;projection[5]=-1;projection[8]=eye?.08f:-.08f;
            quest_projection_set_depth(projection,QUEST_COCKPIT_WORLD_NEAR_M,QUEST_WORLD_FAR);
            quest_cockpit_multiply(clips[eye],projection,view);
            for(int k=0;k<3;++k)points[eye][k]=relocated[k]-axes[2][k]*10;
            points[eye][1]=-points[eye][1];points[eye][2]=-points[eye][2]; /* culling returns guest down/forward */
        }
        CHECK(quest_culling_stereo(planes,clips[0],clips[1],.25f));
        for(int eye=0;eye<2;++eye)for(int plane=0;plane<6;++plane) {
            float value=planes[plane][3];
            for(int k=0;k<3;++k)value+=planes[plane][k]*points[eye][k];
            CHECK(value>=-.01f);
        }
        ++cases;
    }
    printf("Seat/mirror alignment: %u stereo configurations, physical-camera reference, hands and culling passed.\n",cases);
    return 0;
}
int main(int argc,char **argv)
{
    CHECK(!seat_mirror_test());
    QuestVRSettings s=quest_vr_defaults(),round=quest_vr_defaults();char text[512];
    CHECK(s.sensitivity==1 && s.rotation==360 && s.resolution==0);
    quest_vr_parse(&s,"steering_sensitivity=nan\nwheel_rotation=inf\nseat_height=99\nseat_distance=-99\nresolution=999999999999\nunknown=7\n");
    CHECK(s.sensitivity==1 && s.rotation==360 && s.height==.35f && s.distance==-.4f && s.resolution==0);
    s=(QuestVRSettings){2,720,-.13f,.22f,4};quest_vr_format(&s,text,sizeof text);quest_vr_parse(&round,text);
    CHECK(!memcmp(&s,&round,sizeof s));
    for(int i=0;i<1000;++i){quest_vr_adjust(&s,0,1);quest_vr_adjust(&s,1,-1);quest_vr_adjust(&s,2,-1);quest_vr_adjust(&s,3,1);}
    CHECK(s.sensitivity==3 && s.rotation==180 && s.height==-.35f && s.distance==.4f);
    QuestVRMenu menu={0};s=quest_vr_defaults();
    CHECK(!quest_vr_menu_step(&menu,&s,0,0,QUEST_VR_OPEN));
    CHECK(!quest_vr_menu_step(&menu,&s,1,1,QUEST_VR_OPEN) && !menu.open);
    CHECK(!quest_vr_menu_step(&menu,&s,1,1,0));
    CHECK(quest_vr_menu_step(&menu,&s,1,1,QUEST_VR_OPEN)==4 && menu.open);
    quest_vr_menu_step(&menu,&s,2,1,0);
    CHECK(quest_vr_menu_step(&menu,&s,3,1,QUEST_VR_RIGHT)==5 && fabsf(s.sensitivity-1.1f)<1e-5f);
    CHECK(quest_vr_menu_step(&menu,&s,100,1,QUEST_VR_RIGHT)==4);
    CHECK(quest_vr_menu_step(&menu,&s,354,1,QUEST_VR_RIGHT)==5);
    CHECK(quest_vr_menu_step(&menu,&s,400,1,QUEST_VR_BACK)==6 && !menu.open);
    CHECK(quest_vr_menu_step(&menu,&s,500,1,QUEST_VR_BACK)==4);
    CHECK(quest_vr_menu_step(&menu,&s,600,1,0)==4);
    CHECK(!quest_vr_menu_step(&menu,&s,601,1,0));
    CHECK(quest_vr_menu_step(&menu,&s,602,1,QUEST_VR_OPEN)==4 && menu.open);
    CHECK(quest_vr_menu_step(&menu,&s,603,0,QUEST_VR_OPEN)==2 && !menu.open);
    CHECK(!quest_vr_menu_step(&menu,&s,604,1,QUEST_VR_OPEN) && !menu.open);
    /* Separate physical limits and game sensitivity. The displayed wheel
     * and contact matrix must continue beyond 180 degrees at larger ranges. */
    const float pivot[3]={0},axis[3]={0,0,1};
    for(int degrees=180;degrees<=1080;degrees+=90)for(int gain=1;gain<=6;++gain) {
        QuestWheelGrip wheel;quest_wheel_configure(&wheel,pivot,axis,.185f);
        wheel.limit=degrees*QUEST_WHEEL_PI/360;wheel.sensitivity=gain*.5f;
        wheel.latched=1;wheel.grabbed[0]=1;wheel.angle=wheel.limit*.25f;
        int16_t expected=(int16_t)lrintf(fminf(1,.25f*wheel.sensitivity)*32767);
        CHECK(abs(quest_wheel_input(&wheel,1,0)-expected)<=1);
        wheel.anchor[0]=.9f;
        float hand[16],inverse[16],local[16];
        quest_wheel_hand_matrix(&wheel,0,wheel.angle/QUEST_WHEEL_PI,hand);
        quest_cockpit_wheel_angle(inverse,pivot,axis,wheel.angle);
        quest_cockpit_multiply(local,inverse,hand);
        CHECK(fabsf(local[12]-cosf(.9f)*(.185f+.027f))<1e-5f);
        CHECK(fabsf(local[13]-sinf(.9f)*(.185f+.027f))<1e-5f);
    }
    /* Horizontal actions are edge-triggered and never trigger shifts. */
    for(int side=-1;side<=1;side+=2) {
        QuestShiftInput input={0};int count=0,previous=0;
        for(int t=0;t<800;t+=10) {
            uint16_t buttons=0;uint8_t analog[8]={0};int16_t sticks[4]={0,0,t>50?side*25000:0,0};
            quest_driving_input(&input,t,1,&buttons,analog,sticks);
            if(buttons && !previous)++count;previous=buttons;
            CHECK(!analog[2] && !analog[3]);CHECK(!buttons || buttons==(side<0?8:1));
        }
        CHECK(count==1);
    }
    if(argc>1) {
        uint32_t *pixels=malloc(1024*768*4);CHECK(pixels);
        s=quest_vr_defaults();s.resolution=3;menu.open=1;menu.row=4;
        quest_vr_panel(pixels,&s,&menu,1,1);
        FILE *f=fopen(argv[1],"wb");CHECK(f);fprintf(f,"P6\n1024 768\n255\n");
        for(int i=0;i<1024*768;++i){unsigned char rgb[3]={(pixels[i]>>16)&255,(pixels[i]>>8)&255,pixels[i]&255};fwrite(rgb,1,3,f);}
        fclose(f);free(pixels);
    }
    puts("VR settings: parse/bounds/roundtrip, menu consumption, wheel ratios/contact and career-action pulses passed.");return 0;
}
