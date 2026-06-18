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

#ifndef SPELL_H
#define SPELL_H

/* Spell checker wrapper for Hunspell C++ library */
#ifdef HAVE_HUNSPELL
#ifdef PLATFORM_AMIGA
#include "hunspell.h"
#else
#include <hunspell/hunspell.h>
#endif
#endif

typedef struct SpellChecker SpellChecker;

SpellChecker *spell_new(const char *aff_path, const char *dic_path);
void spell_free(SpellChecker *sc);
int spell_check(SpellChecker *sc, const char *word);
char **spell_suggest(SpellChecker *sc, const char *word, int *n_suggestions);
void spell_free_suggestions(SpellChecker *sc, char **suggestions, int n_suggestions);
int spell_add_word(SpellChecker *sc, const char *word);
int spell_remove_word(SpellChecker *sc, const char *word);
const char *spell_get_encoding(SpellChecker *sc);
int spell_is_available(void);

#endif /* SPELL_H */
