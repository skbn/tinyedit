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

/* ncursesw_win32.c - ncursesw for Windows using GDI (own window like Amiga) */

#ifdef PLATFORM_WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>

#include "wrapper.h"
#include "ncursesw_win32.h"
#include "core/utf8.h"
#include "ui/ui_mouse.h"
#include "te_rastport_win32.h"

/* Sentinel value for trailing cells of wide glyphs (same as amiga_te) */
#define WIN32_CELL_WIDE_TRAILING 0x0000FFFEUL

/* Screen dimensions */
int LINES = 25;
int COLS = 80;
int COLOR_PAIRS = 256;
int COLORS = 16;

/* Standard windows */
WINDOW *stdscr = NULL;
WINDOW *curscr = NULL;

/* GDI window handles */
static HWND hWnd = NULL;
static HDC hDC = NULL;
static HDC hMemDC = NULL;
static HBITMAP hBitmap = NULL;
static HFONT hFont = NULL;
static int s_hfont_is_stock = 0;

/* Font metrics */
static int fw = 8, fh = 16, fb = 12;

/* Window position */
static int win_x = 0, win_y = 0;

/* Color pairs table */
static short s_pair_fg[256];
static short s_pair_bg[256];
static int s_pair_ok[256];

/* State flags */
static int s_colors_on = 0;
static int s_echo = 0;
static int s_keypad = 1;
static int s_nodelay = 0;
static int s_cursor_vis = 1;

static int s_tab_size = 4;  /* visual tab stop width, configurable via set_tabsize() */
static int s_ansi_mode = 0; /* ANSI art mode: when ON, do not expand tabs (preserves CP437 art alignment) */

/* Ungetch buffer */
static int s_ungetch = ERR;

/* Non-zero while win32_reinit_window() is destroying/recreating the window */
static int s_reinit_in_progress = 0;

/* Font configuration */
#define WIN_FONT_NAME_MAX 256
static char win_font_name[WIN_FONT_NAME_MAX] = "Consolas";

/* Added TTF files (loaded via AddFontResourceEx) */
#define WIN32_ADDED_FONTS_MAX 16
static char s_added_fonts[WIN32_ADDED_FONTS_MAX][MAX_PATH];
static int s_added_font_count = 0;
static int s_win_font_size = 16;

/* FreeType-based renderer context (when TTF fonts are configured) */
static struct TERenderContext *s_te_dc = NULL;
static int s_use_te = 0;

/* Cursor border color (0..15) */
static int s_cursor_color = COLOR_WHITE;

/* Active fg/bg color indices */
static int s_cur_fg_idx = COLOR_WHITE, s_cur_bg_idx = COLOR_BLACK;

/* Key input buffer */
static int s_key_buf[16];
static int s_key_count = 0;

/* Mouse state */
static int s_left_button_held = 0;
static unsigned long s_mouse_event = 0;
static int s_mouse_event_pending = 0;

/* Pack mouse event: type (8 bits) | x (12 bits) | y (12 bits) */
static unsigned long pack_mouse(UiMouseEventType type, int x, int y)
{
    return ((unsigned long)type & 0xFF) | (((unsigned long)x & 0xFFF) << 8) | (((unsigned long)y & 0xFFF) << 20);
}

/* RGB color map for 16 ncurses colors */
static COLORREF s_rgb_map[16] =
    {
        RGB(10, 10, 10),    /* COLOR_BLACK */
        RGB(128, 0, 0),     /* COLOR_RED */
        RGB(0, 128, 0),     /* COLOR_GREEN */
        RGB(128, 128, 0),   /* COLOR_YELLOW */
        RGB(0, 0, 128),     /* COLOR_BLUE */
        RGB(128, 0, 128),   /* COLOR_MAGENTA */
        RGB(0, 128, 128),   /* COLOR_CYAN */
        RGB(192, 192, 192), /* COLOR_WHITE */
        RGB(128, 128, 128), /* COLOR_BRIGHT_BLACK */
        RGB(255, 0, 0),     /* COLOR_BRIGHT_RED */
        RGB(0, 255, 0),     /* COLOR_BRIGHT_GREEN */
        RGB(255, 255, 0),   /* COLOR_BRIGHT_YELLOW */
        RGB(0, 0, 255),     /* COLOR_BRIGHT_BLUE */
        RGB(255, 0, 255),   /* COLOR_BRIGHT_MAGENTA */
        RGB(0, 255, 255),   /* COLOR_BRIGHT_CYAN */
        RGB(255, 255, 255)  /* COLOR_BRIGHT_WHITE */
};

#define CELL(win, r, c) (&(win)->cells[(r) * (win)->_maxx + (c)])

/* Forward declarations */
static void render_all(void);

static int is_wide_cp(unsigned int cp)
{
    /* ANSI-art view is a strict CP437 1-cell grid: never widen. */
    if (s_ansi_mode)
        return 0;

    if (cp < 0x1100)
        return 0; /* ASCII, Latin, CP437 low glyphs: 1 cell */

    if (cp >= 0x1100 && cp <= 0x115F)
        return 1; /* Hangul Jamo */

    if (cp >= 0x2190 && cp <= 0x2BFF)
        return 0;

    if (cp >= 0x2E80 && cp <= 0x303E)
        return 1; /* CJK Radicals, Kangxi, CJK Symbols */

    if (cp >= 0x3041 && cp <= 0x33FF)
        return 1; /* Hiragana, Katakana, Bopomofo, CJK compat */

    if (cp >= 0x3400 && cp <= 0x4DBF)
        return 1; /* CJK Ext A */

    if (cp >= 0x4E00 && cp <= 0x9FFF)
        return 1; /* CJK Unified Ideographs */

    if (cp >= 0xA000 && cp <= 0xA4CF)
        return 1; /* Yi Syllables */

    if (cp >= 0xAC00 && cp <= 0xD7A3)
        return 1; /* Hangul Syllables */

    if (cp >= 0xF900 && cp <= 0xFAFF)
        return 1; /* CJK Compatibility Ideographs */

    if (cp >= 0xFE30 && cp <= 0xFE4F)
        return 1; /* CJK Compatibility Forms */

    if (cp >= 0xFF00 && cp <= 0xFF60)
        return 1; /* Fullwidth Forms */

    if (cp >= 0xFFE0 && cp <= 0xFFE6)
        return 1; /* Fullwidth signs */

    if (cp >= 0x1F300 && cp <= 0x1FAFF)
        return 1; /* Emoji and pictographs */

    if (cp >= 0x20000 && cp <= 0x3FFFD)
        return 1; /* CJK Ext B..G (supplementary ideographs) */

    return 0;
}

/* Encode codepoint to UTF-16; returns 1 for BMP, 2 for surrogate pair (buf needs 2 wchar_t) */
static int cp_to_utf16(unsigned int cp, wchar_t *buf)
{
    if (cp < 0x10000)
    {
        buf[0] = (wchar_t)cp;
        return 1;
    }

    /* Surrogate pair for codepoints >= U+10000 */
    cp -= 0x10000;

    buf[0] = (wchar_t)(0xD800 + (cp >> 10));
    buf[1] = (wchar_t)(0xDC00 + (cp & 0x3FF));

    return 2;
}

/* Draw a single codepoint using the FreeType renderer */
static void draw_glyph_te(int cx, int cy, int cell_w, unsigned int cp)
{
    char utf8[8];
    int len = 0;
    struct TEGlyphMetrics metrics;
    int x;

    if (!s_te_dc)
    {
        wchar_t wbuf[2];
        RECT clip;
        INT dx[2];
        int origin_x;
        int wlen;
        ABC abc;

        wlen = cp_to_utf16(cp, wbuf);
        origin_x = cx;

        clip.left = cx;
        clip.top = cy;
        clip.right = cx + cell_w;
        clip.bottom = cy + fh;

        if (wlen == 1 && GetCharABCWidthsW(hMemDC, (UINT)wbuf[0], (UINT)wbuf[0], &abc) && abc.abcA < 0)
            origin_x = cx - abc.abcA;

        dx[0] = cell_w;
        dx[1] = 0;

        ExtTextOutW(hMemDC, origin_x, cy, ETO_CLIPPED, &clip, wbuf, wlen, dx);
        return;
    }

    /* Encode codepoint to UTF-8 */
    if (cp < 0x80)
    {
        utf8[0] = (char)cp;
        len = 1;
    }
    else if (cp < 0x800)
    {
        utf8[0] = (char)(0xC0 | (cp >> 6));
        utf8[1] = (char)(0x80 | (cp & 0x3F));
        len = 2;
    }
    else if (cp < 0x10000)
    {
        utf8[0] = (char)(0xE0 | (cp >> 12));
        utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (cp & 0x3F));
        len = 3;
    }
    else
    {
        utf8[0] = (char)(0xF0 | (cp >> 18));
        utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (cp & 0x3F));
        len = 4;
    }

    utf8[len] = '\0';

    /* Center only real wide glyphs; narrow glyphs (e.g. Box Drawing) stay left-aligned */
    x = cx;
    TE_MeasureText(s_te_dc, utf8, 1, &metrics);

    if (s_te_dc && !s_ansi_mode)
    {
        struct TEGlyphMetrics cell_metrics;
        int cell_width = cell_w;

        TE_GetMetrics(s_te_dc, &cell_metrics);

        if (cell_metrics.width > 0)
            cell_width = cell_metrics.width;

        if (metrics.width > cell_width && cell_w > cell_width && metrics.width < cell_w)
            x = cx + (cell_w - metrics.width) / 2;
    }

    TE_RenderText(s_te_dc, hMemDC, x, cy + fb, utf8, 1);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT ps;
    HDC hdc;

    switch (msg)
    {
    case WM_PAINT:
        hdc = BeginPaint(hwnd, &ps);

        if (hMemDC && hBitmap)
        {
            BitBlt(hdc, 0, 0, COLS * fw, LINES * fh, hMemDC, 0, 0, SRCCOPY);
        }

        EndPaint(hwnd, &ps);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        /* Store special/control keys; printable chars come via WM_CHAR */
        if (s_key_count < 16)
        {
            int key = (int)wParam;
            int is_control_key = 0;

            /* Alt+letter -> KEY_ALT; Shift+Alt+letter -> KEY_SHIFT; Ctrl+Alt+letter -> KEY_ALT_CTRL */
            if ((GetKeyState(VK_MENU) & 0x8000) && ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z')))
            {
                int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

                /* normalize to uppercase */
                if (key >= 'a' && key <= 'z')
                    key = key - 'a' + 'A';

                if (ctrl)
                    key = KEY_ALT_CTRL(key);
                else if (shift)
                    key = KEY_SHIFT(key);
                else
                    key = KEY_ALT(key);

                is_control_key = 1;
            }
            else
            {
                /* map arrow and function keys */
                switch (key)
                {
                case VK_UP:
                {
                    int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    int alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

                    if (alt)
                        key = KEY_ALT_UP;
                    else if (ctrl && shift)
                        key = KEY_CSUP;
                    else if (ctrl)
                        key = KEY_CUP;
                    else if (shift)
                        key = KEY_SUP;
                    else
                        key = KEY_UP;

                    is_control_key = 1;
                    break;
                }
                case VK_DOWN:
                {
                    int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    int alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

                    if (alt)
                        key = KEY_ALT_DOWN;
                    else if (ctrl && shift)
                        key = KEY_CSDOWN;
                    else if (ctrl)
                        key = KEY_CDOWN;
                    else if (shift)
                        key = KEY_SDOWN;
                    else
                        key = KEY_DOWN;

                    is_control_key = 1;
                    break;
                }
                case VK_LEFT:
                {
                    int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    int alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

                    if (alt)
                        key = KEY_ALT_LEFT;
                    else if (ctrl && shift)
                        key = KEY_CSLEFT;
                    else if (ctrl)
                        key = KEY_CLEFT;
                    else if (shift)
                        key = KEY_SLEFT;
                    else
                        key = KEY_LEFT;

                    is_control_key = 1;
                    break;
                }
                case VK_RIGHT:
                {
                    int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    int alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

                    if (alt)
                        key = KEY_ALT_RIGHT;
                    else if (ctrl && shift)
                        key = KEY_CSRIGHT;
                    else if (ctrl)
                        key = KEY_CRIGHT;
                    else if (shift)
                        key = KEY_SRIGHT;
                    else
                        key = KEY_RIGHT;

                    is_control_key = 1;
                    break;
                }
                case VK_HOME:
                {
                    int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                    if (ctrl && shift)
                        key = KEY_CSHOME;
                    else if (shift)
                        key = KEY_SHOME;
                    else
                        key = KEY_HOME;

                    is_control_key = 1;
                    break;
                }
                case VK_END:
                {
                    int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                    if (ctrl && shift)
                        key = KEY_CSEND;
                    else if (shift)
                        key = KEY_SEND;
                    else
                        key = KEY_END;

                    is_control_key = 1;
                    break;
                }
                case VK_PRIOR:
                {
                    int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                    /* tinyedit treats KEY_SPPAGE same as KEY_SPREVIOUS */
                    if (shift)
                        key = KEY_SPPAGE;
                    else
                        key = KEY_PPAGE;

                    is_control_key = 1;
                    break;
                }
                case VK_NEXT:
                {
                    int shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                    if (shift)
                        key = KEY_SNPAGE;
                    else
                        key = KEY_NPAGE;
                    is_control_key = 1;

                    break;
                }
                case VK_INSERT:
                    key = KEY_IC;
                    is_control_key = 1;

                    /* Shift+Insert = paste */
                    if (GetKeyState(VK_SHIFT) & 0x8000)
                        key = KEY_SIC; /* Shift+Insert */

                    break;
                case VK_DELETE:
                    key = KEY_DC;
                    is_control_key = 1;
                    break;
                case VK_F1:
                    key = KEY_F(1);
                    is_control_key = 1;
                    break;
                case VK_F2:
                    key = KEY_F(2);
                    is_control_key = 1;
                    break;
                case VK_F3:
                    key = KEY_F(3);
                    is_control_key = 1;
                    break;
                case VK_F4:
                    key = KEY_F(4);
                    is_control_key = 1;
                    break;
                case VK_F5:
                    key = KEY_F(5);
                    is_control_key = 1;
                    break;
                case VK_F6:
                    key = KEY_F(6);
                    is_control_key = 1;
                    break;
                case VK_F7:
                    key = KEY_F(7);
                    is_control_key = 1;
                    break;
                case VK_F8:
                    key = KEY_F(8);
                    is_control_key = 1;
                    break;
                case VK_F9:
                    key = KEY_F(9);
                    is_control_key = 1;
                    break;
                case VK_F10:
                    key = KEY_F(10);
                    is_control_key = 1;
                    break;
                case VK_F11:
                    key = KEY_F(11);
                    is_control_key = 1;
                    break;
                case VK_F12:
                    key = KEY_F(12);
                    is_control_key = 1;
                    break;
                case VK_ESCAPE:
                    is_control_key = 1;
                    break;
                case VK_RETURN:
                    is_control_key = 1;
                    break;
                case VK_TAB:
                    is_control_key = 1;
                    break;
                case VK_BACK:
                    key = KEY_BACKSPACE;
                    is_control_key = 1;
                    break;
                }
            }

            if (is_control_key)
                s_key_buf[s_key_count++] = key;
        }

        return 0;

    case WM_CHAR:
        /* Printable character input */
        if (s_key_count < 16)
        {
            int ch = (int)wParam;

            /* skip chars already handled as control keys */
            if (ch == '\r' || ch == '\n' || ch == '\t' || ch == '\b' || ch == 27)
                return 0;

            /* Ctrl+letter stays as raw control char (1-26) */
            s_key_buf[s_key_count++] = ch;
        }

        return 0;

    case WM_SIZE:
    {
        int new_w = LOWORD(lParam);
        int new_h = HIWORD(lParam);
        int new_cols, new_lines;
        int old_lines, old_cols, r, c;
        Cell *new_cells, *old_cells;
        RECT rect;
        HBRUSH hBrush;

        /* Ignore minimize */
        if (wParam == SIZE_MINIMIZED)
            return 0;

        new_cols = new_w / fw;
        new_lines = new_h / fh;

        if (new_cols < 20)
            new_cols = 20;

        if (new_lines < 5)
            new_lines = 5;

        if (!stdscr || !stdscr->cells || (new_cols == COLS && new_lines == LINES))
            return 0;

        old_lines = LINES;
        old_cols = COLS;
        old_cells = stdscr->cells;

        /* Allocate new cell buffer */
        new_cells = (Cell *)malloc(sizeof(Cell) * new_lines * new_cols);

        /* Keep old buffer on failure */
        if (!new_cells)
            return 0;

        /* Copy old content, fill new area with spaces */
        for (r = 0; r < new_lines; r++)
        {
            for (c = 0; c < new_cols; c++)
            {
                if (r < old_lines && c < old_cols)
                {
                    new_cells[r * new_cols + c] = old_cells[r * old_cols + c];
                }
                else
                {
                    new_cells[r * new_cols + c].ch = ' ';
                    new_cells[r * new_cols + c].attrs = stdscr->attrs;
                }
            }
        }

        stdscr->cells = new_cells;
        stdscr->_maxy = new_lines;
        stdscr->_maxx = new_cols;
        LINES = new_lines;
        COLS = new_cols;

        /* Recreate backing bitmap */
        if (hBitmap)
            DeleteObject(hBitmap);

        hBitmap = CreateCompatibleBitmap(hDC, COLS * fw, LINES * fh);

        if (!hBitmap)
        {
            /* Restore on failure */
            free(new_cells);

            stdscr->cells = old_cells;
            stdscr->_maxy = old_lines;
            stdscr->_maxx = old_cols;
            LINES = old_lines;
            COLS = old_cols;
            return 0;
        }

        /* Free old buffer only after bitmap succeeds */
        free(old_cells);
        SelectObject(hMemDC, hBitmap);

        /* clear, redraw, blit */
        rect.left = 0;
        rect.top = 0;
        rect.right = COLS * fw;
        rect.bottom = LINES * fh;
        hBrush = CreateSolidBrush(RGB(0, 0, 0));

        FillRect(hMemDC, &rect, hBrush);
        DeleteObject(hBrush);

        render_all();
        hdc = GetDC(hWnd);

        if (hdc && hMemDC && hBitmap)
        {
            BitBlt(hdc, 0, 0, COLS * fw, LINES * fh, hMemDC, 0, 0, SRCCOPY);
            ReleaseDC(hWnd, hdc);
        }

        /* Notify application of resize */
        if (s_key_count < 16)
            s_key_buf[s_key_count++] = KEY_RESIZE;
    }
        return 0;

    case WM_CLOSE:
        /* Send ESC so the app can confirm quit */
        if (s_key_count < 16)
            s_key_buf[s_key_count++] = 27;

        return 0;

    case WM_LBUTTONDOWN:
    {
        int mouse_x = LOWORD(lParam) / fw;
        int mouse_y = HIWORD(lParam) / fh;

        s_left_button_held = 1;

        ui_mouse_set_event_time_ms((unsigned long)GetTickCount());

        s_mouse_event = pack_mouse(UI_MOUSE_PRESS_LEFT, mouse_x, mouse_y);
        s_mouse_event_pending = 1;

        if (s_key_count < 16)
            s_key_buf[s_key_count++] = KEY_MOUSE;

        return 0;
    }
    case WM_LBUTTONUP:
    {
        int mouse_x = LOWORD(lParam) / fw;
        int mouse_y = HIWORD(lParam) / fh;

        s_left_button_held = 0;

        s_mouse_event = pack_mouse(UI_MOUSE_RELEASE_LEFT, mouse_x, mouse_y);
        s_mouse_event_pending = 1;

        if (s_key_count < 16)
            s_key_buf[s_key_count++] = KEY_MOUSE;

        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (s_left_button_held)
        {
            int mouse_x = LOWORD(lParam) / fw;
            int mouse_y = HIWORD(lParam) / fh;

            s_mouse_event = pack_mouse(UI_MOUSE_DRAG_LEFT, mouse_x, mouse_y);
            s_mouse_event_pending = 1;

            if (s_key_count < 16)
                s_key_buf[s_key_count++] = KEY_MOUSE;
        }

        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int mouse_x = LOWORD(lParam) / fw;
        int mouse_y = HIWORD(lParam) / fh;
        UiMouseEventType mtype = (delta > 0) ? UI_MOUSE_WHEEL_UP : UI_MOUSE_WHEEL_DOWN;

        s_mouse_event = pack_mouse(mtype, mouse_x, mouse_y);
        s_mouse_event_pending = 1;

        if (s_key_count < 16)
            s_key_buf[s_key_count++] = KEY_MOUSE;

        return 0;
    }
    case WM_DESTROY:
        /* During win32_reinit_window() we destroy the window ourselves */
        if (!s_reinit_in_progress)
            PostQuitMessage(0);

        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* Clear cell buffer */
static void clear_cells(WINDOW *win)
{
    int i, size;
    Cell *cells;

    if (!win || !win->cells)
        return;

    size = win->_maxx * win->_maxy;
    cells = win->cells;

    for (i = 0; i < size; i++)
    {
        cells[i].ch = ' ';
        cells[i].attrs = win->attrs;
        cells[i].full_cp = 0;
    }
}

/* Set fg/bg GDI colors from a color pair and attrs */
static void apply_colors(int pair, attr_t attrs)
{
    int fg_idx, bg_idx;

    if (pair >= 0 && pair < COLOR_PAIRS && s_pair_ok[pair])
    {
        fg_idx = s_pair_fg[pair];
        bg_idx = s_pair_bg[pair];
    }
    else
    {
        fg_idx = COLOR_WHITE;
        bg_idx = COLOR_BLACK;
    }

    if (fg_idx < 0 || fg_idx > 15)
        fg_idx = COLOR_WHITE;

    if (bg_idx < 0 || bg_idx > 15)
        bg_idx = COLOR_BLACK;

    if (attrs & A_REVERSE)
    {
        int tmp = fg_idx;

        fg_idx = bg_idx;
        bg_idx = tmp;
    }

    /* update global indices used by render_cell */
    s_cur_fg_idx = fg_idx;
    s_cur_bg_idx = bg_idx;

    SetTextColor(hMemDC, s_rgb_map[fg_idx]);
    SetBkColor(hMemDC, s_rgb_map[bg_idx]);

    if (s_use_te && s_te_dc)
    {
        TE_SetColorRGB(s_te_dc,
                       (ULONG)((s_rgb_map[fg_idx] >> 16) & 0xFF) |
                           ((ULONG)((s_rgb_map[fg_idx] >> 8) & 0xFF) << 8) |
                           ((ULONG)(s_rgb_map[fg_idx] & 0xFF) << 16),
                       (ULONG)((s_rgb_map[bg_idx] >> 16) & 0xFF) |
                           ((ULONG)((s_rgb_map[bg_idx] >> 8) & 0xFF) << 8) |
                           ((ULONG)(s_rgb_map[bg_idx] & 0xFF) << 16));
    }
}

/* Render a single cell to the memory DC */
static void render_cell(int row, int col, Cell *cell)
{
    RECT rect;
    HBRUSH hBrush;
    int pair;
    int cell_w = fw; /* Width of the area to repaint -- fw or 2*fw */
    unsigned int draw_cp;
    chtype ch;
    attr_t attrs;

    if (!hMemDC || row < 0 || row >= LINES || col < 0 || col >= COLS || !cell)
        return;

    ch = cell->ch;
    attrs = cell->attrs;

    if (ch == WIN32_CELL_WIDE_TRAILING)
    {
        if (!s_ansi_mode)
            return;

        /* ANSI mode: treat as a literal space, fall through to normal paint */
        ch = ' ';
        cell->ch = ' ';
        cell->full_cp = 0;
    }

    /* Use full 32-bit codepoint if available; otherwise fall back to ch */
    draw_cp = cell->full_cp ? (unsigned int)cell->full_cp : (unsigned int)ch;

    if (!s_ansi_mode && stdscr && stdscr->cells && is_wide_cp(draw_cp))
        cell_w = 2 * fw;

    pair = (int)PAIR_NUMBER(attrs);
    apply_colors(pair, attrs);

    /* Clear background over 1 or 2 cells */
    rect.left = col * fw;
    rect.top = row * fh;
    rect.right = rect.left + cell_w;
    rect.bottom = rect.top + fh;

    hBrush = CreateSolidBrush(s_rgb_map[s_cur_bg_idx]);

    FillRect(hMemDC, &rect, hBrush);
    DeleteObject(hBrush);

    /* Render glyph, surrogate-pair aware */
    if (draw_cp >= 0x20)
    {
        if (s_te_dc)
        {
            ULONG te_style = TE_STY_NORMAL;

            if (attrs & A_BOLD)
                te_style |= TE_STY_BOLD;

            if (attrs & A_DIM)
                te_style |= TE_STY_ITALIC;

            TE_SetStyle(s_te_dc, te_style);
        }

        draw_glyph_te(rect.left, rect.top, cell_w, draw_cp);

        if (s_te_dc)
            TE_SetStyle(s_te_dc, TE_STY_NORMAL);
    }

    /* Draw underline if requested */
    if (attrs & A_UNDERLINE)
    {
        int uy = row * fh + fb + 1;
        HPEN hPen;
        HPEN hOldPen;

        if (uy >= (row + 1) * fh)
            uy = (row + 1) * fh - 1;

        hPen = CreatePen(PS_SOLID, 1, s_rgb_map[s_cur_fg_idx]);
        hOldPen = (HPEN)SelectObject(hMemDC, hPen);

        MoveToEx(hMemDC, rect.left, uy, NULL);
        LineTo(hMemDC, rect.right, uy);

        SelectObject(hMemDC, hOldPen);
        DeleteObject(hPen);
    }
}

/* Force complete redraw (call after font change) */
void win32_force_redraw(void)
{
    render_all();
}

/* Set font height used by initscr (6..96). Call before initscr() */
int win32_set_font_size(int size)
{
    if (size >= 6 && size <= 96)
        s_win_font_size = size;

    return 0;
}

/* Read big-endian 16-bit value from buffer */
static unsigned short read_be16(const unsigned char *p)
{
    return (unsigned short)((p[0] << 8) | p[1]);
}

/* Read big-endian 32-bit value from buffer */
static unsigned long read_be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

/* Convert UTF-16BE string to UTF-8. Returns number of bytes written (excluding NUL) */
static int utf16be_to_utf8(const unsigned char *src, int src_len, char *dst, int dst_sz)
{
    int i;
    int dst_pos = 0;
    unsigned short low;

    for (i = 0; i < src_len; i += 2)
    {
        unsigned short wc;
        unsigned long cp;
        int src_adv = 2;

        if (i + 1 >= src_len)
            break;

        wc = (unsigned short)((src[i] << 8) | src[i + 1]);
        low = 0;

        if (wc >= 0xD800 && wc <= 0xDBFF)
        {
            if (i + 3 >= src_len)
                break;

            low = (unsigned short)((src[i + 2] << 8) | src[i + 3]);

            if (low < 0xDC00 || low > 0xDFFF)
                break;

            cp = 0x10000UL + (((unsigned long)(wc - 0xD800)) << 10) + (low - 0xDC00);
            src_adv = 4;
        }
        else if (wc >= 0xDC00 && wc <= 0xDFFF)
        {
            continue;
        }
        else
        {
            cp = wc;
        }

        if (cp < 0x80)
        {
            if (dst_pos + 1 >= dst_sz)
                break;

            dst[dst_pos++] = (char)cp;
        }
        else if (cp < 0x800)
        {
            if (dst_pos + 2 >= dst_sz)
                break;

            dst[dst_pos++] = (char)(0xC0 | (cp >> 6));
            dst[dst_pos++] = (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            if (dst_pos + 3 >= dst_sz)
                break;

            dst[dst_pos++] = (char)(0xE0 | (cp >> 12));
            dst[dst_pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[dst_pos++] = (char)(0x80 | (cp & 0x3F));
        }
        else
        {
            if (dst_pos + 4 >= dst_sz)
                break;

            dst[dst_pos++] = (char)(0xF0 | (cp >> 18));
            dst[dst_pos++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            dst[dst_pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[dst_pos++] = (char)(0x80 | (cp & 0x3F));
        }

        i += src_adv - 2;
    }

    if (dst_pos < dst_sz)
        dst[dst_pos] = '\0';
    else if (dst_sz > 0)
        dst[dst_sz - 1] = '\0';

    return dst_pos;
}

/* Extract the font family name (nameID 1) from a TTF/OTF file */
int win32_get_font_family_name(const char *path, char *out, int out_sz)
{
    FILE *fp = NULL;
    unsigned char header[12];
    unsigned short num_tables;
    unsigned long name_offset = 0;
    unsigned long name_length = 0;
    int i;
    unsigned char *name_table = NULL;
    int found = -1;
    unsigned short count;
    unsigned short string_offset;
    const unsigned char *record = NULL;

    if (!path || !out || out_sz < 1)
        return -1;

    out[0] = '\0';

    fp = fopen(path, "rb");

    if (!fp)
        return -1;

    if (fread(header, 1, sizeof(header), fp) != sizeof(header))
    {
        fclose(fp);
        return -1;
    }

    num_tables = read_be16(header + 4);

    for (i = 0; i < num_tables; i++)
    {
        unsigned char entry[16];
        char tag[5];

        if (fread(entry, 1, sizeof(entry), fp) != sizeof(entry))
            break;

        tag[0] = (char)entry[0];
        tag[1] = (char)entry[1];
        tag[2] = (char)entry[2];
        tag[3] = (char)entry[3];
        tag[4] = '\0';

        if (strcmp(tag, "name") == 0)
        {
            name_offset = read_be32(entry + 8);
            name_length = read_be32(entry + 12);
            break;
        }
    }

    if (name_offset == 0 || name_length == 0)
    {
        fclose(fp);
        return -1;
    }

    if (fseek(fp, (long)name_offset, SEEK_SET) != 0)
    {
        fclose(fp);
        return -1;
    }

    name_table = (unsigned char *)malloc((size_t)name_length);
    if (!name_table)
    {
        fclose(fp);
        return -1;
    }

    if (fread(name_table, 1, (size_t)name_length, fp) != (size_t)name_length)
    {
        free(name_table);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    count = read_be16(name_table + 2);
    string_offset = read_be16(name_table + 4);
    record = name_table + 6;

    for (i = 0; i < count && found < 0; i++)
    {
        unsigned short platform_id = read_be16(record);
        unsigned short encoding_id = read_be16(record + 2);
        unsigned short language_id = read_be16(record + 4);
        unsigned short name_id = read_be16(record + 6);
        unsigned short length = read_be16(record + 8);
        unsigned short offset = read_be16(record + 10);

        if (name_id == 1 && platform_id == 3 && encoding_id == 1 && language_id == 0x0409)
        {
            if ((unsigned long)string_offset + offset + length <= name_length)
            {
                utf16be_to_utf8(name_table + string_offset + offset, length, out, out_sz);
                found = 0;
            }
        }

        record += 12;
    }

    if (found < 0)
    {
        record = name_table + 6;

        for (i = 0; i < count; i++)
        {
            unsigned short platform_id = read_be16(record);
            unsigned short name_id = read_be16(record + 6);
            unsigned short length = read_be16(record + 8);
            unsigned short offset = read_be16(record + 10);

            if (name_id == 1 && platform_id == 3)
            {
                if ((unsigned long)string_offset + offset + length <= name_length)
                {
                    utf16be_to_utf8(name_table + string_offset + offset, length, out, out_sz);
                    found = 0;
                    break;
                }
            }

            record += 12;
        }
    }

    if (found < 0)
    {
        record = name_table + 6;

        for (i = 0; i < count; i++)
        {
            unsigned short platform_id = read_be16(record);
            unsigned short name_id = read_be16(record + 6);
            unsigned short length = read_be16(record + 8);
            unsigned short offset = read_be16(record + 10);

            if (name_id == 1 && platform_id == 1)
            {
                if ((unsigned long)string_offset + offset + length <= name_length)
                {
                    int j;

                    for (j = 0; j < length && j < out_sz - 1; j++)
                        out[j] = (char)name_table[string_offset + offset + j];

                    if (j < out_sz)
                        out[j] = '\0';
                    else if (out_sz > 0)
                        out[out_sz - 1] = '\0';

                    found = 0;
                    break;
                }
            }

            record += 12;
        }
    }

    free(name_table);
    return found;
}

/* Add a TTF/OTF font file to the Windows session. Call before initscr() */
int win32_add_font_file(const char *path)
{
    wchar_t *wpath = NULL;

    if (!path || !path[0])
        return 0;

    if (s_added_font_count >= WIN32_ADDED_FONTS_MAX)
        return -1;

    wpath = utf8_to_wcs(path, NULL);

    if (!wpath)
        return -1;

    AddFontResourceExW(wpath, FR_PRIVATE, 0);
    free(wpath);

    strncpy(s_added_fonts[s_added_font_count], path, MAX_PATH - 1);

    s_added_fonts[s_added_font_count][MAX_PATH - 1] = '\0';
    s_added_font_count++;

    return 0;
}

/* Remove all font files added by win32_add_font_file() */
void win32_clear_font_files(void)
{
    int i;

    if (s_te_dc)
    {
        TE_ContextRelease(s_te_dc);
        s_te_dc = NULL;
    }

    s_use_te = 0;

    for (i = 0; i < s_added_font_count; i++)
    {
        wchar_t *wpath = utf8_to_wcs(s_added_fonts[i], NULL);

        if (wpath)
        {
            RemoveFontResourceExW(wpath, FR_PRIVATE, 0);
            free(wpath);
        }

        s_added_fonts[i][0] = '\0';
    }

    s_added_font_count = 0;
}

/* Render all cells to hMemDC, then blit to window */
static void render_all(void)
{
    int r, c;

    if (!stdscr || !stdscr->cells || !hMemDC)
        return;

    for (r = 0; r < LINES; r++)
    {
        for (c = 0; c < COLS; c++)
        {
            Cell *cell = CELL(stdscr, r, c);
            render_cell(r, c, cell);
        }
    }

    /* Draw cursor as a rectangle border */
    if (s_cursor_vis && stdscr && stdscr->_cury >= 0 && stdscr->_cury < LINES && stdscr->_curx >= 0 && stdscr->_curx < COLS)
    {
        int cy = stdscr->_cury;
        int cx = stdscr->_curx;
        int cx_pixel, cy_pixel;
        int cursor_w = fw;
        int draw_x_cell = cx;
        Cell *cur = CELL(stdscr, cy, cx);
        RECT rc;

        if (cur->ch == WIN32_CELL_WIDE_TRAILING && cx > 0)
        {
            draw_x_cell = cx - 1;
            cursor_w = 2 * fw;
        }
        else if (cx + 1 < COLS)
        {
            Cell *nxt = CELL(stdscr, cy, cx + 1);

            if (nxt->ch == WIN32_CELL_WIDE_TRAILING)
                cursor_w = 2 * fw;
        }

        cx_pixel = draw_x_cell * fw;
        cy_pixel = cy * fh;

        rc.left = cx_pixel;
        rc.top = cy_pixel;
        rc.right = cx_pixel + cursor_w;
        rc.bottom = cy_pixel + fh;
        InvertRect(hMemDC, &rc);
    }

    /* Blit to window */
    if (hWnd)
    {
        InvalidateRect(hWnd, NULL, FALSE);
        UpdateWindow(hWnd);
    }
}

/* Process window messages */
static void pump_messages(void)
{
    MSG msg;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        /* Signal to exit */
        if (msg.message == WM_QUIT)
            s_ungetch = 27; /* ESC */

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

WINDOW *initscr(void)
{
    WNDCLASS wc;
    HDC screenDC;
    int win_w, win_h;
    int scr_w, scr_h;
    RECT rect;
    HBRUSH hBrush;
    TEXTMETRIC tm;
    HDC tmpDC;
    HDC tmpMemDC;
    HFONT tmpFont;

    /* Register window class */
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = TEXT("CrashEditClass");

    if (!RegisterClass(&wc))
    {
        WNDCLASS existing;

        if (!GetClassInfo(GetModuleHandle(NULL), TEXT("CrashEditClass"), &existing) || existing.lpfnWndProc != WndProc)
            return NULL;
    }

    /* Initial size */
    COLS = 80;
    LINES = 25;
    fw = 8;
    fh = s_win_font_size;
    fb = 12;

    /* Measure requested font for accurate cell metrics */

    tmpDC = GetDC(NULL);
    tmpMemDC = tmpDC ? CreateCompatibleDC(tmpDC) : NULL;
    tmpFont = tmpMemDC ? CreateFont(s_win_font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, TEXT(win_font_name)) : NULL;

    if (tmpFont && tmpMemDC)
    {
        SelectObject(tmpMemDC, tmpFont);

        if (GetTextMetrics(tmpMemDC, &tm))
        {
            fw = tm.tmAveCharWidth;
            fh = tm.tmHeight;
            fb = tm.tmAscent;
        }
    }

    if (tmpFont)
        DeleteObject(tmpFont);

    if (tmpMemDC)
        DeleteDC(tmpMemDC);

    if (tmpDC)
        ReleaseDC(NULL, tmpDC);

    /* Create FreeType renderer and override GDI metrics with actual font metrics */
    if (s_added_font_count > 0)
    {
        int i;
        struct TEGlyphMetrics metrics;

        if (s_te_dc)
            TE_ContextRelease(s_te_dc);

        s_te_dc = TE_ContextCreate();
        s_use_te = 0;

        if (s_te_dc)
        {
            TE_SetFlags(s_te_dc, TE_FLAG_ANTIALIAS | TE_FLAG_HIGHQUALITY | TE_FLAG_FIXEDWIDTH);
            TE_SetTabWidth(s_te_dc, (ULONG)s_tab_size);

            for (i = 0; i < s_added_font_count; i++)
                TE_FontAdd(s_te_dc, s_added_fonts[i], (LONG)s_win_font_size, 0);

            TE_GetMetrics(s_te_dc, &metrics);

            if (metrics.width > 0 && metrics.height > 0)
            {
                s_use_te = 1;
                fw = metrics.width;
                fh = metrics.height;
                fb = metrics.baseY;
            }
            else
            {
                TE_ContextRelease(s_te_dc);
                s_te_dc = NULL;
            }
        }
    }

    /* Center on screen */
    scr_w = GetSystemMetrics(SM_CXSCREEN);
    scr_h = GetSystemMetrics(SM_CYSCREEN);
    win_w = COLS * fw + 16;
    win_h = LINES * fh + 39;
    win_x = (scr_w - win_w) / 2;
    win_y = (scr_h - win_h) / 2;

    /* Create window */
    hWnd = CreateWindow(
        TEXT("CrashEditClass"),
        TEXT(WRAPPER_PID),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME,
        win_x, win_y, win_w, win_h,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!hWnd)
    {
        UnregisterClass(TEXT("CrashEditClass"), GetModuleHandle(NULL));
        return NULL;
    }

    /* Device contexts */
    hDC = GetDC(hWnd);

    if (!hDC)
    {
        DestroyWindow(hWnd);
        UnregisterClass(TEXT("CrashEditClass"), GetModuleHandle(NULL));
        return NULL;
    }

    screenDC = GetDC(NULL);

    if (!screenDC)
    {
        ReleaseDC(hWnd, hDC);

        DestroyWindow(hWnd);
        UnregisterClass(TEXT("CrashEditClass"), GetModuleHandle(NULL));
        return NULL;
    }

    hMemDC = CreateCompatibleDC(screenDC);

    if (!hMemDC)
    {
        ReleaseDC(NULL, screenDC);
        ReleaseDC(hWnd, hDC);

        DestroyWindow(hWnd);
        UnregisterClass(TEXT("CrashEditClass"), GetModuleHandle(NULL));
        return NULL;
    }

    hBitmap = CreateCompatibleBitmap(hDC, COLS * fw, LINES * fh);

    if (!hBitmap)
    {
        DeleteDC(hMemDC);
        ReleaseDC(NULL, screenDC);
        ReleaseDC(hWnd, hDC);

        DestroyWindow(hWnd);
        UnregisterClass(TEXT("CrashEditClass"), GetModuleHandle(NULL));
        return NULL;
    }

    SelectObject(hMemDC, hBitmap);
    ReleaseDC(NULL, screenDC);

    /* Clear bitmap to black */
    rect.left = 0;
    rect.top = 0;
    rect.right = COLS * fw;
    rect.bottom = LINES * fh;
    hBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hMemDC, &rect, hBrush);
    DeleteObject(hBrush);

    /* Create font */
    hFont = CreateFont(fh, fw, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, TEXT(win_font_name));

    s_hfont_is_stock = 0;

    if (!hFont)
    {
        hFont = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);
        s_hfont_is_stock = 1;
    }

    SelectObject(hMemDC, hFont);

    if (GetTextMetrics(hMemDC, &tm) && !s_use_te)
    {
        fh = tm.tmHeight;
        fb = tm.tmAscent;
    }

    SetBkMode(hMemDC, OPAQUE);
    SetTextColor(hMemDC, RGB(255, 255, 255));
    SetBkColor(hMemDC, RGB(0, 0, 0));
    SetTextAlign(hMemDC, TA_LEFT | TA_TOP);
    SetTextCharacterExtra(hMemDC, 0);

    /* Show window */
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    /* Drain initial messages */
    pump_messages();

    /* Allocate stdscr */
    stdscr = (WINDOW *)malloc(sizeof(WINDOW));

    if (!stdscr)
    {
        endwin();
        return NULL;
    }

    memset(stdscr, 0, sizeof(WINDOW));

    stdscr->_maxy = LINES;
    stdscr->_maxx = COLS;
    stdscr->_begy = 0;
    stdscr->_begx = 0;
    stdscr->attrs = A_NORMAL;
    stdscr->color_pair = 0;

    stdscr->cells = (Cell *)malloc(sizeof(Cell) * LINES * COLS);

    if (!stdscr->cells)
    {
        free(stdscr);
        stdscr = NULL;
        endwin();
        return NULL;
    }

    clear_cells(stdscr);
    curscr = stdscr;

    /* Default color pair */
    memset(s_pair_ok, 0, sizeof(s_pair_ok));

    s_pair_fg[0] = COLOR_WHITE;
    s_pair_bg[0] = COLOR_BLACK;
    s_pair_ok[0] = 1;
    s_colors_on = 1;

    return stdscr;
}

int endwin(void)
{
    if (hBitmap)
        DeleteObject(hBitmap);

    if (hMemDC)
        DeleteDC(hMemDC);

    if (hFont && !s_hfont_is_stock)
        DeleteObject(hFont);

    if (hDC && hWnd)
        ReleaseDC(hWnd, hDC);

    if (hWnd)
    {
        DestroyWindow(hWnd);
        UnregisterClass(TEXT("CrashEditClass"), GetModuleHandle(NULL));
    }

    hBitmap = NULL;
    hMemDC = NULL;
    hFont = NULL;
    hDC = NULL;
    hWnd = NULL;
    s_hfont_is_stock = 0;

    win32_clear_font_files();

    if (s_te_dc)
    {
        TE_ContextRelease(s_te_dc);
        s_te_dc = NULL;
        s_use_te = 0;
    }

    if (stdscr)
    {
        if (stdscr->cells)
            free(stdscr->cells);

        free(stdscr);
        stdscr = NULL;
    }

    curscr = NULL;
    return OK;
}

/* Reinitialize the window after changing font configuration */
int win32_reload_ttf(const char *font_path, int new_size)
{
    int i;
    struct TEGlyphMetrics metrics;

    if (!font_path || !font_path[0])
        return 0;

    if (new_size < 6 || new_size > 96)
        return 0;

    s_win_font_size = new_size;

    if (!s_use_te || !s_te_dc)
        return 1;

    TE_FontFlush(s_te_dc);

    if (!TE_FontAdd(s_te_dc, font_path, (LONG)new_size, 0))
    {
        TE_ContextRelease(s_te_dc);
        s_te_dc = NULL;
        s_use_te = 0;
        return 0;
    }

    for (i = 1; i < s_added_font_count; i++)
        TE_FontAdd(s_te_dc, s_added_fonts[i], (LONG)new_size, 0);

    /* Keep the recorded primary font in sync with what we just loaded */
    strncpy(s_added_fonts[0], font_path, MAX_PATH - 1);
    s_added_fonts[0][MAX_PATH - 1] = '\0';

    if (s_added_font_count < 1)
        s_added_font_count = 1;

    TE_GetMetrics(s_te_dc, &metrics);

    if (metrics.width > 0 && metrics.height > 0)
    {
        int old_fw = fw;
        int old_fh = fh;

        fw = metrics.width;
        fh = metrics.height;
        fb = metrics.baseY;

        /* Cell size changed: resize the physical window and its backing bitmap to match */
        if ((fw != old_fw || fh != old_fh) && hWnd)
        {
            RECT client, wrect;
            int new_win_w, new_win_h;
            int border_w, border_h;

            /* Recreate the backing bitmap at the new cell size FIRST */
            if (hDC && hBitmap)
            {
                RECT rect;
                HBRUSH hBrush;
                HBITMAP newBitmap = CreateCompatibleBitmap(hDC, COLS * fw, LINES * fh);

                if (newBitmap)
                {
                    DeleteObject(hBitmap);
                    hBitmap = newBitmap;
                    SelectObject(hMemDC, hBitmap);

                    rect.left = 0;
                    rect.top = 0;
                    rect.right = COLS * fw;
                    rect.bottom = LINES * fh;
                    hBrush = CreateSolidBrush(RGB(0, 0, 0));

                    FillRect(hMemDC, &rect, hBrush);
                    DeleteObject(hBrush);
                }
            }

            /*  Resize the physical window to keep COLS x LINES at the new cell size*/
            GetClientRect(hWnd, &client);
            GetWindowRect(hWnd, &wrect);

            border_w = (wrect.right - wrect.left) - client.right;
            border_h = (wrect.bottom - wrect.top) - client.bottom;

            new_win_w = COLS * fw + border_w;
            new_win_h = LINES * fh + border_h;

            SetWindowPos(hWnd, NULL, 0, 0, new_win_w, new_win_h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    render_all();
    return 1;
}

int win32_reinit_window(void)
{
    char saved_fonts[WIN32_ADDED_FONTS_MAX][MAX_PATH];
    int saved_count = 0;
    int i;
    WINDOW *newscr;
    int result = 0;

    if (!hWnd)
        return 0; /* Window not open yet */

    /* Save added font-file paths so initscr() can recreate the TTE context */
    for (i = 0; i < s_added_font_count; i++)
        snprintf(saved_fonts[i], MAX_PATH, "%s", s_added_fonts[i]);

    saved_count = s_added_font_count;

    s_reinit_in_progress = 1;

    endwin();

    /* Restore font paths before initscr() recreates the TTE context */
    for (i = 0; i < saved_count; i++)
        win32_add_font_file(saved_fonts[i]);

    newscr = initscr();

    if (newscr)
        result = 1;

    s_reinit_in_progress = 0;
    return result;
}

bool isendwin(void)
{
    return (stdscr == NULL) ? TRUE : FALSE;
}

WINDOW *newwin(int nlines, int ncols, int begin_y, int begin_x)
{
    WINDOW *win;

    if (nlines <= 0 || ncols <= 0)
        return NULL;

    if (begin_y < 0 || begin_x < 0)
        return NULL;

    if (begin_y + nlines > LINES || begin_x + ncols > COLS)
        return NULL;

    win = (WINDOW *)malloc(sizeof(WINDOW));

    if (!win)
        return NULL;

    memset(win, 0, sizeof(WINDOW));

    win->_maxy = nlines;
    win->_maxx = ncols;
    win->_begy = begin_y;
    win->_begx = begin_x;
    win->attrs = A_NORMAL;
    win->color_pair = 0;

    win->cells = (Cell *)malloc(sizeof(Cell) * nlines * ncols);

    if (!win->cells)
    {
        free(win);
        return NULL;
    }

    clear_cells(win);
    return win;
}

int delwin(WINDOW *win)
{
    if (!win)
        return ERR;

    if (win->cells)
        free(win->cells);

    free(win);
    return OK;
}

WINDOW *subwin(WINDOW *orig, int nlines, int ncols, int begin_y, int begin_x)
{
    WINDOW *win;

    if (!orig)
        return NULL;

    win = newwin(nlines, ncols, begin_y, begin_x);

    if (win)
        win->_parent = orig;

    return win;
}

WINDOW *derwin(WINDOW *orig, int nlines, int ncols, int begin_y, int begin_x)
{
    if (!orig)
        return NULL;

    return subwin(orig, nlines, ncols, orig->_begy + begin_y, orig->_begx + begin_x);
}

int mvwin(WINDOW *win, int y, int x)
{
    if (!win)
        return ERR;

    if (y < 0 || x < 0 || y + win->_maxy > LINES || x + win->_maxx > COLS)
        return ERR;

    win->_begy = y;
    win->_begx = x;

    return OK;
}

int wrefresh(WINDOW *win)
{
    int r, c;
    Cell *src, *dst;

    if (!win)
        return ERR;

    if (win != stdscr)
    {
        /* Copy to stdscr */
        for (r = 0; r < win->_maxy; r++)
        {
            int dst_y = win->_begy + r;

            if (dst_y < 0 || dst_y >= LINES)
                continue;

            for (c = 0; c < win->_maxx; c++)
            {
                int dst_x = win->_begx + c;

                if (dst_x < 0 || dst_x >= COLS)
                    continue;

                src = CELL(win, r, c);
                dst = CELL(stdscr, dst_y, dst_x);
                *dst = *src;
            }
        }
    }

    pump_messages();
    render_all();
    return OK;
}

int refresh(void)
{
    return wrefresh(stdscr);
}

int wnoutrefresh(WINDOW *win)
{
    return wrefresh(win);
}

int doupdate(void)
{
    return wrefresh(stdscr);
}

int redrawwin(WINDOW *win)
{
    return wrefresh(win);
}

int clear(void)
{
    return wclear(stdscr);
}

int wclear(WINDOW *win)
{
    if (!win)
        return ERR;
    clear_cells(win);
    return OK;
}

int erase(void)
{
    return werase(stdscr);
}

int werase(WINDOW *win)
{
    return wclear(win);
}

int clrtobot(void)
{
    return wclrtobot(stdscr);
}

int wclrtobot(WINDOW *win)
{
    int r, c, start_r, start_c;

    if (!win)
        return ERR;

    start_r = win->_cury;
    start_c = win->_curx;

    for (r = start_r; r < win->_maxy; r++)
    {
        for (c = (r == start_r ? start_c : 0); c < win->_maxx; c++)
        {
            CELL(win, r, c)->ch = ' ';
            CELL(win, r, c)->attrs = win->attrs;
            CELL(win, r, c)->full_cp = 0;
        }
    }

    return OK;
}

int clrtoeol(void)
{
    return wclrtoeol(stdscr);
}

int wclrtoeol(WINDOW *win)
{
    int c, r;

    if (!win)
        return ERR;

    r = win->_cury;

    for (c = win->_curx; c < win->_maxx; c++)
    {
        CELL(win, r, c)->ch = ' ';
        CELL(win, r, c)->attrs = win->attrs;
        CELL(win, r, c)->full_cp = 0;
    }

    return OK;
}

int move(int y, int x)
{
    return wmove(stdscr, y, x);
}

int wmove(WINDOW *win, int y, int x)
{
    if (!win)
        return ERR;

    if (y < 0 || y >= win->_maxy || x < 0 || x >= win->_maxx)
        return ERR;

    win->_cury = y;
    win->_curx = x;

    return OK;
}

int addch(const chtype ch)
{
    return waddch(stdscr, ch);
}

int waddch(WINDOW *win, const chtype ch)
{
    int r, c;
    chtype ch_out;
    attr_t attrs;
    int cw;

    if (!win)
        return ERR;

    r = win->_cury;
    c = win->_curx;

    if (r < 0 || r >= win->_maxy || c < 0 || c >= win->_maxx)
        return ERR;

    /* Emoji / supplementary-plane: do not truncate to 16 bits */
    if (ch > 0xFFFFUL)
        return waddch32(win, ch);

    ch_out = ch & A_CHARTEXT;
    attrs = (ch & A_ATTRIBUTES) | win->attrs;
    attrs &= ~A_COLOR;
    attrs |= COLOR_PAIR(win->color_pair);

    /* Writing on TRAILING -> orphan the lead */
    if (c > 0)
    {
        Cell *cur = CELL(win, r, c);

        if (cur->ch == WIN32_CELL_WIDE_TRAILING)
        {
            Cell *prev = CELL(win, r, c - 1);
            prev->ch = ' ';
            prev->full_cp = 0;
        }
    }

    /* Tab expands to tab stop; in ANSI mode '\t' is one CP437 cell */
    if (!s_ansi_mode && ch_out == '\t')
    {
        int tab_w = s_tab_size - (c % s_tab_size);
        int j;

        if (c + tab_w > win->_maxx)
            tab_w = win->_maxx - c;

        for (j = 0; j < tab_w && c + j < win->_maxx; j++)
        {
            Cell *tab_cell = CELL(win, r, c + j);

            tab_cell->ch = (j == 0) ? '\t' : ' ';
            tab_cell->attrs = attrs;
            tab_cell->full_cp = 0;
        }

        win->_curx += tab_w;

        if (win->_curx >= win->_maxx)
        {
            win->_curx = 0;
            win->_cury++;

            if (win->_cury >= win->_maxy && (win->_flags & 2))
            {
                win->_cury = win->_maxy - 1;
                wscrl(win, 1);
            }
        }

        return OK;
    }

    cw = is_wide_cp((unsigned int)ch_out) ? 2 : 1;

    /* Narrow into lead -> orphan the trailing */
    if (cw == 1 && c + 1 < win->_maxx)
    {
        Cell *nxt = CELL(win, r, c + 1);

        if (nxt->ch == WIN32_CELL_WIDE_TRAILING)
        {
            nxt->ch = ' ';
            nxt->attrs = attrs;
            nxt->full_cp = 0;
        }
    }

    /* Wide over wide -> orphan the next trailing */
    if (cw == 2 && c + 2 < win->_maxx)
    {
        Cell *thd = CELL(win, r, c + 2);

        if (thd->ch == WIN32_CELL_WIDE_TRAILING)
        {
            thd->ch = ' ';
            thd->attrs = attrs;
            thd->full_cp = 0;
        }
    }

    /* Write lead cell */
    CELL(win, r, c)->ch = ch_out;
    CELL(win, r, c)->attrs = attrs;
    CELL(win, r, c)->full_cp = 0;
    win->_curx++;

    /* Write trailing cell for wide glyphs */
    if (cw == 2)
    {
        Cell *trail = CELL(win, r, win->_curx);

        trail->ch = WIN32_CELL_WIDE_TRAILING;
        trail->attrs = attrs;
        trail->full_cp = 0;
        win->_curx++;
    }

    if (win->_curx >= win->_maxx)
    {
        win->_curx = 0;
        win->_cury++;

        /* Scroll if at bottom and scrollok set */
        if (win->_cury >= win->_maxy && (win->_flags & 2))
        {
            win->_cury = win->_maxy - 1;
            wscrl(win, 1);
        }
    }
    return OK;
}

int mvaddch(int y, int x, const chtype ch)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return waddch(stdscr, ch);
}

int mvwaddch(WINDOW *win, int y, int x, const chtype ch)
{
    if (wmove(win, y, x) == ERR)
        return ERR;

    return waddch(win, ch);
}

/* Write full 32-bit Unicode codepoint on Windows (wchar_t is 16-bit) */
int waddch32(WINDOW *win, unsigned long cp)
{
    int r, c;
    chtype ch_out;
    attr_t attrs;
    int cw;

    if (!win)
        return ERR;

    r = win->_cury;
    c = win->_curx;

    if (r < 0 || r >= win->_maxy || c < 0 || c >= win->_maxx)
        return ERR;

    attrs = win->attrs;
    attrs &= ~A_COLOR;
    attrs |= COLOR_PAIR(win->color_pair);

    /* Reuse waddch's truncation for width/ncurses compatibility; then override full_cp */
    ch_out = (chtype)(cp & A_CHARTEXT);

    /* Case A: writing on TRAILING -> orphan the lead */
    if (c > 0)
    {
        Cell *cur = CELL(win, r, c);

        if (cur->ch == WIN32_CELL_WIDE_TRAILING)
        {
            Cell *prev = CELL(win, r, c - 1);
            prev->ch = ' ';
            prev->full_cp = 0;
        }
    }

    cw = is_wide_cp((unsigned int)cp) ? 2 : 1;

    /* Case B: narrow into lead -> orphan the trailing */
    if (cw == 1 && c + 1 < win->_maxx)
    {
        Cell *nxt = CELL(win, r, c + 1);

        if (nxt->ch == WIN32_CELL_WIDE_TRAILING)
        {
            nxt->ch = ' ';
            nxt->attrs = attrs;
            nxt->full_cp = 0;
        }
    }

    /* Case C: wide over wide -> orphan the next trailing */
    if (cw == 2 && c + 2 < win->_maxx)
    {
        Cell *thd = CELL(win, r, c + 2);

        if (thd->ch == WIN32_CELL_WIDE_TRAILING)
        {
            thd->ch = ' ';
            thd->attrs = attrs;
            thd->full_cp = 0;
        }
    }

    /* Write lead cell */
    CELL(win, r, c)->ch = ch_out;
    CELL(win, r, c)->attrs = attrs;
    CELL(win, r, c)->full_cp = cp;
    win->_curx++;

    /* Write trailing cell for wide glyphs */
    if (cw == 2)
    {
        Cell *trail = CELL(win, r, win->_curx);

        trail->ch = WIN32_CELL_WIDE_TRAILING;
        trail->attrs = attrs;
        trail->full_cp = 0;
        win->_curx++;
    }

    if (win->_curx >= win->_maxx)
    {
        win->_curx = 0;
        win->_cury++;

        if (win->_cury >= win->_maxy && (win->_flags & 2))
        {
            win->_cury = win->_maxy - 1;
            wscrl(win, 1);
        }
    }

    return OK;
}

int mvaddch32(int y, int x, unsigned long cp)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return waddch32(stdscr, cp);
}

chtype mvinch(int y, int x)
{
    return mvwinch(stdscr, y, x);
}

chtype mvwinch(WINDOW *win, int y, int x)
{
    if (!win)
        return (chtype)ERR;

    if (y < 0 || y >= win->_maxy || x < 0 || x >= win->_maxx)
        return (chtype)ERR;

    return CELL(win, y, x)->ch;
}

int addstr(const char *str)
{
    return waddstr(stdscr, str);
}

int addnstr(const char *str, int n)
{
    return waddnstr(stdscr, str, n);
}

int waddstr(WINDOW *win, const char *str)
{
    if (!str)
        return ERR;
    return waddnstr(win, str, (int)strlen(str));
}

static int utf8_decode_one_cp(const unsigned char *p, int max, unsigned long *out, int *consumed)
{
    unsigned char b0;

    if (max <= 0)
    {
        *out = 0;
        *consumed = 0;
        return -1;
    }

    b0 = p[0];

    if (b0 < 0x80)
    {
        *out = (unsigned long)b0;
        *consumed = 1;
        return 0;
    }

    if ((b0 & 0xE0) == 0xC0 && max >= 2 && (p[1] & 0xC0) == 0x80)
    {
        *out = (unsigned long)(((b0 & 0x1F) << 6) | (p[1] & 0x3F));
        *consumed = 2;
        return 0;
    }

    if ((b0 & 0xF0) == 0xE0 && max >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
    {
        *out = (unsigned long)(((b0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
        *consumed = 3;
        return 0;
    }

    if ((b0 & 0xF8) == 0xF0 && max >= 4 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80)
    {
        *out = (unsigned long)(((b0 & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F));
        *consumed = 4;
        return 0;
    }

    *out = (unsigned long)b0;
    *consumed = 1;
    return -1;
}

int waddnstr(WINDOW *win, const char *str, int n)
{
    int i;

    if (!win || !str)
        return ERR;

    if (n < 0)
        n = (int)strlen(str);

    i = 0;

    while (i < n && str[i])
    {
        unsigned long cp = 0;
        int consumed = 1;

        utf8_decode_one_cp((const unsigned char *)&str[i], n - i, &cp, &consumed);

        if (consumed <= 0)
            consumed = 1; /* Always make progress */

        if (cp > 0xFFFF || is_wide_cp((unsigned int)cp))
        {
            if (waddch32(win, cp) == ERR)
                return ERR;
        }
        else
        {
            if (waddch(win, (chtype)cp) == ERR)
                return ERR;
        }

        i += consumed;
    }

    return OK;
}

int mvaddstr(int y, int x, const char *str)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return waddstr(stdscr, str);
}

int mvaddnstr(int y, int x, const char *str, int n)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return waddnstr(stdscr, str, n);
}

int mvwaddstr(WINDOW *win, int y, int x, const char *str)
{
    if (wmove(win, y, x) == ERR)
        return ERR;

    return waddstr(win, str);
}

int mvwaddnstr(WINDOW *win, int y, int x, const char *str, int n)
{
    if (wmove(win, y, x) == ERR)
        return ERR;

    return waddnstr(win, str, n);
}

int addwstr(const wchar_t *wstr)
{
    return waddwstr(stdscr, wstr);
}

int addnwstr(const wchar_t *wstr, int n)
{
    return waddnwstr(stdscr, wstr, n);
}

int waddwstr(WINDOW *win, const wchar_t *wstr)
{
    if (!wstr)
        return ERR;

    return waddnwstr(win, wstr, (int)wcslen(wstr));
}

int waddnwstr(WINDOW *win, const wchar_t *wstr, int n)
{
    int i, len;

    if (!win || !wstr)
        return ERR;

    len = (int)wcslen(wstr);

    if (n < 0 || n > len)
        n = len;

    for (i = 0; i < n && wstr[i]; i++)
    {
        unsigned int wc = (unsigned int)wstr[i];
        unsigned long cp = wc;

        if (wc >= 0xD800 && wc <= 0xDBFF && i + 1 < n)
        {
            unsigned int lo = (unsigned int)wstr[i + 1];

            if (lo >= 0xDC00 && lo <= 0xDFFF)
            {
                cp = 0x10000UL + (((unsigned long)(wc - 0xD800)) << 10) + (unsigned long)(lo - 0xDC00);
                i++; /* Consume trailing surrogate */
            }
        }

        /* Supplementary plane or wide BMP -> waddch32 (preserves cp) */
        if (cp > 0xFFFF || is_wide_cp((unsigned int)cp))
        {
            if (waddch32(win, cp) == ERR)
                return ERR;
        }
        else
        {
            if (waddch(win, (chtype)cp) == ERR)
                return ERR;
        }
    }

    return OK;
}

int mvaddwstr(int y, int x, const wchar_t *wstr)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return waddwstr(stdscr, wstr);
}

int mvaddnwstr(int y, int x, const wchar_t *wstr, int n)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return waddnwstr(stdscr, wstr, n);
}

int mvwaddwstr(WINDOW *win, int y, int x, const wchar_t *wstr)
{
    if (wmove(win, y, x) == ERR)
        return ERR;

    return waddwstr(win, wstr);
}

int mvwaddnwstr(WINDOW *win, int y, int x, const wchar_t *wstr, int n)
{
    if (wmove(win, y, x) == ERR)
        return ERR;

    return waddnwstr(win, wstr, n);
}

int mvwchgat(WINDOW *win, int y, int x, int n, attr_t attr, short color, const void *opts)
{
    int i;
    (void)opts;

    if (!win || y < 0 || y >= win->_maxy || x < 0 || x >= win->_maxx)
        return ERR;

    if (n < 0)
        n = win->_maxx - x;

    for (i = 0; i < n && (x + i) < win->_maxx; i++)
    {
        Cell *c = CELL(win, y, x + i);

        c->attrs = (c->attrs & A_COLOR) | (attr & ~A_COLOR);

        if (color >= 0)
            c->attrs = (c->attrs & ~A_COLOR) | COLOR_PAIR(color);
    }

    return OK;
}

int mvchgat(int y, int x, int n, attr_t attr, short color, const void *opts)
{
    return mvwchgat(stdscr, y, x, n, attr, color, opts);
}

int printw(const char *fmt, ...)
{
    va_list ap;
    char buf[4096];
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= sizeof(buf))
        buf[sizeof(buf) - 1] = '\0';

    return waddstr(stdscr, buf);
}

int wprintw(WINDOW *win, const char *fmt, ...)
{
    va_list ap;
    char buf[4096];
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= sizeof(buf))
        buf[sizeof(buf) - 1] = '\0';

    return waddstr(win, buf);
}

int mvprintw(int y, int x, const char *fmt, ...)
{
    va_list ap;
    char buf[4096];
    int n;

    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= sizeof(buf))
        buf[sizeof(buf) - 1] = '\0';

    return waddstr(stdscr, buf);
}

int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...)
{
    va_list ap;
    char buf[4096];
    int n;

    if (wmove(win, y, x) == ERR)
        return ERR;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= sizeof(buf))
        buf[sizeof(buf) - 1] = '\0';

    return waddstr(win, buf);
}

int attron(int attrs)
{
    return wattron(stdscr, attrs);
}

int wattroff(WINDOW *win, int attrs)
{
    if (!win)
        return ERR;

    win->attrs &= ~(attr_t)attrs;

    /* reset pair when removing A_COLOR */
    if (attrs & A_COLOR)
        win->color_pair = 0;

    return OK;
}

int wattron(WINDOW *win, int attrs)
{
    if (!win)
        return ERR;

    if (attrs & A_COLOR)
        win->attrs &= ~A_COLOR;

    win->attrs |= (attr_t)attrs;

    /* Extract and set color pair if A_COLOR is present */
    if (attrs & A_COLOR)
    {
        int new_pair = (int)PAIR_NUMBER(attrs);

        if (new_pair != 0)
            win->color_pair = new_pair;
    }

    return OK;
}

int attroff(int attrs)
{
    return wattroff(stdscr, attrs);
}

int attrset(int attrs)
{
    return wattrset(stdscr, attrs);
}

int wattrset(WINDOW *win, int attrs)
{
    if (!win)
        return ERR;

    win->attrs = (attr_t)attrs;

    /* Extract color pair */
    if (attrs & A_COLOR)
        win->color_pair = (int)PAIR_NUMBER(attrs);
    else
        win->color_pair = 0;

    return OK;
}

int color_set(short color_pair_number, void *opts)
{
    return wcolor_set(stdscr, color_pair_number, NULL);
}

int wcolor_set(WINDOW *win, short color_pair_number, void *opts)
{
    if (!win)
        return ERR;

    win->attrs = (win->attrs & ~A_COLOR) | COLOR_PAIR(color_pair_number);
    win->color_pair = (int)color_pair_number;

    return OK;
}

chtype getattrs(WINDOW *win)
{
    if (!win)
        return (chtype)A_NORMAL;

    return win->attrs;
}

int attr_on(attr_t attrs, void *opts)
{
    return wattron(stdscr, (int)attrs);
}

int attr_off(attr_t attrs, void *opts)
{
    return wattroff(stdscr, (int)attrs);
}

int attr_set(attr_t attrs, short color_pair, void *opts)
{
    if (!stdscr)
        return ERR;

    stdscr->attrs = (attr_t)(attrs | COLOR_PAIR(color_pair));
    stdscr->color_pair = color_pair;

    return OK;
}

int wattr_get(WINDOW *win, attr_t *attrs, short *color_pair, void *opts)
{
    if (!win)
        return ERR;

    if (attrs)
        *attrs = win->attrs;

    if (color_pair)
        *color_pair = (short)PAIR_NUMBER(win->attrs);

    return OK;
}

bool has_colors(void)
{
    return TRUE;
}

bool can_change_color(void)
{
    return FALSE;
}

int start_color(void)
{
    s_colors_on = 1;

    /* Default pair 0 */
    s_pair_fg[0] = COLOR_WHITE;
    s_pair_bg[0] = COLOR_BLACK;
    s_pair_ok[0] = 1;

    return OK;
}

int init_pair(short pair, short fg, short bg)
{
    if (pair < 0 || pair >= COLOR_PAIRS)
        return ERR;

    if (fg < 0 || fg > 15 || bg < 0 || bg > 15)
        return ERR;

    s_pair_fg[pair] = fg;
    s_pair_bg[pair] = bg;
    s_pair_ok[pair] = 1;

    return OK;
}

int use_default_colors(void)
{
    return OK;
}

int pair_content(short pair, short *fg, short *bg)
{
    if (pair < 0 || pair >= COLOR_PAIRS)
        return ERR;

    if (!s_pair_ok[pair])
        return ERR;

    if (fg)
        *fg = s_pair_fg[pair];

    if (bg)
        *bg = s_pair_bg[pair];

    return OK;
}

int getch(void)
{
    return wgetch(stdscr);
}

int wgetch(WINDOW *win)
{
    MSG msg;
    int ch;
    int i;

    /* Check ungetch buffer first */
    if (s_ungetch != ERR)
    {
        ch = s_ungetch;
        s_ungetch = ERR;
        return ch;
    }

    /* Mouse event pending */
    if (s_mouse_event_pending)
    {
        s_mouse_event_pending = 0;
        return KEY_MOUSE;
    }

    /* Wait for a key */
    while (s_key_count == 0)
    {
        if (s_nodelay)
        {
            /* non-blocking: poll once */
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    s_ungetch = 27;
                    return 27;
                }

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
                return ERR;
        }
        else
        {
            /* Blocking: wait for message */
            BOOL result = GetMessage(&msg, NULL, 0, 0);

            if (result == 0)
            {
                /* WM_QUIT */
                s_ungetch = 27;
                return 27;
            }
            else if (result == -1)
            {
                /* error */
                return ERR;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    ch = s_key_buf[0];

    /* Shift buffer */
    for (i = 0; i < s_key_count - 1; i++)
        s_key_buf[i] = s_key_buf[i + 1];

    s_key_count--;

    return ch;
}

int mvgetch(int y, int x)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return wgetch(stdscr);
}

int mvwgetch(WINDOW *win, int y, int x)
{
    if (wmove(win, y, x) == ERR)
        return ERR;

    return wgetch(win);
}

int ungetch(int ch)
{
    if (s_ungetch != ERR)
        return ERR;

    s_ungetch = ch;

    return OK;
}

int flushinp(void)
{
    s_key_count = 0;
    return OK;
}

int get_wch(wint_t *wch)
{
    return wget_wch(stdscr, wch);
}

int mvget_wch(int y, int x, wint_t *wch)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return get_wch(wch);
}

int mvwget_wch(WINDOW *win, int y, int x, wint_t *wch)
{
    if (wmove(win, y, x) == ERR)
        return ERR;

    return wget_wch(win, wch);
}

int wget_wch(WINDOW *win, wint_t *wch)
{
    int ch;

    if (!wch)
        return ERR;

    ch = wgetch(win);

    if (ch == ERR)
        return ERR;

    if (ch >= KEY_MIN)
    {
        *wch = (wint_t)ch;
        return KEY_CODE_YES;
    }

    *wch = (wint_t)ch;

    return OK;
}

int getstr(char *str)
{
    return wgetstr(stdscr, str);
}

int getnstr(char *str, int n)
{
    return wgetnstr(stdscr, str, n);
}

int mvgetstr(int y, int x, char *str)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return getstr(str);
}

int mvgetnstr(int y, int x, char *str, int n)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return getnstr(str, n);
}

int mvwgetstr(WINDOW *win, int y, int x, char *str)
{
    if (wmove(win, y, x) == ERR)
        return ERR;

    return wgetstr(win, str);
}

int mvwgetnstr(WINDOW *win, int y, int x, char *str, int n)
{
    if (wmove(win, y, x) == ERR)
        return ERR;

    return wgetnstr(win, str, n);
}

int wgetstr(WINDOW *win, char *str)
{
    return wgetnstr(win, str, 255);
}

int wgetnstr(WINDOW *win, char *str, int n)
{
    int ch, i;

    if (!str || n <= 0)
        return ERR;

    i = 0;

    while (i < n - 1)
    {
        ch = wgetch(win);

        if (ch == ERR || ch == '\n' || ch == '\r')
            break;

        if (ch == KEY_BACKSPACE || ch == '\b')
        {
            if (i > 0)
                i--;

            continue;
        }

        str[i++] = (char)ch;
    }

    str[i] = '\0';
    return OK;
}

int nodelay(WINDOW *win, bool bf)
{
    s_nodelay = bf ? 1 : 0;
    return OK;
}

int timeout(int delay)
{
    s_nodelay = (delay == 0) ? 1 : 0;
    return OK;
}

int wtimeout(WINDOW *win, int delay)
{
    s_nodelay = (delay == 0) ? 1 : 0;
    return OK;
}

int cbreak(void)
{
    return OK;
}

int nocbreak(void)
{
    return OK;
}

int echo(void)
{
    s_echo = 1;
    return OK;
}

int noecho(void)
{
    s_echo = 0;
    return OK;
}

int raw(void)
{
    return OK;
}

int noraw(void)
{
    return OK;
}

int nl(void)
{
    return OK;
}

int nonl(void)
{
    return OK;
}

int keypad(WINDOW *win, bool bf)
{
    s_keypad = bf ? 1 : 0;
    return OK;
}

int curs_set(int visibility)
{
    int old = s_cursor_vis;
    s_cursor_vis = visibility;
    return old;
}

int set_tabsize(int n)
{
    if (n < 1)
        n = 1;

    if (n > 16)
        n = 16;

    s_tab_size = n;

    return OK;
}

int beep(void)
{
    MessageBeep(MB_OK);
    return OK;
}

int flash(void)
{
    return beep();
}

int getmouse(MEVENT *ev)
{
    UiMouseEventType type;
    int x, y;

    if (!ev)
        return ERR;

    if (s_mouse_event == 0)
        return ERR;

    /* Unpack the event saved by the WindowProc on WM_LBUTTON*, WM_MOUSEMOVE, WM_MOUSEWHEEL */
    type = (UiMouseEventType)(s_mouse_event & 0xFF);
    x = (int)((s_mouse_event >> 8) & 0xFFF);
    y = (int)((s_mouse_event >> 20) & 0xFFF);

    /* Consume so a subsequent KEY_MOUSE without an event returns ERR */
    s_mouse_event = 0;

    ev->x = (short)x;
    ev->y = (short)y;
    ev->bstate = 0;

    switch (type)
    {
    case UI_MOUSE_PRESS_LEFT:
        ev->bstate = BUTTON1_PRESSED;
        break;

    case UI_MOUSE_RELEASE_LEFT:
        ev->bstate = BUTTON1_RELEASED;
        break;

    case UI_MOUSE_DRAG_LEFT:
        /* Use REPORT_MOUSE_POSITION only so editor classifies as drag, not new press */
        ev->bstate = REPORT_MOUSE_POSITION;
        break;

    case UI_MOUSE_WHEEL_UP:
        ev->bstate = BUTTON4_PRESSED;
        break;

    case UI_MOUSE_WHEEL_DOWN:
        ev->bstate = BUTTON5_PRESSED;
        break;

    default:
        return ERR;
    }

    return OK;
}

int border(chtype ls, chtype rs, chtype ts, chtype bs, chtype tl, chtype tr, chtype bl, chtype br)
{
    return wborder(stdscr, ls, rs, ts, bs, tl, tr, bl, br);
}

int wborder(WINDOW *win, chtype ls, chtype rs, chtype ts, chtype bs, chtype tl, chtype tr, chtype bl, chtype br)
{
    int y, x, my, mx;

    if (!win)
        return ERR;

    my = win->_maxy;
    mx = win->_maxx;

    if (my > 0 && mx > 0)
    {
        wmove(win, 0, 0);
        waddch(win, tl ? tl : ACS_ULCORNER);
        wmove(win, 0, mx - 1);
        waddch(win, tr ? tr : ACS_URCORNER);
        wmove(win, my - 1, 0);
        waddch(win, bl ? bl : ACS_LLCORNER);
        wmove(win, my - 1, mx - 1);
        waddch(win, br ? br : ACS_LRCORNER);
    }

    for (x = 1; x < mx - 1; x++)
    {
        wmove(win, 0, x);
        waddch(win, ts ? ts : ACS_HLINE);
        wmove(win, my - 1, x);
        waddch(win, bs ? bs : ACS_HLINE);
    }

    for (y = 1; y < my - 1; y++)
    {
        wmove(win, y, 0);
        waddch(win, ls ? ls : ACS_VLINE);
        wmove(win, y, mx - 1);
        waddch(win, rs ? rs : ACS_VLINE);
    }

    return OK;
}

int box(WINDOW *win, chtype verch, chtype horch)
{
    return wborder(win, verch, verch, horch, horch, 0, 0, 0, 0);
}

int hline(chtype ch, int n)
{
    return whline(stdscr, ch, n);
}

int whline(WINDOW *win, chtype ch, int n)
{
    int i;
    chtype ch_use;

    if (!win || n <= 0)
        return ERR;

    ch_use = ch ? ch : ACS_HLINE;

    for (i = 0; i < n && win->_curx < win->_maxx; i++)
        waddch(win, ch_use);

    return OK;
}

int vline(chtype ch, int n)
{
    return wvline(stdscr, ch, n);
}

int wvline(WINDOW *win, chtype ch, int n)
{
    int i, start_y;
    chtype ch_use;

    if (!win || n <= 0)
        return ERR;

    ch_use = ch ? ch : ACS_VLINE;
    start_y = win->_cury;

    for (i = 0; i < n && start_y + i < win->_maxy; i++)
    {
        wmove(win, start_y + i, win->_curx);
        waddch(win, ch_use);
    }

    return OK;
}

int mvhline(int y, int x, chtype ch, int n)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return whline(stdscr, ch, n);
}

int mvwhline(WINDOW *win, int y, int x, chtype ch, int n)
{
    if (wmove(win, y, x) == ERR)
        return ERR;

    return whline(win, ch, n);
}

int mvvline(int y, int x, chtype ch, int n)
{
    if (wmove(stdscr, y, x) == ERR)
        return ERR;

    return wvline(stdscr, ch, n);
}

int mvwvline(WINDOW *win, int y, int x, chtype ch, int n)
{
    if (wmove(win, y, x) == ERR)
        return ERR;

    return wvline(win, ch, n);
}

int bkgd(chtype ch)
{
    wbkgd(stdscr, ch);
    return OK;
}

void bkgdset(chtype ch)
{
    wbkgdset(stdscr, ch);
}

void wbkgd(WINDOW *win, chtype ch)
{
    if (!win)
        return;

    win->attrs = (attr_t)ch;
}

void wbkgdset(WINDOW *win, chtype ch)
{
    if (!win)
        return;

    win->attrs = (attr_t)ch;
}

chtype getbkgd(WINDOW *win)
{
    if (!win)
        return (chtype)A_NORMAL;

    return (chtype)win->attrs;
}

int scroll(WINDOW *win)
{
    if (!win)
        return ERR;

    return wscrl(win, 1);
}

int scrl(int n)
{
    return wscrl(stdscr, n);
}

int wscrl(WINDOW *win, int n)
{
    int r, c;

    if (!win || n == 0)
        return OK;

    if (n > 0)
    {
        for (r = 0; r < win->_maxy - n; r++)
        {
            for (c = 0; c < win->_maxx; c++)
            {
                CELL(win, r, c)->ch = CELL(win, r + n, c)->ch;
                CELL(win, r, c)->attrs = CELL(win, r + n, c)->attrs;
                CELL(win, r, c)->full_cp = CELL(win, r + n, c)->full_cp;
            }
        }

        for (r = win->_maxy - n; r < win->_maxy; r++)
        {
            for (c = 0; c < win->_maxx; c++)
            {
                CELL(win, r, c)->ch = ' ';
                CELL(win, r, c)->attrs = win->attrs;
                CELL(win, r, c)->full_cp = 0;
            }
        }
    }
    else
    {
        for (r = win->_maxy - 1; r >= -n; r--)
        {
            for (c = 0; c < win->_maxx; c++)
            {
                CELL(win, r, c)->ch = CELL(win, r + n, c)->ch;
                CELL(win, r, c)->attrs = CELL(win, r + n, c)->attrs;
                CELL(win, r, c)->full_cp = CELL(win, r + n, c)->full_cp;
            }
        }

        for (r = 0; r < -n; r++)
        {
            for (c = 0; c < win->_maxx; c++)
            {
                CELL(win, r, c)->ch = ' ';
                CELL(win, r, c)->attrs = win->attrs;
                CELL(win, r, c)->full_cp = 0;
            }
        }
    }

    return OK;
}

int setscrreg(int top, int bot)
{
    return OK;
}

int wsetscrreg(WINDOW *win, int top, int bot)
{
    return OK;
}

void wtouchln(WINDOW *win, int y, int n, int changed)
{
}

int resize_term(int nlines, int ncols)
{
    RECT rect;
    HBRUSH hBrush;
    int old_lines = LINES;
    int old_cols = COLS;

    if (nlines <= 0 || ncols <= 0)
        return ERR;

    LINES = nlines;
    COLS = ncols;

    if (stdscr)
    {
        if (stdscr->cells)
            free(stdscr->cells);

        stdscr->cells = (Cell *)malloc(sizeof(Cell) * LINES * COLS);

        if (!stdscr->cells)
        {
            /* Restore on alloc failure */
            COLS = old_cols;
            LINES = old_lines;
            return ERR;
        }

        stdscr->_maxy = LINES;
        stdscr->_maxx = COLS;
        clear_cells(stdscr);
    }

    /* Recreate GDI bitmap with new size */
    if (hBitmap && hMemDC)
    {
        DeleteObject(hBitmap);
        hBitmap = CreateCompatibleBitmap(hDC, COLS * fw, LINES * fh);
        if (!hBitmap)
        {
            /* Restore on bitmap failure */
            COLS = old_cols;
            LINES = old_lines;

            if (stdscr)
            {
                if (stdscr->cells)
                    free(stdscr->cells);

                stdscr->cells = (Cell *)malloc(sizeof(Cell) * LINES * COLS);

                if (stdscr->cells)
                {
                    stdscr->_maxy = LINES;
                    stdscr->_maxx = COLS;

                    clear_cells(stdscr);
                }
                else
                {
                    /* Both allocs failed: mark stdscr invalid */
                    stdscr->_maxy = 0;
                    stdscr->_maxx = 0;
                }
            }

            return ERR;
        }

        SelectObject(hMemDC, hBitmap);

        rect.left = 0;
        rect.top = 0;
        rect.right = COLS * fw;
        rect.bottom = LINES * fh;
        hBrush = CreateSolidBrush(RGB(0, 0, 0));

        FillRect(hMemDC, &rect, hBrush);
        DeleteObject(hBrush);
    }

    return OK;
}

/* Set font name (call before initscr) */
int win32_set_font_name(const char *font_name)
{
    const char *src = (font_name && font_name[0]) ? font_name : "Consolas";

    strncpy(win_font_name, src, WIN_FONT_NAME_MAX - 1);
    win_font_name[WIN_FONT_NAME_MAX - 1] = '\0';

    return 0;
}

/* Set cursor border color (0..15); returns previous value */
int win32_set_cursor_pen(int color)
{
    int old = s_cursor_color;

    if (color < 0)
        color = 0;

    if (color > 15)
        color = 15;

    s_cursor_color = color;

    return old;
}

/* Toggle ANSI art mode (CP437 art): every cell is 1 column wide */
void win32_set_ansi_mode(int use_ansi)
{
    s_ansi_mode = use_ansi ? 1 : 0;
    win32_force_redraw();
}

/* Drain the Windows message queue before showing a popup */
void win32_drain_messages(void)
{
    MSG msg;

    while (PeekMessage(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
        ;
    while (PeekMessage(&msg, NULL, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE))
        ;
}

/* Copy a rectangle of cells from src to dst */
int copywin(const WINDOW *src, WINDOW *dst, int sminrow, int smincol, int dminrow, int dmincol, int dmaxrow, int dmaxcol, int overlay)
{
    int drow, dcol;

    if (!src || !dst || !src->cells || !dst->cells)
        return ERR;

    for (drow = dminrow; drow <= dmaxrow; drow++)
    {
        int srow = sminrow + (drow - dminrow);

        if (drow < 0 || drow >= dst->_maxy)
            continue;

        if (srow < 0 || srow >= src->_maxy)
            continue;

        for (dcol = dmincol; dcol <= dmaxcol; dcol++)
        {
            int scol = smincol + (dcol - dmincol);
            Cell *sc, *dc;

            if (dcol < 0 || dcol >= dst->_maxx)
                continue;

            if (scol < 0 || scol >= src->_maxx)
                continue;

            sc = &src->cells[srow * src->_maxx + scol];
            dc = &dst->cells[drow * dst->_maxx + dcol];

            /* Overlay: skip blank src cells */
            if (overlay && (sc->ch == ' ' || sc->ch == 0))
                continue;

            *dc = *sc;
        }
    }

    return OK;
}

#endif /* PLATFORM_WIN32 */