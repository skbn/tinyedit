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

/* layout.c -- line breaking and hyphenation, see layout.h */

#include <stdlib.h>
#include "layout.h"

void layout_opts_default(LayoutOpts *o)
{
    if (!o)
        return;

    o->hyphenate = 0;
    o->min_word = 5;
    o->min_left = 2;
    o->min_right = 3;
    o->max_ladder = 2;
    o->width = NULL;
    o->width_user = NULL;
    o->hyphen = NULL;
    o->hyphen_user = NULL;
}

static int char_width(const LayoutOpts *o, wchar_t ch, int col)
{
    int w;

    if (!o->width)
        return 1;

    w = o->width(o->width_user, ch, col);

    /* A zero or negative width would let a line grow forever */
    return w > 0 ? w : 1;
}

static int is_space(wchar_t c)
{
    return c == L' ' || c == L'\t';
}

/* A word already carrying a hyphen is never hyphenated again */
static int word_has_hyphen(const wchar_t *text, int from, int to)
{
    int i;

    for (i = from; i < to; i++)
    {
        if (text[i] == L'-')
            return 1;
    }

    return 0;
}

/* End of the word that starts at i */
static int word_end(const wchar_t *text, int len, int i)
{
    while (i < len && !is_space(text[i]))
        i++;

    return i;
}

/* Best break of word [ws,we) so the part before it plus '-' fits, 0 if none */
static int try_hyphen(const wchar_t *text, int ws, int we, int line_start, int width, const LayoutOpts *o)
{
    int breaks[64];
    int n;
    int wlen = we - ws;
    int best = 0;
    int i;

    if (!o->hyphenate || !o->hyphen)
        return 0;

    if (wlen < o->min_word)
        return 0;

    if (word_has_hyphen(text, ws, we))
        return 0;

    n = o->hyphen(o->hyphen_user, text + ws, wlen, breaks, (int)(sizeof(breaks) / sizeof(breaks[0])));

    if (n <= 0)
        return 0;

    /* Keep the longest break that still fits with the hyphen */
    for (i = 0; i < n; i++)
    {
        int bp = breaks[i];
        int col;
        int j;

        if (bp < o->min_left || wlen - bp < o->min_right)
            continue;

        /* Measure the line up to the break, then the hyphen itself */
        col = 0;

        for (j = line_start; j < ws + bp; j++)
            col += char_width(o, text[j], col);

        col += char_width(o, L'-', col);

        if (col <= width && bp > best)
            best = bp;
    }

    return best;
}

int layout_paragraph(const wchar_t *text, int len, int width, const LayoutOpts *o, LayoutLine *out, int max_lines)
{
    LayoutOpts def;
    int n = 0;
    int start = 0;
    int ladder = 0;

    if (!text || len < 0 || !out || max_lines <= 0)
        return -1;

    if (!o)
    {
        layout_opts_default(&def);
        o = &def;
    }

    if (width < 1)
        width = 1;

    /* An empty paragraph is still one line */
    if (len == 0)
    {
        out[0].start = 0;
        out[0].end = 0;
        out[0].brk = LB_PARA;

        return 1;
    }

    while (start < len)
    {
        int col = 0;
        int i = start;
        int last_space = -1;
        int fits_all = 1;
        int ws;
        int we;
        int bp = 0;

        /* Walk until the line is full, remembering the last space we passed */
        while (i < len)
        {
            int w = char_width(o, text[i], col);

            if (col + w > width)
            {
                fits_all = 0;
                break;
            }

            /* Only a plain space may be consumed, a tab restored as space would change the text */
            if (text[i] == L' ')
                last_space = i;

            col += w;
            i++;
        }

        if (n >= max_lines)
            return -1;

        if (fits_all)
        {
            /* The rest of the paragraph fits on this line */
            out[n].start = start;
            out[n].end = len;
            out[n].brk = LB_PARA;

            return n + 1;
        }

        /* The char that overflows is the space itself: break here, the words before it fit */
        if (text[i] == L' ')
        {
            out[n].start = start;
            out[n].end = i;
            out[n].brk = LB_SPACE;

            start = i + 1;
            ladder = 0;
            n++;

            continue;
        }

        /* Start of the word that overflows */
        ws = i;

        while (ws > start && !is_space(text[ws - 1]))
            ws--;

        we = word_end(text, len, ws);

        /* A hyphenated line always has a continuation, but a hyphen run has to stop somewhere */
        if (ladder < o->max_ladder)
            bp = try_hyphen(text, ws, we, start, width, o);

        if (bp > 0)
        {
            out[n].start = start;
            out[n].end = ws + bp;
            out[n].brk = LB_HYPHEN;

            start = ws + bp;
            ladder++;
            n++;

            continue;
        }

        ladder = 0;

        /* last_space must leave text behind or an empty line appears mid paragraph */
        if (last_space > start)
        {
            /* Ordinary break at a space, the space itself is consumed */
            out[n].start = start;
            out[n].end = last_space;
            out[n].brk = LB_SPACE;

            start = last_space + 1;
            n++;

            continue;
        }

        /* One single word wider than the line: cut it, never lose text */
        if (i == start)
            i = start + 1;

        out[n].start = start;
        out[n].end = i;
        out[n].brk = LB_WORD;

        start = i;
        n++;
    }

    /* Text ending in the consumed space leaves an empty final line */
    if (n > 0 && out[n - 1].brk != LB_PARA)
    {
        if (n >= max_lines)
            return -1;

        out[n].start = len;
        out[n].end = len;
        out[n].brk = LB_PARA;

        n++;
    }

    return n;
}

int layout_join(const wchar_t *text, const LayoutLine *lines, int n, wchar_t *out, int cap)
{
    int used = 0;
    int i;

    if (!text || !lines || !out || n < 0)
        return -1;

    for (i = 0; i < n; i++)
    {
        int j;

        for (j = lines[i].start; j < lines[i].end; j++)
        {
            if (used + 1 >= cap)
                return -1;

            out[used++] = text[j];
        }

        /* The space that the break ate has to come back */
        if (lines[i].brk == LB_SPACE)
        {
            if (used + 1 >= cap)
                return -1;

            out[used++] = L' ';
        }
    }

    out[used] = L'\0';

    return used;
}

/* Alignment indent, see layout.h. Pure math, no dependencies on the display */
int layout_align_indent(unsigned char align, int text_vw, int avail)
{
    int pad;

    /* EA_ALIGN_LEFT (0) and EA_ALIGN_JUST (3): no leading indent */
    if (align == 1) /* EA_ALIGN_CENTER */
        pad = (avail - text_vw) / 2;
    else if (align == 2) /* EA_ALIGN_RIGHT */
        pad = avail - text_vw;
    else
        return 0;

    return pad > 0 ? pad : 0;
}
