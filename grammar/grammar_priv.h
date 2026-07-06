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

#ifndef GRAMMAR_PRIV_H
#define GRAMMAR_PRIV_H

#include "grammar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef PLATFORM_AMIGA
#include <exec/types.h>
#include <dos/dos.h>
#endif

/* Word list — used for WORD_CAP (proper nouns, days, months) and WORD_UPPER (siglas / initialisms). Sorted by lowercase form */
#ifndef GC_MAX_WORDLIST
#ifdef PLATFORM_AMIGA
#define GC_MAX_WORDLIST 128
#else
#define GC_MAX_WORDLIST 1024
#endif
#endif

enum
{
    GR_NONE = 0,
    GR_PUNCT_NO_SPACE_BEFORE, /* data: chars (UTF-8 string of trigger chars) */
    GR_PUNCT_SPACE_AFTER,     /* data: chars */
    GR_PUNCT_NO_DOUBLE,       /* data: chars */
    GR_CAPITALIZE_SENTENCE,   /* no data */
    GR_REPEAT_WORD,           /* no data (skip list is separate) */
    GR_BRACKET_PAIRS,         /* data: pair string "()[]{}" (UTF-8; even chars=open, odd chars=close, in pairs) */
    GR_SPACE_MULTIPLE,        /* no data */
    GR_PAIR,                  /* data: src\0dst\0 (interned in pair table) */
    GR_SPACE_BEFORE_QUOTE_FR, /* no data (French/Spanish NBSP rule) */

    GR_PUNCT_NO_SPACE_AFTER, /* data: chars (e.g. "(¿¡") - no space AFTER these */
    GR_COMMA_BEFORE_DOT,     /* no data - flag ",." */
    GR_SENTENCE_OPEN_ES,     /* data: "?!" - question/exclaim needs ¿ ¡ opener */
    GR_TRIPLE_DOT,           /* no data - "..." -> suggest "…" */
    GR_DOUBLE_DASH,          /* no data - "--" -> suggest "—" */
    GR_STRAIGHT_QUOTES,      /* no data - flag ASCII " and ' */
    GR_WORD_CAP,             /* no data (list in gc_word_caps table) - words that should be capitalized */
    GR_WORD_UPPER,           /* no data (list in gc_word_uppers table) - words always ALL CAPS (siglas) */
    GR_ALL_CAPS_WORD,        /* data: min-length (default 5) - flag ALL-CAPS words above N letters */
    GR_SENTENCE_TOO_LONG,    /* data: max-words (default 30) - flag long sentences */
    GR_TOO_MANY_COMMAS,      /* data: "N W" - N+ commas in W word window */
    GR_REPEAT_PHRASE,        /* no data - flag repeated 2..4-word phrases */
    GR_TRAILING_DOTS,        /* no data - flag multiple '.' at end of line (excluding "...") */

    GR_EXCESSIVE_PUNCT,        /* param1: max run (default 2) - flag "!!!", "?!?!" runs of !? */
    GR_NBSP_REQUIRED,          /* data: chars needing NBSP before them (fr: ;:!?) */
    GR_WORD_LOWER,             /* wordlist: words that must be lowercase (es months mid-sentence) */
    GR_AGREEMENT_ART_NOUN,     /* es/pt/it/fr heuristic: el+fem / la+masc */
    GR_AGREEMENT_NOUN_ADJ,     /* es heuristic: noun-o + adj-a mismatch */
    GR_ARTICLE_FEM,            /* list of feminine articles loaded from .rul */
    GR_ARTICLE_MASC,           /* list of masculine articles loaded from .rul */
    GR_FEM_ENDINGS,            /* list of feminine word endings loaded from .rul */
    GR_MASC_ENDINGS,           /* list of masculine word endings loaded from .rul */
    GR_MASC_EXCEPTION_ENDINGS, /* list of masculine exception endings loaded from .rul */
    GR_WORD_DENSITY,           /* param1: N repeats, param2: M word window */
    GR_SUBJUNCTIVE_AFTER,      /* phrase list: "para que|sin que|..." expect subjunctive next */
    GR_TENSE_MIX,              /* es heuristic: present + preterite in same sentence */
    GR_QUOTE_SPACING,          /* space INSIDE quotes: " foo" / "foo " */
    GR_SPELL_UNKNOWN,          /* host-provided spell callback says word unknown */

    GR_KIND_MAX
};

/* Compact rule entry */
struct gc_rule
{
    unsigned short kind;
    unsigned short category;
    unsigned char severity;
    unsigned char reserved;
    const char *data;
    const char *msg;
    const char *name;
    /* Numeric parameters for rules that need them (e.g. min-length, N, W) */
    short param1;
    short param2;
};

/* Word-pair table entry. Kept separate from rules for locality */
struct gc_pair
{
    const char *src; /* interned, UTF-8, lower-cased */
    const char *dst; /* interned, UTF-8, suggested spelling */
    const char *msg; /* optional, may be NULL */
    unsigned short len_src;
    unsigned char severity;
    unsigned char reserved;
};

/* LRU line-cache entry. Same layout idea as spell/check_cache but keyed by a 32-bit hash pair. Value is a small array of issues */
struct gc_cache_entry
{
    unsigned long key_hash;      /* 0 == empty slot */
    unsigned long key_secondary; /* collision guard (second hash) */
    unsigned short n_issues;
    short prev; /* LRU list linkage */
    short next;
    unsigned char prev_terminated; /* 1 = previous line ended with . / ! / ? */
    GcIssue issues[GC_MAX_ISSUES_PER_LINE];
};

/* Bump arena for interned strings loaded from rule packs. Never freed until gc_free() — everything lives as long as the checker */
struct gc_arena
{
    char *base;
    size_t used;
    size_t cap;
};

/* Skip-word list (for REPEAT_WORD_SKIP). Words are lowercased on insert */
struct gc_skip
{
    const char *w[GC_MAX_SKIPWORDS];
    int n;
};

struct gc_wordlist
{
    const char **words; /* array of lowercase forms, interned */
    int n;
    int cap;
};

/* Main handle */
struct gramcheck
{
    /* Metadata (owned in arena) */
    const char *lang;
    const char *name;

    /* Rule table */
    struct gc_rule rules[GC_MAX_RULES];
    int n_rules;

    /* Word pair dictionary (sorted by src for bsearch) */
    struct gc_pair *pairs;
    int n_pairs;
    int cap_pairs;

    /* Skip list for REPEAT_WORD */
    struct gc_skip skip;

    /* Word lists for WORD_CAP / WORD_UPPER */
    struct gc_wordlist word_caps;
    struct gc_wordlist word_uppers;

    /* Words that must stay lowercase mid-sentence (es months) */
    struct gc_wordlist word_lowers;

    /* Subjunctive-trigger phrases ("para que", "sin que", ...) */
    struct gc_wordlist subj_triggers;

    /* Indicative endings that flag a violation after a trigger */
    struct gc_wordlist subj_bad_endings;

    /* Preterite endings (for TENSE_MIX heuristic) */
    struct gc_wordlist tense_past_endings;

    /* Present endings (for TENSE_MIX heuristic) */
    struct gc_wordlist tense_pres_markers;

    /* Exceptions for AGREEMENT_ART_NOUN (el agua, el alma, ...) */
    struct gc_wordlist agree_exceptions;

    /* Feminine and masculine articles for agreement rules */
    struct gc_wordlist articles_fem;
    struct gc_wordlist articles_masc;

    /* Feminine and masculine word endings for agreement rules */
    struct gc_wordlist fem_endings;
    struct gc_wordlist masc_endings;
    struct gc_wordlist masc_exception_endings;

    /* Host spell-checker callback (GR_SPELL_UNKNOWN). Returns 1 if the UTF-8 word is CORRECT, 0 if unknown/misspelled. NULL = off */
    int (*spell_cb)(void *ud, const char *utf8_word, int len);
    void *spell_ud;

    /* Enabled categories bitmask */
    unsigned enabled_mask;

    /* Sentence-continuation flag (see gc_set_sentence_start) */
    int sentence_start;

    /* String arena */
    struct gc_arena arena;

    /* LRU line cache */
    struct gc_cache_entry cache[GC_CACHE_N];
    short head;
    short tail;
    short count;
};

/* Bump-alloc n bytes zeroed, or NULL if the arena would overflow */
void *gc_arena_alloc(struct gc_arena *a, size_t n);

/* Duplicate a NUL-terminated string into the arena. Returns NULL if
 * out-of-space or if src is NULL */
const char *gc_arena_strdup(struct gc_arena *a, const char *s);

/* Duplicate n bytes plus a NUL terminator into the arena */
const char *gc_arena_strndup(struct gc_arena *a, const char *s, size_t n);

/* UTF-8 decode single codepoint. *bytes_used gets 1..4 on success, 1 on invalid (advance-and-report). Returns codepoint (>=0) */
unsigned long gc_utf8_decode(const char *s, int *bytes_used);

/* True if codepoint is an ASCII whitespace or Unicode space */
int gc_is_space_cp(unsigned long cp);

/* True if codepoint is a letter (Unicode-lite: covers Latin, Latin-1 supplement, Latin Extended A/B via ranges; ASCII fast-path) */
int gc_is_letter_cp(unsigned long cp);

/* True if codepoint is uppercase (subset: ASCII + Latin-1 A-Ö/Ø-Þ + some) */
int gc_is_upper_cp(unsigned long cp);

/* Lowercase a codepoint (subset — same as gc_is_upper_cp coverage) */
unsigned long gc_tolower_cp(unsigned long cp);

/* Case-fold copy of s into out (bounded). Returns bytes written (excl. NUL) */
int gc_utf8_lower(const char *s, char *out, size_t outsz);

/* Test whether codepoint appears in NUL-terminated UTF-8 charset string */
int gc_charset_contains(const char *charset, unsigned long cp);

/* True if codepoint is a double quote (ASCII " or typographic 0x201C/0x201D/0xAB/0xBB) */
int gc_is_str_quote_cp(unsigned long cp);

/* True if codepoint is a single quote (ASCII ' or typographic 0x2018/0x2019) */
int gc_is_char_quote_cp(unsigned long cp);

/* FNV-1a 32-bit over a byte range */
unsigned long gc_hash32(const char *s, size_t n);

/* Djb2 secondary hash (for cache collision guard) */
unsigned long gc_hash32b(const char *s, size_t n);

void gc_cache_init(GramCheck *g);
int gc_cache_find(GramCheck *g, unsigned long h1, unsigned long h2, int prev_terminated);
int gc_cache_acquire(GramCheck *g);
void gc_cache_unlink(GramCheck *g, int idx);
void gc_cache_push_front(GramCheck *g, int idx);
void gc_cache_store(GramCheck *g, unsigned long h1, unsigned long h2, int prev_terminated, const GcIssue *issues, int n);

int gc_load_rules(GramCheck *g, const char *path);
int gc_add_pair(GramCheck *g, const char *src, const char *dst, int severity, const char *msg);
void gc_sort_pairs(GramCheck *g);
const struct gc_pair *gc_find_pair(GramCheck *g, const char *lower_word, size_t len);

/* Add a lower-cased word to a wordlist (WORD_CAP / WORD_UPPER) */
int gc_wl_add(GramCheck *g, struct gc_wordlist *wl, const char *word);
void gc_wl_sort(struct gc_wordlist *wl);
int gc_wl_contains(const struct gc_wordlist *wl, const char *lower_word, size_t len);

int gc_run_checks(GramCheck *g, const char *line, int prev_terminated, GcIssue *out, int cap);

/* Utility to append an issue while respecting cap. Returns updated count */
int gc_emit(GcIssue *out, int cur, int cap, unsigned short off, unsigned short len, unsigned short rule_id, unsigned char sev, unsigned short cat);

#endif /* GRAMMAR_PRIV_H */
