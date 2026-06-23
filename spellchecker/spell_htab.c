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

#include "spell_priv.h"
#include <limits.h>

int htab_init(struct spell *m, int initial_pow2)
{
    int sz = 1;

    while (sz < initial_pow2)
        sz <<= 1;

    m->htab = (struct ms_hentry *)calloc((size_t)sz, sizeof(struct ms_hentry));

    if (!m->htab)
        return -1;

    m->htab_size = sz;
    m->htab_count = 0;

    return 0;
}

void htab_insert_into(struct ms_hentry *tab, int sz, char *word, ms_cp *flags, int n_flags, unsigned char attrs)
{
    unsigned long h = ms_hash(word);
    int mask = sz - 1;
    int i = (int)(h & (unsigned long)mask);

    while (tab[i].word)
    {
        if (strcmp(tab[i].word, word) == 0)
        {
            /* Dup: merge flags (accumulate new ones with existing) */
            int k;

            if (n_flags > 0 && flags)
            {
                int new_count = tab[i].n_flags + n_flags;
                unsigned long *merged = (unsigned long *)realloc(tab[i].flags, (size_t)new_count * sizeof(ms_cp));

                if (merged)
                {
                    tab[i].flags = merged;

                    for (k = 0; k < n_flags; k++)
                    {
                        if (!has_flag(tab[i].flags, tab[i].n_flags, flags[k]))
                        {
                            tab[i].flags[tab[i].n_flags++] = flags[k];
                        }
                    }
                }
            }

            /* Merge attrs by OR (any decl on either copy sticks) */
            tab[i].attrs |= attrs;

            free(word);
            free(flags);

            return;
        }

        i = (i + 1) & mask;
    }

    tab[i].word = word;
    tab[i].flags = flags;
    tab[i].n_flags = n_flags;
    tab[i].attrs = attrs;
}

int htab_resize(struct spell *m, int new_sz)
{
    struct ms_hentry *nt = (struct ms_hentry *)calloc((size_t)new_sz, sizeof(struct ms_hentry));
    int i;

    if (!nt)
        return -1;

    for (i = 0; i < m->htab_size; i++)
    {
        if (m->htab[i].word)
            htab_insert_into(nt, new_sz, m->htab[i].word, m->htab[i].flags, m->htab[i].n_flags, m->htab[i].attrs);
    }

    free(m->htab);

    m->htab = nt;
    m->htab_size = new_sz;

    return 0;
}

void htab_insert(struct spell *m, const char *word, const char *flags_str)
{
    char *w = NULL;
    ms_cp *flags = NULL;
    int n_flags = 0;
    int new_size;
    unsigned char attrs;
    int k;

    /* Resize if over 70% */
    if (m->htab_count * 10 >= m->htab_size * 7)
    {
        /* Check for overflow before doubling */
        if (m->htab_size > INT_MAX / 2)
            return;

        new_size = m->htab_size * 2;

        if (htab_resize(m, new_size) != 0)
            return;
    }

    w = ms_strdup(word);

    /* Check if flags_str is AF alias (all digits): expand using af_flags[i] instead of parsing */
    if (m->n_af > 0 && flags_str && *flags_str)
    {
        const char *p = flags_str;
        int is_alias = 1;

        while (*p)
        {
            if (*p < '0' || *p > '9')
            {
                is_alias = 0;
                break;
            }

            p++;
        }

        if (is_alias)
        {
            int idx = atoi(flags_str);

            if (idx >= 1 && idx <= m->n_af)
            {
                /* Duplicate alias's flag array so htab entry owns it (free_aff doesn't track it) */
                int nf = m->af_n_flags[idx];

                if (nf > 0)
                {
                    flags = (ms_cp *)malloc((size_t)nf * sizeof(ms_cp));

                    if (flags)
                    {
                        memcpy(flags, m->af_flags[idx], (size_t)nf * sizeof(ms_cp));
                        n_flags = nf;
                    }
                }

                if (!w)
                {
                    free(flags);
                    return;
                }

                if (!htab_find(m, w))
                    m->htab_count++;

                attrs = 0;

                for (k = 0; k < n_flags; k++)
                {
                    if (m->flag_nosuggest && flags[k] == m->flag_nosuggest)
                        attrs |= MS_ATTR_NOSUGGEST;

                    if (m->flag_forbidden && flags[k] == m->flag_forbidden)
                        attrs |= MS_ATTR_FORBIDDEN;

                    if (m->flag_keepcase && flags[k] == m->flag_keepcase)
                        attrs |= MS_ATTR_KEEPCASE;
                }

                htab_insert_into(m->htab, m->htab_size, w, flags, n_flags, attrs);

                return;
            }
        }
    }

    flags = parse_flags(flags_str, m->flag_type, &n_flags);

    if (!w)
    {
        free(flags);
        return;
    }

    if (!htab_find(m, w))
        m->htab_count++;

    attrs = 0;

    for (k = 0; k < n_flags; k++)
    {
        if (m->flag_nosuggest && flags[k] == m->flag_nosuggest)
            attrs |= MS_ATTR_NOSUGGEST;

        if (m->flag_forbidden && flags[k] == m->flag_forbidden)
            attrs |= MS_ATTR_FORBIDDEN;

        if (m->flag_keepcase && flags[k] == m->flag_keepcase)
            attrs |= MS_ATTR_KEEPCASE;
    }

    htab_insert_into(m->htab, m->htab_size, w, flags, n_flags, attrs);
}

struct ms_hentry *htab_find(struct spell *m, const char *word)
{
    unsigned long h = ms_hash(word);
    int mask = m->htab_size - 1;
    int i = (int)(h & (unsigned long)mask);

    while (m->htab[i].word)
    {
        if (strcmp(m->htab[i].word, word) == 0)
            return &m->htab[i];

        i = (i + 1) & mask;
    }

    return NULL;
}

void check_cache_init(struct spell *m)
{
    int i;

    m->head = -1;
    m->tail = -1;
    m->cache_count = 0;

    for (i = 0; i < SPELL_CACHE_N; i++)
    {
        m->cache[i].key[0] = '\0';
        m->cache[i].prev = -1;
        m->cache[i].next = -1;
    }
}

void check_cache_unlink(struct spell *m, int idx)
{
    struct ms_check_cache *e = &m->cache[idx];

    if (e->prev != -1)
        m->cache[e->prev].next = e->next;
    else
        m->head = e->next;

    if (e->next != -1)
        m->cache[e->next].prev = e->prev;
    else
        m->tail = e->prev;

    e->prev = -1;
    e->next = -1;
}

void check_cache_push_front(struct spell *m, int idx)
{
    struct ms_check_cache *e = &m->cache[idx];

    e->prev = -1;
    e->next = m->head;

    if (m->head != -1)
        m->cache[m->head].prev = (short)idx;

    m->head = (short)idx;

    if (m->tail == -1)
        m->tail = (short)idx;
}

int check_cache_find(struct spell *m, const char *word)
{
    int i;

    for (i = m->head; i != -1; i = m->cache[i].next)
    {
        if (strcmp(m->cache[i].key, word) == 0)
            return i;
    }

    return -1;
}

int check_cache_acquire(struct spell *m)
{
    int i;

    if (m->cache_count < SPELL_CACHE_N)
    {
        for (i = 0; i < SPELL_CACHE_N; i++)
        {
            if (m->cache[i].key[0] == '\0')
            {
                m->cache_count++;
                return i;
            }
        }
    }

    i = m->tail;

    if (i == -1)
        return 0;

    check_cache_unlink(m, i);

    return i;
}

void check_cache_put(struct spell *m, const char *word, int res)
{
    int idx;
    size_t wlen = strlen(word);

    if (wlen == 0 || wlen >= SPELL_MAX_WORD)
        return;

    idx = check_cache_acquire(m);

    memcpy(m->cache[idx].key, word, wlen + 1);

    m->cache[idx].result = (unsigned char)(res ? 1 : 0);

    check_cache_push_front(m, idx);
}

void spell_cache_clear(struct spell *s)
{
    if (s)
        check_cache_init(s);
}

const char *spell_get_encoding(struct spell *s)
{
    return s ? s->encoding : NULL;
}
