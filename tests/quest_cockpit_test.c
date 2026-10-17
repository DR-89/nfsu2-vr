#include "../android/runtime/quest_cockpit.h"
#include "../android/runtime/quest_cockpit_vehicle.h"
#include "../android/runtime/quest_cockpit_asset.h"
#include "../android/runtime/quest_cockpit_panels.h"
#include "../android/runtime/quest_cockpit_occlusion.h"
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
#include "quest_cockpit_windows_test.inc"
static float dot_point(const float view[16],const float p[3],int column)
{ return view[column]*p[0]+view[4+column]*p[1]+view[8+column]*p[2]+view[12+column]; }
static int ui_notification_test(void)
{
    UiTestPage pages[2]={{.address=0x003fb000},{.address=0x80010000}};
    UiTestMemory memory={pages,2};
    uint32_t frontend=0x80010020,manager=0x80010100,first=0x80010300;
    memcpy(pages[0].bytes+0xd08,&frontend,4);
    memcpy(pages[1].bytes+0x28,&manager,4);
    memcpy(pages[1].bytes+0x1e4,&first,4);
    const struct { const char *name[3];int menu,pause; } cases[]={
        {{"HUD_SingleRace.fng"},0},
        {{"HUD_SingleRace.fng","Chyron_IG.fng"},0},
        {{"Chyron_IG.fng","HUD_SingleRace.fng"},0},
        {{"HUD_SingleRace.fng","Chyron_IG.fng","UI_Pause.fng"},1,1},
        {{"HUD_SingleRace.fng","GenericDialog_SMALL.fng"},1},
        {{"HUD_SingleRace.fng","Chyron_FE.fng"},1},
        {{"HUD_SingleRace.fng","Chyron_IG.fngX"},1},
        {{"HUD_SingleRace.fng","Chyron_IG.fng "},1},
        {{"Chyron_IG.fng"},1},
        {{"HUD_Drag.fng","chyron_ig.FNG"},0},
        {{"HUD_SingleRace.fng","UI_PauseOptionsMain.fng"},1},
        {{"HUD_SingleRace.fng","Unknown.fng"},1},
        {{"UI_Pause.fng"},1,1},
        {{"HUD_SingleRace.fng","ui_pause.FNG"},1,1},
        {{"HUD_SingleRace.fng","UI_Pause.fng","GenericDialog_SMALL.fng"},1,0},
        {{"UI_Pause.fng","UI_PauseOptionsMain.fng"},1,0},
        {{"UI_Pause.fngX"},1,0},
        {{"UI_Pause.fng "},1,0}};
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];++i) {
        for(unsigned n=0;n<3;++n) {
            uint32_t fields[4]={0,0,0,0x80010400+n*128};
            if(n<2 && cases[i].name[n+1])fields[1]=first+(n+1)*32;
            memcpy(pages[1].bytes+0x300+n*32,fields,sizeof fields);
            memset(pages[1].bytes+0x400+n*128,0,128);
            if(cases[i].name[n])strcpy((char *)pages[1].bytes+0x400+n*128,cases[i].name[n]);
        }
        CHECK(quest_cockpit_ui_menu(ui_test_read,&memory)==cases[i].menu);
        CHECK(quest_cockpit_ui_paused(ui_test_read,&memory)==cases[i].pause);
    }
    /* A cyclic/incomplete active list must not authorize settings. */
    strcpy((char *)pages[1].bytes+0x400,"UI_Pause.fng");
    memcpy(pages[1].bytes+0x304,&first,4);
    CHECK(!quest_cockpit_ui_paused(ui_test_read,&memory));
    memory.count=1;CHECK(!quest_cockpit_ui_paused(ui_test_read,&memory));
    puts("Cockpit UI: exact pause authorization; notifications keep instruments seated; menus, dialogs and unknown packages remain whole.");
    return 0;
}
int main(int argc,char **argv)
{
    CHECK(QUEST_COCKPIT_VEHICLES==30 && !quest_cockpit_vehicle_index("UNKNOWN"));
    const QuestCockpitVehicle *miata=quest_cockpit_vehicle(quest_cockpit_vehicle_index("MIATA"));
    CHECK(miata->seat[0]<-.30f && miata->seat[2]<.83f && miata->rear_limit<.9f);
    unsigned right_hand=0,two_seat=0;
    for(int i=1;i<QUEST_COCKPIT_VEHICLES;++i) {
        const QuestCockpitVehicle *fit=quest_cockpit_vehicle(i);
        CHECK(quest_cockpit_vehicle_index(fit->name)==i);
        CHECK(fit->seat[1]*fit->side>0 && fit->near_width>.6f && fit->far_width<1.5f);
        float gauge[3]={0,-.205f,-.66f},fitted[3];quest_cockpit_fit_point(fitted,gauge,fit);
        CHECK(fitted[2]<-.45f && fitted[2]>-.8f && fitted[1]==gauge[1]);
        float column[3]={0,-.21f,-.33f};quest_cockpit_fit_point(fitted,column,fit);
        CHECK(fabsf(fitted[2]-column[2])<1e-6f && fitted[0]==0);
        right_hand+=fit->side<0;two_seat+=fit->rear_limit<1;
        /* Asset bounds: retain full front backrests (z <= .425), remove
         * rear cushion from its leading edge (z >= .500), not just its back. */
        if(fit->rear_limit<1)CHECK(fit->rear_limit>.425f && fit->rear_limit<.499f);
    }
    CHECK(right_hand==2 && two_seat==2);
    const QuestCockpitVehicle *peugeot=quest_cockpit_vehicle(quest_cockpit_vehicle_index("PEUGOT"));
    CHECK(fabsf(peugeot->seat[0]+.1f)<1e-6f && peugeot->near_width==1 && peugeot->front_depth==1);
    puts("Vehicle fitting: 29 profiles, right-hand cabins, two-seat rear limits and dashboard/wheel alignment pass.");
    CHECK(!ui_notification_test());
    CHECK(!cockpit_windows_test(argc>8?argv[8]:NULL));
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
    quest_cockpit_panel(panel,0,-.205f,-.66f,.24f,.24f,440,260,640,480);
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
    /* Driver cards obstruct the seated eye, but ordinary glass/trim must not
     * be mistaken for them. Use synthetic meshes here, never retail assets. */
    const float cards[8][4]={{-.23f,.39f,1.1f,1},{.09f,.39f,.7f,1},
        {.09f,.39f,1.1f,1},{-.23f,.39f,.7f,1},{-.07f,.15f,1.1f,1},
        {-.07f,.63f,.7f,1},{-.07f,.63f,1.1f,1},{-.07f,.15f,.7f,1}};
    const uint32_t card_indices[12]={1,0,3,4,5,6,5,4,7,0,1,2};
    CHECK(quest_cockpit_driver_cards(cards,card_indices));
    for(int angle=0;angle<360;angle+=15) {
        float rotated[8][4],c=cosf(angle*.01745329252f),s=sinf(angle*.01745329252f);
        memcpy(rotated,cards,sizeof rotated);
        for(int v=0;v<8;++v) {
            float x=cards[v][0]+.07f,y=cards[v][1]-.39f;
            rotated[v][0]=c*x-s*y-.07f;rotated[v][1]=s*x+c*y+.39f;
        }
        CHECK(quest_cockpit_driver_cards((const float (*)[4])rotated,card_indices));
    }
    /* Different driver-side and seat heights must not affect recognition. */
    for(int variant=0;variant<4;++variant) {
        float moved[8][4];memcpy(moved,cards,sizeof moved);
        for(int v=0;v<8;++v) {
            if(variant==0)moved[v][1]-=.08f; /* Peugeot/Lancer */
            if(variant==1)moved[v][1]=-moved[v][1]; /* A3/Skyline */
            if(variant==2)moved[v][2]+=.5f; /* SUV */
            if(variant==3)moved[v][0]-=.4f; /* rearward seat */
        }
        CHECK(quest_cockpit_driver_cards((const float (*)[4])moved,card_indices));
    }
    for(int change=0;change<6;++change) {
        float rejected[8][4];uint32_t indices[12];
        memcpy(rejected,cards,sizeof rejected);memcpy(indices,card_indices,sizeof indices);
        if(change==0)for(int v=0;v<8;++v)rejected[v][0]*=5; /* body-sized panels */
        if(change==1)for(int v=0;v<8;++v)rejected[v][2]*=.1f; /* thin trim */
        if(change==2)for(int v=0;v<8;++v){float y=rejected[v][1];rejected[v][1]=rejected[v][2];rejected[v][2]=y;} /* horizontal plane */
        if(change==3)rejected[0][0]=NAN;
        if(change==4)indices[0]=8;
        if(change==5)for(int v=0;v<8;++v)rejected[v][1]=.39f; /* single plane */
        CHECK(!quest_cockpit_driver_cards((const float (*)[4])rejected,indices));
    }
    /* Different body styles: raised/lowered/slanted glass, outside the
     * Peugeot reference bounds. Reject a neighboring housing triangle. */
    for(int side=1;side<=2;++side)for(int height=0;height<3;++height)for(int slope=0;slope<3;++slope) {
        float x=side==1?-.83f:.83f,y=.52f+height*.37f,z=-.82f;
        float q[4][3]={{x-.09f,y-.04f,z-slope*.02f},{x+.09f,y-.04f,z+slope*.02f},
                       {x+.09f,y+.04f,z+slope*.02f},{x-.09f,y+.04f,z-slope*.02f}};
        const uint32_t ix[6]={0,1,2,0,2,3};const float origin[3]={0};
        CHECK(quest_cockpit_mirror_mesh(panel,192,(const float (*)[3])q,4,ix,6,origin,side)==6);
        CHECK(!quest_cockpit_mirror_mesh(panel,192,(const float (*)[3])q,4,ix,6,origin,3-side));
        for(int v=0;v<6;++v)CHECK(isfinite(panel[v].uv[0]) && isfinite(panel[v].uv[1]));
    }
    /* Low coupe mirrors must work as well as high SUV mirrors. The center
     * glass can share a batch with a separate narrow stalk; do not paint it. */
    for(int height=0;height<5;++height) {
        float y=.90f+height*.15f;
        float q[8][3]={{-.09f,y,-.47f},{.09f,y,-.47f},
            {.09f,y+.05f,-.47f},{-.09f,y+.05f,-.47f},
            {-.005f,y+.045f,-.48f},{.005f,y+.045f,-.48f},
            {.005f,y+.09f,-.48f},{-.005f,y+.09f,-.48f}};
        const uint32_t ix[12]={0,1,2,0,2,3,4,5,6,4,6,7};const float zero[3]={0};
        CHECK(quest_cockpit_mirror_mesh(panel,192,(const float (*)[3])q,8,ix,12,zero,0)==6);
        for(int v=0;v<6;++v)CHECK(panel[v].position[1]<=y+.0501f);
    }
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
            for(int pose=0;pose<4+QUEST_COCKPIT_VEHICLES;++pose) {
                const float *pose_seat=pose<4?seats[pose]:quest_cockpit_vehicle(pose-4)->seat;
                float matrix[16],delta[3]={pose_seat[1]-reference[1],reference[2]-pose_seat[2],pose_seat[0]-reference[0]};
                memcpy(matrix,m,sizeof matrix);
                matrix[3]+=delta[0]*scale[0];matrix[7]+=delta[1]*scale[1];matrix[15]-=delta[2];
                float center[3]={matrix[3]/scale[0],matrix[7]/scale[1],-matrix[15]};
                float (*moved)[3]=malloc(v*12);CHECK(moved);
                for(unsigned j=0;j<v;++j)for(int a=0;a<3;++a)moved[j][a]=q[j][a]+delta[a];
                for(int k=0;k<3;++k) {
                    unsigned got=quest_cockpit_own_car(matrix,scale[0],scale[1],pose_seat)?
                        quest_cockpit_mirror_mesh(panel,192,(const float (*)[3])moved,v,ix,n,center,k):0;
                    if(got!=header[3+k])fprintf(stderr,"glass draw=%u seat=%d part=%d got=%u expected=%u\n",header[0],pose,k,got,header[3+k]);
                    CHECK(got==header[3+k]);if(!pose)found[k]+=got;
                }
                free(moved);
            }
            free(q);free(ix);++cases;
        }
        CHECK(feof(f));fclose(f);CHECK(cases>30 && found[0] && found[1] && found[2]);
        printf("Original mirror capture: %u batches at 4 user positions and %u vehicle profiles, %u/%u/%u glass vertices\n",cases,QUEST_COCKPIT_VEHICLES,found[0],found[1],found[2]);
    }
    if(argc>4) {
        FILE *f=fopen(argv[4],"rb");CHECK(f);uint32_t header[3];unsigned cases=0,matched=0;
        while(fread(header,sizeof header,1,f)==1) {
            unsigned v=header[1],n=header[2];CHECK(v<=20000 && n<=120000);
            float (*p)[4]=malloc(v*16);uint32_t *ix=malloc(n*4);CHECK(p && ix);
            CHECK(fread(p,16,v,f)==v && fread(ix,4,n,f)==n);
            if(v==8 && n==12 && quest_cockpit_driver_cards((const float (*)[4])p,ix))++matched;
            free(p);free(ix);++cases;
        }
        CHECK(feof(f));fclose(f);CHECK(cases>30 && matched==1);
        printf("Near-geometry capture: %u batches, exactly one driver proxy removed.\n",cases);
    }
    if(argc>5) {
        FILE *f=fopen(argv[5],"rb");CHECK(f);uint32_t header[4];unsigned cases=0,matched=0;
        while(fread(header,sizeof header,1,f)==1) {
            unsigned v=header[1],n=header[2];CHECK(v<=65536 && n<=120000 && header[3]<=1);
            float (*p)[4]=malloc(v*16);uint32_t *ix=malloc(n*4);CHECK(p && ix);
            CHECK(fread(p,16,v,f)==v && fread(ix,4,n,f)==n);
            int found=v==8 && n==12 && quest_cockpit_driver_cards((const float (*)[4])p,ix);
            if(found!=(int)header[3])fprintf(stderr,"Stock submesh %u: got %d expected %u\n",header[0],found,header[3]);
            CHECK(found==(int)header[3]);matched+=found;
            free(p);free(ix);++cases;
        }
        CHECK(feof(f));fclose(f);CHECK(matched==29 && cases>1000);
        printf("Stock geometry: %u submeshes, all %u driver proxies removed, other meshes retained.\n",cases,matched);
    }
    if(argc>6) {
        FILE *f=fopen(argv[6],"rb");CHECK(f);uint32_t header[4];unsigned cases=0,mirrors=0;
        while(fread(header,sizeof header,1,f)==1) {
            unsigned v=header[1],n=header[2];CHECK(v<=65536 && n<=120000 && header[3]<=192);
            float (*p)[4]=malloc(v*16),(*q)[3]=malloc(v*12);uint32_t *ix=malloc(n*4);CHECK(p && q && ix);
            CHECK(fread(p,16,v,f)==v && fread(ix,4,n,f)==n);
            for(unsigned j=0;j<v;++j){q[j][0]=-p[j][1];q[j][1]=p[j][2];q[j][2]=-p[j][0];}
            const float zero[3]={0};
            unsigned found=v<=512 && n<=1536?quest_cockpit_mirror_mesh(panel,192,(const float (*)[3])q,v,ix,n,zero,0):0;
            if(found!=header[3])fprintf(stderr,"Stock center mirror submesh %u: got %u expected %u\n",header[0],found,header[3]);
            CHECK(found==header[3]);mirrors+=found!=0;
            free(p);free(q);free(ix);++cases;
        }
        CHECK(feof(f));fclose(f);CHECK(mirrors==29 && cases>1000);
        printf("Stock center mirrors: %u submeshes, all %u mirrors selected; other meshes excluded.\n",cases,mirrors);
    }
    if(argc>7) {
        FILE *f=fopen(argv[7],"rb");CHECK(f);uint32_t header[5];unsigned cases=0,found[QUEST_COCKPIT_VEHICLES][2]={{0}};
        while(fread(header,sizeof header,1,f)==1) {
            unsigned car=header[0],side=header[1],v=header[3],n=header[4];
            CHECK(car>0 && car<QUEST_COCKPIT_VEHICLES && side>=1 && side<=2 && v<=512 && n<=1536);
            float (*p)[3]=malloc(v*12),(*q)[3]=malloc(v*12);uint32_t *ix=malloc(n*4);CHECK(p && q && ix);
            CHECK(fread(p,12,v,f)==v && fread(ix,4,n,f)==n);
            for(unsigned j=0;j<v;++j){q[j][0]=-p[j][1];q[j][1]=p[j][2];q[j][2]=-p[j][0];}
            const float zero[3]={0};
            unsigned selected=quest_cockpit_mirror_mesh(panel,192,(const float (*)[3])q,v,ix,n,zero,side);
            CHECK(!quest_cockpit_mirror_mesh(panel,192,(const float (*)[3])q,v,ix,n,zero,3-side));
            found[car][side-1]+=selected!=0;
            free(p);free(q);free(ix);++cases;
        }
        CHECK(feof(f));fclose(f);
        for(unsigned car=1;car<QUEST_COCKPIT_VEHICLES;++car)CHECK(found[car][0] && found[car][1]);
        printf("Stock side mirrors: %u material batches; glass selected on both sides of all 29 cars, wrong-side candidates excluded.\n",cases);
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
