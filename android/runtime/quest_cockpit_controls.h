#pragma once
#include "quest_ghost_hands.h"
#include "quest_radio_display.h"
#include "quest_shifter.h"

/* All quads lie in cabin X/Z, with the top of the text toward the dash.
 * Four keys + 13 letters, a crossbar, at most seven rails and gear labels,
 * neutral and the live cursor fit in 34 quads. */
enum { QUEST_CONTROL_VERTICES=36*6 };
enum { QUEST_KEY_FACE, QUEST_KEY_HOVER, QUEST_GATE_RAIL, QUEST_GATE_CURSOR, QUEST_KEY_TEXT };
static inline void quest_control_quad(QuestGhostVertex **next,const float p[3],float w,float h,
    int style,const uint32_t *glyph)
{
    const int corners[6]={0,1,2,0,2,3};
    for(int i=0;i<6;++i) {
        int c=corners[i],u=c==1 || c==2,v=c>=2;
        *(*next)++=(QuestGhostVertex){{p[0]+(u-.5f)*w,p[1],p[2]+(v-.5f)*h},
            {(float)style,glyph?(float)glyph[0]:0,glyph?(float)glyph[1]:0},{(float)u,(float)v}};
    }
}
static inline unsigned quest_cockpit_controls(QuestGhostVertex *out,int vehicle,const QuestRadioFont *font,
    uint32_t hover,int gears,float lever_x,float lever_z)
{
    QuestGhostVertex *next=out;
    const QuestCockpitVehicle *fit=quest_cockpit_vehicle(vehicle);
    const char *labels[]={"FWD","BACK","UP","DOWN"};
    for(int i=0;i<4;++i) {
        float p[3];quest_seat_point(p,fit,.5f,(i+.5f)/4.f);
        quest_control_quad(&next,p,.047f*fit->near_width,.039f,
            (hover&(1u<<(QUEST_SEAT_FORWARD+i)))?QUEST_KEY_HOVER:QUEST_KEY_FACE,NULL);
        size_t n=strlen(labels[i]);
        float advance=.009f*fit->near_width;
        p[0]-=(n-1)*.5f*advance;
        for(size_t c=0;c<n;++c,p[0]+=advance)
            quest_control_quad(&next,p,advance*.83f,.015f,QUEST_KEY_TEXT,font->bits[(unsigned)labels[i][c]]);
    }
    if(gears>=4 && gears<=6) {
        QuestShifter neutral={0};float center[3],p[3];
        quest_shifter_knob(&neutral,vehicle,center);
        center[1]+=.045f;
        int last=quest_shifter_lane(QUEST_SHIFT_R,gears);
        memcpy(p,center,sizeof p);
        p[0]+=(last*.5f-1)*QUEST_SHIFT_PITCH;
        quest_control_quad(&next,p,last*QUEST_SHIFT_PITCH+.004f,.004f,QUEST_GATE_RAIL,NULL);
        for(int gear=0;gear<=gears+1;++gear) {
            if(gear==QUEST_SHIFT_N)continue;
            QuestShifter stop;quest_shifter_detent(&stop,gear,gears);
            p[0]=center[0]+stop.x;p[2]=center[2]+stop.z*.5f;
            quest_control_quad(&next,p,.004f,QUEST_SHIFT_THROW,QUEST_GATE_RAIL,NULL);
            p[2]=center[2]+stop.z*1.25f;
            quest_control_quad(&next,p,.012f,.017f,QUEST_KEY_TEXT,font->bits[gear==0?'R':'0'+gear-1]);
        }
        p[0]=center[0]+.011f;p[2]=center[2]-.011f;
        quest_control_quad(&next,p,.010f,.014f,QUEST_KEY_TEXT,font->bits['N']);
        p[0]=center[0]+lever_x;p[2]=center[2]+lever_z;
        quest_control_quad(&next,p,.017f,.017f,QUEST_GATE_CURSOR,NULL);
    }
    return (unsigned)(next-out);
}
