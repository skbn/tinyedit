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

/* Additional helper functions */
int charset_select(TeApp *app);

void clear_search_highlights(TeApp *app);

/* Functions from ui_editor.c needed by ui_editor_helper.c */
void soft_reset_desired(void);
void soft_reset_viewport_to_cursor(TeApp *app, int width);
int editor_eff_wrap(const TeApp *app);
char *wrap_paste_text(TeApp *app, const char *utf8, int col);

/* Function key implementations */
int search_prev(TeApp *app);
int search_next(TeApp *app);
int replace(TeApp *app);
int do_search(TeApp *app);
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

/* On Amiga: declare wcswidth (Markus Kuhn's tables) for use without pulling in full implementation */
#if defined(PLATFORM_AMIGA) && !defined(wcswidth)
int wcswidth(const wchar_t *wcs, size_t n);
#endif

/* Visual width in display columns: wide glyphs=2, normal=1, control=1 */
int wcs_vwidth(const wchar_t *s, int n);

#endif /* UI_EDITOR_HELPER_H */
