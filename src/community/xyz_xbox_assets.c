#include "xyz_xbox_assets.h"
#include <stdio.h>
#include <string.h>

/* Algorithms adapted from MIT upstream roam_game.c part_packs/packs_wake;
 * Xbox addresses/layouts independently compared to the owned US executable.
 * See vendor/roam/LICENSE and config/nfsu2-xyz-xbox-map.json. */
enum { STREAMER=0x3ea794,HEAD=STREAMER+0x18,PARTPACKS=0x42a2ac,
       PARTDB=0x45be28,HIBER=0x3f07a0,MODEL_BYTES=24 };
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t u16(const uint8_t *p){return (uint16_t)(p[0]|(unsigned)p[1]<<8);}
static int rd(const XyzXboxGuest *g,uint32_t a,void *b,size_t n){return g && g->read && xyz_xbox_address(a,n) && g->read(g->context,a,b,n);}
static int wr(const XyzXboxGuest *g,uint32_t a,const void *b,size_t n){return g && g->write && xyz_xbox_address(a,n) && g->write(g->context,a,b,n);}
static int r32(const XyzXboxGuest *g,uint32_t a,uint32_t *v){uint8_t b[4];if(!rd(g,a,b,4))return 0;*v=u32(b);return 1;}
static int w32(const XyzXboxGuest *g,uint32_t a,uint32_t v){uint8_t b[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)};return wr(g,a,b,4);}
static int w16(const XyzXboxGuest *g,uint32_t a,uint16_t v){uint8_t b[2]={(uint8_t)v,(uint8_t)(v>>8)};return wr(g,a,b,2);}
static int string(const XyzXboxGuest *g,uint32_t a,char *out,unsigned cap) {
    if(!cap)return 0;
    for(unsigned i=0;i<cap;i++){uint8_t c;if(!rd(g,a+i,&c,1))return 0;out[i]=(char)c;if(!c)return i!=0;if(c<32 || c>126)return 0;}
    return 0;
}
static int path_ok(const char *s) {
    // The retail Xbox AUDIO path ends in lowercase .bin. Preserve the game's
    // filename spelling; it is still a bounded game-owned geometry path.
    size_t n=strlen(s);if(n<18 || n>=XYZ_PACK_PATH || strncmp(s,"CARS\\",5) ||
        (strcmp(s+n-4,".BIN") && strcmp(s+n-4,".bin")) || strstr(s,".."))return 0;
    for(size_t i=0;i<n;i++)if(!((s[i]>='A'&&s[i]<='Z') || (s[i]>='a'&&s[i]<='z') || (s[i]>='0'&&s[i]<='9') || s[i]=='_' || s[i]=='\\' || s[i]=='.'))return 0;
    return 1;
}
static int add_path(XyzXboxPackCatalog *c,const char *path) {
    if(!path_ok(path))return 0;
    for(unsigned i=0;i<c->count;i++)if(!strcmp(path,c->path[i]))return 1;
    if(c->count==XYZ_PACK_CAP)return 0;
    memcpy(c->path[c->count++],path,strlen(path)+1);return 1;
}
static int part_db(const XyzXboxGuest *g) {
    uint32_t at,previous=PARTDB;if(!r32(g,PARTDB,&at))return 0;
    for(unsigned i=0;i<128;i++) {
        uint32_t back;if(at==PARTDB)return r32(g,PARTDB+4,&back) && back==previous;
        uint8_t b[0x3c];if(!rd(g,at,b,sizeof b) || u32(b+4)!=previous || u32(b)==at ||
            u32(b+0x38)>65536 || (u32(b+0x38) && !xyz_xbox_address(u32(b+0x34),(size_t)u32(b+0x38)*14)))return 0;
        previous=at;at=u32(b);
    }
    return 0;
}
int xyz_xbox_pack_catalog(const XyzXboxGuest *g,XyzXboxPackCatalog *c) {
    if(!c || !g || !g->call)return 0;memset(c,0,sizeof *c);
    for(unsigned i=0;i<11;i++) {
        uint32_t file;char path[XYZ_PACK_PATH];
        if(!r32(g,PARTPACKS+4*i,&file) || !string(g,file,path,sizeof path) || !add_path(c,path))return 0;
    }
    if(!part_db(g))return 0;
    uint32_t previous=0,seen[64];unsigned count=0;
    for(;count<64;count++) {
        uint32_t a[5]={0xa6e1b733,0x43,0,previous,0xffffffffu},part=0,name=0;uint8_t bytes[14];
        if(!g->call(g->context,XYZ_G_PART_NEXT,PARTDB,0,5,a,&part))return 0;
        if(!part)return 1;
        for(unsigned j=0;j<count;j++)if(seen[j]==part)return 0;
        if(!rd(g,part,bytes,sizeof bytes) || !g->call(g->context,XYZ_G_PART_NAME,part,0,0,0,&name))return 0;
        char brand[40],path[XYZ_PACK_PATH];if(!string(g,name,brand,sizeof brand))return 0;
        for(unsigned j=0;brand[j];j++)if(!((brand[j]>='A'&&brand[j]<='Z') || (brand[j]>='0'&&brand[j]<='9') || brand[j]=='_'))return 0;
        snprintf(path,sizeof path,"CARS\\WHEELS\\GEOMETRY_%s.BIN",brand);
        if(!add_path(c,path))return 0;seen[count]=previous=part;
    }
    return 0;
}
int xyz_xbox_pack_find(const XyzXboxGuest *g,const char *path,uint32_t *pack) {
    if(!path || !pack || strlen(path)>=128)return -1;*pack=0;
    uint32_t at,previous=HEAD;if(!r32(g,HEAD,&at))return -1;
    for(unsigned i=0;i<512;i++) {
        uint32_t back;
        if(at==HEAD)return r32(g,HEAD+4,&back) && back==previous?(*pack?1:0):-1;
        uint8_t b[0xc0];if(!rd(g,at,b,sizeof b) || u32(b+4)!=previous || u32(b)==at || !memchr(b+8,0,128))return -1;
        if(!strcmp((const char*)b+8,path)){if(*pack)return -1;*pack=at;}
        previous=at;at=u32(b);
    }
    return -1;
}
int xyz_xbox_pack_idle(const XyzXboxGuest *g,uint32_t pack) {
    uint8_t b[0xc0];if(!rd(g,pack,b,sizeof b))return 0;
    return !b[0x8a] && u32(b+0x90) && u32(b+0xa0) && (int32_t)u32(b+0xa8)<=0;
}
/* Returns -1 invalid, 0 absent, 1 present. The original pack table is sorted. */
static int model(const XyzXboxGuest *g,uint32_t table,uint32_t count,uint32_t name,uint32_t *entry,uint8_t *bytes) {
    if(count>100000 || (count && !xyz_xbox_address(table,(size_t)count*MODEL_BYTES)))return -1;
    uint32_t low=0,high=count;
    while(low<high) {
        uint32_t mid=low+(high-low)/2,key;if(!r32(g,table+mid*MODEL_BYTES,&key))return -1;
        if(key<name)low=mid+1;else high=mid;
    }
    if(low==count)return 0;
    *entry=table+low*MODEL_BYTES;if(!rd(g,*entry,bytes,MODEL_BYTES))return -1;
    return u32(bytes)==name;
}
static int data_valid(const uint8_t *e){uint32_t a=u32(e+20),n=u32(e+8);return !a || (n && n<=16*1024*1024 && xyz_xbox_address(a,n));}
int xyz_xbox_wake_prepare(const XyzXboxGuest *g,uint32_t pack,XyzXboxWakePlan *p) {
    if(!p)return -1;memset(p,0,sizeof *p);
    uint8_t b[0xc0],h[0x80],solid_header[0x80];uint32_t found;
    if(!rd(g,pack,b,sizeof b) || !memchr(b+8,0,128) ||
        xyz_xbox_pack_find(g,(const char*)b+8,&found)!=1 || found!=pack)return -1;
    if(!xyz_xbox_pack_idle(g,pack))return 0;
    if(!rd(g,HIBER,h,sizeof h))return -1;
    p->hiber_count=u32(h+12);p->hiber_table=u32(h+0x78);p->hiber_solids=u32(h+0x74);
    if(p->hiber_count>XYZ_HIBER_CAP)return -1;
    if(!p->hiber_count){p->pack=pack;return 1;}
    if(!rd(g,u32(b+0xb8),solid_header,sizeof solid_header))return -1;
    p->table=u32(b+0xa0);p->solids=u32(solid_header+0x74);p->pack=pack;
    uint32_t n=u32(b+0xa4);
    if(n>100000 || !xyz_xbox_address(p->table,(size_t)n*24) || !xyz_xbox_address(p->solids,(size_t)n*8) ||
       !xyz_xbox_address(p->hiber_table,(size_t)p->hiber_count*24) || !xyz_xbox_address(p->hiber_solids,(size_t)p->hiber_count*8))return -1;
    uint8_t entries[XYZ_HIBER_CAP][24],solids[XYZ_HIBER_CAP][8];
    if(!rd(g,p->hiber_table,entries,(size_t)p->hiber_count*24) || !rd(g,p->hiber_solids,solids,(size_t)p->hiber_count*8))return -1;
    uint32_t previous=0;
    for(unsigned i=0;i<p->hiber_count;i++) {
        uint32_t name=u32(entries[i]);
        if(!name || (i && name<=previous) || u32(solids[i])!=name || !data_valid(entries[i]))return -1;
        previous=name;
        uint32_t e;uint8_t eb[24];int match=model(g,p->table,n,name,&e,eb);
        if(match<0 || (match && !data_valid(eb)))return -1;
        if(match)++p->matches;
        if(!match || !u32(eb+20))continue;
        XyzXboxWakeFix *f=&p->fix[p->count++];f->name=name;f->entry=e;f->hiber=p->hiber_table+24*i;
        memcpy(f->original,eb,24);memcpy(f->hiber_original,entries[i],24);f->loads=u16(eb+18);
        uint8_t ps[8];if(!rd(g,p->solids+8*((e-p->table)/24),ps,8) || u32(ps)!=name)return -1;
        f->solid=u32(ps+4);
        uint32_t hd=u32(entries[i]+20),pd=u32(eb+20),hs=u32(solids[i]+4);
        int idle=!((eb[17]|entries[i][17])&0x30);
        if(idle && hd && hs>=hd && (uint64_t)hs<(uint64_t)hd+u32(entries[i]+8) &&
           f->solid>=pd && (uint64_t)f->solid<(uint64_t)pd+u32(eb+8) && (unsigned)f->loads+u16(entries[i]+18)<=0xfffe)f->kind=XYZ_WAKE_FREE;
        else if(idle && !hd)f->kind=XYZ_WAKE_KEEP;
        else {
            f->kind=XYZ_WAKE_HIDE;uint32_t candidate=name^0x5a5a5a5au;
            for(unsigned attempt=0;attempt<2048;attempt++,candidate+=0x9e3779b9u) {
                int used=!candidate;uint32_t ignored;uint8_t tmp[24];
                int has=model(g,p->table,n,candidate,&ignored,tmp);if(has<0)return -1;used|=has;
                for(unsigned j=0;j<p->hiber_count;j++)used|=u32(entries[j])==candidate;
                for(unsigned j=0;j+1<p->count;j++)used|=p->fix[j].temporary==candidate;
                if(!used){f->temporary=candidate;break;}
            }
            if(!f->temporary)return -1;
        }
    }
    return 1;
}
int xyz_xbox_wake_begin(const XyzXboxGuest *g,XyzXboxWakePlan *p) {
    if(!p || !p->pack || p->count>XYZ_HIBER_CAP || p->begun || !g || !g->call || !xyz_xbox_pack_idle(g,p->pack))return 0;
    uint32_t n,table;if(!r32(g,HIBER+12,&n) || n!=p->hiber_count || !r32(g,HIBER+0x78,&table) || table!=p->hiber_table)return 0;
    // Revalidate every planned edit before doing any of them.
    for(unsigned i=0;i<p->count;i++) {
        XyzXboxWakeFix *f=&p->fix[i];uint8_t e[24],h[24];
        if(!rd(g,f->entry,e,24) || !rd(g,f->hiber,h,24) || memcmp(e,f->original,24) || memcmp(h,f->hiber_original,24))return 0;
    }
    p->begun=1;
    for(unsigned i=0;i<p->count;i++) {
        XyzXboxWakeFix *f=&p->fix[i];uint32_t args[2]={p->pack,f->entry},ignored;
        if(f->kind==XYZ_WAKE_FREE && !g->call(g->context,XYZ_G_MODEL_FREE,STREAMER,0,2,args,&ignored))return 0;
        if(f->kind==XYZ_WAKE_HIDE && !w32(g,f->hiber,f->temporary))return 0;
    }
    return 1;
}
int xyz_xbox_wake_finish(const XyzXboxGuest *g,XyzXboxWakePlan *p) {
    if(!p || p->count>XYZ_HIBER_CAP || !p->begun)return 0;
    uint32_t n,table;if(!r32(g,HIBER+12,&n) || n>XYZ_HIBER_CAP || !r32(g,HIBER+0x78,&table) ||
        (n && !xyz_xbox_address(table,(size_t)n*24)))return 0;
    for(unsigned i=0;i<p->count;i++) {
        XyzXboxWakeFix *f=&p->fix[i];
        if(f->kind==XYZ_WAKE_HIDE) {
            unsigned found=0;
            for(unsigned j=0;j<n;j++){uint32_t name;if(!r32(g,table+24*j,&name))return 0;if(name==f->temporary){if(!w32(g,table+24*j,f->name))return 0;++found;}}
            if(found!=1)return 0;
        } else {
            uint8_t e[24];if(!rd(g,f->entry,e,24) || u32(e)!=f->name)return 0;
            if(f->kind==XYZ_WAKE_KEEP) {
                if(!wr(g,f->entry,f->original,24) || !w32(g,p->solids+8*((f->entry-p->table)/24)+4,f->solid))return 0;
            } else {
                unsigned sum=u16(e+18)+(unsigned)f->loads;if(sum>0xfffe)return 0;
                if(!w16(g,f->entry+18,(uint16_t)sum))return 0;
            }
        }
    }
    p->begun=0;return 1;
}
