#pragma once
#include <stdlib.h>
#include "quest_radio.h"
#include "quest_bitmap_font.h"

enum { QUEST_RADIO_COLUMNS=20 };
/* One std140 uvec4 per cell, two 5x7 glyphs (title and artist). The extra
 * cell enters from the right while a long line scrolls. */
typedef struct {
    uint32_t cells[QUEST_RADIO_COLUMNS+1][4];
    float scroll[2];
    uint32_t hover_mask,padding;
} QuestRadioLCD;
typedef struct { uint32_t bits[128][2]; } QuestRadioFont;

static inline void quest_radio_font_init(QuestRadioFont *font)
{
    memset(font,0,sizeof *font);
    for(size_t i=0;i<sizeof quest_vr_font/sizeof *quest_vr_font;++i) {
        unsigned c=(unsigned char)quest_vr_font[i].c;
        const char *rows=quest_vr_font[i].rows;
        for(int y=0;y<7;++y) {
            char hex[]={rows[2*y],rows[2*y+1],0};
            font->bits[c][y/4]|=(uint32_t)strtoul(hex,NULL,16)<<(5*(y%4));
        }
        if(c>='A' && c<='Z')memcpy(font->bits[c+32],font->bits[c],sizeof font->bits[c]);
    }
}
static inline void quest_radio_lcd(QuestRadioLCD *lcd,const QuestRadioFont *font,
                                   const QuestRadioSong *song,uint32_t elapsed)
{
    memset(lcd,0,sizeof *lcd);
    const char *lines[]={song->title[0]?song->title:"EA TRAX",song->artist};
    for(int row=0;row<2;++row) {
        int length=(int)strlen(lines[row]);
        float offset=0;
        if(length>QUEST_RADIO_COLUMNS) {
            /* Hold at both ends so the first and last words can be read. */
            uint32_t travel=(uint32_t)(length-QUEST_RADIO_COLUMNS)*350;
            uint32_t tick=elapsed%(travel+3600);
            if(tick>1800)offset=fminf((float)(tick-1800),(float)travel)/350.f;
        }
        int first=(int)offset;
        lcd->scroll[row]=offset-first;
        for(int col=0;col<=QUEST_RADIO_COLUMNS && first+col<length;++col) {
            unsigned c=(unsigned char)lines[row][first+col];
            if(c<128)memcpy(&lcd->cells[col][2*row],font->bits[c],sizeof font->bits[c]);
        }
    }
}
