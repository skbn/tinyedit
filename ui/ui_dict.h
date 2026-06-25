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

#ifndef UI_DICT_H
#define UI_DICT_H

#include "te.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Draw dict panel inside TeWindow. No-op if spell_panel_mode != 2 */
    void ui_dict_draw_panel(TeApp *app);

    /* Set panel content. word_or_phrase in header, text is definition */
    void ui_dict_set_result(TeApp *app, const char *word_or_phrase, const char *text);

    /* Free buffered result */
    void ui_dict_free(TeApp *app);

    /* Scroll handlers. Return 1 if scroll happened, 0 otherwise */
    int ui_dict_scroll_up(TeApp *app);
    int ui_dict_scroll_down(TeApp *app);

#ifdef __cplusplus
}
#endif

#endif /* UI_DICT_H */
