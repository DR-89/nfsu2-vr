#include "../android/runtime/quest_copchase_scene.h"
#include "../android/runtime/quest_cockpit.h"
#include <assert.h>
#include <stdlib.h>
int main(void)
{
    QuestCopChaseVertex *v=calloc(QUEST_COPCHASE_SCENE_VERTICES+2,sizeof *v);assert(v);
    QuestCopChaseScene s={0};s.active=1;s.forward[0]=s.left[1]=1;s.half_width=4;
    const float eye[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    for(int angle=0;angle<24;++angle){
        float a=angle*.2617993878f,c=cosf(a),q=sinf(a);
        s.forward[0]=c;s.forward[1]=q;s.left[0]=-q;s.left[1]=c;
        s.position[0]=1200;s.position[1]=-730;s.position[2]=45;
        v[QUEST_COPCHASE_SCENE_VERTICES].color[0]=1234;
        unsigned n=quest_copchase_scene_mesh(v,&s);assert(n>18 && n<QUEST_COPCHASE_SCENE_VERTICES && n%3==0);
        assert(v[QUEST_COPCHASE_SCENE_VERTICES].color[0]==1234);
        for(unsigned i=0;i<n;++i){float dx=v[i].position[0]-s.position[0],dy=v[i].position[1]-s.position[1];
            assert(fabsf(dx*c+dy*q)<.141f);assert(fabsf(-dx*q+dy*c)<4.181f);
            assert(v[i].position[2]>=45 && v[i].position[2]<=45.151f);
        }
        float rotation[12]={c,q,0,0,-q,c,0,0,0,0,1,0},seat[3]={0},view[16],matrix[16];
        assert(quest_cockpit_view(view,rotation,s.position,seat));assert(quest_copchase_scene_matrix(matrix,eye,view));
        /* A point 10 m ahead, 2 m left, 1 m up lands left/up/forward. */
        float point[4]={s.position[0]+10*c-2*q,s.position[1]+10*q+2*c,46,1},p[4]={0};
        for(int r=0;r<4;++r)for(int k=0;k<4;++k)p[r]+=matrix[k*4+r]*point[k];
        assert(fabsf(p[0]+2)<.001f && fabsf(p[1]-1)<.001f && fabsf(p[2]+10)<.001f && p[3]==1);
    }
    s.half_width=16;assert(quest_copchase_scene_mesh(v,&s)<QUEST_COPCHASE_SCENE_VERTICES);
    s.half_width=17;assert(!quest_copchase_scene_mesh(v,&s));s.half_width=NAN;assert(!quest_copchase_scene_mesh(v,&s));
    s.half_width=4;s.forward[0]=NAN;assert(!quest_copchase_scene_mesh(v,&s));
    s.forward[0]=s.left[0]=1;s.forward[1]=s.left[1]=0;assert(!quest_copchase_scene_mesh(v,&s));
    float bad[16],out[16];memcpy(bad,eye,sizeof bad);bad[0]=NAN;assert(!quest_copchase_scene_matrix(out,eye,bad));
    memcpy(bad,eye,sizeof bad);bad[15]=0;assert(!quest_copchase_scene_matrix(out,eye,bad));
    free(v);return 0;
}
