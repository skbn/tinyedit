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

/* editor.h -- Text editor with wchar_t internal */
#ifndef WRAPPER_EDITOR_H
#define WRAPPER_EDITOR_H

#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include "layout.h"

/* Number of decoded lines ed_line_wcs() can hand out at once */
enum
{
    ED_WCS_VIEW_SLOTS = 64
};

typedef struct Ed Ed;

typedef struct
{
    int active;
    int anchor_row, anchor_col;
} EdBlock;

typedef struct
{
    int row, col; /* col = character index */
    int top;
    int line_count;
    int insert_mode;
    EdBlock block;
    int modified;
} EdInfo;

typedef struct
{
    void *text; /* Packed codepoints, cw bytes each, always NUL terminated */
    struct EdAttrRunStruct *attrs;

    int len;                        /* Character count, not bytes */
    int cap;                        /* Allocated character slots */
    int wrap_count_cache;           /* -1 = invalid, otherwise cached visual rows */
    int word_count;                 /* Cached word count for this line */
    unsigned char word_count_dirty; /* 1 if word_count needs recomputation */

    unsigned short n_attrs;
    unsigned short cap_attrs;
    short wrap_cache_width; /* Width for which wrap_count_cache is valid */

    unsigned char cw;         /* Bytes per stored codepoint: 1, 2 or 4 */
    unsigned char t_arena;    /* Text lives in the load arena, never freed one by one */
    unsigned char brk;        /* How this line joins to the next, see layout.h LineBreak the wrap hyphen is NEVER a character in the text */
    unsigned char para_align; /* EA_ALIGN_* paragraph alignment */

#if defined(PLATFORM_AMIGA)
    void *mem_pool; /* Owning Ed's memory pool, NULL => plain malloc */
#endif
} EdLine;

/* Undo engine types, see undo.h: one op = a line range delta */
typedef struct UndoOp
{
    int row;
    EdLine **before;
    int n_before;
    EdLine **after;
    int n_after;
} UndoOp;

typedef struct UndoGroup
{
    UndoOp *ops;
    int count;
    int cap;
    int row_before;
    int col_before;
    int row_after;
    int col_after;
    int coalesce;
} UndoGroup;

/* Read the codepoint stored at character index i */
unsigned int ed_line_char(const EdLine *ln, int i);

struct Ed
{
    void *mem_pool;   /* Exec memory pool on Amiga, always NULL elsewhere */
    void *slab_head;  /* Chunks the line structs are carved from */
    void *slab_free;  /* Free list of line slots */
    void *arena_head; /* Bump chunks holding exact-fit text from the loader */

    EdLine **lines;
    int count, alloc;
    int row, col; /* col = character index */
    int top, page;
    int insert_mode, modified;
    EdBlock block;
    wchar_t *killbuf; /* Malloc'd wchar_t copy */
    int killlen;
    EdLine **killbuf_lines; /* Rich internal clipboard: copied EdLine objects */
    int killbuf_line_count;

    /* Undo and redo stacks of groups, see undo.h */
    UndoGroup *undo_stack;
    int undo_top, undo_cap, undo_max;

    UndoGroup *redo_stack;
    int redo_top, redo_cap, redo_max;

    int undo_typing; /* 1 = single line typing run, commits coalesce */

    /* Lines captured by undo_begin, waiting for undo_commit */
    EdLine **pending_before;
    int pending_row;
    int pending_n;
    int pending_row_cur;
    int pending_col_cur;
    int pending_doc_count; /* ed->count when the capture was taken */

    int undo_snapshot_mode; /* 1 = a range delta covers the edit, skip per-op recording */
    int hard_wrap;          /* 0=soft-wrap, 1=hard-wrap */
    int word_move_mode;     /* 0=standard, 1=vim-like (non-space blocks) */

    /* Prefix sum array for soft-wrap: prefix[i] = total visual rows up to line i */
    int *prefix;           /* Prefix sum array */
    int prefix_alloc;      /* Allocated capacity */
    int prefix_valid;      /* 0 = invalid, 1 = valid */
    int prefix_width;      /* Width for which prefix was calculated */
    int prefix_dirty_from; /* -1 = clean, >=0 = dirty from this line onwards */

    int *syntax_state_cache;     /* Cached syntax state at start of each line, -1 = unknown */
    int syntax_state_alloc;      /* Allocated capacity */
    int syntax_state_dirty_from; /* -1 = clean, >=0 = dirty from this line onwards */
    int syntax_state_lang;       /* Language for which cache is valid, -1 = invalid */
    int prefix_start;            /* Start line of current prefix range (for range rebuild) */
    int prefix_end;              /* End line of current prefix range (for range rebuild) */
    int prefix_base;             /* Visual rows before prefix_start (for absolute positioning) */

    int word_count_total;       /* Cached total word count */
    int word_count_initialized; /* 1 = word counts are valid, 0 = lazy init pending */

    /* Compact lines are decoded here on demand. Entries stay valid until the next edit clears the ring */
    wchar_t *wcs_view[ED_WCS_VIEW_SLOTS];
    int wcs_view_line[ED_WCS_VIEW_SLOTS];
    int wcs_view_cap[ED_WCS_VIEW_SLOTS];
    int wcs_view_next;

    /* Reusable decode buffer for wrap counting */
    wchar_t *wrap_scratch;
    int wrap_scratch_cap;

    /* Input attribute mask applied to newly inserted characters */
    unsigned short input_mask;
};

#define INIT_ALLOC 1024

Ed *ed_new(void);
void ed_free(Ed *ed);
int ed_load_stream(Ed *ed, FILE *fp); /* Streaming UTF-8 in */
int ed_load_stream_charset(Ed *ed, FILE *fp, const char *charset);
void ed_load(Ed *ed, const char *utf8_text);                /* UTF-8 in */
char *ed_to_string(const Ed *ed);                           /* UTF-8 out (caller frees) */
char *ed_range_to_string(const Ed *ed, int start, int end); /* Serialise only [start, end) */
void ed_auto_rewrap_capture_pre_snapshot(Ed *ed);
int ed_save_to_file(const Ed *ed, const char *path, const char *charset_out); /* streaming save */

/* Cursor movement */
void ed_move_up(Ed *ed);
void ed_move_down(Ed *ed);
void ed_move_left(Ed *ed);
void ed_move_right(Ed *ed);
void ed_move_home(Ed *ed);
void ed_move_end(Ed *ed);
void ed_move_pgup(Ed *ed, int page_size);
void ed_move_pgdn(Ed *ed, int page_size);
void ed_move_top(Ed *ed);
void ed_move_bottom(Ed *ed);
void ed_word_left(Ed *ed);
void ed_word_right(Ed *ed);
void ed_set_word_move_mode(Ed *ed, int mode);
void ed_goto_line(Ed *ed, int line);

/* Editing -- ch is a Unicode codepoint (wchar_t) */
int ed_insert_char(Ed *ed, wchar_t ch);
int ed_enter(Ed *ed);
int ed_backspace(Ed *ed);
int ed_delete(Ed *ed);
int ed_delete_line(Ed *ed);
int ed_delete_to_eol(Ed *ed);
int ed_delete_word_left(Ed *ed);
int ed_delete_word_right(Ed *ed);
int ed_duplicate_line(Ed *ed);
int ed_move_line_up(Ed *ed);
int ed_move_line_down(Ed *ed);
int ed_insert_tab(Ed *ed, int tabstop);
int ed_paste_text(Ed *ed, const char *utf8_text);           /* UTF-8 in */
int ed_paste_text_with_undo(Ed *ed, const char *utf8_text); /* UTF-8 in, single undo record */

/* Block operations */
void ed_block_anchor(Ed *ed);
void ed_block_clear(Ed *ed);
int ed_block_copy(Ed *ed);
int ed_block_cut(Ed *ed);
int ed_block_delete(Ed *ed);
int ed_block_paste(Ed *ed);
char *ed_block_get_utf8(const Ed *ed);   /* UTF-8 out (caller frees) */
char *ed_killbuf_get_utf8(const Ed *ed); /* UTF-8 of internal clipboard mirror (caller frees) */

/* Undo/redo */
void ed_save_undo(Ed *ed);
int ed_undo(Ed *ed);
void ed_set_undo_levels(Ed *ed, int levels);
int ed_undo_depth(const Ed *ed);
int ed_redo(Ed *ed);
int ed_redo_depth(const Ed *ed);

/* Mode */
void ed_toggle_insert(Ed *ed);

/* Query for rendering */
void ed_get_info(const Ed *ed, EdInfo *info);
const wchar_t *ed_line_wcs(const Ed *ed, int line);
int ed_line_len(const Ed *ed, int line);
int ed_word_count(Ed *ed);

/* Get line as UTF-8 into caller buffer, returns bytes written or -1 */
int ed_line_utf8(const Ed *ed, int line, char *buf, int bufsz);

/* Scroll */
void ed_set_page(Ed *ed, int visible_rows);
void ed_ensure_visible(Ed *ed);

/* Test helper: set cursor position directly */
void ed_set_pos(Ed *ed, int row, int col);

/* Text operations (no UI dependencies) */
/* Case-insensitive, wraps */
int ed_search_forward(Ed *ed, const wchar_t *needle);
/* Find all matches, returns count and malloc'd arrays */
int ed_search_all(Ed *ed, const wchar_t *needle, int **out_rows, int **out_cols);
/* Custom search with case_sensitive and whole_word support */
int ed_search_all_custom(Ed *ed, const wchar_t *needle, int case_sensitive, int whole_word, int **out_rows, int **out_cols);
/* Re-wrap the current FTN reply quote block (Ctrl+W) */
int ed_rewrap_ftn_reply(Ed *ed, int width);

/* Re-flow the hard-wrap paragraph around the cursor, see layout.h The wrap hyphen is never stored, the painter draws it from the break kind */
int ed_rewrap_paragraph_ex(Ed *ed, int width, LayoutHyphenFn hyph, void *hyph_user);

/* Reflow the cursor paragraph with no undo record */
int ed_rewrap_paragraph_no_undo(Ed *ed, int width, LayoutHyphenFn hyph, void *hyph_user);

/* Fit a loaded document to width, brk bounded, no undo record */
int ed_rewrap_loaded_document(Ed *ed, int width, LayoutHyphenFn hyph, void *hyph_user);

/* Refit the whole document to width, one undo entry */
int ed_rewrap_document(Ed *ed, int width, LayoutHyphenFn hyph, void *hyph_user);
int ed_layout_char_width(void *user, wchar_t ch, int col);

/* Insert a text file at the cursor position (with undo) */
int ed_load_file_at_cursor(Ed *ed, const char *path, const char *charset_in);
/* Export the current block selection to a text file */
int ed_export_block_to_file(Ed *ed, const char *path, const char *charset_out);

/* Sort selected lines alphabetically (case-insensitive) */
int ed_sort_block_lines(Ed *ed);

/* Convert case of selected block; mode: 0=UPPER, 1=lower, 2=Title */
int ed_convert_block_case(Ed *ed, int mode);

/* Set modified flag */
void ed_set_modified(Ed *ed, int modified);

/* Hard wrap mode */
int ed_get_hard_wrap(const Ed *ed);
void ed_set_hard_wrap(Ed *ed, int hard_wrap);

/* Tab width for soft-wrap calculations */
void ed_set_tab_width(int n);
int ed_get_tab_width(void);

/* Prefix sum for soft-wrap performance */
void ed_prefix_invalidate(Ed *ed);
int ed_prefix_rebuild(Ed *ed, int width);
int ed_prefix_rebuild_range(Ed *ed, int width, int start_line, int end_line);
int ed_prefix_rebuild_to(Ed *ed, int width, int max_line);
int ed_prefix_rebuild_from_row(Ed *ed, int from_row, int width);
int ed_prefix_get(const Ed *ed, int line);

/* Soft-wrap helpers */
int ed_wrap_next(const wchar_t *line, int len, int width, int start);
int ed_wrap_count(const wchar_t *line, int len, int width);

/* Internal functions made public for editor_helper.c */
void ed_clamp(Ed *ed);
void ed_redo_clear(Ed *ed);
void ed_clear_undo_redo(Ed *ed);

/* Line primitives shared with the undo engine */
EdLine *ed_line_clone(Ed *ed, const EdLine *src);
EdLine *ed_line_from_wcs(Ed *ed, const wchar_t *w, int n);

/* How the line joins to the next one, LB_HYPHEN means draw a hyphen */
int ed_line_break(const Ed *ed, int line);
void ed_line_set_break(Ed *ed, int line, int brk);
void ed_join_breaks(Ed *ed);

/* Drop a hyphen left at the end of the line by an older build or another editor */
void ed_line_drop_trailing_hyphen(Ed *ed, int line);
void ed_line_destroy(Ed *ed, EdLine *ln);
int ed_lines_splice(Ed *ed, int row, int n_remove, EdLine **insert, int n_insert);
int ed_replace_range_from_utf8(Ed *ed, int start, int count_to_remove, const char *utf8_text);

/* Replace [start, end) on one row as a single undo entry */
int ed_replace_word_with_undo(Ed *ed, int row, int start, int end, const wchar_t *replacement, int rlen);

/* Helper functions from editor_helper.c */
wchar_t *line_to_wcs(EdLine *ln);
wchar_t *line_to_wcs_range(EdLine *ln, int start, int end);
char *ed_block_to_string(Ed *ed, int r1, int c1, int r2, int c2);
void ed_prefix_invalidate(Ed *ed);
void ed_prefix_invalidate_from(Ed *ed, int from_line);
void ed_set_pos(Ed *ed, int row, int col);
int ed_detect_quote_prefix(const wchar_t *line);

#endif
