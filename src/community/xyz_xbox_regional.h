#pragma once
#include "xyz_xbox_remote.h"
#include <string.h>
/* Retail Xbox NA tables, audited at 112FC0 (sale filter) and 10F7F0
 * (AI selection). The two padding bytes belong to neither regional list. */
static inline int xyz_xbox_regional_install(const XyzXboxGuest *g){
    uint32_t types=0,locked[2];uint8_t ai[28];char names[2][32];
    if(!g || !g->read || !g->write || !g->read(g->context,0x45af88,&types,4) || !types)return 0;
    if(!xyz_xbox_address(types,46*0x890) || !g->read(g->context,0x3a1270,locked,8) ||
       !g->read(g->context,0x3a03f4,ai,28) || !g->read(g->context,types+6*0x890,names[0],32) ||
       !g->read(g->context,types+7*0x890,names[1],32))return -1;
    if(locked[0]!=7 || locked[1]!=6 || ai[24]!=5 || ai[25]!=18 || ai[26] || ai[27] ||
       memcmp(names[0],"PEUGOT106",10) || memcmp(names[1],"CORSA",6))return -1;
    uint32_t unlocked[2]={UINT32_MAX,UINT32_MAX};uint8_t extra[2]={7,6};
    if(!g->write(g->context,0x3a1270,unlocked,8))return -1;
    if(!g->write(g->context,0x3a040e,extra,2)){g->write(g->context,0x3a1270,locked,8);return -1;}
    return 1;
}
static inline unsigned xyz_xbox_regional_ai_count(int enabled,uint32_t caller,uint32_t list,unsigned count,uint32_t bytes){
    return enabled && caller==0x10f8e6 && list==0x3a03f4 && count==26 && bytes==104?28:count;
}
