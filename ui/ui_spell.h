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

#ifndef UI_SPELL_H
#define UI_SPELL_H

#include "te.h"

void ui_spell_draw_panel(TeApp *app);

#ifdef HAVE_HUNSPELL

#define UI_SPELL_ADD_TO_DICT (-2)

void ui_spell_free_app_suggestions(TeApp *app);
int ui_spell_suggest(TeApp *app, const char *word, char **suggestions, int count);
int spell_load_from_config(TeApp *app);
int spell_check_word(TeApp *app);
int ui_spell_check_word_simple(TeApp *app, const wchar_t *word, int word_len);
#endif

#endif /* UI_SPELL_H */
