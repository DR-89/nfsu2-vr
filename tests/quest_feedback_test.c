#include "quest_feedback.h"
#include <stdio.h>
static int failed;
#define REQUIRE(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); ++failed; } } while(0)
int main(void)
{
    float c[2][4]={{1,1,1,1},{0}}, v[16][4]={{2,3,0.5f,1},{9,8,7,1}}, out[4];
    uint32_t program[2][4]={
        {0,(1u<<21)|0x1b,2u<<26,(15u<<24)|(1u<<20)},
        /* Paired MOV v1 -> R1 and MOV old R1 -> oPos. */
        {0,(1u<<25)|(1u<<21)|(1u<<9)|0x1b,(2u<<26)|(0x1bu<<2),
            (1u<<30)|(1u<<28)|(15u<<24)|(1u<<20)|(15u<<12)|0x800|4|1}
    };
    REQUIRE(quest_feedback_position(program,2,c,2,v,out));
    REQUIRE(!memcmp(out,v[0],sizeof out));
    program[1][3]&=~1u;
    REQUIRE(!quest_feedback_position(program,2,c,2,v,out));
    program[1][3]|=1;
    program[1][3]&=~0x800u;
    REQUIRE(!quest_feedback_position(program,2,c,2,v,out));
    program[1][3]|=0x800;
    program[1][3]|=2;
    REQUIRE(!quest_feedback_position(program,2,c,2,v,out));
    program[1][3]&=~2u;
    program[0][1]|=4u<<21;
    REQUIRE(!quest_feedback_position(program,2,c,2,v,out));
    program[0][1]=(1u<<21)|0x1b;
    v[0][3]=-1;
    REQUIRE(!quest_feedback_position(program,2,c,2,v,out));
    v[0][3]=1; v[0][0]=NAN;
    REQUIRE(!quest_feedback_position(program,2,c,2,v,out));
    float p[3][4]={{0.53125f,0.53125f,0,1},{1280.53125f,0.53125f,0,1},{0.53125f,960.53125f,0,1}};
    REQUIRE(quest_feedback_covers(p,640,480,1680,1760));
    REQUIRE(quest_feedback_covers(p,640,480,640,480));
    float tmp[4]; memcpy(tmp,p[1],sizeof tmp); memcpy(p[1],p[2],sizeof tmp); memcpy(p[2],tmp,sizeof tmp);
    REQUIRE(quest_feedback_covers(p,640,480,1680,1760));
    p[1][1]=480.53125f; p[2][0]=640.53125f;
    REQUIRE(!quest_feedback_covers(p,640,480,1680,1760)); /* bbox covers, triangle doesn't */
    p[0][0]=NAN;
    REQUIRE(!quest_feedback_covers(p,640,480,1680,1760));
    printf("Quest feedback evaluator/coverage: %d failures\n",failed);
    return failed?1:0;
}
