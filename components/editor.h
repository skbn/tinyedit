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

Ed *ed_new();
void ed_free(Ed *ed);
void ed_load(Ed *ed, const char *utf8_text); /* UTF-8 in */
char *ed_to_string(const Ed *ed);            /* UTF-8 out (caller frees) */

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
int ed_paste_text(Ed *ed, const char *utf8_text); /* UTF-8 in */

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
int search_all_custom(Ed *ed, const wchar_t *needle, int case_sensitive, int whole_word, int **out_rows, int **out_cols);
/* Re-flow with quote preservation */
int ed_rewrap_paragraph(Ed *ed, int width);

/* Insert a text file at the cursor position (with undo) */
int ed_load_file_at_cursor(Ed *ed, const char *path, const char *charset_in);
/* Export the current block selection (F6 to mark) to a text file */
int ed_export_block_to_file(Ed *ed, const char *path, const char *charset_out);

#endif
