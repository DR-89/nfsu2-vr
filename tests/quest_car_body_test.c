/* Private-fixture regression: compile the real lifted point/box routine and
 * the real manual wrapper extracted by tools/audit-cockpit-cars.py. The test
 * checks car retention, unrelated callers, ABI and floating-point state.
 * It does not claim to render or visually inspect every car. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <stdio.h>
#include <stdlib.h>

ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_seh_ebp;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_cc,g_fp_control_word;
volatile int g_gil_contended;
void recomp_preempt(void) { abort(); }
static uint32_t cockpit_player_car;
static int active;
static int quest_xr_cockpit_active(void) { return active; }
#include "cockpit_body_generated.inc"

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
typedef struct { char name[32];float low[3],high[3]; } CarBounds;
typedef struct { uint32_t ax,cx,dx,sp,bx,si,di;int top,cmp;uint16_t cc;double fp[8]; } Result;
static Result run(int wrapped,uint32_t caller,uint32_t low,uint32_t high,const float point[3])
{
    g_eax=0xface1234;g_ecx=0xdecafbad;g_edx=0xabadcafe;
    g_ebx=0x11223344;g_esi=0x22334455;g_edi=0x33445566;
    g_esp=0x30000;g_ebp=0x31000;g_seh_ebp=0x32000;
    g_fp_top=3;g_fp_cmp=-1;g_fp_cc=0x100;g_fp_control_word=0x37f;
    for(int i=0;i<8;++i)g_fp_stack[i]=i+.125;
    MEM32(g_esp)=caller;MEM32(g_esp+4)=low;MEM32(g_esp+8)=high;
    MEM32(g_esp+12)=0x21000;MEMF(g_esp+16)=0;
    memcpy((void *)XBOX_PTR(0x21000),point,12);
    if(wrapped)sub_0004CA40();else sub_0004CA40_gen();
    Result r={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_fp_top,g_fp_cmp,g_fp_cc};
    memcpy(r.fp,g_fp_stack,sizeof r.fp);return r;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);FILE *f=fopen(argv[1],"rb");CHECK(f);
    uint32_t header[2];CHECK(fread(header,sizeof header,1,f)==1 && header[0]==0x4e434252 && header[1]>0 && header[1]<100);
    unsigned char *memory=calloc(1,0x500000);CHECK(memory);g_xbox_mem_offset=(ptrdiff_t)memory;
    unsigned checks=0,rescued=0;
    for(unsigned car=0;car<header[1];++car) {
        CarBounds b;CHECK(fread(&b,sizeof b,1,f)==1 && b.name[31]==0);
        uint32_t owner=0x10000+car*0x1000,info=0x80000+car*0x4000;
        MEM32(owner+0x554)=info;
        memcpy((void *)XBOX_PTR(info+0x10),b.low,12);memcpy((void *)XBOX_PTR(info+0x20),b.high,12);
        memcpy((void *)XBOX_PTR(info+0x110),b.low,12);memcpy((void *)XBOX_PTR(info+0x120),b.high,12);
        for(int point_case=0;point_case<4;++point_case) {
            float point[3]={-.1f,.35f,1.45f};
            if(point_case==1)for(int i=0;i<3;++i)point[i]=(b.low[i]+b.high[i])*.5f;
            if(point_case==2)for(int i=0;i<3;++i)point[i]=b.low[i];
            if(point_case==3)point[0]=b.high[0]+1;
            for(int context=0;context<7;++context) {
                cockpit_player_car=context==5?0:owner;active=context!=2;
                MEM32(0x428e34)=context==3?3:6;
                MEM32(owner+0x554)=context==6?0:info;
                uint32_t caller=context==1?0x123456:0x20c282;
                uint32_t low=info+(context==4?0x110:0x10),high=low+0x10;
                Result reference=run(0,caller,low,high,point),actual=run(1,caller,low,high,point);
                unsigned expected=context==0?0:reference.ax;
                CHECK(actual.ax==expected);
                CHECK(actual.cx==reference.cx && actual.dx==reference.dx && actual.sp==0x30004);
                CHECK(actual.bx==reference.bx && actual.si==reference.si && actual.di==reference.di);
                CHECK(actual.top==reference.top && actual.cmp==reference.cmp && actual.cc==reference.cc);
                CHECK(!memcmp(actual.fp,reference.fp,sizeof actual.fp));
                if(context==0 && point_case==0 && reference.ax)++rescued;
                ++checks;
            }
        }
    }
    CHECK(fgetc(f)==EOF);fclose(f);free(memory);
    printf("PASS: %u vehicle bounds, %u real lifted/wrapper comparisons; %u original fixed-seat exclusions corrected. Other callers/cars/states and ABI unchanged.\n",header[1],checks,rescued);
    return 0;
}
