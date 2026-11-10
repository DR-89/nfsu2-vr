#include "xyz_race_info.h"
#include "xyz_xbox_tracks.h"
#include <string.h>
static int space(char c){return c==' ' || c=='\t' || c=='\r' || c=='\n';}
int xyz_xbox_race_info(XyzXboxRead read,void *context,const XyzLobbyGame *g,XyzRaceInfo *out){
    if(!out)return 0;memset(out,0,sizeof *out);out->mode=UINT32_MAX;
    if(!g || !memchr(g->params,0,sizeof g->params))return 0;
    const char *p=g->params;unsigned fields=0;
    while(*p){
        while(space(*p))++p;if(!*p)break;
        unsigned flag=0,value=0;
        if(!strncmp(p,"TRACK=",6)){flag=1;p+=6;}
        else if(!strncmp(p,"DIR=",4)){flag=2;p+=4;}
        else if(!strncmp(p,"LAPS=",5)){flag=4;p+=5;}
        else goto bad;
        if((fields&flag) || *p<'0' || *p>'9')goto bad;
        while(*p>='0' && *p<='9'){
            unsigned digit=(unsigned)(*p++-'0');if(value>(UINT32_MAX-digit)/10)goto bad;value=value*10+digit;
        }
        if(*p && !space(*p))goto bad;
        if(flag==1)out->track=value;else if(flag==2)out->reverse=value;else out->laps=value;
        fields|=flag;
    }
    if(fields!=7 || !out->track || out->track>INT16_MAX || out->reverse>1 || !out->laps || out->laps>10)goto bad;
    out->parsed=1;
    xyz_xbox_track_name(read,context,out->track,(int)out->reverse,out->track_name,sizeof out->track_name);
    switch(g->custom_flags&0xfe000000u){
        case 0x04000000:out->mode=0;break;case 0x08000000:out->mode=1;break;
        case 0x10000000:out->mode=2;break;case 0x20000000:out->mode=3;break;
        case 0x40000000:out->mode=10;break;case 0x80000000:out->mode=4;break;
        default:return 1;
    }
    XyzRaceTrack tracks[XYZ_TRACK_LIMIT];unsigned count=0;
    if(!xyz_xbox_tracks(read,context,out->mode,tracks,XYZ_TRACK_LIMIT,&count))return 1;
    for(unsigned i=0;i<count;++i)if(tracks[i].id==out->track){
        XyzRaceRules rules;
        out->compatible=xyz_xbox_track_rules(out->mode,&tracks[i],&rules) &&
            out->laps>=rules.min_laps && out->laps<=rules.max_laps &&
            g->min_players>=2 && g->min_players<=g->max_players && g->max_players<=rules.max_players &&
            g->players<=g->max_players && (!out->reverse || tracks[i].reverse);
        break;
    }
    return 1;
bad:
    memset(out,0,sizeof *out);out->mode=UINT32_MAX;return 0;
}
