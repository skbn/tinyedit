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

TeApp *te_app_new(void)
{
    TeApp *app = (TeApp *)calloc(1, sizeof(TeApp));

    if (!app)
        return NULL;

    app->editor = ed_new();

    if (!app->editor)
    {
        free(app);
        return NULL;
    }

    /* Initialize charset fields to UTF-8 by default */
    strncpy(app->charset_in, "UTF-8", sizeof(app->charset_in) - 1);
    app->charset_in[sizeof(app->charset_in) - 1] = '\0';

    strncpy(app->charset_out, "UTF-8", sizeof(app->charset_out) - 1);
    app->charset_out[sizeof(app->charset_out) - 1] = '\0';

    app->show_line_numbers = 0; /* Line numbers disabled by default */

    return app;
}

void te_app_free(TeApp *app)
{
    if (!app)
        return;

    if (app->editor)
        ed_free(app->editor);

    if (app->search.rows)
        free(app->search.rows);

    if (app->search.cols)
        free(app->search.cols);

    if (app->raw_bytes)
        free(app->raw_bytes);

    free(app);
}

/* Colors */
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
    }
}

/* Status */
void te_status(TeApp *app, const char *fmt, ...)
{
    va_list ap;

    if (!app)
        return;

    va_start(ap, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, ap);
    va_end(ap);
}

/* Drawing helpers */
void te_draw_titlebar(TeApp *app)
{
    int x;
    char left[128], right[64];
    EdInfo info;
    const char *fn;
    int mod = 0;
    int prefix_len, suffix_len, max_fn_len, fn_len;
    char truncated[128];
    int avail;

    attron(COLOR_PAIR(COL_TITLEBAR));

    move(0, 0);

    for (x = 0; x < COLS; x++)
        addch(' ');

    /* Left: "tinyedit - filename [*]" */

    if (app && app->filename[0])
    {
        const char *last_slash = strrchr(app->filename, '/');

        if (!last_slash)
            last_slash = strrchr(app->filename, '\\');

        fn = last_slash ? last_slash + 1 : app->filename;
    }
    else
    {
        fn = "[No Name]";
    }

    if (app && app->editor)
    {
        ed_get_info(app->editor, &info);
        mod = info.modified;
    }

    /* Truncate filename if too long based on actual terminal width */
    prefix_len = (int)strlen(WRAPPER_PID) + 2;
    suffix_len = mod ? 4 : 0;
    max_fn_len = COLS - prefix_len - suffix_len - 30; /* Reserve space for right side */

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

    /* Right: "Ln X/Y  Col Z  INS/OVR" */
    if (app && app->editor)
    {
        ed_get_info(app->editor, &info);
        snprintf(right, sizeof(right), "Ln %d/%d  Col %d  %s %s", info.row + 1, info.line_count, info.col + 1, app->hard_wrap ? "HARD" : "SOFT", info.insert_mode ? "INS" : "OVR");
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

void te_draw_statusbar(TeApp *app)
{
    int x, y;
    int msg_len, rzone_len, rzone_start, max_left;
    char hint[64];
    char charset_info[128];

    y = LINES - 1;
    attron(COLOR_PAIR(COL_STATUS));
    move(y, 0);

    for (x = 0; x < COLS; x++)
        addch(' ');

    /* Right: short key hint */
    if (app && app->search.is_mode)
        snprintf(hint, sizeof(hint), " F3=Prev F4=Next F5=Change F6=ALL ESC=Exit ");
    else
        snprintf(hint, sizeof(hint), " F2=Save F3=Charset F1=Help ");

    rzone_len = (int)strlen(hint);
    rzone_start = COLS - rzone_len - 1;

    if (rzone_start < 0)
        rzone_start = COLS;

    if (rzone_start < COLS)
        mvaddnstr(y, rzone_start, hint, rzone_len);

    /* Left: status message or match info or charset info */
    if (app && (app->search.is_mode || app->search.only_mode) && app->search.match_total > 0)
    {
        char match_msg[64];

        snprintf(match_msg, sizeof(match_msg), "Match %d/%d", app->search.match_current, app->search.match_total);
        msg_len = (int)strlen(match_msg);
        max_left = (rzone_start < COLS ? rzone_start : COLS) - 2;

        if (max_left > msg_len)
            max_left = msg_len;

        if (max_left > 0)
            mvaddnstr(y, 1, match_msg, max_left);
    }
    else if (app && app->status[0])
    {
        msg_len = (int)strlen(app->status);
        max_left = (rzone_start < COLS ? rzone_start : COLS) - 2;

        if (max_left > msg_len)
            max_left = msg_len;

        if (max_left > 0)
            mvaddnstr(y, 1, app->status, max_left);
    }
    else if (app)
    {
        /* Show charset info when no status message */
        snprintf(charset_info, sizeof(charset_info), "View: %s  Save: %s", app->charset_in[0] ? app->charset_in : "UTF-8", app->charset_out[0] ? app->charset_out : "UTF-8");

        msg_len = (int)strlen(charset_info);
        max_left = (rzone_start < COLS ? rzone_start : COLS) - 2;

        if (max_left > msg_len)
            max_left = msg_len;

        if (max_left > 0)
            mvaddnstr(y, 1, charset_info, max_left);
    }

    attroff(COLOR_PAIR(COL_STATUS));
}

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
    char *out;
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
