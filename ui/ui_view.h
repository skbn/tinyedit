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

/* ui_view.h -- soft-wrap viewport: sub-rows, scrolling and visual movement */
#ifndef UI_VIEW_H
#define UI_VIEW_H

#include <wchar.h>
#include "te.h"

/* Tab stop width used by every visual width computation */
void view_set_tab_width(int n);
int view_tab_width(void);

/* Left indent of a segment for a paragraph alignment */
int view_align_indent(unsigned char align, int text_vw, int avail);

/* Sub-row primitives on one decoded line */
int view_wrap_next(const wchar_t *l, int len, int width, int start);
int view_wrap_count(const wchar_t *l, int len, int width);
int view_subrow_range(const wchar_t *l, int len, int width, int target_sub, int *seg_start, int *seg_end);
int view_subrow_of_col(const wchar_t *l, int len, int width, int col);

/* Viewport anchor */
int view_top_line(void);
int view_top_sub(void);
void view_set_top(int line, int sub);
void view_reset_to_cursor(TeApp *app, int width);
void view_reset_viewport_to_cursor(TeApp *app, int width);
void view_reset_desired(void);

/* Walk delta visual rows from an anchor */
void view_walk_forward(Ed *ed, int width, int from_line, int from_sub, int delta, int *out_line, int *out_sub);
void view_walk_backward(Ed *ed, int width, int from_line, int from_sub, int delta, int *out_line, int *out_sub);
int view_vrows_between(Ed *ed, int width, int a_line, int a_sub, int b_line, int b_sub);

/* Cursor geometry */
int view_cursor_vcol(Ed *ed, int width);
int view_cursor_align_indent(Ed *ed, int width);

/* Extra columns to shift the cursor by when its sub-row is being visually justified */
int view_cursor_justify_shift(Ed *ed, int width);
int view_cursor_screen_row(TeApp *app, int width);
void view_ensure_visible(TeApp *app, int width, int body_rows);

/* Visual movement */
void view_move_vrows(TeApp *app, int width, int delta);
void view_move_up(TeApp *app, int width);
void view_move_down(TeApp *app, int width);
void view_move_home(TeApp *app, int width);
void view_move_end(TeApp *app, int width);
void view_move_pgup(TeApp *app, int width, int pg);
void view_move_pgdn(TeApp *app, int width, int pg);

#endif
