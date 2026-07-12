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
#include "../core/portable.h"
#include "../components/editor.h"
#include "../components/ed_attr.h"
#include "te.h"
#include "ui_files.h"
#include "../components/fmt_rtf.h"
#include "../components/fmt_wp4.h"
#include "ui_editor_helper.h"
#include "ui_setup.h"

#include "ui_syntax.h"
#include "ui_spell.h"
#include "ui_grammar.h"
#include "ui_dict.h"
#include "ui_dict_picker.h"
#include "ui_dict_reverse.h"
#include "ui_glyph_picker.h"
#include "ui_mouse.h"
#include "ui_assist.h"

#ifdef HAVE_TRANSLATE
#include "ui_translate.h"
#endif

#ifdef HAVE_TTS
#include "ui_tts.h"
#endif

#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
#include "ui_hyph.h"
#endif

#if defined(HAVE_HUNSPELL) && defined(HAVE_MYTHES)
#include "ui_thes.h"
#endif

#define SYNTAX_CACHE_STEP 2048

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
        "    Ins / Alt+I      Toggle insert / overwrite",
        "    Ctrl+W           Rewrap paragraph",
        "    Tab              Insert tab",
        "    Alt+Q            Toggle wrap mode",
        "    Alt+D            Toggle line numbers",
        "    Alt+E            Toggle hyphen wrap",
        "    F3 / Alt+C       Choose output charset",
        "",
        "  Block (selection):",
        "    Ctrl+C           Copy block",
        "    Ctrl+X           Cut block",
        "    Ctrl+V           Paste block (or clipboard)",
        "    BS / Del         Delete block (no clipboard)",
        "    Ctrl+O           Export block to file",
        "    Alt+V            Sort selected lines",
        "    Alt+X            Convert case of selected block",
        "",
        "  Search:",
        "    F5 / Alt+F       Search / Replace current",
        "    Ctrl+R           Find & replace",
        "    Alt+G            Go to line",
        "    F3 / Alt+C       Prev",
        "    F4 / Alt+T       Next",
        "    F6 / Alt+B       Replace all (search mode)",
        "",
        "  Files:",
        "    F7 / Alt+O       Insert file at cursor",
        "    Ctrl+L           Open file (new tab)",
        "    Ctrl+Alt+Y       Open recent file",
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
#ifdef HAVE_HUNSPELL
        "",
        "  Spell checker:",
        "    Alt+S            Toggle spell/dict panel",
        "    Alt+H            Toggle spell checker",
        "    Alt+P            Spell check word under cursor",
#ifdef HAVE_MYTHES
        "    Alt+A            Thesaurus lookup for word under cursor",
#endif

#ifdef HAVE_HYPHEN
        "    Alt+E            Toggle hyphen wrap",
#endif

#endif /* HAVE_HUNSPELL */

#ifdef HAVE_TRANSLATE
        "",
        "  Translate:",
        "    Alt+R            Translate selected text",
        "    Alt+M            Dictionary popup (pick translation)",
        "    Alt+N            Reverse lookup (scan dict)",
        "    Ctrl+T           Toggle translator",
        "    Alt+B            Exchange languages",
        "    Alt+D            Toggle line numbers / dict panel",
#endif
#ifdef HAVE_TTS
        "",
        "  Text-to-speech:",
#ifdef PLATFORM_AMIGA
        "    Alt+Shift+X     Speak selection / paragraph",
        "    Alt+Shift+K     Speak entire document",
        "    Alt+Shift+P     Pause / resume speech",
        "    Alt+Shift+O     Stop speech",
        "    Alt+Shift+V     Voice settings popup",
#else
        "    Ctrl+Alt+X      Speak selection / paragraph",
        "    Ctrl+Alt+K      Speak entire document",
        "    Ctrl+Alt+P      Pause / resume speech",
        "    Ctrl+Alt+O      Stop speech",
        "    Ctrl+Alt+V      Voice settings popup",
#endif
#endif
        "",
        "  Rich text (WP4/RTF mode):",
#ifdef PLATFORM_AMIGA
        "    Alt+Shift+B      Bold",
        "    Alt+Shift+I      Italic",
        "    Alt+Shift+U      Underline",
        "    Alt+Shift+L      Align left",
        "    Alt+Shift+E      Align center",
        "    Alt+Shift+R      Align right",
        "    Alt+Shift+J      Justify",
#else
        "    Ctrl+Alt+B       Bold",
        "    Ctrl+Alt+I       Italic",
        "    Ctrl+Alt+U       Underline",
        "    Ctrl+Alt+L       Align left",
        "    Ctrl+Alt+E       Align center",
        "    Ctrl+Alt+R       Align right",
        "    Ctrl+Alt+J       Justify",
#endif
        "",
        "  Other:",
        "    F4 / Alt+T       Setup / configuration",
        "    Alt+U            Unicode glyph picker",
#ifdef PLATFORM_AMIGA
        "    Alt+Shift+S      Cycle syntax language",
#else
        "    Ctrl+Alt+S       Cycle syntax language",
#endif
};

#define HELP_N ((int)(sizeof(HELP_LINES) / sizeof(HELP_LINES[0])))

#ifdef PLATFORM_AMIGA
#define INSIDE_BLOCK_COMMENT_LOOKUP_LIMIT 256
#define SYNTAX_STATE_LOOKUP_LIMIT 256
#define BRACKET_MATCH_LOOKUP_LIMIT 256
#else
#define INSIDE_BLOCK_COMMENT_LOOKUP_LIMIT 1024
#define SYNTAX_STATE_LOOKUP_LIMIT 1024
#define BRACKET_MATCH_LOOKUP_LIMIT 1024
#endif

/* Soft-wrap viewport: anchored by top line/sub-row */
static int s_soft_top_line = 0;
static int s_soft_top_sub = 0;

/* Whitespace marker glyphs (hex escapes keep this C89-valid) */
static const wchar_t s_ws_arrow[2] = {0x2192, 0}; /* rightwards arrow */
static const wchar_t s_ws_dot[2] = {0xB7, 0};     /* middle dot */
static int s_soft_desired_vcol = -1;
static int s_soft_last_width = -1;
static int s_tab_width = 4; /* visual tab stop width, copied from config */

/* Alignment indent of the sub-row the cursor sits in (soft-wrap) */
static int line_align_indent(unsigned char align, int text_vw, int avail);

/* Insert a full Unicode codepoint; on Win32 wchar_t is 16-bit, so split into a surrogate pair */
static void editor_insert_cp(Ed *ed, unsigned long cp)
{
    if (cp < 0x10000)
    {
        ed_insert_char(ed, (wchar_t)cp);
    }
    else
    {
#ifdef PLATFORM_WIN32
        unsigned long v = cp - 0x10000;

        ed_insert_char(ed, (wchar_t)(0xD800 + (v >> 10)));
        ed_insert_char(ed, (wchar_t)(0xDC00 + (v & 0x3FF)));
#else
        ed_insert_char(ed, (wchar_t)cp);
#endif
    }
}

void soft_reset_desired(void)
{
    s_soft_desired_vcol = -1;
}

/* Soft-wrap: break at last space or hard-cut at boundary */
static int wrap_next(const wchar_t *line, int len, int width, int start)
{
    int vcol = 0;
    int col = 0;
    int k = start;
    int hard_end;
    int tab_width = s_tab_width > 0 ? s_tab_width : 4;

    if (width < 1)
        width = 1;

    /* Align tab stops to visual width of skipped prefix */
    if (start > 0 && start <= len)
        col = wcs_vwidth_ex(line, start, 0, tab_width);

    /* Walk forward, accumulating visual width */
    while (k < len)
    {
        int w = 1;

        if (line[k] == L'\t')
            w = tab_width - (col % tab_width);
        else
        {
            w = wcswidth(&line[k], 1);
            if (w <= 0)
                w = 1; /* control/zero-width -> 1 */
        }

        if (vcol + w > width)
            break;

        vcol += w;
        col += w;
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
    const wchar_t *l = NULL;
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
            const wchar_t *l = NULL;
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
    const wchar_t *l = NULL;
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

    /* start_col must be 0 for soft-wrap sub-row consistency */
    return wcs_vwidth_ex(&l[seg_start], n, 0, s_tab_width);
}

static int soft_cursor_align_indent(Ed *ed, int width)
{
    EdInfo info;
    const wchar_t *l = NULL;
    int len;
    int sub;
    int seg_start = 0, seg_end = 0;
    int seg_vw;

    ed_get_info(ed, &info);

    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);

    if (!l || len <= 0)
        return 0;

    sub = line_subrow_of_col(l, len, width, info.col);
    line_subrow_range(l, len, width, sub, &seg_start, &seg_end);

    seg_vw = wcs_vwidth_ex(&l[seg_start], seg_end - seg_start, 0, s_tab_width);

    return line_align_indent(ed->lines[info.row]->para_align, seg_vw, width);
}

/* Return visual rows between two positions. O(|b_line - a_line|) */
static int soft_vrows_between(Ed *ed, int width, int a_line, int a_sub, int b_line, int b_sub)
{
    int i;
    int delta = 0;
    const wchar_t *l = NULL;
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

/* Convert screen coordinates (y, x) to buffer position (line, col) Used for mouse clicks. Works in both wrap modes */
int ui_editor_screen_to_logical(TeApp *app, int width, int screen_y, int screen_x, int *out_line, int *out_col)
{
    Ed *ed = NULL;
    EdInfo info;
    int line;
    int sub;
    int remaining;
    int last;
    int ll;
    int soft;
    const wchar_t *l = NULL;
    int len;
    int j;
    int acc_w;
    int cw;
    int align_ind;

    if (!app || !out_line || !out_col)
        return -1;

    ed = te_app_get_editor(app);

    if (!ed)
        return -1;

    ed_get_info(ed, &info);

    if (info.line_count <= 0)
    {
        *out_line = 0;
        *out_col = 0;
        return 0;
    }

    if (width < 1)
        width = 1;

    if (screen_y < 0)
        screen_y = 0;

    if (screen_x < 0)
        screen_x = 0;

    /* In hard-wrap mode, use info.top directly (1 logical line = 1 screen row) */
    soft = !app->hard_wrap;

    line = soft ? s_soft_top_line : info.top + screen_y;
    sub = soft ? s_soft_top_sub : 0;
    remaining = soft ? screen_y : 0;

    if (!soft)
    {
        if (line >= info.line_count)
        {
            line = info.line_count - 1;

            ll = ed_line_len(ed, line);

            *out_line = line;
            *out_col = ll;
            return 0;
        }

        if (line < 0)
            line = 0;

        /* Calculate column from screen_x (walk char by char, summing visual width) */
        l = ed_line_wcs(ed, line);
        len = ed_line_len(ed, line);

        /* Paragraph alignment shifts the text right; subtract the indent before measuring */
        align_ind = line_align_indent(ed->lines[line]->para_align, (l && len > 0) ? wcs_vwidth_ex(l, len, 0, s_tab_width) : 0, width);

        screen_x -= align_ind;

        if (screen_x < 0)
            screen_x = 0;

        acc_w = 0;

        for (j = 0; j < len; j++)
        {
            cw = wcs_vwidth_ex(&l[j], 1, acc_w, s_tab_width);

            if (acc_w + cw > screen_x)
                break;

            acc_w += cw;
        }

        *out_line = line;
        *out_col = j;
        return 0;
    }

    /* Soft-wrap: walk forward through logical lines, consuming sub-rows */
    while (line < info.line_count)
    {
        const wchar_t *l = ed_line_wcs(ed, line);
        int ll = ed_line_len(ed, line);
        int n_sub = wrap_count(l ? l : L"", l ? ll : 0, width);
        int sub_left_in_line = n_sub - sub;

        if (remaining < sub_left_in_line)
        {
            /* Target sub-row is in this logical line at sub + remaining */
            int target_sub = sub + remaining;
            int seg_start = 0;
            int seg_end = 0;
            int j;
            int acc_w = 0;

            line_subrow_range(l ? l : L"", l ? ll : 0, width, target_sub, &seg_start, &seg_end);

            /* Paragraph alignment shifts the sub-row text right; subtract the indent before measuring */
            align_ind = line_align_indent(ed->lines[line]->para_align, (l && seg_end > seg_start) ? wcs_vwidth_ex(&l[seg_start], seg_end - seg_start, 0, s_tab_width) : 0, width);

            screen_x -= align_ind;

            if (screen_x < 0)
                screen_x = 0;

            /* Walk char by char, summing visual width until we exceed screen_x */
            for (j = seg_start; j < seg_end; j++)
            {
                int cw = wcs_vwidth_ex(&l[j], 1, acc_w, s_tab_width);

                if (acc_w + cw > screen_x)
                    break;

                acc_w += cw;
            }

            *out_line = line;
            *out_col = j; /* may equal seg_end (click past end of segment) */
            return 0;
        }

        remaining -= sub_left_in_line;
        line++;
        sub = 0;
    }

    /* y is past last line -> clamp to end of document */
    last = info.line_count - 1;
    ll = ed_line_len(ed, last);
    *out_line = last;
    *out_col = ll;

    return 0;
}

/* Return cursor screen row. O(|info.row - s_soft_top_line|) */
static int soft_cursor_screen_row(TeApp *app, int width)
{
    EdInfo info;
    Ed *ed = te_app_get_editor(app);
    int sub_cursor;
    const wchar_t *l = NULL;
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
    const wchar_t *l = NULL;
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
    const wchar_t *l = NULL;
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
            int w = wcs_vwidth_ex(&l[i], 1, v, s_tab_width);

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
    const wchar_t *l = NULL;
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
    const wchar_t *l = NULL;
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
    const wchar_t *l = NULL;
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
    wchar_t *w = NULL;
    int wlen = 0;
    int i;
    int line_start = 0;
    int last_space = -1;
    int col_pos = 0;
    int out_cap, out_len = 0;
    wchar_t *out = NULL;
    char *result = NULL;

#ifdef HAVE_HYPHEN
    int use_hyphen = 0;

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
            cw = s_tab_width - (col_pos % s_tab_width);

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
                                {
                                    if (out[m] == L'\t')
                                        break_col += s_tab_width - (break_col % s_tab_width);
                                    else
                                        break_col += paste_char_width(out[m]);
                                }

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
                                {
                                    if (out[m] == L'\t')
                                        width_after += s_tab_width - (width_after % s_tab_width);
                                    else
                                        width_after += paste_char_width(out[m]);
                                }

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
                    int w2;

                    if (out[j] == L'\t')
                        w2 = s_tab_width - (width_after % s_tab_width);
                    else
                        w2 = paste_char_width(out[j]);

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

/* Effective wrap column. Clamp to visible body width; 0=disabled */
int editor_eff_wrap(TeApp *app)
{
    int cfgw = app->wrap_col;
    int ln_offset = 0;
    int limit;

    if (cfgw <= 0)
        return 0; /* AUTOWRAP disabled */

    if (COLS <= 10)
        return 0; /* Too narrow: scroll instead */

    if (te_app_get_show_line_numbers(app))
    {
        EdInfo info;

        ed_get_info(te_app_get_editor(app), &info);
        ln_offset = editor_body_offset(app, info.line_count);
    }

    limit = COLS - ln_offset - 1; /* one column margin */

    if (limit < 20)
        limit = 20;

    if (cfgw > limit)
        return limit; /* Screen narrower than configured */

    return cfgw;
}

/* Text width for soft-wrap and alignment. In rich mode, clamp to wrap_col */
int editor_text_width(TeApp *app, int body_width)
{
    int cfgw;
    int limit;

    if (!app->rich_mode || body_width <= 0)
        return body_width;

    cfgw = app->wrap_col;

    if (cfgw <= 0)
        return body_width;

    limit = body_width - 1;

    if (limit < 1)
        limit = 1;

    if (cfgw > limit)
        return limit;

    return cfgw;
}

/* Hyphenation callback bridge for ed_rewrap_paragraph_ex() user_data is TeApp* */
#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
static int hwrap_hyph_cb(void *user_data, const wchar_t *word, int word_wlen, int col_limit)
{
    TeApp *app = (TeApp *)user_data;

    return hyph_find_break(app, word, word_wlen, col_limit);
}
#endif

/* Trigger paragraph reflow after an edit in hard-wrap mode */
void ed_auto_rewrap_after_edit(TeApp *app)
{
    int width;
    int (*hyph_cb)(void *, const wchar_t *, int, int) = NULL;
    void *hyph_data = NULL;

    if (!app->hard_wrap)
        return;

    width = editor_eff_wrap(app);

    if (width <= 0)
        return;

#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
    if (app->hyph_wrap_enabled && app->hyph_handle)
    {
        hyph_cb = hwrap_hyph_cb;
        hyph_data = app;
    }
#endif

    te_app_get_editor(app)->undo_snapshot_mode = 1;

    ed_rewrap_paragraph_ex(te_app_get_editor(app), width, hyph_cb, hyph_data);

    te_app_get_editor(app)->undo_snapshot_mode = 0;
    ed_ensure_visible(te_app_get_editor(app));
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

#ifdef HAVE_HUNSPELL
/* Check whether a word is misspelled, ignoring words split across two lines by a trailing hyphen (e.g. "adqui-" / "rido") */
static int spell_word_incorrect(TeApp *app, int line_idx, const wchar_t *line, int line_len, int word_start, int word_end)
{
    int word_len = word_end - word_start;
    int incorrect = 0;
    int joined_len = 0;
    int i, j;
    int next_end = 0;
    int prev_word_end = 0;
    int prev_word_start = 0;
    int next_len = 0;
    int prev_len = 0;
    wchar_t joined[512];
    const wchar_t *next_line = NULL;
    const wchar_t *prev_line = NULL;
    Ed *ed = NULL;

    if (word_len <= 1 || !app || !app->spell_handle || !app->spell_active)
        return 0;

    incorrect = ui_spell_check_word_simple(app, &line[word_start], word_len);

    if (!incorrect)
        return 0;

    ed = te_app_get_editor(app);

    if (!ed || line_idx < 0 || line_idx >= ed->count)
        return incorrect;

    /* Word followed by '-' at EOL and continuation on next line */
    if (word_end < line_len && line[word_end] == L'-' && word_end + 1 == line_len && line_idx + 1 < ed->count)
    {
        next_line = ed_line_wcs(ed, line_idx + 1);
        next_len = next_line ? ed_line_len(ed, line_idx + 1) : -1;

        if (next_len > 0)
        {
            while (next_end < next_len && iswalnum(next_line[next_end]))
                next_end++;

            if (next_end > 0 && iswlower(next_line[0]))
            {
                for (i = 0; i < word_len && joined_len < 510; i++)
                    joined[joined_len++] = line[word_start + i];

                for (j = 0; j < next_end && joined_len < 510; j++)
                    joined[joined_len++] = next_line[j];

                joined[joined_len] = L'\0';

                if (!ui_spell_check_word_simple(app, joined, joined_len))
                    incorrect = 0;
            }
        }
    }

    /* Word at column 0 preceded by "word-" on previous line */
    if (incorrect && word_start == 0 && line_idx > 0)
    {
        prev_line = ed_line_wcs(ed, line_idx - 1);
        prev_len = prev_line ? ed_line_len(ed, line_idx - 1) : -1;

        if (prev_len >= 2 && prev_line[prev_len - 1] == L'-')
        {
            prev_word_end = prev_len - 1;
            prev_word_start = prev_word_end;

            while (prev_word_start > 0 && iswalnum(prev_line[prev_word_start - 1]))
                prev_word_start--;

            if (prev_word_end > prev_word_start)
            {
                joined_len = 0;

                for (j = prev_word_start; j < prev_word_end && joined_len < 510; j++)
                    joined[joined_len++] = prev_line[j];

                for (i = 0; i < word_len && joined_len < 510; i++)
                    joined[joined_len++] = line[word_start + i];

                joined[joined_len] = L'\0';

                if (!ui_spell_check_word_simple(app, joined, joined_len))
                    incorrect = 0;
            }
        }
    }

    return incorrect;
}
#endif

static SyntaxState state_after_line(const wchar_t *line, int len, SyntaxState state, SyntaxLang lang)
{
    int i;

    for (i = 0; i < len; i++)
    {
        if (state == SYNTAX_STATE_COMMENT)
        {
            if (i + 1 < len && line[i] == L'*' && line[i + 1] == L'/')
            {
                state = SYNTAX_STATE_NORMAL;
                i++;
            }

            continue;
        }

        if (state == SYNTAX_STATE_STRING)
        {
            if (line[i] == L'\\' && i + 1 < len)
                i++;
            else if (ui_syntax_is_str_quote(line[i]))
                state = SYNTAX_STATE_NORMAL;

            continue;
        }

        if (state == SYNTAX_STATE_CHAR)
        {
            if (line[i] == L'\\' && i + 1 < len)
                i++;
            else if (ui_syntax_is_char_quote(line[i]))
                state = SYNTAX_STATE_NORMAL;

            continue;
        }

        if (lang == SYNTAX_LANG_C || lang == SYNTAX_LANG_CPP || lang == SYNTAX_LANG_M68K_C)
        {
            if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'*')
            {
                state = SYNTAX_STATE_COMMENT;
                i++;

                continue;
            }

            if (i + 1 < len && line[i] == L'/' && line[i + 1] == L'/')
                break;

            if (ui_syntax_is_str_quote(line[i]))
            {
                state = SYNTAX_STATE_STRING;
                continue;
            }

            if (ui_syntax_is_char_quote(line[i]))
            {
                state = SYNTAX_STATE_CHAR;
                continue;
            }
        }
        else if (lang == SYNTAX_LANG_X86_ASM || lang == SYNTAX_LANG_M68K_ASM)
        {
            if (line[i] == L';' || (i + 1 < len && line[i] == L'/' && line[i + 1] == L'/'))
                break;

            if (ui_syntax_is_str_quote(line[i]))
            {
                state = SYNTAX_STATE_STRING;
                continue;
            }

            if (ui_syntax_is_char_quote(line[i]))
            {
                state = SYNTAX_STATE_CHAR;
                continue;
            }
        }
    }

    if (state == SYNTAX_STATE_STRING || state == SYNTAX_STATE_CHAR)
        state = SYNTAX_STATE_NORMAL;

    return state;
}

static SyntaxState compute_syntax_state_at(Ed *ed, int row, SyntaxLang lang)
{
    int r;
    int r_min;
    int count;
    SyntaxState state;
    int *cache = NULL;

    if (lang <= SYNTAX_LANG_NONE || row <= 0)
        return SYNTAX_STATE_NORMAL;

    if (!ed)
        return SYNTAX_STATE_NORMAL;

    count = ed->count;

    if (row > count)
        row = count;

    if (ed->syntax_state_alloc < count)
    {
        int new_alloc = count + SYNTAX_CACHE_STEP;
        int *new_cache = (int *)realloc(ed->syntax_state_cache, (size_t)new_alloc * sizeof(int));

        if (!new_cache)
            return SYNTAX_STATE_NORMAL;

        for (r = ed->syntax_state_alloc; r < new_alloc; r++)
            new_cache[r] = -1;

        ed->syntax_state_cache = new_cache;
        ed->syntax_state_alloc = new_alloc;
    }

    cache = ed->syntax_state_cache;

    if (ed->syntax_state_lang != (int)lang)
    {
        for (r = 0; r < ed->syntax_state_alloc; r++)
            cache[r] = -1;

        ed->syntax_state_lang = (int)lang;
    }

    if (ed->syntax_state_dirty_from >= 0)
    {
        for (r = ed->syntax_state_dirty_from; r < ed->syntax_state_alloc; r++)
            cache[r] = -1;

        ed->syntax_state_dirty_from = -1;
    }

    if (cache[row] >= 0)
        return (SyntaxState)cache[row];

    cache[0] = SYNTAX_STATE_NORMAL;

    state = SYNTAX_STATE_NORMAL;
    r = 0;
    r_min = row - SYNTAX_STATE_LOOKUP_LIMIT;

    if (r_min < 0)
        r_min = 0;

    for (r = row - 1; r >= r_min; r--)
    {
        if (cache[r] >= 0)
        {
            state = (SyntaxState)cache[r];
            break;
        }
    }

    if (r < 0)
        r = 0;

    for (; r < row; r++)
    {
        const wchar_t *line = ed_line_wcs(ed, r);
        int len = ed_line_len(ed, r);

        state = state_after_line(line, len, state, lang);
        cache[r + 1] = (int)state;
    }

    return state;
}

/* Alignment indent: center/right offset, left/justify return 0 */
static int line_align_indent(unsigned char align, int text_vw, int avail)
{
    int slack = avail - text_vw;

    /* Empty lines stay flush left to keep cursor/selection at margin */
    if (text_vw <= 0)
        return 0;

    if (slack <= 0)
        return 0;

    if (align == EA_ALIGN_CENTER)
        return slack / 2;

    if (align == EA_ALIGN_RIGHT)
        return slack;

    return 0;
}

/* Draw editor body */
static void draw_body(TeApp *app)
{
    EdInfo info;
    TeWindow *win = NULL;
    int rich_bar_h = app->rich_mode ? 1 : 0;
    int body_top = 1 + rich_bar_h;
    int body_bot = LINES - 2;
    int body_rows;
    int width = COLS;
    int body_width = COLS;
    int offset_x = 0;
    int offset_y = 0;
    int soft = !app->hard_wrap;
    int b_r1 = -1;
    int b_c1 = 0;
    int b_r2 = -1;
    int b_c2 = 0;
    int ln_width = 0;
    int ln_offset = 0;
    int show_lnum = te_app_get_show_line_numbers(app);
    int match_row;
    int match_col;
    TeTab *tab = te_app_get_active_tab(app);
    SyntaxLang lang = SYNTAX_LANG_NONE;
    SyntaxState syntax_state = SYNTAX_STATE_NORMAL;
    SyntaxClass *shared_classes = NULL;
    int shared_classes_cap = 0;
    Ed *ed = te_app_get_editor(app);

    if (app->cfg.syntax_enabled)
        lang = (tab && tab->syntax_lang != SYNTAX_LANG_NONE) ? tab->syntax_lang : ui_syntax_lang_from_filename(te_app_get_filename(app));

    standend();

    /* Use window coordinates from layout manager */
    win = wm_get_window_by_type(app->wm, WIN_EDITOR);

    if (win && win->visible)
    {
        body_top = win->y;
        body_bot = win->y + win->h - 1;
        width = win->w;
        body_width = win->w;
        offset_x = win->x;
        offset_y = win->y;
    }

    body_rows = body_bot - body_top;

    if (body_rows < 1)
        body_rows = 1;

    ed_set_page(te_app_get_editor(app), body_rows);
    ed_ensure_visible(te_app_get_editor(app));
    ed_get_info(te_app_get_editor(app), &info);

    if (!soft && lang > SYNTAX_LANG_NONE)
        syntax_state = compute_syntax_state_at(ed, info.top, lang);

    /* Bracket matching: find partner bracket across buffer */
    match_row = -1;
    match_col = -1;
    app->bracket_match_row = -1;
    app->bracket_match_col = -1;

    if (app->cfg.show_brackets)
    {
        const wchar_t *line = ed_line_wcs(te_app_get_editor(app), info.row);
        int line_len = ed_line_len(te_app_get_editor(app), info.row);

        if (line && info.col < line_len)
        {
            wchar_t ch = line[info.col];
            wchar_t partner = 0;
            int dir = 0;

            switch (ch)
            {
            case L'(':
                partner = L')';
                dir = +1;
                break;
            case L'[':
                partner = L']';
                dir = +1;
                break;
            case L'{':
                partner = L'}';
                dir = +1;
                break;
            case L')':
                partner = L'(';
                dir = -1;
                break;
            case L']':
                partner = L'[';
                dir = -1;
                break;
            case L'}':
                partner = L'{';
                dir = -1;
                break;
            default:
                break;
            }

            if (partner)
            {
                int depth = 1;
                int row;

                if (dir > 0)
                {
                    int row_max = info.row + BRACKET_MATCH_LOOKUP_LIMIT;

                    if (row_max >= info.line_count)
                        row_max = info.line_count - 1;

                    for (row = info.row; row <= row_max && match_row < 0; row++)
                    {
                        const wchar_t *rl = ed_line_wcs(te_app_get_editor(app), row);
                        int rl_len = ed_line_len(te_app_get_editor(app), row);
                        int col;
                        int start_col = (row == info.row) ? info.col + 1 : 0;

                        if (!rl)
                            continue;

                        for (col = start_col; col < rl_len; col++)
                        {
                            if (rl[col] == ch)
                                depth++;
                            else if (rl[col] == partner)
                            {
                                depth--;

                                if (depth == 0)
                                {
                                    match_row = row;
                                    match_col = col;
                                    break;
                                }
                            }
                        }
                    }
                }
                else
                {
                    int row_min = info.row - BRACKET_MATCH_LOOKUP_LIMIT;

                    if (row_min < 0)
                        row_min = 0;

                    for (row = info.row; row >= row_min && match_row < 0; row--)
                    {
                        const wchar_t *rl = ed_line_wcs(te_app_get_editor(app), row);
                        int rl_len = ed_line_len(te_app_get_editor(app), row);
                        int col;
                        int start_col = (row == info.row) ? info.col - 1 : rl_len - 1;

                        if (!rl)
                            continue;

                        for (col = start_col; col >= 0; col--)
                        {
                            if (rl[col] == ch)
                                depth++;
                            else if (rl[col] == partner)
                            {
                                depth--;

                                if (depth == 0)
                                {
                                    match_row = row;
                                    match_col = col;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if (match_row >= 0)
            {
                app->bracket_match_row = match_row;
                app->bracket_match_col = match_col;
            }
        }
    }

    /* Calculate line number width if enabled */
    if (show_lnum)
    {
        ln_width = lineno_width(info.line_count);
        ln_offset = editor_body_offset(app, info.line_count);
        width = win->w - ln_offset; /* reduce width for text */
    }

    body_width = width;

    /* In rich mode, wrap and align to the configured page width */
    width = editor_text_width(app, width);

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

    /* SOFT-WRAP: one logical line spans several screen rows */
    if (soft)
    {
        int li, sr;
        int sub_skip;

        /* Ensure cursor is inside viewport. Adjust s_soft_top_line/sub */
        soft_ensure_visible(app, width, body_rows);

        if (lang > SYNTAX_LANG_NONE)
            syntax_state = compute_syntax_state_at(ed, s_soft_top_line, lang);

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
            SyntaxClass *line_classes = NULL;

            if (!l || len <= 0)
            {
                /* Empty line: one blank sub-row. Paint if not skipped */
                if (sub_skip == 0)
                {
                    if (show_lnum)
                    {
                        attrset(COLOR_PAIR(COL_BORDER));
                        mvprintw(offset_y + sr, offset_x, "%*d", ln_width - 1, li + 1);

                        standend();
                        attron(COLOR_PAIR(COL_NORMAL));
                    }

                    /* Current-line highlight across the empty row */
                    if (app->cfg.highlight_line && li == info.row)
                    {
                        int x;

                        attron(COLOR_PAIR(COL_CURRENT_LINE));

                        for (x = offset_x + ln_offset; x < offset_x + ln_offset + width; x++)
                            mvaddch(offset_y + sr, x, ' ');

                        attroff(COLOR_PAIR(COL_CURRENT_LINE));
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

            if (lang > SYNTAX_LANG_NONE && l && len > 0)
            {
                if (len > shared_classes_cap)
                {
                    int new_cap = len + 256;
                    SyntaxClass *new_classes = (SyntaxClass *)realloc(shared_classes, (size_t)new_cap * sizeof(SyntaxClass));

                    if (new_classes)
                    {
                        shared_classes = new_classes;
                        shared_classes_cap = new_cap;
                    }
                }

                if (len <= shared_classes_cap)
                {
                    line_classes = shared_classes;
                    syntax_state = ui_syntax_classify(l, len, line_classes, syntax_state, lang);
                }
            }

            while (sr < body_rows)
            {
                int seg_start = pos;
                int seg_end = wrap_next(l, len, width, pos);
                int np = seg_end;
                int text_vw;
                int x_text_end;
                int x_screen_end;
                int has_more_subrows;

                if (s >= sub_skip)
                {
                    int seg_len = seg_end - seg_start;

                    /* Tab offset is 0 inside each sub-row */
                    int seg_start_vcol = 0;

                    /* Alignment indent for this sub-row */
                    int align_ind;
                    int eff_ln_offset;

                    if (seg_len < 0)
                        seg_len = 0;

                    align_ind = line_align_indent(l ? ed->lines[li]->para_align : 0, seg_len > 0 ? wcs_vwidth_ex(&l[seg_start], seg_len, 0, s_tab_width) : 0, width);
                    eff_ln_offset = ln_offset + align_ind;

                    /* Line number on first painted sub-row */
                    if (show_lnum && first_seg)
                    {
                        attrset(COLOR_PAIR(COL_BORDER));
                        mvprintw(offset_y + sr, offset_x, "%*d", ln_width - 1, li + 1);
                        standend();

                        attron(COLOR_PAIR(COL_NORMAL));

                        first_seg = 0;
                    }

                    if (seg_len > 0)
                    {
                        if (line_classes)
                            ui_draw_wcs_line_with_tabs_and_colors(offset_y + sr, offset_x + eff_ln_offset, &l[seg_start], seg_len, s_tab_width, &line_classes[seg_start], seg_start_vcol);
                        else
                            ui_draw_wcs_line_with_tabs(offset_y + sr, offset_x + eff_ln_offset, &l[seg_start], seg_len, s_tab_width);

                        ui_draw_wcs_attr_runs(offset_y + sr, offset_x + eff_ln_offset, l, te_app_get_editor(app)->lines[li], seg_start, seg_end, seg_start_vcol, s_tab_width);
                    }

                    /* Paint tabs and trailing spaces as visible glyphs */
                    if (app->cfg.show_whitespace && seg_len > 0)
                    {
                        int k;
                        int trail_start;

                        /* Find trailing whitespace start in this segment */
                        trail_start = seg_end;

                        if (seg_end == len)
                        {
                            while (trail_start > seg_start && (l[trail_start - 1] == L' ' || l[trail_start - 1] == L'\t'))
                                trail_start--;
                        }

                        attron(COLOR_PAIR(COL_BORDER));

                        for (k = 0; k < seg_len; k++)
                        {
                            wchar_t ch = l[seg_start + k];
                            int col_x = offset_x + eff_ln_offset + wcs_vwidth_ex(&l[seg_start], k, seg_start_vcol, s_tab_width);

                            if (ch == L'\t')
                            {
                                /* Mark every tab with an arrow */
#ifdef PLATFORM_AMIGA
                                if (app->cfg.ttf_enabled)
                                    mvaddnwstr(offset_y + sr, col_x, s_ws_arrow, 1);
                                else
                                    mvaddch(offset_y + sr, col_x, '>');
#else
                                mvaddnwstr(offset_y + sr, col_x, s_ws_arrow, 1);
#endif
                            }
                            else if (ch == L' ' && (seg_start + k) >= trail_start)
                            {
                                /* Trailing space: middle dot */
                                mvaddnwstr(offset_y + sr, col_x, s_ws_dot, 1);
                            }
                        }

                        attroff(COLOR_PAIR(COL_BORDER));
                        standend();
                    }

                    /* Highlight matched bracket partner only */
                    if (app->cfg.show_brackets && seg_len > 0 && app->bracket_match_row == li && app->bracket_match_col >= seg_start && app->bracket_match_col < seg_end)
                    {
                        int tc = app->bracket_match_col;
                        int col_x = offset_x + eff_ln_offset + wcs_vwidth_ex(&l[seg_start], tc - seg_start, seg_start_vcol, s_tab_width);

                        attron(COLOR_PAIR(COL_BRACKET_MATCH) | A_BOLD);
                        mvaddnwstr(offset_y + sr, col_x, &l[tc], 1);
                        attroff(COLOR_PAIR(COL_BRACKET_MATCH) | A_BOLD);

                        standend();
                    }

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
                                    mvaddnwstr(offset_y + sr, offset_x + eff_ln_offset + wcs_vwidth_ex(&l[seg_start], match_col - seg_start, seg_start_vcol, s_tab_width), &l[match_col], match_len);
                                    attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                                }
                                else if (match_col >= seg_start && match_col < seg_end)
                                {
                                    int partial_len = seg_end - match_col;
                                    attron(COLOR_PAIR(COL_SEARCH_MATCH));
                                    mvaddnwstr(offset_y + sr, offset_x + eff_ln_offset + wcs_vwidth_ex(&l[seg_start], match_col - seg_start, seg_start_vcol, s_tab_width), &l[match_col], partial_len);
                                    attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                                }
                                else if (match_end > seg_start && match_end <= seg_end)
                                {
                                    int partial_len = match_end - seg_start;
                                    attron(COLOR_PAIR(COL_SEARCH_MATCH));
                                    mvaddnwstr(offset_y + sr, offset_x + eff_ln_offset, &l[seg_start], partial_len);
                                    attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                                }
                            }
                        }
                        standend();
                    }

#ifdef HAVE_HUNSPELL
                    /* Highlight misspelled words AND/OR repeated words. Enter loop if either feature is active */
                    if ((app->spell_active && app->spell_handle) || app->cfg.assist_repeat_check)
                    {
                        int word_start = seg_start;
                        int word_end;
                        int spell_on = (app->spell_active && app->spell_handle);

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
                                int marked = 0;

                                /* Ignore single-character words */
                                if (spell_on && word_len > 1 && spell_word_incorrect(app, li, l, len, word_start, word_end))
                                {
                                    attron(COLOR_PAIR(COL_SPELL_CURRENT));
                                    mvaddnwstr(offset_y + sr, offset_x + eff_ln_offset + wcs_vwidth_ex(&l[seg_start], word_start - seg_start, seg_start_vcol, s_tab_width), &l[word_start], word_len);
                                    attroff(COLOR_PAIR(COL_SPELL_CURRENT));

                                    marked = 1;
                                }

                                /* Repeated-word check (independent of spell) Highlight if previous word on same line is the same */
                                if (!marked && app->cfg.assist_repeat_check && ui_assist_check_repeat(app, li, word_start, word_len))
                                {
                                    attron(A_REVERSE);
                                    mvaddnwstr(offset_y + sr, offset_x + eff_ln_offset + wcs_vwidth_ex(&l[seg_start], word_start - seg_start, seg_start_vcol, s_tab_width), &l[word_start], word_len);
                                    attroff(A_REVERSE);
                                }
                            }

                            word_start = word_end;
                        }
                    }
#endif /* HAVE_HUNSPELL */

#ifdef HAVE_GRAMMAR
                    /* Grammar overlay for this sub-row segment, runs once per logical line (LRU cache absorbs subsequent sub-rows) */
                    if (app->grammar_active && app->grammar_handle && l && len > 0 && seg_start < seg_end)
                        ui_grammar_draw_row_segment(app, offset_y + sr, offset_x + eff_ln_offset, s_tab_width, l, len, seg_start, seg_end, seg_start_vcol, li);

#endif

                    standend();

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
                            mvaddnwstr(offset_y + sr, offset_x + eff_ln_offset + wcs_vwidth_ex(&l[seg_start], hs - seg_start, seg_start_vcol, s_tab_width), &l[hs], he - hs);
                            attroff(A_REVERSE);
                        }
                        else if (hs == seg_start && he == seg_start)
                        {
                            attron(A_REVERSE);
                            mvaddch(offset_y + sr, offset_x + eff_ln_offset, ' ');
                            attroff(A_REVERSE);
                        }
                    }

                    standend();

                    /* Visual overlays using colour-pairs */
                    text_vw = wcs_vwidth_ex(&l[seg_start], seg_len, 0, s_tab_width);
                    x_text_end = offset_x + eff_ln_offset + text_vw;
                    x_screen_end = offset_x + ln_offset + width;
                    has_more_subrows = (seg_end < len);

                    /* Highlight current line: full-width bar under text */
                    if (app->cfg.highlight_line && li == info.row)
                    {
                        int x;
                        int x_text_start = offset_x + eff_ln_offset;

                        attron(COLOR_PAIR(COL_CURRENT_LINE));

                        for (x = offset_x + ln_offset; x < x_text_start; x++)
                            mvaddch(offset_y + sr, x, ' ');

                        for (x = x_text_end; x < x_screen_end; x++)
                            mvaddch(offset_y + sr, x, ' ');

                        attroff(COLOR_PAIR(COL_CURRENT_LINE));
                    }

                    /* Draw column ruler */
                    if (app->cfg.ruler_col > 0)
                    {
                        int rx = offset_x + ln_offset + app->cfg.ruler_col;

                        if (rx >= x_text_end && rx < offset_x + ln_offset + body_width)
                        {
                            attron(COLOR_PAIR(COL_GUIDE));
                            mvaddch(offset_y + sr, rx, '|');
                            attroff(COLOR_PAIR(COL_GUIDE));
                        }
                    }

                    /* Draw wrap indicator */
                    if (app->cfg.wrap_indicator && has_more_subrows && x_screen_end > 0)
                    {
                        int free_cols = x_screen_end - x_text_end;

                        if (free_cols >= 1)
                        {
                            attron(COLOR_PAIR(COL_GUIDE));

                            if (free_cols >= 2)
                            {
                                int wx = x_screen_end - 2;
                                wchar_t wrap_mark[2];
#ifdef PLATFORM_AMIGA
                                wrap_mark[0] = app->cfg.ttf_enabled ? L'\x21B5' : L'<';
#else
                                wrap_mark[0] = L'\x21B5';
#endif
                                wrap_mark[1] = L'\0';

                                mvaddnwstr(offset_y + sr, wx, wrap_mark, 1);
                                mvaddch(offset_y + sr, x_screen_end - 1, ' ');
                            }
                            else
                            {
                                mvaddch(offset_y + sr, x_screen_end - 1, '<');
                            }

                            attroff(COLOR_PAIR(COL_GUIDE));
                        }
                    }

                    /* Draw indent guides */
                    if (app->cfg.indent_guides && s == sub_skip && seg_len > 0)
                    {
                        int k;
                        int leading_count = 0;

                        for (k = seg_start; k < seg_end && k < len; k++)
                        {
                            if (l[k] != L' ')
                                break;

                            leading_count++;
                        }

                        if (leading_count > 0 && s_tab_width > 1)
                        {
                            int g;

                            attron(COLOR_PAIR(COL_GUIDE));

                            for (g = s_tab_width; g <= leading_count; g += s_tab_width)
                            {
                                int gx = offset_x + ln_offset + g - 1;

                                if (gx >= offset_x + ln_offset && gx < offset_x + ln_offset + width)
                                    mvaddch(offset_y + sr, gx, '|');
                            }

                            attroff(COLOR_PAIR(COL_GUIDE));
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
            const wchar_t *wl = NULL;
            int line_vw;
            int x_text_end;
            int x_screen_end;
            int eff_ln_offset = ln_offset;
            SyntaxClass *line_classes = NULL;

            move(offset_y + i, offset_x);
            clrtoeol();

            if (line_idx >= info.line_count)
                continue;

            /* Draw line number if enabled */
            if (show_lnum)
            {
                attrset(COLOR_PAIR(COL_BORDER));
                mvprintw(offset_y + i, offset_x, "%*d", ln_width - 1, line_idx + 1);
                standend();

                attron(COLOR_PAIR(COL_NORMAL));
            }

            /* mvaddnwstr: n is in wide chars */
            wl = ed_line_wcs(te_app_get_editor(app), line_idx);
            line_len = ed_line_len(te_app_get_editor(app), line_idx);

            /* Alignment indent for the whole line */
            eff_ln_offset = ln_offset + line_align_indent(ed->lines[line_idx]->para_align, (wl && line_len > 0) ? wcs_vwidth_ex(wl, line_len, 0, s_tab_width) : 0, width);

            if (lang > SYNTAX_LANG_NONE && wl && line_len > 0)
            {
                if (line_len > shared_classes_cap)
                {
                    int new_cap = line_len + 256;
                    SyntaxClass *new_classes = (SyntaxClass *)realloc(shared_classes, (size_t)new_cap * sizeof(SyntaxClass));

                    if (new_classes)
                    {
                        shared_classes = new_classes;
                        shared_classes_cap = new_cap;
                    }
                }

                if (line_len <= shared_classes_cap)
                {
                    line_classes = shared_classes;
                    syntax_state = ui_syntax_classify(wl, line_len, line_classes, syntax_state, lang);
                }
            }

            if (wl && line_len > 0)
            {
                /* Limit line length to available width */
                int max_chars = width;

                if (line_len > max_chars)
                    line_len = max_chars;

                if (line_classes)
                    ui_draw_wcs_line_with_tabs_and_colors(offset_y + i, offset_x + eff_ln_offset, wl, line_len, s_tab_width, line_classes, 0);
                else
                    ui_draw_wcs_line_with_tabs(offset_y + i, offset_x + eff_ln_offset, wl, line_len, s_tab_width);

                ui_draw_wcs_attr_runs(offset_y + i, offset_x + eff_ln_offset, wl, te_app_get_editor(app)->lines[line_idx], 0, line_len, 0, s_tab_width);
            }

            /* Highlight matched bracket partner only */
            if (app->cfg.show_brackets && app->bracket_match_row == line_idx && app->bracket_match_col >= 0 && app->bracket_match_col < line_len && wl)
            {
                int tc = app->bracket_match_col;
                int col_x = offset_x + eff_ln_offset + wcs_vwidth_ex(wl, tc, 0, s_tab_width);

                attron(COLOR_PAIR(COL_BRACKET_MATCH) | A_BOLD);
                mvaddnwstr(offset_y + i, col_x, &wl[tc], 1);
                attroff(COLOR_PAIR(COL_BRACKET_MATCH) | A_BOLD);

                standend();
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
                            mvaddnwstr(offset_y + i, offset_x + eff_ln_offset + wcs_vwidth_ex(wl, match_col, 0, s_tab_width), &wl[match_col], match_len);
                            attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
                standend();
            }

#ifdef HAVE_HUNSPELL
            /* Highlight misspelled words AND/OR repeated words */
            if ((app->spell_active && app->spell_handle) || app->cfg.assist_repeat_check)
            {
                int word_start = 0;
                int word_end;
                int spell_on = (app->spell_active && app->spell_handle);

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
                        int marked = 0;

                        /* Ignore single-character words like word processors do */
                        if (spell_on && word_len > 1 && spell_word_incorrect(app, line_idx, wl, line_len, word_start, word_end))
                        {
                            attron(COLOR_PAIR(COL_SPELL_CURRENT));
                            mvaddnwstr(offset_y + i, offset_x + eff_ln_offset + wcs_vwidth_ex(wl, word_start, 0, s_tab_width), &wl[word_start], word_len);
                            attroff(COLOR_PAIR(COL_SPELL_CURRENT));

                            marked = 1;
                        }

                        /* Repeated-word check (independent of spell active) */
                        if (!marked && app->cfg.assist_repeat_check && ui_assist_check_repeat(app, line_idx, word_start, word_len))
                        {
                            attron(A_REVERSE);
                            mvaddnwstr(offset_y + i, offset_x + eff_ln_offset + wcs_vwidth_ex(wl, word_start, 0, s_tab_width), &wl[word_start], word_len);
                            attroff(A_REVERSE);
                        }
                    }

                    word_start = word_end;
                }
            }
#endif /* HAVE_HUNSPELL */

#ifdef HAVE_GRAMMAR
            /* Grammar/punctuation overlay, viewport-scoped, LRU cache absorbs redraws, prewarm extends coverage for smooth scrolling */
            if (app->grammar_active && app->grammar_handle && wl && line_len > 0)
                ui_grammar_draw_row(app, offset_y + i, offset_x + eff_ln_offset, s_tab_width, wl, line_len, line_idx);
#endif

            standend();

            /* Whitespace overlay: paint tabs as "→" and trailing spaces as "·" using a dim colour */
            if (app->cfg.show_whitespace && line_len > 0)
            {
                int k;
                int trail_start = line_len;
                int max_chars = width;
                int display_len = line_len;

                if (display_len > max_chars)
                    display_len = max_chars;

                while (trail_start > 0 && (wl[trail_start - 1] == L' ' || wl[trail_start - 1] == L'\t'))
                    trail_start--;

                attron(COLOR_PAIR(COL_BORDER));

                for (k = 0; k < display_len; k++)
                {
                    wchar_t ch = wl[k];
                    int col_x = offset_x + eff_ln_offset + wcs_vwidth_ex(wl, k, 0, s_tab_width);

                    if (ch == L'\t')
                    {
#ifdef PLATFORM_AMIGA
                        if (app->cfg.ttf_enabled)
                            mvaddnwstr(offset_y + i, col_x, s_ws_arrow, 1);
                        else
                            mvaddch(offset_y + i, col_x, '>');
#else
                        mvaddnwstr(offset_y + i, col_x, s_ws_arrow, 1);
#endif
                    }
                    else if (ch == L' ' && k >= trail_start)
                        mvaddnwstr(offset_y + i, col_x, s_ws_dot, 1);
                }

                attroff(COLOR_PAIR(COL_BORDER));
                standend();
            }

            /* Block-selection overlay (logical-span) */
            if (b_r1 >= 0 && line_idx >= b_r1 && line_idx <= b_r2)
            {
                const wchar_t *wcs = NULL;
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
                    mvaddnwstr(offset_y + i, offset_x + eff_ln_offset + wcs_vwidth_ex(wl, hs, 0, s_tab_width), &wcs[hs], he - hs);
                    attroff(A_REVERSE);
                }
                else if (hs == 0 && he == 0)
                {
                    /* Cursor at col 0 or empty line: show reversed space */
                    attron(A_REVERSE);
                    mvaddch(offset_y + i, offset_x + eff_ln_offset, ' ');
                    attroff(A_REVERSE);
                }
            }

            standend();

            /* Visual overlays (hard-wrap path) */
            line_vw = wl ? wcs_vwidth_ex(wl, line_len, 0, s_tab_width) : 0;
            x_text_end = offset_x + eff_ln_offset + line_vw;
            x_screen_end = offset_x + ln_offset + width;

            /* Highlight current line: full-width bar */
            if (app->cfg.highlight_line && line_idx == info.row)
            {
                int x;
                int x_text_start = offset_x + eff_ln_offset;

                attron(COLOR_PAIR(COL_CURRENT_LINE));

                for (x = offset_x + ln_offset; x < x_text_start; x++)
                    mvaddch(offset_y + i, x, ' ');

                for (x = x_text_end; x < x_screen_end; x++)
                    mvaddch(offset_y + i, x, ' ');

                attroff(COLOR_PAIR(COL_CURRENT_LINE));
            }

            /* Column ruler */
            if (app->cfg.ruler_col > 0)
            {
                int rx = offset_x + ln_offset + app->cfg.ruler_col;

                if (rx >= x_text_end && rx < offset_x + ln_offset + body_width)
                {
                    attron(COLOR_PAIR(COL_GUIDE));
                    mvaddch(offset_y + i, rx, '|');
                    attroff(COLOR_PAIR(COL_GUIDE));
                }
            }

            /* Indent guides */
            if (app->cfg.indent_guides && wl && line_len > 0 && s_tab_width > 1)
            {
                int k;
                int leading_count = 0;

                for (k = 0; k < line_len; k++)
                {
                    if (wl[k] != L' ')
                        break;
                    leading_count++;
                }

                if (leading_count > 0)
                {
                    int g;

                    attron(COLOR_PAIR(COL_GUIDE));

                    for (g = s_tab_width; g <= leading_count; g += s_tab_width)
                    {
                        int gx = offset_x + ln_offset + g - 1;

                        if (gx >= offset_x + ln_offset && gx < offset_x + ln_offset + width)
                            mvaddch(offset_y + i, gx, '|');
                    }

                    attroff(COLOR_PAIR(COL_GUIDE));
                }
            }
        }
    }

#ifdef HAVE_GRAMMAR
    /* Prewarm grammar cache for a small margin above/below the viewport so that single-line scrolls hit cache instead of re-checking */
    if (app->grammar_active && app->grammar_handle)
        ui_grammar_prewarm(app, info.top, body_rows, info.line_count);
#endif

    attroff(COLOR_PAIR(COL_NORMAL));
    standend();

    free(shared_classes);
}

/* Position terminal cursor on editor cursor */
static void position_cursor(TeApp *app)
{
    EdInfo info;
    TeWindow *win = NULL;
    int rich_bar_h = app->rich_mode ? 1 : 0;
    int body_rows = LINES - 2 - rich_bar_h;
    int width = COLS;
    int offset_x = 0;
    int offset_y = 0;
    int soft = !app->hard_wrap;
    int ln_offset = 0;
    int show_lnum = te_app_get_show_line_numbers(app);

    standend();

    /* Use window coordinates from layout manager */
    win = wm_get_window_by_type(app->wm, WIN_EDITOR);

    if (win && win->visible)
    {
        body_rows = win->h;
        width = win->w;
        offset_x = win->x;
        offset_y = win->y;
    }

    if (body_rows < 1)
        body_rows = 1;

    /* Clear residual attributes before positioning cursor */
    standend();

    ed_get_info(te_app_get_editor(app), &info);

    /* Calculate line number offset if enabled */
    if (show_lnum)
    {
        ln_offset = editor_body_offset(app, info.line_count);
        width = width - ln_offset;
    }

    /* In rich mode, wrap and align to the configured page width */
    width = editor_text_width(app, width);

    if (soft)
    {
        int cy, cx;
        int max_y;
        int screen_row;
        int vcol;

        screen_row = soft_cursor_screen_row(app, width);
        vcol = soft_cursor_vcol(te_app_get_editor(app), width);

        cy = offset_y + screen_row;
        cx = offset_x + ln_offset + vcol + soft_cursor_align_indent(te_app_get_editor(app), width);

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

        cx = offset_x + ln_offset + (wl ? wcs_vwidth_ex(wl, wchar_col, 0, s_tab_width) : wchar_col);

        /* Follow paragraph alignment so the cursor sits with the text */
        cx += line_align_indent(te_app_get_editor(app)->lines[info.row]->para_align, (wl && line_len > 0) ? wcs_vwidth_ex(wl, line_len, 0, s_tab_width) : 0, width);

        if (cy >= LINES - 1)
            cy = LINES - 2;

        if (cx >= COLS)
            cx = COLS - 1;

        move(cy, cx);
    }

    /* Use normal cursor visibility */
    curs_set(1);
}

static void cycle_syntax_lang(TeApp *app)
{
    static const char *names[] = {"Auto", "C", "C++", "x86 asm", "m68k asm", "Amiga C"};
    TeTab *tab = te_app_get_active_tab(app);

    if (!tab)
        return;

    tab->syntax_lang++;

    if (tab->syntax_lang >= SYNTAX_LANG_COUNT)
        tab->syntax_lang = SYNTAX_LANG_NONE;

    te_status(app, "Syntax: %s", names[tab->syntax_lang + 1]);

    draw_body(app);
    te_draw_statusbar(app);
    doupdate();
}

/* Mouse SGR sequence parser for SSH terminals */
static int parse_sgr_mouse(int *out_type, int *out_x, int *out_y)
{
    char buf[32];
    int i = 0;
    wint_t wch;
    int wrc;
    int button;
    int x;
    int y;
    char end_char;
    int parsed;

    while (i < (int)sizeof(buf) - 1)
    {
        wrc = wrapper_read_key(&wch);

        if (wrc == ERR)
            return 0;

        if (wrc == KEY_CODE_YES)
            return 0;

        buf[i] = (char)wch;
        i++;

        if (wch == 'M' || wch == 'm')
            break;
    }

    buf[i] = '\0';

    parsed = sscanf(buf, "%d;%d;%d%c", &button, &x, &y, &end_char);

    if (parsed != 4)
    {
        parsed = sscanf(buf, "%d%c", &button, &end_char);

        if (parsed == 2)
        {
            x = -1;
            y = -1;
        }
        else
        {
            return 0;
        }
    }

    if (end_char == 'M')
    {
        switch (button)
        {
        case 0:
            *out_type = UI_MOUSE_PRESS_LEFT;
            break;
        case 1:
            *out_type = UI_MOUSE_PRESS_LEFT;
            break;
        case 2:
            *out_type = UI_MOUSE_PRESS_LEFT;
            break;
        case 32:
            *out_type = UI_MOUSE_DRAG_LEFT;
            break;
        case 33:
            *out_type = UI_MOUSE_DRAG_LEFT;
            break;
        case 34:
            *out_type = UI_MOUSE_DRAG_LEFT;
            break;
        case 64:
            *out_type = UI_MOUSE_WHEEL_UP;
            break;
        case 65:
            *out_type = UI_MOUSE_WHEEL_DOWN;
            break;
        default:
            return 0;
        }
    }
    else
    {
        *out_type = UI_MOUSE_RELEASE_LEFT;
    }

    if (x > 0)
        *out_x = x - 1;
    else
        *out_x = -1;

    if (y > 0)
        *out_y = y - 1;
    else
        *out_y = -1;

    return 1;
}

/* Paste helpers */
static char *collect_bracketed_paste(void)
{
    wchar_t *wbuf = NULL;
    int wlen = 0, wcap = 0;
    char *out = NULL;

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
    char *out = NULL;
    const int MAX_CHARS = 10; /* 10+ chars = paste, not typing */
    wint_t next_wch;
    wint_t third_wch;
    int next_wrc;
    int third_wrc;

    /* Check for more characters (rapid paste detection) */
    nodelay(stdscr, TRUE);

    next_wrc = get_wch(&next_wch);

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
    third_wrc = get_wch(&third_wch);

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
    int r = 0;
    char dir_input[1024];
    char name_input[1024];
    char filename_buf[TAB_FILENAME_MAX];
    const char *last_slash = NULL;

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

    /* RTF filenames save through the RTF exporter, keeping the styles */
    if (ui_files_is_rtf(te_app_get_filename(app)))
    {
        FILE *fp = fopen(te_app_get_filename(app), "wb");
        int rc = -1;

        if (fp)
        {
            rc = rtf_export(te_app_get_editor(app), fp);
            fclose(fp);
        }

        if (rc != 0)
        {
            te_status(app, "Cannot write: %s", te_app_get_filename(app));
            return -1;
        }

        app->rich_mode = 1;

        if (te_app_get_active_tab(app))
            te_app_get_active_tab(app)->rich_mode = 1;
    }
    else if (ui_files_is_wp4(te_app_get_filename(app)))
    {
        FILE *fp = fopen(te_app_get_filename(app), "wb");
        char werr[128];
        char wwarn[128];
        int rc = -1;

        werr[0] = '\0';
        wwarn[0] = '\0';

        if (fp)
        {
            rc = wp4_export(te_app_get_editor(app), fp, app->charset_out, werr, sizeof(werr), wwarn, sizeof(wwarn));
            fclose(fp);
        }

        if (rc != 0)
        {
            te_status(app, "WP error: %s", werr[0] ? werr : "cannot write");
            return -1;
        }

        app->rich_mode = 1;

        if (te_app_get_active_tab(app))
            te_app_get_active_tab(app)->rich_mode = 1;

        if (wwarn[0])
            te_status(app, "Saved: %s (%s)", te_app_get_filename(app), wwarn);
    }
    else if (ed_save_to_file(te_app_get_editor(app), te_app_get_filename(app), app->charset_out) != 0)
    {
        te_status(app, "Cannot write: %s", te_app_get_filename(app));
        return -1;
    }

    /* Clear modified flag */
    ed_set_modified(te_app_get_editor(app), 0);

    /* Swap is no longer needed after a successful save */
    ui_editor_swp_remove(te_app_get_filename(app));

    ui_editor_recent_add(te_app_get_filename(app));

    te_status(app, "Saved: %s", te_app_get_filename(app));

#ifdef HAVE_HUNSPELL
    /* If the user edited the custom dictionary file directly, reload it so new words are recognized immediately */
    if (app->cfg.spell_custom_dict[0] && strcmp(te_app_get_filename(app), app->cfg.spell_custom_dict) == 0)
        spell_load_from_config(app);
#endif

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
            char old_charset_in[TE_CFG_STR_MAX];
            char old_charset_out[TE_CFG_STR_MAX];
            TeTab *tab = NULL;

            strncpy(old_charset_in, app->cfg.charset_in, sizeof(old_charset_in) - 1);
            old_charset_in[sizeof(old_charset_in) - 1] = '\0';

            strncpy(old_charset_out, app->cfg.charset_out, sizeof(old_charset_out) - 1);
            old_charset_out[sizeof(old_charset_out) - 1] = '\0';

            if (ui_setup_run(app, &app->cfg, app->cfg_path) == 1)
            {
                /* Config saved: apply changes */
                if (app->cfg.undo_levels > 0)
                    ed_set_undo_levels(te_app_get_editor(app), app->cfg.undo_levels);

                app->hard_wrap = app->cfg.hard_wrap;
                ed_set_hard_wrap(te_app_get_editor(app), app->cfg.hard_wrap);

                app->wrap_col = app->cfg.autowrap_col;
                te_app_set_show_line_numbers(app, app->cfg.show_line_numbers);

                /* If charset_in changed in setup, update charset_in */
                if (strcasecmp(old_charset_in, app->cfg.charset_in) != 0)
                {
                    strncpy(app->charset_in, app->cfg.charset_in, sizeof(app->charset_in) - 1);
                    app->charset_in[sizeof(app->charset_in) - 1] = '\0';
                }

                /* If charset_out changed in setup, update charset_out */
                if (strcasecmp(old_charset_out, app->cfg.charset_out) != 0)
                {
                    strncpy(app->charset_out, app->cfg.charset_out, sizeof(app->charset_out) - 1);
                    app->charset_out[sizeof(app->charset_out) - 1] = '\0';
                }

                /* Update active tab charset values for status bar display */
                tab = te_app_get_active_tab(app);

                if (tab)
                {
                    strncpy(tab->charset_in, app->charset_in, sizeof(tab->charset_in) - 1);
                    tab->charset_in[sizeof(tab->charset_in) - 1] = '\0';

                    strncpy(tab->charset_out, app->charset_out, sizeof(tab->charset_out) - 1);
                    tab->charset_out[sizeof(tab->charset_out) - 1] = '\0';
                }

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
                /* Reconfigure mouse if setting changed */
                if (app->cfg.mouse_enabled)
                {
                    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
                    mouseinterval(0);
                    printf("\033[?1002h");
                    printf("\033[?1006h");
                    fflush(stdout);
                }
                else
                {
                    mousemask(0, NULL);
                    printf("\033[?1002l");
                    printf("\033[?1006l");
                    fflush(stdout);
                }
#endif

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

    /* F6 / Alt+B : Replace All in search mode, or swap translate from/to */
    if ((is_key && ch == KEY_F(6)) || (ch == KEY_ALT('B')))
    {
        if (app->search.is_mode && app->search.count > 0)
            return replace_all(app);
#ifdef HAVE_TRANSLATE
        else
        {
            char tmp[16];

            snprintf(tmp, sizeof(tmp), "%s", app->cfg.translate_from_lang);

            snprintf(app->cfg.translate_from_lang, sizeof(app->cfg.translate_from_lang), "%s", app->cfg.translate_to_lang);

            snprintf(app->cfg.translate_to_lang, sizeof(app->cfg.translate_to_lang), "%s", tmp);

            te_status(app, "Swapped: %s <-> %s", app->cfg.translate_from_lang, app->cfg.translate_to_lang);
            return 1;
        }
#endif
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
            ed_block_clear(te_app_get_editor(app));
            editor_insert_cp(te_app_get_editor(app), (unsigned long)cp);

            clear_search_highlights(app);
        }

        return 1;
    }

#ifdef HAVE_TRANSLATE
    /* Alt+R : translate selected text */
    if (ch == KEY_ALT('R'))
    {
        ui_translate_action(app);
        return 1;
    }

#ifdef HAVE_TRANSLATE_STARDICT
    if (ch == KEY_ALT('M'))
    {
        ui_dict_picker(app);
        return 1;
    }

    /* Alt+N: reverse dictionary lookup */
    if (ch == KEY_ALT('N'))
    {
        if (app->spell_panel_mode == 2)
        {
            ui_dict_reverse(app);
            return 1;
        }
        else
        {
            return do_search_in_files(app);
        }
    }

#endif
#endif

    /* Alt+V : sort selected lines alphabetically (case-insensitive) */
    if (ch == KEY_ALT('V'))
    {
        Ed *ed = te_app_get_editor(app);

        if (!ed || !ed->block.active)
        {
            te_status(app, "Sort: no block selected (use Shift+arrows)");
            return 1;
        }

        if (ed_sort_block_lines(ed) == 0)
        {
            te_status(app, "Lines sorted");
            ed_block_clear(ed);
        }
        else
        {
            te_status(app, "Sort: needs 2+ lines in the block");
        }

        return 1;
    }

    /* Alt+X : convert case (popup: U=UPPER / L=lower / T=Title) */
    if (ch == KEY_ALT('X'))
    {
        Ed *ed = te_app_get_editor(app);
        const char *items[3];
        int choice;

        if (!ed || !ed->block.active)
        {
            te_status(app, "Convert case: no block selected");
            return 1;
        }

        items[0] = "UPPER CASE";
        items[1] = "lower case";
        items[2] = "Title Case";

        choice = ui_popup_list("Convert case", items, 3, 0);

        if (choice < 0 || choice > 2)
            return 1;

        if (ed_convert_block_case(ed, choice) == 0)
            te_status(app, "Case converted");
        else
            te_status(app, "Convert case: error");

        return 1;
    }

    /* Ctrl+Alt+Y : open recent files */
    if (is_key && ch == KEY_ALT_CTRL('Y'))
    {
        ui_editor_recent_open(app);

        return 1;
    }

#ifdef PLATFORM_AMIGA
    /* Alt+Shift+S : cycle syntax language (Ctrl+Alt+S is used by AmigaOS) */
    if (is_key && ch == KEY_SHIFT('S'))
#else
    /* Ctrl+Alt+S : cycle syntax language */
    if (is_key && ch == KEY_ALT_CTRL('S'))
#endif
    {
        cycle_syntax_lang(app);
        return 1;
    }

    /* Rich-text: bold/italic/underline/alignment -- only in rich_mode */
    if (app->rich_mode)
    {
#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('B'))
#else
        if (is_key && ch == KEY_ALT_CTRL('B'))
#endif
        {
            ui_rich_attr_toggle(app, EA_BOLD);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('I'))
#else
        if (is_key && ch == KEY_ALT_CTRL('I'))
#endif
        {
            ui_rich_attr_toggle(app, EA_ITALIC);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('U'))
#else
        if (is_key && ch == KEY_ALT_CTRL('U'))
#endif
        {
            ui_rich_attr_toggle(app, EA_UNDERLINE);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('L'))
#else
        if (is_key && ch == KEY_ALT_CTRL('L'))
#endif
        {
            ui_rich_align_set(app, EA_ALIGN_LEFT);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('E'))
#else
        if (is_key && ch == KEY_ALT_CTRL('E'))
#endif
        {
            ui_rich_align_set(app, EA_ALIGN_CENTER);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('R'))
#else
        if (is_key && ch == KEY_ALT_CTRL('R'))
#endif
        {
            ui_rich_align_set(app, EA_ALIGN_RIGHT);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('J'))
#else
        if (is_key && ch == KEY_ALT_CTRL('J'))
#endif
        {
            ui_rich_align_set(app, EA_ALIGN_JUST);
            return 1;
        }
    }

#ifdef HAVE_TTS
    /* TTS shortcuts: Alt+Shift+X (Amiga) / Ctrl+Alt+X (rest) speak, Ctrl+Alt+P/O/V pause/stop/popup */
#ifdef PLATFORM_AMIGA
    if (is_key && ch == KEY_SHIFT('X'))
#else
    if (is_key && ch == KEY_ALT_CTRL('X'))
#endif
    {
        ui_tts_speak_action(app);
        return 1;
    }

    /* Whole-document dictation from line 0, Amiga: Alt+Shift+K, else: Ctrl+Alt+K */
#ifdef PLATFORM_AMIGA
    if (is_key && ch == KEY_SHIFT('K'))
#else
    if (is_key && ch == KEY_ALT_CTRL('K'))
#endif
    {
        ui_tts_speak_doc_action(app);
        return 1;
    }

#ifdef PLATFORM_AMIGA
    if (is_key && ch == KEY_SHIFT('P'))
#else
    if (is_key && ch == KEY_ALT_CTRL('P'))
#endif
    {
        ui_tts_pause_toggle(app);
        return 1;
    }

#ifdef PLATFORM_AMIGA
    if (is_key && ch == KEY_SHIFT('O'))
#else
    if (is_key && ch == KEY_ALT_CTRL('O'))
#endif
    {
        ui_tts_stop(app);
        return 1;
    }

#ifdef PLATFORM_AMIGA
    if (is_key && ch == KEY_SHIFT('V'))
#else
    if (is_key && ch == KEY_ALT_CTRL('V'))
#endif
    {
        ui_tts_popup(app);
        return 1;
    }
#endif /* HAVE_TTS */

    return 0;
}

static int inside_block_comment(Ed *ed, int row, int col)
{
    int r;
    int needed = 1;

    int r_min = row - INSIDE_BLOCK_COMMENT_LOOKUP_LIMIT;

    if (r_min < 0)
        r_min = 0;

    for (r = row; r >= r_min; r--)
    {
        const wchar_t *rl = ed_line_wcs(ed, r);
        int rl_len = ed_line_len(ed, r);
        int c;
        int start;

        if (!rl)
            continue;

        start = (r == row) ? col - 1 : rl_len - 1;

        for (c = start; c >= 0; c--)
        {
            if (c + 1 >= rl_len)
                continue;

            if (rl[c] == L'*' && rl[c + 1] == L'/')
                needed++;
            else if (rl[c] == L'/' && rl[c + 1] == L'*')
            {
                /* The current row's opener must be fully before the cursor */
                if (r == row && c + 1 >= col)
                    continue;

                needed--;

                if (needed <= 0)
                    return 1;
            }
        }
    }

    return 0;
}

static int insert_block_comment_prefix(Ed *ed, const wchar_t *prev_line, int prev_llen, int prev_col, int skip_leading_ws)
{
    int leading_ws = 0;
    int k;
    int line_starts_with_opener = 0;
    int line_has_star_prefix = 0;
    int line_has_closer = 0;

    if (!prev_line || prev_llen <= 0)
        return 0;

    for (k = 0; k < prev_llen; k++)
    {
        if (prev_line[k] == L' ' || prev_line[k] == L'\t')
            leading_ws++;
        else
            break;
    }

    if (leading_ws + 1 < prev_llen && prev_line[leading_ws] == L'/' && prev_line[leading_ws + 1] == L'*')
        line_starts_with_opener = 1;

    if (leading_ws < prev_llen && prev_line[leading_ws] == L'*')
        line_has_star_prefix = 1;

    if (prev_llen >= 2 && prev_line[prev_llen - 2] == L'*' && prev_line[prev_llen - 1] == L'/')
        line_has_closer = 1;

    if (line_has_closer)
        return 0;

    if (!line_starts_with_opener && !line_has_star_prefix)
        return 0;

    if (!skip_leading_ws)
    {
        for (k = 0; k < leading_ws; k++)
            ed_insert_char(ed, L' ');
    }

    if (line_starts_with_opener)
        ed_insert_char(ed, L' ');

    ed_insert_char(ed, L'*');
    ed_insert_char(ed, L' ');

    return 1;
}

/* Insert a new line, optionally copying the leading whitespace from the current line when smart-indent is enabled */
static void do_smart_enter(TeApp *app)
{
    Ed *ed = te_app_get_editor(app);
    EdInfo si_info;
    const wchar_t *si_line;
    int si_llen;
    wchar_t indent[64];
    int indent_n = 0;
    int k;
    int k2;

    ed_get_info(ed, &si_info);
    si_line = ed_line_wcs(ed, si_info.row);
    si_llen = ed_line_len(ed, si_info.row);

    if (app->cfg.smart_indent)
    {
        if (si_line)
        {
            for (k = 0; k < si_llen && k < (int)(sizeof(indent) / sizeof(wchar_t)) - 1; k++)
            {
                if (si_line[k] == L' ' || si_line[k] == L'\t')
                    indent[indent_n++] = si_line[k];
                else
                    break;
            }
        }

        ed_enter(ed);

        for (k2 = 0; k2 < indent_n; k2++)
            ed_insert_char(ed, indent[k2]);
    }
    else
    {
        ed_enter(ed);
    }

    /* Continue C-style block comments */
    if (inside_block_comment(ed, si_info.row, si_info.col))
        insert_block_comment_prefix(ed, si_line, si_llen, si_info.col, app->cfg.smart_indent);
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

#ifdef HAVE_TRANSLATE
    /* Ctrl+T : toggle translator */
    if (!is_key && ch == CTRL('T'))
    {
        if (!app->translate_handle && app->cfg.translate_enabled)
            ui_translate_load_from_config(app);

        if (app->translate_handle)
        {
            app->translate_active = !app->translate_active;
            te_status(app, "Translator %s", app->translate_active ? "enabled" : "disabled");
        }
        else if (!app->cfg.translate_enabled)
        {
            te_status(app, "Translator disabled in config (enable in Setup F2)");
        }
        else
        {
            te_status(app, "Cannot load translator");
        }

        return 1;
    }
#endif

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
        if (!app->hyph_handle)
        {
            te_status(app, "No hyphenation dictionary loaded (configure HYPH_DICT_*)");
            return 1;
        }

        if (app->hard_wrap)
        {
            app->hyph_wrap_enabled = !app->hyph_wrap_enabled;
            te_status(app, "Hyphen wrap %s", app->hyph_wrap_enabled ? "ON" : "OFF");

            ed_auto_rewrap_after_edit(app);
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
        TeTab *tab = NULL;
        EdInfo info;
        char closed_filename[TAB_FILENAME_MAX];

        tab = app->tabs[app->active_tab];
        closed_filename[0] = '\0';

        if (tab)
        {
            ed_get_info(tab->editor, &info);

            if (info.modified)
            {
                if (ui_popup_confirm("Close tab", "Tab has unsaved changes. Close anyway?") != 1)
                    return 1;
            }

            strncpy(closed_filename, tab->filename, sizeof(closed_filename) - 1);
            closed_filename[sizeof(closed_filename) - 1] = '\0';
        }

        te_app_close_tab(app, app->active_tab);

        if (closed_filename[0])
            ui_editor_swp_remove(closed_filename);

        /* If no tabs left, create a new empty tab */
        if (app->tab_count == 0)
        {
            TeTab *new_tab = te_tab_new();

            if (new_tab)
            {
                ed_set_word_move_mode(new_tab->editor, app->cfg.word_move_mode);
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
        TeTab *new_tab = NULL;
        int result;

        new_tab = te_tab_new();

        if (!new_tab)
            return 1;

        ed_set_word_move_mode(new_tab->editor, app->cfg.word_move_mode);
        te_app_add_tab(app, new_tab);
        te_app_switch_tab(app, app->tab_count - 1);

        result = ui_files_open(app);

        if (result != 0)
        {
            /* User cancelled or error - close the tab we just created */
            te_app_close_tab(app, app->tab_count - 1);
            return result;
        }

        /* Re-apply configuration after loading file */
        ed_set_undo_levels(te_app_get_editor(app), app->cfg.undo_levels);
        ed_set_hard_wrap(te_app_get_editor(app), app->cfg.hard_wrap);
        ed_set_word_move_mode(te_app_get_editor(app), app->cfg.word_move_mode);

        return result;
    }

    /* Ctrl+N : new file (create new tab) */
    if (!is_key && ch == CTRL('N'))
    {
        TeTab *new_tab = NULL;

        new_tab = te_tab_new();

        if (!new_tab)
            return 1;

        ed_set_word_move_mode(new_tab->editor, app->cfg.word_move_mode);
        te_app_add_tab(app, new_tab);
        te_app_switch_tab(app, app->tab_count - 1);

        te_status(app, "[No Name]");

        return 1;
    }

    /* Ctrl+W : rewrap FTN reply quote block */
    if (!is_key && ch == CTRL('W'))
        return ftn_reply(app);

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
    case KEY_ALT_UP:
        if (ed_move_line_up(te_app_get_editor(app)) == 0)
            te_status(app, "Line moved up");
        else
            te_status(app, "Cannot move line up");
        return 1;

    case KEY_ALT_DOWN:
        if (ed_move_line_down(te_app_get_editor(app)) == 0)
            te_status(app, "Line moved down");
        else
            te_status(app, "Cannot move line down");
        return 1;

    case KEY_UP:
        ed_block_clear(te_app_get_editor(app));

        if (soft)
        {
            soft_move_up_visual(app, width);
            *preserve_desired = 1;
        }
        else
            ed_move_up(te_app_get_editor(app));

        return 1;

    case KEY_DOWN:
        ed_block_clear(te_app_get_editor(app));

        if (soft)
        {
            soft_move_down_visual(app, width);
            *preserve_desired = 1;
        }
        else
            ed_move_down(te_app_get_editor(app));

        return 1;

    case KEY_LEFT:
        ed_block_clear(te_app_get_editor(app));
        ed_move_left(te_app_get_editor(app));
        return 1;

    case KEY_RIGHT:
        ed_block_clear(te_app_get_editor(app));
        ed_move_right(te_app_get_editor(app));
        return 1;

    case KEY_HOME:
        ed_block_clear(te_app_get_editor(app));

        if (soft)
            soft_move_home_visual(app, width);
        else
            ed_move_home(te_app_get_editor(app));

        return 1;

    case KEY_END:
        ed_block_clear(te_app_get_editor(app));

        if (soft)
            soft_move_end_visual(app, width);
        else
            ed_move_end(te_app_get_editor(app));

        return 1;

    case KEY_PPAGE:
#ifdef HAVE_TRANSLATE_STARDICT
        if (app->spell_panel_mode == 2 && app->dict_result && app->dict_result[0])
        {
            int i;
            int rows = DICT_PANEL_ROWS;

            for (i = 0; i < rows; i++)
            {
                if (!ui_dict_scroll_up(app))
                    break;
            }

            return 1;
        }
#endif
        ed_block_clear(te_app_get_editor(app));

        if (soft)
        {
            soft_move_pgup_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgup(te_app_get_editor(app), 0);

        return 1;

    case KEY_NPAGE:
#ifdef HAVE_TRANSLATE_STARDICT
        if (app->spell_panel_mode == 2 && app->dict_result && app->dict_result[0])
        {
            int i;
            int rows = DICT_PANEL_ROWS;

            for (i = 0; i < rows; i++)
            {
                if (!ui_dict_scroll_down(app))
                    break;
            }

            return 1;
        }
#endif
        ed_block_clear(te_app_get_editor(app));

        if (soft)
        {
            soft_move_pgdn_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgdn(te_app_get_editor(app), 0);

        return 1;

    case KEY_ENTER:
        ed_block_clear(te_app_get_editor(app));
        do_smart_enter(app);
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
            ed_auto_rewrap_after_edit(app);
        }
        else
        {
            ed_block_clear(te_app_get_editor(app));

            ed_backspace(te_app_get_editor(app));
            ed_auto_rewrap_after_edit(app);
        }

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
            ed_auto_rewrap_after_edit(app);
        }
        else
        {
            ed_block_clear(te_app_get_editor(app));

            ed_delete(te_app_get_editor(app));
            ed_auto_rewrap_after_edit(app);
        }

        clear_search_highlights(app);

        return 1;
    }

    case KEY_IC: /* Ins */
    case KEY_ALT('I'):
        ed_toggle_insert(te_app_get_editor(app));
        return 1;

    case KEY_CLEFT:
        ed_block_clear(te_app_get_editor(app));
        ed_word_left(te_app_get_editor(app));
        return 1;

    case KEY_CRIGHT:
        ed_block_clear(te_app_get_editor(app));
        ed_word_right(te_app_get_editor(app));
        return 1;

    case KEY_CUP:
        ed_move_up(te_app_get_editor(app));
        return 1;

    case KEY_CDOWN:
        ed_move_down(te_app_get_editor(app));
        return 1;

    case KEY_CSLEFT:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_word_left(te_app_get_editor(app));
        return 1;

    case KEY_CSRIGHT:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_word_right(te_app_get_editor(app));
        return 1;

    case KEY_CSUP:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_up(te_app_get_editor(app));
        return 1;

    case KEY_CSDOWN:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_down(te_app_get_editor(app));
        return 1;

    case KEY_SPPAGE:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_pgup(te_app_get_editor(app), 0);
        return 1;

    case KEY_SNPAGE:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_pgdn(te_app_get_editor(app), 0);
        return 1;

    case KEY_CSUPD:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_pgdn(te_app_get_editor(app), 0);
        return 1;

    case KEY_CSDOWNU:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_pgup(te_app_get_editor(app), 0);
        return 1;

    case KEY_SLEFT:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_left(te_app_get_editor(app));
        return 1;

    case KEY_SRIGHT:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_right(te_app_get_editor(app));
        return 1;

    case KEY_SUP:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_up(te_app_get_editor(app));
        return 1;

    case KEY_SDOWN:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_down(te_app_get_editor(app));
        return 1;

    case KEY_SHOME:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_home(te_app_get_editor(app));
        return 1;

    case KEY_SEND:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_end(te_app_get_editor(app));
        return 1;

    case KEY_CSHOME:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_top(te_app_get_editor(app));
        return 1;

    case KEY_CSEND:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_bottom(te_app_get_editor(app));
        return 1;

    case KEY_ALT('Z'):
        ed_redo(te_app_get_editor(app));
        app->hard_wrap = ed_get_hard_wrap(te_app_get_editor(app));
        clear_search_highlights(app);

        return 1;

    case KEY_ALT('D'):
        /* If dictionary panel is visible, Alt+D hides it; otherwise toggle line numbers */
        if (app->spell_panel_mode == 2)
        {
            toggle_spell_panel(app);
            te_status(app, "Dictionary panel hidden");
            return 1;
        }

        te_app_set_show_line_numbers(app, !te_app_get_show_line_numbers(app));
        te_status(app, "Line numbers: %s", te_app_get_show_line_numbers(app) ? "ON" : "OFF");
        return 1;

    case KEY_ALT('Q'):
    {
        app->hard_wrap = !app->hard_wrap;
        ed_set_hard_wrap(te_app_get_editor(app), app->hard_wrap);

        /* Ensure cursor stays within visible area when dict panel is active */
#ifdef HAVE_TRANSLATE_STARDICT
        if (app->spell_panel_mode == 2)
            ed_ensure_visible(te_app_get_editor(app));
#endif

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
        ed_block_clear(te_app_get_editor(app));
        do_smart_enter(app);
        clear_search_highlights(app);
        return 1;

    case 8:
    case 127:
        ed_block_clear(te_app_get_editor(app));
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
#ifdef HAVE_TRANSLATE_STARDICT
        if (app->spell_panel_mode == 2 && app->dict_result && app->dict_result[0])
        {
            int i;
            int rows = DICT_PANEL_ROWS;

            for (i = 0; i < rows; i++)
            {
                if (!ui_dict_scroll_up(app))
                    break;
            }

            return 1;
        }
#endif
        if (soft)
        {
            soft_move_pgup_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgup(te_app_get_editor(app), 0);

        return 1;

    case CTRL('D'):
#ifdef HAVE_TRANSLATE_STARDICT
        if (app->spell_panel_mode == 2 && app->dict_result && app->dict_result[0])
        {
            int i;
            int rows = DICT_PANEL_ROWS;

            for (i = 0; i < rows; i++)
            {
                if (!ui_dict_scroll_down(app))
                    break;
            }

            return 1;
        }
#endif
        if (soft)
        {
            soft_move_pgdn_visual(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgdn(te_app_get_editor(app), 0);

        return 1;

    case CTRL('Y'):
        ed_block_clear(te_app_get_editor(app));
        ed_delete_line(te_app_get_editor(app));
        clear_search_highlights(app);
        return 1;

    case CTRL('Z'):
        ed_undo(te_app_get_editor(app));
        app->hard_wrap = ed_get_hard_wrap(te_app_get_editor(app));
        clear_search_highlights(app);
        return 1;

#ifdef HAVE_TRANSLATE
    case CTRL('T'):
        if (!app->translate_handle && app->cfg.translate_enabled)
            ui_translate_load_from_config(app);

        if (app->translate_handle)
        {
            app->translate_active = !app->translate_active;
            te_status(app, "Translator %s", app->translate_active ? "enabled" : "disabled");
        }
        else if (!app->cfg.translate_enabled)
        {
            te_status(app, "Translator disabled in config (enable in Setup)");
        }
        else
        {
            te_status(app, "Cannot load translator");
        }
        return 1;
#endif

    case '\t':
        ed_insert_tab(te_app_get_editor(app), 4);

        clear_search_highlights(app);
        ed_auto_rewrap_after_edit(app);
        return 1;

    default:
        if (wch >= 0x20 && wch != 127)
        {
            EdInfo ac_info;
            const wchar_t *ac_line;
            wchar_t next = L'\0';
            wchar_t close = L'\0';
            int ac_llen;

            ed_block_clear(te_app_get_editor(app));

            ed_insert_char(te_app_get_editor(app), (wchar_t)wch);
            clear_search_highlights(app);

            /* Auto-close open brackets and Spanish opening marks */
            if (app->cfg.autoclose && (wch == L'(' || wch == L'[' || wch == L'{' || wch == L'"' || wch == L'\'' || wch == L'¿' || wch == L'¡'))
            {
                ed_get_info(te_app_get_editor(app), &ac_info);

                ac_line = ed_line_wcs(te_app_get_editor(app), ac_info.row);
                ac_llen = ed_line_len(te_app_get_editor(app), ac_info.row);

                if (ac_line && ac_info.col < ac_llen)
                    next = ac_line[ac_info.col];

                if (wch == L'(')
                    close = L')';
                else if (wch == L'[')
                    close = L']';
                else if (wch == L'{')
                    close = L'}';
                else if (wch == L'"')
                    close = L'"';
                else if (wch == L'\'')
                    close = L'\'';
                else if (wch == L'¿')
                    close = L'?';
                else if (wch == L'¡')
                    close = L'!';

                if (close &&
                    (next == L'\0' || next == L' ' || next == L'\t' ||
                     next == L')' || next == L']' || next == L'}' ||
                     next == L'"' || next == L'\'' || next == L',' ||
                     next == L';' || next == L'.' || next == L':' ||
                     next == L'!' || next == L'?'))
                {
                    ed_insert_char(te_app_get_editor(app), close);

                    /* Step cursor back so it sits between the pair */
                    ed_set_pos(te_app_get_editor(app), ac_info.row, ac_info.col);
                }
            }

            /* Auto-close a block comment opener with its matching closer */
            if (app->cfg.autoclose && wch == L'*')
            {
                wchar_t slash = L'\0';

                ed_get_info(te_app_get_editor(app), &ac_info);

                ac_line = ed_line_wcs(te_app_get_editor(app), ac_info.row);
                ac_llen = ed_line_len(te_app_get_editor(app), ac_info.row);

                if (ac_line && ac_info.col >= 2 && ac_info.col - 2 < ac_llen)
                    slash = ac_line[ac_info.col - 2];

                next = L'\0';

                if (ac_line && ac_info.col < ac_llen)
                    next = ac_line[ac_info.col];

                if (slash == L'/' && next != L'/')
                {
                    ed_insert_char(te_app_get_editor(app), L'*');
                    ed_insert_char(te_app_get_editor(app), L'/');

                    ed_set_pos(te_app_get_editor(app), ac_info.row, ac_info.col);
                }
            }

            /* Editor assists: smart quotes, auto-cap. Independent of wrap mode they only touch the buffer */
            ui_assist_on_char(app, (wchar_t)wch);

            /* Hard-wrap: reflow paragraph when a word separator is inserted */
            if (wch == L' ' || wch == L'\t')
                ed_auto_rewrap_after_edit(app);

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
    wm_recalc_layout_left(app->wm, COLS, LINES, app->show_tabs, app->spell_panel_mode, app->rich_mode);

    te_draw_titlebar(app);

    te_draw_richbar(app);

    ui_tabs_draw_panel(app);

    draw_body(app);

#ifdef HAVE_HUNSPELL
    ui_spell_draw_panel(app);
#endif

#ifdef HAVE_TRANSLATE_STARDICT
    ui_dict_draw_panel(app);
#endif

    te_draw_statusbar(app);

    position_cursor(app);

    refresh();
}

/* Auto-save buffer to .swp companion file using streaming */
static void do_autosave_to_swp(TeApp *app)
{
    TeTab *tab = NULL;
    const char *fn = NULL;
    char swp_path[1024];
    char tmp_path[1024];
    size_t swp_len;

    if (!app)
        return;

    tab = te_app_get_active_tab(app);

    if (!tab || !tab->editor)
        return;

    fn = tab->filename;

    /* Untitled buffers: skip if filename is empty (no place to put it) */
    if (!fn || !fn[0])
        return;

    pf_swap_path(fn, swp_path, sizeof(swp_path));

    if (!swp_path[0])
        return;

    swp_len = strlen(swp_path);

    if (swp_len + 5 >= sizeof(tmp_path))
    {
        te_status(app, "Auto-save failed: %s", swp_path);
        return;
    }

    memcpy(tmp_path, swp_path, swp_len);
    memcpy(tmp_path + swp_len, ".tmp", 5); /* includes NUL */

    /* Stream editor content to temp file, then atomically rename to .swp */
    if (ed_save_to_file(tab->editor, tmp_path, NULL) != 0)
    {
        te_status(app, "Auto-save failed: %s", swp_path);
        return;
    }

    if (pf_atomic_rename(tmp_path, swp_path) != 0)
    {
        pf_remove_file(tmp_path);
        te_status(app, "Auto-save failed: %s", swp_path);
    }
}

void ui_editor_run(TeApp *app)
{
    int body_rows;
    int soft, width;
    int screen_dirty;

    /* Last autosave attempt timestamp */
    static unsigned long s_last_autosave_ms = 0;

    if (!app)
        return;

    if (!te_app_get_editor(app))
        return;

    /* Reset soft-wrap state */
    s_soft_top_line = 0;
    s_soft_top_sub = 0;
    s_soft_desired_vcol = -1;
    s_soft_last_width = COLS;
    s_tab_width = app->cfg.tab_width > 0 ? app->cfg.tab_width : 4;

    ed_set_tab_width(s_tab_width);

    TE_BRACKET_PASTE_ON();

    /* Non-blocking input with 1s timeout for autosave */
    if (app->cfg.autosave)
        timeout(1000);
    else
        timeout(-1); /* fully blocking */

    screen_dirty = 1; /* Force initial redraw */

    for (;;)
    {
        wint_t wch;
        int wrc, ch, is_key, preserve_desired;
        TeWindow *win = NULL;

        /* Get editor window for dimensions */
        win = wm_get_window_by_type(app->wm, WIN_EDITOR);

        if (!win || !win->visible)
        {
            width = COLS;
            body_rows = LINES - 2 - (app->rich_mode ? 1 : 0);
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
                width = win->w - editor_body_offset(app, info.line_count);
            else
                width = COLS - editor_body_offset(app, info.line_count);
        }

        /* In rich mode, wrap and align to the configured page width */
        width = editor_text_width(app, width);

        if (body_rows < 1)
            body_rows = 1;

#ifdef HAVE_TTS
        /* Poll input with short timeout while speech plays, Amiga uses nodelay polling plus sleep */
#ifdef PLATFORM_AMIGA
        if (ui_tts_is_busy(app))
            nodelay(stdscr, TRUE);
        else
            nodelay(stdscr, FALSE);
#else
        if (ui_tts_is_busy(app))
            timeout(150);
        else if (app->cfg.autosave)
            timeout(1000);
        else
            timeout(-1);
#endif
#endif

        wrc = wrapper_read_key(&wch);

#if defined(HAVE_TTS) && defined(PLATFORM_AMIGA)
        /* Idle tick while speaking: don't spin the CPU */
        if (wrc == ERR && ui_tts_is_busy(app))
        {
            if (ui_tts_tick(app))
                te_draw_statusbar(app);

#ifdef PLATFORM_AMIGA
            pf_sleep_ms(60);
#endif
            continue;
        }
#endif

        /* Run autosave on timeout/error */
        if (app->cfg.autosave && app->cfg.autosave_interval > 0)
        {
            unsigned long now = pf_now_ms();

            if (s_last_autosave_ms == 0)
                s_last_autosave_ms = now;

            if (now - s_last_autosave_ms >=
                (unsigned long)app->cfg.autosave_interval * 1000UL)
            {
                do_autosave_to_swp(app);
                s_last_autosave_ms = now;
            }
        }

#ifdef HAVE_TTS
        /* Advance TTS state machine and redraw status bar on state change */
        if (ui_tts_tick(app))
            te_draw_statusbar(app);
#endif

        if (wrc == ERR)
            continue;

        ch = (int)wch;

        is_key = (wrc == KEY_CODE_YES);

        if (!is_key && (ch == KEY_UP || ch == KEY_DOWN || ch == KEY_LEFT || ch == KEY_RIGHT ||
                        ch == KEY_HOME || ch == KEY_END || ch == KEY_PPAGE || ch == KEY_NPAGE ||
                        ch == KEY_BACKSPACE || ch == KEY_DC || ch == KEY_ENTER))
            is_key = 1;

        if (is_key && ch == KEY_MOUSE_SGR)
        {
            int mtype;
            int mx;
            int my;

            if (parse_sgr_mouse(&mtype, &mx, &my))
            {
                win = wm_get_window_by_type(app->wm, WIN_EDITOR);

                if (win && win->visible)
                {
                    my -= win->y;
                    mx -= win->x;
                }

                if (ui_mouse_dispatch(app, mtype, my, mx))
                    screen_dirty = 1;
            }

            continue;
        }

        if (is_key && ch == KEY_MOUSE)
        {
#ifdef PLATFORM_AMIGA
            unsigned long m = getmouse();
            int my;
            int mx;
            int mtype;

            /* Decode: low 8 bits = type, next 12 = x, next 12 = y */
            mtype = m & 0xFF;
            mx = (m >> 8) & 0xFFF;
            my = (m >> 20) & 0xFFF;
#else
            static int s_mouse_button_down = 0;
            MEVENT ev;
            int my;
            int mx;
            int mtype;

            if (getmouse(&ev) != OK)
                continue;

            mx = ev.x;
            my = ev.y;

            /* Map ncurses bstate to UiMouseEventType */
            if (ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))
            {
                s_mouse_button_down = 1;
                mtype = UI_MOUSE_PRESS_LEFT;
            }
            else if (ev.bstate & BUTTON1_RELEASED)
            {
                s_mouse_button_down = 0;
                mtype = UI_MOUSE_RELEASE_LEFT;
            }
            else if (ev.bstate & BUTTON4_PRESSED)
                mtype = UI_MOUSE_WHEEL_UP;
            else if (ev.bstate & BUTTON5_PRESSED)
                mtype = UI_MOUSE_WHEEL_DOWN;
            else if (s_mouse_button_down && (ev.bstate & REPORT_MOUSE_POSITION))
                mtype = UI_MOUSE_DRAG_LEFT;
            else
                continue; /* Unknown event */
#endif

            /* Adjust mouse coordinates for editor frame offset */
            win = wm_get_window_by_type(app->wm, WIN_EDITOR);

            if (win && win->visible)
            {
                my -= win->y;
                mx -= win->x;
            }

            if (ui_mouse_dispatch(app, mtype, my, mx))
                screen_dirty = 1;

            continue;
        }

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

                /* Pre-wrap pasted text in hard-wrap mode; soft-wrap inserts verbatim */
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
            int mod_idx;

            if (app->search.is_mode || app->search.only_mode)
            {
                clear_search_highlights(app);

                app->search.is_mode = 0;
                app->search.only_mode = 0;

                te_status(app, "Search mode exited");
                screen_dirty = 1;
                continue;
            }

            mod_idx = te_app_first_modified_tab(app);

            if (mod_idx >= 0)
            {
                int r;

                te_app_switch_tab(app, mod_idx);
                screen_dirty = 1;

                r = ui_popup_confirm("Quit", "Discard changes and quit?");

                if (r != 1)
                    continue;
            }

            curs_set(0);
            TE_BRACKET_PASTE_OFF();

            ui_editor_session_save(app);
            ui_editor_swp_cleanup_all(app);

            break;
        }

        /* F10 : quit */
        if (is_key && ch == KEY_F(10))
        {
            int mod_idx;

            mod_idx = te_app_first_modified_tab(app);

            if (mod_idx >= 0)
            {
                int r;

                te_app_switch_tab(app, mod_idx);
                screen_dirty = 1;

                r = ui_popup_confirm("Quit", "Discard changes and quit?");

                if (r != 1)
                    continue;
            }

            curs_set(0);
            TE_BRACKET_PASTE_OFF();

            ui_editor_session_save(app);
            ui_editor_swp_cleanup_all(app);

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

                    /* Pre-wrap pasted text in hard-wrap mode; soft-wrap inserts verbatim */
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
