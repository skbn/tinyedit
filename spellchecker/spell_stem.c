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
#include "dict_scan.h"

static int ends_with_dic(const unsigned char *name)
{
    size_t len;

    if (!name)
        return 0;

    len = strlen((const char *)name);

    if (len < 4)
        return 0;

    return (tolower(name[len - 4]) == '.' &&
            tolower(name[len - 3]) == 'd' &&
            tolower(name[len - 2]) == 'i' &&
            tolower(name[len - 1]) == 'c');
}

static char *extract_dict_name(const unsigned char *name)
{
    size_t len;
    char *base = NULL;

    if (!name)
        return NULL;

    len = strlen((const char *)name);

    if (len < 4)
        return NULL;

    base = (char *)malloc(len - 3);

    if (!base)
        return NULL;

    memcpy(base, name, len - 4);
    base[len - 4] = '\0';

    return base;
}

char **spell_list_dictionaries(const char *dir_path, int *n_dicts)
{
    return dict_scan(dir_path, n_dicts, (dict_filter_fn)ends_with_dic, (dict_xform_fn)extract_dict_name);
}

void spell_free_dictionaries(char **dicts, int n_dicts)
{
    int i;

    if (!dicts)
        return;

    for (i = 0; i < n_dicts; i++)
        free(dicts[i]);

    free(dicts);
}

/* Add unique string to a growing heap array; returns 0 on OOM */
static int stem_add(char ***arr, int *n, int *cap, const char *word)
{
    char **ng = NULL;
    char *copy = NULL;
    int i;

    for (i = 0; i < *n; i++)
    {
        if (strcmp((*arr)[i], word) == 0)
            return 1;
    }

    if (*n == *cap)
    {
        int new_cap = *cap > 0 ? *cap * 2 : 8;

        ng = (char **)realloc(*arr, (size_t)new_cap * sizeof(char *));

        if (!ng)
            return 0;

        *arr = ng;
        *cap = new_cap;
    }

    copy = ms_strdup(word);

    if (!copy)
        return 0;

    (*arr)[(*n)++] = copy;

    return 1;
}

static void stem_free_res(char **res, int n)
{
    int k;

    for (k = 0; k < n; k++)
        free(res[k]);

    free(res);
}

static int stem_collect(struct spell *m, const char *word, char ***out)
{
    char **res = NULL;
    int n = 0;
    int cap = 8;
    char base[SPELL_MAX_WORD * 2];
    char mid[SPELL_MAX_WORD * 2];
    struct ms_hentry *he = NULL;
    int g;
    int g2;
    int r;
    int r2;

    res = (char **)malloc((size_t)cap * sizeof(char *));

    if (!res)
        return -1;

    /* Word already in dictionary: it is its own stem */
    he = htab_find(m, word);

    if (he)
    {
        if (!stem_add(&res, &n, &cap, word))
        {
            free(res);
            return -1;
        }
    }

    /* Suffix strip */
    for (g = 0; g < m->n_groups; g++)
    {
        struct ms_aff_group *grp = &m->groups[g];

        if (grp->kind != 'S')
            continue;

        for (r = 0; r < grp->n_rules; r++)
        {
            if (!sfx_unapply(&grp->rules[r], word, base, sizeof(base)))
                continue;

            he = htab_find(m, base);

            if (he && has_flag(he->flags, he->n_flags, grp->flag))
            {
                if (!stem_add(&res, &n, &cap, base))
                {
                    stem_free_res(res, n);
                    return -1;
                }
            }
        }
    }

    /* Prefix strip */
    for (g = 0; g < m->n_groups; g++)
    {
        struct ms_aff_group *grp = &m->groups[g];

        if (grp->kind != 'P')
            continue;

        for (r = 0; r < grp->n_rules; r++)
        {
            if (!pfx_unapply(&grp->rules[r], word, base, sizeof(base)))
                continue;

            he = htab_find(m, base);

            if (he && has_flag(he->flags, he->n_flags, grp->flag))
            {
                if (!stem_add(&res, &n, &cap, base))
                {
                    stem_free_res(res, n);
                    return -1;
                }
            }
        }
    }

    /* Cross-product: suffix then prefix */
    for (g = 0; g < m->n_groups; g++)
    {
        struct ms_aff_group *gs = &m->groups[g];

        if (gs->kind != 'S' || gs->cross_product != 'Y')
            continue;

        for (r = 0; r < gs->n_rules; r++)
        {
            if (!sfx_unapply(&gs->rules[r], word, mid, sizeof(mid)))
                continue;

            for (g2 = 0; g2 < m->n_groups; g2++)
            {
                struct ms_aff_group *gp = &m->groups[g2];

                if (gp->kind != 'P' || gp->cross_product != 'Y')
                    continue;

                for (r2 = 0; r2 < gp->n_rules; r2++)
                {
                    if (!pfx_unapply(&gp->rules[r2], mid, base, sizeof(base)))
                        continue;

                    he = htab_find(m, base);

                    if (he && has_flag(he->flags, he->n_flags, gs->flag) && has_flag(he->flags, he->n_flags, gp->flag))
                    {
                        if (!stem_add(&res, &n, &cap, base))
                        {
                            stem_free_res(res, n);
                            return -1;
                        }
                    }
                }
            }
        }
    }

    *out = res;
    return n;
}

/* Merge unique strings from res2[n2] into *list[*n], growing as needed */
static void stem_merge(char ***list, int *n, char **res2, int n2)
{
    int i;
    int j;

    for (i = 0; i < n2; i++)
    {
        char **ng = NULL;

        if (!res2[i])
            continue;

        for (j = 0; j < *n; j++)
        {
            if (strcmp((*list)[j], res2[i]) == 0)
                break;
        }

        if (j < *n)
            continue;

        ng = (char **)realloc(*list, (size_t)(*n + 1) * sizeof(char *));

        if (!ng)
            break;

        *list = ng;
        (*list)[(*n)++] = res2[i];

        res2[i] = NULL;
    }
}

int spell_stem(struct spell *s, const char *word, char ***out_list)
{
    char utf8_word[SPELL_MAX_WORD];
    char lower_utf8[SPELL_MAX_WORD];
    char lower_enc[SPELL_MAX_WORD];
    const char *word_utf8;
    const char *word_in_enc;
    int is_utf8;
    int n;
    char **res2 = NULL;
    int n2;
    int i;

    if (out_list)
        *out_list = NULL;

    if (!s || !word || !word[0] || !out_list)
        return -1;

    is_utf8 = (strcasecmp(s->encoding, "UTF-8") == 0 || strcasecmp(s->encoding, "UTF8") == 0);

    /* Normalise input to UTF-8 for case folding */
    if (is_utf8)
    {
        word_utf8 = word;
        word_in_enc = word;
    }
    else
    {
        int r = charset_to_utf8(s->encoding, word, (int)strlen(word), utf8_word, (int)sizeof(utf8_word));

        word_utf8 = (r > 0) ? utf8_word : word;
        word_in_enc = word;
    }

    /* Collect stems for the word as-is */
    n = stem_collect(s, word_in_enc, out_list);

    /* Build lowercase UTF-8 version */
    if ((int)sizeof(lower_utf8) <= (int)strlen(word_utf8))
        return n < 0 ? 0 : n;

    strcpy(lower_utf8, word_utf8);
    utf8_tolower(lower_utf8);

    /* Skip if identical (all lowercase already) */
    if (strcmp(lower_utf8, word_utf8) == 0)
        return n < 0 ? 0 : n;

    /* Convert lowercase back to dictionary encoding if needed */
    if (is_utf8)
    {
        word_in_enc = lower_utf8;
    }
    else
    {
        int r = utf8_to_charset(s->encoding, lower_utf8, (int)strlen(lower_utf8), lower_enc, (int)sizeof(lower_enc));

        word_in_enc = (r > 0) ? lower_enc : lower_utf8;
    }

    n2 = stem_collect(s, word_in_enc, &res2);

    if (n2 <= 0 || !res2)
        return n < 0 ? 0 : n;

    if (n < 0)
    {
        *out_list = res2;
        return n2;
    }

    stem_merge(out_list, &n, res2, n2);

    for (i = 0; i < n2; i++)
        free(res2[i]);

    free(res2);

    return n;
}

int spell_generate(struct spell *s, const char *word, const char *example, char ***out_list)
{
    if (out_list)
        *out_list = NULL;

    return -1;
}

void spell_free_list(struct spell *s, char **list, int n)
{
    int i;

    if (!list)
        return;

    for (i = 0; i < n; i++)
        free(list[i]);

    free(list);
}
