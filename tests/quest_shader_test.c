#include "gl_vsh.h"
#include "gl_psh.h"
#include <stdio.h>
#include <string.h>

static int failed;
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); ++failed; } } while(0)

static void check_paired_clip_completion(void)
{
    /* World shader captured on PCVR: c96/oPos.x is written by the same
     * instruction that reads oPos.w for RCC. The other three rows precede it.
     * RCC writes R1.x, so the completed position is safe AFTER this pair. */
    uint32_t program[7][4]={
        {0,0x00ec201b,0x08363800,0x20b04800},
        {0,0x00ec401b,0x08365800,0x20b02800},
        {0,0x00ec601b,0x08367800,0x20b01800},
        {0,0x006f001a,0x0800146a,0x3ea00000},
        {0,0x06ec001b,0x08361bff,0x10b88800},
        {0,0x0054001b,0xc436186c,0x2070e800},
        {0,0x0094201b,0xc400286c,0x3070e801}
    };
    char source[16384];
    nv2a_shader_vk=1;
    REQUIRE(nv2a_gl_vsh_program(program,7,0,source,sizeof source)>0);
    const char *pair=strstr(source,"{ /* 4 */");
    const char *write=pair?strstr(pair,"oPos.x = (m).x;"):NULL;
    const char *capture=strstr(source,"vrPosition = oPos;");
    const char *viewport=strstr(source,"{ /* 5 */");
    REQUIRE(write && capture && viewport && write<capture && capture<viewport);
    REQUIRE(capture && !strstr(capture+1,"vrPosition = oPos;"));

    program[0][3]=0; /* Missing y: do not capture an incomplete position. */
    REQUIRE(nv2a_gl_vsh_program(program,7,0,source,sizeof source)>0);
    REQUIRE(!strstr(source,"vrPosition = oPos;"));
    program[0][3]=0x20b04800;
    program[4][2]=0x08361bf7; /* Reciprocal of R4, not clip w. */
    REQUIRE(nv2a_gl_vsh_program(program,7,0,source,sizeof source)>0);
    REQUIRE(!strstr(source,"vrPosition = oPos;"));
    program[4][2]=0x08361bff;
    /* MAC writes x through R12, but ILU also overwrites oPos via output. */
    program[4][3]=0x18c88804;
    REQUIRE(nv2a_gl_vsh_program(program,7,0,source,sizeof source)>0);
    REQUIRE(!strstr(source,"vrPosition = oPos;"));
    program[4][3]=0x10b88800;
    nv2a_shader_vk=0;
    REQUIRE(nv2a_gl_vsh_program(program,7,0,source,sizeof source)>0);
    REQUIRE(!strstr(source,"vrPosition"));
}

int main(int argc, char **argv)
{
    check_paired_clip_completion();
    /* Clip writes followed by a paired DP4/RCC: sources must be captured
     * before either half writes. The viewport code still needs to execute. */
    uint32_t program[7][4]={
        {0,0x00ec001b,0x0836186c,0x20708800},
        {0,0x00ec201b,0x0836186c,0x20704800},
        {0,0x00ec401b,0x0836186c,0x20702800},
        {0,0x00ec601b,0x0836186c,0x20701800},
        {0,0x06ee801b,0x08361bff,0x18280ff8},
        {0,0x0054001b,0xc436186c,0x2070e800},
        {0,0x0094201b,0xc400286c,0x3070e801}
    };
    char source[16384];
    nv2a_shader_vk=1;
    REQUIRE(nv2a_gl_vsh_program(program,7,0,source,sizeof source)>0);
    char *capture=strstr(source,"vrPosition = oPos;");
    char *divide=strstr(source,"l = vec4(nv2a_rcc");
    REQUIRE(capture && divide && capture<divide);
    REQUIRE(capture && !strstr(capture+1,"vrPosition = oPos;"));
    REQUIRE(strstr(source,"c[nv2a_ib + 160]")!=NULL);
    REQUIRE(strstr(nv2a_gl_vsh_main_program(),"nv2a_vr_position(vrPosition)")!=NULL);
    REQUIRE(strstr(nv2a_gl_vsh_main_program(),"u_vr_reflection")!=NULL);
    REQUIRE(strstr(nv2a_gl_vsh_prelude(),"constant_id = 2")!=NULL);
    REQUIRE(strstr(nv2a_gl_vsh_prelude(),"gl_ViewIndex == 0 ? u_vr : u_vr_right")!=NULL);
    if(argc>2) {
        FILE *f=fopen(argv[2],"wb");
        REQUIRE(f!=NULL);
        if(f) {
            const char *prelude=nv2a_gl_vsh_prelude(), *body=strchr(prelude,'\n');
            REQUIRE(body!=NULL);
            if(body) {
                fwrite(prelude,1,(size_t)(body+1-prelude),f);
                fputs(argc>3?"#define NV2A_VK_MULTIVIEW 1\n#define NV2A_VK_SINGLE_INSTANCE 1\n":
                    "#define NV2A_VK_INSTANCE_SPECIALIZE 1\n",f);
                fputs(body+1,f); fputs(source,f); fputs(nv2a_gl_vsh_main_program(),f);
            }
            fclose(f);
        }
    }
    /* A fog reciprocal or an incomplete position is not a clip boundary. */
    program[4][2]=0x08361bf7; /* C now R4 rather than oPos */
    REQUIRE(nv2a_gl_vsh_program(program,7,0,source,sizeof source)>0);
    REQUIRE(!strstr(source,"vrPosition = oPos;"));
    program[4][2]=0x08361bff;
    program[2][3]=0;
    REQUIRE(nv2a_gl_vsh_program(program,7,0,source,sizeof source)>0);
    REQUIRE(!strstr(source,"vrPosition = oPos;"));
    program[2][3]=0x20702800;
    nv2a_shader_vk=0;
    REQUIRE(nv2a_gl_vsh_program(program,7,0,source,sizeof source)>0);
    REQUIRE(!strstr(source,"vrPosition"));
    REQUIRE(!strstr(nv2a_gl_vsh_prelude(),"vrPosition"));
    REQUIRE(!strstr(nv2a_gl_vsh_prelude(),"constant_id"));
    REQUIRE(!strstr(nv2a_gl_vsh_prelude(),"gl_ViewIndex"));
    REQUIRE(!strstr(nv2a_gl_vsh_main_program(),"u_vr_reflection"));
    REQUIRE(strstr(nv2a_gl_vsh_main_program(),"gl_Position = nv2a_clip(s * w, w);")!=NULL);
    Nv2aPshKey pk={0};
    if(argc>3) pk.shader_program=1|(15u<<5)|(16u<<10)|(3u<<15);
    nv2a_shader_vk=1;
    REQUIRE(nv2a_gl_psh(&pk,source,sizeof source)>0);
    REQUIRE(strstr(source,"constant_id = 0")!=NULL);
    REQUIRE(strstr(source,"constant_id = 1")!=NULL);
    REQUIRE(strstr(source,"if (nv2a_alpha_test)")!=NULL);
    REQUIRE(strstr(source,"nv2a_hud_clip &&")!=NULL);
    REQUIRE(!strstr(source,"early_fragment_tests"));
    if(argc>1) {
        FILE *f=fopen(argv[1],"wb");
        REQUIRE(f!=NULL);
        if(f) {
            const char *body=strchr(source,'\n');
            if(argc>3 && body) {
                fwrite(source,1,(size_t)(body+1-source),f);
                fputs("#define NV2A_VK_MULTIVIEW 1\n",f);
                fputs(body+1,f);
            } else REQUIRE(fwrite(source,1,strlen(source),f)==strlen(source));
            fclose(f);
        }
    }
    /* Texture shader clip planes must still discard even when opaque/HUD
     * checks are specialized away. GL must keep its original uniform path. */
    pk.shader_program=5;
    REQUIRE(nv2a_gl_psh(&pk,source,sizeof source)>0);
    REQUIRE(strstr(source,"if (any(lessThan(vT0, vec4(0.0)))) discard;")!=NULL);
    nv2a_shader_vk=0;
    REQUIRE(nv2a_gl_psh(&pk,source,sizeof source)>0);
    REQUIRE(!strstr(source,"constant_id"));
    REQUIRE(!strstr(source,"nv2a_alpha_test"));
    REQUIRE(!strstr(source,"nv2a_hud_clip"));
    REQUIRE(strstr(source,"if (!pass) discard;")!=NULL);
    printf("Quest Vulkan shader checks: %d failures\n",failed);
    return failed?1:0;
}
