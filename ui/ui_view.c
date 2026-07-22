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

/* ui_view.c -- soft-wrap viewport: sub-rows, scrolling and visual movement */

#include <wchar.h>
#include "te.h"
#include "ui_editor_helper.h"
#include "ui_view.h"
#include "../components/ed_attr.h"
#include "../components/layout.h"

/* Viewport anchored on a logical line and one of its sub-rows */
static int s_top_line = 0;
static int s_top_sub = 0;

/* Visual column kept across vertical moves, -1 = recompute */
static int s_desired_vcol = -1;

static int s_tab = 4;

void view_set_tab_width(int n)
{
    s_tab = n > 0 ? n : 4;
}

int view_tab_width(void)
{
    return s_tab;
}

int view_top_line(void)
{
    return s_top_line;
}

int view_top_sub(void)
{
    return s_top_sub;
}

void view_set_top(int line, int sub)
{
    s_top_line = line < 0 ? 0 : line;
    s_top_sub = sub < 0 ? 0 : sub;
}

void view_reset_desired(void)
{
    s_desired_vcol = -1;
}

int view_align_indent(unsigned char align, int text_vw, int avail)
{
    return layout_align_indent(align, text_vw, avail);
}

/* End offset of the sub-row starting at start: last space fit, else hard cut */
int view_wrap_next(const wchar_t *l, int len, int width, int start)
{
    int vcol = 0;
    int col = 0;
    int last_space = -1;
    int k = start;

    if (width < 1)
        width = 1;

    if (start > 0 && start <= len)
        col = wcs_vwidth_ex(l, start, 0, s_tab);

    while (k < len)
    {
        int w = wcs_vwidth_ex(&l[k], 1, col, s_tab);

        if (vcol + w > width)
            break;

        vcol += w;
        col += w;
        k++;

        if (l[k - 1] == L' ' || l[k - 1] == L'\t')
            last_space = k;
    }

    if (k >= len)
        return len;

    if (last_space > start)
        return last_space;

    /* A single glyph wider than the row still has to advance */
    return k > start ? k : start + 1;
}

int view_wrap_count(const wchar_t *l, int len, int width)
{
    int pos = 0;
    int rows = 1;

    if (len <= 0)
        return 1;

    for (;;)
    {
        int end = view_wrap_next(l, len, width, pos);

        if (end >= len)
            break;

        if (end <= pos)
            end = pos + (width < 1 ? 1 : width);

        pos = end;
        rows++;
    }

    return rows;
}

/* Byte range of target_sub, returns the sub-row actually reached */
int view_subrow_range(const wchar_t *l, int len, int width, int target_sub, int *seg_start, int *seg_end)
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
        int end = view_wrap_next(l, len, width, pos);

        if (sub == target_sub || end >= len)
        {
            *seg_start = pos;
            *seg_end = end;

            return sub;
        }

        if (end <= pos)
            end = pos + (width < 1 ? 1 : width);

        pos = end;
        sub++;
    }
}

int view_subrow_of_col(const wchar_t *l, int len, int width, int col)
{
    int pos = 0;
    int sub = 0;

    if (!l || len <= 0)
        return 0;

    for (;;)
    {
        int end = view_wrap_next(l, len, width, pos);

        if (col < end || end >= len)
            return sub;

        if (end <= pos)
            end = pos + (width < 1 ? 1 : width);

        pos = end;
        sub++;
    }
}

/* Sub-rows of one logical line */
static int line_subs(Ed *ed, int line, int width)
{
    const wchar_t *l = ed_line_wcs(ed, line);
    int len = ed_line_len(ed, line);

    return view_wrap_count(l ? l : L"", l ? len : 0, width);
}

/* Sub-row holding the cursor */
static int cursor_sub(Ed *ed, int width, const EdInfo *info)
{
    const wchar_t *l = ed_line_wcs(ed, info->row);
    int len = ed_line_len(ed, info->row);

    return view_subrow_of_col(l ? l : L"", l ? len : 0, width, info->col);
}

void view_reset_to_cursor(TeApp *app, int width)
{
    Ed *ed = te_app_get_editor(app);
    EdInfo info;

    ed_get_info(ed, &info);

    s_top_line = info.row;
    s_top_sub = cursor_sub(ed, width, &info);
    s_desired_vcol = -1;
}

void view_walk_forward(Ed *ed, int width, int from_line, int from_sub, int delta, int *out_line, int *out_sub)
{
    EdInfo info;
    int line = from_line;
    int sub = from_sub;
    int left = delta;

    ed_get_info(ed, &info);

    while (left > 0 && line < info.line_count)
    {
        int avail = line_subs(ed, line, width) - sub - 1;

        if (left <= avail)
        {
            sub += left;
            left = 0;

            break;
        }

        left -= avail + 1;
        line++;
        sub = 0;
    }

    if (line >= info.line_count)
    {
        if (info.line_count > 0)
        {
            line = info.line_count - 1;
            sub = line_subs(ed, line, width) - 1;

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

void view_walk_backward(Ed *ed, int width, int from_line, int from_sub, int delta, int *out_line, int *out_sub)
{
    int line = from_line;
    int sub = from_sub;
    int left = delta;

    while (left > 0)
    {
        if (sub > 0)
        {
            int take = sub < left ? sub : left;

            sub -= take;
            left -= take;

            if (left == 0)
                break;
        }

        if (line == 0)
        {
            sub = 0;
            break;
        }

        line--;
        sub = line_subs(ed, line, width) - 1;

        if (sub < 0)
            sub = 0;

        left--;
    }

    if (line < 0)
    {
        line = 0;
        sub = 0;
    }

    *out_line = line;
    *out_sub = sub;
}

/* Signed visual row distance from a to b */
int view_vrows_between(Ed *ed, int width, int a_line, int a_sub, int b_line, int b_sub)
{
    int lo;
    int hi;
    int lo_sub;
    int hi_sub;
    int delta;
    int i;

    if (a_line == b_line)
        return b_sub - a_sub;

    if (a_line < b_line)
    {
        lo = a_line;
        lo_sub = a_sub;
        hi = b_line;
        hi_sub = b_sub;
    }
    else
    {
        lo = b_line;
        lo_sub = b_sub;
        hi = a_line;
        hi_sub = a_sub;
    }

    delta = line_subs(ed, lo, width) - lo_sub;

    for (i = lo + 1; i < hi; i++)
        delta += line_subs(ed, i, width);

    delta += hi_sub;

    return (a_line < b_line) ? delta : -delta;
}

int view_cursor_vcol(Ed *ed, int width)
{
    EdInfo info;
    const wchar_t *l = NULL;
    int len;
    int seg_start = 0;
    int seg_end = 0;
    int n;

    ed_get_info(ed, &info);

    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);

    if (!l || len <= 0)
        return 0;

    view_subrow_range(l, len, width, view_subrow_of_col(l, len, width, info.col), &seg_start, &seg_end);

    n = info.col - seg_start;

    if (n < 0)
        n = 0;

    if (n > len - seg_start)
        n = len - seg_start;

    return wcs_vwidth_ex(&l[seg_start], n, 0, s_tab);
}

int view_cursor_align_indent(Ed *ed, int width)
{
    EdInfo info;
    const wchar_t *l = NULL;
    int len;
    int seg_start = 0;
    int seg_end = 0;
    int seg_vw;

    ed_get_info(ed, &info);

    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);

    if (!l || len <= 0)
        return 0;

    view_subrow_range(l, len, width, view_subrow_of_col(l, len, width, info.col), &seg_start, &seg_end);

    seg_vw = wcs_vwidth_ex(&l[seg_start], seg_end - seg_start, 0, s_tab);

    return view_align_indent(ed->lines[info.row]->para_align, seg_vw, width);
}

/* Extra columns to add to the cursor when its sub-row is being justified */
int view_cursor_justify_shift(Ed *ed, int width)
{
    EdInfo info;
    EdLine *ln = NULL;
    const wchar_t *l = NULL;
    int len;
    int seg_start = 0;
    int seg_end = 0;
    int seg_len;
    int seg_vw;
    int offsets[4096];
    int col_in_seg;

    ed_get_info(ed, &info);

    if (info.row < 0 || info.row >= ed->count)
        return 0;

    ln = ed->lines[info.row];

    if (ln->para_align != EA_ALIGN_JUST)
        return 0;

    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);

    if (!l || len <= 0)
        return 0;

    view_subrow_range(l, len, width, view_subrow_of_col(l, len, width, info.col), &seg_start, &seg_end);

    seg_len = seg_end - seg_start;

    if (seg_len <= 0 || seg_len > (int)(sizeof(offsets) / sizeof(offsets[0])))
        return 0;

    /* Single-line paragraphs justify, last row of multi-line paragraph does not */
    if (!ed_segment_should_justify(EA_ALIGN_JUST, seg_end == len && ln->brk == LB_PARA, seg_start == 0 && seg_end == len))
        return 0;

    seg_vw = wcs_vwidth_ex(&l[seg_start], seg_len, 0, s_tab);

    if (!ui_justify_offsets(&l[seg_start], seg_len, seg_vw, width, offsets))
        return 0;

    col_in_seg = info.col - seg_start;

    if (col_in_seg < 0)
        col_in_seg = 0;

    if (col_in_seg >= seg_len)
        col_in_seg = seg_len - 1;

    return offsets[col_in_seg];
}

int view_cursor_screen_row(TeApp *app, int width)
{
    Ed *ed = te_app_get_editor(app);
    EdInfo info;

    ed_get_info(ed, &info);

    return view_vrows_between(ed, width, s_top_line, s_top_sub, info.row, cursor_sub(ed, width, &info));
}

void view_ensure_visible(TeApp *app, int width, int body_rows)
{
    Ed *ed = te_app_get_editor(app);
    EdInfo info;
    int sub;
    int screen_row;
    int tn;

    ed_get_info(ed, &info);

    if (info.line_count <= 0)
    {
        s_top_line = 0;
        s_top_sub = 0;

        return;
    }

    if (s_top_line < 0)
        s_top_line = 0;

    if (s_top_line >= info.line_count)
        s_top_line = info.line_count - 1;

    tn = line_subs(ed, s_top_line, width);

    if (s_top_sub < 0)
        s_top_sub = 0;

    if (s_top_sub >= tn)
        s_top_sub = tn - 1;

    sub = cursor_sub(ed, width, &info);
    screen_row = view_vrows_between(ed, width, s_top_line, s_top_sub, info.row, sub);

    if (screen_row < 0)
    {
        s_top_line = info.row;
        s_top_sub = sub;
    }
    else if (screen_row >= body_rows)
    {
        int line;
        int nsub;

        view_walk_forward(ed, width, s_top_line, s_top_sub, screen_row - (body_rows - 1), &line, &nsub);

        s_top_line = line;
        s_top_sub = nsub;
    }
}

/* Put the cursor on (line, sub) at the given visual column */
static void set_cursor_at(Ed *ed, int width, int line, int sub, int desired_vcol)
{
    const wchar_t *l = ed_line_wcs(ed, line);
    int len = ed_line_len(ed, line);
    int seg_start = 0;
    int seg_end = 0;
    int v = 0;
    int col;
    int i;

    view_subrow_range(l ? l : L"", l ? len : 0, width, sub, &seg_start, &seg_end);

    if (desired_vcol < 0)
        desired_vcol = 0;

    col = seg_start;

    if (l && len > 0)
    {
        for (i = seg_start; i < seg_end; i++)
        {
            int w = wcs_vwidth_ex(&l[i], 1, v, s_tab);

            if (v + w > desired_vcol)
                break;

            v += w;
            col = i + 1;
        }
    }

    /* Only the last sub-row may hold the cursor past its last glyph */
    if (seg_end < len && col >= seg_end)
        col = seg_end > seg_start ? seg_end - 1 : seg_start;

    if (col < 0)
        col = 0;

    if (col > len)
        col = len;

    ed_set_pos(ed, line, col);
}

void view_move_vrows(TeApp *app, int width, int delta)
{
    Ed *ed = te_app_get_editor(app);
    EdInfo info;
    int sub;
    int line;
    int nsub;

    if (delta == 0)
        return;

    ed_get_info(ed, &info);

    if (info.line_count <= 0)
        return;

    sub = cursor_sub(ed, width, &info);

    if (s_desired_vcol < 0)
        s_desired_vcol = view_cursor_vcol(ed, width);

    if (delta > 0)
        view_walk_forward(ed, width, info.row, sub, delta, &line, &nsub);
    else
        view_walk_backward(ed, width, info.row, sub, -delta, &line, &nsub);

    set_cursor_at(ed, width, line, nsub, s_desired_vcol);
}

void view_move_up(TeApp *app, int width)
{
    view_move_vrows(app, width, -1);
}

void view_move_down(TeApp *app, int width)
{
    view_move_vrows(app, width, 1);
}

void view_move_home(TeApp *app, int width)
{
    Ed *ed = te_app_get_editor(app);
    EdInfo info;
    const wchar_t *l = NULL;
    int len;
    int seg_start = 0;
    int seg_end = 0;

    ed_get_info(ed, &info);

    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);

    view_subrow_range(l ? l : L"", l ? len : 0, width, cursor_sub(ed, width, &info), &seg_start, &seg_end);
    ed_set_pos(ed, info.row, seg_start);
    view_reset_desired();
}

void view_move_end(TeApp *app, int width)
{
    Ed *ed = te_app_get_editor(app);
    EdInfo info;
    const wchar_t *l = NULL;
    int len;
    int seg_start = 0;
    int seg_end = 0;
    int target;

    ed_get_info(ed, &info);

    l = ed_line_wcs(ed, info.row);
    len = ed_line_len(ed, info.row);

    view_subrow_range(l ? l : L"", l ? len : 0, width, cursor_sub(ed, width, &info), &seg_start, &seg_end);

    if (seg_end >= len)
        target = len;
    else
        target = seg_end > seg_start ? seg_end - 1 : seg_start;

    ed_set_pos(ed, info.row, target);
    view_reset_desired();
}

void view_move_pgup(TeApp *app, int width, int pg)
{
    Ed *ed = te_app_get_editor(app);
    int line;
    int sub;

    if (pg <= 0)
        pg = 1;

    view_move_vrows(app, width, -pg);
    view_walk_backward(ed, width, s_top_line, s_top_sub, pg, &line, &sub);

    s_top_line = line;
    s_top_sub = sub;
}

void view_move_pgdn(TeApp *app, int width, int pg)
{
    Ed *ed = te_app_get_editor(app);
    int line;
    int sub;

    if (pg <= 0)
        pg = 1;

    view_move_vrows(app, width, pg);
    view_walk_forward(ed, width, s_top_line, s_top_sub, pg, &line, &sub);

    s_top_line = line;
    s_top_sub = sub;
}

void view_reset_viewport_to_cursor(TeApp *app, int width)
{
    view_reset_to_cursor(app, width);
}

/* Map a screen cell inside a sub-row segment to a char index, honouring justify shifts */
static int hittest_seg(const wchar_t *l, int len, int seg_start, int seg_end, unsigned char brk, unsigned char para_align, int width, int screen_x)
{
    int seg_len = seg_end - seg_start;
    int just_offsets[4096];
    const int *offsets = NULL;
    int is_para_last;
    int hyph_reserve;
    int acc;
    int j;

    if (seg_len <= 0)
        return seg_start;

    /* Same rule as paint_segment: justify only intermediate sub-rows of a paragraph */
    is_para_last = (seg_end == len) && (brk == LB_PARA);
    hyph_reserve = (seg_end == len && brk == LB_HYPHEN) ? 1 : 0;

    if (ed_segment_should_justify(para_align, is_para_last, seg_start == 0 && seg_end == len) && seg_len < (int)(sizeof(just_offsets) / sizeof(just_offsets[0])))
    {
        int text_vw_now = wcs_vwidth_ex(&l[seg_start], seg_len, 0, s_tab);
        int target_vw = width - hyph_reserve;

        if (target_vw > text_vw_now && ui_justify_offsets(&l[seg_start], seg_len, text_vw_now, target_vw, just_offsets))
            offsets = just_offsets;
    }

    acc = 0;

    for (j = 0; j < seg_len; j++)
    {
        int cw = wcs_vwidth_ex(&l[seg_start + j], 1, acc, s_tab);
        int shift = offsets ? offsets[j] : 0;

        if (acc + shift + cw > screen_x)
            break;

        acc += cw;
    }

    return seg_start + j;
}

/* Map a screen cell to a buffer position, both wrap modes */
int ui_editor_screen_to_logical(TeApp *app, int width, int screen_y, int screen_x, int *out_line, int *out_col)
{
    Ed *ed = NULL;
    EdInfo info;
    const wchar_t *l = NULL;
    int len;
    int line;
    int sub;
    int left;
    int soft;
    int indent;

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

    soft = !app->hard_wrap;

    /* Hard wrap: one logical line per screen row */
    if (!soft)
    {
        line = info.top + screen_y;

        if (line >= info.line_count)
        {
            *out_line = info.line_count - 1;
            *out_col = ed_line_len(ed, info.line_count - 1);

            return 0;
        }

        if (line < 0)
            line = 0;

        l = ed_line_wcs(ed, line);
        len = ed_line_len(ed, line);
        indent = view_align_indent(ed->lines[line]->para_align, (l && len > 0) ? wcs_vwidth_ex(l, len, 0, s_tab) : 0, width);

        screen_x -= indent;

        if (screen_x < 0)
            screen_x = 0;

        *out_line = line;
        *out_col = hittest_seg(l ? l : L"", len, 0, len, ed->lines[line]->brk, ed->lines[line]->para_align, width, screen_x);

        return 0;
    }

    /* Soft wrap: consume sub-rows from the viewport anchor */
    line = s_top_line;
    sub = s_top_sub;
    left = screen_y;

    while (line < info.line_count)
    {
        int nsub = line_subs(ed, line, width);
        int in_line = nsub - sub;

        if (left < in_line)
        {
            int seg_start = 0;
            int seg_end = 0;

            l = ed_line_wcs(ed, line);
            len = ed_line_len(ed, line);

            view_subrow_range(l ? l : L"", l ? len : 0, width, sub + left, &seg_start, &seg_end);

            indent = view_align_indent(ed->lines[line]->para_align, (l && seg_end > seg_start) ? wcs_vwidth_ex(&l[seg_start], seg_end - seg_start, 0, s_tab) : 0, width);

            screen_x -= indent;

            if (screen_x < 0)
                screen_x = 0;

            *out_line = line;
            *out_col = hittest_seg(l ? l : L"", len, seg_start, seg_end, ed->lines[line]->brk, ed->lines[line]->para_align, width, screen_x);

            return 0;
        }

        left -= in_line;
        line++;
        sub = 0;
    }

    *out_line = info.line_count - 1;
    *out_col = ed_line_len(ed, info.line_count - 1);

    return 0;
}
