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

#include "spell_priv.h"

/* Decode word UTF-8 to codepoints array returns length in chars max max_chars */
static int word_to_cps(const char *word, ms_cp *out, int max_chars)
{
    int n = 0;
    const char *p = word;

    while (*p && n < max_chars)
    {
        ms_cp cp;
        int adv = utf8_decode(p, &cp);

        out[n++] = cp;
        p += adv;
    }

    return n;
}

/* Levenshtein distance in codepoints, SPELL_MAX_WORD limits cost */
static int key_neighbors(const char *layout, ms_cp a, ms_cp b)
{
    char ca;
    char cb;
    const char *pa = NULL;
    const char *pb = NULL;
    int diff;

    if (!layout || a > 0x7F || b > 0x7F || a == b)
        return 0;

    ca = (char)a;
    cb = (char)b;

    /* Match case-insensitively: keyboards are case-blind */
    if (ca >= 'A' && ca <= 'Z')
        ca += 32;

    if (cb >= 'A' && cb <= 'Z')
        cb += 32;

    pa = strchr(layout, ca);
    pb = strchr(layout, cb);

    if (!pa || !pb)
        return 0;

    /* They must be at offset exactly 1 with no '|' between them (different rows) */
    diff = (int)(pa < pb ? pb - pa : pa - pb);

    if (diff != 1)
        return 0;

    /* Adjacent in string, but check that neither is a '|' */
    if (*pa == '|' || *pb == '|')
        return 0;

    return 1;
}

static int levenshtein_cp(const ms_cp *a, int la, const ms_cp *b, int lb,
                          const char *key_layout)
{
    int prev[SPELL_MAX_WORD + 1];
    int curr[SPELL_MAX_WORD + 1];
    int i;
    int j;

    if (la > SPELL_MAX_WORD)
        la = SPELL_MAX_WORD;

    if (lb > SPELL_MAX_WORD)
        lb = SPELL_MAX_WORD;

    for (j = 0; j <= lb; j++)
        prev[j] = j * 2; /* base cost = 2 per op so neighbour bonus has room */

    for (i = 1; i <= la; i++)
    {
        curr[0] = i * 2;

        for (j = 1; j <= lb; j++)
        {
            int sub_cost;
            int del = prev[j] + 2;
            int ins = curr[j - 1] + 2;
            int sub;
            int m;

            if (a[i - 1] == b[j - 1])
                sub_cost = 0;
            else if (key_layout && key_neighbors(key_layout, a[i - 1], b[j - 1]))
                sub_cost = 1; /* neighbour: half the cost of a normal sub */
            else
                sub_cost = 2;

            sub = prev[j - 1] + sub_cost;

            m = del < ins ? del : ins;

            if (sub < m)
                m = sub;

            curr[j] = m;
        }

        /* Copy curr -> prev */
        for (j = 0; j <= lb; j++)
            prev[j] = curr[j];
    }

    return prev[lb];
}

/* Sort suggestions by distance to word uses insertion sort n <= SPELL_MAX_SUGGS */
static void sugg_sort_by_distance(struct spell *s, const char *word)
{
    ms_cp word_cps[SPELL_MAX_WORD];
    int word_n;
    int dist[SPELL_MAX_SUGGS];
    int i;
    int j;

    if (s->sugg_n <= 1)
        return;

    word_n = word_to_cps(word, word_cps, SPELL_MAX_WORD);

    /* Calculate distance of each suggestion */
    for (i = 0; i < s->sugg_n; i++)
    {
        ms_cp sug_cps[SPELL_MAX_WORD];
        int sug_n = word_to_cps(s->sugg_buf[i], sug_cps, SPELL_MAX_WORD);

        dist[i] = levenshtein_cp(word_cps, word_n, sug_cps, sug_n, s->key_layout);
    }

    /* Insertion sort by dist[] carrying sugg_buf[] in parallel */
    for (i = 1; i < s->sugg_n; i++)
    {
        int d_i = dist[i];
        char *w_i = s->sugg_buf[i];

        j = i - 1;

        while (j >= 0 && dist[j] > d_i)
        {
            dist[j + 1] = dist[j];
            s->sugg_buf[j + 1] = s->sugg_buf[j];
            j--;
        }

        dist[j + 1] = d_i;
        s->sugg_buf[j + 1] = w_i;
    }
}

static void sugg_clear(struct spell *s)
{
    int i;

    for (i = 0; i < s->sugg_n; i++)
    {
        if (s->sugg_buf[i])
        {
            free(s->sugg_buf[i]);
            s->sugg_buf[i] = NULL;
        }
    }

    s->sugg_n = 0;
}

static int sugg_has(struct spell *s, const char *w)
{
    int i;
    char w_lower[SPELL_MAX_WORD];
    char s_lower[SPELL_MAX_WORD];

    if (!w)
        return 0;

    strcpy(w_lower, w);
    utf8_tolower(w_lower);

    for (i = 0; i < s->sugg_n; i++)
    {
        if (s->sugg_buf[i])
        {
            strcpy(s_lower, s->sugg_buf[i]);
            utf8_tolower(s_lower);

            if (strcmp(s_lower, w_lower) == 0)
                return 1;
        }
    }

    return 0;
}

/* Try 'cand' as suggestion. Returns 1 if added, 0 if not */
int sugg_try(struct spell *s, const char *cand)
{
    if (!cand || !*cand)
        return 0;

    if (s->sugg_n >= SPELL_MAX_SUGGS)
        return 0;

    if (sugg_has(s, cand))
        return 0;

    if (spell_check(s, cand) != 1)
        return 0;

    /* NOSUGGEST: word accepted by spell_check but must not be suggested (profanity, etc) */
    if (s->flag_nosuggest)
    {
        struct ms_hentry *he = htab_find(s, cand);

        if (he && (he->attrs & MS_ATTR_NOSUGGEST))
            return 0;

        /* Also block if the lowercase variant has the flag */
        if (!he)
        {
            char lower[SPELL_MAX_WORD];
            size_t cl = strlen(cand);

            if (cl + 1 <= sizeof(lower))
            {
                memcpy(lower, cand, cl + 1);

                utf8_tolower(lower);

                he = htab_find(s, lower);

                if (he && (he->attrs & MS_ATTR_NOSUGGEST))
                    return 0;
            }
        }
    }

    s->sugg_buf[s->sugg_n] = ms_strdup(cand);

    if (!s->sugg_buf[s->sugg_n])
        return 0;

    s->sugg_n++;

    return 1;
}

/* Generate variants based on REP table */
static void gen_rep(struct spell *s, const char *word)
{
    int i;
    char cand[SPELL_MAX_WORD * 2];

    for (i = 0; i < s->n_reps && s->sugg_n < SPELL_MAX_SUGGS; i++)
    {
        const char *pos = word;
        const char *fnd = NULL;
        size_t flen = strlen(s->reps[i].from);

        if (flen == 0)
            continue;

        while ((fnd = strstr(pos, s->reps[i].from)) != NULL && s->sugg_n < SPELL_MAX_SUGGS)
        {
            size_t prefix = (size_t)(fnd - word);
            size_t tlen = strlen(s->reps[i].to);
            size_t suffix = strlen(fnd + flen);

            if (prefix + tlen + suffix + 1 > sizeof(cand))
                break;

            memcpy(cand, word, prefix);
            memcpy(cand + prefix, s->reps[i].to, tlen);
            memcpy(cand + prefix + tlen, fnd + flen, suffix + 1);

            sugg_try(s, cand);
            pos = fnd + flen;
        }
    }
}

/* Length of UTF-8 character starting at byte i */
static int char_len_at(const char *s, int i)
{
    unsigned char c = (unsigned char)s[i];

    if (c < 0x80)
        return 1;

    if ((c & 0xE0) == 0xC0)
        return 2;

    if ((c & 0xF0) == 0xE0)
        return 3;

    if ((c & 0xF8) == 0xF0)
        return 4;

    return 1;
}

/* Deletechar: delete one character */
static void gen_delete(struct spell *s, const char *word)
{
    char cand[SPELL_MAX_WORD];
    int chars = utf8_strlen(word);
    int i;

    for (i = 0; i < chars && s->sugg_n < SPELL_MAX_SUGGS; i++)
    {
        int b = utf8_offset(word, i);
        int cl = char_len_at(word, b);
        size_t left = (size_t)b;
        size_t right = strlen(word + b + cl);

        if (left + right + 1 > sizeof(cand))
            continue;

        memcpy(cand, word, left);
        memcpy(cand + left, word + b + cl, right + 1);

        sugg_try(s, cand);
    }
}

/* Swap: swap two adjacent characters */
static void gen_swap(struct spell *s, const char *word)
{
    char cand[SPELL_MAX_WORD];
    int chars = utf8_strlen(word);
    int i;

    for (i = 0; i < chars - 1 && s->sugg_n < SPELL_MAX_SUGGS; i++)
    {
        int b1 = utf8_offset(word, i);
        int b2 = utf8_offset(word, i + 1);
        int l1 = char_len_at(word, b1);
        int l2 = char_len_at(word, b2);
        size_t total = strlen(word);

        if (total + 1 > sizeof(cand))
            continue;

        memcpy(cand, word, (size_t)b1);
        memcpy(cand + b1, word + b2, (size_t)l2);
        memcpy(cand + b1 + l2, word + b1, (size_t)l1);
        memcpy(cand + b1 + l2 + l1, word + b2 + l2, total - (size_t)(b2 + l2) + 1);

        sugg_try(s, cand);
    }
}

/* Insertchar: insert each TRY char at each position */
static void gen_insert(struct spell *s, const char *word)
{
    char cand[SPELL_MAX_WORD];
    int chars;
    int i;
    const char *t = NULL;

    if (!s->try_chars)
        return;

    chars = utf8_strlen(word);

    for (i = 0; i <= chars && s->sugg_n < SPELL_MAX_SUGGS; i++)
    {
        int b = utf8_offset(word, i);
        t = s->try_chars;

        while (*t && s->sugg_n < SPELL_MAX_SUGGS)
        {
            ms_cp cp;
            int adv = utf8_decode(t, &cp);
            char ch_buf[8];
            char ref_buf[8];
            size_t total = strlen(word);
            int use_ref = 0;

            if (total + adv + 1 > sizeof(cand))
            {
                t += adv;
                continue;
            }

            /* Determine reference character for case matching */
            if (i == 0 && word[0] != '\0')
            {
                /* Use first character of word as reference */
                int ref_len = char_len_at(word, 0);

                if (ref_len > 0 && ref_len < (int)sizeof(ref_buf))
                {
                    memcpy(ref_buf, word, (size_t)ref_len);

                    ref_buf[ref_len] = '\0';
                    use_ref = 1;
                }
            }
            else if (i > 0)
            {
                /* Use previous character as reference */
                int prev_b = utf8_offset(word, i - 1);
                int prev_len = char_len_at(word, prev_b);

                if (prev_len > 0 && prev_len < (int)sizeof(ref_buf))
                {
                    memcpy(ref_buf, word + prev_b, (size_t)prev_len);

                    ref_buf[prev_len] = '\0';
                    use_ref = 1;
                }
            }

            memcpy(ch_buf, t, (size_t)adv);
            ch_buf[adv] = '\0';

            /* Match case using UTF-8 functions */
            if (use_ref)
            {
                if (!utf8_match_char_case(ref_buf, ch_buf, adv))
                {
                    t += adv;
                    continue; /* Skip: case mismatch */
                }
            }

            memcpy(cand, word, (size_t)b);
            memcpy(cand + b, ch_buf, (size_t)adv);
            memcpy(cand + b + adv, word + b, total - (size_t)b + 1);

            sugg_try(s, cand);

            t += adv;
        }
    }
}

/* Replacechar: replace each char with each TRY char */
static void gen_replace(struct spell *s, const char *word)
{
    char cand[SPELL_MAX_WORD];
    int chars;
    int i;
    const char *t = NULL;

    if (!s->try_chars)
        return;

    chars = utf8_strlen(word);

    for (i = 0; i < chars && s->sugg_n < SPELL_MAX_SUGGS; i++)
    {
        int b = utf8_offset(word, i);
        int cl = char_len_at(word, b);
        char orig_buf[8];

        t = s->try_chars;

        /* Get original character as reference for case matching */
        if (cl > 0 && cl < (int)sizeof(orig_buf))
        {
            memcpy(orig_buf, word + b, (size_t)cl);
            orig_buf[cl] = '\0';
        }
        else
        {
            orig_buf[0] = '\0';
        }

        while (*t && s->sugg_n < SPELL_MAX_SUGGS)
        {
            ms_cp cp;
            int adv = utf8_decode(t, &cp);
            char ch_buf[8];
            size_t total = strlen(word);

            if ((size_t)b + adv + total - (size_t)(b + cl) + 1 > sizeof(cand))
            {
                t += adv;
                continue;
            }

            memcpy(ch_buf, t, (size_t)adv);
            ch_buf[adv] = '\0';

            /* Match case of original character using UTF-8 functions */
            if (orig_buf[0] != '\0')
            {
                if (!utf8_match_char_case(orig_buf, ch_buf, adv))
                {
                    t += adv;
                    continue; /* Skip: case mismatch */
                }
            }

            memcpy(cand, word, (size_t)b);
            memcpy(cand + b, ch_buf, (size_t)adv);
            memcpy(cand + b + adv, word + b + cl, total - (size_t)(b + cl) + 1);

            sugg_try(s, cand);

            t += adv;
        }
    }
}

/* Mapchars: for each char in word, if belongs to MAP group, substitute with each equivalent (useful for accents) */
static void gen_map(struct spell *s, const char *word)
{
    char cand[SPELL_MAX_WORD];
    int chars;
    int i;
    int gi;
    int mi;

    if (!s->maps || s->n_maps == 0)
        return;

    chars = utf8_strlen(word);

    for (i = 0; i < chars && s->sugg_n < SPELL_MAX_SUGGS; i++)
    {
        int b = utf8_offset(word, i);
        int cl = char_len_at(word, b);

        /* Identify if char at position i belongs to any MAP group */
        for (gi = 0; gi < s->n_maps && s->sugg_n < SPELL_MAX_SUGGS; gi++)
        {
            char **group = s->maps[gi];
            int matched = 0;

            if (!group)
                continue;

            /* Current char in this group */
            for (mi = 0; group[mi]; mi++)
            {
                if ((int)strlen(group[mi]) == cl && memcmp(group[mi], word + b, (size_t)cl) == 0)
                {
                    matched = 1;
                    break;
                }
            }

            if (!matched)
                continue;

            /* Try all alternatives of the group */
            for (mi = 0; group[mi] && s->sugg_n < SPELL_MAX_SUGGS; mi++)
            {
                int alt_len = (int)strlen(group[mi]);
                size_t total = strlen(word);

                /* Skip same letter no change */
                if (alt_len == cl && memcmp(group[mi], word + b, (size_t)cl) == 0)
                    continue;

                if ((size_t)b + alt_len + total - (size_t)(b + cl) + 1 > sizeof(cand))
                    continue;

                memcpy(cand, word, (size_t)b);
                memcpy(cand + b, group[mi], (size_t)alt_len);
                memcpy(cand + b + alt_len, word + b + cl, total - (size_t)(b + cl) + 1);

                sugg_try(s, cand);
            }

            break; /* un char solo puede estar en un grupo */
        }
    }
}

/* Count shared n-grams between s1 and s2 (byte-level, works for UTF-8) */
static int ms_ngram(int n, const char *s1, const char *s2, int opt)
{
    int l1 = (int)strlen(s1);
    int l2 = (int)strlen(s2);
    int nscore = 0;
    int j;
    int ns = 0;

    if (l2 == 0)
        return 0;

    for (j = 1; j <= n; j++)
    {
        int ns = 0;
        int i;

        for (i = 0; i <= l1 - j; i++)
        {
            int found = 0;
            int l;

            for (l = 0; l <= l2 - j && !found; l++)
            {
                if (memcmp(s1 + i, s2 + l, (size_t)j) == 0)
                    found = 1;
            }

            if (found)
            {
                ns++;
            }
            else if (opt & MS_NGRAM_WEIGHTED)
            {
                ns--;

                if (i == 0 || i == l1 - j)
                    ns--;
            }
        }

        nscore += ns;

        if (ns < 2 && !(opt & MS_NGRAM_WEIGHTED))
            break;
    }

    if (opt & MS_NGRAM_LONGER_WORSE)
        ns = (l2 - l1) - 2;
    if (opt & MS_NGRAM_ANY_MISMATCH)
        ns = abs(l2 - l1) - 2;

    return nscore - (ns > 0 ? ns : 0);
}

/* Length of shared prefix in UTF-8 codepoints */
static int ms_leftcommon(const char *s1, const char *s2)
{
    int n = 0;
    ms_cp cp1;
    ms_cp cp2;

    while (*s1 && *s2)
    {
        int a1 = utf8_decode(s1, &cp1);
        int a2 = utf8_decode(s2, &cp2);

        if (cp1 != cp2)
            break;

        s1 += a1;
        s2 += a2;
        n++;
    }

    return n;
}

/* Longest common subsequence length (codepoint-level) */
static int ms_lcslen(const char *s1, const char *s2)
{
    ms_cp a[SPELL_MAX_WORD];
    ms_cp b[SPELL_MAX_WORD];
    int dp[SPELL_MAX_WORD + 1];
    int prev[SPELL_MAX_WORD + 1];
    int la = 0;
    int lb = 0;
    int i;
    int j;
    const char *p = NULL;

    p = s1;

    while (*p && la < SPELL_MAX_WORD)
        p += utf8_decode(p, &a[la++]);

    p = s2;

    while (*p && lb < SPELL_MAX_WORD)
        p += utf8_decode(p, &b[lb++]);

    for (j = 0; j <= lb; j++)
        dp[j] = 0;

    for (i = 0; i < la; i++)
    {
        for (j = 0; j <= lb; j++)
            prev[j] = dp[j];

        for (j = 1; j <= lb; j++)
        {
            if (a[i] == b[j - 1])
                dp[j] = prev[j - 1] + 1;
            else if (dp[j - 1] > prev[j])
                dp[j] = dp[j - 1];
            else
                dp[j] = prev[j];
        }
    }

    return dp[lb];
}

/* Ngram suggestion: scan full dictionary, score each entry, keep best */
static void gen_ngram(struct spell *s, const char *word)
{
    struct ms_hentry *roots[MS_MAX_ROOTS];
    int scores[MS_MAX_ROOTS];
    char lower_word[SPELL_MAX_WORD];
    int wlen = (int)strlen(word);
    int i;
    int lp;
    int thresh;
    int sp;

    /* Build lowercase copy of word for scoring */
    if (wlen >= SPELL_MAX_WORD)
        return;

    strcpy(lower_word, word);
    utf8_tolower(lower_word);

    /* Init root heap */
    for (i = 0; i < MS_MAX_ROOTS; i++)
    {
        roots[i] = NULL;
        scores[i] = -100 * i;
    }

    lp = MS_MAX_ROOTS - 1;

    /* Scan htab, score with ngram(3, LONGER_WORSE) + leftcommon */
    for (i = 0; i < s->htab_size; i++)
    {
        struct ms_hentry *he = &s->htab[i];
        char flow[SPELL_MAX_WORD];
        int sc;
        int diff;

        if (!he->word)
            continue;

        diff = (int)strlen(he->word) - wlen;

        if (diff < 0)
            diff = -diff;

        if (diff > 4)
            continue;

        /* Lowercase dict entry for scoring */
        if ((int)sizeof(flow) <= (int)strlen(he->word))
            continue;

        strcpy(flow, he->word);
        utf8_tolower(flow);

        sc = ms_ngram(3, lower_word, flow, MS_NGRAM_LONGER_WORSE) + ms_leftcommon(lower_word, flow);

        if (sc > scores[lp])
        {
            int j;
            scores[lp] = sc;
            roots[lp] = he;
            for (j = 0; j < MS_MAX_ROOTS; j++)
            {
                if (scores[j] < scores[lp])
                    lp = j;
            }
        }
    }

    /* Compute threshold from mangled versions of the word */
    thresh = 0;

    for (sp = 1; sp < 4; sp++)
    {
        char mw[SPELL_MAX_WORD];
        int k;

        strcpy(mw, lower_word);

        for (k = sp; k < wlen; k += 4)
            mw[k] = '*';

        thresh += ms_ngram(wlen, lower_word, mw, MS_NGRAM_ANY_MISMATCH);
    }

    thresh = thresh / 3 - 1;

    /* Re-score roots with ngram(n, ANY_MISMATCH) + lcs reweight */
    for (i = 0; i < MS_MAX_ROOTS; i++)
    {
        char flow[SPELL_MAX_WORD];
        char gl[SPELL_MAX_WORD];
        int sc;
        int lcs;
        int re;

        if (!roots[i])
            continue;

        if ((int)sizeof(flow) <= (int)strlen(roots[i]->word))
            continue;

        strcpy(flow, roots[i]->word);
        utf8_tolower(flow);

        sc = ms_ngram(wlen, lower_word, flow, MS_NGRAM_ANY_MISMATCH) + ms_leftcommon(lower_word, flow);

        if (sc <= thresh)
            continue;

        /* LCS-based re-weighting */
        strcpy(gl, flow);
        lcs = ms_lcslen(lower_word, gl);

        if (wlen == (int)strlen(gl) && wlen == lcs)
        {
            sc += 2000;
        }
        else
        {
            re = ms_ngram(2, lower_word, gl, MS_NGRAM_ANY_MISMATCH | MS_NGRAM_WEIGHTED);
            re += ms_ngram(2, gl, lower_word, MS_NGRAM_ANY_MISMATCH | MS_NGRAM_WEIGHTED);
            sc += re + ms_ngram(4, lower_word, gl, MS_NGRAM_ANY_MISMATCH) + ms_leftcommon(lower_word, gl) + lcs - abs((int)strlen(lower_word) - (int)strlen(gl));
        }

        sugg_try(s, roots[i]->word);
    }
}

char **spell_suggest(struct spell *s, const char *word, int *n_suggestions)
{
    char utf8_word[SPELL_MAX_WORD];
    char encoded[SPELL_MAX_WORD];
    const char *word_to_suggest = word;
    int i;
    int converted;

    if (!s || !word || !n_suggestions)
        return NULL;

    /* Convert from dictionary encoding to UTF-8 if needed */
    if (strcasecmp(s->encoding, "UTF-8") != 0 && strcasecmp(s->encoding, "UTF8") != 0)
    {
        converted = charset_to_utf8(s->encoding, word, (int)strlen(word), utf8_word, sizeof(utf8_word));

        if (converted > 0)
            word_to_suggest = utf8_word;
    }

    sugg_clear(s);

    /* Apply strategies in quality order */
    gen_rep(s, word_to_suggest);
    gen_map(s, word_to_suggest);
    gen_swap(s, word_to_suggest);
    gen_delete(s, word_to_suggest);
    gen_replace(s, word_to_suggest);
    gen_insert(s, word_to_suggest);

    /* Ngram scan: fills remaining slots with similar dictionary words */
    gen_ngram(s, word_to_suggest);

    /* Convert suggestions from UTF-8 to dictionary encoding if needed */
    if (strcasecmp(s->encoding, "UTF-8") != 0 && strcasecmp(s->encoding, "UTF8") != 0)
    {
        for (i = 0; i < s->sugg_n; i++)
        {
            converted = utf8_to_charset(s->encoding, s->sugg_buf[i], (int)strlen(s->sugg_buf[i]), encoded, sizeof(encoded));

            if (converted > 0)
                strcpy(s->sugg_buf[i], encoded);
        }
    }

    /* Sort by Levenshtein distance more similar first */
    sugg_sort_by_distance(s, word_to_suggest);

    *n_suggestions = s->sugg_n;
    return s->sugg_buf;
}

void spell_free_suggestions(struct spell *s, char **suggestions, int n_suggestions)
{
    /* stub */
}
