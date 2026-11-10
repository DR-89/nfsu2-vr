#include "../android/runtime/quest_vr_panel.h"
#include <assert.h>
int main(void)
{
    QuestVRSettings s=quest_vr_defaults();QuestVRMenu m={0};
    assert(!s.copchase);
    m.copchase_data=QUEST_COPCHASE_DATA_MISSING;
    assert(strstr(quest_vr_copchase_data_hint(&s,&m),"IS OFF"));
    s.copchase=1;
    assert(strstr(quest_vr_copchase_data_hint(&s,&m),"REINSTALL"));
    m.copchase_data=QUEST_COPCHASE_DATA_UNCHECKED;
    assert(strstr(quest_vr_copchase_data_hint(&s,&m),"RESTART"));
    m.copchase_data=QUEST_COPCHASE_DATA_READY;
    assert(strstr(quest_vr_copchase_data_hint(&s,&m),"DATA READY"));
    quest_vr_menu_step(&m,&s,1,0,0);
    assert(m.copchase_data==QUEST_COPCHASE_DATA_READY && !m.open);
    char saved[8192];int n=quest_vr_format(&s,saved,sizeof saved);
    assert(n>0 && !strstr(saved,"copchase_data") && strstr(saved,"copchase=1\n"));
    uint32_t *pixels=calloc(1024u*768u+2,sizeof *pixels);assert(pixels);
    pixels[0]=pixels[1024u*768u+1]=0x12345678;
    for(int data=0;data<=QUEST_COPCHASE_DATA_MISSING;++data){
        m.copchase_data=data;m.open=1;
        for(int row=QUEST_VR_COPCHASE_ROW;row<=QUEST_VR_COPCHASE_LIMIT_ROW;++row){
            m.row=row;quest_vr_panel(pixels+1,&s,&m,0,0);
            assert(pixels[0]==0x12345678 && pixels[1024u*768u+1]==0x12345678);
        }
        m.row=QUEST_VR_MODS_ROW;quest_vr_panel(pixels+1,&s,&m,0,0);
        assert(pixels[0]==0x12345678 && pixels[1024u*768u+1]==0x12345678);
    }
    m.copchase_data=QUEST_COPCHASE_DATA_MISSING;m.row=QUEST_VR_COPCHASE_ROW;m.held=0;
    assert(quest_vr_menu_step(&m,&s,10,1,QUEST_VR_ACCEPT)&1);
    assert(!s.copchase); /* The hint never blocks changing the user's setting. */
    free(pixels);return 0;
}
