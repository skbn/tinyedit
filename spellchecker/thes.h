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

#ifndef THES_H
#define THES_H

#include <stddef.h>
#include <stdio.h>

/* LRU cache size, adjustable. Each cached entry ~512 bytes, 32 entries = ~16KB */
#ifndef THES_CACHE_N
#define THES_CACHE_N 32
#endif

#ifndef THES_MAX_WORD
#define THES_MAX_WORD 128
#endif

#ifndef THES_MAX_MEANINGS
#define THES_MAX_MEANINGS 16
#endif

#ifndef THES_MAX_SYNS
#define THES_MAX_SYNS 32
#endif

struct thes_meaning
{
    char pos[16];              /* "-", "noun", ... */
    char *syns[THES_MAX_SYNS]; /* synonyms (allocated) */
    int n_syns;
};

struct thes_result
{
    char word[THES_MAX_WORD]; /* the searched word */
    struct thes_meaning meanings[THES_MAX_MEANINGS];
    int n_meanings;
};

/* internal cache structure (not exported to users) */
struct thes_cache_entry
{
    char key[THES_MAX_WORD]; /* empty key = free */
    struct thes_result result;
    short prev, next; /* doubly linked list */
};

struct thes
{
    FILE *dat;        /* open .dat file */
    char enc_idx[24]; /* encoding of .idx */
    char enc_dat[24]; /* encoding of .dat */
    int n_entries;    /* number of entries in idx */

    /* alphabetically sorted index table */
    char **idx_words; /* idx_words[i] = word */
    long *idx_offs;   /* idx_offs[i]  = offset in .dat */

    /* LRU cache */
    struct thes_cache_entry cache[THES_CACHE_N];
    short head, tail;
    short count;
};

/* public API (internal structures) */
struct thes *thes_open(const char *idx_path, const char *dat_path);
void thes_close(struct thes *t);
int thes_lookup_raw(struct thes *t, const char *word, struct thes_result **out);
void thes_cache_clear(struct thes *t);
const char *thes_get_encoding(struct thes *t);

#ifndef THES_SPELLCHECKER_TYPEDEF
#define THES_SPELLCHECKER_TYPEDEF
typedef struct spell SpellChecker;
#endif

/* opaque handle */
typedef struct thes ThesHandle;

/* each meaning has a definition (pos) and synonym list */
typedef struct
{
    char *def; /* never NULL; "" if no definition */
    int nsyns;
    char **syns; /* nsyns entries */
} ThesMeaning;

/* open idx + dat (e.g. /path/th_es_ES_v2.idx, .dat), returns NULL on error */
ThesHandle *thes_new(const char *idx_path, const char *dat_path);

void thes_free(ThesHandle *t);

/* attach/detach spell checker for stemming fallback (no-op in this engine) */
void thes_set_speller(ThesHandle *t, SpellChecker *sc);

/* lookup word, returns number of meanings or -1 on error, caller must free with thes_free_meanings() */
int thes_lookup(ThesHandle *t, const char *word, ThesMeaning **out_meanings);

void thes_free_meanings(ThesHandle *t, ThesMeaning *m, int nmeanings);

/* list available dictionaries in directory, returns base names array or NULL */
char **thes_list_dictionaries(const char *dir_path, int *n_dicts);
void thes_free_dictionaries(char **dicts, int n_dicts);

/* return 1 if thesaurus is available (compiled) */
int thes_is_available(void);

#endif
