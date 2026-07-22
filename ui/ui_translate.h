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

#ifndef UI_TRANSLATE_H
#define UI_TRANSLATE_H

#include "te.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef HAVE_TRANSLATE

    /* Load translator handle from current config (TRANSLATE_*) */
    int ui_translate_load_from_config(TeApp *app);

    /* Free the translator handle and any cached state. NULL-safe */
    void ui_translate_unload(TeApp *app);

    /* User pressed Alt+R - translate selection */
    int ui_translate_action(TeApp *app);

    /* Replace selection with translated text */
    int ui_translate_replace_selection(TeApp *app, int first, int last, const char *new_utf8);

#else

/* Inert stubs */
int ui_translate_load_from_config(TeApp *app);
void ui_translate_unload(TeApp *app);
int ui_translate_action(TeApp *app);

#endif

#ifdef __cplusplus
}
#endif

#endif /* UI_TRANSLATE_H */
