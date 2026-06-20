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
#include "ui_spell.h"

#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
#include "ui_hyph.h"
#endif

#if defined(HAVE_HUNSPELL) && defined(HAVE_MYTHES)
#include "ui_thes.h"
#endif

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
        "    Alt+Q            Toggle wrap mode",
        "    Alt+D            Toggle line numbers",
        "    Alt+E            Toggle hyphen wrap",
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
        "    F5 / Alt+F       Search / Replace current",
        "    Ctrl+R           Find & replace",
        "    Alt+G            Go to line",
        "    F3 / Alt+C       Prev",
        "    F4 / Alt+T       Next",
        "    F6 / Alt+B       Replace all",
        "",
        "  Files:",
        "    F7 / Alt+O       Insert file at cursor",
        "    Ctrl+L           Open file (new tab)",
        "    Ctrl+N           New file (new tab)",
        "    F2 / Ctrl+S      Save",
        "    ESC / F10        Quit (confirm if modified)",
        "    F1 / Alt+Y       This help",
        "",
        "  Tabs:",
        "    F9 / Alt+L       Toggle tabs panel",
        "    Alt+J            Next tab",
        "    Alt+K            Previous tab",
        "    Alt+W            Close current tab",
        "",
        "  Spell/Translate:",
        "    Alt+S            Toggle spell/translate panel",
        "    Alt+H            Toggle spell checker",
        "    Alt+P            Spell check word under cursor",
        "    Alt+A            Thesaurus lookup for word under cursor",
        "    Alt+E            Toggle hyphen wrap",
        "",
        "  Other:",
        "    F4 / Alt+T       Setup / configuration",
        "    Alt+U            Unicode glyph picker",
};
#define HELP_N ((int)(sizeof(HELP_LINES) / sizeof(HELP_LINES[0])))

/* Soft-wrap viewport: anchored by top line/sub-row */
static int s_soft_top_line = 0;
static int s_soft_top_sub = 0;
static int s_soft_desired_vcol = -1;
static int s_soft_last_width = -1;

void soft_reset_desired(void)
{
    s_soft_desired_vcol = -1;
}

/* Soft-wrap: break at last space or hard-cut at boundary */
static int wrap_next(const wchar_t *line, int len, int width, int start)
{
    int vcol = 0;
    int k = start;
    int hard_end;

    if (width < 1)
        width = 1;

    /* Walk forward, accumulating visual width */
    while (k < len)
    {
        int w = wcswidth(&line[k], 1);

        if (w <= 0)
            w = 1; /* control/zero-width -> 1 */

        if (vcol + w > width)
            break;

        vcol += w;
        k++;
    }

    hard_end = k;

    if (k >= len)
        return len;

    /* Search backwards for space to break */
    for (k = hard_end; k > start; k--)
    {
        if (line[k - 1] == L' ' || line[k - 1] == L'\t')
            return k;
    }

    /* No space: hard cut at visual boundary */
    return hard_end;
}

/* Number of visual sub-rows a logical line occupies */
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

/* Return wchar range for target sub-row. O(target_sub) */
static int line_subrow_range(const wchar_t *l, int len, int width, int target_sub, int *seg_start, int *seg_end)
{
    int pos = 0;
    int sub = 0;

    if (!l || len <= 0)
    {
        *seg_start = 0;
        *seg_end = 0;
        return 0;
    }

    for (;;)
    {
        int end = wrap_next(l, len, width, pos);

        if (sub == target_sub || end >= len)
        {
            *seg_start = pos;
            *seg_end = end;
            return sub;
        }

        if (end <= pos)
            end = pos + (width < 1 ? 1 : width); /* never stall */

        pos = end;
        sub++;
    }
}

/* Return sub-row index where column col lives. O(sub-rows in line) */
static int line_subrow_of_col(const wchar_t *l, int len, int width, int col)
{
    int pos = 0;
    int sub = 0;

    if (!l || len <= 0)
        return 0;

    for (;;)
    {
        int end = wrap_next(l, len, width, pos);

        if (col < end || end >= len)
            return sub;

        if (end <= pos)
            end = pos + (width < 1 ? 1 : width);

        pos = end;
        sub++;
    }
}

/* Reset soft-wrap viewport to cursor position */
void soft_reset_viewport_to_cursor(TeApp *app, int width)
{
    EdInfo info;
    const wchar_t *l;
    int len;

    ed_get_info(te_app_get_editor(app), &info);

    l = ed_line_wcs(te_app_get_editor(app), info.row);
    len = ed_line_len(te_app_get_editor(app), info.row);

    s_soft_top_line = info.row;
    s_soft_top_sub = line_subrow_of_col(l ? l : L"", l ? len : 0, width, info.col);
    s_soft_desired_vcol = -1;
}

/* Walk N visual rows from (from_line, from_sub). O(delta_lines) */
static void walk_vrows_forward(Ed *ed, int width, int from_line, int from_sub, int delta, int *out_line, int *out_sub)
{
    EdInfo info;
    int line = from_line;
    int sub = from_sub;
    int remaining = delta;

    ed_get_info(ed, &info);

    while (remaining > 0 && line < info.line_count)
    {
        const wchar_t *l = ed_line_wcs(ed, line);
        int len = ed_line_len(ed, line);
        int n = wrap_count(l ? l : L"", l ? len : 0, width);
        int avail = n - sub - 1; /* remaining sub-rows below sub */

        if (remaining <= avail)
        {
            sub += remaining;
            remaining = 0;
            break;
        }

        /* consume rest of line, move to next */
        remaining -= (avail + 1);
        line++;
        sub = 0;
    }

    if (line >= info.line_count)
    {
        /* clamp to last line's last sub-row */
        if (info.line_count > 0)
        {
            const wchar_t *l;
            int len, n;

            line = info.line_count - 1;
            l = ed_line_wcs(ed, line);
            len = ed_line_len(ed, line);
            n = wrap_count(l ? l : L"", l ? len : 0, width);
            sub = n - 1;

            if (sub < 0)
                sub = 0;
        }
        else
        {
            line = 0;
            sub = 0;
        }
    }

    *out_line = line;
    *out_sub = sub;
}

static void walk_vrows_backward(Ed *ed, int width, int from_line, int from_sub, int delta, int *out_line, int *out_sub)
{
    int line = from_line;
    int sub = from_sub;
    int remaining = delta;

    while (remaining > 0)
    {
        if (sub > 0)
        {
            int take = sub;

            if (take > remaining)
                take = remaining;

            sub -= take;
            remaining -= take;

            if (remaining == 0)
                break;
        }

        /* step to previous line if possible */
        if (line == 0)
        {
            sub = 0;
            break;
        }

        line--;
        {
            const wchar_t *l = ed_line_wcs(ed, line);
            int len = ed_line_len(ed, line);
            int n = wrap_count(l ? l : L"", l ? len : 0, width);

            sub = n - 1;
        }

        remaining--;

        if (remaining == 0)
            break;
    }

    if (line < 0)
    {
        line = 0;
        sub = 0;
    }

    *out_line = line;
    *out_sub = sub;
}

/* Return visual column of cursor within its sub-row. O(width of sub-row) */
static int soft_cursor_vcol(Ed *ed, int width)
{
    EdInfo info;
    const wchar_t *l;
    int len;
    int sub;
    int seg_start = 0, seg_end = 0;
    int n;

    ed_get_info(ed, &info);

    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);

    if (!l || len <= 0)
        return 0;

    sub = line_subrow_of_col(l, len, width, info.col);
    line_subrow_range(l, len, width, sub, &seg_start, &seg_end);

    n = info.col - seg_start;

    if (n < 0)
        n = 0;

    if (n > len - seg_start)
        n = len - seg_start;

    return wcs_vwidth(&l[seg_start], n);
}

/* Return visual rows between two positions. O(|b_line - a_line|) */
static int soft_vrows_between(Ed *ed, int width, int a_line, int a_sub, int b_line, int b_sub)
{
    int i;
    int delta = 0;
    const wchar_t *l;
    int len;
    int n;

    if (a_line == b_line)
        return b_sub - a_sub;

    if (a_line < b_line)
    {
        /* Walk forward from a */
        const wchar_t *l = ed_line_wcs(ed, a_line);
        int len = ed_line_len(ed, a_line);
        int n = wrap_count(l ? l : L"", l ? len : 0, width);

        delta = (n - a_sub); /* rows to end of a_line + 1 step */

        for (i = a_line + 1; i < b_line; i++)
        {
            l = ed_line_wcs(ed, i);
            len = ed_line_len(ed, i);
            delta += wrap_count(l ? l : L"", l ? len : 0, width);
        }

        delta += b_sub;

        return delta;
    }

    /* walk backward */
    l = ed_line_wcs(ed, b_line);
    len = ed_line_len(ed, b_line);
    n = wrap_count(l ? l : L"", l ? len : 0, width);

    delta = (n - b_sub); /* rows to end of b_line + 1 step */

    for (i = b_line + 1; i < a_line; i++)
    {
        l = ed_line_wcs(ed, i);
        len = ed_line_len(ed, i);
        delta += wrap_count(l ? l : L"", l ? len : 0, width);
    }

    delta += a_sub;

    return -delta;
}

/* Return cursor screen row. O(|info.row - s_soft_top_line|) */
static int soft_cursor_screen_row(TeApp *app, int width)
{
    EdInfo info;
    Ed *ed = te_app_get_editor(app);
    int sub_cursor;
    const wchar_t *l;
    int len;

    ed_get_info(ed, &info);

    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);
    sub_cursor = line_subrow_of_col(l ? l : L"", l ? len : 0, width, info.col);

    return soft_vrows_between(ed, width, s_soft_top_line, s_soft_top_sub, info.row, sub_cursor);
}

/* Adjust viewport to keep cursor visible. Bounded by body_rows */
static void soft_ensure_visible(TeApp *app, int width, int body_rows)
{
    EdInfo info;
    Ed *ed = te_app_get_editor(app);
    int sub_cursor;
    int screen_row;
    const wchar_t *l;
    int len;
    const wchar_t *tl;
    int tlen;
    int tn;

    ed_get_info(ed, &info);

    if (info.line_count <= 0)
    {
        s_soft_top_line = 0;
        s_soft_top_sub = 0;
        return;
    }

    /* Clamp top to valid range */
    if (s_soft_top_line < 0)
        s_soft_top_line = 0;

    if (s_soft_top_line >= info.line_count)
        s_soft_top_line = info.line_count - 1;

    tl = ed_line_wcs(ed, s_soft_top_line);
    tlen = ed_line_len(ed, s_soft_top_line);
    tn = wrap_count(tl ? tl : L"", tl ? tlen : 0, width);

    if (s_soft_top_sub < 0)
        s_soft_top_sub = 0;

    if (s_soft_top_sub >= tn)
        s_soft_top_sub = tn - 1;

    /* Cursor position relative to viewport */
    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);
    sub_cursor = line_subrow_of_col(l ? l : L"", l ? len : 0, width, info.col);

    screen_row = soft_vrows_between(ed, width, s_soft_top_line, s_soft_top_sub, info.row, sub_cursor);

    if (screen_row < 0)
    {
        /* Cursor above viewport: pull top up */
        s_soft_top_line = info.row;
        s_soft_top_sub = sub_cursor;
    }
    else if (screen_row >= body_rows)
    {
        /* Cursor below viewport: push top down */
        int over = screen_row - (body_rows - 1);
        int new_line, new_sub;

        walk_vrows_forward(ed, width, s_soft_top_line, s_soft_top_sub, over, &new_line, &new_sub);

        s_soft_top_line = new_line;
        s_soft_top_sub = new_sub;
    }
}

/* Position the cursor on (line, sub) with desired visual column */
static void soft_set_cursor_at(Ed *ed, int width, int line, int sub, int desired_vcol)
{
    const wchar_t *l;
    int len;
    int seg_start = 0, seg_end = 0;
    int v, i, col;

    l = ed_line_wcs(ed, line);
    len = ed_line_len(ed, line);

    line_subrow_range(l ? l : L"", l ? len : 0, width, sub, &seg_start, &seg_end);

    if (desired_vcol < 0)
        desired_vcol = 0;

    /* Walk through chars, accumulate visual width until desired_vcol */
    v = 0;
    col = seg_start;

    if (l && len > 0)
    {
        for (i = seg_start; i < seg_end; i++)
        {
            int w = wcswidth(&l[i], 1);

            if (w != 2)
                w = 1;

            if (v + w > desired_vcol)
                break;

            v += w;
            col = i + 1;
        }
    }

    /* On last sub-row: allow cursor at line end. Otherwise stop at seg_end-1 */
    if (seg_end < len && col >= seg_end)
        col = seg_end > seg_start ? seg_end - 1 : seg_start;

    if (col < 0)
        col = 0;

    if (col > len)
        col = len;

    ed_set_pos(ed, line, col);
}

/* Move cursor delta visual rows. Preserves desired_vcol. O(|delta|) */
static void soft_move_vrows(TeApp *app, int width, int delta)
{
    EdInfo info;
    Ed *ed = te_app_get_editor(app);
    const wchar_t *l;
    int len;
    int sub_cursor;
    int new_line, new_sub;
    int vcol;

    if (delta == 0)
        return;

    ed_get_info(ed, &info);

    if (info.line_count <= 0)
        return;

    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);
    sub_cursor = line_subrow_of_col(l ? l : L"", l ? len : 0, width, info.col);

    /* Record desired visual column on first vertical move */
    if (s_soft_desired_vcol < 0)
        s_soft_desired_vcol = soft_cursor_vcol(ed, width);

    if (delta > 0)
        walk_vrows_forward(ed, width, info.row, sub_cursor, delta, &new_line, &new_sub);
    else
        walk_vrows_backward(ed, width, info.row, sub_cursor, -delta, &new_line, &new_sub);

    vcol = s_soft_desired_vcol;
    soft_set_cursor_at(ed, width, new_line, new_sub, vcol);
}

/* Handlers for ui_editor key dispatch */
static void soft_move_up_visual(TeApp *app, int width)
{
    soft_move_vrows(app, width, -1);
}

static void soft_move_down_visual(TeApp *app, int width)
{
    soft_move_vrows(app, width, +1);
}

static void soft_move_home_visual(TeApp *app, int width)
{
    EdInfo info;
    Ed *ed = te_app_get_editor(app);
    const wchar_t *l;
    int len;
    int sub_cursor;
    int seg_start = 0, seg_end = 0;

    ed_get_info(ed, &info);

    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);
    sub_cursor = line_subrow_of_col(l ? l : L"", l ? len : 0, width, info.col);

    line_subrow_range(l ? l : L"", l ? len : 0, width, sub_cursor, &seg_start, &seg_end);

    ed_set_pos(ed, info.row, seg_start);
    soft_reset_desired();
}

static void soft_move_end_visual(TeApp *app, int width)
{
    EdInfo info;
    Ed *ed = te_app_get_editor(app);
    const wchar_t *l;
    int len;
    int sub_cursor;
    int seg_start = 0, seg_end = 0;
    int target_col;

    ed_get_info(ed, &info);

    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);
    sub_cursor = line_subrow_of_col(l ? l : L"", l ? len : 0, width, info.col);

    line_subrow_range(l ? l : L"", l ? len : 0, width, sub_cursor, &seg_start, &seg_end);

    /* End-of-sub-row: put cursor at line end if last sub-row, else at last visible char */
    if (seg_end >= len)
        target_col = len;
    else
        target_col = seg_end > seg_start ? seg_end - 1 : seg_start;

    ed_set_pos(ed, info.row, target_col);
    soft_reset_desired();
}

static void soft_move_pgup_visual(TeApp *app, int width, int pg)
{
    int new_line, new_sub;
    Ed *ed = te_app_get_editor(app);

    if (pg <= 0)
        pg = 1;

    /* Move cursor up pg visual rows */
    soft_move_vrows(app, width, -pg);

    /* Slide viewport up by pg visual rows */
    walk_vrows_backward(ed, width, s_soft_top_line, s_soft_top_sub, pg, &new_line, &new_sub);

    s_soft_top_line = new_line;
    s_soft_top_sub = new_sub;
}

static void soft_move_pgdn_visual(TeApp *app, int width, int pg)
{
    int new_line, new_sub;
    Ed *ed = te_app_get_editor(app);

    if (pg <= 0)
        pg = 1;

    soft_move_vrows(app, width, +pg);

    walk_vrows_forward(ed, width, s_soft_top_line, s_soft_top_sub, pg, &new_line, &new_sub);

    s_soft_top_line = new_line;
    s_soft_top_sub = new_sub;
}

/* Word-wrap UTF-8 paste to col columns, preserving newlines */
static int paste_char_width(wchar_t c)
{
    /* For FTN editing: char column width (0 for combining marks, BOM) */
    if (c == 0)
        return 0;

    if (c < 0x20)
        return 0; /* control chars */

    if (c >= 0x0300 && c <= 0x036F)
        return 0; /* combining diacritical marks */

    if (c == 0x200B || c == 0xFEFF)
        return 0; /* ZWSP / BOM */

    return 1;
}

char *wrap_paste_text(TeApp *app, const char *utf8, int col)
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
    int use_hyphen = 0;

#ifdef HAVE_HYPHEN
    use_hyphen = app && app->hyph_wrap_enabled && app->hyph_handle;
#endif

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

        if (col_pos > col)
        {
            int new_start;
            int j;
            int width_after = 0;
            int break_found = 0;

#ifdef HAVE_HYPHEN
            if (use_hyphen)
            {
                /* Word starts after last space (or line start) and extends to current char */
                int word_start = (last_space >= 0) ? last_space + 1 : line_start;
                int word_end = out_len;
                int word_wlen = word_end - word_start;

                if (word_wlen > 3 && word_wlen < 512)
                {
                    /* Convert to UTF-8 for libhyphen (works on bytes) */
                    char *utf8_word = wcs_to_utf8(&out[word_start], word_wlen);

                    if (utf8_word)
                    {
                        int utf8_len = (int)strlen(utf8_word);
                        int hyph_pos[16];
                        int hyph_count = 0;
                        int k;

                        if (hyph_split_word(app, utf8_word, utf8_len, hyph_pos, &hyph_count))
                        {
                            /* Walk from rightmost candidate, pick first fitting in wrap column. hyph_pos[k] is BYTE offset, convert to wchar offset */
                            for (k = hyph_count - 1; k >= 0; k--)
                            {
                                int char_off = utf8_charcount(utf8_word, hyph_pos[k]);
                                int break_at = word_start + char_off;
                                int break_col = 0;
                                int m;

                                /* Must leave at least one char before and after break */
                                if (break_at <= word_start || break_at >= word_end)
                                    continue;

                                if (break_at <= line_start)
                                    continue;

                                for (m = line_start; m < break_at; m++)
                                    break_col += (out[m] == L'\t') ? 1 : paste_char_width(out[m]);

                                /* Reserve one column for the trailing '-' */
                                if (break_col > col - 1)
                                    continue;

                                /* Need room for two extra wchars: '-' and '\n'. Old code only reserved one and overwrote a letter */
                                if (out_len + 2 > out_cap)
                                    break; /* no room -- fall through */

                                /* Shift [break_at..out_len) right by 2 (iterate from end to keep source intact) */
                                for (m = out_len - 1; m >= break_at; m--)
                                    out[m + 2] = out[m];

                                out[break_at] = L'-';
                                out[break_at + 1] = L'\n';

                                out_len += 2;
                                new_start = break_at + 2;
                                break_found = 1;

                                width_after = 0;

                                for (m = new_start; m < out_len; m++)
                                    width_after += (out[m] == L'\t') ? 1 : paste_char_width(out[m]);

                                line_start = new_start;
                                last_space = -1;
                                col_pos = width_after;
                                break;
                            }
                        }

                        free(utf8_word);
                    }
                }
            }
#endif

            if (!break_found && last_space > line_start)
            {
                /* Normal space-based break */
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
            else if (!break_found)
            {
                /* Hard cut at column limit */
                if (out_len + 1 < out_cap)
                {
                    out[out_len++] = L'\n';

                    line_start = out_len;
                    last_space = -1;
                    col_pos = 0;
                }
            }
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

/* Effective wrap column. Clamp to COLS-1; 0=disabled */
int editor_eff_wrap(const TeApp *app)
{
    int cfgw = app->wrap_col;
    int limit = COLS - 1; /* one column margin */

    if (cfgw <= 0)
        return 0; /* AUTOWRAP disabled */

    if (COLS <= 10)
        return 0; /* Too narrow: scroll instead */

    if (cfgw > limit)
        return limit; /* Screen narrower than configured */

    return cfgw;
}

/* Calculate width for line numbers (digits + 1 space) */
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

    return width + 1; /* space after number */
}

/* Draw editor body */
static void draw_body(TeApp *app)
{
    EdInfo info;
    TeWindow *win;
    int body_top = 1;
    int body_bot = LINES - 2;
    int body_rows;
    int width = COLS;
    int offset_x = 0;
    int offset_y = 0;
    int soft = !app->hard_wrap;
    int b_r1 = -1;
    int b_c1 = 0;
    int b_r2 = -1;
    int b_c2 = 0;
    int screen_row;
    int ln_width = 0;
    int ln_offset = 0;
    int show_lnum = te_app_get_show_line_numbers(app);

    /* Use window coordinates from layout manager */
    win = wm_get_window_by_type(app->wm, WIN_EDITOR);

    if (win && win->visible)
    {
        body_top = win->y;
        body_bot = win->y + win->h - 1;
        width = win->w;
        offset_x = win->x;
        offset_y = win->y;
    }

    body_rows = body_bot - body_top;

    if (body_rows < 1)
        body_rows = 1;

    ed_set_page(te_app_get_editor(app), body_rows);
    ed_ensure_visible(te_app_get_editor(app));
    ed_get_info(te_app_get_editor(app), &info);

    /* Calculate line number width if enabled */
    if (show_lnum)
    {
        ln_width = lineno_width(info.line_count);
        ln_offset = ln_width;
        width = win->w - ln_offset; /* reduce width for text */
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
        int li, sr;
        int sub_skip;

        /* Ensure cursor is inside viewport. Adjust s_soft_top_line/sub */
        soft_ensure_visible(app, width, body_rows);

        /* Clear body region */
        for (sr = 0; sr < body_rows; sr++)
        {
            move(offset_y + sr, offset_x);
            clrtoeol();
        }

        /* Start from s_soft_top_line, skipping first s_soft_top_sub sub-rows */
        li = s_soft_top_line;
        sub_skip = s_soft_top_sub;
        sr = 0;

        while (li < info.line_count && sr < body_rows)
        {
            const wchar_t *l = ed_line_wcs(te_app_get_editor(app), li);
            int len = ed_line_len(te_app_get_editor(app), li);
            int pos = 0;
            int s = 0;         /* sub-row index within line */
            int first_seg = 1; /* first painted sub-row for line number */

            if (!l || len <= 0)
            {
                /* Empty line: one blank sub-row. Paint if not skipped */
                if (sub_skip == 0)
                {
                    if (show_lnum)
                    {
                        attron(COLOR_PAIR(COL_BORDER));
                        mvprintw(offset_y + sr, offset_x, "%*d", ln_width - 1, li + 1);
                        attroff(COLOR_PAIR(COL_BORDER));
                    }

                    /* Block-selection overlay for empty line */
                    if (b_r1 >= 0 && li >= b_r1 && li <= b_r2)
                    {
                        attron(A_REVERSE);
                        mvaddch(offset_y + sr, offset_x + ln_offset, ' ');
                        attroff(A_REVERSE);
                    }

                    sr++;
                }

                li++;
                sub_skip = 0;
                continue;
            }

            while (sr < body_rows)
            {
                int seg_start = pos;
                int seg_end = wrap_next(l, len, width, pos);
                int np = seg_end;

                if (s >= sub_skip)
                {
                    int seg_len = seg_end - seg_start;

                    if (seg_len < 0)
                        seg_len = 0;

                    /* Line number on first painted sub-row */
                    if (show_lnum && first_seg)
                    {
                        attron(COLOR_PAIR(COL_BORDER));
                        mvprintw(offset_y + sr, offset_x, "%*d", ln_width - 1, li + 1);
                        attroff(COLOR_PAIR(COL_BORDER));

                        first_seg = 0;
                    }

                    if (seg_len > 0)
                        mvaddnwstr(offset_y + sr, offset_x + ln_offset, &l[seg_start], seg_len);

                    /* Highlight search matches */
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

                                if (match_col >= seg_start && match_end <= seg_end)
                                {
                                    attron(COLOR_PAIR(COL_SEARCH_MATCH));
                                    mvaddnwstr(offset_y + sr, offset_x + ln_offset + wcs_vwidth(&l[seg_start], match_col - seg_start), &l[match_col], match_len);
                                    attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                                }
                                else if (match_col >= seg_start && match_col < seg_end)
                                {
                                    int partial_len = seg_end - match_col;
                                    attron(COLOR_PAIR(COL_SEARCH_MATCH));
                                    mvaddnwstr(offset_y + sr, offset_x + ln_offset + wcs_vwidth(&l[seg_start], match_col - seg_start), &l[match_col], partial_len);
                                    attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                                }
                                else if (match_end > seg_start && match_end <= seg_end)
                                {
                                    int partial_len = match_end - seg_start;
                                    attron(COLOR_PAIR(COL_SEARCH_MATCH));
                                    mvaddnwstr(offset_y + sr, offset_x + ln_offset, &l[seg_start], partial_len);
                                    attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                                }
                            }
                        }
                    }

#ifdef HAVE_HUNSPELL
                    /* Highlight misspelled words */
                    if (app->spell_active && app->spell_handle)
                    {
                        int word_start = seg_start;
                        int word_end;

                        standend();

                        while (word_start < seg_end)
                        {
                            /* Skip non-alphanumeric characters */
                            while (word_start < seg_end && !iswalnum(l[word_start]))
                                word_start++;

                            if (word_start >= seg_end)
                                break;

                            /* Find word end */
                            word_end = word_start;

                            while (word_end < seg_end && iswalnum(l[word_end]))
                                word_end++;

                            if (word_end > word_start)
                            {
                                int word_len = word_end - word_start;

                                /* Ignore single-character words */
                                if (word_len > 1 && ui_spell_check_word_simple(app, &l[word_start], word_len))
                                {
                                    attron(COLOR_PAIR(COL_SPELL_CURRENT));
                                    mvaddnwstr(offset_y + sr, offset_x + ln_offset + wcs_vwidth(&l[seg_start], word_start - seg_start), &l[word_start], word_len);
                                    attroff(COLOR_PAIR(COL_SPELL_CURRENT));
                                }
                            }

                            word_start = word_end;
                        }
                    }
#endif /* HAVE_HUNSPELL */

                    /* Block-selection overlay */
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
                            mvaddnwstr(offset_y + sr, offset_x + ln_offset + wcs_vwidth(&l[seg_start], hs - seg_start), &l[hs], he - hs);
                            attroff(A_REVERSE);
                        }
                        else if (hs == seg_start && he == seg_start)
                        {
                            attron(A_REVERSE);
                            mvaddch(offset_y + sr, offset_x + ln_offset, ' ');
                            attroff(A_REVERSE);
                        }
                    }

                    sr++;
                }

                s++;

                if (np >= len)
                    break;

                if (np <= pos)
                    np = pos + (width < 1 ? 1 : width);

                pos = np;
            }

            li++;
            sub_skip = 0; /* subsequent lines start at sub-row 0 */
        }
    }
    else /* HARD-WRAP: 1 logical line == 1 screen row */
    {
        int i;

        for (i = 0; i < body_rows; i++)
        {
            int line_idx = info.top + i;
            int line_len;
            const wchar_t *wl;

            move(offset_y + i, offset_x);
            clrtoeol();

            if (line_idx >= info.line_count)
                continue;

            /* Draw line number if enabled */
            if (show_lnum)
            {
                attron(COLOR_PAIR(COL_BORDER));
                mvprintw(offset_y + i, offset_x, "%*d", ln_width - 1, line_idx + 1);
                attroff(COLOR_PAIR(COL_BORDER));
            }

            /* mvaddnwstr: n is in wide chars */
            wl = ed_line_wcs(te_app_get_editor(app), line_idx);
            line_len = ed_line_len(te_app_get_editor(app), line_idx);

            if (wl && line_len > 0)
            {
                /* Limit line length to available width */
                int max_chars = width;

                if (line_len > max_chars)
                    line_len = max_chars;

                mvaddnwstr(offset_y + i, offset_x + ln_offset, wl, line_len);
            }

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
                            mvaddnwstr(offset_y + i, offset_x + ln_offset + wcs_vwidth(wl, match_col), &wl[match_col], match_len);
                            attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
            }

#ifdef HAVE_HUNSPELL
            /* Highlight misspelled words */
            if (app->spell_active && app->spell_handle)
            {
                int word_start = 0;
                int word_end;

                standend();

                while (word_start < line_len)
                {
                    /* Skip non-alphanumeric characters */
                    while (word_start < line_len && !iswalnum(wl[word_start]))
                        word_start++;

                    if (word_start >= line_len)
                        break;

                    /* Find word end */
                    word_end = word_start;

                    while (word_end < line_len && iswalnum(wl[word_end]))
                        word_end++;

                    if (word_end > word_start)
                    {
                        int word_len = word_end - word_start;

                        /* Ignore single-character words like word processors do */
                        if (word_len > 1 && ui_spell_check_word_simple(app, &wl[word_start], word_len))
                        {
                            attron(COLOR_PAIR(COL_SPELL_CURRENT));
                            mvaddnwstr(offset_y + i, offset_x + ln_offset + wcs_vwidth(wl, word_start), &wl[word_start], word_len);
                            attroff(COLOR_PAIR(COL_SPELL_CURRENT));
                        }
                    }

                    word_start = word_end;
                }
            }
#endif /* HAVE_HUNSPELL */

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
                    mvaddnwstr(offset_y + i, offset_x + ln_offset + wcs_vwidth(wl, hs), &wcs[hs], he - hs);
                    attroff(A_REVERSE);
                }
                else if (hs == 0 && he == 0)
                {
                    /* Cursor at col 0 or empty line: show reversed space */
                    attron(A_REVERSE);
                    mvaddch(offset_y + i, offset_x + ln_offset, ' ');
                    attroff(A_REVERSE);
                }
            }
        }
    }

    attroff(COLOR_PAIR(COL_NORMAL));
}

/* Position terminal cursor on editor cursor */
static void position_cursor(TeApp *app)
{
    EdInfo info;
    TeWindow *win;
    int body_top = 1;
    int body_rows = LINES - 2;
    int width = COLS;
    int offset_x = 0;
    int offset_y = 0;
    int soft = !app->hard_wrap;
    int ln_offset = 0;
    int show_lnum = te_app_get_show_line_numbers(app);

    /* Use window coordinates from layout manager */
    win = wm_get_window_by_type(app->wm, WIN_EDITOR);

    if (win && win->visible)
    {
        body_top = win->y;
        body_rows = win->h;
        width = win->w;
        offset_x = win->x;
        offset_y = win->y;
    }

    if (body_rows < 1)
        body_rows = 1;

    ed_get_info(te_app_get_editor(app), &info);

    /* Calculate line number offset if enabled */
    if (show_lnum)
    {
        ln_offset = lineno_width(info.line_count);
        width = width - ln_offset;
    }

    if (soft)
    {
        int cy, cx;
        int max_y;
        int screen_row;
        int vcol;

        screen_row = soft_cursor_screen_row(app, width);
        vcol = soft_cursor_vcol(te_app_get_editor(app), width);

        cy = offset_y + screen_row;
        cx = offset_x + ln_offset + vcol;

        /* Clamp to body region */
        max_y = offset_y + body_rows - 1;

        if (max_y < offset_y)
            max_y = offset_y;

        if (cy < offset_y)
            cy = offset_y;

        if (cy > max_y)
            cy = max_y;

        if (cx < offset_x + ln_offset)
            cx = offset_x + ln_offset;

        if (cx > COLS - 1)
            cx = COLS - 1;

        move(cy, cx);
    }
    else
    {
        int cy = offset_y + (info.row - info.top);
        int cx;
        const wchar_t *wl = ed_line_wcs(te_app_get_editor(app), info.row);
        int line_len = ed_line_len(te_app_get_editor(app), info.row);
        int wchar_col = info.col;

        /* Convert wchar index to visual column for wide glyphs */
        if (wchar_col > line_len)
            wchar_col = line_len;

        if (wchar_col < 0)
            wchar_col = 0;

        cx = offset_x + ln_offset + (wl ? wcs_vwidth(wl, wchar_col) : wchar_col);

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

/* Detect rapid paste (fallback for terminals without bracketed paste) */
static char *collect_rapid_paste(wint_t first_wch)
{
    wchar_t *wbuf = NULL;
    int wlen = 0, wcap = 0;
    char *out;
    const int MAX_CHARS = 10; /* 10+ chars = paste, not typing */

    /* Check for more characters (rapid paste detection) */
    nodelay(stdscr, TRUE);

    wint_t next_wch;
    int next_wrc = get_wch(&next_wch);

    /* No more chars: not a paste, return NULL */
    if (next_wrc == ERR)
    {
        nodelay(stdscr, FALSE);
        return NULL;
    }

    /* Special key: not a paste */
    if (next_wrc == KEY_CODE_YES)
    {
        nodelay(stdscr, FALSE);
        return NULL;
    }

    /* One more char available - check for 2 more to confirm paste */
    wint_t third_wch;
    int third_wrc = get_wch(&third_wch);

    if (third_wrc == ERR || third_wrc == KEY_CODE_YES)
    {
        /* Only 2 chars: probably typing - push back second char */
        nodelay(stdscr, FALSE);
        ungetch((int)next_wch);
        return NULL;
    }

    /* At least 3 chars: paste, collect all */
    wbuf = (wchar_t *)malloc(256 * sizeof(wchar_t));

    if (!wbuf)
    {
        nodelay(stdscr, FALSE);
        return NULL;
    }

    wcap = 256;

    wbuf[wlen++] = (wchar_t)first_wch;
    wbuf[wlen++] = (wchar_t)next_wch;
    wbuf[wlen++] = (wchar_t)third_wch;

    /* Continue collecting remaining characters */
    while (wlen < MAX_CHARS)
    {
        wint_t more_wch;
        int more_wrc = get_wch(&more_wch);

        /* No more chars: end of rapid paste */
        if (more_wrc == ERR)
            break;

        /* Special key: not a paste */
        if (more_wrc == KEY_CODE_YES)
            break;

        /* Add to buffer */
        if (wlen + 1 >= wcap)
        {
            int ncap = wcap * 2;
            wchar_t *nb = (wchar_t *)realloc(wbuf, (size_t)ncap * sizeof(wchar_t));

            if (!nb)
            {
                free(wbuf);

                nodelay(stdscr, FALSE);

                return NULL;
            }

            wbuf = nb;
            wcap = ncap;
        }

        wbuf[wlen++] = (wchar_t)more_wch;
    }

    nodelay(stdscr, FALSE);

    /* Convert to UTF-8 */
    out = wcs_to_utf8(wbuf, wlen);
    free(wbuf);

    return out;
}

/* Save file */
static int do_save(TeApp *app)
{
    char *utf8;
    FILE *fp;
    int r = 0;
    char *text;
    char dir_input[1024];
    char name_input[1024];
    char filename_buf[TAB_FILENAME_MAX];
    const char *last_slash;

    /* Extract directory and name from current filename */
    if (te_app_get_filename(app)[0])
    {
        last_slash = strrchr(te_app_get_filename(app), '/');

        if (!last_slash)
            last_slash = strrchr(te_app_get_filename(app), '\\');

        if (last_slash)
        {
            int dir_len = (int)(last_slash - te_app_get_filename(app));

            strncpy(dir_input, te_app_get_filename(app), dir_len);
            dir_input[dir_len] = '\0';

            strncpy(name_input, last_slash + 1, sizeof(name_input) - 1);
            name_input[sizeof(name_input) - 1] = '\0';
        }
        else
        {
            dir_input[0] = '\0';
            strncpy(name_input, te_app_get_filename(app), sizeof(name_input) - 1);
            name_input[sizeof(name_input) - 1] = '\0';
        }
    }
    else
    {
        dir_input[0] = '\0';
        name_input[0] = '\0';
    }

    /* Always show save dialog */
    strncpy(filename_buf, te_app_get_filename(app), sizeof(filename_buf) - 1);
    filename_buf[sizeof(filename_buf) - 1] = '\0';

    if (ui_files_save("Save as", dir_input, name_input, filename_buf, sizeof(filename_buf)) != 0)
        return -1;

    if (!filename_buf[0])
        return -1;

    te_app_set_filename(app, filename_buf);

    utf8 = ed_to_string(te_app_get_editor(app));

    if (!utf8)
    {
        te_status(app, "Memory error");
        return -1;
    }

    fp = fopen(te_app_get_filename(app), "wb");

    if (!fp)
    {
        te_status(app, "Cannot write: %s", te_app_get_filename(app));
        free(utf8);
        return -1;
    }

    /* Charset conversion if requested */
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

    /* Update raw_bytes to match disk content for charset re-decode */
    if (app->charset_out[0] && strcasecmp(app->charset_out, "UTF-8") != 0 && strcasecmp(app->charset_out, "UTF8") != 0)
    {
        int srclen = (int)strlen(utf8);
        char *new_bytes;

        free(te_app_get_raw_bytes(app));

        new_bytes = (char *)malloc(srclen + 1);

        if (new_bytes)
        {
            memcpy(new_bytes, utf8, srclen + 1);
            te_app_set_raw_bytes(app, new_bytes, srclen);
        }
        else
        {
            te_app_set_raw_bytes(app, NULL, 0);
        }
    }
    else
    {
        /* UTF-8: raw_bytes = utf8 (ownership transferred) */
        free(te_app_get_raw_bytes(app));

        te_app_set_raw_bytes(app, utf8, (int)strlen(utf8));

        utf8 = NULL; /* Don't free below */
    }

    if (utf8)
        free(utf8);

    /* Clear modified flag */
    ed_set_modified(te_app_get_editor(app), 0);

    te_status(app, "Saved: %s", te_app_get_filename(app));

    return r;
}

/* Handle function keys */
static int handle_function_keys(TeApp *app, int ch, int is_key)
{
    /* F1 / ? : help */
    if ((is_key && ch == KEY_F(1)) || (is_key && ch == KEY_ALT('Y')))
    {
        ui_popup_help("Help", HELP_LINES, HELP_N);
        return 1;
    }

    /* F9 : toggle tabs panel */
    if (is_key && ch == KEY_F(9))
    {
        app->show_tabs = !app->show_tabs;
        return 1;
    }

    /* F2 / Ctrl+S : save */
    if ((is_key && ch == KEY_F(2)) || (!is_key && ch == CTRL('S')))
    {
        do_save(app);
        return 1;
    }

    /* F3 / Alt+C : charset or Previous match */
    if ((is_key && ch == KEY_F(3)) || (ch == KEY_ALT('C')))
    {
        if (app->search.only_mode || (app->search.is_mode && app->search.count > 0))
            return search_prev(app);
        else
            return charset_select(app);
    }

    /* F4 / Alt+T : Setup or Next match */
    if ((is_key && ch == KEY_F(4)) || (ch == KEY_ALT('T')))
    {
        if (app->search.only_mode || (app->search.is_mode && app->search.count > 0))
        {
            return search_next(app);
        }
        else
        {
            /* Normal setup functionality */
            char old_charset[TE_CFG_STR_MAX];
            int old_hard_wrap = app->hard_wrap;

            strncpy(old_charset, app->cfg.charset, sizeof(old_charset) - 1);
            old_charset[sizeof(old_charset) - 1] = '\0';

            if (ui_setup_run(app, &app->cfg, app->cfg_path) == 1)
            {
                /* Config saved: apply changes */
                if (app->cfg.undo_levels > 0)
                    ed_set_undo_levels(te_app_get_editor(app), app->cfg.undo_levels);

                app->hard_wrap = app->cfg.hard_wrap;
                ed_set_hard_wrap(te_app_get_editor(app), app->cfg.hard_wrap);

                app->wrap_col = app->cfg.autowrap_col;
                te_app_set_show_line_numbers(app, app->cfg.show_line_numbers);

                /* If charset changed in setup, update charset_out */
                if (strcasecmp(old_charset, app->cfg.charset) != 0)
                {
                    strncpy(app->charset_out, app->cfg.charset, sizeof(app->charset_out) - 1);
                    app->charset_out
                        [sizeof(app->charset_out) - 1] = '\0';
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
        else if (!app->search.only_mode && !app->search.is_mode)
        {
            /* Normal block anchor functionality */
            ed_block_anchor(te_app_get_editor(app));
            return 1;
        }
    }

    /* F7 / Alt+O : insert file */
    if ((is_key && ch == KEY_F(7)) || (ch == KEY_ALT('O')))
        return insert_file(app);

    /* F8 / Alt+U : Unicode glyph picker */
    if ((is_key && ch == KEY_F(8)) || (ch == KEY_ALT('U')))
    {
        long cp = ui_glyph_pick();

        if (cp >= 0)
        {
            ed_insert_char(te_app_get_editor(app), (wchar_t)cp);
            clear_search_highlights(app);
        }

        return 1;
    }

    return 0;
}

/* Handle control key combinations */
static int handle_control_keys(TeApp *app, int ch, int is_key)
{
    /* Ctrl+V : paste from internal buffer or system clipboard */
    if (!is_key && ch == CTRL('V'))
        return paste(app);

    /* Alt+G : goto line */
    if (ch == KEY_ALT('G'))
        return ui_editor_goto_line(app);

#ifdef HAVE_HUNSPELL
    /* Alt+P : spell check word under cursor */
    if (ch == KEY_ALT('P'))
    {
        spell_check_word(app);
        return 1; /* Force redraw */
    }

    /* Alt+H : toggle spell checker */
    if (ch == KEY_ALT('H'))
    {
        if (!app->spell_handle && app->cfg.spell_enabled && app->cfg.spell_dict_path[0] && app->cfg.spell_dict_name[0])
            spell_load_from_config(app);

        if (app->spell_handle)
        {
            app->spell_active = !app->spell_active;
            te_status(app, "Spell checker %s", app->spell_active ? "enabled" : "disabled");
        }
        else if (!app->cfg.spell_enabled)
        {
            te_status(app, "Spell disabled in config (enable in Setup F2)");
        }
        else if (!app->cfg.spell_dict_path[0] || !app->cfg.spell_dict_name[0])
        {
            te_status(app, "Set dict path and name in Setup F2");
        }
        else
        {
            te_status(app, "Cannot load %s/%s.aff ", app->cfg.spell_dict_path, app->cfg.spell_dict_name);
        }

        return 1;
    }
#endif /* HAVE_HUNSPELL */

#if defined(HAVE_HUNSPELL) && defined(HAVE_MYTHES)
    /* Alt+A : thesaurus lookup for word under cursor */
    if (ch == KEY_ALT('A'))
    {
        ui_thes_lookup_word(app);
        return 1;
    }
#endif

#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
    /* Alt+E : toggle hyphen wrap */
    if (ch == KEY_ALT('E'))
    {
        if (app->hard_wrap)
        {
            app->hyph_wrap_enabled = !app->hyph_wrap_enabled;
            te_status(app, "Hyphen wrap %s", app->hyph_wrap_enabled ? "ON" : "OFF");
        }
        else
        {
            te_status(app, "Hyphen wrap requires hard-wrap mode");
        }
        return 1;
    }
#endif

    /* Alt+J : next tab */
    if (ch == KEY_ALT('J'))
        return ui_tabs_switch_next(app);

    /* Alt+K : previous tab */
    if (ch == KEY_ALT('K'))
        return ui_tabs_switch_prev(app);

    /* Alt+S : toggle spell/translate panel */
    if (ch == KEY_ALT('S'))
        return toggle_spell_panel(app);

    /* Alt+W : close current tab */
    if (ch == KEY_ALT('W'))
    {
        TeTab *tab;
        EdInfo info;

        tab = app->tabs[app->active_tab];

        if (tab)
        {
            ed_get_info(tab->editor, &info);

            if (info.modified)
            {
                if (ui_popup_confirm("Close tab", "Tab has unsaved changes. Close anyway?") != 1)
                    return 1;
            }
        }

        te_app_close_tab(app, app->active_tab);

        /* If no tabs left, create a new empty tab */
        if (app->tab_count == 0)
        {
            TeTab *new_tab = te_tab_new();

            if (new_tab)
            {
                te_app_add_tab(app, new_tab);
                te_app_switch_tab(app, 0);
            }
        }

        return 1;
    }

    /* Alt+L : toggle tabs panel */
    if (ch == KEY_ALT('L'))
    {
        app->show_tabs = !app->show_tabs;
        return 1;
    }

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

    /* Ctrl+L : open file (create new tab) */
    if (!is_key && ch == CTRL('L'))
    {
        TeTab *new_tab;
        int result;

        new_tab = te_tab_new();

        if (!new_tab)
            return 1;

        te_app_add_tab(app, new_tab);
        te_app_switch_tab(app, app->tab_count - 1);

        result = ui_files_open(app);

        /* Re-apply configuration after loading file */
        ed_set_undo_levels(te_app_get_editor(app), app->cfg.undo_levels);
        ed_set_hard_wrap(te_app_get_editor(app), app->cfg.hard_wrap);

        return result;
    }

    /* Ctrl+N : new file (create new tab) */
    if (!is_key && ch == CTRL('N'))
    {
        TeTab *new_tab;

        new_tab = te_tab_new();

        if (!new_tab)
            return 1;

        te_app_add_tab(app, new_tab);
        te_app_switch_tab(app, app->tab_count - 1);

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

/* Handle navigation keys */
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
            ed_move_up(te_app_get_editor(app));

        return 1;

    case KEY_DOWN:
        if (soft)
        {
            soft_move_down_visual(app, width);
            *preserve_desired = 1;
        }
        else
            ed_move_down(te_app_get_editor(app));

        return 1;

    case KEY_LEFT:
        ed_move_left(te_app_get_editor(app));
        return 1;

    case KEY_RIGHT:
        ed_move_right(te_app_get_editor(app));
        return 1;

    case KEY_HOME:
        if (soft)
            soft_move_home_visual(app, width);
        else
            ed_move_home(te_app_get_editor(app));

        return 1;

    case KEY_END:
        if (soft)
            soft_move_end_visual(app, width);
        else
            ed_move_end(te_app_get_editor(app));

        return 1;

    case KEY_PPAGE:
        if (soft)
        {
            soft_move_pgup_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgup(te_app_get_editor(app), 0);

        return 1;

    case KEY_NPAGE:
        if (soft)
        {
            soft_move_pgdn_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgdn(te_app_get_editor(app), 0);

        return 1;

    case KEY_ENTER:
        ed_enter(te_app_get_editor(app));
        clear_search_highlights(app);

        return 1;

    case KEY_BACKSPACE:
    {
        EdInfo i2;
        ed_get_info(te_app_get_editor(app), &i2);

        if (i2.block.active)
        {
            ed_block_delete(te_app_get_editor(app));
            te_status(app, "Block deleted");
        }
        else
            ed_backspace(te_app_get_editor(app));

        clear_search_highlights(app);

        return 1;
    }

    case KEY_DC:
    {
        EdInfo i2;
        ed_get_info(te_app_get_editor(app), &i2);

        if (i2.block.active)
        {
            ed_block_delete(te_app_get_editor(app));
            te_status(app, "Block deleted");
        }
        else
            ed_delete(te_app_get_editor(app));

        clear_search_highlights(app);

        return 1;
    }

    case KEY_IC: /* Ins */
    case KEY_ALT('I'):
        ed_toggle_insert(te_app_get_editor(app));
        return 1;

    case KEY_CLEFT:
        ed_word_left(te_app_get_editor(app));
        return 1;

    case KEY_CRIGHT:
        ed_word_right(te_app_get_editor(app));
        return 1;

    case KEY_ALT('Z'):
        ed_redo(te_app_get_editor(app));
        app->hard_wrap = ed_get_hard_wrap(te_app_get_editor(app));
        clear_search_highlights(app);

        return 1;

    case KEY_ALT('D'):
        te_app_set_show_line_numbers(app, !te_app_get_show_line_numbers(app));
        te_status(app, "Line numbers: %s", te_app_get_show_line_numbers(app) ? "ON" : "OFF");
        return 1;

    case KEY_ALT('Q'):
    {
        app->hard_wrap = !app->hard_wrap;
        ed_set_hard_wrap(te_app_get_editor(app), app->hard_wrap);
        return 1;
    }

    default:
        return 0;
    }
}

/* Handle text editing keys */
static int handle_editing_keys(TeApp *app, int ch, wint_t wch, int soft, int width, int body_rows, int *preserve_desired)
{
    switch (ch)
    {
    case '\n':
    case '\r':
        ed_enter(te_app_get_editor(app));
        clear_search_highlights(app);

        return 1;

    case 8:
    case 127:
        ed_backspace(te_app_get_editor(app));
        clear_search_highlights(app);
        return 1;

    case CTRL('B'):
        if (soft)
            soft_move_home_visual(app, width);
        else
            ed_move_home(te_app_get_editor(app));

        return 1;

    case CTRL('E'):
        if (soft)
            soft_move_end_visual(app, width);
        else
            ed_move_end(te_app_get_editor(app));

        return 1;

    case CTRL('U'):
        if (soft)
        {
            soft_move_pgup_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgup(te_app_get_editor(app), 0);

        return 1;

    case CTRL('D'):
        if (soft)
        {
            soft_move_pgdn_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgdn(te_app_get_editor(app), 0);

        return 1;

    case CTRL('Y'):
        ed_delete_line(te_app_get_editor(app));
        clear_search_highlights(app);
        return 1;

    case CTRL('Z'):
        ed_undo(te_app_get_editor(app));
        app->hard_wrap = ed_get_hard_wrap(te_app_get_editor(app));
        clear_search_highlights(app);
        return 1;

    case CTRL('T'):
        ed_delete_word_right(te_app_get_editor(app));
        clear_search_highlights(app);
        return 1;

    case CTRL('_'):
        ed_delete_word_left(te_app_get_editor(app));
        clear_search_highlights(app);
        return 1;

    case '\t':
        ed_insert_tab(te_app_get_editor(app), 4);
        clear_search_highlights(app);
        return 1;

    default:
        if (wch >= 0x20 && wch != 127)
        {
            ed_insert_char(te_app_get_editor(app), (wchar_t)wch);
            clear_search_highlights(app);

            /* HARD-WRAP only: insert CR at wrap col; soft-wrap leaves line intact */
            if (app->hard_wrap)
            {
                int eff_wrap = editor_eff_wrap(app);

                if (eff_wrap > 0)
                {
                    EdInfo wi;
                    int linelen;

                    ed_get_info(te_app_get_editor(app), &wi);
                    linelen = ed_line_len(te_app_get_editor(app), wi.row);

                    /* Activate when cursor is at end of line */
                    if (wi.col == linelen)
                    {
                        const wchar_t *line = ed_line_wcs(te_app_get_editor(app), wi.row);

                        if (line)
                        {
                            int word_start = 0;
                            int word_len;
                            int space_available;
                            int k;
                            int hyphen_performed = 0;

                            /* Identify the current word (from last space to cursor) */
                            for (k = wi.col; k > 0; k--)
                            {
                                if (line[k - 1] == L' ' || line[k - 1] == L'\n' || line[k - 1] == L'\t')
                                {
                                    word_start = k;
                                    break;
                                }
                            }

                            word_len = wi.col - word_start;
                            space_available = eff_wrap - word_start;

#ifdef HAVE_HYPHEN
                            /* If word doesn't fit in available space, try hyphen */
                            if (word_len > space_available && space_available >= 0 && app->hyph_wrap_enabled && app->hyph_handle)
                            {
                                int hyph_break = hyph_find_break(app, &line[word_start], word_len, space_available);

                                /* Validate hyphenation breakpoint: must be valid and leave at least one character after hyphen */
                                if (hyph_break >= 0 && hyph_break > 0 && hyph_break < word_len - 1)
                                {
                                    int break_pos = word_start + hyph_break;

                                    /* Validate that break_pos is within the line */
                                    if (break_pos < linelen)
                                    {
                                        /* Save state for undo */
                                        ed_save_undo(te_app_get_editor(app));

                                        /* Insert '-' at breakpoint */
                                        ed_set_pos(te_app_get_editor(app), wi.row, break_pos);

                                        if (ed_insert_char(te_app_get_editor(app), L'-') == 0)
                                        {
                                            /* Insert '\n' after '-' using ed_enter to properly split the line */
                                            if (ed_enter(te_app_get_editor(app)) == 0)
                                            {
                                                /* Calculate tail: characters remaining after breakpoint in the word */
                                                int tail = word_len - hyph_break;

                                                if (tail < 0)
                                                    tail = 0;

                                                ed_set_pos(te_app_get_editor(app), wi.row + 1, tail);
                                                clear_search_highlights(app);

                                                hyphen_performed = 1;
                                            }
                                        }
                                    }
                                }
                            }
#endif

                            /* If line exceeds limit, try to break at space (but not if hyphen already handled it) */
                            if (wi.col > eff_wrap && !hyphen_performed)
                            {
                                int brk = -1;
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

                                if (wch == L' ')
                                {
                                    ed_backspace(te_app_get_editor(app)); /* replace trailing space with newline */
                                    ed_enter(te_app_get_editor(app));

                                    clear_search_highlights(app);
                                }
                                else if (brk >= 0)
                                {
                                    int tail = linelen - brk - 1;

                                    ed_set_pos(te_app_get_editor(app), wi.row, brk);
                                    ed_delete(te_app_get_editor(app));
                                    ed_enter(te_app_get_editor(app));

                                    clear_search_highlights(app);
                                    ed_set_pos(te_app_get_editor(app), wi.row + 1, tail);
                                }
                            }
                        }
                    }
                }
            }

            return 1;
        }

        return 0;
    }

    return 0;
}

/* Helper function to redraw editor */
static void redraw_editor(TeApp *app)
{
    erase();
    standend();

    /* Recalculate layout */
    wm_recalc_layout_left(app->wm, COLS, LINES, app->show_tabs, app->spell_panel_mode);

    te_draw_titlebar(app);
    ui_tabs_draw_panel(app);

    draw_body(app);

#ifdef HAVE_HUNSPELL
    ui_spell_draw_panel(app);
#endif /* HAVE_HUNSPELL */

    te_draw_statusbar(app);

    position_cursor(app);
    refresh();
}

void ui_editor_run(TeApp *app)
{
    int body_rows;
    int soft, width;
    int screen_dirty;

    if (!app)
        return;

    if (!te_app_get_editor(app))
        return;

    /* Reset soft-wrap state */
    s_soft_top_line = 0;
    s_soft_top_sub = 0;
    s_soft_desired_vcol = -1;
    s_soft_last_width = COLS;

    TE_BRACKET_PASTE_ON();

    screen_dirty = 1; /* Force initial redraw */

    for (;;)
    {
        wint_t wch;
        int wrc, ch, is_key, preserve_desired;
        TeWindow *win;

        /* Get editor window for dimensions */
        win = wm_get_window_by_type(app->wm, WIN_EDITOR);

        if (!win || !win->visible)
        {
            width = COLS;
            body_rows = LINES - 2;
        }
        else
        {
            width = win->w;
            body_rows = win->h;
        }

        soft = !app->hard_wrap;

        /* On resize, reset goal column to re-sync from new layout */
        if (COLS != s_soft_last_width)
        {
            soft_reset_desired();
            s_soft_last_width = COLS;
            screen_dirty = 1;
        }

        /* Redraw only when screen is dirty (has changes) */
        if (screen_dirty)
        {
            redraw_editor(app);
            screen_dirty = 0;
        }

        /* Adjust width for line numbers if enabled */
        if (te_app_get_show_line_numbers(app))
        {
            EdInfo info;

            ed_get_info(te_app_get_editor(app), &info);

            if (win && win->visible)
                width = win->w - lineno_width(info.line_count);
            else
                width = COLS - lineno_width(info.line_count);
        }

        if (body_rows < 1)
            body_rows = 1;

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

        /* Tabs panel navigation mode */
        if (app->tabs_panel_active)
        {
            if (is_key && ch == KEY_UP)
            {
                if (app->tabs_panel_selected > 0)
                    app->tabs_panel_selected--;

                screen_dirty = 1;
                continue;
            }

            if (is_key && ch == KEY_DOWN)
            {
                if (app->tabs_panel_selected < app->tab_count - 1)
                    app->tabs_panel_selected++;

                screen_dirty = 1;
                continue;
            }

            if (is_key && ch == KEY_ENTER)
            {
                te_app_switch_tab(app, app->tabs_panel_selected);

                app->tabs_panel_active = 0;
                screen_dirty = 1;
                continue;
            }

            if (is_key && ch == 27) /* ESC */
            {
                app->tabs_panel_active = 0;
                screen_dirty = 1;
                continue;
            }

            /* Any other key exits panel mode */
            app->tabs_panel_active = 0;
            screen_dirty = 1;
        }
        else
        {
            /* Enter tabs panel mode with Tab key */
            if (!is_key && ch == '\t' && app->show_tabs)
            {
                app->tabs_panel_active = 1;
                app->tabs_panel_selected = app->active_tab;

                screen_dirty = 1;
                continue;
            }
        }

        /* Bracketed paste - check BEFORE other keys to avoid interference */
        if (is_key && ch == KEY_PASTE_START)
        {
            char *buf = collect_bracketed_paste();

            if (buf)
            {
                char *wrapped = NULL;
                const char *to_insert = buf;
                int reported_len;

                ed_save_undo(te_app_get_editor(app));

                /* HARD-WRAP only: reflow pasted text; soft-wrap inserts verbatim */
                if (app->hard_wrap)
                {
                    int pw = editor_eff_wrap(app);

                    if (pw > 0)
                    {
                        wrapped = wrap_paste_text(app, buf, pw);

                        if (wrapped)
                            to_insert = wrapped;
                    }
                }

                ed_paste_text_with_undo(te_app_get_editor(app), to_insert);

                clear_search_highlights(app);
                soft_reset_desired();

                reported_len = (int)strlen(to_insert);

                te_status(app, "Pasted %d bytes", reported_len);

                free(buf);

                if (wrapped)
                    free(wrapped);
            }

            screen_dirty = 1;
            continue;
        }

        /* Handle TAB key explicitly before function keys */
        if (!is_key && ch == '\t')
        {
            ed_save_undo(te_app_get_editor(app));
            ed_insert_tab(te_app_get_editor(app), 4);

            clear_search_highlights(app);
            screen_dirty = 1;
            continue;
        }

        /* Handle function keys */
        if (handle_function_keys(app, ch, is_key))
        {
            screen_dirty = 1;
            continue;
        }

        /* ESC : exit search mode or quit */
        if (!is_key && ch == 27)
        {
            if (app->search.is_mode || app->search.only_mode)
            {
                clear_search_highlights(app);

                app->search.is_mode = 0;
                app->search.only_mode = 0;

                te_status(app, "Search mode exited");
                screen_dirty = 1;
                continue;
            }

            EdInfo info;
            ed_get_info(te_app_get_editor(app), &info);

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
            ed_get_info(te_app_get_editor(app), &info);

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
        {
            screen_dirty = 1;
            continue;
        }

        /* Handle control keys */
        if (handle_control_keys(app, ch, is_key))
        {
            screen_dirty = 1;
            continue;
        }

        /* Body key handling */
        if (is_key)
        {
            if (handle_navigation_keys(app, ch, soft, width, body_rows, &preserve_desired))
            {
                /* Reset desired column unless vertical move (UP/DOWN/PgUp/PgDn preserve it) */
                if (!preserve_desired)
                    soft_reset_desired();

                screen_dirty = 1;
                continue;
            }
        }
        else /* printable / control chars */
        {
            /* Try rapid paste detection first (fallback for terminals without bracketed paste) */
            if (wch >= 0x20 && wch != 127)
            {
                char *rapid_buf = collect_rapid_paste(wch);

                if (rapid_buf)
                {
                    /* Rapid paste detected - process as block */
                    char *wrapped = NULL;
                    const char *to_insert = rapid_buf;
                    int reported_len;

                    ed_save_undo(te_app_get_editor(app));

                    /* HARD-WRAP only: reflow pasted text; soft-wrap inserts verbatim */
                    if (app->hard_wrap)
                    {
                        int pw = editor_eff_wrap(app);

                        if (pw > 0)
                        {
                            wrapped = wrap_paste_text(app, rapid_buf, pw);

                            if (wrapped)
                                to_insert = wrapped;
                        }
                    }

                    ed_paste_text_with_undo(te_app_get_editor(app), to_insert);

                    clear_search_highlights(app);
                    soft_reset_desired();

                    reported_len = (int)strlen(to_insert);

                    te_status(app, "Pasted %d bytes", reported_len);

                    free(rapid_buf);

                    if (wrapped)
                        free(wrapped);

                    screen_dirty = 1;
                    continue;
                }
            }

            if (handle_editing_keys(app, ch, wch, soft, width, body_rows, &preserve_desired))
            {
                if (!preserve_desired)
                    soft_reset_desired();

                screen_dirty = 1;
                continue;
            }
        }

        if (!preserve_desired)
            soft_reset_desired();

        /* Redraw after processing key */
        redraw_editor(app);

        screen_dirty = 0;
    }
}
