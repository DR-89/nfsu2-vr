#ifndef QUEST_COCKPIT_UI_H
#define QUEST_COCKPIT_UI_H
#include <stdint.h>
#include <stddef.h>

typedef int (*QuestUiRead)(void *context,uint32_t address,void *out,size_t size);

/* US retail frontend: 13A2A0/126AA0 search this active package list;
 * 163050 traverses it for updates. Loaded package-cache entries are not enough:
 * UI_pause.fng may remain cached after resuming. Query only on the guest thread.
 * Keep any non-HUD interface (pause, options, confirmation dialogs) together.
 * An incomplete/invalid list also gets the unsplit presentation. */
static inline int quest_cockpit_ui_menu(QuestUiRead read,void *context)
{
    uint32_t frontend,manager,node;
    if(!read(context,0x003fbd08u,&frontend,4) || !frontend ||
       !read(context,frontend+8,&manager,4) || !manager ||
       !read(context,manager+0xe4,&node,4) || !node) return 1;
    for(unsigned count=0;node && count<32;++count) {
        uint32_t fields[4];unsigned char name[4];
        if(!read(context,node,fields,sizeof fields) || !fields[3] ||
           !read(context,fields[3],name,sizeof name)) return 1;
        if((name[0]|32)!='h' || (name[1]|32)!='u' ||
           (name[2]|32)!='d' || name[3]!='_') return 1;
        node=fields[1];
        if(!node) return 0;
    }
    return 1;
}
#endif
