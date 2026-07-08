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

static char *gc_rstrip(char *s)
{
    size_t n;

    if (!s)
        return s;

    n = strlen(s);

    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';

    return s;
}

static char *gc_lstrip(char *s)
{
    if (!s)
        return s;

    while (*s == ' ' || *s == '\t')
        s++;

    return s;
}

/* Read one logical line, up to sz-1 bytes. Returns 1 on success, 0 on EOF, -1 on error */
static int gc_readline(FILE *fp, char *buf, size_t sz)
{
    if (!fgets(buf, (int)sz, fp))
        return feof(fp) ? 0 : -1;

    gc_rstrip(buf);
    return 1;
}

/* Split whitespace-delimited token, advance *rest to remainder */
static char *gc_split_token(char *s, char **rest)
{
    char *p = NULL;
    char *tok = NULL;

    p = gc_lstrip(s);
    tok = p;

    while (*p && *p != ' ' && *p != '\t')
        p++;

    if (*p)
    {
        *p = '\0';
        p++;
        p = gc_lstrip(p);
    }

    if (rest)
        *rest = p;

    return tok;
}

/* Like gc_split_token but a leading quote groups words until the closing quote */
static char *gc_split_qtoken(char *s, char **rest)
{
    char *p = NULL;
    char *tok = NULL;

    p = gc_lstrip(s);

    if (*p == '"')
    {
        p++;
        tok = p;

        while (*p && *p != '"')
            p++;
    }
    else
    {
        tok = p;

        while (*p && *p != ' ' && *p != '\t')
            p++;
    }

    if (*p)
    {
        *p = '\0';
        p++;
        p = gc_lstrip(p);
    }

    if (rest)
        *rest = p;

    return tok;
}

static void gc_add_rule(GramCheck *g, unsigned short kind, unsigned short category, unsigned char severity, const char *data, const char *msg, const char *name)
{
    struct gc_rule *r = NULL;

    if (g->n_rules >= GC_MAX_RULES)
        return;

    r = &g->rules[g->n_rules++];
    r->kind = kind;
    r->category = category;
    r->severity = severity;
    r->reserved = 0;
    r->data = data;
    r->msg = msg;
    r->name = name;
    r->param1 = 0;
    r->param2 = 0;
}

/* Turn a comma list into an interned "|a|b|" membership string, lower-cased */
static const char *gc_intern_ctx_list(GramCheck *g, const char *list)
{
    char buf[256];
    char lower[256];
    size_t n = 0;
    const char *p = list;

    if (!list || !*list)
        return NULL;

    buf[n++] = '|';

    while (*p && n < sizeof(buf) - 2)
    {
        if (*p == ',')
        {
            if (buf[n - 1] != '|')
                buf[n++] = '|';

            p++;
            continue;
        }

        buf[n++] = *p++;
    }

    if (buf[n - 1] != '|')
        buf[n++] = '|';

    buf[n] = '\0';

    gc_utf8_lower(buf, lower, sizeof(lower));

    return gc_arena_strdup(&g->arena, lower);
}

int gc_add_pair_ctx(GramCheck *g, const char *src, const char *dst, int severity, const char *msg, const char *prev, const char *next, unsigned char flags)
{
    struct gc_pair *p = NULL;
    char lower[128];

    if (!g || !src || !*src)
        return -1;

    if (g->n_pairs >= GC_MAX_PAIRS)
        return -1;

    if (!g->pairs)
    {
        g->cap_pairs = GC_MAX_PAIRS;
        g->pairs = (struct gc_pair *)calloc((size_t)g->cap_pairs, sizeof(struct gc_pair));

        if (!g->pairs)
            return -1;
    }

    gc_utf8_lower(src, lower, sizeof(lower));

    p = &g->pairs[g->n_pairs];
    p->src = gc_arena_strdup(&g->arena, lower);
    p->dst = dst ? gc_arena_strdup(&g->arena, dst) : NULL;
    p->msg = msg ? gc_arena_strdup(&g->arena, msg) : NULL;
    p->prev = gc_intern_ctx_list(g, prev);
    p->next = gc_intern_ctx_list(g, next);

    if (!p->src)
        return -1;

    p->len_src = (unsigned short)strlen(p->src);
    p->severity = (unsigned char)severity;
    p->flags = flags;

    g->n_pairs++;

    return 0;
}

int gc_add_pair(GramCheck *g, const char *src, const char *dst, int severity, const char *msg)
{
    return gc_add_pair_ctx(g, src, dst, severity, msg, NULL, NULL, 0);
}

static int gc_pair_cmp(const void *a, const void *b)
{
    const struct gc_pair *pa = (const struct gc_pair *)a;
    const struct gc_pair *pb = (const struct gc_pair *)b;

    if (!pa->src && !pb->src)
        return 0;

    if (!pa->src)
        return 1;

    if (!pb->src)
        return -1;

    return strcmp(pa->src, pb->src);
}

void gc_sort_pairs(GramCheck *g)
{
    if (!g || !g->pairs || g->n_pairs < 2)
        return;

    qsort(g->pairs, (size_t)g->n_pairs, sizeof(struct gc_pair), gc_pair_cmp);
}

const struct gc_pair *gc_find_pair(GramCheck *g, const char *lower_word, size_t len)
{
    int lo, hi, mid, cmp;
    const struct gc_pair *p = NULL;
    char tmp[128];

    if (!g || !g->pairs || g->n_pairs <= 0 || !lower_word || len == 0)
        return NULL;

    if (len >= sizeof(tmp))
        return NULL;

    memcpy(tmp, lower_word, len);
    tmp[len] = '\0';

    lo = 0;
    hi = g->n_pairs - 1;

    while (lo <= hi)
    {
        mid = (lo + hi) >> 1;
        p = &g->pairs[mid];

        if (!p->src)
            return NULL;

        cmp = strcmp(p->src, tmp);

        if (cmp == 0)
            return p;

        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    return NULL;
}

/* First entry of the equal-src run so callers can walk context variants */
const struct gc_pair *gc_find_pair_first(GramCheck *g, const char *lower_word, size_t len)
{
    const struct gc_pair *p = gc_find_pair(g, lower_word, len);

    if (!p)
        return NULL;

    while (p > g->pairs && strcmp((p - 1)->src, p->src) == 0)
        p--;

    return p;
}

/* Membership test against an interned "|a|b|" list */
int gc_ctx_list_has(const char *list, const char *lower_tok, size_t len)
{
    const char *p = NULL;

    if (!list || !lower_tok || len == 0)
        return 0;

    p = list;

    while ((p = strchr(p, '|')) != NULL)
    {
        p++;

        if (strncmp(p, lower_tok, len) == 0 && p[len] == '|')
            return 1;
    }

    return 0;
}

int gc_wl_add(GramCheck *g, struct gc_wordlist *wl, const char *word)
{
    char lower[128];

    if (!g || !wl || !word || !*word)
        return -1;

    if (strlen(word) >= sizeof(lower))
        return -1;

    if (!wl->words)
    {
        wl->cap = GC_MAX_WORDLIST;
        wl->words = (const char **)calloc((size_t)wl->cap, sizeof(char *));

        if (!wl->words)
            return -1;
    }

    if (wl->n >= wl->cap)
        return -1;

    gc_utf8_lower(word, lower, sizeof(lower));

    wl->words[wl->n] = gc_arena_strdup(&g->arena, lower);

    if (!wl->words[wl->n])
        return -1;

    wl->n++;
    return 0;
}

static int gc_wl_cmp(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;

    if (!sa && !sb)
        return 0;

    if (!sa)
        return 1;

    if (!sb)
        return -1;

    return strcmp(sa, sb);
}

void gc_wl_sort(struct gc_wordlist *wl)
{
    if (!wl || !wl->words || wl->n < 2)
        return;

    qsort(wl->words, (size_t)wl->n, sizeof(char *), gc_wl_cmp);
}

int gc_wl_contains(const struct gc_wordlist *wl, const char *lower_word, size_t len)
{
    int lo, hi, mid, cmp;
    char tmp[128];

    if (!wl || !wl->words || wl->n <= 0 || !lower_word || len == 0)
        return 0;

    if (len >= sizeof(tmp))
        return 0;

    memcpy(tmp, lower_word, len);
    tmp[len] = '\0';

    lo = 0;
    hi = wl->n - 1;

    while (lo <= hi)
    {
        mid = (lo + hi) >> 1;

        if (!wl->words[mid])
            return 0;

        cmp = strcmp(wl->words[mid], tmp);

        if (cmp == 0)
            return 1;

        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    return 0;
}

/* Load a comma-separated word list into a wordlist */
static void gc_load_wordlist(GramCheck *g, struct gc_wordlist *wl, const char *csv)
{
    const char *p = NULL;
    const char *s = NULL;
    char buf[128];
    size_t n;

    if (!csv)
        return;

    p = csv;

    while (*p && wl->n < GC_MAX_WORDLIST)
    {
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;

        if (!*p)
            break;

        s = p;

        while (*p && *p != ',' && *p != ' ' && *p != '\t')
            p++;

        n = (size_t)(p - s);

        if (n == 0 || n >= sizeof(buf))
            continue;

        memcpy(buf, s, n);
        buf[n] = '\0';

        gc_wl_add(g, wl, buf);
    }
}

/* Load '|'-separated phrase list, lower-case and trim each phrase */
static void gc_load_phraselist(GramCheck *g, struct gc_wordlist *wl, const char *psv)
{
    const char *p = NULL;
    const char *s = NULL;
    char buf[128];
    size_t n;

    if (!psv)
        return;

    p = psv;

    while (*p && wl->n < GC_MAX_WORDLIST)
    {
        while (*p == '|' || *p == ' ' || *p == '\t')
            p++;

        if (!*p)
            break;

        s = p;

        while (*p && *p != '|')
            p++;

        n = (size_t)(p - s);

        /* Trim trailing spaces */
        while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
            n--;

        if (n == 0 || n >= sizeof(buf))
            continue;

        memcpy(buf, s, n);
        buf[n] = '\0';

        gc_wl_add(g, wl, buf);
    }
}

static void gc_load_skip(GramCheck *g, const char *csv)
{
    const char *p = NULL;
    const char *s = NULL;
    char buf[64];
    char lower[64];
    size_t n;

    if (!g || !csv)
        return;

    g->skip.n = 0;
    p = csv;

    while (*p && g->skip.n < GC_MAX_SKIPWORDS)
    {
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;

        if (!*p)
            break;

        s = p;

        while (*p && *p != ',' && *p != ' ' && *p != '\t')
            p++;

        n = (size_t)(p - s);

        if (n == 0 || n >= sizeof(buf))
            continue;

        memcpy(buf, s, n);
        buf[n] = '\0';

        gc_utf8_lower(buf, lower, sizeof(lower));

        g->skip.w[g->skip.n++] = gc_arena_strdup(&g->arena, lower);
    }
}

int gc_load_rules(GramCheck *g, const char *path)
{
    FILE *fp = NULL;
    char line[1024];
    char *raw = NULL;
    char *tok = NULL;
    char *rest = NULL;
    unsigned char severity;
    int rc;

    if (!g || !path)
        return -1;

    fp = fopen(path, "r");

    if (!fp)
        return -1;

    for (;;)
    {
        rc = gc_readline(fp, line, sizeof(line));

        if (rc <= 0)
            break;

        raw = gc_lstrip(line);

        if (!*raw || *raw == '#')
            continue;

        tok = gc_split_token(raw, &rest);

        if (strcmp(tok, "LANG") == 0)
        {
            g->lang = gc_arena_strdup(&g->arena, gc_rstrip(rest));
        }
        else if (strcmp(tok, "NAME") == 0)
        {
            g->name = gc_arena_strdup(&g->arena, gc_rstrip(rest));
        }
        else if (strcmp(tok, "ARENA") == 0)
        {
            /* Advisory only — the arena is already sized at gc_new() */
        }
        else if (strcmp(tok, "PUNCT_NO_SPACE_BEFORE") == 0)
        {
            const char *data = gc_arena_strdup(&g->arena, gc_rstrip(rest));

            gc_add_rule(g, GR_PUNCT_NO_SPACE_BEFORE, GC_CAT_PUNCT | GC_CAT_SPACING, GC_WARN, data, "no space before punctuation", "punct.no_space_before");
        }
        else if (strcmp(tok, "PUNCT_SPACE_AFTER") == 0)
        {
            const char *data = gc_arena_strdup(&g->arena, gc_rstrip(rest));

            gc_add_rule(g, GR_PUNCT_SPACE_AFTER, GC_CAT_PUNCT | GC_CAT_SPACING, GC_WARN, data, "missing space after punctuation", "punct.space_after");
        }
        else if (strcmp(tok, "PUNCT_NO_DOUBLE") == 0)
        {
            const char *data = gc_arena_strdup(&g->arena, gc_rstrip(rest));

            gc_add_rule(g, GR_PUNCT_NO_DOUBLE, GC_CAT_PUNCT, GC_WARN, data, "duplicated punctuation", "punct.no_double");
        }
        else if (strcmp(tok, "CAPITALIZE_SENTENCE") == 0)
        {
            gc_add_rule(g, GR_CAPITALIZE_SENTENCE, GC_CAT_CASE, GC_WARN, NULL, "sentence should start with a capital letter", "case.sentence_start");
        }
        else if (strcmp(tok, "REPEAT_WORD") == 0)
        {
            gc_add_rule(g, GR_REPEAT_WORD, GC_CAT_REPEAT, GC_WARN, NULL, "repeated word", "repeat.word");
        }
        else if (strcmp(tok, "REPEAT_WORD_SKIP") == 0)
        {
            gc_load_skip(g, gc_rstrip(rest));
        }
        else if (strcmp(tok, "BRACKET_PAIRS") == 0)
        {
            const char *data = gc_arena_strdup(&g->arena, gc_rstrip(rest));

            gc_add_rule(g, GR_BRACKET_PAIRS, GC_CAT_BRACKET, GC_ERROR, data, "unbalanced bracket", "bracket.pairs");
        }
        else if (strcmp(tok, "SPACE_MULTIPLE") == 0)
        {
            gc_add_rule(g, GR_SPACE_MULTIPLE, GC_CAT_SPACING, GC_INFO, NULL, "multiple consecutive spaces", "space.multiple");
        }
        else if (strcmp(tok, "SPACE_BEFORE_QUOTE_FR") == 0)
        {
            gc_add_rule(g, GR_SPACE_BEFORE_QUOTE_FR, GC_CAT_SPACING, GC_INFO, NULL, "expected NBSP before ; : ! ?", "space.fr_nbsp");
        }
        else if (strcmp(tok, "PUNCT_NO_SPACE_AFTER") == 0)
        {
            const char *data = gc_arena_strdup(&g->arena, gc_rstrip(rest));

            gc_add_rule(g, GR_PUNCT_NO_SPACE_AFTER, GC_CAT_SPACING | GC_CAT_PUNCT, GC_WARN, data, "no space after opening punctuation", "punct.no_space_after");
        }
        else if (strcmp(tok, "COMMA_BEFORE_DOT") == 0)
        {
            gc_add_rule(g, GR_COMMA_BEFORE_DOT, GC_CAT_PUNCT, GC_WARN, NULL, "comma immediately before period", "punct.comma_dot");
        }
        else if (strcmp(tok, "SENTENCE_OPEN_ES") == 0)
        {
            const char *data = gc_arena_strdup(&g->arena, gc_rstrip(rest));

            gc_add_rule(g, GR_SENTENCE_OPEN_ES, GC_CAT_PUNCT, GC_WARN, data, "missing opening ¿ or ¡", "punct.open_es");
        }
        else if (strcmp(tok, "TRIPLE_DOT") == 0)
        {
            gc_add_rule(g, GR_TRIPLE_DOT, GC_CAT_STYLE, GC_INFO, NULL, "three dots — consider '…'", "typo.triple_dot");
        }
        else if (strcmp(tok, "DOUBLE_DASH") == 0)
        {
            gc_add_rule(g, GR_DOUBLE_DASH, GC_CAT_STYLE, GC_INFO, NULL, "double dash — consider '—' (em-dash)", "typo.double_dash");
        }
        else if (strcmp(tok, "STRAIGHT_QUOTES") == 0)
        {
            gc_add_rule(g, GR_STRAIGHT_QUOTES, GC_CAT_STYLE, GC_INFO, NULL, "straight quote — consider typographic", "typo.straight_quote");
        }
        else if (strcmp(tok, "WORD_CAP") == 0)
        {
            gc_load_wordlist(g, &g->word_caps, gc_rstrip(rest));
        }
        else if (strcmp(tok, "WORD_UPPER") == 0)
        {
            gc_load_wordlist(g, &g->word_uppers, gc_rstrip(rest));
        }
        else if (strcmp(tok, "ALL_CAPS_WORD") == 0)
        {
            struct gc_rule *r = NULL;
            int minlen = atoi(gc_rstrip(rest));

            if (minlen <= 0)
                minlen = 5;

            gc_add_rule(g, GR_ALL_CAPS_WORD, GC_CAT_STYLE, GC_INFO, NULL, "avoid all-caps words", "style.all_caps");

            r = &g->rules[g->n_rules - 1];
            r->param1 = (short)minlen;
        }
        else if (strcmp(tok, "SENTENCE_TOO_LONG") == 0)
        {
            struct gc_rule *r = NULL;
            int maxw = atoi(gc_rstrip(rest));

            if (maxw <= 0)
                maxw = 30;

            gc_add_rule(g, GR_SENTENCE_TOO_LONG, GC_CAT_STYLE, GC_INFO, NULL, "sentence too long", "style.long_sentence");

            r = &g->rules[g->n_rules - 1];
            r->param1 = (short)maxw;
        }
        else if (strcmp(tok, "TOO_MANY_COMMAS") == 0)
        {
            struct gc_rule *r = NULL;
            int n_com = 0, w_win = 0;
            char *r2 = NULL;
            char *t1 = gc_split_token(rest, &r2);
            char *t2 = r2 ? gc_split_token(r2, NULL) : NULL;

            n_com = t1 ? atoi(t1) : 4;
            w_win = t2 ? atoi(t2) : 10;

            if (n_com <= 0)
                n_com = 4;

            if (w_win <= 0)
                w_win = 10;

            gc_add_rule(g, GR_TOO_MANY_COMMAS, GC_CAT_STYLE, GC_INFO, NULL, "many commas in a short span", "style.many_commas");

            r = &g->rules[g->n_rules - 1];
            r->param1 = (short)n_com;
            r->param2 = (short)w_win;
        }
        else if (strcmp(tok, "REPEAT_PHRASE") == 0)
        {
            gc_add_rule(g, GR_REPEAT_PHRASE, GC_CAT_REPEAT, GC_INFO, NULL, "possibly repeated phrase", "repeat.phrase");
        }
        else if (strcmp(tok, "TRAILING_DOTS") == 0)
        {
            gc_add_rule(g, GR_TRAILING_DOTS, GC_CAT_PUNCT, GC_INFO, NULL, "multiple dots at end of line", "punct.trailing_dots");
        }
        else if (strcmp(tok, "EXCESSIVE_PUNCT") == 0)
        {
            struct gc_rule *r = NULL;
            int maxrun = atoi(gc_rstrip(rest));

            if (maxrun <= 0)
                maxrun = 2;

            gc_add_rule(g, GR_EXCESSIVE_PUNCT, GC_CAT_PUNCT, GC_WARN, NULL, "excessive ! or ? marks", "punct.excessive");

            r = &g->rules[g->n_rules - 1];
            r->param1 = (short)maxrun;
        }
        else if (strcmp(tok, "NBSP_REQUIRED") == 0)
        {
            const char *data = gc_arena_strdup(&g->arena, gc_rstrip(rest));

            gc_add_rule(g, GR_NBSP_REQUIRED, GC_CAT_TYPO | GC_CAT_SPACING, GC_INFO, data, "use non-breaking space here", "typo.nbsp");
        }
        else if (strcmp(tok, "WORD_LOWER") == 0)
        {
            gc_load_wordlist(g, &g->word_lowers, gc_rstrip(rest));

            /* Ensure a rule entry exists so it has a rule_id/name */
            gc_add_rule(g, GR_WORD_LOWER, GC_CAT_CASE, GC_INFO, NULL, "word should be lowercase here", "case.word_lower");
        }
        else if (strcmp(tok, "AGREEMENT_ART_NOUN") == 0)
        {
            gc_add_rule(g, GR_AGREEMENT_ART_NOUN, GC_CAT_GRAMMAR, GC_INFO, NULL, "article/noun gender mismatch", "gram.art_noun");
        }
        else if (strcmp(tok, "AGREEMENT_EXCEPTIONS") == 0)
        {
            gc_load_wordlist(g, &g->agree_exceptions, gc_rstrip(rest));
        }
        else if (strcmp(tok, "ARTICLE_FEM") == 0)
        {
            gc_load_wordlist(g, &g->articles_fem, gc_rstrip(rest));
        }
        else if (strcmp(tok, "ARTICLE_MASC") == 0)
        {
            gc_load_wordlist(g, &g->articles_masc, gc_rstrip(rest));
        }
        else if (strcmp(tok, "FEM_ENDINGS") == 0)
        {
            gc_load_wordlist(g, &g->fem_endings, gc_rstrip(rest));
        }
        else if (strcmp(tok, "MASC_ENDINGS") == 0)
        {
            gc_load_wordlist(g, &g->masc_endings, gc_rstrip(rest));
        }
        else if (strcmp(tok, "MASC_EXCEPTION_ENDINGS") == 0)
        {
            gc_load_wordlist(g, &g->masc_exception_endings, gc_rstrip(rest));
        }
        else if (strcmp(tok, "AGREEMENT_NOUN_ADJ") == 0)
        {
            gc_add_rule(g, GR_AGREEMENT_NOUN_ADJ, GC_CAT_GRAMMAR, GC_INFO, NULL, "noun/adjective agreement mismatch", "gram.noun_adj");
        }
        else if (strcmp(tok, "WORD_DENSITY") == 0)
        {
            struct gc_rule *r = NULL;
            int n_rep = 0, w_win = 0;
            char *r2 = NULL;
            char *t1 = gc_split_token(rest, &r2);
            char *t2 = r2 ? gc_split_token(r2, NULL) : NULL;

            n_rep = t1 ? atoi(t1) : 3;
            w_win = t2 ? atoi(t2) : 12;

            if (n_rep <= 0)
                n_rep = 3;

            if (w_win <= 0)
                w_win = 12;

            gc_add_rule(g, GR_WORD_DENSITY, GC_CAT_STYLE | GC_CAT_REPEAT, GC_INFO, NULL, "word repeated too often nearby", "style.word_density");

            r = &g->rules[g->n_rules - 1];
            r->param1 = (short)n_rep;
            r->param2 = (short)w_win;
        }
        else if (strcmp(tok, "SUBJUNCTIVE_AFTER") == 0)
        {
            gc_load_phraselist(g, &g->subj_triggers, gc_rstrip(rest));
            gc_add_rule(g, GR_SUBJUNCTIVE_AFTER, GC_CAT_GRAMMAR, GC_INFO, NULL, "expected subjunctive after this phrase", "gram.subjunctive");
        }
        else if (strcmp(tok, "SUBJUNCTIVE_BAD_ENDINGS") == 0)
        {
            gc_load_wordlist(g, &g->subj_bad_endings, gc_rstrip(rest));
        }
        else if (strcmp(tok, "TENSE_PAST_ENDINGS") == 0)
        {
            gc_load_wordlist(g, &g->tense_past_endings, gc_rstrip(rest));
        }
        else if (strcmp(tok, "TENSE_PRESENT_MARKERS") == 0)
        {
            gc_load_wordlist(g, &g->tense_pres_markers, gc_rstrip(rest));
        }
        else if (strcmp(tok, "TENSE_MIX") == 0)
        {
            gc_add_rule(g, GR_TENSE_MIX, GC_CAT_GRAMMAR, GC_INFO, NULL, "mixed verb tenses in one sentence", "gram.tense_mix");
        }
        else if (strcmp(tok, "QUOTE_SPACING") == 0)
        {
            gc_add_rule(g, GR_QUOTE_SPACING, GC_CAT_SPACING, GC_INFO, NULL, "space just inside quotes", "space.quotes");
        }
        else if (strcmp(tok, "SPELL_UNKNOWN") == 0)
        {
            gc_add_rule(g, GR_SPELL_UNKNOWN, GC_CAT_SPELL, GC_WARN, NULL, "possibly misspelled word", "spell.unknown");
        }
        else if (strcmp(tok, "PAIR") == 0)
        {
            char *src = NULL;
            char *dst = NULL;
            char *msg = NULL;
            char *r2 = NULL;

            /* Quotes group multi-word sources and suggestions */
            src = gc_split_qtoken(rest, &r2);
            dst = gc_split_qtoken(r2, &msg);
            msg = gc_lstrip(msg);

            severity = GC_INFO;

            /* Optional leading severity keyword */
            if (msg && *msg && (strncmp(msg, "warn", 4) == 0) && (msg[4] == '\0' || msg[4] == ' ' || msg[4] == '\t' || msg[4] == '#'))
            {
                severity = GC_WARN;
                msg = gc_lstrip(msg + 4);
            }
            else if (msg && *msg && (strncmp(msg, "err", 3) == 0) && (msg[3] == '\0' || msg[3] == ' ' || msg[3] == '\t' || msg[3] == '#'))
            {
                severity = GC_ERROR;
                msg = gc_lstrip(msg + 3);
            }
            else if (msg && *msg && (strncmp(msg, "info", 4) == 0) && (msg[4] == '\0' || msg[4] == ' ' || msg[4] == '\t' || msg[4] == '#'))
            {
                severity = GC_INFO;
                msg = gc_lstrip(msg + 4);
            }

            if (msg && *msg == '#')
                msg = gc_lstrip(msg + 1);

            if (src && *src && dst && *dst)
                gc_add_pair(g, src, dst, severity, (msg && *msg) ? msg : NULL);
        }
        else if (strcmp(tok, "PAIRCTX") == 0)
        {
            char *src = NULL;
            char *dst = NULL;
            char *opt = NULL;
            char *r2 = NULL;
            char *prev = NULL;
            char *next = NULL;
            char *msg = NULL;
            unsigned char flags = 0;

            src = gc_split_qtoken(rest, &r2);
            dst = gc_split_qtoken(r2, &r2);

            severity = GC_INFO;

            /* Options in any order: PREV=a,b NEXT=a,$ QMARK warn/err/info */
            while (r2 && *r2 && *r2 != '#')
            {
                opt = gc_split_token(r2, &r2);

                if (strncmp(opt, "PREV=", 5) == 0)
                    prev = opt + 5;
                else if (strncmp(opt, "NEXT=", 5) == 0)
                    next = opt + 5;
                else if (strcmp(opt, "QMARK") == 0)
                    flags |= GC_PAIRF_QMARK;
                else if (strcmp(opt, "warn") == 0)
                    severity = GC_WARN;
                else if (strcmp(opt, "err") == 0)
                    severity = GC_ERROR;
                else if (strcmp(opt, "info") == 0)
                    severity = GC_INFO;
            }

            if (r2 && *r2 == '#')
                msg = gc_lstrip(r2 + 1);

            if (src && *src && dst && *dst && (prev || next || flags))
                gc_add_pair_ctx(g, src, dst, severity, (msg && *msg) ? msg : NULL, prev, next, flags);
        }
    }

    fclose(fp);

    gc_sort_pairs(g);
    gc_wl_sort(&g->word_caps);
    gc_wl_sort(&g->word_uppers);
    gc_wl_sort(&g->word_lowers);
    gc_wl_sort(&g->agree_exceptions);
    gc_wl_sort(&g->articles_fem);
    gc_wl_sort(&g->articles_masc);
    gc_wl_sort(&g->fem_endings);
    gc_wl_sort(&g->masc_endings);
    gc_wl_sort(&g->masc_exception_endings);

    /* Add rule for PAIR entries to report rule name */
    if (g->n_pairs > 0)
        gc_add_rule(g, GR_PAIR, GC_CAT_TYPO | GC_CAT_STYLE, GC_INFO, NULL, "commonly confused word", "pair.confusion");

    return 0;
}
