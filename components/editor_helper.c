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

/* editor_helper.c -- Helper functions for undo operations and search */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "editor.h"
#include "../core/utf8.h"
#include "../core/charset.h"

#ifdef HAVE_HYPHEN

#if defined(PLATFORM_AMIGA)
#include "../spellchecker/hyph.h"
#else
#include "../hyph_wrap/hyph_wrap.h"
#endif

#endif

typedef struct
{
    const wchar_t *joined;
    size_t jlen;
    int width;
    int (*hyph_cb)(void *, const wchar_t *, int, int);
    void *hyph_data;
    wchar_t *outw;
    size_t out_cap;
    size_t out_used;
    size_t pos;
    int lines_produced;
    int *wrap_flags;
    int flags_cap;
    int **out_wrap_flags;
} HwrapSplitCtx;

/* Convert entire line to wchar_t string (caller frees) */
wchar_t *line_to_wcs(EdLine *ln)
{
    wchar_t *result = NULL;
    int i;

    if (!ln)
        return NULL;

    result = (wchar_t *)malloc((ln->len + 1) * sizeof(wchar_t));

    if (!result)
        return NULL;

    for (i = 0; i < ln->len; i++)
        result[i] = (wchar_t)ed_line_char(ln, i);

    result[ln->len] = L'\0';

    return result;
}

/* Convert line range to wchar_t string (caller frees) */
wchar_t *line_to_wcs_range(EdLine *ln, int start, int end)
{
    int len;
    int i;
    wchar_t *result = NULL;

    if (!ln || start < 0 || end > ln->len || start >= end)
        return NULL;

    len = end - start;
    result = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));

    if (!result)
        return NULL;

    for (i = 0; i < len; i++)
        result[i] = (wchar_t)ed_line_char(ln, start + i);

    result[len] = L'\0';
    return result;
}

/* Convert block to UTF-8 string (caller frees) */
char *ed_block_to_string(Ed *ed, int r1, int c1, int r2, int c2)
{
    char *result = NULL;
    char *temp = NULL;
    size_t total_len = 0;
    int i;
    size_t pos;

    if (!ed || r1 < 0 || r2 >= ed->count || r1 > r2)
        return NULL;

    /* Calculate total length needed */
    for (i = r1; i <= r2; i++)
    {
        EdLine *ln = ed->lines[i];
        int start = (i == r1) ? c1 : 0;
        int end = (i == r2) ? c2 : ln->len;

        if (start < end)
        {
            wchar_t *segment = line_to_wcs_range(ln, start, end);

            if (segment)
            {
                char *utf8_seg = wcs_to_utf8(segment, end - start);

                if (utf8_seg)
                {
                    total_len += strlen(utf8_seg);

                    free(utf8_seg);
                }

                free(segment);
            }
        }

        /* Add newline for all but last line */
        if (i < r2)
            total_len += 1;
    }

    /* Allocate result */
    result = (char *)malloc(total_len + 1);

    if (!result)
        return NULL;

    result[0] = '\0';

    /* Build result with memcpy to avoid O(n^2) strcat */
    pos = 0;

    for (i = r1; i <= r2; i++)
    {
        EdLine *ln = ed->lines[i];
        int start = (i == r1) ? c1 : 0;
        int end = (i == r2) ? c2 : ln->len;

        if (start < end)
        {
            wchar_t *segment = line_to_wcs_range(ln, start, end);

            if (segment)
            {
                temp = wcs_to_utf8(segment, end - start);

                if (temp)
                {
                    size_t n = strlen(temp);

                    /* Clamp to buffer overflow if estimate was too low */
                    if (pos + n > total_len)
                        n = total_len - pos;

                    if (n > 0)
                    {
                        memcpy(result + pos, temp, n);
                        pos += n;
                    }

                    free(temp);
                }

                free(segment);
            }
        }

        /* Add newline for all but last line */
        if (i < r2 && pos < total_len)
            result[pos++] = '\n';
    }

    result[pos] = '\0';

    return result;
}

int ed_search_forward(Ed *ed, const wchar_t *needle)
{
    int start_row;
    int start_col;
    int row;
    int col;
    int nlen;
    int i, found, k;
    const wchar_t *line = NULL;

    if (!ed || !needle || !needle[0] || ed->count <= 0)
        return 0;

    nlen = (int)wcslen(needle);
    start_row = ed->row;
    start_col = ed->col + 1;

    for (i = 0; i < ed->count + 1; i++)
    {
        row = (start_row + i) % ed->count;
        line = ed_line_wcs(ed, row);
        col = (i == 0) ? start_col : 0;

        if (col > ed->lines[row]->len)
            col = ed->lines[row]->len;

        for (; col + nlen <= ed->lines[row]->len; col++)
        {
            found = 1;

            for (k = 0; k < nlen; k++)
            {
                wchar_t a = line[col + k];
                wchar_t b = needle[k];

                a = towlower(a);
                b = towlower(b);

                if (a != b)
                {
                    found = 0;
                    break;
                }
            }

            if (found)
            {
                ed->row = row;
                ed->col = col;

                ed_clamp(ed);
                ed_ensure_visible(ed);

                return 1;
            }
        }
    }

    return 0;
}

int ed_search_all(Ed *ed, const wchar_t *needle, int **out_rows, int **out_cols)
{
    int count = 0;
    int i, j;
    int nlen;
    const wchar_t *line = NULL;
    int line_len;
    int *rows = NULL;
    int *cols = NULL;

    if (!ed || !needle || !needle[0] || !out_rows || !out_cols)
        return 0;

    if (ed->count <= 0)
        return 0;

    nlen = (int)wcslen(needle);

    /* First pass: count matches */
    for (i = 0; i < ed->count; i++)
    {
        line = ed_line_wcs(ed, i);
        line_len = ed->lines[i]->len;

        /* Search for needle in this line */
        for (j = 0; j + nlen <= line_len; j++)
        {
            int k;
            int match = 1;

            for (k = 0; k < nlen; k++)
            {
                wchar_t a = line[j + k];
                wchar_t b = needle[k];

                /* Case-insensitive comparison */
                a = towlower(a);
                b = towlower(b);

                if (a != b)
                {
                    match = 0;
                    break;
                }
            }

            if (match)
            {
                count++;
            }
        }
    }

    if (count == 0)
    {
        *out_rows = NULL;
        *out_cols = NULL;
        return 0;
    }

    /* Allocate arrays for matches */
    rows = (int *)malloc((size_t)count * sizeof(int));
    cols = (int *)malloc((size_t)count * sizeof(int));

    if (!rows || !cols)
    {
        if (rows)
            free(rows);

        if (cols)
            free(cols);

        *out_rows = NULL;
        *out_cols = NULL;

        return 0;
    }

    /* Second pass: fill arrays */
    count = 0;
    for (i = 0; i < ed->count; i++)
    {
        line = ed_line_wcs(ed, i);
        line_len = ed->lines[i]->len;

        /* Search for needle in this line */
        for (j = 0; j + nlen <= line_len; j++)
        {
            int k;
            int match = 1;

            for (k = 0; k < nlen; k++)
            {
                wchar_t a = line[j + k];
                wchar_t b = needle[k];

                /* Case-insensitive comparison */
                a = towlower(a);
                b = towlower(b);

                if (a != b)
                {
                    match = 0;
                    break;
                }
            }

            if (match)
            {
                rows[count] = i;
                cols[count] = j;
                count++;
            }
        }
    }

    *out_rows = rows;
    *out_cols = cols;

    return count;
}

/* Custom search with case_sensitive and whole_word support */
int ed_search_all_custom(Ed *ed, const wchar_t *needle, int case_sensitive, int whole_word, int **out_rows, int **out_cols)
{
    int count = 0;
    int capacity = 1024;
    int i, j;
    int needle_len;

    if (!needle || !needle[0])
        return 0;

    needle_len = (int)wcslen(needle);

    *out_rows = malloc(capacity * sizeof(int));
    *out_cols = malloc(capacity * sizeof(int));

    if (!*out_rows || !*out_cols)
    {
        free(*out_rows);
        free(*out_cols);
        *out_rows = *out_cols = NULL;

        return 0;
    }

    for (i = 0; i < ed->count; i++)
    {
        const wchar_t *line = ed_line_wcs(ed, i);
        int line_len = ed->lines[i]->len;

        for (j = 0; j <= line_len - needle_len; j++)
        {
            int match = 1;
            int k;

            /* Check if needle matches at this position */
            for (k = 0; k < needle_len; k++)
            {
                wchar_t c1 = line[j + k];
                wchar_t c2 = needle[k];

                if (!case_sensitive)
                {
                    c1 = towlower(c1);
                    c2 = towlower(c2);
                }

                if (c1 != c2)
                {
                    match = 0;
                    break;
                }
            }

            if (match)
            {
                /* Check whole word boundary */
                if (whole_word)
                {
                    wchar_t prev_char = (j > 0) ? line[j - 1] : 0;
                    wchar_t next_char = (j + needle_len < line_len) ? line[j + needle_len] : 0;

                    /* Require word boundaries on both sides */
                    if ((prev_char != 0 && !iswspace(prev_char) && !iswpunct(prev_char)) || (next_char != 0 && !iswspace(next_char) && !iswpunct(next_char)))
                    {
                        match = 0;
                    }
                }

                if (match)
                {
                    /* Add match to results */
                    if (count >= capacity)
                    {
                        int *new_rows = NULL;
                        int *new_cols = NULL;

                        capacity *= 2;
                        new_rows = realloc(*out_rows, capacity * sizeof(int));

                        if (!new_rows)
                        {
                            free(*out_rows);
                            free(*out_cols);

                            *out_rows = NULL;
                            *out_cols = NULL;
                            return 0;
                        }

                        *out_rows = new_rows;

                        new_cols = realloc(*out_cols, capacity * sizeof(int));

                        if (!new_cols)
                        {
                            free(*out_rows);
                            free(*out_cols);

                            *out_rows = NULL;
                            *out_cols = NULL;
                            return 0;
                        }

                        *out_cols = new_cols;
                    }

                    (*out_rows)[count] = i;
                    (*out_cols)[count] = j;

                    count++;
                }
            }
        }
    }

    return count;
}

/* Detect leading quote prefix length (returns 0 if not a quote) */
int ed_detect_quote_prefix(const wchar_t *line)
{
    int p = 0;

    if (!line)
        return 0;

    while (p < 8 && line[p] == L' ')
        p++;

    while (p < 8 && line[p] && line[p] != L'>' && line[p] != L'<')
        p++;

    if (line[p] == L'>' && line[p + 1] == L' ')
        return p + 2;

    return 0;
}

/* Find bounds of a contiguous quote block with the same prefix */
static void ftn_reply_bounds(Ed *ed, int row, const wchar_t *prefix, int prefix_len, int *out_first, int *out_last)
{
    int first = row;
    int last = row;

    while (first > 0)
    {
        const wchar_t *l = ed_line_wcs(ed, first - 1);

        if (!l || !l[0] || wcsncmp(l, prefix, (size_t)prefix_len) != 0)
            break;

        first--;
    }

    while (last < ed->count - 1)
    {
        const wchar_t *l = ed_line_wcs(ed, last + 1);

        if (!l || !l[0] || wcsncmp(l, prefix, (size_t)prefix_len) != 0)
            break;

        last++;
    }

    *out_first = first;
    *out_last = last;
}

/* True if the trailing '-' is a wrap-inserted hyphen */
static int ftn_is_wrap_hyphen(const wchar_t *line, int len, int prefix_len)
{
    wchar_t before;

    if (len - prefix_len < 2)
        return 0;

    if (line[len - 1] != L'-')
        return 0;

    before = line[len - 2];

    if (before == L' ' || before == L'\t')
        return 0;

    return 1;
}

/* Join a quote block into one normalized word string */
static wchar_t *ftn_join_reply_block(Ed *ed, int first, int last, int prefix_len, size_t *out_len)
{
    size_t cap = 256;
    size_t used = 0;
    wchar_t *joined = (wchar_t *)malloc(cap * sizeof(wchar_t));
    int i;
    int need_space = 0;

    if (!joined)
        return NULL;

    for (i = first; i <= last; i++)
    {
        wchar_t *l = line_to_wcs(ed->lines[i]);
        int ll = ed->lines[i]->len;
        int skip = prefix_len;
        int j;
        int has_content = 0;

        if (!l)
        {
            free(joined);
            return NULL;
        }

        if (skip > ll)
            skip = ll;

        if (used > 0 && i > first)
        {
            wchar_t *prev = line_to_wcs(ed->lines[i - 1]);
            int is_hyph = prev ? ftn_is_wrap_hyphen(prev, ed->lines[i - 1]->len, prefix_len) : 0;

            free(prev);

            if (is_hyph)
            {
                if (joined[used - 1] == L'-')
                    used--;

                need_space = 0;
            }
        }

        for (j = skip; j < ll; j++)
        {
            wchar_t ch = l[j];

            if (ch == L' ' || ch == L'\t')
            {
                if (used > 0 && joined[used - 1] != L' ')
                    need_space = 1;
            }
            else
            {
                if (need_space)
                {
                    if (used + 1 >= cap)
                    {
                        cap = (cap + 64) * 2;
                        wchar_t *tmp = (wchar_t *)realloc(joined, cap * sizeof(wchar_t));

                        if (!tmp)
                        {
                            free(l);
                            free(joined);
                            return NULL;
                        }

                        joined = tmp;
                    }

                    joined[used++] = L' ';
                    need_space = 0;
                }

                if (used + 1 >= cap)
                {
                    cap = (cap + 64) * 2;
                    wchar_t *tmp = (wchar_t *)realloc(joined, cap * sizeof(wchar_t));

                    if (!tmp)
                    {
                        free(l);
                        free(joined);
                        return NULL;
                    }

                    joined = tmp;
                }

                joined[used++] = ch;
                has_content = 1;
            }
        }

        free(l);

        if (has_content)
            need_space = 1;
    }

    if (used > 0 && joined[used - 1] == L' ')
        used--;

    joined[used] = L'\0';
    *out_len = used;

    return joined;
}

/* Find the best break point for the next line and return the emitted char count */
static int ftn_find_break(const wchar_t *joined, size_t len, size_t pos, int avail)
{
    size_t rem = len - pos;
    int k;
    int break_at;

    if (rem <= (size_t)avail)
        return (int)rem;

    break_at = -1;

    for (k = avail; k > 0; k--)
    {
        if (joined[pos + k - 1] == L' ')
        {
            break_at = k;
            break;
        }
    }

    if (break_at > 0)
    {
        while (break_at > 0 && joined[pos + break_at - 1] == L' ')
            break_at--;

        if (break_at <= 0)
            break_at = 1;

        return break_at;
    }

    return avail;
}

/* Split the normalized joined text into wrapped lines with prefix */
static wchar_t *ftn_split_reply_lines(const wchar_t *joined, size_t joined_len, const wchar_t *prefix, int prefix_len, int width, size_t *out_len)
{
    int avail = width - prefix_len;
    size_t out_cap = joined_len * 2 + 64;
    size_t out_used = 0;
    wchar_t *outw = (wchar_t *)malloc(out_cap * sizeof(wchar_t));
    size_t pos = 0;
    int k;
    wchar_t *tmp = NULL;

    if (!outw)
        return NULL;

    if (avail < 10)
        avail = 10;

    while (pos < joined_len)
    {
        int break_at;

        if (out_used + (size_t)prefix_len + (size_t)avail + 4 >= out_cap)
        {
            out_cap = (out_cap + (size_t)avail + 64) * 2;
            tmp = (wchar_t *)realloc(outw, out_cap * sizeof(wchar_t));

            if (!tmp)
            {
                free(outw);
                return NULL;
            }

            outw = tmp;
        }

        for (k = 0; k < prefix_len; k++)
            outw[out_used++] = prefix[k];

        break_at = ftn_find_break(joined, joined_len, pos, avail);

        for (k = 0; k < break_at; k++)
            outw[out_used++] = joined[pos + k];

        while (out_used > 0 && outw[out_used - 1] == L' ')
            out_used--;

        pos += (size_t)break_at;

        while (pos < joined_len && joined[pos] == L' ')
            pos++;

        if (pos < joined_len)
            outw[out_used++] = L'\n';
    }

    outw[out_used] = L'\0';
    *out_len = out_used;
    return outw;
}

/* Join a plain paragraph into one word stream and record cursor offset */
static wchar_t *hwrap_join_para(Ed *ed, int first, int last, int cursor_row, int cursor_col, int *out_cursor_offset, size_t *out_len)
{
    size_t cap = 256;
    size_t used = 0;
    wchar_t *joined = (wchar_t *)malloc(cap * sizeof(wchar_t));
    int logical_pos = 0; /* Logical chars before the current point (no wrap hyphens) */
    int cursor_offset = -1;
    int prev_had_break = 0; /* Previous line had a wrap hyphen we removed */
    int i;
    wchar_t *tmp = NULL;

    if (!joined)
        return NULL;

    for (i = first; i <= last; i++)
    {
        wchar_t *l = line_to_wcs(ed->lines[i]);
        int ll = ed->lines[i]->len;
        int has_wrap = ed->lines[i]->has_wrap_hyphen;
        int skip_hyphen = -1;
        int skip_space = -1;
        int logical_len;
        int had_break = 0;
        int j;

        if (!l)
        {
            free(joined);
            return NULL;
        }

        logical_len = ll;

        /* Remove wrap hyphen and following space to rejoin word */
        if (has_wrap)
        {
            for (j = ll - 1; j >= 0; j--)
            {
                if (l[j] == L'-')
                {
                    skip_hyphen = j;
                    logical_len--;
                    had_break = 1;

                    if (j + 1 < ll && (l[j + 1] == L' ' || l[j + 1] == L'\t'))
                    {
                        skip_space = j + 1;
                        logical_len--;
                    }

                    break;
                }
            }
        }

        if (i > first)
        {
            if (prev_had_break)
            {
                /* Remove the trailing '-' that was kept in the joined stream */
                if (used > 0 && joined[used - 1] == L'-')
                    used--;
            }
            else
            {
                /* Add a joining space if neither side already has one */
                if (used > 0 && joined[used - 1] != L' ' && joined[used - 1] != L'\t' && l[0] != L' ' && l[0] != L'\t')
                {
                    if (used + 1 >= cap)
                    {
                        cap = (cap + 64) * 2;
                        tmp = (wchar_t *)realloc(joined, cap * sizeof(wchar_t));

                        if (!tmp)
                        {
                            free(l);
                            free(joined);
                            return NULL;
                        }

                        joined = tmp;
                    }

                    joined[used++] = L' ';
                    logical_pos++; /* Joining space is a real logical char */
                }
            }
        }

        /* Record logical cursor offset before the line's chars are copied */
        if (i == cursor_row)
        {
            if (cursor_col <= 0)
            {
                cursor_offset = logical_pos;
            }
            else
            {
                int logical_up_to_col = 0;

                for (j = 0; j < cursor_col && j < ll; j++)
                {
                    if (j == skip_hyphen || j == skip_space)
                        continue;

                    logical_up_to_col++;
                }

                cursor_offset = logical_pos + logical_up_to_col;
            }
        }

        /* Copy the line, skipping the removed hyphen and its following space */
        for (j = 0; j < ll; j++)
        {
            if (j == skip_hyphen || j == skip_space)
                continue;

            if (used + 1 >= cap)
            {
                cap = (cap + 64) * 2;
                tmp = (wchar_t *)realloc(joined, cap * sizeof(wchar_t));

                if (!tmp)
                {
                    free(l);
                    free(joined);
                    return NULL;
                }

                joined = tmp;
            }

            joined[used++] = l[j];
        }

        logical_pos += logical_len;

        free(l);

        prev_had_break = had_break;
    }

    joined[used] = L'\0';

    *out_len = used;
    *out_cursor_offset = cursor_offset;

    return joined;
}

/* Split joined text into hard-wrapped lines and optionally return wrap flags */
static int hwrap_grow_out(HwrapSplitCtx *ctx, size_t extra)
{
    size_t new_cap;
    wchar_t *tmp = NULL;

    if (ctx->out_used + extra < ctx->out_cap)
        return 0;

    new_cap = (ctx->out_cap + extra + 64) * 2;
    tmp = (wchar_t *)realloc(ctx->outw, new_cap * sizeof(wchar_t));

    if (!tmp)
        return -1;

    ctx->outw = tmp;
    ctx->out_cap = new_cap;
    return 0;
}

static int hwrap_grow_flags(HwrapSplitCtx *ctx)
{
    int new_cap;
    int *tmp = NULL;

    if (!ctx->out_wrap_flags || ctx->lines_produced < ctx->flags_cap)
        return 0;

    new_cap = ctx->flags_cap ? ctx->flags_cap * 2 : 8;
    tmp = (int *)realloc(ctx->wrap_flags, (size_t)new_cap * sizeof(int));

    if (!tmp)
        return -1;

    ctx->wrap_flags = tmp;
    ctx->flags_cap = new_cap;
    return 0;
}

static int hwrap_try_hyphenation(HwrapSplitCtx *ctx)
{
    size_t pos = ctx->pos;
    int avail = ctx->width;
    int word_start = avail - 1;
    int word_end;
    int wlen;
    int core_len;
    int col_limit;
    int bp;
    int emit;
    int k;

    if (!ctx->hyph_cb)
        return 0;

    /* Step back to find start of the overflowing word */
    while (word_start > 0 && ctx->joined[pos + word_start - 1] != L' ')
        word_start--;

    /* Find end of that word */
    word_end = word_start;

    while ((int)pos + word_end < (int)ctx->jlen && ctx->joined[pos + word_end] != L' ')
        word_end++;

    wlen = word_end - word_start;

    if (wlen < 4 || wlen >= 512)
        return 0;

    /* Hyphenate only if word is mid-line and trailing space does not fit */
    if (word_start + wlen <= avail)
    {
        if (word_start == 0)
            return 0;

        if (word_end < (int)ctx->jlen && ctx->joined[pos + word_end] == L' ' && word_end + 1 <= avail)
            return 0;
    }

    /* Strip trailing punctuation before hyphenation */
    while (word_end > word_start)
    {
        wchar_t c = ctx->joined[pos + word_end - 1];

        if (c == L'-' || !iswpunct(c))
            break;

        word_end--;
    }

    core_len = word_end - word_start;

    if (core_len < 4)
        return 0;

    col_limit = avail - word_start - 1;

    bp = ctx->hyph_cb(ctx->hyph_data, &ctx->joined[pos + word_start], core_len, col_limit);

    /* Need 2+ chars before and after break */
    if (bp < 2 || bp > core_len - 2)
        return 0;

    emit = word_start + bp;

    if (hwrap_grow_out(ctx, (size_t)emit + 2) != 0)
        return -1;

    if (hwrap_grow_flags(ctx) != 0)
        return -1;

    for (k = 0; k < emit; k++)
        ctx->outw[ctx->out_used++] = ctx->joined[pos + k];

    ctx->outw[ctx->out_used++] = L'-';
    ctx->outw[ctx->out_used++] = L'\n';

    if (ctx->out_wrap_flags)
        ctx->wrap_flags[ctx->lines_produced] = 1;

    ctx->lines_produced++;
    ctx->pos = pos + (size_t)emit;

    /* Skip spaces at start of next line */
    while (ctx->pos < ctx->jlen && ctx->joined[ctx->pos] == L' ')
        ctx->pos++;

    return 1;
}

static int hwrap_try_space_break(HwrapSplitCtx *ctx)
{
    size_t pos = ctx->pos;
    int avail = ctx->width;
    int break_at = -1;
    int k;

    for (k = avail; k > 0; k--)
    {
        if (ctx->joined[pos + k - 1] == L' ')
        {
            break_at = k - 1;
            break;
        }
    }

    if (break_at <= 0)
        return 0;

    if (hwrap_grow_out(ctx, (size_t)break_at + 1) != 0)
        return -1;

    if (hwrap_grow_flags(ctx) != 0)
        return -1;

    for (k = 0; k < break_at; k++)
        ctx->outw[ctx->out_used++] = ctx->joined[pos + k];

    /* Trim trailing spaces */
    while (ctx->out_used > 0 && ctx->outw[ctx->out_used - 1] == L' ')
        ctx->out_used--;

    ctx->outw[ctx->out_used++] = L'\n';
    ctx->pos = pos + (size_t)break_at + 1;

    while (ctx->pos < ctx->jlen && ctx->joined[ctx->pos] == L' ')
        ctx->pos++;

    if (ctx->out_wrap_flags)
        ctx->wrap_flags[ctx->lines_produced] = 0;

    ctx->lines_produced++;

    return 1;
}

static int hwrap_try_hard_cut(HwrapSplitCtx *ctx)
{
    size_t pos = ctx->pos;
    int avail = ctx->width;
    int word_len = 0;
    int k;

    while (pos + word_len < ctx->jlen && ctx->joined[pos + word_len] != L' ')
        word_len++;

    if (hwrap_grow_flags(ctx) != 0)
        return -1;

    if (word_len > 0 && word_len <= ctx->width)
    {
        /* If we are at the very start of the output or of a fresh line, emit the word directly rather than creating an empty line */
        int at_line_start = (ctx->out_used == 0 || ctx->outw[ctx->out_used - 1] == L'\n');

        if (at_line_start)
        {
            /* Emit the word on this line, then newline */
            int k;

            if (hwrap_grow_out(ctx, (size_t)word_len + 1) != 0)
                return -1;

            for (k = 0; k < word_len; k++)
                ctx->outw[ctx->out_used++] = ctx->joined[pos + k];

            ctx->outw[ctx->out_used++] = L'\n';
            ctx->pos = pos + (size_t)word_len;

            /* Skip trailing spaces */
            while (ctx->pos < ctx->jlen && ctx->joined[ctx->pos] == L' ')
                ctx->pos++;
        }
        else
        {
            /* Move the whole word to the next line */
            if (hwrap_grow_out(ctx, 1) != 0)
                return -1;

            ctx->outw[ctx->out_used++] = L'\n';
            ctx->pos = pos + (size_t)word_len;
        }
    }
    else
    {
        /* Word is longer than line width: hard cut at avail */
        if (hwrap_grow_out(ctx, (size_t)avail + 1) != 0)
            return -1;

        for (k = 0; k < avail && (int)pos + k < (int)ctx->jlen; k++)
            ctx->outw[ctx->out_used++] = ctx->joined[pos + k];

        ctx->outw[ctx->out_used++] = L'\n';
        ctx->pos = pos + (size_t)avail;
    }

    if (ctx->out_wrap_flags)
        ctx->wrap_flags[ctx->lines_produced] = 0;

    ctx->lines_produced++;

    return 1;
}

static wchar_t *hwrap_split_fail(HwrapSplitCtx *ctx)
{
    free(ctx->outw);
    free(ctx->wrap_flags);
    return NULL;
}

static wchar_t *hwrap_split(const wchar_t *joined, size_t jlen, int width, int (*hyph_cb)(void *, const wchar_t *, int, int), void *hyph_data, int *out_lines, int **out_wrap_flags)
{
    HwrapSplitCtx ctx;
    size_t n;
    int k;

    ctx.joined = joined;
    ctx.jlen = jlen;
    ctx.width = width < 4 ? 4 : width;
    ctx.hyph_cb = hyph_cb;
    ctx.hyph_data = hyph_data;
    ctx.out_cap = jlen * 2 + 64;
    ctx.outw = (wchar_t *)malloc(ctx.out_cap * sizeof(wchar_t));
    ctx.out_used = 0;
    ctx.pos = 0;
    ctx.lines_produced = 0;
    ctx.wrap_flags = NULL;
    ctx.flags_cap = 0;
    ctx.out_wrap_flags = out_wrap_flags;

    if (!ctx.outw)
        return NULL;

    if (out_wrap_flags)
        *out_wrap_flags = NULL;

    while (ctx.pos < jlen)
    {
        size_t rem = jlen - ctx.pos;
        int avail = ctx.width;
        int rc;

        /* Ensure output buffer has room for one full line + '\n' */
        if (hwrap_grow_out(&ctx, (size_t)avail + 4) != 0)
            return hwrap_split_fail(&ctx);

        /* Ensure wrap_flags has room for one more line */
        if (hwrap_grow_flags(&ctx) != 0)
            return hwrap_split_fail(&ctx);

        if ((int)rem <= avail)
        {
            /* Rest fits: copy everything and done */
            n = rem;

            for (k = 0; k < (int)n; k++)
                ctx.outw[ctx.out_used++] = joined[ctx.pos + k];

            if (out_wrap_flags)
                ctx.wrap_flags[ctx.lines_produced] = 0;

            ctx.lines_produced++;
            break;
        }

        rc = hwrap_try_hyphenation(&ctx);

        if (rc < 0)
            return hwrap_split_fail(&ctx);

        if (rc > 0)
            continue;

        rc = hwrap_try_space_break(&ctx);

        if (rc < 0)
            return hwrap_split_fail(&ctx);

        if (rc > 0)
            continue;

        if (hwrap_try_hard_cut(&ctx) < 0)
            return hwrap_split_fail(&ctx);
    }

    if (ctx.out_used < ctx.out_cap)
        ctx.outw[ctx.out_used] = L'\0';
    else
        ctx.outw[ctx.out_cap - 1] = L'\0';

    *out_lines = ctx.lines_produced;

    if (out_wrap_flags)
        *out_wrap_flags = ctx.wrap_flags;
    else
        free(ctx.wrap_flags);

    return ctx.outw;
}

/* Map cursor offset in the joined paragraph to the wrapped (row, col) */
static void hwrap_find_cursor(Ed *ed, int first, int inserted, int cursor_offset, int *out_row, int *out_col)
{
    int i;
    int pos = 0;
    int last_ins;

    for (i = first; i < first + inserted && i < ed->count; i++)
    {
        wchar_t *l = line_to_wcs(ed->lines[i]);
        int ll = ed->lines[i]->len;
        int has_wrap = ed->lines[i]->has_wrap_hyphen;
        int logical_len = ll;

        if (!l)
        {
            *out_row = i;
            *out_col = 0;
            return;
        }

        /* The trailing wrap hyphen is not a logical character */
        if (has_wrap && ll > 0 && l[ll - 1] == L'-')
            logical_len--;

        /* Cursor at the start of this line */
        if (pos >= cursor_offset)
        {
            free(l);

            *out_row = i;
            *out_col = 0;
            return;
        }

        if (i > first)
        {
            /* Add a joining space unless the previous line ended with a wrap hyphen or either side has whitespace at the boundary */
            wchar_t *prev = line_to_wcs(ed->lines[i - 1]);
            int prev_len = ed->lines[i - 1]->len;
            int prev_has_wrap = ed->lines[i - 1]->has_wrap_hyphen;
            int prev_last = prev_len > 0 ? prev[prev_len - 1] : 0;
            int needs_space = 0;

            if (prev && pos > 0)
            {
                if (!(prev_has_wrap && prev_last == L'-') && prev_last != L' ' && prev_last != L'\t' && l[0] != L' ' && l[0] != L'\t')
                    needs_space = 1;
            }

            free(prev);

            if (needs_space)
            {
                pos++;

                if (pos >= cursor_offset)
                {
                    free(l);

                    *out_row = i;
                    *out_col = 0;
                    return;
                }
            }
        }

        /* Cursor inside this line */
        if (cursor_offset > pos && cursor_offset <= pos + logical_len)
        {
            *out_row = i;
            *out_col = cursor_offset - pos;

            free(l);
            return;
        }

        pos += logical_len;

        free(l);
    }

    /* Cursor offset past end: place at end of last inserted line */
    last_ins = first + inserted - 1;

    if (last_ins >= ed->count)
        last_ins = ed->count - 1;

    *out_row = last_ins;
    *out_col = ed->lines[last_ins]->len;
}

/* Reflow the hard-wrap paragraph around the cursor */
int ed_rewrap_paragraph_ex(Ed *ed, int width, int (*hyph_cb)(void *, const wchar_t *, int, int), void *hyph_data)
{
    int first, last;
    int old_count, new_count;
    int replace_count;
    int inserted;
    int cur_row_before, cur_col_before;
    int cur_row_after, cur_col_after;
    int cursor_offset;
    size_t joined_len = 0;
    wchar_t *joined = NULL;
    wchar_t *outw = NULL;
    char *outu = NULL;
    char *snapshot_before = NULL;
    char *snapshot_after = NULL;
    int *wrap_flags = NULL;
    int out_lines = 0;
    size_t outw_len;
    int fi;

    if (!ed || width < 4 || ed->count <= 0)
        return -1;

    /* Empty line: paragraph separator -- nothing to reflow */
    if (ed->lines[ed->row]->len == 0)
        return -1;

    /* Find paragraph boundaries (contiguous non-empty lines) */
    first = ed->row;

    while (first > 0 && ed->lines[first - 1]->len > 0)
        first--;

    last = ed->row;

    while (last < ed->count - 1 && ed->lines[last + 1]->len > 0)
        last++;

    replace_count = last - first + 1;

    cur_row_before = ed->row;
    cur_col_before = ed->col;

    /* Use pre-edit snapshot if available to restore undo state */
    if (ed->auto_rewrap_pre_snapshot && ed->auto_rewrap_pre_start == first)
    {
        const char *p = ed->auto_rewrap_pre_snapshot;
        int pre_count = 0;

        while (*p)
        {
            if (*p == '\n')
                pre_count++;

            p++;
        }

        snapshot_before = ed->auto_rewrap_pre_snapshot;
        ed->auto_rewrap_pre_snapshot = NULL;

        old_count = pre_count;

        cur_row_before = ed->auto_rewrap_pre_cursor_row;
        cur_col_before = ed->auto_rewrap_pre_cursor_col;
    }
    else
    {
        free(ed->auto_rewrap_pre_snapshot);

        ed->auto_rewrap_pre_snapshot = NULL;

        snapshot_before = ed_range_to_string(ed, first, last + 1);

        if (!snapshot_before)
            return -1;

        old_count = last - first + 1;
        cur_row_before = ed->row;
        cur_col_before = ed->col;
    }

    /* Join the paragraph into one stream and remember the cursor offset */
    joined = hwrap_join_para(ed, first, last, ed->row, ed->col, &cursor_offset, &joined_len);

    if (!joined)
    {
        free(snapshot_before);
        return -1;
    }

    if (cursor_offset < 0)
        cursor_offset = (int)joined_len;

    if (!ed->undo_snapshot_mode)
        ed->undo_snapshot_mode = 1;

    /* Re-split the joined stream into wrapped lines */
    outw = hwrap_split(joined, joined_len, width, hyph_cb, hyph_data, &out_lines, &wrap_flags);

    free(joined);

    if (!outw)
    {
        ed->undo_snapshot_mode = 0;

        free(snapshot_before);
        free(wrap_flags);
        return -1;
    }

    /* Convert to UTF-8 and replace the range */
    outw_len = wcslen(outw);
    outu = wcs_to_utf8(outw, (int)outw_len);

    free(outw);

    if (!outu)
    {
        ed->undo_snapshot_mode = 0;

        free(snapshot_before);
        free(wrap_flags);
        return -1;
    }

    inserted = ed_replace_range_from_utf8(ed, first, replace_count, outu);

    free(outu);

    if (inserted <= 0)
    {
        ed->undo_snapshot_mode = 0;

        free(snapshot_before);
        free(wrap_flags);
        return -1;
    }

    /* Mark wrap-hyphens on the newly inserted lines */
    if (wrap_flags)
    {
        for (fi = 0; fi < inserted && fi < out_lines; fi++)
        {
            if (first + fi < ed->count)
                ed->lines[first + fi]->has_wrap_hyphen = wrap_flags[fi];
        }

        free(wrap_flags);
        wrap_flags = NULL;
    }

    /* Restore the cursor position from the same offset */
    hwrap_find_cursor(ed, first, inserted, cursor_offset, &cur_row_after, &cur_col_after);

    ed->row = cur_row_after;
    ed->col = cur_col_after;
    ed->modified = 1;

    ed_prefix_invalidate_from(ed, first);

    ed->undo_snapshot_mode = 0;

    /* Build after-snapshot and push undo record */
    new_count = inserted;

    if (new_count < 0)
        new_count = 0;

    snapshot_after = ed_range_to_string(ed, first, first + new_count);

    if (!snapshot_after)
    {
        free(snapshot_before);
        return -1;
    }

    ed_redo_clear(ed);

    if (ed_undo_open_group(ed) != 0)
    {
        free(snapshot_before);
        free(snapshot_after);
        return -1;
    }

    if (undo_push_snapshot_range(ed, first, cur_col_before, snapshot_before, snapshot_after, old_count, new_count, cur_row_before, cur_col_before, cur_row_after, cur_col_after) != 0)
        return -1;

    ed->undo_open = 0;
    ed_prefix_invalidate(ed);

    return 0;
}

/* Reflow the current FTN reply quote block */
int ed_rewrap_ftn_reply(Ed *ed, int width)
{
    const wchar_t *line;
    int prefix_len;
    wchar_t prefix[16];
    int first = 0;
    int last = 0;
    int old_count = 0;
    int new_count = 0;
    int doc_count_before = 0;
    int inserted = 0;
    int cursor_row_before = 0;
    int cursor_col_before = 0;
    int cursor_row_after = 0;
    int cursor_col_after = 0;
    int last_line_empty = 0;
    int last_line_len = 0;
    char *snapshot_before = NULL;
    char *snapshot_after = NULL;
    wchar_t *joined = NULL;
    size_t joined_len = 0;
    wchar_t *outw = NULL;
    size_t out_len = 0;
    char *outu = NULL;

    if (!ed || width < 10 || ed->count <= 0)
        return -1;

    line = ed_line_wcs(ed, ed->row);

    if (!line)
        return -1;

    prefix_len = ed_detect_quote_prefix(line);

    if (prefix_len <= 0)
        return -1;

    if (prefix_len >= (int)(sizeof(prefix) / sizeof(prefix[0])))
        prefix_len = (int)(sizeof(prefix) / sizeof(prefix[0])) - 1;

    wmemcpy(prefix, line, (size_t)prefix_len);
    prefix[prefix_len] = L'\0';

    ftn_reply_bounds(ed, ed->row, prefix, prefix_len, &first, &last);

    old_count = last - first + 1;
    cursor_row_before = ed->row;
    cursor_col_before = ed->col;

    snapshot_before = ed_range_to_string(ed, first, last + 1);

    if (!snapshot_before)
        return -1;

    ed->undo_snapshot_mode = 1;

    joined = ftn_join_reply_block(ed, first, last, prefix_len, &joined_len);

    if (!joined)
    {
        ed->undo_snapshot_mode = 0;

        free(snapshot_before);
        return -1;
    }

    outw = ftn_split_reply_lines(joined, joined_len, prefix, prefix_len, width, &out_len);

    free(joined);

    if (!outw)
    {
        ed->undo_snapshot_mode = 0;

        free(snapshot_before);
        return -1;
    }

    outu = wcs_to_utf8(outw, (int)out_len);
    if (!outu)
    {
        free(outw);

        ed->undo_snapshot_mode = 0;

        free(snapshot_before);
        return -1;
    }

    if (out_len > 0 && outw[out_len - 1] == L'\n')
    {
        last_line_empty = 1;
    }
    else
    {
        wchar_t *last_nl = wcsrchr(outw, L'\n');

        last_line_len = last_nl ? (int)(out_len - (size_t)(last_nl - outw) - 1) : (int)out_len;
    }

    free(outw);

    doc_count_before = ed->count;
    inserted = ed_replace_range_from_utf8(ed, first, old_count, outu);

    free(outu);

    if (inserted <= 0)
    {
        ed->undo_snapshot_mode = 0;

        free(snapshot_before);
        return -1;
    }

    if (last_line_empty)
    {
        cursor_row_after = first + inserted - 1;
        cursor_col_after = 0;
    }
    else
    {
        cursor_row_after = first + inserted - 1;
        cursor_col_after = last_line_len;
    }

    ed->row = cursor_row_after;
    ed->col = cursor_col_after;
    ed->modified = 1;

    ed_prefix_invalidate_from(ed, first);

    ed->undo_snapshot_mode = 0;

    new_count = ed->count - (doc_count_before - old_count);

    if (new_count < 0)
        new_count = 0;

    snapshot_after = ed_range_to_string(ed, first, first + new_count);

    if (!snapshot_after)
    {
        free(snapshot_before);
        return -1;
    }

    ed_redo_clear(ed);

    if (ed_undo_open_group(ed) != 0)
    {
        free(snapshot_before);
        free(snapshot_after);
        return -1;
    }

    if (undo_push_snapshot_range(ed, first, cursor_col_before, snapshot_before, snapshot_after, old_count, new_count, cursor_row_before, cursor_col_before, cursor_row_after, cursor_col_after) != 0)
        return -1;

    ed->undo_open = 0;
    ed_prefix_invalidate(ed);

    return 0;
}

int ed_load_file_at_cursor(Ed *ed, const char *path, const char *charset_in)
{
    FILE *f = NULL;
    long sz;
    char *buf = NULL;
    char *content_to_paste = NULL;
    size_t r;
    int needs_conv;
    int cursor_row_before, cursor_col_before;
    int cursor_row_after, cursor_col_after;
    EdInfo info;
    UndoGroup *g = NULL;

    if (!ed || !path || !path[0])
        return -1;

    /* Save cursor position before paste */
    cursor_row_before = ed->row;
    cursor_col_before = ed->col;

    /* Convert charset if not AUTO/UTF-8 */
    needs_conv = charset_in && charset_in[0] && strcasecmp(charset_in, "AUTO") != 0 && strcasecmp(charset_in, "UTF-8") != 0 && strcasecmp(charset_in, "UTF8") != 0;

    f = fopen(path, "rb");

    if (!f)
        return -1;

    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0)
    {
        fclose(f);
        return -1;
    }

    buf = (char *)malloc((size_t)sz + 1);

    if (!buf)
    {
        fclose(f);
        return -1;
    }

    r = fread(buf, 1, (size_t)sz, f);

    fclose(f);

    buf[r] = '\0';

    if (needs_conv)
    {
        /* Allocate 4x buffer for charset conversion */
        size_t outsz = (size_t)r * 4 + 16;
        char *out = (char *)malloc(outsz);
        int wrote;

        if (!out)
        {
            free(buf);
            return -1;
        }

        wrote = charset_body_to_utf8(charset_in, buf, (int)r, out, (int)outsz);

        free(buf);

        buf = NULL; /* Ownership transferred; only out lives from here */

        if (wrote < 0 || wrote >= (int)outsz)
        {
            free(out);

            return -1;
        }

        out[wrote] = '\0';
        content_to_paste = out;
    }
    else
    {
        content_to_paste = buf;
        buf = NULL; /* Ownership transferred to content_to_paste */
    }

    /* Open undo group for paste operation */
    if (ed_undo_open_group(ed) != 0)
    {
        free(content_to_paste);
        return -1;
    }

    /* Suspend undo recording during paste to avoid duplicate operations */
    ed->undo_snapshot_mode = 1;

    /* Paste the text */
    if (ed_paste_text(ed, content_to_paste) != 0)
    {
        ed->undo_snapshot_mode = 0;
        ed->undo_open = 0;

        free(content_to_paste);
        return -1;
    }

    /* Restore undo recording */
    ed->undo_snapshot_mode = 0;

    /* Save state after paste */
    ed_get_info(ed, &info);

    cursor_row_after = info.row;
    cursor_col_after = info.col;

    /* Create OP_PASTE operation */
    g = &ed->undo_stack[ed->undo_top - 1];
    g->cur_row = cursor_row_before;
    g->cur_col = cursor_col_before;
    g->end_row = cursor_row_after;
    g->end_col = cursor_col_after;

    if (g->count >= g->cap)
    {
        int nc = (g->cap > 0) ? (g->cap * 2) : 4;
        UndoOp *t = (UndoOp *)realloc(g->ops, (size_t)nc * sizeof(UndoOp));

        if (!t)
        {
            ed->undo_open = 0;

            free(content_to_paste);
            return -1;
        }

        g->ops = t;
        g->cap = nc;
    }

    /* Store the pasted text with block coordinates */
    g->ops[g->count].type = OP_PASTE;
    g->ops[g->count].row = cursor_row_before;
    g->ops[g->count].col = cursor_col_before;
    g->ops[g->count].len = 0;
    g->ops[g->count].join_col = 0;
    g->ops[g->count].text = NULL;
    g->ops[g->count].utf8_snapshot_new = NULL;
    g->ops[g->count].utf8_snapshot = strdup(content_to_paste);

    if (!g->ops[g->count].utf8_snapshot)
    {
        ed->undo_open = 0;

        free(content_to_paste);
        return -1;
    }

    g->ops[g->count].hard_wrap_mode = ed->hard_wrap;
    g->ops[g->count].end_row = cursor_row_after;
    g->ops[g->count].end_col = cursor_col_after;
    g->count++;
    ed->undo_open = 0;

    /* Free the buffer */
    free(content_to_paste);

    /* Invalidate prefix after paste */
    ed_prefix_invalidate_from(ed, cursor_row_before);

    return 0;
}

int ed_export_block_to_file(Ed *ed, const char *path, const char *charset_out)
{
    int r1, c1, r2, c2;
    int i;
    FILE *f = NULL;
    char *utf8 = NULL;
    int needs_conv;
    char conv[4096];

    if (!ed || !path || !path[0] || !ed->block.active)
        return -1;

    /* Convert charset if not AUTO/UTF-8 */
    needs_conv = charset_out && charset_out[0] && strcasecmp(charset_out, "AUTO") != 0 && strcasecmp(charset_out, "UTF-8") != 0 && strcasecmp(charset_out, "UTF8") != 0;

    if (ed->block.anchor_row < ed->row || (ed->block.anchor_row == ed->row && ed->block.anchor_col <= ed->col))
    {
        r1 = ed->block.anchor_row;
        c1 = ed->block.anchor_col;
        r2 = ed->row;
        c2 = ed->col;
    }
    else
    {
        r1 = ed->row;
        c1 = ed->col;
        r2 = ed->block.anchor_row;
        c2 = ed->block.anchor_col;
    }

    f = fopen(path, "wb");

    if (!f)
        return -1;

    for (i = r1; i <= r2; i++)
    {
        const wchar_t *l = ed_line_wcs(ed, i);
        int ll = ed->lines[i]->len;
        int s = (i == r1) ? c1 : 0;
        int e = (i == r2) ? c2 : ll;

        if (s > ll)
            s = ll;

        if (e > ll)
            e = ll;

        if (e > s)
        {
            utf8 = wcs_to_utf8(&l[s], e - s);

            if (utf8)
            {
                if (needs_conv)
                {
                    int cn = charset_body_from_utf8(charset_out, utf8, (int)strlen(utf8), conv, sizeof(conv));

                    if (cn > 0)
                        fwrite(conv, 1, (size_t)cn, f);
                }
                else
                {
                    fwrite(utf8, 1, strlen(utf8), f);
                }

                free(utf8);
            }
        }

        if (i < r2)
            fputc('\n', f);
    }

    fclose(f);

    return 0;
}
