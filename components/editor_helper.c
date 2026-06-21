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

/* Convert entire line to wchar_t string (caller frees) */
wchar_t *line_to_wcs(EdLine *ln)
{
    wchar_t *result = NULL;

    if (!ln)
        return NULL;

    result = (wchar_t *)malloc((ln->len + 1) * sizeof(wchar_t));

    if (!result)
        return NULL;

    if (ln->len > 0)
        memcpy(result, ln->wcs, ln->len * sizeof(wchar_t));

    result[ln->len] = L'\0';

    return result;
}

/* Convert line range to wchar_t string (caller frees) */
wchar_t *line_to_wcs_range(EdLine *ln, int start, int end)
{
    int len;
    wchar_t *result = NULL;

    if (!ln || start < 0 || end > ln->len || start >= end)
        return NULL;

    len = end - start;
    result = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));

    if (!result)
        return NULL;

    if (len > 0)
        memcpy(result, &ln->wcs[start], len * sizeof(wchar_t));

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

    /* Build result string using cursor + memcpy for O(n) instead of O(n^2) with strcat */
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
        line = ed->lines[row]->wcs;
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

                if (a >= L'A' && a <= L'Z')
                    a = a - L'A' + L'a';

                if (b >= L'A' && b <= L'Z')
                    b = b - L'A' + L'a';

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
        line = ed->lines[i]->wcs;
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
                if (a >= L'A' && a <= L'Z')
                    a = a - L'A' + L'a';

                if (b >= L'A' && b <= L'Z')
                    b = b - L'A' + L'a';

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
        line = ed->lines[i]->wcs;
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
                if (a >= L'A' && a <= L'Z')
                    a = a - L'A' + L'a';

                if (b >= L'A' && b <= L'Z')
                    b = b - L'A' + L'a';

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
        const wchar_t *line = ed->lines[i]->wcs;
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

                    /* Check if previous and next characters are word boundaries */
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
                        new_cols = realloc(*out_cols, capacity * sizeof(int));

                        if (!new_rows || !new_cols)
                        {
                            free(new_rows);
                            free(new_cols);
                            *out_rows = *out_cols = NULL;
                            return 0;
                        }

                        *out_rows = new_rows;
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

    while (line[p] == L' ' && p < 8)
        p++;

    while (p < 8 && line[p] && line[p] != L'>' && line[p] != L'<')
        p++;

    if (line[p] == L'>' && line[p + 1] == L' ')
        return p + 2;

    return 0;
}

int ed_rewrap_paragraph(Ed *ed, int width)
{
    return ed_rewrap_paragraph_ex(ed, width, NULL, NULL);
}

int ed_rewrap_paragraph_ex(Ed *ed, int width, EdHyphenFn hyph, void *hyph_data)
{
    int first, last, i;
    int prefix_len;
    const wchar_t *line = NULL;
    wchar_t prefix[64];
    wchar_t *joined = NULL;
    wchar_t *tmp_joined = NULL;
    wchar_t *tmp = NULL;
    size_t cap, used;
    int avail = 0;
    size_t pos = 0;
    size_t out_cap = 0;
    size_t out_used = 0;
    wchar_t *outw = NULL;
    char *outu = NULL;
    int k;
    char *snapshot_before = NULL;
    char *snapshot_after = NULL;
    UndoGroup *g = NULL;
    int need_snapshot = 0;
    int cursor_row_before = 0;
    int cursor_col_before = 0;
    int cursor_row_after = 0;
    int cursor_col_after = 0;
    int old_count = 0;
    int doc_count_before = 0;

    if (!ed || width < 20 || ed->count <= 0)
        return -1;

    /* Check if we need to create a snapshot (only if not already in snapshot mode) */
    if (!ed->undo_snapshot_mode)
        need_snapshot = 1;

    line = ed->lines[ed->row]->wcs;

    if (!line || !line[0])
        return -1;

    prefix_len = ed_detect_quote_prefix(line);

    if (prefix_len >= (int)(sizeof(prefix) / sizeof(prefix[0])))
        prefix_len = (int)(sizeof(prefix) / sizeof(prefix[0])) - 1;

    if (prefix_len > 0)
        wmemcpy(prefix, line, (size_t)prefix_len);

    prefix[prefix_len] = L'\0';

    /* Find paragraph boundaries (same prefix, non-empty) */
    first = ed->row;

    while (first > 0)
    {
        const wchar_t *l = ed->lines[first - 1]->wcs;

        if (!l || !l[0])
            break;

        if (prefix_len > 0)
        {
            if (wcsncmp(l, prefix, (size_t)prefix_len) != 0)
                break;
        }
        else
        {
            int p = 0;

            while (l[p] == L' ' && p < 8)
                p++;

            if (l[p] == L'>')
                break;
        }

        first--;
    }

    last = ed->row;

    while (last < ed->count - 1)
    {
        const wchar_t *l = ed->lines[last + 1]->wcs;

        if (!l || !l[0])
            break;

        if (prefix_len > 0)
        {
            if (wcsncmp(l, prefix, (size_t)prefix_len) != 0)
                break;
        }
        else
        {
            int p = 0;

            while (l[p] == L' ' && p < 8)
                p++;

            if (l[p] == L'>')
                break;
        }

        last++;
    }

    /* Join all lines (without prefix) into one buffer */
    cap = 256;
    used = 0;
    joined = (wchar_t *)malloc(cap * sizeof(wchar_t));

    if (!joined)
        return -1;

    for (i = first; i <= last; i++)
    {
        const wchar_t *l = ed->lines[i]->wcs;
        int ll = ed->lines[i]->len;
        int skip = prefix_len;

        if (skip > ll)
            skip = ll;

        if (used + (size_t)(ll - skip) + 2 >= cap)
        {
            cap = (used + (size_t)(ll - skip) + 64) * 2;
            tmp_joined = (wchar_t *)realloc(joined, cap * sizeof(wchar_t));

            if (!tmp_joined)
            {
                free(joined);
                return -1;
            }

            joined = tmp_joined;
        }

        if (used > 0 && joined[used - 1] != L' ')
            joined[used++] = L' ';

        wmemcpy(&joined[used], l + skip, (size_t)(ll - skip));
        used += (size_t)(ll - skip);
    }

    joined[used] = L'\0';

    /* Build the re-wrapped output buffer (wchar_t with L'\n' between lines) */
    avail = width - prefix_len;
    pos = 0;
    out_cap = used * 2 + 64;
    out_used = 0;
    outw = (wchar_t *)malloc(out_cap * sizeof(wchar_t));

    if (!outw)
    {
        free(joined);
        return -1;
    }

    if (avail < 10)
        avail = 10;

    while (pos < used)
    {
        int line_len, break_at;
        int hyph_inserted = 0; /* set when we break mid-word with '-' */

        if (out_used + (size_t)prefix_len + (size_t)avail + 2 >= out_cap)
        {
            out_cap = (out_cap + (size_t)avail + 64) * 2;
            tmp = (wchar_t *)realloc(outw, out_cap * sizeof(wchar_t));

            if (!tmp)
            {
                free(outw);
                free(joined);

                return -1;
            }

            outw = tmp;
        }

        for (k = 0; k < prefix_len; k++)
            outw[out_used++] = prefix[k];

        line_len = (int)(used - pos);

        if (line_len > avail)
            line_len = avail;

        break_at = line_len;

        if ((size_t)(pos + line_len) < used)
        {
            int j;
            int space_found = 0;

            for (j = line_len; j > avail / 3; j--)
            {
                if (joined[pos + j - 1] == L' ')
                {
                    break_at = j;
                    space_found = 1;
                    break;
                }
            }

            /* No space and hyphenator available: split word with hyphen */
            if (!space_found && hyph)
            {
                size_t ws, we;
                char *uw = NULL;

                /* Find word boundaries: ws back, we forward */
                ws = pos + (size_t)line_len;

                while (ws > pos && joined[ws - 1] != L' ')
                    ws--;

                we = pos + (size_t)line_len;

                while (we < used && joined[we] != L' ')
                    we++;

                if (we > ws + 3)
                {
                    uw = wcs_to_utf8(&joined[ws], (int)(we - ws));

                    if (uw)
                    {
                        int uw_bytes = (int)strlen(uw);
                        int hp[16];
                        int hn = 16;

                        if (hyph(hyph_data, uw, uw_bytes, hp, &hn) && hn > 0)
                        {
                            int idx;

                            /* Pick rightmost break that fits (avail - 1) */
                            for (idx = hn - 1; idx >= 0; idx--)
                            {
                                int cco = utf8_charcount(uw, hp[idx]);
                                int wc_pos = (int)ws + cco; /* in joined */
                                int line_chars = wc_pos - (int)pos;

                                if (line_chars >= avail) /* leave 1 col for '-' */
                                    continue;

                                if (line_chars <= 0)
                                    break;

                                break_at = line_chars;
                                hyph_inserted = 1;
                                break;
                            }
                        }

                        free(uw);
                    }
                }
            }
        }

        for (k = 0; k < break_at; k++)
            outw[out_used++] = joined[pos + k];

        if (hyph_inserted)
        {
            /* Emit hyphen, continue at word second half */
            outw[out_used++] = L'-';
            pos += (size_t)break_at;
        }
        else
        {
            /* Trim trailing spaces from the line we just added */
            while (out_used > 0 && outw[out_used - 1] == L' ')
                out_used--;

            pos += (size_t)break_at;

            while (pos < used && joined[pos] == L' ')
                pos++;
        }

        outw[out_used++] = L'\n';
    }

    outw[out_used] = L'\0';
    free(joined);

    /* Save snapshot of affected paragraph only (not whole doc) for efficient undo */
    old_count = 0;
    doc_count_before = ed->count;

    if (need_snapshot)
    {
        cursor_row_before = ed->row;
        cursor_col_before = ed->col;
        old_count = last - first + 1;

        snapshot_before = ed_range_to_string(ed, first, last + 1);

        if (!snapshot_before)
        {
            free(outw);
            return -1;
        }

        /* Enable snapshot mode to block individual undo operations */
        ed->undo_snapshot_mode = 1;
    }

    /* Delete the old paragraph block */
    ed_set_pos(ed, first, 0);

    for (i = first; i <= last; i++)
        ed_delete_line(ed);

    ed_set_pos(ed, first, 0);

    outu = wcs_to_utf8(outw, (int)out_used);

    free(outw);

    if (outu)
    {
        ed_paste_text(ed, outu);
        free(outu);
    }

    /* Disable snapshot mode and save snapshot after rewrap if needed */
    if (need_snapshot)
    {
        int new_count;

        cursor_row_after = ed->row;
        cursor_col_after = ed->col;

        ed->undo_snapshot_mode = 0;

        /* Calculate new line count after rewrap */
        new_count = ed->count - (doc_count_before - old_count);

        if (new_count < 0)
            new_count = 0;

        snapshot_after = ed_range_to_string(ed, first, first + new_count);

        if (!snapshot_after)
        {
            free(snapshot_before);

            return -1;
        }

        /* Push snapshot to undo stack */
        ed_redo_clear(ed);

        if (ed_undo_open_group(ed) != 0)
        {
            free(snapshot_before);
            free(snapshot_after);

            return -1;
        }

        if (ed->undo_top <= 0)
        {
            free(snapshot_before);
            free(snapshot_after);

            return -1;
        }

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
                free(snapshot_before);
                free(snapshot_after);

                return -1;
            }

            g->ops = t;
            g->cap = nc;
        }

        g->ops[g->count].type = OP_SNAPSHOT_RANGE;
        g->ops[g->count].row = first;
        g->ops[g->count].col = cursor_col_before;
        g->ops[g->count].len = 0;
        g->ops[g->count].join_col = 0;
        g->ops[g->count].text = NULL;
        g->ops[g->count].utf8_snapshot = snapshot_before;
        g->ops[g->count].utf8_snapshot_new = snapshot_after;
        g->ops[g->count].hard_wrap_mode = ed->hard_wrap;
        g->ops[g->count].end_row = old_count; /* OLD line count */
        g->ops[g->count].end_col = new_count; /* NEW line count */
        g->count++;
        ed->undo_open = 0;

        /* Snapshot ownership transferred to undo op */
        snapshot_before = NULL;
        snapshot_after = NULL;

        ed_prefix_invalidate(ed);
    }

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

    /* If it's null, auto, or detects utf8, it's passed directly; otherwise, it's converted */
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
        /* Allocate 4x buffer for charset conversion (max 3 UTF-8 bytes per input byte) */
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
    }

    /* Open undo group for paste operation */
    if (ed_undo_open_group(ed) != 0)
    {
        if (needs_conv)
            free(content_to_paste);
        else
            free(buf);
        return -1;
    }

    /* Suspend undo recording during paste to avoid duplicate operations */
    ed->undo_snapshot_mode = 1;

    /* Paste the text */
    if (ed_paste_text(ed, content_to_paste) != 0)
    {
        ed->undo_snapshot_mode = 0;
        ed->undo_open = 0;
        if (needs_conv)
            free(content_to_paste);
        else
            free(buf);
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

            if (needs_conv)
                free(content_to_paste);
            else
                free(buf);

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

        if (needs_conv)
            free(content_to_paste);
        else
            free(buf);

        return -1;
    }

    g->ops[g->count].hard_wrap_mode = ed->hard_wrap;
    g->ops[g->count].end_row = cursor_row_after;
    g->ops[g->count].end_col = cursor_col_after;
    g->count++;
    ed->undo_open = 0;

    /* Free the buffer */
    if (needs_conv)
        free(content_to_paste);
    else
        free(buf);

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

    /* If it's null, auto, or detects utf8, it's passed directly; otherwise, it's converted */
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
        const wchar_t *l = ed->lines[i]->wcs;
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
