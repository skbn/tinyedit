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
    (void)aff_path;
    (void)dic_path;
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
    (void)sc;
    (void)word;
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
    (void)sc;
    (void)word;
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
#else
    (void)sc;
    (void)suggestions;
    (void)n_suggestions;
#endif
}

int spell_add_word(SpellChecker *sc, const char *word)
{
#ifdef HAVE_HUNSPELL
    if (!sc || !sc->handle || !word)
        return 0;

    return Hunspell_add(sc->handle, word);
#else
    (void)sc;
    (void)word;
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
    (void)sc;
    (void)word;
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
    (void)sc;
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
