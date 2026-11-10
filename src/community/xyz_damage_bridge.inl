/* Audited Xbox game boundary. The C++ damage core never sees native register
 * state, and all additional game calls restore the complete register file. */
#include "xyz_damage.h"
#include "xyz_damage_arena.h"
static uint32_t community_damage_render,community_damage_forced;
static int community_damage_ready,community_damage_requested=-1;
static recomp_func_t community_damage_function;
static float *community_damage_float;
static void community_damage_thunk(void){
    community_damage_function();
    if(community_damage_float)*community_damage_float=(float)g_fp_stack[g_fp_top&7];
}
static void *community_damage_map(uint32_t a){return a?(void*)XBOX_PTR(a):NULL;}
static uint32_t community_damage_address(const void *p){return p?(uint32_t)((uintptr_t)p-g_xbox_mem_offset):0;}
static int community_damage_valid(uint32_t a,uint32_t n){return xyz_xbox_address(a,n);}
static uint32_t community_damage_alloc(uint32_t n){
    uint32_t a=xyz_damage_arena_alloc(n);
    if(a && xyz_xbox_address(a,n))memset((void*)XBOX_PTR(a),0,n);
    else if(a){xyz_damage_arena_free(a);a=0;}
    return a;
}
static uint32_t community_damage_call(uint32_t fn,uint32_t self,int count,const uint32_t *args,float *value){
    static const struct {uint32_t fn;int count;} allowed[]={
        {0xA1580,3},{0xA0B00,1},{0x181890,3},{0x219760,1},{0x9C860,0},
        {0x1D13F0,0},{0x1D14A0,0},{0x1D6E70,2},{0x1D7890,4},{0x1D8440,5}
    };
    unsigned i=0;for(;i<sizeof allowed/sizeof *allowed;i++)if(allowed[i].fn==fn && allowed[i].count==count)break;
    if(i==sizeof allowed/sizeof *allowed)return 0;
    recomp_func_t function=recomp_lookup_manual(fn);if(!function)function=recomp_lookup(fn);
    if(!function)return 0;
    recomp_func_t before=community_damage_function;float *before_float=community_damage_float;
    community_damage_function=function;community_damage_float=value;
    uint32_t result=guest_call_full(community_damage_thunk,self,0,0x18ab7f,(unsigned)count,args);
    community_damage_function=before;community_damage_float=before_float;return result;
}
/* Isolated development fixture: original AI drives an offline Quick Race.
 * Three labelled visual impacts exercise private geometry without changing
 * physics or the result. Never enabled without scripted test input. */
static void community_damage_probe(void){
    static int requested=-1,driving,hits,repair,reported=-1,debris_test;static uint64_t start;static uint32_t scratch;
    if(requested<0){const char *e=getenv("NFSU2_DAMAGE_PROBE"),*p=getenv("RECOMP_PAD_SCRIPT"),*d=getenv("NFSU2_DAMAGE_DEBRIS_PROBE");requested=e && *e=='1' && p && *p;debris_test=d && *d=='1';}
    if(!requested || MEM32(0x428e34)!=6 || MEM32(0x3cc470)!=1 || MEM32(0x449478)==0 || MEM32(0x41c338)!=2)return;
    uint32_t player=MEM32(0x44940c),world=MEM32(0x4493e0);
    uint32_t car=xyz_xbox_address(player,0x238)?MEM32(player+4):0;
    if(!car || xyz_xbox_find_car(cockpit_ui_read,NULL,world,car,0)!=car)return;
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);uint64_t now=(uint64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
    if(!start)start=now;unsigned elapsed=(unsigned)(now-start)/1000;
    if(!driving && elapsed>=5 && MEM32(MEM32(0x449478))==4){guest_call_full(recomp_lookup(0x1eb840),player,0,0x1eb840,0,NULL);driving=1;fprintf(stderr,"[VR damage probe] original offline autopilot enabled\n");}
    if(hits<(debris_test?13:3) && elapsed>=(hits<3?15u+(unsigned)hits*10u:45u+((unsigned)hits-3)*3u)){
        uint32_t body=MEM32(car+0x3c),rigid=xyz_xbox_address(body,0x134)?MEM32(body+0x68):0;
        if(xyz_xbox_address(rigid,0x200) && MEM32(body+0x130)==car){
            if(!scratch)scratch=community_damage_alloc(32);
            if(scratch){
                float local[3]={2.0f,0.0f,0.6f};
                if(hits==1){local[0]=0.4f;local[1]=0.9f;}
                if(hits==2){local[0]=-2.0f;local[1]=0;}
                if(hits>=3){local[1]=(hits&1)?.5f:-.5f;local[2]=.35f;}
                for(unsigned a=0;a<3;a++){
                    float p=0;for(unsigned k=0;k<3;k++)p+=(local[k]-MEMF(rigid+0x1f0+k*4))*MEMF(rigid+0x30+k*16+a*4);
                    MEMF(scratch+16+a*4)=p;
                    MEMF(scratch+a*4)=(hits==2?100.0f:-100.0f)*MEMF(rigid+0x30+(hits==1?16:0)+a*4);
                }
                dmg_impulse(body,scratch,scratch+16);++hits;
                fprintf(stderr,"[VR damage probe] synthetic visual impact %d car=%08x\n",hits,car);
            }
        }
    }
    if(!repair && elapsed>=(debris_test?100u:65u) && xyz_xbox_address(MEM32(car+0x958),4)){
        dmg_zero(MEM32(car+0x958),0);repair=1;fprintf(stderr,"[VR damage probe] synthetic repair requested\n");
    }
    int period=(int)(elapsed/5);if(period!=reported){reported=period;XyzDamageStats st;xyz_damage_stats(&st);
        fprintf(stderr,"[VR damage probe] t=%u clones=%u retired=%u cars=%u failures=%u hits=%d repair=%d drawn=%u overlays=%u loose=%u fallen=%u debris=%u started=%u ground=%u/%u wall=%u/%u\n",elapsed,st.clones,st.retired,st.cars,st.failures,hits,repair,st.drawn,st.overlays,st.loose,st.fallen,st.debris,st.debris_started,st.ground_found,st.ground_queries,st.wall_found,st.wall_queries);}
}
static void community_damage_frame(void){
    if(community_damage_requested<0)community_damage_requested=quest_xr_crash_damage();
    if(!community_damage_requested)return;
    if(!community_damage_ready){
        xyz_damage_arena_init(xbox_ContiguousAlloc);
        XyzDamageHost host={community_damage_map,community_damage_address,community_damage_valid,community_damage_alloc,
            xyz_damage_arena_free,xyz_damage_arena_available,community_damage_call,NULL};
        community_damage_ready=xyz_damage_start(&host,
#ifdef __ANDROID__
            1
#else
            0
#endif
        );
    }
    if(community_damage_ready){
        int running=MEM32(0x428e34)==6 && !quest_cockpit_ui_paused(cockpit_ui_read,NULL);
        xyz_damage_frame(running?MEMF(0x428a0c):0);
        community_damage_probe();
    }
}
