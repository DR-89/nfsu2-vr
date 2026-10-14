#include "quest_projection.h"
#include <stdio.h>
#include <string.h>

static int failed;
#define REQUIRE(condition) do { if (!(condition)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#condition); ++failed; } } while(0)

int main(void)
{
    float scale[2], m[16]={0};
    float projection[12]={2,0,0,0, 0,-3,0,0, 0,0,1.0001f,-0.5f};
    REQUIRE(quest_projection_constants(projection,scale));
    REQUIRE(scale[0]==2 && scale[1]==3);
    float depth[4];
    REQUIRE(quest_projection_depth(projection,-0.5f,depth));
    /* Identity-camera reprojection must retain signed depth offsets, instead
     * of putting a decal at exactly the same depth as its underlying mesh. */
    for(int sign=-1;sign<=1;++sign) {
        float w=50, offset=sign*0.0005f;
        float guest=projection[10]*w+projection[11]+offset;
        float eye=projection[10]*w-0.5f;
        float corrected=eye+(guest-(depth[0]*w+depth[1]))*depth[2];
        REQUIRE(fabsf(corrected-guest)<1e-6f);
    }
    REQUIRE(quest_projection_depth(projection,-0.05f,depth));
    REQUIRE(fabsf(depth[2]-0.1f)<1e-6f);
    REQUIRE(!quest_projection_depth(projection,NAN,depth));
    REQUIRE(!quest_projection_depth(projection,0.5f,depth));
    projection[2]=1; REQUIRE(!quest_projection_constants(projection,scale));
    m[0]=2; m[5]=3; m[10]=1.01f; m[11]=-0.1f; m[14]=1;
    REQUIRE(quest_projection_scales(m,scale));
    REQUIRE(fabsf(scale[0]-2)<1e-5f && fabsf(scale[1]-3)<1e-5f);
    /* A translated, rotated, uniformly scaled object must retain its FOV. */
    float rotated[16]={1.2f,0,-1.6f,30, 0,3,0,-8, .808f,0,.606f,7, .8f,0,.6f,12};
    for(int i=0;i<16;++i) rotated[i]*=4;
    REQUIRE(quest_projection_scales(rotated,scale));
    REQUIRE(fabsf(scale[0]-2)<1e-5f && fabsf(scale[1]-3)<1e-5f);
    float ortho[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    REQUIRE(!quest_projection_scales(ortho,scale));
    m[2]=0.5f; REQUIRE(!quest_projection_scales(m,scale));
    m[2]=0; m[0]=NAN; REQUIRE(!quest_projection_scales(m,scale));
    uint32_t program[4][4]={{0}};
    for(unsigned i=0;i<4;++i) {
        program[i][1]=(7u<<21)|((96+i)<<13)|0x1b; /* DP4 c[96+i].xyzw */
        program[i][2]=(3u<<26)|(2u<<11); /* constant A, input B */
        program[i][3]=((8u>>i)<<12)|0x800; /* oPos component */
    }
    program[3][3]|=1;
    REQUIRE(quest_program_has_mvp(program,0,4));
    program[1][3]|=2; REQUIRE(!quest_program_has_mvp(program,0,4));
    program[1][3]&=~2u;
    program[2][3]=(8u<<12)|0x800; REQUIRE(!quest_program_has_mvp(program,0,4));
    memset(program,0,sizeof program); REQUIRE(!quest_program_has_mvp(program,0,4));
    uint32_t road[26][4]={{0}};
    road[23][1]=0x00544015; road[23][2]=0xc4aa186c; road[23][3]=0x2c400ff8;
    road[25][1]=0x00400015; road[25][2]=0x45ff886c; road[25][3]=0x2070c851;
    REQUIRE(quest_program_projects_reflection(road,0,26));
    REQUIRE(!quest_program_projects_reflection(road,0,25));
    REQUIRE(!quest_program_projects_reflection(road,30,26));
    road[25][3]^=0x8; REQUIRE(!quest_program_projects_reflection(road,0,26));
    printf("Quest camera projection checks: %d failures\n",failed);
    return failed?1:0;
}
