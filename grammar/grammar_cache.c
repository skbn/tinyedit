/*
 * tinyedit - Text editor for AmigaOS
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include "grammar_priv.h"

void gc_cache_init(GramCheck *g)
{
    int i;

    if (!g)
        return;

    g->head = -1;
    g->tail = -1;
    g->count = 0;

    for (i = 0; i < GC_CACHE_N; i++)
    {
        g->cache[i].key_hash = 0ul;
        g->cache[i].key_secondary = 0ul;
        g->cache[i].n_issues = 0;
        g->cache[i].prev = -1;
        g->cache[i].next = -1;
    }
}

int gc_cache_find(GramCheck *g, unsigned long h1, unsigned long h2)
{
    int i;

    if (!g || h1 == 0ul)
        return -1;

    for (i = g->head; i != -1; i = g->cache[i].next)
    {
        if (g->cache[i].key_hash == h1 && g->cache[i].key_secondary == h2)
            return i;
    }
    return -1;
}

void gc_cache_unlink(GramCheck *g, int idx)
{
    struct gc_cache_entry *e = NULL;

    if (!g || idx < 0 || idx >= GC_CACHE_N)
        return;

    e = &g->cache[idx];

    if (e->prev != -1)
        g->cache[e->prev].next = e->next;
    else
        g->head = e->next;

    if (e->next != -1)
        g->cache[e->next].prev = e->prev;
    else
        g->tail = e->prev;

    e->prev = -1;
    e->next = -1;
}

void gc_cache_push_front(GramCheck *g, int idx)
{
    struct gc_cache_entry *e = NULL;

    if (!g || idx < 0 || idx >= GC_CACHE_N)
        return;

    e = &g->cache[idx];

    e->prev = -1;
    e->next = g->head;

    if (g->head != -1)
        g->cache[g->head].prev = (short)idx;

    g->head = (short)idx;

    if (g->tail == -1)
        g->tail = (short)idx;
}

int gc_cache_acquire(GramCheck *g)
{
    int i;

    if (!g)
        return -1;

    if (g->count < GC_CACHE_N)
    {
        for (i = 0; i < GC_CACHE_N; i++)
        {
            if (g->cache[i].key_hash == 0ul)
            {
                g->count++;
                return i;
            }
        }
    }

    i = g->tail;

    if (i == -1)
        return 0;

    gc_cache_unlink(g, i);
    return i;
}

void gc_cache_store(GramCheck *g, unsigned long h1, unsigned long h2, const GcIssue *issues, int n)
{
    int idx;
    struct gc_cache_entry *e = NULL;

    if (!g || h1 == 0ul)
        return;

    if (n < 0)
        n = 0;

    if (n > GC_MAX_ISSUES_PER_LINE)
        n = GC_MAX_ISSUES_PER_LINE;

    idx = gc_cache_acquire(g);

    if (idx < 0)
        return;

    e = &g->cache[idx];
    e->key_hash = h1;
    e->key_secondary = h2;
    e->n_issues = (unsigned short)n;

    if (n > 0 && issues)
        memcpy(e->issues, issues, (size_t)n * sizeof(GcIssue));

    gc_cache_push_front(g, idx);
}

void gc_cache_clear(GramCheck *g)
{
    if (g)
        gc_cache_init(g);
}

void gc_cache_invalidate_line(GramCheck *g, const char *utf8_line)
{
    unsigned long h1;
    unsigned long h2;
    size_t len;
    int idx;
    struct gc_cache_entry *e = NULL;

    if (!g || !utf8_line)
        return;

    len = strlen(utf8_line);
    h1 = gc_hash32(utf8_line, len);
    h2 = gc_hash32b(utf8_line, len);

    idx = gc_cache_find(g, h1, h2);

    if (idx < 0)
        return;

    e = &g->cache[idx];

    gc_cache_unlink(g, idx);

    e->key_hash = 0;
    e->key_secondary = 0;
    e->n_issues = 0;

    if (g->count > 0)
        g->count--;
}
