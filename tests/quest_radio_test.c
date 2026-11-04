#include "../android/runtime/quest_radio_display.h"
#include <stdio.h>

#define CHECK(x) do { if(!(x)) {fprintf(stderr,"radio test failed at %d: %s\n",__LINE__,#x);return 1;} } while(0)
static void fingertip(float pose[16],const QuestCockpitVehicle *fit,float u,float v)
{
    memset(pose,0,64);pose[0]=pose[5]=pose[10]=pose[15]=1;
    quest_radio_point(pose+12,fit,u,v);pose[13]-=.045f;
}
int main(void)
{
    const float u[]={.2f,.55f,.73f,.91f};
    for(int vehicle=0;vehicle<QUEST_COCKPIT_VEHICLES;++vehicle)for(int hand=0;hand<2;++hand) {
        const QuestCockpitVehicle *fit=quest_cockpit_vehicle(vehicle);
        QuestRadioGrip grip={0};int valid[2]={1,1},busy[2]={0};float pose[2][16]={{0}},squeeze[2]={0};
        float left[3],right[3];quest_radio_point(left,fit,0,0);quest_radio_point(right,fit,1,0);
        CHECK(left[0]<right[0]);
        float label[3],hit_u;quest_radio_point(label,fit,.55f,1.15f);
        CHECK(!quest_radio_hit(label,fit,&hit_u));
        for(int i=0;i<4;++i) {
            fingertip(pose[hand],fit,u[i],.5f);squeeze[hand]=0;
            CHECK(!quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action);
            CHECK(grip.hover_mask==(1u<<(i+1)) && !grip.hand);
            squeeze[hand]=1;
            CHECK(quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action==i+1);
            CHECK(grip.hand==hand+1 && !grip.hover_mask);
            if(i)CHECK(!quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action);
            else {
                fingertip(pose[hand],fit,.05f,.5f);
                CHECK(quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).volume==0);
                fingertip(pose[hand],fit,.44f,.5f);
                CHECK(quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).volume==1);
                fingertip(pose[hand],fit,.91f,.5f);
                CHECK(quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action==QUEST_RADIO_VOLUME);
            }
        }
        valid[hand]=0;CHECK(!quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action && !grip.hand && !grip.hover_mask);
        valid[hand]=1;CHECK(!quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action && !grip.hover_mask);
        squeeze[hand]=0;quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy);
        busy[hand]=1;squeeze[hand]=1;CHECK(!quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action && !grip.hover_mask);
        busy[hand]=0;squeeze[hand]=0;
        fingertip(pose[hand],fit,.55f,.5f);fingertip(pose[1-hand],fit,.91f,.5f);
        CHECK(!quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action);
        CHECK(grip.hover_mask==((1u<<QUEST_RADIO_PREV)|(1u<<QUEST_RADIO_NEXT)));
        pose[hand][14]+=.05f;
        CHECK(!quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action);
        CHECK(grip.hover_mask==(1u<<QUEST_RADIO_NEXT));
        squeeze[1-hand]=.6f;
        CHECK(!quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action && grip.hover_mask==(1u<<QUEST_RADIO_NEXT));
        squeeze[1-hand]=1;
        CHECK(quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action==QUEST_RADIO_NEXT && !grip.hover_mask);
        fingertip(pose[hand],fit,.55f,.5f);
        CHECK(!quest_radio_update(&grip,fit,1,valid,pose,squeeze,busy).action && !grip.hover_mask);
        squeeze[1-hand]=0;
        CHECK(!quest_radio_update(&grip,fit,0,valid,pose,squeeze,busy).action && !grip.hover_mask);
    }
    const uint32_t playlist=(1u<<3)|(1u<<7)|(1u<<26);
    CHECK(quest_radio_track(3,playlist,1)==7 && quest_radio_track(7,playlist,-1)==3);
    CHECK(quest_radio_track(26,playlist,1)==3 && quest_radio_track(3,playlist,-1)==26);
    CHECK(quest_radio_track(-1,playlist,-1)==26 && quest_radio_track(42,playlist,1)==3);
    CHECK(quest_radio_track(1,0,1)==-1 && quest_radio_track(7,1u<<7,1)==7);
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
    puts("Radio contact, hover, grab and playlist tests passed for all vehicles and both hands.");
    return 0;
}
