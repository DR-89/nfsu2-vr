#ifndef QUEST_COCKPIT_UI_H
#define QUEST_COCKPIT_UI_H
#include <stdint.h>
#include <stddef.h>

typedef int (*QuestUiRead)(void *context,uint32_t address,void *out,size_t size);

/* Authorization is deliberately separate from whole-plane UI rendering.
 * Only the active root pause package allows X; a dialog/options package on
 * top blocks it. Missing/corrupt state must never expose the settings entry. */
static inline int quest_cockpit_ui_paused(QuestUiRead read,void *context)
{
    uint32_t frontend,manager,node;
    if(!read(context,0x003fbd08u,&frontend,4) || !frontend ||
       !read(context,frontend+8,&manager,4) || !manager ||
       !read(context,manager+0xe4,&node,4) || !node) return 0;
    int pause=0;
    for(unsigned count=0;node && count<32;++count) {
        uint32_t fields[4];unsigned char name[4];
        if(!read(context,node,fields,sizeof fields) || !fields[3] ||
           !read(context,fields[3],name,sizeof name)) return 0;
        if((name[0]|32)!='h' || (name[1]|32)!='u' ||
           (name[2]|32)!='d' || name[3]!='_') {
            const char *allowed[]={"UI_Pause.fng","Chyron_IG.fng"};
            int match=-1;
            for(int k=0;k<2;++k) {
                unsigned char full[14];size_t size=0;
                while(allowed[k][size])++size;
                if(!read(context,fields[3],full,size+1) || full[size])continue;
                size_t i=0;
                while(i<size && (full[i]|32)==(allowed[k][i]|32))++i;
                if(i==size){match=k;break;}
            }
            if(match<0)return 0;
            if(match==0)pause=1;
        }
        node=fields[1];
        if(!node)return pause;
    }
    return 0;
}

/* US retail frontend: 13A2A0/126AA0 search this active package list;
 * 163050 traverses it for updates. Loaded package-cache entries are not enough:
 * UI_pause.fng may remain cached after resuming. Query only on the guest thread.
 * Keep menus (pause, options, confirmation dialogs) together. Chyron_IG is
 * a transient driving notification, not a menu: treating it as paused moves
 * the instruments back to the complete HUD plane until the message expires.
 * An incomplete/invalid list also gets the unsplit presentation. */
static inline int quest_cockpit_ui_menu(QuestUiRead read,void *context)
{
    uint32_t frontend,manager,node;
    if(!read(context,0x003fbd08u,&frontend,4) || !frontend ||
       !read(context,frontend+8,&manager,4) || !manager ||
       !read(context,manager+0xe4,&node,4) || !node) return 1;
    int has_hud=0;
    for(unsigned count=0;node && count<32;++count) {
        uint32_t fields[4];unsigned char name[4];
        if(!read(context,node,fields,sizeof fields) || !fields[3] ||
           !read(context,fields[3],name,sizeof name)) return 1;
        if((name[0]|32)!='h' || (name[1]|32)!='u' ||
           (name[2]|32)!='d' || name[3]!='_') {
            /* Exact, terminated name only. Unknown packages still use the
             * complete plane; never misclassify a real dialog or frontend. */
            static const char overlay[]="Chyron_IG.fng";
            unsigned char full[sizeof overlay];
            if(!read(context,fields[3],full,sizeof full) || full[sizeof overlay-1])return 1;
            for(unsigned i=0;i+1<sizeof overlay;++i)
                if((full[i]|32)!=(overlay[i]|32))return 1;
        } else has_hud=1;
        node=fields[1];
        if(!node) return !has_hud;
    }
    return 1;
}
#endif
