#ifndef QUEST_COCKPIT_WINDOWS_H
#define QUEST_COCKPIT_WINDOWS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define QUEST_COCKPIT_WINDOWS 7
typedef int (*QuestWindowRead)(void *context,uint32_t address,void *out,size_t size);

static inline int quest_cockpit_window_address(uint32_t address,size_t size,uint32_t physical_end)
{
    uint64_t end=(uint64_t)address+size;
    return (address>=0x10000u && end<=0x04000000u) ||
           (address>=0x80000000u && end<=physical_end);
}

/* Retail RVM/RVM_MASK TextureInfo entries. Pixel addresses can move when a
 * race loads; resolve them afresh rather than filtering an allocation ID. */
static inline void quest_cockpit_mirror_masks(QuestWindowRead read,void *context,
                                             const uint32_t info[2],uint32_t out[2],uint32_t physical_end)
{
    memset(out,0,2*sizeof *out);
    for(int i=0;i<2;++i) {
        uint32_t fields[0x74/4];
        if(!quest_cockpit_window_address(info[i],sizeof fields,physical_end) ||
           !read(context,info[i],fields,sizeof fields) || fields[0x6c/4]!=info[i] ||
           !quest_cockpit_window_address(fields[0x70/4],1,physical_end))continue;
        out[i]=fields[0x70/4];
    }
}

/* US retail CarRenderInfo constructor (2177E9): these seven replacement
 * slots are the window materials, including the rear defroster. Resolve the
 * current TextureInfo each camera update: custom tint changes its hash and
 * streaming can relocate its pixels. Never retain old addresses after a
 * missing entry or identify glass by a particular car/texture allocation. */
static inline void quest_cockpit_window_textures_range(QuestWindowRead read,void *context,
                                                uint32_t car,uint32_t out[QUEST_COCKPIT_WINDOWS],uint32_t physical_end)
{
    static const uint32_t semantic[QUEST_COCKPIT_WINDOWS]={
        0x7b220ddfu,0x1b0763a0u,0xe7e4ef49u,0x4cdebfcaU,
        0x60f8b13cu,0x0ab88f5du,0xd195be56u};
    uint32_t info,entries[QUEST_COCKPIT_WINDOWS][4];
    memset(out,0,QUEST_COCKPIT_WINDOWS*sizeof *out);
    if(physical_end!=0x84000000u && physical_end!=0x88000000u)return;
    if(!quest_cockpit_window_address(car,0x558,physical_end) ||
       !read(context,car+0x554,&info,4) ||
       !quest_cockpit_window_address(info,0x778,physical_end) ||
       !read(context,info+0x708,entries,sizeof entries))return;
    for(unsigned i=0;i<QUEST_COCKPIT_WINDOWS;++i) {
        uint32_t texture=entries[i][2],fields[0x74/4];
        if(entries[i][0]!=semantic[i] || !entries[i][1] ||
           !quest_cockpit_window_address(texture,sizeof fields,physical_end) ||
           !read(context,texture,fields,sizeof fields) ||
           fields[0x24/4]!=entries[i][1] || fields[0x6c/4]!=texture ||
           !quest_cockpit_window_address(fields[0x70/4],1,physical_end))continue;
        out[i]=fields[0x70/4];
    }
}
static inline void quest_cockpit_window_textures(QuestWindowRead read,void *context,
                                                uint32_t car,uint32_t out[QUEST_COCKPIT_WINDOWS])
{
    quest_cockpit_window_textures_range(read,context,car,out,0x84000000u);
}

/* The caller restricts this to the player's rigid shell in a cockpit
 * world pass. Mirror glass, opaque trim and UI must survive even if a texture
 * address is reused. Window glass uses depth-tested, non-writing alpha blend. */
static inline int quest_cockpit_window_draw(const uint32_t windows[QUEST_COCKPIT_WINDOWS],
                                           uint32_t texture,const uint32_t *r,int own_body)
{
    if(!own_body || !texture || !r[0x30c/4] || r[0x35c/4] ||
       !r[0x304/4] || r[0x344/4]!=0x302 || r[0x348/4]!=0x303 ||
       !(r[0x1b0c/4]&0x40000000u))return 0;
    for(unsigned i=0;i<QUEST_COCKPIT_WINDOWS;++i)
        if(texture==windows[i])return 1;
    return 0;
}
#endif
