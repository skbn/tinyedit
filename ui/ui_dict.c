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
#include "../core/utf8.h"

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

/* Truncate UTF-8 string to max_cols display columns, appending "..." if needed */
static char *truncate_utf8_cols(const char *s, int max_cols)
{
    wchar_t *w = NULL;
    char *out = NULL;
    int wlen;
    int width;
    int i;

    if (!s || !*s || max_cols <= 0)
        return NULL;

    w = utf8_to_wcs(s, &wlen);

    if (!w)
        return NULL;

    width = wcswidth(w, wlen);

    if (width <= max_cols)
    {
        out = wcs_to_utf8(w, wlen);

        free(w);
        return out;
    }

    if (max_cols < 3)
        max_cols = 3;

    for (i = wlen; i > 0; i--)
    {
        if (wcswidth(w, i) <= max_cols - 3)
            break;
    }

    w[i] = L'.';
    w[i + 1] = L'.';
    w[i + 2] = L'.';
    w[i + 3] = L'\0';

    out = wcs_to_utf8(w, (int)wcslen(w));

    free(w);
    return out;
}

void ui_dict_draw_panel(TeApp *app)
{
    TeWindow *win = NULL;
    int x;
    int y;
    int w;
    int h;
    int j;
    int row;
    int content_rows;
    int line_idx;
    const char *p = NULL;
    char line_buf[512];
    char title[160];
    char ind[40];
    int total;
    int x_pos;
    int title_max;
    char *title_disp = NULL;
    const char *prefix = "[Dictionary]";

    if (!app)
        return;

    if (app->spell_panel_mode != 2)
        return;

    win = wm_get_window_by_type(app->wm, WIN_DICT);

    if (!win || !win->visible)
        return;

    x = win->x;
    y = win->y;
    w = win->w;
    h = win->h;

    /* Prepare indicator first so we know how much room the title has */
    content_rows = DICT_PANEL_ROWS;

    ind[0] = '\0';

    if (content_rows > 0 && app->dict_result && app->dict_result[0])
    {
        total = count_lines(app->dict_result);

        if (total > content_rows)
        {
            int below = (app->dict_scroll + content_rows < total);
            int above = (app->dict_scroll > 0);

            if (above && below)
                snprintf(ind, sizeof(ind), " [%d/%d] PgUp/PgDn", app->dict_scroll + 1, total);
            else if (above)
                snprintf(ind, sizeof(ind), " [%d/%d] PgUp", app->dict_scroll + 1, total);
            else
                snprintf(ind, sizeof(ind), " [%d/%d] PgDn", app->dict_scroll + 1, total);
        }
    }

    /* Title: keep the prefix, truncate only the word part */
    title_max = w - 3;

    if (ind[0])
        title_max = w - (int)strlen(ind) - 3;

    if (title_max < 12)
        title_max = 12;

    if (app->dict_word[0] && title_max > 12 + 1 + 3)
    {
        int word_max = title_max - 12 - 1;
        char *trunc_word = truncate_utf8_cols(app->dict_word, word_max);

        if (trunc_word)
        {
            snprintf(title, sizeof(title), "%s %s", prefix, trunc_word);
            free(trunc_word);
        }
        else
        {
            snprintf(title, sizeof(title), "%s %s", prefix, app->dict_word);
        }
    }
    else
    {
        snprintf(title, sizeof(title), "%s", prefix);
    }

    title_disp = truncate_utf8_cols(title, title_max);

    /* Title bar in titlebar color */
    standend();
    attron(COLOR_PAIR(COL_TITLEBAR));

    for (j = 0; j < w; j++)
        mvaddch(y, x + j, ' ');

    if (title_disp)
    {
        mvaddstr(y, x + 1, title_disp);
        free(title_disp);
    }
    else
    {
        mvaddstr(y, x + 1, title);
    }

    /* Scroll indicator on the title bar (titlebar color) */
    if (ind[0])
    {
        x_pos = x + w - (int)strlen(ind) - 1;

        if (x_pos > x + 1)
            mvaddstr(y, x_pos, ind);
    }

    if (content_rows <= 0)
    {
        standend();
        return;
    }

    /* Content area in normal colors */
    standend();

    for (row = 1; row < h; row++)
    {
        for (j = 0; j < w; j++)
            mvaddch(y + row, x + j, ' ');
    }

    if (!app->dict_result || !app->dict_result[0])
    {
        mvaddstr(y + 1, x + 1, "Select text and press Alt+R (backend must be STARDICT)");
        standend();
        return;
    }

    p = line_at(app->dict_result, app->dict_scroll);

    if (!p)
        p = app->dict_result;

    for (line_idx = 0; line_idx < content_rows && *p; line_idx++)
    {
        int limit = w - 2;

        if (limit < 1)
            limit = 1;

        if (limit > (int)sizeof(line_buf) - 1)
            limit = (int)sizeof(line_buf) - 1;

        copy_line(p, line_buf, limit + 1);
        mvaddstr(y + 1 + line_idx, x + 1, line_buf);

        while (*p && *p != '\n')
            p++;

        if (*p == '\n')
            p++;
    }

    standend();
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

    content_rows = DICT_PANEL_ROWS;

    if (content_rows <= 0)
        return 0;

    total = count_lines(app->dict_result);

    if (app->dict_scroll >= total - content_rows)
        return 0;

    app->dict_scroll++;
    return 1;
}
