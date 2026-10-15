#pragma once
#include <math.h>
#include <string.h>

/* Small immutable glove meshes, built once. They are deliberately translucent
 * and have no arms; no hand-tracking extension or per-frame mesh upload. */
#define QUEST_GHOST_MAX_VERTICES 8192
typedef struct { float position[3],normal[3],uv[2]; } QuestGhostVertex;
typedef struct { QuestGhostVertex vertices[QUEST_GHOST_MAX_VERTICES]; unsigned count; } QuestGhostMesh;
static inline void quest_ghost_ellipsoid(QuestGhostMesh *mesh,const float center[3],const float radius[3])
{
    const int rings=6,sides=10;
    for(int r=0;r<rings;++r) for(int s=0;s<sides;++s) {
        const int corners[6][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
        for(int i=0;i<6;++i) {
            float lat=-1.57079632679f+(r+corners[i][0])*3.14159265359f/rings;
            float lon=(s+corners[i][1])*6.28318530718f/sides;
            float n[3]={cosf(lat)*cosf(lon),sinf(lat),cosf(lat)*sinf(lon)};
            if(mesh->count>=QUEST_GHOST_MAX_VERTICES) return;
            QuestGhostVertex *v=&mesh->vertices[mesh->count++];
            float length=0;
            for(int k=0;k<3;++k) { v->position[k]=center[k]+n[k]*radius[k]; v->normal[k]=n[k]/radius[k]; length+=v->normal[k]*v->normal[k]; }
            length=sqrtf(length); for(int k=0;k<3;++k) v->normal[k]/=length;
        }
    }
}
static inline void quest_ghost_segment(QuestGhostMesh *mesh,const float a[3],const float b[3],float width)
{
    unsigned start=mesh->count;
    float dir[3],len=0,mid[3];
    for(int k=0;k<3;++k) { dir[k]=b[k]-a[k]; len+=dir[k]*dir[k]; mid[k]=(a[k]+b[k])*.5f; }
    len=sqrtf(len); for(int k=0;k<3;++k) dir[k]/=len;
    float right[3]={dir[1],-dir[0],0};
    float rl=sqrtf(right[0]*right[0]+right[1]*right[1]);
    if(rl<.001f) { right[0]=1; right[1]=0; } else { right[0]/=rl; right[1]/=rl; }
    float front[3]={right[1]*dir[2],-right[0]*dir[2],right[0]*dir[1]-right[1]*dir[0]};
    const float zero[3]={0,0,0},radii[3]={width,len*.5f+width*.5f,width};
    quest_ghost_ellipsoid(mesh,zero,radii);
    for(unsigned i=start;i<mesh->count;++i) {
        QuestGhostVertex old=mesh->vertices[i];
        for(int k=0;k<3;++k) {
            mesh->vertices[i].position[k]=mid[k]+right[k]*old.position[0]+dir[k]*old.position[1]+front[k]*old.position[2];
            mesh->vertices[i].normal[k]=right[k]*old.normal[0]+dir[k]*old.normal[1]+front[k]*old.normal[2];
        }
    }
}
static inline void quest_ghost_mesh(QuestGhostMesh *mesh,int closed)
{
    memset(mesh,0,sizeof *mesh);
    const float palm[3]={0,-.005f,.006f},size[3]={.038f,.046f,.017f};
    const float wrist[3]={0,-.055f,.006f},wrist_size[3]={.027f,.024f,.016f};
    quest_ghost_ellipsoid(mesh,palm,size); quest_ghost_ellipsoid(mesh,wrist,wrist_size);
    const float lengths[4]={.066f,.078f,.073f,.055f};
    for(int finger=0;finger<4;++finger) {
        float x=.027f-finger*.018f,l=lengths[finger];
        float a[3]={x,.026f,.006f};
        float b[3]={x,.026f+l*.53f,closed?-.012f:.002f};
        float c[3]={x,.026f+l*(closed?.45f:.94f),closed?-.047f:-.009f};
        quest_ghost_segment(mesh,a,b,.009f-finger*.0005f);
        quest_ghost_segment(mesh,b,c,.008f-finger*.0004f);
    }
    const float thumb_a[3]={.030f,-.012f,.006f};
    float thumb_b[3]={closed?.047f:.058f,closed?.015f:.011f,closed?-.012f:0};
    float thumb_c[3]={closed?.037f:.071f,closed?.032f:.039f,closed?-.038f:-.009f};
    quest_ghost_segment(mesh,thumb_a,thumb_b,.012f);
    quest_ghost_segment(mesh,thumb_b,thumb_c,.010f);
    /* Canonical right glove: viewed from its back, the thumb is on the left. */
    for(unsigned i=0;i<mesh->count;++i) {
        mesh->vertices[i].position[0]=-mesh->vertices[i].position[0];
        mesh->vertices[i].normal[0]=-mesh->vertices[i].normal[0];
    }
}
