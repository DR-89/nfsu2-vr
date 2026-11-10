#include "../android/runtime/quest_vr_panel.h"
#include "../android/runtime/quest_cockpit_controls.h"
#include "../android/runtime/quest_telemetry.h"
#include "../android/runtime/quest_wheel_grip.h"
#include "quest_steering_reference.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)

static int shifting(void)
{
    for(int vehicle=0;vehicle<QUEST_COCKPIT_VEHICLES;++vehicle)for(int h=0;h<2;++h)
    for(int mode=0;mode<3;++mode)for(int reverse=0;reverse<2;++reverse)for(int axis=0;axis<2;++axis) {
        QuestShiftConfig config=quest_shift_defaults();config.mode=mode==0;
        config.gestures=mode;config.reverse=config.gesture_reverse=reverse;config.gesture_axis=axis;
        QuestShiftMotion motion={0};QuestShifter lever={0};
        int valid[2]={0},busy[2]={0};float pos[2][3]={{0}},pose[2][16]={{0}},squeeze[2]={0};
        valid[h]=1;pose[h][0]=pose[h][5]=pose[h][10]=pose[h][15]=1;
        quest_shifter_knob(&lever,vehicle,pos[h]);squeeze[h]=mode?0:1;
        int coordinate=mode && axis?1:2;float center=pos[h][coordinate];uint32_t now=1;
        for(;now<160;now+=10)CHECK(quest_shift_motion(&motion,&lever,&config,now,vehicle,6,3,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE);
        CHECK(motion.armed && motion.zone[h]);
        pos[h][coordinate]=center+(reverse?-.06f:.06f);
        CHECK(quest_shift_motion(&motion,&lever,&config,now,vehicle,6,3,valid,pos,pose,squeeze,busy)==4);now+=10;
        for(int i=0;i<10;++i,now+=10)CHECK(quest_shift_motion(&motion,&lever,&config,now,vehicle,6,4,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE);
        pos[h][coordinate]=center;
        CHECK(quest_shift_motion(&motion,&lever,&config,now,vehicle,6,4,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE);now+=10;
        pos[h][coordinate]=center+(reverse?.06f:-.06f);
        int result=quest_shift_motion(&motion,&lever,&config,now,vehicle,6,4,valid,pos,pose,squeeze,busy);now+=10;
        CHECK(result==(mode==2?QUEST_SHIFT_NONE:3));
        if(mode==2) {
            CHECK(motion.pending);
            pos[h][coordinate]=center;
            CHECK(quest_shift_motion(&motion,&lever,&config,now,vehicle,6,4,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE);now+=10;
            pos[h][coordinate]=center+(reverse?.06f:-.06f);
            CHECK(quest_shift_motion(&motion,&lever,&config,now,vehicle,6,4,valid,pos,pose,squeeze,busy)==3);now+=10;
        }
        valid[h]=0;CHECK(quest_shift_motion(&motion,&lever,&config,now,vehicle,6,3,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE);now+=10;
        CHECK(!motion.hand && !motion.zone[h] && !motion.pending);
        valid[h]=1;pos[h][coordinate]=center;
        CHECK(quest_shift_motion(&motion,&lever,&config,now,vehicle,6,3,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE);now+=10;
        busy[h]=1;CHECK(quest_shift_motion(&motion,&lever,&config,now,vehicle,6,3,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE && !motion.zone[h]);
    }
    CHECK(quest_shift_step(7,6,1,0)==QUEST_SHIFT_NONE);
    CHECK(quest_shift_step(0,6,-1,0)==QUEST_SHIFT_NONE);
    CHECK(quest_shift_step(1,6,1,1)==2);
    CHECK(quest_shift_step(2,6,-1,1)==QUEST_SHIFT_NONE);
    /* Wrist-only flicks, slow drift, entry motion, lost frames and double-flick expiry. */
    QuestShiftConfig config=quest_shift_defaults();config.gestures=2;config.gesture_reverse=0;
    QuestShiftMotion m={0};QuestShifter lever={0};int valid[2]={1,0},busy[2]={0};
    float pos[2][3]={{0}},pose[2][16]={{0}},squeeze[2]={0};
    pose[0][0]=pose[0][5]=pose[0][10]=pose[0][15]=1;quest_shifter_knob(&lever,0,pos[0]);
    for(int t=1;t<161;t+=10)CHECK(quest_shift_motion(&m,&lever,&config,t,0,6,3,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE);
    pose[0][9]=-.5f;pose[0][10]=.8660254f;
    CHECK(quest_shift_motion(&m,&lever,&config,161,0,6,3,valid,pos,pose,squeeze,busy)==4);
    CHECK(quest_shift_motion(&m,&lever,&config,500,0,6,4,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE && !m.armed);
    m=(QuestShiftMotion){0};pose[0][9]=0;pose[0][10]=1;
    for(int t=1;t<161;t+=10)quest_shift_motion(&m,&lever,&config,t,0,6,4,valid,pos,pose,squeeze,busy);
    float start=pos[0][2];
    for(int t=161;t<361;t+=10){pos[0][2]+=.003f;CHECK(quest_shift_motion(&m,&lever,&config,t,0,6,4,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE);}
    pos[0][2]=start;quest_shift_motion(&m,&lever,&config,361,0,6,4,valid,pos,pose,squeeze,busy);
    pos[0][2]=start-.06f;CHECK(quest_shift_motion(&m,&lever,&config,371,0,6,4,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE && m.pending);
    pos[0][2]=start;
    for(int t=381;t<1000;t+=10)quest_shift_motion(&m,&lever,&config,t,0,6,4,valid,pos,pose,squeeze,busy);
    CHECK(!m.pending);pos[0][2]=start-.06f;
    CHECK(quest_shift_motion(&m,&lever,&config,1001,0,6,4,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE && m.pending);
    return 0;
}
static int sequential_pickup(void)
{
    for(int v=0;v<QUEST_COCKPIT_VEHICLES;++v)for(int h=0;h<2;++h)for(int dir=-1;dir<=1;dir+=2) {
        QuestShiftConfig c=quest_shift_defaults();c.mode=1;c.zone=.30f;c.travel=.10f;
        QuestShiftMotion m={0};QuestShifter lever={0};
        int valid[2]={0},busy[2]={0};float pos[2][3]={{0}},pose[2][16]={{0}},squeeze[2]={0};
        valid[h]=1;squeeze[h]=1;pose[h][0]=pose[h][5]=pose[h][10]=pose[h][15]=1;
        quest_shifter_knob(&lever,v,pos[h]);float center=pos[h][2];
        pos[h][2]=center+.12f;
        for(int t=1;t<201;t+=10)CHECK(quest_shift_motion(&m,&lever,&c,t,v,6,3,valid,pos,pose,squeeze,busy)==QUEST_SHIFT_NONE);
        CHECK(!m.hand && !lever.hand && !m.zone[h]); /* old wide zone cannot grab */
        CHECK(!quest_shift_touch_blocked(&lever,&c,v,pos[h]));
        pos[h][2]=center;pos[h][0]+=.06f; /* pickup near the edge */
        for(int t=201;t<361;t+=10)quest_shift_motion(&m,&lever,&c,t,v,6,3,valid,pos,pose,squeeze,busy);
        CHECK(m.armed && lever.hand==h+1);
        pos[h][2]=center+dir*.101f;
        CHECK(quest_shift_motion(&m,&lever,&c,361,v,6,3,valid,pos,pose,squeeze,busy)==3+dir);
        CHECK(lever.hand==h+1); /* long strokes retain the grip */
        pos[h][2]=center;quest_shift_motion(&m,&lever,&c,371,v,6,3+dir,valid,pos,pose,squeeze,busy);
        CHECK(m.armed);
        squeeze[h]=0;quest_shift_motion(&m,&lever,&c,381,v,6,3+dir,valid,pos,pose,squeeze,busy);
        CHECK(!lever.hand);
        pos[h][0]-=.06f;pos[h][2]=center+.12f;squeeze[h]=1;
        quest_shift_motion(&m,&lever,&c,391,v,6,3+dir,valid,pos,pose,squeeze,busy);
        CHECK(!lever.hand && !m.hand); /* release restores compact pickup */
        c.gestures=1;squeeze[h]=0;
        quest_shift_motion(&m,&lever,&c,401,v,6,3,valid,pos,pose,squeeze,busy);
        CHECK(m.zone[h] && m.gesture && !lever.hand); /* optional gesture zone survives */
        CHECK(!quest_shift_touch_blocked(&lever,&c,v,pos[h]));
        busy[h]=1;quest_shift_motion(&m,&lever,&c,411,v,6,3,valid,pos,pose,squeeze,busy);
        CHECK(!m.hand && !m.zone[h]); /* pointing at controls wins over gestures */
        busy[h]=0;c.mode=c.gestures=0;
        quest_shift_motion(&m,&lever,&c,421,v,6,3,valid,pos,pose,squeeze,busy);
        CHECK(m.zone[h] && !m.hand); /* H-pattern proximity rumble stays available */
    }
    return 0;
}
static int screens(void)
{
    for(int v=0;v<QUEST_COCKPIT_VEHICLES;++v)for(int h=0;h<2;++h) {
        const QuestCockpitVehicle *fit=quest_cockpit_vehicle(v);
        QuestScreenSettings p=quest_cockpit_defaults().screen;CHECK(p.movable);
        QuestScreenGrip g={0};QuestMinimapMount mount=quest_screen_mount(fit);
        float matrix[16],point[3],original[3];quest_minimap_matrix(matrix,&mount,&p.placement);
        for(int corner=0;corner<4;++corner) {
            float u=(float)(corner&1),w=(float)(corner>>1);
            quest_screen_point(point,matrix,quest_radio_screen_width(fit),u,w);quest_radio_screen_point(original,fit,u,w);
            for(int k=0;k<3;++k)CHECK(fabsf(point[k]-original[k])<1.e-5f);
        }
        float pose[2][16]={{0}},squeeze[2]={0};int valid[2]={1,1},busy[2]={0};
        for(int i=0;i<2;++i){pose[i][0]=pose[i][5]=pose[i][10]=pose[i][15]=1;pose[i][12]=10;}
        memcpy(pose[h]+12,mount.center,12);
        CHECK(!quest_screen_update(&g,&p,fit,1,1,valid,pose,squeeze,busy,0,0) && g.grip.hover[h]);
        squeeze[h]=1;CHECK(!quest_screen_update(&g,&p,fit,11,1,valid,pose,squeeze,busy,0,0) && g.grip.hand==h+1);
        pose[h][12]+=.12f;pose[h][14]+=.2f;
        quest_screen_update(&g,&p,fit,111,1,valid,pose,squeeze,busy,0,32767);
        CHECK(fabsf(p.placement.offset[0]-.12f)<1.e-5f && p.scale>1 && p.opacity==1);
        pose[h][0]=pose[h][10]=cosf(.4f);pose[h][2]=-sinf(.4f);pose[h][8]=sinf(.4f);
        quest_screen_update(&g,&p,fit,121,1,valid,pose,squeeze,busy,0,0);
        CHECK(fabsf(p.placement.yaw-.4f)<1.e-5f);
        valid[h]=0;CHECK(quest_screen_update(&g,&p,fit,131,1,valid,pose,squeeze,busy,0,0) && !g.grip.hand);
        CHECK(!quest_screen_update(&g,&p,fit,141,1,valid,pose,squeeze,busy,0,0));
        valid[h]=1;p.movable=0;squeeze[h]=0;
        CHECK(!quest_screen_update(&g,&p,fit,151,1,valid,pose,squeeze,busy,0,0) && !g.grip.hover[h]);
        /* Horizontal input changes opacity only while held, including at
         * both limits. An opacity-only edit must request a save on release. */
        for(int direction=-1;direction<=1;direction+=2) {
            g=(QuestScreenGrip){0};p=quest_cockpit_defaults().screen;p.opacity=.5f;
            memset(pose[h],0,sizeof pose[h]);pose[h][0]=pose[h][5]=pose[h][10]=pose[h][15]=1;
            memcpy(pose[h]+12,mount.center,12);squeeze[h]=0;
            quest_screen_update(&g,&p,fit,200,1,valid,pose,squeeze,busy,direction*32767,0);
            CHECK(p.opacity==.5f && p.scale==1);
            squeeze[h]=1;quest_screen_update(&g,&p,fit,210,1,valid,pose,squeeze,busy,0,0);
            CHECK(g.grip.hand==h+1);
            quest_screen_update(&g,&p,fit,220,1,valid,pose,squeeze,busy,6000,-6000);
            CHECK(p.opacity==.5f && p.scale==1); /* stick deadzone */
            for(uint32_t tick=320;tick<=3320;tick+=100)
                quest_screen_update(&g,&p,fit,tick,1,valid,pose,squeeze,busy,direction*32767,0);
            CHECK(p.opacity==(direction<0?.05f:1.f) && p.scale==1);
            CHECK(!memcmp(&p.placement,&g.grip.start,sizeof p.placement));
            squeeze[h]=0;
            CHECK(quest_screen_update(&g,&p,fit,3330,1,valid,pose,squeeze,busy,0,0));
            CHECK(!g.grip.hand && !quest_screen_update(&g,&p,fit,3340,1,valid,pose,squeeze,busy,0,0));
        }
    }
    return 0;
}
static int settings_and_steering(void)
{
    QuestVRSettings s=quest_vr_defaults(),loaded=quest_vr_defaults();char text[4096];
    CHECK(!s.cockpit.shift.mode && !s.cockpit.shift.gestures && s.cockpit.screen.movable);
    quest_vr_parse(&loaded,"screen_movable=0\n");CHECK(!loaded.cockpit.screen.movable);
    for(int i=0;i<QUEST_COCKPIT_OPTION_COUNT;++i)quest_option_set(&s.cockpit,i,quest_cockpit_options[i].high);
    int n=quest_vr_format(&s,text,sizeof text);CHECK(n>0 && n<(int)sizeof text);quest_vr_parse(&loaded,text);
    for(int i=0;i<QUEST_COCKPIT_OPTION_COUNT;++i)CHECK(fabsf(quest_option_get(&s.cockpit,i)-quest_option_get(&loaded.cockpit,i))<1e-5f);
    quest_vr_parse(&loaded,"screen_opacity=nan\nscreen_scale=99\nshift_gestures=1.5\nsteering_deadzone=inf\n");
    CHECK(loaded.cockpit.screen.opacity==1 && loaded.cockpit.screen.scale==3 && !loaded.cockpit.shift.gestures && !loaded.cockpit.steering_deadzone);
    QuestVRMenu menu={.open=1,.row=QUEST_VR_COCKPIT_ROW};
    CHECK(quest_vr_menu_step(&menu,&s,1,1,QUEST_VR_ACCEPT)==4 && menu.row==QUEST_VR_COCKPIT_FIRST);
    quest_vr_menu_step(&menu,&s,2,1,0);CHECK(quest_vr_menu_step(&menu,&s,3,1,QUEST_VR_RIGHT)==5 && !s.cockpit.shift.mode);
    quest_vr_menu_step(&menu,&s,4,1,0);CHECK(quest_vr_menu_step(&menu,&s,5,1,QUEST_VR_UP)==4 && menu.row==QUEST_VR_COCKPIT_BACK);
    CHECK(quest_vr_menu_step(&menu,&s,6,1,QUEST_VR_ACCEPT)==4 && menu.row==QUEST_VR_COCKPIT_ROW);
    float pivot[3]={0},axis[3]={0,0,1};QuestWheelGrip w;quest_wheel_configure(&w,pivot,axis,.18f);w.latched=1;w.grabbed[0]=1;w.deadzone=.1f;
    w.angle=w.limit*.05f;CHECK(quest_wheel_input(&w,1,0)==0);
    w.angle=w.limit*.55f;CHECK(fabsf(retail_steering(quest_wheel_input(&w,1,0))-.5f)<.00005f);
    w.angle=-w.limit;CHECK(fabsf(retail_steering(quest_wheel_input(&w,1,0))+1)<.00005f);
    return 0;
}
static int controls(void)
{
    QuestRadioFont font;quest_radio_font_init(&font);
    for(int v=0;v<QUEST_COCKPIT_VEHICLES;++v)for(int h=0;h<2;++h)for(int button=0;button<2;++button) {
        const QuestCockpitVehicle *fit=quest_cockpit_vehicle(v);float p[3],pose[16]={0},tip[3]={0};QuestCockpitTouch touch={0};
        quest_radio_point(p,fit,.275f+.45f*button,2.2f);pose[0]=pose[5]=pose[10]=pose[15]=1;memcpy(pose+12,p,12);
        pose[14]+=.06f;CHECK(!quest_touch_update(&touch,fit,1,0,pose,tip,0).action);
        memcpy(pose+12,p,12);CHECK(quest_touch_update(&touch,fit,1,0,pose,tip,0).action==QUEST_COCKPIT_LED+button);
        CHECK(!quest_touch_update(&touch,fit,1,0,pose,tip,0).action);
        QuestGhostVertex vertices[QUEST_CONTROL_VERTICES];
        for(int mode=0;mode<2;++mode)for(int rev=0;rev<2;++rev) {
            unsigned count=quest_cockpit_controls_mode(vertices,v,&font,0,6,0,.03f,mode,rev);
            CHECK(count<=QUEST_CONTROL_VERTICES && vertices[count-1].normal[0]==QUEST_GATE_CURSOR);
        }
    }
    return 0;
}
int main(int argc,char **argv)
{
    CHECK(!shifting());CHECK(!sequential_pickup());CHECK(!screens());CHECK(!settings_and_steering());CHECK(!controls());
    QuestTelemetry t={0};
    quest_telemetry_sample(&t,1000,1,3000,6000,10,.25f);
    quest_telemetry_sample(&t,1050,1,3500,6000,10.2f,.5f);CHECK(fabsf(t.accel-4)<.001f && t.count==2);
    quest_telemetry_sample(&t,2000,1,3500,6000,40,0);CHECK(t.count==1 && t.accel==0);
    quest_telemetry_sample(&t,2050,2,3500,6000,50,0);CHECK(t.count==1 && t.accel==0);
    quest_telemetry_sample(&t,2100,2,NAN,6000,50,0);CHECK(!t.count && !t.car);
    for(int i=0;i<160;++i)quest_telemetry_sample(&t,3000+i*50,1,3500+2500*sinf(i*.06f),7000,20+5*sinf(i*.04f),fmaxf(0,sinf(i*.075f)));
    CHECK(t.count==QUEST_TELEMETRY_SAMPLES);
    if(argc>1) {
        uint8_t pixels[320*180*4];quest_telemetry_render(&t,t.tick,pixels);FILE *f=fopen(argv[1],"wb");CHECK(f);fprintf(f,"P6\n320 180\n255\n");
        for(int i=0;i<320*180;++i)fwrite(pixels+4*i,1,3,f);fclose(f);
    }
    if(argc>2) {
        uint32_t *pixels=malloc(1024*768*4);CHECK(pixels);QuestVRSettings s=quest_vr_defaults();QuestVRMenu menu={.open=1,.row=QUEST_VR_COCKPIT_FIRST};
        for(int page=0;page<4;++page) {
            menu.row=page==3?QUEST_VR_COCKPIT_ROW:page==0?QUEST_VR_COCKPIT_FIRST:page==1?QUEST_VR_COCKPIT_FIRST+13:QUEST_VR_COCKPIT_BACK;
            quest_vr_panel(pixels,&s,&menu,1,0);char path[1024];snprintf(path,sizeof path,"%s-%d.ppm",argv[2],page);
            FILE *f=fopen(path,"wb");CHECK(f);fprintf(f,"P6\n1024 768\n255\n");
            for(int i=0;i<1024*768;++i){uint8_t rgb[]={(uint8_t)(pixels[i]>>16),(uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};fwrite(rgb,1,3,f);}fclose(f);
        }
        free(pixels);
    }
    puts("Cockpit interactions: all vehicles, both hands, sequential/gesture directions, double flicks, tracking, screen transforms, buttons, settings, steering and telemetry passed.");
    return 0;
}
