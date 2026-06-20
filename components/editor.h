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
#include <wchar.h>

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
    wchar_t *wcs; /* malloc'd, always NUL-terminated */
    int len;      /* character count (not bytes) */
    int cap;      /* allocated wchar_t slots */
} EdLine;

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
    int row, col;            /* position where op occurred */
    wchar_t *text;           /* owned; used by OP_INSERT and OP_DELETE */
    int len;                 /* chars in text */
    int join_col;            /* for OP_JOIN: length of previous line before join */
    char *utf8_snapshot;     /* owned; used by OP_SNAPSHOT: full document UTF-8 */
    char *utf8_snapshot_new; /* owned; used by OP_SNAPSHOT_RANGE for redo */
    int hard_wrap_mode;      /* used by OP_SNAPSHOT: 0=soft-wrap, 1=hard-wrap */
    int end_row, end_col;    /* For OP_PASTE: block coordinates end position after paste */
} UndoOp;

/* Group of ops treated as one undo/redo step */
typedef struct
{
    UndoOp *ops; /* owned array */
    int count, cap;
    int cur_row, cur_col; /* cursor before the group */
    int end_row, end_col; /* cursor after the group */
} UndoGroup;

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

    /* Prefix sum array for soft-wrap: prefix[i] = total visual rows up to line i */
    int *prefix;           /* prefix sum array */
    int prefix_alloc;      /* allocated capacity */
    int prefix_valid;      /* 0 = invalid, 1 = valid */
    int prefix_width;      /* width for which prefix was calculated */
    int prefix_dirty_from; /* -1 = clean, >=0 = dirty from this line onwards */
    int prefix_start;      /* start line of current prefix range (for range rebuild) */
    int prefix_end;        /* end line of current prefix range (for range rebuild) */
    int prefix_base;       /* visual rows before prefix_start (for absolute positioning) */
};

#define INIT_ALLOC 256

Ed *ed_new(void);
void ed_free(Ed *ed);
void ed_load(Ed *ed, const char *utf8_text);                /* UTF-8 in */
char *ed_to_string(const Ed *ed);                           /* UTF-8 out (caller frees) */
char *ed_range_to_string(const Ed *ed, int start, int end); /* serialise only [start, end) */

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

/* Undo/redo (default 50 levels) */
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
/* Find all matches, returns count and malloc'd arrays (caller must free) */
int ed_search_all(Ed *ed, const wchar_t *needle, int **out_rows, int **out_cols);
/* Custom search with case_sensitive and whole_word support */
int ed_search_all_custom(Ed *ed, const wchar_t *needle, int case_sensitive, int whole_word, int **out_rows, int **out_cols);
/* Re-flow with quote preservation */
int ed_rewrap_paragraph(Ed *ed, int width);
int ed_rewrap_document(Ed *ed, int width);

/* Hyphenation callback: returns break positions in word_utf8 */
typedef int (*EdHyphenFn)(void *user_data, const char *word_utf8, int word_byte_len, int *out_byte_pos, int *out_count);

/* Like ed_rewrap_paragraph, but uses hyph to split overflow words */
int ed_rewrap_paragraph_ex(Ed *ed, int width, EdHyphenFn hyph, void *hyph_data);

/* Insert a text file at the cursor position (with undo) */
int ed_load_file_at_cursor(Ed *ed, const char *path, const char *charset_in);
/* Export the current block selection (F6 to mark) to a text file */
int ed_export_block_to_file(Ed *ed, const char *path, const char *charset_out);

/* Set modified flag */
void ed_set_modified(Ed *ed, int modified);

/* Hard wrap mode */
int ed_get_hard_wrap(const Ed *ed);
void ed_set_hard_wrap(Ed *ed, int hard_wrap);

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
int ed_undo_open_group(Ed *ed);
void ed_prefix_invalidate(Ed *ed);
void ed_prefix_invalidate_from(Ed *ed, int from_line);
int ed_undo_stack_make_room(UndoGroup **stack, int *top, int *cap, int max);
void ed_set_pos(Ed *ed, int row, int col);
int ed_detect_quote_prefix(const wchar_t *line);

#endif
