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
#include <ctype.h>
#include "../core/utf8.h"
#include "../core/charset.h"
#include "../core/clipboard.h"
#include "../components/editor.h"
#include "te.h"
#include "ui_files.h"
#include "ui_editor_helper.h"
#include "ui_setup.h"

/* Help text */
static const char *HELP_LINES[] =
    {
        "tinyedit -- Key Bindings",
        "",
        "  Navigation:",
        "    Arrows           Move cursor",
        "    Home / Ctrl+B    Line start",
        "    End / Ctrl+E     Line end",
        "    PgUp / Ctrl+U    Page up",
        "    PgDn / Ctrl+D    Page down",
        "    Ctrl+Left/Right  Word left / right",
        "    Ctrl+G           Go to start of document",
        "    Ctrl+K           Go to end of document",
        "",
        "  Editing:",
        "    Ctrl+Y           Delete line",
        "    Ctrl+Z           Undo",
        "    Alt+Z            Redo",
        "    Ctrl+T           Delete word right",
        "    Ctrl+_           Delete word left",
        "    Ins / Alt+I      Toggle insert / overwrite",
        "    Ctrl+W           Rewrap paragraph",
        "    Tab              Insert tab (4 spaces)",
        "    Alt+W            Toggle hard-wrap",
        "    Alt+L            Toggle line numbers",
        "    F3 / Alt+C       Choose output charset",
        "",
        "  Block (selection):",
        "    F6 / Alt+B       Mark / unmark block",
        "    Ctrl+C           Copy block",
        "    Ctrl+X           Cut block",
        "    Ctrl+V           Paste block (or clipboard)",
        "    BS / Del         Delete block (no clipboard)",
        "    Ctrl+O           Export block to file",
        "",
        "  Search:",
        "    F5 / Alt+F       Search (show all matches)",
        "    Ctrl+R           Find & replace",
        "    Ctrl+Q           Go to line",
        "    Alt+G            Clear search highlights",
        "",
        "  Files:",
        "    F7 / Alt+O       Insert file at cursor",
        "    Ctrl+L           Open file (clears editor)",
        "    Ctrl+N           New file (clears editor)",
        "    F2 / Ctrl+S      Save",
        "    ESC / F10        Quit (confirm if modified)",
        "    F1 / ?           This help",
        "",
        "  Other:",
        "    F4 / Alt+S       Setup / configuration",
};
#define HELP_N ((int)(sizeof(HELP_LINES) / sizeof(HELP_LINES[0])))

/* Soft-wrap viewport state (module-level, reset on editor_run) */
static int s_soft_vtop = 0;
static int s_soft_desired_vcol = -1;
static int s_soft_last_width = -1;

static void soft_reset_desired(void)
{
    s_soft_desired_vcol = -1;
}

/* Soft-wrap: returns the end of the current visual segment (exclusive)
 * Breaks at the last space boundary that fits within width columns
 * If no space fits (word longer than width), hard-cuts at start+width
 * The next segment starts exactly at the returned position - no chars skipped */
static int wrap_next(const wchar_t *line, int len, int width, int start)
{
    int hard_end;
    int k;

    if (width < 1)
        width = 1;

    hard_end = start + width;

    if (hard_end >= len)
        return len;

    /* Search backwards from hard_end for a space to break at */
    for (k = hard_end; k > start; k--)
    {
        if (line[k - 1] == L' ' || line[k - 1] == L'\t')
            return k;
    }

    /* No space found: hard cut */
    return hard_end;
}

/* Number of visual sub-rows a logical line occupies (>= 1) */
static int wrap_count(const wchar_t *line, int len, int width)
{
    int pos = 0;
    int rows = 1;

    if (len <= 0)
        return 1;

    for (;;)
    {
        int end = wrap_next(line, len, width, pos);

        if (end >= len)
            break;

        if (end <= pos)
            end = pos + (width < 1 ? 1 : width); /* never stall */

        pos = end;
        rows++;
    }

    return rows;
}

/* Visual row/col of cursor within soft-wrapped text */
static void soft_cursor_vpos(const TeApp *app, int width, int *out_vrow, int *out_vcol)
{
    EdInfo info;
    int li, vrow = 0;
    const wchar_t *l;
    int len;
    int pos;
    int col;

    ed_get_info(app->editor, &info);

    for (li = 0; li < info.row && li < info.line_count; li++)
    {
        const wchar_t *l = ed_line_wcs(app->editor, li);
        int len = ed_line_len(app->editor, li);

        vrow += wrap_count(l ? l : L"", l ? len : 0, width);
    }

    l = ed_line_wcs(app->editor, info.row);
    len = ed_line_len(app->editor, info.row);
    pos = 0;
    col = info.col;

    if (l && len > 0)
    {
        for (;;)
        {
            int end = wrap_next(l, len, width, pos);

            /* Cursor is on this sub-row if it sits before the next
             * sub-row's start, or this is the last sub-row */
            if (end >= len || col < end)
            {
                if (out_vcol)
                {
                    int vc = col - pos;

                    if (vc < 0)
                        vc = 0;

                    *out_vcol = vc;
                }

                break;
            }

            if (end <= pos)
                end = pos + (width < 1 ? 1 : width);

            pos = end;
            vrow++;
        }
    }
    else if (out_vcol)
    {
        *out_vcol = 0;
    }

    if (out_vrow)
        *out_vrow = vrow;
}

/* Total visual rows occupied by logical lines [0..upto) at the given width */
static int soft_count_rows_before(Ed *ed, int upto, int width)
{
    int total = 0;
    int li;

    for (li = 0; li < upto; li++)
    {
        const wchar_t *l = ed_line_wcs(ed, li);
        int len = ed_line_len(ed, li);

        total += wrap_count(l ? l : L"", l ? len : 0, width);
    }

    return total;
}

/* Within one logical line, find the logical column that corresponds to
 * (vrow_in_line, target_vcol). vrow_in_line is the sub-row offset within
 * this line (0 = first sub-row)
 *
 * Returns 0 on success, -1 if vrow_in_line is beyond this line's sub-rows
 * If target_vcol overruns the sub-row's content, the result is clamped to
 * the end of that sub-row's segment - matching what the user sees */
static int soft_seg_at(const wchar_t *l, int len, int width, int vrow_in_line, int target_vcol, int *out_col)
{
    int pos = 0;
    int sub = 0;

    if (!l || len <= 0)
    {
        if (vrow_in_line == 0)
        {
            *out_col = 0;
            return 0;
        }

        return -1;
    }

    for (;;)
    {
        int seg_end = wrap_next(l, len, width, pos);
        int seg_len = seg_end - pos;
        int is_last = (seg_end >= len);
        int v;

        if (sub == vrow_in_line)
        {
            v = target_vcol;
            if (v < 0)
                v = 0;

            /* End on last segment: go to insertion point (seg_len)
             * End on interior segment: stay on last visible char (seg_len-1)
             * so cursor doesn't jump to next row */
            if (v >= seg_len)
            {
                if (is_last)
                    v = seg_len;
                else
                    v = (seg_len > 0) ? seg_len - 1 : 0;
            }

            *out_col = pos + v;

            return 0;
        }

        sub++;

        if (seg_end >= len)
            return -1; /* requested vrow past last sub-row */

        if (seg_end <= pos)
            seg_end = pos + (width < 1 ? 1 : width);

        pos = seg_end;
    }
}

/* Convert an absolute visual position (target_vrow, target_vcol) - across
 * all logical lines - to (row, col) for ed_set_pos
 *
 * Out-of-range vrow clamps to the last visual row of the document
 * Out-of-range vcol clamps to the end of the destination sub-row */
static void soft_visual_to_logical(Ed *ed, int width, int target_vrow, int target_vcol, int *out_row, int *out_col)
{
    EdInfo info;
    int li;
    int vacc = 0;

    ed_get_info(ed, &info);

    if (target_vrow < 0)
        target_vrow = 0;

    for (li = 0; li < info.line_count; li++)
    {
        const wchar_t *l = ed_line_wcs(ed, li);
        int len = ed_line_len(ed, li);
        int wc = wrap_count(l ? l : L"", l ? len : 0, width);

        if (target_vrow < vacc + wc)
        {
            int vrow_in_line = target_vrow - vacc;
            int col = 0;

            if (soft_seg_at(l, len, width, vrow_in_line, target_vcol, &col) == 0)
            {
                *out_row = li;
                *out_col = col;

                return;
            }
        }

        vacc += wc;
    }

    /* Past the end: clamp to last line's last sub-row */
    if (info.line_count > 0)
    {
        int last = info.line_count - 1;
        const wchar_t *l = ed_line_wcs(ed, last);
        int len = ed_line_len(ed, last);
        int wc = wrap_count(l ? l : L"", l ? len : 0, width);
        int col = 0;

        if (wc > 0 && soft_seg_at(l, len, width, wc - 1, target_vcol, &col) == 0)
        {
            *out_row = last;
            *out_col = col;

            return;
        }

        *out_row = last;
        *out_col = len;

        return;
    }

    *out_row = 0;
    *out_col = 0;
}

/* Move cursor one visual row up. Preserves desired_vcol so consecutive
 * UPs over shorter rows still land back on the same column on long rows */
static void soft_move_up_visual(TeApp *app, int width)
{
    int vr, vc, nr, nc;

    soft_cursor_vpos(app, width, &vr, &vc);

    /* Already on the first visual row: nothing to do (matches ed_move_up) */
    if (vr <= 0)
        return;

    if (s_soft_desired_vcol < 0)
        s_soft_desired_vcol = vc;

    soft_visual_to_logical(app->editor, width, vr - 1, s_soft_desired_vcol, &nr, &nc);
    ed_set_pos(app->editor, nr, nc);
    ed_ensure_visible(app->editor);
}

/* Move cursor one visual row down. Mirror of soft_move_up_visual */
static void soft_move_down_visual(TeApp *app, int width)
{
    EdInfo info;
    int total_vrows;
    int vr, vc, nr, nc;

    ed_get_info(app->editor, &info);

    if (info.line_count <= 0)
        return;

    total_vrows = soft_count_rows_before(app->editor, info.line_count, width);
    soft_cursor_vpos(app, width, &vr, &vc);

    /* Already on the last visual row: nothing to do (matches ed_move_down) */
    if (vr >= total_vrows - 1)
        return;

    if (s_soft_desired_vcol < 0)
        s_soft_desired_vcol = vc;

    soft_visual_to_logical(app->editor, width, vr + 1, s_soft_desired_vcol, &nr, &nc);
    ed_set_pos(app->editor, nr, nc);
    ed_ensure_visible(app->editor);
}

/* HOME in soft-wrap: go to column 0 of current visual row */
static void soft_move_home_visual(TeApp *app, int width)
{
    int vr, vc, nr, nc;

    soft_cursor_vpos(app, width, &vr, &vc);
    soft_visual_to_logical(app->editor, width, vr, 0, &nr, &nc);
    ed_set_pos(app->editor, nr, nc);
    ed_ensure_visible(app->editor);
}

/* END in soft-wrap: go to last column of current visual row */
static void soft_move_end_visual(TeApp *app, int width)
{
    int vr, vc, nr, nc;

    soft_cursor_vpos(app, width, &vr, &vc);

    /* Use large value (not INT_MAX) to clamp to segment end */
    soft_visual_to_logical(app->editor, width, vr, width + 1000000, &nr, &nc);

    ed_set_pos(app->editor, nr, nc);
    ed_ensure_visible(app->editor);
}

/* Move pg visual rows up. Preserves desired_vcol */
static void soft_move_pgup_visual(TeApp *app, int width, int pg)
{
    int vr, vc, nr, nc;
    int target;

    if (pg <= 0)
        pg = 1;

    soft_cursor_vpos(app, width, &vr, &vc);

    if (s_soft_desired_vcol < 0)
        s_soft_desired_vcol = vc;

    target = vr - pg;

    if (target < 0)
        target = 0;

    soft_visual_to_logical(app->editor, width, target, s_soft_desired_vcol, &nr, &nc);
    ed_set_pos(app->editor, nr, nc);

    /* Scroll viewport so the cursor stays at roughly the same screen row */
    s_soft_vtop -= pg;

    if (s_soft_vtop < 0)
        s_soft_vtop = 0;

    ed_ensure_visible(app->editor);
}

/* Move pg visual rows down. Mirror of soft_move_pgup_visual */
static void soft_move_pgdn_visual(TeApp *app, int width, int pg)
{
    EdInfo info;
    int total_vrows;
    int vr, vc, nr, nc;
    int target;

    if (pg <= 0)
        pg = 1;

    ed_get_info(app->editor, &info);

    if (info.line_count <= 0)
        return;

    total_vrows = soft_count_rows_before(app->editor, info.line_count, width);
    soft_cursor_vpos(app, width, &vr, &vc);

    if (s_soft_desired_vcol < 0)
        s_soft_desired_vcol = vc;

    target = vr + pg;

    if (target > total_vrows - 1)
        target = total_vrows - 1;

    if (target < 0)
        target = 0;

    soft_visual_to_logical(app->editor, width, target, s_soft_desired_vcol, &nr, &nc);
    ed_set_pos(app->editor, nr, nc);

    s_soft_vtop += pg;

    if (s_soft_vtop < 0)
        s_soft_vtop = 0;

    ed_ensure_visible(app->editor);
}

/* Word-wrap UTF-8 paste to col columns, preserving newlines. No hard-breaks for URLs/code */
static int paste_char_width(wchar_t c)
{
    /* For FTN editing: does this char take 1 column or 0? Zero-width cases: combining marks, BOM. CJK treated as 1 */
    if (c == 0)
        return 0;

    if (c < 0x20)
        return 0; /* control chars, including \r */

    if (c >= 0x0300 && c <= 0x036F)
        return 0; /* combining diacritical marks */

    if (c == 0x200B || c == 0xFEFF)
        return 0; /* ZWSP / BOM */

    return 1;
}

static char *wrap_paste_text(const char *utf8, int col)
{
    wchar_t *w;
    int wlen = 0;
    int i;
    int line_start = 0;
    int last_space = -1;
    int col_pos = 0;
    int out_cap, out_len = 0;
    wchar_t *out;
    char *result;

    if (col <= 0)
        return NULL;

    w = utf8_to_wcs(utf8, &wlen);

    if (!w)
        return NULL;

    out_cap = wlen + (wlen / col) + 16;
    out = (wchar_t *)malloc((size_t)out_cap * sizeof(wchar_t));

    if (!out)
    {
        free(w);
        return NULL;
    }

    for (i = 0; i < wlen; i++)
    {
        wchar_t ch = w[i];
        int cw;

        if (ch == L'\n')
        {
            if (out_len < out_cap)
                out[out_len++] = L'\n';

            line_start = out_len;
            last_space = -1;
            col_pos = 0;

            continue;
        }
        if (ch == L'\r')
            continue;

        cw = paste_char_width(ch);

        if (ch == L'\t')
            cw = 1;

        if (out_len < out_cap)
            out[out_len++] = ch;

        if (ch == L' ' || ch == L'\t')
            last_space = out_len - 1;

        col_pos += cw;

        if (col_pos > col && last_space > line_start)
        {
            int new_start;
            int j;
            int width_after = 0;

            out[last_space] = L'\n';
            new_start = last_space + 1;

            for (j = new_start; j < out_len; j++)
            {
                int w2 = (out[j] == L'\t') ? 1 : paste_char_width(out[j]);
                width_after += w2;
            }

            line_start = new_start;
            last_space = -1;
            col_pos = width_after;
        }
    }

    free(w);

    if (out_len < out_cap)
        out[out_len] = L'\0';
    else
        out[out_cap - 1] = L'\0';

    result = wcs_to_utf8(out, out_len);
    free(out);

    return result;
}

/* Effective wrap column. Clamp AUTOWRAP to COLS-1; 0=disabled */
static int editor_eff_wrap(const TeApp *app)
{
    int cfgw = app->wrap_col;
    int limit = COLS - 1; /* leave one column of margin */

    if (cfgw <= 0)
        return 0; /* AUTOWRAP disabled in config: never wrap */

    if (COLS <= 10)
        return 0; /* Unusably narrow: scroll instead of wrapping */

    if (cfgw > limit)
        return limit; /* Screen narrower than the configured column */

    return cfgw;
}

/* Calculate width needed for line numbers (digits + 1 space) */
static int lineno_width(int line_count)
{
    int width = 1;
    int n = line_count;

    if (n <= 0)
        n = 1;

    while (n >= 10)
    {
        n /= 10;
        width++;
    }

    return width + 1; /* +1 for space after number */
}

/* Body drawing */
static void draw_body(TeApp *app)
{
    EdInfo info;
    int body_top = 1;         /* row 0 = titlebar */
    int body_bot = LINES - 2; /* last row = statusbar */
    int body_rows = body_bot - body_top;
    int width = COLS;
    int soft = !app->hard_wrap;
    int b_r1 = -1;
    int b_c1 = 0;
    int b_r2 = -1;
    int b_c2 = 0;
    int screen_row;
    int ln_width = 0;  /* line number width */
    int ln_offset = 0; /* offset for editor content */
    int show_lnum = app->show_line_numbers;

    if (body_rows < 1)
        body_rows = 1;

    ed_set_page(app->editor, body_rows);
    ed_ensure_visible(app->editor);
    ed_get_info(app->editor, &info);

    /* Calculate line number width if enabled */
    if (show_lnum)
    {
        ln_width = lineno_width(info.line_count);
        ln_offset = ln_width;
        width = COLS - ln_offset; /* Reduce available width for text */
    }

    /* Normalize block range */
    if (info.block.active)
    {
        if (info.block.anchor_row < info.row || (info.block.anchor_row == info.row && info.block.anchor_col <= info.col))
        {
            b_r1 = info.block.anchor_row;
            b_c1 = info.block.anchor_col;
            b_r2 = info.row;
            b_c2 = info.col;
        }
        else
        {
            b_r1 = info.row;
            b_c1 = info.col;
            b_r2 = info.block.anchor_row;
            b_c2 = info.block.anchor_col;
        }
    }

    attron(COLOR_PAIR(COL_NORMAL));

    screen_row = body_top;

    /* SOFT-WRAP: one logical line spans several screen rows */
    if (soft)
    {
        int cur_vrow, cur_vcol;
        int li, sr, vrow;

        soft_cursor_vpos(app, width, &cur_vrow, &cur_vcol);

        /* Keep the cursor's visual row inside [s_soft_vtop, +body_rows) */
        if (cur_vrow < s_soft_vtop)
            s_soft_vtop = cur_vrow;
        else if (cur_vrow >= s_soft_vtop + body_rows)
            s_soft_vtop = cur_vrow - body_rows + 1;

        if (s_soft_vtop < 0)
            s_soft_vtop = 0;

        /* Walk logical lines, emitting visual sub-rows, skipping the
         * first s_soft_vtop of them, filling `body_rows` screen rows */
        for (sr = 0; sr < body_rows; sr++)
        {
            move(body_top + sr, 0);
            clrtoeol();
        }

        vrow = 0; /* absolute visual row being produced */
        sr = 0;   /* screen row index 0..body_rows-1 */

        for (li = 0; li < info.line_count && sr < body_rows; li++)
        {
            const wchar_t *l = ed_line_wcs(app->editor, li);
            int len = ed_line_len(app->editor, li);
            int pos = 0;
            int done = 0;
            int first_seg = 1; /* Track first segment of each line for line number */

            while (!done && sr < body_rows)
            {
                int seg_start = pos;
                int seg_end, seg_len, np;

                if (l && len > 0)
                {
                    seg_end = wrap_next(l, len, width, pos);
                    np = seg_end;
                }
                else
                {
                    seg_end = 0;
                    np = len; /* empty line: one (blank) sub-row */
                }

                if (vrow >= s_soft_vtop)
                {
                    seg_len = seg_end - seg_start;

                    if (seg_len < 0)
                        seg_len = 0;

                    /* Draw line number on first segment of each line */
                    if (show_lnum && first_seg)
                    {
                        attron(COLOR_PAIR(COL_BORDER));
                        mvprintw(body_top + sr, 0, "%*d", ln_width - 1, li + 1);
                        attroff(COLOR_PAIR(COL_BORDER));

                        first_seg = 0;
                    }

                    if (l && seg_len > 0)
                        mvaddnwstr(body_top + sr, ln_offset, &l[seg_start], seg_len);

                    /* Highlight search matches in softwrap */
                    if (app->search.rows && app->search.cols && app->search.count > 0)
                    {
                        int j;
                        int match_len = (int)wcslen(app->search.query);

                        for (j = 0; j < app->search.count; j++)
                        {
                            if (app->search.rows[j] == li)
                            {
                                int match_col = app->search.cols[j];
                                int match_end = match_col + match_len;

                                /* Check if match is within this segment */
                                if (match_col >= seg_start && match_end <= seg_end)
                                {
                                    /* Match entirely within this segment */
                                    attron(COLOR_PAIR(COL_SEARCH_MATCH));
                                    mvaddnwstr(body_top + sr, ln_offset + match_col - seg_start, &l[match_col], match_len);
                                    attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                                }
                                else if (match_col >= seg_start && match_col < seg_end)
                                {
                                    /* Match starts in this segment, continues to next */
                                    int partial_len = seg_end - match_col;
                                    attron(COLOR_PAIR(COL_SEARCH_MATCH));
                                    mvaddnwstr(body_top + sr, ln_offset + match_col - seg_start, &l[match_col], partial_len);
                                    attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                                }
                                else if (match_end > seg_start && match_end <= seg_end)
                                {
                                    /* Match ends in this segment, started in previous */
                                    int partial_len = match_end - seg_start;
                                    attron(COLOR_PAIR(COL_SEARCH_MATCH));
                                    mvaddnwstr(body_top + sr, ln_offset, &l[seg_start], partial_len);
                                    attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                                }
                            }
                        }
                    }

                    /* Block-selection overlay (logical-span) */
                    if (b_r1 >= 0 && li >= b_r1 && li <= b_r2 && l)
                    {
                        int hs = (li == b_r1) ? b_c1 : 0;
                        int he = (li == b_r2) ? b_c2 : len;

                        if (hs < seg_start)
                            hs = seg_start;

                        if (he > seg_end)
                            he = seg_end;

                        if (hs < he)
                        {
                            attron(A_REVERSE);
                            mvaddnwstr(body_top + sr, ln_offset + hs - seg_start, &l[hs], he - hs);
                            attroff(A_REVERSE);
                        }
                        else if (hs == seg_start && he == seg_start)
                        {
                            /* Cursor at col 0 or empty line: show one reversed space */
                            attron(A_REVERSE);
                            mvaddch(body_top + sr, ln_offset, ' ');
                            attroff(A_REVERSE);
                        }
                    }

                    sr++;
                }

                if (np >= len)
                    done = 1;

                if (np <= pos)
                    np = pos + (width < 1 ? 1 : width);

                pos = np;
                vrow++;
            }
        }
    }
    else /* HARD-WRAP: classic 1 logical line == 1 screen row */
    {
        int i;

        for (i = 0; i < body_rows; i++)
        {
            int line_idx = info.top + i;
            int line_len;
            const wchar_t *wl;

            move(body_top + i, 0);
            clrtoeol();

            if (line_idx >= info.line_count)
                continue;

            /* Draw line number if enabled */
            if (show_lnum)
            {
                attron(COLOR_PAIR(COL_BORDER));
                mvprintw(body_top + i, 0, "%*d", ln_width - 1, line_idx + 1);
                attroff(COLOR_PAIR(COL_BORDER));
            }

            /* mvaddnwstr: n is in wide chars, no UTF-8 conversion needed */
            wl = ed_line_wcs(app->editor, line_idx);
            line_len = ed_line_len(app->editor, line_idx);

            if (wl && line_len > 0)
                mvaddnwstr(body_top + i, ln_offset, wl, line_len);

            /* Highlight search matches */
            if (app->search.rows && app->search.cols && app->search.count > 0)
            {
                int j;
                int match_len = (int)wcslen(app->search.query);

                for (j = 0; j < app->search.count; j++)
                {
                    if (app->search.rows[j] == line_idx)
                    {
                        int match_col = app->search.cols[j];

                        if (match_col >= 0 && match_col + match_len <= line_len)
                        {
                            attron(COLOR_PAIR(COL_SEARCH_MATCH));
                            mvaddnwstr(body_top + i, ln_offset + match_col, &wl[match_col], match_len);
                            attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
            }

            /* Block-selection overlay (logical-span) */
            if (b_r1 >= 0 && line_idx >= b_r1 && line_idx <= b_r2)
            {
                const wchar_t *wcs;
                int hs, he;

                hs = (line_idx == b_r1) ? b_c1 : 0;
                he = (line_idx == b_r2) ? b_c2 : line_len;

                if (hs < 0)
                    hs = 0;

                if (he > line_len)
                    he = line_len;

                wcs = wl;

                if (hs < he)
                {
                    attron(A_REVERSE);
                    mvaddnwstr(body_top + i, ln_offset + hs, &wcs[hs], he - hs);
                    attroff(A_REVERSE);
                }
                else if (hs == 0 && he == 0)
                {
                    /* Cursor at col 0 or empty line: show one reversed space */
                    attron(A_REVERSE);
                    mvaddch(body_top + i, ln_offset, ' ');
                    attroff(A_REVERSE);
                }
            }
        }
    }

    attroff(COLOR_PAIR(COL_NORMAL));
}

/* Position the actual terminal cursor on the editor cursor */
static void position_cursor(TeApp *app)
{
    EdInfo info;
    int body_top = 1;
    int body_rows = LINES - 2;
    int width = COLS;
    int soft = !app->hard_wrap;
    int ln_offset = 0;
    int show_lnum = app->show_line_numbers;

    if (body_rows < 1)
        body_rows = 1;

    ed_get_info(app->editor, &info);

    /* Calculate line number offset if enabled */
    if (show_lnum)
    {
        ln_offset = lineno_width(info.line_count);
        width = COLS - ln_offset;
    }

    if (soft)
    {
        int vrow = 0, vcol = 0;
        int cy, cx;
        int max_y;

        soft_cursor_vpos(app, width, &vrow, &vcol);
        cy = body_top + (vrow - s_soft_vtop);
        cx = ln_offset + vcol;

        /* Clamp to body region */
        max_y = body_top + body_rows - 1;

        if (max_y < body_top)
            max_y = body_top;

        if (cy < body_top)
            cy = body_top;

        if (cy > max_y)
            cy = max_y;

        if (cx < ln_offset)
            cx = ln_offset;

        if (cx > COLS - 1)
            cx = COLS - 1;

        move(cy, cx);
    }
    else
    {
        int cy = body_top + (info.row - info.top);
        int cx = ln_offset + info.col;

        if (cy >= LINES - 1)
            cy = LINES - 2;

        if (cx >= COLS)
            cx = COLS - 1;

        move(cy, cx);
    }

    curs_set(1);
}

/* Paste helpers */
static char *collect_bracketed_paste(void)
{
    wchar_t *wbuf = NULL;
    int wlen = 0, wcap = 0;
    char *out;

    for (;;)
    {
        wint_t wch;
        int wrc = wrapper_read_key(&wch);

        if (wrc == ERR)
            break;

        if (wrc == KEY_CODE_YES && (int)wch == KEY_PASTE_END)
            break;

        if (wrc == KEY_CODE_YES)
            continue;

        if (wch != L'\n' && wch != L'\t' && wch < 0x20)
            continue;

        if (wlen + 1 >= wcap)
        {
            int ncap = wcap ? wcap * 2 : 256;
            wchar_t *nb = (wchar_t *)realloc(wbuf, (size_t)ncap * sizeof(wchar_t));

            if (!nb)
            {
                free(wbuf);
                return NULL;
            }

            wbuf = nb;
            wcap = ncap;
        }

        wbuf[wlen++] = (wchar_t)wch;
    }

    if (!wbuf || wlen == 0)
    {
        free(wbuf);
        return NULL;
    }

    out = wcs_to_utf8(wbuf, wlen);
    free(wbuf);

    return out;
}

/* Save */
static int do_save(TeApp *app)
{
    char *utf8;
    FILE *fp;
    int r = 0;
    char *text;
    char dir_input[1024];
    char name_input[1024];
    const char *last_slash;

    /* Extract directory and name from current filename */
    if (app->filename[0])
    {
        last_slash = strrchr(app->filename, '/');

        if (!last_slash)
            last_slash = strrchr(app->filename, '\\');

        if (last_slash)
        {
            int dir_len = (int)(last_slash - app->filename);

            strncpy(dir_input, app->filename, dir_len);
            dir_input[dir_len] = '\0';

            strncpy(name_input, last_slash + 1, sizeof(name_input) - 1);
            name_input[sizeof(name_input) - 1] = '\0';
        }
        else
        {
            dir_input[0] = '\0';
            strncpy(name_input, app->filename, sizeof(name_input) - 1);
            name_input[sizeof(name_input) - 1] = '\0';
        }
    }
    else
    {
        dir_input[0] = '\0';
        name_input[0] = '\0';
    }

    /* Always show save dialog */
    if (ui_files_save("Save as", dir_input, name_input, app->filename, sizeof(app->filename)) != 0)
        return -1;

    if (!app->filename[0])
        return -1;

    utf8 = ed_to_string(app->editor);

    if (!utf8)
    {
        te_status(app, "Memory error");
        return -1;
    }

    fp = fopen(app->filename, "wb");

    if (!fp)
    {
        te_status(app, "Cannot write: %s", app->filename);
        free(utf8);
        return -1;
    }

    /* Charset conversion if requested using charset_out */
    if (app->charset_out[0] && strcasecmp(app->charset_out, "UTF-8") != 0 && strcasecmp(app->charset_out, "UTF8") != 0)
    {
        int srclen = (int)strlen(utf8);
        int dstsz = srclen * 2 + 16;
        char *converted = (char *)malloc((size_t)dstsz);

        if (converted)
        {
            int n = charset_body_from_utf8(app->charset_out, utf8, srclen, converted, dstsz);

            if (n > 0)
            {
                fwrite(converted, 1, (size_t)n, fp);
                free(converted);
            }
            else
            {
                free(converted);
                fwrite(utf8, 1, strlen(utf8), fp);
            }
        }
        else
        {
            fwrite(utf8, 1, strlen(utf8), fp);
        }
    }
    else
    {
        fwrite(utf8, 1, strlen(utf8), fp);
    }

    fclose(fp);

    /* Update raw_bytes to match what was written to disk so charset re-decode stays current.
     * If we encoded to charset_out, re-read those bytes; otherwise raw_bytes = utf8 (UTF-8) */
    if (app->charset_out[0] && strcasecmp(app->charset_out, "UTF-8") != 0 && strcasecmp(app->charset_out, "UTF8") != 0)
    {
        int srclen = (int)strlen(utf8);

        free(app->raw_bytes);

        app->raw_bytes = (char *)malloc(srclen + 1);

        if (app->raw_bytes)
        {
            memcpy(app->raw_bytes, utf8, srclen + 1);
            app->raw_len = srclen;
        }
        else
        {
            app->raw_len = 0;
        }
    }
    else
    {
        /* UTF-8: raw_bytes = utf8 (ownership transferred) */
        free(app->raw_bytes);

        app->raw_bytes = utf8;
        app->raw_len = (int)strlen(utf8);
        utf8 = NULL; /* Don't free below */
    }

    if (utf8)
        free(utf8);

    /* Clear modified flag */
    ed_set_modified(app->editor, 0);

    te_status(app, "Saved: %s", app->filename);

    return r;
}

/* Handle function keys (F1-F7) */
static int handle_function_keys(TeApp *app, int ch, int is_key)
{
    /* F1 / ? : help */
    if ((is_key && ch == KEY_F(1)) || (!is_key && ch == '?'))
    {
        ui_popup_help("tinyedit Help", HELP_LINES, HELP_N);
        return 1;
    }

    /* F2 / Ctrl+S : save */
    if ((is_key && ch == KEY_F(2)) || (!is_key && ch == CTRL('S')))
    {
        do_save(app);
        return 1;
    }

    /* F3 / Alt+C : charset OR Previous match in search mode */
    if ((is_key && ch == KEY_F(3)) || (ch == KEY_ALT('C')))
    {
        if (app->search.only_mode || (app->search.is_mode && app->search.count > 0))
            return search_prev(app);
        else
            return charset_select(app);
    }

    /* F4 / Alt+S : Setup or Next match in search mode */
    if ((is_key && ch == KEY_F(4)) || (ch == KEY_ALT('S')))
    {
        if (app->search.only_mode || (app->search.is_mode && app->search.count > 0))
            return search_next(app);
        else
        {
            /* Normal setup functionality */
            char old_charset[TE_CFG_STR_MAX];
            int old_hard_wrap = app->hard_wrap;

            strncpy(old_charset, app->cfg.charset, sizeof(old_charset) - 1);
            old_charset[sizeof(old_charset) - 1] = '\0';

            if (ui_setup_run(&app->cfg, app->cfg_path) == 1)
            {
                /* Config saved: apply changes */
                if (app->cfg.undo_levels > 0)
                    ed_set_undo_levels(app->editor, app->cfg.undo_levels);

                /* Check if hard_wrap changed from 0 to 1 (soft to hard) */
                if (old_hard_wrap == 0 && app->cfg.hard_wrap == 1)
                {
                    /* Ask user if they want to rewrap the document */
                    char msg[128];

                    snprintf(msg, sizeof(msg), "Convert document to hard-wrap at column %d?", app->cfg.autowrap_col);

                    if (ui_popup_confirm("Hard Wrap", msg) == 1)
                    {
                        ed_rewrap_document(app->editor, app->cfg.autowrap_col);
                        te_status(app, "Document rewrapped to hard-wrap");
                    }
                }

                app->hard_wrap = app->cfg.hard_wrap;
                ed_set_hard_wrap(app->editor, app->cfg.hard_wrap);
                app->wrap_col = app->cfg.autowrap_col;
                app->show_line_numbers = app->cfg.show_line_numbers;

                /* If charset changed in setup, update charset_out */
                if (strcasecmp(old_charset, app->cfg.charset) != 0)
                {
                    strncpy(app->charset_out, app->cfg.charset, sizeof(app->charset_out) - 1);
                    app->charset_out[sizeof(app->charset_out) - 1] = '\0';
                }

                te_init_colors(&app->cfg);
            }

            return 1;
        }
    }

    /* F5 / Alt+F : search */
    if ((is_key && ch == KEY_F(5)) || (ch == KEY_ALT('F')))
    {
        if (app->search.is_mode && app->search.count > 0)
        {
            return replace_current(app);
        }
        else if (!app->search.only_mode && !app->search.is_mode)
            return do_search(app);
    }

    /* F6 / Alt+B : toggle block anchor OR Replace All in search mode */
    if ((is_key && ch == KEY_F(6)) || (ch == KEY_ALT('B')))
    {
        if (app->search.is_mode && app->search.count > 0)
            return replace_all(app);
        else if (!app->search.only_mode)
        {
            /* Normal block anchor functionality */
            ed_block_anchor(app->editor);
            return 1;
        }
    }

    /* F7 / Alt+O : insert file */
    if ((is_key && ch == KEY_F(7)) || (ch == KEY_ALT('O')))
        return insert_file(app);

    return 0; /* not handled */
}

/* Handle control key combinations (Ctrl+...) */
static int handle_control_keys(TeApp *app, int ch, int is_key)
{
    /* Ctrl+V : paste from internal buffer or system clipboard */
    if (!is_key && ch == CTRL('V'))
        return paste(app);

    /* Ctrl+Q : goto line */
    if (!is_key && ch == CTRL('Q'))
        return ui_editor_goto_line(app);

    /* Ctrl+R : find & replace */
    if (!is_key && ch == CTRL('R'))
        return replace(app);

    /* Ctrl+C : copy block */
    if (!is_key && ch == CTRL('C'))
        return copy(app);

    /* Ctrl+X : cut block */
    if (!is_key && ch == CTRL('X'))
        return cut(app);

    /* Ctrl+O : export block to file */
    if (!is_key && ch == CTRL('O'))
        return ui_editor_export(app);

    /* Ctrl+L : open file (clears editor) */
    if (!is_key && ch == CTRL('L'))
    {
        EdInfo info;
        ed_get_info(app->editor, &info);

        if (info.modified)
        {
            if (ui_popup_confirm("Open file", "Unsaved changes: opening file will lose edits. Continue?") != 1)
                return 1;
        }

        return ui_files_open(app);
    }

    /* Ctrl+N : new file (clears editor) */
    if (!is_key && ch == CTRL('N'))
    {
        EdInfo info;
        int has_content;

        ed_get_info(app->editor, &info);

        has_content = info.modified || info.line_count > 1;

        if (!has_content && info.line_count == 1)
        {
            int first_line_len = ed_line_len(app->editor, 0);
            has_content = (first_line_len > 0);
        }

        if (has_content)
        {
            if (ui_popup_confirm("New file", "Editor has content: new file will clear it. Continue?") != 1)
                return 1;
        }

        ed_load(app->editor, "");
        app->filename[0] = '\0';

        free(app->raw_bytes);

        app->raw_bytes = NULL;
        app->raw_len = 0;
        te_status(app, "[No Name]");

        return 1;
    }

    /* Ctrl+W : rewrap paragraph */
    if (!is_key && ch == CTRL('W'))
        return rewrap(app);

    /* go to start */
    if (!is_key && ch == CTRL('G'))
        return ui_editor_goto_start(app);

    /* go to end */
    if (!is_key && ch == CTRL('K'))
        return ui_editor_goto_end(app);

    return 0;
}

/* Handle navigation keys (arrow keys, page up/down, etc.) */
static int handle_navigation_keys(TeApp *app, int ch, int soft, int width, int body_rows, int *preserve_desired)
{
    switch (ch)
    {
    case KEY_UP:
        if (soft)
        {
            soft_move_up_visual(app, width);
            *preserve_desired = 1;
        }
        else
            ed_move_up(app->editor);

        return 1;

    case KEY_DOWN:
        if (soft)
        {
            soft_move_down_visual(app, width);
            *preserve_desired = 1;
        }
        else
            ed_move_down(app->editor);

        return 1;

    case KEY_LEFT:
        ed_move_left(app->editor);
        return 1;

    case KEY_RIGHT:
        ed_move_right(app->editor);
        return 1;

    case KEY_HOME:
        if (soft)
            soft_move_home_visual(app, width);
        else
            ed_move_home(app->editor);

        return 1;

    case KEY_END:
        if (soft)
            soft_move_end_visual(app, width);
        else
            ed_move_end(app->editor);

        return 1;

    case KEY_PPAGE:
        if (soft)
        {
            soft_move_pgup_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgup(app->editor, 0);

        return 1;

    case KEY_NPAGE:
        if (soft)
        {
            soft_move_pgdn_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgdn(app->editor, 0);

        return 1;

    case KEY_ENTER:
        ed_enter(app->editor);
        clear_search_highlights(app);

        return 1;

    case KEY_BACKSPACE:
    {
        EdInfo i2;
        ed_get_info(app->editor, &i2);

        if (i2.block.active)
        {
            ed_save_undo(app->editor);
            ed_block_delete(app->editor);
            te_status(app, "Block deleted");
        }
        else
            ed_backspace(app->editor);

        clear_search_highlights(app);

        return 1;
    }

    case KEY_DC:
    {
        EdInfo i2;
        ed_get_info(app->editor, &i2);

        if (i2.block.active)
        {
            ed_save_undo(app->editor);
            ed_block_delete(app->editor);
            te_status(app, "Block deleted");
        }
        else
            ed_delete(app->editor);

        clear_search_highlights(app);

        return 1;
    }

    case KEY_IC: /* Ins */
    case KEY_ALT('I'):
        ed_toggle_insert(app->editor);
        return 1;

    case KEY_CLEFT:
        ed_word_left(app->editor);
        return 1;

    case KEY_CRIGHT:
        ed_word_right(app->editor);
        return 1;

    case KEY_ALT('Z'):
        ed_redo(app->editor);
        app->hard_wrap = ed_get_hard_wrap(app->editor);
        clear_search_highlights(app);

        return 1;

    case KEY_ALT('L'):
        app->show_line_numbers = !app->show_line_numbers;
        te_status(app, "Line numbers: %s", app->show_line_numbers ? "ON" : "OFF");
        return 1;

    case KEY_ALT('W'):
    {
        if (app->hard_wrap == 0)
        {
            /* Changing from soft to hard: ask if user wants to rewrap */
            char msg[128];

            snprintf(msg, sizeof(msg), "Convert document to hard-wrap at column %d?", app->wrap_col);

            if (ui_popup_confirm("Hard Wrap", msg) == 1)
            {
                app->hard_wrap = 1;
                ed_set_hard_wrap(app->editor, 1);
                ed_rewrap_document(app->editor, app->wrap_col);
                te_status(app, "Hard wrap: ON (rewrapped)");
            }
        }
        else
        {
            /* Changing from hard to soft: just toggle without asking */
            app->hard_wrap = 0;
            ed_set_hard_wrap(app->editor, 0);
            te_status(app, "Hard wrap: OFF");
        }

        return 1;
    }

    default:
        return 0;
    }
}

/* Handle text editing keys (printable chars and control chars) */
static int handle_editing_keys(TeApp *app, int ch, wint_t wch, int soft, int width, int body_rows, int *preserve_desired)
{
    switch (ch)
    {
    case '\n':
    case '\r':
        ed_enter(app->editor);
        clear_search_highlights(app);

        return 1;

    case 8:
    case 127:
        ed_backspace(app->editor);
        clear_search_highlights(app);
        return 1;

    case CTRL('B'):
        if (soft)
            soft_move_home_visual(app, width);
        else
            ed_move_home(app->editor);

        return 1;

    case CTRL('E'):
        if (soft)
            soft_move_end_visual(app, width);
        else
            ed_move_end(app->editor);

        return 1;

    case CTRL('U'):
        if (soft)
        {
            soft_move_pgup_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgup(app->editor, 0);

        return 1;

    case CTRL('D'):
        if (soft)
        {
            soft_move_pgdn_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgdn(app->editor, 0);

        return 1;

    case CTRL('Y'):
        ed_save_undo(app->editor);
        ed_delete_line(app->editor);
        clear_search_highlights(app);
        return 1;

    case CTRL('Z'):
        ed_undo(app->editor);
        app->hard_wrap = ed_get_hard_wrap(app->editor);
        clear_search_highlights(app);
        return 1;

    case CTRL('T'):
        ed_save_undo(app->editor);
        ed_delete_word_right(app->editor);
        clear_search_highlights(app);
        return 1;

    case CTRL('_'):
        ed_save_undo(app->editor);
        ed_delete_word_left(app->editor);
        clear_search_highlights(app);
        return 1;

    case '\t':
        ed_save_undo(app->editor);
        ed_insert_tab(app->editor, 4);
        clear_search_highlights(app);
        return 1;

    default:
        if (wch >= 0x20 && wch != 127)
        {
            ed_insert_char(app->editor, (wchar_t)wch);
            clear_search_highlights(app);

            /* HARD-WRAP only: insert CR at wrap col; soft-wrap leaves line intact */
            if (app->hard_wrap)
            {
                int eff_wrap = editor_eff_wrap(app);

                if (eff_wrap > 0)
                {
                    EdInfo wi;
                    int linelen;

                    ed_get_info(app->editor, &wi);
                    linelen = ed_line_len(app->editor, wi.row);

                    if (wi.col > eff_wrap && wi.col == linelen)
                    {
                        const wchar_t *line = ed_line_wcs(app->editor, wi.row);
                        int brk = -1;
                        int k;

                        if (line)
                        {
                            int limit = eff_wrap;

                            if (limit > linelen)
                                limit = linelen;

                            for (k = limit; k > 0; k--)
                            {
                                if (line[k - 1] == L' ')
                                {
                                    brk = k - 1;
                                    break;
                                }
                            }
                        }

                        if (wch == L' ')
                        {
                            ed_backspace(app->editor); /* replace trailing space with newline */
                            ed_enter(app->editor);
                            clear_search_highlights(app);
                        }
                        else if (brk >= 0)
                        {
                            int tail = linelen - brk - 1;

                            ed_set_pos(app->editor, wi.row, brk);
                            ed_delete(app->editor);
                            ed_enter(app->editor);
                            clear_search_highlights(app);
                            ed_set_pos(app->editor, wi.row + 1, tail);
                        }
                    }
                }
            }
            return 1;
        }

        return 0;
    }
}

/* Main event loop */
void ui_editor_run(TeApp *app)
{
    int body_rows;
    int soft, width;

    if (!app)
        return;

    /* Reset soft-wrap state */
    s_soft_vtop = 0;
    s_soft_desired_vcol = -1;
    s_soft_last_width = COLS;

    TE_BRACKET_PASTE_ON();

    for (;;)
    {
        wint_t wch;
        int wrc, ch, is_key, preserve_desired;

        soft = !app->hard_wrap;
        width = COLS;
        body_rows = LINES - 2;

        /* Adjust width for line numbers if enabled */
        if (app->show_line_numbers)
        {
            EdInfo info;
            ed_get_info(app->editor, &info);
            width = COLS - lineno_width(info.line_count);
        }

        if (body_rows < 1)
            body_rows = 1;

        if (COLS != s_soft_last_width)
        {
            soft_reset_desired();
            s_soft_last_width = COLS;
        }

        erase();
        te_draw_titlebar(app);
        draw_body(app);
        te_draw_statusbar(app);
        position_cursor(app);
        refresh();

        wrc = wrapper_read_key(&wch);
        if (wrc == ERR)
            continue;

        ch = (int)wch;

        is_key = (wrc == KEY_CODE_YES);

        if (!is_key && (ch == KEY_UP || ch == KEY_DOWN || ch == KEY_LEFT || ch == KEY_RIGHT ||
                        ch == KEY_HOME || ch == KEY_END || ch == KEY_PPAGE || ch == KEY_NPAGE ||
                        ch == KEY_BACKSPACE || ch == KEY_DC || ch == KEY_ENTER))
            is_key = 1;

        preserve_desired = 0;

        /* Bracketed paste - check BEFORE other keys to avoid interference */
        if (is_key && ch == KEY_PASTE_START)
        {
            char *buf = collect_bracketed_paste();

            if (buf)
            {
                char *wrapped = NULL;
                const char *to_insert = buf;
                int reported_len;

                ed_save_undo(app->editor);

                /* HARD-WRAP only: reflow pasted text; soft-wrap inserts verbatim */
                if (app->hard_wrap)
                {
                    int pw = editor_eff_wrap(app);

                    if (pw > 0)
                    {
                        wrapped = wrap_paste_text(buf, pw);

                        if (wrapped)
                            to_insert = wrapped;
                    }
                }

                ed_paste_text(app->editor, to_insert);
                clear_search_highlights(app);
                soft_reset_desired();

                s_soft_vtop = 0;
                reported_len = (int)strlen(to_insert);

                te_status(app, "Pasted %d bytes", reported_len);

                free(buf);

                if (wrapped)
                    free(wrapped);
            }

            continue;
        }

        /* Handle function keys */
        if (handle_function_keys(app, ch, is_key))
            continue;

        /* ESC : exit search mode or quit */
        if (!is_key && ch == 27)
        {
            if (app->search.is_mode || app->search.only_mode)
            {
                clear_search_highlights(app);
                app->search.is_mode = 0;
                app->search.only_mode = 0;
                te_status(app, "Search mode exited");
                continue;
            }

            EdInfo info;
            ed_get_info(app->editor, &info);

            if (info.modified)
            {
                int r = ui_popup_confirm("Quit", "Discard changes and quit?");

                if (r != 1)
                    continue;
            }

            curs_set(0);
            TE_BRACKET_PASTE_OFF();

            break;
        }

        /* F10 : quit */
        if (is_key && ch == KEY_F(10))
        {
            EdInfo info;
            ed_get_info(app->editor, &info);

            if (info.modified)
            {
                int r = ui_popup_confirm("Quit", "Discard changes and quit?");

                if (r != 1)
                    continue;
            }

            curs_set(0);
            TE_BRACKET_PASTE_OFF();

            return;
        }

        /* Resize */
        if (is_key && ch == KEY_RESIZE)
            continue;

        /* Handle control keys */
        if (handle_control_keys(app, ch, is_key))
            continue;

        /* Alt+G : clear search highlights */
        if (ch == KEY_ALT('G'))
        {
            clear_search_highlights(app);
            te_status(app, "Search highlights cleared");

            continue;
        }

        /* Body key handling */
        if (is_key)
        {
            if (handle_navigation_keys(app, ch, soft, width, body_rows, &preserve_desired))
            {
                /* Reset desired column unless this was a vertical move that
                 * needs to preserve it (UP/DOWN/PgUp/PgDn set preserve_desired=1) */
                if (!preserve_desired)
                    soft_reset_desired();

                continue;
            }
        }
        else /* printable / control chars */
        {
            if (handle_editing_keys(app, ch, wch, soft, width, body_rows, &preserve_desired))
            {
                if (!preserve_desired)
                    soft_reset_desired();

                continue;
            }
        }

        if (!preserve_desired)
            soft_reset_desired();
    }
}
