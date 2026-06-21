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

/* ui_glyph_picker.c -- Unicode glyph selection popup */

#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include "te.h"
#include "ui_glyph_picker.h"

struct GlyphBlock
{
    const char *name;
    unsigned long start;
    int count;
};

static const struct GlyphBlock s_blocks[] =
    {
        {"Latin-1 Supplement", 0x00A0, 96},
        {"Latin Extended-A", 0x0100, 128},
        {"Latin Extended-B", 0x0180, 208},
        {"IPA Extensions", 0x0250, 96},
        {"Greek and Coptic", 0x0370, 144},
        {"Cyrillic", 0x0400, 256},
        {"Hebrew", 0x0590, 112},
        {"Arabic", 0x0600, 256},
        {"General Punctuation", 0x2000, 112},
        {"Superscripts/Subscripts", 0x2070, 48},
        {"Currency Symbols", 0x20A0, 48},
        {"Letterlike Symbols", 0x2100, 80},
        {"Number Forms", 0x2150, 64},
        {"Arrows", 0x2190, 112},
        {"Mathematical Operators", 0x2200, 256},
        {"Misc Technical", 0x2300, 256},
        {"Control Pictures", 0x2400, 64},
        {"Box Drawing", 0x2500, 128},
        {"Block Elements", 0x2580, 32},
        {"Geometric Shapes", 0x25A0, 96},
        {"Misc Symbols", 0x2600, 256},
        {"Dingbats", 0x2700, 192},
        {"Braille Patterns", 0x2800, 256},
        {"Misc Math Symbols-B", 0x2980, 128},
        {"Suppl Math Operators", 0x2A00, 256},
        {"Misc Symbols and Arrows", 0x2B00, 256},
        {"CJK Symbols/Punctuation", 0x3000, 64},
        {"Hiragana", 0x3040, 96},
        {"Katakana", 0x30A0, 96},
        {"Emoticons", 0x1F600, 80},
        {"Misc Symbols+Pictographs", 0x1F300, 256},
        {"Transport and Map", 0x1F680, 128},
        {"Supplemental Symbols", 0x1F900, 256},
};

#define NUM_BLOCKS ((int)(sizeof(s_blocks) / sizeof(s_blocks[0])))

#ifdef PLATFORM_WIN32
#define CELL_W 4
#else
#define CELL_W 3
#endif
#define GRID_COLS 16

/* Active filter: -1=all blocks, 0..N-1=specific block (static for persistence) */
static int s_filter = -1;

static int glyph_total(void)
{
    int i = 0;
    int total = 0;

    if (s_filter >= 0 && s_filter < NUM_BLOCKS)
        return s_blocks[s_filter].count;

    for (i = 0; i < NUM_BLOCKS; i++)
        total += s_blocks[i].count;

    return total;
}

static unsigned long glyph_at(int flat_idx, int *out_block)
{
    int i = 0;
    int base = 0;

    if (flat_idx < 0)
        return (unsigned long)-1;

    if (s_filter >= 0 && s_filter < NUM_BLOCKS)
    {
        if (flat_idx >= s_blocks[s_filter].count)
            return (unsigned long)-1;

        if (out_block)
            *out_block = s_filter;

        return s_blocks[s_filter].start + (unsigned long)flat_idx;
    }

    for (i = 0; i < NUM_BLOCKS; i++)
    {
        if (flat_idx < base + s_blocks[i].count)
        {
            if (out_block)
                *out_block = i;

            return s_blocks[i].start + (unsigned long)(flat_idx - base);
        }

        base += s_blocks[i].count;
    }

    return (unsigned long)-1;
}

static int glyph_find(unsigned long cp)
{
    int i, base = 0;
    int best_idx = 0;
    long best_dist = 0x7FFFFFFFL;
    long d;

    if (s_filter >= 0 && s_filter < NUM_BLOCKS)
    {
        unsigned long bstart = s_blocks[s_filter].start;
        unsigned long bend = bstart + (unsigned long)s_blocks[s_filter].count;

        if (cp < bstart)
            return 0;

        if (cp >= bend)
            return s_blocks[s_filter].count - 1;

        return (int)(cp - bstart);
    }

    for (i = 0; i < NUM_BLOCKS; i++)
    {
        unsigned long bstart = s_blocks[i].start;
        unsigned long bend = bstart + (unsigned long)s_blocks[i].count;

        if (cp >= bstart && cp < bend)
            return base + (int)(cp - bstart);

        if (cp < bstart)
            d = (long)(bstart - cp);
        else
            d = (long)(cp - bend + 1);

        if (d < best_dist)
        {
            best_dist = d;

            if (cp < bstart)
                best_idx = base;
            else
                best_idx = base + s_blocks[i].count - 1;
        }

        base += s_blocks[i].count;
    }

    return best_idx;
}

static const char *block_name_at(int flat_idx)
{
    int bidx = 0;
    glyph_at(flat_idx, &bidx);

    if (bidx >= 0 && bidx < NUM_BLOCKS)
        return s_blocks[bidx].name;

    return "";
}

/* Block selector popup: returns chosen block index or -2 on cancel */
static int ui_block_pick(int initial)
{
    int y, x, h, w;
    int want_h = LINES - 4;
    int want_w = 36;
    int i;
    int sel, top;
    int visible;
    int dirty = 1;
    int rc;
    wint_t wch;

    /* Calculate width based on longest block name */
    for (i = 0; i < NUM_BLOCKS; i++)
    {
        int len = (int)strlen(s_blocks[i].name);
        if (len + 6 > want_w)
            want_w = len + 6;
    }

    if (want_h < 14)
        want_h = 14;

    sel = (initial < 0) ? 0 : (initial + 1);

    if (sel > NUM_BLOCKS)
        sel = 0;

    ui_popup_center(want_h, want_w, &y, &x, &h, &w);

    visible = h - 4;

    if (visible < 1)
        visible = 1;

    top = 0;

    if (sel >= visible)
        top = sel - visible + 1;

    for (;;)
    {
        if (dirty)
        {
            int row;

            standend();

            ui_draw_popup_frame(y, x, h, w, "Select Unicode Block");

            for (row = 0; row < visible; row++)
            {
                int idx = top + row;
                const char *label = NULL;
                int j;

                if (idx > NUM_BLOCKS)
                    break;

                if (idx == 0)
                    label = "[All blocks]";
                else
                    label = s_blocks[idx - 1].name;

                if (idx == sel)
                    attron(COLOR_PAIR(COL_POPUP_SEL));
                else
                    attron(COLOR_PAIR(COL_POPUP));

                move(y + 2 + row, x + 2);

                for (j = 0; j < w - 4; j++)
                    addch(' ');

                mvaddnstr(y + 2 + row, x + 3, label, w - 6);

                if (idx == sel)
                    attroff(COLOR_PAIR(COL_POPUP_SEL));
                else
                    attroff(COLOR_PAIR(COL_POPUP));
            }

            refresh();
            dirty = 0;
        }

        rc = get_wch(&wch);

        if (rc == ERR)
            continue;

        if (wch == 27)
            return -2;

        if (wch == '\n' || wch == '\r' || wch == KEY_ENTER)
            return (sel == 0) ? -1 : (sel - 1);

        if (rc == KEY_CODE_YES)
        {
            int old_sel = sel;

            if (wch == KEY_UP)
            {
                if (sel > 0)
                    sel--;
            }
            else if (wch == KEY_DOWN)
            {
                if (sel < NUM_BLOCKS)
                    sel++;
            }
            else if (wch == KEY_PPAGE)
                sel = (sel > visible) ? sel - visible : 0;
            else if (wch == KEY_NPAGE)
                sel = (sel + visible <= NUM_BLOCKS) ? sel + visible : NUM_BLOCKS;
            else if (wch == KEY_HOME)
                sel = 0;
            else if (wch == KEY_END)
                sel = NUM_BLOCKS;

            if (sel != old_sel)
            {
                if (sel < top)
                    top = sel;
                else if (sel >= top + visible)
                    top = sel - visible + 1;

                if (top < 0)
                    top = 0;

                dirty = 1;
            }
        }
        else
        {
            /* Control key navigation */
            int old_sel = sel;

            if (wch == CTRL('D'))
                sel = (sel + visible <= NUM_BLOCKS) ? sel + visible : NUM_BLOCKS;
            else if (wch == CTRL('U'))
                sel = (sel > visible) ? sel - visible : 0;
            else if (wch == CTRL('E'))
                sel = NUM_BLOCKS;
            else if (wch == CTRL('B'))
                sel = 0;

            if (sel != old_sel)
            {
                if (sel < top)
                    top = sel;
                else if (sel >= top + visible)
                    top = sel - visible + 1;

                if (top < 0)
                    top = 0;

                dirty = 1;
            }
        }
    }
}

long ui_glyph_pick(void)
{
    int total;
    int popup_y, popup_x, popup_h, popup_w;
    int want_h, want_w;
    int sel;
    int top_row;
    int visible_rows;
    char hex_buf[8];
    int hex_len = 0;
    int dirty = 1;
    int rc;
    wint_t wch;

    total = glyph_total();

    if (total <= 0)
        return -1;

    want_w = GRID_COLS * CELL_W + 6;
    want_h = LINES - 4;

    if (want_h < 14)
        want_h = 14;

    curs_set(0);
    ui_popup_center(want_h, want_w, &popup_y, &popup_x, &popup_h, &popup_w);

    visible_rows = popup_h - 5 - 5;

    if (visible_rows < 4)
        visible_rows = 4;

    sel = 0;
    top_row = 0;
    hex_buf[0] = '\0';

    for (;;)
    {
        if (dirty)
        {
            int row;
            unsigned long sel_cp;
            int sel_block = 0;
            char info[160];
            const char *filter_label = NULL;

            standend();

            ui_draw_popup_frame(popup_y, popup_x, popup_h, popup_w, "Unicode Glyph Selector");

            attron(COLOR_PAIR(COL_POPUP));

            sel_cp = glyph_at(sel, &sel_block);

            if (hex_len > 0)
                snprintf(info, sizeof(info), "Code: U+%s_   (type hex to jump)", hex_buf);
            else
                snprintf(info, sizeof(info), "Code: U+%04lX   (type hex to jump)", sel_cp);

            mvaddnstr(popup_y + 1, popup_x + 2, info, popup_w - 4);

            filter_label = (s_filter < 0) ? "[All blocks]" : s_blocks[s_filter].name;

            if (s_filter < 0)
                snprintf(info, sizeof(info), "Filter: %s   (TAB)   Now: %s", filter_label, block_name_at(sel));
            else
                snprintf(info, sizeof(info), "Filter: %s   (TAB to change)", filter_label);

            mvaddnstr(popup_y + 3, popup_x + 2, info, popup_w - 4);
            attroff(COLOR_PAIR(COL_POPUP));

            /* Grid: paint background with ASCII, place glyph with mvaddnwstr(len=1) to avoid ncursesw_amiga bug */
            for (row = 0; row < visible_rows; row++)
            {
                int col;
                int row_idx = top_row + row;
                int line_y = popup_y + 5 + row;

                for (col = 0; col < GRID_COLS; col++)
                {
                    int flat = row_idx * GRID_COLS + col;
                    int cell_x = popup_x + 3 + col * CELL_W;
                    int is_sel = (flat == sel);
                    unsigned long cp;
                    wchar_t wcs[2];

                    if (flat >= total)
                    {
                        attron(COLOR_PAIR(COL_POPUP));
                        mvaddnstr(line_y, cell_x, "   ", CELL_W);
                        attroff(COLOR_PAIR(COL_POPUP));

                        continue;
                    }

                    cp = glyph_at(flat, NULL);

                    wcs[0] = (wchar_t)cp;
                    wcs[1] = L'\0';

                    if (is_sel)
                        attron(COLOR_PAIR(COL_POPUP_SEL));
                    else
                        attron(COLOR_PAIR(COL_POPUP));

                    mvaddnstr(line_y, cell_x, "   ", CELL_W);
                    mvaddnwstr(line_y, cell_x + 1, wcs, 1);

                    if (is_sel)
                        attroff(COLOR_PAIR(COL_POPUP_SEL));
                    else
                        attroff(COLOR_PAIR(COL_POPUP));
                }
            }

            attron(COLOR_PAIR(COL_POPUP));
            snprintf(info, sizeof(info), "Selected: U+%04lX", (unsigned long)sel_cp);
            mvaddnstr(popup_y + popup_h - 3, popup_x + 2, info, popup_w - 4);

            mvaddnstr(popup_y + popup_h - 2, popup_x + 2, "ENTER=Insert  ESC=Cancel  TAB=Filter  Arrows  hex=Jump", popup_w - 4);
            attroff(COLOR_PAIR(COL_POPUP));

            refresh();
            dirty = 0;
        }

        rc = get_wch(&wch);

        if (rc == ERR)
            continue;

        if (wch == 27)
        {
            curs_set(1);
            return -1;
        }

        if (wch == '\n' || wch == '\r' || wch == KEY_ENTER)
        {
            if (sel >= 0 && sel < total)
            {
                unsigned long cp = glyph_at(sel, NULL);

                curs_set(1);
                return (long)cp;
            }
        }

        /* TAB: open the block filter sub-popup */
        if (wch == '\t' || wch == 9)
        {
            int new_filter = ui_block_pick(s_filter);

            if (new_filter != -2 && new_filter != s_filter)
            {
                s_filter = new_filter;
                total = glyph_total();
                sel = 0;
                top_row = 0;
                hex_len = 0;
                hex_buf[0] = '\0';
            }

            dirty = 1;
            continue;
        }

        if (wch == KEY_BACKSPACE || wch == 127 || wch == 8)
        {
            if (hex_len > 0)
            {
                hex_buf[--hex_len] = '\0';
                dirty = 1;
            }

            continue;
        }

        if ((wch >= '0' && wch <= '9') || (wch >= 'a' && wch <= 'f') || (wch >= 'A' && wch <= 'F'))
        {
            if (hex_len < 6)
            {
                hex_buf[hex_len++] = (char)toupper((int)wch);
                hex_buf[hex_len] = '\0';

                if (hex_len >= 2)
                {
                    unsigned long cp = strtoul(hex_buf, NULL, 16);
                    int idx = glyph_find(cp);

                    if (idx >= 0)
                    {
                        sel = idx;
                        {
                            int sel_row = sel / GRID_COLS;

                            if (sel_row < top_row)
                                top_row = sel_row;
                            else if (sel_row >= top_row + visible_rows)
                                top_row = sel_row - visible_rows + 1;
                        }
                    }
                }
                dirty = 1;
            }

            continue;
        }

        if (rc == KEY_CODE_YES)
        {
            int old_sel = sel;

            if (wch == KEY_LEFT)
            {
                if (sel > 0)
                    sel--;
            }
            else if (wch == KEY_RIGHT)
            {
                if (sel + 1 < total)
                    sel++;
            }
            else if (wch == KEY_UP)
            {
                if (sel - GRID_COLS >= 0)
                    sel -= GRID_COLS;
            }
            else if (wch == KEY_DOWN)
            {
                if (sel + GRID_COLS < total)
                    sel += GRID_COLS;
                else if (sel < total - 1)
                    sel = total - 1;
            }
            else if (wch == KEY_PPAGE)
            {
                int delta = GRID_COLS * visible_rows;
                sel = (sel > delta) ? sel - delta : 0;
            }
            else if (wch == KEY_NPAGE)
            {
                int delta = GRID_COLS * visible_rows;
                sel = (sel + delta < total) ? sel + delta : total - 1;
            }
            else if (wch == KEY_HOME)
                sel = 0;
            else if (wch == KEY_END)
                sel = total - 1;

            if (sel != old_sel)
            {
                int sel_row = sel / GRID_COLS;

                if (sel_row < top_row)
                    top_row = sel_row;
                else if (sel_row >= top_row + visible_rows)
                    top_row = sel_row - visible_rows + 1;

                if (top_row < 0)
                    top_row = 0;

                hex_len = 0;
                hex_buf[0] = '\0';
                dirty = 1;
            }
        }
        else
        {
            int old_sel = sel;

            if (wch == CTRL('D'))
            {
                int delta = GRID_COLS * visible_rows;
                sel = (sel + delta < total) ? sel + delta : total - 1;
            }
            else if (wch == CTRL('U'))
            {
                int delta = GRID_COLS * visible_rows;
                sel = (sel > delta) ? sel - delta : 0;
            }
            else if (wch == CTRL('E'))
                sel = total - 1;
            else if (wch == CTRL('Y'))
                sel = 0;
            else if (wch == CTRL('B'))
            {
                if (sel > 0)
                    sel--;
            }

            if (sel != old_sel)
            {
                int sel_row = sel / GRID_COLS;

                if (sel_row < top_row)
                    top_row = sel_row;
                else if (sel_row >= top_row + visible_rows)
                    top_row = sel_row - visible_rows + 1;

                if (top_row < 0)
                    top_row = 0;

                hex_len = 0;
                hex_buf[0] = '\0';
                dirty = 1;
            }
        }
    }
}
