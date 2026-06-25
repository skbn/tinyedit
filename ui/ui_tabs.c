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

#include "te.h"
#include <stdlib.h>
#include <string.h>

/* Switch to next tab (Alt+J) */
int ui_tabs_switch_next(TeApp *app)
{
    int next;

    if (!app)
        return -1;

    if (app->tab_count <= 0)
        return -1;

    next = app->active_tab + 1;

    if (next >= app->tab_count)
        next = 0;

    te_app_switch_tab(app, next);

    return 0;
}

/* Switch to previous tab (Alt+K) */
int ui_tabs_switch_prev(TeApp *app)
{
    int prev;

    if (!app)
        return -1;

    if (app->tab_count <= 0)
        return -1;

    prev = app->active_tab - 1;

    if (prev < 0)
        prev = app->tab_count - 1;

    te_app_switch_tab(app, prev);

    return 0;
}

/* Switch to tab by index (Alt+1-9, Alt+0) */
int ui_tabs_switch_by_index(TeApp *app, int index)
{
    if (!app)
        return -1;

    if (index < 0 || index >= app->tab_count)
        return -1;

    te_app_switch_tab(app, index);

    return 0;
}

/* Draw tabs panel on the left side */
void ui_tabs_draw_panel(TeApp *app)
{
    int i;
    int x, y;
    int max_h;
    int scroll_offset;
    TeWindow *win = NULL;

    if (!app)
        return;

    if (!app->show_tabs)
        return;

    win = wm_get_window_by_type(app->wm, WIN_TABLIST);

    if (!win || !win->visible)
        return;

    max_h = win->h;
    scroll_offset = 0;

    /* Calculate scroll offset if active tab is not visible */
    if (app->active_tab >= max_h)
        scroll_offset = app->active_tab - max_h + 1;

    standend();

    /* Draw panel background */
    attron(COLOR_PAIR(COL_NORMAL));

    for (y = 0; y < max_h; y++)
    {
        for (x = 0; x < win->w; x++)
        {
            mvaddch(win->y + y, win->x + x, ' ');
        }
    }

    /* Draw tabs */
    for (i = 0; i < app->tab_count; i++)
    {
        int display_index;
        const char *name = NULL;
        char buf[128];
        TeTab *t = NULL;

        display_index = i - scroll_offset;

        if (display_index < 0 || display_index >= max_h)
            continue;

        t = app->tabs[i];

        if (!t)
            continue;

        name = t->filename;

        if (name[0] == '\0')
            name = "[No Name]";

        snprintf(buf, sizeof(buf), "%s%s", t->modified ? "[+]" : "", name);

        if (app->tabs_panel_active)
        {
            if (i == app->tabs_panel_selected)
                attron(COLOR_PAIR(COL_POPUP_SEL));
            else
                attron(COLOR_PAIR(COL_NORMAL));
        }
        else
        {
            if (i == app->active_tab)
                attron(COLOR_PAIR(COL_POPUP_SEL));
            else
                attron(COLOR_PAIR(COL_NORMAL));
        }

        mvaddnstr(win->y + display_index, win->x + 1, buf, win->w - 2);

        if (app->tabs_panel_active)
        {
            if (i == app->tabs_panel_selected)
                attroff(COLOR_PAIR(COL_POPUP_SEL));
            else
                attroff(COLOR_PAIR(COL_NORMAL));
        }
        else
        {
            if (i == app->active_tab)
                attroff(COLOR_PAIR(COL_POPUP_SEL));
            else
                attroff(COLOR_PAIR(COL_NORMAL));
        }
    }

    /* Draw border */
    attron(COLOR_PAIR(COL_BORDER));

    mvvline(win->y, win->x + win->w - 1, ACS_VLINE, max_h);

    attroff(COLOR_PAIR(COL_BORDER));
}
