#include "../android/runtime/quest_stance.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(int argc,char **argv){
    QuestStanceMenu menu={0};menu.open=1;menu.row=8;quest_stance_step(&menu,100,1,QUEST_VR_DOWN);CHECK(menu.row==0);
    quest_stance_step(&menu,110,1,0);quest_stance_step(&menu,120,1,QUEST_VR_ACCEPT);CHECK(menu.action==0 && menu.step==1);
    quest_stance_step(&menu,130,1,QUEST_VR_ACCEPT);CHECK(menu.action==-1);
    quest_stance_step(&menu,150,1,QUEST_VR_BACK);CHECK(!menu.open);
    if(argc>1){uint32_t *pixels=malloc(1024*768*4);CHECK(pixels);menu.open=1;menu.row=2;memset(&menu.view,0,sizeof menu.view);
        menu.view.available=1;menu.view.enabled=1;menu.view.custom=1;strcpy(menu.view.name,"NISSAN SKYLINE");menu.view.value=(XyzStance){100,150,80,-40,130};
        quest_stance_panel(pixels,&menu);FILE *f=fopen(argv[1],"wb");CHECK(f);fprintf(f,"P6\n1024 768\n255\n");
        for(int i=0;i<1024*768;i++){uint32_t p=pixels[i];unsigned char rgb[3]={(unsigned char)(p>>16),(unsigned char)(p>>8),(unsigned char)p};fwrite(rgb,1,3,f);}fclose(f);free(pixels);}
    puts("Stance menu: navigation, held input and back passed");return 0;
}
