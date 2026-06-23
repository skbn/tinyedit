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

struct spell *spell_new(const char *aff_path, const char *dic_path)
{
    struct spell *m = NULL;

    if (!aff_path || !dic_path)
        return NULL;

    m = (struct spell *)calloc(1, sizeof(*m));

    if (!m)
        return NULL;

    strcpy(m->encoding, "UTF-8");

    /* Initialize prefix buffer fields */
    m->prefix_buf = NULL;
    m->prefix_cap = 0;

    if (load_aff(m, aff_path) != 0)
    {
        spell_free(m);
        return NULL;
    }

    if (load_dic(m, dic_path) != 0)
    {
        spell_free(m);
        return NULL;
    }

    check_cache_init(m);

    return m;
}

void spell_free(struct spell *s)
{
    int i;
    int j;

    if (!s)
        return;

    /* Free sugg buffer */
    for (i = 0; i < SPELL_MAX_SUGGS; i++)
    {
        if (s->sugg_buf[i])
            free(s->sugg_buf[i]);
    }

    /* Free hash table */
    if (s->htab)
    {
        for (i = 0; i < s->htab_size; i++)
        {
            if (s->htab[i].word)
            {
                free(s->htab[i].word);
                free(s->htab[i].flags);
            }
        }

        free(s->htab);
    }

    /* Free aff groups */
    for (i = 0; i < s->n_groups; i++)
    {
        for (j = 0; j < s->groups[i].n_rules; j++)
        {
            free(s->groups[i].rules[j].strip);
            free(s->groups[i].rules[j].add);
            free(s->groups[i].rules[j].cond);
        }

        free(s->groups[i].rules);
    }

    free(s->groups);

    /* Free reps */
    for (i = 0; i < s->n_reps; i++)
    {
        free(s->reps[i].from);
        free(s->reps[i].to);
    }

    free(s->reps);

    /* Free maps */
    for (i = 0; i < s->n_maps; i++)
    {
        if (s->maps[i])
        {
            int k = 0;

            while (s->maps[i][k])
            {
                free(s->maps[i][k]);
                k++;
            }

            free(s->maps[i]);
        }
    }

    free(s->maps);
    free(s->try_chars);
    free(s->wordchars);
    free(s->key_layout);

    if (s->break_chars)
    {
        int bi;

        for (bi = 0; bi < s->n_break; bi++)
            free(s->break_chars[bi]);

        free(s->break_chars);
    }

    /* Free AF alias table (slot 0 reserved, [1..n_af] hold ms_cp arrays) */
    if (s->af_flags)
    {
        int ai;

        for (ai = 1; ai <= s->n_af; ai++)
            free(s->af_flags[ai]);

        free(s->af_flags);
    }

    free(s->af_n_flags);

    /* Free custom dictionary */
    if (s->custom_words)
    {
        int ci;

        for (ci = 0; ci < s->custom_n; ci++)
            free(s->custom_words[ci]);

        free(s->custom_words);
    }

    /* Free ignore list */
    if (s->ignored_words)
    {
        int ci;

        for (ci = 0; ci < s->ignored_n; ci++)
            free(s->ignored_words[ci]);

        free(s->ignored_words);
    }

    /* Free prefix buffer */
    if (s->prefix_buf)
        free(s->prefix_buf);

    free(s);
}

int spell_check(struct spell *s, const char *word)
{
    int idx;
    int converted;
    struct ms_hentry *he = NULL;
    size_t wlen;
    char utf8_word[SPELL_MAX_WORD];
    char normalized[SPELL_MAX_WORD];
    char lower[SPELL_MAX_WORD];
    const char *word_to_check = word;

    if (!s || !word)
        return -1;

    /* Convert from dictionary encoding to UTF-8 if needed */
    if (strcasecmp(s->encoding, "UTF-8") != 0 && strcasecmp(s->encoding, "UTF8") != 0)
    {
        converted = charset_to_utf8(s->encoding, word, (int)strlen(word), utf8_word, sizeof(utf8_word));

        if (converted > 0)
            word_to_check = utf8_word;
    }

    /* Normalize typographic chars to ASCII before lookup */
    if (spell_normalize_chars(word_to_check, normalized, sizeof(normalized)) > 0)
        word_to_check = normalized;

    wlen = strlen(word_to_check);

    if (wlen == 0)
        return 1;

    if (wlen >= SPELL_MAX_WORD)
        return -1;

    /* Cache */
    idx = check_cache_find(s, word_to_check);

    if (idx != -1)
    {
        check_cache_unlink(s, idx);
        check_cache_push_front(s, idx);

        return s->cache[idx].result ? 1 : 0;
    }

    /* Custom user dictionary first */
    if (spell_is_custom_word(s, word_to_check))
    {
        check_cache_put(s, word_to_check, 1);
        return 1;
    }

    /* Session ignore list */
    if (spell_is_ignored(s, word_to_check))
    {
        check_cache_put(s, word_to_check, 1);
        return 1;
    }

    /* Lookup directo */
    he = htab_find(s, word_to_check);

    if (he)
    {
        /* FORBIDDENWORD: explicit blacklist, reject even if direct hit */
        if (he->attrs & MS_ATTR_FORBIDDEN)
        {
            check_cache_put(s, word_to_check, 0);
            return 0;
        }

        check_cache_put(s, word_to_check, 1);

        return 1;
    }

    /* Try lowercase version (handles any Unicode uppercase via utf8_tolower) */
    if (wlen + 1 <= sizeof(lower))
    {
        memcpy(lower, word_to_check, wlen + 1);
        utf8_tolower(lower);

        if (strcmp(lower, word_to_check) != 0)
        {
            he = htab_find(s, lower);

            if (he)
            {
                /* FORBIDDENWORD on lowercase: also reject */
                if (he->attrs & MS_ATTR_FORBIDDEN)
                {
                    check_cache_put(s, word_to_check, 0);
                    return 0;
                }

                /* KEEPCASE: dict entry requires exact case match; reject if user typed mixed case differing from dict */
                if (he->attrs & MS_ATTR_KEEPCASE)
                {
                    check_cache_put(s, word_to_check, 0);
                    return 0;
                }

                check_cache_put(s, word_to_check, 1);

                return 1;
            }

            if (check_with_affixes(s, lower))
            {
                check_cache_put(s, word_to_check, 1);
                return 1;
            }
        }
    }

    /* Try removing affixes */
    if (check_with_affixes(s, word_to_check))
    {
        check_cache_put(s, word_to_check, 1);
        return 1;
    }

    /* BREAK fallback: split word at BREAK chars and check halves */
    if (s->n_break > 0)
    {
        int bi;

        for (bi = 0; bi < s->n_break; bi++)
        {
            const char *brk = s->break_chars[bi];
            const char *pos = NULL;
            size_t bl;
            size_t left_len;
            char left[SPELL_MAX_WORD];
            const char *right = NULL;

            if (!brk || !*brk)
                continue;

            bl = strlen(brk);
            pos = strstr(word_to_check, brk);

            /* Don't split if break is at start/end of word (would leave empty half) */
            if (!pos || pos == word_to_check || !*(pos + bl))
                continue;

            left_len = (size_t)(pos - word_to_check);
            right = pos + bl;

            if (left_len + 1 > sizeof(left))
                continue;

            memcpy(left, word_to_check, left_len);
            left[left_len] = '\0';

            if (spell_check(s, left) == 1 && spell_check(s, right) == 1)
            {
                check_cache_put(s, word_to_check, 1);
                return 1;
            }
        }
    }

    check_cache_put(s, word_to_check, 0);

    return 0;
}
