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

#include "thes.h"
#include "spell.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../core/utf8.h"

#ifdef PLATFORM_AMIGA
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#endif

/* Lowercase a string in-place; UTF-8 aware when enc is UTF-8, else byte tolower */
static void mt_tolower_enc(char *s, const char *enc)
{
    unsigned char *p = (unsigned char *)s;

    if (strcasecmp(enc, "UTF-8") == 0 || strcasecmp(enc, "UTF8") == 0)
    {
        utf8_tolower(s);
        return;
    }

    while (*p)
    {
        *p = (unsigned char)tolower(*p);
        p++;
    }
}

/* Convert UTF-8 word to thesaurus encoding; returns src if already UTF-8 or on error */
static const char *mt_to_enc(const char *enc, const char *word, char *buf, size_t bufsz)
{
    int r;

    if (strcasecmp(enc, "UTF-8") == 0 || strcasecmp(enc, "UTF8") == 0)
        return word;

    r = utf8_to_charset(enc, word, (int)strlen(word), buf, (int)bufsz);

    if (r > 0)
        return buf;

    return word;
}

static char *mt_strdup(const char *s)
{
    size_t n;
    char *r = NULL;

    if (!s)
        return NULL;

    n = strlen(s) + 1;
    r = (char *)malloc(n);

    if (!r)
        return NULL;

    memcpy(r, s, n);

    return r;
}

/* Read line from fp into buf (size sz), removes \r and \n, returns length or -1 on EOF */
static int mt_readline(FILE *fp, char *buf, size_t sz)
{
    int c;
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

static void cache_init(struct thes *t)
{
    int i;

    t->head = -1;
    t->tail = -1;
    t->count = 0;

    for (i = 0; i < THES_CACHE_N; i++)
    {
        t->cache[i].key[0] = '\0';
        t->cache[i].prev = -1;
        t->cache[i].next = -1;
    }
}

/* Free synonyms allocated in cache entry */
static void cache_entry_free_strings(struct thes_cache_entry *e)
{
    int m;
    int s;

    for (m = 0; m < e->result.n_meanings; m++)
    {
        for (s = 0; s < e->result.meanings[m].n_syns; s++)
        {
            if (e->result.meanings[m].syns[s])
            {
                free(e->result.meanings[m].syns[s]);
                e->result.meanings[m].syns[s] = NULL;
            }
        }

        e->result.meanings[m].n_syns = 0;
    }

    e->result.n_meanings = 0;
}

static void cache_unlink(struct thes *t, int idx)
{
    struct thes_cache_entry *e = &t->cache[idx];

    if (e->prev != -1)
        t->cache[e->prev].next = e->next;
    else
        t->head = e->next;

    if (e->next != -1)
        t->cache[e->next].prev = e->prev;
    else
        t->tail = e->prev;

    e->prev = -1;
    e->next = -1;
}

static void cache_push_front(struct thes *t, int idx)
{
    struct thes_cache_entry *e = &t->cache[idx];

    e->prev = -1;
    e->next = t->head;

    if (t->head != -1)
        t->cache[t->head].prev = (short)idx;

    t->head = (short)idx;

    if (t->tail == -1)
        t->tail = (short)idx;
}

static int cache_find(struct thes *t, const char *word)
{
    int i;

    for (i = t->head; i != -1; i = t->cache[i].next)
    {
        if (strcmp(t->cache[i].key, word) == 0)
            return i;
    }

    return -1;
}

/* Obtiene un slot libre (o el LRU si todos llenos) */
static int cache_acquire(struct thes *t)
{
    int i;

    if (t->count < THES_CACHE_N)
    {
        for (i = 0; i < THES_CACHE_N; i++)
        {
            if (t->cache[i].key[0] == '\0')
            {
                t->count++;
                return i;
            }
        }
    }

    /* All occupied, evict tail (LRU) */
    i = t->tail;

    if (i == -1)
        return 0;

    cache_unlink(t, i);
    cache_entry_free_strings(&t->cache[i]);

    return i;
}

void thes_cache_clear(struct thes *t)
{
    int i;

    if (!t)
        return;

    for (i = 0; i < THES_CACHE_N; i++)
    {
        if (t->cache[i].key[0] != '\0')
        {
            cache_entry_free_strings(&t->cache[i]);
            t->cache[i].key[0] = '\0';
            t->cache[i].prev = -1;
            t->cache[i].next = -1;
        }
    }

    t->head = -1;
    t->tail = -1;
    t->count = 0;
}

/* Parse "word|N" into *word_out and *n_out */
static int parse_header(const char *line, char *word_out, int *n_out)
{
    const char *bar = strchr(line, '|');
    size_t len;

    if (!bar)
        return -1;

    len = (size_t)(bar - line);

    if (len >= THES_MAX_WORD)
        len = THES_MAX_WORD - 1;

    memcpy(word_out, line, len);
    word_out[len] = '\0';

    *n_out = atoi(bar + 1);

    return 0;
}

/* Parse meaning line "count|meaning|syn1|syn2|..." */
static void parse_meaning(const char *line, struct thes_meaning *m)
{
    const char *p = line;
    const char *bar = NULL;
    size_t len;
    size_t pos_len;
    size_t syn_len;
    char *s = NULL;
    int field = 0;

    m->n_syns = 0;
    m->pos[0] = '\0';

    while (*p && m->n_syns < THES_MAX_SYNS)
    {
        bar = strchr(p, '|');
        len = bar ? (size_t)(bar - p) : strlen(p);

        if (field == 0)
        {
            /* First field: POS (part of speech), e.g. "-" */
            if (len >= sizeof(m->pos))
                len = sizeof(m->pos) - 1;

            memcpy(m->pos, p, len);
            m->pos[len] = '\0';
        }
        else if (field == 1)
        {
            /* Second field: first synonym, combine with POS for definition */
            pos_len = strlen(m->pos);
            syn_len = len;

            if (pos_len + 1 + syn_len < sizeof(m->pos))
            {
                m->pos[pos_len] = ' ';

                memcpy(m->pos + pos_len + 1, p, syn_len);
                m->pos[pos_len + 1 + syn_len] = '\0';
            }

            /* Also save as synonym */
            s = (char *)malloc(len + 1);

            if (!s)
                break;

            memcpy(s, p, len);
            s[len] = '\0';

            m->syns[m->n_syns++] = s;
        }
        else
        {
            /* Following fields: synonyms */
            s = (char *)malloc(len + 1);

            if (!s)
                break;

            memcpy(s, p, len);
            s[len] = '\0';

            m->syns[m->n_syns++] = s;
        }

        field++;

        if (!bar)
            break;

        p = bar + 1;
    }
}

/* Read entry from .dat at offset and fill result */
static int read_entry(struct thes *t, long off, struct thes_result *r)
{
    char line[2048];
    int n_meanings = 0;
    int m;

    if (fseek(t->dat, off, SEEK_SET) != 0)
        return -1;

    if (mt_readline(t->dat, line, sizeof(line)) < 0)
        return -1;

    if (parse_header(line, r->word, &n_meanings) != 0)
        return -1;

    if (n_meanings < 0)
        n_meanings = 0;

    if (n_meanings > THES_MAX_MEANINGS)
        n_meanings = THES_MAX_MEANINGS;

    r->n_meanings = n_meanings;

    for (m = 0; m < n_meanings; m++)
    {
        if (mt_readline(t->dat, line, sizeof(line)) < 0)
        {
            r->n_meanings = m;
            return 0;
        }

        parse_meaning(line, &r->meanings[m]);
    }

    return 0;
}

static int idx_bsearch(struct thes *t, const char *word, long *off_out)
{
    int lo = 0;
    int hi;
    int mid;
    int cmp;

    if (t->n_entries == 0)
        return 0;

    hi = t->n_entries - 1;

    while (lo <= hi)
    {
        mid = (lo + hi) / 2;
        cmp = strcmp(t->idx_words[mid], word);

        if (cmp == 0)
        {
            *off_out = t->idx_offs[mid];
            return 1;
        }

        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    return 0;
}

struct thes *thes_open(const char *idx_path, const char *dat_path)
{
    struct thes *t = NULL;
    FILE *fidx = NULL;
    char line[1024];
    int i;
    int count;

    if (!idx_path || !dat_path)
        return NULL;

    fidx = fopen(idx_path, "rb");

    if (!fidx)
        return NULL;

    t = (struct thes *)calloc(1, sizeof(*t));

    if (!t)
    {
        fclose(fidx);
        return NULL;
    }

    /* Line 1: encoding */
    if (mt_readline(fidx, line, sizeof(line)) < 0)
    {
        fclose(fidx);
        free(t);
        return NULL;
    }

    strncpy(t->enc_idx, line, sizeof(t->enc_idx) - 1);
    t->enc_idx[sizeof(t->enc_idx) - 1] = '\0';

    /* Line 2: number of entries */
    if (mt_readline(fidx, line, sizeof(line)) < 0)
    {
        fclose(fidx);
        free(t);
        return NULL;
    }

    count = atoi(line);

    /* Validate count - must be positive and reasonable to avoid OOM */
    if (count <= 0)
    {
        fclose(fidx);
        free(t);
        return NULL;
    }

    /* Limit to ~6 million entries (~100MB) to avoid OOM */
    if (count > 6000000)
    {
        fclose(fidx);
        free(t);
        return NULL;
    }

    t->n_entries = count;
    t->idx_words = (char **)calloc((size_t)count, sizeof(char *));
    t->idx_offs = (long *)calloc((size_t)count, sizeof(long));

    if (!t->idx_words || !t->idx_offs)
    {
        fclose(fidx);
        thes_close(t);
        return NULL;
    }

    /* Parse all entries "word|offset" */
    for (i = 0; i < count; i++)
    {
        char *bar = NULL;

        if (mt_readline(fidx, line, sizeof(line)) < 0)
        {
            t->n_entries = i;
            break;
        }

        bar = strchr(line, '|');

        if (!bar)
        {
            t->n_entries = i;
            break;
        }

        *bar = '\0';

        t->idx_words[i] = mt_strdup(line);
        t->idx_offs[i] = atol(bar + 1);

        if (!t->idx_words[i])
        {
            fclose(fidx);
            thes_close(t);
            return NULL;
        }
    }

    fclose(fidx);
    fidx = NULL;

    /* Open .dat and read its encoding */
    t->dat = fopen(dat_path, "rb");

    if (!t->dat)
    {
        thes_close(t);
        return NULL;
    }

    if (mt_readline(t->dat, line, sizeof(line)) < 0)
    {
        thes_close(t);
        return NULL;
    }

    strncpy(t->enc_dat, line, sizeof(t->enc_dat) - 1);
    t->enc_dat[sizeof(t->enc_dat) - 1] = '\0';

    cache_init(t);
    return t;
}

void thes_close(struct thes *t)
{
    int i;

    if (!t)
        return;

    thes_cache_clear(t);

    if (t->dat)
        fclose(t->dat);

    if (t->idx_words)
    {
        for (i = 0; i < t->n_entries; i++)
        {
            if (t->idx_words[i])
                free(t->idx_words[i]);
        }

        free(t->idx_words);
    }

    if (t->idx_offs)
        free(t->idx_offs);

    free(t);
}

int thes_lookup_raw(struct thes *t, const char *word, struct thes_result **out)
{
    int idx;
    long off;
    size_t wlen;
    int was_free;

    if (out)
        *out = NULL;

    if (!t || !word || !out)
        return -1;

    wlen = strlen(word);

    if (wlen == 0 || wlen >= THES_MAX_WORD)
        return 0;

    /* Cache hit? */
    idx = cache_find(t, word);

    if (idx != -1)
    {
        cache_unlink(t, idx);
        cache_push_front(t, idx);
        *out = &t->cache[idx].result;
        return 1;
    }

    /* Search in idx */
    if (!idx_bsearch(t, word, &off))
        return 0;

    /* Read entry from .dat and put in cache */

    was_free = (t->count < THES_CACHE_N);

    idx = cache_acquire(t);

    strncpy(t->cache[idx].key, word, THES_MAX_WORD - 1);
    t->cache[idx].key[THES_MAX_WORD - 1] = '\0';

    if (read_entry(t, off, &t->cache[idx].result) != 0)
    {
        t->cache[idx].key[0] = '\0';

        if (was_free)
            t->count--;

        return -1;
    }

    cache_push_front(t, idx);
    *out = &t->cache[idx].result;

    return 1;
}

const char *thes_get_encoding(struct thes *t)
{
    return t ? t->enc_dat : NULL;
}

ThesHandle *thes_new(const char *idx_path, const char *dat_path)
{
    return thes_open(idx_path, dat_path);
}

void thes_free(ThesHandle *t)
{
    thes_close(t);
}

void thes_set_speller(ThesHandle *t, SpellChecker *sc)
{
    if (t)
        t->sc = sc;
}

/* Helper: materialise thes_result into a ThesMeaning array */
static ThesMeaning *thes_build_meanings(struct thes_result *res, int *n_out)
{
    ThesMeaning *m = NULL;
    int n = res->n_meanings;
    int i;
    int j;

    m = (ThesMeaning *)calloc((size_t)n, sizeof(*m));

    if (!m)
        return NULL;

    for (i = 0; i < n; i++)
    {
        struct thes_meaning *src = &res->meanings[i];
        m[i].def = mt_strdup(src->pos);

        if (!m[i].def)
        {
            m[i].def = (char *)malloc(1);

            if (m[i].def)
                m[i].def[0] = '\0';
        }

        m[i].nsyns = src->n_syns;

        if (m[i].nsyns > 0)
        {
            m[i].syns = (char **)calloc((size_t)m[i].nsyns, sizeof(char *));

            if (!m[i].syns)
            {
                m[i].nsyns = 0;
                continue;
            }

            for (j = 0; j < m[i].nsyns; j++)
            {
                m[i].syns[j] = mt_strdup(src->syns[j]);

                if (!m[i].syns[j])
                {
                    m[i].syns[j] = (char *)malloc(1);

                    if (m[i].syns[j])
                        m[i].syns[j][0] = '\0';
                }
            }
        }
    }

    *n_out = n;
    return m;
}

/* Lookup with automatic encoding conversion and case fallback */
static int thes_lookup_norm(ThesHandle *t, const char *word_enc, ThesMeaning **out, int *n_out)
{
    struct thes_result *res = NULL;
    char lower[THES_MAX_WORD];
    size_t wlen;

    if (thes_lookup_raw(t, word_enc, &res) == 1 && res && res->n_meanings > 0)
    {
        *out = thes_build_meanings(res, n_out);
        return *out ? *n_out : -1;
    }

    /* Try lowercase fallback (encoding-aware) */
    wlen = strlen(word_enc);

    if (wlen >= sizeof(lower))
    {
        *out = NULL;
        *n_out = 0;
        return 0;
    }

    strncpy(lower, word_enc, sizeof(lower) - 1);
    lower[sizeof(lower) - 1] = '\0';

    mt_tolower_enc(lower, t->enc_idx);

    if (strcmp(lower, word_enc) != 0)
    {
        if (thes_lookup_raw(t, lower, &res) == 1 && res && res->n_meanings > 0)
        {
            *out = thes_build_meanings(res, n_out);
            return *out ? *n_out : -1;
        }
    }

    *out = NULL;
    *n_out = 0;

    return 0;
}

int thes_lookup(ThesHandle *t, const char *word, ThesMeaning **out_meanings)
{
    ThesMeaning *m = NULL;
    int n = 0;
    char enc_buf[THES_MAX_WORD];
    const char *word_enc;

    if (out_meanings)
        *out_meanings = NULL;

    if (!t || !word || !out_meanings)
        return -1;

    /* Convert input (always UTF-8 from UI) to thesaurus encoding */
    word_enc = mt_to_enc(t->enc_idx, word, enc_buf, sizeof(enc_buf));

    if (thes_lookup_norm(t, word_enc, &m, &n) > 0)
    {
        *out_meanings = m;
        return n;
    }

    /* Stem fallback: spell_stem returns roots in spell encoding (same as thes) */
    if (t->sc)
    {
        char **stems = NULL;
        int ns = spell_stem(t->sc, word_enc, &stems);
        int si;

        for (si = 0; si < ns; si++)
        {
            ThesMeaning *more = NULL;
            int nm = 0;
            ThesMeaning *grow;

            if (!stems[si] || !stems[si][0])
                continue;

            if (strcmp(stems[si], word_enc) == 0)
                continue;

            if (thes_lookup_norm(t, stems[si], &more, &nm) <= 0 || !more)
                continue;

            grow = (ThesMeaning *)realloc(m, (size_t)(n + nm) * sizeof(*m));

            if (!grow)
            {
                thes_free_meanings(t, more, nm);
                spell_free_list(t->sc, stems, ns);
                thes_free_meanings(t, m, n);
                return -1;
            }

            m = grow;
            memcpy(m + n, more, (size_t)nm * sizeof(*m));
            free(more);
            n += nm;
        }

        if (stems)
            spell_free_list(t->sc, stems, ns > 0 ? ns : 0);
    }

    *out_meanings = m;
    return n;
}

void thes_free_meanings(ThesHandle *t, ThesMeaning *m, int nmeanings)
{
    int i, j;

    if (!m)
        return;

    for (i = 0; i < nmeanings; i++)
    {
        if (m[i].def)
            free(m[i].def);

        for (j = 0; j < m[i].nsyns; j++)
        {
            if (m[i].syns[j])
                free(m[i].syns[j]);
        }

        if (m[i].syns)
            free(m[i].syns);
    }

    free(m);
}

static int ends_with_idx(const unsigned char *name)
{
    size_t len;

    if (!name)
        return 0;

    len = strlen((const char *)name);

    if (len < 4)
        return 0;

    return (tolower(name[len - 4]) == '.' &&
            tolower(name[len - 3]) == 'i' &&
            tolower(name[len - 2]) == 'd' &&
            tolower(name[len - 1]) == 'x');
}

static char *extract_thes_name(const unsigned char *name)
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

char **thes_list_dictionaries(const char *dir_path, int *n_dicts)
{
    char **list = NULL;
    int cap;
    int count;

#ifdef PLATFORM_AMIGA
    BPTR lock;
    struct FileInfoBlock *fib = NULL;
#endif

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
    lock = Lock((CONST_STRPTR)dir_path, ACCESS_READ);

    if (lock)
    {
        fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

        if (fib && Examine(lock, fib))
        {
            while (ExNext(lock, fib))
            {
                if (fib->fib_DirEntryType < 0 && ends_with_idx(fib->fib_FileName))
                {
                    char *base = extract_thes_name(fib->fib_FileName);

                    if (base)
                    {
                        if (count >= cap)
                        {
                            int new_cap = cap * 2;
                            char **g = (char **)realloc(list, (size_t)new_cap * sizeof(char *));

                            if (!g)
                            {
                                free(base);
                                continue;
                            }

                            list = g;
                            cap = new_cap;
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
#endif

    if (count == 0)
    {
        free(list);
        return NULL;
    }

    *n_dicts = count;

    return list;
}

void thes_free_dictionaries(char **dicts, int n_dicts)
{
    int i;

    if (!dicts)
        return;

    for (i = 0; i < n_dicts; i++)
        free(dicts[i]);

    free(dicts);
}

int thes_is_available(void)
{
    return 1;
}
