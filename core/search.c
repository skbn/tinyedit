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

/* search.c -- Text search for editor */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "search.h"

/* Case-insensitive ASCII memmem, returns match offset or -1 */
static int memcasestr(const char *hay, int n, const char *needle, int m)
{
    int i, j;

    if (m <= 0)
        return 0; /* empty needle matches at start */

    if (m > n)
        return -1;

    for (i = 0; i <= n - m; i++)
    {
        for (j = 0; j < m; j++)
        {
            unsigned char a = (unsigned char)hay[i + j];
            unsigned char b = (unsigned char)needle[j];

            if (a >= 'A' && a <= 'Z')
                a = (unsigned char)(a + 32);

            if (b >= 'A' && b <= 'Z')
                b = (unsigned char)(b + 32);

            if (a != b)
                break;
        }

        if (j == m)
            return i;
    }

    return -1;
}

/* Case-SENSITIVE memmem variant */
static int memmemstr(const char *hay, int n, const char *needle, int m)
{
    int i;

    if (m <= 0)
        return 0;

    if (m > n)
        return -1;

    for (i = 0; i <= n - m; i++)
    {
        if (memcmp(hay + i, needle, (size_t)m) == 0)
            return i;
    }

    return -1;
}

SearchSession *search_new(const char *pattern, int case_sensitive, int max_hits)
{
    SearchSession *s;
    size_t pl;

    if (!pattern || !pattern[0])
        return NULL;

    pl = strlen(pattern);

    if (pl >= SEARCH_PATTERN_MAX)
        pl = SEARCH_PATTERN_MAX - 1;

    s = (SearchSession *)calloc(1, sizeof(SearchSession));

    if (!s)
        return NULL;

    /* Clamp into 1..HARD_MAX. <=0 means "use the default" */
    if (max_hits <= 0)
        max_hits = SEARCH_DEFAULT_MAX;

    if (max_hits > SEARCH_HITS_HARD_MAX)
        max_hits = SEARCH_HITS_HARD_MAX;

    s->hits = (SearchHit *)malloc((size_t)max_hits * sizeof(SearchHit));

    if (!s->hits)
    {
        /* Couldn't get a buffer that big -- tell the caller */
        free(s);
        return NULL;
    }

    memcpy(s->pattern, pattern, pl);

    s->pattern[pl] = '\0';
    s->pat_len = (int)pl;
    s->case_sensitive = case_sensitive ? 1 : 0;
    s->max_hits = max_hits;

    return s;
}

void search_free(SearchSession *s)
{
    if (s)
    {
        free(s->hits);
        free(s);
    }
}

/* Copy at most cap-1 chars + NUL from src to dst (src is null-terminated) */
static void clip_copy(char *dst, int cap, const char *src)
{
    int i = 0;

    if (!dst || cap <= 0)
        return;

    if (src)
    {
        for (; i < cap - 1 && src[i]; i++)
            dst[i] = src[i];
    }

    dst[i] = '\0';
}

/* Extract preview context around match position */
static void extract_preview(const char *line, int line_len, int match_pos, int pat_len, char *preview, int preview_chars)
{
    int start, end, i;

    if (!line || !preview || preview_chars <= 0)
        return;

    /* Calculate start position (include some context before match) */
    start = match_pos - (preview_chars / 3);

    if (start < 0)
        start = 0;

    /* Calculate end position (include some context after match) */
    end = match_pos + pat_len + (preview_chars / 3);

    if (end > line_len)
        end = line_len;

    /* Ensure we don't exceed preview buffer */
    if (end - start > preview_chars - 1)
        end = start + preview_chars - 1;

    /* Copy preview */
    for (i = 0; i < end - start && line[start + i]; i++)
        preview[i] = line[start + i];

    preview[i] = '\0';
}

/* Append one hit to the session. Returns 1 if added, 0 if cap hit */
static int record_hit(SearchSession *s, int line, int col, const char *preview)
{
    SearchHit *h;

    if (s->n_hits >= s->max_hits)
    {
        s->hit_limit_reached = 1;
        return 0;
    }

    h = &s->hits[s->n_hits++];
    h->line = line;
    h->col = col;
    h->flags = SEARCH_HIT_TEXT;

    clip_copy(h->preview, SEARCH_PREVIEW_MAX, preview);

    return 1;
}

/* Search text buffer, record hits, return count or -1 if cancelled */
int search_text(SearchSession *s, const char **text_lines, int nlines, int preview_chars)
{
    int i, hits_before;

    if (!s || !text_lines || nlines <= 0)
        return 0;

    if (s->cancel)
        return -1;

    hits_before = s->n_hits;
    s->total_lines = nlines;
    s->scanned_lines = 0;

    for (i = 0; i < nlines; i++)
    {
        const char *line = text_lines[i];
        int line_len;
        int pos;
        int preview_buf_size;
        char *preview_buf = NULL;

        /* Cancel check every 64 lines */
        if ((i & 63) == 0 && s->cancel)
            break;

        if (!line)
        {
            s->scanned_lines++;
            continue;
        }

        line_len = (int)strlen(line);

        /* Search for pattern in this line */
        pos = 0;
        while (pos <= line_len - s->pat_len)
        {
            int match;

            if (s->case_sensitive)
                match = (memmemstr(line + pos, line_len - pos, s->pattern, s->pat_len) >= 0);
            else
                match = (memcasestr(line + pos, line_len - pos, s->pattern, s->pat_len) >= 0);

            if (match)
            {
                /* Find exact position */
                int exact_pos;

                if (s->case_sensitive)
                    exact_pos = pos + memmemstr(line + pos, line_len - pos, s->pattern, s->pat_len);
                else
                    exact_pos = pos + memcasestr(line + pos, line_len - pos, s->pattern, s->pat_len);

                /* Allocate preview buffer */
                preview_buf_size = preview_chars + 1;

                if (preview_buf_size < SEARCH_PREVIEW_MAX)
                    preview_buf_size = SEARCH_PREVIEW_MAX;

                preview_buf = (char *)malloc(preview_buf_size);

                if (preview_buf)
                {
                    extract_preview(line, line_len, exact_pos, s->pat_len, preview_buf, preview_chars);

                    if (!record_hit(s, i, exact_pos, preview_buf))
                    {
                        /* Cap reached */
                        free(preview_buf);
                        break;
                    }

                    free(preview_buf);
                }
                else
                {
                    /* No preview buffer, still record hit */
                    if (!record_hit(s, i, exact_pos, ""))
                        break;
                }

                /* Move past this match to find next one */
                pos = exact_pos + s->pat_len;
            }
            else
            {
                break;
            }
        }

        s->scanned_lines++;
    }

    if (s->cancel)
        return -1;

    return s->n_hits - hits_before;
}
