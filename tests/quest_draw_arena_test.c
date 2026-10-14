#include "quest_draw_arena.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    QuestDrawArena a = {0};
    unsigned char *p = quest_draw_alloc(&a, 19);
    assert(p && !((uintptr_t)p & 15));
    memset(p, 0xa5, 19);
    void *q = quest_draw_alloc(&a, QUEST_DRAW_CHUNK);
    assert(q && !((uintptr_t)q & 15));
    memset(q, 0x31, QUEST_DRAW_CHUNK);
    for (unsigned i = 0; i < 19; ++i) assert(p[i] == 0xa5);
    assert(a.reserved == 2 * QUEST_DRAW_CHUNK);
    quest_draw_reset(&a);
    assert(quest_draw_alloc(&a, 19) == p);
    assert(quest_draw_alloc(&a, QUEST_DRAW_CHUNK) == q);
    assert(a.reserved == 2 * QUEST_DRAW_CHUNK);
    /* A batch larger than the normal chunk, without moving existing data. */
    assert(quest_draw_alloc(&a, 2 * QUEST_DRAW_CHUNK + 7));
    assert(p[18] == 0xa5);
    assert(!quest_draw_alloc(&a, SIZE_MAX));
    assert(!quest_draw_alloc(&a, 0));
    quest_draw_destroy(&a);
    assert(quest_draw_alloc(&a, QUEST_DRAW_LIMIT));
    assert(!quest_draw_alloc(&a, 1));
    quest_draw_reset(&a);
    assert(quest_draw_alloc(&a, QUEST_DRAW_LIMIT));
    quest_draw_destroy(&a);
    assert(!a.first && !a.current && !a.reserved);
    puts("Quest draw arena: alignment, pointer lifetime, reuse, capacity OK");
    return 0;
}
