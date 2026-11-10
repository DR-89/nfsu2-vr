#include "xyz_xbox_vehicle.h"
#include "xyz_limits.h"
#include <math.h>
#include <string.h>

/* Anchors and evidence: config/nfsu2-xyz-xbox-map.json. Never substitute
 * PC addresses here: the wire record is portable, the game objects are not. */
enum { G_WORLD=0x4493e0, G_PLAYER=0x44940c, G_TYPES=0x45af88,
       G_OWNERS=0x45af74, G_FLOW=0x428e34, G_TRACK=0x3cc460,
       G_RACE_TYPE=0x3cc470, CAR_BYTES=0x9e0, RIDE_BYTES=0x8f0,
       TYPE_BYTES=0x890, TYPE_COUNT=46, WORLD_LIMIT=XYZ_WORLD_CAR_LIMIT };

static uint32_t physical_size=0x04000000u;
int xyz_xbox_set_physical_size(uint32_t bytes) {
    if(bytes!=0x04000000u && bytes!=0x08000000u)return 0;
    physical_size=bytes;return 1;
}
int xyz_xbox_address(uint32_t address, size_t size) {
    uint64_t end=(uint64_t)address+size;
    return size<=physical_size &&
        ((address>=0x10000u && end<=0x04000000u) ||
         (address>=0x80000000u && end<=0x80000000ull+physical_size));
}
static int get(XyzXboxRead read, void *ctx, uint32_t a, void *out, size_t n) {
    return read && xyz_xbox_address(a,n) && read(ctx,a,out,n);
}
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static float f32(const uint8_t *p) {
    uint32_t bits=u32(p); float v; memcpy(&v,&bits,4); return v;
}
static int get32(XyzXboxRead read, void *ctx, uint32_t a, uint32_t *v) {
    uint8_t b[4]; if(!get(read,ctx,a,b,4)) return 0; *v=u32(b); return 1;
}
int xyz_xbox_streaming(XyzXboxRead read,void *ctx,XyzXboxStreaming *out) {
    if(!out)return 0;memset(out,0,sizeof *out);
    XyzXboxStreaming state;
    if(!get32(read,ctx,0x4419e0+0x168,&state.phase) || state.phase>6 ||
       !get32(read,ctx,0x45c4a0+0x14,&state.car_busy))return 0;
    *out=state;return 1;
}
static void vec(const uint8_t *p, float *v) {
    for(int i=0;i<3;i++) v[i]=f32(p+4*i);
}
static float finite_or_zero(float f) { return isfinite(f)?f:0.f; }
static float unit(float f) { return !isfinite(f)||f<0?0:f>1?1:f; }

uint32_t xyz_xbox_find_car(XyzXboxRead read, void *ctx, uint32_t world,
                          uint32_t wanted, uint32_t record) {
    uint32_t at, previous=world, found=0;
    if((!wanted && !record) || !xyz_xbox_address(world,0x118) ||
       !get32(read,ctx,world,&at)) return 0;
    for(unsigned i=0; i<WORLD_LIMIT; i++) {
        uint8_t b[0x18];
        if(at==world) {
            uint32_t tail;
            return get32(read,ctx,world+4,&tail) && tail==previous?found:0;
        }
        if(!xyz_xbox_address(at,CAR_BYTES) || !get(read,ctx,at,b,sizeof b) ||
           u32(b+4)!=previous || at==u32(b)) return 0;
        if((!wanted || at==wanted) && (!record || u32(b+0x14)==record)) {
            if(found) return 0; /* duplicate record: ambiguous ownership */
            found=at;
        }
        previous=at; at=u32(b);
    }
    return 0; /* unterminated/cyclic/corrupt list */
}

static int pose(XyzXboxRead read, void *ctx, const uint8_t *b, uint32_t time, rc_state *s) {
    float axes[3][3];
    memset(s,0,sizeof *s); s->t_us=time;
    vec(b+0x60,s->p); vec(b+0x70,s->v); vec(b+0x110,s->w);
    for(int i=0;i<3;i++) {
        if(!isfinite(s->p[i]) || !isfinite(s->v[i])) return 0;
        s->w[i]=finite_or_zero(s->w[i]);
        vec(b+0x90+16*i,axes[i]);
        float length=0;
        for(int j=0;j<3;j++) {
            if(!isfinite(axes[i][j])) return 0;
            length+=axes[i][j]*axes[i][j];
        }
        if(length<0.5f || length>1.5f) return 0;
    }
    /* Reject an uninitialised/reflected/skewed basis rather than manufacture
     * an apparently valid car from it. Retail transforms are orthonormal. */
    for(int i=0;i<3;i++) for(int j=i+1;j<3;j++) {
        float dot=0; for(int k=0;k<3;k++) dot+=axes[i][k]*axes[j][k];
        if(fabsf(dot)>0.1f) return 0;
    }
    float det=axes[0][0]*(axes[1][1]*axes[2][2]-axes[1][2]*axes[2][1])
             -axes[0][1]*(axes[1][0]*axes[2][2]-axes[1][2]*axes[2][0])
             +axes[0][2]*(axes[1][0]*axes[2][1]-axes[1][1]*axes[2][0]);
    if(det<0.5f || det>1.5f || fabsf(s->p[2]+123456.f)<100.f) return 0;
    rc_m2q(axes[0],axes[1],axes[2],s->q);
    for(int i=0;i<4;i++) if(!isfinite(s->q[i])) return 0;
    if(fabsf(s->p[0])>20000.f || fabsf(s->p[1])>20000.f || fabsf(s->p[2])>2000.f ||
       s->v[0]*s->v[0]+s->v[1]*s->v[1]+s->v[2]*s->v[2]>40000.f) return 0;
    s->speed=finite_or_zero(f32(b+0x42c)); s->rpm=finite_or_zero(f32(b+0x440));
    int32_t gear=(int32_t)u32(b+0x434); s->gear=gear>=-1 && gear<=127?(int8_t)gear:0;
    uint8_t control[0x220];
    uint32_t ctl=u32(b+0x30);
    if(ctl && get(read,ctx,ctl,control,sizeof control)) {
        s->throttle=unit(f32(control+0x20c)); s->brake=unit(f32(control+0x214));
        s->steer=(float)(int16_t)((uint16_t)control[0x1e0]|(uint16_t)control[0x1e1]<<8)/32767.f;
        if(s->steer < -1.f) s->steer=-1.f;
        if(s->brake>0.1f) s->bits|=RC_BIT_BRAKE;
        if(f32(control+0x210)>0.1f) s->bits|=RC_BIT_HANDBRAKE;
    }
    if(s->speed < -0.5f) s->bits|=RC_BIT_REVERSE;
    /* Sound, hydraulics, damage and show-off need their own Xbox adapters.
     * Do not advertise unmeasured extension fields as working features. */
    return 1;
}

int xyz_xbox_car_state(XyzXboxRead read,void *ctx,uint32_t world,uint32_t car,uint32_t time,rc_state *out) {
    if(!out)return 0;memset(out,0,sizeof *out);uint8_t b[CAR_BYTES];
    return car && xyz_xbox_find_car(read,ctx,world,car,0)==car &&
        get(read,ctx,car,b,sizeof b) && pose(read,ctx,b,time,out);
}
int xyz_xbox_ride_config(XyzXboxRead read, void *ctx, uint32_t ride, uint8_t *out, int capacity) {
    if(!out || capacity<=0) return 0;
    uint8_t b[RIDE_BYTES], ti[0xd4];
    uint32_t types,owners;
    if(!get(read,ctx,ride,b,sizeof b) || !get32(read,ctx,G_TYPES,&types) ||
       !get32(read,ctx,G_OWNERS,&owners) || !xyz_xbox_address(owners,256*4) ||
       !xyz_xbox_address(types,TYPE_COUNT*TYPE_BYTES)) return 0;
    uint32_t type=u32(b);
    if(type>=TYPE_COUNT || !get(read,ctx,types+type*TYPE_BYTES,ti,sizeof ti)) return 0;
    rc_carcfg c; memset(&c,0,sizeof c); c.type_hash=u32(ti+0xd0);
    if(!c.type_hash || !ti[0]) return 0;
    for(int i=0;i<RC_CFG_TYPE_NAME && ti[i];i++) {
        if(ti[i]<32 || ti[i]>126) return 0;
        c.type_name[i]=(char)ti[i];
    }
    memcpy(c.ride_b,b+4,4);
    for(int i=0;i<10;i++) if(u32(b+0x318+4*i)) c.perf[i>>3]|=(uint8_t)(0x80u>>(i&7));
    for(int i=0;i<68;i++) if(b[0x340+i]) c.perf[2+(i>>3)]|=(uint8_t)(0x80u>>(i&7));
    memcpy(c.perf+11,b+0x398,41); c.has_perf=1;
    for(unsigned i=0;i<RC_PART_SLOTS;i++) {
        uint32_t ptr=u32(b+0x590+4*i); uint8_t part[14];
        if(!ptr) continue;
        /* A non-null invalid part makes this a partial/unstable ride. Sending
         * it would make another client repeatedly replace the car's look. */
        if(!get(read,ctx,ptr,part,sizeof part)) return 0;
        uint32_t owner;
        if(!get32(read,ctx,owners+4u*part[7],&owner)) return 0;
        c.part[c.n_parts].slot=(uint8_t)i;
        c.part[c.n_parts].key=u32(part); c.part[c.n_parts].owner=owner; c.n_parts++;
    }
    int n=rc_carcfg_encode(&c,out,capacity);
    if(n<=0 || n>RC_CFG_MAX) return 0;
    return n;
}

enum XyzXboxStatus xyz_xbox_sample(XyzXboxRead read, void *ctx,
                                  uint32_t time, XyzXboxFrame *out) {
    memset(out,0,sizeof *out);
    if(!get32(read,ctx,G_FLOW,&out->flow)) return out->status=XYZ_XBOX_BAD_WORLD;
    if(out->flow!=6) return out->status=XYZ_XBOX_FRONTEND;
    if(!get32(read,ctx,G_TRACK,&out->track) || !get32(read,ctx,G_RACE_TYPE,&out->race_type) ||
       !get32(read,ctx,G_WORLD,&out->world)) return out->status=XYZ_XBOX_BAD_WORLD;
    if(!out->world) return out->status=XYZ_XBOX_LOADING;
    int roaming=out->track==4000 && out->race_type==0;
    uint32_t index;
    if(!get32(read,ctx,G_PLAYER,&out->player) || !xyz_xbox_address(out->player,0x14) ||
       !get32(read,ctx,out->player+0x10,&index) || index!=0 ||
       !get32(read,ctx,out->player+4,&out->car)) return out->status=XYZ_XBOX_BAD_CAR;
    if(!out->car) return out->status=XYZ_XBOX_LOADING;
    if(!xyz_xbox_find_car(read,ctx,out->world,out->car,0)) return out->status=XYZ_XBOX_BAD_WORLD;
    uint8_t car[CAR_BYTES];
    if(!get(read,ctx,out->car,car,sizeof car)) return out->status=XYZ_XBOX_BAD_CAR;
    out->ride=u32(car+0x10);
    if(!pose(read,ctx,car,time,&out->state)) return out->status=XYZ_XBOX_BAD_POSE;
    int size=xyz_xbox_ride_config(read,ctx,out->ride,out->config,sizeof out->config);
    if(!size) {
        memset(&out->state,0,sizeof out->state);
        return out->status=XYZ_XBOX_BAD_RIDE;
    }
    out->config_size=(uint16_t)size;
    /* A race's car can be inspected by diagnostics/future race adapters, but
     * the free-roam link must only publish XYZ_XBOX_READY. */
    return out->status=roaming?XYZ_XBOX_READY:XYZ_XBOX_OTHER_RACE;
}
const char *xyz_xbox_status_text(enum XyzXboxStatus status) {
    static const char *const names[]={"frontend","loading","other-race","ready",
        "invalid-world","invalid-car","invalid-pose","invalid-ride"};
    return (unsigned)status<sizeof names/sizeof *names?names[status]:"unknown";
}
