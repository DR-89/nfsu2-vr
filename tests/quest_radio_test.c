#include "../android/runtime/quest_cockpit_controls.h"
#include <stdio.h>

#define CHECK(x) do { if(!(x)) {fprintf(stderr,"radio test failed at %d: %s\n",__LINE__,#x);return 1;} } while(0)
static const float tip[3]={-.034f,.14f,.005f};
static void point_at(float pose[16],const float p[3],int hand)
{
    memset(pose,0,64);
    pose[0]=hand?1.f:-1.f;pose[5]=pose[10]=pose[15]=1;
    for(int k=0;k<3;++k)pose[12+k]=p[k]-pose[4*k+k]*tip[k];
}
int main(void)
{
    const float u[]={.2f,.55f,.73f,.91f};
    QuestRadioFont digits;quest_radio_font_init(&digits);
    for(int vehicle=0;vehicle<QUEST_COCKPIT_VEHICLES;++vehicle)for(int hand=0;hand<2;++hand) {
        const QuestCockpitVehicle *fit=quest_cockpit_vehicle(vehicle);
        QuestCockpitTouch touch={0};float pose[16]={0};
        for(int id=QUEST_RADIO_VOLUME;id<=QUEST_SEAT_DOWN;++id) {
            int radio=id<=QUEST_RADIO_NEXT;
            float p[3],normal[3]={0,1,0};
            if(radio) {
                float a[3],b[3];quest_radio_point(a,fit,0,0);quest_radio_point(b,fit,1,1);
                float len=hypotf(b[1]-a[1],b[2]-a[2]);
                normal[1]=(b[2]-a[2])/len;normal[2]=(a[1]-b[1])/len;
                quest_radio_point(p,fit,u[id-1],.5f);
            } else quest_seat_point(p,fit,.5f,(id-QUEST_SEAT_FORWARD+.5f)/4);
            point_at(pose,p,hand);
            for(int k=0;k<3;++k)pose[12+k]+=normal[k]*.045f;
            CHECK(!quest_touch_update(&touch,fit,1,radio,pose,tip,0).action);
            CHECK(touch.pointing==id);
            point_at(pose,p,hand);
            CHECK(quest_touch_update(&touch,fit,1,radio,pose,tip,0).action==id);
            CHECK(touch.control==id && touch.pointing==id);
            if(id==QUEST_RADIO_VOLUME) {
                quest_radio_point(p,fit,.44f,.5f);point_at(pose,p,hand);
                CHECK(quest_touch_update(&touch,fit,1,1,pose,tip,0).volume==1);
                quest_radio_point(p,fit,.06f,.5f);point_at(pose,p,hand);
                CHECK(quest_touch_update(&touch,fit,1,1,pose,tip,0).volume==0);
            } else CHECK(!quest_touch_update(&touch,fit,1,radio,pose,tip,0).action);
            for(int k=0;k<3;++k)pose[12+k]-=.055f*normal[k];
            CHECK(!quest_touch_update(&touch,fit,1,radio,pose,tip,0).action);
            point_at(pose,p,hand);
            CHECK(!quest_touch_update(&touch,fit,1,radio,pose,tip,0).action);
            CHECK(!quest_touch_update(&touch,fit,0,radio,pose,tip,0).action && !touch.pointing);
            CHECK(!quest_touch_update(&touch,fit,1,radio,pose,tip,0).action);
        }
        float p[3];quest_radio_point(p,fit,.55f,1.15f);
        CHECK(!quest_radio_hit(quest_radio_position(p,fit)).control);
        quest_radio_point(p,fit,.55f,.5f);point_at(pose,p,hand);
        CHECK(!quest_touch_update(&touch,fit,1,0,pose,tip,0).action && !touch.pointing);

        /* A resting finger on the door cannot block a quick poke. Trace the
         * crossing between two samples, then withdraw without firing twice. */
        QuestCockpitTouch door={0};
        quest_seat_point(p,fit,.5f,.1f);point_at(pose,p,1-hand);
        CHECK(quest_touch_update(&door,fit,1,1,pose,tip,0).action==QUEST_SEAT_FORWARD);
        quest_radio_point(p,fit,.73f,.5f);
        float norm=sqrtf(1.f+.16f*fit->front_depth*fit->front_depth);
        float ny=.4f*fit->front_depth/norm,nz=1.f/norm;
        point_at(pose,p,hand);pose[13]+=.082f*ny;pose[14]+=.082f*nz;
        CHECK(!quest_touch_update(&touch,fit,1,1,pose,tip,door.control).action);
        pose[13]-=.15f*ny;pose[14]-=.15f*nz;
        CHECK(quest_touch_update(&touch,fit,1,1,pose,tip,door.control).action==QUEST_RADIO_PLAY);
        CHECK(!quest_touch_update(&touch,fit,1,1,pose,tip,door.control).action);
        point_at(pose,p,hand);
        CHECK(!quest_touch_update(&touch,fit,1,1,pose,tip,door.control).action);
        /* Tracking loss cancels history, including a pending crossing. */
        pose[13]+=.08f*ny;pose[14]+=.08f*nz;
        CHECK(!quest_touch_update(&touch,fit,1,1,pose,tip,0).action);
        CHECK(!quest_touch_update(&touch,fit,0,1,pose,tip,0).action);
        point_at(pose,p,hand);
        CHECK(!quest_touch_update(&touch,fit,1,1,pose,tip,0).action);
        touch=(QuestCockpitTouch){0};
        CHECK(!quest_touch_update(&touch,fit,1,1,pose,tip,QUEST_RADIO_PLAY).action);
        for(int gears=4;gears<=6;++gears) {
            QuestGhostVertex overlay[QUEST_CONTROL_VERTICES];
            float x=.013f,z=-.008f,center[3];QuestShifter lever={0};
            quest_shifter_knob(&lever,vehicle,center);
            unsigned count=quest_cockpit_controls(overlay,vehicle,&digits,0,gears,x,z);
            CHECK(count<=QUEST_CONTROL_VERTICES && overlay[count-1].normal[0]==QUEST_GATE_CURSOR);
            const float *lo=overlay[count-6].position,*hi=overlay[count-4].position;
            CHECK(fabsf((lo[0]+hi[0])*.5f-center[0]-x)<1.e-5f);
            CHECK(fabsf((lo[2]+hi[2])*.5f-center[2]-z)<1.e-5f);
            unsigned off=quest_cockpit_controls(overlay,vehicle,&digits,0,0,x,z);
            CHECK(off<count && overlay[off-1].normal[0]==QUEST_KEY_TEXT);
        }
    }
    const uint32_t playlist=(1u<<3)|(1u<<7)|(1u<<26);
    CHECK(quest_radio_track(3,playlist,1)==7 && quest_radio_track(7,playlist,-1)==3);
    CHECK(quest_radio_track(26,playlist,1)==3 && quest_radio_track(3,playlist,-1)==26);
    CHECK(quest_radio_track(-1,playlist,-1)==26 && quest_radio_track(42,playlist,1)==3);
    CHECK(quest_radio_track(1,0,1)==-1 && quest_radio_track(7,1u<<7,1)==7);
    /* A stopped live session retained sample node 26 and playlist cursor 18.
     * Only an active music voice can identify a playing song. */
    CHECK(quest_radio_playing(26,UINT32_MAX,UINT16_MAX)==-1);
    CHECK(quest_radio_playing(26,26,UINT16_MAX)==-1);
    CHECK(quest_radio_playing(26,UINT32_MAX,26)==-1);
    CHECK(quest_radio_playing(UINT32_MAX,26,26)==-1);
    CHECK(quest_radio_track(quest_radio_playing(26,26,26),0x7ffffc0u,-1)==7);
    CHECK(quest_radio_mix_gain(0)>0 && lrintf(32767.f*quest_radio_mix_gain(0))==0);
    CHECK(quest_radio_mix_gain(.8f)==.8f && quest_radio_volume_changed(.00005f,0) && quest_radio_volume_changed(0,.00005f));
    QuestRadioFont font;quest_radio_font_init(&font);
    QuestRadioLCD lcd;QuestRadioSong song={"12345678901234567890 & (Remix)","Artist"};
    quest_radio_lcd(&lcd,&font,&song,0);
    CHECK(lcd.cells[0][0]==font.bits['1'][0] && lcd.cells[0][2]==font.bits['A'][0] && lcd.scroll[0]==0);
    quest_radio_lcd(&lcd,&font,&song,2325);
    CHECK(lcd.cells[0][0]==font.bits['2'][0] && lcd.scroll[0]==.5f && lcd.scroll[1]==0);
    quest_radio_lcd(&lcd,&font,&song,5500);
    CHECK(lcd.cells[QUEST_RADIO_COLUMNS-1][0]==font.bits[')'][0] && lcd.cells[QUEST_RADIO_COLUMNS][0]==0);
    puts("Cockpit fingertip presses, H-pattern cursor and playlist tests passed for all vehicles and both hands.");
    return 0;
}
