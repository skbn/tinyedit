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
#include <limits.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include "editor.h"
#include "../core/utf8.h"
#include "../core/charset.h"
#include "../core/portable.h"

#define SAVE_BUF_SIZE (1024 * 1024)

/* Amiga memory pool tuning: 64 KiB puddle, 8 KiB threshold */
#define ED_POOL_PUDDLE (64 * 1024)
#define ED_POOL_THRESHOLD (8 * 1024)

/* Stream loading / buffer limits */
#define ED_MAX_LINE_SIZE (1024 * 1024)
#define ED_READ_CHUNK_SIZE 8192
#define ED_BUF_INITIAL_SIZE 4096

/* Line capacity allocation */
#define ED_LINE_CAP_THRESHOLD 64
#define ED_LINE_CAP_SMALL 16

/* Line growth / charset conversion padding */
#define ED_LINE_GROW_PAD 64
#define ED_CHARSET_CONV_PAD 16

/* Editor defaults */
#define ED_TAB_WIDTH_DEFAULT 4
#define ED_TAB_WIDTH_MAX 16
#define ED_PAGE_DEFAULT 25
#define ED_UNDO_MAX_DEFAULT 50
#define ED_REDO_MAX_DEFAULT 50
#define ED_UNDO_TEXT_INIT_CAP 16

static int s_tab_width = ED_TAB_WIDTH_DEFAULT; /* Visual tab stop width for soft-wrap calculations */

#define ED_AUTO_REWRAP_SNAPSHOT_LINE_CAP 512

/* Forward declarations for undo record helpers (defined after editing funcs) */
static void record_insert(Ed *ed, int row, int col, wchar_t ch);
static void record_delete_back(Ed *ed, int row, int col, wchar_t ch);
static void record_delete_fwd(Ed *ed, int row, int col, wchar_t ch);
static void record_split(Ed *ed, int row, int col);
static void record_join(Ed *ed, int row, int join_col);

static void block_range(const Ed *ed, int *r1, int *c1, int *r2, int *c2);

/* Forward declarations for undo group functions */
int ed_undo_open_group(Ed *ed);
static int undo_push_op(Ed *ed, UndoOpType type, int row, int col, const wchar_t *text, int len, int join_col);
int undo_push_snapshot_range(Ed *ed, int row, int col, char *snapshot_before, char *snapshot_after, int old_count, int new_count, int cur_row, int cur_col, int end_row, int end_col);

/* Forward declarations for helper functions */
wchar_t *line_to_wcs(EdLine *ln);
wchar_t *line_to_wcs_range(EdLine *ln, int start, int end);
char *ed_block_to_string(Ed *ed, int r1, int c1, int r2, int c2);

/* Prefix sum functions */
static int prefix_init(Ed *ed);
static void prefix_free(Ed *ed);
void ed_prefix_invalidate(Ed *ed);
static void ed_wcs_view_reset(Ed *ed);
static void ed_wcs_view_free(Ed *ed);
static int prefix_rebuild(Ed *ed, int width, int max_line);
static int prefix_rebuild_from(Ed *ed, int from_line, int width);
static int ed_line_wrap_count(Ed *ed, EdLine *ln, int width);

/* Per-document memory pool helpers. AmigaOS uses AllocPooled/DeletePool, other platforms use plain malloc/realloc/free */
#if defined(PLATFORM_AMIGA)

#include <exec/memory.h>
#include <proto/exec.h>

static void *ed_pool_alloc(Ed *ed, size_t n)
{
    if (ed && ed->mem_pool)
        return AllocPooled((APTR)ed->mem_pool, (ULONG)n);

    return malloc(n);
}

static void ed_pool_free(Ed *ed, void *p, size_t n)
{
    if (!p)
        return;

    if (ed && ed->mem_pool)
        FreePooled((APTR)ed->mem_pool, p, (ULONG)n);
    else
        free(p);
}

static void *ed_pool_realloc(Ed *ed, void *p, size_t old_n, size_t new_n)
{
    void *np = NULL;

    if (new_n == 0)
    {
        ed_pool_free(ed, p, old_n);
        return NULL;
    }

    if (!ed || !ed->mem_pool)
        return realloc(p, new_n);

    np = AllocPooled((APTR)ed->mem_pool, (ULONG)new_n);

    if (!np)
        return NULL;

    if (p && old_n)
    {
        memcpy(np, p, old_n < new_n ? old_n : new_n);
        FreePooled((APTR)ed->mem_pool, p, (ULONG)old_n);
    }

    return np;
}
#else

/* Non-Amiga pass-through; ed unused */
static void *ed_pool_alloc(Ed *ed, size_t n)
{
    return malloc(n);
}

static void ed_pool_free(Ed *ed, void *p, size_t n)
{
    free(p);
}

static void *ed_pool_realloc(Ed *ed, void *p, size_t old_n, size_t new_n)
{
    return realloc(p, new_n);
}
#endif

static int cw_for_cp(unsigned int cp)
{
    /* Narrowest element size that can hold this codepoint */
    if (cp < 0x100u)
        return 1;

    if (cp < 0x10000u)
        return 2;

    return 4;
}

unsigned int ed_line_char(const EdLine *ln, int i)
{
    /* Decode the codepoint stored at character index i */
    if (!ln || i < 0 || i >= ln->len)
        return 0;

    if (ln->cw == 1)
        return ((const unsigned char *)ln->text)[i];

    if (ln->cw == 2)
        return ((const unsigned short *)ln->text)[i];

    return ((const unsigned int *)ln->text)[i];
}

static void line_put_raw(EdLine *ln, int i, unsigned int cp)
{
    /* Store a codepoint that already fits the line's element size */
    if (ln->cw == 1)
        ((unsigned char *)ln->text)[i] = (unsigned char)cp;
    else if (ln->cw == 2)
        ((unsigned short *)ln->text)[i] = (unsigned short)cp;
    else
        ((unsigned int *)ln->text)[i] = cp;
}

static void line_move(EdLine *ln, int dst, int src, int n)
{
    /* Move n characters within a line, honouring the element size */
    char *base = (char *)ln->text;

    memmove(base + (size_t)dst * (size_t)ln->cw, base + (size_t)src * (size_t)ln->cw, (size_t)n * (size_t)ln->cw);
}

static size_t line_hdr_bytes(void)
{
    /* Struct size rounded up so the inline text area stays aligned */
    return (sizeof(EdLine) + 3u) & ~(size_t)3u;
}

static int line_text_inline(const EdLine *ln)
{
    /* Inline text lives right after the struct in the same allocation */
    return (const char *)ln->text == (const char *)ln + line_hdr_bytes();
}

static void *line_mem_alloc(EdLine *ln, size_t n)
{
    /* Allocate a text buffer from the owning pool or the heap */
#if defined(PLATFORM_AMIGA)
    if (ln->mem_pool)
        return AllocPooled((APTR)ln->mem_pool, (ULONG)n);
#endif

    return malloc(n);
}

static void line_mem_free(EdLine *ln, void *p, size_t n)
{
    /* Release a text buffer to the owning pool or the heap */
#if defined(PLATFORM_AMIGA)
    if (ln->mem_pool)
    {
        FreePooled((APTR)ln->mem_pool, p, (ULONG)n);
        return;
    }
#endif

    free(p);
}

static int line_count_words(const EdLine *ln)
{
    wchar_t stackbuf[256];
    wchar_t *buf = NULL;
    int i;
    int n;

    if (!ln || ln->len <= 0)
        return 0;

    /* pf_count_words_wcs() needs a wchar_t view, so decode into one */
    if (ln->len < (int)(sizeof(stackbuf) / sizeof(stackbuf[0])))
    {
        buf = stackbuf;
    }
    else
    {
        buf = (wchar_t *)malloc((size_t)(ln->len + 1) * sizeof(wchar_t));

        if (!buf)
            return 0;
    }

    for (i = 0; i < ln->len; i++)
        buf[i] = (wchar_t)ed_line_char(ln, i);

    n = pf_count_words_wcs(buf, ln->len);

    if (buf != stackbuf)
        free(buf);

    return n;
}

static int line_reserve(EdLine *ln, int need)
{
    int nc;
    void *t = NULL;

    /* Grow the character capacity, keeping the current element size */
    if (ln->cap > need + 1)
        return 0;

    nc = need + ED_LINE_GROW_PAD;

    if ((size_t)nc > (size_t)-1 / (size_t)ln->cw)
        return -1;

    t = line_mem_alloc(ln, (size_t)nc * (size_t)ln->cw);

    if (!t)
        return -1;

    memcpy(t, ln->text, (size_t)(ln->len + 1) * (size_t)ln->cw);

    /* The inline area is part of the struct block, so only free external text */
    if (!line_text_inline(ln))
        line_mem_free(ln, ln->text, (size_t)ln->cap * (size_t)ln->cw);

    ln->text = t;
    ln->cap = nc;

    return 0;
}

static int line_widen(EdLine *ln, int w)
{
    void *t = NULL;
    unsigned short *d2 = NULL;
    unsigned int *d4 = NULL;
    int i;

    /* Promote the line to a wider element size, re-encoding every char */
    if (ln->cw >= w)
        return 0;

    if ((size_t)ln->cap > (size_t)-1 / (size_t)w)
        return -1;

    t = line_mem_alloc(ln, (size_t)ln->cap * (size_t)w);

    if (!t)
        return -1;

    if (w == 2)
    {
        d2 = (unsigned short *)t;

        for (i = 0; i <= ln->len; i++)
            d2[i] = (unsigned short)ed_line_char(ln, i);
    }
    else
    {
        d4 = (unsigned int *)t;

        for (i = 0; i <= ln->len; i++)
            d4[i] = ed_line_char(ln, i);
    }

    /* The inline area is part of the struct block, so only free external text */
    if (!line_text_inline(ln))
        line_mem_free(ln, ln->text, (size_t)ln->cap * (size_t)ln->cw);

    ln->text = t;
    ln->cw = w;

    return 0;
}

static int line_need(EdLine *ln, int need, int w)
{
    /* Ensure the line can hold need chars at least w bytes wide */
    if (w > ln->cw && line_widen(ln, w) != 0)
        return -1;

    if (line_reserve(ln, need) != 0)
        return -1;

    return 0;
}

static EdLine *line_build(Ed *ed, const wchar_t *src, int len, int cap)
{
    EdLine *ln = NULL;
    unsigned int cp;
    int i;
    int w;
    size_t hdr;
    size_t txt;

    /* Shared constructor for line_new() and line_new_take() */
    if (cap < len + 1)
        cap = len + 1;

    /* Choose the narrowest element size that fits the whole source */
    w = 1;

    for (i = 0; i < len; i++)
    {
        cp = (unsigned int)src[i];

        if (cp >= 0x10000u)
        {
            w = 4;
            break;
        }

        if (cp >= 0x100u && w < 2)
            w = 2;
    }

    hdr = line_hdr_bytes();

    if ((size_t)cap > ((size_t)-1 - hdr) / (size_t)w)
        return NULL;

    txt = (size_t)cap * (size_t)w;

    if (txt > (size_t)INT_MAX)
        return NULL;

    /* One allocation holds both the struct and its inline text area */
    ln = (EdLine *)ed_pool_alloc(ed, hdr + txt);

    if (!ln)
        return NULL;

    ln->cw = w;
    ln->cap = cap;
    ln->len = len;
    ln->text = (char *)ln + hdr;
    ln->emb = (int)txt;

#if defined(PLATFORM_AMIGA)
    ln->mem_pool = ed ? ed->mem_pool : NULL;
#endif

    for (i = 0; i < len; i++)
        line_put_raw(ln, i, (unsigned int)src[i]);

    line_put_raw(ln, len, 0);

    ln->word_count = 0;
    ln->has_wrap_hyphen = 0;
    ln->wrap_count_cache = -1;
    ln->wrap_cache_width = 0;

    return ln;
}

/* Ed routes allocations to the per-document pool on AmigaOS */
static EdLine *line_new(Ed *ed, const wchar_t *src, int len)
{
    int cap;

    /* Interactive lines get head-room to avoid an immediate regrow */
    if (len > ED_LINE_CAP_THRESHOLD)
        cap = len + 1;
    else
        cap = len + ED_LINE_CAP_SMALL;

    return line_build(ed, src, len, cap);
}

static EdLine *line_new_take(Ed *ed, wchar_t *wcs, int len)
{
    EdLine *ln = NULL;

    if (!wcs)
        return NULL;

    /* Loaded lines have a known size, so allocate an exact fit */
    ln = line_build(ed, wcs, len, len + 1);

    free(wcs);

    return ln;
}

static EdLine *line_empty(Ed *ed)
{
    return line_new(ed, L"", 0);
}

static void line_free(EdLine *ln)
{
    if (!ln)
        return;

#if defined(PLATFORM_AMIGA)
    if (ln->mem_pool)
    {
        /* FreePooled needs the exact original size: header plus inline area */
        size_t total = line_hdr_bytes() + (size_t)ln->emb;

        if (!line_text_inline(ln))
            FreePooled((APTR)ln->mem_pool, ln->text, (ULONG)((size_t)ln->cap * (size_t)ln->cw));

        FreePooled((APTR)ln->mem_pool, ln, (ULONG)total);
        return;
    }
#endif

    if (!line_text_inline(ln))
        free(ln->text);

    free(ln);
}

static int line_insert(Ed *ed, EdLine *ln, int pos, wchar_t ch)
{
    unsigned int cp;
    int old_count;

    if (pos < 0)
        pos = 0;

    if (pos > ln->len)
        pos = ln->len;

    cp = (unsigned int)ch;

    if (line_need(ln, ln->len + 1, cw_for_cp(cp)) != 0)
        return -1;

    /* Shift the tail and the NUL one slot right, then drop the char in */
    line_move(ln, pos + 1, pos, ln->len - pos + 1);
    line_put_raw(ln, pos, cp);

    ln->len++;
    ln->wrap_count_cache = -1;

    ed_wcs_view_reset(ed);

    if (ed->word_count_initialized)
    {
        old_count = ln->word_count;
        ln->word_count = line_count_words(ln);
        ed->word_count_total += ln->word_count - old_count;
    }

    return 0;
}

static int line_delete(Ed *ed, EdLine *ln, int pos)
{
    int old_count;

    if (pos < 0 || pos >= ln->len)
        return -1;

    /* If deleting the wrap-hyphen (last char), clear the flag */
    if (ln->has_wrap_hyphen && pos == ln->len - 1)
        ln->has_wrap_hyphen = 0;

    line_move(ln, pos, pos + 1, ln->len - pos);
    ln->len--;
    ln->wrap_count_cache = -1;
    ed_wcs_view_reset(ed);

    if (ed->word_count_initialized)
    {
        old_count = ln->word_count;
        ln->word_count = line_count_words(ln);
        ed->word_count_total += ln->word_count - old_count;
    }

    return 0;
}

static int line_delete_range(Ed *ed, EdLine *ln, int pos, int n)
{
    int old_count;

    if (pos < 0 || n <= 0 || pos >= ln->len)
        return -1;

    if (pos + n > ln->len)
        n = ln->len - pos;

    line_move(ln, pos, pos + n, ln->len - pos - n + 1);

    ln->len -= n;
    ln->wrap_count_cache = -1;

    ed_wcs_view_reset(ed);

    if (ed->word_count_initialized)
    {
        old_count = ln->word_count;
        ln->word_count = line_count_words(ln);
        ed->word_count_total += ln->word_count - old_count;
    }

    return 0;
}

static void line_truncate(Ed *ed, EdLine *ln, int pos)
{
    int old_count;

    if (pos < 0)
        pos = 0;

    if (pos >= ln->len)
        return;

    ln->len = pos;
    line_put_raw(ln, pos, 0);

    ln->wrap_count_cache = -1;

    ed_wcs_view_reset(ed);

    if (ed->word_count_initialized)
    {
        old_count = ln->word_count;
        ln->word_count = line_count_words(ln);
        ed->word_count_total += ln->word_count - old_count;
    }
}

static EdLine *line_from_slice(Ed *ed, EdLine *ln, int start, int count)
{
    wchar_t *tmp = NULL;
    EdLine *out = NULL;
    int i;

    /* Build a new line from a character range of an existing one */
    if (count <= 0)
        return line_empty(ed);

    tmp = (wchar_t *)malloc((size_t)(count + 1) * sizeof(wchar_t));

    if (!tmp)
        return NULL;

    for (i = 0; i < count; i++)
        tmp[i] = (wchar_t)ed_line_char(ln, start + i);

    tmp[count] = L'\0';
    out = line_new(ed, tmp, count);

    free(tmp);

    return out;
}

static int line_append_line(Ed *ed, EdLine *dst, const EdLine *src, int start, int count)
{
    unsigned int cp;
    int old_count;
    int i;
    int w;

    /* Append a range of src onto dst without a wchar_t round-trip */
    if (count <= 0)
        return 0;

    w = dst->cw;

    for (i = 0; i < count; i++)
    {
        cp = ed_line_char(src, start + i);

        if (cw_for_cp(cp) > w)
            w = cw_for_cp(cp);
    }

    if (line_need(dst, dst->len + count, w) != 0)
        return -1;

    for (i = 0; i < count; i++)
        line_put_raw(dst, dst->len + i, ed_line_char(src, start + i));

    dst->len += count;
    line_put_raw(dst, dst->len, 0);

    dst->wrap_count_cache = -1;

    ed_wcs_view_reset(ed);

    if (ed->word_count_initialized)
    {
        old_count = dst->word_count;
        dst->word_count = line_count_words(dst);
        ed->word_count_total += dst->word_count - old_count;
    }

    return 0;
}

static void line_copy_out(const EdLine *ln, int start, int count, wchar_t *dst)
{
    int i;

    /* Decode a character range into a caller-supplied wchar_t buffer */
    for (i = 0; i < count; i++)
        dst[i] = (wchar_t)ed_line_char(ln, start + i);
}

static int line_set_char(EdLine *ln, int i, unsigned int cp)
{
    /* Overwrite one character, widening the line first if needed */
    if (cw_for_cp(cp) > ln->cw && line_widen(ln, cw_for_cp(cp)) != 0)
        return -1;

    line_put_raw(ln, i, cp);
    return 0;
}

static char *line_to_utf8(const EdLine *ln, int start, int count)
{
    wchar_t *tmp = NULL;
    char *out = NULL;

    /* Encode a character range straight to a fresh UTF-8 string */
    if (count < 0)
        count = 0;

    tmp = (wchar_t *)malloc((size_t)(count + 1) * sizeof(wchar_t));

    if (!tmp)
        return NULL;

    line_copy_out(ln, start, count, tmp);
    tmp[count] = L'\0';

    out = wcs_to_utf8(tmp, count);

    free(tmp);

    return out;
}

static int doc_grow(Ed *ed)
{
    int na;
    EdLine **t = NULL;

    if (ed->count < ed->alloc)
        return 0;

    na = ed->alloc > 0 ? ed->alloc * 2 : INIT_ALLOC;

    t = (EdLine **)ed_pool_realloc(ed, ed->lines, (size_t)ed->alloc * sizeof(EdLine *), (size_t)na * sizeof(EdLine *));

    if (!t)
        return -1;

    ed->lines = t;
    ed->alloc = na;

    return 0;
}

static int doc_insert_line(Ed *ed, int at, EdLine *ln)
{
    if (doc_grow(ed) != 0)
    {
        line_free(ln);
        return -1;
    }

    if (at < 0)
        at = 0;

    if (at > ed->count)
        at = ed->count;

    /* Shift tail right with memmove (faster than loop) */
    if (ed->count > at)
        memmove(&ed->lines[at + 1], &ed->lines[at], (size_t)(ed->count - at) * sizeof(EdLine *));

    ed->lines[at] = ln;
    ed->count++;

    if (ed->word_count_initialized)
    {
        ln->word_count = line_count_words(ln);
        ed->word_count_total += ln->word_count;
    }

    return 0;
}

static EdLine *doc_remove_line(Ed *ed, int at)
{
    EdLine *ln = NULL;

    if (at < 0 || at >= ed->count)
        return NULL;

    ln = ed->lines[at];

    if (ed->word_count_initialized)
        ed->word_count_total -= ln->word_count;

    /* Shift tail left with one memmove (was for-loop) */
    if (at < ed->count - 1)
        memmove(&ed->lines[at], &ed->lines[at + 1], (size_t)(ed->count - at - 1) * sizeof(EdLine *));

    ed->count--;

    return ln;
}

static void doc_clear(Ed *ed)
{
    int i;

    for (i = 0; i < ed->count; i++)
        line_free(ed->lines[i]);

    ed->count = 0;
    ed->word_count_total = 0;
    ed->word_count_initialized = 0;
    ed->syntax_state_dirty_from = 0;
    ed->syntax_state_lang = -1;

    /* Drop decoded views; their line indices now point at freed lines */
    ed_wcs_view_reset(ed);
}

/* Prefix sum implementation */
static int prefix_init(Ed *ed)
{
    ed->prefix_alloc = INIT_ALLOC;
    ed->prefix = (int *)ed_pool_alloc(ed, (size_t)ed->prefix_alloc * sizeof(int));

    if (!ed->prefix)
        return -1;

    ed->prefix_valid = 0;
    ed->prefix_width = 0;
    ed->prefix_dirty_from = -1;
    ed->prefix_base = 0;

    return 0;
}

static void prefix_free(Ed *ed)
{
    if (ed->prefix)
    {
        ed_pool_free(ed, ed->prefix, (size_t)ed->prefix_alloc * sizeof(int));
        ed->prefix = NULL;
    }

    ed->prefix_alloc = 0;
    ed->prefix_valid = 0;
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

static void ed_wcs_view_reset(Ed *ed)
{
    int i;

    /* Mark every decoded view stale without freeing the buffers */
    if (!ed)
        return;

    for (i = 0; i < ED_WCS_VIEW_SLOTS; i++)
        ed->wcs_view_line[i] = -1;

    ed->wcs_view_next = 0;
}

static void ed_wcs_view_free(Ed *ed)
{
    int i;

    /* Release the decode buffers; they live on the heap, not the pool */
    if (!ed)
        return;

    for (i = 0; i < ED_WCS_VIEW_SLOTS; i++)
    {
        free(ed->wcs_view[i]);

        ed->wcs_view[i] = NULL;
        ed->wcs_view_cap[i] = 0;
        ed->wcs_view_line[i] = -1;
    }
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

/* Public prefix functions */
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

/* Rebuild prefix incrementally from a specific line onwards */
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

void ed_set_tab_width(int n)
{
    if (n < 1)
        n = 1;

    if (n > ED_TAB_WIDTH_MAX)
        n = ED_TAB_WIDTH_MAX;

    s_tab_width = n;
}

int ed_get_tab_width(void)
{
    return s_tab_width;
}

/* Soft-wrap helper: returns end of current visual segment */
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

/* Soft-wrap helper: number of visual sub-rows a logical line occupies */
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

/* Per-line cached wrap count: recomputes only when content or width changed */
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

/* Free all ops inside a group (does not free the group itself) */
static void undo_group_clear(UndoGroup *g)
{
    int i;

    for (i = 0; i < g->count; i++)
    {
        free(g->ops[i].text);
        free(g->ops[i].utf8_snapshot);
        free(g->ops[i].utf8_snapshot_new);
    }

    free(g->ops);

    g->ops = NULL;
    g->count = 0;
    g->cap = 0;
}

/* Free all groups in a stack */
static void undo_stack_clear(UndoGroup *stack, int top)
{
    int i;

    for (i = 0; i < top; i++)
        undo_group_clear(&stack[i]);
}

Ed *ed_new(void)
{
    Ed *ed = (Ed *)calloc(1, sizeof(Ed));

    if (!ed)
        return NULL;

    ed->insert_mode = 1;
    ed->page = ED_PAGE_DEFAULT;
    ed->undo_max = ED_UNDO_MAX_DEFAULT;
    ed->redo_max = ED_REDO_MAX_DEFAULT;
    ed->undo_snapshot_mode = 0;
    ed->hard_wrap = 0;
    ed->word_move_mode = 0;

#if defined(PLATFORM_AMIGA)
    /* Create per-document pool before allocation. Fallback to malloc on failure */
    ed->mem_pool = (void *)CreatePool(MEMF_ANY | MEMF_CLEAR, (ULONG)ED_POOL_PUDDLE, (ULONG)ED_POOL_THRESHOLD);
#endif

    ed->lines = (EdLine **)ed_pool_alloc(ed, INIT_ALLOC * sizeof(EdLine *));

    if (!ed->lines)
    {
#if defined(PLATFORM_AMIGA)
        if (ed->mem_pool)
        {
            DeletePool((APTR)ed->mem_pool);
            ed->mem_pool = NULL;
        }
#endif

        free(ed);
        return NULL;
    }

    ed->alloc = INIT_ALLOC;
    ed->lines[0] = line_empty(ed);

    if (!ed->lines[0])
    {
        ed_pool_free(ed, ed->lines, INIT_ALLOC * sizeof(EdLine *));

#if defined(PLATFORM_AMIGA)
        if (ed->mem_pool)
        {
            DeletePool((APTR)ed->mem_pool);
            ed->mem_pool = NULL;
        }
#endif

        free(ed);
        return NULL;
    }

    ed->count = 1;
    ed->syntax_state_dirty_from = -1;
    ed->syntax_state_lang = -1;

    if (prefix_init(ed) != 0)
    {
        doc_clear(ed);
        ed_pool_free(ed, ed->lines, (size_t)ed->alloc * sizeof(EdLine *));

#if defined(PLATFORM_AMIGA)
        if (ed->mem_pool)
        {
            DeletePool((APTR)ed->mem_pool);
            ed->mem_pool = NULL;
        }
#endif

        free(ed);

        return NULL;
    }

    return ed;
}

void ed_free(Ed *ed)
{
    if (!ed)
        return;

#if defined(PLATFORM_AMIGA)
    /* AmigaOS fast path: DeletePool() reclaims all pool memory in one call */
    if (ed->mem_pool)
    {
        DeletePool((APTR)ed->mem_pool);
        ed->mem_pool = NULL;

        /* Null dangling pointers so post-free reads fault loudly */
        ed->lines = NULL;
        ed->prefix = NULL;
        ed->count = 0;
        ed->alloc = 0;
        ed->prefix_alloc = 0;
    }
    else
    {
        /* Pool creation failed at ed_new: use slow path */
        doc_clear(ed);

        free(ed->lines);
        prefix_free(ed);
    }

    free(ed->killbuf);
    free(ed->auto_rewrap_pre_snapshot);
    free(ed->syntax_state_cache);

    if (ed->undo_stack)
    {
        undo_stack_clear(ed->undo_stack, ed->undo_top);
        free(ed->undo_stack);
    }

    if (ed->redo_stack)
    {
        undo_stack_clear(ed->redo_stack, ed->redo_top);
        free(ed->redo_stack);
    }

    ed_wcs_view_free(ed);
    free(ed->wrap_scratch);
    free(ed);
    return;
#else
    /* Non-Amiga path: unchanged */
    doc_clear(ed);

    free(ed->lines);
    free(ed->killbuf);
    free(ed->auto_rewrap_pre_snapshot);
    free(ed->syntax_state_cache);

    prefix_free(ed);

    if (ed->undo_stack)
    {
        undo_stack_clear(ed->undo_stack, ed->undo_top);

        free(ed->undo_stack);
    }

    if (ed->redo_stack)
    {
        undo_stack_clear(ed->redo_stack, ed->redo_top);

        free(ed->redo_stack);
    }

    ed_wcs_view_free(ed);
    free(ed->wrap_scratch);
    free(ed);
#endif
}

void ed_load(Ed *ed, const char *utf8_text)
{
    const char *p = NULL;
    const char *start = NULL;

    if (!ed)
        return;

    doc_clear(ed);

    ed->row = ed->col = ed->top = 0;
    ed->modified = 0;
    ed->block.active = 0;
    ed->undo_open = 0;
    ed->syntax_state_dirty_from = 0;
    ed->syntax_state_lang = -1;

    if (!utf8_text || !*utf8_text)
    {
        EdLine *ln = line_empty(ed);

        if (ln)
            doc_insert_line(ed, 0, ln);

        return;
    }

    /* Split by lines, convert each to wchar_t */
    p = utf8_text;

    while (*p)
    {
        wchar_t *wcs = NULL;
        int wlen;
        char *line_utf8 = NULL;
        int blen;
        EdLine *ln = NULL;

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
            ln = line_new_take(ed, wcs, wlen);

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
        EdLine *ln = line_empty(ed);

        if (ln)
            doc_insert_line(ed, 0, ln);
    }
}

/* Stream-load UTF-8 from FILE* in 8 KB chunks. Lower peak RAM than ed_load() because we never hold the whole file plus the wchar_t copy at once */
int ed_load_stream(Ed *ed, FILE *fp)
{
    char chunk[ED_READ_CHUNK_SIZE];
    char *acc = NULL;
    size_t acc_len = 0;
    size_t acc_cap = 0;
    size_t got;

    if (!ed || !fp)
        return -1;

    doc_clear(ed);

    ed->row = ed->col = ed->top = 0;
    ed->modified = 0;
    ed->block.active = 0;
    ed->undo_open = 0;
    ed->syntax_state_dirty_from = 0;
    ed->syntax_state_lang = -1;

    acc_cap = ED_BUF_INITIAL_SIZE;

    acc = (char *)malloc(acc_cap);

    if (!acc)
        return -1;

    while ((got = fread(chunk, 1, sizeof(chunk), fp)) > 0)
    {
        size_t i = 0;

        while (i < got)
        {
            size_t segment_start = i;
            size_t seg;
            size_t need;

            while (i < got && chunk[i] != '\r' && chunk[i] != '\n')
                i++;

            seg = i - segment_start;
            need = acc_len + seg + 1;

            if (need > ED_MAX_LINE_SIZE)
            {
                seg = (ED_MAX_LINE_SIZE > acc_len + 1) ? (ED_MAX_LINE_SIZE - acc_len - 1) : 0;
                need = acc_len + seg + 1;
            }

            if (need > acc_cap)
            {
                size_t nc = acc_cap;
                char *na = NULL;

                while (nc < need)
                    nc *= 2;

                if (nc > ED_MAX_LINE_SIZE + 1)
                    nc = ED_MAX_LINE_SIZE + 1;

                na = (char *)realloc(acc, nc);

                if (!na)
                {
                    free(acc);
                    return -1;
                }

                acc = na;
                acc_cap = nc;
            }

            if (seg > 0)
            {
                memcpy(acc + acc_len, chunk + segment_start, seg);
                acc_len += seg;
            }

            if (i < got && (chunk[i] == '\r' || chunk[i] == '\n'))
            {
                wchar_t *wcs = NULL;
                int wlen;
                EdLine *ln = NULL;

                acc[acc_len] = '\0';
                wcs = utf8_to_wcs(acc, &wlen);

                if (wcs)
                {
                    ln = line_new_take(ed, wcs, wlen);

                    if (ln)
                        doc_insert_line(ed, ed->count, ln);
                }

                acc_len = 0;

                if (chunk[i] == '\r')
                {
                    i++;

                    if (i < got && chunk[i] == '\n')
                        i++;
                    else if (i == got)
                    {
                        int c = fgetc(fp);

                        if (c != '\n' && c != EOF)
                            ungetc(c, fp);
                    }
                }
                else
                {
                    i++;
                }
            }
        }
    }

    if (acc_len > 0)
    {
        wchar_t *wcs = NULL;
        int wlen;
        EdLine *ln = NULL;

        acc[acc_len] = '\0';
        wcs = utf8_to_wcs(acc, &wlen);

        if (wcs)
        {
            ln = line_new_take(ed, wcs, wlen);

            if (ln)
                doc_insert_line(ed, ed->count, ln);
        }
    }

    free(acc);

    if (ed->count == 0)
    {
        EdLine *ln = line_empty(ed);

        if (ln)
            doc_insert_line(ed, 0, ln);
    }

    return 0;
}

/* Load a stream converting each line from a single-byte charset to UTF-8 */
int ed_load_stream_charset(Ed *ed, FILE *fp, const char *charset)
{
    char chunk[ED_READ_CHUNK_SIZE];
    char *acc = NULL;
    char *cbuf = NULL;
    size_t acc_len = 0;
    size_t acc_cap = 0;
    size_t cbuf_cap = 0;
    size_t got;

    if (!ed || !fp)
        return -1;

    if (!charset || !charset[0] || strcasecmp(charset, "UTF-8") == 0 || strcasecmp(charset, "UTF8") == 0)
        return ed_load_stream(ed, fp);

    doc_clear(ed);

    ed->row = ed->col = ed->top = 0;
    ed->modified = 0;
    ed->block.active = 0;
    ed->undo_open = 0;
    ed->syntax_state_dirty_from = 0;
    ed->syntax_state_lang = -1;

    acc_cap = ED_BUF_INITIAL_SIZE;
    acc = (char *)malloc(acc_cap);

    if (!acc)
        return -1;

    while ((got = fread(chunk, 1, sizeof(chunk), fp)) > 0)
    {
        size_t i = 0;

        while (i < got)
        {
            size_t segment_start = i;
            size_t seg;
            size_t need;

            while (i < got && chunk[i] != '\r' && chunk[i] != '\n')
                i++;

            seg = i - segment_start;
            need = acc_len + seg + 1;

            if (need > ED_MAX_LINE_SIZE)
            {
                seg = (ED_MAX_LINE_SIZE > acc_len + 1) ? (ED_MAX_LINE_SIZE - acc_len - 1) : 0;
                need = acc_len + seg + 1;
            }

            if (need > acc_cap)
            {
                size_t nc = acc_cap;
                char *na = NULL;

                while (nc < need)
                    nc *= 2;

                if (nc > ED_MAX_LINE_SIZE + 1)
                    nc = ED_MAX_LINE_SIZE + 1;

                na = (char *)realloc(acc, nc);

                if (!na)
                {
                    free(acc);
                    free(cbuf);
                    return -1;
                }

                acc = na;
                acc_cap = nc;
            }

            if (seg > 0)
            {
                memcpy(acc + acc_len, chunk + segment_start, seg);
                acc_len += seg;
            }

            if (i < got && (chunk[i] == '\r' || chunk[i] == '\n'))
            {
                wchar_t *wcs = NULL;
                int wlen;
                int wrote;
                EdLine *ln = NULL;
                size_t cneed = acc_len * 3 + 16;

                if (cneed > cbuf_cap)
                {
                    char *ncb = (char *)realloc(cbuf, cneed);

                    if (!ncb)
                    {
                        free(acc);
                        free(cbuf);
                        return -1;
                    }

                    cbuf = ncb;
                    cbuf_cap = cneed;
                }

                acc[acc_len] = '\0';
                wrote = charset_body_to_utf8(charset, acc, (int)acc_len, cbuf, (int)cbuf_cap);

                if (wrote >= 0)
                {
                    cbuf[wrote] = '\0';
                    wcs = utf8_to_wcs(cbuf, &wlen);
                }

                if (wcs)
                {
                    ln = line_new_take(ed, wcs, wlen);

                    if (ln)
                        doc_insert_line(ed, ed->count, ln);
                }

                acc_len = 0;

                if (chunk[i] == '\r')
                {
                    i++;

                    if (i < got && chunk[i] == '\n')
                        i++;
                    else if (i == got)
                    {
                        int c = fgetc(fp);

                        if (c != '\n' && c != EOF)
                            ungetc(c, fp);
                    }
                }
                else
                {
                    i++;
                }
            }
        }
    }

    if (acc_len > 0)
    {
        wchar_t *wcs = NULL;
        int wlen;
        int wrote;
        EdLine *ln = NULL;
        size_t cneed = acc_len * 3 + 16;

        if (cneed > cbuf_cap)
        {
            char *ncb = (char *)realloc(cbuf, cneed);

            if (ncb)
            {
                cbuf = ncb;
                cbuf_cap = cneed;
            }
        }

        if (cbuf_cap >= cneed)
        {
            acc[acc_len] = '\0';
            wrote = charset_body_to_utf8(charset, acc, (int)acc_len, cbuf, (int)cbuf_cap);

            if (wrote >= 0)
            {
                cbuf[wrote] = '\0';
                wcs = utf8_to_wcs(cbuf, &wlen);

                if (wcs)
                {
                    ln = line_new_take(ed, wcs, wlen);

                    if (ln)
                        doc_insert_line(ed, ed->count, ln);
                }
            }
        }
    }

    free(acc);
    free(cbuf);

    if (ed->count == 0)
    {
        EdLine *ln = line_empty(ed);

        if (ln)
            doc_insert_line(ed, 0, ln);
    }

    return 0;
}

char *ed_to_string(const Ed *ed)
{
    char *out = NULL;
    size_t cap = 0;
    size_t used = 0;
    char *line_buf = NULL;
    int line_cap = ED_BUF_INITIAL_SIZE;
    int i;

    if (!ed)
        return NULL;

    if (ed->count == 0)
        return strdup("");

    line_buf = (char *)malloc((size_t)line_cap);

    if (!line_buf)
        return NULL;

    for (i = 0; i < ed->count; i++)
    {
        int line_len;

        for (;;)
        {
            line_len = ed_line_utf8(ed, i, line_buf, line_cap);

            if (line_len > 0)
                break;

            if (line_len == 0)
            {
                line_len = 0;
                break;
            }

            line_cap *= 2;
            {
                char *tmp = (char *)realloc(line_buf, (size_t)line_cap);

                if (!tmp)
                {
                    free(line_buf);
                    free(out);
                    return NULL;
                }

                line_buf = tmp;
            }
        }

        if (used + (size_t)line_len + 2 > cap)
        {
            size_t new_cap = cap ? cap * 2 : ED_BUF_INITIAL_SIZE;
            char *new_out = NULL;

            while (new_cap < used + (size_t)line_len + 2)
                new_cap *= 2;

            new_out = (char *)realloc(out, new_cap);

            if (!new_out)
            {
                free(line_buf);
                free(out);

                return NULL;
            }

            out = new_out;
            cap = new_cap;
        }

        memcpy(out + used, line_buf, (size_t)line_len);

        used += (size_t)line_len;
        out[used++] = '\n';
    }

    free(line_buf);

    if (!out)
        return strdup("");

    out[used] = '\0';
    return out;
}

/* Flush an accumulated UTF-8 buffer through optional charset conversion */
static int save_flush(FILE *fp, const char *charset_out, const char *buf, size_t len)
{
    if (len == 0)
        return 0;

    if (charset_out && charset_out[0] && strcasecmp(charset_out, "UTF-8") != 0 && strcasecmp(charset_out, "UTF8") != 0)
    {
        int dstsz = (int)(len * 2 + ED_CHARSET_CONV_PAD);
        char *converted = (char *)malloc((size_t)dstsz);

        if (converted)
        {
            int n = charset_body_from_utf8(charset_out, buf, (int)len, converted, dstsz);

            if (n > 0)
            {
                size_t nw = fwrite(converted, 1, (size_t)n, fp);

                free(converted);

                return (nw == (size_t)n) ? 0 : -1;
            }

            free(converted);
        }
    }

    return (fwrite(buf, 1, len, fp) == len) ? 0 : -1;
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
            char *tmp = NULL;
            line_len = ed_line_utf8(ed, i, line_buf, line_cap);

            if (line_len > 0)
                break;

            if (line_len == 0)
            {
                line_len = 0;
                break;
            }

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

        /* Line alone bigger than buffer: flush current buffer and write it directly */
        if ((size_t)line_len + 1 > SAVE_BUF_SIZE)
        {
            if (save_flush(fp, charset_out, buf, used) != 0 || save_flush(fp, charset_out, line_buf, line_len) != 0 || fwrite("\n", 1, 1, fp) != 1)
            {
                free(buf);
                free(line_buf);

                fclose(fp);
                return -1;
            }

            used = 0;
        }
        /* Current buffer would overflow: flush and start fresh */
        else if (used + (size_t)line_len + 1 > SAVE_BUF_SIZE)
        {
            if (save_flush(fp, charset_out, buf, used) != 0)
            {
                free(buf);
                free(line_buf);

                fclose(fp);
                return -1;
            }

            used = 0;

            memcpy(buf, line_buf, (size_t)line_len);

            used = (size_t)line_len;
            buf[used++] = '\n';
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

/* Serialise line range to UTF-8 for efficient paragraph-only snapshot */
char *ed_range_to_string(const Ed *ed, int start, int end)
{
    char **parts = NULL;
    int i;
    int n;
    int total = 0;
    char *out = NULL;
    char *p = NULL;
    int ok = 1;

    if (!ed || start < 0 || end <= start || start >= ed->count)
        return NULL;

    if (end > ed->count)
        end = ed->count;

    n = end - start;

    /* calloc so every slot starts NULL and cleanup can free uniformly */
    parts = (char **)calloc((size_t)n, sizeof(char *));

    if (!parts)
        return NULL;

    for (i = 0; i < n; i++)
    {
        EdLine *ln = ed->lines[start + i];

        parts[i] = line_to_utf8(ln, 0, ln->len);

        if (!parts[i])
        {
            /* Fall back to an empty line so OOM does not drop content */
            parts[i] = (char *)malloc(1);

            if (!parts[i])
            {
                ok = 0;
                break;
            }

            parts[i][0] = '\0';
        }

        total += (int)strlen(parts[i]) + 1; /* +1 for the newline */
    }

    if (ok)
    {
        out = (char *)malloc((size_t)(total + 1));

        if (out)
        {
            p = out;

            for (i = 0; i < n; i++)
            {
                int slen = (int)strlen(parts[i]);

                memcpy(p, parts[i], (size_t)slen);

                p += slen;
                *p++ = '\n';
            }

            *p = '\0';
        }
    }

    /* Uniform cleanup: unused slots are NULL, so free() is safe on all */
    for (i = 0; i < n; i++)
        free(parts[i]);

    free(parts);

    return out;
}

void ed_auto_rewrap_capture_pre_snapshot(Ed *ed)
{
    int first, last;
    int paragraph_size;

    if (!ed || ed->count <= 0)
        return;

    /* Snapshot empty line to allow undo of first word in fresh paragraph */
    if (ed->lines[ed->row]->len == 0)
    {
        free(ed->auto_rewrap_pre_snapshot);

        ed->auto_rewrap_pre_snapshot = ed_range_to_string(ed, ed->row, ed->row + 1);
        ed->auto_rewrap_pre_start = ed->row;
        ed->auto_rewrap_pre_end = ed->row + 1;
        ed->auto_rewrap_pre_cursor_row = ed->row;
        ed->auto_rewrap_pre_cursor_col = ed->col;
        return;
    }

    first = ed->row;

    while (first > 0 && ed->lines[first - 1]->len > 0)
        first--;

    last = ed->row;

    while (last < ed->count - 1 && ed->lines[last + 1]->len > 0)
        last++;

    paragraph_size = last - first + 1;

    /* Guard: don't try to serialize a multi-MB paragraph */
    if (paragraph_size > ED_AUTO_REWRAP_SNAPSHOT_LINE_CAP)
    {
        free(ed->auto_rewrap_pre_snapshot);

        ed->auto_rewrap_pre_snapshot = NULL;
        ed->auto_rewrap_pre_start = -1;
        ed->auto_rewrap_pre_end = -1;
        return;
    }

    free(ed->auto_rewrap_pre_snapshot);

    ed->auto_rewrap_pre_snapshot = ed_range_to_string(ed, first, last + 1);
    ed->auto_rewrap_pre_start = first;
    ed->auto_rewrap_pre_end = last + 1;
    ed->auto_rewrap_pre_cursor_row = ed->row;
    ed->auto_rewrap_pre_cursor_col = ed->col;
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

void ed_clamp(Ed *ed)
{
    /* Skip clamp on empty document to avoid reading freed line */
    if (ed->count <= 0)
    {
        ed->row = 0;
        ed->col = 0;
        return;
    }

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

    ed_clamp(ed);
}

/* Cursor movement */
void ed_move_up(Ed *ed)
{
    if (ed && ed->row > 0)
    {
        ed->row--;

        ed_clamp(ed);
        ed_ensure_visible(ed);
    }
}

void ed_move_down(Ed *ed)
{
    if (ed && ed->row < ed->count - 1)
    {
        ed->row++;

        ed_clamp(ed);
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
    EdLine *line = NULL;

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

    ed_clamp(ed);
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

    ed_clamp(ed);
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

static int is_wordch(Ed *ed, wchar_t ch)
{
    if (!ed)
        return 0;

    /* Vim-like: every non-space character is a word */
    if (ed->word_move_mode == 1)
        return ch != L' ' && ch != L'\t';

    /* Standard: ASCII alnum + underscore; non-ASCII as word chars */
    if (ch < 0x80)
        return isalnum((int)ch) || ch == '_';

    return 1; /* non-ASCII: treat as word char (accented letters etc) */
}

void ed_word_left(Ed *ed)
{
    EdLine *ln = NULL;

    if (!ed)
        return;

    ln = ed->lines[ed->row];

    if (ed->col == 0 && ed->row > 0)
    {
        ed->row--;
        ed->col = ed->lines[ed->row]->len;

        ed_ensure_visible(ed);
        return;
    }

    while (ed->col > 0 && !is_wordch(ed, (wchar_t)ed_line_char(ln, ed->col - 1)))
        ed->col--;

    while (ed->col > 0 && is_wordch(ed, (wchar_t)ed_line_char(ln, ed->col - 1)))
        ed->col--;
}

void ed_word_right(Ed *ed)
{
    EdLine *ln = NULL;

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

    while (ed->col < ln->len && is_wordch(ed, (wchar_t)ed_line_char(ln, ed->col)))
        ed->col++;

    while (ed->col < ln->len && !is_wordch(ed, (wchar_t)ed_line_char(ln, ed->col)))
        ed->col++;
}

void ed_set_word_move_mode(Ed *ed, int mode)
{
    if (ed)
        ed->word_move_mode = mode ? 1 : 0;
}

/* Editing */
int ed_insert_char(Ed *ed, wchar_t ch)
{
    EdLine *ln = NULL;

    if (!ed || ch == 0)
        return -1;

    if (ed->hard_wrap)
    {
        if (!ed->undo_snapshot_mode)
            ed_auto_rewrap_capture_pre_snapshot(ed);

        ed->undo_snapshot_mode = 1;
    }

    ln = ed->lines[ed->row];

    if (ed->insert_mode)
    {
        record_insert(ed, ed->row, ed->col, ch);

        if (line_insert(ed, ln, ed->col, ch) != 0)
            return -1;
    }
    else
    {
        if (ed->col < ln->len)
        {
            /* Overwrite, record delete of old char then insert new */
            record_delete_fwd(ed, ed->row, ed->col, (wchar_t)ed_line_char(ln, ed->col));

            ed_save_undo(ed);
            record_insert(ed, ed->row, ed->col, ch);

            line_set_char(ln, ed->col, (unsigned int)ch);
            ln->wrap_count_cache = -1;
            ed_wcs_view_reset(ed);

            if (ed->word_count_initialized)
            {
                int old_count = ln->word_count;

                ln->word_count = line_count_words(ln);
                ed->word_count_total += ln->word_count - old_count;
            }
        }
        else
        {
            record_insert(ed, ed->row, ed->col, ch);

            if (line_insert(ed, ln, ed->col, ch) != 0)
                return -1;
        }
    }

    ed->col++;

    ed_set_modified(ed, 1);

    ed_prefix_invalidate_from(ed, ed->row);

    return 0;
}

int ed_enter(Ed *ed)
{
    EdLine *ln = NULL;
    EdLine *nl = NULL;

    if (!ed)
        return -1;

    ln = ed->lines[ed->row];

    record_split(ed, ed->row, ed->col);

    nl = line_from_slice(ed, ln, ed->col, ln->len - ed->col);

    if (!nl)
        return -1;

    line_truncate(ed, ln, ed->col);

    if (doc_insert_line(ed, ed->row + 1, nl) != 0)
        return -1;

    ed->row++;
    ed->col = 0;

    ed_set_modified(ed, 1);

    ed_prefix_invalidate_from(ed, ed->row - 1);
    ed_ensure_visible(ed);

    return 0;
}

int ed_backspace(Ed *ed)
{
    EdLine *ln = NULL;
    EdLine *prev = NULL;
    wchar_t deleted_ch = 0;

    if (!ed)
        return -1;

    if (ed->hard_wrap)
    {
        if (!ed->undo_snapshot_mode)
            ed_auto_rewrap_capture_pre_snapshot(ed);

        ed->undo_snapshot_mode = 1;
    }

    ln = ed->lines[ed->row];

    if (ed->col > 0)
    {
        deleted_ch = (wchar_t)ed_line_char(ln, ed->col - 1);

        record_delete_back(ed, ed->row, ed->col - 1, deleted_ch);
        line_delete(ed, ln, ed->col - 1);

        ed->col--;

        ed_set_modified(ed, 1);

        ed_prefix_invalidate_from(ed, ed->row);
    }
    else if (ed->row > 0)
    {
        prev = ed->lines[ed->row - 1];

        record_join(ed, ed->row - 1, prev->len);

        ed->col = prev->len;

        /* Joining lines: remove the wrap-hyphen character and clear the flag */
        if (prev->has_wrap_hyphen && prev->len > 0)
        {
            unsigned int last_cp = ed_line_char(prev, prev->len - 1);

            if (last_cp == (unsigned int)'-')
            {
                prev->len--;
                ed->col = prev->len;
            }

            prev->has_wrap_hyphen = 0;
        }

        line_append_line(ed, prev, ln, 0, ln->len);
        line_free(doc_remove_line(ed, ed->row));

        ed->row--;

        ed_set_modified(ed, 1);
        ed_prefix_invalidate_from(ed, ed->row);

        ed_ensure_visible(ed);
    }

    return 0;
}

int ed_delete(Ed *ed)
{
    EdLine *ln = NULL;
    EdLine *nxt = NULL;
    wchar_t deleted_ch = 0;

    if (!ed)
        return -1;

    if (ed->hard_wrap)
    {
        if (!ed->undo_snapshot_mode)
            ed_auto_rewrap_capture_pre_snapshot(ed);

        ed->undo_snapshot_mode = 1;
    }

    ln = ed->lines[ed->row];

    if (ed->col < ln->len)
    {
        deleted_ch = (wchar_t)ed_line_char(ln, ed->col);

        record_delete_fwd(ed, ed->row, ed->col, deleted_ch);
        line_delete(ed, ln, ed->col);

        ed_set_modified(ed, 1);
        ed_prefix_invalidate_from(ed, ed->row);
    }
    else if (ed->row < ed->count - 1)
    {
        nxt = ed->lines[ed->row + 1];

        /* Del at EOL joins lines: record as join from next line's perspective */
        record_join(ed, ed->row, ln->len);

        /* Joining lines via Delete: remove the wrap-hyphen character and clear the flag */
        if (ln->has_wrap_hyphen && ln->len > 0)
        {
            unsigned int last_cp = ed_line_char(ln, ln->len - 1);

            if (last_cp == (unsigned int)'-')
                ln->len--;

            ln->has_wrap_hyphen = 0;
        }

        line_append_line(ed, ln, nxt, 0, nxt->len);
        line_free(doc_remove_line(ed, ed->row + 1));

        ed_set_modified(ed, 1);
        ed_prefix_invalidate_from(ed, ed->row);
    }

    return 0;
}

int ed_delete_line(Ed *ed)
{
    EdLine *deleted_line = NULL;
    wchar_t *deleted_text = NULL;
    int deleted_row;

    if (!ed)
        return -1;

    deleted_row = ed->row;

    if (ed->count <= 1)
    {
        /* Save line content before clearing */
        deleted_text = line_to_wcs(ed->lines[0]);

        line_truncate(ed, ed->lines[0], 0);
        ed->col = 0;
    }
    else
    {
        /* Save line content before deletion */
        deleted_line = ed->lines[ed->row];
        deleted_text = line_to_wcs(deleted_line);

        line_free(doc_remove_line(ed, ed->row));

        if (ed->row >= ed->count)
            ed->row = ed->count - 1;

        ed_clamp(ed);
    }

    /* Record undo operation */
    if (deleted_text)
    {
        if (ed->undo_snapshot_mode)
        {
            free(deleted_text);
        }
        else
        {
            /* Append \n to distinguish delete line from delete chars for undo */
            int dlen = (int)wcslen(deleted_text);
            wchar_t *with_nl = (wchar_t *)malloc((size_t)(dlen + 2) * sizeof(wchar_t));

            if (with_nl)
            {
                if (dlen > 0)
                    memcpy(with_nl, deleted_text, (size_t)dlen * sizeof(wchar_t));

                with_nl[dlen] = L'\n';
                with_nl[dlen + 1] = L'\0';

                ed_save_undo(ed);
                ed_redo_clear(ed);

                if (ed_undo_open_group(ed) == 0)
                {
                    /* Set cursor to deleted line so undo restores it there */
                    UndoGroup *g = &ed->undo_stack[ed->undo_top - 1];

                    g->cur_row = deleted_row;
                    g->cur_col = 0;

                    undo_push_op(ed, OP_DELETE, deleted_row, 0, with_nl, dlen + 1, 0);

                    ed->undo_open = 0;
                }

                free(with_nl);
            }

            free(deleted_text);
        }
    }

    ed_set_modified(ed, 1);
    ed_prefix_invalidate_from(ed, deleted_row);
    ed_ensure_visible(ed);

    return 0;
}

int ed_delete_to_eol(Ed *ed)
{
    EdLine *ln = NULL;
    wchar_t *deleted_text = NULL;
    int deleted_len;

    if (!ed)
        return -1;

    ln = ed->lines[ed->row];

    /* Save text before deletion */
    if (ed->col < ln->len)
    {
        deleted_text = line_to_wcs_range(ln, ed->col, ln->len);
        deleted_len = ln->len - ed->col;
    }

    line_truncate(ed, ed->lines[ed->row], ed->col);

    /* Record undo operation */
    if (deleted_text)
    {
        if (ed->undo_snapshot_mode)
        {
            free(deleted_text);
        }
        else
        {
            undo_push_op(ed, OP_DELETE, ed->row, ed->col, deleted_text, deleted_len, 0);
            free(deleted_text);
        }
    }

    ed_set_modified(ed, 1);
    ed_prefix_invalidate_from(ed, ed->row);

    return 0;
}

/* Delete from cursor backwards to start of previous word */
int ed_delete_word_left(Ed *ed)
{
    EdLine *ln = NULL;
    int target;
    int del_len;
    wchar_t *deleted_text = NULL;

    if (!ed)
        return -1;

    if (ed_undo_open_group(ed) != 0)
        return -1;

    if (ed->col == 0)
    {
        ed->undo_open = 0;
        return ed_backspace(ed);
    }

    ln = ed->lines[ed->row];
    target = ed->col;

    while (target > 0 && !is_wordch(ed, (wchar_t)ed_line_char(ln, target - 1)))
        target--;

    while (target > 0 && is_wordch(ed, (wchar_t)ed_line_char(ln, target - 1)))
        target--;

    /* Save text before deletion */
    if (ed->col > target)
        deleted_text = line_to_wcs_range(ln, target, ed->col);

    /* Capture length before loop (ed->col changes during deletion) */
    del_len = ed->col - target;

    while (ed->col > target)
    {
        line_delete(ed, ln, ed->col - 1);
        ed->col--;
    }

    /* Record undo operation */
    if (deleted_text)
    {
        if (ed->undo_snapshot_mode)
        {
            free(deleted_text);
        }
        else
        {
            if (undo_push_op(ed, OP_DELETE, ed->row, target, deleted_text, del_len, 0) != 0)
            {
                free(deleted_text);

                ed->undo_open = 0;

                return -1;
            }

            free(deleted_text);
        }
    }

    ed->undo_open = 0;

    ed_set_modified(ed, 1);
    ed_prefix_invalidate_from(ed, ed->row);

    return 0;
}

/* Delete from cursor forward through end of current word */
int ed_delete_word_right(Ed *ed)
{
    EdLine *ln = NULL;
    int target;
    wchar_t *deleted_text = NULL;

    if (!ed)
        return -1;

    if (ed_undo_open_group(ed) != 0)
        return -1;

    ln = ed->lines[ed->row];

    if (ed->col >= ln->len)
    {
        ed->undo_open = 0;
        return ed_delete(ed);
    }

    target = ed->col;

    while (target < ln->len && is_wordch(ed, (wchar_t)ed_line_char(ln, target)))
        target++;

    while (target < ln->len && !is_wordch(ed, (wchar_t)ed_line_char(ln, target)))
        target++;

    /* Save text before deletion */
    if (target > ed->col)
        deleted_text = line_to_wcs_range(ln, ed->col, target);

    while (ln->len > ed->col && ed->col < target)
    {
        line_delete(ed, ln, ed->col);
        target--;
    }

    /* Record undo operation */
    if (deleted_text)
    {
        if (ed->undo_snapshot_mode)
        {
            free(deleted_text);
        }
        else
        {
            if (undo_push_op(ed, OP_DELETE, ed->row, ed->col, deleted_text, wcslen(deleted_text), 0) != 0)
            {
                free(deleted_text);

                ed->undo_open = 0;
                return -1;
            }

            free(deleted_text);
        }
    }

    ed->undo_open = 0;

    ed_set_modified(ed, 1);

    ed_prefix_invalidate_from(ed, ed->row);

    return 0;
}

int ed_duplicate_line(Ed *ed)
{
    EdLine *ln = NULL;
    EdLine *dup = NULL;

    if (!ed)
        return -1;

    ln = ed->lines[ed->row];

    dup = line_from_slice(ed, ln, 0, ln->len);

    if (!dup)
        return -1;

    dup->has_wrap_hyphen = ln->has_wrap_hyphen;

    /* Record undo operation - split at current position */
    if (!ed->undo_snapshot_mode)
        undo_push_op(ed, OP_SPLIT, ed->row, ed->col, NULL, 0, 0);

    if (doc_insert_line(ed, ed->row + 1, dup) != 0)
        return -1;

    ed->row++;

    ed_set_modified(ed, 1);
    ed_prefix_invalidate_from(ed, ed->row - 1);
    ed_ensure_visible(ed);

    return 0;
}

/* Move the current line up, or the selected block if active */
int ed_move_line_up(Ed *ed)
{
    int top;
    int bot;
    EdLine *moved = NULL;
    int had_block = 0;
    int start;
    int old_count;
    int new_count;
    int cur_row;
    int cur_col;
    char *snapshot_before = NULL;
    char *snapshot_after = NULL;

    if (!ed || ed->count <= 1)
        return -1;

    if (ed->block.active)
    {
        int r1, c1, r2, c2;

        block_range(ed, &r1, &c1, &r2, &c2);

        top = r1;
        bot = r2;
        had_block = 1;
    }
    else
    {
        top = ed->row;
        bot = ed->row;
    }

    if (top <= 0)
        return -1;

    cur_row = ed->row;
    cur_col = ed->col;
    start = top - 1;
    old_count = bot - start + 1;
    new_count = old_count;

    snapshot_before = ed_range_to_string(ed, start, bot + 1);

    if (!snapshot_before)
        return -1;

    if (ed_undo_open_group(ed) != 0)
    {
        free(snapshot_before);
        return -1;
    }

    /* Pull line (top-1) and re-insert at `bot`. Net effect: the block slides up by one row */
    moved = doc_remove_line(ed, top - 1);

    if (!moved)
    {
        free(snapshot_before);
        return -1;
    }

    if (doc_insert_line(ed, bot, moved) != 0)
    {
        line_free(moved);
        free(snapshot_before);
        return -1;
    }

    ed->row--;

    if (ed->row < 0)
        ed->row = 0;

    if (had_block)
    {
        ed->block.anchor_row--;

        if (ed->block.anchor_row < 0)
            ed->block.anchor_row = 0;
    }

    ed_clamp(ed);

    ed_set_modified(ed, 1);
    ed_prefix_invalidate_from(ed, top - 1);

    snapshot_after = ed_range_to_string(ed, start, bot + 1);

    if (snapshot_after)
        undo_push_snapshot_range(ed, start, cur_col, snapshot_before, snapshot_after, old_count, new_count, cur_row, cur_col, ed->row, ed->col);
    else
        free(snapshot_before);

    ed->undo_open = 0;
    ed_ensure_visible(ed);

    return 0;
}

/* Move the current line (or active block) one position down */
int ed_move_line_down(Ed *ed)
{
    int top;
    int bot;
    EdLine *moved = NULL;
    int had_block = 0;
    int start;
    int old_count;
    int new_count;
    int cur_row;
    int cur_col;
    char *snapshot_before = NULL;
    char *snapshot_after = NULL;

    if (!ed || ed->count <= 1)
        return -1;

    if (ed->block.active)
    {
        int r1;
        int c1;
        int r2;
        int c2;

        block_range(ed, &r1, &c1, &r2, &c2);

        top = r1;
        bot = r2;
        had_block = 1;
    }
    else
    {
        top = ed->row;
        bot = ed->row;
    }

    if (bot >= ed->count - 1)
        return -1;

    cur_row = ed->row;
    cur_col = ed->col;
    start = top;
    old_count = bot + 1 - start + 1;
    new_count = old_count;

    snapshot_before = ed_range_to_string(ed, start, bot + 2);

    if (!snapshot_before)
        return -1;

    if (ed_undo_open_group(ed) != 0)
    {
        free(snapshot_before);
        return -1;
    }

    /* Pull line (bot+1) and re-insert at `top` */
    moved = doc_remove_line(ed, bot + 1);

    if (!moved)
    {
        free(snapshot_before);
        return -1;
    }

    if (doc_insert_line(ed, top, moved) != 0)
    {
        line_free(moved);
        free(snapshot_before);
        return -1;
    }

    ed->row++;

    if (ed->row >= ed->count)
        ed->row = ed->count - 1;

    if (had_block)
    {
        ed->block.anchor_row++;

        if (ed->block.anchor_row >= ed->count)
            ed->block.anchor_row = ed->count - 1;
    }

    ed_clamp(ed);

    ed_set_modified(ed, 1);
    ed_prefix_invalidate_from(ed, top);

    snapshot_after = ed_range_to_string(ed, start, bot + 2);

    if (snapshot_after)
        undo_push_snapshot_range(ed, start, cur_col, snapshot_before, snapshot_after, old_count, new_count, cur_row, cur_col, ed->row, ed->col);
    else
        free(snapshot_before);

    ed->undo_open = 0;
    ed_ensure_visible(ed);

    return 0;
}

int ed_insert_tab(Ed *ed, int ts)
{
    (void)ts;

    if (!ed)
        return -1;

    /* Open undo group for tab insertion */
    if (ed_undo_open_group(ed) != 0)
        return -1;

    if (ed_insert_char(ed, L'\t') != 0)
    {
        ed->undo_open = 0;
        return -1;
    }

    ed->undo_open = 0;
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
    size_t need = 0;
    size_t pos = 0;
    wchar_t *buf = NULL;

    block_range(ed, &r1, &c1, &r2, &c2);

    for (i = r1; i <= r2; i++)
    {
        int s = (i == r1) ? c1 : 0, e = (i == r2) ? c2 : ed->lines[i]->len;

        if (s > ed->lines[i]->len)
            s = ed->lines[i]->len;

        if (e > ed->lines[i]->len)
            e = ed->lines[i]->len;

        need += (e > s) ? (size_t)(e - s) : 0;

        if (i < r2)
            need++;
    }

    /* Check for overflow before multiplication */
    if (need > (SIZE_MAX - 1) / sizeof(wchar_t))
    {
        if (out_len)
            *out_len = 0;

        return NULL;
    }

    buf = (wchar_t *)malloc((need + 1) * sizeof(wchar_t));

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
            line_copy_out(ed->lines[i], s, ln, &buf[pos]);

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
    wchar_t *t = NULL;
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
    wchar_t *wcs = NULL;
    int wcs_len;
    char *utf8 = NULL;

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
    char *cut_text = NULL;

    if (!ed || !ed->block.active)
        return -1;

    block_range(ed, &r1, &c1, &r2, &c2);

    /* Save the block content before cutting */
    cut_text = ed_block_to_string(ed, r1, c1, r2, c2);

    if (!cut_text)
        return -1;

    /* Open undo group for block cut operation */
    if (ed->undo_snapshot_mode)
    {
        free(cut_text);
        return -1;
    }

    if (ed_undo_open_group(ed) != 0)
    {
        free(cut_text);
        return -1;
    }

    if (ed_block_copy(ed) != 0)
    {
        free(cut_text);

        ed->undo_open = 0;
        return -1;
    }

    if (r1 == r2)
    {
        if (c1 < c2)
        {
            /* Save deleted text */
            wchar_t *deleted = line_to_wcs_range(ed->lines[r1], c1, c2);

            if (deleted)
            {
                undo_push_op(ed, OP_DELETE, r1, c1, deleted, c2 - c1, 0);
                free(deleted);
            }

            line_delete_range(ed, ed->lines[r1], c1, c2 - c1);
        }
    }
    else
    {
        EdLine *first = ed->lines[r1], *last = ed->lines[r2];
        int from;
        int upto;
        int k;

        /* Record deletion of middle lines */
        for (i = r1 + 1; i < r2; i++)
        {
            wchar_t *line_text = line_to_wcs(ed->lines[i]);

            if (line_text)
            {
                undo_push_op(ed, OP_DELETE, i, 0, line_text, wcslen(line_text), 0);

                free(line_text);
            }
        }

        /* Record truncation of first line */
        if (c1 < first->len)
        {
            wchar_t *truncated = line_to_wcs_range(first, c1, first->len);

            if (truncated)
            {
                undo_push_op(ed, OP_DELETE, r1, c1, truncated, wcslen(truncated), 0);

                free(truncated);
            }
        }

        /* Record deletion from last line */
        if (c2 < last->len)
        {
            wchar_t *deleted = line_to_wcs_range(last, 0, c2);

            if (deleted)
            {
                undo_push_op(ed, OP_DELETE, r1, c1, deleted, wcslen(deleted), 0);

                free(deleted);
            }
        }

        line_truncate(ed, first, c1);

        if (c2 < last->len)
            line_append_line(ed, first, last, c2, last->len - c2);

        /* Bulk-delete lines r1+1 .. r2 (was K calls to doc_remove_line) */
        from = r1 + 1;
        upto = r2;
        k = upto - from + 1;

        if (k > 0 && from < ed->count)
        {
            int j;

            if (upto >= ed->count)
                upto = ed->count - 1;

            k = upto - from + 1;

            for (j = from; j <= upto; j++)
            {
                if (ed->word_count_initialized)
                    ed->word_count_total -= ed->lines[j]->word_count;

                line_free(ed->lines[j]);
            }

            if (upto < ed->count - 1)
                memmove(&ed->lines[from], &ed->lines[upto + 1], (size_t)(ed->count - upto - 1) * sizeof(EdLine *));

            ed->count -= k;
        }
    }

    ed->row = r1;
    ed->col = c1;

    ed_clamp(ed);
    ed->undo_open = 0;

    ed_set_modified(ed, 1);

    ed_prefix_invalidate_from(ed, r1);
    ed_ensure_visible(ed);

    free(cut_text);
    return 0;
}

int ed_block_delete(Ed *ed)
{
    int r1;
    int c1;
    int r2;
    int c2;
    int old_count;
    int new_count;
    char *snapshot_before = NULL;
    char *snapshot_after = NULL;
    EdLine *first = NULL;
    EdLine *last = NULL;

    if (!ed || !ed->block.active)
        return -1;

    block_range(ed, &r1, &c1, &r2, &c2);

    /* Open undo group for block delete operation */
    if (ed->undo_snapshot_mode)
        return -1;

    if (ed_undo_open_group(ed) != 0)
        return -1;

    /* Save snapshot of affected range before deletion */
    old_count = r2 - r1 + 1;
    snapshot_before = ed_range_to_string(ed, r1, r2 + 1);

    if (!snapshot_before)
    {
        ed->undo_open = 0;
        return -1;
    }

    if (r1 == r2)
    {
        if (c1 < c2)
        {
            line_delete_range(ed, ed->lines[r1], c1, c2 - c1);
        }
    }
    else
    {
        int from;
        int upto;
        int k;

        first = ed->lines[r1];
        last = ed->lines[r2];

        line_truncate(ed, first, c1);

        if (c2 < last->len)
            line_append_line(ed, first, last, c2, last->len - c2);

        /* Bulk-delete lines r1+1 .. r2 (was K calls to doc_remove_line) */
        from = r1 + 1;
        upto = r2;
        k = upto - from + 1;

        if (k > 0 && from < ed->count)
        {
            int j;

            if (upto >= ed->count)
                upto = ed->count - 1;

            k = upto - from + 1;

            /* Free EdLines we are dropping; subtract from word count cache if it's live */
            for (j = from; j <= upto; j++)
            {
                if (ed->word_count_initialized)
                    ed->word_count_total -= ed->lines[j]->word_count;

                line_free(ed->lines[j]);
            }

            /* Single memmove to collapse the tail */
            if (upto < ed->count - 1)
                memmove(&ed->lines[from], &ed->lines[upto + 1], (size_t)(ed->count - upto - 1) * sizeof(EdLine *));

            ed->count -= k;
        }
    }

    ed->row = r1;
    ed->col = c1;

    ed_clamp(ed);
    ed->undo_open = 0;

    ed_set_modified(ed, 1);
    ed_prefix_invalidate_from(ed, r1);
    ed_ensure_visible(ed);

    /* Save snapshot of affected range after deletion */
    new_count = 1;
    snapshot_after = ed_range_to_string(ed, r1, r1 + 1);

    if (!snapshot_after)
    {
        free(snapshot_before);

        ed_block_clear(ed);
        return -1;
    }

    /* Push OP_SNAPSHOT_RANGE with both snapshots */
    if (undo_push_snapshot_range(ed, r1, c1, snapshot_before, snapshot_after, old_count, new_count, r1, c1, r1, c1) != 0)
    {
        /* undo_push_snapshot_range freed both snapshots on failure */
        ed_block_clear(ed);
        return -1;
    }

    /* Clear block selection after delete */
    ed_block_clear(ed);

    return 0;
}

int ed_block_paste(Ed *ed)
{
    char *utf8 = NULL;

    if (!ed || !ed->killbuf || !ed->killlen)
        return -1;

    utf8 = wcs_to_utf8(ed->killbuf, ed->killlen);

    if (!utf8)
        return -1;

    if (ed_paste_text_with_undo(ed, utf8) != 0)
    {
        free(utf8);

        return -1;
    }

    free(utf8);

    return 0;
}

/* Drop redo stack (called on any new edit) */
void ed_redo_clear(Ed *ed)
{
    if (!ed->redo_stack)
        return;

    undo_stack_clear(ed->redo_stack, ed->redo_top);

    free(ed->redo_stack);

    ed->redo_stack = NULL;
    ed->redo_top = 0;
    ed->redo_cap = 0;
}

/* Clear both undo and redo stacks (used before loading a new document) */
void ed_clear_undo_redo(Ed *ed)
{
    if (!ed)
        return;

    ed_save_undo(ed);

    if (ed->undo_stack)
    {
        undo_stack_clear(ed->undo_stack, ed->undo_top);

        free(ed->undo_stack);

        ed->undo_stack = NULL;
        ed->undo_top = 0;
        ed->undo_cap = 0;
    }

    if (ed->redo_stack)
    {
        undo_stack_clear(ed->redo_stack, ed->redo_top);

        free(ed->redo_stack);

        ed->redo_stack = NULL;
        ed->redo_top = 0;
        ed->redo_cap = 0;
    }

    ed->undo_open = 0;
}

/* Ensure the undo stack has room for one more group */
int ed_undo_stack_make_room(UndoGroup **stack, int *top, int *cap, int max)
{
    UndoGroup *t = NULL;
    int nc;

    if (!stack || !top || !cap)
        return -1;

    if (*top < *cap)
        return 0;

    if (max <= 0)
        return -1;

    nc = (*cap > 0) ? (*cap * 2) : 8;

    if (nc > max)
        nc = max;

    if (nc <= *cap)
    {
        /* At max depth: drop oldest entry */
        if (*top <= 0)
            return -1;

        undo_group_clear(&(*stack)[0]);
        memmove(&(*stack)[0], &(*stack)[1], (size_t)(*top - 1) * sizeof(UndoGroup));

        (*top)--;
        return 0;
    }

    t = (UndoGroup *)realloc(*stack, (size_t)nc * sizeof(UndoGroup));

    if (!t)
        return -1;

    *stack = t;
    *cap = nc;

    return 0;
}

/* Open a new group on top of the undo stack */
int ed_undo_open_group(Ed *ed)
{
    UndoGroup *g = NULL;

    if (ed_undo_stack_make_room(&ed->undo_stack, &ed->undo_top, &ed->undo_cap, ed->undo_max) != 0)
        return -1;

    g = &ed->undo_stack[ed->undo_top];

    g->ops = NULL;
    g->count = 0;
    g->cap = 0;
    g->cur_row = ed->row;
    g->cur_col = ed->col;
    g->end_row = ed->row;
    g->end_col = ed->col;
    ed->undo_top++;
    ed->undo_open = 1;

    return 0;
}

/* Append one op to the current (top) group */
static int undo_push_op(Ed *ed, UndoOpType type, int row, int col, const wchar_t *text, int len, int join_col)
{
    UndoGroup *g = NULL;
    UndoOp *op = NULL;
    UndoOp *t = NULL;
    int nc;

    if (ed->undo_top <= 0)
        return -1;

    g = &ed->undo_stack[ed->undo_top - 1];

    if (g->count >= g->cap)
    {
        nc = (g->cap > 0) ? (g->cap * 2) : 4;
        t = (UndoOp *)realloc(g->ops, (size_t)nc * sizeof(UndoOp));

        if (!t)
            return -1;

        g->ops = t;
        g->cap = nc;
    }

    op = &g->ops[g->count++];
    op->type = type;
    op->row = row;
    op->col = col;
    op->len = len;
    op->cap = 0;
    op->join_col = join_col;
    op->text = NULL;
    op->utf8_snapshot = NULL;
    op->utf8_snapshot_new = NULL;

    if ((type == OP_INSERT || type == OP_DELETE) && text && (len > 0 || (type == OP_DELETE && col == 0)))
    {
        op->text = (wchar_t *)malloc((size_t)(len + 1) * sizeof(wchar_t));

        if (!op->text)
        {
            g->count--;
            return -1;
        }

        op->cap = len + 1;
        wmemcpy(op->text, text, (size_t)len);
        op->text[len] = L'\0';
    }

    return 0;
}

/* Push an OP_SNAPSHOT_RANGE operation to the current undo group */
int undo_push_snapshot_range(Ed *ed, int row, int col, char *snapshot_before, char *snapshot_after, int old_count, int new_count, int cur_row, int cur_col, int end_row, int end_col)
{
    UndoGroup *g = NULL;
    UndoOp *t = NULL;
    int nc;

    if (ed->undo_top <= 0)
    {
        /* Function owns snapshots and frees them on any failure path */
        free(snapshot_before);
        free(snapshot_after);

        return -1;
    }

    g = &ed->undo_stack[ed->undo_top - 1];

    g->cur_row = cur_row;
    g->cur_col = cur_col;
    g->end_row = end_row;
    g->end_col = end_col;

    if (g->count >= g->cap)
    {
        nc = (g->cap > 0) ? (g->cap * 2) : 4;
        t = (UndoOp *)realloc(g->ops, (size_t)nc * sizeof(UndoOp));

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
    g->ops[g->count].row = row;
    g->ops[g->count].col = col;
    g->ops[g->count].len = 0;
    g->ops[g->count].cap = 0;
    g->ops[g->count].join_col = 0;
    g->ops[g->count].text = NULL;
    g->ops[g->count].utf8_snapshot = snapshot_before;
    g->ops[g->count].utf8_snapshot_new = snapshot_after;
    g->ops[g->count].end_row = old_count;
    g->ops[g->count].end_col = new_count;
    g->count++;

    return 0;
}

/* Grow op->text geometrically to hold need chars + NUL */
static int undo_text_grow(UndoOp *op, int need)
{
    int nc;
    wchar_t *t;

    if (op->cap >= need + 1)
        return 0;

    nc = op->cap > 0 ? op->cap : ED_UNDO_TEXT_INIT_CAP;

    while (nc < need + 1)
        nc *= 2;

    t = (wchar_t *)realloc(op->text, (size_t)nc * sizeof(wchar_t));

    if (!t)
        return -1;

    op->text = t;
    op->cap = nc;

    return 0;
}

/* Append a single wchar_t to the text of the last INSERT op */
static int undo_coalesce_insert(Ed *ed, wchar_t ch)
{
    UndoGroup *g = NULL;
    UndoOp *op = NULL;

    if (ed->undo_top <= 0)
        return -1;

    g = &ed->undo_stack[ed->undo_top - 1];

    if (g->count <= 0)
        return -1;

    op = &g->ops[g->count - 1];

    if (op->type != OP_INSERT)
        return -1;

    if (undo_text_grow(op, op->len + 1) != 0)
        return -1;

    op->text[op->len++] = ch;
    op->text[op->len] = L'\0';

    return 0;
}

/* Prepend a wchar_t to the last DELETE op's text */
static int undo_coalesce_delete_prepend(Ed *ed, wchar_t ch)
{
    UndoGroup *g = NULL;
    UndoOp *op = NULL;

    if (ed->undo_top <= 0)
        return -1;

    g = &ed->undo_stack[ed->undo_top - 1];

    if (g->count <= 0)
        return -1;

    op = &g->ops[g->count - 1];

    if (op->type != OP_DELETE)
        return -1;

    if (undo_text_grow(op, op->len + 1) != 0)
        return -1;

    /* Shift existing chars right by 1 and place ch at the front */
    wmemmove(&op->text[1], op->text, (size_t)op->len);

    op->text[0] = ch;
    op->len++;
    op->text[op->len] = L'\0';
    op->col--; /* deletion site moves one left */

    return 0;
}

/* Append a single wchar_t to the text of the last DELETE op (Del key) */
static int undo_coalesce_delete_append(Ed *ed, wchar_t ch)
{
    UndoGroup *g = NULL;
    UndoOp *op = NULL;

    if (ed->undo_top <= 0)
        return -1;

    g = &ed->undo_stack[ed->undo_top - 1];

    if (g->count <= 0)
        return -1;

    op = &g->ops[g->count - 1];

    if (op->type != OP_DELETE)
        return -1;

    if (undo_text_grow(op, op->len + 1) != 0)
        return -1;

    op->text[op->len++] = ch;
    op->text[op->len] = L'\0';

    return 0;
}

/* Record insert at (row, col), coalescing with previous if possible */
static void record_insert(Ed *ed, int row, int col, wchar_t ch)
{
    if (ed->undo_max <= 0)
        return;

    if (ed->undo_snapshot_mode)
        return;

    if (ed->undo_open && ed->undo_top > 0 && ed->undo_last_op == OP_INSERT && ed->undo_last_row == row && ed->undo_last_col_end == col)
    {
        if (undo_coalesce_insert(ed, ch) == 0)
        {
            ed->undo_last_col_end++;
            return;
        }
    }

    /* New op: ensure open group */
    if (!ed->undo_open || ed->undo_top == 0)
    {
        ed_redo_clear(ed);

        if (ed_undo_open_group(ed) != 0)
            return;
    }

    if (undo_push_op(ed, OP_INSERT, row, col, &ch, 1, 0) != 0)
        return;

    ed->undo_open = 1;
    ed->undo_last_op = OP_INSERT;
    ed->undo_last_row = row;
    ed->undo_last_col_end = col + 1;
}

/* Record a backspace-delete of ch that was at (row, col) before deletion */
static void record_delete_back(Ed *ed, int row, int col, wchar_t ch)
{
    if (ed->undo_max <= 0)
        return;

    if (ed->undo_snapshot_mode)
        return;

    /* Coalesce: consecutive backspaces on same row, cursor moving left */
    if (ed->undo_open && ed->undo_top > 0 && ed->undo_last_op == OP_DELETE && ed->undo_last_row == row && ed->undo_last_col_end == col + 1)
    {
        if (undo_coalesce_delete_prepend(ed, ch) == 0)
        {
            ed->undo_last_col_end = col;
            return;
        }
    }

    if (!ed->undo_open || ed->undo_top == 0)
    {
        ed_redo_clear(ed);

        if (ed_undo_open_group(ed) != 0)
            return;
    }

    if (undo_push_op(ed, OP_DELETE, row, col, &ch, 1, 0) != 0)
        return;

    ed->undo_open = 1;
    ed->undo_last_op = OP_DELETE;
    ed->undo_last_row = row;
    ed->undo_last_col_end = col;
}

/* Record a forward-delete (Del key) of ch at (row, col) */
static void record_delete_fwd(Ed *ed, int row, int col, wchar_t ch)
{
    if (ed->undo_max <= 0)
        return;

    if (ed->undo_snapshot_mode)
        return;

    /* Coalesce: consecutive Del presses at same position */
    if (ed->undo_open && ed->undo_top > 0 && ed->undo_last_op == OP_DELETE && ed->undo_last_row == row && ed->undo_last_col_end == col)
    {
        if (undo_coalesce_delete_append(ed, ch) == 0)
            return;
    }

    if (!ed->undo_open || ed->undo_top == 0)
    {
        ed_redo_clear(ed);

        if (ed_undo_open_group(ed) != 0)
            return;
    }

    if (undo_push_op(ed, OP_DELETE, row, col, &ch, 1, 0) != 0)
        return;

    ed->undo_open = 1;
    ed->undo_last_op = OP_DELETE;
    ed->undo_last_row = row;
    ed->undo_last_col_end = col;
}

/* Record an Enter (split) at (row, col). Always a break point */
static void record_split(Ed *ed, int row, int col)
{
    if (ed->undo_max <= 0)
        return;

    if (ed->undo_snapshot_mode)
        return;

    /* Close any open group before starting a new one */
    ed_save_undo(ed);
    ed_redo_clear(ed);

    if (ed_undo_open_group(ed) != 0)
        return;

    undo_push_op(ed, OP_SPLIT, row, col, NULL, 0, 0);

    ed->undo_open = 0; /* each Enter is its own group */
    ed->undo_last_op = OP_SPLIT;
    ed->undo_last_row = row;
    ed->undo_last_col_end = 0;
}

/* Record a join (backspace at col 0) at row, prev_line_len = join_col */
static void record_join(Ed *ed, int row, int join_col)
{
    if (ed->undo_max <= 0)
        return;

    if (ed->undo_snapshot_mode)
        return;

    /* Close any open group before starting a new one */
    ed_save_undo(ed);
    ed_redo_clear(ed);

    if (ed_undo_open_group(ed) != 0)
        return;

    undo_push_op(ed, OP_JOIN, row, 0, NULL, 0, join_col);

    ed->undo_open = 0;
    ed->undo_last_op = OP_JOIN;
    ed->undo_last_row = row;
    ed->undo_last_col_end = 0;
}

/* Insert multiple lines at once (O(N) total, no per-line memmove) */
static int doc_insert_lines_bulk(Ed *ed, int start, EdLine **lines, int count)
{
    int i;
    int needed;

    if (!ed || !lines || count <= 0)
        return 0;

    if (start < 0)
        start = 0;

    if (start > ed->count)
        start = ed->count;

    if (ed->count > INT_MAX - count)
        return -1;

    needed = ed->count + count;

    while (needed > ed->alloc)
    {
        int na = ed->alloc > 0 ? ed->alloc * 2 : INIT_ALLOC;
        EdLine **t = (EdLine **)realloc(ed->lines, (size_t)na * sizeof(EdLine *));

        if (!t)
            return -1;

        ed->lines = t;
        ed->alloc = na;
    }

    if (start < ed->count)
        memmove(&ed->lines[start + count], &ed->lines[start], (size_t)(ed->count - start) * sizeof(EdLine *));

    memcpy(&ed->lines[start], lines, (size_t)count * sizeof(EdLine *));

    if (ed->word_count_initialized)
    {
        for (i = 0; i < count; i++)
        {
            lines[i]->word_count = line_count_words(lines[i]);
            ed->word_count_total += lines[i]->word_count;
        }
    }

    ed->count += count;

    return 0;
}

/* Count newline-terminated lines in a UTF-8 buffer */
static int count_utf8_lines(const char *utf8_text)
{
    const char *p = utf8_text;
    int count = 0;

    if (!p)
        return 0;

    while (*p)
    {
        count++;

        while (*p && *p != '\r' && *p != '\n')
            p++;

        if (*p == '\r')
            p++;

        if (*p == '\n')
            p++;
    }

    return count;
}

/* Replace line range with UTF-8 text for OP_SNAPSHOT_RANGE undo/redo */
int ed_replace_range_from_utf8(Ed *ed, int start, int count_to_remove, const char *utf8_text)
{
    const char *p = NULL;
    int i;
    int inserted = 0;
    int line_count = 0;
    EdLine **new_lines = NULL;
    int new_lines_count = 0;

    if (!ed || start < 0)
        return -1;

    if (start > ed->count)
        start = ed->count;

    /* Lines are about to move/free, so drop any decoded views */
    ed_wcs_view_reset(ed);

    /* Bulk remove old lines (free + single memmove) */
    if (count_to_remove > 0 && start < ed->count)
    {
        int upto = start + count_to_remove - 1;
        int k;
        int j;

        if (upto >= ed->count)
            upto = ed->count - 1;

        k = upto - start + 1;

        for (j = start; j <= upto; j++)
        {
            if (ed->word_count_initialized)
                ed->word_count_total -= ed->lines[j]->word_count;

            line_free(ed->lines[j]);
        }

        if (upto < ed->count - 1)
            memmove(&ed->lines[start], &ed->lines[upto + 1], (size_t)(ed->count - upto - 1) * sizeof(EdLine *));

        ed->count -= k;
    }

    /* No new content -- ensure at least one line in doc */
    if (!utf8_text)
    {
        if (ed->count == 0)
        {
            EdLine *ln = line_empty(ed);

            if (ln && doc_grow(ed) == 0)
            {
                ed->lines[0] = ln;
                ed->count = 1;
            }
            else if (ln)
            {
                line_free(ln);
            }
        }

        return 0;
    }

    /* Count new lines and allocate array once */
    line_count = count_utf8_lines(utf8_text);

    if (line_count > 0)
    {
        new_lines = (EdLine **)malloc((size_t)line_count * sizeof(EdLine *));

        if (!new_lines)
            return 0;
    }

    /* Parse UTF-8 and fill array */
    p = utf8_text;

    while (*p)
    {
        const char *line_start = p;
        int blen;
        char *line_utf8 = NULL;
        wchar_t *wcs = NULL;
        int wlen;
        EdLine *ln = NULL;

        while (*p && *p != '\r' && *p != '\n')
            p++;

        blen = (int)(p - line_start);

        line_utf8 = (char *)malloc((size_t)(blen + 1));

        if (!line_utf8)
        {
            if (*p == '\r')
                p++;

            if (*p == '\n')
                p++;

            continue;
        }

        memcpy(line_utf8, line_start, (size_t)blen);
        line_utf8[blen] = '\0';

        wcs = utf8_to_wcs(line_utf8, &wlen);
        free(line_utf8);

        if (wcs)
        {
            ln = line_new_take(ed, wcs, wlen);

            if (ln && new_lines)
                new_lines[new_lines_count++] = ln;
        }

        if (*p == '\r')
            p++;

        if (*p == '\n')
            p++;
    }

    /* Bulk insert parsed lines */
    if (new_lines_count > 0)
    {
        if (doc_insert_lines_bulk(ed, start, new_lines, new_lines_count) == 0)
        {
            inserted = new_lines_count;

            /* Mark artificial wrap-hyphens inserted by rewrap */
            for (i = 0; i < new_lines_count - 1; i++)
            {
                EdLine *ln = new_lines[i];

                if (ln->len >= 2)
                {
                    unsigned int last = ed_line_char(ln, ln->len - 1);
                    unsigned int prev = ed_line_char(ln, ln->len - 2);

                    if (last == (unsigned int)'-' && prev != (unsigned int)' ' && prev != (unsigned int)'\t')
                        ln->has_wrap_hyphen = 1;
                }
            }
        }
        else
        {
            for (i = 0; i < new_lines_count; i++)
                line_free(new_lines[i]);
        }
    }

    free(new_lines);

    /* Document must always have at least one line */
    if (ed->count == 0)
    {
        EdLine *ln = line_empty(ed);

        if (ln && doc_grow(ed) == 0)
        {
            ed->lines[0] = ln;
            ed->count = 1;
        }
        else if (ln)
        {
            line_free(ln);
        }
    }

    return inserted;
}

static int apply_group_reverse(Ed *ed, UndoGroup *g)
{
    int i;
    UndoOp *op = NULL;
    EdLine *prev = NULL;
    EdLine *cur = NULL;
    EdLine *nl = NULL;
    int min_row = 0;
    int lines_inserted = 0; /* Track how many lines we've inserted to adjust row indices */

    for (i = g->count - 1; i >= 0; i--)
    {
        op = &g->ops[i];

        switch (op->type)
        {
        case OP_INSERT:
            /* Undo insert: delete the inserted chars */
            if (op->row < ed->count)
            {
                line_delete_range(ed, ed->lines[op->row], op->col, op->len);

                if (op->row < min_row)
                    min_row = op->row;
            }

            break;

        case OP_DELETE:
            /* Undo delete: re-insert the deleted chars */
            if (op->text)
            {
                /* Check if this is a line deletion (text contains newline) or character deletion */
                int is_line_delete = 0;
                int k;

                /* Line deletion: text contains newline character */
                for (k = 0; k < op->len; k++)
                {
                    if (op->text[k] == L'\n')
                    {
                        is_line_delete = 1;
                        break;
                    }
                }

                /* Line deletion can insert at or after end; char deletion needs valid row */
                if (is_line_delete && op->row <= ed->count)
                {
                    int adjusted_row = op->row + lines_inserted;
                    EdLine *nl = line_new(ed, op->text, op->len);

                    if (nl)
                    {
                        doc_insert_line(ed, adjusted_row, nl);
                        lines_inserted++;

                        if (adjusted_row < min_row)
                            min_row = adjusted_row;
                    }
                }
                else if (op->row < ed->count)
                {
                    /* Character/range deletion: insert inline */
                    int j;

                    for (j = 0; j < op->len; j++)
                        line_insert(ed, ed->lines[op->row], op->col + j, op->text[j]);

                    if (op->row < min_row)
                        min_row = op->row;
                }
            }

            break;

        case OP_SPLIT:
            /* Undo Enter: join lines op->row and op->row+1 */
            if (op->row < ed->count - 1)
            {
                cur = ed->lines[op->row];
                prev = ed->lines[op->row + 1];

                line_append_line(ed, cur, prev, 0, prev->len);
                line_free(doc_remove_line(ed, op->row + 1));

                if (op->row < min_row)
                    min_row = op->row;
            }

            break;

        case OP_JOIN:
            /* Undo join: split line op->row at join_col */
            if (op->row < ed->count)
            {
                cur = ed->lines[op->row];
                nl = line_from_slice(ed, cur, op->join_col, cur->len - op->join_col);

                if (nl)
                {
                    line_truncate(ed, cur, op->join_col);
                    doc_insert_line(ed, op->row + 1, nl);

                    if (op->row < min_row)
                        min_row = op->row;
                }
            }

            break;

        case OP_SNAPSHOT:
            /* Undo snapshot: restore document from UTF-8 snapshot */
            if (op->utf8_snapshot)
            {
                doc_clear(ed);
                ed_load(ed, op->utf8_snapshot);

                ed->hard_wrap = op->hard_wrap_mode;

                ed_set_pos(ed, op->row, op->col);
                min_row = 0; /* Snapshot affects entire document */
            }

            break;

        case OP_SNAPSHOT_RANGE:
            /* Undo range snapshot: replace NEW range with OLD lines */
            if (op->utf8_snapshot)
            {
                int start = op->row;
                int newc = op->end_col;

                ed_replace_range_from_utf8(ed, start, newc, op->utf8_snapshot);
                ed_set_pos(ed, g->cur_row, g->cur_col);

                if (start < min_row)
                    min_row = start;
            }

            break;

        case OP_PASTE:
            /* Undo paste: delete the pasted text block directly */
            if (op->row == op->end_row)
            {
                /* Single line paste: delete the range */
                if (op->row < ed->count)
                {
                    line_delete_range(ed, ed->lines[op->row], op->col, op->end_col - op->col);

                    if (op->row < min_row)
                        min_row = op->row;
                }
            }
            else
            {
                /* Multi-line paste: delete from end to start */
                int i;
                EdLine *first_line, *last_line;

                /* Delete characters on the last line */
                if (op->end_row < ed->count)
                    line_delete_range(ed, ed->lines[op->end_row], 0, op->end_col);

                /* Delete all lines in between */
                for (i = op->end_row - 1; i > op->row; i--)
                {
                    if (i < ed->count)
                        line_free(doc_remove_line(ed, i));
                }

                /* Delete characters on the first line and join with remaining */
                if (op->row < ed->count)
                {
                    first_line = ed->lines[op->row];

                    if (op->row + 1 < ed->count)
                    {
                        last_line = ed->lines[op->row + 1];

                        line_delete_range(ed, first_line, op->col, first_line->len - op->col);
                        line_append_line(ed, first_line, last_line, 0, last_line->len);
                        line_free(doc_remove_line(ed, op->row + 1));
                    }
                    else
                    {
                        line_delete_range(ed, first_line, op->col, first_line->len - op->col);
                    }

                    if (op->row < min_row)
                        min_row = op->row;
                }
            }

            /* Ensure cursor is at correct start position */
            ed_set_pos(ed, op->row, op->col);

            /* Ensure visibility */
            ed_ensure_visible(ed);

            break;
        }
    }

    return min_row;
}

static int apply_group_forward(Ed *ed, UndoGroup *g)
{
    int i;
    UndoOp *op = NULL;
    EdLine *cur = NULL;
    EdLine *nl = NULL;
    int min_row = 0;

    for (i = 0; i < g->count; i++)
    {
        op = &g->ops[i];

        switch (op->type)
        {
        case OP_INSERT:
            if (op->row < ed->count && op->text)
            {
                int j;

                for (j = 0; j < op->len; j++)
                    line_insert(ed, ed->lines[op->row], op->col + j, op->text[j]);

                if (op->row < min_row)
                    min_row = op->row;
            }

            break;

        case OP_DELETE:
            if (op->row < ed->count)
            {
                /* Check if this is a line deletion (text contains newline) */
                int is_line_delete = 0;
                int k;

                for (k = 0; k < op->len; k++)
                {
                    if (op->text && op->text[k] == L'\n')
                    {
                        is_line_delete = 1;
                        break;
                    }
                }

                if (is_line_delete)
                {
                    /* Line deletion: remove the entire line */
                    line_free(doc_remove_line(ed, op->row));
                }
                else
                {
                    /* Character deletion: delete within line */
                    line_delete_range(ed, ed->lines[op->row], op->col, op->len);
                }

                if (op->row < min_row)
                    min_row = op->row;
            }

            break;

        case OP_SPLIT:
            if (op->row < ed->count)
            {
                cur = ed->lines[op->row];
                nl = line_from_slice(ed, cur, op->col, cur->len - op->col);

                if (nl)
                {
                    line_truncate(ed, cur, op->col);
                    doc_insert_line(ed, op->row + 1, nl);

                    if (op->row < min_row)
                        min_row = op->row;
                }
            }

            break;

        case OP_JOIN:
            if (op->row < ed->count - 1)
            {
                cur = ed->lines[op->row];

                line_append_line(ed, cur, ed->lines[op->row + 1], 0, ed->lines[op->row + 1]->len);
                line_free(doc_remove_line(ed, op->row + 1));

                if (op->row < min_row)
                    min_row = op->row;
            }

            break;

        case OP_SNAPSHOT:
            /* Redo snapshot: restore document from UTF-8 snapshot */
            if (op->utf8_snapshot)
            {
                doc_clear(ed);
                ed_load(ed, op->utf8_snapshot);

                ed->hard_wrap = op->hard_wrap_mode;

                ed_set_pos(ed, op->row, op->col);
                min_row = 0; /* Snapshot affects entire document */
            }
            break;

        case OP_SNAPSHOT_RANGE:
            /* Redo range snapshot: replace OLD range with NEW lines */
            if (op->utf8_snapshot_new)
            {
                int start = op->row;
                int oldc = op->end_row;

                ed_replace_range_from_utf8(ed, start, oldc, op->utf8_snapshot_new);

                /* Jump to end cursor position stored when snapshot was taken */
                ed_set_pos(ed, g->end_row, g->end_col);

                if (start < min_row)
                    min_row = start;
            }
            break;

        case OP_PASTE:
            /* Redo paste: paste the UTF-8 text */
            if (op->utf8_snapshot)
            {
                ed_set_pos(ed, op->row, op->col);
                ed_paste_text(ed, op->utf8_snapshot);

                if (op->row < min_row)
                    min_row = op->row;
            }
            break;
        }
    }

    return min_row;
}

/* Close current group (break point for coalescing) */
void ed_save_undo(Ed *ed)
{
    if (!ed)
        return;

    if (ed->undo_open && ed->undo_top > 0)
    {
        UndoGroup *g = &ed->undo_stack[ed->undo_top - 1];

        g->end_row = ed->row;
        g->end_col = ed->col;
    }

    ed->undo_open = 0;
}

int ed_undo(Ed *ed)
{
    UndoGroup *src = NULL;
    UndoGroup tmp;
    int min_row;

    if (!ed || ed->undo_top <= 0)
        return -1;

    /* Close any open group first */
    ed_save_undo(ed);

    /* Move top undo group to redo stack */
    if (ed_undo_stack_make_room(&ed->redo_stack, &ed->redo_top, &ed->redo_cap, ed->redo_max) != 0)
        return -1;

    ed->undo_top--;
    src = &ed->undo_stack[ed->undo_top];

    /* Save current cursor into end fields before moving */
    src->end_row = ed->row;
    src->end_col = ed->col;

    /* Copy group header to redo (move ownership of ops array) */
    tmp = *src;
    ed->redo_stack[ed->redo_top++] = tmp;
    src->ops = NULL;
    src->count = 0;
    src->cap = 0;

    /* Suspend invalidation during undo */
    ed->undo_snapshot_mode = 1;

    /* Apply ops in reverse */
    min_row = apply_group_reverse(ed, &ed->redo_stack[ed->redo_top - 1]);

    /* Ensure document has at least one line after undo */
    if (ed->count <= 0)
    {
        EdLine *empty = line_empty(ed);

        if (empty)
            doc_insert_line(ed, 0, empty);
    }

    ed->undo_snapshot_mode = 0;

    /* Restore cursor to before-state */
    ed->row = tmp.cur_row;
    ed->col = tmp.cur_col;

    ed_clamp(ed);
    ed_ensure_visible(ed);

    ed_set_modified(ed, 1);
    ed_prefix_invalidate_from(ed, min_row);

    return 0;
}

int ed_redo(Ed *ed)
{
    UndoGroup *src = NULL;
    UndoGroup tmp;
    int min_row;

    if (!ed || ed->redo_top <= 0)
        return -1;

    /* Move top redo group to undo stack */
    if (ed_undo_stack_make_room(&ed->undo_stack, &ed->undo_top, &ed->undo_cap, ed->undo_max) != 0)
        return -1;

    ed->redo_top--;
    src = &ed->redo_stack[ed->redo_top];

    tmp = *src;
    ed->undo_stack[ed->undo_top++] = tmp;
    src->ops = NULL;
    src->count = 0;
    src->cap = 0;

    /* Suspend invalidation during redo */
    ed->undo_snapshot_mode = 1;

    /* Apply ops forward */
    min_row = apply_group_forward(ed, &ed->undo_stack[ed->undo_top - 1]);

    /* Ensure document has at least one line after undo */
    if (ed->count <= 0)
    {
        EdLine *empty = line_empty(ed);

        if (empty)
            doc_insert_line(ed, 0, empty);
    }

    ed->undo_snapshot_mode = 0;

    /* Restore cursor to after-state */
    ed->row = tmp.end_row;
    ed->col = tmp.end_col;

    ed_clamp(ed);
    ed_ensure_visible(ed);

    ed_set_modified(ed, 1);
    ed_prefix_invalidate_from(ed, min_row);

    return 0;
}

void ed_set_undo_levels(Ed *ed, int levels)
{
    if (!ed)
        return;

    if (levels < 1)
        levels = 1;

    if (levels > 10000)
        levels = 10000;

    ed->undo_max = levels;
    ed->redo_max = levels;

    while (ed->undo_top > levels)
    {
        undo_group_clear(&ed->undo_stack[0]);
        memmove(&ed->undo_stack[0], &ed->undo_stack[1], (size_t)(ed->undo_top - 1) * sizeof(UndoGroup));
        ed->undo_top--;
    }

    while (ed->redo_top > levels)
    {
        undo_group_clear(&ed->redo_stack[0]);
        memmove(&ed->redo_stack[0], &ed->redo_stack[1], (size_t)(ed->redo_top - 1) * sizeof(UndoGroup));
        ed->redo_top--;
    }
}

void ed_set_undo_snapshot_mode(Ed *ed, int mode)
{
    if (!ed)
        return;

    ed->undo_snapshot_mode = mode;
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
    {
        ed->modified = modified;

        if (modified)
        {
            int from = ed->row;

            if (from < 0)
                from = 0;

            if (ed->syntax_state_dirty_from < 0 || from < ed->syntax_state_dirty_from)
                ed->syntax_state_dirty_from = from;
        }
    }
}

int ed_get_hard_wrap(const Ed *ed)
{
    if (!ed)
        return 0;

    return ed->hard_wrap;
}

void ed_set_hard_wrap(Ed *ed, int hard_wrap)
{
    if (ed)
        ed->hard_wrap = hard_wrap;
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
    /* Decode compact line into ring; view is valid until the next edit */
    Ed *m = NULL;
    EdLine *ln = NULL;
    wchar_t *buf = NULL;
    int slot;
    int i;
    int need;

    if (!ed || line < 0 || line >= ed->count)
        return NULL;

    m = (Ed *)ed;
    ln = m->lines[line];

    for (slot = 0; slot < ED_WCS_VIEW_SLOTS; slot++)
        if (m->wcs_view[slot] && m->wcs_view_line[slot] == line)
            return m->wcs_view[slot];

    slot = m->wcs_view_next;
    m->wcs_view_next = (slot + 1) % ED_WCS_VIEW_SLOTS;
    need = ln->len + 1;

    if (m->wcs_view_cap[slot] < need)
    {
        buf = (wchar_t *)realloc(m->wcs_view[slot], (size_t)need * sizeof(wchar_t));

        if (!buf)
            return NULL;

        m->wcs_view[slot] = buf;
        m->wcs_view_cap[slot] = need;
    }

    buf = m->wcs_view[slot];

    for (i = 0; i < ln->len; i++)
        buf[i] = (wchar_t)ed_line_char(ln, i);

    buf[ln->len] = L'\0';
    m->wcs_view_line[slot] = line;

    return buf;
}

int ed_line_len(const Ed *ed, int line)
{
    if (!ed || line < 0 || line >= ed->count)
        return -1;

    return ed->lines[line]->len;
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

int ed_line_utf8(const Ed *ed, int line, char *buf, int bufsz)
{
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

    wlen = ed->lines[line]->len;

    /* Convert line to UTF-8 in buf; return bytes written or -1 on truncate */
    n = 0;

    for (i = 0; i < wlen; i++)
    {
        int need;
        cp = (unsigned long)ed_line_char(ed->lines[line], i);

        need = (cp < 0x80) ? 1 : (cp < 0x800) ? 2
                             : (cp < 0x10000) ? 3
                                              : 4;

        /* +1 for the trailing NUL */
        if (n + need + 1 > bufsz)
        {
            buf[(n < bufsz) ? n : (bufsz - 1)] = '\0';
            return -1;
        }

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

int ed_paste_text(Ed *ed, const char *utf8_text)
{
    EdLine *cur = NULL;
    char *prefix = NULL;
    char *suffix = NULL;
    char *combined = NULL;
    int prefix_len;
    int suffix_len;
    int text_len;
    int combined_len;
    int inserted;
    int new_row;
    int new_col;
    const char *last_nl = NULL;
    const char *last_seg = NULL;
    wchar_t *last_wcs = NULL;
    int last_wlen;
    int orig_row;
    int orig_col;

    if (!ed || !utf8_text)
        return -1;

    if (!*utf8_text)
        return 0;

    orig_row = ed->row;
    orig_col = ed->col;

    cur = ed->lines[ed->row];

    /* Convert the current line split at the cursor into UTF-8 */
    prefix = line_to_utf8(cur, 0, ed->col);
    suffix = line_to_utf8(cur, ed->col, cur->len - ed->col);

    if (!prefix || !suffix)
    {
        free(prefix);
        free(suffix);

        return -1;
    }

    prefix_len = strlen(prefix);
    suffix_len = strlen(suffix);
    text_len = strlen(utf8_text);

    combined_len = prefix_len + text_len + suffix_len;

    combined = (char *)malloc((size_t)(combined_len + 1));

    if (!combined)
    {
        free(prefix);
        free(suffix);

        return -1;
    }

    memcpy(combined, prefix, (size_t)prefix_len);
    memcpy(combined + prefix_len, utf8_text, (size_t)text_len);
    memcpy(combined + prefix_len + text_len, suffix, (size_t)suffix_len);

    combined[combined_len] = '\0';

    free(prefix);
    prefix = NULL;

    free(suffix);
    suffix = NULL;

    /* Replace the current line with the combined text (O(N) bulk insert) */
    inserted = ed_replace_range_from_utf8(ed, ed->row, 1, combined);

    free(combined);
    combined = NULL;

    if (inserted <= 0)
        return -1;

    new_row = ed->row + inserted - 1;

    /* Compute cursor column: end of the pasted text on the last line */
    last_nl = strrchr(utf8_text, '\n');

    if (last_nl)
    {
        last_seg = last_nl + 1;

        if (*last_seg == '\0')
        {
            new_col = 0;
        }
        else
        {
            last_wcs = utf8_to_wcs(last_seg, &last_wlen);
            new_col = last_wcs ? last_wlen : 0;

            free(last_wcs);
            last_wcs = NULL;
        }
    }
    else
    {
        last_wcs = utf8_to_wcs(utf8_text, &last_wlen);
        new_col = last_wcs ? last_wlen : 0;
        free(last_wcs);
        last_wcs = NULL;
    }

    ed->row = new_row;

    /* Single-line paste: cursor advances by pasted text length; multi-line paste already computed absolute new_col */
    ed->col = new_col + (new_row == orig_row ? orig_col : 0);

    ed_set_modified(ed, 1);

    ed_clamp(ed);
    ed_prefix_invalidate_from(ed, ed->row);
    ed_ensure_visible(ed);

    return 0;
}

int ed_paste_text_with_undo(Ed *ed, const char *utf8_text)
{
    int cursor_row_before;
    int cursor_col_before;
    int cursor_row_after;
    int cursor_col_after;
    EdInfo info;
    UndoGroup *g = NULL;

    if (!ed || !utf8_text)
        return -1;

    /* Save state before paste */
    ed_get_info(ed, &info);

    cursor_row_before = info.row;
    cursor_col_before = info.col;

    /* Open undo group for paste operation */
    if (ed_undo_open_group(ed) != 0)
        return -1;

    /* Suspend undo recording during paste to avoid duplicate operations */
    ed->undo_snapshot_mode = 1;

    /* Paste the text */
    if (ed_paste_text(ed, utf8_text) != 0)
    {
        ed->undo_snapshot_mode = 0;
        ed->undo_open = 0;
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
    g->ops[g->count].utf8_snapshot = strdup(utf8_text);

    if (!g->ops[g->count].utf8_snapshot)
    {
        ed->undo_open = 0;
        return -1;
    }

    g->ops[g->count].hard_wrap_mode = ed->hard_wrap;
    g->ops[g->count].end_row = cursor_row_after;
    g->ops[g->count].end_col = cursor_col_after;
    g->count++;
    ed->undo_open = 0;

    /* Invalidate prefix after paste */
    ed_prefix_invalidate_from(ed, cursor_row_before);

    return 0;
}

static int ed_sort_cmp(const void *a, const void *b)
{
    const EdLine *la = *(const EdLine *const *)a;
    const EdLine *lb = *(const EdLine *const *)b;
    int n;
    int i;

    if (!la || la->len == 0)
        return (lb && lb->len > 0) ? -1 : 0;

    if (!lb || lb->len == 0)
        return 1;

    n = la->len < lb->len ? la->len : lb->len;

    for (i = 0; i < n; i++)
    {
        wchar_t ca = (wchar_t)ed_line_char(la, i);
        wchar_t cb = (wchar_t)ed_line_char(lb, i);

        ca = (wchar_t)towlower((wint_t)ca);
        cb = (wchar_t)towlower((wint_t)cb);

        if (ca != cb)
            return ca < cb ? -1 : 1;
    }

    return la->len - lb->len;
}

int ed_sort_block_lines(Ed *ed)
{
    int r1;
    int c1;
    int r2;
    int c2;
    int n;
    int i;
    EdLine **arr = NULL;
    char *snapshot_before = NULL;
    char *snapshot_after = NULL;
    int old_count, new_count;
    int cur_row, cur_col;

    if (!ed || !ed->block.active)
        return -1;

    if (ed->undo_snapshot_mode)
        return -1;

    block_range(ed, &r1, &c1, &r2, &c2);

    if (r1 < 0)
        r1 = 0;

    if (r2 >= ed->count)
        r2 = ed->count - 1;

    n = r2 - r1 + 1;

    if (n < 2)
        return -1;

    cur_row = ed->row;
    cur_col = ed->col;
    old_count = n;
    new_count = n;

    ed_redo_clear(ed);

    if (ed_undo_open_group(ed) != 0)
        return -1;

    snapshot_before = ed_range_to_string(ed, r1, r2 + 1);

    if (!snapshot_before)
    {
        ed->undo_open = 0;
        return -1;
    }

    arr = (EdLine **)malloc((size_t)n * sizeof(EdLine *));

    if (!arr)
    {
        ed->undo_open = 0;
        free(snapshot_before);
        return -1;
    }

    for (i = 0; i < n; i++)
        arr[i] = ed->lines[r1 + i];

    qsort(arr, (size_t)n, sizeof(EdLine *), ed_sort_cmp);

    for (i = 0; i < n; i++)
        ed->lines[r1 + i] = arr[i];

    free(arr);

    ed_set_modified(ed, 1);

    ed_prefix_invalidate_from(ed, r1);
    ed_ensure_visible(ed);

    ed->undo_open = 0;

    snapshot_after = ed_range_to_string(ed, r1, r2 + 1);

    if (!snapshot_after)
    {
        free(snapshot_before);
        return -1;
    }

    if (undo_push_snapshot_range(ed, r1, 0, snapshot_before, snapshot_after, old_count, new_count, cur_row, cur_col, ed->row, ed->col) != 0)
        return -1;

    return 0;
}

int ed_convert_block_case(Ed *ed, int mode)
{
    int r1;
    int c1;
    int r2;
    int c2;
    int li;
    char *snapshot_before = NULL;
    char *snapshot_after = NULL;
    int old_count, new_count;
    int cur_row, cur_col;
    int in_word_state = 0;

    if (!ed || !ed->block.active)
        return -1;

    if (ed->undo_snapshot_mode)
        return -1;

    block_range(ed, &r1, &c1, &r2, &c2);

    if (r1 < 0)
        r1 = 0;

    if (r2 >= ed->count)
        r2 = ed->count - 1;

    if (r1 > r2)
        return -1;

    cur_row = ed->row;
    cur_col = ed->col;
    old_count = r2 - r1 + 1;
    new_count = old_count;

    ed_redo_clear(ed);

    if (ed_undo_open_group(ed) != 0)
        return -1;

    snapshot_before = ed_range_to_string(ed, r1, r2 + 1);

    if (!snapshot_before)
    {
        ed->undo_open = 0;
        return -1;
    }

    for (li = r1; li <= r2; li++)
    {
        EdLine *ln = ed->lines[li];
        int start = 0;
        int end = ln->len;
        int i;
        int old_count = 0;

        if (ed->word_count_initialized)
            old_count = ln->word_count;

        if (li == r1)
            start = c1;

        if (li == r2)
            end = c2;

        if (start < 0)
            start = 0;

        if (end > ln->len)
            end = ln->len;

        if (mode == 2)
            in_word_state = 0;

        for (i = start; i < end; i++)
        {
            wchar_t c = (wchar_t)ed_line_char(ln, i);

            if (mode == 0)
            {
                if (c >= L'a' && c <= L'z')
                    line_put_raw(ln, i, (unsigned int)(c - 32));
            }
            else if (mode == 1)
            {
                if (c >= L'A' && c <= L'Z')
                    line_put_raw(ln, i, (unsigned int)(c + 32));
            }
            else
            {
                int is_alpha = (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');

                if (is_alpha)
                {
                    if (!in_word_state)
                    {
                        if (c >= L'a' && c <= L'z')
                            line_put_raw(ln, i, (unsigned int)(c - 32));

                        in_word_state = 1;
                    }
                    else
                    {
                        if (c >= L'A' && c <= L'Z')
                            line_put_raw(ln, i, (unsigned int)(c + 32));
                    }
                }
                else
                {
                    in_word_state = 0;
                }
            }
        }

        ln->wrap_count_cache = -1;

        if (ed->word_count_initialized)
        {
            ln->word_count = line_count_words(ln);
            ed->word_count_total += ln->word_count - old_count;
        }
    }

    ed_set_modified(ed, 1);
    ed_prefix_invalidate_from(ed, r1);
    ed->undo_open = 0;

    snapshot_after = ed_range_to_string(ed, r1, r2 + 1);

    if (!snapshot_after)
    {
        free(snapshot_before);
        return -1;
    }

    if (undo_push_snapshot_range(ed, r1, 0, snapshot_before, snapshot_after, old_count, new_count, cur_row, cur_col, ed->row, ed->col) != 0)
        return -1;

    return 0;
}
