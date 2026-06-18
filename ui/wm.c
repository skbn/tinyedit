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

#include <stdlib.h>
#include <string.h>
#ifdef PLATFORM_AMIGA
#include "../ncursesw_amiga.h"
#elif defined(PLATFORM_WIN32)
#include "../ncursesw_win32.h"
#else
#include <ncurses.h>
#endif
#include "wm.h"

/* Create new window manager */
TeWindowManager *wm_new(void)
{
    TeWindowManager *wm;

    wm = (TeWindowManager *)calloc(1, sizeof(TeWindowManager));

    if (!wm)
        return NULL;

    wm->win_cap = 8;
    wm->windows = (TeWindow *)calloc(wm->win_cap, sizeof(TeWindow));

    if (!wm->windows)
    {
        free(wm);
        return NULL;
    }

    wm->win_count = 0;
    wm->focused_win = -1;
    wm->layout = LAYOUT_LEFT;

    return wm;
}

/* Free window manager and all its windows */
void wm_free(TeWindowManager *wm)
{
    int i;

    if (!wm)
        return;

    for (i = 0; i < wm->win_count; i++)
    {
        if (wm->windows[i].nc_win)
        {
#ifdef PLATFORM_AMIGA
            delwin((WINDOW *)wm->windows[i].nc_win);
#elif defined(PLATFORM_WIN32)
            delwin((WINDOW *)wm->windows[i].nc_win);
#else
            delwin((WINDOW *)wm->windows[i].nc_win);
#endif
            wm->windows[i].nc_win = NULL;
        }
    }

    free(wm->windows);
    free(wm);
}

/* Add new window to manager */
TeWindow *wm_add_window(TeWindowManager *wm, WinType type, int x, int y, int w, int h)
{
    TeWindow *win;
    int new_cap;
    TeWindow *new_windows;

    if (!wm)
        return NULL;

    if (wm->win_count >= wm->win_cap)
    {
        new_cap = wm->win_cap * 2;
        new_windows = (TeWindow *)realloc(wm->windows, new_cap * sizeof(TeWindow));

        if (!new_windows)
            return NULL;

        wm->windows = new_windows;
        wm->win_cap = new_cap;
    }

    win = &wm->windows[wm->win_count];
    memset(win, 0, sizeof(TeWindow));

    win->type = type;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->visible = 1;
    win->has_focus = 0;
    win->nc_win = NULL;
    win->data = NULL;

    wm->win_count++;

    return win;
}

/* Remove window by index */
int wm_remove_window(TeWindowManager *wm, int index)
{
    int i;

    if (!wm)
        return -1;

    if (index < 0 || index >= wm->win_count)
        return -1;

    if (wm->windows[index].nc_win)
    {
#ifdef PLATFORM_AMIGA
        delwin((WINDOW *)wm->windows[index].nc_win);
#elif defined(PLATFORM_WIN32)
        delwin((WINDOW *)wm->windows[index].nc_win);
#else
        delwin((WINDOW *)wm->windows[index].nc_win);
#endif
        wm->windows[index].nc_win = NULL;
    }

    for (i = index; i < wm->win_count - 1; i++)
        wm->windows[i] = wm->windows[i + 1];

    wm->win_count--;

    if (wm->focused_win >= wm->win_count)
        wm->focused_win = wm->win_count - 1;

    return 0;
}

/* Destroy all ncurses windows without freeing structures */
void wm_destroy_all_ncurses(TeWindowManager *wm)
{
    int i;

    if (!wm)
        return;

    for (i = 0; i < wm->win_count; i++)
    {
        if (wm->windows[i].nc_win)
        {
#ifdef PLATFORM_AMIGA
            delwin((WINDOW *)wm->windows[i].nc_win);
#elif defined(PLATFORM_WIN32)
            delwin((WINDOW *)wm->windows[i].nc_win);
#else
            delwin((WINDOW *)wm->windows[i].nc_win);
#endif
            wm->windows[i].nc_win = NULL;
        }
    }
}

/* Move focus to next window */
void wm_focus_next(TeWindowManager *wm)
{
    if (!wm)
        return;

    if (wm->win_count == 0)
        return;

    if (wm->focused_win < 0 || wm->focused_win >= wm->win_count)
        wm->focused_win = 0;

    wm->windows[wm->focused_win].has_focus = 0;
    wm->focused_win++;

    if (wm->focused_win >= wm->win_count)
        wm->focused_win = 0;

    wm->windows[wm->focused_win].has_focus = 1;
}

/* Move focus to previous window */
void wm_focus_prev(TeWindowManager *wm)
{
    if (!wm)
        return;

    if (wm->win_count == 0)
        return;

    if (wm->focused_win < 0 || wm->focused_win >= wm->win_count)
        wm->focused_win = 0;

    wm->windows[wm->focused_win].has_focus = 0;
    wm->focused_win--;

    if (wm->focused_win < 0)
        wm->focused_win = wm->win_count - 1;

    wm->windows[wm->focused_win].has_focus = 1;
}

/* Set focus to specific window by index */
void wm_focus_by_index(TeWindowManager *wm, int index)
{
    int i;

    if (!wm)
        return;

    if (wm->win_count == 0)
        return;

    if (index < 0 || index >= wm->win_count)
        return;

    for (i = 0; i < wm->win_count; i++)
        wm->windows[i].has_focus = 0;

    wm->focused_win = index;
    wm->windows[index].has_focus = 1;
}

/* Recalculate layout for LEFT layout (tabs left, editor right) */
void wm_recalc_layout_left(TeWindowManager *wm, int screen_w, int screen_h, int show_tabs, int show_translate, int show_spell)
{
    int tab_w;
    int panel_h;
    int i;

    if (!wm)
        return;

    tab_w = show_tabs ? 20 : 0;
    panel_h = (show_translate || show_spell) ? 10 : 0;

    for (i = 0; i < wm->win_count; i++)
    {
        TeWindow *win;

        win = &wm->windows[i];

        if (win->type == WIN_TABLIST)
        {
            win->x = 0;
            win->y = 1;
            win->w = tab_w;
            win->h = screen_h - 1;
            win->visible = show_tabs;
        }
        else if (win->type == WIN_EDITOR)
        {
            win->x = tab_w;
            win->y = 1;
            win->w = screen_w - tab_w;
            win->h = screen_h - 1 - panel_h;
        }
        else if (win->type == WIN_TRANSLATE)
        {
            win->x = tab_w;
            win->y = screen_h - panel_h;
            win->w = screen_w - tab_w;
            win->h = panel_h;
            win->visible = show_translate;
        }
        else if (win->type == WIN_SPELL)
        {
            win->x = tab_w;
            win->y = screen_h - panel_h;
            win->w = screen_w - tab_w;
            win->h = panel_h;
            win->visible = show_spell;
        }
    }
}

/* Get window by type */
TeWindow *wm_get_window_by_type(TeWindowManager *wm, WinType type)
{
    int i;

    if (!wm)
        return NULL;

    for (i = 0; i < wm->win_count; i++)
    {
        if (wm->windows[i].type == type)
            return &wm->windows[i];
    }

    return NULL;
}
