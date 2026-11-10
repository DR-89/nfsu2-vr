#include "xyz_xbox_vehicle.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
/* Synthetic objects, not copied game data. Different COM/render origins catch
 * the easy-to-miss 0x50 versus 0x60 error in cross-client positions. */
enum { WORLD=0x100000, PLAYER=0x101000, CAR=0x102000, RIDE=0x103000,
       TYPES=0x110000, OWNERS=0x140000, PART=0x141000, CONTROL=0x142000,
       MEMORY=0x500000 };
static uint8_t ram[MEMORY];
static unsigned reads;
static int read_memory(void *context,uint32_t a,void *out,size_t n) {
    (void)context; reads++;
    if(a>MEMORY || n>MEMORY-a) return 0;
    memcpy(out,ram+a,n); return 1;
}
static void put32(uint32_t a,uint32_t v) {
    for(int i=0;i<4;i++) ram[a+i]=(uint8_t)(v>>(8*i));
}
static void putf(uint32_t a,float f) { uint32_t u; memcpy(&u,&f,4); put32(a,u); }
static void fixture(void) {
    memset(ram,0,sizeof ram); reads=0;
    put32(0x428e34,6); put32(0x3cc460,4000); put32(0x4493e0,WORLD);
    put32(0x44940c,PLAYER); put32(PLAYER+4,CAR);
    put32(WORLD,CAR); put32(WORLD+4,CAR); put32(CAR,WORLD); put32(CAR+4,WORLD);
    put32(CAR+0x10,RIDE); put32(CAR+0x14,0x123000); put32(CAR+0x30,CONTROL);
    putf(CAR+0x50,9999); putf(CAR+0x60,12.25f); putf(CAR+0x64,-34.5f); putf(CAR+0x68,1.75f);
    putf(CAR+0x70,8); putf(CAR+0x74,-9); putf(CAR+0x78,0.5f);
    putf(CAR+0x90,1); putf(CAR+0xa4,1); putf(CAR+0xb8,1);
    putf(CAR+0x110,0.2f); putf(CAR+0x42c,13); putf(CAR+0x440,4500); put32(CAR+0x434,3);
    putf(CONTROL+0x20c,0.75f); putf(CONTROL+0x214,0.25f); putf(CONTROL+0x210,1);
    ram[CONTROL+0x1e0]=0x00; ram[CONTROL+0x1e1]=0xc0;
    put32(0x45af88,TYPES); put32(0x45af74,OWNERS); put32(RIDE,2);
    memcpy(ram+TYPES+2*0x890,"TESTCAR",8); put32(TYPES+2*0x890+0xd0,0x87654321);
    ram[RIDE+4]=1; ram[RIDE+5]=2; ram[RIDE+6]=3; ram[RIDE+7]=4;
    put32(RIDE+0x318,1); put32(RIDE+0x318+9*4,1);
    ram[RIDE+0x340]=1; ram[RIDE+0x340+67]=1;
    for(int i=0;i<41;i++) ram[RIDE+0x398+i]=(uint8_t)(17+i);
    put32(RIDE+0x590+169*4,PART); put32(PART,0x1234abcd);
    ram[PART+7]=255; put32(OWNERS+255*4,0xfeedbeef);
}
int main(int argc,char **argv) {
    XyzXboxFrame frame; rc_carcfg cfg;
    CHECK(!xyz_xbox_address(0xffff,1)); CHECK(xyz_xbox_address(0x10000,1));
    CHECK(!xyz_xbox_address(0xfffffff0,64)); CHECK(!xyz_xbox_address(0x80000000,(size_t)-1));
    CHECK(xyz_xbox_address(0x83ffffff,1)); CHECK(!xyz_xbox_address(0x83ffffff,2));
    CHECK(!xyz_xbox_set_physical_size(0xffffffffu));
    CHECK(xyz_xbox_set_physical_size(128u<<20));
    CHECK(xyz_xbox_address(0x84000000,0x4000000) && !xyz_xbox_address(0x87ffffff,2));
    CHECK(!xyz_xbox_address(0x04000000,1)); // low RAM retains its original bound
    CHECK(xyz_xbox_set_physical_size(64u<<20) && !xyz_xbox_address(0x84000000,1));
    fixture();
    CHECK(xyz_xbox_sample(read_memory,0,123456,&frame)==XYZ_XBOX_READY);
    CHECK(frame.world==WORLD && frame.car==CAR && frame.ride==RIDE);
    CHECK(frame.state.t_us==123456 && frame.state.p[0]==12.25f && frame.state.p[2]==1.75f);
    CHECK(frame.state.q[3]==1 && frame.state.q[0]==0 && frame.state.v[0]==8);
    CHECK(frame.state.rpm==4500 && frame.state.gear==3 && frame.state.speed==13);
    CHECK(fabsf(frame.state.steer+0.5000153f)<0.000001f && frame.state.throttle==0.75f);
    CHECK((frame.state.bits&(RC_BIT_BRAKE|RC_BIT_HANDBRAKE))==(RC_BIT_BRAKE|RC_BIT_HANDBRAKE));
    CHECK(!frame.state.ext && !frame.state.eng);
    CHECK(rc_carcfg_decode(frame.config,frame.config_size,&cfg)==RC_OK);
    CHECK(cfg.type_hash==0x87654321 && !strcmp(cfg.type_name,"TESTCAR"));
    CHECK(cfg.n_parts==1 && cfg.part[0].slot==169 && cfg.part[0].key==0x1234abcd && cfg.part[0].owner==0xfeedbeef);
    CHECK(cfg.has_perf && cfg.perf[0]==0x80 && cfg.perf[1]==0x40 && cfg.perf[2]==0x80 && cfg.perf[10]==0x10);
    CHECK(cfg.perf[11]==17 && cfg.perf[51]==57 && cfg.ride_b[3]==4);
    CHECK(xyz_xbox_find_car(read_memory,0,WORLD,0,0x123000)==CAR);
    CHECK(!xyz_xbox_find_car(read_memory,0,WORLD,0,0x123001));
    /* Round-trip through the actual snapshot wire codec, not a second copy of
     * the adapter's field reader. */
    uint8_t wire[RC_STATE_BYTES]; rc_w w; rc_w_init(&w,wire,sizeof wire);
    rc_state_put(&w,&frame.state,0); CHECK(!w.err && w.n==RC_STATE_BYTES);
    rc_r r; rc_r_init(&r,wire,sizeof wire); rc_state state;
    CHECK(rc_state_get(&r,&state,0)==RC_OK && fabsf(state.p[0]-12.25f)<0.02f && state.gear==3);
    /* Finite cosmetic controls can be repaired; a bad pose cannot be sent. */
    putf(CAR+0x440,NAN); putf(CAR+0x110,INFINITY); putf(CONTROL+0x20c,INFINITY);
    CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_READY);
    CHECK(!frame.state.rpm && !frame.state.w[0] && !frame.state.throttle);
    putf(CAR+0x60,NAN); CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_BAD_POSE);
    fixture(); putf(CAR+0x90,-1); CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_BAD_POSE);
    fixture(); putf(CAR+0x68,-123456); CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_BAD_POSE);
    fixture(); put32(RIDE,46); CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_BAD_RIDE);
    fixture(); put32(RIDE+0x590+169*4,0xdeadbeef); CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_BAD_RIDE && !frame.config_size);
    fixture(); put32(CAR,CAR); CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_BAD_WORLD);
    fixture(); put32(CAR+4,0); CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_BAD_WORLD);
    fixture(); put32(WORLD+4,0); CHECK(!xyz_xbox_find_car(read_memory,0,WORLD,CAR,0));
    fixture(); put32(0x4493e0,0); CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_LOADING && !frame.car);
    fixture(); put32(0x3cc470,7); CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_OTHER_RACE && frame.config_size);
    fixture(); put32(PLAYER+0x10,1); CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_BAD_CAR);
    fixture(); put32(0x428e34,3); reads=0;
    CHECK(xyz_xbox_sample(read_memory,0,1,&frame)==XYZ_XBOX_FRONTEND && reads==1 && !frame.world);
    if(argc==2) {
        uint8_t captured[RC_CFG_MAX+1],encoded[RC_CFG_MAX];
        FILE *file=fopen(argv[1],"rb"); CHECK(file);
        size_t n=fread(captured,1,sizeof captured,file); CHECK(!ferror(file)); fclose(file);
        CHECK(n && n<=RC_CFG_MAX && rc_carcfg_decode(captured,(int)n,&cfg)==RC_OK);
        int size=rc_carcfg_encode(&cfg,encoded,sizeof encoded);
        CHECK(size==(int)n && !memcmp(captured,encoded,n));
        printf("Runtime capture: type=%s hash=%08x parts=%u performance=%u bytes=%zu; exact round-trip passed\n",
               cfg.type_name,cfg.type_hash,cfg.n_parts,cfg.has_perf,n);
    } else CHECK(argc==1);
    puts("Xbox vehicle: wire round-trip, tuning, ownership, transitions and malformed-memory checks passed");
    return 0;
}
