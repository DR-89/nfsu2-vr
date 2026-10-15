#include "../android/runtime/quest_cockpit.h"
#include "../android/runtime/quest_cockpit_asset.h"
#include "../android/runtime/quest_cockpit_panels.h"
#include "../android/runtime/quest_cockpit_ui.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1;} } while(0)
typedef struct { uint32_t address; unsigned char bytes[4096]; } UiTestPage;
typedef struct { UiTestPage *pages;unsigned count; } UiTestMemory;
static int ui_test_read(void *context,uint32_t address,void *out,size_t size)
{
    UiTestMemory *m=context;
    while(size) {
        unsigned page;
        for(page=0;page<m->count;++page)
            if(m->pages[page].address==(address&~4095u)) break;
        if(page==m->count) return 0;
        unsigned offset=address&4095u;size_t n=4096-offset;if(n>size)n=size;
        memcpy(out,m->pages[page].bytes+offset,n);
        out=(unsigned char *)out+n;address+=(uint32_t)n;size-=n;
    }
    return 1;
}
static float dot_point(const float view[16],const float p[3],int column)
{ return view[column]*p[0]+view[4+column]*p[1]+view[8+column]*p[2]+view[12+column]; }
int main(int argc,char **argv)
{
    /* Private real guest-memory fixtures: paused with UI_pause.fng plus the
     * retained race HUD, and resumed with only HUD_SingleRace.fng. */
    UiTestMemory ui={0};
    CHECK(quest_cockpit_ui_menu(ui_test_read,&ui)); /* incomplete state stays whole */
    if(argc>3) {
        FILE *f=fopen(argv[3],"rb");CHECK(f);uint32_t header[2];
        CHECK(fread(header,sizeof header,1,f)==1 && header[0]==0x4e55494d && header[1]>=2 && header[1]<=32);
        for(unsigned i=0;i<header[1];++i) {
            uint32_t h[2];CHECK(fread(h,sizeof h,1,f)==1 && h[0]<=1 && h[1]>0 && h[1]<=256);
            ui.count=h[1];ui.pages=calloc(ui.count,sizeof *ui.pages);CHECK(ui.pages);
            CHECK(fread(ui.pages,sizeof *ui.pages,ui.count,f)==ui.count);
            CHECK(quest_cockpit_ui_menu(ui_test_read,&ui)==(int)h[0]);
            free(ui.pages);ui.pages=NULL;ui.count=0;
        }
        CHECK(fgetc(f)==EOF && !ferror(f));fclose(f);
        printf("Cockpit UI: %u captured pause/resume states classified correctly\n",header[1]);
    }
    const float *seat=quest_cockpit_seat;
    const float pos[3]={105,-87,3};
    float view[16],rotation[12];
    for(int i=0;i<360;++i) {
        float a=i*.01745329251994f,c=cosf(a),s=sinf(a);
        const float r[12]={c,s,0,0,-s,c,0,0,0,0,1,0};
        memcpy(rotation,r,sizeof r);
        CHECK(quest_cockpit_view(view,rotation,pos,seat));
        float eye[3],point[3];
        for(int k=0;k<3;++k) eye[k]=pos[k]+seat[0]*r[k]+seat[1]*r[4+k]+seat[2]*r[8+k];
        for(int k=0;k<3;++k) CHECK(fabsf(dot_point(view,eye,k))<.0001f);
        for(int k=0;k<3;++k) point[k]=eye[k]+5*r[k];
        CHECK(fabsf(dot_point(view,point,2)-5)<.0001f);
        for(int k=0;k<3;++k) point[k]=eye[k]+r[8+k];
        CHECK(fabsf(dot_point(view,point,1)+1)<.0001f);
        for(int k=0;k<3;++k) point[k]=eye[k]-r[4+k];
        CHECK(fabsf(dot_point(view,point,0)-1)<.0001f);
    }
    rotation[0]=NAN; CHECK(!quest_cockpit_view(view,rotation,pos,seat));
    memset(rotation,0,sizeof rotation); CHECK(!quest_cockpit_view(view,rotation,pos,seat));
    QuestCameraButton b={0};
    CHECK(!quest_camera_button(&b,100,1,0));
    CHECK(quest_camera_button(&b,300,0,0));
    CHECK(!quest_camera_button(&b,401,0,0));
    CHECK(!quest_camera_button(&b,500,1,0));
    CHECK(!quest_camera_button(&b,1500,1,0)); /* holding has no separate action */
    CHECK(quest_camera_button(&b,1600,0,0));
    CHECK(!quest_camera_button(&b,1701,0,0));
    CHECK(!quest_camera_button(&b,2000,1,0));
    CHECK(!quest_camera_button(&b,2400,1,1));
    CHECK(!quest_camera_button(&b,2800,1,0));
    CHECK(!quest_camera_button(&b,2900,0,0));
    for(unsigned last=3;last<=4;++last) {
        unsigned slot=0, expected=0; int selected=0,entry=0;
        /* Compare the retained retail state to an independent complete ring,
         * including changes of direction while inside the inserted cockpit. */
        for(int i=0;i<240;++i) {
            int direction=(i/17)%2 ? -1 : 1;
            int action=quest_cockpit_cycle(slot,last,direction,selected,1,&entry);
            selected=action==1;
            if(action==0 || action==2)
                slot=(slot+last+1+direction)%(last+1);
            expected=(expected+last+2+direction)%(last+2);
            CHECK((selected?last+1:slot)==expected && slot<=last);
        }
    }
    int entry=1;
    CHECK(!quest_cockpit_cycle(3,3,1,0,0,&entry)); /* no asset: retail */
    CHECK(!quest_cockpit_cycle(6,3,1,0,1,&entry)); /* invalid profile slot */
    CHECK(!quest_cockpit_cycle(3,3,0,0,1,&entry)); /* query, not a cycle */
    CHECK(quest_cockpit_cycle(3,3,1,1,0,&entry)==2); /* always permit exit */
    const float pivot[3]={.03f,-.36f,-.30f},axis[3]={0,.6f,.8f};
    float wheel[16], inverse[16], identity[16];
    for(int n=-100;n<=100;++n) {
        quest_cockpit_wheel(wheel,pivot,axis,n*.01f);
        quest_cockpit_wheel(inverse,pivot,axis,-n*.01f);
        quest_cockpit_multiply(identity,wheel,inverse);
        for(int i=0;i<16;++i) CHECK(fabsf(identity[i]-(i%5==0?1:0))<.00001f);
        for(int row=0;row<3;++row) {
            float p=wheel[12+row],a=0;
            for(int col=0;col<3;++col) { p+=wheel[col*4+row]*pivot[col]; a+=wheel[col*4+row]*axis[col]; }
            CHECK(fabsf(p-pivot[row])<.00001f && fabsf(a-axis[row])<.00001f);
        }
    }
    const float origin[3]={0,0,0},front[3]={0,0,1};
    quest_cockpit_wheel(wheel,origin,front,.25f);
    CHECK(wheel[4]>.7f && wheel[5]>.7f); /* top of rim moves right */
    quest_cockpit_wheel(wheel,origin,front,-.25f);
    CHECK(wheel[4]<-.7f && wheel[5]>.7f);
    for(int direction=-1;direction<=1;direction+=2) {
        quest_cockpit_wheel(wheel,origin,front,(float)direction);
        CHECK(fabsf(wheel[4])<.00001f && fabsf(wheel[5]+1)<.00001f); /* lock: 180 degrees */
        quest_cockpit_wheel(inverse,origin,front,.5f*direction);
        CHECK(fabsf(inverse[4]-direction)<.00001f && fabsf(inverse[5])<.00001f); /* halfway: 90 degrees */
        quest_cockpit_wheel(inverse,origin,front,2.0f*direction);
        for(int i=0;i<16;++i) CHECK(fabsf(wheel[i]-inverse[i])<.00001f);
    }
    float fast=0,slow=0;
    for(int i=0;i<120;++i) fast=quest_cockpit_steering_smooth(fast,1,1.0f/120);
    for(int i=0;i<72;++i) slow=quest_cockpit_steering_smooth(slow,1,1.0f/72);
    CHECK(fabsf(fast-slow)<.00001f && slow>.999f && fast<=1);
    CHECK(quest_cockpit_steering_smooth(1,-1,0)==1);
    CHECK(quest_cockpit_steering_smooth(1,-1,1)==-1);
    QuestGhostVertex panel[192];
    quest_cockpit_menu_panel(panel);
    unsigned corners=0;
    for(int i=0;i<6;++i) {
        float u=panel[i].uv[0],v=panel[i].uv[1];
        CHECK((u==0 || u==1) && (v==0 || v==1));
        corners|=1u<<((unsigned)u+2*(unsigned)v);
        CHECK(fabsf(panel[i].position[0]-(u-.5f)*2.8f)<.00001f);
        CHECK(fabsf(panel[i].position[1]-(1.45f-v*1.7f))<.00001f);
        CHECK(panel[i].position[2]==-3);
    }
    CHECK(corners==15); /* full UI, including both previously relocated lower corners */
    CHECK(!memcmp(panel[0].position,panel[3].position,12));
    CHECK(!memcmp(panel[2].position,panel[4].position,12)); /* continuous shared diagonal */
    quest_cockpit_panel(panel,0,-.205f,-.56f,.24f,.24f,440,260,640,480);
    for(int i=0;i<6;++i) {
        CHECK(panel[i].position[2]<-.329f); /* behind the wheel, not head locked */
        CHECK(panel[i].position[1]<0 && fabsf(panel[i].position[0])<.121f);
        CHECK(panel[i].uv[0]>=440.0f/640 && panel[i].uv[0]<=1);
        CHECK(panel[i].uv[1]>=260.0f/480 && panel[i].uv[1]<=1);
    }
    float own[16]={0,-1.22f,0,.427f,0,0,2.21f,-2.3f,1,0,0,-.32f,1,0,0,.10f};
    CHECK(quest_cockpit_own_car(own,1.22f,2.21f,seat));
    own[15]+=.10f;CHECK(!quest_cockpit_own_car(own,1.22f,2.21f,seat));own[15]-=.10f;
    own[13]=.1f;CHECK(!quest_cockpit_own_car(own,1.22f,2.21f,seat));
    /* Optional private capture fixture: expected glass counts are assigned
     * after inspecting the retail geometry, not generated by this detector. */
    if(argc>2) {
        FILE *f=fopen(argv[2],"rb");CHECK(f);unsigned cases=0,found[3]={0};
        uint32_t header[6];
        while(fread(header,sizeof header,1,f)==1) {
            float m[16],scale[2];CHECK(fread(m,sizeof m,1,f)==1 && fread(scale,sizeof scale,1,f)==1);
            unsigned v=header[1],n=header[2];CHECK(v<=20000 && n<=120000);
            float (*q)[3]=malloc(v*12);uint32_t *ix=malloc(n*4);CHECK(q && ix);
            CHECK(fread(q,12,v,f)==v && fread(ix,4,n,f)==n);
            const float reference[3]={-.18f,.35f,1.04f};
            const float *seats[4]={reference,seat,(const float[3]){.05f,.30f,.85f},(const float[3]){-.35f,.45f,1.10f}};
            for(int pose=0;pose<4;++pose) {
                float matrix[16],delta[3]={seats[pose][1]-reference[1],reference[2]-seats[pose][2],seats[pose][0]-reference[0]};
                memcpy(matrix,m,sizeof matrix);
                matrix[3]+=delta[0]*scale[0];matrix[7]+=delta[1]*scale[1];matrix[15]-=delta[2];
                float center[3]={matrix[3]/scale[0],matrix[7]/scale[1],-matrix[15]};
                float (*moved)[3]=malloc(v*12);CHECK(moved);
                for(unsigned j=0;j<v;++j)for(int a=0;a<3;++a)moved[j][a]=q[j][a]+delta[a];
                for(int k=0;k<3;++k) {
                    unsigned got=quest_cockpit_own_car(matrix,scale[0],scale[1],seats[pose])?
                        quest_cockpit_mirror_mesh(panel,192,(const float (*)[3])moved,v,ix,n,center,k):0;
                    if(got!=header[3+k])fprintf(stderr,"glass draw=%u seat=%d part=%d got=%u expected=%u\n",header[0],pose,k,got,header[3+k]);
                    CHECK(got==header[3+k]);if(!pose)found[k]+=got;
                }
                free(moved);
            }
            free(q);free(ix);++cases;
        }
        CHECK(feof(f));fclose(f);CHECK(cases>30 && found[0] && found[1] && found[2]);
        printf("Original mirror capture: %u batches at 4 seat positions, %u/%u/%u glass vertices\n",cases,found[0],found[1],found[2]);
    }
    if(argc>1) {
        FILE *f=fopen(argv[1],"rb"); CHECK(f);
        CHECK(!fseek(f,0,SEEK_END)); long length=ftell(f); CHECK(length>0 && length<100000000);
        rewind(f); unsigned char *data=malloc((size_t)length); CHECK(data);
        CHECK(fread(data,1,(size_t)length,f)==(size_t)length); fclose(f);
        QuestCockpitAsset asset; CHECK(quest_cockpit_asset(&asset,data,(size_t)length));
        CHECK(!quest_cockpit_asset(&asset,data,(size_t)length-1));
        unsigned char saved[4]; memcpy(saved,data+8,4); memset(data+8,255,4);
        CHECK(!quest_cockpit_asset(&asset,data,(size_t)length)); memcpy(data+8,saved,4);
        size_t offset=64+(size_t)asset.vertices*32; memcpy(saved,data+offset,4); memset(data+offset,255,4);
        CHECK(!quest_cockpit_asset(&asset,data,(size_t)length)); memcpy(data+offset,saved,4);
        for(size_t i=24;i<=60;i+=4) {
            memcpy(saved,data+i,4); memset(data+i,255,4);
            CHECK(!quest_cockpit_asset(&asset,data,(size_t)length)); memcpy(data+i,saved,4);
        }
        float bad=NAN; memcpy(data+64,&bad,4); CHECK(!quest_cockpit_asset(&asset,data,(size_t)length));
        free(data);
    }
    puts("Cockpit camera, controls, wheel rotation and asset validation checks passed");
    return 0;
}
