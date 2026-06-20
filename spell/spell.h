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

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(PLATFORM_AMIGA)
#ifndef SPELL_CACHE_N
#define SPELL_CACHE_N 256
#endif
#ifndef SPELL_CACHE_KEY_MAX
#define SPELL_CACHE_KEY_MAX 256
#endif
#else
#ifndef SPELL_CACHE_N
#define SPELL_CACHE_N 4096
#endif
#ifndef SPELL_CACHE_KEY_MAX
#define SPELL_CACHE_KEY_MAX 256
#endif
#endif

    typedef struct SpellChecker SpellChecker;

    SpellChecker *spell_new(const char *aff_path, const char *dic_path);
    void spell_free(SpellChecker *sc);
    int spell_check(SpellChecker *sc, const char *word);
    void spell_cache_clear(SpellChecker *sc);
    char **spell_suggest(SpellChecker *sc, const char *word, int *n_suggestions);
    void spell_free_suggestions(SpellChecker *sc, char **suggestions, int n_suggestions);
    int spell_add_word(SpellChecker *sc, const char *word);
    int spell_remove_word(SpellChecker *sc, const char *word);
    const char *spell_get_encoding(SpellChecker *sc);
    char **spell_list_dictionaries(const char *dir_path, int *n_dicts);
    void spell_free_dictionaries(char **dicts, int n_dicts);
    int spell_is_available(void);

    int spell_stem(SpellChecker *sc, const char *word, char ***out_list);
    int spell_generate(SpellChecker *sc, const char *word, const char *example, char ***out_list);
    void spell_free_list(SpellChecker *sc, char **list, int n);

    int spell_load_custom(SpellChecker *sc, const char *path);
    int spell_add_to_custom_dict(SpellChecker *sc, const char *word, const char *custom_dict_path);

#ifdef __cplusplus
}
#endif

#endif
