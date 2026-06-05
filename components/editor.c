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

/* editor.c -- Text editor with wchar_t internal representation */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include "editor.h"
#include "../core/utf8.h"
#include <wctype.h>
#include "../core/charset.h"

/* Line: dynamic wchar_t array */
typedef struct
{
    wchar_t *wcs; /* malloc'd, always NUL-terminated */
    int len;      /* character count (not bytes) */
    int cap;      /* allocated wchar_t slots */
} EdLine;

struct Ed
{
    EdLine **lines;
    int count, alloc;
    int row, col; /* col = character index */
    int top, page;
    int insert_mode, modified;
    EdBlock block;
    wchar_t *killbuf; /* malloc'd wchar_t copy */
    int killlen;

    /* Multi-level undo: bounded ring of UTF-8 snapshots */
    struct UndoEntry
    {
        char *text; /* malloc'd UTF-8 snapshot, owned */
        int row, col;
    } *undo_stack;

    int undo_top; /* number of entries in stack (0..undo_max) */
    int undo_cap; /* allocated capacity */
    int undo_max; /* maximum depth (default 50) */

    /* Redo stack */
    struct UndoEntry *redo_stack;
    int redo_top;
    int redo_cap;
    int redo_max;
};

#define INIT_ALLOC 256

static EdLine *line_new(const wchar_t *src, int len)
{
    EdLine *ln = (EdLine *)malloc(sizeof(EdLine));

    if (!ln)
        return NULL;

    ln->cap = len + 16;
    ln->wcs = (wchar_t *)malloc((size_t)ln->cap * sizeof(wchar_t));

    if (!ln->wcs)
    {
        free(ln);
        return NULL;
    }

    if (len > 0)
        memcpy(ln->wcs, src, (size_t)len * sizeof(wchar_t));

    ln->wcs[len] = L'\0';
    ln->len = len;

    return ln;
}

static EdLine *line_empty()
{
    return line_new(L"", 0);
}

static void line_free(EdLine *ln)
{
    if (!ln)
        return;

    free(ln->wcs);
    free(ln);
}

static int line_grow(EdLine *ln, int need)
{
    int nc;
    wchar_t *t;

    if (ln->cap > need + 1)
        return 0;

    nc = need + 64;

    t = (wchar_t *)realloc(ln->wcs, (size_t)nc * sizeof(wchar_t));

    if (!t)
        return -1;

    ln->wcs = t;
    ln->cap = nc;

    return 0;
}

static int line_insert(EdLine *ln, int pos, wchar_t ch)
{
    if (pos < 0)
        pos = 0;

    if (pos > ln->len)
        pos = ln->len;

    if (line_grow(ln, ln->len + 1) != 0)
        return -1;

    wmemmove(&ln->wcs[pos + 1], &ln->wcs[pos], (size_t)(ln->len - pos + 1));

    ln->wcs[pos] = ch;
    ln->len++;

    return 0;
}

static int line_delete(EdLine *ln, int pos)
{
    if (pos < 0 || pos >= ln->len)
        return -1;

    wmemmove(&ln->wcs[pos], &ln->wcs[pos + 1], (size_t)(ln->len - pos));
    ln->len--;

    return 0;
}

static int line_delete_range(EdLine *ln, int pos, int n)
{
    if (pos < 0 || n <= 0 || pos >= ln->len)
        return -1;

    if (pos + n > ln->len)
        n = ln->len - pos;

    wmemmove(&ln->wcs[pos], &ln->wcs[pos + n], (size_t)(ln->len - pos - n + 1));
    ln->len -= n;

    return 0;
}

static void line_truncate(EdLine *ln, int pos)
{
    if (pos < 0)
        pos = 0;

    if (pos < ln->len)
    {
        ln->len = pos;
        ln->wcs[pos] = L'\0';
    }
}

static int line_append(EdLine *ln, const wchar_t *s, int slen)
{
    if (slen <= 0)
        return 0;

    if (line_grow(ln, ln->len + slen) != 0)
        return -1;

    wmemcpy(&ln->wcs[ln->len], s, (size_t)slen);

    ln->len += slen;
    ln->wcs[ln->len] = L'\0';

    return 0;
}

static int doc_grow(Ed *ed)
{
    int na;
    EdLine **t;

    if (ed->count < ed->alloc)
        return 0;

    na = ed->alloc > 0 ? ed->alloc * 2 : INIT_ALLOC;

    t = (EdLine **)realloc(ed->lines, (size_t)na * sizeof(EdLine *));

    if (!t)
        return -1;

    ed->lines = t;
    ed->alloc = na;

    return 0;
}

static int doc_insert_line(Ed *ed, int at, EdLine *ln)
{
    int i;

    if (doc_grow(ed) != 0)
    {
        line_free(ln);
        return -1;
    }

    if (at < 0)
        at = 0;

    if (at > ed->count)
        at = ed->count;

    for (i = ed->count; i > at; i--)
        ed->lines[i] = ed->lines[i - 1];

    ed->lines[at] = ln;
    ed->count++;

    return 0;
}

static EdLine *doc_remove_line(Ed *ed, int at)
{
    EdLine *ln;
    int i;

    if (at < 0 || at >= ed->count)
        return NULL;

    ln = ed->lines[at];

    for (i = at; i < ed->count - 1; i++)
        ed->lines[i] = ed->lines[i + 1];

    ed->count--;

    return ln;
}

static void doc_clear(Ed *ed)
{
    int i;

    for (i = 0; i < ed->count; i++)
        line_free(ed->lines[i]);

    ed->count = 0;
}

Ed *ed_new(void)
{
    Ed *ed = (Ed *)calloc(1, sizeof(Ed));

    if (!ed)
        return NULL;

    ed->insert_mode = 1;
    ed->page = 25;
    ed->undo_max = 50;
    ed->redo_max = 50;
    ed->lines = (EdLine **)malloc(INIT_ALLOC * sizeof(EdLine *));

    if (!ed->lines)
    {
        free(ed);
        return NULL;
    }

    ed->alloc = INIT_ALLOC;
    ed->lines[0] = line_empty();

    if (!ed->lines[0])
    {
        free(ed->lines);
        free(ed);
        return NULL;
    }

    ed->count = 1;

    return ed;
}

void ed_free(Ed *ed)
{
    int i;

    if (!ed)
        return;

    doc_clear(ed);

    free(ed->lines);
    free(ed->killbuf);

    if (ed->undo_stack)
    {
        for (i = 0; i < ed->undo_top; i++)
            free(ed->undo_stack[i].text);

        free(ed->undo_stack);
    }

    if (ed->redo_stack)
    {
        for (i = 0; i < ed->redo_top; i++)
            free(ed->redo_stack[i].text);

        free(ed->redo_stack);
    }

    free(ed);
}

void ed_load(Ed *ed, const char *utf8_text)
{
    const char *p, *start;

    if (!ed)
        return;

    doc_clear(ed);

    ed->row = ed->col = ed->top = 0;
    ed->modified = 0;
    ed->block.active = 0;

    if (!utf8_text || !*utf8_text)
    {
        EdLine *ln = line_empty();

        if (ln)
            doc_insert_line(ed, 0, ln);

        return;
    }

    /* Split by lines, convert each to wchar_t */
    p = utf8_text;

    while (*p)
    {
        wchar_t *wcs;
        int wlen;
        char *line_utf8;
        int blen;
        EdLine *ln;

        start = p;

        while (*p && *p != '\r' && *p != '\n')
            p++;

        blen = (int)(p - start);

        /* Extract line as temporary UTF-8 */
        line_utf8 = (char *)malloc((size_t)(blen + 1));

        if (!line_utf8)
        {
            if (*p == '\r')
                p++;

            if (*p == '\n')
                p++;

            continue;
        }

        memcpy(line_utf8, start, (size_t)blen);
        line_utf8[blen] = '\0';

        wcs = utf8_to_wcs(line_utf8, &wlen);
        free(line_utf8);

        if (wcs)
        {
            ln = line_new(wcs, wlen);
            free(wcs);

            if (ln)
                doc_insert_line(ed, ed->count, ln);
        }

        if (*p == '\r')
            p++;

        if (*p == '\n')
            p++;
    }

    if (ed->count == 0)
    {
        EdLine *ln = line_empty();

        if (ln)
            doc_insert_line(ed, 0, ln);
    }
}

char *ed_to_string(const Ed *ed)
{
    char **parts;
    int i = 0;
    int total = 0;
    char *out;
    char *p;

    if (!ed || ed->count == 0)
        return NULL;

    /* Convert each line to UTF-8, measure total */
    parts = (char **)malloc((size_t)ed->count * sizeof(char *));

    if (!parts)
        return NULL;

    for (i = 0; i < ed->count; i++)
    {
        parts[i] = wcs_to_utf8(ed->lines[i]->wcs, ed->lines[i]->len);

        if (!parts[i])
        {
            parts[i] = (char *)malloc(1);

            if (!parts[i])
            {
                int j;

                for (j = 0; j < i; j++)
                    free(parts[j]);

                free(parts);
                return NULL;
            }

            parts[i][0] = 0;
        }

        total += (int)strlen(parts[i]) + 1; /* +1 for \n */
    }

    out = (char *)malloc((size_t)(total + 1));

    if (!out)
    {
        for (i = 0; i < ed->count; i++)
            free(parts[i]);

        free(parts);
        return NULL;
    }

    p = out;

    for (i = 0; i < ed->count; i++)
    {
        int slen = (int)strlen(parts[i]);

        memcpy(p, parts[i], (size_t)slen);

        p += slen;
        *p++ = '\n';

        free(parts[i]);
    }

    *p = '\0';

    free(parts);

    return out;
}

/* Scroll + clamp */
void ed_set_page(Ed *ed, int v)
{
    if (ed && v > 0)
        ed->page = v;
}

void ed_ensure_visible(Ed *ed)
{
    if (!ed)
        return;

    if (ed->row < ed->top)
        ed->top = ed->row;
    else if (ed->row >= ed->top + ed->page)
        ed->top = ed->row - ed->page + 1;

    if (ed->top < 0)
        ed->top = 0;
}

static void clamp(Ed *ed)
{
    if (ed->row < 0)
        ed->row = 0;

    if (ed->row >= ed->count)
        ed->row = ed->count - 1;

    if (ed->row < 0)
        ed->row = 0;

    if (ed->col > ed->lines[ed->row]->len)
        ed->col = ed->lines[ed->row]->len;

    if (ed->col < 0)
        ed->col = 0;
}

void ed_set_pos(Ed *ed, int row, int col)
{
    if (!ed)
        return;

    ed->row = row;
    ed->col = col;

    clamp(ed);
}

/* Cursor movement */
void ed_move_up(Ed *ed)
{
    if (ed && ed->row > 0)
    {
        ed->row--;

        clamp(ed);
        ed_ensure_visible(ed);
    }
}

void ed_move_down(Ed *ed)
{
    if (ed && ed->row < ed->count - 1)
    {
        ed->row++;

        clamp(ed);
        ed_ensure_visible(ed);
    }
}

void ed_move_home(Ed *ed)
{
    if (!ed)
        return;

    if (ed->row < 0 || ed->row >= ed->count)
        return;

    ed->col = 0;
}

void ed_move_end(Ed *ed)
{
    EdLine *line;

    if (!ed)
        return;

    if (ed->row < 0 || ed->row >= ed->count)
        return;

    line = ed->lines[ed->row];

    if (!line)
        return;

    ed->col = line->len;
}

void ed_move_top(Ed *ed)
{
    if (ed)
    {
        ed->row = 0;
        ed->col = 0;
        ed->top = 0;
    }
}

void ed_move_bottom(Ed *ed)
{
    if (!ed)
        return;

    if (ed->count <= 0)
    {
        ed->row = 0;
        ed->col = 0;
        ed->top = 0;
        return;
    }

    ed->row = ed->count - 1;
    ed->col = 0;

    ed_ensure_visible(ed);
}

void ed_move_left(Ed *ed)
{
    if (!ed)
        return;

    if (ed->col > 0)
        ed->col--;

    else if (ed->row > 0)
    {
        ed->row--;
        ed->col = ed->lines[ed->row]->len;

        ed_ensure_visible(ed);
    }
}

void ed_move_right(Ed *ed)
{
    if (!ed)
        return;

    if (ed->col < ed->lines[ed->row]->len)
        ed->col++;
    else if (ed->row < ed->count - 1)
    {
        ed->row++;
        ed->col = 0;

        ed_ensure_visible(ed);
    }
}

void ed_move_pgup(Ed *ed, int pg)
{
    if (!ed)
        return;

    if (pg <= 0)
        pg = ed->page;

    ed->row -= pg;

    if (ed->row < 0)
        ed->row = 0;

    clamp(ed);
    ed_ensure_visible(ed);
}

void ed_move_pgdn(Ed *ed, int pg)
{
    if (!ed)
        return;

    if (pg <= 0)
        pg = ed->page;

    ed->row += pg;

    if (ed->row >= ed->count)
        ed->row = ed->count - 1;

    clamp(ed);
    ed_ensure_visible(ed);
}

void ed_goto_line(Ed *ed, int line)
{
    if (!ed)
        return;

    ed->row = line;

    if (ed->row < 0)
        ed->row = 0;

    if (ed->row >= ed->count)
        ed->row = ed->count - 1;

    ed->col = 0;
    ed_ensure_visible(ed);
}

static int is_wordch(wchar_t ch)
{
    /* ASCII alnum + underscore; non-ASCII as word chars */
    if (ch < 0x80)
        return isalnum((int)ch) || ch == '_';

    return 1; /* non-ASCII: treat as word char (accented letters etc) */
}

void ed_word_left(Ed *ed)
{
    wchar_t *w;

    if (!ed)
        return;

    w = ed->lines[ed->row]->wcs;

    if (ed->col == 0 && ed->row > 0)
    {
        ed->row--;
        ed->col = ed->lines[ed->row]->len;

        ed_ensure_visible(ed);
        return;
    }

    while (ed->col > 0 && !is_wordch(w[ed->col - 1]))
        ed->col--;

    while (ed->col > 0 && is_wordch(w[ed->col - 1]))
        ed->col--;
}

void ed_word_right(Ed *ed)
{
    EdLine *ln;

    if (!ed)
        return;

    ln = ed->lines[ed->row];

    if (ed->col >= ln->len && ed->row < ed->count - 1)
    {
        ed->row++;
        ed->col = 0;
        ed_ensure_visible(ed);
        return;
    }

    while (ed->col < ln->len && is_wordch(ln->wcs[ed->col]))
        ed->col++;

    while (ed->col < ln->len && !is_wordch(ln->wcs[ed->col]))
        ed->col++;
}

/* Editing */
int ed_insert_char(Ed *ed, wchar_t ch)
{
    EdLine *ln;

    if (!ed || ch == 0)
        return -1;

    ln = ed->lines[ed->row];

    if (ed->insert_mode)
    {
        if (line_insert(ln, ed->col, ch) != 0)
            return -1;
    }
    else
    {
        if (ed->col < ln->len)
        {
            ln->wcs[ed->col] = ch;
        }
        else
        {
            if (line_insert(ln, ed->col, ch) != 0)
                return -1;
        }
    }

    ed->col++;
    ed->modified = 1;

    return 0;
}

int ed_enter(Ed *ed)
{
    EdLine *ln;
    EdLine *nl;

    if (!ed)
        return -1;

    ln = ed->lines[ed->row];
    nl = line_new(&ln->wcs[ed->col], ln->len - ed->col);

    if (!nl)
        return -1;

    line_truncate(ln, ed->col);

    if (doc_insert_line(ed, ed->row + 1, nl) != 0)
        return -1;

    ed->row++;
    ed->col = 0;
    ed->modified = 1;
    ed_ensure_visible(ed);

    return 0;
}

int ed_backspace(Ed *ed)
{
    EdLine *ln;
    EdLine *prev;

    if (!ed)
        return -1;

    ln = ed->lines[ed->row];

    if (ed->col > 0)
    {
        line_delete(ln, ed->col - 1);
        ed->col--;
        ed->modified = 1;
    }
    else if (ed->row > 0)
    {
        prev = ed->lines[ed->row - 1];
        ed->col = prev->len;

        line_append(prev, ln->wcs, ln->len);
        line_free(doc_remove_line(ed, ed->row));

        ed->row--;
        ed->modified = 1;

        ed_ensure_visible(ed);
    }

    return 0;
}

int ed_delete(Ed *ed)
{
    EdLine *ln;
    EdLine *nxt;

    if (!ed)
        return -1;

    ln = ed->lines[ed->row];

    if (ed->col < ln->len)
    {
        line_delete(ln, ed->col);
        ed->modified = 1;
    }
    else if (ed->row < ed->count - 1)
    {
        nxt = ed->lines[ed->row + 1];

        line_append(ln, nxt->wcs, nxt->len);
        line_free(doc_remove_line(ed, ed->row + 1));

        ed->modified = 1;
    }

    return 0;
}

int ed_delete_line(Ed *ed)
{
    if (!ed)
        return -1;

    if (ed->count <= 1)
    {
        line_truncate(ed->lines[0], 0);
        ed->col = 0;
    }
    else
    {
        line_free(doc_remove_line(ed, ed->row));

        if (ed->row >= ed->count)
            ed->row = ed->count - 1;

        clamp(ed);
    }

    ed->modified = 1;
    ed_ensure_visible(ed);

    return 0;
}

int ed_delete_to_eol(Ed *ed)
{
    if (!ed)
        return -1;

    line_truncate(ed->lines[ed->row], ed->col);
    ed->modified = 1;

    return 0;
}

/* Delete from cursor backwards to start of previous word */
int ed_delete_word_left(Ed *ed)
{
    EdLine *ln;
    int target;
    wchar_t *w;

    if (!ed)
        return -1;

    if (ed->col == 0)
        return ed_backspace(ed);

    ln = ed->lines[ed->row];
    w = ln->wcs;
    target = ed->col;

    while (target > 0 && !is_wordch(w[target - 1]))
        target--;

    while (target > 0 && is_wordch(w[target - 1]))
        target--;

    while (ed->col > target)
    {
        line_delete(ln, ed->col - 1);
        ed->col--;
    }

    ed->modified = 1;

    return 0;
}

/* Delete from cursor forward through end of current word */
int ed_delete_word_right(Ed *ed)
{
    EdLine *ln;
    int target;

    if (!ed)
        return -1;

    ln = ed->lines[ed->row];

    if (ed->col >= ln->len)
        return ed_delete(ed);

    target = ed->col;

    while (target < ln->len && is_wordch(ln->wcs[target]))
        target++;

    while (target < ln->len && !is_wordch(ln->wcs[target]))
        target++;

    while (ln->len > ed->col && ed->col < target)
    {
        line_delete(ln, ed->col);
        target--;
    }

    ed->modified = 1;

    return 0;
}

int ed_duplicate_line(Ed *ed)
{
    EdLine *ln;
    EdLine *dup;

    if (!ed)
        return -1;

    ln = ed->lines[ed->row];
    dup = line_new(ln->wcs, ln->len);

    if (!dup)
        return -1;

    if (doc_insert_line(ed, ed->row + 1, dup) != 0)
        return -1;

    ed->row++;
    ed->modified = 1;
    ed_ensure_visible(ed);

    return 0;
}

int ed_insert_tab(Ed *ed, int ts)
{
    int sp, i;

    if (!ed || ts <= 0)
        return -1;

    sp = ts - (ed->col % ts);

    for (i = 0; i < sp; i++)
    {
        if (ed_insert_char(ed, L' ') != 0)
            return -1;
    }

    return 0;
}

int ed_paste_text(Ed *ed, const char *utf8_text)
{
    const char *p;
    uint32_t cp;

    if (!ed || !utf8_text)
        return -1;

    p = utf8_text;

    while (*p)
    {
        if (*p == '\r')
        {
            p++;
            continue;
        }

        if (*p == '\n')
        {
            ed_enter(ed);
            p++;

            continue;
        }

        cp = utf8_next(&p);

        if (cp > 0)
            ed_insert_char(ed, (wchar_t)cp);
    }

    ed_ensure_visible(ed);
    return 0;
}

/* Block operations */
void ed_block_anchor(Ed *ed)
{
    if (!ed)
        return;

    if (ed->block.active)
        ed->block.active = 0;
    else
    {
        ed->block.active = 1;
        ed->block.anchor_row = ed->row;
        ed->block.anchor_col = ed->col;
    }
}

void ed_block_clear(Ed *ed)
{
    if (ed)
        ed->block.active = 0;
}

static void block_range(const Ed *ed, int *r1, int *c1, int *r2, int *c2)
{
    if (ed->block.anchor_row < ed->row || (ed->block.anchor_row == ed->row && ed->block.anchor_col <= ed->col))
    {
        *r1 = ed->block.anchor_row;
        *c1 = ed->block.anchor_col;
        *r2 = ed->row;
        *c2 = ed->col;
    }
    else
    {
        *r1 = ed->row;
        *c1 = ed->col;
        *r2 = ed->block.anchor_row;
        *c2 = ed->block.anchor_col;
    }

    if (*r1 >= ed->count)
        *r1 = ed->count - 1;

    if (*r2 >= ed->count)
        *r2 = ed->count - 1;

    if (*c1 > ed->lines[*r1]->len)
        *c1 = ed->lines[*r1]->len;

    if (*c2 > ed->lines[*r2]->len)
        *c2 = ed->lines[*r2]->len;
}

/* Extract block as malloc'd wchar_t string */
static wchar_t *block_extract_wcs(const Ed *ed, int *out_len)
{
    int r1;
    int c1;
    int r2;
    int c2;
    int i;
    int need = 0;
    int pos = 0;
    wchar_t *buf;

    block_range(ed, &r1, &c1, &r2, &c2);

    for (i = r1; i <= r2; i++)
    {
        int s = (i == r1) ? c1 : 0, e = (i == r2) ? c2 : ed->lines[i]->len;

        if (s > ed->lines[i]->len)
            s = ed->lines[i]->len;

        if (e > ed->lines[i]->len)
            e = ed->lines[i]->len;

        need += (e > s) ? (e - s) : 0;

        if (i < r2)
            need++;
    }

    buf = (wchar_t *)malloc((size_t)(need + 1) * sizeof(wchar_t));

    if (!buf)
    {
        if (out_len)
            *out_len = 0;

        return NULL;
    }

    for (i = r1; i <= r2; i++)
    {
        int s = (i == r1) ? c1 : 0, e = (i == r2) ? c2 : ed->lines[i]->len;
        int ln;

        if (s > ed->lines[i]->len)
            s = ed->lines[i]->len;

        if (e > ed->lines[i]->len)
            e = ed->lines[i]->len;

        ln = e - s;

        if (ln < 0)
            ln = 0;

        if (ln > 0)
            wmemcpy(&buf[pos], &ed->lines[i]->wcs[s], (size_t)ln);

        pos += ln;

        if (i < r2)
            buf[pos++] = L'\n';
    }

    buf[pos] = L'\0';

    if (out_len)
        *out_len = pos;

    return buf;
}

int ed_block_copy(Ed *ed)
{
    wchar_t *t;
    int tlen;

    if (!ed || !ed->block.active)
        return -1;

    t = block_extract_wcs(ed, &tlen);

    if (!t)
        return -1;

    free(ed->killbuf);

    ed->killbuf = t;
    ed->killlen = tlen;
    ed->block.active = 0;

    return 0;
}

char *ed_block_get_utf8(const Ed *ed)
{
    wchar_t *wcs;
    int wcs_len;
    char *utf8;

    if (!ed || !ed->block.active)
        return NULL;

    wcs = block_extract_wcs(ed, &wcs_len);

    if (!wcs)
        return NULL;

    utf8 = wcs_to_utf8(wcs, wcs_len);

    free(wcs);

    return utf8;
}

int ed_block_cut(Ed *ed)
{
    int r1;
    int c1;
    int r2;
    int c2;
    int i;

    if (!ed || !ed->block.active)
        return -1;

    block_range(ed, &r1, &c1, &r2, &c2);
    ed_save_undo(ed);

    if (ed_block_copy(ed) != 0)
        return -1;

    if (r1 == r2)
    {
        if (c1 < c2)
            line_delete_range(ed->lines[r1], c1, c2 - c1);
    }
    else
    {
        EdLine *first = ed->lines[r1], *last = ed->lines[r2];
        line_truncate(first, c1);

        if (c2 < last->len)
            line_append(first, &last->wcs[c2], last->len - c2);

        for (i = 0; i < r2 - r1 - 1 && ed->count > r1 + 1; i++)
            line_free(doc_remove_line(ed, r1 + 1));

        line_free(doc_remove_line(ed, r1 + 1));
    }

    ed->row = r1;
    ed->col = c1;

    clamp(ed);

    ed->modified = 1;
    ed_ensure_visible(ed);

    return 0;
}

int ed_block_delete(Ed *ed)
{
    int r1;
    int c1;
    int r2;
    int c2;
    int i;

    if (!ed || !ed->block.active)
        return -1;

    block_range(ed, &r1, &c1, &r2, &c2);

    if (r1 == r2)
    {
        if (c1 < c2)
            line_delete_range(ed->lines[r1], c1, c2 - c1);
    }
    else
    {
        EdLine *first = ed->lines[r1], *last = ed->lines[r2];
        line_truncate(first, c1);

        if (c2 < last->len)
            line_append(first, &last->wcs[c2], last->len - c2);

        for (i = 0; i < r2 - r1 - 1 && ed->count > r1 + 1; i++)
            line_free(doc_remove_line(ed, r1 + 1));

        line_free(doc_remove_line(ed, r1 + 1));
    }

    ed->row = r1;
    ed->col = c1;

    clamp(ed);

    ed->modified = 1;
    ed_ensure_visible(ed);

    return 0;
}

int ed_block_paste(Ed *ed)
{
    wchar_t *kb;
    int i;

    if (!ed || !ed->killbuf || !ed->killlen)
        return -1;

    ed_save_undo(ed);
    kb = ed->killbuf;

    for (i = 0; i < ed->killlen; i++)
    {
        if (kb[i] == L'\n')
            ed_enter(ed);
        else
            ed_insert_char(ed, kb[i]);
    }

    return 0;
}

/* Redo clear helper */
static void redo_clear(Ed *ed)
{
    int i;

    if (!ed->redo_stack)
        return;

    for (i = 0; i < ed->redo_top; i++)
        free(ed->redo_stack[i].text);

    free(ed->redo_stack);

    ed->redo_stack = NULL;
    ed->redo_top = 0;
    ed->redo_cap = 0;
}

/* Undo */
/* Push snapshot onto undo stack (drops oldest when full) */
void ed_save_undo(Ed *ed)
{
    char *snap;

    if (!ed || ed->undo_max <= 0)
        return;

    /* Clear redo stack on new edit */
    redo_clear(ed);

    /* Grow capacity if needed */
    if (ed->undo_top >= ed->undo_cap)
    {
        int nc = ed->undo_cap > 0 ? ed->undo_cap * 2 : 8;
        struct UndoEntry *t;

        if (nc > ed->undo_max)
            nc = ed->undo_max;

        if (nc <= ed->undo_cap)
        {
            /* At max: drop oldest by shifting down */
            free(ed->undo_stack[0].text);

            memmove(&ed->undo_stack[0], &ed->undo_stack[1], (size_t)(ed->undo_top - 1) * sizeof(struct UndoEntry));
            ed->undo_top--;
        }
        else
        {
            t = (struct UndoEntry *)realloc(ed->undo_stack, (size_t)nc * sizeof(struct UndoEntry));

            if (!t)
                return;

            ed->undo_stack = t;
            ed->undo_cap = nc;
        }
    }

    snap = ed_to_string(ed);

    if (!snap)
        return;

    ed->undo_stack[ed->undo_top].text = snap;
    ed->undo_stack[ed->undo_top].row = ed->row;
    ed->undo_stack[ed->undo_top].col = ed->col;
    ed->undo_top++;
}

int ed_undo(Ed *ed)
{
    struct UndoEntry *e;
    char *snap;

    if (!ed || ed->undo_top <= 0)
        return -1;

    /* Save current state to redo stack before undoing */
    snap = ed_to_string(ed);

    if (snap)
    {
        /* Push to redo stack (similar logic to ed_save_undo) */
        if (ed->redo_top >= ed->redo_cap)
        {
            int nc = ed->redo_cap > 0 ? ed->redo_cap * 2 : 8;
            struct UndoEntry *t;

            if (nc > ed->redo_max)
                nc = ed->redo_max;

            if (nc <= ed->redo_cap)
            {
                /* At max: drop oldest by shifting down */
                free(ed->redo_stack[0].text);
                memmove(&ed->redo_stack[0], &ed->redo_stack[1], (size_t)(ed->redo_top - 1) * sizeof(struct UndoEntry));
                ed->redo_top--;
            }
            else
            {
                t = (struct UndoEntry *)realloc(ed->redo_stack, (size_t)nc * sizeof(struct UndoEntry));
                if (!t)
                {
                    /* Realloc failed: drop snapshot to avoid dangling pointer */
                    free(snap);
                    snap = NULL;
                }
                else
                {
                    ed->redo_stack = t;
                    ed->redo_cap = nc;
                }
            }
        }

        if (snap)
        {
            ed->redo_stack[ed->redo_top].text = snap;
            ed->redo_stack[ed->redo_top].row = ed->row;
            ed->redo_stack[ed->redo_top].col = ed->col;
            ed->redo_top++;
        }
    }

    ed->undo_top--;
    e = &ed->undo_stack[ed->undo_top];

    ed_load(ed, e->text);

    ed->row = e->row;
    ed->col = e->col;

    clamp(ed);
    ed_ensure_visible(ed);

    free(e->text);
    e->text = NULL;

    return 0;
}

int ed_redo(Ed *ed)
{
    struct UndoEntry *e;
    char *snap;

    if (!ed || ed->redo_top <= 0)
        return -1;

    /* Save current state to undo stack before redoing */
    snap = ed_to_string(ed);

    if (snap)
    {
        /* Push to undo stack (similar logic to ed_save_undo) */
        if (ed->undo_top >= ed->undo_cap)
        {
            int nc = ed->undo_cap > 0 ? ed->undo_cap * 2 : 8;
            struct UndoEntry *t;

            if (nc > ed->undo_max)
                nc = ed->undo_max;

            if (nc <= ed->undo_cap)
            {
                /* At max: drop oldest by shifting down */
                free(ed->undo_stack[0].text);

                memmove(&ed->undo_stack[0], &ed->undo_stack[1], (size_t)(ed->undo_top - 1) * sizeof(struct UndoEntry));
                ed->undo_top--;
            }
            else
            {
                t = (struct UndoEntry *)realloc(ed->undo_stack, (size_t)nc * sizeof(struct UndoEntry));

                if (!t)
                {
                    /* NULL out snap to skip push and avoid freed pointer */
                    free(snap);
                    snap = NULL;
                }
                else
                {
                    ed->undo_stack = t;
                    ed->undo_cap = nc;
                }
            }
        }

        if (snap)
        {
            ed->undo_stack[ed->undo_top].text = snap;
            ed->undo_stack[ed->undo_top].row = ed->row;
            ed->undo_stack[ed->undo_top].col = ed->col;
            ed->undo_top++;
        }
    }

    ed->redo_top--;
    e = &ed->redo_stack[ed->redo_top];

    ed_load(ed, e->text);

    ed->row = e->row;
    ed->col = e->col;

    clamp(ed);
    ed_ensure_visible(ed);

    ed->modified = 1;

    free(e->text);
    e->text = NULL;

    return 0;
}

void ed_set_undo_levels(Ed *ed, int levels)
{
    if (!ed)
        return;

    if (levels < 1)
        levels = 1;

    if (levels > 1000)
        levels = 1000;

    ed->undo_max = levels;
    ed->redo_max = levels;

    /* Drop oldest entries if exceeding new max */
    while (ed->undo_top > levels)
    {
        free(ed->undo_stack[0].text);

        memmove(&ed->undo_stack[0], &ed->undo_stack[1], (size_t)(ed->undo_top - 1) * sizeof(struct UndoEntry));
        ed->undo_top--;
    }

    /* Drop oldest redo entries if exceeding new max */
    while (ed->redo_top > levels)
    {
        free(ed->redo_stack[0].text);

        memmove(&ed->redo_stack[0], &ed->redo_stack[1], (size_t)(ed->redo_top - 1) * sizeof(struct UndoEntry));
        ed->redo_top--;
    }
}

int ed_undo_depth(const Ed *ed)
{
    return ed ? ed->undo_top : 0;
}

int ed_redo_depth(const Ed *ed)
{
    return ed ? ed->redo_top : 0;
}

/* Mode + Query */
void ed_toggle_insert(Ed *ed)
{
    if (ed)
        ed->insert_mode = !ed->insert_mode;
}

void ed_set_modified(Ed *ed, int modified)
{
    if (ed)
        ed->modified = modified;
}

void ed_get_info(const Ed *ed, EdInfo *info)
{
    if (!info)
        return;

    memset(info, 0, sizeof(*info));

    if (!ed)
        return;

    info->row = ed->row;
    info->col = ed->col;
    info->top = ed->top;
    info->line_count = ed->count;
    info->insert_mode = ed->insert_mode;
    info->block = ed->block;
    info->modified = ed->modified;
}

const wchar_t *ed_line_wcs(const Ed *ed, int line)
{
    if (!ed || line < 0 || line >= ed->count)
        return NULL;

    return ed->lines[line]->wcs;
}

int ed_line_len(const Ed *ed, int line)
{
    if (!ed || line < 0 || line >= ed->count)
        return -1;

    return ed->lines[line]->len;
}

int ed_line_utf8(const Ed *ed, int line, char *buf, int bufsz)
{
    const wchar_t *wcs;
    int wlen;
    int i;
    int n;
    unsigned long cp;

    if (!ed || !buf || bufsz <= 0 || line < 0 || line >= ed->count)
    {
        if (buf && bufsz > 0)
            buf[0] = '\0';

        return -1;
    }

    wcs = ed->lines[line]->wcs;
    wlen = ed->lines[line]->len;

    /* Fast path: convert directly to buf (hot path for redraws) */
    n = 0;

    for (i = 0; i < wlen && n < bufsz - 4; i++)
    {
        cp = (unsigned long)wcs[i];

        if (cp < 0x80)
            buf[n++] = (char)cp;
        else if (cp < 0x800)
        {
            buf[n++] = (char)(0xC0 | (cp >> 6));
            buf[n++] = (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            buf[n++] = (char)(0xE0 | (cp >> 12));
            buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[n++] = (char)(0x80 | (cp & 0x3F));
        }
        else
        {
            buf[n++] = (char)(0xF0 | (cp >> 18));
            buf[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[n++] = (char)(0x80 | (cp & 0x3F));
        }
    }

    buf[n] = '\0';

    return n;
}

/* Higher-level text operations (no UI deps) */
int ed_search_forward(Ed *ed, const wchar_t *needle)
{
    int start_row;
    int start_col;
    int row;
    int col;
    int nlen;
    int i, found, k;
    const wchar_t *line;

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

                clamp(ed);
                ed_ensure_visible(ed);

                return 1;
            }
        }
    }

    return 0;
}

/* Detect leading quote prefix length (returns 0 if not a quote) */
static int detect_quote_prefix(const wchar_t *line)
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
    int first, last, i;
    int prefix_len;
    const wchar_t *line;
    wchar_t prefix[64];
    wchar_t *joined;
    size_t cap, used;

    int avail = 0;
    size_t pos = 0;
    size_t out_cap = 0;
    size_t out_used = 0;
    wchar_t *outw = NULL;
    char *outu;
    int k;

    if (!ed || width < 20 || ed->count <= 0)
        return -1;

    line = ed->lines[ed->row]->wcs;

    if (!line || !line[0])
        return -1;

    prefix_len = detect_quote_prefix(line);

    if (prefix_len > 0 && prefix_len < (int)(sizeof(prefix) / sizeof(prefix[0])))
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
            wchar_t *tmp_joined = (wchar_t *)realloc(joined, cap * sizeof(wchar_t));

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

        if (out_used + (size_t)prefix_len + (size_t)avail + 2 >= out_cap)
        {
            out_cap = (out_cap + (size_t)avail + 64) * 2;
            wchar_t *tmp = (wchar_t *)realloc(outw, out_cap * sizeof(wchar_t));

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

            for (j = line_len; j > avail / 3; j--)
            {
                if (joined[pos + j - 1] == L' ')
                {
                    break_at = j;
                    break;
                }
            }
        }

        for (k = 0; k < break_at; k++)
            outw[out_used++] = joined[pos + k];

        pos += (size_t)break_at;

        while (pos < used && joined[pos] == L' ')
            pos++;

        outw[out_used++] = L'\n';
    }

    outw[out_used] = L'\0';
    free(joined);

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

    return 0;
}

int ed_load_file_at_cursor(Ed *ed, const char *path, const char *charset_in)
{
    FILE *f;
    long sz;
    char *buf;
    size_t r;
    int needs_conv;

    if (!ed || !path || !path[0])
        return -1;

    /* If it's null, auto, or detects utf8, it's passed directly; otherwise, it's converted */
    needs_conv = charset_in && charset_in[0] && strcasecmp(charset_in, "AUTO") != 0 && strcasecmp(charset_in, "UTF-8") != 0 && strcasecmp(charset_in, "UTF8") != 0;

    f = fopen(path, "rb");

    if (!f)
        return -1;

    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 0 || sz > 8 * 1024 * 1024)
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
        /* Worst case for our supported single-byte charsets: each byte
         * expands to at most 3 UTF-8 bytes (CP437 box-drawing, accented
         * LATIN-1, etc.). Pad with +16 for safety / NUL */
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
        ed_save_undo(ed);
        ed_paste_text(ed, out);
        free(out);
    }
    else
    {
        ed_save_undo(ed);
        ed_paste_text(ed, buf);
        free(buf);
    }

    return 0;
}

int ed_export_block_to_file(Ed *ed, const char *path, const char *charset_out)
{
    int r1, c1, r2, c2;
    int i;
    FILE *f;
    char *utf8;
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

/* Search: find all matches of needle in editor, case-insensitive */
int ed_search_all(Ed *ed, const wchar_t *needle, int **out_rows, int **out_cols)
{
    int count = 0;
    int i, j;
    int nlen;
    const wchar_t *line;
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
int search_all_custom(Ed *ed, const wchar_t *needle, int case_sensitive, int whole_word, int **out_rows, int **out_cols)
{
    int count = 0;
    int capacity = 1024;
    int i, j;
    int needle_len = (int)wcslen(needle);

    if (!needle || needle_len == 0)
        return 0;

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
                        capacity *= 2;
                        *out_rows = realloc(*out_rows, capacity * sizeof(int));
                        *out_cols = realloc(*out_cols, capacity * sizeof(int));

                        if (!*out_rows || !*out_cols)
                        {
                            free(*out_rows);
                            free(*out_cols);
                            *out_rows = *out_cols = NULL;
                            return 0;
                        }
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
