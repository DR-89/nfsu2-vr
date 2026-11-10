#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/vr_community_mods.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)

static int retail(const char *path)
{
    FILE *f=fopen(path,"rb");CHECK(f);
    CHECK(fseek(f,0,SEEK_END)==0);long n=ftell(f);CHECK(n>0 && n<=131072);rewind(f);
    uint8_t *original=malloc((size_t)n),*patched=malloc((size_t)n),*scratch=malloc((size_t)n);
    CHECK(original && patched && scratch);
    CHECK(fread(original,1,(size_t)n,f)==(size_t)n);CHECK(fclose(f)==0);
    memcpy(patched,original,(size_t)n);
    CHECK(vr_mod_hidden_races(patched,(size_t)n,0)==0 && !memcmp(patched,original,(size_t)n));
    CHECK(vr_mod_hidden_races(patched,(size_t)n,1)==1);
    size_t races=0,stages=0;
    for(size_t o=8;o<(size_t)n;o+=8+vr_mod_u32(original+o+4)) {
        if(vr_mod_u32(original+o)==0x34a11u)races=o+8;
        if(vr_mod_u32(original+o)==0x34a18u)stages=o+8;
    }
    CHECK(races && stages);
    unsigned hidden=0,changed=0;
    for(unsigned i=0;i<224;++i) {
        hidden+=original[races+i*136+127]!=0;
        CHECK(patched[races+i*136+127]==0);
    }
    CHECK(hidden==33);
    for(unsigned i=0;i<6;++i)for(unsigned k=48;k<53;++k)CHECK(patched[stages+i*80+k]==8);
    for(size_t i=0;i<(size_t)n;++i)if(original[i]!=patched[i]) {
        int race=i>=races && i<races+224*136 && (i-races)%136==127;
        int stage=i>=stages && i<stages+6*80 && (i-stages)%80>=48 && (i-stages)%80<53;
        CHECK(race || stage);++changed;
    }
    CHECK(changed==63);
    memcpy(scratch,patched,(size_t)n);
    CHECK(vr_mod_hidden_races(patched,(size_t)n,1)==2 && !memcmp(patched,scratch,(size_t)n));
    /* A changed stage must reject the whole operation, including race writes. */
    memcpy(patched,original,(size_t)n);patched[stages]^=1;memcpy(scratch,patched,(size_t)n);
    CHECK(vr_mod_hidden_races(patched,(size_t)n,1)==-1 && !memcmp(patched,scratch,(size_t)n));
    /* Truncated buffers must leave the complete supplied buffer untouched. */
    for(size_t bytes=0;bytes<(size_t)n;++bytes)
        CHECK(vr_mod_hidden_races(original,bytes,1)==-1);
    memcpy(patched,original,(size_t)n);
    memset(patched+stages-4,0xff,4);memcpy(scratch,patched,(size_t)n);
    CHECK(vr_mod_hidden_races(patched,(size_t)n,1)==-1 && !memcmp(patched,scratch,(size_t)n));
    free(original);free(patched);free(scratch);
    puts("Retail career: exactly 63 intended bytes changed; malformed/modified input preserved.");
    return 0;
}
int main(int argc,char **argv)
{
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},beam[16];
    CHECK(vr_mod_high_beam(identity,identity,beam));
    CHECK(fabsf(beam[5]-.9961947f)<1e-6f && fabsf(beam[6]-.08715574f)<1e-6f && fabsf(beam[9]+.08715574f)<1e-6f);
    CHECK(beam[0]==1 && beam[15]==1 && fabsf(beam[5]*beam[5]+beam[6]*beam[6]-1)<1e-6f);
    float invalid[16];memcpy(invalid,identity,sizeof invalid);invalid[2]=NAN;
    float previous[16];memcpy(previous,beam,sizeof beam);
    CHECK(!vr_mod_high_beam(invalid,identity,beam) && !memcmp(previous,beam,sizeof beam));
    CHECK(vr_mod_qr_top(0,4014,1)==5 && vr_mod_qr_top(2,4401,1)==3 && vr_mod_qr_top(2,4402,1)==2);
    CHECK(vr_mod_qr_top(4,4701,2)==4 && vr_mod_qr_top(2,4402,4)==0);
    CHECK(vr_mod_qr_step(1,1,127,0)==127 && vr_mod_qr_step(127,1,127,1)==1 && vr_mod_qr_step(10,1,127,1)==11);
    CHECK(vr_mod_qr_step(0,1,5,0)==5 && vr_mod_qr_step(5,0,5,1)==0);
    float pace=1.3f,boost=1.3f;
    CHECK(!vr_mod_catchup(0,1,0,&pace,&boost) && pace==1.3f && boost==1.3f);
    CHECK(!vr_mod_catchup(2,0,0,&pace,&boost) && !vr_mod_catchup(2,1,1,&pace,&boost));
    CHECK(vr_mod_catchup(1,1,0,&pace,&boost) && pace==1.f && boost==1.f);
    pace=.8f;CHECK(!vr_mod_catchup(1,1,0,&pace,&boost) && pace==.8f);
    CHECK(vr_mod_catchup(2,1,0,&pace,&boost) && pace==1.f);
    pace=NAN;boost=1.2f;CHECK(!vr_mod_catchup(2,1,0,&pace,&boost) && isnan(pace) && boost==1.2f);
    CHECK(vr_mod_visual_rating(-1)==-1.f && vr_mod_visual_rating(7)==-1.f);
    CHECK(vr_mod_visual_rating(INT32_MIN)==-1.f && vr_mod_visual_rating(INT32_MAX)==-1.f);
    CHECK(vr_mod_visual_rating(0)==0.f && vr_mod_visual_rating(1)==1.f && vr_mod_visual_rating(2)==3.f);
    CHECK(vr_mod_visual_rating(3)==5.5f && vr_mod_visual_rating(4)==8.f && vr_mod_visual_rating(5)==10.f && vr_mod_visual_rating(6)==10.f);
    CHECK(vr_mod_hidden_races(NULL,0,0)==0);
    CHECK(vr_mod_hidden_races(NULL,0,1)==-1);
    uint8_t bad[16]={0x10,0x4a,0x03,0x80,8,0,0,0,0x11,0x4a,3,0,0xff,0xff,0xff,0xff};
    uint8_t before[sizeof bad];memcpy(before,bad,sizeof bad);
    CHECK(vr_mod_hidden_races(bad,sizeof bad,1)==-1 && !memcmp(bad,before,sizeof bad));
    for(int i=1;i<argc;++i)CHECK(retail(argv[i])==0);
    puts("Community mod checks passed.");return 0;
}
