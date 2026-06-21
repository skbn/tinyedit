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

#include "hyph_wrap.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifdef HAVE_HYPHEN
#include <hyphen.h>
#include <dirent.h>
#include <sys/types.h>
#endif

typedef struct
{
    char word[HYPH_CACHE_KEY_MAX]; /* '\0' = empty slot */
    unsigned char nbreaks;
    unsigned char breaks[HYPH_MAX_BREAKS];
    short prev;
    short next;
} HyphEntry;

struct HyphDict
{
#ifdef HAVE_HYPHEN
    HyphenDict *dict;
#endif

    short head;
    short tail;
    short count;

    HyphEntry cache[HYPH_CACHE_N];
};

#ifdef HAVE_HYPHEN
static void hcache_init(HyphDict *h)
{
    int i;

    h->head = -1;
    h->tail = -1;
    h->count = 0;

    for (i = 0; i < HYPH_CACHE_N; i++)
    {
        h->cache[i].word[0] = '\0';
        h->cache[i].nbreaks = 0;
        h->cache[i].prev = -1;
        h->cache[i].next = -1;
    }
}

static void hcache_unlink(HyphDict *h, int idx)
{
    HyphEntry *e;

    e = &h->cache[idx];

    if (e->prev != -1)
        h->cache[e->prev].next = e->next;
    else
        h->head = e->next;

    if (e->next != -1)
        h->cache[e->next].prev = e->prev;
    else
        h->tail = e->prev;

    e->prev = -1;
    e->next = -1;
}

static void hcache_push_front(HyphDict *h, int idx)
{
    HyphEntry *e;

    e = &h->cache[idx];
    e->prev = -1;
    e->next = h->head;

    if (h->head != -1)
        h->cache[h->head].prev = (short)idx;

    h->head = (short)idx;

    if (h->tail == -1)
        h->tail = (short)idx;
}

static int hcache_find(const HyphDict *h, const char *word)
{
    int i;

    for (i = h->head; i != -1; i = h->cache[i].next)
    {
        if (strcmp(h->cache[i].word, word) == 0)
            return i;
    }

    return -1;
}

static int hcache_acquire_slot(HyphDict *h)
{
    int i;

    if (h->count < HYPH_CACHE_N)
    {
        for (i = 0; i < HYPH_CACHE_N; i++)
        {
            if (h->cache[i].word[0] == '\0')
            {
                h->count++;
                return i;
            }
        }
    }

    i = h->tail;
    hcache_unlink(h, i);

    return i;
}

static void hcache_put(HyphDict *h, const char *word, const unsigned char *breaks, int nbreaks)
{
    int idx, i;
    size_t wlen;

    wlen = strlen(word);

    /* Reject empty word (corruption risk) */
    if (wlen == 0)
        return;

    if (wlen >= HYPH_CACHE_KEY_MAX)
        return;

    if (nbreaks > HYPH_MAX_BREAKS)
        nbreaks = HYPH_MAX_BREAKS;

    idx = hcache_acquire_slot(h);

    memcpy(h->cache[idx].word, word, wlen + 1);

    h->cache[idx].nbreaks = (unsigned char)nbreaks;

    for (i = 0; i < nbreaks; i++)
        h->cache[idx].breaks[i] = breaks[i];

    hcache_push_front(h, idx);
}

#endif

void hyph_cache_clear(HyphDict *h)
{
#ifdef HAVE_HYPHEN
    if (h)
        hcache_init(h);
#endif
}

HyphDict *hyph_new(const char *dict_path)
{
#ifdef HAVE_HYPHEN
    HyphDict *h = NULL;

    if (!dict_path)
        return NULL;

    h = (HyphDict *)calloc(1, sizeof(*h));

    if (!h)
        return NULL;

    h->dict = hnj_hyphen_load(dict_path);

    if (!h->dict)
    {
        free(h);
        return NULL;
    }

    hcache_init(h);

    return h;
#else
    return NULL;
#endif
}

void hyph_free(HyphDict *h)
{
    if (!h)
        return;

#ifdef HAVE_HYPHEN
    if (h->dict)
        hnj_hyphen_free(h->dict);
#endif

    free(h);
}

#ifdef HAVE_HYPHEN

/* Run hyphenation and translate output to position array */
static int hyph_compute(HyphDict *h, const char *word, int word_len, unsigned char *out_pos, int *out_count)
{
    /* Buffer sizes: hyphens (word+5), hyphword (5*word+5) */
    char hyphens[HYPH_CACHE_KEY_MAX + 5];
    char hyphword[5 * HYPH_CACHE_KEY_MAX + 5];
    char **rep = NULL;
    int *pos = NULL;
    int *cut = NULL;
    int i, n;

    *out_count = 0;

    if (word_len < 4 || word_len >= HYPH_CACHE_KEY_MAX)
        return 0; /* too short to hyphenate, or too long for buffers */

    if (hnj_hyphen_hyphenate2(h->dict, word, word_len, hyphens, hyphword, &rep, &pos, &cut) != 0)
    {
        /* On error, free rep/pos/cut */
        *out_count = 0;
    }
    else
    {
        /* Odd values in hyphens[] mark break points */
        n = 0;

        for (i = 0; i < word_len; i++)
        {
            if ((hyphens[i] & 1) && n < HYPH_MAX_BREAKS)
                out_pos[n++] = (unsigned char)(i + 1);
        }

        *out_count = n;
    }

    /* Free rep/pos/cut (allocated by libhyphen) */
    if (rep)
    {
        for (i = 0; i < word_len; i++)
        {
            if (rep[i])
                free(rep[i]);
        }

        free(rep);
    }

    if (pos)
        free(pos);

    if (cut)
        free(cut);

    return (*out_count > 0) ? 1 : 0;
}
#endif

int hyph_breakpoints(HyphDict *h, const char *word, int word_len, int *out_pos, int *out_count)
{
#ifdef HAVE_HYPHEN
    int i, n;
    unsigned char tmp[HYPH_MAX_BREAKS];

    if (out_count)
        *out_count = 0;

    if (!h || !h->dict || !word || word_len <= 0 || !out_pos || !out_count)
        return 0;

    /* Reject zero-length (corrupts LRU list) */
    if (word[0] == '\0')
        return 0;

    /* Cache fast path */
    if (word_len < HYPH_CACHE_KEY_MAX)
    {
        int idx = hcache_find(h, word);

        if (idx != -1)
        {
            hcache_unlink(h, idx);
            hcache_push_front(h, idx);

            n = h->cache[idx].nbreaks;

            for (i = 0; i < n; i++)
                out_pos[i] = h->cache[idx].breaks[i];

            *out_count = n;

            return n > 0 ? 1 : 0;
        }
    }

    if (!hyph_compute(h, word, word_len, tmp, &n))
    {
        /* Cache negative result (avoids re-asking libhyphen) */
        hcache_put(h, word, tmp, 0);
        return 0;
    }

    hcache_put(h, word, tmp, n);

    for (i = 0; i < n; i++)
        out_pos[i] = tmp[i];

    *out_count = n;

    return 1;
#else

    if (out_count)
        *out_count = 0;

    return 0;
#endif
}

int hyph_is_available(void)
{
#ifdef HAVE_HYPHEN
    return 1;
#else
    return 0;
#endif
}

char **hyph_list_dictionaries(const char *dir_path, int *n_dicts)
{
#ifdef HAVE_HYPHEN

    char **result = NULL;
    int count = 0;
    int capacity = 0;
    DIR *dir = NULL;
    struct dirent *entry = NULL;

    if (!dir_path || !n_dicts)
        return NULL;

    *n_dicts = 0;

    dir = opendir(dir_path);

    if (!dir)
        return NULL;

    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;
        size_t len = strlen(name);

        /* Look for *.dic files (any name ending in .dic) */
        if (len > 4 && strcmp(name + len - 4, ".dic") == 0)
        {
            /* Extract dictionary name */
            char *dict_name = NULL;
            size_t start = 0;     /* start from beginning */
            size_t end = len - 4; /* before ".dic" */

            if (end <= start)
                continue;

            dict_name = malloc(end - start + 1);

            if (!dict_name)
                continue;

            memcpy(dict_name, name + start, end - start);
            dict_name[end - start] = '\0';

            /* Add to array */
            if (count >= capacity)
            {
                int new_cap = (capacity == 0) ? 16 : capacity * 2;
                char **new_result = realloc(result, new_cap * sizeof(char *));

                if (!new_result)
                {
                    free(dict_name);
                    continue;
                }

                result = new_result;
                capacity = new_cap;
            }

            result[count++] = dict_name;
        }
    }

    closedir(dir);

    if (count == 0)
    {
        if (result)
            free(result);

        return NULL;
    }

    *n_dicts = count;

    return result;
#else

    if (n_dicts)
        *n_dicts = 0;

    return NULL;
#endif
}

void hyph_free_dictionaries(char **dicts, int n_dicts)
{
    int i;

    if (!dicts)
        return;

    for (i = 0; i < n_dicts; i++)
    {
        if (dicts[i])
            free(dicts[i]);
    }

    free(dicts);
}
