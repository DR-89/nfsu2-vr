#pragma once
#include <math.h>
#include <stdio.h>
#include "quest_cockpit_controls.h"
#include "quest_minimap.h"

enum { QUEST_GPS_TEXT=24, QUEST_GPS_VERTICES=(QUEST_GPS_TEXT+1)*6 };
/* Straight-line distance. Round before switching units to avoid 1000 M or
 * 5280 FT. Invalid/inactive snapshots produce no panel at all. */
static inline void quest_gps_format(char text[QUEST_GPS_TEXT],float metres,int metric)
{
    text[0]=0;if(!isfinite(metres) || metres<0 || metres>=1000000)return;
    double small=metric?metres:metres/.3048,threshold=metric?1000:5280;
    if(small<threshold-.5)snprintf(text,QUEST_GPS_TEXT,"%.0f %s",small,metric?"M":"FT");
    else snprintf(text,QUEST_GPS_TEXT,"%.1f %s",small/threshold,metric?"KM":"MI");
}
/* Glyphs share the map's rigid transform and follow its saved position.
 * Keep the strip below the circular map so no route/marker is covered. */
static inline unsigned quest_gps_vertices(QuestGhostVertex *out,const QuestRadioFont *font,const char *text)
{
    size_t n=0;while(n<QUEST_GPS_TEXT && text[n])++n;
    if(!n || n==QUEST_GPS_TEXT)return 0;
    QuestGhostVertex *next=out;
    const float advance=.010f,height=.014f,y=-QUEST_MINIMAP_RADIUS-.016f;
    float p[3]={0,0,0};
    quest_control_quad(&next,p,n*advance+.012f,height+.010f,QUEST_KEY_FACE,NULL);
    p[0]=-(float)(n-1)*advance*.5f;
    for(size_t i=0;i<n;++i,p[0]+=advance){
        unsigned c=(unsigned char)text[i];
        quest_control_quad(&next,p,advance*.83f,height,QUEST_KEY_TEXT,font->bits[c<128?c:'?']);
    }
    for(QuestGhostVertex *v=out;v<next;++v){
        float down=v->position[2];v->position[1]=y-down;
        v->position[2]=v->normal[0]==QUEST_KEY_TEXT?.002f:.001f;
    }
    return (unsigned)(next-out);
}
