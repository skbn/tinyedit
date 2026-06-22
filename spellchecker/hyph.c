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

#include "hyph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef PLATFORM_AMIGA
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#endif

#include "../core/utf8.h"

static char *mh_strdup(const char *s)
{
    size_t n;
    char *r = NULL;

    if (!s)
        return NULL;

    n = strlen(s) + 1;
    r = (char *)malloc(n);

    if (r)
        memcpy(r, s, n);

    return r;
}

static int mh_readline(FILE *fp, char *buf, size_t sz)
{
    int c = 0;
    int n = 0;

    if (sz == 0)
        return -1;

    for (;;)
    {
        c = fgetc(fp);

        if (c == EOF)
        {
            if (n == 0)
                return -1;

            break;
        }

        if (c == '\n')
            break;

        if (c == '\r')
            continue;

        if ((size_t)n < sz - 1)
            buf[n++] = (char)c;
    }

    buf[n] = '\0';

    return n;
}

/* trim trailing spaces */
static void mh_rstrip(char *s)
{
    int n = (int)strlen(s);

    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
}

static void cache_init(struct hyph *h)
{
    int i;

    h->head = -1;
    h->tail = -1;
    h->count = 0;

    for (i = 0; i < HYPH_CACHE_N; i++)
    {
        h->cache[i].key[0] = '\0';
        h->cache[i].hyph[0] = '\0';
        h->cache[i].prev = -1;
        h->cache[i].next = -1;
    }
}

static void cache_unlink(struct hyph *h, int idx)
{
    struct hyph_cache_entry *e = &h->cache[idx];

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

static void cache_push_front(struct hyph *h, int idx)
{
    struct hyph_cache_entry *e = &h->cache[idx];

    e->prev = -1;
    e->next = h->head;

    if (h->head != -1)
        h->cache[h->head].prev = (short)idx;

    h->head = (short)idx;

    if (h->tail == -1)
        h->tail = (short)idx;
}

static int cache_find(struct hyph *h, const char *word)
{
    int i;

    for (i = h->head; i != -1; i = h->cache[i].next)
    {
        if (strcmp(h->cache[i].key, word) == 0)
            return i;
    }

    return -1;
}

static int cache_acquire(struct hyph *h)
{
    int i;

    if (h->count < HYPH_CACHE_N)
    {
        for (i = 0; i < HYPH_CACHE_N; i++)
        {
            if (h->cache[i].key[0] == '\0')
            {
                h->count++;
                return i;
            }
        }
    }

    i = h->tail;
    cache_unlink(h, i);

    return i;
}

void hyph_cache_clear(struct hyph *h)
{
    if (h)
        cache_init(h);
}

/* parse pattern like "2b1c" into letters and levels */
static int parse_pattern(const char *src, struct hyph_pattern *p)
{
    size_t n = strlen(src);
    size_t i;
    unsigned char *letters = NULL;
    unsigned char *levels = NULL;
    int ln = 0;  /* index in letters */
    int lvn = 0; /* index in levels */

    if (n == 0)
        return -1;

    letters = (unsigned char *)malloc(n + 1);
    levels = (unsigned char *)malloc(n + 2);

    if (!letters || !levels)
    {
        free(letters);
        free(levels);
        return -1;
    }

    memset(letters, 0, n + 1);
    memset(levels, 0, n + 2);

    for (i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)src[i];

        if (c >= '0' && c <= '9')
        {
            /* digit: priority before next letter */
            levels[lvn] = (unsigned char)(c - '0');
        }
        else
        {
            letters[ln++] = c;
            lvn++;
        }
    }

    /* extra level for end */
    lvn = ln + 1;

    p->letters = letters;
    p->levels = levels;
    p->llen = (unsigned short)ln;

    return 0;
}

/* qsort comparator */
static int pattern_cmp(const void *a, const void *b)
{
    const struct hyph_pattern *pa = (const struct hyph_pattern *)a;
    const struct hyph_pattern *pb = (const struct hyph_pattern *)b;
    int la = pa->llen;
    int lb = pb->llen;
    int m = (la < lb) ? la : lb;
    int c = memcmp(pa->letters, pb->letters, (size_t)m);

    if (c != 0)
        return c;

    return la - lb;
}

struct hyph *hyph_new(const char *path)
{
    struct hyph *h = NULL;
    FILE *fp = NULL;
    char line[1024];
    int cap = 8192;
    int n = 0;
    int c;
    int i;
    int first_byte;

    if (!path)
        return NULL;

    fp = fopen(path, "rb");

    if (!fp)
        return NULL;

    h = (struct hyph *)calloc(1, sizeof(*h));

    if (!h)
    {
        fclose(fp);
        return NULL;
    }

    h->lhmin = 2;
    h->rhmin = 2;

    /* line 1: encoding */
    if (mh_readline(fp, line, sizeof(line)) < 0)
    {
        fclose(fp);
        free(h);
        return NULL;
    }

    strncpy(h->enc, line, sizeof(h->enc) - 1);
    h->enc[sizeof(h->enc) - 1] = '\0';

    h->pats = (struct hyph_pattern *)malloc((size_t)cap * sizeof(*h->pats));

    if (!h->pats)
    {
        fclose(fp);
        free(h);
        return NULL;
    }

    while (mh_readline(fp, line, sizeof(line)) >= 0)
    {
        mh_rstrip(line);

        if (line[0] == '\0' || line[0] == '%')
            continue;

        /* directives */
        if (strncmp(line, "LEFTHYPHENMIN ", 14) == 0)
        {
            h->lhmin = atoi(line + 14);
            continue;
        }

        if (strncmp(line, "RIGHTHYPHENMIN ", 15) == 0)
        {
            h->rhmin = atoi(line + 15);
            continue;
        }

        if (strncmp(line, "COMPOUNDLEFTHYPHENMIN ", 22) == 0)
            continue;

        if (strncmp(line, "COMPOUNDRIGHTHYPHENMIN ", 23) == 0)
            continue;

        if (strncmp(line, "NEXTLEVEL ", 10) == 0)
        {
            /* OOo extension: stop parsing here */
            break;
        }

        /* it's a pattern */
        if (n == cap)
        {
            struct hyph_pattern *ng = NULL;

            cap *= 2;

            ng = (struct hyph_pattern *)realloc(h->pats, (size_t)cap * sizeof(*h->pats));

            if (!ng)
            {
                fclose(fp);
                hyph_free(h);
                return NULL;
            }

            h->pats = ng;
        }

        if (parse_pattern(line, &h->pats[n]) == 0)
            n++;
    }

    fclose(fp);

    h->n_pats = n;

    /* sort and build index by first byte */
    qsort(h->pats, (size_t)n, sizeof(struct hyph_pattern), pattern_cmp);

    for (i = 0; i < 257; i++)
        h->idx_first[i] = n;

    for (i = n - 1; i >= 0; i--)
    {
        first_byte = (h->pats[i].llen > 0) ? h->pats[i].letters[0] : 0;
        h->idx_first[first_byte] = i;
    }

    /* backward propagation for index */
    for (i = 255; i >= 0; i--)
    {
        if (h->idx_first[i] == n)
            h->idx_first[i] = h->idx_first[i + 1];
    }

    cache_init(h);
    return h;
}

void hyph_free(struct hyph *h)
{
    int i;

    if (!h)
        return;

    if (h->pats)
    {
        for (i = 0; i < h->n_pats; i++)
        {
            free(h->pats[i].letters);
            free(h->pats[i].levels);
        }

        free(h->pats);
    }

    free(h);
}

const char *hyph_get_encoding(struct hyph *h)
{
    return h ? h->enc : NULL;
}

/* check if pattern matches at position */
static int pattern_matches(const struct hyph_pattern *p, const unsigned char *nw, int nw_len, int pos)
{
    if (pos + p->llen > nw_len)
        return 0;

    return memcmp(p->letters, nw + pos, p->llen) == 0;
}

/* apply all patterns at position */
static void apply_patterns_at(struct hyph *h, const unsigned char *nw, int nw_len, int pos, unsigned char *lev)
{
    int first_byte = nw[pos];
    int start = h->idx_first[first_byte];
    int end = h->idx_first[first_byte + 1];
    int i, k;

    for (i = start; i < end; i++)
    {
        const struct hyph_pattern *p = &h->pats[i];

        if (!pattern_matches(p, nw, nw_len, pos))
            continue;

        /* apply levels accumulating max */
        for (k = 0; k <= p->llen; k++)
        {
            if (p->levels[k] > lev[pos + k])
                lev[pos + k] = p->levels[k];
        }
    }
}

/* check if byte position is UTF-8 char start */
static int is_char_start(const unsigned char *nw, int pos)
{
    unsigned char c = nw[pos];

    return (c & 0xC0) != 0x80;
}

int hyph_hyphenate(struct hyph *h, const char *word, char *out, size_t outsz)
{
    unsigned char nw[HYPH_MAX_WORD + 4];
    unsigned char lev[HYPH_MAX_WORD + 4 + 1];
    size_t wlen;
    int i;
    int nw_len;
    int pos;
    int written;
    int cache_idx;
    int char_index; /* character index (not byte) */
    int total_chars;

    if (!h || !word || !out || outsz < 2)
        return -1;

    wlen = strlen(word);

    if (wlen == 0 || wlen >= HYPH_MAX_WORD)
    {
        if (outsz > wlen)
        {
            memcpy(out, word, wlen + 1);
            return (int)wlen;
        }

        return -1;
    }

    /* check cache */
    cache_idx = cache_find(h, word);

    if (cache_idx != -1)
    {
        cache_unlink(h, cache_idx);
        cache_push_front(h, cache_idx);

        written = (int)strlen(h->cache[cache_idx].hyph);

        if ((size_t)written + 1 > outsz)
            return -1;

        memcpy(out, h->cache[cache_idx].hyph, (size_t)written + 1);

        return written;
    }

    /* build nw = "." + lowercase(word) + "." */
    nw[0] = '.';

    memcpy(nw + 1, word, wlen);

    nw[wlen + 1] = '.';
    nw[wlen + 2] = '\0';

    nw_len = (int)wlen + 2;

    utf8_tolower((char *)nw); /* Unicode-aware case folding */

    /* apply all patterns */
    memset(lev, 0, sizeof(lev));

    for (pos = 0; pos < nw_len; pos++)
        apply_patterns_at(h, nw, nw_len, pos, lev);

    /* build output with hyphens */
    total_chars = 0;

    for (i = 1; i < nw_len - 1; i++)
    {
        /* without the '.' */
        if (is_char_start(nw, i))
            total_chars++;
    }

    written = 0;
    char_index = 0;

    for (i = 1; i < nw_len - 1; i++)
    {
        /* without the '.' of nw from original (preserves case) */
        unsigned char c = (unsigned char)word[i - 1];

        if (is_char_start(nw, i))
        {
            /* insert hyphen before char */
            if (char_index > 0 && lev[i] & 1 && char_index >= h->lhmin && (total_chars - char_index) >= h->rhmin)
            {
                if ((size_t)(written + 1) >= outsz)
                    break;

                out[written++] = '=';
            }

            char_index++;
        }

        if ((size_t)(written + 1) >= outsz)
            break;

        out[written++] = (char)c;
    }

    out[written] = '\0';

    /* store in cache */
    cache_idx = cache_acquire(h);

    strncpy(h->cache[cache_idx].key, word, HYPH_MAX_WORD - 1);
    h->cache[cache_idx].key[HYPH_MAX_WORD - 1] = '\0';

    strncpy(h->cache[cache_idx].hyph, out, sizeof(h->cache[cache_idx].hyph) - 1);
    h->cache[cache_idx].hyph[sizeof(h->cache[cache_idx].hyph) - 1] = '\0';

    cache_push_front(h, cache_idx);

    return written;
}

/* hyph_new and hyph_free are public names */
int hyph_breakpoints(HyphDict *h, const char *word, int word_len, int *out_pos, int *out_count)
{
    char hyph_buf[HYPH_MAX_WORD * 2];
    int i;
    int n;
    int written;

    if (out_count)
        *out_count = 0;

    if (!h || !word || word_len <= 0 || !out_pos || !out_count)
        return -1;

    written = hyph_hyphenate(h, word, hyph_buf, sizeof(hyph_buf));

    if (written < 0)
        return -1;

    n = 0;

    for (i = 0; i < written && n < HYPH_MAX_BREAKS; i++)
    {
        if (hyph_buf[i] == '=')
            continue;

        if (i > 0 && hyph_buf[i - 1] == '=')
        {
            /* break point before character */
            out_pos[n] = i - n - 1;
            n++;
        }
    }

    *out_count = n;

    return n;
}

static int ends_with_dic(const unsigned char *name)
{
    size_t len;

    if (!name)
        return 0;

    len = strlen((const char *)name);

    if (len < 4)
        return 0;

    return (tolower(name[len - 4]) == '.' &&
            tolower(name[len - 3]) == 'd' &&
            tolower(name[len - 2]) == 'i' &&
            tolower(name[len - 1]) == 'c');
}

static char *extract_dict_name(const unsigned char *name)
{
    size_t len;
    char *base = NULL;

    if (!name)
        return NULL;

    len = strlen((const char *)name);

    if (len < 4)
        return NULL;

    base = (char *)malloc(len - 3);

    if (!base)
        return NULL;

    memcpy(base, name, len - 4);
    base[len - 4] = '\0';

    return base;
}

char **hyph_list_dictionaries(const char *dir_path, int *n_dicts)
{
    char **list = NULL;
    int cap;
    int count;

    if (n_dicts)
        *n_dicts = 0;

    if (!dir_path || !n_dicts)
        return NULL;

    cap = 16;
    count = 0;
    list = (char **)malloc((size_t)cap * sizeof(char *));

    if (!list)
        return NULL;

#ifdef PLATFORM_AMIGA
    {
        BPTR lock;
        struct FileInfoBlock *fib = NULL;

        lock = Lock((CONST_STRPTR)dir_path, ACCESS_READ);

        if (lock)
        {
            fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

            if (fib && Examine(lock, fib))
            {
                while (ExNext(lock, fib))
                {
                    if (fib->fib_DirEntryType < 0 && ends_with_dic(fib->fib_FileName))
                    {
                        char *base = extract_dict_name(fib->fib_FileName);

                        if (base)
                        {
                            if (count >= cap)
                            {
                                char **g = NULL;

                                cap *= 2;
                                g = (char **)realloc(list, (size_t)cap * sizeof(char *));

                                if (!g)
                                {
                                    free(base);
                                    continue;
                                }

                                list = g;
                            }

                            list[count++] = base;
                        }
                    }
                }
            }

            if (fib)
                FreeDosObject(DOS_FIB, fib);

            UnLock(lock);
        }
    }
#endif

    if (count == 0)
    {
        free(list);
        return NULL;
    }

    *n_dicts = count;

    return list;
}

void hyph_free_dictionaries(char **dicts, int n_dicts)
{
    int i;

    if (!dicts)
        return;

    for (i = 0; i < n_dicts; i++)
        free(dicts[i]);

    free(dicts);
}

int hyph_is_available(void)
{
    return 1;
}
