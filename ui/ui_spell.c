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

#include <stdio.h>
#include <string.h>
#include "te.h"
#include "wm.h"

/* Draw spell/translate panel */
void ui_spell_draw_panel(TeApp *app)
{
    TeWindow *win;
    int x, y;

    if (!app)
        return;

    if (app->spell_panel_mode < 0)
        return;

    win = wm_get_window_by_type(app->wm, WIN_SPELL);

    if (!win || !win->visible)
        return;

    /* Draw panel background */
    attron(COLOR_PAIR(COL_NORMAL));

    for (y = 0; y < win->h; y++)
    {
        for (x = 0; x < win->w; x++)
            mvaddch(win->y + y, win->x + x, ' ');
    }

    /* Draw panel title */
    attron(COLOR_PAIR(COL_TITLEBAR));
    mvaddstr(win->y, win->x + 1, app->spell_panel_mode == 0 ? "[Spell Checker]" : "[Translator]");
    attron(COLOR_PAIR(COL_NORMAL));

    /* Draw placeholder text */
    if (app->spell_panel_mode == 0)
        mvaddstr(win->y + 1, win->x + 1, "Spell checker not yet implemented");
    else
        mvaddstr(win->y + 1, win->x + 1, "Translator not yet implemented");
}
