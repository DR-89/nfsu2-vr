#include "../android/runtime/quest_copchase_hud.h"
#include <assert.h>
int main(void)
{
    QuestRadioFont font;quest_radio_font_init(&font);
    struct {QuestGhostVertex vertices[QUEST_COPCHASE_VERTICES];unsigned guard;} output;
    QuestCopChaseHUD state={1,5,10,0,.625f,0,123456,601};
    char text[QUEST_COPCHASE_LINES][QUEST_COPCHASE_TEXT];quest_copchase_text(text,&state);
    assert(!strcmp(text[0],"HEAT 5   POLICE 10"));assert(!strcmp(text[1],"BUST 62%"));
    output.guard=0x12345678;unsigned n=quest_copchase_vertices(output.vertices,&font,&state);
    assert(n && n<=QUEST_COPCHASE_VERTICES && output.guard==0x12345678);
    for(unsigned i=0;i<n;++i){assert(output.vertices[i].position[1]>QUEST_MINIMAP_RADIUS);
        for(unsigned j=0;j<3;++j)assert(isfinite(output.vertices[i].position[j]));}
    state.phase=2;state.cooldown=1.4f;quest_copchase_text(text,&state);assert(!strcmp(text[1],"COOLDOWN 100%"));
    state.notice=1;quest_copchase_text(text,&state);assert(!strcmp(text[0],"PURSUIT EVADED") && !strcmp(text[1],"TIME 10:01"));
    state.notice=2;quest_copchase_text(text,&state);assert(!strcmp(text[0],"BUSTED"));
    state.cops=64;assert(!quest_copchase_vertices(output.vertices,&font,&state));
    state.cops=10;state.cooldown=NAN;assert(!quest_copchase_vertices(output.vertices,&font,&state));
    memset(&state,0,sizeof state);assert(!quest_copchase_vertices(output.vertices,&font,&state));
    puts("CopChase HUD: PASS");return 0;
}
