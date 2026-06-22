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

#ifndef HYPH_H
#define HYPH_H

#include <stddef.h>

#ifndef HYPH_CACHE_N
#define HYPH_CACHE_N 64
#endif

#ifndef HYPH_MAX_WORD
#define HYPH_MAX_WORD 128
#endif

#ifndef HYPH_MAX_BREAKS
#define HYPH_MAX_BREAKS 32
#endif

struct hyph_cache_entry
{
    char key[HYPH_MAX_WORD];
    char hyph[HYPH_MAX_WORD * 2]; /* result with '=' inserted */
    short prev, next;
};

struct hyph_pattern
{
    unsigned char *letters; /* only letters (no digits), UTF-8 bytes */
    unsigned char *levels;  /* level[i] = priority before letters[i] */
    unsigned short llen;    /* number of bytes in letters */
};

struct hyph
{
    char enc[24]; /* "UTF-8" or "ISO8859-1" */
    int lhmin;    /* LEFTHYPHENMIN */
    int rhmin;    /* RIGHTHYPHENMIN */

    struct hyph_pattern *pats;
    int n_pats;

    /* index by first byte to speed up search */
    int idx_first[257]; /* idx_first[c] = first pattern starting with c */

    /* LRU cache */
    struct hyph_cache_entry cache[HYPH_CACHE_N];
    short head, tail;
    short count;
};

/* public API */
struct hyph *hyph_open(const char *path);
void hyph_close(struct hyph *h);
void hyph_cache_clear(struct hyph *h);
const char *hyph_get_encoding(struct hyph *h);
int hyph_hyphenate(struct hyph *h, const char *word, char *out, size_t outsz);

typedef struct hyph HyphDict;

HyphDict *hyph_new(const char *dict_path);
void hyph_free(HyphDict *h);

/* return break points (byte offsets) in out_pos[], count in *out_count */
int hyph_breakpoints(HyphDict *h, const char *word, int word_len, int *out_pos, int *out_count);

/* list available dictionaries in directory, returns base names (without .dic) */
char **hyph_list_dictionaries(const char *dir_path, int *n_dicts);
void hyph_free_dictionaries(char **dicts, int n_dicts);

/* return 1 if hyphenation is available (compiled) */
int hyph_is_available(void);

#endif
