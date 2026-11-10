#pragma once
#include "quest_ghost_hands.h"
#include "quest_radio_display.h"
#include "quest_shifter.h"

/* All quads lie in cabin X/Z, with the top of the text toward the dash.
 * Seat keys, console LED/page buttons and either shift guide fit in the
 * shared vertex budget. Console keys follow the fitted sloping face. */
enum { QUEST_CONTROL_VERTICES=56*6 };
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
static inline void quest_console_quad(QuestGhostVertex **next,const QuestCockpitVehicle *fit,
    float u,float v,float w,float h,int style,const uint32_t *glyph)
{
    QuestGhostVertex *start=*next;const float zero[3]={0};quest_control_quad(next,zero,1,1,style,glyph);
    for(int i=0;i<6;++i)quest_radio_point(start[i].position,fit,u+(start[i].uv[0]-.5f)*w,v+(start[i].uv[1]-.5f)*h);
}
static inline unsigned quest_cockpit_controls_mode(QuestGhostVertex *out,int vehicle,const QuestRadioFont *font,
    uint32_t hover,int gears,float lever_x,float lever_z,int sequential,int reverse)
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
    for(int button=0;button<2;++button) {
        const char *label=button?"PAGE >":"LED";size_t n=strlen(label);float u=.275f+.45f*button;
        quest_console_quad(&next,fit,u,2.2f,.43f,.60f,(hover&(1u<<(QUEST_COCKPIT_LED+button)))?QUEST_KEY_HOVER:QUEST_KEY_FACE,NULL);
        for(size_t c=0;c<n;++c)quest_console_quad(&next,fit,u+((float)c-(n-1)*.5f)*.052f,2.2f,.044f,.25f,QUEST_KEY_TEXT,font->bits[(unsigned)label[c]]);
    }
    if(sequential && gears>=4 && gears<=6) {
        QuestShifter neutral={0};float p[3];quest_shifter_knob(&neutral,vehicle,p);p[1]+=.045f;
        quest_control_quad(&next,p,.004f,QUEST_SHIFT_THROW*2,QUEST_GATE_RAIL,NULL);
        p[2]-=QUEST_SHIFT_THROW*1.25f;
        quest_control_quad(&next,p,.014f,.020f,QUEST_KEY_TEXT,font->bits[reverse?'+':'-']);
        p[2]+=QUEST_SHIFT_THROW*2.5f;
        quest_control_quad(&next,p,.014f,.020f,QUEST_KEY_TEXT,font->bits[reverse?'-':'+']);
        p[2]-=QUEST_SHIFT_THROW*1.25f;p[2]+=lever_z;
        quest_control_quad(&next,p,.017f,.017f,QUEST_GATE_CURSOR,NULL);
    } else if(gears>=4 && gears<=6) {
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

static inline unsigned quest_cockpit_controls(QuestGhostVertex *out,int vehicle,const QuestRadioFont *font,
    uint32_t hover,int gears,float lever_x,float lever_z)
{ return quest_cockpit_controls_mode(out,vehicle,font,hover,gears,lever_x,lever_z,0,0); }
