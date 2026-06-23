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

/* Parse line "SFX X N" or "SFX X 0 ado [^aeiou]" */
int parse_aff_header(const char *line, int flag_type, ms_cp *flag_out, char *cp_out, int *n_out)
{
    /* Line starts after "SFX " or "PFX " */
    char tmp[64];
    const char *p = line;
    ms_cp f;
    int i = 0;

    /* Skip spaces */
    while (*p == ' ')
        p++;

    /* Flag - according to flag_type */
    if (!*p)
        return -1;

    if (flag_type == 2)
    {
        /* FLAG NUM: number */
        char *end = NULL;

        f = (ms_cp)strtoul(p, &end, 10);

        if (end == p)
            return -1;

        p = end;
    }
    else if (flag_type == 3)
    {
        /* FLAG LONG: pair of ASCII chars, packed as (c0 << 8) | c1 */
        if (!p[0] || !p[1])
            return -1;

        f = ((ms_cp)(unsigned char)p[0] << 8) | (ms_cp)(unsigned char)p[1];
        p += 2;
    }
    else
    {
        /* FLAG ASCII or UTF-8: character/codepoint */
        int adv = utf8_decode(p, &f);
        p += adv;
    }

    *flag_out = f;

    /* Skip spaces */
    while (*p == ' ')
        p++;

    if (!*p)
        return -1;

    /* Cross-product Y/N */
    *cp_out = *p++;

    while (*p == ' ')
        p++;

    /* N_rules */
    while (*p && *p != ' ' && i < (int)sizeof(tmp) - 1)
        tmp[i++] = *p++;

    tmp[i] = '\0';
    *n_out = atoi(tmp);

    return 0;
}

/* Parse "SFX X strip add cond [morph]" => strip, add, cond */
int parse_aff_rule(const char *line, ms_cp expected_flag, int flag_type, char **strip, char **add, char **cond)
{
    const char *p = line;
    char tok[256];
    int i;
    ms_cp f;

    /* Skip spaces */
    while (*p == ' ')
        p++;

    if (!*p)
        return -1;

    /* Flag - according to flag_type */
    if (flag_type == 2)
    {
        /* FLAG NUM: number */
        char *end = NULL;

        f = (ms_cp)strtoul(p, &end, 10);

        if (end == p)
            return -1;

        p = end;
    }
    else if (flag_type == 3)
    {
        /* FLAG LONG: pair of ASCII chars */
        if (!p[0] || !p[1])
            return -1;

        f = ((ms_cp)(unsigned char)p[0] << 8) | (ms_cp)(unsigned char)p[1];
        p += 2;
    }
    else
    {
        /* FLAG ASCII or UTF-8: character/codepoint */
        int adv = utf8_decode(p, &f);

        p += adv;
    }

    if (f != expected_flag)
        return -1;

    while (*p == ' ')
        p++;

    /* Strip */
    i = 0;

    while (*p && *p != ' ' && i < (int)sizeof(tok) - 1)
        tok[i++] = *p++;

    tok[i] = '\0';

    if (strcmp(tok, "0") == 0)
        *strip = ms_strdup("");
    else
        *strip = ms_strdup(tok);

    while (*p == ' ')
        p++;

    /* Add (may include "/flags" - we ignore that part for our model) */
    i = 0;

    while (*p && *p != ' ' && *p != '/' && i < (int)sizeof(tok) - 1)
        tok[i++] = *p++;

    tok[i] = '\0';

    if (strcmp(tok, "0") == 0)
        *add = ms_strdup("");
    else
        *add = ms_strdup(tok);

    /* Skip /flags part if present */
    while (*p && *p != ' ')
        p++;

    while (*p == ' ')
        p++;

    /* Cond */
    i = 0;

    while (*p && *p != ' ' && *p != '\t' && i < (int)sizeof(tok) - 1)
        tok[i++] = *p++;

    tok[i] = '\0';

    if (tok[0])
        *cond = ms_strdup(tok);
    else
        *cond = ms_strdup(".");

    return 0;
}

/* Parse TRY abc... line */
void parse_try(struct spell *m, const char *rest)
{
    free(m->try_chars);
    m->try_chars = ms_strdup(rest);
}

/* Parse "REP N" header (does nothing, dynamic capacity) */
/* Parse "REP from to" line */
void parse_rep(struct spell *m, const char *line)
{
    /* "REP " already removed */
    const char *sp = strchr(line, ' ');
    char from[128];
    size_t flen;
    struct ms_rep *ng = NULL;
    char *from_copy = NULL;
    char *to_copy = NULL;

    if (!sp)
        return;

    flen = (size_t)(sp - line);

    if (flen >= sizeof(from))
        flen = sizeof(from) - 1;

    memcpy(from, line, flen);
    from[flen] = '\0';

    /* Allocate strings first before modifying m->reps */
    from_copy = ms_strdup(from);
    to_copy = ms_strdup(sp + 1);

    if (!from_copy || !to_copy)
    {
        free(from_copy);
        free(to_copy);
        return;
    }

    /* Alloc/append */
    ng = (struct ms_rep *)realloc(m->reps, (size_t)(m->n_reps + 1) * sizeof(*m->reps));

    if (!ng)
    {
        free(from_copy);
        free(to_copy);
        return;
    }

    m->reps = ng;
    m->reps[m->n_reps].from = from_copy;
    m->reps[m->n_reps].to = to_copy;
    m->n_reps++;
}

void parse_map(struct spell *m, const char *line)
{
    char ***ng = NULL;
    char **arr = NULL;
    const char *p = line;
    int cap = 8;
    int n = 0;
    int i;

    /* Build the array first before modifying m->maps */
    arr = (char **)malloc((size_t)cap * sizeof(char *));

    if (!arr)
        return;

    while (*p)
    {
        char buf[8];
        ms_cp cp;
        int adv = utf8_decode(p, &cp);
        char *entry = NULL;

        if (n == cap - 1)
        {
            char **g = (char **)realloc(arr, (size_t)(cap * 2) * sizeof(char *));

            if (!g)
            {
                /* Free all entries allocated so far */
                for (i = 0; i < n; i++)
                    free(arr[i]);

                free(arr);
                return;
            }

            arr = g;
            cap *= 2;
        }

        memcpy(buf, p, (size_t)adv);

        buf[adv] = '\0';
        entry = ms_strdup(buf);

        if (!entry)
        {
            /* Free all entries allocated so far */
            for (i = 0; i < n; i++)
                free(arr[i]);

            free(arr);
            return;
        }

        arr[n++] = entry;
        p += adv;
    }

    arr[n] = NULL;

    /* Now modify m->maps only after arr is fully built */
    ng = (char ***)realloc(m->maps, (size_t)(m->n_maps + 1) * sizeof(char **));

    if (!ng)
    {
        /* Free the array since we can't add it */
        for (i = 0; i < n; i++)
            free(arr[i]);

        free(arr);
        return;
    }

    m->maps = ng;
    m->maps[m->n_maps++] = arr;
}

int load_aff(struct spell *m, const char *path)
{
    FILE *fp = fopen(path, "rb");
    char line[1024];

    if (!fp)
        return -1;

    /* Defaults */
    strcpy(m->encoding, "UTF-8");

    m->flag_utf8 = 0;

    while (ms_readline(fp, line, sizeof(line)) >= 0)
    {
        ms_rstrip(line);

        if (line[0] == '\0' || line[0] == '#')
            continue;

        if (strncmp(line, "SET ", 4) == 0)
        {
            strncpy(m->encoding, line + 4, sizeof(m->encoding) - 1);
            m->encoding[sizeof(m->encoding) - 1] = '\0';
            continue;
        }

        if (strcmp(line, "FLAG UTF-8") == 0)
        {
            m->flag_type = 1;
            m->flag_utf8 = 1;
            continue;
        }

        if (strcmp(line, "FLAG NUM") == 0)
        {
            m->flag_type = 2;
            m->flag_utf8 = 0;
            continue;
        }

        if (strcmp(line, "FLAG LONG") == 0)
        {
            m->flag_type = 3;
            m->flag_utf8 = 0;
            continue;
        }

        if (strncmp(line, "TRY ", 4) == 0)
        {
            parse_try(m, line + 4);
            continue;
        }

        if (strncmp(line, "WORDCHARS ", 10) == 0)
        {
            /* Extra characters that count as part of a word */
            const char *rest = line + 10;

            /* Skip leading spaces */
            while (*rest == ' ')
                rest++;

            if (*rest)
            {
                free(m->wordchars);
                m->wordchars = ms_strdup(rest);
            }

            continue;
        }

        if (strncmp(line, "KEY ", 4) == 0)
        {
            /* Keyboard adjacency layout for suggest sort: KEY row1|row2|row3 */
            const char *rest = line + 4;

            while (*rest == ' ')
                rest++;

            if (*rest)
            {
                free(m->key_layout);
                m->key_layout = ms_strdup(rest);
            }

            continue;
        }

        if (strncmp(line, "BREAK ", 6) == 0)
        {
            /* BREAK table: characters/substrings that split words for checking */
            const char *rest = line + 6;
            char **g = NULL;
            const char *p = NULL;
            int is_header;

            while (*rest == ' ')
                rest++;

            if (!*rest)
                continue;

            /* Skip pure-numeric "BREAK N" header */
            p = rest;
            is_header = 1;

            while (*p)
            {
                if (*p < '0' || *p > '9')
                {
                    is_header = 0;
                    break;
                }

                p++;
            }

            if (is_header)
                continue;

            g = (char **)realloc(m->break_chars, (size_t)(m->n_break + 1) * sizeof(char *));

            if (!g)
                continue;

            m->break_chars = g;
            m->break_chars[m->n_break] = ms_strdup(rest);

            if (m->break_chars[m->n_break])
                m->n_break++;

            continue;
        }

        /* Single-flag declarations: NOSUGGEST, FORBIDDENWORD, KEEPCASE */
        if (strncmp(line, "NOSUGGEST ", 10) == 0 || strncmp(line, "FORBIDDENWORD ", 14) == 0 || strncmp(line, "KEEPCASE ", 9) == 0)
        {
            const char *rest;
            ms_cp *flags;
            int nf = 0;
            ms_cp val = 0;
            int which; /* 0=NOSUGGEST 1=FORBIDDEN 2=KEEPCASE */

            if (line[0] == 'N')
            {
                rest = line + 10;
                which = 0;
            }
            else if (line[0] == 'F')
            {
                rest = line + 14;
                which = 1;
            }
            else
            {
                rest = line + 9;
                which = 2;
            }

            /* parse_flags reads the whole "flag list" but here we expect one */
            flags = parse_flags(rest, m->flag_type, &nf);

            if (flags && nf > 0)
                val = flags[0];

            free(flags);

            if (val != 0)
            {
                if (which == 0)
                    m->flag_nosuggest = val;
                else if (which == 1)
                    m->flag_forbidden = val;
                else
                    m->flag_keepcase = val;
            }

            continue;
        }

        if (strncmp(line, "REP ", 4) == 0)
        {
            /* REP N or REP from to */
            const char *rest = line + 4;
            const char *sp = strchr(rest, ' ');

            if (!sp)
                continue; /* "REP 20" header */

            parse_rep(m, rest);
            continue;
        }

        if (strncmp(line, "MAP ", 4) == 0)
        {
            const char *rest = line + 4;

            /* "MAP N" header se ignora; "MAP letras" es entrada */
            if (rest[0] >= '0' && rest[0] <= '9')
            {
                int n = 0;
                const char *p = rest;

                while (*p && *p != ' ')
                {
                    if (*p < '0' || *p > '9')
                    {
                        n = -1;
                        break;
                    }

                    p++;
                }

                if (n != -1)
                    continue; /* header */
            }

            parse_map(m, rest);

            continue;
        }

        if (strncmp(line, "AF ", 3) == 0)
        {
            /* AF alias table: numeric aliases for flag lists (word/N in .dic) */
            const char *rest = line + 3;
            ms_cp *flags = NULL;
            int n_flags = 0;
            int new_idx;
            ms_cp **g_flags = NULL;
            int *g_n;

            /* Skip "AF N" header */
            if (rest[0] >= '0' && rest[0] <= '9')
            {
                const char *p = rest;
                int is_header = 1;

                while (*p && *p != ' ')
                {
                    if (*p < '0' || *p > '9')
                    {
                        is_header = 0;
                        break;
                    }
                    p++;
                }

                if (is_header)
                    continue;
            }

            flags = parse_flags(rest, m->flag_type, &n_flags);

            new_idx = m->n_af + 1;

            g_flags = (ms_cp **)realloc(m->af_flags, (size_t)(new_idx + 1) * sizeof(ms_cp *));

            if (!g_flags)
            {
                free(flags);
                continue;
            }

            g_n = (int *)realloc(m->af_n_flags, (size_t)(new_idx + 1) * sizeof(int));

            if (!g_n)
            {
                m->af_flags = g_flags; /* keep what we have */
                free(flags);
                continue;
            }

            m->af_flags = g_flags;
            m->af_n_flags = g_n;

            /* Initialize slot 0 the first time */
            if (m->n_af == 0)
            {
                m->af_flags[0] = NULL;
                m->af_n_flags[0] = 0;
            }

            m->af_flags[new_idx] = flags; /* may be NULL if empty */
            m->af_n_flags[new_idx] = n_flags;
            m->n_af = new_idx;

            continue;
        }

        if (strncmp(line, "SFX ", 4) == 0 || strncmp(line, "PFX ", 4) == 0)
        {
            char kind = line[0];
            ms_cp flag;
            char cp_flag;
            int n_rules;
            struct ms_aff_group *ng = NULL;
            struct ms_aff_group *g = NULL;
            int r;

            if (parse_aff_header(line + 4, m->flag_type, &flag, &cp_flag, &n_rules) != 0)
                continue;

            if (n_rules == 0)
                continue; /* no associated rules */

            /* Alloc group */
            ng = (struct ms_aff_group *)realloc(m->groups, (size_t)(m->n_groups + 1) * sizeof(*m->groups));

            if (!ng)
                continue;

            m->groups = ng;
            g = &m->groups[m->n_groups];

            g->flag = flag;
            g->cross_product = cp_flag;

            g->kind = kind;

            g->rules = (struct ms_aff_rule *)calloc((size_t)n_rules, sizeof(*g->rules));
            g->n_rules = 0;

            /* Don't increment n_groups since allocation failed */
            if (!g->rules)
                continue;

            m->n_groups++;

            for (r = 0; r < n_rules; r++)
            {
                char *strip = NULL;
                char *add = NULL;
                char *cond = NULL;
                int got = 0;
                int skipped = 0;

                while (skipped < 512)
                {
                    if (ms_readline(fp, line, sizeof(line)) < 0)
                        break;

                    ms_rstrip(line);
                    skipped++;

                    if (line[0] == '\0' || line[0] == '#')
                        continue;

                    if (strncmp(line, "SFX ", 4) != 0 && strncmp(line, "PFX ", 4) != 0)
                        continue;

                    got = 1;

                    break;
                }

                if (!got)
                    break;

                if (parse_aff_rule(line + 4, flag, m->flag_type, &strip, &add, &cond) == 0 && strip && add && cond)
                {
                    g->rules[g->n_rules].strip = strip;
                    g->rules[g->n_rules].add = add;
                    g->rules[g->n_rules].cond = cond;
                    g->rules[g->n_rules].strip_len = (unsigned short)strlen(strip);
                    g->rules[g->n_rules].add_len = (unsigned short)strlen(add);
                    g->n_rules++;
                }
                else
                {
                    free(strip);
                    free(add);
                    free(cond);
                }
            }

            continue;
        }
    }

    fclose(fp);
    return 0;
}

int load_dic(struct spell *m, const char *path)
{
    FILE *fp = fopen(path, "rb");
    char line[1024];
    char utf8_word[1024];
    int n_expected = 0;

    if (!fp)
        return -1;

    /* Count */
    if (ms_readline(fp, line, sizeof(line)) < 0)
    {
        fclose(fp);
        return -1;
    }

    n_expected = atoi(line);

    if (n_expected <= 0)
        n_expected = 1024;

    if (htab_init(m, n_expected * 2) != 0)
    {
        fclose(fp);
        return -1;
    }

    while (ms_readline(fp, line, sizeof(line)) >= 0)
    {
        char *slash = NULL;
        char *tab = NULL;
        char *word_to_insert = line;

        if (line[0] == '\0' || line[0] == '#')
            continue;

        /* Keep only "word" + "flags" */
        /* Trim morphology after tab/space */
        tab = line;

        while (*tab && *tab != '\t')
            tab++;

        *tab = '\0';

        /* Convert from dictionary encoding to internal UTF-8 if needed */
        if (strcasecmp(m->encoding, "UTF-8") != 0 && strcasecmp(m->encoding, "UTF8") != 0)
        {
            int converted = charset_to_utf8(m->encoding, line, (int)strlen(line), utf8_word, sizeof(utf8_word));

            if (converted > 0)
                word_to_insert = utf8_word;
        }

        slash = strchr(word_to_insert, '/');

        if (slash)
        {
            *slash = '\0';
            htab_insert(m, word_to_insert, slash + 1);
        }
        else
        {
            htab_insert(m, word_to_insert, "");
        }
    }

    fclose(fp);
    return 0;
}

int cond_match(const char *cond, const char *word, int is_suffix)
{
    /* Calculate cond length (in "items": each item is '.', [..], or a UTF-8 char) */
    /* To simplify: we count items and then match */
    int items_count = 0;
    const char *p = NULL;
    int wlen = (int)strlen(word);
    int wpos;

    /* Count items */
    p = cond;

    while (*p)
    {
        if (*p == '[')
        {
            const char *e = strchr(p, ']');

            if (!e)
                return 0;

            p = e + 1;
        }
        else
        {
            ms_cp cp;
            p += utf8_decode(p, &cp);
        }

        items_count++;
    }

    if (items_count == 0)
        return 1; /* "." or empty => match */

    if (items_count > wlen)
        return 0; /* requires more chars than available */

    /* Start position in word */
    if (is_suffix)
    {
        /* Count bytes from end equivalent to 'items_count' UTF-8 chars */
        int bytes = 0;
        int chars = 0;
        int i;

        for (i = wlen - 1; i >= 0 && chars < items_count; i--)
        {
            bytes++;

            if (((unsigned char)word[i] & 0xC0) != 0x80)
                chars++;
        }

        if (chars < items_count)
            return 0;

        wpos = wlen - bytes;
    }
    else
    {
        wpos = 0;
    }

    /* Iterate item by item */
    p = cond;

    while (*p)
    {
        const char *q = NULL;

        if (wpos >= wlen)
            return 0;

        if (*p == '[')
        {
            int negate = 0;
            const char *e = NULL;
            ms_cp wcp;
            int wadv;
            int found = 0;

            p++;

            if (*p == '^')
            {
                negate = 1;
                p++;
            }

            e = strchr(p, ']');

            if (!e)
                return 0;

            wadv = utf8_decode(word + wpos, &wcp);
            q = p;

            while (q < e)
            {
                ms_cp scp;
                int sadv = utf8_decode(q, &scp);

                if (scp == wcp)
                {
                    found = 1;
                    break;
                }

                q += sadv;
            }

            if (negate)
                found = !found;

            if (!found)
                return 0;

            p = e + 1;
            wpos += wadv;
        }
        else if (*p == '.')
        {
            ms_cp wcp;
            int wadv = utf8_decode(word + wpos, &wcp);

            p++;
            wpos += wadv;
        }
        else
        {
            /* Literal */
            ms_cp ccp, wcp;
            int cadv = utf8_decode(p, &ccp);
            int wadv = utf8_decode(word + wpos, &wcp);

            if (ccp != wcp)
                return 0;

            p += cadv;
            wpos += wadv;
        }
    }

    return 1;
}

/* Apply SFX rule in reverse: remove 'add' from end, put 'strip' */
int sfx_unapply(const struct ms_aff_rule *r, const char *word, char *out, size_t outsz)
{
    int wlen = (int)strlen(word);
    int alen = r->add_len;
    int slen = r->strip_len;
    int base_len;

    if (alen == 0 || wlen < alen)
        return 0;

    /* Must end with 'add' */
    if (memcmp(word + wlen - alen, r->add, (size_t)alen) != 0)
        return 0;

    base_len = wlen - alen + slen;

    if ((size_t)base_len + 1 > outsz)
        return 0;

    memcpy(out, word, (size_t)(wlen - alen));

    if (slen)
        memcpy(out + (wlen - alen), r->strip, (size_t)slen);

    out[base_len] = '\0';

    /* Condition evaluated on BASE word (with strip applied) */
    if (!cond_match(r->cond, out, 1))
        return 0;

    return 1;
}

/* Apply PFX rule in reverse */
int pfx_unapply(const struct ms_aff_rule *r, const char *word, char *out, size_t outsz)
{
    int wlen = (int)strlen(word);
    int alen = r->add_len;
    int slen = r->strip_len;
    int base_len;

    if (alen == 0 || wlen < alen)
        return 0;

    if (memcmp(word, r->add, (size_t)alen) != 0)
        return 0;

    base_len = wlen - alen + slen;

    if ((size_t)base_len + 1 > outsz)
        return 0;

    if (slen)
        memcpy(out, r->strip, (size_t)slen);

    memcpy(out + slen, word + alen, (size_t)(wlen - alen));
    out[base_len] = '\0';

    if (!cond_match(r->cond, out, 0))
        return 0;

    return 1;
}

/* Check if 'word' is valid applying SFX/PFX rules in reverse */
int check_with_affixes(struct spell *m, const char *word)
{
    char base[SPELL_MAX_WORD * 2];
    int g;
    int r;
    struct ms_hentry *he;

    /* Try removing a suffix */
    for (g = 0; g < m->n_groups; g++)
    {
        struct ms_aff_group *grp = &m->groups[g];

        if (grp->kind != 'S')
            continue;

        for (r = 0; r < grp->n_rules; r++)
        {
            if (sfx_unapply(&grp->rules[r], word, base, sizeof(base)))
            {
                he = htab_find(m, base);

                if (he && has_flag(he->flags, he->n_flags, grp->flag))
                    return 1;
            }
        }
    }

    /* Try removing a prefix */
    for (g = 0; g < m->n_groups; g++)
    {
        struct ms_aff_group *grp = &m->groups[g];

        if (grp->kind != 'P')
            continue;

        for (r = 0; r < grp->n_rules; r++)
        {
            if (pfx_unapply(&grp->rules[r], word, base, sizeof(base)))
            {
                he = htab_find(m, base);

                if (he && has_flag(he->flags, he->n_flags, grp->flag))
                    return 1;
            }
        }
    }

    /* Try removing prefix + suffix (cross product) */
    for (g = 0; g < m->n_groups; g++)
    {
        struct ms_aff_group *gs = &m->groups[g];
        int g2;
        int r2;
        int r1;

        if (gs->kind != 'S' || gs->cross_product != 'Y')
            continue;

        for (r1 = 0; r1 < gs->n_rules; r1++)
        {
            char mid[SPELL_MAX_WORD * 2];

            if (!sfx_unapply(&gs->rules[r1], word, mid, sizeof(mid)))
                continue;

            for (g2 = 0; g2 < m->n_groups; g2++)
            {
                struct ms_aff_group *gp = &m->groups[g2];

                if (gp->kind != 'P' || gp->cross_product != 'Y')
                    continue;

                for (r2 = 0; r2 < gp->n_rules; r2++)
                {
                    if (pfx_unapply(&gp->rules[r2], mid, base, sizeof(base)))
                    {
                        he = htab_find(m, base);

                        if (he && has_flag(he->flags, he->n_flags, gs->flag) && has_flag(he->flags, he->n_flags, gp->flag))
                            return 1;
                    }
                }
            }
        }
    }

    return 0;
}
