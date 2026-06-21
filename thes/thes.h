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

#ifndef CE_THES_H
#define CE_THES_H

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct ThesHandle ThesHandle;

    /* Forward-declared from spell.h for stemming */
#ifndef THES_SPELLCHECKER_TYPEDEF
#define THES_SPELLCHECKER_TYPEDEF
    typedef struct SpellChecker SpellChecker;
#endif

    /* Each meaning has a definition and synonym list. Owned until thes_free_meanings() */
    typedef struct
    {
        char *def; /* never NULL; "" if no definition */
        int nsyns;
        char **syns; /* nsyns entries */
    } ThesMeaning;

    /* Load idx + dat (e.g. /path/th_es_ES_v2.idx, .dat).  NULL on failure. */
    ThesHandle *thes_new(const char *idx_path, const char *dat_path);

    void thes_free(ThesHandle *t);

    /* Attach/detach spell checker for stemming fallback. Borrowed pointer, caller owns lifetime */
    void thes_set_speller(ThesHandle *t, SpellChecker *sc);

    /* Look up word. Returns meaning count or -1 on error. Caller must free with thes_free_meanings() */
    int thes_lookup(ThesHandle *t, const char *word, ThesMeaning **out_meanings);

    void thes_free_meanings(ThesHandle *t, ThesMeaning *m, int nmeanings);

    /* Encoding of the loaded thesaurus (e.g. "ISO8859-1"). */
    const char *thes_get_encoding(ThesHandle *t);

    /* List available dictionaries in directory. Returns malloc'd array or NULL. Free with thes_free_dictionaries() */
    char **thes_list_dictionaries(const char *dir_path, int *n_dicts);

    void thes_free_dictionaries(char **dicts, int n_dicts);

    int thes_is_available(void);

#ifdef __cplusplus
}
#endif

#endif
