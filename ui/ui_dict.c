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
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "te.h"
#include "wm.h"
#include "ui_dict.h"
#include "../components/config.h"

static int count_lines(const char *text)
{
    int n;
    const char *p = NULL;
    int saw_char = 0;

    if (!text || !*text)
        return 0;

    n = 1;

    for (p = text; *p; p++)
    {
        if (*p == '\n')
        {
            n++;
            saw_char = 0;
        }
        else
        {
            saw_char = 1;
        }
    }

    if (!saw_char && n > 0)
        n--;

    return n;
}

static const char *line_at(const char *text, int n)
{
    int cur = 0;
    const char *p = text;

    if (!text)
        return NULL;

    if (n <= 0)
        return text;

    while (*p && cur < n)
    {
        if (*p == '\n')
            cur++;

        p++;
    }

    if (!*p && cur < n)
        return NULL;

    return p;
}

static int copy_line(const char *src, char *out, int max_bytes)
{
    int n = 0;

    while (n < max_bytes - 1 && src[n] && src[n] != '\n')
    {
        out[n] = src[n];
        n++;
    }

    out[n] = '\0';
    return n;
}

void ui_dict_draw_panel(TeApp *app)
{
    TeWindow *win = NULL;
    int x, y;
    int content_rows;
    int line_idx;
    const char *p = NULL;
    char line_buf[512];
    char title[160];
    int total;
    int x_pos;

    if (!app)
        return;

    if (app->spell_panel_mode != 2)
        return;

    win = wm_get_window_by_type(app->wm, WIN_DICT);

    if (!win || !win->visible)
        return;

    standend();
    attron(COLOR_PAIR(COL_NORMAL));

    /* Clear panel */
    for (y = 0; y < win->h; y++)
    {
        for (x = 0; x < win->w; x++)
            mvaddch(win->y + y, win->x + x, ' ');
    }

    /* Title */
    attron(COLOR_PAIR(COL_TITLEBAR));

    if (app->dict_word[0])
        snprintf(title, sizeof(title), "[Dictionary] %s", app->dict_word);
    else
        snprintf(title, sizeof(title), "[Dictionary]");

    mvaddstr(win->y, win->x + 1, title);

    attron(COLOR_PAIR(COL_NORMAL));

    content_rows = win->h - 1;

    if (content_rows <= 0)
        return;

    if (!app->dict_result || !app->dict_result[0])
    {
        mvaddstr(win->y + 1, win->x + 1, "Select text and press Alt+R (backend must be STARDICT)");
        return;
    }

    p = line_at(app->dict_result, app->dict_scroll);

    if (!p)
        p = app->dict_result;

    for (line_idx = 0; line_idx < content_rows && *p; line_idx++)
    {
        int limit = win->w - 2;

        if (limit < 1)
            limit = 1;

        if (limit > (int)sizeof(line_buf) - 1)
            limit = (int)sizeof(line_buf) - 1;

        copy_line(p, line_buf, limit + 1);
        mvaddstr(win->y + 1 + line_idx, win->x + 1, line_buf);

        while (*p && *p != '\n')
            p++;

        if (*p == '\n')
            p++;
    }

    /* Scroll indicator */

    total = count_lines(app->dict_result);

    if (total > content_rows)
    {
        char ind[32];
        int below = (app->dict_scroll + content_rows < total);
        int above = (app->dict_scroll > 0);

        if (above && below)
            snprintf(ind, sizeof(ind), " [%d/%d] PgUp/PgDn", app->dict_scroll + 1, total);
        else if (above)
            snprintf(ind, sizeof(ind), " [%d/%d] PgUp", app->dict_scroll + 1, total);
        else
            snprintf(ind, sizeof(ind), " [%d/%d] PgDn", app->dict_scroll + 1, total);

        attron(COLOR_PAIR(COL_TITLEBAR));

        x_pos = win->x + win->w - (int)strlen(ind) - 1;

        if (x_pos > win->x + 1)
            mvaddstr(win->y, x_pos, ind);

        attron(COLOR_PAIR(COL_NORMAL));
    }
}

void ui_dict_set_result(TeApp *app, const char *word_or_phrase, const char *text)
{
    size_t tl;

    if (!app)
        return;

    if (app->dict_result)
    {
        free(app->dict_result);
        app->dict_result = NULL;
    }

    if (!text)
    {
        app->dict_word[0] = '\0';
        app->dict_scroll = 0;
        return;
    }

    if (word_or_phrase)
    {
        strncpy(app->dict_word, word_or_phrase, sizeof(app->dict_word) - 1);
        app->dict_word[sizeof(app->dict_word) - 1] = '\0';
    }
    else
    {
        app->dict_word[0] = '\0';
    }

    tl = strlen(text);

    app->dict_result = (char *)malloc(tl + 1);

    if (app->dict_result)
        memcpy(app->dict_result, text, tl + 1);

    app->dict_scroll = 0;
}

void ui_dict_free(TeApp *app)
{
    if (!app)
        return;

    if (app->dict_result)
    {
        free(app->dict_result);
        app->dict_result = NULL;
    }

    app->dict_word[0] = '\0';
    app->dict_scroll = 0;
}

int ui_dict_scroll_up(TeApp *app)
{
    if (!app || app->spell_panel_mode != 2)
        return 0;

    if (!app->dict_result || !app->dict_result[0])
        return 0;

    if (app->dict_scroll <= 0)
        return 0;

    app->dict_scroll--;
    return 1;
}

int ui_dict_scroll_down(TeApp *app)
{
    TeWindow *win = NULL;
    int total;
    int content_rows;

    if (!app || app->spell_panel_mode != 2)
        return 0;

    if (!app->dict_result || !app->dict_result[0])
        return 0;

    win = wm_get_window_by_type(app->wm, WIN_DICT);

    if (!win)
        return 0;

    content_rows = win->h - 1;

    if (content_rows <= 0)
        return 0;

    total = count_lines(app->dict_result);

    if (app->dict_scroll >= total - content_rows)
        return 0;

    app->dict_scroll++;
    return 1;
}
