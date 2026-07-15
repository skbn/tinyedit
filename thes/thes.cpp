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
#include "../spell/spell.h" /* SpellChecker, spell_stem/generate/free_list */
#include "../core/portable.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

#ifdef HAVE_MYTHES
#include <cstdio>
#include <new>
#include <mythes.hxx>
#endif

#ifdef PLATFORM_WIN32
#include <windows.h>
#elif defined(PLATFORM_AMIGA)
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#else
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef HAVE_MYTHES

typedef struct
{
    char syn[THES_CACHE_TXT_MAX];
    char def[THES_CACHE_TXT_MAX];
    int meaning_idx; /* meaning index */
} ThesCacheSyn;

typedef struct
{
    char word[THES_CACHE_KEY_MAX];                      /* empty if '\0' */
    int nmeanings;                                      /* 0 = not found */
    int nsyns;                                          /* total synonyms */
    char defs[THES_CACHE_SYNS_MAX][THES_CACHE_TXT_MAX]; /* definitions */
    ThesCacheSyn syns[THES_CACHE_SYNS_MAX];
    short prev;
    short next;
} ThesCacheEntry;

#endif

extern "C"
{
    struct ThesHandle
    {
#ifdef HAVE_MYTHES
        MyThes *mt;
        SpellChecker *sc; /* borrowed, may be NULL */
        short cache_head;
        short cache_tail;
        short cache_count;
        ThesCacheEntry cache[THES_CACHE_N];
#else
        int dummy;
#endif
    };

#ifdef HAVE_MYTHES

    /* Portable strdup */
    static char *te_strdup(const char *s)
    {
        size_t n;
        char *r = NULL;

        if (!s)
            return NULL;

        n = strlen(s) + 1;
        r = static_cast<char *>(malloc(n));

        if (!r)
            return NULL;

        memcpy(r, s, n);
        return r;
    }

    static void tcache_init(ThesHandle *t)
    {
        int i;

        t->cache_head = -1;
        t->cache_tail = -1;
        t->cache_count = 0;

        for (i = 0; i < THES_CACHE_N; i++)
        {
            t->cache[i].word[0] = '\0';
            t->cache[i].nmeanings = 0;
            t->cache[i].nsyns = 0;
            t->cache[i].prev = -1;
            t->cache[i].next = -1;
        }
    }

    static void tcache_unlink(ThesHandle *t, int idx)
    {
        ThesCacheEntry *e = &t->cache[idx];

        if (e->prev != -1)
            t->cache[e->prev].next = e->next;
        else
            t->cache_head = e->next;

        if (e->next != -1)
            t->cache[e->next].prev = e->prev;
        else
            t->cache_tail = e->prev;

        e->prev = -1;
        e->next = -1;
    }

    static void tcache_push_front(ThesHandle *t, int idx)
    {
        ThesCacheEntry *e = &t->cache[idx];

        e->prev = -1;
        e->next = t->cache_head;

        if (t->cache_head != -1)
            t->cache[t->cache_head].prev = static_cast<short>(idx);

        t->cache_head = static_cast<short>(idx);

        if (t->cache_tail == -1)
            t->cache_tail = static_cast<short>(idx);
    }

    static int tcache_find(const ThesHandle *t, const char *word)
    {
        int i;

        for (i = t->cache_head; i != -1; i = t->cache[i].next)
        {
            if (strcmp(t->cache[i].word, word) == 0)
                return i;
        }

        return -1;
    }

    static int tcache_acquire_slot(ThesHandle *t)
    {
        int i;

        if (t->cache_count < THES_CACHE_N)
        {
            for (i = 0; i < THES_CACHE_N; i++)
            {
                if (t->cache[i].word[0] == '\0')
                {
                    t->cache_count++;
                    return i;
                }
            }
        }

        i = t->cache_tail;
        tcache_unlink(t, i);

        return i;
    }

    /* Reconstruct ThesMeaning array from cache */
    static int tcache_materialise(const ThesCacheEntry *e, ThesMeaning **out)
    {
        ThesMeaning *meanings = NULL;
        int m, i;
        int *counts = NULL;

        *out = NULL;

        if (e->nmeanings <= 0 || e->nsyns <= 0)
            return 0;

        meanings = static_cast<ThesMeaning *>(calloc(static_cast<size_t>(e->nmeanings), sizeof(ThesMeaning)));

        if (!meanings)
            return -1;

        counts = static_cast<int *>(calloc(static_cast<size_t>(e->nmeanings), sizeof(int)));

        if (!counts)
        {
            free(meanings);
            return -1;
        }

        /* Count syns per meaning */
        for (i = 0; i < e->nsyns; i++)
        {
            int m_idx = e->syns[i].meaning_idx;

            if (m_idx >= 0 && m_idx < e->nmeanings)
                counts[m_idx]++;
        }

        /* Allocate slots */
        for (m = 0; m < e->nmeanings; m++)
        {
            meanings[m].def = te_strdup(e->defs[m]);
            meanings[m].nsyns = 0;

            if (counts[m] > 0)
            {
                meanings[m].syns = static_cast<char **>(calloc(static_cast<size_t>(counts[m]), sizeof(char *)));

                if (!meanings[m].syns)
                {
                    /* Roll back on OOM */
                    int mm;

                    for (mm = 0; mm <= m; mm++)
                    {
                        free(meanings[mm].def);

                        if (meanings[mm].syns)
                        {
                            int kk;

                            for (kk = 0; kk < meanings[mm].nsyns; kk++)
                                free(meanings[mm].syns[kk]);

                            free(meanings[mm].syns);
                        }
                    }

                    free(meanings);
                    free(counts);

                    return -1;
                }
            }
        }

        /* Copy synonym strings */
        for (i = 0; i < e->nsyns; i++)
        {
            int m_idx = e->syns[i].meaning_idx;

            if (m_idx >= 0 && m_idx < e->nmeanings)
            {
                int k = meanings[m_idx].nsyns;

                meanings[m_idx].syns[k] = te_strdup(e->syns[i].syn);
                meanings[m_idx].nsyns++;
            }
        }

        free(counts);

        *out = meanings;

        return e->nmeanings;
    }

    /* Store ThesMeaning array in cache (truncates if needed) */
    static void tcache_store(ThesHandle *t, const char *word, const ThesMeaning *m, int nmeanings)
    {
        int idx, i, k;
        int total_syns;
        size_t wlen;
        int eff_meanings;
        ThesCacheEntry *e = NULL;

        wlen = strlen(word);

        if (wlen == 0 || wlen >= THES_CACHE_KEY_MAX)
            return;

        eff_meanings = nmeanings;

        /* Cap meanings to cache limit */
        if (eff_meanings > THES_CACHE_SYNS_MAX)
            eff_meanings = THES_CACHE_SYNS_MAX;

        idx = tcache_acquire_slot(t);
        e = &t->cache[idx];

        memcpy(e->word, word, wlen + 1);

        e->nmeanings = eff_meanings;
        total_syns = 0;

        for (i = 0; i < eff_meanings; i++)
        {
            const char *def = m[i].def ? m[i].def : "";

            strncpy(e->defs[i], def, THES_CACHE_TXT_MAX - 1);
            e->defs[i][THES_CACHE_TXT_MAX - 1] = '\0';

            for (k = 0; k < m[i].nsyns; k++)
            {
                const char *syn = m[i].syns[k] ? m[i].syns[k] : "";

                if (total_syns >= THES_CACHE_SYNS_MAX)
                    break;

                strncpy(e->syns[total_syns].syn, syn, THES_CACHE_TXT_MAX - 1);
                e->syns[total_syns].syn[THES_CACHE_TXT_MAX - 1] = '\0';

                /* Store meaning index */
                e->syns[total_syns].def[0] = '\0';
                e->syns[total_syns].meaning_idx = i;

                total_syns++;
            }

            if (total_syns >= THES_CACHE_SYNS_MAX)
                break;
        }

        e->nsyns = total_syns;

        tcache_push_front(t, idx);
    }

    /* Mark word as not found */
    static void tcache_store_negative(ThesHandle *t, const char *word)
    {
        int idx;
        size_t wlen;
        ThesCacheEntry *e = NULL;

        wlen = strlen(word);

        if (wlen == 0 || wlen >= THES_CACHE_KEY_MAX)
            return;

        idx = tcache_acquire_slot(t);
        e = &t->cache[idx];

        memcpy(e->word, word, wlen + 1);

        e->nmeanings = 0;
        e->nsyns = 0;

        tcache_push_front(t, idx);
    }

#endif /* HAVE_MYTHES */

    ThesHandle *thes_new(const char *idx_path, const char *dat_path)
    {
#ifdef HAVE_MYTHES
        ThesHandle *t = NULL;
        FILE *fp;

        if (!idx_path || !dat_path)
            return NULL;

        /* Check files exist before loading */
        fp = fopen(idx_path, "rb");

        if (!fp)
            return NULL;

        fclose(fp);

        fp = fopen(dat_path, "rb");

        if (!fp)
            return NULL;

        fclose(fp);

        t = static_cast<ThesHandle *>(calloc(1, sizeof(*t)));

        if (!t)
            return NULL;

        t->mt = new (std::nothrow) MyThes(idx_path, dat_path);

        if (!t->mt)
        {
            free(t);
            return NULL;
        }

        tcache_init(t);

        return t;
#else

        return NULL;
#endif
    }

    void thes_free(ThesHandle *t)
    {
        if (!t)
            return;

#ifdef HAVE_MYTHES
        if (t->mt)
            delete t->mt;
#endif

        /* Cache is inline, freed with handle */
        free(t);
    }

    void thes_set_speller(ThesHandle *t, SpellChecker *sc)
    {
#ifdef HAVE_MYTHES
        if (t)
            t->sc = sc;
#endif
    }

#ifdef HAVE_MYTHES

    /* Direct MyThes lookup with deep copy. Returns count or -1 on OOM */
    static int thes_direct_lookup(ThesHandle *t, const char *word, ThesMeaning **out_meanings, const char *inflect_example)
    {
        mentry *me = NULL;
        int n, i, j;
        ThesMeaning *out = NULL;

        *out_meanings = NULL;

        n = t->mt->Lookup(word, static_cast<int>(strlen(word)), &me);

        if (n <= 0)
            return 0;

        out = static_cast<ThesMeaning *>(calloc(static_cast<size_t>(n), sizeof(ThesMeaning)));

        if (!out)
        {
            t->mt->CleanUpAfterLookup(&me, n);
            return -1;
        }

        for (i = 0; i < n; i++)
        {
            out[i].def = te_strdup(me[i].defn ? me[i].defn : "");
            out[i].nsyns = 0;
            out[i].syns = NULL;

            if (me[i].count > 0)
            {
                out[i].syns = static_cast<char **>(calloc(static_cast<size_t>(me[i].count), sizeof(char *)));

                if (out[i].syns)
                {
                    for (j = 0; j < me[i].count; j++)
                    {
                        const char *raw = me[i].psyns[j] ? me[i].psyns[j] : "";
                        char *cooked = NULL;

                        /* Try inflection via speller */
                        if (inflect_example && t->sc && raw[0])
                        {
                            char **gen = NULL;
                            int ng = spell_generate(t->sc, raw, inflect_example, &gen);

                            if (ng > 0 && gen && gen[0])
                                cooked = te_strdup(gen[0]);

                            if (gen)
                                spell_free_list(t->sc, gen, ng > 0 ? ng : 0);
                        }

                        if (!cooked)
                            cooked = te_strdup(raw);

                        out[i].syns[j] = cooked;
                    }

                    out[i].nsyns = me[i].count;
                }
            }
        }

        t->mt->CleanUpAfterLookup(&me, n);

        *out_meanings = out;
        return n;
    }

    /* Append meanings to accumulator. Returns 1 ok, 0 on OOM */
    static int thes_meanings_append(ThesMeaning **acc, int *accn, ThesMeaning *add, int addn)
    {
        ThesMeaning *grown = NULL;
        int i;

        if (addn <= 0)
            return 1;

        if (!add)
            return 0;

        grown = static_cast<ThesMeaning *>(realloc(*acc, static_cast<size_t>(*accn + addn) * sizeof(ThesMeaning)));

        if (!grown)
        {
            for (i = 0; i < *accn; i++)
            {
                free((*acc)[i].def);

                if ((*acc)[i].syns)
                {
                    int k;

                    for (k = 0; k < (*acc)[i].nsyns; k++)
                        free((*acc)[i].syns[k]);

                    free((*acc)[i].syns);
                }
            }

            free(*acc);

            *acc = NULL;
            *accn = 0;

            for (i = 0; i < addn; i++)
            {
                free(add[i].def);

                if (add[i].syns)
                {
                    int k;

                    for (k = 0; k < add[i].nsyns; k++)
                        free(add[i].syns[k]);

                    free(add[i].syns);
                }
            }

            free(add);
            return 0;
        }

        *acc = grown;

        for (i = 0; i < addn; i++)
            (*acc)[*accn + i] = add[i];

        *accn += addn;

        free(add);

        return 1;
    }

#endif /* HAVE_MYTHES */

    int thes_lookup(ThesHandle *t, const char *word, ThesMeaning **out_meanings)
    {
#ifdef HAVE_MYTHES
        int n;
        ThesMeaning *acc = NULL;
        int acc_n;
        int cache_idx;

        if (out_meanings)
            *out_meanings = NULL;

        if (!t || !t->mt || !word || !word[0] || !out_meanings)
            return -1;

        /* Check cache */
        cache_idx = tcache_find(t, word);

        if (cache_idx != -1)
        {
            ThesCacheEntry *e = &t->cache[cache_idx];

            /* Move to front */
            tcache_unlink(t, cache_idx);
            tcache_push_front(t, cache_idx);

            /* Negative cache hit */
            if (e->nmeanings == 0 || e->nsyns == 0)
                return 0;

            n = tcache_materialise(e, out_meanings);

            if (n < 0)
                return -1;

            return n;
        }

        acc = NULL;
        acc_n = 0;

        /* Try direct lookup */
        n = thes_direct_lookup(t, word, &acc, NULL);

        if (n < 0)
            return -1;

        if (n > 0)
        {
            acc_n = n;

            /* Cache result */
            tcache_store(t, word, acc, acc_n);

            *out_meanings = acc;
            return acc_n;
        }

        /* Try stem lookup */
        if (t->sc)
        {
            char **stems = NULL;
            int ns = spell_stem(t->sc, word, &stems);
            int i;

            for (i = 0; i < ns; i++)
            {
                ThesMeaning *more = NULL;
                int nm;

                if (!stems[i] || !stems[i][0])
                    continue;

                if (strcmp(stems[i], word) == 0)
                    continue;

                nm = thes_direct_lookup(t, stems[i], &more, word);

                if (nm > 0 && more)
                {
                    if (!thes_meanings_append(&acc, &acc_n, more, nm))
                    {
                        /* OOM during append */
                        spell_free_list(t->sc, stems, ns);
                        return -1;
                    }
                }
                else if (more)
                {
                    /* Shouldn't happen */
                    thes_free_meanings(t, more, nm);
                }
            }

            if (stems)
                spell_free_list(t->sc, stems, ns > 0 ? ns : 0);
        }

        if (acc_n > 0)
            tcache_store(t, word, acc, acc_n);
        else
            tcache_store_negative(t, word);

        *out_meanings = acc;

        return acc_n;
#else

        if (out_meanings)
            *out_meanings = NULL;

        return -1;
#endif
    }

    void thes_free_meanings(ThesHandle *t, ThesMeaning *m, int nmeanings)
    {
        int i;
        int j;

        if (!m)
            return;

        for (i = 0; i < nmeanings; i++)
        {
            if (m[i].def)
                free(m[i].def);

            if (m[i].syns)
            {
                for (j = 0; j < m[i].nsyns; j++)
                {
                    if (m[i].syns[j])
                        free(m[i].syns[j]);
                }

                free(m[i].syns);
            }
        }

        free(m);
    }

    const char *thes_get_encoding(ThesHandle *t)
    {
#ifdef HAVE_MYTHES
        if (t && t->mt)
            return t->mt->get_th_encoding();

        return NULL;
#else

        return NULL;
#endif
    }

    static int thes_ends_with_idx(const char *name)
    {
        size_t len;

        if (!name)
            return 0;

        len = strlen(name);

        if (len < 4)
            return 0;

        return (tolower((unsigned char)name[len - 4]) == '.' &&
                tolower((unsigned char)name[len - 3]) == 'i' &&
                tolower((unsigned char)name[len - 2]) == 'd' &&
                tolower((unsigned char)name[len - 1]) == 'x');
    }

    static char *thes_extract_name(const char *name)
    {
        size_t len;
        char *base = NULL;

        if (!name)
            return NULL;

        len = strlen(name);

        if (len < 4)
            return NULL;

        base = static_cast<char *>(malloc(len - 3));

        if (!base)
            return NULL;

        memcpy(base, name, len - 4);
        base[len - 4] = '\0';

        return base;
    }

    static int thes_name_exists(char **dicts, int count, const char *name)
    {
        int i;

        for (i = 0; i < count; i++)
        {
            if (dicts[i] && strcmp(dicts[i], name) == 0)
                return 1;
        }

        return 0;
    }

    void thes_free_dictionaries(char **dicts, int n_dicts)
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

    char **thes_list_dictionaries(const char *dir_path, int *n_dicts)
    {
#ifdef HAVE_MYTHES
        char **dicts = NULL;
        int capacity;
        int count;
        char *name = NULL;
        char **new_dicts = NULL;

#ifdef PLATFORM_WIN32
        WIN32_FIND_DATAW fd;
        HANDLE h;
        wchar_t *wdir = NULL;
        wchar_t wpattern[512];
#elif defined(PLATFORM_AMIGA)
        BPTR lock;
        struct FileInfoBlock *fib = NULL;
#else
        DIR *d = NULL;
        struct dirent *e = NULL;
#endif

        if (!n_dicts)
            return NULL;

        *n_dicts = 0;

        if (!dir_path || !dir_path[0])
            return NULL;

        capacity = 16;
        count = 0;
        dicts = static_cast<char **>(malloc(capacity * sizeof(char *)));

        if (!dicts)
            return NULL;

#ifdef PLATFORM_WIN32
        wdir = pf_utf8_to_utf16(dir_path);

        if (!wdir)
        {
            free(dicts);
            return NULL;
        }

        swprintf(wpattern, sizeof(wpattern) / sizeof(wchar_t), L"%s\\*.idx", wdir);

        free(wdir);

        h = FindFirstFileW(wpattern, &fd);

        if (h == INVALID_HANDLE_VALUE)
        {
            free(dicts);
            return NULL;
        }

        do
        {
            char *name_utf8 = NULL;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            name_utf8 = pf_utf16_to_utf8(fd.cFileName);

            if (!name_utf8)
                continue;

            name = thes_extract_name(name_utf8);

            free(name_utf8);

            if (!name)
                continue;

            if (!thes_name_exists(dicts, count, name))
            {
                if (count >= capacity)
                {
                    if (capacity > INT_MAX / 2)
                        break;

                    capacity *= 2;
                    new_dicts = static_cast<char **>(realloc(dicts, capacity * sizeof(char *)));

                    if (!new_dicts)
                    {
                        free(name);

                        FindClose(h);
                        thes_free_dictionaries(dicts, count);
                        return NULL;
                    }

                    dicts = new_dicts;
                }

                dicts[count++] = name;
            }
            else
            {
                free(name);
            }
        } while (FindNextFileW(h, &fd));

        FindClose(h);

#elif defined(PLATFORM_AMIGA)
        lock = Lock((STRPTR)dir_path, ACCESS_READ);

        if (!lock)
        {
            free(dicts);
            return NULL;
        }

        fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

        if (!fib)
        {
            UnLock(lock);
            free(dicts);
            return NULL;
        }

        if (Examine(lock, fib))
        {
            while (ExNext(lock, fib))
            {
                if (fib->fib_DirEntryType > 0)
                    continue;

                if (!thes_ends_with_idx(fib->fib_FileName))
                    continue;

                name = thes_extract_name(fib->fib_FileName);

                if (!name)
                    continue;

                if (!thes_name_exists(dicts, count, name))
                {
                    if (count >= capacity)
                    {
                        if (capacity > INT_MAX / 2)
                            break;

                        capacity *= 2;
                        new_dicts = static_cast<char **>(realloc(dicts, capacity * sizeof(char *)));

                        if (!new_dicts)
                        {
                            free(name);
                            FreeDosObject(DOS_FIB, fib);
                            UnLock(lock);
                            thes_free_dictionaries(dicts, count);
                            return NULL;
                        }

                        dicts = new_dicts;
                    }

                    dicts[count++] = name;
                }
                else
                {
                    free(name);
                }
            }
        }

        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);

#else
        d = opendir(dir_path);

        if (!d)
        {
            free(dicts);
            return NULL;
        }

        while ((e = readdir(d)) != NULL)
        {
            if (!thes_ends_with_idx(e->d_name))
                continue;

            name = thes_extract_name(e->d_name);

            if (!name)
                continue;

            if (!thes_name_exists(dicts, count, name))
            {
                if (count >= capacity)
                {
                    if (capacity > INT_MAX / 2)
                        break;

                    capacity *= 2;
                    new_dicts = static_cast<char **>(realloc(dicts, capacity * sizeof(char *)));

                    if (!new_dicts)
                    {
                        free(name);
                        closedir(d);
                        thes_free_dictionaries(dicts, count);
                        return NULL;
                    }

                    dicts = new_dicts;
                }

                dicts[count++] = name;
            }
            else
            {
                free(name);
            }
        }

        closedir(d);
#endif

        if (count == 0)
        {
            free(dicts);
            return NULL;
        }

        *n_dicts = count;
        return dicts;

#else

        if (n_dicts)
            *n_dicts = 0;

        return NULL;
#endif
    }

} /* extern "C" */
