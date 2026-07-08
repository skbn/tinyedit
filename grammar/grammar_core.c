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

/* Word span table used by the sequence pass */
struct gc_span
{
    unsigned short s, e;
};

#ifndef GC_MAX_SPANS
#ifdef PLATFORM_AMIGA
#define GC_MAX_SPANS 48
#else
#define GC_MAX_SPANS 96
#endif
#endif

int gc_emit(GcIssue *out, int cur, int cap, unsigned short off, unsigned short len, unsigned short rule_id, unsigned char sev, unsigned short cat)
{
    if (!out || cur >= cap)
        return cur;

    out[cur].byte_off = off;
    out[cur].byte_len = len;
    out[cur].rule_id = rule_id;
    out[cur].severity = sev;
    out[cur].category = cat;

    return cur + 1;
}

static int rule_enabled(GramCheck *g, const struct gc_rule *r)
{
    return (g->enabled_mask & (unsigned)r->category) != 0u;
}

static int is_skipped_word(GramCheck *g, const char *lower, size_t len)
{
    int i;

    for (i = 0; i < g->skip.n; i++)
    {
        if (!g->skip.w[i])
            continue;

        if (strlen(g->skip.w[i]) == len && memcmp(g->skip.w[i], lower, len) == 0)
            return 1;
    }

    return 0;
}

/* Compare two byte-ranges case-insensitively. Returns 1 on match */
static int equal_ci(const char *line, size_t a_off, size_t a_len, size_t b_off, size_t b_len)
{
    char la[128];
    char lb[128];
    char aa[128];
    char bb[128];
    size_t na;
    size_t nb;

    if (a_len == 0 || b_len == 0 || a_len >= sizeof(aa) || b_len >= sizeof(bb))
        return 0;

    memcpy(aa, line + a_off, a_len);
    aa[a_len] = '\0';

    memcpy(bb, line + b_off, b_len);
    bb[b_len] = '\0';

    na = (size_t)gc_utf8_lower(aa, la, sizeof(la));
    nb = (size_t)gc_utf8_lower(bb, lb, sizeof(lb));

    if (na != nb)
        return 0;

    return memcmp(la, lb, na) == 0;
}

/* Lowercase copy of a word span into buf. Returns length or 0 */
static size_t span_lower(const char *line, size_t s, size_t e, char *buf, size_t bufsz)
{
    char tmp[128];
    size_t n = e - s;

    if (n == 0 || n >= sizeof(tmp) || n >= bufsz)
        return 0;

    memcpy(tmp, line + s, n);
    tmp[n] = '\0';

    return (size_t)gc_utf8_lower(tmp, buf, bufsz);
}

/* Does lowercase word end with the given suffix? */
static int ends_with(const char *w, size_t wl, const char *suf)
{
    size_t sl = strlen(suf);

    if (wl < sl)
        return 0;

    return memcmp(w + wl - sl, suf, sl) == 0;
}

/* Does word end with any suffix in the list? */
static int ends_with_any(const struct gc_wordlist *wl, const char *w, size_t wlen)
{
    int i;

    if (!wl || !wl->words || wl->n <= 0)
        return 0;

    for (i = 0; i < wl->n; i++)
    {
        if (wl->words[i] && ends_with(w, wlen, wl->words[i]))
            return 1;
    }

    return 0;
}

/* Build spans of letter/digit words. Returns count */
static int build_spans(const char *line, size_t line_len, struct gc_span *sp, int spcap)
{
    int ns = 0;
    size_t p = 0;
    int inw = 0;
    size_t ws = 0;

    while (p < line_len && ns < spcap)
    {
        int u;
        unsigned long c = gc_utf8_decode(line + p, &u);

        if (u <= 0)
            break;

        if (gc_is_letter_cp(c) || (c >= '0' && c <= '9'))
        {
            if (!inw)
            {
                inw = 1;
                ws = p;
            }
        }
        else if (!gc_is_char_quote_cp(c))
        {
            if (inw)
            {
                sp[ns].s = (unsigned short)ws;
                sp[ns].e = (unsigned short)p;

                ns++;
                inw = 0;
            }
        }
        p += (size_t)u;
    }

    if (inw && ns < spcap)
    {
        sp[ns].s = (unsigned short)ws;
        sp[ns].e = (unsigned short)line_len;

        ns++;
    }

    return ns;
}

/* Sequence pass: SPELL_UNKNOWN, WORD_LOWER, AGREEMENT_ART_NOUN, AGREEMENT_NOUN_ADJ, WORD_DENSITY, SUBJUNCTIVE_AFTER, TENSE_MIX. Operates on a prebuilt span table so rules share tokenization */
static int gc_sequence_pass(GramCheck *g, const char *line, size_t line_len, int prev_terminated, GcIssue *out, int n_out, int cap)
{
    struct gc_span sp[GC_MAX_SPANS];
    int ns;
    int i;
    int j;
    char wa[96];
    char wb[96];
    size_t la;
    size_t lb;

    const struct gc_rule *r_spell = NULL;
    const struct gc_rule *r_wlower = NULL;
    const struct gc_rule *r_artnoun = NULL;
    const struct gc_rule *r_noundadj = NULL;
    const struct gc_rule *r_density = NULL;
    const struct gc_rule *r_subj = NULL;
    const struct gc_rule *r_tense = NULL;
    unsigned short id_spell = 0;
    unsigned short id_wlower = 0;
    unsigned short id_artnoun = 0;
    unsigned short id_noundadj = 0;
    unsigned short id_density = 0;
    unsigned short id_subj = 0;
    unsigned short id_tense = 0;

    for (i = 0; i < g->n_rules; i++)
    {
        switch (g->rules[i].kind)
        {
        case GR_SPELL_UNKNOWN:
            r_spell = &g->rules[i];
            id_spell = (unsigned short)i;
            break;
        case GR_WORD_LOWER:
            r_wlower = &g->rules[i];
            id_wlower = (unsigned short)i;
            break;
        case GR_AGREEMENT_ART_NOUN:
            r_artnoun = &g->rules[i];
            id_artnoun = (unsigned short)i;
            break;
        case GR_AGREEMENT_NOUN_ADJ:
            r_noundadj = &g->rules[i];
            id_noundadj = (unsigned short)i;
            break;
        case GR_WORD_DENSITY:
            r_density = &g->rules[i];
            id_density = (unsigned short)i;
            break;
        case GR_SUBJUNCTIVE_AFTER:
            r_subj = &g->rules[i];
            id_subj = (unsigned short)i;
            break;
        case GR_TENSE_MIX:
            r_tense = &g->rules[i];
            id_tense = (unsigned short)i;
            break;
        default:
            break;
        }
    }

    if (!r_spell && !r_wlower && !r_artnoun && !r_noundadj && !r_density && !r_subj && !r_tense)
        return n_out;

    ns = build_spans(line, line_len, sp, GC_MAX_SPANS);

    if (ns <= 0)
        return n_out;

    /* Spelling: host spell callback */
    if (r_spell && rule_enabled(g, r_spell) && g->spell_cb)
    {
        for (i = 0; i < ns && n_out < cap; i++)
        {
            int wl = sp[i].e - sp[i].s;
            int has_letter = 0;
            size_t q = sp[i].s;

            while (q < (size_t)sp[i].e)
            {
                int u;
                unsigned long c = gc_utf8_decode(line + q, &u);

                if (gc_is_letter_cp(c))
                {
                    has_letter = 1;
                    break;
                }

                if (u <= 0)
                    break;

                q += (size_t)u;
            }

            if (!has_letter || wl <= 1)
                continue;

            if (!g->spell_cb(g->spell_ud, line + sp[i].s, wl))
                n_out = gc_emit(out, n_out, cap, sp[i].s, (unsigned short)wl, id_spell, r_spell->severity, r_spell->category);
        }
    }

    /* Uppercase: WORD_LOWER (e.g. months mid-sentence in es) */
    if (r_wlower && rule_enabled(g, r_wlower) && g->word_lowers.n > 0)
    {
        for (i = 0; i < ns && n_out < cap; i++)
        {
            int u;
            unsigned long fcp = gc_utf8_decode(line + sp[i].s, &u);
            size_t b = sp[i].s;
            int after_terminator = 0;

            if (!gc_is_upper_cp(fcp))
                continue;

            while (b > 0)
            {
                char c = line[b - 1];

                if (c == ' ' || c == '\t')
                {
                    b--;
                    continue;
                }

                if (c == '.' || c == '!' || c == '?')
                    after_terminator = 1;
                break;
            }

            /* Skip word-lower words that are at the start of a new sentence. Within the line, after_terminator handles it; across lines, prev_terminated does */
            if (after_terminator || (b == 0 && prev_terminated))
                continue;

            la = span_lower(line, sp[i].s, sp[i].e, wa, sizeof(wa));

            if (la > 0 && gc_wl_contains(&g->word_lowers, wa, la))
                n_out = gc_emit(out, n_out, cap, sp[i].s, (unsigned short)(sp[i].e - sp[i].s), id_wlower, r_wlower->severity, r_wlower->category);
        }
    }

    /* Agreement: article + noun */
    if (r_artnoun && rule_enabled(g, r_artnoun) && (g->articles_fem.n > 0 || g->articles_masc.n > 0))
    {
        for (i = 0; i + 1 < ns && n_out < cap; i++)
        {
            int fem_art;
            int masc_art;

            la = span_lower(line, sp[i].s, sp[i].e, wa, sizeof(wa));

            if (la == 0)
                continue;

            fem_art = gc_wl_contains(&g->articles_fem, wa, la);
            masc_art = gc_wl_contains(&g->articles_masc, wa, la);

            if (!fem_art && !masc_art)
                continue;

            lb = span_lower(line, sp[i + 1].s, sp[i + 1].e, wb, sizeof(wb));

            if (lb < 3)
                continue;

            if (gc_wl_contains(&g->agree_exceptions, wb, lb))
                continue;

            if (fem_art && ends_with_any(&g->masc_endings, wb, lb))
            {
                n_out = gc_emit(out, n_out, cap, sp[i].s, (unsigned short)(sp[i + 1].e - sp[i].s), id_artnoun, r_artnoun->severity, r_artnoun->category);
            }
            else if (masc_art && ends_with_any(&g->fem_endings, wb, lb))
            {
                /* Skip masculine nouns ending in -ma (e.g. problem, language, theme) */
                if (!ends_with_any(&g->masc_exception_endings, wb, lb))
                {
                    n_out = gc_emit(out, n_out, cap, sp[i].s, (unsigned short)(sp[i + 1].e - sp[i].s), id_artnoun, r_artnoun->severity, r_artnoun->category);
                }
            }
        }
    }

    /* Agreement: noun + adjective (heuristic, conservative to minimize false positives): requires article BEFORE that fixes gender, e.g. "la casa blanco" */
    if (r_noundadj && rule_enabled(g, r_noundadj) && (g->articles_fem.n > 0 || g->articles_masc.n > 0))
    {
        for (i = 0; i + 2 < ns && n_out < cap; i++)
        {
            int fem_art;
            int masc_art;
            int n_a;
            int n_o;
            int a_a;
            int a_o;
            char wn[96];
            char wj[96];
            size_t ln2;
            size_t lj;

            la = span_lower(line, sp[i].s, sp[i].e, wa, sizeof(wa));

            if (la == 0)
                continue;

            fem_art = gc_wl_contains(&g->articles_fem, wa, la);
            masc_art = gc_wl_contains(&g->articles_masc, wa, la);

            if (!fem_art && !masc_art)
                continue;

            ln2 = span_lower(line, sp[i + 1].s, sp[i + 1].e, wn, sizeof(wn));
            lj = span_lower(line, sp[i + 2].s, sp[i + 2].e, wj, sizeof(wj));

            if (ln2 < 3 || lj < 4)
                continue;

            if (gc_wl_contains(&g->agree_exceptions, wn, ln2))
                continue;

            if (gc_wl_contains(&g->agree_exceptions, wj, lj))
                continue;

            if (ends_with_any(&g->masc_exception_endings, wn, ln2) || ends_with_any(&g->masc_exception_endings, wj, lj))
                continue;

            n_a = ends_with_any(&g->fem_endings, wn, ln2);
            n_o = ends_with_any(&g->masc_endings, wn, ln2);
            a_a = ends_with_any(&g->fem_endings, wj, lj);
            a_o = ends_with_any(&g->masc_endings, wj, lj);

            /* Article fixes the gender; noun agrees; adjective doesn't */
            if (fem_art && n_a && a_o)
                n_out = gc_emit(out, n_out, cap, sp[i + 1].s, (unsigned short)(sp[i + 2].e - sp[i + 1].s), id_noundadj, r_noundadj->severity, r_noundadj->category);
            else if (masc_art && n_o && a_a)
                n_out = gc_emit(out, n_out, cap, sp[i + 1].s, (unsigned short)(sp[i + 2].e - sp[i + 1].s), id_noundadj, r_noundadj->severity, r_noundadj->category);
        }
    }

    /* Style: WORD_DENSITY */
    if (r_density && rule_enabled(g, r_density))
    {
        int n_rep = r_density->param1 > 0 ? r_density->param1 : 3;
        int w_win = r_density->param2 > 0 ? r_density->param2 : 12;

        for (i = 0; i < ns && n_out < cap; i++)
        {
            int count = 1;

            la = span_lower(line, sp[i].s, sp[i].e, wa, sizeof(wa));

            if (la < 4)
                continue;

            if (is_skipped_word(g, wa, la))
                continue;

            for (j = i + 1; j < ns && j <= i + w_win; j++)
            {
                lb = span_lower(line, sp[j].s, sp[j].e, wb, sizeof(wb));

                if (lb == la && memcmp(wa, wb, la) == 0)
                    count++;
            }
            if (count >= n_rep)
            {
                n_out = gc_emit(out, n_out, cap, sp[i].s, (unsigned short)(sp[i].e - sp[i].s), id_density, r_density->severity, r_density->category);

                i += w_win;
            }
        }
    }

    /* Grammar: SUBJUNCTIVE_AFTER */
    if (r_subj && rule_enabled(g, r_subj) && g->subj_triggers.n > 0 && g->subj_bad_endings.n > 0)
    {
        for (i = 0; i + 1 < ns && n_out < cap; i++)
        {
            int t;

            for (t = 0; t < g->subj_triggers.n; t++)
            {
                const char *trig = g->subj_triggers.words[t];
                size_t tl;
                int words_in_trig = 1;
                const char *q = NULL;
                char phrase[128];
                size_t pl = 0;
                int k;
                int ok = 1;

                if (!trig)
                    continue;

                tl = strlen(trig);

                for (q = trig; *q; q++)
                {
                    if (*q == ' ')
                        words_in_trig++;
                }

                if (i + words_in_trig >= ns)
                    continue;

                for (k = 0; k < words_in_trig; k++)
                {
                    char wtmp[96];
                    size_t wl2 = span_lower(line, sp[i + k].s, sp[i + k].e, wtmp, sizeof(wtmp));

                    if (wl2 == 0 || pl + wl2 + 2 >= sizeof(phrase))
                    {
                        ok = 0;
                        break;
                    }

                    if (k > 0)
                        phrase[pl++] = ' ';

                    memcpy(phrase + pl, wtmp, wl2);

                    pl += wl2;
                }
                phrase[pl] = '\0';

                if (!ok || pl != tl || memcmp(phrase, trig, tl) != 0)
                    continue;

                lb = span_lower(line, sp[i + words_in_trig].s, sp[i + words_in_trig].e, wb, sizeof(wb));

                if (lb >= 3)
                {
                    int be;

                    for (be = 0; be < g->subj_bad_endings.n; be++)
                    {
                        const char *suf = g->subj_bad_endings.words[be];

                        if (suf && ends_with(wb, lb, suf))
                        {
                            n_out = gc_emit(out, n_out, cap, sp[i + words_in_trig].s, (unsigned short)(sp[i + words_in_trig].e - sp[i + words_in_trig].s), id_subj, r_subj->severity, r_subj->category);

                            break;
                        }
                    }
                }

                break;
            }
        }
    }

    /* Grammar: TENSE_MIX (heuristic) */
    if (r_tense && rule_enabled(g, r_tense) && g->tense_past_endings.n > 0 && g->tense_pres_markers.n > 0)
    {
        int past_at = -1;
        int pres_found = 0;

        for (i = 0; i < ns; i++)
        {
            la = span_lower(line, sp[i].s, sp[i].e, wa, sizeof(wa));

            if (la < 3)
                continue;

            if (!pres_found && gc_wl_contains(&g->tense_pres_markers, wa, la))
                pres_found = 1;

            if (past_at < 0)
            {
                int pe;

                for (pe = 0; pe < g->tense_past_endings.n; pe++)
                {
                    const char *suf = g->tense_past_endings.words[pe];

                    if (suf && la > strlen(suf) + 1 && ends_with(wa, la, suf))
                    {
                        past_at = i;
                        break;
                    }
                }
            }

            if (pres_found && past_at >= 0)
                break;
        }

        if (pres_found && past_at >= 0 && n_out < cap)
            n_out = gc_emit(out, n_out, cap, sp[past_at].s, (unsigned short)(sp[past_at].e - sp[past_at].s), id_tense, r_tense->severity, r_tense->category);
    }

    return n_out;
}

int gc_run_checks(GramCheck *g, const char *line, int prev_terminated, GcIssue *out, int cap)
{
    const char *p = NULL;
    size_t line_len;
    size_t off;
    int used;
    unsigned long cp;
    unsigned long prev_cp;
    size_t prev_off;
    int in_word;
    size_t word_start;
    size_t word_end;
    size_t prev_word_start;
    size_t prev_word_end;

    /* Sentence tracker: 1 = next letter should be capital */
    int need_cap;

    /* Bracket stack: chars pushed as codepoints (open); we pair on close */
    unsigned long bstack[GC_MAX_BRACKET_DEPTH];
    int bstack_off[GC_MAX_BRACKET_DEPTH];
    int bdepth;

    /* Output cursor */
    int n_out;

    /* Rule locators (per-kind cache for hot loops) */
    const struct gc_rule *r_no_space_before = NULL;
    const struct gc_rule *r_space_after = NULL;
    const struct gc_rule *r_no_double = NULL;
    const struct gc_rule *r_capitalize = NULL;
    const struct gc_rule *r_repeat = NULL;
    const struct gc_rule *r_bracket = NULL;
    const struct gc_rule *r_multispace = NULL;
    const struct gc_rule *r_fr_nbsp = NULL;
    const struct gc_rule *r_pair = NULL;
    const struct gc_rule *r_no_space_after = NULL;
    const struct gc_rule *r_comma_dot = NULL;
    const struct gc_rule *r_open_es = NULL;
    const struct gc_rule *r_triple_dot = NULL;
    const struct gc_rule *r_double_dash = NULL;
    const struct gc_rule *r_straight_q = NULL;
    const struct gc_rule *r_word_cap = NULL;
    const struct gc_rule *r_word_upper = NULL;
    const struct gc_rule *r_allcaps = NULL;
    const struct gc_rule *r_toolong = NULL;
    const struct gc_rule *r_manycom = NULL;
    const struct gc_rule *r_rep_phrase = NULL;
    const struct gc_rule *r_trail_dots = NULL;
    const struct gc_rule *r_excess = NULL;
    const struct gc_rule *r_nbsp = NULL;
    const struct gc_rule *r_qspace = NULL;

    unsigned short id_no_space_before;
    unsigned short id_space_after;
    unsigned short id_no_double;
    unsigned short id_capitalize;
    unsigned short id_repeat;
    unsigned short id_bracket;
    unsigned short id_multispace;
    unsigned short id_fr_nbsp;
    unsigned short id_pair;
    unsigned short id_no_space_after;
    unsigned short id_comma_dot;
    unsigned short id_open_es;
    unsigned short id_triple_dot;
    unsigned short id_double_dash;
    unsigned short id_straight_q;
    unsigned short id_word_cap;
    unsigned short id_word_upper;
    unsigned short id_allcaps;
    unsigned short id_toolong;
    unsigned short id_manycom;
    unsigned short id_rep_phrase;
    unsigned short id_trail_dots;
    unsigned short id_excess;
    unsigned short id_nbsp;
    unsigned short id_qspace;

    /* Run-length of consecutive ! / ? for EXCESSIVE_PUNCT */
    int punct_run;
    size_t punct_run_start;

    /* Sentence tracking for SENTENCE_OPEN_ES / SENTENCE_TOO_LONG */
    int sentence_word_count;
    size_t sentence_start_off;

    /* Comma-window tracking for TOO_MANY_COMMAS */
    int comma_window_count;
    int comma_word_count;

    int i;

    if (!g || !line || !out || cap <= 0)
        return 0;

    line_len = strlen(line);

    if (line_len == 0)
        return 0;

    id_no_space_before = 0;
    id_space_after = 0;
    id_no_double = 0;
    id_capitalize = 0;
    id_repeat = 0;
    id_bracket = 0;
    id_multispace = 0;
    id_fr_nbsp = 0;
    id_pair = 0;
    id_no_space_after = 0;
    id_comma_dot = 0;
    id_open_es = 0;
    id_triple_dot = 0;
    id_double_dash = 0;
    id_straight_q = 0;
    id_word_cap = 0;
    id_word_upper = 0;
    id_allcaps = 0;
    id_toolong = 0;
    id_manycom = 0;
    id_rep_phrase = 0;
    id_trail_dots = 0;
    id_excess = 0;
    id_nbsp = 0;
    id_qspace = 0;
    punct_run = 0;
    punct_run_start = 0;

    for (i = 0; i < g->n_rules; i++)
    {
        switch (g->rules[i].kind)
        {
        case GR_PUNCT_NO_SPACE_BEFORE:
            r_no_space_before = &g->rules[i];
            id_no_space_before = (unsigned short)i;
            break;
        case GR_PUNCT_SPACE_AFTER:
            r_space_after = &g->rules[i];
            id_space_after = (unsigned short)i;
            break;
        case GR_PUNCT_NO_DOUBLE:
            r_no_double = &g->rules[i];
            id_no_double = (unsigned short)i;
            break;
        case GR_CAPITALIZE_SENTENCE:
            r_capitalize = &g->rules[i];
            id_capitalize = (unsigned short)i;
            break;
        case GR_REPEAT_WORD:
            r_repeat = &g->rules[i];
            id_repeat = (unsigned short)i;
            break;
        case GR_BRACKET_PAIRS:
            r_bracket = &g->rules[i];
            id_bracket = (unsigned short)i;
            break;
        case GR_SPACE_MULTIPLE:
            r_multispace = &g->rules[i];
            id_multispace = (unsigned short)i;
            break;
        case GR_SPACE_BEFORE_QUOTE_FR:
            r_fr_nbsp = &g->rules[i];
            id_fr_nbsp = (unsigned short)i;
            break;
        case GR_PAIR:
            r_pair = &g->rules[i];
            id_pair = (unsigned short)i;
            break;
        case GR_PUNCT_NO_SPACE_AFTER:
            r_no_space_after = &g->rules[i];
            id_no_space_after = (unsigned short)i;
            break;
        case GR_COMMA_BEFORE_DOT:
            r_comma_dot = &g->rules[i];
            id_comma_dot = (unsigned short)i;
            break;
        case GR_SENTENCE_OPEN_ES:
            r_open_es = &g->rules[i];
            id_open_es = (unsigned short)i;
            break;
        case GR_TRIPLE_DOT:
            r_triple_dot = &g->rules[i];
            id_triple_dot = (unsigned short)i;
            break;
        case GR_DOUBLE_DASH:
            r_double_dash = &g->rules[i];
            id_double_dash = (unsigned short)i;
            break;
        case GR_STRAIGHT_QUOTES:
            r_straight_q = &g->rules[i];
            id_straight_q = (unsigned short)i;
            break;
        case GR_WORD_CAP:
            r_word_cap = &g->rules[i];
            id_word_cap = (unsigned short)i;
            break;
        case GR_WORD_UPPER:
            r_word_upper = &g->rules[i];
            id_word_upper = (unsigned short)i;
            break;
        case GR_ALL_CAPS_WORD:
            r_allcaps = &g->rules[i];
            id_allcaps = (unsigned short)i;
            break;
        case GR_SENTENCE_TOO_LONG:
            r_toolong = &g->rules[i];
            id_toolong = (unsigned short)i;
            break;
        case GR_TOO_MANY_COMMAS:
            r_manycom = &g->rules[i];
            id_manycom = (unsigned short)i;
            break;
        case GR_REPEAT_PHRASE:
            r_rep_phrase = &g->rules[i];
            id_rep_phrase = (unsigned short)i;
            break;
        case GR_TRAILING_DOTS:
            r_trail_dots = &g->rules[i];
            id_trail_dots = (unsigned short)i;
            break;
        case GR_EXCESSIVE_PUNCT:
            r_excess = &g->rules[i];
            id_excess = (unsigned short)i;
            break;
        case GR_NBSP_REQUIRED:
            r_nbsp = &g->rules[i];
            id_nbsp = (unsigned short)i;
            break;
        case GR_QUOTE_SPACING:
            r_qspace = &g->rules[i];
            id_qspace = (unsigned short)i;
            break;
        default:
            break;
        }
    }

    sentence_word_count = 0;
    sentence_start_off = 0;
    comma_window_count = 0;
    comma_word_count = 0;

    p = line;
    off = 0;
    prev_cp = 0;
    prev_off = 0;
    in_word = 0;
    word_start = 0;
    word_end = 0;
    prev_word_start = 0;
    prev_word_end = 0;
    need_cap = prev_terminated ? 1 : 0;
    bdepth = 0;
    n_out = 0;

    while (off < line_len)
    {
        size_t this_off;
        this_off = off;
        cp = gc_utf8_decode(p + off, &used);

        if (used <= 0)
            break;

        /* End-of-word transition and word-level checks */
        if (in_word && !gc_is_letter_cp(cp) && !(cp >= '0' && cp <= '9') && !gc_is_char_quote_cp(cp))
        {
            size_t wlen;
            char lower[128];
            char tmp[128];

            word_end = this_off;
            wlen = word_end - word_start;

            /* Lowercase once for reuse across word rules */
            if (wlen > 0 && wlen < sizeof(lower))
            {
                memcpy(tmp, line + word_start, wlen);
                tmp[wlen] = '\0';

                gc_utf8_lower(tmp, lower, sizeof(lower));
            }
            else
            {
                lower[0] = '\0';
            }

            /* PAIR (whole word confusion) */
            if (r_pair && rule_enabled(g, r_pair) && g->pairs && g->n_pairs > 0 && wlen > 0 && lower[0])
            {
                const struct gc_pair *hit = gc_find_pair(g, lower, strlen(lower));

                if (hit)
                    n_out = gc_emit(out, n_out, cap, (unsigned short)word_start, (unsigned short)wlen, id_pair, hit->severity, r_pair->category);
            }

            /* WORD_CAP: this word should start with uppercase (proper nouns, days, months, ...). Match on lowercased form */
            if (r_word_cap && rule_enabled(g, r_word_cap) && wlen > 0 && lower[0])
            {
                if (gc_wl_contains(&g->word_caps, lower, strlen(lower)))
                {
                    /* Flag if first codepoint isn't upper */
                    int u;
                    unsigned long fcp = gc_utf8_decode(line + word_start, &u);

                    if (!gc_is_upper_cp(fcp))
                        n_out = gc_emit(out, n_out, cap, (unsigned short)word_start, (unsigned short)wlen, id_word_cap, r_word_cap->severity, r_word_cap->category);
                }
            }

            /* WORD_UPPER: this word should be all uppercase (siglas) */
            if (r_word_upper && rule_enabled(g, r_word_upper) && wlen > 0 && lower[0])
            {
                if (gc_wl_contains(&g->word_uppers, lower, strlen(lower)))
                {
                    /* Flag if not all upper */
                    int j;
                    int all_up = 1;

                    for (j = 0; (size_t)j < wlen;)
                    {
                        int u2;
                        unsigned long c2 = gc_utf8_decode(line + word_start + j, &u2);

                        if (gc_is_letter_cp(c2) && !gc_is_upper_cp(c2))
                        {
                            all_up = 0;
                            break;
                        }

                        j += u2 > 0 ? u2 : 1;
                    }

                    if (!all_up)
                        n_out = gc_emit(out, n_out, cap, (unsigned short)word_start, (unsigned short)wlen, id_word_upper, r_word_upper->severity, r_word_upper->category);
                }
            }

            /* ALL_CAPS_WORD: word longer than minlen and fully uppercase */
            if (r_allcaps && rule_enabled(g, r_allcaps) && wlen > 0)
            {
                int minlen = r_allcaps->param1 > 0 ? r_allcaps->param1 : 5;
                int nletters = 0, nupper = 0;
                int j;

                for (j = 0; (size_t)j < wlen;)
                {
                    int u2;
                    unsigned long c2 = gc_utf8_decode(line + word_start + j, &u2);

                    if (gc_is_letter_cp(c2))
                    {
                        nletters++;

                        if (gc_is_upper_cp(c2))
                            nupper++;
                    }

                    j += u2 > 0 ? u2 : 1;
                }

                if (nletters >= minlen && nletters == nupper)
                {
                    /* Skip if word is in word_uppers list (known sigla) */
                    if (!gc_wl_contains(&g->word_uppers, lower, strlen(lower)))
                        n_out = gc_emit(out, n_out, cap, (unsigned short)word_start, (unsigned short)wlen, id_allcaps, r_allcaps->severity, r_allcaps->category);
                }
            }

            /* REPEAT_WORD */
            if (r_repeat && rule_enabled(g, r_repeat) && prev_word_end > 0)
            {
                size_t a_len = prev_word_end - prev_word_start;
                size_t b_len = word_end - word_start;

                if (a_len > 0 && b_len > 0)
                {
                    if (equal_ci(line, prev_word_start, a_len, word_start, b_len))
                    {
                        int skipped = 0;

                        if (lower[0] && is_skipped_word(g, lower, strlen(lower)))
                            skipped = 1;

                        if (!skipped)
                            n_out = gc_emit(out, n_out, cap, (unsigned short)word_start, (unsigned short)b_len, id_repeat, r_repeat->severity, r_repeat->category);
                    }
                }
            }

            prev_word_start = word_start;
            prev_word_end = word_end;
            in_word = 0;

            /* Sentence-scope word counter (for SENTENCE_TOO_LONG etc.) */
            sentence_word_count++;
            comma_word_count++;

            /* Slide the comma window */
            if (r_manycom)
            {
                int w_win = r_manycom->param2 > 0 ? r_manycom->param2 : 10;

                if (comma_word_count > w_win)
                {
                    /* Reset window on overflow (sliding window heuristic for comma spike detection) */
                    if (comma_window_count >= (r_manycom->param1 > 0 ? r_manycom->param1 : 4))
                        n_out = gc_emit(out, n_out, cap, (unsigned short)prev_word_start, (unsigned short)(word_end - prev_word_start), id_manycom, r_manycom->severity, r_manycom->category);

                    comma_window_count = 0;
                    comma_word_count = 0;
                }
            }
        }

        /* Start-of-word transition */
        if (!in_word && (gc_is_letter_cp(cp) || (cp >= '0' && cp <= '9')))
        {
            in_word = 1;
            word_start = this_off;

            /* CAPITALIZE_SENTENCE — check first letter of the token */
            if (r_capitalize && rule_enabled(g, r_capitalize) && need_cap && gc_is_letter_cp(cp))
            {
                if (!gc_is_upper_cp(cp))
                    n_out = gc_emit(out, n_out, cap, (unsigned short)word_start, (unsigned short)used, id_capitalize, r_capitalize->severity, r_capitalize->category);

                need_cap = 0;
            }
            else if (r_capitalize && need_cap && !gc_is_letter_cp(cp))
            {
                /* TODO */
            }
            else
            {
                need_cap = 0;
            }
        }

        /* PUNCT_NO_SPACE_BEFORE: current cp is a trigger, prev was space */
        if (r_no_space_before && rule_enabled(g, r_no_space_before) && r_no_space_before->data && gc_charset_contains(r_no_space_before->data, cp))
        {
            /* Report the space, not the punctuation. Byte offset is prev_off */
            if (prev_cp != 0ul && gc_is_space_cp(prev_cp) && prev_cp != 0xA0ul)
                n_out = gc_emit(out, n_out, cap, (unsigned short)prev_off, (unsigned short)(this_off - prev_off), id_no_space_before, r_no_space_before->severity, r_no_space_before->category);
        }

        /* PUNCT_SPACE_AFTER: prev cp was a trigger; current cp is a letter/digit */
        if (r_space_after && rule_enabled(g, r_space_after) && r_space_after->data && gc_charset_contains(r_space_after->data, prev_cp))
        {
            int need_space = 1;

            /* Allow another same-class punct or a space to satisfy */
            if (gc_is_space_cp(cp))
                need_space = 0;

            if (gc_charset_contains(r_space_after->data, cp))
                need_space = 0;

            /* Tolerate numeric contexts like "1,000" or "12:34" by skipping when prev/current are digits */
            if (need_space && (cp >= '0' && cp <= '9'))
            {
                /* Rough heuristic: assume numeric context */
                need_space = 0;
            }

            if (need_space)
                n_out = gc_emit(out, n_out, cap, (unsigned short)prev_off, (unsigned short)(this_off - prev_off), id_space_after, r_space_after->severity, r_space_after->category);
        }

        /* PUNCT_NO_DOUBLE: prev cp equal to cp, both in trigger set */
        if (r_no_double && rule_enabled(g, r_no_double) && r_no_double->data && cp == prev_cp && gc_charset_contains(r_no_double->data, cp))
            n_out = gc_emit(out, n_out, cap, (unsigned short)prev_off, (unsigned short)((this_off + (size_t)used) - prev_off), id_no_double, r_no_double->severity, r_no_double->category);

        /* SPACE_MULTIPLE: ASCII space followed by ASCII space, not at line start */
        if (r_multispace && rule_enabled(g, r_multispace) && cp == 0x20ul && prev_cp == 0x20ul && prev_off > 0)
            n_out = gc_emit(out, n_out, cap, (unsigned short)prev_off, 2, id_multispace, r_multispace->severity, r_multispace->category);

        /* PUNCT_NO_SPACE_AFTER: prev cp is a trigger and current is space. Example: "( foo" -> flag the space after '(' */
        if (r_no_space_after && rule_enabled(g, r_no_space_after) && r_no_space_after->data && gc_is_space_cp(cp) && cp != 0xA0ul && gc_charset_contains(r_no_space_after->data, prev_cp))
            n_out = gc_emit(out, n_out, cap, (unsigned short)this_off, (unsigned short)used, id_no_space_after, r_no_space_after->severity, r_no_space_after->category);

        /* COMMA_BEFORE_DOT: ",." */
        if (r_comma_dot && rule_enabled(g, r_comma_dot) && prev_cp == ',' && cp == '.')
            n_out = gc_emit(out, n_out, cap, (unsigned short)prev_off, (unsigned short)((this_off + (size_t)used) - prev_off), id_comma_dot, r_comma_dot->severity, r_comma_dot->category);

        /* TRIPLE_DOT: three consecutive '.' -> suggest '…' */
        if (r_triple_dot && rule_enabled(g, r_triple_dot) && cp == '.' && prev_cp == '.' && this_off >= 2 && (unsigned char)line[this_off - 2] == '.')
            n_out = gc_emit(out, n_out, cap, (unsigned short)(this_off - 2), 3, id_triple_dot, r_triple_dot->severity, r_triple_dot->category);

        /* DOUBLE_DASH: "--" -> suggest em-dash */
        if (r_double_dash && rule_enabled(g, r_double_dash) && cp == '-' && prev_cp == '-')
            n_out = gc_emit(out, n_out, cap, (unsigned short)prev_off, 2, id_double_dash, r_double_dash->severity, r_double_dash->category);

        /* STRAIGHT_QUOTES: flag ASCII " and ' */
        if (r_straight_q && rule_enabled(g, r_straight_q) && (gc_is_str_quote_cp(cp) || gc_is_char_quote_cp(cp)))
            n_out = gc_emit(out, n_out, cap, (unsigned short)this_off, (unsigned short)used, id_straight_q, r_straight_q->severity, r_straight_q->category);

        /* SENTENCE_OPEN_ES: '?' or '!' without matching opener (¿ / ¡) earlier in sentence. Only fires on first mark of a run */
        if (r_open_es && rule_enabled(g, r_open_es) && r_open_es->data && prev_cp != '?' && prev_cp != '!')
        {
            int trigger = 0;
            unsigned long need_open = 0;

            if (cp == '?' && gc_charset_contains(r_open_es->data, '?'))
            {
                trigger = 1;
                need_open = 0xBFul; /* ¿ */
            }
            else if (cp == '!' && gc_charset_contains(r_open_es->data, '!'))
            {
                trigger = 1;
                need_open = 0xA1ul; /* ¡ */
            }

            if (trigger)
            {
                /* Scan backward from sentence_start_off looking for opener */
                int found = 0;
                size_t s;

                for (s = sentence_start_off; s < this_off;)
                {
                    int su;
                    unsigned long sc = gc_utf8_decode(line + s, &su);

                    if (sc == need_open)
                    {
                        found = 1;
                        break;
                    }

                    if (su <= 0)
                        break;

                    s += (size_t)su;
                }

                if (!found)
                    n_out = gc_emit(out, n_out, cap, (unsigned short)this_off, (unsigned short)used, id_open_es, r_open_es->severity, r_open_es->category);
            }
        }

        /* Track commas for TOO_MANY_COMMAS window */
        if (r_manycom && cp == ',')
            comma_window_count++;

        /* EXCESSIVE_PUNCT: runs of ! and ? (mixed) longer than param1. "!!" is run 2 (OK if param1>=2), "!!!" or "?!?" is run 3+ */
        if (r_excess && rule_enabled(g, r_excess))
        {
            if (cp == '!' || cp == '?')
            {
                if (punct_run == 0)
                    punct_run_start = this_off;

                punct_run++;
            }
            else if (punct_run > 0)
            {
                int maxrun = r_excess->param1 > 0 ? r_excess->param1 : 2;

                if (punct_run > maxrun)
                    n_out = gc_emit(out, n_out, cap, (unsigned short)punct_run_start, (unsigned short)(this_off - punct_run_start), id_excess, r_excess->severity, r_excess->category);

                punct_run = 0;
            }
        }

        /* NBSP_REQUIRED: chars must be preceded by NBSP (0xA0), not ASCII space */
        if (r_nbsp && rule_enabled(g, r_nbsp) && r_nbsp->data && gc_charset_contains(r_nbsp->data, cp) && prev_cp == 0x20ul)
            n_out = gc_emit(out, n_out, cap, (unsigned short)prev_off, (unsigned short)(this_off - prev_off), id_nbsp, r_nbsp->severity, r_nbsp->category);

        /* QUOTE_SPACING: space after opening quote or before closing quote */
        if (r_qspace && rule_enabled(g, r_qspace))
        {
            /* Space right after a quote char */
            if (gc_is_space_cp(cp) && cp != 0xA0ul && gc_is_str_quote_cp(prev_cp))
                n_out = gc_emit(out, n_out, cap, (unsigned short)this_off, (unsigned short)used, id_qspace, r_qspace->severity, r_qspace->category);

            /* Space right before a closing quote char */
            if (gc_is_str_quote_cp(cp) && prev_cp == 0x20ul)
                n_out = gc_emit(out, n_out, cap, (unsigned short)prev_off, (unsigned short)(this_off - prev_off), id_qspace, r_qspace->severity, r_qspace->category);
        }

        /* French/Spanish NBSP-before rule: ; : ! ? preceded by ASCII space */
        if (r_fr_nbsp && rule_enabled(g, r_fr_nbsp))
        {
            if (cp == ';' || cp == ':' || cp == '!' || cp == '?')
            {
                if (prev_cp == 0x20ul)
                    n_out = gc_emit(out, n_out, cap, (unsigned short)prev_off, (unsigned short)(this_off - prev_off), id_fr_nbsp, r_fr_nbsp->severity, r_fr_nbsp->category);
            }
        }

        /* BRACKET_PAIRS: pair chars listed as consecutive open/close */
        if (r_bracket && rule_enabled(g, r_bracket) && r_bracket->data)
        {
            const char *bp = r_bracket->data;
            int used_bp;
            unsigned long open_c;
            unsigned long close_c;

            while (*bp)
            {
                open_c = gc_utf8_decode(bp, &used_bp);

                if (used_bp <= 0)
                    break;

                bp += used_bp;

                if (!*bp)
                    break;

                close_c = gc_utf8_decode(bp, &used_bp);

                if (used_bp <= 0)
                    break;

                bp += used_bp;

                if (cp == open_c)
                {
                    if (bdepth < GC_MAX_BRACKET_DEPTH)
                    {
                        bstack[bdepth] = close_c;
                        bstack_off[bdepth] = (int)this_off;
                        bdepth++;
                    }

                    break;
                }

                if (cp == close_c)
                {
                    if (bdepth == 0 || bstack[bdepth - 1] != close_c)
                        n_out = gc_emit(out, n_out, cap, (unsigned short)this_off, (unsigned short)used, id_bracket, r_bracket->severity, r_bracket->category);
                    else
                        bdepth--;

                    break;
                }
            }
        }

        /* Sentence-boundary tracker */
        if (cp == '.' || cp == '!' || cp == '?')
        {
            /* Next letter should be capital, unless part of an ellipsis */
            if (prev_cp != '.' && prev_cp != '!' && prev_cp != '?')
                need_cap = 1;

            /* SENTENCE_TOO_LONG check on sentence end */
            if (r_toolong && rule_enabled(g, r_toolong))
            {
                int maxw = r_toolong->param1 > 0 ? r_toolong->param1 : 30;

                if (sentence_word_count > maxw)
                    n_out = gc_emit(out, n_out, cap, (unsigned short)sentence_start_off, (unsigned short)((this_off + (size_t)used) - sentence_start_off), id_toolong, r_toolong->severity, r_toolong->category);
            }

            if (prev_cp != '.' && prev_cp != '!' && prev_cp != '?')
            {
                sentence_word_count = 0;
                sentence_start_off = this_off + (size_t)used;
                comma_window_count = 0;
                comma_word_count = 0;
            }
        }

        prev_cp = cp;
        prev_off = this_off;
        off += (size_t)used;
    }

    /* End-of-line word finalisation for PAIR / REPEAT_WORD */
    if (in_word)
    {
        word_end = line_len;

        if (r_pair && rule_enabled(g, r_pair) && g->pairs && g->n_pairs > 0)
        {
            char lower[128];
            size_t wlen = word_end - word_start;

            if (wlen > 0 && wlen < sizeof(lower))
            {
                char tmp[128];
                const struct gc_pair *hit = NULL;

                memcpy(tmp, line + word_start, wlen);
                tmp[wlen] = '\0';

                gc_utf8_lower(tmp, lower, sizeof(lower));

                hit = gc_find_pair(g, lower, strlen(lower));

                if (hit)
                    n_out = gc_emit(out, n_out, cap, (unsigned short)word_start, (unsigned short)wlen, id_pair, hit->severity, r_pair->category);
            }
        }

        if (r_repeat && rule_enabled(g, r_repeat) && prev_word_end > 0)
        {
            size_t a_len = prev_word_end - prev_word_start;
            size_t b_len = word_end - word_start;

            if (a_len > 0 && b_len > 0 && equal_ci(line, prev_word_start, a_len, word_start, b_len))
            {
                char lower[128];
                char tmp[128];
                int skipped = 0;

                if (b_len < sizeof(lower))
                {
                    memcpy(tmp, line + word_start, b_len);
                    tmp[b_len] = '\0';

                    gc_utf8_lower(tmp, lower, sizeof(lower));

                    if (is_skipped_word(g, lower, strlen(lower)))
                        skipped = 1;
                }

                if (!skipped)
                    n_out = gc_emit(out, n_out, cap, (unsigned short)word_start, (unsigned short)b_len, id_repeat, r_repeat->severity, r_repeat->category);
            }
        }
    }

    /* Unclosed brackets at EOL */
    if (r_bracket && rule_enabled(g, r_bracket) && bdepth > 0)
    {
        for (i = 0; i < bdepth && n_out < cap; i++)
            n_out = gc_emit(out, n_out, cap, (unsigned short)bstack_off[i], 1, id_bracket, r_bracket->severity, r_bracket->category);
    }

    /* TRAILING_DOTS: more than one '.' at end, but not exactly 3 (ellipsis) */
    if (r_trail_dots && rule_enabled(g, r_trail_dots) && line_len >= 2)
    {
        size_t end = line_len;
        int ndots = 0;

        while (end > 0 && line[end - 1] == '.')
        {
            end--;
            ndots++;
        }

        if (ndots >= 2 && ndots != 3)
            n_out = gc_emit(out, n_out, cap, (unsigned short)end, (unsigned short)ndots, id_trail_dots, r_trail_dots->severity, r_trail_dots->category);
    }

    /* REPEAT_PHRASE: naive 2-word phrase repeat detector. Split line into word spans, look for adjacent (span[i], span[i+1]) == (span[i+2], span[i+3]) matches */
    if (r_rep_phrase && rule_enabled(g, r_rep_phrase))
    {
        struct
        {
            size_t s;
            size_t e;
        } spans[64];

        int ns = 0;
        size_t p2 = 0;
        int inw = 0;
        size_t wsx = 0;
        int j;

        while (p2 < line_len && ns < 64)
        {
            int uu;
            unsigned long cc = gc_utf8_decode(line + p2, &uu);

            if (uu <= 0)
                break;

            if (gc_is_letter_cp(cc) || (cc >= '0' && cc <= '9'))
            {
                if (!inw)
                {
                    inw = 1;
                    wsx = p2;
                }
            }
            else
            {
                if (inw)
                {
                    spans[ns].s = wsx;
                    spans[ns].e = p2;
                    ns++;
                    inw = 0;
                }
            }

            p2 += (size_t)uu;
        }

        if (inw && ns < 64)
        {
            spans[ns].s = wsx;
            spans[ns].e = line_len;
            ns++;
        }

        for (j = 0; j + 3 < ns; j++)
        {
            if (equal_ci(line, spans[j].s, spans[j].e - spans[j].s, spans[j + 2].s, spans[j + 2].e - spans[j + 2].s) && equal_ci(line, spans[j + 1].s, spans[j + 1].e - spans[j + 1].s, spans[j + 3].s, spans[j + 3].e - spans[j + 3].s))
            {
                n_out = gc_emit(out, n_out, cap, (unsigned short)spans[j + 2].s, (unsigned short)(spans[j + 3].e - spans[j + 2].s), id_rep_phrase, r_rep_phrase->severity, r_rep_phrase->category);
                j += 3; /* Skip past to avoid overlap */
            }
        }
    }

    /* EXCESSIVE_PUNCT: flush run that reaches end of line */
    if (r_excess && rule_enabled(g, r_excess) && punct_run > 0)
    {
        int maxrun = r_excess->param1 > 0 ? r_excess->param1 : 2;

        if (punct_run > maxrun)
            n_out = gc_emit(out, n_out, cap, (unsigned short)punct_run_start, (unsigned short)(line_len - punct_run_start), id_excess, r_excess->severity, r_excess->category);
    }

    /* Word-sequence pass (spell / word_lower / agreement / density / subjunctive / tense). Shares one tokenization for all of them */
    n_out = gc_sequence_pass(g, line, line_len, prev_terminated, out, n_out, cap);

    return n_out;
}
