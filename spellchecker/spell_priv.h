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

#ifndef SPELL_PRIV_H
#define SPELL_PRIV_H

#include "spell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>

#ifdef PLATFORM_AMIGA
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#endif

#include "../core/utf8.h"

typedef unsigned long ms_cp; /* UTF-8 codepoint */

struct ms_aff_rule
{
    char *strip; /* "" if "0" */
    char *add;   /* suffix/prefix to apply */
    char *cond;  /* simplified regex condition */
    unsigned short strip_len;
    unsigned short add_len;
};

struct ms_aff_group
{
    ms_cp flag;
    char cross_product; /* 'Y' or 'N' */
    char kind;          /* 'S' (suffix) or 'P' (prefix) */
    struct ms_aff_rule *rules;
    int n_rules;
};

struct ms_hentry
{
    char *word;   /* word (utf-8), NULL if slot free */
    ms_cp *flags; /* array of associated flags */
    int n_flags;
    unsigned char attrs; /* bitfield: 1=NOSUGGEST, 2=FORBIDDEN, 4=KEEPCASE */
};

#define MS_ATTR_NOSUGGEST 0x01
#define MS_ATTR_FORBIDDEN 0x02
#define MS_ATTR_KEEPCASE 0x04

struct ms_rep
{
    char *from;
    char *to;
};

struct ms_check_cache
{
    char key[SPELL_MAX_WORD];
    unsigned char result; /* 0 = bad, 1 = good */
    short prev, next;
};

struct spell
{
    char encoding[24];
    int flag_type; /* 0=ASCII, 1=UTF-8, 2=NUM, 3=LONG */
    int flag_utf8; /* 1 if FLAG UTF-8 (deprecated, use flag_type) */

    /* Special-purpose flags: NOSUGGEST, FORBIDDENWORD, KEEPCASE (0 if undeclared) */
    ms_cp flag_nosuggest;
    ms_cp flag_forbidden;
    ms_cp flag_keepcase;

    struct ms_hentry *htab;
    int htab_size; /* Always power of 2 */
    int htab_count;

    struct ms_aff_group *groups;
    int n_groups;

    struct ms_rep *reps;
    int n_reps;

    char ***maps; /* maps[i] = array of strings terminated by NULL */
    int n_maps;

    /* AF alias table: af_flags[i] holds flag list for word/N references (1-indexed) */
    ms_cp **af_flags; /* af_flags[i] = decoded flag array */
    int *af_n_flags;  /* count of flags in af_flags[i] */
    int n_af;         /* number of AF entries declared (af_flags has n_af+1 slots) */

    char *try_chars; /* UTF-8 string with characters to try */

    /* WORDCHARS: extra word characters (e.g. .', digits). KEY: keyboard-adjacent rows for suggest sort. */
    char *key_layout;

    char *wordchars;

    /* BREAK table: characters/strings that split compound words for checking (e.g. BREAK - for light-bulb) */
    char **break_chars;
    int n_break;

    struct ms_check_cache cache[SPELL_CACHE_N];
    short head, tail;
    short cache_count;

    char *sugg_buf[SPELL_MAX_SUGGS];
    int sugg_n;

    char **custom_words;
    int custom_n;
    int custom_cap;

    char **ignored_words;
    int ignored_n;
    int ignored_cap;

    const char **prefix_buf;
    int prefix_cap;
};

/* spell_utils */
char *ms_strdup(const char *s);
int ms_readline(FILE *fp, char *buf, size_t sz);
void ms_rstrip(char *s);
int utf8_decode(const char *p, ms_cp *cp);
ms_cp *parse_flags(const char *s, int flag_type, int *n_out);
int has_flag(const ms_cp *flags, int n, ms_cp f);
unsigned long ms_hash(const char *s);
int utf8_char_is_upper(const char *str, int byte_pos);
int utf8_match_char_case(const char *ref_char, char *target_char, int target_len);
int te_is_word_char(wint_t wc);
int spell_normalize_chars(const char *in, char *out, size_t outsz);

/* spell_htab */
int htab_init(struct spell *m, int initial_pow2);
void htab_insert_into(struct ms_hentry *tab, int sz, char *word, ms_cp *flags, int n_flags, unsigned char attrs);
int htab_resize(struct spell *m, int new_sz);
void htab_insert(struct spell *m, const char *word, const char *flags_str);
struct ms_hentry *htab_find(struct spell *m, const char *word);
void check_cache_init(struct spell *m);
void check_cache_unlink(struct spell *m, int idx);
void check_cache_push_front(struct spell *m, int idx);
int check_cache_find(struct spell *m, const char *word);
int check_cache_acquire(struct spell *m);
void check_cache_put(struct spell *m, const char *word, int res);

/* spell_affix */
int load_aff(struct spell *m, const char *path);
int load_dic(struct spell *m, const char *path);
int cond_match(const char *cond, const char *word, int is_suffix);
int sfx_unapply(const struct ms_aff_rule *r, const char *word, char *out, size_t outsz);
int pfx_unapply(const struct ms_aff_rule *r, const char *word, char *out, size_t outsz);
int check_with_affixes(struct spell *m, const char *word);

/* spell_suggest */
int sugg_try(struct spell *s, const char *cand);

#endif
