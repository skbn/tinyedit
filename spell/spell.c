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

/* Per-platform directory listing */
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

/* Check if filename ends with ".dic" (case-insensitive) */
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

/* Extract base name without .dic extension */
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

/* Check if dict name already exists in array */
static int dict_exists(char **dicts, int count, const char *name)
{
    int i;

    for (i = 0; i < count; i++)
    {
        if (dicts[i] && strcmp(dicts[i], name) == 0)
            return 1;
    }

    return 0;
}

#ifdef HAVE_HUNSPELL

struct SpellChecker
{
    Hunhandle *handle;
};

#else

struct SpellChecker
{
    int dummy;
};

#endif

SpellChecker *spell_new(const char *aff_path, const char *dic_path)
{
#ifdef HAVE_HUNSPELL
    SpellChecker *sc;

    if (!aff_path || !dic_path)
        return NULL;

    sc = (SpellChecker *)malloc(sizeof(SpellChecker));
    if (!sc)
        return NULL;

    sc->handle = Hunspell_create(aff_path, dic_path);
    if (!sc->handle)
    {
        free(sc);
        return NULL;
    }

    return sc;
#else
    return NULL;
#endif
}

void spell_free(SpellChecker *sc)
{
#ifdef HAVE_HUNSPELL
    if (sc)
    {
        if (sc->handle)
            Hunspell_destroy(sc->handle);
        free(sc);
    }
#else
    if (sc)
        free(sc);
#endif
}

int spell_check(SpellChecker *sc, const char *word)
{
#ifdef HAVE_HUNSPELL
    if (!sc || !sc->handle || !word)
        return -1;

    return Hunspell_spell(sc->handle, word);
#else
    return -1;
#endif
}

char **spell_suggest(SpellChecker *sc, const char *word, int *n_suggestions)
{
#ifdef HAVE_HUNSPELL
    char **suggestions;

    if (!sc || !sc->handle || !word || !n_suggestions)
    {
        return NULL;
    }

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
    if (!sc || !sc->handle || !word)
        return 0;

    return Hunspell_add(sc->handle, word);
#else
    return 0;
#endif
}

int spell_remove_word(SpellChecker *sc, const char *word)
{
#ifdef HAVE_HUNSPELL
    if (!sc || !sc->handle || !word)
        return 0;

    return Hunspell_remove(sc->handle, word);
#else
    return 0;
#endif
}

const char *spell_get_encoding(SpellChecker *sc)
{
#ifdef HAVE_HUNSPELL
    if (!sc || !sc->handle)
        return NULL;

    return Hunspell_get_dic_encoding(sc->handle);
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

void spell_free_dictionaries(char **dicts, int n_dicts)
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

char **spell_list_dictionaries(const char *search_path, int *n_dicts)
{
    char **dicts;
    int capacity;
    int count;
    char *name;
    char **new_dicts;

#ifdef PLATFORM_WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[300];
#elif defined(PLATFORM_AMIGA)
    BPTR lock;
    struct FileInfoBlock *fib;
#else
    DIR *d;
    struct dirent *e;
#endif

    if (!n_dicts)
        return NULL;

    *n_dicts = 0;

    if (!search_path || !search_path[0])
        return NULL;

    capacity = 16;
    count = 0;
    dicts = (char **)malloc(capacity * sizeof(char *));

    if (!dicts)
        return NULL;

#ifdef PLATFORM_WIN32
    snprintf(pattern, sizeof(pattern), "%s\\*.dic", search_path);
    h = FindFirstFileA(pattern, &fd);

    if (h == INVALID_HANDLE_VALUE)
    {
        free(dicts);
        return NULL;
    }

    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        name = extract_dict_name(fd.cFileName);

        if (!name)
            continue;

        if (!dict_exists(dicts, count, name))
        {
            if (count >= capacity)
            {
                capacity *= 2;
                new_dicts = (char **)realloc(dicts, capacity * sizeof(char *));

                if (!new_dicts)
                {
                    free(name);

                    FindClose(h);
                    spell_free_dictionaries(dicts, count);

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
    } while (FindNextFileA(h, &fd));

    FindClose(h);

#elif defined(PLATFORM_AMIGA)
    lock = Lock((STRPTR)search_path, ACCESS_READ);

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

            if (!ends_with_dic(fib->fib_FileName))
                continue;

            name = extract_dict_name(fib->fib_FileName);
            if (!name)
                continue;

            if (!dict_exists(dicts, count, name))
            {
                if (count >= capacity)
                {
                    capacity *= 2;
                    new_dicts = (char **)realloc(dicts, capacity * sizeof(char *));

                    if (!new_dicts)
                    {
                        free(name);

                        FreeDosObject(DOS_FIB, fib);
                        UnLock(lock);
                        spell_free_dictionaries(dicts, count);
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
    d = opendir(search_path);

    if (!d)
    {
        free(dicts);
        return NULL;
    }

    while ((e = readdir(d)) != NULL)
    {
        if (!ends_with_dic(e->d_name))
            continue;

        name = extract_dict_name(e->d_name);
        if (!name)
            continue;

        if (!dict_exists(dicts, count, name))
        {
            if (count >= capacity)
            {
                capacity *= 2;
                new_dicts = (char **)realloc(dicts, capacity * sizeof(char *));

                if (!new_dicts)
                {
                    free(name);

                    closedir(d);
                    spell_free_dictionaries(dicts, count);
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
}
