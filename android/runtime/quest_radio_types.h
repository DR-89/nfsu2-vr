#pragma once

enum { QUEST_RADIO_BANDS=16 };
enum { QUEST_MUSIC_AUTO, QUEST_MUSIC_CUSTOM_ONLY, QUEST_MUSIC_EA_ONLY, QUEST_MUSIC_MODES };

enum { QUEST_RADIO_NONE, QUEST_RADIO_VOLUME, QUEST_RADIO_PREV, QUEST_RADIO_PLAY, QUEST_RADIO_NEXT,
       QUEST_SEAT_FORWARD, QUEST_SEAT_BACK, QUEST_SEAT_UP, QUEST_SEAT_DOWN };
typedef struct { int action;float volume; } QuestRadioCommand;
typedef struct { char title[64],artist[64]; } QuestRadioSong;
