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

#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "ed_attr.h"

/* Two runs carry the same visual state */
static int ea_same_state(const EdAttrRun *a, const EdAttrRun *b)
{
    return a->mask == b->mask && a->font_id == b->font_id && a->size == b->size;
}

/* A run in the default state carries no information */
static int ea_is_default(const EdAttrRun *r)
{
    return r->mask == 0 && r->font_id < 0 && r->size <= 0;
}

/* Grow the run array to hold at least n entries */
static int ea_reserve(EdLine *ln, int n)
{
    EdAttrRun *na = NULL;
    int nc;

    if (n <= ln->cap_attrs)
        return 0;

    nc = ln->cap_attrs > 0 ? ln->cap_attrs * 2 : 4;

    if (nc < n)
        nc = n;

    na = (EdAttrRun *)realloc(ln->attrs, (size_t)nc * sizeof(EdAttrRun));

    if (!na)
        return -1;

    ln->attrs = na;
    ln->cap_attrs = nc;

    return 0;
}

/* Drop empties and merge equal-state neighbours in place */
static void ea_normalize(EdLine *ln)
{
    int r = 0;
    int w = 0;

    while (r < ln->n_attrs)
    {
        EdAttrRun cur = ln->attrs[r++];

        if (cur.end <= cur.start || ea_is_default(&cur))
            continue;

        if (w > 0 && ln->attrs[w - 1].end == cur.start && ea_same_state(&ln->attrs[w - 1], &cur))
        {
            ln->attrs[w - 1].end = cur.end;
            continue;
        }

        ln->attrs[w++] = cur;
    }

    ln->n_attrs = w;
}

void ed_attr_line_free(EdLine *ln)
{
    if (!ln)
        return;

    free(ln->attrs);

    ln->attrs = NULL;
    ln->n_attrs = 0;
    ln->cap_attrs = 0;
}

unsigned short ed_attr_mask_at(const EdLine *ln, int col, short *font_id, short *size)
{
    int i;

    if (font_id)
        *font_id = -1;

    if (size)
        *size = 0;

    if (!ln || !ln->attrs)
        return 0;

    for (i = 0; i < ln->n_attrs; i++)
    {
        if (col < ln->attrs[i].start)
            break;

        if (col < ln->attrs[i].end)
        {
            if (font_id)
                *font_id = ln->attrs[i].font_id;

            if (size)
                *size = ln->attrs[i].size;

            return ln->attrs[i].mask;
        }
    }

    return 0;
}

int ed_attr_runs(const EdLine *ln, const EdAttrRun **out)
{
    if (out)
        *out = ln ? ln->attrs : NULL;

    return ln ? ln->n_attrs : 0;
}

int ed_attr_line_apply(EdLine *ln, int c1, int c2, unsigned short mask_set, unsigned short mask_clear, short font_id, short size)
{
    EdAttrRun *na = NULL;
    EdAttrRun seg;
    int nn = 0;
    int nc;
    int i;
    int pos;

    if (!ln)
        return -1;

    if (c1 < 0)
        c1 = 0;

    if (c2 > ln->len)
        c2 = ln->len;

    if (c1 >= c2)
        return 0;

    /* Worst case: every old run splits in three plus one new gap run */
    nc = ln->n_attrs * 3 + 2;
    na = (EdAttrRun *)malloc((size_t)nc * sizeof(EdAttrRun));

    if (!na)
        return -1;

    /* Walk old runs and the gaps between them, restyling inside [c1, c2) */
    pos = 0;
    i = 0;

    while (pos < ln->len)
    {
        int seg_end;

        if (i < ln->n_attrs && pos >= ln->attrs[i].start)
        {
            seg = ln->attrs[i];
            seg_end = seg.end;
        }
        else
        {
            /* Gap in the default state up to the next run or line end */
            seg.mask = 0;
            seg.font_id = -1;
            seg.size = 0;
            seg_end = i < ln->n_attrs ? ln->attrs[i].start : ln->len;
        }

        seg.start = pos;
        seg.end = seg_end;

        /* Clip the segment against the change interval and emit pieces */
        while (seg.start < seg.end)
        {
            EdAttrRun piece = seg;

            if (piece.start < c1)
            {
                if (piece.end > c1)
                    piece.end = c1;
            }
            else if (piece.start < c2)
            {
                if (piece.end > c2)
                    piece.end = c2;

                piece.mask = (unsigned short)((piece.mask | mask_set) & (unsigned short)~mask_clear);

                if (font_id >= 0)
                    piece.font_id = font_id;

                if (size > 0)
                    piece.size = size;
            }

            if (!ea_is_default(&piece))
                na[nn++] = piece;

            seg.start = piece.end;
        }

        pos = seg_end;

        if (i < ln->n_attrs && pos >= ln->attrs[i].end)
            i++;
    }

    free(ln->attrs);

    ln->attrs = na;
    ln->n_attrs = nn;
    ln->cap_attrs = nc;

    ea_normalize(ln);

    if (ln->n_attrs == 0)
        ed_attr_line_free(ln);

    return 0;
}

void ed_attr_on_insert(EdLine *ln, int pos, int n)
{
    int i;

    if (!ln || !ln->attrs || n <= 0)
        return;

    for (i = 0; i < ln->n_attrs; i++)
    {
        EdAttrRun *r = &ln->attrs[i];

        /* New text inherits the style of the character to its left */
        if (r->start < pos && pos <= r->end)
        {
            r->end += n;
        }
        else if (r->start >= pos)
        {
            r->start += n;
            r->end += n;
        }
    }
}

void ed_attr_on_delete(EdLine *ln, int pos, int n)
{
    int i;
    int d1 = pos;
    int d2;

    if (!ln || !ln->attrs || n <= 0)
        return;

    d2 = pos + n;

    for (i = 0; i < ln->n_attrs; i++)
    {
        EdAttrRun *r = &ln->attrs[i];

        /* Standard interval subtraction of [d1, d2) */
        if (r->start >= d2)
            r->start -= n;
        else if (r->start > d1)
            r->start = d1;

        if (r->end >= d2)
            r->end -= n;
        else if (r->end > d1)
            r->end = d1;
    }

    ea_normalize(ln);

    if (ln->n_attrs == 0)
        ed_attr_line_free(ln);
}

void ed_attr_on_truncate(EdLine *ln, int pos)
{
    int i;

    if (!ln || !ln->attrs)
        return;

    for (i = 0; i < ln->n_attrs; i++)
    {
        if (ln->attrs[i].start > pos)
            ln->attrs[i].start = pos;

        if (ln->attrs[i].end > pos)
            ln->attrs[i].end = pos;
    }

    ea_normalize(ln);

    if (ln->n_attrs == 0)
        ed_attr_line_free(ln);
}

int ed_attr_copy_slice(const EdLine *src, int start, int count, EdLine *dst)
{
    int i;

    if (!src || !dst || !src->attrs || count <= 0)
        return 0;

    for (i = 0; i < src->n_attrs; i++)
    {
        EdAttrRun r = src->attrs[i];

        if (r.end <= start || r.start >= start + count)
            continue;

        if (r.start < start)
            r.start = start;

        if (r.end > start + count)
            r.end = start + count;

        r.start -= start;
        r.end -= start;

        if (ea_reserve(dst, dst->n_attrs + 1) != 0)
            return -1;

        dst->attrs[dst->n_attrs++] = r;
    }

    ea_normalize(dst);

    return 0;
}

int ed_attr_append_slice(EdLine *dst, int at, const EdLine *src, int start, int count)
{
    int i;

    if (!dst || !src || !src->attrs || count <= 0)
        return 0;

    for (i = 0; i < src->n_attrs; i++)
    {
        EdAttrRun r = src->attrs[i];

        if (r.end <= start || r.start >= start + count)
            continue;

        if (r.start < start)
            r.start = start;

        if (r.end > start + count)
            r.end = start + count;

        r.start += at - start;
        r.end += at - start;

        if (ea_reserve(dst, dst->n_attrs + 1) != 0)
            return -1;

        dst->attrs[dst->n_attrs++] = r;
    }

    ea_normalize(dst);

    return 0;
}

int ed_attr_apply_range(struct Ed *ed, int r1, int c1, int r2, int c2, unsigned short mask_set, unsigned short mask_clear, short font_id, short size)
{
    int row;

    if (!ed || r1 > r2)
        return -1;

    for (row = r1; row <= r2 && row < ed->count; row++)
    {
        EdLine *ln = ed->lines[row];
        int a = row == r1 ? c1 : 0;
        int b = row == r2 ? c2 : ln->len;

        if (ed_attr_line_apply(ln, a, b, mask_set, mask_clear, font_id, size) != 0)
            return -1;
    }

    return 0;
}
