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
    int len;    /* Character count, not bytes */
    int cap;    /* Allocated character slots */
    int cw;     /* Bytes per stored codepoint: 1, 2 or 4 */
    int word_count;
    int has_wrap_hyphen;  /* 1 if last char is an artificial wrap-hyphen */
    int wrap_count_cache; /* -1 = invalid, otherwise cached visual rows */
    int wrap_cache_width; /* Width for which wrap_count_cache is valid */
    int emb;              /* Byte size of the text area allocated inline with the struct */

#if defined(PLATFORM_AMIGA)
    void *mem_pool; /* Owning Ed's memory pool, NULL => plain malloc */
#endif
} EdLine;

/* Read the codepoint stored at character index i */
unsigned int ed_line_char(const EdLine *ln, int i);

typedef enum
{
    OP_INSERT,        /* inserted wchar_t text at (row,col) */
    OP_DELETE,        /* deleted wchar_t text at (row,col) */
    OP_SPLIT,         /* Enter: split line at (row,col) */
    OP_JOIN,          /* Backspace at col 0: join row with row-1, col=join_col */
    OP_SNAPSHOT,      /* Full document snapshot: text = full document UTF-8 */
    OP_PASTE,         /* Paste UTF-8 text at (row,col): text = UTF-8 string */
    OP_SNAPSHOT_RANGE /* Localized snapshot of [row, row+end_row) lines */
} UndoOpType;

typedef struct
{
    UndoOpType type;
    int row, col;            /* Position where op occurred */
    wchar_t *text;           /* Owned; used by OP_INSERT and OP_DELETE */
    int len;                 /* Chars in text */
    int cap;                 /* Text capacity in wchar_t */
    int join_col;            /* For OP_JOIN: length of previous line before join */
    char *utf8_snapshot;     /* Owned; used by OP_SNAPSHOT: full document UTF-8 */
    char *utf8_snapshot_new; /* Owned; used by OP_SNAPSHOT_RANGE for redo */
    int hard_wrap_mode;      /* Used by OP_SNAPSHOT: 0=soft-wrap, 1=hard-wrap */
    int end_row, end_col;    /* For OP_PASTE: block coordinates end position after paste */
} UndoOp;

/* Group of ops treated as one undo/redo step */
typedef struct
{
    UndoOp *ops; /* Owned array */
    int count, cap;
    int cur_row, cur_col; /* Cursor before the group */
    int end_row, end_col; /* Cursor after the group */
} UndoGroup;

struct Ed
{
#if defined(PLATFORM_AMIGA)
    void *mem_pool; /* Per-document memory pool (APTR from CreatePool) */
#endif

    EdLine **lines;
    int count, alloc;
    int row, col; /* col = character index */
    int top, page;
    int insert_mode, modified;
    EdBlock block;
    wchar_t *killbuf; /* Malloc'd wchar_t copy */
    int killlen;

    /* Undo stack of groups */
    UndoGroup *undo_stack;
    int undo_top, undo_cap, undo_max;

    /* Redo stack of groups */
    UndoGroup *redo_stack;
    int redo_top, redo_cap, redo_max;

    /* Coalescing state */
    int undo_open; /* 1 = current group is open for appending */
    UndoOpType undo_last_op;
    int undo_last_row;
    int undo_last_col_end;  /* col after last recorded char */
    int undo_snapshot_mode; /* 1 = only allow snapshot operations, block individual ops */
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

    /* Pre-edit snapshot for auto-rewrap undo */
    char *auto_rewrap_pre_snapshot;
    int auto_rewrap_pre_start;
    int auto_rewrap_pre_end;
    int auto_rewrap_pre_cursor_row;
    int auto_rewrap_pre_cursor_col;

    /* Compact lines are decoded here on demand. Entries stay valid until the next edit clears the ring */
    wchar_t *wcs_view[ED_WCS_VIEW_SLOTS];
    int wcs_view_line[ED_WCS_VIEW_SLOTS];
    int wcs_view_cap[ED_WCS_VIEW_SLOTS];
    int wcs_view_next;

    /* Reusable decode buffer for wrap counting */
    wchar_t *wrap_scratch;
    int wrap_scratch_cap;
};

#define INIT_ALLOC 1024

Ed *ed_new(void);
void ed_free(Ed *ed);
int ed_load_stream(Ed *ed, FILE *fp); /* Streaming UTF-8 in */
int ed_load_stream_charset(Ed *ed, FILE *fp, const char *charset);
void ed_load(Ed *ed, const char *utf8_text);                /* UTF-8 in */
char *ed_to_string(const Ed *ed);                           /* UTF-8 out (caller frees) */
char *ed_range_to_string(const Ed *ed, int start, int end); /* serialise only [start, end) */
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
char *ed_block_get_utf8(const Ed *ed); /* UTF-8 out (caller frees) */

/* Undo/redo */
void ed_save_undo(Ed *ed);
int ed_undo(Ed *ed);
void ed_set_undo_levels(Ed *ed, int levels);
int ed_undo_depth(const Ed *ed);
int ed_redo(Ed *ed);
int ed_redo_depth(const Ed *ed);
void ed_set_undo_snapshot_mode(Ed *ed, int mode);

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

/* Re-flow hard-wrap paragraph around cursor; hyph_cb returns break offset or -1 */
int ed_rewrap_paragraph_ex(Ed *ed, int width, int (*hyph_cb)(void *, const wchar_t *, int, int), void *hyph_data);

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
int ed_undo_open_group(Ed *ed);
int undo_push_snapshot_range(Ed *ed, int row, int col, char *snapshot_before, char *snapshot_after, int old_count, int new_count, int cur_row, int cur_col, int end_row, int end_col);
int ed_replace_range_from_utf8(Ed *ed, int start, int count_to_remove, const char *utf8_text);

/* Helper functions from editor_helper.c */
wchar_t *line_to_wcs(EdLine *ln);
wchar_t *line_to_wcs_range(EdLine *ln, int start, int end);
char *ed_block_to_string(Ed *ed, int r1, int c1, int r2, int c2);
void ed_prefix_invalidate(Ed *ed);
void ed_prefix_invalidate_from(Ed *ed, int from_line);
int ed_undo_stack_make_room(UndoGroup **stack, int *top, int *cap, int max);
void ed_set_pos(Ed *ed, int row, int col);
int ed_detect_quote_prefix(const wchar_t *line);

#endif
