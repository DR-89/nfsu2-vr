#include "xyz_xbox_race_settings.h"
#include "xyz_auth.h"
#include <string.h>
enum {PROFILE=0x41c340};
/* Audited in original Create (0x198230) and the network option controls.
 * Password and confirmation are one field so they cannot be split on restore. */
static const struct {unsigned offset,size;} fields[]={
    {0,2},{4,4},{8,4},{0xc,1},{0x18,1},{0x19,1},{0x1a,1},
    {0x1b,29},{0x38,2},{0x3a,2},{0x3c,4},{0x48,1}
};
static int read_bytes(const XyzLobbyGuest *g,uint32_t at,void *out,size_t size){
    return g && g->memory && g->memory->read && xyz_xbox_address(at,size) &&
        g->memory->read(g->memory->context,at,out,size);
}
static int write_bytes(const XyzLobbyGuest *g,uint32_t at,const void *in,size_t size){
    return g && g->memory && g->memory->write && xyz_xbox_address(at,size) &&
        g->memory->write(g->memory->context,at,in,size);
}
static int frontend(const XyzLobbyGuest *g,const XyzXboxFrame *f){
    if(!f || f->status!=XYZ_XBOX_FRONTEND || f->flow!=3)return 0;
    const uint32_t gates[]={0x3fbbf4,0x3fbbfc,0x428e14,0x4493e0,0x44940c};
    for(unsigned i=0;i<sizeof gates/sizeof gates[0];++i){uint32_t value;
        if(!read_bytes(g,gates[i],&value,4) || value)return 0;
    }
    uint32_t net[8];uint8_t players;
    if(!read_bytes(g,0x456290,net,sizeof net) || !read_bytes(g,0x4562b0,&players,1) || players)return 0;
    for(unsigned i=0;i<8;++i)if(net[i])return 0;
    return 1;
}
int xyz_xbox_race_settings_valid(const XyzLobbyGuest *g,const XyzRaceSettings *o){
    if(!g || !g->memory || !o || !o->track || o->track>INT16_MAX ||
       o->min_players<2 || o->min_players>o->max_players || o->max_players>6 ||
       o->reverse>1 || o->collision>1 || o->catchup>1 || o->ranked>1 ||
       o->private_game>1 || o->unrestricted>1 || o->performance>3 ||
       !memchr(o->password,0,sizeof o->password))return 0;
    /* Keep controller-entered passwords within a bounded plain alphabet. No
     * control bytes or TagField syntax enter the fixed-size guest fields. */
    for(const unsigned char *p=(const unsigned char *)o->password;*p;++p)
        if(!((*p>='A' && *p<='Z') || (*p>='a' && *p<='z') ||
             (*p>='0' && *p<='9') || *p=='_' || *p=='-'))return 0;
    XyzRaceTrack tracks[XYZ_TRACK_LIMIT];unsigned count=0;
    if(!xyz_xbox_tracks(g->memory->read,g->memory->context,o->mode,tracks,XYZ_TRACK_LIMIT,&count))return 0;
    for(unsigned i=0;i<count;++i)if(tracks[i].id==o->track){
        XyzRaceRules rules;
        return xyz_xbox_track_rules(o->mode,&tracks[i],&rules) &&
            o->laps>=rules.min_laps && o->laps<=rules.max_laps &&
            o->max_players<=rules.max_players && (!o->reverse || tracks[i].reverse) &&
            (!(o->mode==3 || o->mode==10) || !o->catchup);
    }
    return 0;
}
static void put32(uint8_t *at,unsigned value){uint32_t v=value;memcpy(at,&v,4);}
static void put16(uint8_t *at,unsigned value){uint16_t v=(uint16_t)value;memcpy(at,&v,2);}
int xyz_xbox_race_settings_join(const XyzLobbyGuest *g,const XyzXboxLobby *l,
    XyzRaceSettingsLease *s,const XyzXboxFrame *f,const XyzRaceSettings *o){
    uint32_t protocol;
    if(!s || s->captured || !l || l->stage!=XYZ_LOBBY_READY || !l->initialized ||
       !l->generation || !l->protocol || !frontend(g,f) ||
       !read_bytes(g,l->manager+0x44,&protocol,4) || protocol!=l->protocol ||
       !xyz_xbox_race_settings_valid(g,o) || !read_bytes(g,PROFILE,s->previous,sizeof s->previous))return 0;
    memcpy(s->written,s->previous,sizeof s->written);
    put32(s->written+8,o->mode);s->written[0x18]=(uint8_t)o->ranked;
    s->written[0x19]=(uint8_t)o->catchup;s->written[0x1a]=(uint8_t)o->collision;
    put16(s->written+0x38,o->track);put16(s->written+0x3a,o->laps);put32(s->written+0x3c,o->reverse);
    s->generation=l->generation;s->protocol=l->protocol;s->captured=1;
    s->remaining=(1u<<2)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<8)|(1u<<9)|(1u<<10);
    return 1;
}
int xyz_xbox_race_settings_apply(const XyzLobbyGuest *g,const XyzXboxLobby *l,
    XyzRaceSettingsLease *s,const XyzXboxFrame *f,const XyzRaceSettings *o){
    uint32_t protocol;
    if(!s || s->captured || !l || l->stage!=XYZ_LOBBY_READY || !l->initialized ||
       !l->generation || !l->protocol || !frontend(g,f) ||
       !read_bytes(g,l->manager+0x44,&protocol,4) || protocol!=l->protocol ||
       !xyz_xbox_race_settings_valid(g,o) || !read_bytes(g,PROFILE,s->previous,sizeof s->previous))return 0;
    memcpy(s->written,s->previous,sizeof s->written);
    uint8_t *b=s->written;
    b[0]=(uint8_t)o->max_players;b[1]=(uint8_t)o->min_players;
    put32(b+4,0);put32(b+8,o->mode);b[0xc]=(uint8_t)o->unrestricted;
    b[0x18]=(uint8_t)o->ranked;b[0x19]=(uint8_t)o->catchup;b[0x1a]=(uint8_t)o->collision;
    b[0x1b]=o->password[0]!=0;memset(b+0x1c,0,28);
    memcpy(b+0x1c,o->password,strlen(o->password));memcpy(b+0x29,o->password,strlen(o->password));
    put16(b+0x38,o->track);put16(b+0x3a,o->laps);put32(b+0x3c,o->reverse);b[0x48]=(uint8_t)o->private_game;
    s->generation=l->generation;s->protocol=l->protocol;s->captured=1;
    s->remaining=(1u<<(sizeof fields/sizeof fields[0]))-1u;
    return write_bytes(g,PROFILE,b,sizeof s->written);
}
int xyz_xbox_race_settings_release(const XyzLobbyGuest *g,XyzRaceSettingsLease *s,const XyzXboxFrame *f){
    if(!s)return 0;if(!s->captured)return 1;
    if(!frontend(g,f))return 0;
    for(unsigned i=0;i<sizeof fields/sizeof fields[0];++i)if(s->remaining&(1u<<i)){
        unsigned offset=fields[i].offset,size=fields[i].size;uint8_t current[29];
        if(!read_bytes(g,PROFILE+offset,current,size))return 0;
        int ours=!memcmp(current,s->written+offset,size);
        xyz_auth_clear(current,sizeof current);
        if(ours && !write_bytes(g,PROFILE+offset,s->previous+offset,size))return 0;
        s->remaining&=~(1u<<i);
    }
    xyz_auth_clear(s,sizeof *s);return 1;
}
