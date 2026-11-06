#include "../android/runtime/quest_vr_settings.h"
#include "../android/runtime/quest_vr_panel.h"
#include "../android/runtime/quest_wheel_grip.h"
#include "../android/runtime/quest_cockpit.h"
#include "../android/runtime/quest_driving_input.h"
#include "../android/runtime/quest_cockpit_panels.h"
#include "../android/runtime/quest_cockpit_vehicle.h"
#include "../android/runtime/quest_culling.h"
#include "../android/runtime/quest_projection.h"
#include "quest_steering_reference.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static float point_component(const float m[16],const float p[3],int row)
{ return m[row]*p[0]+m[4+row]*p[1]+m[8+row]*p[2]+m[12+row]; }
static int led_menu_test(void)
{
    QuestVRSettings s=quest_vr_defaults(),loaded=quest_vr_defaults();
    QuestVRMenu menu={.open=1,.row=QUEST_VR_LED_ROW};
    quest_vr_parse(&s,"version=1\nled_color=0\nled_strips=1\n");
    CHECK(s.led_music_pulse==1); /* existing save with no pulse setting */
    quest_vr_parse(&s,"led_color=nan\nled_color=1.5\nled_strips=2\nled_music_pulse=0.5\n");
    CHECK(s.led_strips==1 && s.led_color==0 && s.led_music_pulse==1);
    CHECK(quest_vr_menu_step(&menu,&s,0,1,QUEST_VR_RIGHT)==5 && !s.led_strips);
    CHECK(quest_vr_menu_step(&menu,&s,500,1,QUEST_VR_RIGHT)==4 && !s.led_strips);
    quest_vr_menu_step(&menu,&s,510,1,QUEST_VR_DOWN);
    CHECK(menu.row==QUEST_VR_LED_COLOR_ROW);
    CHECK(quest_vr_menu_step(&menu,&s,520,1,QUEST_VR_LEFT)==5 && s.led_color==QUEST_LED_COLORS-1);
    quest_vr_menu_step(&menu,&s,530,1,QUEST_VR_DOWN);
    CHECK(menu.row==QUEST_VR_LED_PULSE_ROW && s.led_music_pulse);
    CHECK(quest_vr_menu_step(&menu,&s,540,1,QUEST_VR_RIGHT)==5 && !s.led_music_pulse);
    CHECK(quest_vr_menu_step(&menu,&s,1000,1,QUEST_VR_RIGHT)==4 && !s.led_music_pulse);
    char text[512];
    CHECK(quest_vr_format(&s,text,sizeof text)<(int)sizeof text);
    quest_vr_parse(&loaded,text);
    CHECK(loaded.led_color==s.led_color && !loaded.led_strips && !loaded.led_music_pulse);
    quest_vr_menu_step(&menu,&loaded,1010,1,QUEST_VR_UP);
    quest_vr_menu_step(&menu,&loaded,1020,1,0);
    quest_vr_menu_step(&menu,&loaded,1030,1,QUEST_VR_UP);
    CHECK(menu.row==QUEST_VR_LED_ROW);
    CHECK(quest_vr_menu_step(&menu,&loaded,1040,1,QUEST_VR_ACCEPT)==5 && loaded.led_strips);
    CHECK(loaded.led_color==s.led_color && !loaded.led_music_pulse);
    menu.row=QUEST_VR_DEFAULTS_ROW;quest_vr_menu_step(&menu,&loaded,1050,1,0);
    quest_vr_menu_step(&menu,&loaded,1060,1,QUEST_VR_ACCEPT);
    CHECK(loaded.led_color==0 && loaded.led_strips && loaded.led_music_pulse);
    return 0;
}
static int haptics_menu_test(void)
{
    QuestVRSettings s=quest_vr_defaults();
    QuestVRMenu m={.open=1,.row=QUEST_VR_HAPTICS_ROW};
    quest_vr_parse(&s,"version=1\nseat_height=0.05\n");
    CHECK(!s.engine_rumble && s.grip_feedback && s.master_gain_db==0);
    quest_vr_parse(&s,"engine_rumble=2\ngrip_feedback=nan\nmaster_gain_db=inf\nmaster_gain_db=4.5\n");
    CHECK(!s.engine_rumble && s.grip_feedback && s.master_gain_db==0);
    CHECK(quest_vr_menu_step(&m,&s,1,1,QUEST_VR_ACCEPT)==4 && m.row==QUEST_VR_ENGINE_ROW);
    CHECK(quest_vr_menu_step(&m,&s,2,1,QUEST_VR_ACCEPT)==4 && !s.engine_rumble);
    quest_vr_menu_step(&m,&s,3,1,0);
    CHECK(quest_vr_menu_step(&m,&s,4,1,QUEST_VR_ACCEPT)==5 && s.engine_rumble);
    quest_vr_menu_step(&m,&s,5,1,QUEST_VR_DOWN);
    CHECK(m.row==QUEST_VR_GRIP_ROW && quest_vr_menu_step(&m,&s,6,1,QUEST_VR_LEFT)==5 && !s.grip_feedback);
    CHECK(quest_vr_menu_step(&m,&s,500,1,QUEST_VR_LEFT)==4 && !s.grip_feedback);
    quest_vr_menu_step(&m,&s,501,1,QUEST_VR_DOWN);
    CHECK(m.row==QUEST_VR_HAPTICS_BACK && quest_vr_menu_step(&m,&s,502,1,QUEST_VR_DOWN)==4);
    quest_vr_menu_step(&m,&s,900,1,QUEST_VR_DOWN);
    CHECK(m.row==QUEST_VR_ENGINE_ROW && quest_vr_menu_step(&m,&s,901,1,QUEST_VR_BACK)==4);
    CHECK(m.open && m.row==QUEST_VR_HAPTICS_ROW);
    quest_vr_menu_step(&m,&s,902,1,QUEST_VR_UP);
    CHECK(m.row==QUEST_VR_GAIN_ROW && quest_vr_menu_step(&m,&s,903,1,QUEST_VR_RIGHT)==5 && s.master_gain_db==1);
    for(int n=0;n<30;++n)quest_vr_adjust(&s,m.row,1);
    CHECK(s.master_gain_db==12);
    for(int n=0;n<30;++n)quest_vr_adjust(&s,m.row,-1);
    CHECK(s.master_gain_db==-12);
    m.row=QUEST_VR_DEFAULTS_ROW;
    CHECK(quest_vr_menu_step(&m,&s,904,1,QUEST_VR_ACCEPT)==(5|QUEST_VR_RESET_MIRRORS));
    CHECK(!s.engine_rumble && s.grip_feedback && s.master_gain_db==0);
    return 0;
}
static int minimap_shortcut_test(void)
{
    const unsigned both=QUEST_MAP_X|QUEST_MAP_A;
    for(int h=0;h<2;++h) {
        unsigned button=1u<<h,other=both^button;
        QuestMinimapGrip grip={0};
        QuestMinimapShortcut key={0};
        CHECK(!quest_minimap_shortcut(&key,0,1,&grip,both,0));
        CHECK(!quest_minimap_shortcut(&key,1,1,&grip,both,button));
        grip.hover[h]=1;
        CHECK(!quest_minimap_shortcut(&key,2,1,&grip,both,button)); /* already held on approach */
        CHECK(!quest_minimap_shortcut(&key,3,1,&grip,both,other) && !key.captured);
        CHECK(quest_minimap_shortcut(&key,4,1,&grip,both,both) && key.captured==button);
        uint8_t analog[8]={255,0,255};int16_t sticks[4]={0};uint16_t digital=0;
        QuestShiftInput shift={0};
        quest_minimap_mask_buttons(key.captured,analog);
        quest_driving_input(&shift,4,1,&digital,analog,sticks);
        CHECK(analog[1]==(h?0:255) && analog[2]==(h?255:0));
        sticks[3]=24000;
        quest_driving_input(&shift,5,1,&digital,analog,sticks);
        CHECK(analog[2]==255); /* right-stick upshift has its own input */
        grip.hover[h]=0;
        CHECK(quest_minimap_shortcut(&key,103,1,&grip,both,button));
        CHECK(!quest_minimap_shortcut(&key,104,1,&grip,both,button) && key.captured==button);
        CHECK(!quest_minimap_shortcut(&key,105,0,&grip,0,0) && key.captured==button);
        CHECK(!quest_minimap_shortcut(&key,106,0,&grip,both,button) && key.captured==button);
        CHECK(!quest_minimap_shortcut(&key,107,0,&grip,both,0) && !key.captured);
        grip.hover[h]=1;
        CHECK(!quest_minimap_shortcut(&key,108,0,&grip,both,button) && !key.captured); /* race or menu */
        CHECK(!quest_minimap_shortcut(&key,109,1,&grip,both,button));
        CHECK(!quest_minimap_shortcut(&key,110,1,&grip,both,0));
        grip.hand=h+1;grip.hover[h]=0;
        CHECK(!quest_minimap_shortcut(&key,111,1,&grip,both,button) && key.captured==button);
        grip.hand=0;grip.hover[h]=1;
        CHECK(!quest_minimap_shortcut(&key,112,1,&grip,0,0));
        CHECK(!quest_minimap_shortcut(&key,113,1,&grip,both,button)); /* tracking resumes held */
        CHECK(!quest_minimap_shortcut(&key,114,1,&grip,both,0));
        grip.hover[1-h]=1;
        CHECK(quest_minimap_shortcut(&key,115,1,&grip,both,both) && key.captured==both);
        CHECK(!quest_minimap_shortcut(&key,116,0,&grip,both,both) && key.captured==both);
    }
    puts("Minimap shortcut: hand-specific buttons, driving bindings, grip priority and menu transitions passed.");
    return 0;
}
static int minimap_grip_test(void)
{
    for(int car=0;car<QUEST_COCKPIT_VEHICLES;++car)for(int h=0;h<2;++h) {
        const QuestCockpitVehicle *fit=quest_cockpit_vehicle(car);
        QuestMinimapMount mount=quest_minimap_mount(fit);
        CHECK(fabsf(hypotf(mount.up_y,mount.up_z)-1.f)<1.e-6f);
        QuestVRSettings settings=quest_vr_defaults();
        float *offset=settings.map.offset;offset[0]=.18f;
        float wrist[2][16]={{0}},squeeze[2]={0};
        float *p[2]={wrist[0]+12,wrist[1]+12},matrix[16],local[3];
        for(int i=0;i<2;++i)wrist[i][0]=wrist[i][5]=wrist[i][10]=wrist[i][15]=1;
        quest_minimap_matrix(matrix,&mount,&settings.map);
        int valid[2]={1,1},busy[2]={0};
        QuestMinimapGrip grip={0};
        for(int k=0;k<3;++k)p[h][k]=mount.center[k]+offset[k];
        p[h][0]+=QUEST_MINIMAP_RADIUS;
        p[h][1]+=mount.up_y*QUEST_MINIMAP_RADIUS;p[h][2]+=mount.up_z*QUEST_MINIMAP_RADIUS;
        CHECK(!quest_minimap_contact(matrix,p[h],local)); /* outside the circular rim */
        p[h][0]-=QUEST_MINIMAP_RADIUS;
        CHECK(quest_minimap_contact(matrix,p[h],local));
        p[h][2]+=.20f;
        CHECK(!quest_minimap_contact(matrix,p[h],local));
        quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        squeeze[h]=1;quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        p[h][2]-=.20f;
        quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        CHECK(!grip.hand && !grip.hover[h]); /* squeezing elsewhere then touching the map */
        squeeze[h]=0;quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        CHECK(grip.hover[h]);
        busy[h]=1;squeeze[h]=1;quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        CHECK(!grip.hand && !grip.hover[h]);
        busy[h]=0;squeeze[h]=0;quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        squeeze[h]=1;quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        CHECK(grip.hand==h+1 && !grip.hover[h] && !grip.hover[1-h] && offset[0]==.18f && offset[1]==0 && offset[2]==0);
        memcpy(p[1-h],p[h],3*sizeof(float));squeeze[1-h]=1;
        p[h][0]-=.123f;p[h][1]+=.147f;p[h][2]+=.241f;
        CHECK(!quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy) && grip.hand==h+1);
        CHECK(fabsf(offset[0]-.057f)<1e-6f && fabsf(offset[1]-.147f)<1e-6f && fabsf(offset[2]-.241f)<1e-6f);
        p[h][2]+=.013f;squeeze[h]=0;
        CHECK(quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy) && !grip.hand);
        CHECK(fabsf(offset[2]-.254f)<1e-6f);
        char text[512];QuestVRSettings loaded=quest_vr_defaults();
        quest_vr_format(&settings,text,sizeof text);quest_vr_parse(&loaded,text);
        for(int k=0;k<3;++k)CHECK(fabsf(loaded.map.offset[k]-offset[k])<.00051f);
        squeeze[h]=1;quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        CHECK(grip.hand==h+1); /* regrab at the new mount */
        p[h][1]+=3;quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        CHECK(offset[1]==QUEST_MINIMAP_LIMIT);
        valid[h]=0;p[h][1]-=1;
        CHECK(quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy) && !grip.hand);
        valid[h]=1;
        CHECK(!quest_minimap_update(&grip,&mount,&settings.map,0,valid,wrist,squeeze,busy) && offset[1]==QUEST_MINIMAP_LIMIT);
        CHECK(!quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy) && !grip.hand);
    }
    puts("Minimap: fitted contact, both hands, exclusive grab, continuous placement, release/save and tracking loss passed.");
    return 0;
}
static int minimap_tilt_test(void)
{
    QuestMinimapMount mount=quest_minimap_mount(quest_cockpit_vehicle(0));
    for(int h=0;h<2;++h) {
        QuestMinimapGrip grip={0};
        QuestVRSettings settings=quest_vr_defaults(),loaded=quest_vr_defaults();
        float wrist[2][16]={{0}},squeeze[2]={0},m[16],local[3];
        int valid[2]={1,1},busy[2]={0};
        float *hand=wrist[h];hand[0]=hand[5]=hand[10]=hand[15]=1;
        memcpy(hand+12,mount.center,sizeof mount.center);hand[12]+=.07f;
        quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        squeeze[h]=1;quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        CHECK(grip.hand==h+1);
        const float yaw=.45f,pitch=-.32f;
        hand[0]=cosf(yaw);hand[2]=-sinf(yaw);
        hand[4]=sinf(yaw)*sinf(pitch);hand[5]=cosf(pitch);hand[6]=cosf(yaw)*sinf(pitch);
        hand[8]=sinf(yaw)*cosf(pitch);hand[9]=-sinf(pitch);hand[10]=cosf(yaw)*cosf(pitch);
        hand[12]+=.12f;hand[13]-=.08f;
        quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy);
        CHECK(fabsf(settings.map.pitch-pitch)<1e-5f && fabsf(settings.map.yaw-yaw)<1e-5f);
        quest_minimap_matrix(m,&mount,&settings.map);
        CHECK(quest_minimap_contact(m,hand+12,local));
        CHECK(fabsf(local[0]-.07f)<1e-5f && fabsf(local[1])+fabsf(local[2])<1e-5f);
        valid[h]=0;
        CHECK(quest_minimap_update(&grip,&mount,&settings.map,1,valid,wrist,squeeze,busy));
        char text[512];quest_vr_format(&settings,text,sizeof text);quest_vr_parse(&loaded,text);
        CHECK(fabsf(loaded.map.pitch-pitch)<1e-5f && fabsf(loaded.map.yaw-yaw)<1e-5f);
        quest_vr_parse(&loaded,"map_pitch=nan\nmap_yaw=99\n");
        CHECK(loaded.map.pitch==0 && loaded.map.yaw==QUEST_MINIMAP_ANGLE_LIMIT);
    }
    puts("Minimap tilt: both wrists, off-center contact, save/load and limits passed.");
    return 0;
}
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
    CHECK(!haptics_menu_test());
    CHECK(!led_menu_test());
    CHECK(!minimap_grip_test());
    CHECK(!minimap_tilt_test());
    CHECK(!minimap_shortcut_test());
    CHECK(!seat_mirror_test());
    QuestVRSettings s=quest_vr_defaults(),round=quest_vr_defaults();char text[512];
    CHECK(s.sensitivity==1 && s.rotation==360 && s.resolution==0);
    quest_vr_parse(&s,"steering_sensitivity=nan\nwheel_rotation=inf\nseat_height=99\nseat_distance=-99\nresolution=999999999999\nunknown=7\n");
    CHECK(s.sensitivity==1 && s.rotation==360 && s.height==.35f && s.distance==-.4f && s.resolution==0);
    CHECK(s.return_speed==QUEST_WHEEL_RETURN_DEFAULT);
    CHECK(s.music_source==QUEST_MUSIC_AUTO);
    QuestVRMenu music_menu={.open=1,.row=QUEST_VR_MUSIC_ROW};
    CHECK(quest_vr_menu_step(&music_menu,&s,0,1,QUEST_VR_RIGHT)==5 && s.music_source==QUEST_MUSIC_CUSTOM_ONLY);
    quest_vr_parse(&s,"music_source=nan\nmusic_source=3\nmusic_source=-1\nmusic_source=1.5\n");
    CHECK(s.music_source==QUEST_MUSIC_CUSTOM_ONLY);
    CHECK(quest_vr_format(&s,text,sizeof text)<(int)sizeof text);
    quest_vr_parse(&round,text);CHECK(round.music_source==QUEST_MUSIC_CUSTOM_ONLY);
    quest_vr_adjust(&s,QUEST_VR_MUSIC_ROW,1);CHECK(s.music_source==QUEST_MUSIC_EA_ONLY);
    quest_vr_adjust(&s,QUEST_VR_MUSIC_ROW,1);CHECK(s.music_source==QUEST_MUSIC_AUTO);
    CHECK(s.mirror_resolution==2 && s.shift_guide==QUEST_GUIDE_ALWAYS && s.map.offset[2]==0);
    quest_vr_parse(&s,"mirror_resolution=nan\nshift_guide=3\nshift_guide=1.5\nmap_x=inf\nmap_y=42\n");
    CHECK(s.mirror_resolution==2 && s.shift_guide==QUEST_GUIDE_ALWAYS && s.map.offset[0]==0 && s.map.offset[1]==2);
    s=(QuestVRSettings){2,720,-.13f,.22f,4,330,4,0,{{.24f,-.32f,.26f},.15f,-.32f},1,0,6};quest_vr_format(&s,text,sizeof text);quest_vr_parse(&round,text);
    CHECK(!memcmp(&s,&round,sizeof s));
    quest_vr_parse(&s,"wheel_return_speed=-3\n");CHECK(s.return_speed==0);
    quest_vr_parse(&s,"wheel_return_speed=9999\n");CHECK(s.return_speed==QUEST_WHEEL_RETURN_MAX);
    quest_vr_parse(&s,"wheel_return_speed=nan\n");CHECK(s.return_speed==QUEST_WHEEL_RETURN_DEFAULT);
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
    menu.row=QUEST_VR_RETURN_ROW;quest_vr_menu_step(&menu,&s,360,1,0);
    CHECK(quest_vr_menu_step(&menu,&s,370,1,QUEST_VR_LEFT)==5 && s.return_speed==QUEST_WHEEL_RETURN_DEFAULT-30);
    CHECK(quest_vr_menu_step(&menu,&s,400,1,QUEST_VR_BACK)==6 && !menu.open);
    CHECK(quest_vr_menu_step(&menu,&s,500,1,QUEST_VR_BACK)==4);
    CHECK(quest_vr_menu_step(&menu,&s,600,1,0)==4);
    CHECK(!quest_vr_menu_step(&menu,&s,601,1,0));
    CHECK(quest_vr_menu_step(&menu,&s,602,1,QUEST_VR_OPEN)==4 && menu.open);
    CHECK(quest_vr_menu_step(&menu,&s,603,0,QUEST_VR_OPEN)==2 && !menu.open);
    CHECK(!quest_vr_menu_step(&menu,&s,604,1,QUEST_VR_OPEN) && !menu.open);
    for(int preset=0;preset<QUEST_MIRROR_PRESETS;++preset) {
        quest_vr_adjust(&s,QUEST_VR_MIRROR_ROW,1);
        CHECK(s.mirror_resolution==(3+preset)%QUEST_MIRROR_PRESETS);
    }
    menu=(QuestVRMenu){.open=1,.row=QUEST_VR_GUIDE_ROW};
    CHECK(quest_vr_menu_step(&menu,&s,604,1,QUEST_VR_RIGHT)==5 && s.shift_guide==QUEST_GUIDE_AT_LEVER);
    quest_vr_format(&s,text,sizeof text);quest_vr_parse(&round,text);
    CHECK(round.shift_guide==QUEST_GUIDE_AT_LEVER);
    quest_vr_menu_step(&menu,&s,605,1,0);
    CHECK(quest_vr_menu_step(&menu,&s,606,1,QUEST_VR_RIGHT)==5 && s.shift_guide==QUEST_GUIDE_OFF);
    quest_vr_adjust(&s,QUEST_VR_GUIDE_ROW,-1);CHECK(s.shift_guide==QUEST_GUIDE_AT_LEVER);
    quest_vr_adjust(&s,QUEST_VR_GUIDE_ROW,-1);CHECK(s.shift_guide==QUEST_GUIDE_ALWAYS);
    quest_vr_adjust(&s,QUEST_VR_GUIDE_ROW,-1);CHECK(s.shift_guide==QUEST_GUIDE_OFF);
    quest_vr_parse(&s,"map_x=0.12\nmap_y=0.143\nmap_z=-0.217\nmap_pitch=0.3\nmap_yaw=-0.2\n");
    QuestMinimapPlacement saved_map=s.map;
    menu=(QuestVRMenu){.open=1,.row=QUEST_VR_LED_PULSE_ROW};
    CHECK(quest_vr_menu_step(&menu,&s,607,1,QUEST_VR_DOWN)==4 && menu.row==QUEST_VR_MUSIC_ROW);
    CHECK(quest_vr_menu_step(&menu,&s,608,1,QUEST_VR_RIGHT)==5 && s.music_source==QUEST_MUSIC_CUSTOM_ONLY);
    CHECK(quest_vr_menu_step(&menu,&s,609,1,QUEST_VR_UP)==4 && menu.row==QUEST_VR_LED_PULSE_ROW);
    CHECK(!memcmp(&s.map,&saved_map,sizeof saved_map));
    QuestReplayButton replay={0};uint8_t b=255;
    CHECK(!quest_replay_button(&replay,1,0,&b) && !b);
    CHECK(!quest_replay_button(&replay,1,10,&b));
    b=255;CHECK(!quest_replay_button(&replay,1,20,&b) && !b);
    CHECK(quest_replay_button(&replay,1,210,&b));
    CHECK(!quest_replay_button(&replay,1,220,&b));
    b=255;CHECK(!quest_replay_button(&replay,1,300,&b) && !b);
    b=255;CHECK(!quest_replay_button(&replay,1,801,&b) && b==255);
    b=0;CHECK(!quest_replay_button(&replay,1,850,&b));
    b=255;CHECK(!quest_replay_button(&replay,0,900,&b) && b==255);
    QuestReplayClick click={0};
    CHECK(!quest_replay_click(&click,1,0,1) && !quest_replay_click(&click,1,0,0));
    CHECK(!quest_replay_click(&click,1,0,1) && !quest_replay_click(&click,1,0,1));
    CHECK(quest_replay_click(&click,1,0,0) && !quest_replay_click(&click,1,0,0));
    CHECK(!quest_replay_click(&click,1,0,1) && !quest_replay_click(&click,1,1,1));
    CHECK(!quest_replay_click(&click,1,0,1) && !quest_replay_click(&click,1,0,0));
    CHECK(!quest_replay_click(&click,1,0,1) && !quest_replay_click(&click,2,0,0));
    CHECK(!quest_replay_click(&click,2,0,1) && quest_replay_click(&click,2,0,0));
    CHECK(!quest_replay_click(&click,2,0,1) && !quest_replay_click(&click,0,0,0));
    CHECK(!quest_replay_click(&click,2,0,1) && !quest_replay_click(&click,2,0,0));
    /* Separate physical limits and game sensitivity. The displayed wheel
     * and contact matrix must continue beyond 180 degrees at larger ranges. */
    const float pivot[3]={0},axis[3]={0,0,1};
    for(int degrees=180;degrees<=1080;degrees+=90)for(int gain=1;gain<=6;++gain) {
        QuestWheelGrip wheel;quest_wheel_configure(&wheel,pivot,axis,.185f);
        wheel.limit=degrees*QUEST_WHEEL_PI/360;wheel.sensitivity=gain*.5f;
        wheel.latched=1;wheel.grabbed[0]=1;wheel.angle=wheel.limit*.25f;
        float expected=fminf(1,.25f*wheel.sensitivity);
        CHECK(fabsf(retail_steering(quest_wheel_input(&wheel,1,0))-expected)<.00003f);
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
    /* Reset this car's mirrors is a distinct edge-triggered menu action. */
    s=quest_vr_defaults();s.sensitivity=1.5f;s.height=.04f;
    menu=(QuestVRMenu){0};menu.open=1;menu.row=QUEST_VR_RESET_MIRRORS_ROW;
    CHECK(quest_vr_menu_step(&menu,&s,700,1,QUEST_VR_ACCEPT)==(4|QUEST_VR_RESET_MIRRORS));
    CHECK(menu.open && menu.mirrors_centered && s.sensitivity==1.5f && s.height==.04f);
    CHECK(quest_vr_menu_step(&menu,&s,730,1,QUEST_VR_ACCEPT)==4);
    quest_vr_menu_step(&menu,&s,750,1,0);menu.row=QUEST_VR_SAVE_ROW;
    CHECK(quest_vr_menu_step(&menu,&s,780,1,QUEST_VR_ACCEPT)==6 && !menu.open);
    for(int page=1;page<argc && page<=2;++page) {
        uint32_t *pixels=malloc(1024*768*4);CHECK(pixels);
        s=quest_vr_defaults();s.resolution=3;menu.open=1;menu.row=page==1?QUEST_VR_RETURN_ROW:QUEST_VR_GRIP_ROW;
        quest_vr_panel(pixels,&s,&menu,1,1);
        FILE *f=fopen(argv[page],"wb");CHECK(f);fprintf(f,"P6\n1024 768\n255\n");
        for(int i=0;i<1024*768;++i){unsigned char rgb[3]={(pixels[i]>>16)&255,(pixels[i]>>8)&255,pixels[i]&255};fwrite(rgb,1,3,f);}
        fclose(f);free(pixels);
    }
    puts("VR settings: parse/bounds/roundtrip, menu consumption, wheel ratios/contact and career-action pulses passed.");return 0;
}
