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

#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include "te.h"
#include "../core/utf8.h"
#include "../components/config.h"
#include "../components/editor.h"
#include "../wrapper.h"

#ifdef HAVE_HUNSPELL
#include "../spell/spell.h"
#include "ui_spell.h"
#endif

#ifdef HAVE_MYTHES
#include "../thes/thes.h"
#endif

#ifdef HAVE_HYPHEN
#include "../hyph_wrap/hyph_wrap.h"
#endif

TeApp *te_app_new(void)
{
    TeApp *app = NULL;

    app = (TeApp *)calloc(1, sizeof(TeApp));

    if (!app)
        return NULL;

    app->wm = wm_new();

    if (!app->wm)
    {
        free(app);
        return NULL;
    }

    /* Create windows for layout */
    wm_add_window(app->wm, WIN_TABLIST, 0, 1, 20, LINES - 1);
    wm_add_window(app->wm, WIN_EDITOR, 20, 1, COLS - 20, LINES - 1);
    wm_add_window(app->wm, WIN_TRANSLATE, 20, LINES - SPELL_PANEL_HEIGHT - 1, COLS - 20, SPELL_PANEL_HEIGHT);
    wm_add_window(app->wm, WIN_SPELL, 20, LINES - SPELL_PANEL_HEIGHT - 1, COLS - 20, SPELL_PANEL_HEIGHT);

    app->tab_cap = 8;
    app->tabs = (TeTab **)calloc(app->tab_cap, sizeof(TeTab *));

    if (!app->tabs)
    {
        wm_free(app->wm);
        free(app);

        return NULL;
    }

    app->tab_count = 0;
    app->active_tab = -1;
    app->show_line_numbers = 0;
    app->show_tabs = 1;
    app->show_translate = 0;
    app->show_spell = 0;
    app->spell_panel_mode = -1; /* -1 = hidden, 0 = spell checker, 1 = translator */
    app->tabs_panel_active = 0;
    app->tabs_panel_selected = 0;
    app->hard_wrap = 0;
    app->wrap_col = 75;

    app->search.rows = NULL;
    app->search.cols = NULL;
    app->search.count = 0;
    app->search.is_mode = 0;
    app->search.only_mode = 0;
    app->search.match_current = 0;
    app->search.match_total = 0;
    app->search.query[0] = L'\0';

#ifdef HAVE_HUNSPELL
    app->spell_handle = NULL;
    app->spell_enabled = 0; /* Will be set from config after loading */
    app->spell_active = 0;  /* Will be set from config after loading */
    app->spell_current_word[0] = L'\0';
    app->spell_word_status = 0;
    app->spell_suggestions = NULL;
    app->spell_suggestion_count = 0;
    app->spell_scroll_offset = 0;

#ifdef HAVE_HYPHEN
    app->hyph_handle = NULL;
    app->hyph_wrap_enabled = 0; /* Will be set from config after loading */
#endif

#ifdef HAVE_MYTHES
    app->thes_handle = NULL;
#endif
#endif

    strncpy(app->charset_in, "UTF-8", sizeof(app->charset_in) - 1);
    app->charset_in[sizeof(app->charset_in) - 1] = '\0';

    strncpy(app->charset_out, "UTF-8", sizeof(app->charset_out) - 1);
    app->charset_out[sizeof(app->charset_out) - 1] = '\0';

    return app;
}

void te_app_free(TeApp *app)
{
    int i;

    if (!app)
        return;

    for (i = 0; i < app->tab_count; i++)
    {
        if (app->tabs[i])
        {
            te_tab_free(app->tabs[i]);
            app->tabs[i] = NULL;
        }
    }

    free(app->tabs);

    app->tabs = NULL;

    if (app->search.rows)
    {
        free(app->search.rows);
        app->search.rows = NULL;
    }

    if (app->search.cols)
    {
        free(app->search.cols);
        app->search.cols = NULL;
    }

#ifdef HAVE_HUNSPELL

    if (app->spell_handle)
    {
        spell_free((SpellChecker *)app->spell_handle);
        app->spell_handle = NULL;
    }
#endif

#ifdef HAVE_MYTHES
    if (app->thes_handle)
    {
        thes_free((ThesHandle *)app->thes_handle);
        app->thes_handle = NULL;
    }
#endif

#ifdef HAVE_HYPHEN
    if (app->hyph_handle)
    {
        hyph_free((HyphDict *)app->hyph_handle);
        app->hyph_handle = NULL;
    }
#endif

    if (app->wm)
    {
        wm_free(app->wm);
        app->wm = NULL;
    }

    free(app);
}

/* Get currently active tab */
TeTab *te_app_get_active_tab(TeApp *app)
{
    if (!app)
        return NULL;

    if (app->active_tab < 0 || app->active_tab >= app->tab_count)
        return NULL;

    return app->tabs[app->active_tab];
}

/* Add tab to app */
int te_app_add_tab(TeApp *app, TeTab *tab)
{
    TeTab **new_tabs;
    int new_cap;

    if (!app || !tab)
        return -1;

    if (app->tab_count >= app->tab_cap)
    {
        new_cap = app->tab_cap * 2;
        new_tabs = (TeTab **)realloc(app->tabs, new_cap * sizeof(TeTab *));

        if (!new_tabs)
            return -1;

        app->tabs = new_tabs;
        app->tab_cap = new_cap;
    }

    /* Apply app configuration to new tab */
    ed_set_undo_levels(tab->editor, app->cfg.undo_levels);
    ed_set_hard_wrap(tab->editor, app->cfg.hard_wrap);

    tab->show_line_numbers = app->show_line_numbers;

    app->tabs[app->tab_count] = tab;
    app->tab_count++;

    if (app->active_tab < 0)
        app->active_tab = 0;

    return 0;
}

/* Close tab by index */
int te_app_close_tab(TeApp *app, int index)
{
    int i;

    if (!app)
        return -1;

    if (index < 0 || index >= app->tab_count)
        return -1;

    if (app->tabs[index])
    {
        te_tab_free(app->tabs[index]);
        app->tabs[index] = NULL;
    }

    for (i = index; i < app->tab_count - 1; i++)
        app->tabs[i] = app->tabs[i + 1];

    app->tab_count--;

    if (app->active_tab >= app->tab_count)
        app->active_tab = app->tab_count - 1;

    return 0;
}

/* Switch to tab by index */
void te_app_switch_tab(TeApp *app, int index)
{
    if (!app)
        return;

    if (index < 0 || index >= app->tab_count)
        return;

    app->active_tab = index;
}

/* Set raw bytes in active tab */
void te_app_set_raw_bytes(TeApp *app, char *ptr, int len)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return;

    tab->raw_bytes = ptr;
    tab->raw_len = len;
}

/* Helper functions to access active tab data */
Ed *te_app_get_editor(TeApp *app)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return NULL;

    return tab->editor;
}

const char *te_app_get_filename(TeApp *app)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return "";

    return tab->filename;
}

char *te_app_get_raw_bytes(TeApp *app)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return NULL;

    return tab->raw_bytes;
}

int te_app_get_raw_len(TeApp *app)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return 0;

    return tab->raw_len;
}

int te_app_get_show_line_numbers(TeApp *app)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return 0;

    return tab->show_line_numbers;
}

void te_app_set_filename(TeApp *app, const char *val)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return;

    strncpy(tab->filename, val, TAB_FILENAME_MAX - 1);
    tab->filename[TAB_FILENAME_MAX - 1] = '\0';
}

void te_app_clear_filename(TeApp *app)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return;

    tab->filename[0] = '\0';
}

void te_app_set_show_line_numbers(TeApp *app, int val)
{
    TeTab *tab = NULL;

    app->show_line_numbers = val;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return;

    tab->show_line_numbers = val;
}

/* Initialize ncurses colors from config */
void te_init_colors(const TeConfig *cfg)
{
    int i;

    if (!has_colors())
        return;

    start_color();
    use_default_colors();

    /* Use config colors if provided, otherwise use defaults */
    if (cfg)
    {
        /* Apply colors; remap non-explicit slots via color_map */
        for (i = 1; i < TE_CFG_COLOR_MAX; i++)
        {
            int fg = cfg->color_fg[i];
            int bg = cfg->color_bg[i];

            if (!cfg->color_explicit[i] && cfg->color_map_initialized)
            {
                if (fg >= 0 && fg < 16)
                    fg = cfg->color_map[fg];

                if (bg >= 0 && bg < 16)
                    bg = cfg->color_map[bg];
            }

            init_pair((short)i, (short)fg, (short)bg);
        }

        /* Apply default background to entire screen */
        bkgd(COLOR_PAIR(COL_NORMAL) | ' ');

        erase();
        refresh();
    }
    else
    {
        /* Default colors */
        init_pair(COL_NORMAL, COLOR_WHITE, COLOR_BLACK);
        init_pair(COL_STATUS, COLOR_BLACK, COLOR_WHITE);
        init_pair(COL_TITLEBAR, COLOR_BLACK, COLOR_CYAN);
        init_pair(COL_POPUP, COLOR_WHITE, COLOR_BLUE);
        init_pair(COL_POPUP_SEL, COLOR_BLACK, COLOR_CYAN);
        init_pair(COL_BORDER, COLOR_CYAN, COLOR_BLACK);
        init_pair(COL_SEARCH_MATCH, COLOR_BLACK, COLOR_YELLOW);
        init_pair(COL_SPELL_CURRENT, COLOR_WHITE, COLOR_MAGENTA);
    }
}

/* Set status bar message */
void te_status(TeApp *app, const char *fmt, ...)
{
    va_list ap;

    if (!app)
        return;

    va_start(ap, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, ap);
    va_end(ap);
}

/* Draw title bar with filename and cursor info */
void te_draw_titlebar(TeApp *app)
{
    int x;
    char left[128], right[64];
    EdInfo info;
    const char *fn = NULL;
    int mod = 0;
    int prefix_len, suffix_len, max_fn_len, fn_len;
    char truncated[128];
    int avail;
    TeTab *tab = NULL;

    attron(COLOR_PAIR(COL_TITLEBAR));

    move(0, 0);

    for (x = 0; x < COLS; x++)
        addch(' ');

    tab = te_app_get_active_tab(app);

    if (tab && tab->filename[0])
    {
        const char *last_slash = strrchr(tab->filename, '/');

        if (!last_slash)
            last_slash = strrchr(tab->filename, '\\');

        fn = last_slash ? last_slash + 1 : tab->filename;
    }
    else
    {
        fn = "[No Name]";
    }

    if (tab && tab->editor)
    {
        ed_get_info(tab->editor, &info);
        mod = info.modified;
    }

    prefix_len = (int)strlen(WRAPPER_PID) + 2;
    suffix_len = mod ? 4 : 0;
    max_fn_len = COLS - prefix_len - suffix_len - 30;

    fn_len = (int)strlen(fn);

    if (fn_len > max_fn_len)
    {
        avail = max_fn_len - 3;

        if (avail < 0)
            avail = 0;

        strncpy(truncated, fn, avail);

        truncated[avail] = '\0';

        strcat(truncated, "...");
        fn = truncated;
    }

    snprintf(left, sizeof(left), "%s  %s%s", WRAPPER_PID, fn, mod ? " [+]" : "");

    if (tab && tab->editor)
    {
        ed_get_info(tab->editor, &info);

#ifdef HAVE_HUNSPELL
        snprintf(right, sizeof(right), "Ln %d/%d  Col %d  %s %s%s%s", info.row + 1, info.line_count, info.col + 1, app->hard_wrap ? "HARD" : "SOFT", (app->spell_enabled && app->spell_active && app->spell_handle) ? "SP " : "",
#ifdef HAVE_HYPHEN
                 (app->hyph_wrap_enabled && app->hyph_handle) ? "HY " : "",
#else
                 "",
#endif
                 info.insert_mode ? "INS" : "OVR");
#else
        snprintf(right, sizeof(right), "Ln %d/%d  Col %d  %s %s", info.row + 1, info.line_count, info.col + 1, app->hard_wrap ? "HARD" : "SOFT", info.insert_mode ? "INS" : "OVR");
#endif
    }
    else
    {
        right[0] = '\0';
    }

    mvaddnstr(0, 0, left, COLS);

    if (right[0])
    {
        int rlen = (int)strlen(right);
        int rx = COLS - rlen;

        if (rx > (int)strlen(left) + 2)
            mvaddnstr(0, rx, right, rlen);
    }

    attroff(COLOR_PAIR(COL_TITLEBAR));
}

/* Draw status bar with hints and charset info */
void te_draw_statusbar(TeApp *app)
{
    int x, y;
    int msg_len, rzone_len, rzone_start, max_left;
    char hint[64];
    char charset_info[128];
    TeTab *tab = NULL;

    y = LINES - 1;
    attron(COLOR_PAIR(COL_STATUS));
    move(y, 0);

    for (x = 0; x < COLS; x++)
        addch(' ');

    snprintf(hint, sizeof(hint), " F2=Save F3=Charset F1=Help ");

    rzone_len = (int)strlen(hint);
    rzone_start = COLS - rzone_len - 1;

    if (rzone_start < 0)
        rzone_start = COLS;

    if (rzone_start < COLS)
        mvaddnstr(y, rzone_start, hint, rzone_len);

    if (app)
    {
        tab = te_app_get_active_tab(app);

        if (tab)
        {
            const char *fn = NULL;
            const char *last_slash = NULL;

            if (tab->filename[0])
            {
                last_slash = strrchr(tab->filename, '/');

                if (!last_slash)
                    last_slash = strrchr(tab->filename, '\\');

                fn = last_slash ? last_slash + 1 : tab->filename;

                snprintf(charset_info, sizeof(charset_info), "%s  View: %s  Save: %s", fn, tab->charset_in[0] ? tab->charset_in : "UTF-8", tab->charset_out[0] ? tab->charset_out : "UTF-8");
            }
            else
            {
                snprintf(charset_info, sizeof(charset_info), "View: %s  Save: %s", tab->charset_in[0] ? tab->charset_in : "UTF-8", tab->charset_out[0] ? tab->charset_out : "UTF-8");
            }
        }
        else
        {
            snprintf(charset_info, sizeof(charset_info), "View: UTF-8  Save: UTF-8");
        }

        msg_len = (int)strlen(charset_info);
        max_left = (rzone_start < COLS ? rzone_start : COLS) - 2;

        if (max_left > msg_len)
            max_left = msg_len;

        if (max_left > 0)
            mvaddnstr(y, 1, charset_info, max_left);
    }

    attroff(COLOR_PAIR(COL_STATUS));
}

/* Draw box with borders */
void ui_box(int y, int x, int h, int w)
{
    int i;

    if (h < 2 || w < 2)
        return;

    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);

    for (i = 1; i < w - 1; i++)
    {
        mvaddch(y, x + i, ACS_HLINE);
        mvaddch(y + h - 1, x + i, ACS_HLINE);
    }

    for (i = 1; i < h - 1; i++)
    {
        mvaddch(y + i, x, ACS_VLINE);
        mvaddch(y + i, x + w - 1, ACS_VLINE);
    }
}

/* Draw horizontal line */
void te_hline(int y, int x, int len)
{
    int i;

    for (i = 0; i < len; i++)
        mvaddch(y, x + i, ACS_HLINE);
}

/* wchar_t -> UTF-8 (rotating static pool, safe for multiple calls per printf) */
const char *te_wcs2u8(const wchar_t *wcs)
{
    static char pool[8][512];
    static int slot = 0;
    char *out = NULL;
    int n, i;

    if (!wcs)
        return "";

    out = pool[slot];
    slot = (slot + 1) & 7;
    n = 0;

    for (i = 0; wcs[i] && n < (int)sizeof(pool[0]) - 4; i++)
    {
        unsigned long cp = (unsigned long)wcs[i];

        if (cp < 0x80)
        {
            out[n++] = (char)cp;
        }
        else if (cp < 0x800)
        {
            out[n++] = (char)(0xC0 | (cp >> 6));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out[n++] = (char)(0xE0 | (cp >> 12));
            out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        }
        else
        {
            out[n++] = (char)(0xF0 | (cp >> 18));
            out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        }
    }

    out[n] = '\0';

    return out;
}
