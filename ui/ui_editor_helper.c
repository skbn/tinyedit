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
#include "../components/editor.h"
#include "te.h"
#include "ui_files.h"

/* Static variables that need to be accessible */
static int s_soft_vtop = 0;
static int s_soft_desired_vcol = -1;

/* Forward declarations for static functions */
static int ui_paste_char_width(wchar_t ch);

/* Visual width in display columns of n wchars starting at s. Wide chars
 * (CJK ideographs, wide emoji) count 2, narrow 1, control/non-printable 1
 * Used by ui_editor.c to convert wchar offsets into visual column offsets
 * when positioning mvaddnwstr() within a line that contains wide glyphs */
int wcs_vwidth(const wchar_t *s, int n)
{
    int v = 0;
    int i;

    if (!s || n <= 0)
        return 0;

    for (i = 0; i < n; i++)
    {
        int w = wcswidth(&s[i], 1);

        if (w == 2)
            v += 2;
        else
            v += 1; /* narrow, zero-width, control -> 1 */
    }

    return v;
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

/* Navigate to previous match in editor.  Wraps at the start. */
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

    ed_set_pos(app->editor, match_row, match_col);
    ed_ensure_visible(app->editor);

    return 1;
}

/* Navigate to next match in editor.  Wraps at the end. */
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

    ed_set_pos(app->editor, match_row, match_col);
    ed_ensure_visible(app->editor);

    return 1;
}

static int do_replace(TeApp *app, const wchar_t *needle, const wchar_t *repl)
{
    int count = 0;
    int nlen = (int)wcslen(needle);
    int rlen = (int)wcslen(repl);
    int *rows = NULL, *cols = NULL;
    int match_count, i;

    /* Find all matches with current case-sensitive and whole-word options */
    match_count = ed_search_all_custom(app->editor, needle, app->search.case_sensitive, app->search.whole_word, &rows, &cols);

    if (match_count == 0)
        return 0;

    /* Replace from end to start to avoid position shifts */
    for (i = match_count - 1; i >= 0; i--)
    {
        int j;

        ed_set_pos(app->editor, rows[i], cols[i]);

        /* Delete needle */
        for (j = 0; j < nlen; j++)
            ed_delete(app->editor);

        /* Insert replacement */
        for (j = 0; j < rlen; j++)
            ed_insert_char(app->editor, repl[j]);

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
    match_count = ed_search_all_custom(app->editor, app->search.query, case_sensitive, whole_word, &rows, &cols);

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

        /* Move cursor to first match and ensure it's visible */
        ed_set_pos(app->editor, rows[0], cols[0]);
        ed_ensure_visible(app->editor);
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
    int *new_rows;
    int *new_cols;
    int new_match_count;

    if (app->search.last_replace[0] != L'\0')
    {
        /* Use the last replacement text - no popup */
        wchar_t repl[64];
        wcsncpy(repl, app->search.last_replace, 63);
        repl[63] = L'\0';

        /* Move cursor to current match position */
        if (!app->search.rows || app->search.count == 0)
            return 0;

        match_row = app->search.rows[app->search.current_match];
        match_col = app->search.cols[app->search.current_match];

        ed_set_pos(app->editor, match_row, match_col);

        /* Save undo state */
        ed_save_undo(app->editor);

        /* Replace the current occurrence */
        nlen = (int)wcslen(app->search.query);
        rlen = (int)wcslen(repl);

        /* Delete the search text */
        for (i = 0; i < nlen; i++)
            ed_delete(app->editor);

        /* Insert replacement */
        for (i = 0; i < rlen; i++)
            ed_insert_char(app->editor, repl[i]);

        /* Update search results since text changed */
        free(app->search.rows);
        free(app->search.cols);

        app->search.rows = NULL;
        app->search.cols = NULL;
        app->search.count = 0;

        /* Re-run search to find remaining matches */
        new_rows = NULL;
        new_cols = NULL;
        new_match_count = ed_search_all_custom(app->editor, app->search.query, app->search.case_sensitive, app->search.whole_word, &new_rows, &new_cols);

        if (new_match_count > 0)
        {
            app->search.rows = new_rows;
            app->search.cols = new_cols;
            app->search.count = new_match_count;
            app->search.current_match = 0;
            app->search.match_current = 1;
            app->search.match_total = new_match_count;

            /* Move cursor to first remaining match */
            ed_set_pos(app->editor, new_rows[0], new_cols[0]);
            ed_ensure_visible(app->editor);

            te_status(app, "Replaced. %d match(es) remaining", new_match_count);
        }
        else
        {
            free(new_rows);
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
            ed_save_undo(app->editor);
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
    match_count = ed_search_all(app->editor, app->search.query, &rows, &cols);

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
        ed_set_pos(app->editor, rows[0], cols[0]);
        ed_ensure_visible(app->editor);
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

    /* Multiple matches: show the picker popup; user chooses one. */

    choice = ui_popup_search_results_popup(app, app->search.query, rows, cols, match_count);

    if (choice >= 0)
    {
        ed_set_pos(app->editor, rows[choice], cols[choice]);
        ed_ensure_visible(app->editor);
        te_status(app, "Jumped to line %d", rows[choice] + 1);

        app->search.current_match = choice;
        app->search.match_current = choice + 1;
    }
    else
    {
        /* User cancelled the picker -- keep the matches armed so
         * F3/F4 still work, just leave the cursor in place */
        te_status(app, "Search cancelled");
        app->search.current_match = 0;
        app->search.match_current = 1;
    }

    /* Save matches for highlighting and activate search mode
     * (don't free rows/cols -- ownership transfers to the struct) */
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

    if (ed_load_file_at_cursor(app->editor, path, in_cs[0] ? in_cs : NULL) == 0)
    {
        clear_search_highlights(app);
        te_status(app, "Inserted: %s", path);
    }
    else
        te_status(app, "Cannot read: %s", path);

    return 1;
}

/* Control key implementations */
int paste(TeApp *app)
{
    /* Try internal block buffer first */
    if (ed_block_paste(app->editor) == 0)
    {
        clear_search_highlights(app);
        soft_reset_desired();
        s_soft_vtop = 0;
        te_status(app, "Pasted");
    }
    else
    {
        /* Fall back to system clipboard */
        char *clip = clipboard_paste();

        if (clip && clip[0])
        {
            char *wrapped = NULL;
            const char *to_insert = clip;

            /* HARD-WRAP only: reflow pasted text; soft-wrap inserts verbatim */
            if (app->hard_wrap)
            {
                int pw = editor_eff_wrap(app);

                if (pw > 0)
                {
                    wrapped = wrap_paste_text(clip, pw);

                    if (wrapped)
                        to_insert = wrapped;
                }
            }

            ed_paste_text_with_undo(app->editor, to_insert);
            clear_search_highlights(app);
            soft_reset_desired();

            s_soft_vtop = 0;
            te_status(app, "Pasted from clipboard");

            free(clip);

            if (wrapped)
                free(wrapped);
        }
        else
        {
            te_status(app, "Clipboard empty (install xclip or wl-clipboard)");
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
            ed_goto_line(app->editor, n - 1);
            ed_ensure_visible(app->editor);
        }
    }

    return 1;
}

int copy(TeApp *app)
{
    EdInfo info;
    ed_get_info(app->editor, &info);

    if (info.block.active)
    {
        char *utf8 = ed_block_get_utf8(app->editor);

        if (ed_block_copy(app->editor) == 0)
        {
            if (utf8)
            {
                clipboard_copy(utf8);
                te_status(app, "Block copied");
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
    ed_get_info(app->editor, &info);

    if (info.block.active)
    {
        char *utf8 = ed_block_get_utf8(app->editor);

        if (ed_block_cut(app->editor) == 0)
        {
            clear_search_highlights(app);

            if (utf8)
            {
                clipboard_copy(utf8);
                te_status(app, "Block cut");
            }
        }

        if (utf8)
            free(utf8);
    }

    return 1;
}

int ui_editor_goto_start(TeApp *app)
{
    ed_set_pos(app->editor, 0, 0);
    ed_ensure_visible(app->editor);
    return 1;
}

int ui_editor_goto_end(TeApp *app)
{
    EdInfo info;
    ed_get_info(app->editor, &info);

    if (info.line_count > 0)
    {
        int ll = info.line_count - 1;

        ed_set_pos(app->editor, ll, ed_line_len(app->editor, ll));
        ed_ensure_visible(app->editor);
    }

    return 1;
}

int ui_editor_export(TeApp *app)
{
    EdInfo info;
    ed_get_info(app->editor, &info);

    if (!info.block.active)
    {
        te_status(app, "No block marked (F6 to mark)");
    }
    else
    {
        char path[256], out_cs[32];
        path[0] = '\0';
        out_cs[0] = '\0';

        if (ui_files_save("Export block", NULL, "block_export.txt", path, sizeof(path)) != 0)
            return 1;

        ui_popup_charset("Output charset (empty=UTF-8)", "", out_cs, sizeof(out_cs));

        if (ed_export_block_to_file(app->editor, path, out_cs[0] ? out_cs : NULL) == 0)
            te_status(app, "Block written: %s", path);
        else
            te_status(app, "Cannot write: %s", path);
    }

    return 1;
}

int rewrap(TeApp *app)
{
    int col = (app->wrap_col > 0) ? app->wrap_col : 75;
    ed_save_undo(app->editor);

    if (ed_rewrap_paragraph(app->editor, col) == 0)
    {
        clear_search_highlights(app);
        te_status(app, "Paragraph rewrapped");
    }

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
            ed_get_info(app->editor, &info);

            if (info.modified && app->filename[0])
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
        if (view_changed && app->filename[0])
        {
            FILE *fp;
            long size;
            char *buf;
            size_t r;

            fp = fopen(app->filename, "rb");
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

                        /* Update raw_bytes */
                        free(app->raw_bytes);

                        app->raw_bytes = (char *)malloc(r + 1);

                        if (app->raw_bytes)
                        {
                            memcpy(app->raw_bytes, buf, r + 1);
                            app->raw_len = (int)r;
                        }
                        else
                        {
                            app->raw_len = 0;
                        }

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
                                    ed_load(app->editor, utf8);
                                }

                                free(utf8);
                            }
                        }
                        else
                        {
                            ed_load(app->editor, buf);
                        }

                        free(buf);
                    }
                }

                fclose(fp);
            }
        }
        else if (!view_changed && app->raw_bytes && app->raw_len > 0)
        {
            /* TODO */
        }

        te_status(app, "View: %s  Save: %s", new_view[0] ? new_view : "UTF-8", new_save[0] ? new_save : "UTF-8");
    }

    return 1;
}
