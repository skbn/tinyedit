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

#ifndef UI_EDITOR_HELPER_H
#define UI_EDITOR_HELPER_H

#include "te.h"
#include "ui_syntax.h"

typedef struct
{
    int first_row;
    int first_start;
    int first_end;
    int second_row;
    int second_start;
    int second_end;
    wchar_t joined[256];
    int joined_len;
} UiHyphenWord;

int ui_hyphen_word_find(Ed *ed, const EdInfo *info, const wchar_t *line, int line_len, int word_start, int word_end, UiHyphenWord *word);
void ui_hyphen_word_replace(Ed *ed, const UiHyphenWord *word, const wchar_t *replacement, int replacement_len);

/* Additional helper functions */
int charset_select(TeApp *app);

#ifdef HAVE_HYPHEN
#endif

void clear_search_highlights(TeApp *app);

/* Functions from ui_editor.c needed by ui_editor_helper.c */
void soft_reset_desired(void);
void soft_reset_viewport_to_cursor(TeApp *app, int width);
int editor_eff_wrap(TeApp *app);
int editor_text_width(TeApp *app, int body_width);
char *wrap_paste_text(TeApp *app, const char *utf8, int col);

/* Trigger paragraph reflow after an edit in hard-wrap mode */
void ed_auto_rewrap_after_edit(TeApp *app);

/* Silent reflow for callers that keep their own delta open */
void ed_auto_rewrap_after_edit_silent(TeApp *app);

/* Fit a freshly loaded document to the configured column */
void ui_editor_rewrap_loaded(TeApp *app);

/* Refit the document after a mode or column change, one undo step */
void ui_editor_rewrap_docwide(TeApp *app);

/* Left margin for editor body with line numbers */
int editor_body_offset(TeApp *app, int line_count);

/* Autosave swap helpers */
void ui_editor_swp_remove(const char *path);
int ui_editor_swp_recover(TeApp *app, const char *path);
void ui_editor_swp_cleanup_all(TeApp *app);

/* Recent files and session helpers */
void ui_editor_recent_load(void);
void ui_editor_recent_add(const char *path);
void ui_editor_recent_free(void);
int ui_editor_recent_open(TeApp *app);
void ui_editor_recent_save(void);

/* Quote current message as an FTN reply */
int ftn_reply(TeApp *app);
void ui_editor_session_save(TeApp *app);
void ui_editor_session_restore(TeApp *app);

/* Function key implementations */
int ui_editor_detect_wrap_hyphens(TeApp *app);
int search_prev(TeApp *app);
int search_next(TeApp *app);
int replace(TeApp *app);
int do_search(TeApp *app);
int do_search_in_files(TeApp *app);
int replace_current(TeApp *app);
int replace_all(TeApp *app);
int insert_file(TeApp *app);
int toggle_spell_panel(TeApp *app);

/* Control key implementations */
int paste(TeApp *app);
int ui_editor_goto_line(TeApp *app);
int copy(TeApp *app);
int cut(TeApp *app);
int ui_editor_goto_start(TeApp *app);
int ui_editor_goto_end(TeApp *app);
int ui_editor_export(TeApp *app);
int rewrap(TeApp *app);

/* Rich-text attribute toggle and alignment */
int ui_rich_attr_toggle(TeApp *app, unsigned short bit);
int ui_rich_align_set(TeApp *app, unsigned char align);

/* On Amiga: declare wcswidth (Markus Kuhn's tables) for use without pulling in full implementation */
#if defined(PLATFORM_AMIGA) && !defined(wcswidth)
int wcswidth(const wchar_t *wcs, size_t n);
#endif

/* Visual width with tab-stop support */
int wcs_vwidth_ex(const wchar_t *s, int n, int start_col, int tab_width);

/* Compute the screen X where a wrap hyphen must be drawn for a hard-wrap line */
int editor_hyphen_screen_x(int offset_x, int ln_offset, int width, int align_ind, const wchar_t *line, int line_len, int tab_width, int *out_visible);

/* Draw wide string with tab expansion */
void ui_draw_wcs_line_with_tabs(int y, int x, const wchar_t *s, int n, int tab_width);
void ui_draw_wcs_line_with_tabs_ex(int y, int x, const wchar_t *s, int n, int tab_width, const int *offsets);

/* Repaint bold/underline runs over an already-drawn segment */
void ui_draw_wcs_attr_runs(int y, int x, const wchar_t *l, const EdLine *ln, int seg_start, int seg_end, int seg_start_vcol, int tab_width);
void ui_draw_wcs_attr_runs_ex(int y, int x, const wchar_t *l, const EdLine *ln, int seg_start, int seg_end, int seg_start_vcol, int tab_width, const int *offsets);

/* Draw wide string with tab expansion and per-character syntax colors */
void ui_draw_wcs_line_with_tabs_and_colors(int y, int x, const wchar_t *s, int n, int tab_width, const SyntaxClass *classes, int start_col);
void ui_draw_wcs_line_with_tabs_and_colors_ex(int y, int x, const wchar_t *s, int n, int tab_width, const SyntaxClass *classes, int start_col, const int *offsets);

/* Compute per-char cumulative extra column offsets to justify seg to target_vw */
int ui_justify_offsets(const wchar_t *seg, int seg_len, int cur_vw, int target_vw, int *offsets);

/* Paint an overlay sub-row range, handling tabs and per-char justify shifts */
void ui_paint_shifted_range(int screen_y, int col_offset, const wchar_t *wl, int seg_start, int seg_end, int seg_start_vcol, int tab_width, const int *offsets, int c_start, int c_end, unsigned int attr);

#endif /* UI_EDITOR_HELPER_H */
