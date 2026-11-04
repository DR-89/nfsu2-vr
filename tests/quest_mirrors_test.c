#include "../android/runtime/quest_mirrors.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"mirror test failed at %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static void same(const float a[3],const float b[3])
{ for(int j=0;j<3;++j)CHECK(fabsf(a[j]-b[j])<.0001f); }

static void hand_pose(float pose[16],float yaw,float pitch,const float pos[3])
{
    memset(pose,0,64);pose[15]=1;
    for(int i=0;i<3;++i) {
        float axis[3]={0};axis[i]=1;
        quest_mirror_rotate(pose+4*i,axis,(QuestMirrorAngle){yaw,pitch},0);
    }
    memcpy(pose+12,pos,12);
}
static void grab_test(int hand,int part)
{
    QuestMirrorMount mounts[3]={
        {{.31f,.18f,-.37f},{.11f,.035f,.015f},1},{{-.52f,-.12f,-.46f},{.08f,.045f,.02f},1},
        {{1.22f,-.12f,-.46f},{.08f,.045f,.02f},1}};
    QuestMirrorSetting setting={0};QuestMirrorGrip grip={0};quest_mirror_release(&grip);
    int valid[2]={1,1},busy[2]={0,0};float squeeze[2]={0},pose[2][16]={{0}},pos[3];
    memcpy(pos,mounts[part].center,sizeof pos);pos[0]+=.055f;
    pos[2]+=.20f;hand_pose(pose[hand],0,0,pos);
    quest_mirror_grip(&grip,&setting,mounts,0,valid,pose,squeeze,busy);
    squeeze[hand]=1;quest_mirror_grip(&grip,&setting,mounts,10,valid,pose,squeeze,busy);CHECK(!grip.hand);
    pos[2]-=.20f;hand_pose(pose[hand],0,0,pos);busy[hand]=1;
    quest_mirror_grip(&grip,&setting,mounts,20,valid,pose,squeeze,busy);CHECK(!grip.hand);
    busy[hand]=0;quest_mirror_grip(&grip,&setting,mounts,30,valid,pose,squeeze,busy);
    CHECK(grip.hand==hand+1 && grip.part==part);
    hand_pose(pose[hand],.12f,.08f,pos);
    quest_mirror_grip(&grip,&setting,mounts,40,valid,pose,squeeze,busy);
    QuestMirrorAngle a=quest_mirror_angle(&setting,part);
    CHECK(a.yaw>.01f && a.yaw<.02f);
    for(uint32_t t=50;t<=450;t+=10)quest_mirror_grip(&grip,&setting,mounts,t,valid,pose,squeeze,busy);
    a=quest_mirror_angle(&setting,part);
    CHECK(a.yaw>.050f && a.yaw<.055f && a.pitch>.034f && a.pitch<.038f);
    if(part) {
        QuestMirrorAngle other=quest_mirror_angle(&setting,3-part);
        CHECK(fabsf(other.yaw+a.yaw)<1e-6f && fabsf(other.pitch-a.pitch)<1e-6f);
        CHECK(setting.inside.yaw==0 && setting.inside.pitch==0);
    } else CHECK(setting.outside.yaw==0 && setting.outside.pitch==0);
    /* Turning back to the initial pose restores exactly the initial aim. */
    hand_pose(pose[hand],0,0,pos);
    for(uint32_t t=460;t<=900;t+=10)quest_mirror_grip(&grip,&setting,mounts,t,valid,pose,squeeze,busy);
    a=quest_mirror_angle(&setting,part);CHECK(fabsf(a.yaw)+fabsf(a.pitch)<1e-6f);
    pos[2]-=.015f;hand_pose(pose[hand],0,0,pos);
    for(uint32_t t=910;t<=1300;t+=10)quest_mirror_grip(&grip,&setting,mounts,t,valid,pose,squeeze,busy);
    a=quest_mirror_angle(&setting,part);CHECK(a.yaw>.023f && a.yaw<.025f);
    /* With opposing wrist/edge input, 0.2 mm used to cross the
     * winner-takes-all threshold and flip the mirror by five degrees. */
    pos[2]=mounts[part].center[2]+.0076f;hand_pose(pose[hand],.10f,0,pos);
    for(uint32_t t=1310;t<=1800;t+=10)quest_mirror_grip(&grip,&setting,mounts,t,valid,pose,squeeze,busy);
    a=quest_mirror_angle(&setting,part);
    pos[2]+=.0002f;hand_pose(pose[hand],.10f,0,pos);
    for(uint32_t t=1810;t<=2300;t+=10) {
        quest_mirror_grip(&grip,&setting,mounts,t,valid,pose,squeeze,busy);
        float delta=a.yaw-quest_mirror_angle(&setting,part).yaw;
        CHECK(delta>=0 && delta<.0005f);
    }
    CHECK(a.yaw-quest_mirror_angle(&setting,part).yaw>.0002f);
    QuestMirrorSetting saved=setting;valid[hand]=0;
    quest_mirror_grip(&grip,&setting,mounts,2310,valid,pose,squeeze,busy);
    pos[2]+=.40f;hand_pose(pose[hand],-.30f,.3f,pos);valid[hand]=1;
    quest_mirror_grip(&grip,&setting,mounts,2320,valid,pose,squeeze,busy);
    CHECK(!memcmp(&saved,&setting,sizeof saved));
    squeeze[hand]=0;valid[hand]=0;
    quest_mirror_grip(&grip,&setting,mounts,2330,valid,pose,squeeze,busy);CHECK(!grip.hand && !memcmp(&saved,&setting,sizeof saved));
}
static void optics_test(void)
{
    const float seat[3]={0,.04f,.06f};
    float eyes[2][4]={{.168f,.04f,.06f,0},{.232f,.04f,.06f,0}};
    quest_mirror_comfort_eyes(eyes,seat);
    CHECK(fabsf((eyes[0][0]+eyes[1][0])*.5f-.14f)<1e-6f);
    CHECK(fabsf(eyes[1][0]-eyes[0][0]-.064f)<1e-6f);
    CHECK(eyes[0][1]==seat[1] && eyes[1][2]==seat[2]);
    eyes[0][0]=.468f;eyes[1][0]=.532f;
    quest_mirror_comfort_eyes(eyes,seat);
    CHECK(fabsf((eyes[0][0]+eyes[1][0])*.5f-.20f)<1e-6f);
    CHECK(fabsf(eyes[1][0]-eyes[0][0]-.064f)<1e-6f);
    QuestMirrorAngle limit=quest_mirror_trim(-.38f,.30f);
    CHECK(limit.yaw==-QUEST_MIRROR_YAW_LIMIT && limit.pitch==QUEST_MIRROR_PITCH_LIMIT);
    /* Eye, glass and object must be collinear after reflecting the eye in
     * the plane. This includes asymmetric driver seats and translated eyes. */
    for(int p=0;p<3;++p)for(int side=-1;side<=1;side+=2) {
        QuestMirrorMount m={{(p-1)*.85f+side*.35f,.1f,-.4f},{.1f,.05f,.02f},1};
        for(int i=-3;i<=3;++i) {
            float n[3],e[3]={i*.04f,.12f,.07f},v[3],ray[3],line[3];
            quest_mirror_normal(n,&m,p,(QuestMirrorAngle){i*.08f,i*.04f},seat);
            quest_mirror_virtual_eye(v,e,m.center,n);
            for(int k=0;k<3;++k)ray[k]=m.center[k]-e[k];
            float d=quest_mirror_dot(ray,n);
            for(int k=0;k<3;++k){ray[k]-=2*d*n[k];line[k]=m.center[k]-v[k];}
            same(ray,line);
            float recovered[3];quest_mirror_virtual_eye(recovered,v,m.center,n);same(e,recovered);
        }
    }
    float front[16]={1,0,0,0,0,1,0,0,0,0,1,0,-100,4,-31,1};
    float rear[16]={-1,0,0,0,0,1,0,0,0,0,-1,0,102,3,34,1};
    QuestMirrorCamera cam;CHECK(quest_mirror_camera(&cam,front,rear));
    const float origin[3]={2,-1,-3},forward[3]={0,0,1};
    same(cam.origin,origin);same(cam.axis[2],forward);
    rear[0]=NAN;CHECK(!quest_mirror_camera(&cam,front,rear));
}
/* Regress the reported Lancer pose: the old yaw stop and full tracked
 * translation could send a whole mirror past the edge of eView 3. */
static void lancer_coverage_test(void)
{
    const QuestMirrorMount mounts[3]={
        {{.381f,.122f,-.362f},{.079f,.024f,.001f},1},
        {{-.496f,-.118f,-.571f},{.083f,.047f,.027f},1},
        {{1.252f,-.118f,-.571f},{.083f,.047f,.027f},1}};
    const float seat[3]={0,.04f,.06f};
    for(int p=0;p<3;++p)for(int yaw=-1;yaw<=1;++yaw)for(int pitch=-1;pitch<=1;++pitch) {
        QuestMirrorAngle a=quest_mirror_trim((float)yaw,(float)pitch);
        float n[3];quest_mirror_normal(n,&mounts[p],p,a,seat);
        for(int lean=0;lean<8;++lean) {
            float eyes[2][4]={{0}};
            for(int e=0;e<2;++e)for(int k=0;k<3;++k) {
                float delta=(lean&(1<<k)?1.f:-1.f)*(k?.12f:.20f);
                eyes[e][k]=seat[k]+delta+(k==0?(e?.032f:-.032f):0.f);
            }
            quest_mirror_comfort_eyes(eyes,seat);
            for(int corner=0;corner<8;++corner)for(int e=0;e<2;++e) {
                float v[3],ray[3];
                for(int k=0;k<3;++k)v[k]=(corner&(1<<k)?1.f:-1.f)*mounts[p].half[k];
                quest_mirror_rotate(ray,v,a,0);
                for(int k=0;k<3;++k)ray[k]+=mounts[p].center[k]-eyes[e][k];
                float dot=quest_mirror_dot(ray,n);for(int k=0;k<3;++k)ray[k]-=2*dot*n[k];
                CHECK(ray[2]>0 && fabsf(.304f*ray[0])<ray[2] && fabsf(1.145f*ray[1])<ray[2]);
            }
        }
    }
}
int main(void)
{
    for(int h=0;h<2;++h)for(int p=0;p<3;++p)grab_test(h,p);
    optics_test();lancer_coverage_test();
    puts("Mirror grab, paired adjustment, seat calibration, damped eyes and Lancer rear-camera coverage passed.");
    return 0;
}
