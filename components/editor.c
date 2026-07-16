/*
 * tinyedit - Text editor for AmigaOS
 *
 * Copyright (C) 2026 Tanausu M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

/* editor.c -- document core behind editor.h, packed codepoint lines */

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

#define ED_EMB_CHARS 8
#define ED_BUF_INITIAL_SIZE 1024
#define SAVE_BUF_SIZE 0xFFFF

/* Guard: never serialise a multi-MB paragraph for the rewrap snapshot */
#define ED_AUTO_REWRAP_SNAPSHOT_LINE_CAP 512

/* Inline text area lives right after the struct */
#if defined(PLATFORM_AMIGA)
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>

/* One puddle holds many lines, so a 100k line file costs few exec allocations */
#define ED_POOL_PUDDLE 0xFFFF
#define ED_POOL_THRESH 2048
#endif

#define ED_ARENA_CHUNK 0xFFFF

typedef struct EdArena
{
    struct EdArena *next;
    size_t used;
    size_t cap;
} EdArena;

#define ED_SLAB_SLOTS 256

typedef struct EdSlab
{
    struct EdSlab *next;
    char *mem;
    int used;
} EdSlab;

#if defined(PLATFORM_AMIGA)
/* Set while ed_free is deleting the pool, see line_free */
static int s_pool_teardown = 0;
#endif

static void ed_wcs_view_reset(Ed *ed);
static int ed_line_wrap_count(Ed *ed, EdLine *ln, int width);
static int prefix_rebuild(Ed *ed, int width, int max_line);
static int prefix_rebuild_from(Ed *ed, int from_line, int width);

static int s_tab_width = 8;

void ed_set_tab_width(int n)
{
    if (n >= 1 && n <= 16)
        s_tab_width = n;
}

int ed_get_tab_width(void)
{
    return s_tab_width;
}

/* Width in bytes needed to store one codepoint */
static int cw_for(unsigned int cp)
{
    if (cp < 0x100)
        return 1;

    if (cp < 0x10000)
        return 2;

    return 4;
}

/* Read the codepoint stored at character index i */
unsigned int ed_line_char(const EdLine *ln, int i)
{
    const unsigned char *p;

    if (!ln || !ln->text || i < 0 || i >= ln->len)
        return 0;

    p = (const unsigned char *)ln->text;

    if (ln->cw == 1)
        return p[i];

    if (ln->cw == 2)
        return ((const unsigned short *)ln->text)[i];

    return ((const unsigned int *)ln->text)[i];
}

/* Store one codepoint at character index i, cw must already fit */
static void line_put(EdLine *ln, int i, unsigned int cp)
{
    if (ln->cw == 1)
        ((unsigned char *)ln->text)[i] = (unsigned char)cp;
    else if (ln->cw == 2)
        ((unsigned short *)ln->text)[i] = (unsigned short)cp;
    else
        ((unsigned int *)ln->text)[i] = cp;
}

/* Zero terminator after len */
static void line_term(EdLine *ln)
{
    if (ln->text)
        line_put(ln, ln->len, 0);
}

/* Take memory for a line, from the document pool when there is one */
static void *line_mem_alloc(void *pool, size_t n)
{
#if defined(PLATFORM_AMIGA)
    if (pool)
        return (void *)AllocPooled((APTR)pool, (ULONG)n);
#else
    (void)pool;
#endif

    return malloc(n);
}

/* Give it back, FreePooled needs the exact size the block was taken with */
static void line_mem_free(void *pool, void *p, size_t n)
{
    if (!p)
        return;

#if defined(PLATFORM_AMIGA)
    if (pool)
    {
        FreePooled((APTR)pool, (APTR)p, (ULONG)n);
        return;
    }
#else
    (void)pool;
    (void)n;
#endif

    free(p);
}

/* Bump allocation, individual blocks are never freed */
static void *arena_alloc(Ed *ed, size_t n)
{
    EdArena *a = NULL;
    char *p = NULL;

    if (!ed)
        return NULL;

    /* Keep every block aligned for the widest codepoint */
    n = (n + 3u) & ~(size_t)3u;

    a = (EdArena *)ed->arena_head;

    if (!a || a->cap - a->used < n)
    {
        size_t body = n > ED_ARENA_CHUNK ? n : ED_ARENA_CHUNK;

        a = (EdArena *)line_mem_alloc(ed->mem_pool, sizeof(EdArena) + body);

        if (!a)
            return NULL;

        a->used = 0;
        a->cap = body;
        a->next = (EdArena *)ed->arena_head;

        ed->arena_head = a;
    }

    p = (char *)(a + 1) + a->used;
    a->used += n;

    return p;
}

/* Release the arena in whole chunks at teardown */
static void arena_free_all(Ed *ed)
{
    EdArena *a = NULL;

    if (!ed)
        return;

    a = (EdArena *)ed->arena_head;

    while (a)
    {
        EdArena *next = a->next;

        line_mem_free(ed->mem_pool, a, sizeof(EdArena) + a->cap);

        a = next;
    }

    ed->arena_head = NULL;
}

static size_t slab_slot_bytes(void)
{
    size_t n = sizeof(EdLine) + (size_t)ED_EMB_CHARS;

    /* keep the slots aligned like malloc would */
    return (n + 15u) & ~(size_t)15u;
}

static void *slab_alloc(Ed *ed)
{
    EdSlab *s = NULL;
    void *p = NULL;

    if (!ed)
        return NULL;

    if (ed->slab_free)
    {
        p = ed->slab_free;
        ed->slab_free = *(void **)p;

        return p;
    }

    s = (EdSlab *)ed->slab_head;

    if (!s || s->used >= ED_SLAB_SLOTS)
    {
        s = (EdSlab *)line_mem_alloc(ed->mem_pool, sizeof(EdSlab));

        if (!s)
            return NULL;

        s->mem = (char *)line_mem_alloc(ed->mem_pool, slab_slot_bytes() * ED_SLAB_SLOTS);

        if (!s->mem)
        {
            line_mem_free(ed->mem_pool, s, sizeof(EdSlab));
            return NULL;
        }

        s->used = 0;
        s->next = (EdSlab *)ed->slab_head;

        ed->slab_head = s;
    }

    p = s->mem + (size_t)s->used * slab_slot_bytes();
    s->used++;

    return p;
}

static void slab_release(Ed *ed, void *p)
{
    if (!ed || !p)
        return;

    /* Back on the free list, the chunk itself stays for the next line */
    *(void **)p = ed->slab_free;

    ed->slab_free = p;
}

static void slab_free_all(Ed *ed)
{
    EdSlab *s = NULL;

    if (!ed)
        return;

    s = (EdSlab *)ed->slab_head;

    while (s)
    {
        EdSlab *next = s->next;

        line_mem_free(ed->mem_pool, s->mem, slab_slot_bytes() * ED_SLAB_SLOTS);
        line_mem_free(ed->mem_pool, s, sizeof(EdSlab));

        s = next;
    }

    ed->slab_head = NULL;
    ed->slab_free = NULL;
}

/* The pool of the line, NULL when it came from plain malloc */
static void *line_pool(EdLine *ln)
{
#if defined(PLATFORM_AMIGA)
    return ln ? ln->mem_pool : NULL;
#else

    return NULL;
#endif
}

/* Byte size of a detached text block */
static size_t line_text_bytes(EdLine *ln)
{
    return (size_t)ln->cap * (size_t)ln->cw;
}

static void *line_emb_area(EdLine *ln)
{
    return (void *)((char *)ln + sizeof(EdLine));
}

static int line_text_is_emb(const EdLine *ln)
{
    return ln->emb > 0 && ln->text == (const void *)((const char *)ln + sizeof(EdLine));
}

/* New empty line with a small inline text area */
static EdLine *line_new(Ed *ed)
{
    EdLine *ln = NULL;
    int emb = ED_EMB_CHARS;

    ln = (EdLine *)slab_alloc(ed);

    if (!ln)
        return NULL;

    memset(ln, 0, sizeof(EdLine));

    ln->owner = ed;

#if defined(PLATFORM_AMIGA)
    ln->mem_pool = ed ? ed->mem_pool : NULL;
#endif

    ln->emb = emb;
    ln->text = line_emb_area(ln);
    ln->cap = emb - 1;
    ln->cw = 1;
    ln->len = 0;
    ln->wrap_count_cache = -1;
    ln->para_align = EA_ALIGN_LEFT;

    line_term(ln);

    return ln;
}

static void line_free(EdLine *ln)
{
    if (!ln)
        return;

    ed_attr_line_free(ln);

#if defined(PLATFORM_AMIGA)
    /* During teardown the pool frees every line at once, skip per line frees */
    if (ln->mem_pool && s_pool_teardown)
        return;
#endif

    if (ln->text && !line_text_is_emb(ln) && !ln->t_arena)
        line_mem_free(line_pool(ln), ln->text, line_text_bytes(ln));

    slab_release(ln->owner, ln);
}

/* Invalidate the per line caches after any content change */
static void line_touch(EdLine *ln)
{
    ln->wrap_count_cache = -1;
    ln->word_count = -1;
}

/* Ensure capacity for chars codepoints of width want_cw, repacks if needed */
static int line_need(EdLine *ln, int chars, int want_cw)
{
    int new_cw = ln->cw >= want_cw ? ln->cw : want_cw;
    int new_cap;
    void *nt = NULL;
    int i;

    if (new_cw == ln->cw && chars + 1 <= ln->cap)
        return 0;

    new_cap = ln->cap > 0 ? ln->cap : 16;

    while (new_cap < chars + 1)
        new_cap *= 2;

    nt = line_mem_alloc(line_pool(ln), (size_t)new_cap * (size_t)new_cw);

    if (!nt)
        return -1;

    /* Repack the existing chars into the new width */
    for (i = 0; i < ln->len; i++)
    {
        unsigned int cp = ed_line_char(ln, i);

        if (new_cw == 1)
            ((unsigned char *)nt)[i] = (unsigned char)cp;
        else if (new_cw == 2)
            ((unsigned short *)nt)[i] = (unsigned short)cp;
        else
            ((unsigned int *)nt)[i] = cp;
    }

    /* Arena text is never freed one by one, the abandoned bytes go at close */
    if (!line_text_is_emb(ln) && !ln->t_arena)
        line_mem_free(line_pool(ln), ln->text, line_text_bytes(ln));

    ln->t_arena = 0;
    ln->text = nt;
    ln->cap = new_cap;
    ln->cw = new_cw;

    line_term(ln);

    return 0;
}

/* Insert count codepoints from src at char index pos */
static int line_insert_cps(EdLine *ln, int pos, const unsigned int *src, int count)
{
    int want = 1;
    int i;

    for (i = 0; i < count; i++)
    {
        int w = cw_for(src[i]);

        if (w > want)
            want = w;
    }

    if (line_need(ln, ln->len + count, want) != 0)
        return -1;

    memmove((char *)ln->text + (size_t)(pos + count) * ln->cw, (char *)ln->text + (size_t)pos * ln->cw, (size_t)(ln->len - pos) * ln->cw);

    for (i = 0; i < count; i++)
        line_put(ln, pos + i, src[i]);

    ln->len += count;

    line_term(ln);
    ed_attr_on_insert(ln, pos, count);
    line_touch(ln);

    return 0;
}

/* Delete count codepoints at char index pos */
static void line_delete_cps(EdLine *ln, int pos, int count)
{
    if (pos < 0 || pos >= ln->len)
        return;

    if (pos + count > ln->len)
        count = ln->len - pos;

    memmove((char *)ln->text + (size_t)pos * ln->cw, (char *)ln->text + (size_t)(pos + count) * ln->cw, (size_t)(ln->len - pos - count) * ln->cw);

    ln->len -= count;

    line_term(ln);
    ed_attr_on_delete(ln, pos, count);
    line_touch(ln);
}

/* Truncate the line at char index pos */
static void line_truncate(EdLine *ln, int pos)
{
    if (pos < 0 || pos > ln->len)
        return;

    ln->len = pos;

    line_term(ln);
    ed_attr_on_truncate(ln, pos);
    line_touch(ln);
}

/* Append the slice [start, start+count) of src to dst with its attrs */
static int line_append_slice(EdLine *dst, const EdLine *src, int start, int count)
{
    int at = dst->len;
    int want = 1;
    int i;

    if (start < 0)
        start = 0;

    if (start + count > src->len)
        count = src->len - start;

    if (count <= 0)
        return 0;

    for (i = 0; i < count; i++)
    {
        int w = cw_for(ed_line_char(src, start + i));

        if (w > want)
            want = w;
    }

    if (line_need(dst, dst->len + count, want) != 0)
        return -1;

    for (i = 0; i < count; i++)
        line_put(dst, at + i, ed_line_char(src, start + i));

    dst->len += count;

    line_term(dst);
    ed_attr_append_slice(dst, at, src, start, count);
    line_touch(dst);

    return 0;
}

/* Deep copy of the slice [start, start+count) keeping attrs and align */
static EdLine *line_clone_slice(Ed *ed, const EdLine *src, int start, int count)
{
    EdLine *ln = line_new(ed);

    if (!ln)
        return NULL;

    if (start < 0)
        start = 0;

    if (start + count > src->len)
        count = src->len - start;

    if (count > 0)
    {
        int want = 1;
        int i;

        for (i = 0; i < count; i++)
        {
            int w = cw_for(ed_line_char(src, start + i));

            if (w > want)
                want = w;
        }

        if (line_need(ln, count, want) != 0)
        {
            line_free(ln);
            return NULL;
        }

        for (i = 0; i < count; i++)
            line_put(ln, i, ed_line_char(src, start + i));

        ln->len = count;

        line_term(ln);
    }

    ln->para_align = src->para_align;
    ln->brk = (unsigned char)((start + count == src->len) ? src->brk : LB_PARA);

    ed_attr_copy_slice(src, start, count, ln);

    return ln;
}

/* Invalidate caches that depend on line content from a given row */
static void doc_dirty_from(Ed *ed, int row)
{
    if (ed->prefix_dirty_from < 0 || row < ed->prefix_dirty_from)
        ed->prefix_dirty_from = row;

    if (ed->syntax_state_dirty_from < 0 || row < ed->syntax_state_dirty_from)
        ed->syntax_state_dirty_from = row;

    ed->word_count_initialized = 0;

    ed_wcs_view_reset(ed);
}

/* Make room for count line pointers at row */
static int doc_make_room(Ed *ed, int row, int count)
{
    int i;

    if (ed->count + count > ed->alloc)
    {
        int na = ed->alloc > 0 ? ed->alloc : INIT_ALLOC;
        EdLine **t;

        while (na < ed->count + count)
            na *= 2;

        t = (EdLine **)realloc(ed->lines, (size_t)na * sizeof(EdLine *));

        if (!t)
            return -1;

        ed->lines = t;
        ed->alloc = na;
    }

    for (i = ed->count - 1; i >= row; i--)
        ed->lines[i + count] = ed->lines[i];

    for (i = 0; i < count; i++)
        ed->lines[row + i] = NULL;

    ed->count += count;

    return 0;
}

/* Insert an owned line at row */
static int doc_insert_line(Ed *ed, int row, EdLine *ln)
{
    if (doc_make_room(ed, row, 1) != 0)
        return -1;

    ed->lines[row] = ln;

    doc_dirty_from(ed, row);

    return 0;
}

/* Remove and free count lines at row without inheriting the previous line's break flag */
static void doc_remove_lines_raw(Ed *ed, int row, int count)
{
    int i;

    if (row < 0 || row >= ed->count)
        return;

    if (row + count > ed->count)
        count = ed->count - row;

    for (i = 0; i < count; i++)
    {
        if (ed->lines[row + i])
            line_free(ed->lines[row + i]);
    }

    for (i = row; i + count < ed->count; i++)
        ed->lines[i] = ed->lines[i + count];

    ed->count -= count;

    doc_dirty_from(ed, row);
}

/* Remove and free count lines at row */
static void doc_remove_lines(Ed *ed, int row, int count)
{
    if (row < 0 || row >= ed->count)
        return;

    if (row + count > ed->count)
        count = ed->count - row;

    /* Let the previous line inherit the last removed line's break flag */
    if (count > 0 && row > 0)
        ed->lines[row - 1]->brk = ed->lines[row + count - 1]->brk;

    doc_remove_lines_raw(ed, row, count);
}

/* Build a line from decoded characters, used by the layout reflow */
EdLine *ed_line_from_wcs(Ed *ed, const wchar_t *w, int n)
{
    EdLine *ln = NULL;
    unsigned int *cps = NULL;
    int i;

    ln = line_new(ed);

    if (!ln)
        return NULL;

    if (n <= 0)
        return ln;

    cps = (unsigned int *)malloc((size_t)n * sizeof(unsigned int));

    if (!cps)
    {
        line_free(ln);
        return NULL;
    }

    for (i = 0; i < n; i++)
        cps[i] = (unsigned int)w[i];

    if (line_insert_cps(ln, 0, cps, n) != 0)
    {
        free(cps);
        line_free(ln);

        return NULL;
    }

    free(cps);

    return ln;
}

/* Display width of one char, must measure exactly like the painter */
int ed_layout_char_width(void *user, wchar_t ch, int col)
{
    int tab = ed_get_tab_width();

    if (tab <= 0)
        tab = 8;

    if (ch == L'\t')
        return tab - (col % tab);

    if (ch < 32)
        return 1;

    if (ch >= 0x1100 && (ch <= 0x115F || (ch >= 0x2E80 && ch <= 0xA4CF) || (ch >= 0xAC00 && ch <= 0xD7A3) || (ch >= 0xF900 && ch <= 0xFAFF) || (ch >= 0xFE30 && ch <= 0xFE6F) || (ch >= 0xFF00 && ch <= 0xFF60) || (ch >= 0xFFE0 && ch <= 0xFFE6)))
        return 2;

    return 1;
}

int ed_line_break(const Ed *ed, int line)
{
    if (!ed || line < 0 || line >= ed->count)
        return LB_PARA;

    return (int)ed->lines[line]->brk;
}

/* Join every marked break into logical lines, used when loading in soft mode */
void ed_join_breaks(Ed *ed)
{
    int read;
    int write;
    int failed = 0;

    if (!ed || ed->count <= 0)
        return;

    read = 0;
    write = 0;

    while (read < ed->count && !failed)
    {
        EdLine *head = ed->lines[read];

        if (write != read)
            ed->lines[write] = head;

        while (read < ed->count - 1 && head->brk != LB_PARA && !failed)
        {
            EdLine *next = ed->lines[read + 1];

            if (head->brk == LB_SPACE)
            {
                unsigned int sp = (unsigned int)' ';

                if (line_insert_cps(head, head->len, &sp, 1) != 0)
                {
                    /* Abort but keep what we've compacted so the doc stays coherent */
                    failed = 1;
                    break;
                }
            }

            if (line_append_slice(head, next, 0, next->len) != 0)
            {
                failed = 1;
                break;
            }

            head->brk = next->brk;

            line_free(next);
            ed->lines[read + 1] = NULL;
            read++;
        }

        if (failed)
            break;

        write++;
        read++;
    }

    /* Free any lines still dangling past the compacted region */
    while (read < ed->count)
    {
        if (ed->lines[read])
        {
            line_free(ed->lines[read]);
            ed->lines[read] = NULL;
        }

        read++;
    }

    ed->count = write;

    doc_dirty_from(ed, 0);

    ed_clamp(ed);
}

void ed_line_set_break(Ed *ed, int line, int brk)
{
    if (!ed || line < 0 || line >= ed->count)
        return;

    ed->lines[line]->brk = (unsigned char)brk;
}

void ed_line_drop_trailing_hyphen(Ed *ed, int line)
{
    EdLine *ln = NULL;

    if (!ed || line < 0 || line >= ed->count)
        return;

    ln = ed->lines[line];

    if (ln->len > 0 && ed_line_char(ln, ln->len - 1) == (unsigned int)'-')
    {
        line_delete_cps(ln, ln->len - 1, 1);
        doc_dirty_from(ed, line);
    }
}

EdLine *ed_line_clone(Ed *ed, const EdLine *src)
{
    return line_clone_slice(ed, src, 0, src->len);
}

void ed_line_destroy(EdLine *ln)
{
    line_free(ln);
}

/* Splice owned lines at row without mutating the previous break */
int ed_lines_splice(Ed *ed, int row, int n_remove, EdLine **insert, int n_insert)
{
    int i;

    if (!ed || row < 0 || row > ed->count)
        return -1;

    if (n_remove > 0)
        doc_remove_lines_raw(ed, row, n_remove);

    if (n_insert > 0)
    {
        if (doc_make_room(ed, row, n_insert) != 0)
            return -1;

        for (i = 0; i < n_insert; i++)
            ed->lines[row + i] = insert[i];
    }

    doc_dirty_from(ed, row);

    return 0;
}

void ed_clamp(Ed *ed)
{
    if (!ed)
        return;

    if (ed->count <= 0)
    {
        EdLine *ln = line_new(ed);

        if (ln && doc_insert_line(ed, 0, ln) != 0)
            line_free(ln);
    }

    if (ed->row < 0)
        ed->row = 0;

    if (ed->row >= ed->count)
        ed->row = ed->count - 1;

    if (ed->col < 0)
        ed->col = 0;

    if (ed->col > ed->lines[ed->row]->len)
        ed->col = ed->lines[ed->row]->len;

    if (ed->top < 0)
        ed->top = 0;

    if (ed->top >= ed->count)
        ed->top = ed->count - 1;
}

int ed_line_len(const Ed *ed, int line)
{
    if (!ed || line < 0 || line >= ed->count)
        return 0;

    return ed->lines[line]->len;
}

/* Decode one line into the rotating wide view ring */
const wchar_t *ed_line_wcs(const Ed *ed, int line)
{
    Ed *e = (Ed *)ed;
    EdLine *ln = NULL;
    int slot;
    int i;

    if (!ed || line < 0 || line >= ed->count)
        return L"";

    for (i = 0; i < ED_WCS_VIEW_SLOTS; i++)
    {
        if (e->wcs_view_line[i] == line && e->wcs_view[i])
            return e->wcs_view[i];
    }

    ln = e->lines[line];
    slot = e->wcs_view_next;
    e->wcs_view_next = (slot + 1) % ED_WCS_VIEW_SLOTS;

    if (e->wcs_view_cap[slot] < ln->len + 1)
    {
        wchar_t *t = (wchar_t *)realloc(e->wcs_view[slot], (size_t)(ln->len + 1) * sizeof(wchar_t));

        if (!t)
            return L"";

        e->wcs_view[slot] = t;
        e->wcs_view_cap[slot] = ln->len + 1;
    }

    for (i = 0; i < ln->len; i++)
        e->wcs_view[slot][i] = (wchar_t)ed_line_char(ln, i);

    e->wcs_view[slot][ln->len] = L'\0';
    e->wcs_view_line[slot] = line;

    return e->wcs_view[slot];
}

/* Encode one line as UTF-8, returns bytes written or -1 when short */
int ed_line_utf8(const Ed *ed, int line, char *buf, int bufsz)
{
    EdLine *ln = NULL;
    int used = 0;
    int i;

    if (!ed || line < 0 || line >= ed->count || !buf || bufsz <= 0)
        return -1;

    ln = ed->lines[line];

    for (i = 0; i < ln->len; i++)
    {
        char tmp[8];
        int n = utf8_encode((uint32_t)ed_line_char(ln, i), tmp);

        if (n <= 0)
            continue;

        if (used + n >= bufsz)
            return -1;

        memcpy(buf + used, tmp, (size_t)n);

        used += n;
    }

    buf[used] = '\0';

    return used;
}

/* Serialise lines [start, end) as UTF-8 with newline separators */
char *ed_range_to_string(const Ed *ed, int start, int end)
{
    size_t cap = ED_BUF_INITIAL_SIZE;
    size_t used = 0;
    char *out = NULL;
    int i;

    if (!ed)
        return NULL;

    if (start < 0)
        start = 0;

    if (end > ed->count)
        end = ed->count;

    out = (char *)malloc(cap);

    if (!out)
        return NULL;

    for (i = start; i < end; i++)
    {
        EdLine *ln = ed->lines[i];
        int j;

        for (j = 0; j < ln->len; j++)
        {
            char tmp[8];
            int n = utf8_encode((uint32_t)ed_line_char(ln, j), tmp);

            if (n <= 0)
                continue;

            if (used + (size_t)n + 2 > cap)
            {
                char *t;

                cap *= 2;
                t = (char *)realloc(out, cap);

                if (!t)
                {
                    free(out);
                    return NULL;
                }

                out = t;
            }

            memcpy(out + used, tmp, (size_t)n);

            used += (size_t)n;
        }

        /* Copied text matches what saving writes, the wrap hyphen included */
        if (ln->brk == LB_HYPHEN && i < end - 1)
            out[used++] = '-';

        /* A newline follows every line, including the last one */
        if (used + 2 > cap)
        {
            char *t = NULL;

            cap *= 2;
            t = (char *)realloc(out, cap);

            if (!t)
            {
                free(out);
                return NULL;
            }

            out = t;
        }

        out[used++] = '\n';
    }

    out[used] = '\0';

    return out;
}

char *ed_to_string(const Ed *ed)
{
    return ed_range_to_string(ed, 0, ed ? ed->count : 0);
}

/* mem_pool stays NULL in this core, everything is plain malloc */
static void *ed_pool_realloc(Ed *ed, void *p, size_t old_n, size_t new_n)
{
    return realloc(p, new_n);
}

/* Forget every decoded line in the view ring */
static void ed_wcs_view_reset(Ed *ed)
{
    int i;

    for (i = 0; i < ED_WCS_VIEW_SLOTS; i++)
        ed->wcs_view_line[i] = -1;
}

static void ed_wcs_view_free(Ed *ed)
{
    int i;

    for (i = 0; i < ED_WCS_VIEW_SLOTS; i++)
    {
        free(ed->wcs_view[i]);

        ed->wcs_view[i] = NULL;
        ed->wcs_view_line[i] = -1;
        ed->wcs_view_cap[i] = 0;
    }
}

void ed_prefix_invalidate(Ed *ed)
{
    if (!ed)
        return;

    ed->prefix_valid = 0;
    ed->prefix_dirty_from = -1;

    /* Structural change: decoded views may now map to the wrong line */
    ed_wcs_view_reset(ed);
}

void ed_prefix_invalidate_from(Ed *ed, int from_line)
{
    if (!ed)
        return;

    /* Any content edit reaches here, so drop stale decoded views */
    ed_wcs_view_reset(ed);

    /* Don't invalidate during snapshot mode (paste/undo/redo) */
    if (ed->undo_snapshot_mode)
        return;

    if (from_line < 0)
        from_line = 0;

    if (ed->prefix_dirty_from == -1 || from_line < ed->prefix_dirty_from)
        ed->prefix_dirty_from = from_line;
}

int ed_prefix_rebuild(Ed *ed, int width)
{
    if (!ed)
        return -1;

    return prefix_rebuild(ed, width, -1);
}

int ed_prefix_rebuild_to(Ed *ed, int width, int max_line)
{
    if (!ed)
        return -1;

    return prefix_rebuild(ed, width, max_line);
}

int ed_prefix_rebuild_range(Ed *ed, int width, int start_line, int end_line)
{
    int i;
    int range_size;
    int total;
    int old_start;
    int old_width;
    int need_capacity;

    if (!ed)
        return -1;

    if (start_line < 0)
        start_line = 0;

    if (end_line >= ed->count)
        end_line = ed->count - 1;

    if (start_line > end_line)
        return -1;

    range_size = end_line - start_line + 1;

    /* Ensure prefix array has enough capacity */
    need_capacity = range_size + 1;

    while (need_capacity > ed->prefix_alloc)
    {
        int na;
        int *t = NULL;

        na = ed->prefix_alloc > 0 ? ed->prefix_alloc * 2 : INIT_ALLOC;

        if (na < need_capacity)
            na = need_capacity;

        t = (int *)ed_pool_realloc(ed, ed->prefix, (size_t)ed->prefix_alloc * sizeof(int), (size_t)na * sizeof(int));

        if (!t)
            return -1;

        ed->prefix = t;
        ed->prefix_alloc = na;
    }

    old_start = ed->prefix_start;
    old_width = ed->prefix_width;

    /* Cold cache or width change: compute base by iterating 0..start_line-1 */
    if (!ed->prefix_valid || old_width != width)
    {
        /* COLD start: compute base by iterating 0..start_line-1. O(start) */
        ed->prefix_base = 0;

        for (i = 0; i < start_line; i++)
        {
            EdLine *ln = ed->lines[i];

            ed->prefix_base += ed_line_wrap_count(ed, ln, width);
        }
    }
    else
    {
        /* Cache exists with right width. Slide the base incrementally */
        if (start_line == old_start)
        {
            /* TODO */
        }
        else if (start_line > old_start)
        {
            /* Window moved forward -- add wrap_count of old_start..start_line-1 to base */
            for (i = old_start; i < start_line; i++)
            {
                EdLine *ln = ed->lines[i];

                ed->prefix_base += ed_line_wrap_count(ed, ln, width);
            }
        }
        else
        {
            /* start_line < old_start */
            /* Window moved backward: subtract if close, recompute from 0 if far */
            if (start_line >= 0 && old_start - start_line <= range_size * 2)
            {
                /* Close: subtract */
                for (i = start_line; i < old_start; i++)
                {
                    EdLine *ln = ed->lines[i];

                    ed->prefix_base -= ed_line_wrap_count(ed, ln, width);
                }

                if (ed->prefix_base < 0)
                    ed->prefix_base = 0; /* Safety net for corrupted state */
            }
            else
            {
                /* Far jump -- recompute base from 0. O(start_line) but rare */
                ed->prefix_base = 0;

                for (i = 0; i < start_line; i++)
                {
                    EdLine *ln = ed->lines[i];

                    ed->prefix_base += ed_line_wrap_count(ed, ln, width);
                }
            }
        }
    }

    /* Build prefix[]: visual rows from start_line to start_line+i */
    total = 0;

    for (i = start_line; i <= end_line; i++)
    {
        EdLine *ln = ed->lines[i];

        total += ed_line_wrap_count(ed, ln, width);
        ed->prefix[i - start_line] = total;
    }

    ed->prefix_valid = 1;
    ed->prefix_width = width;
    ed->prefix_start = start_line;
    ed->prefix_end = end_line;
    ed->prefix_dirty_from = -1; /* Fresh */

    return 0;
}

int ed_prefix_get(const Ed *ed, int line)
{
    if (!ed || line < 0)
        return 0;

    if (line >= ed->count)
        line = ed->count - 1;

    if (!ed->prefix_valid)
        return 0;

    /* Inside window: O(1) */
    if (line >= ed->prefix_start && line <= ed->prefix_end)
    {
        /* prefix[i] = sum of wrap_count(start_line .. start_line+i) */
        return ed->prefix_base + ed->prefix[line - ed->prefix_start];
    }

    /* Just before window: prefix_base == vrows up to prefix_start-1 */
    if (line == ed->prefix_start - 1)
        return ed->prefix_base;

    /* Outside window: return conservative estimate (caller should rebuild) */
    if (line > ed->prefix_end)
    {
        int last = ed->prefix_base + ed->prefix[ed->prefix_end - ed->prefix_start];

        return last + (line - ed->prefix_end);
    }
    else
    {
        /* line < ed->prefix_start - 1 */
        int est = ed->prefix_base - (ed->prefix_start - 1 - line);

        return est < 0 ? 0 : est;
    }
}

int ed_prefix_rebuild_from_row(Ed *ed, int from_row, int width)
{
    if (!ed)
        return -1;

    return prefix_rebuild_from(ed, from_row, width);
}

static int prefix_rebuild(Ed *ed, int width, int max_line)
{
    int i;
    int total;
    int from_line;
    int to_line;

    if (ed->prefix_valid && ed->prefix_width == width && ed->prefix_dirty_from == -1)
        return 0;

    /* If width changed or prefix not valid, rebuild everything */
    if (ed->prefix_width != width || !ed->prefix_valid)
    {
        from_line = 0;
    }
    else
    {
        /* Rebuild from dirty line onwards */
        from_line = (ed->prefix_dirty_from >= 0) ? ed->prefix_dirty_from : 0;
    }

    /* Determine how many lines to rebuild */
    if (max_line < 0 || max_line >= ed->count)
        to_line = ed->count;
    else
        to_line = max_line + 1;

    /* Ensure prefix has enough capacity */
    while (ed->count >= ed->prefix_alloc)
    {
        int na;
        int *t = NULL;

        na = ed->prefix_alloc > 0 ? ed->prefix_alloc * 2 : INIT_ALLOC;
        t = (int *)ed_pool_realloc(ed, ed->prefix, (size_t)ed->prefix_alloc * sizeof(int), (size_t)na * sizeof(int));

        if (!t)
            return -1;

        ed->prefix = t;
        ed->prefix_alloc = na;
    }

    /* Base total: visual rows before from_line (cached when possible) */
    if (from_line == 0)
    {
        total = 0;
    }
    else if (ed->prefix_valid && from_line - 1 < ed->prefix_alloc)
    {
        total = ed->prefix[from_line - 1];
    }
    else
    {
        /* Cold path: should only fire if prefix was just allocated empty */
        total = 0;

        for (i = 0; i < from_line; i++)
            total += ed_line_wrap_count(ed, ed->lines[i], width);
    }

    /* Calculate prefix sum from from_line to to_line */
    for (i = from_line; i < to_line; i++)
    {
        EdLine *ln = ed->lines[i];

        total += ed_line_wrap_count(ed, ln, width);
        ed->prefix[i] = total;
    }

    /* If we didn't rebuild everything, mark the rest as dirty */
    if (to_line < ed->count)
        ed->prefix_dirty_from = to_line;
    else
        ed->prefix_dirty_from = -1;

    ed->prefix_valid = 1;
    ed->prefix_width = width;
    ed->prefix_start = from_line;
    ed->prefix_end = to_line - 1;
    ed->prefix_base = total;

    return 0;
}

static int prefix_rebuild_from(Ed *ed, int from_line, int width)
{
    int i;
    int total;

    if (!ed || from_line < 0 || from_line >= ed->count)
        return -1;

    /* If width changed, rebuild everything */
    if (ed->prefix_width != width)
        return prefix_rebuild(ed, width, -1);

    /* Ensure prefix has enough capacity */
    while (ed->count >= ed->prefix_alloc)
    {
        int na;
        int *t = NULL;

        na = ed->prefix_alloc > 0 ? ed->prefix_alloc * 2 : INIT_ALLOC;

        t = (int *)ed_pool_realloc(ed, ed->prefix, (size_t)ed->prefix_alloc * sizeof(int), (size_t)na * sizeof(int));

        if (!t)
            return -1;

        ed->prefix = t;
        ed->prefix_alloc = na;
    }

    /* Same O(1) base lookup as prefix_rebuild - see comment there */
    if (from_line == 0)
    {
        total = 0;
    }
    else if (ed->prefix_valid && from_line - 1 < ed->prefix_alloc)
    {
        total = ed->prefix[from_line - 1];
    }
    else
    {
        total = 0;

        for (i = 0; i < from_line; i++)
            total += ed_line_wrap_count(ed, ed->lines[i], width);
    }

    /* Recalculate from from_line to end */
    for (i = from_line; i < ed->count; i++)
    {
        EdLine *ln = ed->lines[i];

        total += ed_line_wrap_count(ed, ln, width);
        ed->prefix[i] = total;
    }

    ed->prefix_valid = 1;
    ed->prefix_width = width;
    ed->prefix_start = from_line;
    ed->prefix_end = ed->count - 1;
    ed->prefix_base = total;

    return 0;
}

int ed_wrap_next(const wchar_t *line, int len, int width, int start)
{
    int vcol = 0;
    int logical_vcol = 0;
    int k = start;
    int last_space_after = -1;
    int i;

    if (width < 1)
        width = 1;

    /* Track logical column across the skipped prefix so tab stops align */
    for (i = 0; i < start && i < len; i++)
    {
        if (line[i] == L'\t')
            logical_vcol += s_tab_width - (logical_vcol % s_tab_width);
        else
        {
            int w = wcswidth(&line[i], 1);

            if (w != 2)
                w = 1;

            logical_vcol += w;
        }
    }

    while (k < len)
    {
        int w = 1;

        if (line[k] == L'\t')
            w = s_tab_width - (logical_vcol % s_tab_width);
        else
        {
            w = wcswidth(&line[k], 1);

            if (w != 2)
                w = 1;
        }

        if (vcol + w > width)
            break;

        vcol += w;
        logical_vcol += w;
        k++;

        if (k <= len && (line[k - 1] == L' ' || line[k - 1] == L'\t'))
            last_space_after = k;
    }

    if (k >= len)
        return len;

    if (last_space_after > start)
        return last_space_after;

    return k;
}

int ed_wrap_count(const wchar_t *line, int len, int width)
{
    int pos = 0;
    int rows = 1;

    if (len <= 0)
        return 1;

    for (;;)
    {
        int end = ed_wrap_next(line, len, width, pos);

        if (end >= len)
            break;

        if (end <= pos)
            end = pos + (width < 1 ? 1 : width);

        pos = end;
        rows++;
    }

    return rows;
}

static int ed_line_wrap_count(Ed *ed, EdLine *ln, int width)
{
    wchar_t *grown = NULL;
    int i;

    if (!ln || ln->len <= 0)
        return 1;

    if (ln->wrap_count_cache < 0 || ln->wrap_cache_width != width)
    {
        if (ln->len + 1 > ed->wrap_scratch_cap)
        {
            grown = (wchar_t *)realloc(ed->wrap_scratch, (size_t)(ln->len + 1) * sizeof(wchar_t));

            if (!grown)
                return 1;

            ed->wrap_scratch = grown;
            ed->wrap_scratch_cap = ln->len + 1;
        }

        for (i = 0; i < ln->len; i++)
            ed->wrap_scratch[i] = (wchar_t)ed_line_char(ln, i);

        ed->wrap_scratch[ln->len] = L'\0';
        ln->wrap_count_cache = ed_wrap_count(ed->wrap_scratch, ln->len, width);
        ln->wrap_cache_width = width;
    }

    return ln->wrap_count_cache;
}

/* Count words as runs of non space characters */
static int line_count_words(const EdLine *ln)
{
    int words = 0;
    int in_word = 0;
    int i;

    for (i = 0; i < ln->len; i++)
    {
        unsigned int cp = ed_line_char(ln, i);
        int sp = (cp == ' ' || cp == '\t' || iswspace((wint_t)cp));

        if (!sp && !in_word)
            words++;

        in_word = !sp;
    }

    return words;
}

int ed_word_count(Ed *ed)
{
    int i;

    if (!ed)
        return 0;

    if (!ed->word_count_initialized)
    {
        ed->word_count_total = 0;

        for (i = 0; i < ed->count; i++)
        {
            ed->lines[i]->word_count = line_count_words(ed->lines[i]);
            ed->word_count_total += ed->lines[i]->word_count;
        }

        ed->word_count_initialized = 1;
    }

    return ed->word_count_total;
}

Ed *ed_new(void)
{
    Ed *ed = (Ed *)calloc(1, sizeof(Ed));
    EdLine *ln = NULL;

    if (!ed)
        return NULL;

    ed->insert_mode = 1;
    ed->undo_max = 50;
    ed->pending_row = -1;

#if defined(PLATFORM_AMIGA)
    /* Lines come from a pool, one exec allocation per puddle instead of per line */
    ed->mem_pool = (void *)CreatePool(MEMF_ANY | MEMF_CLEAR, ED_POOL_PUDDLE, ED_POOL_THRESH);
#endif

    ed->redo_max = 50;
    ed->prefix_dirty_from = -1;
    ed->syntax_state_dirty_from = 0;
    ed->syntax_state_lang = -1;
    ed->page = 24;

    ed_wcs_view_reset(ed);

    ln = line_new(ed);

    if (!ln)
    {
        free(ed);
        return NULL;
    }

    if (doc_insert_line(ed, 0, ln) != 0)
    {
        line_free(ln);
        free(ed);

        return NULL;
    }

    return ed;
}

void ed_free(Ed *ed)
{
    int i;

    if (!ed)
        return;

#if defined(PLATFORM_AMIGA)
    /* From here on line_free only drops attrs, the pool takes the line memory */
    if (ed->mem_pool)
        s_pool_teardown = 1;
#endif

    for (i = 0; i < ed->count; i++)
    {
        if (ed->lines[i])
            line_free(ed->lines[i]);
    }

    free(ed->lines);
    free(ed->prefix);
    free(ed->syntax_state_cache);
    free(ed->killbuf);

    if (ed->killbuf_lines)
    {
        for (i = 0; i < ed->killbuf_line_count; i++)
        {
            if (ed->killbuf_lines[i])
                line_free(ed->killbuf_lines[i]);
        }

        free(ed->killbuf_lines);
    }

    undo_free_all(ed);

    ed_wcs_view_free(ed);
    free(ed->wrap_scratch);

    /* The line structs go back in whole chunks, not one by one */
    slab_free_all(ed);
    arena_free_all(ed);

#if defined(PLATFORM_AMIGA)
    if (ed->mem_pool)
    {
        DeletePool((APTR)ed->mem_pool);

        ed->mem_pool = NULL;
        s_pool_teardown = 0;
    }
#endif

    free(ed);
}

/* Build one packed line from a UTF-8 slice without trailing newline */
static EdLine *line_from_utf8(Ed *ed, const char *s, int nbytes)
{
    EdLine *ln = line_new(ed);
    const char *p = s;
    const char *end = s + nbytes;
    unsigned int cp;
    int count = 0;
    int cw = 1;
    int i;

    if (!ln)
        return NULL;

    /* How many chars and how wide they need to be stored */
    while (p < end)
    {
        cp = (unsigned int)utf8_next(&p);

        if (!cp)
            break;

        if (cw_for(cp) > cw)
            cw = cw_for(cp);

        count++;
    }

    /* Reserve exactly once: inline if it fits, else one arena block */
    if ((count + 1) * cw > ln->emb)
    {
        void *t = arena_alloc(ed, (size_t)(count + 1) * (size_t)cw);

        if (!t)
        {
            line_free(ln);
            return NULL;
        }

        ln->text = t;
        ln->t_arena = 1;
        ln->cap = count;
    }

    ln->cw = (unsigned char)cw;

    /* Decode straight into the packed buffer */
    p = s;

    for (i = 0; i < count; i++)
        line_put(ln, i, (unsigned int)utf8_next(&p));

    ln->len = count;

    line_term(ln);
    line_touch(ln);

    return ln;
}

/* Replace the whole document from a UTF-8 buffer */
void ed_load(Ed *ed, const char *utf8_text)
{
    const char *p = NULL;

    if (!ed)
        return;

    /* Drop the old content in one pass, per line removal from 0 is O(n squared) */
    if (ed->count > 0)
        doc_remove_lines(ed, 0, ed->count);

    p = utf8_text ? utf8_text : "";

    /* Reserve the line array up front: one guess beats dozens of reallocs */
    if (p[0])
    {
        const char *q = p;
        int lines = 1;

        while ((q = strchr(q, '\n')) != NULL)
        {
            lines++;
            q++;
        }

        if (lines > ed->alloc)
        {
            EdLine **t = (EdLine **)realloc(ed->lines, (size_t)lines * sizeof(EdLine *));

            if (t)
            {
                ed->lines = t;
                ed->alloc = lines;
            }
        }
    }

    /* A text ending in newline gets no extra empty line, the count must not drift */
    while (*p)
    {
        const char *nl = strchr(p, '\n');
        int seg = nl ? (int)(nl - p) : (int)strlen(p);
        EdLine *ln;

        if (seg > 0 && p[seg - 1] == '\r')
            seg--;

        ln = line_from_utf8(ed, p, seg);

        if (!ln || doc_insert_line(ed, ed->count, ln) != 0)
        {
            if (ln)
                line_free(ln);

            break;
        }

        if (!nl)
            break;

        p = nl + 1;
    }

    ed_clamp(ed);

    ed->row = 0;
    ed->col = 0;
    ed->top = 0;
    ed->modified = 0;
    ed->block.active = 0;

    ed_clear_undo_redo(ed);
    doc_dirty_from(ed, 0);
}

/* Read a whole stream into memory */
static char *read_stream(FILE *fp, size_t *out_len)
{
    size_t cap;
    size_t used = 0;
    char *buf = NULL;
    long known_size = -1;

    /* Pre-allocate the read buffer when the stream length is known */
    if (fseek(fp, 0, SEEK_END) == 0)
    {
        long pos = ftell(fp);

        if (fseek(fp, 0, SEEK_SET) == 0 && pos >= 0)
            known_size = pos;
    }

    if (known_size >= 0)
    {
        cap = (size_t)known_size + 1;
        buf = (char *)malloc(cap);

        /* Tolerate a short read. The realloc loop handles the tail */
        if (buf)
        {
            size_t got = fread(buf, 1, (size_t)known_size, fp);
            used = got;
        }
    }

    if (!buf)
    {
        cap = SAVE_BUF_SIZE;
        buf = (char *)malloc(cap);

        if (!buf)
            return NULL;
    }

    for (;;)
    {
        size_t got = fread(buf + used, 1, cap - used - 1, fp);

        used += got;

        if (got == 0)
            break;

        if (used + 1 >= cap)
        {
            char *t = NULL;

            cap *= 2;
            t = (char *)realloc(buf, cap);

            if (!t)
            {
                free(buf);
                return NULL;
            }

            buf = t;
        }
    }

    buf[used] = '\0';
    *out_len = used;

    return buf;
}

int ed_load_stream(Ed *ed, FILE *fp)
{
    size_t len;
    char *buf = NULL;

    if (!ed || !fp)
        return -1;

    buf = read_stream(fp, &len);

    if (!buf)
        return -1;

    ed_load(ed, buf);
    free(buf);

    return 0;
}

int ed_load_stream_charset(Ed *ed, FILE *fp, const char *charset)
{
    size_t len;
    char *raw = NULL;
    char *conv = NULL;
    int conv_cap;
    int n;

    if (!ed || !fp)
        return -1;

    raw = read_stream(fp, &len);

    if (!raw)
        return -1;

    if (!charset || !charset[0] || strcmp(charset_resolve(charset), "UTF-8") == 0)
    {
        ed_load(ed, raw);
        free(raw);

        return 0;
    }

    conv_cap = (int)len * 4 + 16;
    conv = (char *)malloc((size_t)conv_cap);

    if (!conv)
    {
        free(raw);
        return -1;
    }

    n = charset_body_to_utf8(charset, raw, (int)len, conv, conv_cap);

    if (n < 0)
    {
        free(conv);
        free(raw);

        return -1;
    }

    conv[n] = '\0';

    ed_load(ed, conv);
    free(conv);
    free(raw);

    return 0;
}

/* Write one UTF-8 chunk converting to charset_out when needed */
static int save_flush(FILE *fp, const char *charset_out, const char *buf, size_t used)
{
    int cap;
    char *tmp = NULL;
    int n;

    if (used == 0)
        return 0;

    if (!charset_out || !charset_out[0] || strcmp(charset_resolve(charset_out), "UTF-8") == 0)
        return fwrite(buf, 1, used, fp) == used ? 0 : -1;

    cap = (int)used * 4 + 16;
    tmp = (char *)malloc((size_t)cap);

    if (!tmp)
        return -1;

    n = charset_body_from_utf8(charset_out, buf, (int)used, tmp, cap);

    if (n < 0 || fwrite(tmp, 1, (size_t)n, fp) != (size_t)n)
    {
        free(tmp);
        return -1;
    }

    free(tmp);

    return 0;
}

int ed_save_to_file(const Ed *ed, const char *path, const char *charset_out)
{
    FILE *fp = NULL;
    char *buf = NULL;
    char *line_buf = NULL;
    size_t used = 0;
    int line_cap = ED_BUF_INITIAL_SIZE;
    int i;

    if (!ed || !path || !path[0])
        return -1;

    fp = fopen(path, "wb");

    if (!fp)
        return -1;

    buf = (char *)malloc(SAVE_BUF_SIZE);
    line_buf = (char *)malloc((size_t)line_cap);

    if (!buf || !line_buf)
    {
        free(buf);
        free(line_buf);
        fclose(fp);

        return -1;
    }

    for (i = 0; i < ed->count; i++)
    {
        int line_len;

        for (;;)
        {
            char *tmp;

            line_len = ed_line_utf8(ed, i, line_buf, line_cap);

            if (line_len >= 0)
                break;

            line_cap *= 2;
            tmp = (char *)realloc(line_buf, (size_t)line_cap);

            if (!tmp)
            {
                free(line_buf);
                free(buf);
                fclose(fp);

                return -1;
            }

            line_buf = tmp;
        }

        /* The hyphen lives in the break kind, written out here to keep FTN columns */
        if (ed->lines[i]->brk == LB_HYPHEN && line_len >= 0)
        {
            if (line_len + 2 > line_cap)
            {
                char *tmp = (char *)realloc(line_buf, (size_t)(line_len + 2));

                if (!tmp)
                {
                    free(line_buf);
                    free(buf);
                    fclose(fp);

                    return -1;
                }

                line_buf = tmp;
                line_cap = line_len + 2;
            }

            line_buf[line_len++] = '-';
            line_buf[line_len] = '\0';
        }

        if (used + (size_t)line_len + 1 > SAVE_BUF_SIZE)
        {
            if (save_flush(fp, charset_out, buf, used) != 0)
            {
                free(buf);
                free(line_buf);
                fclose(fp);

                return -1;
            }

            used = 0;
        }

        if ((size_t)line_len + 1 > SAVE_BUF_SIZE)
        {
            if (save_flush(fp, charset_out, line_buf, (size_t)line_len) != 0 || fwrite("\n", 1, 1, fp) != 1)
            {
                free(buf);
                free(line_buf);
                fclose(fp);

                return -1;
            }
        }
        else
        {
            memcpy(buf + used, line_buf, (size_t)line_len);

            used += (size_t)line_len;
            buf[used++] = '\n';
        }
    }

    if (save_flush(fp, charset_out, buf, used) != 0)
    {
        free(buf);
        free(line_buf);
        fclose(fp);

        return -1;
    }

    free(buf);
    free(line_buf);

    if (fclose(fp) != 0)
        return -1;

    return 0;
}

/* Replace count_to_remove lines at start with lines parsed from UTF-8 */
int ed_replace_range_from_utf8(Ed *ed, int start, int count_to_remove, const char *utf8_text)
{
    const char *p = NULL;
    int inserted = 0;

    if (!ed || start < 0 || start > ed->count)
        return -1;

    doc_remove_lines(ed, start, count_to_remove);

    p = utf8_text ? utf8_text : "";

    for (;;)
    {
        const char *nl = strchr(p, '\n');
        int seg = nl ? (int)(nl - p) : (int)strlen(p);
        EdLine *ln = NULL;

        if (seg > 0 && p[seg - 1] == '\r')
            seg--;

        ln = line_from_utf8(ed, p, seg);

        if (!ln || doc_insert_line(ed, start + inserted, ln) != 0)
        {
            if (ln)
                line_free(ln);

            break;
        }

        inserted++;

        if (!nl)
            break;

        p = nl + 1;
    }

    ed_clamp(ed);

    return inserted;
}

/* Replace count lines at start with deep copies of the given lines */
void ed_set_pos(Ed *ed, int row, int col)
{
    if (!ed)
        return;

    ed->row = row;
    ed->col = col;

    ed_clamp(ed);
}

void ed_get_info(const Ed *ed, EdInfo *info)
{
    if (!ed || !info)
        return;

    info->row = ed->row;
    info->col = ed->col;
    info->top = ed->top;
    info->line_count = ed->count;
    info->insert_mode = ed->insert_mode;
    info->block = ed->block;
    info->modified = ed->modified;
}

void ed_set_modified(Ed *ed, int modified)
{
    if (ed)
        ed->modified = modified;
}

int ed_get_hard_wrap(const Ed *ed)
{
    return ed ? ed->hard_wrap : 0;
}

void ed_set_hard_wrap(Ed *ed, int hard_wrap)
{
    if (ed)
        ed->hard_wrap = hard_wrap ? 1 : 0;
}

void ed_toggle_insert(Ed *ed)
{
    if (ed)
        ed->insert_mode = !ed->insert_mode;
}

void ed_set_word_move_mode(Ed *ed, int mode)
{
    if (ed)
        ed->word_move_mode = mode ? 1 : 0;
}

void ed_set_page(Ed *ed, int visible_rows)
{
    if (ed && visible_rows > 0)
        ed->page = visible_rows;
}

void ed_ensure_visible(Ed *ed)
{
    if (!ed)
        return;

    if (ed->row < ed->top)
        ed->top = ed->row;

    if (ed->page > 0 && ed->row >= ed->top + ed->page)
        ed->top = ed->row - ed->page + 1;

    if (ed->top < 0)
        ed->top = 0;
}

void ed_move_up(Ed *ed)
{
    if (!ed || ed->row <= 0)
        return;

    ed->row--;

    if (ed->col > ed->lines[ed->row]->len)
        ed->col = ed->lines[ed->row]->len;
}

void ed_move_down(Ed *ed)
{
    if (!ed || ed->row + 1 >= ed->count)
        return;

    ed->row++;

    if (ed->col > ed->lines[ed->row]->len)
        ed->col = ed->lines[ed->row]->len;
}

void ed_move_left(Ed *ed)
{
    if (!ed)
        return;

    if (ed->col > 0)
    {
        ed->col--;
    }
    else if (ed->row > 0)
    {
        ed->row--;
        ed->col = ed->lines[ed->row]->len;
    }
}

void ed_move_right(Ed *ed)
{
    if (!ed)
        return;

    if (ed->col < ed->lines[ed->row]->len)
    {
        ed->col++;
    }
    else if (ed->row + 1 < ed->count)
    {
        ed->row++;
        ed->col = 0;
    }
}

void ed_move_home(Ed *ed)
{
    if (ed)
        ed->col = 0;
}

void ed_move_end(Ed *ed)
{
    if (ed)
        ed->col = ed->lines[ed->row]->len;
}

void ed_move_pgup(Ed *ed, int page_size)
{
    if (!ed || page_size < 1)
        return;

    ed->row -= page_size;
    ed->top -= page_size;

    ed_clamp(ed);
}

void ed_move_pgdn(Ed *ed, int page_size)
{
    if (!ed || page_size < 1)
        return;

    ed->row += page_size;
    ed->top += page_size;

    ed_clamp(ed);
}

void ed_move_top(Ed *ed)
{
    if (!ed)
        return;

    ed->row = 0;
    ed->col = 0;
    ed->top = 0;
}

void ed_move_bottom(Ed *ed)
{
    if (!ed)
        return;

    ed->row = ed->count - 1;
    ed->col = ed->lines[ed->row]->len;

    ed_ensure_visible(ed);
}

void ed_goto_line(Ed *ed, int line)
{
    if (!ed)
        return;

    ed->row = line - 1;
    ed->col = 0;

    ed_clamp(ed);
    ed_ensure_visible(ed);
}

/* Word class: 0 space, 1 word char, 2 punctuation */
static int cp_class(Ed *ed, unsigned int cp)
{
    if (cp == ' ' || cp == '\t' || iswspace((wint_t)cp))
        return 0;

    if (ed->word_move_mode)
        return 1;

    if (cp == '_' || iswalnum((wint_t)cp))
        return 1;

    return 2;
}

void ed_word_left(Ed *ed)
{
    EdLine *ln = NULL;
    int cls;

    if (!ed)
        return;

    if (ed->col == 0)
    {
        ed_move_left(ed);
        return;
    }

    ln = ed->lines[ed->row];

    while (ed->col > 0 && cp_class(ed, ed_line_char(ln, ed->col - 1)) == 0)
        ed->col--;

    if (ed->col == 0)
        return;

    cls = cp_class(ed, ed_line_char(ln, ed->col - 1));

    while (ed->col > 0 && cp_class(ed, ed_line_char(ln, ed->col - 1)) == cls)
        ed->col--;
}

void ed_word_right(Ed *ed)
{
    EdLine *ln = NULL;
    int cls;

    if (!ed)
        return;

    ln = ed->lines[ed->row];

    if (ed->col >= ln->len)
    {
        ed_move_right(ed);
        return;
    }

    cls = cp_class(ed, ed_line_char(ln, ed->col));

    if (cls != 0)
    {
        while (ed->col < ln->len && cp_class(ed, ed_line_char(ln, ed->col)) == cls)
            ed->col++;
    }

    while (ed->col < ln->len && cp_class(ed, ed_line_char(ln, ed->col)) == 0)
        ed->col++;
}

int ed_insert_char(Ed *ed, wchar_t ch)
{
    EdLine *ln = NULL;
    unsigned int cp = (unsigned int)ch;

    if (!ed)
        return -1;

    if (ed->block.active)
        ed_block_delete(ed);

    /* Hard wrap reflows the paragraph afterwards, that delta covers this edit */
    if (ed->hard_wrap)
    {
        /* An edit outside the open capture settles it and starts a new one */
        if (ed->undo_snapshot_mode && !undo_pending_contains(ed, ed->row, 1))
        {
            ed_undo_settle(ed);
            ed->undo_snapshot_mode = 0;
        }

        if (!ed->undo_snapshot_mode)
            ed_auto_rewrap_capture_pre_snapshot(ed);

        ed->undo_snapshot_mode = 1;
    }

    undo_typing_hint(ed, 1);

    if (undo_begin(ed, ed->row, 1) != 0)
        return -1;

    ln = ed->lines[ed->row];

    /* Overwrite mode drops the character under the cursor first */
    if (!ed->insert_mode && ed->col < ln->len)
        line_delete_cps(ln, ed->col, 1);

    if (line_insert_cps(ln, ed->col, &cp, 1) != 0)
    {
        undo_abort(ed);
        return -1;
    }

    if (ed->input_mask)
        ed_attr_line_apply(ln, ed->col, ed->col + 1, ed->input_mask, 0, -1, 0);

    ed->col++;
    ed->modified = 1;

    doc_dirty_from(ed, ed->row);

    return undo_commit(ed, 1);
}

int ed_insert_tab(Ed *ed, int tabstop)
{
    return ed_insert_char(ed, L'\t');
}

int ed_enter(Ed *ed)
{
    EdLine *ln = NULL;
    EdLine *tail = NULL;

    if (!ed)
        return -1;

    if (ed->block.active)
        ed_block_delete(ed);

    undo_typing_hint(ed, 0);

    if (undo_begin(ed, ed->row, 1) != 0)
        return -1;

    ln = ed->lines[ed->row];
    tail = line_clone_slice(ed, ln, ed->col, ln->len - ed->col);

    if (!tail)
    {
        undo_abort(ed);
        return -1;
    }

    tail->para_align = ln->para_align;

    /* The split is a real break, the tail keeps how the line used to end */
    tail->brk = ln->brk;
    ln->brk = LB_PARA;

    line_truncate(ln, ed->col);

    if (doc_insert_line(ed, ed->row + 1, tail) != 0)
    {
        line_free(tail);
        undo_abort(ed);

        return -1;
    }

    ed->row++;
    ed->col = 0;
    ed->modified = 1;

    ed_ensure_visible(ed);

    /* One line became two */
    return undo_commit(ed, 2);
}

int ed_backspace(Ed *ed)
{
    EdLine *ln = NULL;

    if (!ed)
        return -1;

    if (ed->block.active)
        return ed_block_delete(ed);

    if (ed->hard_wrap)
    {
        /* An edit outside the open capture settles it and starts a new one */
        if (ed->undo_snapshot_mode && !undo_pending_contains(ed, ed->row, 1))
        {
            ed_undo_settle(ed);
            ed->undo_snapshot_mode = 0;
        }

        if (!ed->undo_snapshot_mode)
            ed_auto_rewrap_capture_pre_snapshot(ed);

        ed->undo_snapshot_mode = 1;
    }

    if (ed->col > 0)
    {
        undo_typing_hint(ed, 1);

        if (undo_begin(ed, ed->row, 1) != 0)
            return -1;

        ln = ed->lines[ed->row];

        line_delete_cps(ln, ed->col - 1, 1);

        ed->col--;
        ed->modified = 1;

        doc_dirty_from(ed, ed->row);

        return undo_commit(ed, 1);
    }

    if (ed->row > 0)
    {
        int join_col = ed->lines[ed->row - 1]->len;

        undo_typing_hint(ed, 0);

        /* Two lines become one, capture both */
        if (undo_begin(ed, ed->row - 1, 2) != 0)
            return -1;

        if (line_append_slice(ed->lines[ed->row - 1], ed->lines[ed->row], 0, ed->lines[ed->row]->len) != 0)
        {
            undo_abort(ed);
            return -1;
        }

        /* The merged line now ends where the line we swallowed ended */
        ed->lines[ed->row - 1]->brk = ed->lines[ed->row]->brk;

        doc_remove_lines(ed, ed->row, 1);

        ed->row--;
        ed->col = join_col;
        ed->modified = 1;

        return undo_commit(ed, 1);
    }

    return -1;
}

int ed_delete(Ed *ed)
{
    EdLine *ln = NULL;

    if (!ed)
        return -1;

    if (ed->block.active)
        return ed_block_delete(ed);

    if (ed->hard_wrap)
    {
        /* An edit outside the open capture settles it and starts a new one */
        if (ed->undo_snapshot_mode && !undo_pending_contains(ed, ed->row, 1))
        {
            ed_undo_settle(ed);
            ed->undo_snapshot_mode = 0;
        }

        if (!ed->undo_snapshot_mode)
            ed_auto_rewrap_capture_pre_snapshot(ed);

        ed->undo_snapshot_mode = 1;
    }

    ln = ed->lines[ed->row];

    if (ed->col < ln->len)
    {
        undo_typing_hint(ed, 1);

        if (undo_begin(ed, ed->row, 1) != 0)
            return -1;

        line_delete_cps(ln, ed->col, 1);

        ed->modified = 1;

        doc_dirty_from(ed, ed->row);

        return undo_commit(ed, 1);
    }

    if (ed->row + 1 < ed->count)
    {
        undo_typing_hint(ed, 0);

        if (undo_begin(ed, ed->row, 2) != 0)
            return -1;

        if (line_append_slice(ln, ed->lines[ed->row + 1], 0, ed->lines[ed->row + 1]->len) != 0)
        {
            undo_abort(ed);
            return -1;
        }

        ln->brk = ed->lines[ed->row + 1]->brk;

        doc_remove_lines(ed, ed->row + 1, 1);

        ed->modified = 1;

        return undo_commit(ed, 1);
    }

    return -1;
}

/* Snapshot helper for multi line edits, returns 0 when the group opened */
int ed_delete_line(Ed *ed)
{
    int cap_row;
    int cap_n_before;
    int cap_n_after;

    if (!ed || ed->count <= 0)
        return -1;

    if (ed->count == 1)
    {
        if (undo_begin(ed, 0, 1) != 0)
            return -1;

        line_truncate(ed->lines[0], 0);

        ed->col = 0;

        undo_commit(ed, 1);

        return 0;
    }

    /* Widen the undo capture to include the previous line's break flag */
    if (ed->row > 0)
    {
        cap_row = ed->row - 1;
        cap_n_before = 2; /* Prev line + line to delete */
        cap_n_after = 1;  /* Prev line survives */
    }
    else
    {
        cap_row = ed->row;
        cap_n_before = 1;
        cap_n_after = 0;
    }

    if (undo_begin(ed, cap_row, cap_n_before) != 0)
        return -1;

    doc_remove_lines(ed, ed->row, 1);

    ed->col = 0;

    ed_clamp(ed);
    undo_commit(ed, cap_n_after);

    return 0;
}

int ed_delete_to_eol(Ed *ed)
{
    EdLine *ln = NULL;

    if (!ed)
        return -1;

    ln = ed->lines[ed->row];

    if (ed->col >= ln->len)
        return -1;

    undo_typing_hint(ed, 0);

    if (undo_begin(ed, ed->row, 1) != 0)
        return -1;

    line_truncate(ln, ed->col);

    ed->modified = 1;

    doc_dirty_from(ed, ed->row);

    return undo_commit(ed, 1);
}

int ed_delete_word_left(Ed *ed)
{
    int start_col;
    int old_row;
    EdLine *ln = NULL;

    if (!ed || ed->col == 0)
        return ed_backspace(ed);

    start_col = ed->col;
    old_row = ed->row;

    ed_word_left(ed);

    if (ed->row != old_row)
    {
        ed->row = old_row;
        ed->col = start_col;

        return ed_backspace(ed);
    }

    undo_typing_hint(ed, 0);

    if (undo_begin(ed, ed->row, 1) != 0)
        return -1;

    ln = ed->lines[ed->row];

    line_delete_cps(ln, ed->col, start_col - ed->col);

    ed->modified = 1;

    doc_dirty_from(ed, ed->row);

    return undo_commit(ed, 1);
}

int ed_delete_word_right(Ed *ed)
{
    int start_col;
    int end_col;
    int old_row;
    EdLine *ln = NULL;

    if (!ed)
        return -1;

    ln = ed->lines[ed->row];

    if (ed->col >= ln->len)
        return ed_delete(ed);

    start_col = ed->col;
    old_row = ed->row;

    ed_word_right(ed);

    end_col = (ed->row != old_row) ? ln->len : ed->col;

    ed->row = old_row;
    ed->col = start_col;

    undo_typing_hint(ed, 0);

    if (undo_begin(ed, ed->row, 1) != 0)
        return -1;

    line_delete_cps(ln, start_col, end_col - start_col);

    ed->modified = 1;

    doc_dirty_from(ed, ed->row);

    return undo_commit(ed, 1);
}

int ed_duplicate_line(Ed *ed)
{
    EdLine *dup = NULL;

    if (!ed)
        return -1;

    if (undo_begin(ed, ed->row, 1) != 0)
        return -1;

    dup = line_clone_slice(ed, ed->lines[ed->row], 0, ed->lines[ed->row]->len);

    if (!dup || doc_insert_line(ed, ed->row + 1, dup) != 0)
    {
        if (dup)
            line_free(dup);

        undo_abort(ed);

        return -1;
    }

    ed->row++;

    undo_commit(ed, 2);

    return 0;
}

int ed_move_line_up(Ed *ed)
{
    EdLine *tmp = NULL;

    if (!ed || ed->row <= 0)
        return -1;

    if (undo_begin(ed, ed->row - 1, 2) != 0)
        return -1;

    tmp = ed->lines[ed->row - 1];
    ed->lines[ed->row - 1] = ed->lines[ed->row];
    ed->lines[ed->row] = tmp;

    ed->row--;

    doc_dirty_from(ed, ed->row);

    return undo_commit(ed, 2);
}

int ed_move_line_down(Ed *ed)
{
    EdLine *tmp = NULL;

    if (!ed || ed->row + 1 >= ed->count)
        return -1;

    if (undo_begin(ed, ed->row, 2) != 0)
        return -1;

    tmp = ed->lines[ed->row + 1];
    ed->lines[ed->row + 1] = ed->lines[ed->row];
    ed->lines[ed->row] = tmp;

    ed->row++;

    doc_dirty_from(ed, ed->row - 1);

    return undo_commit(ed, 2);
}

void ed_block_anchor(Ed *ed)
{
    if (!ed)
        return;

    ed->block.active = 1;
    ed->block.anchor_row = ed->row;
    ed->block.anchor_col = ed->col;
}

void ed_block_clear(Ed *ed)
{
    if (ed)
        ed->block.active = 0;
}

/* Ordered block range, returns 0 when empty */
static int block_range(const Ed *ed, int *r1, int *c1, int *r2, int *c2)
{
    if (!ed || !ed->block.active)
        return 0;

    if (ed->block.anchor_row == ed->row && ed->block.anchor_col == ed->col)
        return 0;

    if (ed->block.anchor_row < ed->row || (ed->block.anchor_row == ed->row && ed->block.anchor_col < ed->col))
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

    return 1;
}

char *ed_block_get_utf8(const Ed *ed)
{
    int r1, c1, r2, c2;

    if (!block_range(ed, &r1, &c1, &r2, &c2))
        return NULL;

    return ed_block_to_string((Ed *)ed, r1, c1, r2, c2);
}

char *ed_killbuf_get_utf8(const Ed *ed)
{
    char *out = NULL;
    size_t cap;
    size_t used = 0;
    int i;

    if (!ed || !ed->killbuf || ed->killlen <= 0)
        return NULL;

    cap = (size_t)ed->killlen * 4 + 1;
    out = (char *)malloc(cap);

    if (!out)
        return NULL;

    for (i = 0; i < ed->killlen; i++)
    {
        char tmp[8];
        int n = utf8_encode((uint32_t)ed->killbuf[i], tmp);

        if (n <= 0)
            continue;

        memcpy(out + used, tmp, (size_t)n);

        used += (size_t)n;
    }

    out[used] = '\0';

    return out;
}

/* Drop the rich clipboard and its plain mirror */
static void kill_drop(Ed *ed)
{
    int i;

    free(ed->killbuf);

    ed->killbuf = NULL;
    ed->killlen = 0;

    if (ed->killbuf_lines)
    {
        for (i = 0; i < ed->killbuf_line_count; i++)
        {
            if (ed->killbuf_lines[i])
                line_free(ed->killbuf_lines[i]);
        }

        free(ed->killbuf_lines);
    }

    ed->killbuf_lines = NULL;
    ed->killbuf_line_count = 0;
}

/* Copy the block into the rich clipboard plus its wide plain mirror */
int ed_block_copy(Ed *ed)
{
    int r1, c1, r2, c2;
    EdLine **v;
    wchar_t *flat;
    int n;
    int i;
    int used = 0;
    int total = 0;

    if (!block_range(ed, &r1, &c1, &r2, &c2))
        return -1;

    n = r2 - r1 + 1;
    v = (EdLine **)malloc((size_t)n * sizeof(EdLine *));

    if (!v)
        return -1;

    for (i = 0; i < n; i++)
    {
        int a = (i == 0) ? c1 : 0;
        int b = (r1 + i == r2) ? c2 : ed->lines[r1 + i]->len;

        v[i] = line_clone_slice(ed, ed->lines[r1 + i], a, b - a);

        if (!v[i])
        {
            while (--i >= 0)
                line_free(v[i]);

            free(v);

            return -1;
        }

        total += v[i]->len + 1;
    }

    flat = (wchar_t *)malloc((size_t)(total + 1) * sizeof(wchar_t));

    if (!flat)
    {
        for (i = 0; i < n; i++)
            line_free(v[i]);

        free(v);

        return -1;
    }

    for (i = 0; i < n; i++)
    {
        int j;

        for (j = 0; j < v[i]->len; j++)
            flat[used++] = (wchar_t)ed_line_char(v[i], j);

        if (i + 1 < n)
            flat[used++] = L'\n';
    }

    flat[used] = L'\0';

    kill_drop(ed);

    ed->killbuf_lines = v;
    ed->killbuf_line_count = n;
    ed->killbuf = flat;
    ed->killlen = used;

    return 0;
}

/* Remove the block joining the surviving head and tail */
static int block_delete_raw(Ed *ed, int r1, int c1, int r2, int c2)
{
    if (r1 == r2)
    {
        line_delete_cps(ed->lines[r1], c1, c2 - c1);
    }
    else
    {
        EdLine *head = ed->lines[r1];
        EdLine *tail = ed->lines[r2];

        line_truncate(head, c1);

        if (line_append_slice(head, tail, c2, tail->len - c2) != 0)
            return -1;

        doc_remove_lines(ed, r1 + 1, r2 - r1);
    }

    ed->row = r1;
    ed->col = c1;
    ed->block.active = 0;

    doc_dirty_from(ed, r1);

    return 0;
}

int ed_block_delete(Ed *ed)
{
    int r1, c1, r2, c2;

    if (!block_range(ed, &r1, &c1, &r2, &c2))
        return -1;

    if (undo_begin(ed, r1, r2 - r1 + 1) != 0)
        return -1;

    if (block_delete_raw(ed, r1, c1, r2, c2) != 0)
    {
        undo_abort(ed);

        return -1;
    }

    undo_commit(ed, 1);

    return 0;
}

int ed_block_cut(Ed *ed)
{
    if (ed_block_copy(ed) != 0)
        return -1;

    return ed_block_delete(ed);
}

/* Splice the rich clipboard lines in at the cursor */
static int paste_lines(Ed *ed, EdLine **src, int nsrc)
{
    EdLine *cur = NULL;
    EdLine *tail = NULL;
    int i;

    if (nsrc <= 0)
        return -1;

    cur = ed->lines[ed->row];
    tail = line_clone_slice(ed, cur, ed->col, cur->len - ed->col);

    if (!tail)
        return -1;

    line_truncate(cur, ed->col);

    if (line_append_slice(cur, src[0], 0, src[0]->len) != 0)
    {
        line_free(tail);
        return -1;
    }

    if (nsrc == 1)
    {
        ed->col = cur->len;

        if (line_append_slice(cur, tail, 0, tail->len) != 0)
        {
            line_free(tail);
            return -1;
        }

        line_free(tail);
        doc_dirty_from(ed, ed->row);

        return 0;
    }

    for (i = 1; i < nsrc; i++)
    {
        EdLine *cp = line_clone_slice(ed, src[i], 0, src[i]->len);

        if (!cp || doc_insert_line(ed, ed->row + i, cp) != 0)
        {
            if (cp)
                line_free(cp);

            line_free(tail);

            return -1;
        }
    }

    ed->row += nsrc - 1;
    ed->col = ed->lines[ed->row]->len;

    if (line_append_slice(ed->lines[ed->row], tail, 0, tail->len) != 0)
    {
        line_free(tail);
        return -1;
    }

    line_free(tail);
    doc_dirty_from(ed, ed->row - nsrc + 1);

    return 0;
}

int ed_block_paste(Ed *ed)
{
    int start;

    if (!ed)
        return -1;

    if (ed->block.active)
        ed_block_delete(ed);

    if (ed->killbuf_lines && ed->killbuf_line_count > 0)
    {
        int nsrc = ed->killbuf_line_count;
        start = ed->row;

        if (undo_begin(ed, start, 1) != 0)
            return -1;

        if (paste_lines(ed, ed->killbuf_lines, nsrc) != 0)
        {
            undo_abort(ed);

            return -1;
        }

        undo_commit(ed, nsrc);

        return 0;
    }

    if (ed->killbuf && ed->killlen > 0)
    {
        char *utf8 = ed_killbuf_get_utf8(ed);
        int rc;

        if (!utf8)
            return -1;

        rc = ed_paste_text_with_undo(ed, utf8);
        free(utf8);

        return rc;
    }

    return -1;
}

/* Insert UTF-8 text at the cursor without touching the undo stack */
int ed_paste_text(Ed *ed, const char *utf8_text)
{
    const char *p = NULL;
    EdLine *cur = NULL;
    EdLine *tail = NULL;
    int first = 1;

    if (!ed || !utf8_text)
        return -1;

    cur = ed->lines[ed->row];
    tail = line_clone_slice(ed, cur, ed->col, cur->len - ed->col);

    if (!tail)
        return -1;

    line_truncate(cur, ed->col);

    p = utf8_text;

    for (;;)
    {
        const char *nl = strchr(p, '\n');
        int seg = nl ? (int)(nl - p) : (int)strlen(p);
        const char *q = p;
        const char *end = p + seg;

        if (seg > 0 && p[seg - 1] == '\r')
            end--;

        if (!first)
        {
            EdLine *ln = line_new(ed);

            if (!ln || doc_insert_line(ed, ed->row + 1, ln) != 0)
            {
                if (ln)
                    line_free(ln);

                line_free(tail);

                return -1;
            }

            ed->row++;
            ed->col = 0;
        }

        while (q < end)
        {
            unsigned int cp = (unsigned int)utf8_next(&q);

            if (!cp)
                break;

            if (line_insert_cps(ed->lines[ed->row], ed->col, &cp, 1) != 0)
            {
                line_free(tail);
                return -1;
            }

            if (ed->input_mask)
                ed_attr_line_apply(ed->lines[ed->row], ed->col, ed->col + 1, ed->input_mask, 0, -1, 0);

            ed->col++;
        }

        first = 0;

        if (!nl)
            break;

        p = nl + 1;
    }

    if (line_append_slice(ed->lines[ed->row], tail, 0, tail->len) != 0)
    {
        line_free(tail);
        return -1;
    }

    line_free(tail);

    ed->modified = 1;

    doc_dirty_from(ed, ed->row);

    return 0;
}

int ed_paste_text_with_undo(Ed *ed, const char *utf8_text)
{
    int start;
    int newn;

    if (!ed || !utf8_text)
        return -1;

    if (ed->block.active)
        ed_block_delete(ed);

    start = ed->row;

    if (undo_begin(ed, start, 1) != 0)
        return -1;

    if (ed_paste_text(ed, utf8_text) != 0)
    {
        undo_abort(ed);

        return -1;
    }

    newn = ed->row - start + 1;

    undo_commit(ed, newn);

    return 0;
}

int ed_sort_block_lines(Ed *ed)
{
    int r1, c1, r2, c2;
    int n;
    int i;
    int j;

    if (!block_range(ed, &r1, &c1, &r2, &c2))
        return -1;

    n = r2 - r1 + 1;

    if (n < 2)
        return -1;

    if (undo_begin(ed, r1, n) != 0)
        return -1;

    /* Insertion sort on the line pointers, case insensitive */
    for (i = 1; i < n; i++)
    {
        EdLine *key = ed->lines[r1 + i];

        j = i - 1;

        while (j >= 0)
        {
            EdLine *a = ed->lines[r1 + j];
            int k = 0;
            int cmp = 0;

            for (;;)
            {
                unsigned int ca = k < a->len ? ed_line_char(a, k) : 0;
                unsigned int cb = k < key->len ? ed_line_char(key, k) : 0;

                ca = (unsigned int)towlower((wint_t)ca);
                cb = (unsigned int)towlower((wint_t)cb);

                if (ca != cb)
                {
                    cmp = (ca > cb) ? 1 : -1;
                    break;
                }

                if (!ca)
                    break;

                k++;
            }

            if (cmp <= 0)
                break;

            ed->lines[r1 + j + 1] = a;
            j--;
        }

        ed->lines[r1 + j + 1] = key;
    }

    doc_dirty_from(ed, r1);

    /* The lines moved under the cursor, it may now point past the end */
    ed_clamp(ed);

    undo_commit(ed, n);

    return 0;
}

int ed_convert_block_case(Ed *ed, int mode)
{
    int r1, c1, r2, c2;
    int row;
    int at_start = 1;

    if (!block_range(ed, &r1, &c1, &r2, &c2))
        return -1;

    if (undo_begin(ed, r1, r2 - r1 + 1) != 0)
        return -1;

    for (row = r1; row <= r2; row++)
    {
        EdLine *ln = ed->lines[row];
        int a = (row == r1) ? c1 : 0;
        int b = (row == r2) ? c2 : ln->len;
        int i;

        for (i = a; i < b; i++)
        {
            unsigned int cp = ed_line_char(ln, i);
            unsigned int out = cp;

            if (mode == 0)
                out = (unsigned int)towupper((wint_t)cp);
            else if (mode == 1)
                out = (unsigned int)towlower((wint_t)cp);
            else if (mode == 2)
                out = at_start ? (unsigned int)towupper((wint_t)cp) : (unsigned int)towlower((wint_t)cp);

            if (out != cp && cw_for(out) <= ln->cw)
                line_put(ln, i, out);

            at_start = !iswalnum((wint_t)cp);
        }

        line_touch(ln);

        at_start = 1;
    }

    doc_dirty_from(ed, r1);
    undo_commit(ed, r2 - r1 + 1);

    return 0;
}

/* Capture the paragraph before a hard wrap reflow, undo_commit closes it */
void ed_auto_rewrap_capture_pre_snapshot(Ed *ed)
{
    int first;
    int last;
    int n;

    if (!ed || ed->count <= 0)
        return;

    if (ed->lines[ed->row]->len == 0)
    {
        first = ed->row;
        last = ed->row;
    }
    else
    {
        first = ed->row;

        while (first > 0 && ed->lines[first - 1]->len > 0 && ed->lines[first - 1]->brk != LB_PARA)
            first--;

        last = ed->row;

        while (last < ed->count - 1 && ed->lines[last]->brk != LB_PARA && ed->lines[last + 1]->len > 0)
            last++;
    }

    n = last - first + 1;

    if (last + 1 < ed->count && n < ED_AUTO_REWRAP_SNAPSHOT_LINE_CAP)
        n++;

    /* Guard: an oversized paragraph is not worth cloning */
    if (n > ED_AUTO_REWRAP_SNAPSHOT_LINE_CAP)
    {
        undo_abort(ed);

        return;
    }

    undo_begin(ed, first, n);
}

/* Replace [start, end) on one row as a single undo entry */
int ed_replace_word_with_undo(Ed *ed, int row, int start, int end, const wchar_t *replacement, int rlen)
{
    EdLine *ln = NULL;
    unsigned int *cps = NULL;
    int i;

    if (!ed || row < 0 || row >= ed->count || start < 0)
        return -1;

    ln = ed->lines[row];

    if (!ln || start > ln->len || end < start || end > ln->len)
        return -1;

    if (rlen < 0)
        rlen = 0;

    /* One-line, one-delta undo entry: capture the line, mutate, commit */
    undo_abort(ed);

    if (undo_begin(ed, row, 1) != 0)
        return -1;

    /* Suppress the per-op captures of the public entry points */
    ed->undo_snapshot_mode = 1;

    if (end > start)
        line_delete_cps(ln, start, end - start);

    if (rlen > 0)
    {
        cps = (unsigned int *)malloc((size_t)rlen * sizeof(unsigned int));

        if (!cps)
        {
            ed->undo_snapshot_mode = 0;

            undo_abort(ed);
            return -1;
        }

        for (i = 0; i < rlen; i++)
            cps[i] = (unsigned int)replacement[i];

        if (line_insert_cps(ln, start, cps, rlen) != 0)
        {
            free(cps);

            ed->undo_snapshot_mode = 0;
            undo_abort(ed);
            return -1;
        }

        free(cps);
    }

    ed->row = row;
    ed->col = start + rlen;
    ed->modified = 1;

    doc_dirty_from(ed, row);

    ed->undo_snapshot_mode = 0;

    if (undo_commit(ed, 1) != 0)
        return -1;

    ed_save_undo(ed);

    return 0;
}
