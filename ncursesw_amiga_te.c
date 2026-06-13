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

/* ncursesw_amiga_te.c -- ncursesw implementation for AmigaOS 3 */
#ifdef PLATFORM_AMIGA

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wrapper.h"
#include "core/utf8.h"

#include <devices/inputevent.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/keymap.h>

#include <proto/diskfont.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>

#include "ncursesw_amiga.h"

#include "te_rastport.h"
#include "te_rastport.h"

/* Global state */

WINDOW *stdscr = NULL;
WINDOW *curscr = NULL;
int LINES = 25;
int COLS = 80;
int COLOR_PAIRS = 256;
int COLORS = 8;

static struct Window *ami_win = NULL;
static struct Screen *ami_scr = NULL;
static struct RastPort *ami_rp = NULL;
static struct TextFont *ami_font = NULL;
static struct TextFont *ami_font_normal = NULL;
static struct TextFont *ami_font_ansi = NULL;

#define AMI_FONT_NAME_MAX 256
static char ami_font_name[AMI_FONT_NAME_MAX] = "topaz.font";
static char ami_ansi_font_name[AMI_FONT_NAME_MAX] = "topaz.font";
static int fw = 8, fh = 8, fb = 7; /* font width, height, baseline */
static int bx = 0, by = 0;         /* border offsets */

/* DrawContext — holds loaded font chain + glyph cache */
static struct TERenderContext *ami_te_dc = NULL;

/* Screen pointer kept for TE_SetColorPen / TE_UpdatePalette */
static struct Screen *ami_te_screen = NULL;

/* Runtime flag — 1 = render via TE_RenderText(), 0 = render via Text() */
static int s_use_ttf = 0;

/* Configuration set from main BEFORE initscr() */
static char s_ttf_file[512] = {0};
static int s_ttf_size = 14;
static int s_ttf_antialias = 0;    /* 0=off (MONO), 1=on (GRAY antialias) -- internal */
static int s_ttf_aa_auto = 1;      /* 1 = decide AA at apply-time based on screen depth */
static int s_ttf_proportional = 0; /* 1 = render per-cell (proportional font detected) */
static int s_ansi_mode = 0;        /* ANSI mode: draw block glyphs via RectFill for pixel-perfect tiling */
static int s_ttf_use_utf8 = 1;     /* Uses UTF-8 natively */

/* Fallback TTF fonts populated by main via amiga_add_ttf_fallback() before initscr() */
#define AMI_TTF_FALLBACKS 8
static char s_ttf_fallback_file[AMI_TTF_FALLBACKS][512];
static int s_ttf_fallback_size[AMI_TTF_FALLBACKS];
static int s_ttf_fallback_count = 0;

static int s_cursor_vis = 1;
static int s_colors_on = 0;
static int s_raw_mode = 0;
static int s_echo_mode = 0;
static int s_keypad_mode = 1;
static int s_nodelay_mode = 0;

/* Cursor outline pen (configurable via amiga_set_cursor_pen()) */
static UBYTE s_cursor_pen = 1;

/* Default fg/bg when pair == 0 or uninitialized (configurable via amiga_set_default_colors()) */
static short s_default_fg = COLOR_WHITE;
static short s_default_bg = 0; /* Updated dynamically from config */

/* Color pair table */
static short s_pair_fg[256];
static short s_pair_bg[256];
static int s_pair_ok[256];

/* Pushback for ungetch / unget_wch */
static int s_ungetch = ERR;
static wchar_t s_unget_wch_buf = 0;
static int s_unget_wch_valid = 0;

/* Pen mapping: ncurses color index -> Amiga pen */
static UBYTE s_pen[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

/* Default background color for COLOR_PAIR(0) (from config) */
static int s_default_bg_color = COLOR_BLACK; /* Gray in ncursesw */

/* Shadow buffer of last rendered cells to skip unchanged cells */
static Cell *s_shadow = NULL;
static int s_shadow_w = 0, s_shadow_h = 0;
static int s_shadow_dirty = 1; /* force full redraw on next render_all */

#define SHADOW_BLEED_CELLS 4
#define SHADOW_DIRTY_MAX_COLS 1024
static unsigned char s_dirty_row[SHADOW_DIRTY_MAX_COLS];
static unsigned char s_dirty_tmp[SHADOW_DIRTY_MAX_COLS];

static UBYTE s_key_queue[16];
static int s_key_queue_len = 0;
static int s_key_queue_pos = 0;

/* Cell helpers */
#define CELL(win, r, c) (&(win)->cells[(r) * (win)->_maxx + (c)])

/* Wide glyph trailing sentinel (U+FFFE) - occupies second cell of wide glyphs */
#define TE_CELL_WIDE_TRAILING 0x0000FFFEUL

/* wcswidth() from ui_editor_helper.c */
extern int wcswidth(const wchar_t *wcs, size_t n);

static int px(int col) { return bx + col * fw; }

static int py(int row) { return by + row * fh; }

/* Convert UTF-32 codepoint to UTF-8 strings */
static int utf32_to_utf8(uint32_t codepoint, char *buf)
{
    if (codepoint <= 0x7F)
    {
        buf[0] = (char)codepoint;
        return 1;
    }
    else if (codepoint <= 0x7FF)
    {
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    else if (codepoint <= 0xFFFF)
    {
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    else if (codepoint <= 0x10FFFF)
    {
        buf[0] = (char)(0xF0 | (codepoint >> 18));
        buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }

    /* Invalid codepoint - replace with replacement character */
    buf[0] = (char)0xEF;
    buf[1] = (char)0xBF;
    buf[2] = (char)0xBD;

    return 3;
}

/* Compute dirty bitmap for row r into s_dirty_row[0..COLS-1] */
static int compute_dirty_row(int r)
{
    int c;
    int any = 0;
    int cols = COLS;
    int countdown = 0;
    int has_wide = 0;

    if (cols > SHADOW_DIRTY_MAX_COLS)
        cols = SHADOW_DIRTY_MAX_COLS;

    /* Raw per-cell diff vs shadow + wide-glyph detection in single sweep */
    if (!s_shadow || s_shadow_dirty)
    {
        /* Forced full redraw: skip wide check */
        for (c = 0; c < cols; c++)
            s_dirty_tmp[c] = 1;

        any = (cols > 0);
        has_wide = 1; /* paint everything */
    }
    else
    {
        Cell *row_sh = &s_shadow[r * COLS];

        for (c = 0; c < cols; c++)
        {
            Cell *cc = CELL(stdscr, r, c);
            int d = (cc->ch != row_sh[c].ch) || (cc->attrs != row_sh[c].attrs);
            ULONG ch;

            s_dirty_tmp[c] = (unsigned char)d;
            any |= d;

            if (has_wide)
                continue;

            ch = (ULONG)cc->ch;

            if (ch == TE_CELL_WIDE_TRAILING ||
                (ch >= 0x2190 && ch <= 0x21FF) || /* Arrows         */
                (ch >= 0x2500 && ch <= 0x259F) || /* Box / Block    */
                (ch >= 0x25A0 && ch <= 0x25FF) || /* Geometric      */
                (ch >= 0x2600 && ch <= 0x26FF) || /* Misc symbols   */
                (ch >= 0x2700 && ch <= 0x27BF) || /* Dingbats       */
                (ch >= 0x2B00 && ch <= 0x2BFF))   /* Misc symbols 2 */
            {
                has_wide = 1;
            }
        }
    }

    if (!any)
    {
        for (c = 0; c < cols; c++)
            s_dirty_row[c] = 0;

        return 0;
    }

    if (has_wide)
    {
        for (c = 0; c < cols; c++)
            s_dirty_row[c] = 1;

        return 1;
    }

    /* Propagate dirty by ±SHADOW_BLEED_CELLS using running countdown (O(cols)) */
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

/* Apply color pair + attributes to RastPort */
static void apply_colors(int pair, int attrs)
{
    short fg_idx, bg_idx;
    UBYTE fg_pen, bg_pen;

    if (!s_colors_on || !ami_rp)
        return;

    fg_idx = (pair >= 0 && pair < COLOR_PAIRS && s_pair_ok[pair]) ? s_pair_fg[pair] : s_default_fg;
    bg_idx = (pair >= 0 && pair < COLOR_PAIRS && s_pair_ok[pair]) ? s_pair_bg[pair] : s_default_bg;

    if (fg_idx < 0 || fg_idx > 15)
        fg_idx = s_default_fg;

    if (bg_idx < 0 || bg_idx > 15)
        bg_idx = s_default_bg;

    if (attrs & A_REVERSE)
    {
        short tmp = fg_idx;
        fg_idx = bg_idx;
        bg_idx = tmp;
    }

    fg_pen = s_pen[fg_idx];
    bg_pen = s_pen[bg_idx];

    /* Always set both pens */
    SetAPen(ami_rp, fg_pen);
    SetBPen(ami_rp, bg_pen);

    if (ami_rp->DrawMode != JAM2)
        SetDrMd(ami_rp, JAM2);
}

/* Direct block-glyph rendering (U+2580..U+259F) - bypass font for pixel-perfect tiling in ANSI mode */
static int is_block_glyph(uint32_t cp)
{
    return (cp >= 0x2580 && cp <= 0x259F);
}

static void draw_block_glyph(uint32_t cp, int x, int y, int cw, int ch_, UBYTE fg)
{
    /* Standard Amiga area-fill patterns (16-pixel wide, 2-row repeating) */
    static UWORD pat_25[2] = {0x8888, 0x2222}; /* ░ light shade  */
    static UWORD pat_50[2] = {0x5555, 0xAAAA}; /* ▒ medium shade (checker) */
    static UWORD pat_75[2] = {0x7777, 0xDDDD}; /* ▓ dark shade   */
    UBYTE saved;

    int xm = x + cw / 2;  /* horizontal middle */
    int ym = y + ch_ / 2; /* vertical middle */
    int x1 = x + cw - 1;  /* right edge */
    int y1 = y + ch_ - 1; /* bottom edge */

    if (!ami_rp || cw < 2 || ch_ < 2)
        return;

    saved = ami_rp->FgPen;

    SetAPen(ami_rp, fg);

    switch (cp)
    {
    /* Half blocks */
    case 0x2580: /* ▀ UPPER HALF */
        RectFill(ami_rp, x, y, x1, ym - 1);
        break;
    case 0x2584: /* ▄ LOWER HALF */
        RectFill(ami_rp, x, ym, x1, y1);
        break;
    case 0x2588: /* █ FULL BLOCK */
        RectFill(ami_rp, x, y, x1, y1);
        break;
    case 0x258C: /* ▌ LEFT HALF */
        RectFill(ami_rp, x, y, xm - 1, y1);
        break;
    case 0x2590: /* ▐ RIGHT HALF */
        RectFill(ami_rp, xm, y, x1, y1);
        break;

    /* Lower-N-eighths blocks (▁ ▂ ▃ ▅ ▆ ▇) */
    case 0x2581:
        RectFill(ami_rp, x, y + ch_ * 7 / 8, x1, y1);
        break;
    case 0x2582:
        RectFill(ami_rp, x, y + ch_ * 6 / 8, x1, y1);
        break;
    case 0x2583:
        RectFill(ami_rp, x, y + ch_ * 5 / 8, x1, y1);
        break;
    case 0x2585:
        RectFill(ami_rp, x, y + ch_ * 3 / 8, x1, y1);
        break;
    case 0x2586:
        RectFill(ami_rp, x, y + ch_ * 2 / 8, x1, y1);
        break;
    case 0x2587:
        RectFill(ami_rp, x, y + ch_ * 1 / 8, x1, y1);
        break;

    /* Left-N-eighths blocks (▉ ▊ ▋ ▍ ▎ ▏) */
    case 0x2589:
        RectFill(ami_rp, x, y, x + cw * 7 / 8 - 1, y1);
        break;
    case 0x258A:
        RectFill(ami_rp, x, y, x + cw * 6 / 8 - 1, y1);
        break;
    case 0x258B:
        RectFill(ami_rp, x, y, x + cw * 5 / 8 - 1, y1);
        break;
    case 0x258D:
        RectFill(ami_rp, x, y, x + cw * 3 / 8 - 1, y1);
        break;
    case 0x258E:
        RectFill(ami_rp, x, y, x + cw * 2 / 8 - 1, y1);
        break;
    case 0x258F:
        RectFill(ami_rp, x, y, x + cw / 8 - 1, y1);
        break;

    /* One-eighth top and right (▔ ▕) */
    case 0x2594:
        RectFill(ami_rp, x, y, x1, y + ch_ / 8 - 1);
        break;
    case 0x2595:
        RectFill(ami_rp, x + cw * 7 / 8, y, x1, y1);
        break;

    /* Shaded blocks (░ ▒ ▓) — patterned fills */
    case 0x2591:
        SetAfPt(ami_rp, pat_25, 1);
        RectFill(ami_rp, x, y, x1, y1);
        SetAfPt(ami_rp, NULL, 0);
        break;
    case 0x2592:
        SetAfPt(ami_rp, pat_50, 1);
        RectFill(ami_rp, x, y, x1, y1);
        SetAfPt(ami_rp, NULL, 0);
        break;
    case 0x2593:
        SetAfPt(ami_rp, pat_75, 1);
        RectFill(ami_rp, x, y, x1, y1);
        SetAfPt(ami_rp, NULL, 0);
        break;

    /* Quadrants (▖ ▗ ▘ ▙ ▚ ▛ ▜ ▝ ▞ ▟) */
    case 0x2596: /* ▖ lower left */
        RectFill(ami_rp, x, ym, xm - 1, y1);
        break;
    case 0x2597: /* ▗ lower right */
        RectFill(ami_rp, xm, ym, x1, y1);
        break;
    case 0x2598: /* ▘ upper left */
        RectFill(ami_rp, x, y, xm - 1, ym - 1);
        break;
    case 0x2599: /* ▙ upper left + lower */
        RectFill(ami_rp, x, y, xm - 1, ym - 1);
        RectFill(ami_rp, x, ym, x1, y1);
        break;
    case 0x259A: /* ▚ upper left + lower right */
        RectFill(ami_rp, x, y, xm - 1, ym - 1);
        RectFill(ami_rp, xm, ym, x1, y1);
        break;
    case 0x259B: /* ▛ upper + lower left */
        RectFill(ami_rp, x, y, x1, ym - 1);
        RectFill(ami_rp, x, ym, xm - 1, y1);
        break;
    case 0x259C: /* ▜ upper + lower right */
        RectFill(ami_rp, x, y, x1, ym - 1);
        RectFill(ami_rp, xm, ym, x1, y1);
        break;
    case 0x259D: /* ▝ upper right */
        RectFill(ami_rp, xm, y, x1, ym - 1);
        break;
    case 0x259E: /* ▞ upper right + lower left */
        RectFill(ami_rp, xm, y, x1, ym - 1);
        RectFill(ami_rp, x, ym, xm - 1, y1);
        break;
    case 0x259F: /* ▟ upper right + lower */
        RectFill(ami_rp, xm, y, x1, ym - 1);
        RectFill(ami_rp, x, ym, x1, y1);
        break;

    default:
        break;
    }

    SetAPen(ami_rp, saved);
}

/* Render one cell to screen */
static void render_cell(int row, int col, chtype ch, int attrs)
{
    int x, y, pair;
    char buf[2];
    char utf8_buf[5]; /* UTF-8 buffer for TTF (max 4 bytes + null) */
    UBYTE saved;
    int cell_w = fw; /* width of the area to repaint -- fw or 2*fw */
    chtype draw_ch = ch;

    if (!ami_rp || row < 0 || row >= LINES || col < 0 || col >= COLS)
        return;

    /* WIDE-CELL HANDLING: TRAILING snaps to LEAD, LEAD widens to 2*fw */
    if (stdscr && stdscr->cells)
    {
        if (ch == TE_CELL_WIDE_TRAILING && col > 0)
        {
            Cell *lead = CELL(stdscr, row, col - 1);

            col -= 1;
            draw_ch = lead->ch;
            attrs = lead->attrs;
            cell_w = 2 * fw;
        }
        else if (col + 1 < COLS)
        {
            Cell *nxt = CELL(stdscr, row, col + 1);

            if (nxt->ch == TE_CELL_WIDE_TRAILING)
                cell_w = 2 * fw;
        }
    }

    x = px(col);
    y = py(row);
    pair = (attrs & A_COLOR) >> 8;

    apply_colors(pair, attrs);

    /* Clear cell bg over the actual visual extent (1 or 2 cells) */
    saved = ami_rp->FgPen;
    SetAPen(ami_rp, ami_rp->BgPen);
    RectFill(ami_rp, x, y, x + cell_w - 1, y + fh - 1);
    SetAPen(ami_rp, saved);

    /* Draw char: bitmap path (8-bit only) or TTF path (TE_RenderText) */
    if (s_use_ttf)
    {
        uint32_t wc = (uint32_t)(draw_ch & 0xFFFFFFFF);

        if (wc >= 0x20 && wc != TE_CELL_WIDE_TRAILING)
        {
            /* ANSI mode + block glyph: draw directly with RectFill for seamless tiling */
            if (s_ansi_mode && is_block_glyph(wc))
            {
                draw_block_glyph(wc, x, y, fw, fh, saved);
            }
            else
            {
                struct TEDrawPosition pos;
                int utf8_len = utf32_to_utf8(wc, utf8_buf);

                utf8_buf[utf8_len] = '\0';

                TE_SetColorPen(ami_te_dc, ami_te_screen, (LONG)ami_rp->FgPen, (LONG)ami_rp->BgPen);

                pos.x = (WORD)x;
                pos.y = (WORD)(y + fb);

                TE_RenderText(ami_rp, ami_te_dc, &pos, utf8_buf, (ULONG)-1);
            }
        }
    }
    else if ((unsigned char)(draw_ch & 0xFF) >= 0x20)
    {
        buf[0] = (char)(draw_ch & 0xFF);

        Move(ami_rp, x, y + fb);
        Text(ami_rp, (STRPTR)buf, 1);
    }
}

static void shadow_invalidate() { s_shadow_dirty = 1; }

/* Full screen redraw from cell buffer (diff against shadow buffer to minimise AmigaOS calls) */
static void render_all();

/* Force complete redraw (call after font change) */
void amiga_force_redraw()
{
    render_all();
}

static void render_all()
{
    int r, c;
    Cell *cell;
    int last_pair = -1;
    int last_attrs = -1;
    UBYTE saved;
    int i;
    static int s_last_cur_y;
    static int s_last_cur_x;
    int cy_cell;
    int cx_cell;
    int prev_y, prev_x;

    if (!ami_win || !ami_rp || !stdscr || !stdscr->cells)
        return;

    /* (Re)allocate shadow on size change */
    if (!s_shadow || s_shadow_w != COLS || s_shadow_h != LINES)
    {
        free(s_shadow);

        s_shadow = (Cell *)calloc((size_t)(LINES * COLS), sizeof(Cell));
        s_shadow_w = COLS;
        s_shadow_h = LINES;
        s_shadow_dirty = 1;
    }

    for (r = 0; r < LINES; r++)
    {
        /* Compute dirty bitmap with bleed propagation; skip row if nothing dirty */
        if (!compute_dirty_row(r))
            continue;

        c = 0;

        while (c < COLS)
        {
            int run_start, run_pair, run_attrs;
            char run_buf[512]; /* >= any Amiga COLS */
            int run_len;
            int x, y, rx;

            cell = CELL(stdscr, r, c);

            /* Skip cells that are clean after bleed propagation */
            if (!s_dirty_row[c])
            {
                c++;
                continue;
            }

            /* Start run of contiguous dirty cells with same color/attrs */
            run_start = c;
            run_pair = (cell->attrs & A_COLOR) >> 8;
            run_attrs = cell->attrs;
            run_len = 0;

            while (c < COLS && run_len < (int)sizeof(run_buf))
            {
                Cell *cc = CELL(stdscr, r, c);
                int p;

                if (!s_dirty_row[c])
                    break;

                p = (cc->attrs & A_COLOR) >> 8;

                /* Compare color pair and A_REVERSE specifically, ignore other attrs */
                if (p != run_pair || ((cc->attrs ^ run_attrs) & A_REVERSE))
                    break;

                /* TRAILING cell: skip run_buf but advance c for area/RectFill */
                if (cc->ch == TE_CELL_WIDE_TRAILING)
                {
                    if (s_shadow)
                        s_shadow[r * COLS + c] = *cc;

                    run_len++; /* counts for area, not for urun */
                    c++;

                    continue;
                }

                /* run_buf for bitmap mode (8-bit), urun for TTF mode (full BMP codepoint) */
                run_buf[run_len++] = (char)(cc->ch & 0xFF);

                if (s_shadow)
                    s_shadow[r * COLS + c] = *cc;

                c++;
            }

            if (run_len <= 0)
            {
                c = run_start + 1;
                continue;
            }

            if (run_pair != last_pair || run_attrs != last_attrs)
            {
                apply_colors(run_pair, run_attrs);
                last_pair = run_pair;
                last_attrs = run_attrs;
            }

            x = px(run_start);
            y = py(r);
            rx = px(run_start + run_len) - 1;

            /* One wide RectFill for whole run background (includes trailing cells) */
            saved = ami_rp->FgPen;
            SetAPen(ami_rp, ami_rp->BgPen);
            RectFill(ami_rp, x, y, rx, y + fh - 1);
            SetAPen(ami_rp, saved);

            /* One Text() for the whole run (control chars < 0x20 -> space) */
            for (i = 0; i < run_len; i++)
            {
                if ((unsigned char)run_buf[i] < 0x20)
                    run_buf[i] = ' ';
            }

            Move(ami_rp, x, y + fb);

            if (s_use_ttf)
            {
                /* Pre-scan: does this run contain any wide-glyph cells? */
                int run_has_wide = 0;
                {
                    int u2;

                    for (u2 = 0; u2 < run_len; u2++)
                    {
                        Cell *cc2 = CELL(stdscr, r, run_start + u2);

                        if (cc2->ch == TE_CELL_WIDE_TRAILING)
                        {
                            run_has_wide = 1;
                            break;
                        }
                    }
                }

                /* Per-cell render needed for proportional, ANSI mode, or wide glyphs */
                if (s_ttf_proportional || s_ansi_mode || run_has_wide)
                {
                    int u;

                    TE_SetColorPen(ami_te_dc, ami_te_screen, (LONG)ami_rp->FgPen, (LONG)ami_rp->BgPen);

                    for (u = 0; u < run_len; u++)
                    {
                        Cell *cc = CELL(stdscr, r, run_start + u);
                        uint32_t wc = (uint32_t)(cc->ch & 0xFFFFFFFF);
                        int cx = px(run_start + u);
                        UBYTE fg_pen;
                        struct TEDrawPosition pos;
                        char ubuf[5];
                        int ulen;

                        /* TRAILING cells already painted by lead glyph's wide bitmap */
                        if (wc == TE_CELL_WIDE_TRAILING)
                            continue;

                        fg_pen = ami_rp->FgPen;

                        /* Outer run RectFill already cleared background (no per-cell RectFill needed) */

                        /* ANSI mode + block glyph: draw directly (no font) */
                        if (s_ansi_mode && is_block_glyph(wc))
                        {
                            draw_block_glyph(wc, cx, y, fw, fh, fg_pen);
                            continue;
                        }

                        /* TE_RenderText always expects UTF-8 */
                        ulen = utf32_to_utf8(wc, ubuf);
                        ubuf[ulen] = '\0';

                        pos.x = (WORD)cx;
                        pos.y = (WORD)(y + fb);

                        TE_RenderText(ami_rp, ami_te_dc, &pos, ubuf, (ULONG)-1);
                    }
                }
                else
                {
                    /* MONOSPACE: batched TE_RenderText for whole run, skip TRAILING cells */
                    static char urun[2048]; /* 512 chars * 4 bytes max */
                    struct TEDrawPosition pos;
                    int u;
                    int urun_len = 0;

                    for (u = 0; u < run_len; u++)
                    {
                        Cell *cc = CELL(stdscr, r, run_start + u);
                        uint32_t wc = (uint32_t)(cc->ch & 0xFFFFFFFF);

                        if (wc == TE_CELL_WIDE_TRAILING)
                            continue;

                        /*if (wc < 0x20)
                            wc = (uint32_t)' ';*/

                        urun_len += utf32_to_utf8(wc, urun + urun_len);
                    }

                    urun[urun_len] = '\0';

                    TE_SetColorPen(ami_te_dc, ami_te_screen, (LONG)saved, (LONG)ami_rp->BgPen);

                    pos.x = (WORD)x;
                    pos.y = (WORD)(y + fb);

                    TE_RenderText(ami_rp, ami_te_dc, &pos, urun, (ULONG)-1);
                }
            }
            else
            {
                Text(ami_rp, (STRPTR)run_buf, run_len);
            }
        }

        /* Reset color tracking between lines */
        last_pair = -1;
        last_attrs = -1;
    }

    s_shadow_dirty = 0;

    /* Cursor handling: track previous cursor cell to redraw cleanly on move */
    s_last_cur_y = -1;
    s_last_cur_x = -1;
    cy_cell = stdscr ? stdscr->_cury : -1;
    cx_cell = stdscr ? stdscr->_curx : -1;
    prev_y = s_last_cur_y;
    prev_x = s_last_cur_x;

    /* Redraw previous cursor cell to wipe outline (render_cell handles wide glyphs) */
    if (s_shadow && prev_y >= 0 && prev_x >= 0 && prev_y < LINES && prev_x < COLS && (prev_y != cy_cell || prev_x != cx_cell))
    {
        Cell *cell = CELL(stdscr, prev_y, prev_x);
        int sync_lead = prev_x;

        render_cell(prev_y, prev_x, cell->ch, cell->attrs);

        /* If prev was TRAILING, shadow sync target is the lead at col-1 */
        if (cell->ch == TE_CELL_WIDE_TRAILING && prev_x > 0)
            sync_lead = prev_x - 1;

        s_shadow[prev_y * COLS + sync_lead] = *CELL(stdscr, prev_y, sync_lead);

        /* If lead has a trailing companion, also resync that */
        if (sync_lead + 1 < COLS)
        {
            Cell *trail = CELL(stdscr, prev_y, sync_lead + 1);

            if (trail->ch == TE_CELL_WIDE_TRAILING)
                s_shadow[prev_y * COLS + sync_lead + 1] = *trail;
        }
    }

    if (s_cursor_vis && cy_cell >= 0 && cy_cell < LINES && cx_cell >= 0 && cx_cell < COLS)
    {
        int cx, cy;
        int cursor_w = fw;
        int draw_x_cell = cx_cell;
        int trailing_x_cell = -1; /* -1 = none */

        /* Determine cursor visual width: LEAD (2*fw) or TRAILING (2*fw at col-1) */
        if (stdscr && stdscr->cells)
        {
            Cell *cur = CELL(stdscr, cy_cell, cx_cell);

            if (cur->ch == TE_CELL_WIDE_TRAILING && cx_cell > 0)
            {
                draw_x_cell = cx_cell - 1;
                trailing_x_cell = cx_cell;
                cursor_w = 2 * fw;
            }
            else if (cx_cell + 1 < COLS)
            {
                Cell *nxt = CELL(stdscr, cy_cell, cx_cell + 1);

                if (nxt->ch == TE_CELL_WIDE_TRAILING)
                {
                    trailing_x_cell = cx_cell + 1;
                    cursor_w = 2 * fw;
                }
            }
        }

        cx = bx + draw_x_cell * fw;
        cy = by + cy_cell * fh;

        SetAPen(ami_rp, s_cursor_pen);
        Move(ami_rp, cx, cy);
        Draw(ami_rp, cx + cursor_w - 1, cy);
        Move(ami_rp, cx, cy + fh - 1);
        Draw(ami_rp, cx + cursor_w - 1, cy + fh - 1);
        Move(ami_rp, cx, cy);
        Draw(ami_rp, cx, cy + fh - 1);
        Move(ami_rp, cx + cursor_w - 1, cy);
        Draw(ami_rp, cx + cursor_w - 1, cy + fh - 1);

        /* Force next render to redraw under cursor (mark both halves of wide pair) */
        if (s_shadow)
        {
            s_shadow[cy_cell * COLS + draw_x_cell].ch ^= 0x10000;

            if (trailing_x_cell >= 0)
                s_shadow[cy_cell * COLS + trailing_x_cell].ch ^= 0x10000;
        }

        s_last_cur_y = cy_cell;
        s_last_cur_x = cx_cell;
    }
    else
    {
        s_last_cur_y = -1;
        s_last_cur_x = -1;
    }
}

/* Init / shutdown */
static int ami_ttf_try_open()
{
    ULONG prefs;
    int fi;

    if (!s_ttf_file[0])
        return 0; /* TTF disabled in config */

    /* Static TE engine: no library to open, TE_ContextCreate initialises FreeType */
    ami_te_dc = TE_ContextCreate();

    if (!ami_te_dc)
    {
        fprintf(stderr, "TTF: TE_ContextCreate failed (FreeType init?); using bitmap font\n");
        return 0;
    }

    /* Preference flags */
    prefs = TE_FLAG_CLUT_NOMASK | TE_FLAG_HIGHQUALITY;

    if (s_ttf_antialias)
        prefs |= TE_FLAG_ANTIALIAS;

    TE_SetFlags(ami_te_dc, prefs);

    /* Load primary font */
    if (!TE_FontAdd(ami_te_dc, s_ttf_file, s_ttf_size, 0))
    {
        fprintf(stderr, "TTF: TE_FontAdd('%s', %d) failed; using bitmap\n", s_ttf_file, s_ttf_size);

        TE_ContextRelease(ami_te_dc);
        ami_te_dc = NULL;
        return 0;
    }

    /* Load fallback fonts for codepoints missing from primary face (CJK/emoji) */
    for (fi = 0; fi < s_ttf_fallback_count; fi++)
    {
        const char *fp = s_ttf_fallback_file[fi];
        int fs = s_ttf_fallback_size[fi];

        if (!fp[0])
            continue;

        if (fs < 6 || fs > 96)
            fs = s_ttf_size;

        if (!TE_FontAdd(ami_te_dc, fp, fs, 0))
            fprintf(stderr, "TTF: fallback %d TE_FontAdd('%s', %d) failed; skipping\n", fi + 1, fp, fs);
    }

    /* Provisional cell metrics — refined in ami_ttf_apply_to_rastport */
    fw = (s_ttf_size * 5) / 9;

    if (fw < 4)
        fw = 4;

    fh = s_ttf_size + 2;
    fb = s_ttf_size;

    return 1;
}

/* Apply to rastport and re-measure cell metrics via TE_GetMetrics + TE_MeasureText */
static void ami_ttf_apply_to_rastport(struct RastPort *rp)
{
    struct TEGlyphMetrics metric;
    WORD mw, iw;
    struct TEGlyphMetrics ms, is_;
    ULONG prefs;

    if (!s_use_ttf || !ami_te_dc || !rp)
        return;

    /* Line height from font face metrics */
    TE_GetMetrics(ami_te_dc, &metric);

    if (metric.height <= 0)
        TE_MeasureText(ami_te_dc, "Agpqj", -1, &metric);

    if (metric.height > 0)
    {
        fb = (int)metric.baseY;
        fh = (int)metric.height + 1;
    }

    if (fh < 6)
        fh = 6;

    /* Detect proportional: compare M vs i advance widths */
    TE_MeasureText(ami_te_dc, "M", -1, &ms);
    TE_MeasureText(ami_te_dc, "i", -1, &is_);

    mw = ms.width;
    iw = is_.width;

    if (mw > 0 && iw > 0 && (mw - iw) > 1)
        s_ttf_proportional = 1;
    else
        s_ttf_proportional = 0;

    if (!s_ttf_proportional && mw >= 4 && mw <= 64)
        fw = (int)mw;

    /* Register screen for CLUT colour mapping (needed for antialias on indexed screens like AGA) */
    if (ami_te_screen)
        TE_UpdatePalette(ami_te_dc, ami_te_screen);

    /* AUTO antialias: decide based on screen depth (AGA <=8 bits OFF, RTG >=15 bits ON) */
    /*if (s_ttf_aa_auto && ami_te_screen)*/

    prefs = TE_GetFlags(ami_te_dc);

    if (s_ttf_aa_auto && ami_te_screen)
    {
        ULONG depth = (ULONG)GetBitMapAttr(ami_te_screen->RastPort.BitMap, BMA_DEPTH);
        int want = (depth >= 15) ? 1 : 0; /* RTG depths only */

        s_ttf_antialias = want;

        if (want)
            prefs |= TE_FLAG_ANTIALIAS;
        else
            prefs &= ~TE_FLAG_ANTIALIAS;
    }

    /* If font is monospace, tell TE to advance every glyph by monospace_advance (essential for fixed-cell layouts) */
    if (!s_ttf_proportional)
        prefs |= TE_FLAG_FIXEDWIDTH;
    else
        prefs &= ~TE_FLAG_FIXEDWIDTH;

    TE_SetFlags(ami_te_dc, prefs);
}

static void ami_ttf_close()
{
    if (ami_te_dc)
    {
        TE_ContextRelease(ami_te_dc);
        ami_te_dc = NULL;
    }

    ami_te_screen = NULL;
    s_use_ttf = 0;
}

/* Initialization */
WINDOW *initscr()
{
    struct TextAttr ta;
    ULONG idcmp, wfl;
    int dw, dh, sw, sh;

    /* Try TrueType font first; on failure fall back to bitmap */
    s_use_ttf = ami_ttf_try_open();

    if (!s_use_ttf)
    {
        /* Font - load both normal and ANSI fonts at startup */
        ta.ta_Name = (STRPTR)ami_font_name;
        ta.ta_YSize = 8;
        ta.ta_Style = 0;
        ta.ta_Flags = 0;

        ami_font_normal = OpenDiskFont(&ta);

        if (!ami_font_normal && strcmp(ami_font_name, "topaz.font") != 0)
        {
            /* Fallback to topaz.font if configured font failed */
            fprintf(stderr, "Warning: Failed to load font '%s', falling back to topaz.font\n", ami_font_name);

            ta.ta_Name = (STRPTR) "topaz.font";

            ami_font_normal = OpenDiskFont(&ta);
        }

        /* Load ANSI font */
        ta.ta_Name = (STRPTR)ami_ansi_font_name;
        ami_font_ansi = OpenDiskFont(&ta);

        if (!ami_font_ansi && strcmp(ami_ansi_font_name, "topaz.font") != 0)
        {
            /* Fallback to topaz.font if configured font failed */
            fprintf(stderr, "Warning: Failed to load ANSI font '%s', falling back to topaz.font\n", ami_ansi_font_name);

            ta.ta_Name = (STRPTR) "topaz.font";

            ami_font_ansi = OpenDiskFont(&ta);
        }

        /* If ANSI font failed, use normal font */
        if (!ami_font_ansi && ami_font_normal)
            ami_font_ansi = ami_font_normal;

        /* Set default font to normal */
        ami_font = ami_font_normal ? ami_font_normal : ami_font_ansi;

        if (ami_font)
        {
            fw = ami_font->tf_XSize;
            fh = ami_font->tf_YSize;
            fb = ami_font->tf_Baseline;

            fprintf(stderr, "Font loaded: %s (%dx%d)\n", ami_font_name, fw, fh);

            if (ami_font_ansi && ami_font_ansi != ami_font_normal)
                fprintf(stderr, "ANSI font loaded: %s (%dx%d)\n", ami_ansi_font_name, ami_font_ansi->tf_XSize, ami_font_ansi->tf_YSize);
        }
        else
        {
            /* If even topaz.font failed, use default 8x8 */
            fw = 8;
            fh = 8;
            fb = 7;

            fprintf(stderr, "Warning: Failed to load any font, using default 8x8\n");
        }
    }
    else
    {
        /* fw/fh/fb are provisional; refined after window opens, skip diskfont loading */
        ami_font_normal = NULL;
        ami_font_ansi = NULL;
        ami_font = NULL;
    }

    ami_scr = LockPubScreen(NULL);

    if (!ami_scr)
        return NULL;

    /* Window size with borders */
    sw = ami_scr->Width;
    sh = ami_scr->Height;
    dw = 80 * fw + ami_scr->WBorLeft + ami_scr->WBorRight;
    dh = 25 * fh + ami_scr->WBorTop + ami_scr->WBorBottom + ami_scr->Font->ta_YSize + 1;

    if (dw > sw)
        dw = sw;

    if (dh > sh)
        dh = sh;

    idcmp = IDCMP_RAWKEY | IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE;
    wfl = WFLG_SIZEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SIZEBBOTTOM | WFLG_SMART_REFRESH | WFLG_RMBTRAP;

    ami_win = OpenWindowTags(
        NULL,
        WA_Left, (sw - dw) / 2,
        WA_Top, (sh - dh) / 2,
        WA_Width, dw,
        WA_Height, dh,
        WA_MinWidth, 40 * fw,
        WA_MinHeight, 10 * fh,
        WA_MaxWidth, sw,
        WA_MaxHeight, sh,
        WA_Flags, wfl,
        WA_IDCMP, idcmp,
        WA_PubScreen, (ULONG)ami_scr,
        WA_Title, (ULONG)WRAPPER_PID,
        TAG_DONE);

    UnlockPubScreen(NULL, ami_scr);

    ami_scr = NULL;

    if (!ami_win)
        return NULL;

    ami_rp = ami_win->RPort;

    bx = ami_win->BorderLeft;
    by = ami_win->BorderTop;

    if (ami_font)
        SetFont(ami_rp, ami_font);

    if (s_use_ttf)
    {
        ami_te_screen = ami_win->WScreen;

        /* TODO: AGA mode only, no colors, crashes in te_rastport.c LockBitMapTags */
        TE_SetScreen(ami_te_dc, ami_te_screen);

        ami_ttf_apply_to_rastport(ami_rp);
    }

    COLS = (ami_win->Width - ami_win->BorderLeft - ami_win->BorderRight) / fw;
    LINES = (ami_win->Height - ami_win->BorderTop - ami_win->BorderBottom) / fh;

    if (COLS < 20)
        COLS = 20;

    if (LINES < 5)
        LINES = 5;

    /* Create stdscr */
    stdscr = (WINDOW *)malloc(sizeof(WINDOW));

    if (!stdscr)
    {
        CloseWindow(ami_win);

        ami_win = NULL;

        if (ami_font)
        {
            CloseFont(ami_font);
            ami_font = NULL;
        }

        return NULL;
    }

    memset(stdscr, 0, sizeof(WINDOW));

    stdscr->_maxy = LINES;
    stdscr->_maxx = COLS;
    stdscr->cells = (Cell *)calloc((size_t)(LINES * COLS), sizeof(Cell));

    if (!stdscr->cells)
    {
        free(stdscr);
        stdscr = NULL;
        CloseWindow(ami_win);
        ami_win = NULL;
        return NULL;
    }

    curscr = stdscr;

    /* Default color pair */
    memset(s_pair_ok, 0, sizeof(s_pair_ok));

    s_pair_fg[0] = COLOR_WHITE;
    s_pair_bg[0] = s_default_bg_color; /* Use default background color from config */
    s_pair_ok[0] = 1;

    /* Set default window attributes with color pair 0 BEFORE erase() */
    stdscr->attrs = COLOR_PAIR(0);

    erase();
    refresh();

    return stdscr;
}

int endwin()
{
    if (stdscr)
    {
        free(stdscr->cells);
        free(stdscr);
        stdscr = NULL;
    }

    curscr = NULL;

    /* Close TTF (TE_ContextRelease) before window closes */
    ami_ttf_close();

    if (ami_win)
    {
        CloseWindow(ami_win);
        ami_win = NULL;
        ami_rp = NULL;
    }

    if (ami_font_normal)
    {
        CloseFont(ami_font_normal);
        ami_font_normal = NULL;
    }

    if (ami_font_ansi && ami_font_ansi != ami_font_normal)
    {
        CloseFont(ami_font_ansi);
        ami_font_ansi = NULL;
    }

    ami_font = NULL;

    return OK;
}

bool isendwin() { return stdscr == NULL; }

/* Window management */
WINDOW *newwin(int nl, int nc, int beg_y, int beg_x)
{
    WINDOW *w;

    if (nl <= 0)
        nl = LINES - beg_y;

    if (nc <= 0)
        nc = COLS - beg_x;

    if (beg_y < 0 || beg_x < 0 || beg_y + nl > LINES || beg_x + nc > COLS)
        return NULL;

    w = (WINDOW *)malloc(sizeof(WINDOW));

    if (!w)
        return NULL;

    memset(w, 0, sizeof(WINDOW));

    w->_maxy = nl;
    w->_maxx = nc;
    w->_begy = beg_y;
    w->_begx = beg_x;
    w->cells = (Cell *)calloc((size_t)(nl * nc), sizeof(Cell));

    if (!w->cells)
    {
        free(w);
        return NULL;
    }

    return w;
}

int delwin(WINDOW *w)
{
    if (!w)
        return ERR;

    free(w->cells);
    free(w);

    return OK;
}

WINDOW *subwin(WINDOW *o, int nl, int nc, int beg_y, int beg_x)
{
    WINDOW *w = newwin(nl, nc, beg_y, beg_x);

    if (w)
        w->_parent = o;

    return w;
}

WINDOW *derwin(WINDOW *o, int nl, int nc, int dy, int dx)
{
    if (!o)
        return NULL;

    return subwin(o, nl, nc, o->_begy + dy, o->_begx + dx);
}

int mvwin(WINDOW *w, int y, int x)
{
    if (!w)
        return ERR;

    w->_begy = y;
    w->_begx = x;

    return OK;
}

int mvderwin(WINDOW *w, int py, int px)
{
    if (!w || !w->_parent)
        return ERR;

    w->_begy = w->_parent->_begy + py;
    w->_begx = w->_parent->_begx + px;

    return OK;
}

WINDOW *dupwin(WINDOW *w)
{
    WINDOW *d;
    size_t sz;

    if (!w)
        return NULL;

    d = newwin(w->_maxy, w->_maxx, w->_begy, w->_begx);

    if (!d)
        return NULL;

    d->_cury = w->_cury;
    d->_curx = w->_curx;
    d->attrs = w->attrs;
    d->color_pair = w->color_pair;

    if (w->cells && d->cells)
    {
        sz = (size_t)(w->_maxy * w->_maxx) * sizeof(Cell);
        memcpy(d->cells, w->cells, sz);
    }

    return d;
}

/* Refresh */
int wrefresh(WINDOW *w)
{
    if (!w || !ami_win || !w->cells)
        return ERR;

    /* Non-stdscr windows: copy cells into stdscr at begy/begx, then render */
    if (w != stdscr && stdscr && stdscr->cells)
    {
        int r, c;

        for (r = 0; r < w->_maxy; r++)
        {
            int dr = w->_begy + r;

            if (dr < 0 || dr >= LINES)
                continue;

            for (c = 0; c < w->_maxx; c++)
            {
                int dc = w->_begx + c;

                if (dc < 0 || dc >= COLS)
                    continue;

                *CELL(stdscr, dr, dc) = *CELL(w, r, c);
            }
        }
    }

    render_all();

    return OK;
}

int refresh() { return wrefresh(stdscr); }

/* TODO */
int wnoutrefresh(WINDOW *w)
{
    return OK;
}

int doupdate() { return refresh(); }

int redrawwin(WINDOW *w) { return wrefresh(w); }

int wredrawln(WINDOW *w, int bl, int nl)
{
    return OK;
}

/* Clear */
int wclear(WINDOW *w)
{
    int i, n;

    if (!w || !w->cells)
        return ERR;

    n = w->_maxy * w->_maxx;

    for (i = 0; i < n; i++)
    {
        w->cells[i].ch = ' ';
        w->cells[i].attrs = w->attrs;
    }

    w->_cury = 0;
    w->_curx = 0;

    return OK;
}

int clear() { return wclear(stdscr); }

int erase() { return wclear(stdscr); }

int werase(WINDOW *w) { return wclear(w); }

int wclrtobot(WINDOW *w)
{
    int r, c;

    if (!w || !w->cells)
        return ERR;

    if (w->_cury >= w->_maxy)
        return OK; /* cursor past end, nothing to clear */

    for (c = w->_curx; c < w->_maxx; c++)
    {
        CELL(w, w->_cury, c)->ch = ' ';
        CELL(w, w->_cury, c)->attrs = w->attrs;
    }

    for (r = w->_cury + 1; r < w->_maxy; r++)
    {
        for (c = 0; c < w->_maxx; c++)
        {
            CELL(w, r, c)->ch = ' ';
            CELL(w, r, c)->attrs = w->attrs;
        }
    }

    return OK;
}

int clrtobot() { return wclrtobot(stdscr); }

int wclrtoeol(WINDOW *w)
{
    int c;

    if (!w || !w->cells)
        return ERR;

    if (w->_cury >= w->_maxy)
        return OK;

    for (c = w->_curx; c < w->_maxx; c++)
    {
        CELL(w, w->_cury, c)->ch = ' ';
        CELL(w, w->_cury, c)->attrs = w->attrs;
    }

    return OK;
}

int clrtoeol() { return wclrtoeol(stdscr); }

/* Cursor */
int wmove(WINDOW *w, int y, int x)
{
    if (!w || y < 0 || y >= w->_maxy || x < 0 || x >= w->_maxx)
        return ERR;

    w->_cury = y;
    w->_curx = x;

    return OK;
}

int move(int y, int x) { return wmove(stdscr, y, x); }

int curs_set(int v)
{
    int old = s_cursor_vis;
    s_cursor_vis = v;
    return old;
}

/* Character output */
int waddch(WINDOW *w, const chtype ch)
{
    Cell *cell;

    if (!w || !w->cells)
        return ERR;

    if (w->_cury < 0 || w->_cury >= w->_maxy || w->_curx < 0 || w->_curx >= w->_maxx)
        return ERR;

    cell = CELL(w, w->_cury, w->_curx);
    cell->ch = ch & A_CHARTEXT;
    cell->attrs = w->attrs | (ch & A_ATTRIBUTES);
    w->_curx++;

    if (w->_curx >= w->_maxx)
    {
        w->_curx = 0;

        if (w->_cury < w->_maxy - 1)
            w->_cury++;
    }

    return OK;
}

int addch(const chtype ch) { return waddch(stdscr, ch); }

int mvaddch(int y, int x, const chtype ch)
{
    if (move(y, x) == ERR)
        return ERR;

    return addch(ch);
}

int mvwaddch(WINDOW *w, int y, int x, const chtype ch)
{
    if (wmove(w, y, x) == ERR)
        return ERR;

    return waddch(w, ch);
}

/* Read character at position (for cursor XOR, backing store) */
chtype mvinch(int y, int x)
{
    Cell *cell;

    if (!stdscr || !stdscr->cells || y < 0 || y >= LINES || x < 0 || x >= COLS)
        return ' ';

    cell = CELL(stdscr, y, x);

    return (chtype)(cell->ch | cell->attrs);
}

chtype mvwinch(WINDOW *w, int y, int x)
{
    Cell *cell;

    if (!w || !w->cells || y < 0 || y >= w->_maxy || x < 0 || x >= w->_maxx)
        return ' ';

    cell = CELL(w, y, x);

    return (chtype)(cell->ch | cell->attrs);
}

/* String output */

/* UTF-8 aware string output: decode multibyte -> codepoint -> Latin-1 cell */
int waddnstr(WINDOW *w, const char *s, int n)
{
    const char *p;
    const char *end;

    if (!w || !s)
        return ERR;

    if (n < 0)
        n = (int)strlen(s);

    /* When TTF is active, use waddnwstr() which supports full Unicode */
    if (s_use_ttf)
    {
        wchar_t *wcs;
        int wcs_len;
        char *temp_buf;

        if (!w || !s)
            return ERR;

        if (n < 0)
            n = (int)strlen(s);

        /* Create null-terminated temporary buffer for utf8_to_wcs() */
        temp_buf = (char *)malloc((size_t)n + 1);

        if (!temp_buf)
            return ERR;

        memcpy(temp_buf, s, (size_t)n);

        temp_buf[n] = '\0';

        /* Convert UTF-8 to wchar_t */
        wcs = utf8_to_wcs(temp_buf, &wcs_len);

        free(temp_buf);

        if (!wcs)
            return ERR;

        waddnwstr(w, wcs, wcs_len);
        free(wcs);

        return OK;
    }

    p = s;
    end = s + n;

    while (p < end && *p)
    {
        const unsigned char *u = (const unsigned char *)p;
        uint32_t cp;
        int seqlen;

        /* Decode one UTF-8 character */
        if (*u < 0x80)
        {
            cp = *u;
            seqlen = 1;
        }
        else if ((*u & 0xE0) == 0xC0 && p + 1 < end && (u[1] & 0xC0) == 0x80)
        {
            cp = ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
            seqlen = 2;
        }
        else if ((*u & 0xF0) == 0xE0 && p + 2 < end && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80)
        {
            cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) | (u[2] & 0x3F);
            seqlen = 3;
        }
        else if ((*u & 0xF8) == 0xF0 && p + 3 < end && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80)
        {
            cp = ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) | ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
            seqlen = 4;
        }
        else
        {
            /* Invalid or truncated: skip one byte */
            cp = '?';
            seqlen = 1;
        }

        /* Map codepoint to cell character (Topaz = Latin-1 range) */
        if (cp <= 0xFF)
            waddch(w, (chtype)cp);
        else
            waddch(w, (chtype)'?');

        p += seqlen;
    }

    return OK;
}

int waddstr(WINDOW *w, const char *s) { return waddnstr(w, s, -1); }

int addstr(const char *s) { return waddstr(stdscr, s); }

int addnstr(const char *s, int n) { return waddnstr(stdscr, s, n); }

int mvaddstr(int y, int x, const char *s)
{
    if (move(y, x) == ERR)
        return ERR;

    return addstr(s);
}

int mvaddnstr(int y, int x, const char *s, int n)
{
    if (move(y, x) == ERR)
        return ERR;

    return addnstr(s, n);
}

int mvwaddstr(WINDOW *w, int y, int x, const char *s)
{
    if (wmove(w, y, x) == ERR)
        return ERR;

    return waddstr(w, s);
}

int mvwaddnstr(WINDOW *w, int y, int x, const char *s, int n)
{
    if (wmove(w, y, x) == ERR)
        return ERR;

    return waddnstr(w, s, n);
}

/* Wide character output */
int waddnwstr(WINDOW *w, const wchar_t *ws, int n)
{
    int i;

    if (!w || !ws)
        return ERR;

    if (n < 0)
    {
        n = 0;

        while (ws[n])
            n++;
    }

    for (i = 0; i < n && ws[i]; i++)
    {
        chtype ch;
        Cell *cell;
        int cw;

        if (s_use_ttf)
            ch = (ws[i] <= 0x10FFFF) ? (chtype)ws[i] : (chtype)'?';
        else
            ch = (ws[i] <= 0xFF) ? (chtype)ws[i] : (chtype)'?';

        if (w->_cury < 0 || w->_cury >= w->_maxy ||
            w->_curx < 0 || w->_curx >= w->_maxx)
            continue;

        /* Visual width: TTF mode only (wcswidth returns 2 for CJK/wide, 1 for narrow) */
        cw = 1;

        if (s_use_ttf)
        {
            wchar_t one = (wchar_t)(ch & 0x001FFFFFUL);
            int ww = wcswidth(&one, 1);

            if (ww == 2)
                cw = 2;
        }

        /* If wide doesn't fit in remaining row, write space and advance */
        if (cw == 2 && w->_curx + 1 >= w->_maxx)
        {
            cell = CELL(w, w->_cury, w->_curx);

            cell->ch = ' ';
            cell->attrs = w->attrs;
            w->_curx++;
        }
        else
        {
            /* Cleanup edge cases when overwriting existing wide glyphs */
            /* Case A: writing on TRAILING -> lead at col-1 orphaned, replace with space */
            if (w->_curx > 0)
            {
                Cell *cur = CELL(w, w->_cury, w->_curx);

                if (cur->ch == TE_CELL_WIDE_TRAILING)
                {
                    Cell *prev = CELL(w, w->_cury, w->_curx - 1);

                    prev->ch = ' ';
                }
            }

            /* Case B: writing NARROW into LEAD -> trailing at col+1 orphaned, replace with space */
            if (cw == 1 && w->_curx + 1 < w->_maxx)
            {
                Cell *nxt = CELL(w, w->_cury, w->_curx + 1);

                if (nxt->ch == TE_CELL_WIDE_TRAILING)
                {
                    nxt->ch = ' ';
                    nxt->attrs = w->attrs;
                }
            }

            /* Case C: writing WIDE whose trailing slot was LEAD -> cell at curx+2 orphaned, replace with space */
            if (cw == 2 && w->_curx + 2 < w->_maxx)
            {
                Cell *thd = CELL(w, w->_cury, w->_curx + 2);

                if (thd->ch == TE_CELL_WIDE_TRAILING)
                {
                    thd->ch = ' ';
                    thd->attrs = w->attrs;
                }
            }

            /* Write the LEAD */
            cell = CELL(w, w->_cury, w->_curx);
            cell->ch = ch & 0x001FFFFFUL;
            cell->attrs = w->attrs;
            w->_curx++;

            /* Write the TRAILING for wide chars */
            if (cw == 2)
            {
                Cell *trail = CELL(w, w->_cury, w->_curx);

                trail->ch = TE_CELL_WIDE_TRAILING;
                trail->attrs = w->attrs;
                w->_curx++;
            }
        }

        if (w->_curx >= w->_maxx)
        {
            w->_curx = 0;

            if (w->_cury < w->_maxy - 1)
                w->_cury++;
        }
    }

    return OK;
}

int waddwstr(WINDOW *w, const wchar_t *ws) { return waddnwstr(w, ws, -1); }

int addwstr(const wchar_t *ws) { return waddwstr(stdscr, ws); }

int addnwstr(const wchar_t *ws, int n) { return waddnwstr(stdscr, ws, n); }

int mvaddwstr(int y, int x, const wchar_t *ws)
{
    if (move(y, x) == ERR)
        return ERR;

    return addwstr(ws);
}

int mvaddnwstr(int y, int x, const wchar_t *ws, int n)
{
    if (move(y, x) == ERR)
        return ERR;

    return addnwstr(ws, n);
}

int mvwaddwstr(WINDOW *w, int y, int x, const wchar_t *ws)
{
    if (wmove(w, y, x) == ERR)
        return ERR;

    return waddwstr(w, ws);
}

int mvwaddnwstr(WINDOW *w, int y, int x, const wchar_t *ws, int n)
{
    if (wmove(w, y, x) == ERR)
        return ERR;

    return waddnwstr(w, ws, n);
}

/* Printf */
int vw_printw(WINDOW *w, const char *fmt, va_list ap)
{
    char buf[2048]; /* Increased from 512 to reduce truncation of long lines */
    vsnprintf(buf, sizeof(buf), fmt, ap);
    return waddstr(w, buf);
}

int wprintw(WINDOW *w, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vw_printw(w, fmt, ap);
    va_end(ap);

    return r;
}

int printw(const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vw_printw(stdscr, fmt, ap);
    va_end(ap);

    return r;
}

int mvprintw(int y, int x, const char *fmt, ...)
{
    va_list ap;
    int r;

    if (move(y, x) == ERR)
        return ERR;

    va_start(ap, fmt);
    r = vw_printw(stdscr, fmt, ap);
    va_end(ap);

    return r;
}

int mvwprintw(WINDOW *w, int y, int x, const char *fmt, ...)
{
    va_list ap;
    int r;

    if (wmove(w, y, x) == ERR)
        return ERR;

    va_start(ap, fmt);
    r = vw_printw(w, fmt, ap);
    va_end(ap);

    return r;
}

/* Attributes */

int wattrset(WINDOW *w, int a)
{
    if (!w)
        return ERR;

    w->attrs = a;

    return OK;
}

int wattron(WINDOW *w, int a)
{
    if (!w)
        return ERR;

    w->attrs |= a;

    return OK;
}

int wattroff(WINDOW *w, int a)
{
    if (!w)
        return ERR;

    w->attrs &= ~a;

    return OK;
}

int attrset(int a) { return wattrset(stdscr, a); }

int attron(int a) { return wattron(stdscr, a); }

int attroff(int a) { return wattroff(stdscr, a); }

chtype getattrs(WINDOW *w) { return w ? (chtype)w->attrs : 0; }

int wcolor_set(WINDOW *w, short cp, void *o)
{
    if (!w)
        return ERR;

    w->attrs = (w->attrs & ~A_COLOR) | COLOR_PAIR(cp);

    return OK;
}

int color_set(short cp, void *o) { return wcolor_set(stdscr, cp, o); }

int attr_on(attr_t a, void *o)
{
    return attron((int)a);
}

int attr_off(attr_t a, void *o)
{
    return attroff((int)a);
}

int attr_set(attr_t a, short cp, void *o)
{
    if (!stdscr)
        return ERR;

    stdscr->attrs = (int)(a | COLOR_PAIR(cp));

    return OK;
}

int wattr_get(WINDOW *w, attr_t *a, short *cp, void *o)
{
    if (!w)
        return ERR;

    if (a)
        *a = (attr_t)w->attrs;

    if (cp)
        *cp = (short)PAIR_NUMBER(w->attrs);

    return OK;
}

/* Colors */
bool has_colors() { return 1; }

bool can_change_color() { return 0; }

int start_color()
{
    s_colors_on = 1;
    s_pair_fg[0] = COLOR_WHITE;
    s_pair_bg[0] = s_default_bg_color; /* Use default background color from config */
    s_pair_ok[0] = 1;

    return OK;
}

int init_pair(short p, short fg, short bg)
{
    if (p < 0 || p >= COLOR_PAIRS)
        return ERR;

    s_pair_fg[p] = fg;
    s_pair_bg[p] = bg;
    s_pair_ok[p] = 1;

    return OK;
}

int init_color(short c, short r, short g, short b)
{
    return ERR;
}

int use_default_colors() { return OK; }

int pair_content(short p, short *fg, short *bg)
{
    if (p < 0 || p >= COLOR_PAIRS || !s_pair_ok[p])
        return ERR;

    if (fg)
        *fg = s_pair_fg[p];

    if (bg)
        *bg = s_pair_bg[p];

    return OK;
}

int color_content(short c, short *r, short *g, short *b)
{
    if (r)
        *r = 0;

    if (g)
        *g = 0;

    if (b)
        *b = 0;

    return OK;
}

int assume_default_colors(int fg, int bg)
{
    s_pair_fg[0] = (short)fg;
    s_pair_bg[0] = (short)bg;

    return OK;
}

/* Amiga-specific extensions */

/* Set cursor outline pen (0..255), returns previous pen */
int amiga_set_cursor_pen(int pen)
{
    int old = s_cursor_pen;

    if (pen < 0)
        pen = 0;

    if (pen > 255)
        pen = 255;

    s_cursor_pen = (UBYTE)pen;

    return old;
}

/* Set default fg/bg for cells with no color pair (ncurses indices 0..15), forces full redraw */
int amiga_set_default_colors(short fg, short bg)
{
    if (fg < 0 || fg > 15 || bg < 0 || bg > 15)
        return ERR;

    s_default_fg = fg;
    s_default_bg = bg;
    s_shadow_dirty = 1;

    return OK;
}

/* Set default background color for COLOR_PAIR(0) (must call before initscr()) */
int amiga_set_default_bg_color(int color)
{
    int old = s_default_bg_color;

    if (color < 0)
        color = 0;

    if (color > 15)
        color = 15;

    s_default_bg_color = color;
    s_default_bg = (short)color; /* Update also s_default_bg for cells without color pair */
    s_shadow_dirty = 1;          /* Force redraw */

    return old;
}

/* Set font name for Amiga (must call before initscr(), defaults to topaz.font) */
int amiga_set_font_name(const char *font_name)
{
    if (font_name && font_name[0])
    {
        strncpy(ami_font_name, font_name, AMI_FONT_NAME_MAX - 1);
        ami_font_name[AMI_FONT_NAME_MAX - 1] = '\0';
    }
    else
    {
        strncpy(ami_font_name, "topaz.font", AMI_FONT_NAME_MAX - 1);
        ami_font_name[AMI_FONT_NAME_MAX - 1] = '\0';
    }

    return 0;
}

/* Set ANSI font name for Amiga (used when ANSI mode active, defaults to topaz.font) */
int amiga_set_ansi_font_name(const char *font_name)
{
    if (font_name && font_name[0])
    {
        strncpy(ami_ansi_font_name, font_name, AMI_FONT_NAME_MAX - 1);
        ami_ansi_font_name[AMI_FONT_NAME_MAX - 1] = '\0';
    }
    else
    {
        strncpy(ami_ansi_font_name, "topaz.font", AMI_FONT_NAME_MAX - 1);
        ami_ansi_font_name[AMI_FONT_NAME_MAX - 1] = '\0';
    }

    return 0;
}

/* Configure TrueType font (must call BEFORE initscr(), NULL/empty disables TTF) */
int amiga_set_ttf(const char *ttf_file, int size, int antialias)
{
    if (ttf_file && ttf_file[0])
    {
        strncpy(s_ttf_file, ttf_file, sizeof(s_ttf_file) - 1);
        s_ttf_file[sizeof(s_ttf_file) - 1] = '\0';
    }
    else
    {
        s_ttf_file[0] = '\0';
    }

    if (size >= 6 && size <= 96)
        s_ttf_size = size;
    else
        s_ttf_size = 14;

    /* Convention: 0=AUTO, 1=OFF, 2=ON (AUTO decides based on screen depth) */
    if (antialias == 1)
    {
        /* OFF -- force MONO */
        s_ttf_antialias = 0;
        s_ttf_aa_auto = 0;
    }
    else if (antialias == 2)
    {
        /* ON -- force GRAY antialias */
        s_ttf_antialias = 1;
        s_ttf_aa_auto = 0;
    }
    else
    {
        /* AUTO: defer decision until screen depth known (default OFF until then) */
        s_ttf_antialias = 0;
        s_ttf_aa_auto = 1;
    }

    return 0;
}

/* Set UTF-8 encoding (stub for API compatibility, encoding is fixed) */
int amiga_set_ttf_encoding(int use_utf8)
{
    if (s_ttf_use_utf8 != (use_utf8 ? 1 : 0))
    {
        s_ttf_use_utf8 = use_utf8 ? 1 : 0;
        s_shadow_dirty = 1; /* Force full redraw when encoding changes */
    }

    return 0;
}

/* Add fallback TTF font (must call BEFORE initscr(), returns 0 on success, -1 on failure) */
int amiga_add_ttf_fallback(const char *path, int size)
{
    int idx;

    if (!path || !path[0])
        return 0;

    if (s_ttf_fallback_count >= AMI_TTF_FALLBACKS)
        return -1;

    idx = s_ttf_fallback_count;

    strncpy(s_ttf_fallback_file[idx], path, sizeof(s_ttf_fallback_file[idx]) - 1);

    s_ttf_fallback_file[idx][sizeof(s_ttf_fallback_file[idx]) - 1] = '\0';

    s_ttf_fallback_size[idx] = (size >= 6 && size <= 96) ? size : s_ttf_size;

    s_ttf_fallback_count++;
    return 0;
}

/* Clear all configured fallback fonts (idempotent, call before re-applying) */
void amiga_clear_ttf_fallbacks(void)
{
    int i;

    for (i = 0; i < AMI_TTF_FALLBACKS; i++)
    {
        s_ttf_fallback_file[i][0] = '\0';
        s_ttf_fallback_size[i] = 0;
    }

    s_ttf_fallback_count = 0;
}

/* Switch font (call after toggling ANSI mode, use_ansi: 1=ANSI font, 0=regular) */
int amiga_change_font(int use_ansi)
{
    struct TextFont *target_font;

    /* Track ANSI mode regardless of font backend */
    s_ansi_mode = use_ansi ? 1 : 0;
    s_shadow_dirty = 1; /* force full redraw */

    /* In TTF mode the bitmap ANSI font isn't used; nothing more to do */
    if (s_use_ttf)
        return 0;

    target_font = use_ansi ? ami_font_ansi : ami_font_normal;

    if (!ami_win)
        return -1; /* Window not initialized yet */

    if (!target_font)
        return -1; /* Font not loaded */

    /* Switch to the pre-loaded font */
    ami_font = target_font;
    fw = ami_font->tf_XSize;
    fh = ami_font->tf_YSize;
    fb = ami_font->tf_Baseline;

    /* Update RastPort to use the new font */
    if (ami_rp)
        SetFont(ami_rp, ami_font);

    /* Force full redraw */
    s_shadow_dirty = 1;

    return 0;
}

/* Reload TTF with new font path/size, returns 1 on success, 0 on failure */
int amiga_reload_ttf(const char *font_path, int new_size)
{
    char old_file[512];
    int old_size;
    ULONG prefs;
    int fi;
    const char *fp;
    int fs;

    if (!font_path || !font_path[0])
        return 0;

    if (new_size < 6 || new_size > 96)
        return 0;

    if (!s_use_ttf || !ami_te_dc)
    {
        /* TTF not active: just update stored settings */
        strncpy(s_ttf_file, font_path, sizeof(s_ttf_file) - 1);

        s_ttf_file[sizeof(s_ttf_file) - 1] = '\0';
        s_ttf_size = new_size;

        return 1;
    }

    strncpy(old_file, s_ttf_file, sizeof(old_file) - 1);

    old_file[sizeof(old_file) - 1] = '\0';
    old_size = s_ttf_size;

    /* Flush existing fonts and reload with new path/size */
    TE_FontFlush(ami_te_dc);

    if (!TE_FontAdd(ami_te_dc, font_path, new_size, 0))
    {
        fprintf(stderr, "TTF: TE_FontAdd('%s', %d) failed; restoring\n", font_path, new_size);

        /* Try to restore old font */
        TE_FontAdd(ami_te_dc, old_file, old_size, 0);
        return 0;
    }

    /* Reload fallback fonts after primary font */
    for (fi = 0; fi < s_ttf_fallback_count; fi++)
    {
        fp = s_ttf_fallback_file[fi];
        fs = s_ttf_fallback_size[fi];

        if (!fp[0])
            continue;

        if (fs < 6 || fs > 96)
            fs = new_size;

        if (!TE_FontAdd(ami_te_dc, fp, fs, 0))
            fprintf(stderr, "TTF: fallback %d TE_FontAdd('%s', %d) failed; skipping\n", fi + 1, fp, fs);
    }

    /* Update stored settings */
    strncpy(s_ttf_file, font_path, sizeof(s_ttf_file) - 1);

    s_ttf_file[sizeof(s_ttf_file) - 1] = '\0';
    s_ttf_size = new_size;

    /* Update prefs in case antialias changed */
    prefs = TE_FLAG_CLUT_NOMASK | TE_FLAG_HIGHQUALITY;

    if (s_ttf_antialias)
        prefs |= TE_FLAG_ANTIALIAS;

    TE_SetFlags(ami_te_dc, prefs);

    /* Re-measure metrics */
    ami_ttf_apply_to_rastport(ami_rp);

    /* Resize the physical window to keep COLS x LINES with new cell size */
    if (ami_win)
    {
        struct Screen *scr;
        int bw = ami_win->BorderLeft + ami_win->BorderRight;
        int bh = ami_win->BorderTop + ami_win->BorderBottom;
        int want_w = COLS * fw + bw;
        int want_h = LINES * fh + bh;

        /* BorderTop already includes title bar height on backdrop window */
        int win_x = ami_win->LeftEdge;
        int win_y = ami_win->TopEdge;

        /* Clamp to screen bounds */
        scr = ami_win->WScreen;

        if (scr)
        {
            if (want_w > scr->Width)
                want_w = scr->Width;

            if (want_h > scr->Height)
                want_h = scr->Height;

            if (win_x + want_w > scr->Width)
                win_x = scr->Width - want_w;

            if (win_y + want_h > scr->Height)
                win_y = scr->Height - want_h;

            if (win_x < 0)
                win_x = 0;

            if (win_y < 0)
                win_y = 0;

            /* Rebuild CLUT map for new screen/palette */
            ami_te_screen = scr;
            TE_UpdatePalette(ami_te_dc, scr);
        }

        /* ChangeWindowBox is asynchronous: IDCMP_NEWSIZE handler recalculates COLS/LINES and redraws */
        s_shadow_dirty = 1;

        if (s_shadow)
        {
            free(s_shadow);

            s_shadow = NULL;
            s_shadow_w = 0;
            s_shadow_h = 0;
        }

        ChangeWindowBox(ami_win, win_x, win_y, want_w, want_h);
    }

    return 1;
}

/* Keyboard input */

/* Pending bytes from MapRawKey() (dead-key + vowel can generate multiple Latin-1 bytes) */
static int xlat_rawkey(UWORD code, UWORD qual, APTR iaddr)
{
    struct InputEvent ie;
    UBYTE buf[16];
    LONG actual;
    int first;

    if (code & IECODE_UP_PREFIX)
        return ERR;

    /* Modified arrows must be checked before bare-arrow switch below */
    if (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
    {
        if (code == 0x4F)
            return KEY_SLEFT;

        if (code == 0x4E)
            return KEY_SRIGHT;
    }

    if (qual & IEQUALIFIER_CONTROL)
    {
        if (code == 0x4F)
            return KEY_CLEFT;

        if (code == 0x4E)
            return KEY_CRIGHT;
    }

    if (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT))
    {
        if (code == 0x4F)
            return KEY_ALEFT;

        if (code == 0x4E)
            return KEY_ARIGHT;
    }

    /* Special keys - return ncurses KEY_* values, not raw Amiga codes */
    switch (code)
    {
    case 0x4C:
        return KEY_UP;
    case 0x4D:
        return KEY_DOWN;
    case 0x4F:
        return KEY_LEFT;
    case 0x4E:
        return KEY_RIGHT;
    case 0x46:
        return KEY_DC;
    case 0x41:
        return KEY_BACKSPACE;
    case 0x70:
        return KEY_HOME;
    case 0x71:
        return KEY_END;
    case 0x72:
        return KEY_PPAGE;
    case 0x73:
        return KEY_NPAGE;
    case 0x3F:
        return KEY_F(10); /* Help key */
    case 0x50:
        return KEY_F(1);
    case 0x51:
        return KEY_F(2);
    case 0x52:
        return KEY_F(3);
    case 0x53:
        return KEY_F(4);
    case 0x54:
        return KEY_F(5);
    case 0x55:
        return KEY_F(6);
    case 0x56:
        return KEY_F(7);
    case 0x57:
        return KEY_F(8);
    case 0x58:
        return KEY_F(9);
    case 0x59:
        return KEY_F(10);
    }

    /* TODO Check */
    /* Right-Amiga + V = paste (Amiga lacks Insert key, synthesise Ctrl-V for paste handler) */
    if ((qual & IEQUALIFIER_RCOMMAND) && code == 0x34) /* 0x34 = V */
        return 0x16;

    /* Map printable via MapRawKey
     *
     * Alt handling is keymap-dependent and tricky:
     *   - On English/US layouts Alt is typically a "meta" modifier
     *     (Alt+letter sends ESC+letter to apps), and the keymap does
     *     NOT map Alt+key to any character
     *   - On Spanish (and many other non-English) layouts, Alt is the
     *     ONLY way to type characters like @ # | \ [ ] { } EUR - the
     *     keymap maps Alt+2 -> @, Alt+1 -> |, Alt+E -> €, etc
     *
     * So we MUST try MapRawKey with the original qualifiers first
     * If that yields a different printable result from the bare-key
     * mapping, Alt was needed to TYPE the character: emit those bytes
     * directly. If both yield the same byte, Alt was a meta modifier:
     * fall through to the "ESC + bare" convention
     *
     * MapRawKey can return MORE THAN ONE byte (dead-key composition,
     * multibyte locale output). Extra bytes are queued and drained on
     * subsequent wgetch() calls
     *
     * Per AmigaOS keymap.library autodoc, for IDCMP_RAWKEY events
     * im->IAddress is a POINTER TO POINTER to the dead-key prefix
     * data. Without dereferencing it MapRawKey loses dead-key state
     * and accent composition (´+a -> á, ¨+u -> ü) silently fails */
    memset(&ie, 0, sizeof(ie));

    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = code;

    /* COMMAND qualifiers are always stripped (they're for the app's
     * own use, never produce text). ALT is kept on the first pass */
    ie.ie_Qualifier = qual & ~(IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND);

    if (iaddr)
        ie.ie_EventAddress = (APTR)(*((ULONG *)iaddr));
    else
        ie.ie_EventAddress = NULL;

    actual = MapRawKey(&ie, (STRPTR)buf, (LONG)sizeof(buf), NULL);

    /* When Alt is pressed, decide between:
     *   - chord  (returns KEY_ALT(letter))  for letters; the keymap
     *            may produce a character, nothing, or something
     *            different -- we ignore it because the user expects
     *            Alt+letter to be a hotkey
     *   - text   (passes the keymap's result through)  for everything
     *            else, so Alt+2='@', Alt+1='|', Alt+e='€', dead-key
     *            composition, etc. all keep working
     *
     * The base key (without Alt) is queried with a second MapRawKey:
     * if it's a single A-Z letter we return the chord. Otherwise we
     * fall through to the original Alt-modified result */
    if (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT))
    {
        struct InputEvent ie2;
        unsigned char buf2[16];
        LONG actual2;

        memset(&ie2, 0, sizeof(ie2));
        ie2.ie_Class = IECLASS_RAWKEY;
        ie2.ie_Code = code;
        ie2.ie_Qualifier = qual & ~(IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND | IEQUALIFIER_LALT | IEQUALIFIER_RALT);

        if (iaddr)
            ie2.ie_EventAddress = (APTR)(*((ULONG *)iaddr));
        else
            ie2.ie_EventAddress = NULL;

        actual2 = MapRawKey(&ie2, (STRPTR)buf2, (LONG)sizeof(buf2), NULL);

        if (actual2 == 1 && ((buf2[0] >= 'a' && buf2[0] <= 'z') || (buf2[0] >= 'A' && buf2[0] <= 'Z')))
        {
            /* Pure Alt+letter chord. Normalise case so callers write
             * KEY_ALT('L') uniformly (matches the editor's case
             * labels) */
            int letter = (int)buf2[0];

            if (letter >= 'a' && letter <= 'z')
                letter = letter - 'a' + 'A';

            return KEY_ALT(letter);
        }

        /* Not a letter -- the keymap is the authority. Fall through
         * to the Alt-modified result (which gives '@', '|', '€', dead
         * keys etc) */
    }

    if (actual <= 0)
        return ERR;

    first = (int)(unsigned char)buf[0];

    if (actual > 1)
    {
        int i, n = (int)actual - 1;

        if (n > (int)sizeof(s_key_queue))
            n = (int)sizeof(s_key_queue);

        for (i = 0; i < n; i++)
            s_key_queue[i] = buf[1 + i];

        s_key_queue_len = n;
        s_key_queue_pos = 0;
    }

    return first;
}

int wgetch(WINDOW *w)
{
    struct IntuiMessage *imsg;
    ULONG cls;
    UWORD code, qual;
    APTR iaddr;
    int key = ERR;

    if (!ami_win)
        return ERR;

    /* Drain any extra bytes from a previous MapRawKey() call. This is
     * what makes dead-key sequences (´+a = á, ¨+u = ü, ...) work when
     * the keymap returns multiple bytes at once */
    if (s_key_queue_pos < s_key_queue_len)
    {
        key = (int)s_key_queue[s_key_queue_pos++];

        if (s_key_queue_pos >= s_key_queue_len)
        {
            s_key_queue_pos = 0;
            s_key_queue_len = 0;
        }

        return key;
    }

    /* Pushback */
    if (s_ungetch != ERR)
    {
        key = s_ungetch;
        s_ungetch = ERR;
        return key;
    }

    while (key == ERR)
    {
        if (s_nodelay_mode)
        {
            imsg = (struct IntuiMessage *)GetMsg(ami_win->UserPort);

            if (!imsg)
                return ERR;
        }
        else
        {
            WaitPort(ami_win->UserPort);

            imsg = (struct IntuiMessage *)GetMsg(ami_win->UserPort);

            if (!imsg)
                continue;
        }

        cls = imsg->Class;
        code = imsg->Code;
        qual = imsg->Qualifier;
        iaddr = imsg->IAddress;

        /* DEAD KEYS: for IDCMP_RAWKEY, IAddress points to dead-key prefix
         * data that becomes invalid after ReplyMsg. We MUST call
         * MapRawKey (inside xlat_rawkey) BEFORE replying the message,
         * otherwise dead-key sequences (accented chars: á é í ó ú ñ ...)
         * cannot be composed */
        if (cls == IDCMP_RAWKEY)
            key = xlat_rawkey(code, qual, iaddr);

        ReplyMsg((struct Message *)imsg);

        switch (cls)
        {
        case IDCMP_RAWKEY:
            /* already translated above */
            break;
        case IDCMP_CLOSEWINDOW:
            /*key = KEY_F(12);*/
            key = 27; /* ESC */
            break;
        case IDCMP_REFRESHWINDOW:
            BeginRefresh(ami_win);
            shadow_invalidate(); /* OS asked us to redraw everything */
            render_all();
            EndRefresh(ami_win, TRUE);
            break;
        case IDCMP_NEWSIZE:
        {
            int nc = (ami_win->Width - ami_win->BorderLeft - ami_win->BorderRight) / fw;
            int nl = (ami_win->Height - ami_win->BorderTop - ami_win->BorderBottom) / fh;

            if (nc < 20)
                nc = 20;

            if (nl < 5)
                nl = 5;

            bx = ami_win->BorderLeft;
            by = ami_win->BorderTop;

            if (stdscr && (nc != COLS || nl != LINES))
            {
                Cell *nc_cells = (Cell *)calloc((size_t)(nl * nc), sizeof(Cell));

                if (nc_cells)
                {
                    /* Copy old content that fits into new size */
                    int copy_r = nl < LINES ? nl : LINES;
                    int copy_c = nc < COLS ? nc : COLS;
                    int rr, cc;

                    for (rr = 0; rr < copy_r; rr++)
                    {
                        for (cc = 0; cc < copy_c; cc++)
                            nc_cells[rr * nc + cc] = stdscr->cells[rr * COLS + cc];
                    }

                    free(stdscr->cells);

                    stdscr->cells = nc_cells;
                    COLS = nc;
                    LINES = nl;
                    stdscr->_maxx = COLS;
                    stdscr->_maxy = LINES;

                    if (stdscr->_curx >= COLS)
                        stdscr->_curx = COLS - 1;

                    if (stdscr->_cury >= LINES)
                        stdscr->_cury = LINES - 1;
                }
            }

            key = KEY_RESIZE;
            break;
        }
        case IDCMP_SIZEVERIFY:
            break;
        }
    }

    return key;
}

int getch() { return wgetch(stdscr); }

int mvgetch(int y, int x)
{
    if (move(y, x) == ERR)
        return ERR;
    return getch();
}

int mvwgetch(WINDOW *w, int y, int x)
{
    if (wmove(w, y, x) == ERR)
        return ERR;
    return wgetch(w);
}

int ungetch(int ch)
{
    s_ungetch = ch;
    return OK;
}

int flushinp()
{
    s_ungetch = ERR;
    s_unget_wch_valid = 0;
    s_key_queue_len = 0;
    s_key_queue_pos = 0;

    return OK;
}

/* Wide char input */
int wget_wch(WINDOW *w, wint_t *wch)
{
    int key;

    if (!wch)
        return ERR;

    /* Pushback */
    if (s_unget_wch_valid)
    {
        *wch = (wint_t)s_unget_wch_buf;
        s_unget_wch_valid = 0;
        return OK;
    }

    key = wgetch(w);

    if (key == ERR)
        return ERR;

    /* Special keys (>= KEY_MIN) return KEY_CODE_YES */
    if (key >= KEY_MIN)
    {
        *wch = (wint_t)key;

        return KEY_CODE_YES;
    }

    *wch = (wint_t)key;

    return OK;
}

int get_wch(wint_t *wch) { return wget_wch(stdscr, wch); }

int mvget_wch(int y, int x, wint_t *wch)
{
    if (move(y, x) == ERR)
        return ERR;

    return get_wch(wch);
}

int mvwget_wch(WINDOW *w, int y, int x, wint_t *wch)
{
    if (wmove(w, y, x) == ERR)
        return ERR;

    return wget_wch(w, wch);
}

int unget_wch(const wchar_t wch)
{
    s_unget_wch_buf = wch;
    s_unget_wch_valid = 1;

    return OK;
}

/* String input (basic) */
int wgetnstr(WINDOW *w, char *s, int n)
{
    int i = 0, ch;

    if (!s || n <= 0)
        return ERR;

    while (i < n - 1)
    {
        ch = wgetch(w);

        if (ch == ERR || ch == '\n' || ch == '\r')
            break;

        if (ch == KEY_BACKSPACE && i > 0)
        {
            i--;

            continue;
        }

        if (ch >= 0x20)
            s[i++] = (char)ch; /* accept UTF-8 bytes */
    }

    s[i] = '\0';

    return OK;
}

int wgetstr(WINDOW *w, char *s) { return wgetnstr(w, s, 256); }

int getstr(char *s) { return wgetstr(stdscr, s); }

int getnstr(char *s, int n) { return wgetnstr(stdscr, s, n); }

int mvgetstr(int y, int x, char *s)
{
    if (move(y, x) == ERR)
        return ERR;

    return getstr(s);
}

int mvwgetstr(WINDOW *w, int y, int x, char *s)
{
    if (wmove(w, y, x) == ERR)
        return ERR;

    return wgetstr(w, s);
}

int mvgetnstr(int y, int x, char *s, int n)
{
    if (move(y, x) == ERR)
        return ERR;

    return getnstr(s, n);
}

int mvwgetnstr(WINDOW *w, int y, int x, char *s, int n)
{
    if (wmove(w, y, x) == ERR)
        return ERR;
    return wgetnstr(w, s, n);
}

/* Mode settings */

int nodelay(WINDOW *w, bool bf)
{
    s_nodelay_mode = bf;

    return OK;
}

int notimeout(WINDOW *w, bool bf)
{
    return OK;
}

int timeout(int d)
{
    s_nodelay_mode = (d == 0);

    return OK;
}

int wtimeout(WINDOW *w, int d)
{
    s_nodelay_mode = (d == 0);

    return OK;
}

int cbreak()
{
    s_raw_mode = 0;

    return OK;
}

int nocbreak() { return OK; }

int echo()
{
    s_echo_mode = 1;

    return OK;
}

int noecho()
{
    s_echo_mode = 0;

    return OK;
}

int halfdelay(int t)
{
    return OK;
}

int intrflush(WINDOW *w, bool bf)
{
    return OK;
}

int keypad(WINDOW *w, bool bf)
{
    s_keypad_mode = bf;

    return OK;
}

int meta(WINDOW *w, bool bf)
{
    return OK;
}

int raw()
{
    s_raw_mode = 1;

    return OK;
}

int noraw()
{
    s_raw_mode = 0;

    return OK;
}

int nl() { return OK; }

int nonl() { return OK; }

/* Beep / Flash */

int beep()
{
    if (ami_win)
        DisplayBeep(NULL);

    return OK;
}

int flash() { return beep(); }

/* Border / Lines */

int wborder(WINDOW *w, chtype ls, chtype rs, chtype ts, chtype bs, chtype tl, chtype tr, chtype bl, chtype br)
{
    int r, c;

    if (!w || !w->cells)
        return ERR;

    if (!ls)
        ls = '|';

    if (!rs)
        rs = '|';

    if (!ts)
        ts = '-';

    if (!bs)
        bs = '-';

    if (!tl)
        tl = '+';

    if (!tr)
        tr = '+';

    if (!bl)
        bl = '+';

    if (!br)
        br = '+';

    for (r = 1; r < w->_maxy - 1; r++)
    {
        CELL(w, r, 0)->ch = ls;
        CELL(w, r, 0)->attrs = w->attrs;
        CELL(w, r, w->_maxx - 1)->ch = rs;
        CELL(w, r, w->_maxx - 1)->attrs = w->attrs;
    }

    for (c = 1; c < w->_maxx - 1; c++)
    {
        CELL(w, 0, c)->ch = ts;
        CELL(w, 0, c)->attrs = w->attrs;
        CELL(w, w->_maxy - 1, c)->ch = bs;
        CELL(w, w->_maxy - 1, c)->attrs = w->attrs;
    }

    CELL(w, 0, 0)->ch = tl;
    CELL(w, 0, 0)->attrs = w->attrs;
    CELL(w, 0, w->_maxx - 1)->ch = tr;
    CELL(w, 0, w->_maxx - 1)->attrs = w->attrs;
    CELL(w, w->_maxy - 1, 0)->ch = bl;
    CELL(w, w->_maxy - 1, 0)->attrs = w->attrs;
    CELL(w, w->_maxy - 1, w->_maxx - 1)->ch = br;
    CELL(w, w->_maxy - 1, w->_maxx - 1)->attrs = w->attrs;

    return OK;
}

int border(chtype ls, chtype rs, chtype ts, chtype bs, chtype tl, chtype tr, chtype bl, chtype br)
{
    return wborder(stdscr, ls, rs, ts, bs, tl, tr, bl, br);
}

int box(WINDOW *w, chtype v, chtype h)
{
    return wborder(w, v, v, h, h, 0, 0, 0, 0);
}

int whline(WINDOW *w, chtype ch, int n)
{
    int i;

    if (!w || !w->cells)
        return ERR;

    if (!ch)
        ch = '-';

    for (i = 0; i < n && w->_curx + i < w->_maxx; i++)
    {
        CELL(w, w->_cury, w->_curx + i)->ch = ch;
        CELL(w, w->_cury, w->_curx + i)->attrs = w->attrs;
    }

    return OK;
}

int wvline(WINDOW *w, chtype ch, int n)
{
    int i;

    if (!w || !w->cells)
        return ERR;

    if (!ch)
        ch = '|';

    for (i = 0; i < n && w->_cury + i < w->_maxy; i++)
    {
        CELL(w, w->_cury + i, w->_curx)->ch = ch;
        CELL(w, w->_cury + i, w->_curx)->attrs = w->attrs;
    }

    return OK;
}

int hline(chtype ch, int n) { return whline(stdscr, ch, n); }

int vline(chtype ch, int n) { return wvline(stdscr, ch, n); }

int mvhline(int y, int x, chtype ch, int n)
{
    if (move(y, x) == ERR)
        return ERR;

    return hline(ch, n);
}

int mvwhline(WINDOW *w, int y, int x, chtype ch, int n)
{
    if (wmove(w, y, x) == ERR)
        return ERR;

    return whline(w, ch, n);
}

int mvvline(int y, int x, chtype ch, int n)
{
    if (move(y, x) == ERR)
        return ERR;

    return vline(ch, n);
}

int mvwvline(WINDOW *w, int y, int x, chtype ch, int n)
{
    if (wmove(w, y, x) == ERR)
        return ERR;

    return wvline(w, ch, n);
}

/* Background */

int bkgd(chtype ch)
{
    if (!stdscr)
        return ERR;

    stdscr->attrs = (int)ch;

    return OK;
}

void bkgdset(chtype ch)
{
    if (stdscr)
        stdscr->attrs = (int)ch;
}

void wbkgd(WINDOW *w, chtype ch)
{
    if (w)
        w->attrs = (int)ch;
}

void wbkgdset(WINDOW *w, chtype ch)
{
    if (w)
        w->attrs = (int)ch;
}

chtype getbkgd(WINDOW *w) { return w ? (chtype)w->attrs : 0; }

/* Scroll */
int wscrl(WINDOW *w, int n)
{
    int r, c;

    if (!w || !w->cells || n == 0)
        return ERR;

    /* Clamp: if |n| >= height, just clear everything */
    if (n >= w->_maxy || n <= -(w->_maxy))
    {
        for (r = 0; r < w->_maxy; r++)
        {
            for (c = 0; c < w->_maxx; c++)
            {
                CELL(w, r, c)->ch = ' ';
                CELL(w, r, c)->attrs = w->attrs;
            }
        }

        return OK;
    }

    if (n > 0)
    {
        /* scroll up: row 0 gets content of row n */
        for (r = 0; r < w->_maxy - n; r++)
            memcpy(&w->cells[r * w->_maxx], &w->cells[(r + n) * w->_maxx], (size_t)w->_maxx * sizeof(Cell));

        for (r = w->_maxy - n; r < w->_maxy; r++)
        {
            for (c = 0; c < w->_maxx; c++)
            {
                CELL(w, r, c)->ch = ' ';
                CELL(w, r, c)->attrs = w->attrs;
            }
        }
    }
    else
    {
        /* scroll down: last row gets content of row _maxy+n-1 */
        int an = -n;

        for (r = w->_maxy - 1; r >= an; r--)
            memcpy(&w->cells[r * w->_maxx], &w->cells[(r - an) * w->_maxx], (size_t)w->_maxx * sizeof(Cell));

        for (r = 0; r < an; r++)
        {
            for (c = 0; c < w->_maxx; c++)
            {
                CELL(w, r, c)->ch = ' ';
                CELL(w, r, c)->attrs = w->attrs;
            }
        }
    }

    return OK;
}

int scroll(WINDOW *w) { return wscrl(w, 1); }

int scrl(int n) { return wscrl(stdscr, n); }

int setscrreg(int t, int b)
{
    return OK;
}

int wsetscrreg(WINDOW *w, int t, int b)
{
    return OK;
}

/* Overlay / Copy */

int overlay(const WINDOW *s, WINDOW *d)
{
    return copywin(s, d, 0, 0, 0, 0, d->_maxy - 1, d->_maxx - 1, 1);
}

int overwrite(const WINDOW *s, WINDOW *d)
{
    return copywin(s, d, 0, 0, 0, 0, d->_maxy - 1, d->_maxx - 1, 0);
}

int copywin(const WINDOW *s, WINDOW *d, int sr, int sc, int dr, int dc, int dmr, int dmc, int ovl)
{
    int r, c;

    if (!s || !d || !s->cells || !d->cells)
        return ERR;

    for (r = 0; dr + r <= dmr && sr + r < s->_maxy && dr + r < d->_maxy; r++)
    {
        for (c = 0; dc + c <= dmc && sc + c < s->_maxx && dc + c < d->_maxx; c++)
        {
            Cell *src = &((WINDOW *)s)->cells[(sr + r) * s->_maxx + (sc + c)];
            Cell *dst = &d->cells[(dr + r) * d->_maxx + (dc + c)];

            if (!ovl || (src->ch & 0xFF) != ' ')
                *dst = *src;
        }
    }

    return OK;
}

/* Touch (stubs) */

void wtouchln(WINDOW *w, int y, int n, int c)
{
}

int is_linetouched(WINDOW *w, int l)
{
    return 1;
}

int is_wintouched(WINDOW *w)
{
    return 1;
}

void untouchwin(WINDOW *w) {}

/* Mouse (stubs) */

unsigned long getmouse() { return 0; }

int ungetmouse(unsigned long m)
{
    return ERR;
}

/* string.h helpers (Amiga libc has bugs with control chars) */

/* Undefine libc versions first, then provide our implementation */
#ifdef snprintf
#undef snprintf
#endif

/* snprintf that handles SOH correctly - manual implementation */
int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list ap;
    char buf[4096]; /* temp buffer - increased from 1024 to handle long strings */
    int len;
    size_t i;

    if (size == 0)
        return 0; /* avoid underflow in loop */

    va_start(ap, format);
    len = vsnprintf(buf, sizeof(buf), format, ap); /* use vsnprintf to limit write */
    va_end(ap);

    /* Copy with bounds checking - preserve ALL chars including SOH */
    for (i = 0; i < size - 1 && i < (size_t)len; i++)
        str[i] = buf[i];

    str[i] = '\0';

    return len;
}

/* wchar_t string helpers (missing from Amiga libc) */
wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle)
{
    const wchar_t *h, *n;

    if (!needle || !needle[0])
        return (wchar_t *)haystack;

    for (; *haystack; haystack++)
    {
        h = haystack;
        n = needle;

        while (*h && *n && *h == *n)
        {
            h++;
            n++;
        }

        if (!*n)
            return (wchar_t *)haystack;
    }

    return NULL;
}

int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
    if (n == 0)
        return 0;
    while (n-- && *s1 && *s1 == *s2)
    {
        s1++;
        s2++;
    }
    return n == (size_t)-1 ? 0 : (int)(*s1 - *s2);
}

wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d = dst;
    size_t i;

    for (i = 0; i < n && src[i]; i++)
        d[i] = src[i];

    for (; i < n; i++)
        d[i] = L'\0';

    return dst;
}

int iswalpha(wint_t wc)
{
    return (wc >= L'A' && wc <= L'Z') || (wc >= L'a' && wc <= L'z') || (wc >= 0xC0 && wc <= 0xFF && wc != 0xD7 && wc != 0xF7);
}

int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n)
{
    while (n--)
    {
        if (*s1 != *s2)
            return (*s1 < *s2) ? -1 : 1;

        s1++;
        s2++;
    }

    return 0;
}

wchar_t *wmemcpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d = dst;

    while (n--)
        *d++ = *src++;

    return dst;
}

wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n)
{
    wchar_t *d = dst;
    const wchar_t *s = src;

    if (d < s)
    {
        while (n--)
            *d++ = *s++;
    }
    else if (d > s)
    {
        d += n;
        s += n;

        while (n--)
            *--d = *--s;
    }

    return dst;
}

#endif /* PLATFORM_AMIGA */
