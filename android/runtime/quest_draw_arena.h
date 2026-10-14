#pragma once
#include <stdint.h>
#include <stdlib.h>

/* CPU-only snapshots, consumed by the right-eye replay before reset. Chunks
 * never move: growing this arena must not invalidate an earlier batch. */
typedef struct QuestDrawChunk {
    struct QuestDrawChunk *next;
    size_t capacity, used;
    _Alignas(16) unsigned char data[];
} QuestDrawChunk;

typedef struct {
    QuestDrawChunk *first, *current;
    size_t reserved;
} QuestDrawArena;

#define QUEST_DRAW_CHUNK (1024u * 1024u)
#define QUEST_DRAW_LIMIT (64u * 1024u * 1024u)

static inline void *quest_draw_alloc(QuestDrawArena *a, size_t bytes)
{
    if (!bytes || bytes > QUEST_DRAW_LIMIT) return NULL;
    bytes = (bytes + 15) & ~(size_t)15;
    QuestDrawChunk **link = a->current ? &a->current : &a->first;
    while (*link) {
        QuestDrawChunk *c = *link;
        if (bytes <= c->capacity - c->used) {
            void *p = c->data + c->used;
            c->used += bytes;
            a->current = c;
            return p;
        }
        link = &c->next;
    }
    size_t capacity = bytes > QUEST_DRAW_CHUNK ? bytes : QUEST_DRAW_CHUNK;
    if (capacity > QUEST_DRAW_LIMIT - a->reserved) return NULL;
    QuestDrawChunk *c = malloc(sizeof(*c) + capacity);
    if (!c) return NULL;
    c->next = NULL; c->capacity = capacity; c->used = bytes;
    *link = c; a->current = c; a->reserved += capacity;
    return c->data;
}

static inline void quest_draw_reset(QuestDrawArena *a)
{
    for (QuestDrawChunk *c = a->first; c; c = c->next) c->used = 0;
    a->current = a->first;
}

static inline void quest_draw_destroy(QuestDrawArena *a)
{
    QuestDrawChunk *c = a->first;
    while (c) { QuestDrawChunk *next = c->next; free(c); c = next; }
    *a = (QuestDrawArena){0};
}
