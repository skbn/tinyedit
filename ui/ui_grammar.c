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
#include <string.h>
#include <stdlib.h>

#include "ui_grammar.h"

#ifdef HAVE_GRAMMAR

#include "../grammar/grammar.h"
#include "../core/utf8.h"
#include "../components/editor.h"
#include "ui_editor_helper.h"

/* ncurses is pulled in by te.h -> ncursesw shim, which gives us attron/attroff/COLOR_PAIR/mvaddnwstr and the COL_* macros */

#ifndef UI_GRAM_MAX_UTF8
#define UI_GRAM_MAX_UTF8 4096 /* max UTF-8 bytes for one line snapshot */
#endif

#ifndef PATH_SEP
#ifdef PLATFORM_WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif
#endif

/* Small encoder without malloc, returns UTF-8 byte length written excl NUL, or -1 if target buffer would overflow */
static int wcs_to_utf8_buf(const wchar_t *wcs, int wlen, char *out, int outsz)
{
    int i;
    int o;
    unsigned long cp;

    if (!out || outsz <= 0)
        return -1;

    o = 0;

    for (i = 0; i < wlen; i++)
    {
        cp = (unsigned long)wcs[i];

        if (cp < 0x80ul)
        {
            if (o + 1 >= outsz)
                return -1;

            out[o++] = (char)cp;
        }
        else if (cp < 0x800ul)
        {
            if (o + 2 >= outsz)
                return -1;

            out[o++] = (char)(0xC0u | (cp >> 6));
            out[o++] = (char)(0x80u | (cp & 0x3Fu));
        }
        else if (cp < 0x10000ul)
        {
            if (o + 3 >= outsz)
                return -1;

            out[o++] = (char)(0xE0u | (cp >> 12));
            out[o++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            out[o++] = (char)(0x80u | (cp & 0x3Fu));
        }
        else
        {
            if (o + 4 >= outsz)
                return -1;

            out[o++] = (char)(0xF0u | (cp >> 18));
            out[o++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
            out[o++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            out[o++] = (char)(0x80u | (cp & 0x3Fu));
        }
    }

    out[o] = '\0';

    return o;
}

static int prev_line_terminated(TeApp *app, int line_idx)
{
    Ed *ed = NULL;
    const wchar_t *wl = NULL;
    int len;
    int i;
    int prev;

    if (!app || line_idx <= 0)
        return 0;

    ed = te_app_get_editor(app);

    if (!ed)
        return 0;

    for (prev = line_idx - 1; prev >= 0; prev--)
    {
        wl = ed_line_wcs(ed, prev);
        len = ed_line_len(ed, prev);

        if (!wl || len <= 0)
            return 1;

        for (i = len - 1; i >= 0; i--)
        {
            if (wl[i] == L' ' || wl[i] == L'\t')
                continue;

            if (wl[i] == L'.' || wl[i] == L'!' || wl[i] == L'?')
                return 1;

            return 0;
        }
    }

    return 0;
}

static void gg_join(char *out, size_t outsz, const char *dir, const char *name)
{
    size_t dl;
    int n;

    if (!out || outsz == 0)
        return;

    out[0] = '\0';

    if (!dir || !*dir)
    {
        n = snprintf(out, outsz, "%s.rul", name ? name : "");

        if (n < 0 || (size_t)n >= outsz)
            out[0] = '\0'; /* refuse truncated path */

        return;
    }

    dl = strlen(dir);

    if (dl && (dir[dl - 1] == '/' || dir[dl - 1] == '\\' || dir[dl - 1] == ':'))
        n = snprintf(out, outsz, "%s%s.rul", dir, name);
    else
        n = snprintf(out, outsz, "%s%c%s.rul", dir, PATH_SEP, name);

    if (n < 0 || (size_t)n >= outsz)
        out[0] = '\0'; /* refuse truncated path */
}

int ui_grammar_load_from_config(TeApp *app)
{
    char path[512];
    GramCheck *g = NULL;

    if (!app)
        return -1;

    ui_grammar_free_app(app);

    if (!app->cfg.grammar_enabled)
        return -1;

    if (!app->cfg.grammar_dict_name[0])
        return -1;

    gg_join(path, sizeof(path), app->cfg.grammar_dict_path, app->cfg.grammar_dict_name);

    g = gc_new(path);

    if (!g)
    {
        snprintf(app->status, sizeof(app->status), "grammar: cannot load %s", path);
        return -1;
    }

    app->grammar_handle = (void *)g;
    app->grammar_enabled = 1;
    app->grammar_active = 1;

    ui_grammar_apply_toggles(app);

    return 0;
}

void ui_grammar_free_app(TeApp *app)
{
    if (!app || !app->grammar_handle)
        return;

    gc_free((GramCheck *)app->grammar_handle);

    app->grammar_handle = NULL;
    app->grammar_active = 0;
}

int ui_grammar_check_row(TeApp *app, const wchar_t *wl, int line_len, int line_idx, GcIssue *out, int cap)
{
    char utf8[UI_GRAM_MAX_UTF8];
    int n;

    if (!app || !app->grammar_handle || !app->grammar_active)
        return 0;

    if (!wl || line_len <= 0 || !out || cap <= 0)
        return 0;

    n = wcs_to_utf8_buf(wl, line_len, utf8, (int)sizeof(utf8));

    if (n < 0)
        return 0;

    return gc_check_line_ctx((GramCheck *)app->grammar_handle, utf8, prev_line_terminated(app, line_idx), out, cap);
}

/* Map byte offset from GcIssue.byte_off/len produced by encoding wl as UTF-8 back to wchar_t index in wl, linear scan is fine because lines are short and issues are few */
static int byte_off_to_wcs(const wchar_t *wl, int line_len, int byte_off)
{
    int i;
    int b;
    unsigned long cp;

    if (byte_off <= 0 || !wl || line_len <= 0)
        return 0;

    b = 0;

    for (i = 0; i < line_len; i++)
    {
        int step;

        cp = (unsigned long)wl[i];

        if (cp < 0x80ul)
            step = 1;
        else if (cp < 0x800ul)
            step = 2;
        else if (cp < 0x10000ul)
            step = 3;
        else
            step = 4;

        if (b >= byte_off)
            return i;

        b += step;
    }

    return line_len;
}

void ui_grammar_draw_row(TeApp *app, int screen_y, int col_offset, int tab_width, const wchar_t *wl, int line_len, int line_idx)
{
    GcIssue gis[GC_MAX_ISSUES_PER_LINE];
    int gn, k;
    int wcs_off;
    int wcs_end;

    if (!app || !app->grammar_handle || !app->grammar_active)
        return;

    if (!wl || line_len <= 0)
        return;

    gn = ui_grammar_check_row(app, wl, line_len, line_idx, gis, GC_MAX_ISSUES_PER_LINE);

    if (gn <= 0)
        return;

    for (k = 0; k < gn; k++)
    {
        int cp_pair;
        int vcol;
        int wlen;

        wcs_off = byte_off_to_wcs(wl, line_len, (int)gis[k].byte_off);
        wcs_end = byte_off_to_wcs(wl, line_len, (int)gis[k].byte_off + (int)gis[k].byte_len);
        wlen = wcs_end - wcs_off;

        if (wlen <= 0)
            continue;

        vcol = wcs_vwidth_ex(wl, wcs_off, 0, tab_width);
        cp_pair = ui_grammar_color_for_severity(gis[k].severity);

        attron(A_UNDERLINE | COLOR_PAIR(cp_pair));
        mvaddnwstr(screen_y, col_offset + vcol, &wl[wcs_off], wlen);
        attroff(A_UNDERLINE | COLOR_PAIR(cp_pair));
    }
}

void ui_grammar_draw_row_segment(TeApp *app, int screen_y, int col_offset, int tab_width, const wchar_t *wl, int line_len, int seg_start, int seg_end, int seg_start_vcol, int line_idx)
{
    GcIssue gis[GC_MAX_ISSUES_PER_LINE];
    int gn, k;
    int wcs_off, wcs_end;

    if (!app || !app->grammar_handle || !app->grammar_active)
        return;

    if (!wl || line_len <= 0)
        return;

    if (seg_start < 0)
        seg_start = 0;

    if (seg_end > line_len)
        seg_end = line_len;

    if (seg_start >= seg_end)
        return;

    /* Cache-friendly, same line hash across all sub-rows of the line */
    gn = ui_grammar_check_row(app, wl, line_len, line_idx, gis, GC_MAX_ISSUES_PER_LINE);

    if (gn <= 0)
        return;

    for (k = 0; k < gn; k++)
    {
        int cp_pair;
        int vcol;
        int wlen;
        int clip_start, clip_end;

        wcs_off = byte_off_to_wcs(wl, line_len, (int)gis[k].byte_off);
        wcs_end = byte_off_to_wcs(wl, line_len, (int)gis[k].byte_off + (int)gis[k].byte_len);

        /* Clip issue span to current sub-row segment */
        clip_start = wcs_off < seg_start ? seg_start : wcs_off;
        clip_end = wcs_end > seg_end ? seg_end : wcs_end;

        if (clip_start >= clip_end)
            continue;

        wlen = clip_end - clip_start;
        vcol = wcs_vwidth_ex(&wl[seg_start], clip_start - seg_start, seg_start_vcol, tab_width);
        cp_pair = ui_grammar_color_for_severity(gis[k].severity);

        attron(A_UNDERLINE | COLOR_PAIR(cp_pair));
        mvaddnwstr(screen_y, col_offset + vcol, &wl[clip_start], wlen);
        attroff(A_UNDERLINE | COLOR_PAIR(cp_pair));
    }
}

void ui_grammar_prewarm(TeApp *app, int top, int body_rows, int line_count)
{
    Ed *ed = NULL;
    GcIssue scratch[GC_MAX_ISSUES_PER_LINE];
    char utf8[UI_GRAM_MAX_UTF8];
    int budget;
    int margin;
    int k;
    int idx;
    const wchar_t *wl = NULL;
    int wl_len;
    int u;

    if (!app || !app->grammar_handle || !app->grammar_active)
        return;

    ed = te_app_get_editor(app);

    if (!ed)
        return;

    budget = UI_GRAM_PREWARM_BUDGET;
    margin = UI_GRAM_PREWARM_MARGIN;

    /* Above the viewport */
    for (k = 1; k <= margin && budget > 0; k++)
    {
        idx = top - k;

        if (idx < 0 || idx >= line_count)
            break;

        wl = ed_line_wcs(ed, idx);
        wl_len = ed_line_len(ed, idx);

        if (!wl || wl_len <= 0)
            continue;

        u = wcs_to_utf8_buf(wl, wl_len, utf8, (int)sizeof(utf8));

        if (u < 0)
            continue;

        gc_check_line((GramCheck *)app->grammar_handle, utf8, scratch, GC_MAX_ISSUES_PER_LINE);

        budget--;
    }

    /* Below the viewport */
    for (k = 0; k < margin && budget > 0; k++)
    {
        idx = top + body_rows + k;

        if (idx < 0 || idx >= line_count)
            break;

        wl = ed_line_wcs(ed, idx);
        wl_len = ed_line_len(ed, idx);

        if (!wl || wl_len <= 0)
            continue;

        u = wcs_to_utf8_buf(wl, wl_len, utf8, (int)sizeof(utf8));

        if (u < 0)
            continue;

        gc_check_line((GramCheck *)app->grammar_handle, utf8, scratch, GC_MAX_ISSUES_PER_LINE);

        budget--;
    }
}

void ui_grammar_invalidate_row(TeApp *app, const wchar_t *wl, int line_len)
{
    char utf8[UI_GRAM_MAX_UTF8];
    int u;

    if (!app || !app->grammar_handle)
        return;

    if (!wl || line_len <= 0)
        return;

    u = wcs_to_utf8_buf(wl, line_len, utf8, (int)sizeof(utf8));

    if (u < 0)
        return;

    gc_cache_invalidate_line((GramCheck *)app->grammar_handle, utf8);
}

void ui_grammar_flush(TeApp *app)
{
    if (!app || !app->grammar_handle)
        return;

    gc_cache_clear((GramCheck *)app->grammar_handle);
}

void ui_grammar_apply_toggles(TeApp *app)
{
    GramCheck *g = NULL;
    unsigned mask;

    if (!app || !app->grammar_handle)
        return;

    g = (GramCheck *)app->grammar_handle;

    mask = GC_CAT_PUNCT | GC_CAT_SPACING | GC_CAT_CASE | GC_CAT_REPEAT | GC_CAT_BRACKET | GC_CAT_TYPO | GC_CAT_STYLE;

    if (!app->cfg.assist_repeat_check)
        mask &= ~(unsigned)GC_CAT_REPEAT;

    gc_set_enabled_categories(g, ~0u, 0);
    gc_set_enabled_categories(g, mask, 1);
}

int ui_grammar_color_for_severity(unsigned char sev)
{
#ifndef UI_GRAM_COLOR_ERROR
#define UI_GRAM_COLOR_ERROR COL_SPELL_CURRENT /* reuse spell red */
#endif
#ifndef UI_GRAM_COLOR_WARN
#define UI_GRAM_COLOR_WARN COL_BORDER
#endif
#ifndef UI_GRAM_COLOR_INFO
#define UI_GRAM_COLOR_INFO COL_GUIDE
#endif

    switch (sev)
    {
    case GC_ERROR:
        return UI_GRAM_COLOR_ERROR;
    case GC_WARN:
        return UI_GRAM_COLOR_WARN;
    default:
        return UI_GRAM_COLOR_INFO;
    }
}

#endif /* HAVE_GRAMMAR */
