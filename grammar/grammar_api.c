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

#include "grammar_priv.h"
#include "core/portable.h"

/* Default arena size for rule packs */
#ifndef GC_ARENA_DEFAULT
#ifdef PLATFORM_AMIGA
#define GC_ARENA_DEFAULT (48u * 1024u)
#else
#define GC_ARENA_DEFAULT (192u * 1024u)
#endif
#endif

int gc_is_available(void) { return 1; }

/* Category enable/disable */
void gc_set_enabled_categories(GramCheck *g, unsigned mask, int enabled)
{
    if (!g)
        return;

    if (enabled)
        g->enabled_mask |= mask;
    else
        g->enabled_mask &= ~mask;
}

unsigned gc_get_enabled_categories(GramCheck *g)
{
    return g ? g->enabled_mask : 0u;
}

void gc_set_sentence_start(GramCheck *g, int is_start)
{
    if (g)
        g->sentence_start = is_start ? 1 : 0;
}

void gc_set_spell_callback(GramCheck *g, int (*cb)(void *ud, const char *utf8_word, int len), void *ud)
{
    if (!g)
        return;

    g->spell_cb = cb;
    g->spell_ud = ud;

    /* Results depend on the dictionary; flush cached lines */
    gc_cache_clear(g);
}

const char *gc_get_lang(GramCheck *g)
{
    return (g && g->lang) ? g->lang : "";
}

const char *gc_get_name(GramCheck *g)
{
    return (g && g->name) ? g->name : "";
}

const char *gc_rule_name(GramCheck *g, int rule_id)
{
    if (!g || rule_id < 0 || rule_id >= g->n_rules)
        return "?";

    return g->rules[rule_id].name ? g->rules[rule_id].name : "?";
}

GramCheck *gc_new(const char *rul_path)
{
    GramCheck *g = NULL;

    if (!rul_path)
        return NULL;

    g = (GramCheck *)calloc(1u, sizeof(GramCheck));

    if (!g)
        return NULL;

    g->arena.cap = GC_ARENA_DEFAULT;
    g->arena.used = 0;
    g->arena.base = (char *)calloc(g->arena.cap, 1u);

    if (!g->arena.base)
    {
        free(g);
        return NULL;
    }

    g->enabled_mask = GC_CAT_PUNCT | GC_CAT_SPACING | GC_CAT_CASE | GC_CAT_REPEAT | GC_CAT_BRACKET | GC_CAT_TYPO | GC_CAT_STYLE | GC_CAT_SPELL | GC_CAT_GRAMMAR;
    g->sentence_start = 1;

    gc_cache_init(g);

    if (gc_load_rules(g, rul_path) != 0)
    {
        gc_free(g);
        return NULL;
    }

    /* A pack with no rules and no pairs is not useful */
    if (g->n_rules == 0 && g->n_pairs == 0)
    {
        gc_free(g);
        return NULL;
    }

    return g;
}

void gc_free(GramCheck *g)
{
    if (!g)
        return;

    if (g->pairs)
        free(g->pairs);

    if (g->word_caps.words)
        free(g->word_caps.words);

    if (g->word_uppers.words)
        free(g->word_uppers.words);

    if (g->word_lowers.words)
        free(g->word_lowers.words);

    if (g->subj_triggers.words)
        free(g->subj_triggers.words);

    if (g->subj_bad_endings.words)
        free(g->subj_bad_endings.words);

    if (g->tense_past_endings.words)
        free(g->tense_past_endings.words);

    if (g->tense_pres_markers.words)
        free(g->tense_pres_markers.words);

    if (g->agree_exceptions.words)
        free(g->agree_exceptions.words);

    if (g->articles_fem.words)
        free(g->articles_fem.words);

    if (g->articles_masc.words)
        free(g->articles_masc.words);

    if (g->fem_endings.words)
        free(g->fem_endings.words);

    if (g->masc_endings.words)
        free(g->masc_endings.words);

    if (g->masc_exception_endings.words)
        free(g->masc_exception_endings.words);

    if (g->arena.base)
        free(g->arena.base);

    free(g);
}

int gc_check_line_ctx(GramCheck *g, const char *utf8_line, int prev_terminated, GcIssue *out, int cap)
{
    unsigned long h1;
    unsigned long h2;
    size_t len;
    int idx;
    int n;

    if (!g || !utf8_line || !out || cap <= 0)
        return -1;

    len = strlen(utf8_line);

    if (len > GC_MAX_LINE)
        len = GC_MAX_LINE; /* Silently clip */

    h1 = gc_hash32(utf8_line, len);
    h2 = gc_hash32b(utf8_line, len);

    /* Reject h1==0 collision with "empty slot" sentinel */
    if (h1 == 0ul)
        h1 = 0x9E3779B1ul; /* Golden-ratio odd */

    idx = gc_cache_find(g, h1, h2, prev_terminated);

    if (idx >= 0)
    {
        n = g->cache[idx].n_issues;

        if (n > cap)
            n = cap;

        if (n > 0)
            memcpy(out, g->cache[idx].issues, (size_t)n * sizeof(GcIssue));

        /* Bump to LRU-front */
        gc_cache_unlink(g, idx);
        gc_cache_push_front(g, idx);
        return n;
    }

    n = gc_run_checks(g, utf8_line, prev_terminated, out, cap);

    if (n < 0)
        n = 0;

    gc_cache_store(g, h1, h2, prev_terminated, out, n);

    return n;
}

int gc_check_line(GramCheck *g, const char *utf8_line, GcIssue *out, int cap)
{
    return gc_check_line_ctx(g, utf8_line, g ? g->sentence_start : 0, out, cap);
}

char **gc_list_langs(const char *dir_path, int *n_out)
{
    char **v = NULL;
    int cap = 0;
    int n = 0;
    PfDir *d = NULL;
    const char *name = NULL;
    int is_dir;
    size_t nl;

    if (n_out)
        *n_out = 0;

    if (!dir_path)
        return NULL;

    d = pf_dir_open(dir_path);

    if (!d)
        return NULL;

    while ((name = pf_dir_next_entry(d, &is_dir)) != NULL)
    {
        if (is_dir)
            continue;

        nl = strlen(name);

        if (nl <= 4 || strcmp(name + nl - 4, ".rul") != 0)
            continue;

        if (n >= cap)
        {
            int newcap = cap ? cap * 2 : 8;
            char **nv = (char **)realloc(v, (size_t)newcap * sizeof(char *));

            if (!nv)
                break;

            v = nv;
            cap = newcap;
        }

        v[n] = (char *)malloc(nl - 3);

        if (!v[n])
            break;

        memcpy(v[n], name, nl - 4);

        v[n][nl - 4] = '\0';
        n++;
    }

    pf_dir_close(d);

    if (n_out)
        *n_out = n;

    return v;
}

void gc_free_langs(char **v, int n)
{
    int i;

    if (!v)
        return;

    for (i = 0; i < n; i++)
    {
        if (v[i])
            free(v[i]);
    }

    free(v);
}
