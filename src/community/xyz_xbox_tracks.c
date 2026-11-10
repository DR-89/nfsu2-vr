#include "xyz_xbox_tracks.h"
#include <string.h>
#include <stdio.h>
static unsigned mode_mask(unsigned mode){
    switch(mode){case 0:return 1;case 1:return 4;case 2:return 2;case 3:return 8;
        case 4:return 0x2000;case 10:return 0x800;default:return 0;}
}
int xyz_xbox_tracks(XyzXboxRead read,void *context,unsigned mode,XyzRaceTrack *out,unsigned capacity,unsigned *count){
    uint32_t catalog[2],check[2],mask=mode_mask(mode);uint16_t ids[XYZ_TRACK_LIMIT];uint8_t eligible[XYZ_TRACK_LIMIT];unsigned used=0;
    if(count)*count=0;
    if(!out || !count || !capacity || capacity>XYZ_TRACK_LIMIT)return 0;
    memset(out,0,capacity*sizeof *out);
    if(!mask || !read || !read(context,0x43d110,catalog,sizeof catalog) ||
       !catalog[1] || catalog[1]>XYZ_TRACK_LIMIT || !xyz_xbox_address(catalog[0],(size_t)catalog[1]*0x128))return 0;
    for(unsigned i=0;i<catalog[1];++i){
        uint8_t bytes[0x98];uint16_t id;uint32_t flags,point,region;
        if(!read(context,catalog[0]+i*0x128,bytes,sizeof bytes))goto fail;
        memcpy(&id,bytes+0x8a,2);memcpy(&flags,bytes+0x94,4);
        if(!id || id>INT16_MAX)goto fail;
        eligible[i]=id!=4000 && id!=4801 && !bytes[0x88] && (flags&mask)!=0;
        /* Stock data contains two unused 4400 placeholder records. Reject an
         * ambiguous playable ID without rejecting unrelated placeholders. */
        for(unsigned n=0;n<i;++n)if(ids[n]==id && (eligible[n] || eligible[i]))goto fail;
        ids[i]=id;
        /* Retail 0xb2810 excludes the free-roam/pursuit placeholders and
         * disabled records before applying its mode-to-flag jump table. */
        if(!eligible[i])continue;
        memcpy(&point,bytes+0x80,4);memcpy(&region,bytes+0x7c,4);
        if(point>1 || bytes[0x86]>1 || !memchr(bytes,0,32) || used==capacity)goto fail;
        XyzRaceTrack *t=&out[used++];t->id=id;t->reverse=bytes[0x86];t->point_to_point=(uint8_t)point;t->region=region;t->modes=flags;
        memcpy(t->debug_name,bytes,32);
    }
    if(!read(context,0x43d110,check,sizeof check) || memcmp(catalog,check,sizeof check))goto fail;
    *count=used;return 1;
fail:
    memset(out,0,capacity*sizeof *out);return 0;
}
int xyz_xbox_track_rules(unsigned mode,const XyzRaceTrack *track,XyzRaceRules *rules){
    if(!rules)return 0;memset(rules,0,sizeof *rules);
    if(!track || !track->id || track->id>INT16_MAX || track->id==4000 || track->id==4801 ||
       !(track->modes&mode_mask(mode)) || track->point_to_point>1 || track->reverse>1)return 0;
    rules->max_players=mode==2?(track->id==4402?3:4):6;
    rules->min_laps=mode==1 || mode==2 || (mode==3 && track->point_to_point)?1:mode==10?4:2;
    rules->max_laps=rules->min_laps==1?1:10;
    return 1;
}
static int language_name(XyzXboxRead read,void *context,uint32_t hash,char *out,size_t capacity){
    uint32_t header[4],after[4],entry[2];
    if(!read(context,0x3fbc20,header,sizeof header) || !header[0] || header[0]>65536 ||
       !xyz_xbox_address(header[1],1) || !xyz_xbox_address(header[2],(size_t)header[0]*8))return 0;
    unsigned low=0,high=header[0];
    while(low<high){unsigned mid=low+(high-low)/2;
        if(!read(context,header[2]+mid*8,entry,sizeof entry))return 0;
        if(entry[0]<hash)low=mid+1;else high=mid;
    }
    if(low==header[0] || !read(context,header[2]+low*8,entry,sizeof entry) || entry[0]!=hash ||
       entry[1]<header[1] || !xyz_xbox_address(entry[1],capacity))return 0;
    uint32_t adjacent;
    if(low && (!read(context,header[2]+(low-1)*8,&adjacent,4) || adjacent>=hash))return 0;
    if(low+1<header[0] && (!read(context,header[2]+(low+1)*8,&adjacent,4) || adjacent<=hash))return 0;
    size_t i;
    for(i=0;i<capacity;++i){unsigned char ch;
        if(!read(context,entry[1]+(uint32_t)i,&ch,1) || (ch && (ch<32 || ch==127)))return 0;
        out[i]=(char)ch;if(!ch)break;
    }
    return i>0 && i<capacity && read(context,0x3fbc20,after,sizeof after) && !memcmp(header,after,sizeof header);
}
int xyz_xbox_track_name(XyzXboxRead read,void *context,unsigned id,int reverse,char *out,size_t capacity){
    if(!out || !capacity)return 0;out[0]=0;if(capacity>256)return 0;memset(out,0,capacity);
    if(!read || !id || id>INT16_MAX || reverse<0 || reverse>1)return 0;
    for(int direction=reverse;direction>=0;--direction){
        char key[40];snprintf(key,sizeof key,direction?"REVERSE_TRACK_NAME_%u":"TRACK_NAME_%u",id);
        uint32_t hash=UINT32_MAX;for(const unsigned char *p=(const unsigned char *)key;*p;++p)hash=hash*33+*p;
        if(language_name(read,context,hash,out,capacity))return 1;
        memset(out,0,capacity);
    }
    return 0;
}
