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

/* Binary search in sorted custom_words returns index or insertion point negative-encoded */
static int custom_bsearch(struct spell *s, const char *word, int *found)
{
    int lo = 0, hi, mid, cmp;

    *found = 0;

    if (s->custom_n == 0)
        return 0;

    hi = s->custom_n - 1;

    while (lo <= hi)
    {
        mid = (lo + hi) >> 1;
        cmp = strcmp(s->custom_words[mid], word);

        if (cmp == 0)
        {
            *found = 1;
            return mid;
        }

        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    return lo; /* insertion point */
}

int spell_is_custom_word(struct spell *s, const char *word)
{
    int found;

    if (!s || !word || s->custom_n == 0)
        return 0;

    custom_bsearch(s, word, &found);
    return found;
}

int spell_add_word(struct spell *s, const char *word)
{
    int pos;
    int found;
    char *copy;
    int i;
    int ci;

    if (!s || !word || !*word)
        return -1;

    if (strlen(word) >= SPELL_MAX_WORD)
        return -1;

    pos = custom_bsearch(s, word, &found);

    if (found)
        return 0; /* already there */

    /* Grow if needed */
    if (s->custom_n == s->custom_cap)
    {
        int new_cap = s->custom_cap ? s->custom_cap * 2 : SPELL_STEP_CUSTOM;
        char **ng = (char **)realloc(s->custom_words, (size_t)new_cap * sizeof(char *));

        if (!ng)
            return -1;

        s->custom_words = ng;
        s->custom_cap = new_cap;
    }

    copy = ms_strdup(word);

    if (!copy)
        return -1;

    /* Shift right and insert at pos */
    for (i = s->custom_n; i > pos; i--)
        s->custom_words[i] = s->custom_words[i - 1];

    s->custom_words[pos] = copy;
    s->custom_n++;

    /* Invalidate any cached negative result for this word */
    ci = check_cache_find(s, word);

    if (ci != -1)
    {
        check_cache_unlink(s, ci);
        s->cache[ci].key[0] = '\0';
        s->cache_count--;
    }

    return 0;
}

int spell_remove_word(struct spell *s, const char *word)
{
    int pos;
    int found;
    int i;
    int ci;

    if (!s || !word)
        return 0;

    pos = custom_bsearch(s, word, &found);

    if (!found)
        return 0;

    free(s->custom_words[pos]);

    for (i = pos; i < s->custom_n - 1; i++)
        s->custom_words[i] = s->custom_words[i + 1];

    s->custom_n--;

    /* Invalidate cached positive result */
    ci = check_cache_find(s, word);

    if (ci != -1)
    {
        check_cache_unlink(s, ci);

        s->cache[ci].key[0] = '\0';
        s->cache_count--;
    }

    return 1;
}

const char *const *spell_custom_words(struct spell *s, int *n_out)
{
    if (n_out)
        *n_out = s ? s->custom_n : 0;

    return s ? (const char *const *)s->custom_words : NULL;
}

void spell_clear_custom_dict(struct spell *s)
{
    int i;

    if (!s)
        return;

    for (i = 0; i < s->custom_n; i++)
        free(s->custom_words[i]);

    free(s->custom_words);

    s->custom_words = NULL;
    s->custom_n = 0;
    s->custom_cap = 0;

    /* Nuke the cache: positive results from custom no longer hold */
    check_cache_init(s);
}

int spell_load_custom_dict(struct spell *s, const char *path)
{
    FILE *fp = NULL;
    char line[SPELL_MAX_WORD + 8];
    int loaded = 0;

    if (!s || !path)
        return -1;

    fp = fopen(path, "rb");

    if (!fp)
        return -1;

    while (ms_readline(fp, line, sizeof(line)) >= 0)
    {
        /* Trim trailing whitespace */
        int n = (int)strlen(line);

        while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t'))
            line[--n] = '\0';

        /* Comments or empty lines */
        if (line[0] == '\0' || line[0] == '#')
            continue;

        if (spell_add_word(s, line) == 0)
            loaded++;
    }

    fclose(fp);
    return loaded;
}

int spell_save_custom_dict(struct spell *s, const char *path)
{
    FILE *fp = NULL;
    int i;

    if (!s || !path)
        return -1;

    fp = fopen(path, "wb");

    if (!fp)
        return -1;

    fprintf(fp, "# Custom dictionary -- one word per line\n");

    for (i = 0; i < s->custom_n; i++)
        fprintf(fp, "%s\n", s->custom_words[i]);

    fclose(fp);
    return 0;
}

int spell_load_custom(struct spell *s, const char *path)
{
    int r = spell_load_custom_dict(s, path);

    /* Return the number of words loaded (consistent with spell_load_custom_dict) */
    /* Legacy comment preserved for reference */
    return r;
}

int spell_add_to_custom_dict(struct spell *s, const char *word, const char *custom_dict_path)
{
    if (spell_add_word(s, word) != 0)
        return -1;

    /* Persist immediately if a path was supplied */
    if (custom_dict_path && *custom_dict_path)
        return spell_save_custom_dict(s, custom_dict_path);

    return 0;
}

static int ignored_bsearch(struct spell *s, const char *word, int *found)
{
    int lo = 0;
    int hi;
    int mid;
    int cmp;

    *found = 0;

    if (s->ignored_n == 0)
        return 0;

    hi = s->ignored_n - 1;

    while (lo <= hi)
    {
        mid = (lo + hi) >> 1;
        cmp = strcmp(s->ignored_words[mid], word);

        if (cmp == 0)
        {
            *found = 1;
            return mid;
        }

        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    return lo;
}

int spell_is_ignored(struct spell *s, const char *word)
{
    int found;

    if (!s || !word || s->ignored_n == 0)
        return 0;

    ignored_bsearch(s, word, &found);

    return found;
}

int spell_ignore_word(struct spell *s, const char *word)
{
    int pos;
    int found;
    int i;
    char *copy;
    int ci;

    if (!s || !word || !*word)
        return -1;

    if (strlen(word) >= SPELL_MAX_WORD)
        return -1;

    pos = ignored_bsearch(s, word, &found);

    if (found)
        return 0;

    if (s->ignored_n == s->ignored_cap)
    {
        int new_cap = s->ignored_cap ? s->ignored_cap * 2 : SPELL_STEP_CUSTOM;
        char **ng = (char **)realloc(s->ignored_words, (size_t)new_cap * sizeof(char *));

        if (!ng)
            return -1;

        s->ignored_words = ng;
        s->ignored_cap = new_cap;
    }

    copy = ms_strdup(word);

    if (!copy)
        return -1;

    for (i = s->ignored_n; i > pos; i--)
        s->ignored_words[i] = s->ignored_words[i - 1];

    s->ignored_words[pos] = copy;
    s->ignored_n++;

    /* Invalidate cached negative result */

    ci = check_cache_find(s, word);

    if (ci != -1)
    {
        check_cache_unlink(s, ci);

        s->cache[ci].key[0] = '\0';
        s->cache_count--;
    }

    return 0;
}

void spell_clear_ignored(struct spell *s)
{
    int i;

    if (!s)
        return;

    for (i = 0; i < s->ignored_n; i++)
        free(s->ignored_words[i]);

    free(s->ignored_words);

    s->ignored_words = NULL;
    s->ignored_n = 0;
    s->ignored_cap = 0;

    check_cache_init(s);
}

/* Qsort comparator for const char ** */
static int prefix_cmp_strs(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;

    return strcmp(sa, sb);
}

int spell_prefix_search(struct spell *s, const char *prefix, const char **out, int out_cap)
{
    size_t plen;
    int n = 0;
    int i;
    int custom_end;
    int lo = 0;
    int hi = 0;
    int mid;
    int cmp;
    int start = 0;

    if (!s || !prefix || !out || out_cap <= 0)
        return 0;

    plen = strlen(prefix);

    if (plen == 0)
        return 0;

    /* Custom dictionary first (sorted -- binary search lower bound) */
    hi = s->custom_n - 1;
    start = s->custom_n; /* Default: nothing matches */

    while (lo <= hi)
    {
        mid = (lo + hi) >> 1;
        cmp = strncmp(s->custom_words[mid], prefix, plen);

        if (cmp >= 0)
        {
            if (cmp == 0)
                start = mid;

            hi = mid - 1;
        }
        else
            lo = mid + 1;
    }

    for (i = start; i < s->custom_n && n < out_cap; i++)
    {
        if (strncmp(s->custom_words[i], prefix, plen) != 0)
            break;

        out[n++] = s->custom_words[i];
    }

    custom_end = n;

    /* Main dictionary (htab - linear scan, but only matching prefix) */
    for (i = 0; i < s->htab_size && n < out_cap; i++)
    {
        const char *w = s->htab[i].word;
        int j;
        int dup = 0;

        if (!w)
            continue;

        if (strncmp(w, prefix, plen) != 0)
            continue;

        /* Avoid dup with custom */
        for (j = 0; j < custom_end; j++)
        {
            if (strcmp(out[j], w) == 0)
            {
                dup = 1;
                break;
            }
        }

        if (dup)
            continue;

        out[n++] = w;
    }

    /* Sort the main-dict slice alphabetically (custom stays at front) */
    if (n > custom_end + 1)
        qsort((void *)(out + custom_end), (size_t)(n - custom_end), sizeof(const char *), prefix_cmp_strs);

    return n;
}
