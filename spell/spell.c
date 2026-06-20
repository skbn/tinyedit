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

#include "spell.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifdef HAVE_HUNSPELL
#if defined(PLATFORM_AMIGA)
#include "hunspell.h"
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#include <hunspell.h>
#else
#include <hunspell/hunspell.h>
#endif
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
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct
{
    char word[SPELL_CACHE_KEY_MAX];
    unsigned char result;
    short prev;
    short next;
} SpellEntry;

struct SpellChecker
{
#ifdef HAVE_HUNSPELL
    Hunhandle *handle;
    char *encoding;
#endif
    short head;
    short tail;
    short count;
    SpellEntry cache[SPELL_CACHE_N];
};

#ifdef HAVE_HUNSPELL
static char *ce_strdup(const char *s)
{
    size_t n;
    char *r;

    if (!s)
        return NULL;

    n = strlen(s) + 1;
    r = (char *)malloc(n);

    if (!r)
        return NULL;

    memcpy(r, s, n);

    return r;
}

static void cache_init(SpellChecker *sc)
{
    int i;
    sc->head = -1;
    sc->tail = -1;
    sc->count = 0;

    for (i = 0; i < SPELL_CACHE_N; i++)
    {
        sc->cache[i].word[0] = '\0';
        sc->cache[i].prev = -1;
        sc->cache[i].next = -1;
    }
}

static void cache_unlink(SpellChecker *sc, int idx)
{
    SpellEntry *e = &sc->cache[idx];

    if (e->prev != -1)
        sc->cache[e->prev].next = e->next;
    else
        sc->head = e->next;

    if (e->next != -1)
        sc->cache[e->next].prev = e->prev;
    else
        sc->tail = e->prev;

    e->prev = -1;
    e->next = -1;
}

static void cache_push_front(SpellChecker *sc, int idx)
{
    SpellEntry *e = &sc->cache[idx];

    e->prev = -1;
    e->next = sc->head;

    if (sc->head != -1)
        sc->cache[sc->head].prev = (short)idx;

    sc->head = (short)idx;

    if (sc->tail == -1)
        sc->tail = (short)idx;
}

static int cache_find(const SpellChecker *sc, const char *word)
{
    int i;

    for (i = sc->head; i != -1; i = sc->cache[i].next)
    {
        if (strcmp(sc->cache[i].word, word) == 0)
            return i;
    }

    return -1;
}

static int cache_acquire_slot(SpellChecker *sc)
{
    int i;

    if (sc->count < SPELL_CACHE_N)
    {
        for (i = 0; i < SPELL_CACHE_N; i++)
        {
            if (sc->cache[i].word[0] == '\0')
            {
                sc->count++;
                return i;
            }
        }
    }

    i = sc->tail;
    cache_unlink(sc, i);

    return i;
}

static void cache_put(SpellChecker *sc, const char *word, int result)
{
    int idx;
    size_t wlen = strlen(word);

    if (wlen == 0)
        return;

    if (wlen >= SPELL_CACHE_KEY_MAX)
        return;

    idx = cache_acquire_slot(sc);

    memcpy(sc->cache[idx].word, word, wlen + 1);

    sc->cache[idx].result = (unsigned char)(result ? 1 : 0);

    cache_push_front(sc, idx);
}
#endif

void spell_cache_clear(SpellChecker *sc)
{
#ifdef HAVE_HUNSPELL
    if (sc)
        cache_init(sc);
#endif
}

SpellChecker *spell_new(const char *aff_path, const char *dic_path)
{
#ifdef HAVE_HUNSPELL
    SpellChecker *sc;
    FILE *fp;

    if (!aff_path || !dic_path)
        return NULL;

    /* Hunspell_create() prints to stderr on failure */
    fp = fopen(aff_path, "rb");

    if (!fp)
        return NULL;

    fclose(fp);

    fp = fopen(dic_path, "rb");

    if (!fp)
        return NULL;

    fclose(fp);

    sc = (SpellChecker *)calloc(1, sizeof(*sc));

    if (!sc)
        return NULL;

    sc->handle = Hunspell_create(aff_path, dic_path);

    if (!sc->handle)
    {
        free(sc);
        return NULL;
    }

    {
        const char *enc;

        enc = Hunspell_get_dic_encoding(sc->handle);

        if (enc)
            sc->encoding = ce_strdup(enc);
    }

    cache_init(sc);

    return sc;
#else
    return NULL;
#endif
}

void spell_free(SpellChecker *sc)
{
    if (!sc)
        return;

#ifdef HAVE_HUNSPELL
    if (sc->handle)
        Hunspell_destroy(sc->handle);

    if (sc->encoding)
        free(sc->encoding);
#endif

    /* The cache lives inside sc, so one free() reclaims it all */
    free(sc);
}

int spell_check(SpellChecker *sc, const char *word)
{
#ifdef HAVE_HUNSPELL
    int result;
    size_t wlen;

    if (!sc || !sc->handle || !word)
        return -1;

    /* Empty input is vacuously "correct" (and would corrupt the cache see cache_put for the gory details) */
    if (word[0] == '\0')
        return 1;

    wlen = strlen(word);

    /* Fast path: lookup in cache (no alloc) */
    if (wlen < SPELL_CACHE_KEY_MAX)
    {
        int idx = cache_find(sc, word);

        if (idx != -1)
        {
            /* Touch: move to MRU front */
            cache_unlink(sc, idx);
            cache_push_front(sc, idx);

            return sc->cache[idx].result ? 1 : 0;
        }
    }

    /* Slow path: ask Hunspell, then memoise */
    result = Hunspell_spell(sc->handle, word) ? 1 : 0;
    cache_put(sc, word, result);

    return result;
#else
    return -1;
#endif
}

char **spell_suggest(SpellChecker *sc, const char *word, int *n_suggestions)
{
#ifdef HAVE_HUNSPELL
    char **suggestions;

    if (!sc || !sc->handle || !word || !n_suggestions)
        return NULL;

    *n_suggestions = Hunspell_suggest(sc->handle, &suggestions, word);

    return suggestions;
#else
    if (n_suggestions)
        *n_suggestions = 0;

    return NULL;
#endif
}

void spell_free_suggestions(SpellChecker *sc, char **suggestions, int n_suggestions)
{
#ifdef HAVE_HUNSPELL
    if (sc && sc->handle && suggestions && n_suggestions > 0)
        Hunspell_free_list(sc->handle, &suggestions, n_suggestions);
#endif
}

int spell_add_word(SpellChecker *sc, const char *word)
{
#ifdef HAVE_HUNSPELL
    int rc;

    if (!sc || !sc->handle || !word)
        return -1;

    rc = Hunspell_add(sc->handle, word);

    /* The added word may have changed the verdict for previously cached variants, so wipe the cache to keep results consistent */
    spell_cache_clear(sc);

    return rc;
#else
    return -1;
#endif
}

int spell_remove_word(SpellChecker *sc, const char *word)
{
#ifdef HAVE_HUNSPELL
    int rc;

    if (!sc || !sc->handle || !word)
        return -1;

    rc = Hunspell_remove(sc->handle, word);
    spell_cache_clear(sc);

    return rc;
#else
    return -1;
#endif
}

int spell_stem(SpellChecker *sc, const char *word, char ***out_list)
{
#ifdef HAVE_HUNSPELL
    if (out_list)
        *out_list = NULL;

    if (!sc || !sc->handle || !word || !word[0] || !out_list)
        return -1;

    return Hunspell_stem(sc->handle, out_list, word);
#else

    if (out_list)
        *out_list = NULL;

    return -1;
#endif
}

int spell_generate(SpellChecker *sc, const char *word, const char *example, char ***out_list)
{
#ifdef HAVE_HUNSPELL
    if (out_list)
        *out_list = NULL;

    if (!sc || !sc->handle || !word || !word[0] || !example || !example[0] || !out_list)
        return -1;

    return Hunspell_generate(sc->handle, out_list, word, example);
#else

    if (out_list)
        *out_list = NULL;

    return -1;
#endif
}

void spell_free_list(SpellChecker *sc, char **list, int n)
{
#ifdef HAVE_HUNSPELL
    if (sc && sc->handle && list && n > 0)
        Hunspell_free_list(sc->handle, &list, n);
#endif
}

const char *spell_get_encoding(SpellChecker *sc)
{
#ifdef HAVE_HUNSPELL
    return sc ? sc->encoding : NULL;
#else
    return NULL;
#endif
}

int spell_is_available(void)
{
#ifdef HAVE_HUNSPELL
    return 1;
#else
    return 0;
#endif
}

static int ends_with_dic(const char *name)
{
    size_t len;

    if (!name)
        return 0;

    len = strlen(name);

    if (len < 4)
        return 0;

    return (tolower((unsigned char)name[len - 4]) == '.' &&
            tolower((unsigned char)name[len - 3]) == 'd' &&
            tolower((unsigned char)name[len - 2]) == 'i' &&
            tolower((unsigned char)name[len - 1]) == 'c');
}

static char *extract_dict_name(const char *name)
{
    size_t len;
    char *base;

    if (!name)
        return NULL;

    len = strlen(name);

    if (len < 4)
        return NULL;

    base = (char *)malloc(len - 3);

    if (!base)
        return NULL;

    memcpy(base, name, len - 4);
    base[len - 4] = '\0';

    return base;
}

char **spell_list_dictionaries(const char *dir_path, int *n_dicts)
{
    char **list;
    int cap, count;

#ifdef PLATFORM_WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pat[300];
#elif defined(PLATFORM_AMIGA)
    BPTR lock;
    struct FileInfoBlock *fib;
#else
    DIR *d;
    const struct dirent *de;
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

#ifdef PLATFORM_WIN32

    snprintf(pat, sizeof(pat), "%s\\*.dic", dir_path);
    h = FindFirstFileA(pat, &fd);

    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (ends_with_dic(fd.cFileName))
            {
                char *base = extract_dict_name(fd.cFileName);

                if (base)
                {
                    if (count >= cap)
                    {
                        char **g;

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
        } while (FindNextFileA(h, &fd));

        FindClose(h);
    }

#elif defined(PLATFORM_AMIGA)
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
                            char **g;

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
#else

    d = opendir(dir_path);

    if (d)
    {
        while ((de = readdir(d)) != NULL)
        {
            if (ends_with_dic(de->d_name))
            {
                char *base = extract_dict_name(de->d_name);

                if (base)
                {
                    if (count >= cap)
                    {
                        char **g;

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

        closedir(d);
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

void spell_free_dictionaries(char **dicts, int n_dicts)
{
    int i;

    if (!dicts)
        return;

    for (i = 0; i < n_dicts; i++)
        free(dicts[i]);

    free(dicts);
}

int spell_load_custom(SpellChecker *sc, const char *path)
{
#ifdef HAVE_HUNSPELL
    FILE *fp;
    char buf[512];
    int loaded = 0;

    if (!sc || !sc->handle || !path || !path[0])
        return -1;

    fp = fopen(path, "rb");

    if (!fp)
        return -1; /* not fatal -- caller treats as "no custom dict yet" */

    while (fgets(buf, (int)sizeof(buf), fp))
    {
        char *p = buf;
        size_t len;

        /* Skip BOM on the very first line */
        if (loaded == 0 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
            p += 3;

        /* Strip trailing whitespace (LF, CR, spaces, tabs) */
        len = strlen(p);

        while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r' || p[len - 1] == ' ' || p[len - 1] == '\t'))
            p[--len] = '\0';

        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '\0' || *p == '#')
            continue;

        if (Hunspell_add(sc->handle, p) == 0)
            loaded++;
    }

    fclose(fp);

    if (loaded > 0)
        spell_cache_clear(sc);

    return loaded;
#else

    return -1;
#endif
}

int spell_add_to_custom_dict(SpellChecker *sc, const char *word, const char *custom_dict_path)
{
#ifdef HAVE_HUNSPELL
    FILE *fp;
    int file_ok = 0;
    int mem_ok;

    if (!sc || !sc->handle || !word || !word[0])
        return -1;

    if (custom_dict_path && custom_dict_path[0])
    {
        fp = fopen(custom_dict_path, "ab");

        if (fp)
        {
            if (fprintf(fp, "%s\n", word) > 0)
                file_ok = 1;

            fclose(fp);
        }
    }
    else
    {
        file_ok = 1;
    }

    mem_ok = (Hunspell_add(sc->handle, word) == 0);

    spell_cache_clear(sc);

    return (file_ok && mem_ok) ? 0 : -1;
#else

    return -1;
#endif
}
