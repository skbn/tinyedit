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

#ifndef TINYEDIT_WM_H
#define TINYEDIT_WM_H

#include "../core/keys.h"

#define SPELL_PANEL_HEIGHT 5

typedef enum
{
    WIN_EDITOR,
    WIN_TABLIST,
    WIN_TRANSLATE,
    WIN_SPELL,
    WIN_DICT,
    WIN_FILEBROWSER,
    WIN_HELP,
    WIN_CUSTOM
} WinType;

typedef enum
{
    LAYOUT_LEFT,
    LAYOUT_RIGHT,
    LAYOUT_BOTTOM,
    LAYOUT_GRID,
    LAYOUT_TABS,
    LAYOUT_CUSTOM
} LayoutType;

typedef struct TeWindow TeWindow;
typedef struct TeWindowManager TeWindowManager;

struct TeWindow
{
    WinType type;
    void *nc_win;
    int x;
    int y;
    int w;
    int h;
    int visible;
    int has_focus;
    void *data;
};

struct TeWindowManager
{
    TeWindow *windows;
    int win_count;
    int win_cap;
    int focused_win;
    LayoutType layout;
};

TeWindowManager *wm_new(void);
void wm_free(TeWindowManager *wm);
TeWindow *wm_add_window(TeWindowManager *wm, WinType type, int x, int y, int w, int h);
int wm_remove_window(TeWindowManager *wm, int index);
void wm_destroy_all_ncurses(TeWindowManager *wm);
void wm_focus_next(TeWindowManager *wm);
void wm_focus_prev(TeWindowManager *wm);
void wm_focus_by_index(TeWindowManager *wm, int index);
void wm_recalc_layout_left(TeWindowManager *wm, int screen_w, int screen_h, int show_tabs, int spell_panel_mode, int rich_mode);
TeWindow *wm_get_window_by_type(TeWindowManager *wm, WinType type);

#endif
