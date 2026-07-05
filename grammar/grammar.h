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

#ifndef GRAMMAR_H
#define GRAMMAR_H

#include <stddef.h>

#ifndef GC_MAX_LINE
#define GC_MAX_LINE 4096
#endif

#ifndef GC_MAX_ISSUES_PER_LINE
#define GC_MAX_ISSUES_PER_LINE 32
#endif

#ifndef GC_CACHE_N
#ifdef PLATFORM_AMIGA
#define GC_CACHE_N 128
#else
#define GC_CACHE_N 2048
#endif
#endif

#ifndef GC_MAX_RULES
#define GC_MAX_RULES 512
#endif

#ifndef GC_MAX_PAIRS
#ifdef PLATFORM_AMIGA
#define GC_MAX_PAIRS 256
#else
#define GC_MAX_PAIRS 2048
#endif
#endif

#ifndef GC_MAX_SKIPWORDS
#define GC_MAX_SKIPWORDS 64
#endif

#ifndef GC_MAX_BRACKET_DEPTH
#define GC_MAX_BRACKET_DEPTH 32
#endif

/* Severity levels */
#define GC_INFO 0
#define GC_WARN 1
#define GC_ERROR 2

/* Categories (bitmask so a rule can belong to several) */
#define GC_CAT_PUNCT 0x01
#define GC_CAT_SPACING 0x02
#define GC_CAT_CASE 0x04
#define GC_CAT_REPEAT 0x08
#define GC_CAT_BRACKET 0x10
#define GC_CAT_TYPO 0x20
#define GC_CAT_STYLE 0x40
#define GC_CAT_SPELL 0x80    /* host-integrated spelling */
#define GC_CAT_GRAMMAR 0x100 /* agreement / tense heuristics */

/* A single reported issue on a line. Byte offsets are into the input UTF-8 line */
typedef struct
{
    unsigned short byte_off; /* start byte offset of span (0..GC_MAX_LINE) */
    unsigned short byte_len; /* span length in bytes (0 = point marker) */
    unsigned short rule_id;  /* index into rule table (opaque) */
    unsigned char severity;  /* GC_INFO / GC_WARN / GC_ERROR */
    unsigned short category; /* GC_CAT_* bitmask */
} GcIssue;

/* Opaque handle */
struct gramcheck;
typedef struct gramcheck GramCheck;

/* Open a rule pack (.rul UTF-8 text). Returns NULL on error (file missing, parse error, out of memory) */
GramCheck *gc_new(const char *rul_path);

/* Free everything owned by the handle. Safe on NULL */
void gc_free(GramCheck *g);

/* Return the LANG code declared in the rule file (e.g. "en", "es"), or "" if none */
const char *gc_get_lang(GramCheck *g);

/* Return the human-readable NAME declared in the rule file, or "" if none */
const char *gc_get_name(GramCheck *g);

/* Return rule display name for a rule_id returned in a GcIssue. Return "?" if the rule_id is out of range. Never returns NULL */
const char *gc_rule_name(GramCheck *g, int rule_id);

/* 1 if the grammar module was compiled in (i.e. this symbol exists), 0 stub */
int gc_is_available(void);

/* Check one UTF-8 line. Returns issue count or -1 on error */
int gc_check_line(GramCheck *g, const char *utf8_line, GcIssue *out, int cap);

/* Set whether line starts a new sentence (1) or continues (0) */
void gc_set_sentence_start(GramCheck *g, int is_start);

/* Clear all LRU cache entries */
void gc_cache_clear(GramCheck *g);

/* Invalidate cache for a specific line after editing */
void gc_cache_invalidate_line(GramCheck *g, const char *utf8_line);

/* List .rul files in a directory. Free result with gc_free_langs() */
char **gc_list_langs(const char *dir_path, int *n_out);
void gc_free_langs(char **v, int n);

/* Enable/disable rule categories by bitmask. Call (~0u, 1) to enable all */
void gc_set_enabled_categories(GramCheck *g, unsigned mask, int enabled);

/* Query the current enabled mask */
unsigned gc_get_enabled_categories(GramCheck *g);

/* Register spell-checker callback. Return 1 = correct, 0 = unknown. Pass NULL to unregister */
void gc_set_spell_callback(GramCheck *g, int (*cb)(void *ud, const char *utf8_word, int len), void *ud);

#endif /* GRAMMAR_H */
