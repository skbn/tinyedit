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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include "../core/utf8.h"
#include "../core/charset.h"
#include "../core/clipboard.h"
#include "../core/portable.h"
#include "ui_editor_helper.h"
#include "../components/editor.h"
#include "te.h"
#include "ui_files.h"
#include "ui_spell.h"

#ifdef HAVE_HUNSPELL
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/spell.h"
#else
#include "../spell/spell.h"
#endif
#endif

#if defined(PLATFORM_AMIGA)
#define SIF_MAX_HITS 2000
#else
#define SIF_MAX_HITS 1000000
#endif

#define RECENT_MAX 20

static char *s_recent_files[RECENT_MAX];
static int s_recent_count = 0;

typedef struct
{
    char **paths;  /* full path of each hit */
    int *lines;    /* line number of each hit */
    char **labels; /* "path:line: snippet" for ui_popup_list */
    int count;
    int cap;
    int truncated; /* hit SIF_MAX_HITS limit */
} SifResults;

#ifdef HAVE_HYPHEN

#if defined(PLATFORM_AMIGA)
#include "../spellchecker/hyph.h"
#else
#include "../hyph_wrap/hyph_wrap.h"
#endif
#include "ui_hyph.h"

/* Thunk adapting hyph_breakpoints() to EdHyphenFn. user_data is HyphDict* */
int ui_hyph_thunk(void *user_data, const char *word, int word_len, int *out_pos, int *out_count)
{
    HyphDict *h = (HyphDict *)user_data;
    int cap, n;

    if (!out_count)
        return 0;

    cap = *out_count;

    if (cap > 16)
        cap = 16;

    n = 0;

    if (!hyph_breakpoints(h, word, word_len, out_pos, &n) || n == 0)
    {
        *out_count = 0;
        return 0;
    }

    if (n > cap)
        n = cap;

    *out_count = n;
    return 1;
}

#endif

/* Static variables that need to be accessible */
static int s_soft_vtop = 0;

#ifdef PLATFORM_WIN32
/* Windows wcswidth() does not know East Asian Wide / Fullwidth / emoji /
 * box-drawing / block glyphs. Return 2 for those ranges, fall back otherwise */
static int ed_char_vwidth(wchar_t ch)
{
    unsigned int cp = (unsigned int)ch;
    int w;

    if (cp < 0x1100)
        return 1;

    if (cp >= 0x2500 && cp <= 0x257F)
        return 1; /* Box Drawing - narrow, same as ncurses Linux */

    if ((cp >= 0x1100 && cp <= 0x115F) ||
        (cp >= 0x2190 && cp <= 0x21FF) ||
        (cp >= 0x2329 && cp <= 0x232A) ||
        (cp >= 0x2580 && cp <= 0x259F) ||
        (cp >= 0x25A0 && cp <= 0x25FF) ||
        (cp >= 0x2600 && cp <= 0x26FF) ||
        (cp >= 0x2700 && cp <= 0x27BF) ||
        (cp >= 0x2B00 && cp <= 0x2BFF) ||
        (cp >= 0x2E80 && cp <= 0x303E) ||
        (cp >= 0x3041 && cp <= 0x33FF) ||
        (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x4E00 && cp <= 0x9FFF) ||
        (cp >= 0xA000 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE4F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        cp >= 0x1F000)
        return 2;

    w = wcswidth(&ch, 1);

    return (w > 0) ? w : 1;
}

#define CHAR_VWIDTH(c) ed_char_vwidth(c)
#else
#define CHAR_VWIDTH(c) ((wcswidth(&(c), 1) > 0) ? wcswidth(&(c), 1) : 1)
#endif

/* Visual width in display columns: wide chars=2, narrow=1, control=1 */
int wcs_vwidth(const wchar_t *s, int n)
{
    int v = 0;
    int i;

    if (!s || n <= 0)
        return 0;

    for (i = 0; i < n; i++)
        v += CHAR_VWIDTH(s[i]);

    return v;
}

/* Draw wide string with tab expansion */
void ui_draw_wcs_line_with_tabs(int y, int x, const wchar_t *s, int n, int tab_width)
{
    int i;
    int col = 0;
    int out_len = 0;
    wchar_t buf[4096];
    int max_len = (int)(sizeof(buf) / sizeof(buf[0]));

    if (!s || n <= 0 || tab_width < 1)
        return;

    if (n > max_len / 2)
        n = max_len / 2;

    for (i = 0; i < n && out_len < max_len - 1; i++)
    {
        if (s[i] == L'\t')
        {
            int w = tab_width - (col % tab_width);
            int j;

            for (j = 0; j < w && out_len < max_len - 1; j++)
            {
                buf[out_len] = L' ';
                out_len++;
            }

            col += w;
        }
        else
        {
            int w = CHAR_VWIDTH(s[i]);

            buf[out_len] = s[i];
            out_len++;
            col += w;
        }
    }

    mvaddnwstr(y, x, buf, out_len);

    attrset(COLOR_PAIR(COL_NORMAL));
}

/* Draw wide string with tab expansion and per-character syntax colors */
void ui_draw_wcs_line_with_tabs_and_colors(int y, int x, const wchar_t *s, int n, int tab_width, const SyntaxClass *classes, int start_col)
{
    int i;
    int col = start_col;
    int out_len = 0;
    int buf_start_col = start_col;
    int current_color = -1;
    wchar_t buf[4096];
    int max_len = (int)(sizeof(buf) / sizeof(buf[0]));

    if (!s || n <= 0 || tab_width < 1)
        return;

    if (n > max_len / 2)
        n = max_len / 2;

    for (i = 0; i <= n; i++)
    {
        int color = (i < n) ? ui_syntax_color_pair(classes[i]) : -1;

        if (i == n || color != current_color)
        {
            if (out_len > 0)
            {
                int draw_x = x + (buf_start_col - start_col);

                attrset(COLOR_PAIR(current_color));
                mvaddnwstr(y, draw_x, buf, out_len);
                attrset(COLOR_PAIR(COL_NORMAL));

                out_len = 0;
            }

            if (i < n)
            {
                current_color = color;
                buf_start_col = col;
            }
        }

        if (i < n)
        {
            if (s[i] == L'\t')
            {
                int w = tab_width - (col % tab_width);
                int j;

                for (j = 0; j < w && out_len < max_len - 1; j++)
                    buf[out_len++] = L' ';

                col += w;
            }
            else
            {
                int w = CHAR_VWIDTH(s[i]);

                buf[out_len++] = s[i];
                col += w;
            }
        }
    }

    attrset(COLOR_PAIR(COL_NORMAL));
}

/* Visual width with tab-stop support */
int wcs_vwidth_ex(const wchar_t *s, int n, int start_col, int tab_width)
{
    int v = 0;
    int col = start_col;
    int i;

    if (!s || n <= 0 || tab_width < 1)
        return 0;

    for (i = 0; i < n; i++)
    {
        if (s[i] == L'\t')
        {
            int w = tab_width - (col % tab_width);

            v += w;
            col += w;
        }
        else
        {
            int w = CHAR_VWIDTH(s[i]);

            v += w;
            col += w;
        }
    }

    return v;
}

/* Left margin for editor body with line numbers */
int editor_body_offset(const TeApp *app, int line_count)
{
    int margin = 1;
    int tab_width;

    if (!app || !te_app_get_show_line_numbers(app))
        return 0;

    if (line_count <= 0)
        line_count = 1;

    while (line_count >= 10)
    {
        line_count /= 10;
        margin++;
    }

    margin += 1; /* space after the number */

    tab_width = app->cfg.tab_width > 0 ? app->cfg.tab_width : 4;

    /* Round up to the next multiple of tab_width */
    margin = ((margin + tab_width - 1) / tab_width) * tab_width;

    return margin;
}

void clear_search_highlights(TeApp *app)
{
    if (app->search.rows)
    {
        free(app->search.rows);
        app->search.rows = NULL;
    }

    if (app->search.cols)
    {
        free(app->search.cols);
        app->search.cols = NULL;
    }

    app->search.count = 0;
    app->search.is_mode = 0;
    app->search.only_mode = 0;
    app->search.current_match = 0;
    app->search.match_current = 0;
    app->search.match_total = 0;
}

/* Navigate to previous match, wraps at start */
int search_prev(TeApp *app)
{
    int match_row;
    int match_col;

    if (!app->search.rows || app->search.count == 0)
        return 0;

    app->search.current_match = (app->search.current_match - 1 + app->search.count) % app->search.count;
    app->search.match_current = app->search.current_match + 1;

    match_row = app->search.rows[app->search.current_match];
    match_col = app->search.cols[app->search.current_match];

    ed_set_pos(te_app_get_editor(app), match_row, match_col);
    ed_ensure_visible(te_app_get_editor(app));

    return 1;
}

/* Navigate to next match, wraps at end */
int search_next(TeApp *app)
{
    int match_row;
    int match_col;

    if (!app->search.rows || app->search.count == 0)
        return 0;

    app->search.current_match = (app->search.current_match + 1) % app->search.count;
    app->search.match_current = app->search.current_match + 1;

    match_row = app->search.rows[app->search.current_match];
    match_col = app->search.cols[app->search.current_match];

    ed_set_pos(te_app_get_editor(app), match_row, match_col);
    ed_ensure_visible(te_app_get_editor(app));

    return 1;
}

static int do_replace(TeApp *app, const wchar_t *needle, const wchar_t *repl)
{
    int count = 0;
    int nlen = (int)wcslen(needle);
    int rlen = (int)wcslen(repl);
    int *rows = NULL, *cols = NULL;
    int match_count, i;

    /* Find all matches with current options */
    match_count = ed_search_all_custom(te_app_get_editor(app), needle, app->search.case_sensitive, app->search.whole_word, &rows, &cols);

    if (match_count == 0)
        return 0;

    /* Replace from end to start to avoid shifts */
    for (i = match_count - 1; i >= 0; i--)
    {
        int j;

        ed_set_pos(te_app_get_editor(app), rows[i], cols[i]);

        /* Delete needle */
        for (j = 0; j < nlen; j++)
            ed_delete(te_app_get_editor(app));

        /* Insert replacement */
        for (j = 0; j < rlen; j++)
            ed_insert_char(te_app_get_editor(app), repl[j]);

        count++;
    }

    free(rows);
    free(cols);

    return count;
}

int replace(TeApp *app)
{
    wchar_t needle[64], repl[64];
    int case_sensitive = app->search.case_sensitive;
    int whole_word = app->search.whole_word;
    int *rows = NULL, *cols = NULL;
    int match_count;

    wcsncpy(needle, app->search.query, 63);
    needle[63] = L'\0';

    wcsncpy(repl, app->search.last_replace, 63);
    repl[63] = L'\0';

    if (ui_popup_replace(needle, repl, needle, 64, repl, 64, &case_sensitive, &whole_word) != 0 || !needle[0])
        return 1;

    wcsncpy(app->search.query, needle, 63);
    app->search.query[63] = L'\0';

    wcsncpy(app->search.last_replace, repl, 63);
    app->search.last_replace[63] = L'\0';

    app->search.case_sensitive = case_sensitive;
    app->search.whole_word = whole_word;

    /* Perform search and highlight matches */
    match_count = ed_search_all_custom(te_app_get_editor(app), app->search.query, case_sensitive, whole_word, &rows, &cols);

    clear_search_highlights(app);

    if (match_count > 0)
    {
        app->search.rows = rows;
        app->search.cols = cols;
        app->search.count = match_count;
        app->search.is_mode = 1;
        app->search.current_match = 0;
        app->search.match_current = 1;
        app->search.match_total = match_count;

        /* Move cursor to first match and ensure visible */
        ed_set_pos(te_app_get_editor(app), rows[0], cols[0]);
        ed_ensure_visible(te_app_get_editor(app));
    }
    else
    {
        free(rows);
        free(cols);

        app->search.is_mode = 0;
        te_status(app, "No matches found");
    }

    return 1;
}

int replace_current(TeApp *app)
{
    int match_row;
    int match_col;
    int nlen;
    int rlen;
    int i;
    int *new_rows = NULL;
    int *new_cols = NULL;
    int new_match_count;

    if (app->search.last_replace[0] != L'\0')
    {
        /* Use last replacement text - no popup */
        wchar_t repl[64];
        wcsncpy(repl, app->search.last_replace, 63);
        repl[63] = L'\0';

        /* Move cursor to current match position */
        if (!app->search.rows || app->search.count == 0)
            return 0;

        match_row = app->search.rows[app->search.current_match];
        match_col = app->search.cols[app->search.current_match];

        ed_set_pos(te_app_get_editor(app), match_row, match_col);

        /* Save undo state */
        ed_save_undo(te_app_get_editor(app));

        /* Replace current occurrence */
        nlen = (int)wcslen(app->search.query);
        rlen = (int)wcslen(repl);

        /* Delete search text */
        for (i = 0; i < nlen; i++)
            ed_delete(te_app_get_editor(app));

        /* Insert replacement */
        for (i = 0; i < rlen; i++)
            ed_insert_char(te_app_get_editor(app), repl[i]);

        /* Update search results after text change */
        free(app->search.rows);
        free(app->search.cols);

        app->search.rows = NULL;
        app->search.cols = NULL;
        app->search.count = 0;

        /* Re-run search to find remaining matches */
        new_rows = NULL;
        new_cols = NULL;
        new_match_count = ed_search_all_custom(te_app_get_editor(app), app->search.query, app->search.case_sensitive, app->search.whole_word, &new_rows, &new_cols);

        if (new_match_count > 0 && new_rows && new_cols)
        {
            int i;
            int next_idx = 0;

            app->search.rows = new_rows;
            app->search.cols = new_cols;
            app->search.count = new_match_count;
            app->search.match_current = 1;
            app->search.match_total = new_match_count;

            /* Find first match after replacement position */
            for (i = 0; i < new_match_count; i++)
            {
                if (new_rows[i] > match_row || (new_rows[i] == match_row && new_cols[i] > match_col))
                {
                    next_idx = i;
                    break;
                }
            }

            app->search.current_match = next_idx;

            /* Move cursor to next match */
            ed_set_pos(te_app_get_editor(app), new_rows[next_idx], new_cols[next_idx]);
            ed_ensure_visible(te_app_get_editor(app));

            te_status(app, "Replaced. %d match(es) remaining", new_match_count);
        }
        else
        {
            if (new_rows)
                free(new_rows);

            if (new_cols)
                free(new_cols);

            app->search.is_mode = 0;
            app->search.match_total = 0;

            te_status(app, "Replaced 1 occurrence (no more matches)");
        }
        return 1;
    }

    if (app->search.last_replace[0] == L'\0')
        te_status(app, "No replacement text set (use Ctrl+R first)");

    return 0;
}

int replace_all(TeApp *app)
{
    if (app->search.is_mode && app->search.count > 0)
    {
        char msg[128];
        int n;

        snprintf(msg, sizeof(msg), "Replace all %d occurrences?", app->search.count);

        if (ui_popup_confirm("Replace All", msg) == 1)
        {
            ed_save_undo(te_app_get_editor(app));
            n = do_replace(app, app->search.query, app->search.last_replace);

            clear_search_highlights(app);

            te_status(app, "Replaced %d occurrence(s)", n);
        }
        else
        {
            te_status(app, "Replace all cancelled");
        }
    }
    else
    {
        te_status(app, "No search active (use F5/Alt+S first)");
    }

    return 1;
}

int do_search(TeApp *app)
{
    wchar_t tmp[64];
    int *rows = NULL, *cols = NULL;
    int match_count;
    int choice;

    wcsncpy(tmp, app->search.query, 63);
    tmp[63] = L'\0';

    if (ui_popup_input_wcs("Search", "Text:", tmp, 64) != 0 || !tmp[0])
        return 1;

    wcsncpy(app->search.query, tmp, 63);
    app->search.query[63] = L'\0';

    /* Find all matches (no limit) */
    match_count = ed_search_all(te_app_get_editor(app), app->search.query, &rows, &cols);

    /* Free previous search matches */
    clear_search_highlights(app);

    if (match_count == 0)
    {
        free(rows);
        free(cols);
        te_status(app, "Not found");
        return 1;
    }

    if (match_count == 1)
    {
        /* Single match: jump directly */
        ed_set_pos(te_app_get_editor(app), rows[0], cols[0]);
        ed_ensure_visible(te_app_get_editor(app));
        te_status(app, "Found at line %d", rows[0] + 1);

        /* Save matches for highlighting and activate search mode */
        app->search.rows = rows;
        app->search.cols = cols;
        app->search.count = match_count;
        app->search.only_mode = 1;
        app->search.current_match = 0;
        app->search.match_current = 1;
        app->search.match_total = match_count;

        return 1;
    }

    /* Multiple matches: show picker popup for user choice */
    choice = ui_popup_search_results_popup(app, app->search.query, rows, cols, match_count);

    if (choice >= 0)
    {
        ed_set_pos(te_app_get_editor(app), rows[choice], cols[choice]);
        ed_ensure_visible(te_app_get_editor(app));
        te_status(app, "Jumped to line %d", rows[choice] + 1);

        app->search.current_match = choice;
        app->search.match_current = choice + 1;
    }
    else
    {
        /* User cancelled: keep matches armed for F3/F4, leave cursor in place */
        te_status(app, "Search cancelled");

        app->search.current_match = 0;
        app->search.match_current = 1;
    }

    /* Save matches for highlighting, activate search mode (ownership transfers) */
    app->search.rows = rows;
    app->search.cols = cols;
    app->search.count = match_count;
    app->search.only_mode = 1;
    app->search.match_total = match_count;

    return 1;
}

int insert_file(TeApp *app)
{
    char path[512], in_cs[32];
    path[0] = '\0';
    in_cs[0] = '\0';

    if (ui_files_pick("Insert file", NULL, path, sizeof(path)) != 0)
        return 1;

    ui_popup_charset("Input charset (empty=UTF-8)", "", in_cs, sizeof(in_cs));

    if (ed_load_file_at_cursor(te_app_get_editor(app), path, in_cs[0] ? in_cs : NULL) == 0)
    {
        clear_search_highlights(app);
        te_status(app, "Inserted: %s", path);
    }

    return 1;
}

/* Detect loaded wrap-hyphens using the active spell checker */
int ui_editor_detect_wrap_hyphens(TeApp *app)
{
#ifdef HAVE_HUNSPELL
    Ed *ed = NULL;
    EdLine *ln = NULL;
    EdLine *next = NULL;
    int i, j, k;
    int detected = 0;
    int first_start, first_end, next_start, next_end;
    int combined_len;
    int spell_ok;
    char *word_utf8 = NULL;
    wchar_t combined[512];

    if (!app)
        return 0;

    ed = te_app_get_editor(app);

    if (!ed)
        return 0;

    if (!app->spell_handle)
        return 0;

    if (!app->cfg.hyph_detect_on_load)
        return 0;

    for (i = 0; i < ed->count - 1; i++)
    {
        ln = ed->lines[i];
        next = ed->lines[i + 1];

        if (ln->len <= 0 || ln->wcs[ln->len - 1] != L'-')
            continue;

        if (!next || next->len <= 0)
            continue;

        /* Last word before the trailing hyphen */
        first_end = ln->len - 1;
        first_start = first_end - 1;

        while (first_start >= 0 && !iswspace(ln->wcs[first_start]))
            first_start--;

        first_start++;

        /* Strip leading punctuation from the first word */
        while (first_start < first_end && iswpunct(ln->wcs[first_start]))
            first_start++;

        if (first_start >= first_end)
            continue;

        /* First word on the next line */
        next_start = 0;

        while (next_start < next->len && iswspace(next->wcs[next_start]))
            next_start++;

        if (next_start >= next->len)
            continue;

        next_end = next_start + 1;

        while (next_end < next->len && !iswspace(next->wcs[next_end]))
            next_end++;

        /* Strip leading and trailing punctuation from the next word */
        while (next_start < next_end && iswpunct(next->wcs[next_start]))
            next_start++;

        while (next_end > next_start && iswpunct(next->wcs[next_end - 1]))
            next_end--;

        if (next_start >= next_end)
            continue;

        /* Build combined word from both halves */
        combined_len = 0;

        for (j = first_start; j < first_end && combined_len < 511; j++)
            combined[combined_len++] = ln->wcs[j];

        for (k = next_start; k < next_end && combined_len < 511; k++)
            combined[combined_len++] = next->wcs[k];

        combined[combined_len] = L'\0';

        if (combined_len < 4 || combined_len >= 256)
            continue;

        word_utf8 = wcs_to_utf8(combined, combined_len);
        spell_ok = (word_utf8 && spell_check(app->spell_handle, word_utf8));
        free(word_utf8);
        word_utf8 = NULL;

        /* If the joined word is valid, the hyphen was probably a wrap-hyphen */
        if (spell_ok)
        {
            ln->has_wrap_hyphen = 1;
            detected++;
        }
    }

    return detected;
#else
    return 0;
#endif
}

int toggle_spell_panel(TeApp *app)
{
    /* Cycle: -1 (hidden) -> 0 (spell) -> 2 (dict) -> -1 (skip empty translate panel) */
    if (app->spell_panel_mode < 0 || app->spell_panel_mode >= 2)
        app->spell_panel_mode = (app->spell_panel_mode < 0) ? 0 : -1;
    else
    {
        if (app->spell_panel_mode == 0)
            app->spell_panel_mode = 2; /* Skip translate panel (empty) */
        else
            app->spell_panel_mode = -1;
    }

    switch (app->spell_panel_mode)
    {
    case 0:
        te_status(app, "Spell checker panel");
        break;
    case 2:
        te_status(app, "Dictionary panel");
        break;
    default:
        te_status(app, "Side panel hidden");
        break;
    }

    return 1;
}

/* Control key implementations */
int paste(TeApp *app)
{
    /* On Amiga/Windows, always use external clipboard. On Unix, use external only if not in SSH session */
    if (clipboard_use_external())
    {
        char *clip = clipboard_paste();

        if (clip && clip[0])
        {
            char *wrapped = NULL;
            const char *to_insert = clip;
            int pasted = 0;

            /* Pre-wrap pasted text in hard-wrap mode; soft-wrap inserts verbatim */
            if (app->hard_wrap)
            {
                int pw = editor_eff_wrap(app);

                if (pw > 0)
                {
                    wrapped = wrap_paste_text(app, clip, pw);

                    if (wrapped)
                        to_insert = wrapped;
                }
            }

            ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));
            ed_paste_text_with_undo(te_app_get_editor(app), to_insert);

            clear_search_highlights(app);
            soft_reset_desired();

            s_soft_vtop = 0;
            te_status(app, "Pasted from clipboard");

            ed_auto_rewrap_after_edit(app);
            ed_ensure_visible(te_app_get_editor(app));

            free(wrapped);
            free(clip);
        }
        else
        {
            te_status(app, "Clipboard: empty or no backend (install xclip/wl-clipboard, or check clipboard.device)");
            free(clip);
        }
    }
    else
    {
        /* SSH/headless: use internal block only */
        ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));

        if (ed_block_paste(te_app_get_editor(app)) == 0)
        {
            clear_search_highlights(app);
            soft_reset_desired();

            s_soft_vtop = 0;
            te_status(app, "Pasted (internal block)");

            ed_auto_rewrap_after_edit(app);
            ed_ensure_visible(te_app_get_editor(app));
        }
        else
        {
            Ed *ed = te_app_get_editor(app);

            free(ed->auto_rewrap_pre_snapshot);

            ed->auto_rewrap_pre_snapshot = NULL;
            te_status(app, "No internal block to paste (external clipboard unavailable in SSH)");
        }
    }

    return 1;
}

int ui_editor_goto_line(TeApp *app)
{
    wchar_t buf[16];
    buf[0] = L'\0';

    if (ui_popup_input_wcs("Go to line", "Line number:", buf, sizeof(buf) / sizeof(wchar_t)) == 0 && buf[0])
    {
        int n;
#ifdef PLATFORM_AMIGA
        /* wcstol is broken on AmigaOS: convert to char* first, then use strtol */
        char char_buf[16];

        wcstombs(char_buf, buf, sizeof(char_buf));
        char_buf[sizeof(char_buf) - 1] = '\0';

        n = (int)strtol(char_buf, NULL, 10);
#else
        n = (int)wcstol(buf, NULL, 10);
#endif

        if (n >= 1)
        {
            ed_goto_line(te_app_get_editor(app), n - 1);
            ed_ensure_visible(te_app_get_editor(app));

            /* In soft-wrap mode, reset viewport to cursor to avoid slow walking */
            if (!app->hard_wrap)
                soft_reset_viewport_to_cursor(app, COLS);
        }
    }

    return 1;
}

int copy(TeApp *app)
{
    EdInfo info;
    Ed *ed = te_app_get_editor(app);
    ed_get_info(ed, &info);

    if (info.block.active)
    {
        char *utf8 = ed_block_get_utf8(ed);

        if (ed_block_copy(ed) == 0)
        {
            /* Copy to external clipboard if available */
            if (clipboard_use_external() && utf8)
            {
                if (clipboard_copy(utf8) == 0)
                {
                    te_status(app, "Block copied to clipboard");
                }
                else
                {
                    /* External clipboard failed: free internal killbuf so the large block does not sit unused in memory until exit */
                    free(ed->killbuf);

                    ed->killbuf = NULL;
                    ed->killlen = 0;

                    te_status(app, "Clipboard copy failed; internal block freed");
                }
            }
            else
            {
                te_status(app, "Block copied (internal only)");
            }
        }

        if (utf8)
            free(utf8);
    }

    return 1;
}

int cut(TeApp *app)
{
    EdInfo info;
    ed_get_info(te_app_get_editor(app), &info);

    if (info.block.active)
    {
        char *utf8 = ed_block_get_utf8(te_app_get_editor(app));

        if (ed_block_cut(te_app_get_editor(app)) == 0)
        {
            ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));
            clear_search_highlights(app);

            /* Copy to external clipboard if available */
            if (clipboard_use_external() && utf8)
            {
                clipboard_copy(utf8);
                te_status(app, "Block cut to clipboard");
            }
            else
            {
                te_status(app, "Block cut (internal only)");
            }

            ed_auto_rewrap_after_edit(app);
        }

        if (utf8)
            free(utf8);
    }

    return 1;
}

int ui_editor_goto_start(TeApp *app)
{
    EdInfo info;
    ed_get_info(te_app_get_editor(app), &info);

    if (info.block.active)
    {
        /* Extend selection to start of document */
        ed_set_pos(te_app_get_editor(app), 0, 0);
        ed_ensure_visible(te_app_get_editor(app));

        /* In soft-wrap mode, reset viewport to cursor to avoid slow walking */
        if (!app->hard_wrap)
            soft_reset_viewport_to_cursor(app, COLS);
    }
    else
    {
        /* Move to start without selection */
        ed_set_pos(te_app_get_editor(app), 0, 0);
        ed_ensure_visible(te_app_get_editor(app));

        /* In soft-wrap mode, reset viewport to cursor to avoid slow walking */
        if (!app->hard_wrap)
            soft_reset_viewport_to_cursor(app, COLS);
    }

    return 1;
}

int ui_editor_goto_end(TeApp *app)
{
    EdInfo info;
    ed_get_info(te_app_get_editor(app), &info);

    if (info.line_count > 0)
    {
        int ll = info.line_count - 1;

        if (info.block.active)
        {
            /* Extend selection to end of document */
            ed_set_pos(te_app_get_editor(app), ll, ed_line_len(te_app_get_editor(app), ll));
            ed_ensure_visible(te_app_get_editor(app));

            /* In soft-wrap mode, reset viewport to cursor to avoid slow walking */
            if (!app->hard_wrap)
                soft_reset_viewport_to_cursor(app, COLS);
        }
        else
        {
            /* Move to end without selection */
            ed_set_pos(te_app_get_editor(app), ll, ed_line_len(te_app_get_editor(app), ll));
            ed_ensure_visible(te_app_get_editor(app));

            /* In soft-wrap mode, reset viewport to cursor to avoid slow walking */
            if (!app->hard_wrap)
                soft_reset_viewport_to_cursor(app, COLS);
        }
    }

    return 1;
}

int ui_editor_export(TeApp *app)
{
    EdInfo info;
    ed_get_info(te_app_get_editor(app), &info);

    if (!info.block.active)
    {
        te_status(app, "No block marked");
    }
    else
    {
        char path[256], out_cs[32];
        path[0] = '\0';
        out_cs[0] = '\0';

        if (ui_files_save("Export block", NULL, "block_export.txt", path, sizeof(path)) != 0)
            return 1;

        ui_popup_charset("Output charset (empty=UTF-8)", "", out_cs, sizeof(out_cs));

        if (ed_export_block_to_file(te_app_get_editor(app), path, out_cs[0] ? out_cs : NULL) == 0)
            te_status(app, "Block written: %s", path);
        else
            te_status(app, "Cannot write: %s", path);
    }

    return 1;
}

#ifdef HAVE_HYPHEN
static int ftn_reply_hyph_cb(void *user_data, const wchar_t *word, int word_wlen, int col_limit)
{
    TeApp *app = (TeApp *)user_data;

    return hyph_find_break(app, word, word_wlen, col_limit);
}
#endif

int ftn_reply(TeApp *app)
{
    Ed *ed = te_app_get_editor(app);
    int col = editor_eff_wrap(app);
    int rc;
    int (*hyph_cb)(void *, const wchar_t *, int, int) = NULL;
    void *hyph_data = NULL;

    if (col <= 0)
        col = (app->wrap_col > 0) ? app->wrap_col : 75;

    rc = ed_rewrap_ftn_reply(ed, col);

    if (rc == 0)
    {
        clear_search_highlights(app);
        te_status(app, "FTN reply rewrapped");
        ed_ensure_visible(ed);
        return 1;
    }

#ifdef HAVE_HYPHEN
    if (app->hyph_wrap_enabled && app->hyph_handle)
    {
        hyph_cb = ftn_reply_hyph_cb;
        hyph_data = app;
    }
#endif

    rc = ed_rewrap_paragraph_ex(ed, col, hyph_cb, hyph_data);

    clear_search_highlights(app);

    if (rc == 0)
        te_status(app, "Paragraph rewrapped");
    else
        te_status(app, "Nothing to rewrap");

    ed_ensure_visible(ed);

    return 1;
}

/* Additional helper functions */
int charset_select(TeApp *app)
{
    char new_view[32], new_save[32];
    EdInfo info;
    int view_changed = 0;

    strncpy(new_view, app->charset_in, sizeof(new_view) - 1);
    new_view[sizeof(new_view) - 1] = '\0';

    strncpy(new_save, app->charset_out, sizeof(new_save) - 1);
    new_save[sizeof(new_save) - 1] = '\0';

    if (ui_popup_charset_pair(new_view, new_save, new_view, sizeof(new_view), new_save, sizeof(new_save)) == 0)
    {
        /* Check if view charset changed */
        if (strcasecmp(new_view, app->charset_in) != 0)
            view_changed = 1;

        /* If view charset changed and editor has unsaved changes, warn user */
        if (view_changed)
        {
            ed_get_info(te_app_get_editor(app), &info);

            if (info.modified && te_app_get_filename(app)[0])
            {
                if (ui_popup_confirm("Charset", "Unsaved changes: changing charset will reload from disk and lose edits. Continue?") != 1)
                    return 1; /* User cancelled */
            }
        }

        /* Apply charset changes */
        strncpy(app->charset_in, new_view, sizeof(app->charset_in) - 1);
        app->charset_in[sizeof(app->charset_in) - 1] = '\0';

        strncpy(app->charset_out, new_save, sizeof(app->charset_out) - 1);
        app->charset_out[sizeof(app->charset_out) - 1] = '\0';

        /* If view charset changed and we have a file, reload from disk */
        if (view_changed && te_app_get_filename(app)[0])
        {
            FILE *fp = NULL;
            long size;
            char *buf = NULL;
            size_t r;

            fp = fopen(te_app_get_filename(app), "rb");
            if (fp)
            {
                fseek(fp, 0, SEEK_END);
                size = ftell(fp);
                fseek(fp, 0, SEEK_SET);

                if (size >= 0)
                {
                    buf = (char *)malloc((size_t)size + 1);

                    if (buf)
                    {
                        r = fread(buf, 1, (size_t)size, fp);
                        buf[r] = '\0';

                        /* Convert to UTF-8 using new charset_in */
                        if (new_view[0] && strcasecmp(new_view, "UTF-8") != 0 && strcasecmp(new_view, "UTF8") != 0)
                        {
                            size_t outsz = r * 4 + 16;
                            char *utf8 = (char *)malloc(outsz);

                            if (utf8)
                            {
                                int wrote = charset_body_to_utf8(new_view, buf, (int)r, utf8, (int)outsz);

                                if (wrote >= 0)
                                {
                                    utf8[wrote] = '\0';

                                    ed_clear_undo_redo(te_app_get_editor(app));
                                    ed_load(te_app_get_editor(app), utf8);
                                }

                                free(utf8);
                            }
                        }
                        else
                        {
                            ed_clear_undo_redo(te_app_get_editor(app));
                            ed_load(te_app_get_editor(app), buf);
                        }

                        free(buf);
                    }
                }

                fclose(fp);
            }
        }

        te_status(app, "View: %s  Save: %s", new_view[0] ? new_view : "UTF-8", new_save[0] ? new_save : "UTF-8");
    }

    return 1;
}

static int sif_grow(SifResults *r)
{
    int nc = r->cap ? r->cap * 2 : 64;
    char **np = NULL;
    char **nl = NULL;
    int *ni = NULL;

    if (nc > SIF_MAX_HITS)
        nc = SIF_MAX_HITS;

    if (nc == r->cap)
        return -1;

    np = (char **)realloc(r->paths, (size_t)nc * sizeof(char *));

    if (!np)
        return -1;

    r->paths = np;

    nl = (char **)realloc(r->labels, (size_t)nc * sizeof(char *));

    if (!nl)
        return -1;

    r->labels = nl;

    ni = (int *)realloc(r->lines, (size_t)nc * sizeof(int));

    if (!ni)
        return -1;

    r->lines = ni;

    r->cap = nc;
    return 0;
}

static int sif_cb(void *user, const char *path, int line_no, const char *line_text)
{
    SifResults *r = (SifResults *)user;
    char label[512];
    const char *base = NULL;
    const char *p = NULL;
    char *trimmed_text;

    if (r->count >= SIF_MAX_HITS)
    {
        r->truncated = 1;
        return 1; /* stop walk */
    }

    if (r->count >= r->cap)
    {
        if (sif_grow(r) != 0)
            return 1;
    }

    /* Strip leading whitespace from the snippet for readability */
    p = line_text;

    while (*p == ' ' || *p == '\t')
        p++;

    trimmed_text = (char *)p; /* alias, no allocation */

    /* Use just the basename in the label to keep it short */
    base = strrchr(path, '/');

    if (!base)
        base = strrchr(path, '\\');

    base = base ? base + 1 : path;

    snprintf(label, sizeof(label), "%s:%d: %.200s", base, line_no, trimmed_text);

    r->paths[r->count] = strdup(path);
    r->labels[r->count] = strdup(label);
    r->lines[r->count] = line_no;

    if (!r->paths[r->count] || !r->labels[r->count])
    {
        free(r->paths[r->count]);
        free(r->labels[r->count]);
        return 1; /* OOM: stop */
    }

    r->count++;
    return 0;
}

static void sif_free(SifResults *r)
{
    int i;

    for (i = 0; i < r->count; i++)
    {
        free(r->paths[i]);
        free(r->labels[i]);
    }

    free(r->paths);
    free(r->labels);
    free(r->lines);

    memset(r, 0, sizeof(*r));
}

int do_search_in_files(TeApp *app)
{
    wchar_t ext_w[64];
    wchar_t query_w[128];
    char ext_buf[64];
    char query_buf[128];
    char root_dir[1024];
    const char *fn = NULL;
    SifResults res;
    int total;
    char *tmp = NULL;
    const char *src = NULL;
    char *dst = NULL;
    const char **items = NULL;
    int choice;
    char title[80];

    if (!app)
        return 1;

    /* Ask for extensions to scan (blank = all files) */
    wcsncpy(ext_w, L"*.c,*.h,*.txt,*.md", sizeof(ext_w) / sizeof(wchar_t) - 1);
    ext_w[sizeof(ext_w) / sizeof(wchar_t) - 1] = L'\0';

    if (ui_popup_input_wcs("Find in files", "Extensions (comma-separated, blank=all):", ext_w, sizeof(ext_w) / sizeof(wchar_t)) != 0)
        return 1;

    /* Convert extensions to UTF-8 and strip '*' wildcards */
    tmp = wcs_to_utf8(ext_w, (int)wcslen(ext_w));
    dst = ext_buf;

    if (!tmp)
    {
        te_status(app, "Memory error");
        return 1;
    }

    src = tmp;

    while (*src && (size_t)(dst - ext_buf) < sizeof(ext_buf) - 1)
    {
        if (*src == '*')
        {
            src++;
            continue;
        }

        *dst++ = *src++;
    }

    *dst = '\0';

    free(tmp);

    /* Ask for the search term */
    query_w[0] = L'\0';

    if (ui_popup_input_wcs("Find in files", "Term:", query_w, sizeof(query_w) / sizeof(wchar_t)) != 0 || !query_w[0])
        return 1;

    tmp = wcs_to_utf8(query_w, (int)wcslen(query_w));

    if (!tmp)
    {
        te_status(app, "Memory error");
        return 1;
    }

    strncpy(query_buf, tmp, sizeof(query_buf) - 1);
    query_buf[sizeof(query_buf) - 1] = '\0';

    free(tmp);

    /* Pick root directory from current file's dir or cwd */
    root_dir[0] = '\0';

    fn = te_app_get_filename(app);

    if (fn && fn[0])
    {
        const char *last_slash = strrchr(fn, '/');

        if (!last_slash)
            last_slash = strrchr(fn, '\\');

        if (last_slash)
        {
            size_t dir_len = (size_t)(last_slash - fn);

            if (dir_len < sizeof(root_dir))
            {
                memcpy(root_dir, fn, dir_len);
                root_dir[dir_len] = '\0';
            }
        }
    }

    if (!root_dir[0])
        strncpy(root_dir, ".", sizeof(root_dir) - 1);

    /* Run grep, accumulating hits into res */
    memset(&res, 0, sizeof(res));

    te_status(app, "Searching in %s ...", root_dir);
    refresh();

    total = pf_grep_files(root_dir, ext_buf, query_buf, 5, sif_cb, &res);

    if (total < 0 || res.count == 0)
    {
        sif_free(&res);

        te_status(app, "No matches for '%s'", query_buf);
        return 1;
    }

    /* Present results */
    items = (const char **)res.labels;

    if (res.truncated)
        snprintf(title, sizeof(title), "Find in files (%d, truncated)", res.count);
    else
        snprintf(title, sizeof(title), "Find in files (%d)", res.count);

    choice = ui_popup_list(title, items, res.count, 0);

    if (choice >= 0 && choice < res.count)
    {
        char chosen_path[1024];
        int chosen_line = res.lines[choice];
        TeTab *new_tab = NULL;

        strncpy(chosen_path, res.paths[choice], sizeof(chosen_path) - 1);
        chosen_path[sizeof(chosen_path) - 1] = '\0';

        sif_free(&res); /* free before opening the new tab */

        /* Open the file in a new tab and jump to the line */
        new_tab = te_tab_new();

        if (!new_tab)
        {
            te_status(app, "Memory error opening tab");
            return 1;
        }

        ed_set_word_move_mode(new_tab->editor, app->cfg.word_move_mode);
        te_app_add_tab(app, new_tab);
        te_app_switch_tab(app, app->tab_count - 1);

        if (ui_files_open_path(app, chosen_path) != 0)
        {
            te_app_close_tab(app, app->tab_count - 1);
            return 1;
        }

        ed_set_undo_levels(te_app_get_editor(app), app->cfg.undo_levels);
        ed_set_hard_wrap(te_app_get_editor(app), app->cfg.hard_wrap);

        /* Jump to the hit line (1-based -> 0-based) */
        if (chosen_line > 0)
            ed_set_pos(te_app_get_editor(app), chosen_line - 1, 0);

        ed_ensure_visible(te_app_get_editor(app));
        te_status(app, "Opened %s at line %d", chosen_path, chosen_line);

        return 1;
    }

    sif_free(&res);

    return 1;
}

/* Remove the autosave swap companion for a file */
void ui_editor_swp_remove(const char *path)
{
    pf_remove_swp(path);
}

/* Check for a newer swap file and offer to recover it */
int ui_editor_swp_recover(TeApp *app, const char *path)
{
    char swp_path[1024];
    long swp_mtime;
    long orig_mtime;
    FILE *fp = NULL;
    int rc;
    Ed *ed = NULL;

    if (!app || !path || !path[0])
        return 0;

    pf_swap_path(path, swp_path, sizeof(swp_path));

    if (!swp_path[0])
        return 0;

    swp_mtime = pf_get_file_mtime(swp_path);

    /* No swap file */
    if (swp_mtime < 0)
        return 0;

    orig_mtime = pf_get_file_mtime(path);

    /* Swap is older than original, remove stale swap */
    if (orig_mtime >= 0 && swp_mtime <= orig_mtime)
    {
        pf_remove_swp(path);
        return 0;
    }

    /* User declined, discard stale swap */
    if (ui_popup_confirm("Recover", "Auto-save swap found, recover it?") != 1)
    {
        pf_remove_swp(path);
        return 0;
    }

    ed = te_app_get_editor(app);

    if (!ed)
        return -1;

    fp = fopen(swp_path, "rb");

    if (!fp)
        return -1;

    ed_clear_undo_redo(ed);
    rc = ed_load_stream(ed, fp);
    fclose(fp);

    if (rc != 0)
        return -1;

    te_app_set_filename(app, path);
    ed_set_modified(ed, 1);
    ui_editor_recent_add(path);

    return 1;
}

/* Remove swap companions for all tabs in the app */
void ui_editor_swp_cleanup_all(TeApp *app)
{
    int i;

    if (!app)
        return;

    for (i = 0; i < app->tab_count; i++)
    {
        TeTab *tab = app->tabs[i];

        if (tab && tab->filename[0])
            pf_remove_swp(tab->filename);
    }
}

/* Add a path to the recent list without saving to disk */
static void recent_add_internal(const char *path)
{
    int i;
    int found;
    char *moved = NULL;

    if (!path || !path[0])
        return;

    found = -1;

    for (i = 0; i < s_recent_count; i++)
    {
        if (strcmp(s_recent_files[i], path) == 0)
        {
            found = i;
            break;
        }
    }

    if (found >= 0)
    {
        moved = s_recent_files[found];

        for (i = found; i > 0; i--)
            s_recent_files[i] = s_recent_files[i - 1];

        s_recent_files[0] = moved;
        return;
    }

    if (s_recent_count == RECENT_MAX)
    {
        free(s_recent_files[s_recent_count - 1]);
        s_recent_count--;
    }

    for (i = s_recent_count; i > 0; i--)
        s_recent_files[i] = s_recent_files[i - 1];

    s_recent_files[0] = strdup(path);
    s_recent_count++;
}

/* Load recent files from config directory */
void ui_editor_recent_load(void)
{
    char cfg_dir[512];
    char path[1024];
    char line[1024];
    FILE *fp = NULL;

    port_get_config_dir(cfg_dir, sizeof(cfg_dir));
    pf_path_join(path, sizeof(path), cfg_dir, "recent");

    fp = fopen(path, "rb");

    if (!fp)
        return;

    while (fgets(line, sizeof(line), fp))
    {
        size_t len = strlen(line);

        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        if (line[0])
            recent_add_internal(line);
    }

    fclose(fp);
}

/* Save recent files to config directory */
void ui_editor_recent_save(void)
{
    char cfg_dir[512];
    char path[1024];
    FILE *fp = NULL;
    int i;

    port_get_config_dir(cfg_dir, sizeof(cfg_dir));
    pf_path_join(path, sizeof(path), cfg_dir, "recent");

    fp = fopen(path, "wb");

    if (!fp)
        return;

    for (i = 0; i < s_recent_count; i++)
    {
        fputs(s_recent_files[i], fp);
        fputc('\n', fp);
    }

    fclose(fp);
}

/* Add a path to recent files and persist the list */
void ui_editor_recent_add(const char *path)
{
    recent_add_internal(path);
    ui_editor_recent_save();
}

/* Free the in-memory recent files list */
void ui_editor_recent_free(void)
{
    int i;

    for (i = 0; i < s_recent_count; i++)
    {
        free(s_recent_files[i]);
        s_recent_files[i] = NULL;
    }

    s_recent_count = 0;
}

/* Show recent files popup and open the selected one */
int ui_editor_recent_open(TeApp *app)
{
    const char **items;
    int i;
    int sel;

    if (!app || s_recent_count == 0)
    {
        te_status(app, "No recent files");
        return 0;
    }

    items = (const char **)malloc((size_t)s_recent_count * sizeof(char *));

    if (!items)
    {
        te_status(app, "Memory error");
        return -1;
    }

    for (i = 0; i < s_recent_count; i++)
        items[i] = s_recent_files[i];

    sel = ui_popup_list("Recent files", items, s_recent_count, 0);

    free(items);

    if (sel < 0 || sel >= s_recent_count)
        return 0;

    return ui_files_open_path(app, s_recent_files[sel]);
}

/* Save open tabs and active tab to config directory */
void ui_editor_session_save(TeApp *app)
{
    char cfg_dir[512];
    char path[1024];
    FILE *fp = NULL;
    int i;

    if (!app)
        return;

    port_get_config_dir(cfg_dir, sizeof(cfg_dir));
    pf_path_join(path, sizeof(path), cfg_dir, "session");

    fp = fopen(path, "wb");

    if (!fp)
        return;

    for (i = 0; i < app->tab_count; i++)
    {
        TeTab *tab = app->tabs[i];

        if (!tab || !tab->filename[0])
            continue;

        if (i == app->active_tab)
            fputs("active ", fp);

        fputs(tab->filename, fp);
        fputc('\n', fp);
    }

    fclose(fp);
}

/* Restore open tabs and active tab from config directory */
void ui_editor_session_restore(TeApp *app)
{
    char cfg_dir[512];
    char path[1024];
    char line[1024];
    FILE *fp = NULL;
    int active_idx;
    int opened;
    int is_active;
    const char *fn = NULL;
    size_t len;

    if (!app)
        return;

    port_get_config_dir(cfg_dir, sizeof(cfg_dir));
    pf_path_join(path, sizeof(path), cfg_dir, "session");

    fp = fopen(path, "rb");

    if (!fp)
        return;

    active_idx = 0;
    opened = 0;

    while (fgets(line, sizeof(line), fp))
    {
        len = strlen(line);

        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        if (strncmp(line, "active ", 7) == 0)
        {
            is_active = 1;
            fn = line + 7;
        }
        else
        {
            is_active = 0;
            fn = line;
        }

        if (!fn[0])
            continue;

        if (!pf_path_exists(fn))
            continue;

        {
            TeTab *tab = te_tab_new();

            if (!tab)
                continue;

            tab->show_line_numbers = app->cfg.show_line_numbers;

            ed_set_word_move_mode(tab->editor, app->cfg.word_move_mode);
            te_app_add_tab(app, tab);
            te_app_switch_tab(app, app->tab_count - 1);
        }

        if (ui_files_open_path(app, fn) == 0)
        {
            if (is_active)
                active_idx = app->active_tab;

            opened++;
        }
        else
        {
            te_app_close_tab(app, app->tab_count - 1);
        }
    }

    fclose(fp);

    if (opened > 0)
        te_app_switch_tab(app, active_idx);
}
