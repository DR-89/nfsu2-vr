#include "../src/community/xyz_xbox_gps.h"
#include "../android/runtime/quest_gps.h"
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t flow=6,race=0,search=1,path=0;
static uint8_t enabled=1,units=1;
static float target[2]={3,4};static uint32_t fail;
static int read_guest(void *ctx,uint32_t at,void *out,size_t n)
{
    (void)ctx;if(at==fail)return 0;
    switch(at){
    case 0x428e34: CHECK(n==4);memcpy(out,&flow,n);break;
    case 0x3cc470: CHECK(n==4);memcpy(out,&race,n);break;
    case 0x3d8b84: CHECK(n==4);memcpy(out,&search,n);break;
    case 0x3d8b88: CHECK(n==4);memcpy(out,&path,n);break;
    case 0x3fbdd9: CHECK(n==1);memcpy(out,&enabled,n);break;
    case 0x406cda: CHECK(n==1);memcpy(out,&units,n);break;
    case 0x3fbdd0: CHECK(n==8);memcpy(out,target,n);break;
    default: CHECK(0);
    }return 1;
}
int main(void)
{
    float p[3]={0},m;int metric;
    CHECK(xyz_xbox_gps(read_guest,0,p,&m,&metric) && m==5 && metric==1);
    search=2;units=0;CHECK(xyz_xbox_gps(read_guest,0,p,&m,&metric) && m==5 && !metric);
    for(path=3;path<=4;++path)CHECK(!xyz_xbox_gps(read_guest,0,p,&m,&metric) && m==-1);path=0;
    search=0;CHECK(!xyz_xbox_gps(read_guest,0,p,&m,&metric));search=1;
    enabled=0;CHECK(!xyz_xbox_gps(read_guest,0,p,&m,&metric));enabled=1;
    race=1;CHECK(!xyz_xbox_gps(read_guest,0,p,&m,&metric));race=0;
    flow=5;CHECK(!xyz_xbox_gps(read_guest,0,p,&m,&metric));flow=6;
    target[0]=NAN;CHECK(!xyz_xbox_gps(read_guest,0,p,&m,&metric));target[0]=3;
    p[0]=INFINITY;CHECK(!xyz_xbox_gps(read_guest,0,p,&m,&metric));p[0]=0;
    for(unsigned i=0;i<7;++i){const uint32_t addresses[]={0x428e34,0x3cc470,0x3d8b84,0x3d8b88,0x3fbdd9,0x406cda,0x3fbdd0};
        fail=addresses[i];CHECK(!xyz_xbox_gps(read_guest,0,p,&m,&metric));}fail=0;
    char text[QUEST_GPS_TEXT];
    quest_gps_format(text,0,1);CHECK(!strcmp(text,"0 M"));
    quest_gps_format(text,999.4f,1);CHECK(!strcmp(text,"999 M"));
    quest_gps_format(text,999.5f,1);CHECK(!strcmp(text,"1.0 KM"));
    quest_gps_format(text,1609.344f,0);CHECK(!strcmp(text,"1.0 MI"));
    quest_gps_format(text,30.48f,0);CHECK(!strcmp(text,"100 FT"));
    quest_gps_format(text,NAN,0);CHECK(!text[0]);
    quest_gps_format(text,-1,1);CHECK(!text[0]);
    QuestRadioFont font;quest_radio_font_init(&font);
    QuestGhostVertex vertices[QUEST_GPS_VERTICES];
    CHECK(!quest_gps_vertices(vertices,&font,text));
    quest_gps_format(text,1234,1);unsigned n=quest_gps_vertices(vertices,&font,text);
    CHECK(n==42 && n<=QUEST_GPS_VERTICES);
    float lo=100,hi=-100;
    for(unsigned i=0;i<n;++i){CHECK(vertices[i].position[1]<-QUEST_MINIMAP_RADIUS);
        lo=fminf(lo,vertices[i].position[0]);hi=fmaxf(hi,vertices[i].position[0]);}
    CHECK(fabsf(lo+hi)<.000001f);CHECK(vertices[0].position[2]<vertices[6].position[2]);
    puts("GPS: native state/target/units, stale rejection inputs, distance rounding and map-local glyph layout passed.");return 0;
}
