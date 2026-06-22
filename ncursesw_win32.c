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

/* Ungetch buffer */
static int s_ungetch = ERR;

/* Font configuration */
#define WIN_FONT_NAME_MAX 256
static char win_font_name[WIN_FONT_NAME_MAX] = "Consolas";

/* Cursor border color (0..15) */
static int s_cursor_color = COLOR_WHITE;

/* Shadow buffer for dirty-cell optimization */
static Cell *s_shadow = NULL;
static int s_shadow_w = 0, s_shadow_h = 0;
static int s_shadow_dirty = 1;

#define SHADOW_BLEED_CELLS 4
#define SHADOW_DIRTY_MAX_COLS 1024
static unsigned char s_dirty_row[SHADOW_DIRTY_MAX_COLS];
static unsigned char s_dirty_tmp[SHADOW_DIRTY_MAX_COLS];

/* Last rendered cursor position (for erase-on-move) */
static int s_last_cur_y = -1, s_last_cur_x = -1;

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
static int compute_dirty_row(int r);

/* Return 1 if cp is 2-cell-wide glyph (East Asian Wide/Fullwidth + emoji). MSVCRT's wcswidth() is unreliable, so we use table-driven check */
static int is_wide_cp(unsigned int cp)
{
    /* Combining marks and control: not wide */
    if (cp < 0x1100)
        return 0;

    /* Ranges for real-world text and emoji. Others not wide; dirty propagation handles overflow */
    if (cp >= 0x1100 && cp <= 0x115F)
        return 1; /* Hangul Jamo */

    if (cp >= 0x2190 && cp <= 0x21FF)
        return 1; /* Arrows */

    if (cp >= 0x2329 && cp <= 0x232A)
        return 1; /* Angle brackets */

    if (cp >= 0x2500 && cp <= 0x259F)
        return 1; /* Box Drawing, Block Elements */

    if (cp >= 0x25A0 && cp <= 0x25FF)
        return 1; /* Geometric Shapes */

    if (cp >= 0x2600 && cp <= 0x26FF)
        return 1; /* Miscellaneous Symbols */

    if (cp >= 0x2700 && cp <= 0x27BF)
        return 1; /* Dingbats */

    if (cp >= 0x2B00 && cp <= 0x2BFF)
        return 1; /* Miscellaneous Symbols and Arrows */

    if (cp >= 0x2E80 && cp <= 0x303E)
        return 1; /* CJK Radicals  */

    if (cp >= 0x3041 && cp <= 0x33FF)
        return 1; /* Hiragana, Katakana, CJK Symbols */

    if (cp >= 0x3400 && cp <= 0x4DBF)
        return 1; /* CJK Ext A */

    if (cp >= 0x4E00 && cp <= 0x9FFF)
        return 1; /* CJK Unified */

    if (cp >= 0xA000 && cp <= 0xA4CF)
        return 1; /* Yi */

    if (cp >= 0xAC00 && cp <= 0xD7A3)
        return 1; /* Hangul Syllables */

    if (cp >= 0xF900 && cp <= 0xFAFF)
        return 1; /* CJK Compat */

    if (cp >= 0xFE30 && cp <= 0xFE4F)
        return 1; /* CJK Compat Forms */

    if (cp >= 0xFF00 && cp <= 0xFF60)
        return 1; /* Fullwidth ASCII */

    if (cp >= 0xFFE0 && cp <= 0xFFE6)
        return 1; /* Fullwidth signs */

    if (cp >= 0x1F000 && cp <= 0x1F02F)
        return 1; /* Mahjong */

    if (cp >= 0x1F0A0 && cp <= 0x1F0FF)
        return 1; /* Playing cards */

    if (cp >= 0x1F100 && cp <= 0x1F64F)
        return 1; /* Enclosed alphanumerics, emoticons */

    if (cp >= 0x1F680 && cp <= 0x1F6FF)
        return 1; /* Transport */

    if (cp >= 0x1F700 && cp <= 0x1F77F)
        return 1; /* Alchemical */

    if (cp >= 0x1F780 && cp <= 0x1F7FF)
        return 1; /* Geometric shapes ext */

    if (cp >= 0x1F800 && cp <= 0x1F8FF)
        return 1; /* Supplemental arrows-C */

    if (cp >= 0x1F900 && cp <= 0x1F9FF)
        return 1; /* Supplemental symbols and pictographs */

    if (cp >= 0x1FA00 && cp <= 0x1FA6F)
        return 1; /* Chess */

    if (cp >= 0x1FA70 && cp <= 0x1FAFF)
        return 1; /* Symbols extended-A */

    if (cp >= 0x20000 && cp <= 0x2FFFD)
        return 1; /* CJK Ext B..F */

    if (cp >= 0x30000 && cp <= 0x3FFFD)
        return 1; /* CJK Ext G */

    return 0;
}

/* Return 1 if cp needs full-row context to render correctly (Arabic, Hebrew, Indic shaping) */
static int is_complex_cp(unsigned int cp)
{
    /* Hebrew */
    if (cp >= 0x0590 && cp <= 0x05FF)
        return 1;

    /* Arabic and related scripts */
    if (cp >= 0x0600 && cp <= 0x08FF)
        return 1;

    /* Indic scripts (Devanagari, Bengali, Tamil, Thai, Tibetan, ...) */
    if (cp >= 0x0900 && cp <= 0x0FFF)
        return 1;

    /* Myanmar */
    if (cp >= 0x1000 && cp <= 0x109F)
        return 1;

    /* Khmer, Mongolian and related scripts */
    if (cp >= 0x1780 && cp <= 0x1AAF)
        return 1;

    /* Hebrew presentation forms */
    if (cp >= 0xFB1D && cp <= 0xFB4F)
        return 1;

    /* Arabic presentation forms */
    if (cp >= 0xFB50 && cp <= 0xFDFF)
        return 1;

    if (cp >= 0xFE70 && cp <= 0xFEFF)
        return 1;

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

/* Draw glyph centered in a cell_w-pixel-wide slot; glyph overflows right if wider than slot */
static void draw_glyph_in_cell(int cx, int cy, int cell_w, const wchar_t *buf, int len)
{
    SIZE sz;
    int draw_x = cx;

    if (GetTextExtentPoint32W(hMemDC, buf, len, &sz))
    {
        if (sz.cx > 0 && sz.cx < cell_w)
            draw_x = cx + (cell_w - sz.cx) / 2;
    }

    TextOutW(hMemDC, draw_x, cy, buf, len);
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

            /* Alt+letter -> KEY_ALT */
            if ((GetKeyState(VK_MENU) & 0x8000) && ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z')))
            {
                /* normalize to uppercase */
                if (key >= 'a' && key <= 'z')
                    key = key - 'a' + 'A';

                key = KEY_ALT(key);
                is_control_key = 1;
            }
            else
            {
                /* map arrow and function keys */
                switch (key)
                {
                case VK_UP:
                    key = KEY_UP;
                    is_control_key = 1;
                    break;
                case VK_DOWN:
                    key = KEY_DOWN;
                    is_control_key = 1;
                    break;
                case VK_LEFT:
                    if (GetKeyState(VK_CONTROL) & 0x8000)
                    {
                        key = KEY_CLEFT;
                        is_control_key = 1;
                    }
                    else
                    {
                        key = KEY_LEFT;
                        is_control_key = 1;
                    }

                    break;
                case VK_RIGHT:
                    if (GetKeyState(VK_CONTROL) & 0x8000)
                    {
                        key = KEY_CRIGHT;
                        is_control_key = 1;
                    }
                    else
                    {
                        key = KEY_RIGHT;
                        is_control_key = 1;
                    }

                    break;
                case VK_HOME:
                    key = KEY_HOME;
                    is_control_key = 1;
                    break;
                case VK_END:
                    key = KEY_END;
                    is_control_key = 1;
                    break;
                case VK_PRIOR:
                    key = KEY_PPAGE;
                    is_control_key = 1;
                    break;
                case VK_NEXT:
                    key = KEY_NPAGE;
                    is_control_key = 1;
                    break;
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

        /* ignore minimize */
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

        /* allocate new cell buffer */
        new_cells = (Cell *)malloc(sizeof(Cell) * new_lines * new_cols);

        if (!new_cells)
        {
            /* keep old buffer on failure */
            return 0;
        }

        /* copy old content, fill new area with spaces */
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

        /* recreate backing bitmap */
        if (hBitmap)
            DeleteObject(hBitmap);

        hBitmap = CreateCompatibleBitmap(hDC, COLS * fw, LINES * fh);

        if (!hBitmap)
        {
            /* restore on failure */
            free(new_cells);
            stdscr->cells = old_cells;
            stdscr->_maxy = old_lines;
            stdscr->_maxx = old_cols;
            LINES = old_lines;
            COLS = old_cols;
            return 0;
        }

        /* free old buffer only after bitmap succeeds */
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

        s_shadow_dirty = 1;
        render_all();
        hdc = GetDC(hWnd);

        if (hdc && hMemDC && hBitmap)
        {
            BitBlt(hdc, 0, 0, COLS * fw, LINES * fh, hMemDC, 0, 0, SRCCOPY);
            ReleaseDC(hWnd, hdc);
        }

        /* notify application of resize */
        if (s_key_count < 16)
            s_key_buf[s_key_count++] = KEY_RESIZE;
    }
        return 0;

    case WM_CLOSE:
        /* send ESC so the app can confirm quit */
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
}

/* Render a single cell to the memory DC */
static void render_cell(int row, int col, chtype ch, attr_t attrs)
{
    RECT rect;
    HBRUSH hBrush;
    wchar_t buf[2];
    int pair;
    int cell_w = fw; /* width of the area to repaint -- fw or 2*fw */
    chtype draw_ch = ch;

    if (!hMemDC || row < 0 || row >= LINES || col < 0 || col >= COLS)
        return;

    /* trailing cell belongs to the lead glyph, nothing to draw */
    if (ch == WIN32_CELL_WIDE_TRAILING)
        return;

    /* wide glyph: allocate 2 cells */
    if (stdscr && stdscr->cells)
    {
        if (is_wide_cp((unsigned int)ch))
            cell_w = 2 * fw;
    }

    pair = (int)((attrs & A_COLOR) >> 8);
    apply_colors(pair, attrs);

    /* clear background over 1 or 2 cells */
    rect.left = col * fw;
    rect.top = row * fh;
    rect.right = rect.left + cell_w;
    rect.bottom = rect.top + fh;

    hBrush = CreateSolidBrush(s_rgb_map[s_cur_bg_idx]);
    FillRect(hMemDC, &rect, hBrush);
    DeleteObject(hBrush);

    /* render glyph centered in cell, surrogate-pair aware */
    if (draw_ch >= 0x20)
    {
        int wlen = cp_to_utf16((unsigned int)draw_ch, buf);
        draw_glyph_in_cell(rect.left, rect.top, cell_w, buf, wlen);
    }
}

/* Force complete redraw (call after font change) */
void win32_force_redraw(void)
{
    s_shadow_dirty = 1;
    render_all();
}

/* Fill s_dirty_row[] for row r; returns 1 if anything needs redrawing */
static int compute_dirty_row(int r)
{
    int c;
    int any = 0;
    int cols = COLS;
    int countdown = 0;
    int has_wide;

    if (cols > SHADOW_DIRTY_MAX_COLS)
        cols = SHADOW_DIRTY_MAX_COLS;

    /* diff against shadow buffer */
    if (!s_shadow || s_shadow_dirty)
    {
        for (c = 0; c < cols; c++)
            s_dirty_tmp[c] = 1;

        any = (cols > 0);
    }
    else
    {
        Cell *row_sh = &s_shadow[r * COLS];

        for (c = 0; c < cols; c++)
        {
            Cell *cc = CELL(stdscr, r, c);
            int d = (cc->ch != row_sh[c].ch) || (cc->attrs != row_sh[c].attrs);

            s_dirty_tmp[c] = (unsigned char)d;
            any |= d;
        }
    }

    if (!any)
    {
        for (c = 0; c < cols; c++)
            s_dirty_row[c] = 0;

        return 0;
    }

    /* wide or complex-script row: force full-row repaint for correct shaping */
    has_wide = 0;

    for (c = 0; c < cols; c++)
    {
        Cell *cc = CELL(stdscr, r, c);
        ULONG ch = (ULONG)cc->ch;

        if (ch != WIN32_CELL_WIDE_TRAILING)
        {
            if (is_wide_cp((unsigned int)ch) || is_complex_cp((unsigned int)ch))
            {
                has_wide = 1;
                break;
            }
        }

        /* these ranges are also caught by is_wide_cp but kept here for safety */
        if ((ch >= 0x2190 && ch <= 0x21FF) ||
            (ch >= 0x2500 && ch <= 0x259F) ||
            (ch >= 0x25A0 && ch <= 0x25FF) ||
            (ch >= 0x2600 && ch <= 0x26FF) ||
            (ch >= 0x2700 && ch <= 0x27BF) ||
            (ch >= 0x2B00 && ch <= 0x2BFF))
        {
            has_wide = 1;
            break;
        }
    }

    if (has_wide)
    {
        for (c = 0; c < cols; c++)
            s_dirty_row[c] = 1;

        return 1;
    }

    /* bleed dirty region by ±SHADOW_BLEED_CELLS to cover subpixel overflow */
    for (c = 0; c < cols; c++)
    {
        if (s_dirty_tmp[c])
            countdown = SHADOW_BLEED_CELLS + 1;

        s_dirty_row[c] = (unsigned char)(countdown > 0);

        if (countdown > 0)
            countdown--;
    }

    countdown = 0;

    for (c = cols - 1; c >= 0; c--)
    {
        if (s_dirty_tmp[c])
            countdown = SHADOW_BLEED_CELLS + 1;

        if (countdown > 0)
        {
            s_dirty_row[c] = 1;
            countdown--;
        }
    }

    return 1;
}

/* Render all dirty cells to hMemDC, then blit to window */
static void render_all(void)
{
    int r, c;
    Cell *cell;
    int last_pair = -1;
    int last_attrs = -1;
    char *text_buf = NULL;
    int cy_cell, cx_cell, prev_y, prev_x;

    if (!stdscr || !stdscr->cells || !hMemDC)
        return;

    /* reallocate shadow buffer if screen size changed */
    if (!s_shadow || s_shadow_w != COLS || s_shadow_h != LINES)
    {
        if (s_shadow)
            free(s_shadow);

        s_shadow = (Cell *)calloc((size_t)(LINES * COLS), sizeof(Cell));

        if (!s_shadow)
        {
            /* no shadow: full redraw every frame */
            s_shadow_w = 0;
            s_shadow_h = 0;
            s_shadow_dirty = 1;
        }
        else
        {
            s_shadow_w = COLS;
            s_shadow_h = LINES;
            s_shadow_dirty = 1;
        }
    }

    /* temp wchar_t buffer for batched TextOutW */
    text_buf = (char *)malloc((COLS + 1) * sizeof(wchar_t));

    if (!text_buf)
        return;

    for (r = 0; r < LINES; r++)
    {
        /* skip clean rows */
        if (!compute_dirty_row(r))
            continue;

        c = 0;

        while (c < COLS)
        {
            int run_start, run_pair, run_attrs;
            int run_len;
            int run_has_wide;
            RECT rect;
            HBRUSH hBrush;

            cell = CELL(stdscr, r, c);

            /* skip clean cell */
            if (!s_dirty_row[c])
            {
                c++;
                continue;
            }

            /* start a same-color run */
            run_start = c;
            run_pair = (cell->attrs & A_COLOR) >> 8;
            run_attrs = cell->attrs;
            run_len = 0;
            run_has_wide = 0;

            while (c < COLS && run_len < COLS)
            {
                Cell *cc;
                int p;
                wchar_t wc;
                int is_wide; /* unused in accumulation loop but declared for scope */

                cc = CELL(stdscr, r, c);

                if (!s_dirty_row[c])
                    break;

                p = (cc->attrs & A_COLOR) >> 8;

                /* break run on color or reverse-video change */
                if (p != run_pair || ((cc->attrs ^ run_attrs) & A_REVERSE))
                    break;

                /* trailing cell: count it for FillRect area but write no glyph */
                if (cc->ch == WIN32_CELL_WIDE_TRAILING)
                {
                    if (s_shadow)
                        s_shadow[r * COLS + c] = *cc;

                    run_len++; /* area count only */
                    c++;

                    continue;
                }

                /* codepoints >= U+10000 need surrogate pairs -> per-cell path */
                if (cc->ch >= 0x10000)
                {
                    run_has_wide = 1;
                    break;
                }

                wc = (wchar_t)cc->ch;
                ((wchar_t *)text_buf)[run_len++] = wc;

                if (s_shadow)
                    s_shadow[r * COLS + c] = *cc;

                c++;
            }

            if (run_len <= 0)
            {
                c = run_start + 1;
                continue;
            }

            /* check for trailing/wide cells; force per-cell render if found */
            if (!run_has_wide)
            {
                int u2;
                Cell *cc2;

                for (u2 = 0; u2 < run_len; u2++)
                {
                    cc2 = CELL(stdscr, r, run_start + u2);

                    if (cc2->ch == WIN32_CELL_WIDE_TRAILING)
                    {
                        run_has_wide = 1;
                        break;
                    }
                }
            }

            if (run_pair != last_pair || run_attrs != last_attrs)
            {
                apply_colors(run_pair, run_attrs);
                last_pair = run_pair;
                last_attrs = run_attrs;
            }

            if (run_has_wide)
            {
                /* per-cell render: each glyph pinned to its cell column */
                int u;
                int actual_pos = run_start;
                Cell *cc;
                wchar_t wc;
                int cx;
                int cy;
                int is_wide;

                for (u = 0; u < run_len; u++)
                {
                    cc = CELL(stdscr, r, actual_pos);
                    wc = (wchar_t)cc->ch;
                    cx = actual_pos * fw;
                    cy = r * fh;

                    /* trailing: no glyph */
                    if (wc == WIN32_CELL_WIDE_TRAILING)
                    {
                        actual_pos++;
                        continue;
                    }

                    /* wide glyph: 2-cell allocation */
                    is_wide = is_wide_cp((unsigned int)wc);

                    /* clear background (1 or 2 cells) */
                    rect.left = cx;
                    rect.top = cy;
                    rect.right = cx + (is_wide ? 2 * fw : fw);
                    rect.bottom = cy + fh;

                    hBrush = CreateSolidBrush(s_rgb_map[s_cur_bg_idx]);
                    FillRect(hMemDC, &rect, hBrush);
                    DeleteObject(hBrush);

                    /* render glyph centered in cell */
                    if ((unsigned int)cc->ch >= 0x20)
                    {
                        wchar_t buf[2];
                        int wlen = cp_to_utf16((unsigned int)cc->ch, buf);
                        int cell_w_pixels = is_wide ? 2 * fw : fw;

                        draw_glyph_in_cell(cx, cy, cell_w_pixels, buf, wlen);
                    }

                    /* advance past trailing cell for wide glyphs */
                    if (is_wide)
                        actual_pos += 2;
                    else
                        actual_pos++;
                }
            }
            else
            {
                /* batched TextOutW for normal (narrow BMP) runs */
                rect.left = run_start * fw;
                rect.top = r * fh;
                rect.right = rect.left + run_len * fw;
                rect.bottom = rect.top + fh;

                hBrush = CreateSolidBrush(s_rgb_map[s_cur_bg_idx]);
                FillRect(hMemDC, &rect, hBrush);
                DeleteObject(hBrush);

                /* draw the run */
                TextOutW(hMemDC, rect.left, rect.top, (LPCWSTR)text_buf, run_len);
            }
        }

        /* reset color tracking per row */
        last_pair = -1;
        last_attrs = -1;
    }

    free(text_buf);
    s_shadow_dirty = 0;

    /* draw cursor as a rectangle border */
    cy_cell = stdscr ? stdscr->_cury : -1;
    cx_cell = stdscr ? stdscr->_curx : -1;
    prev_y = s_last_cur_y;
    prev_x = s_last_cur_x;

    /* erase old cursor border */
    if (s_shadow && prev_y >= 0 && prev_x >= 0 && prev_y < LINES && prev_x < COLS && (prev_y != cy_cell || prev_x != cx_cell))
    {
        Cell *cell = CELL(stdscr, prev_y, prev_x);
        int sync_lead = prev_x;
        int row_has_complex = 0;
        int cc_col;

        /* skip bleed repaint on complex-script rows (main loop already did full-row repaint) */
        for (cc_col = 0; cc_col < COLS; cc_col++)
        {
            Cell *cb = CELL(stdscr, prev_y, cc_col);
            ULONG ch = (ULONG)cb->ch;

            if (ch != WIN32_CELL_WIDE_TRAILING && is_complex_cp((unsigned int)ch))
            {
                row_has_complex = 1;
                break;
            }
        }

        if (!row_has_complex)
        {
            /* repaint bleed window around old cursor to remove ClearType residue */
            int b;
            int bleed_lo = prev_x - SHADOW_BLEED_CELLS;
            int bleed_hi = prev_x + SHADOW_BLEED_CELLS;

            if (bleed_lo < 0)
                bleed_lo = 0;

            if (bleed_hi >= COLS)
                bleed_hi = COLS - 1;

            for (b = bleed_lo; b <= bleed_hi; b++)
            {
                Cell *cb = CELL(stdscr, prev_y, b);

                if (cb->ch == WIN32_CELL_WIDE_TRAILING && b > 0)
                {
                    /* render from lead: trailing alone is a no-op in render_cell */
                    Cell *lead = CELL(stdscr, prev_y, b - 1);

                    render_cell(prev_y, b - 1, lead->ch, lead->attrs);
                }
                else
                {
                    render_cell(prev_y, b, cb->ch, cb->attrs);
                }
            }
        }

        /* if prev cursor was on a trailing cell, sync the lead */
        if (cell->ch == WIN32_CELL_WIDE_TRAILING && prev_x > 0)
            sync_lead = prev_x - 1;

        s_shadow[prev_y * COLS + sync_lead] = *CELL(stdscr, prev_y, sync_lead);

        /* also resync the trailing companion */
        if (sync_lead + 1 < COLS)
        {
            Cell *trail = CELL(stdscr, prev_y, sync_lead + 1);

            if (trail->ch == WIN32_CELL_WIDE_TRAILING)
                s_shadow[prev_y * COLS + sync_lead + 1] = *trail;
        }
    }

    /* draw cursor outline */
    if (s_cursor_vis && cy_cell >= 0 && cy_cell < LINES && cx_cell >= 0 && cx_cell < COLS)
    {
        HPEN hPen, oldPen;
        int cx, cy;
        int cursor_w = fw;
        int draw_x_cell = cx_cell;
        int trailing_x_cell = -1; /* -1 = none */

        /* cursor width: 2*fw on wide glyph, fw otherwise */
        if (stdscr && stdscr->cells)
        {
            Cell *cur = CELL(stdscr, cy_cell, cx_cell);

            if (cur->ch == WIN32_CELL_WIDE_TRAILING && cx_cell > 0)
            {
                draw_x_cell = cx_cell - 1;
                trailing_x_cell = cx_cell;
                cursor_w = 2 * fw;
            }
            else if (cx_cell + 1 < COLS)
            {
                Cell *nxt = CELL(stdscr, cy_cell, cx_cell + 1);

                if (nxt->ch == WIN32_CELL_WIDE_TRAILING)
                {
                    trailing_x_cell = cx_cell + 1;
                    cursor_w = 2 * fw;
                }
            }
        }

        cx = draw_x_cell * fw;
        cy = cy_cell * fh;

        hPen = CreatePen(PS_SOLID, 1, s_rgb_map[s_cursor_color]);
        oldPen = SelectObject(hMemDC, hPen);

        /* four sides of the cursor rectangle */
        MoveToEx(hMemDC, cx, cy, NULL);

        LineTo(hMemDC, cx + cursor_w - 1, cy);
        MoveToEx(hMemDC, cx, cy + fh - 1, NULL);
        LineTo(hMemDC, cx + cursor_w - 1, cy + fh - 1);

        MoveToEx(hMemDC, cx, cy, NULL);
        LineTo(hMemDC, cx, cy + fh - 1);

        MoveToEx(hMemDC, cx + cursor_w - 1, cy, NULL);
        LineTo(hMemDC, cx + cursor_w - 1, cy + fh - 1);

        SelectObject(hMemDC, oldPen);
        DeleteObject(hPen);

        /* dirty the cursor cells so next render repaints under it */
        if (s_shadow)
        {
            s_shadow[cy_cell * COLS + draw_x_cell].ch ^= 0x10000;

            if (cursor_w > fw && draw_x_cell + 1 < COLS)
                s_shadow[cy_cell * COLS + draw_x_cell + 1].ch ^= 0x10000;
        }

        s_last_cur_y = cy_cell;
        s_last_cur_x = cx_cell;
    }
    else
    {
        s_last_cur_y = -1;
        s_last_cur_x = -1;
    }

    /* blit to window */
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
        if (msg.message == WM_QUIT)
        {
            /* Signal to exit */
            s_ungetch = 27; /* ESC */
        }

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
        return NULL;

    /* initial size */
    COLS = 80;
    LINES = 25;
    fw = 8;
    fh = 16;
    fb = 12;

    /* center on screen */
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

    /* device contexts */
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

    /* clear bitmap to black */
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

    /* read back actual font metrics */
    if (GetTextMetrics(hMemDC, &tm))
    {
        fw = tm.tmAveCharWidth;
        fh = tm.tmHeight;
        fb = tm.tmAscent;
    }

    SetBkMode(hMemDC, OPAQUE);
    SetTextColor(hMemDC, RGB(255, 255, 255));
    SetBkColor(hMemDC, RGB(0, 0, 0));

    /* show window */
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    /* drain initial messages */
    pump_messages();

    /* allocate stdscr */
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

    /* default color pair */
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

    if (s_shadow)
    {
        free(s_shadow);
        s_shadow = NULL;
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
        /* copy to stdscr */
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
    wchar_t wc;
    int cw;

    if (!win)
        return ERR;

    r = win->_cury;
    c = win->_curx;

    if (r < 0 || r >= win->_maxy || c < 0 || c >= win->_maxx)
        return ERR;

    ch_out = ch & A_CHARTEXT;
    attrs = (ch & A_ATTRIBUTES) | win->attrs;
    attrs &= ~A_COLOR;
    attrs |= COLOR_PAIR(win->color_pair);

    /* Case A: writing on TRAILING -> orphan the lead */
    if (c > 0)
    {
        Cell *cur = CELL(win, r, c);

        if (cur->ch == WIN32_CELL_WIDE_TRAILING)
        {
            Cell *prev = CELL(win, r, c - 1);
            prev->ch = ' ';
        }
    }

    wc = (wchar_t)ch_out;
    cw = is_wide_cp((unsigned int)ch_out) ? 2 : 1;

    /* Case B: narrow into lead -> orphan the trailing */
    if (cw == 1 && c + 1 < win->_maxx)
    {
        Cell *nxt = CELL(win, r, c + 1);

        if (nxt->ch == WIN32_CELL_WIDE_TRAILING)
        {
            nxt->ch = ' ';
            nxt->attrs = attrs;
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
        }
    }

    /* write lead cell */
    CELL(win, r, c)->ch = ch_out;
    CELL(win, r, c)->attrs = attrs;
    win->_curx++;

    /* write trailing cell for wide glyphs */
    if (cw == 2)
    {
        Cell *trail = CELL(win, r, win->_curx);

        trail->ch = WIN32_CELL_WIDE_TRAILING;
        trail->attrs = attrs;
        win->_curx++;
    }

    if (win->_curx >= win->_maxx)
    {
        win->_curx = 0;
        win->_cury++;

        /* scroll if at bottom and scrollok set */
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

/* Decode one UTF-8 sequence; returns 0 on success, -1 on error (fallback to Latin-1) */
static int utf8_decode_one(const unsigned char *p, int max, wchar_t *out, int *consumed)
{
    unsigned char b0;

    if (max <= 0)
    {
        *out = 0;
        *consumed = 0;
        return -1;
    }

    b0 = p[0];

    /* ASCII */
    if (b0 < 0x80)
    {
        *out = (wchar_t)b0;
        *consumed = 1;
        return 0;
    }

    /* 2-byte */
    if ((b0 & 0xE0) == 0xC0 && max >= 2 && (p[1] & 0xC0) == 0x80)
    {
        *out = (wchar_t)(((b0 & 0x1F) << 6) | (p[1] & 0x3F));
        *consumed = 2;
        return 0;
    }

    /* 3-byte */
    if ((b0 & 0xF0) == 0xE0 && max >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
    {
        *out = (wchar_t)(((b0 & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
        *consumed = 3;
        return 0;
    }

    /* 4-byte: wchar_t is 16-bit on Windows, replace with U+FFFD */
    if ((b0 & 0xF8) == 0xF0 && max >= 4 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80)
    {
        *out = (wchar_t)0xFFFD;
        *consumed = 4;
        return 0;
    }

    /* malformed: emit as Latin-1 */
    *out = (wchar_t)b0;
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
        wchar_t wc;
        int consumed = 1;

        utf8_decode_one((const unsigned char *)&str[i], n - i, &wc, &consumed);

        if (consumed <= 0)
            consumed = 1; /* always make progress */

        if (waddch(win, (chtype)wc) == ERR)
            return ERR;

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
        if (waddch(win, (chtype)wstr[i]) == ERR)
            return ERR;
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

    win->attrs |= (attr_t)attrs;

    /* extract and set color pair if A_COLOR is present */
    if (attrs & A_COLOR)
    {
        int new_pair = (int)((attrs & A_COLOR) >> 8);

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

    /* extract color pair */
    if (attrs & A_COLOR)
        win->color_pair = (int)((attrs & A_COLOR) >> 8);
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

    /* default pair 0 */
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

    /* check ungetch buffer first */
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

    /* wait for a key */
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
            /* blocking: wait for message */
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

    /* shift buffer */
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

unsigned long getmouse(void)
{
    return s_mouse_event;
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

int beep(void)
{
    MessageBeep(MB_OK);
    return OK;
}

int flash(void)
{
    return beep();
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
            }
        }

        for (r = win->_maxy - n; r < win->_maxy; r++)
        {
            for (c = 0; c < win->_maxx; c++)
            {
                CELL(win, r, c)->ch = ' ';
                CELL(win, r, c)->attrs = win->attrs;
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
            }
        }

        for (r = 0; r < -n; r++)
        {
            for (c = 0; c < win->_maxx; c++)
            {
                CELL(win, r, c)->ch = ' ';
                CELL(win, r, c)->attrs = win->attrs;
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
            /* restore on alloc failure */
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
            /* restore on bitmap failure */
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
                    /* both allocs failed: mark stdscr invalid */
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

            /* overlay: skip blank src cells */
            if (overlay && (sc->ch == ' ' || sc->ch == 0))
                continue;

            *dc = *sc;
        }
    }

    return OK;
}

#endif /* PLATFORM_WIN32 */
