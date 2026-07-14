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
#include "../components/undo.h"
#include "../components/ed_attr.h"
#include "te.h"
#include "ui_files.h"
#include "../components/fmt_rtf.h"
#include "../components/fmt_wp4.h"
#include "ui_editor_helper.h"
#include "ui_view.h"
#include "ui_keys.h"
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

#ifdef PLATFORM_AMIGA
#define SYNTAX_STATE_LOOKUP_LIMIT 256
#define BRACKET_MATCH_LOOKUP_LIMIT 256
#else
#define INSIDE_BLOCK_COMMENT_LOOKUP_LIMIT 1024
#define SYNTAX_STATE_LOOKUP_LIMIT 1024
#define BRACKET_MATCH_LOOKUP_LIMIT 1024
#endif

typedef struct
{
    TeApp *app;
    Ed *ed;
    int cur_row;
    int offset_x;
    int offset_y;
    int ln_offset;
    int ln_width;
    int width;
    int body_width;
    int show_lnum;
    int b_r1;
    int b_c1;
    int b_r2;
    int b_c2;
} PaintCtx;

/* Whitespace marker glyphs (hex escapes keep this C89-valid) */
static const wchar_t s_ws_arrow[2] = {0x2192, 0}; /* Rightwards arrow */
static const wchar_t s_ws_dot[2] = {0xB7, 0};     /* Middle dot */
static int s_soft_last_width = -1;

/* The soft-wrap viewport engine now lives in ui_view.c */
#define s_soft_top_line view_top_line()
#define s_soft_top_sub view_top_sub()
#define s_tab_width view_tab_width()
#define wrap_next(l, len, w, st) view_wrap_next((l), (len), (w), (st))
#define wrap_count(l, len, w) view_wrap_count((l), (len), (w))
#define line_subrow_range(l, len, w, t, a, b) view_subrow_range((l), (len), (w), (t), (a), (b))
#define line_subrow_of_col(l, len, w, c) view_subrow_of_col((l), (len), (w), (c))
#define walk_vrows_forward(e, w, fl, fs, d, ol, os) view_walk_forward((e), (w), (fl), (fs), (d), (ol), (os))
#define walk_vrows_backward(e, w, fl, fs, d, ol, os) view_walk_backward((e), (w), (fl), (fs), (d), (ol), (os))
#define soft_vrows_between(e, w, al, as_, bl, bs) view_vrows_between((e), (w), (al), (as_), (bl), (bs))
#define soft_cursor_vcol(e, w) view_cursor_vcol((e), (w))
#define soft_cursor_align_indent(e, w) view_cursor_align_indent((e), (w))
#define soft_cursor_screen_row(a, w) view_cursor_screen_row((a), (w))
#define soft_ensure_visible(a, w, r) view_ensure_visible((a), (w), (r))
#define soft_move_vrows(a, w, d) view_move_vrows((a), (w), (d))
#define soft_move_up_visual(a, w) view_move_up((a), (w))
#define soft_move_down_visual(a, w) view_move_down((a), (w))
#define soft_move_home_visual(a, w) view_move_home((a), (w))
#define soft_move_end_visual(a, w) view_move_end((a), (w))
#define soft_move_pgup_visual(a, w, p) view_move_pgup((a), (w), (p))
#define soft_move_pgdn_visual(a, w, p) view_move_pgdn((a), (w), (p))

/* Alignment indent of the sub-row the cursor sits in (soft-wrap) */
static int line_align_indent(unsigned char align, int text_vw, int avail);

void soft_reset_desired(void)
{
    view_reset_desired();
}

void soft_reset_viewport_to_cursor(TeApp *app, int width)
{
    view_reset_to_cursor(app, width);
}

/* Soft-wrap: break at last space or hard-cut at boundary */
static int paste_char_width(wchar_t c)
{
    /* For FTN editing: char column width (0 for combining marks, BOM) */
    if (c == 0)
        return 0;

    if (c < 0x20)
        return 0; /* Control chars */

    if (c >= 0x0300 && c <= 0x036F)
        return 0; /* Combining diacritical marks */

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

/* Trigger paragraph reflow after an edit in hard-wrap mode */
void ed_auto_rewrap_after_edit(TeApp *app)
{
    int width;
    LayoutHyphenFn hyph = NULL;
    void *hyph_user = NULL;

    if (!app->hard_wrap)
        return;

    width = editor_eff_wrap(app);

    if (width <= 0)
        return;

#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
    if (app->hyph_wrap_enabled && app->hyph_handle)
    {
        hyph = ui_layout_hyphen;
        hyph_user = app;
    }
#endif

    te_app_get_editor(app)->undo_snapshot_mode = 1;

    ed_rewrap_paragraph_ex(te_app_get_editor(app), width, hyph, hyph_user);

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
/* Check whether a word is misspelled, joining the halves of a word that the layout split across two lines, so a fragment is never reported as a mistake */
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

    /* The word reaches the end of a line broken inside a word: the hyphen is not in the text, the break kind says the word continues on the next line */
    if (word_end == line_len && ed_line_break(ed, line_idx) == LB_HYPHEN && line_idx + 1 < ed->count)
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

        if (prev_len >= 1 && ed_line_break(ed, line_idx - 1) == LB_HYPHEN)
        {
            prev_word_end = prev_len;
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
    return view_align_indent(align, text_vw, avail);
}

/* Find the bracket partner of the char under the cursor, -1 when none */
static void find_bracket_match(TeApp *app, const EdInfo *info, int *out_row, int *out_col)
{
    static const wchar_t open_set[] = L"([{";
    static const wchar_t close_set[] = L")]}";
    Ed *ed = te_app_get_editor(app);
    const wchar_t *line = NULL;
    wchar_t ch;
    wchar_t partner = 0;
    int dir = 0;
    int depth = 1;
    int row;
    int limit;
    int i;

    *out_row = -1;
    *out_col = -1;
    app->bracket_match_row = -1;
    app->bracket_match_col = -1;

    if (!app->cfg.show_brackets)
        return;

    line = ed_line_wcs(ed, info->row);

    if (!line || info->col >= ed_line_len(ed, info->row))
        return;

    ch = line[info->col];

    for (i = 0; i < 3; i++)
    {
        if (ch == open_set[i])
        {
            partner = close_set[i];
            dir = 1;
        }
        else if (ch == close_set[i])
        {
            partner = open_set[i];
            dir = -1;
        }
    }

    if (!partner)
        return;

    limit = (dir > 0) ? info->row + BRACKET_MATCH_LOOKUP_LIMIT : info->row - BRACKET_MATCH_LOOKUP_LIMIT;

    if (dir > 0 && limit >= info->line_count)
        limit = info->line_count - 1;

    if (dir < 0 && limit < 0)
        limit = 0;

    for (row = info->row; (dir > 0) ? row <= limit : row >= limit; row += dir)
    {
        const wchar_t *rl = ed_line_wcs(ed, row);
        int rl_len = ed_line_len(ed, row);
        int col;

        if (!rl)
            continue;

        col = (row == info->row) ? info->col + dir : ((dir > 0) ? 0 : rl_len - 1);

        for (; col >= 0 && col < rl_len; col += dir)
        {
            if (rl[col] == ch)
            {
                depth++;
            }
            else if (rl[col] == partner)
            {
                depth--;

                if (depth == 0)
                {
                    *out_row = row;
                    *out_col = col;
                    app->bracket_match_row = row;
                    app->bracket_match_col = col;

                    return;
                }
            }
        }
    }
}

/* Paint one sub-row of a logical line: text, syntax, rich attrs and every overlay */
static void paint_segment(PaintCtx *pc, int li, const wchar_t *l, int len, int seg_start, int seg_end, int sr, int first_painted, int *first_seg, SyntaxClass *line_classes)
{
    TeApp *app = pc->app;
    Ed *ed = pc->ed;
    int offset_x = pc->offset_x;
    int offset_y = pc->offset_y;
    int ln_offset = pc->ln_offset;
    int ln_width = pc->ln_width;
    int width = pc->width;
    int body_width = pc->body_width;
    int show_lnum = pc->show_lnum;
    int b_r1 = pc->b_r1;
    int b_c1 = pc->b_c1;
    int b_r2 = pc->b_r2;
    int b_c2 = pc->b_c2;
    EdInfo info;
    int seg_len = seg_end - seg_start;
    int text_vw;
    int x_text_end;
    int x_screen_end;
    int has_more_subrows;

    /* Tab offset restarts at every sub-row */
    int seg_start_vcol = 0;

    /* Alignment indent of this sub-row */
    int align_ind;
    int eff_ln_offset;

    ed_get_info(ed, &info);

    if (seg_len < 0)
        seg_len = 0;

    align_ind = line_align_indent(l ? ed->lines[li]->para_align : 0, seg_len > 0 ? wcs_vwidth_ex(&l[seg_start], seg_len, 0, s_tab_width) : 0, width);
    eff_ln_offset = ln_offset + align_ind;

    /* Line number on first painted sub-row */
    if (show_lnum && *first_seg)
    {
        attrset(COLOR_PAIR(COL_BORDER));
        mvprintw(offset_y + sr, offset_x, "%*d", ln_width - 1, li + 1);
        standend();

        attron(COLOR_PAIR(COL_NORMAL));

        *first_seg = 0;
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
    if (app->cfg.indent_guides && first_painted && seg_len > 0)
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
}

/* Draw editor body */
void ui_editor_draw_body(TeApp *app)
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
    PaintCtx pc;

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
    find_bracket_match(app, &info, &match_row, &match_col);

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

    pc.app = app;
    pc.ed = ed;
    pc.cur_row = info.row;
    pc.offset_x = offset_x;
    pc.offset_y = offset_y;
    pc.ln_offset = ln_offset;
    pc.ln_width = ln_width;
    pc.width = width;
    pc.body_width = body_width;
    pc.show_lnum = show_lnum;
    pc.b_r1 = b_r1;
    pc.b_c1 = b_c1;
    pc.b_r2 = b_r2;
    pc.b_c2 = b_c2;

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
            int s = 0;         /* Sub-row index within line */
            int first_seg = 1; /* First painted sub-row for line number */
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

                if (s >= sub_skip)
                {
                    paint_segment(&pc, li, l, len, seg_start, seg_end, sr, s == sub_skip, &first_seg, line_classes);

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
            sub_skip = 0; /* Subsequent lines start at sub-row 0 */
        }
    }
    else /* HARD-WRAP: 1 logical line == 1 screen row */
    {
        int i;

        for (i = 0; i < body_rows; i++)
        {
            int line_idx = info.top + i;
            const wchar_t *wl = NULL;
            int line_len;
            int first_seg = 1;
            SyntaxClass *line_classes = NULL;

            move(offset_y + i, offset_x);
            clrtoeol();

            if (line_idx >= info.line_count)
                continue;

            wl = ed_line_wcs(ed, line_idx);
            line_len = ed_line_len(ed, line_idx);

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

            /* One segment covering the whole line, so no wrap indicator */
            paint_segment(&pc, line_idx, wl ? wl : L"", line_len, 0, line_len, i, 1, &first_seg, line_classes);

            /* Hard-wrap hyphen is drawn after the text, never stored */
            if (ed_line_break(ed, line_idx) == LB_HYPHEN)
            {
                int hx = pc.offset_x + pc.ln_offset + wcs_vwidth_ex(wl ? wl : L"", line_len, 0, ed_get_tab_width());
                int left = pc.offset_x + pc.ln_offset;
                int right = left + pc.width;

                if (hx >= left && hx < right)
                {
                    attron(COLOR_PAIR(COL_NORMAL));
                    mvaddwstr(pc.offset_y + i, hx, L"-");
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

/* Decode a mouse event from either protocol and dispatch it, 1 = screen changed */
static int handle_mouse_key(TeApp *app, int ch, int is_key, int *handled)
{
    TeWindow *win = NULL;
    int mtype;
    int mx;
    int my;

    *handled = 0;

    if (!is_key)
        return 0;

    if (ch == KEY_MOUSE_SGR)
    {
        *handled = 1;

        if (!parse_sgr_mouse(&mtype, &mx, &my))
            return 0;
    }
    else if (ch == KEY_MOUSE)
    {
        *handled = 1;
#ifdef PLATFORM_AMIGA
        unsigned long m;
#else
        static int s_button_down = 0;
        MEVENT ev;
#endif

#ifdef PLATFORM_AMIGA
        /* Packed by the wrapper: low 8 bits type, next 12 x, next 12 y */
        m = getmouse();

        mtype = (int)(m & 0xFF);
        mx = (int)((m >> 8) & 0xFFF);
        my = (int)((m >> 20) & 0xFFF);

#else
        if (getmouse(&ev) != OK)
            return 0;

        mx = ev.x;
        my = ev.y;

        if (ev.bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED))
        {
            s_button_down = 1;
            mtype = UI_MOUSE_PRESS_LEFT;
        }
        else if (ev.bstate & BUTTON1_RELEASED)
        {
            s_button_down = 0;
            mtype = UI_MOUSE_RELEASE_LEFT;
        }
        else if (ev.bstate & BUTTON4_PRESSED)
        {
            mtype = UI_MOUSE_WHEEL_UP;
        }
        else if (ev.bstate & BUTTON5_PRESSED)
        {
            mtype = UI_MOUSE_WHEEL_DOWN;
        }
        else if (s_button_down && (ev.bstate & REPORT_MOUSE_POSITION))
        {
            mtype = UI_MOUSE_DRAG_LEFT;
        }
        else
        {
            return 0;
        }
#endif
    }
    else
    {
        return 0;
    }

    /* Coordinates arrive screen-absolute, the dispatcher wants them window-local */
    win = wm_get_window_by_type(app->wm, WIN_EDITOR);

    if (win && win->visible)
    {
        my -= win->y;
        mx -= win->x;
    }

    return ui_mouse_dispatch(app, mtype, my, mx);
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

    ui_editor_draw_body(app);

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
    view_set_top(0, 0);
    view_reset_desired();
    view_set_tab_width(app->cfg.tab_width > 0 ? app->cfg.tab_width : 4);

    s_soft_last_width = COLS;

    ed_set_tab_width(view_tab_width());

    TE_BRACKET_PASTE_ON();

    /* Non-blocking input with 1s timeout for autosave */
    if (app->cfg.autosave)
        timeout(1000);
    else
        timeout(-1); /* Fully blocking */

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

            if (now - s_last_autosave_ms >= (unsigned long)app->cfg.autosave_interval * 1000UL)
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

        /* Close whatever the previous key left open, so no edit path can wedge the undo engine by forgetting to reflow */
        ed_undo_settle(te_app_get_editor(app));

        if (!is_key && (ch == KEY_UP || ch == KEY_DOWN || ch == KEY_LEFT || ch == KEY_RIGHT ||
                        ch == KEY_HOME || ch == KEY_END || ch == KEY_PPAGE || ch == KEY_NPAGE ||
                        ch == KEY_BACKSPACE || ch == KEY_DC || ch == KEY_ENTER))
            is_key = 1;

        {
            int mouse_handled;

            if (handle_mouse_key(app, ch, is_key, &mouse_handled))
                screen_dirty = 1;

            if (mouse_handled)
                continue;
        }

        preserve_desired = 0;

        /* Tabs panel navigation mode */
        if (ui_tabs_panel_key(app, ch, is_key, &screen_dirty))
            continue;

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
        else /* Printable / control chars */
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
