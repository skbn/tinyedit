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

#ifndef UI_GRAMMAR_H
#define UI_GRAMMAR_H

#include "te.h"

#ifdef HAVE_GRAMMAR

#include "../grammar/grammar.h"

/* Prewarm margin (lines above and below viewport), keep small on Amiga to avoid burning CPU on hidden lines */
#ifndef UI_GRAM_PREWARM_MARGIN
#ifdef PLATFORM_AMIGA
#define UI_GRAM_PREWARM_MARGIN 3
#else
#define UI_GRAM_PREWARM_MARGIN 8
#endif
#endif

/* Maximum prewarm calls per frame, cap to keep frame-time bounded even if user jumps by page-down through cold territory */
#ifndef UI_GRAM_PREWARM_BUDGET
#ifdef PLATFORM_AMIGA
#define UI_GRAM_PREWARM_BUDGET 6
#else
#define UI_GRAM_PREWARM_BUDGET 24
#endif
#endif

/* Load the grammar checker configured in cfg, returns 0 or -1 */
int ui_grammar_load_from_config(TeApp *app);

/* Free the checker handle, safe on NULL */
void ui_grammar_free_app(TeApp *app);

/* Query single visible line, fills out with up to cap issues, returns count, cache-hit is hash + memcpy */
int ui_grammar_check_row(TeApp *app, const wchar_t *wl, int line_len, GcIssue *out, int cap);

/* One-call helper for render loop: check row and paint underline spans over issues, zero-alloc uses stack buffers, screen_y and col_offset are row origin on screen, tab_width is editor's active tab width for visual column mapping, does nothing if checker inactive */
void ui_grammar_draw_row(TeApp *app, int screen_y, int col_offset, int tab_width, const wchar_t *wl, int line_len);

/* Soft-wrap variant: draw only issues intersecting sub-row segment [seg_start, seg_end) of logical line, visual column of seg_start within sub-row is seg_start_vcol (0 for first sub-row, non-zero if tab stop mid-line), grammar check runs on FULL line, cache-friendly across sub-rows */
void ui_grammar_draw_row_segment(TeApp *app, int screen_y, int col_offset, int tab_width, const wchar_t *wl, int line_len, int seg_start, int seg_end, int seg_start_vcol);

/* Prewarm LRU cache for lines just outside viewport, call once after visible-row render loop completes, top = first visible line index, body_rows = number of visible rows, line_count = total logical lines in buffer, does nothing if grammar checker inactive */
void ui_grammar_prewarm(TeApp *app, int top, int body_rows, int line_count);

/* Invalidate cache entry for a single line, call after edit */
void ui_grammar_invalidate_row(TeApp *app, const wchar_t *wl, int line_len);

/* Full flush, call on file open, config reload, or rule pack swap */
void ui_grammar_flush(TeApp *app);

/* Fold the existing assist_* toggles into the checker's category mask */
void ui_grammar_apply_toggles(TeApp *app);

/* Severity -> ncurses colour pair (override with -DUI_GRAM_COLOR_*) */
int ui_grammar_color_for_severity(unsigned char sev);

#else /* HAVE_GRAMMAR */

/* Stubs so callers don't need #ifdef around each call */
#define ui_grammar_load_from_config(app) (-1)
#define ui_grammar_free_app(app) ((void)0)
#define ui_grammar_check_row(app, wl, len, out, cap) (0)
#define ui_grammar_draw_row(app, y, x, tw, wl, len) ((void)0)
#define ui_grammar_draw_row_segment(app, y, x, tw, wl, len, s, e, v) ((void)0)
#define ui_grammar_prewarm(app, t, b, c) ((void)0)
#define ui_grammar_invalidate_row(app, wl, len) ((void)0)
#define ui_grammar_flush(app) ((void)0)
#define ui_grammar_apply_toggles(app) ((void)0)
#define ui_grammar_color_for_severity(s) (0)

#endif /* HAVE_GRAMMAR */

#endif /* UI_GRAMMAR_H */
