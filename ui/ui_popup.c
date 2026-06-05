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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include "te.h"
#include "../core/charset.h"
#include "../core/keys.h"
#include "../core/utf8.h"

void ui_popup_center(int want_h, int want_w, int *y, int *x, int *h, int *w)
{
    *h = want_h < LINES ? want_h : LINES;
    *w = want_w < COLS ? want_w : COLS;
    *y = (LINES - *h) / 2;
    *x = (COLS - *w) / 2;
}

void ui_draw_popup_frame(int y, int x, int h, int w, const char *title)
{
    int i, tl, tx;

    attron(COLOR_PAIR(COL_POPUP));

    /* Fill entire rectangle with popup background color */
    for (i = 0; i < h; i++)
    {
        int j;

        move(y + i, x);

        for (j = 0; j < w; j++)
            addch(' ');
    }

    attroff(COLOR_PAIR(COL_POPUP));
    attron(COLOR_PAIR(COL_BORDER));

    ui_box(y, x, h, w);

    if (title && title[0])
    {
        tl = (int)strlen(title);
        tx = x + (w - tl - 2) / 2;

        if (tx > x)
        {
            mvaddch(y, tx - 1, ' ');
            mvaddnstr(y, tx, title, tl);
            mvaddch(y, tx + tl, ' ');
        }
    }

    attroff(COLOR_PAIR(COL_BORDER));
}

/* Confirm popup */
int ui_popup_confirm(const char *title, const char *msg)
{
    int y, x, h, w;
    int want_w = msg ? (int)strlen(msg) + 8 : 30;
    int ch;

    if (want_w < 30)
        want_w = 30;

    if (want_w > COLS)
        want_w = COLS;

    ui_popup_center(7, want_w, &y, &x, &h, &w);

    for (;;)
    {
        standend(); /* Clear any residual attributes from previous iteration */
        ui_draw_popup_frame(y, x, h, w, title ? title : "Confirm");
        attron(COLOR_PAIR(COL_POPUP));

        if (msg)
            mvaddnstr(y + 2, x + 2, msg, w - 4);

        mvaddnstr(y + 4, x + 2, "[Y]es  [N]o", w - 4);

        attroff(COLOR_PAIR(COL_POPUP));

        move(y + 4, x + 2);
        refresh();

        ch = getch();

        if (ch == 'y' || ch == 'Y' || ch == '\n' || ch == '\r')
            return 1;

        if (ch == 'n' || ch == 'N' || ch == 27)
            return 0;
    }
}

/* List picker */
int ui_popup_list(const char *title, const char **items, int count, int initial)
{
    int y, x, h, w;
    int want_h, want_w, max_item, i;
    int sel, top;
    wint_t wch;
    int rc;

    if (!items || count <= 0)
        return -1;

    max_item = 0;

    for (i = 0; i < count; i++)
    {
        int l = items[i] ? (int)strlen(items[i]) : 0;

        if (l > max_item)
            max_item = l;
    }

    want_h = count + 4;

    if (want_h > LINES - 2)
        want_h = LINES - 2;

    want_w = max_item + 6;

    if (want_w < 24)
        want_w = 24;

    if (want_w > COLS)
        want_w = COLS;

    ui_popup_center(want_h, want_w, &y, &x, &h, &w);

    sel = (initial >= 0 && initial < count) ? initial : 0;
    top = 0;

    if (sel >= h - 4)
        top = sel - (h - 4) + 1;

    for (;;)
    {
        int visible = h - 4;
        int row;

        standend(); /* Clear any residual attributes from previous iteration */
        ui_draw_popup_frame(y, x, h, w, title ? title : "Select");
        attron(COLOR_PAIR(COL_POPUP));

        for (row = 0; row < visible && top + row < count; row++)
        {
            int j;
            int idx = top + row;
            const char *label = items[idx] ? items[idx] : "";

            if (idx == sel)
                attron(COLOR_PAIR(COL_POPUP_SEL));

            move(y + 2 + row, x + 2);

            for (j = 0; j < w - 4; j++)
                addch(' ');

            mvaddnstr(y + 2 + row, x + 3, label, w - 6);

            if (idx == sel)
                attroff(COLOR_PAIR(COL_POPUP_SEL));
        }

        /* Scroll indicator */
        if (count > visible)
        {
            char scroll_info[32];

            snprintf(scroll_info, sizeof(scroll_info), "%d/%d", sel + 1, count);

            mvaddnstr(y + h - 2, x + w - (int)strlen(scroll_info) - 2, scroll_info, (int)strlen(scroll_info));
        }

        /* Status bar */
        attron(COLOR_PAIR(COL_STATUS));

        for (i = 0; i < w - 4; i++)
            mvaddch(y + h - 2, x + 2 + i, ' ');

        mvaddnstr(y + h - 2, x + 2, "Enter=OK  ESC=Cancel", w - 4);

        attroff(COLOR_PAIR(COL_STATUS));
        refresh();

        rc = wrapper_read_key(&wch);

        if (rc == ERR)
            continue;

        if (wch == 27)
            return -1;

        if (wch == L'\n' || wch == L'\r' || (rc == KEY_CODE_YES && wch == KEY_ENTER))
            return sel;

        if ((rc == KEY_CODE_YES && wch == KEY_UP) || wch == 'k')
        {
            if (sel > 0)
                sel--;

            if (sel < top)
                top = sel;
        }
        else if ((rc == KEY_CODE_YES && wch == KEY_DOWN) || wch == 'j')
        {
            if (sel < count - 1)
                sel++;

            if (sel >= top + visible)
                top = sel - visible + 1;
        }
        else if (rc == KEY_CODE_YES && wch == KEY_PPAGE || wch == CTRL('U'))
        {
            sel -= visible;

            if (sel < 0)
                sel = 0;

            top = sel;
        }
        else if (rc == KEY_CODE_YES && wch == KEY_NPAGE || wch == CTRL('D'))
        {
            sel += visible;

            if (sel >= count)
                sel = count - 1;

            if (sel >= top + visible)
                top = sel - visible + 1;
        }
        else if (rc == KEY_CODE_YES && wch == KEY_HOME || wch == CTRL('B'))
        {
            sel = 0;
            top = 0;
        }
        else if (rc == KEY_CODE_YES && wch == KEY_END || wch == CTRL('E'))
        {
            sel = count - 1;
            top = sel - visible + 1;

            if (top < 0)
                top = 0;
        }
    }
}

int ui_popup_input_wcs(const char *title, const char *prompt, wchar_t *wbuf, int wcap)
{
    int y, x, h, w;
    int want_w = wcap + 4;
    int ch;
    InputState state;
    WINDOW *saved;

    if (!wbuf || wcap < 2)
        return -1;

    if (want_w < 50)
        want_w = 50;

    if (want_w > COLS)
        want_w = COLS;

    ui_popup_center(7, want_w, &y, &x, &h, &w);

    saved = newwin(h, w, y, x);
    if (saved)
        copywin(stdscr, saved, y, x, 0, 0, h - 1, w - 1, 0);

    /* Init state */
    state.buf = wbuf;
    state.bufsz = wcap;
    state.cursor = (int)wcslen(wbuf);
    state.len = state.cursor;

    curs_set(1);

    for (;;)
    {
        ui_draw_popup_frame(y, x, h, w, title);
        attron(COLOR_PAIR(COL_POPUP));

        mvaddnstr(y + 2, x + 2, prompt, w - 4);
        input_draw(&state, y + 3, x + 2, w - 4, 1);
        standend(); /* Clear attributes after input_draw */

        attroff(COLOR_PAIR(COL_POPUP));

        input_move_cursor(&state, y + 3, x + 2, w - 4);
        refresh();

        ch = wrapper_getch();

        if (ch == 27)
        {
            curs_set(0);
            if (saved)
            {
                copywin(saved, stdscr, 0, 0, y, x, h - 1, w - 1, 0);
                delwin(saved);
                refresh();
            }

            return -1;
        }

        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER)
        {
            curs_set(0);
            if (saved)
            {
                copywin(saved, stdscr, 0, 0, y, x, h - 1, w - 1, 0);
                delwin(saved);
                refresh();
            }

            return 0;
        }

        if (input_handle_key(&state, ch) == 1)
            continue;
    }
}

/* Wide-char version of input widget */
int input_handle_key(InputState *state, int ch)
{
    if (!state || !state->buf)
        return -1;

    if (ch == KEY_LEFT)
    {
        if (state->cursor > 0)
            state->cursor--;
    }
    else if (ch == KEY_RIGHT)
    {
        if (state->cursor < state->len)
            state->cursor++;
    }
    else if (ch == KEY_HOME || ch == CTRL('B'))
    {
        state->cursor = 0;
    }
    else if (ch == KEY_END || ch == CTRL('E'))
    {
        state->cursor = state->len;
    }
    else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8)
    {
        if (state->cursor > 0)
        {
            wmemmove(&state->buf[state->cursor - 1], &state->buf[state->cursor], (size_t)(state->len - state->cursor + 1));

            state->cursor--;
            state->len--;
            state->buf[state->len] = L'\0';
        }
    }
    else if (ch == KEY_DC)
    {
        if (state->cursor < state->len)
        {
            wmemmove(&state->buf[state->cursor], &state->buf[state->cursor + 1], (size_t)(state->len - state->cursor));

            state->len--;
            state->buf[state->len] = L'\0';
        }
    }
    else if (ch >= 0x20 && ch < 127 && state->len + 1 < state->bufsz)
    {
        wmemmove(&state->buf[state->cursor + 1], &state->buf[state->cursor], (size_t)(state->len - state->cursor + 1));

        state->buf[state->cursor++] = (wchar_t)ch;
        state->len++;
        state->buf[state->len] = L'\0';
    }
    else
    {
        return 0;
    }

    return 1;
}

void input_draw(InputState *state, int y, int x, int width, int is_active)
{
    int avail = width - 2;
    int show_off = 0, used, i;

    if (!state || !state->buf)
        return;

    /* Set background color for the input field */
    if (is_active)
        attron(COLOR_PAIR(COL_POPUP_SEL));
    else
        attron(COLOR_PAIR(COL_POPUP));

    mvaddch(y, x, '[');

    /* Calculate scroll offset - show cursor if it's beyond visible area */
    if (state->len <= avail)
    {
        show_off = 0;
    }
    else if (state->cursor >= avail)
    {
        show_off = state->cursor - avail + 1;
    }
    else
    {
        show_off = (state->len > avail) ? (state->len - avail) : 0;

        if (state->cursor < show_off)
            show_off = state->cursor;
    }

    mvaddnwstr(y, x + 1, state->buf + show_off, avail);
    used = state->len - show_off;

    for (i = used; i < avail; i++)
        mvaddch(y, x + 1 + i, ' ');

    mvaddch(y, x + 1 + avail, ']');

    if (is_active)
        attroff(COLOR_PAIR(COL_POPUP_SEL));
    else
        attroff(COLOR_PAIR(COL_POPUP));
}

void input_move_cursor(InputState *state, int y, int x, int width)
{
    int avail = width - 2;
    int show_off = 0;

    if (!state || !state->buf)
        return;

    /* Calculate scroll offset - same logic as input_draw */
    if (state->len <= avail)
    {
        show_off = 0;
    }
    else if (state->cursor >= avail)
    {
        show_off = state->cursor - avail + 1;
    }
    else
    {
        show_off = (state->len > avail) ? (state->len - avail) : 0;

        if (state->cursor < show_off)
            show_off = state->cursor;
    }

    move(y, x + 1 + (state->cursor - show_off));
}

/* Charset picker */
int ui_popup_charset(const char *title, const char *cur, char *out, int outsz)
{
    const char **names;
    int count, initial = 0, i, choice;

    names = charset_get_list(&count);

    if (!names || count == 0)
        return -1;

    if (cur && cur[0])
    {
        for (i = 0; i < count; i++)
        {
            if (strcasecmp(names[i], cur) == 0)
            {
                initial = i;
                break;
            }
        }
    }

    choice = ui_popup_list(title ? title : "Charset", names, count, initial);

    if (choice < 0)
        return -1;

    if (out && outsz > 0)
    {
        strncpy(out, names[choice], (size_t)(outsz - 1));
        out[outsz - 1] = '\0';
    }

    return 0;
}

/* Two-field charset picker: View (display/decode) and Save (encode on write) */
int ui_popup_charset_pair(const char *view_in, const char *save_in, char *view_out, int view_outsz, char *save_out, int save_outsz)
{
    const char **charset_names;
    int charset_count, n_opts;
    int row = 0; /* 0=view 1=save */
    int sel[2];
    int y, x, h, w;
    int i, ch;

    charset_names = charset_get_list(&charset_count);

    if (!charset_names || charset_count == 0)
        return -1;

    n_opts = charset_count; /* no Auto mode */

    for (i = 0; i < 2; i++)
    {
        const char *cur = (i == 0) ? view_in : save_in;
        int j;
        sel[i] = 0; /* default: first charset */

        if (cur && cur[0])
        {
            for (j = 0; j < charset_count; j++)
            {
                if (strcasecmp(charset_names[j], cur) == 0)
                {
                    sel[i] = j;
                    break;
                }
            }
        }
    }

    ui_popup_center(8, 48, &y, &x, &h, &w);

    for (;;)
    {
        const char *labels[2] = {"View charset:  ", "Save charset:  "};
        const char *cur_label = "Up/Down: field  Left/Right: change  Enter: ok";

        standend(); /* Clear any residual attributes from previous iteration */
        ui_draw_popup_frame(y, x, h, w, "Charsets");
        attron(COLOR_PAIR(COL_POPUP));

        for (i = 0; i < 2; i++)
        {
            int s = sel[i];
            const char *opt = charset_names[s];

            move(y + 2 + i, x + 2);
            addstr(labels[i]);

            if (i == row)
            {
                int used;

                attron(COLOR_PAIR(COL_POPUP_SEL));
                addstr("[ ");
                addstr(opt);
                addstr(" ]");

                used = (int)strlen(labels[i]) + 4 + (int)strlen(opt);

                while (used < w - 4)
                {
                    addch(' ');
                    used++;
                }

                attroff(COLOR_PAIR(COL_POPUP_SEL));
                attron(COLOR_PAIR(COL_POPUP));
            }
            else
            {
                addstr("  ");
                addstr(opt);
                addstr("  ");
            }
        }

        move(y + h - 2, x + 2);

        addnstr(cur_label, w - 4);
        attroff(COLOR_PAIR(COL_POPUP));
        refresh();

        ch = wrapper_getch();

        if (ch == 27)
            return -1;

        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER)
            break;

        if (ch == KEY_UP)
            row = (row == 0) ? 1 : 0;
        else if (ch == KEY_DOWN || ch == '\t')
            row = (row + 1) & 1;
        else if (ch == KEY_LEFT)
            sel[row] = (sel[row] - 1 + n_opts) % n_opts;
        else if (ch == KEY_RIGHT || ch == ' ')
            sel[row] = (sel[row] + 1) % n_opts;
    }

    if (view_out && view_outsz > 0)
    {
        strncpy(view_out, charset_names[sel[0]], (size_t)(view_outsz - 1));
        view_out[view_outsz - 1] = '\0';
    }
    if (save_out && save_outsz > 0)
    {
        strncpy(save_out, charset_names[sel[1]], (size_t)(save_outsz - 1));
        save_out[save_outsz - 1] = '\0';
    }

    return 0;
}

/* Search results popup */
int ui_popup_search_results(const char *title, const int *line_nums, const char **contexts, int count, int initial)
{
    int y, x, h, w;
    int want_h, want_w, max_ctx, i;
    int sel, top;
    wint_t wch;
    int rc;

    if (!line_nums || !contexts || count <= 0)
        return -1;

    max_ctx = 0;

    for (i = 0; i < count; i++)
    {
        int l = contexts[i] ? (int)strlen(contexts[i]) : 0;

        if (l > max_ctx)
            max_ctx = l;
    }

    want_h = count + 4;

    if (want_h > LINES - 2)
        want_h = LINES - 2;

    want_w = max_ctx + 16;

    if (want_w < 40)
        want_w = 40;

    if (want_w > COLS)
        want_w = COLS;

    ui_popup_center(want_h, want_w, &y, &x, &h, &w);

    sel = (initial >= 0 && initial < count) ? initial : 0;
    top = 0;

    if (sel >= h - 4)
        top = sel - (h - 4) + 1;

    for (;;)
    {
        int visible = h - 4;
        int row;
        int j;

        standend(); /* Clear any residual attributes from previous iteration */
        ui_draw_popup_frame(y, x, h, w, title ? title : "Search Results");
        attron(COLOR_PAIR(COL_POPUP));

        for (row = 0; row < visible && top + row < count; row++)
        {
            int idx = top + row;
            char line_buf[256];

            snprintf(line_buf, sizeof(line_buf), "L%4d  %s", line_nums[idx], contexts[idx] ? contexts[idx] : "");

            move(y + 2 + row, x + 2);

            if (idx == sel)
                attron(COLOR_PAIR(COL_POPUP_SEL));

            for (j = 0; j < w - 4; j++)
                addch(' ');

            mvaddnstr(y + 2 + row, x + 3, line_buf, w - 6);

            if (idx == sel)
                attroff(COLOR_PAIR(COL_POPUP_SEL));
        }

        if (count > h - 4)
        {
            char info[32];

            snprintf(info, sizeof(info), "%d/%d", sel + 1, count);
            mvaddnstr(y + h - 2, x + w - (int)strlen(info) - 2, info, (int)strlen(info));
        }

        /* Status bar */
        attron(COLOR_PAIR(COL_STATUS));

        for (j = 0; j < w - 4; j++)
            mvaddch(y + h - 2, x + 2 + j, ' ');

        mvaddnstr(y + h - 2, x + 2, "Enter=Jump  ESC=Cancel", w - 4);

        attroff(COLOR_PAIR(COL_STATUS));
        refresh();

        rc = wrapper_read_key(&wch);

        if (rc == ERR)
            continue;

        if (wch == 27)
            return -1;

        if (wch == L'\n' || wch == L'\r' || (rc == KEY_CODE_YES && wch == KEY_ENTER))
            return sel;

        if ((rc == KEY_CODE_YES && wch == KEY_UP) || wch == 'k')
        {
            if (sel > 0)
                sel--;

            if (sel < top)
                top = sel;
        }
        else if ((rc == KEY_CODE_YES && wch == KEY_DOWN) || wch == 'j')
        {
            if (sel < count - 1)
                sel++;

            if (sel >= top + (h - 4))
                top = sel - (h - 4) + 1;
        }
        else if (rc == KEY_CODE_YES && wch == KEY_PPAGE || wch == CTRL('U'))
        {
            sel -= h - 4;

            if (sel < 0)
                sel = 0;

            top = sel;
        }
        else if (rc == KEY_CODE_YES && wch == KEY_NPAGE || wch == CTRL('D'))
        {
            sel += h - 4;

            if (sel >= count)
                sel = count - 1;

            if (sel >= top + (h - 4))
                top = sel - (h - 4) + 1;
        }
        else if (rc == KEY_CODE_YES && wch == KEY_HOME || wch == CTRL('B'))
        {
            sel = 0;
            top = 0;
        }
        else if (rc == KEY_CODE_YES && wch == KEY_END || wch == CTRL('E'))
        {
            sel = count - 1;
            top = sel - (h - 4) + 1;

            if (top < 0)
                top = 0;
        }
    }
}

/* Help popup */
void ui_popup_help(const char *title, const char *const *lines, int n)
{
    int y, x, h, w;
    int want_h, want_w, max_len, i;
    int top = 0;
    wint_t wch;
    int rc;

    max_len = 0;

    for (i = 0; i < n; i++)
    {
        int l = lines[i] ? (int)strlen(lines[i]) : 0;

        if (l > max_len)
            max_len = l;
    }

    want_h = n + 4;

    if (want_h > LINES - 2)
        want_h = LINES - 2;

    want_w = max_len + 6;

    if (want_w < 40)
        want_w = 40;

    if (want_w > COLS)
        want_w = COLS;

    ui_popup_center(want_h, want_w, &y, &x, &h, &w);

    for (;;)
    {
        int visible = h - 4;
        int row;

        standend(); /* Clear any residual attributes from previous iteration */
        ui_draw_popup_frame(y, x, h, w, title ? title : "Help");
        attron(COLOR_PAIR(COL_POPUP));

        for (row = 0; row < visible && top + row < n; row++)
            mvaddnstr(y + 2 + row, x + 2, lines[top + row] ? lines[top + row] : "", w - 4);

        if (n > visible)
        {
            char info[32];
            snprintf(info, sizeof(info), "%d/%d", top + 1, n);
            mvaddnstr(y + h - 2, x + w - (int)strlen(info) - 2, info, (int)strlen(info));
        }

        /* Status bar */
        attron(COLOR_PAIR(COL_STATUS));

        for (i = 0; i < w - 4; i++)
            mvaddch(y + h - 2, x + 2 + i, ' ');

        mvaddnstr(y + h - 2, x + 2, "Up/Dn/PgUp/PgDn  ESC=Close", w - 4);

        attroff(COLOR_PAIR(COL_STATUS));
        refresh();

        rc = wrapper_read_key(&wch);

        if (rc == ERR)
            continue;

        if (wch == 27 || wch == 'q' || (rc == KEY_CODE_YES && (wch == KEY_F(1) || wch == KEY_ENTER)))
            break;

        if ((rc == KEY_CODE_YES && wch == KEY_UP) || wch == 'k')
        {
            if (top > 0)
                top--;
        }
        else if ((rc == KEY_CODE_YES && wch == KEY_DOWN) || wch == 'j')
        {
            if (top + visible < n)
                top++;
        }
        else if (rc == KEY_CODE_YES && wch == KEY_PPAGE || wch == CTRL('U'))
        {
            top -= visible;
            if (top < 0)
                top = 0;
        }
        else if (rc == KEY_CODE_YES && wch == KEY_NPAGE || wch == CTRL('D'))
        {
            top += visible;

            if (top + visible >= n)
                top = n - visible;

            if (top < 0)
                top = 0;
        }
    }
}

/* Find & replace popup with multiple fields */
int ui_popup_replace(const wchar_t *search_in, const wchar_t *replace_in, wchar_t *search_out, int search_outsz, wchar_t *replace_out, int replace_outsz, int *case_sensitive, int *whole_word)
{
    int y, x, h, w;
    int field = 0; /* 0=search, 1=replace, 2=case, 3=whole */
    int ch;
    wchar_t search_buf[64], replace_buf[64];
    InputState search_state, replace_state;
    int case_flag = case_sensitive ? *case_sensitive : 0;
    int whole_flag = whole_word ? *whole_word : 0;

    /* Initialize buffers */
    wcsncpy(search_buf, search_in, 63);
    search_buf[63] = L'\0';

    wcsncpy(replace_buf, replace_in, 63);
    replace_buf[63] = L'\0';

    /* Init search state */
    search_state.buf = search_buf;
    search_state.bufsz = 64;
    search_state.cursor = (int)wcslen(search_buf);
    search_state.len = search_state.cursor;

    /* Init replace state */
    replace_state.buf = replace_buf;
    replace_state.bufsz = 64;
    replace_state.cursor = (int)wcslen(replace_buf);
    replace_state.len = replace_state.cursor;

    ui_popup_center(8, 70, &y, &x, &h, &w);

    curs_set(1);

    for (;;)
    {
        const char *labels[4] = {"Search:      ", "Replace with: ", "Case sens:    ", "Whole word:   "};
        int field_start = 15;
        int i;

        ui_draw_popup_frame(y, x, h, w, "Find & Replace");
        attron(COLOR_PAIR(COL_POPUP));

        /* Draw search field */
        /* Clear the search field row with popup background */
        attron(COLOR_PAIR(COL_POPUP));

        for (i = 0; i < w - 4; i++)
            mvaddch(y + 2, x + 2 + i, ' ');

        if (field == 0)
            attron(COLOR_PAIR(COL_POPUP_SEL));

        mvaddnstr(y + 2, x + 2, labels[0], 14);

        if (field == 0)
            attroff(COLOR_PAIR(COL_POPUP_SEL));

        input_draw(&search_state, y + 2, x + field_start, w - field_start - 3, field == 0);
        standend(); /* Clear attributes after input_draw */

        /* Draw replace field */
        /* Clear the replace field row with popup background */
        attron(COLOR_PAIR(COL_POPUP));

        for (i = 0; i < w - 4; i++)
            mvaddch(y + 3, x + 2 + i, ' ');

        if (field == 1)
            attron(COLOR_PAIR(COL_POPUP_SEL));

        mvaddnstr(y + 3, x + 2, labels[1], 14);

        if (field == 1)
            attroff(COLOR_PAIR(COL_POPUP_SEL));

        input_draw(&replace_state, y + 3, x + field_start, w - field_start - 3, field == 1);
        standend(); /* Clear attributes after input_draw */

        /* Draw checkboxes */
        /* Clear the case sens row with popup background */
        attron(COLOR_PAIR(COL_POPUP));

        for (i = 0; i < w - 4; i++)
            mvaddch(y + 4, x + 2 + i, ' ');

        mvaddnstr(y + 4, x + 2, labels[2], 14);

        if (field == 2)
            attron(COLOR_PAIR(COL_POPUP_SEL));

        mvaddnstr(y + 4, x + field_start, case_flag ? "<Yes>" : "<No>", -1);

        if (field == 2)
            attroff(COLOR_PAIR(COL_POPUP_SEL));

        standend(); /* Clear attributes after checkbox */

        /* Clear the whole word row with popup background */
        attron(COLOR_PAIR(COL_POPUP));

        for (i = 0; i < w - 4; i++)
            mvaddch(y + 5, x + 2 + i, ' ');

        mvaddnstr(y + 5, x + 2, labels[3], 14);

        if (field == 3)
            attron(COLOR_PAIR(COL_POPUP_SEL));

        mvaddnstr(y + 5, x + field_start, whole_flag ? "<Yes>" : "<No>", -1);

        if (field == 3)
            attroff(COLOR_PAIR(COL_POPUP_SEL));

        standend(); /* Clear attributes after checkbox */

        /* Status bar */
        attron(COLOR_PAIR(COL_STATUS));

        for (i = 0; i < w - 4; i++)
            mvaddch(y + h - 2, x + 2 + i, ' ');

        mvaddnstr(y + h - 2, x + 2, "TAB: field  Enter=OK  ESC=Cancel", w - 4);

        attroff(COLOR_PAIR(COL_STATUS));

        /* Position cursor */
        if (field == 0)
            input_move_cursor(&search_state, y + 2, x + field_start, w - field_start - 3);
        else if (field == 1)
            input_move_cursor(&replace_state, y + 3, x + field_start, w - field_start - 3);
        else if (field == 2)
            move(y + 4, x + field_start);
        else if (field == 3)
            move(y + 5, x + field_start);

        refresh();

        ch = getch();

        if (ch == 27)
        {
            standend();
            curs_set(0);
            refresh();
            return -1;
        }

        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER)
        {
            standend();
            curs_set(0);
            refresh();

            /* Copy results */
            wcsncpy(search_out, search_buf, search_outsz - 1);
            search_out[search_outsz - 1] = L'\0';

            wcsncpy(replace_out, replace_buf, replace_outsz - 1);
            replace_out[replace_outsz - 1] = L'\0';

            /* Copy checkbox states */
            if (case_sensitive)
                *case_sensitive = case_flag;

            if (whole_word)
                *whole_word = whole_flag;

            return 0;
        }

        if (ch == '\t' || ch == KEY_DOWN)
        {
            field = (field + 1) % 4;
            continue;
        }

        if (ch == KEY_UP)
        {
            field = (field - 1 + 4) % 4;
            continue;
        }

        /* Handle space and arrow keys for checkboxes */
        if ((ch == ' ' || ch == KEY_LEFT || ch == KEY_RIGHT) && (field == 2 || field == 3))
        {
            if (field == 2)
                case_flag = !case_flag;
            else if (field == 3)
                whole_flag = !whole_flag;
            continue;
        }

        /* Handle text input for search/replace fields */
        if (field == 0)
        {
            if (input_handle_key(&search_state, ch) == 1)
                continue;
        }

        if (field == 1)
        {
            if (input_handle_key(&replace_state, ch) == 1)
                continue;
        }
    }
}

/* Show search results popup and return selected line */
int ui_popup_search_results_popup(TeApp *app, const wchar_t *search, int *rows, int *cols, int match_count)
{
    int i;
    const char **ctxs = (const char **)malloc((size_t)match_count * sizeof(char *));
    char **ctxbufs = (char **)malloc((size_t)match_count * sizeof(char *));
    int *lnums = (int *)malloc((size_t)match_count * sizeof(int));
    int choice;

    if (!ctxs || !ctxbufs || !lnums)
    {
        if (ctxs)
            free(ctxs);

        if (ctxbufs)
            free(ctxbufs);

        if (lnums)
            free(lnums);

        return -1;
    }

    /* Build context strings */
    for (i = 0; i < match_count; i++)
    {
        const wchar_t *l = ed_line_wcs(app->editor, rows[i]);
        int ll = ed_line_len(app->editor, rows[i]);
        int clen = ll - cols[i];

        if (clen > 60)
            clen = 60;

        if (clen < 0)
            clen = 0;

        ctxbufs[i] = (char *)malloc(128);

        if (ctxbufs[i])
        {
            if (l && clen > 0)
            {
                char *u8 = wcs_to_utf8(&l[cols[i]], clen);

                if (u8)
                {
                    snprintf(ctxbufs[i], 128, "%s", u8);
                    free(u8);
                }
                else
                    ctxbufs[i][0] = '\0';
            }
            else
                ctxbufs[i][0] = '\0';
        }

        ctxs[i] = ctxbufs[i] ? ctxbufs[i] : "";
        lnums[i] = rows[i] + 1;
    }

    /* Show popup with results */
    choice = ui_popup_search_results("Search Results", lnums, ctxs, match_count, 0);

    /* Free allocated memory */
    for (i = 0; i < match_count; i++)
    {
        if (ctxbufs[i])
            free(ctxbufs[i]);
    }

    free(ctxbufs);
    free(ctxs);
    free(lnums);

    return choice;
}
