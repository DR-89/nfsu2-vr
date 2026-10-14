#include "quest_culling.h"
#include <stdio.h>

static int failures;
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); ++failures; } } while(0)

static int contains(const float planes[6][4], const float point[3])
{
    for (int p=0;p<6;++p)
        if (planes[p][0]*point[0]+planes[p][1]*point[1]+planes[p][2]*point[2]+planes[p][3]<-0.01f) return 0;
    return 1;
}

/* Independent physical camera construction: basis columns are right/up/back.
 * Camera position and sampled view rays are transformed without unprojection. */
static void eye_matrix(float clip[16], float basis[3][3], const float position[3],
                       float yaw, float pitch, float skew)
{
    float sy=sinf(yaw),cy=cosf(yaw),sp=sinf(pitch),cp=cosf(pitch);
    float axes[3][3]={{cy,0,-sy},{sy*sp,cp,cy*sp},{sy*cp,-sp,cy*cp}};
    memcpy(basis,axes,sizeof axes);
    float v[16]={0},p[16]={0};
    for (int r=0;r<3;++r) {
        for (int c=0;c<3;++c) v[c*4+r]=basis[r][c];
        for (int c=0;c<3;++c) v[12+r]-=basis[r][c]*position[c];
    }
    v[15]=1;
    p[0]=1; p[5]=-1.1f; p[8]=skew; p[9]=-.1f;
    p[10]=-10000.0f/9999.5f; p[11]=-1; p[14]=-5000.0f/9999.5f;
    memset(clip,0,64);
    for (int c=0;c<4;++c) for (int r=0;r<4;++r) for (int k=0;k<4;++k)
        clip[c*4+r]+=p[k*4+r]*v[c*4+k];
}

int main(void)
{
    const float game_view[16]={0,0,1,-30, 0,1,0,-20, -1,0,0,10, 0,0,0,1};
    float eyes[2][16],basis[2][3][3],planes[6][4],world[6][4];
    const float positions[2][3]={{-.032f,.2f,-.1f},{.032f,.2f,-.1f}};
    const float yaw_values[]={0,.9f,2.1f,-1.57f},pitch_values[]={0,.8f,-.8f};
    for (unsigned y=0;y<4;++y) for (unsigned p=0;p<3;++p) {
        for (int e=0;e<2;++e) eye_matrix(eyes[e],basis[e],positions[e],yaw_values[y]+e*.015f,pitch_values[p],e?.15f:-.15f);
        REQUIRE(quest_culling_stereo(planes,eyes[0],eyes[1],.25f));
        REQUIRE(quest_culling_world(world,planes,game_view));
        for (int e=0;e<2;++e) for (int corner=0;corner<8;++corner) {
            float distance=corner&4?9000:0.5f;
            float skew=e?.15f:-.15f;
            float ray[3]={((corner&1?1.3f:-1.3f)+skew)*distance,
                          -((corner&2?1.3f:-1.3f)-.1f)*distance/1.1f,-distance};
            float point[3];
            for (int r=0;r<3;++r) {
                point[r]=positions[e][r];
                for (int c=0;c<3;++c) point[r]+=basis[e][c][r]*ray[c];
            }
            point[1]=-point[1]; point[2]=-point[2]; /* XR up/back -> guest down/forward */
            REQUIRE(contains(planes,point));
            REQUIRE(quest_culling_sphere(planes,point,0));
            float absolute[3]={10-point[2],20+point[1],30+point[0]};
            REQUIRE(contains(world,absolute));
            REQUIRE(quest_culling_sphere(world,absolute,0));
        }
    }
    const float origin[3]={0};
    eye_matrix(eyes[0],basis[0],origin,0,0,0);
    REQUIRE(quest_culling_stereo(planes,eyes[0],eyes[0],.25f));
    REQUIRE(contains(planes,(float[3]){8,0,10})); /* outside narrow original game FOV */
    REQUIRE(!contains(planes,(float[3]){0,0,-2}));
    REQUIRE(!contains(planes,(float[3]){0,0,12000}));
    REQUIRE(!contains(planes,(float[3]){100,0,10}));
    /* Retail camera rows use +Y down. Looking up must retain overhead world
     * points and reject those below; negating only Z reverses this test. */
    eye_matrix(eyes[0],basis[0],origin,0,.8f,0);
    REQUIRE(quest_culling_stereo(planes,eyes[0],eyes[0],.25f));
    const float down_view[16]={1,0,0,0, 0,-1,0,0, 0,0,1,0, 0,0,0,1};
    REQUIRE(quest_culling_world(world,planes,down_view));
    REQUIRE(contains(world,(float[3]){0,20,2}));
    REQUIRE(!contains(world,(float[3]){0,-20,2}));
    REQUIRE(!quest_culling_stereo(planes,eyes[0],(float[16]){0},.25f));
    REQUIRE(!quest_culling_stereo(planes,eyes[0],eyes[0],NAN));
    REQUIRE(!quest_culling_world(world,planes,(float[16]){0}));
    /* Regression: looking past the original camera's side reveals objects
     * in its rear hemisphere. The later retail LOD gate rejects these even
     * after the tracked box test accepted them. */
    eye_matrix(eyes[0],basis[0],origin,2.1f,0,0);
    REQUIRE(quest_culling_stereo(planes,eyes[0],eyes[0],.25f));
    float rear[3]={-50*sinf(2.1f),0,50*cosf(2.1f)};
    REQUIRE(rear[2] < -2); /* rejected by retail forward dot < -radius */
    REQUIRE(quest_culling_sphere(planes,rear,2));
    REQUIRE(!quest_culling_sphere(planes,(float[3]){-rear[0],0,-rear[2]},2));
    REQUIRE(!quest_culling_sphere(planes,rear,-1));
    REQUIRE(!quest_culling_sphere(planes,rear,NAN));
    REQUIRE(!quest_culling_sphere(planes,(float[3]){NAN,0,0},2));
    REQUIRE(!quest_culling_sphere((float[6][4]){{0}},rear,2));
    /* Intersecting a plane is visible even if the centre is just outside.
     * Scaling planes must not alter that classification. */
    float cube[6][4]={{1,0,0,10},{-1,0,0,10},{0,1,0,10},
                      {0,-1,0,10},{0,0,1,10},{0,0,-1,10}};
    REQUIRE(quest_culling_sphere(cube,(float[3]){12,0,0},2));
    REQUIRE(!quest_culling_sphere(cube,(float[3]){12.01f,0,0},2));
    for (int p=0;p<6;++p) for (int c=0;c<4;++c) cube[p][c]*=3;
    REQUIRE(quest_culling_sphere(cube,(float[3]){12,0,0},2));
    REQUIRE(!quest_culling_sphere(cube,(float[3]){12.01f,0,0},2));
    printf("Quest stereo culling checks: %d failures\n",failures);
    return failures?1:0;
}
