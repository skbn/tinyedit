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
#include "undo.h"
#include "ed_attr.h"
#include "../core/utf8.h"
#include "../core/charset.h"

#ifdef HAVE_HYPHEN

#if defined(PLATFORM_AMIGA)
#include "../spellchecker/hyph.h"
#else
#include "../hyph_wrap/hyph_wrap.h"
#endif

#endif

/* One char of the joined paragraph with the styling it carried */
typedef struct
{
    wchar_t ch;
    unsigned short mask;
    short font;
    short size;
} ParaChar;

/* Grow-only scratch shared across many rewraps to skip a per-para malloc storm */
typedef struct
{
    ParaChar *para;
    int para_cap;
    wchar_t *text;
    int text_cap;
    LayoutLine *lines;
    int lines_cap;
    EdLine **built;
    int built_cap;
} RewrapScratch;

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

    /* Count matches */
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

    /* Fill arrays */
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

        *out_rows = NULL;
        *out_cols = NULL;

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
                        match = 0;
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
    wchar_t *tmp = NULL;
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
                    need_space = 0;
                }

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

/* Reflow the current FTN reply quote block */
int ed_rewrap_ftn_reply(Ed *ed, int width)
{
    const wchar_t *line = NULL;
    int prefix_len;
    wchar_t prefix[16];
    int first = 0;
    int last = 0;
    int old_count = 0;
    int new_count = 0;
    int doc_count_before = 0;
    int inserted = 0;
    int cursor_row_after = 0;
    int cursor_col_after = 0;
    int last_line_empty = 0;
    int last_line_len = 0;
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

    /* Capture the quote block, the commit at the end closes this delta */
    undo_abort(ed);

    if (undo_begin(ed, first, old_count) != 0)
        return -1;

    ed->undo_snapshot_mode = 1;

    joined = ftn_join_reply_block(ed, first, last, prefix_len, &joined_len);

    if (!joined)
    {
        ed->undo_snapshot_mode = 0;

        undo_abort(ed);
        return -1;
    }

    outw = ftn_split_reply_lines(joined, joined_len, prefix, prefix_len, width, &out_len);

    free(joined);

    if (!outw)
    {
        ed->undo_snapshot_mode = 0;

        undo_abort(ed);
        return -1;
    }

    outu = wcs_to_utf8(outw, (int)out_len);
    if (!outu)
    {
        free(outw);

        ed->undo_snapshot_mode = 0;

        undo_abort(ed);
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

        undo_abort(ed);
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

    new_count = ed->count - (doc_count_before - ed->pending_n);

    if (new_count < 0)
        new_count = 0;

    /* The pre edit capture is closed here by the reflow commit */
    if (undo_commit(ed, new_count) != 0)
        return -1;

    ed_save_undo(ed);
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
    int cursor_row_before;
    int cursor_row_after;
    EdInfo info;

    if (!ed || !path || !path[0])
        return -1;

    /* Save cursor position before paste */
    cursor_row_before = ed->row;

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

    /* One delta: the cursor line becomes the pasted lines */
    if (undo_begin(ed, cursor_row_before, 1) != 0)
    {
        free(content_to_paste);
        return -1;
    }

    /* That delta already covers the paste, skip per character recording */
    ed->undo_snapshot_mode = 1;

    if (ed_paste_text(ed, content_to_paste) != 0)
    {
        ed->undo_snapshot_mode = 0;

        undo_abort(ed);
        free(content_to_paste);

        return -1;
    }

    ed->undo_snapshot_mode = 0;

    ed_get_info(ed, &info);

    cursor_row_after = info.row;

    if (undo_commit(ed, cursor_row_after - cursor_row_before + 1) != 0)
    {
        free(content_to_paste);
        return -1;
    }

    ed_save_undo(ed);
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

/* Paragraph bounds from the brk bits: LB_PARA ends one, never merge across */
static void para_bounds(Ed *ed, int row, int *first, int *last)
{
    int f = row;
    int l = row;

    if (ed->lines[row]->len == 0)
    {
        *first = row;
        *last = row;

        return;
    }

    while (f > 0 && ed->lines[f - 1]->len > 0 && ed->lines[f - 1]->brk != LB_PARA)
        f--;

    while (l < ed->count - 1 && ed->lines[l]->brk != LB_PARA && ed->lines[l + 1]->len > 0)
        l++;

    *first = f;
    *last = l;
}

static void rewrap_scratch_init(RewrapScratch *s)
{
    s->para = NULL;
    s->para_cap = 0;
    s->text = NULL;
    s->text_cap = 0;
    s->lines = NULL;
    s->lines_cap = 0;
    s->built = NULL;
    s->built_cap = 0;
}

static void rewrap_scratch_free(RewrapScratch *s)
{
    free(s->para);
    free(s->text);
    free(s->lines);
    free(s->built);

    rewrap_scratch_init(s);
}

static int scratch_reserve_para(RewrapScratch *s, int need)
{
    ParaChar *t = NULL;

    if (need <= s->para_cap)
        return 0;

    t = (ParaChar *)realloc(s->para, (size_t)need * sizeof(ParaChar));

    if (!t)
        return -1;

    s->para = t;
    s->para_cap = need;

    return 0;
}

static int scratch_reserve_text(RewrapScratch *s, int need)
{
    wchar_t *t = NULL;

    if (need <= s->text_cap)
        return 0;

    t = (wchar_t *)realloc(s->text, (size_t)need * sizeof(wchar_t));

    if (!t)
        return -1;

    s->text = t;
    s->text_cap = need;

    return 0;
}

static int scratch_reserve_lines(RewrapScratch *s, int need)
{
    LayoutLine *t = NULL;

    if (need <= s->lines_cap)
        return 0;

    t = (LayoutLine *)realloc(s->lines, (size_t)need * sizeof(LayoutLine));

    if (!t)
        return -1;

    s->lines = t;
    s->lines_cap = need;

    return 0;
}

static int scratch_reserve_built(RewrapScratch *s, int need)
{
    EdLine **t = NULL;

    if (need <= s->built_cap)
        return 0;

    t = (EdLine **)realloc(s->built, (size_t)need * sizeof(EdLine *));

    if (!t)
        return -1;

    s->built = t;
    s->built_cap = need;

    return 0;
}

/* Join the paragraph into scratch->para, word breaks straight, space breaks add one */
static int para_join_into(Ed *ed, int first, int last, int cur_row, int cur_col, int *out_len, int *out_cursor, RewrapScratch *scr)
{
    int cap = 0;
    int n = 0;
    int i;
    ParaChar *buf = NULL;

    for (i = first; i <= last; i++)
        cap += ed->lines[i]->len + 1;

    if (cap <= 0)
        cap = 1;

    if (scratch_reserve_para(scr, cap) != 0)
        return -1;

    buf = scr->para;
    *out_cursor = 0;

    for (i = first; i <= last; i++)
    {
        EdLine *ln = ed->lines[i];
        int len = ln->len;
        int last_ch;
        int stripped;
        int j;

        /* Read chars from the packed line directly, no per-line wcs alloc */
        last_ch = (len > 0) ? (int)ed_line_char(ln, len - 1) : 0;
        stripped = 0;

        /* A stored trailing hyphen is legacy layout from disk, drop it once */
        if (ln->brk != LB_HYPHEN && len > 0 && last_ch == (int)L'-' && i < last)
        {
            len--;
            stripped = 1;
        }

        if (cur_row == i)
            *out_cursor = n + (cur_col < len ? cur_col : len);

        for (j = 0; j < len; j++)
        {
            short font = 0;
            short size = 0;

            buf[n].ch = (wchar_t)ed_line_char(ln, j);
            buf[n].mask = ed_attr_mask_at(ln, j, &font, &size);
            buf[n].font = font;
            buf[n].size = size;

            n++;
        }

        /* Eaten space returns, breaks inside a word join with nothing */
        if (i < last && ln->brk != LB_HYPHEN && ln->brk != LB_WORD && !stripped && n > 0)
        {
            int prev = n - 1;

            buf[n].ch = L' ';
            buf[n].mask = buf[prev].mask;
            buf[n].font = buf[prev].font;
            buf[n].size = buf[prev].size;

            n++;
        }
    }

    *out_len = n;

    return 0;
}

/* Reflow lines [first..last] into a paragraph using scratch buffers */
static int rewrap_range_no_undo_ex(Ed *ed, int width, LayoutHyphenFn hyph, void *hyph_user, int first, int last, int *out_new_lines, RewrapScratch *scr)
{
    LayoutOpts opt;
    int plen = 0;
    int cursor = 0;
    int max_lines;
    int n;
    int i;
    int new_row = first;
    int new_col = 0;
    int old_count = last - first + 1;
    int same;

    if (out_new_lines)
        *out_new_lines = old_count;

    if (para_join_into(ed, first, last, ed->row, ed->col, &plen, &cursor, scr) != 0)
        return -1;

    if (scratch_reserve_text(scr, plen + 1) != 0)
        return -1;

    for (i = 0; i < plen; i++)
        scr->text[i] = scr->para[i].ch;

    scr->text[plen] = 0;

    /* At most one line per width chars, small safety margin */
    max_lines = plen / (width > 0 ? width : 1) + 4;

    if (scratch_reserve_lines(scr, max_lines) != 0)
        return -1;

    layout_opts_default(&opt);

    opt.width = ed_layout_char_width;
    opt.width_user = ed;
    opt.hyphenate = hyph ? 1 : 0;
    opt.hyphen = hyph;
    opt.hyphen_user = hyph_user;

    n = layout_paragraph(scr->text, plen, width, &opt, scr->lines, max_lines);

    if (n < 0)
        return -1;

    if (scratch_reserve_built(scr, n) != 0)
        return -1;

    for (i = 0; i < n; i++)
    {
        int start = scr->lines[i].start;
        int count = scr->lines[i].end - start;
        EdLine *ln = NULL;
        int j;
        int k;

        ln = ed_line_from_wcs(ed, scr->text + start, count);

        if (!ln)
        {
            for (k = 0; k < i; k++)
                ed_line_destroy(ed, scr->built[k]);

            return -1;
        }

        for (j = 0; j < count; j++)
        {
            ParaChar *pc = &scr->para[start + j];

            if (pc->mask || pc->font || pc->size)
                ed_attr_line_apply(ln, j, j + 1, pc->mask, 0, pc->font, pc->size);
        }

        ln->brk = (unsigned char)scr->lines[i].brk;
        ln->para_align = ed->lines[first]->para_align;

        scr->built[i] = ln;

        if (cursor >= start && cursor <= scr->lines[i].end)
        {
            new_row = first + i;
            new_col = cursor - start;
        }
    }

    /* Same count and lengths means no cut moved, skip the splice */
    same = 0;

    if (n == old_count)
    {
        same = 1;

        for (i = 0; i < n; i++)
        {
            if (scr->built[i]->len != ed->lines[first + i]->len || scr->built[i]->brk != ed->lines[first + i]->brk)
            {
                same = 0;
                break;
            }
        }
    }

    if (same)
    {
        for (i = 0; i < n; i++)
            ed_line_destroy(ed, scr->built[i]);

        if (out_new_lines)
            *out_new_lines = n;

        return 0;
    }

    if (ed_lines_splice(ed, first, old_count, scr->built, n) != 0)
    {
        for (i = 0; i < n; i++)
            ed_line_destroy(ed, scr->built[i]);

        return -1;
    }

    ed->row = new_row;
    ed->col = new_col;
    ed->modified = 1;

    ed_clamp(ed);
    ed_prefix_invalidate_from(ed, first);

    if (out_new_lines)
        *out_new_lines = n;

    return 0;
}

/* Old signature: local scratch, single call, free on return */
static int rewrap_range_no_undo(Ed *ed, int width, LayoutHyphenFn hyph, void *hyph_user, int first, int last, int *out_new_lines)
{
    RewrapScratch scr;
    int rc;

    rewrap_scratch_init(&scr);

    rc = rewrap_range_no_undo_ex(ed, width, hyph, hyph_user, first, last, out_new_lines, &scr);

    rewrap_scratch_free(&scr);

    return rc;
}

/* Blank-line paragraph bounds from the cursor row, then reflow */
static int rewrap_one_paragraph_no_undo(Ed *ed, int width, LayoutHyphenFn hyph, void *hyph_user, int *out_new_lines)
{
    int first;
    int last;

    para_bounds(ed, ed->row, &first, &last);

    return rewrap_range_no_undo(ed, width, hyph, hyph_user, first, last, out_new_lines);
}

int ed_rewrap_paragraph_ex(Ed *ed, int width, LayoutHyphenFn hyph, void *hyph_user)
{
    int n = 0;
    int n_after = 0;

    if (!ed || width <= 0 || ed->count <= 0)
        return -1;

    if (rewrap_one_paragraph_no_undo(ed, width, hyph, hyph_user, &n) != 0)
    {
        undo_abort(ed);
        ed->undo_snapshot_mode = 0;
        return -1;
    }

    /* The paragraph was captured before the edit, this closes that delta */
    ed->undo_snapshot_mode = 0;

    /* Adjust the post-edit line count when the captured range includes a guard line beyond the paragraph */
    if (ed->pending_before)
        n_after = ed->pending_n + (ed->count - ed->pending_doc_count);
    else
        n_after = n;

    if (n_after < 0)
        n_after = 0;

    if (undo_commit(ed, n_after) != 0)
        return -1;

    ed_save_undo(ed);

    return 0;
}

/* Same reflow with no undo record, for callers holding an open delta */
int ed_rewrap_paragraph_no_undo(Ed *ed, int width, LayoutHyphenFn hyph, void *hyph_user)
{
    int n = 0;

    if (!ed || width <= 0 || ed->count <= 0)
        return -1;

    return rewrap_one_paragraph_no_undo(ed, width, hyph, hyph_user, &n);
}

/* Refit every paragraph to width, brk bounded, one undo entry */
int ed_rewrap_document(Ed *ed, int width, LayoutHyphenFn hyph, void *hyph_user)
{
    int row;
    int first;
    int last;
    int n;
    int old_count;
    int old_row;
    int old_col;

    if (!ed || width <= 0 || ed->count <= 0)
        return 0;

    old_row = ed->row;
    old_col = ed->col;
    old_count = ed->count;

    /* One delta covers the whole document */
    undo_abort(ed);

    if (undo_begin(ed, 0, old_count) != 0)
        return -1;

    row = 0;

    while (row < ed->count)
    {
        if (ed->lines[row]->len == 0)
        {
            row++;
            continue;
        }

        /* Paragraph runs while the break bit says it continues */
        first = row;
        last = row;

        while (last < ed->count - 1 && ed->lines[last]->brk != LB_PARA)
            last++;

        n = 0;

        if (rewrap_range_no_undo(ed, width, hyph, hyph_user, first, last, &n) != 0)
        {
            undo_abort(ed);
            return -1;
        }

        row = first + (n > 0 ? n : 1);
    }

    if (undo_commit(ed, ed->count) != 0)
        return -1;

    ed_save_undo(ed);

    if (old_row < ed->count)
        ed_set_pos(ed, old_row, old_col);
    else
        ed_set_pos(ed, ed->count - 1, 0);

    return 0;
}

/* Fit a loaded document to width, brk bounded, untouched if it fits */
int ed_rewrap_loaded_document(Ed *ed, int width, LayoutHyphenFn hyph, void *hyph_user)
{
    RewrapScratch scr;
    int row;
    int rc = 0;

    if (!ed || width <= 0 || ed->count <= 0)
        return 0;

    rewrap_scratch_init(&scr);
    row = 0;

    while (row < ed->count)
    {
        int first = row;
        int last = row;
        int n = 0;
        EdLine *ln;

        if (ed->lines[row]->len == 0)
        {
            row++;
            continue;
        }

        /* The paragraph runs while the break bit says "continues" */
        while (last < ed->count - 1 && ed->lines[last]->brk != LB_PARA)
            last++;

        /* Fast path: single line that already fits and carries no styling */
        ln = ed->lines[first];

        if (first == last && ln->len <= width && ln->n_attrs == 0)
        {
            row = first + 1;
            continue;
        }

        if (rewrap_range_no_undo_ex(ed, width, hyph, hyph_user, first, last, &n, &scr) != 0)
        {
            rc = -1;
            break;
        }

        row = first + (n > 0 ? n : 1);
    }

    rewrap_scratch_free(&scr);

    if (rc != 0)
        return rc;

    ed_set_pos(ed, 0, 0);
    ed_clamp(ed);

    return 0;
}
