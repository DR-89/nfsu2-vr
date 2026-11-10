#include "../vr_texpacks.h"
/* The owned PC and Xbox 131-record TrackInfo chunks are byte-identical.
 * Only the six map framing fields accepted by the pack reader are changed.
 * The feature is immutable for this process; every fresh load is handled. */
static void community_map_textures(void)
{
    unsigned edits=0;const VRTrackMapEdit *e=vr_texpacks_map_edits(&edits);
    if(!edits)return;
    uint32_t table=MEM32(0x43d110),count=MEM32(0x43d114);
    if(!table || !count || count>XYZ_TRACK_LIMIT || !xyz_xbox_address(table,(size_t)count*0x128))return;
    for(unsigned k=0;k<edits;k++)for(unsigned i=0;i<count;i++){
        uint32_t row=table+i*0x128;
        if(MEM16(row+0x8a)==e[k].track){MEMF(row+e[k].offset)=e[k].value;break;}
    }
}
