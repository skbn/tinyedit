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

#include "ui_assist.h"
#include "../components/editor.h"
#include "../spellchecker/spell.h"

#include <wctype.h>
#include <string.h>

/* SMART QUOTES: Map ASCII straight quotes to typographic curly quotes based on context */
static int do_smart_quotes(TeApp *app, wchar_t just_typed)
{
    Ed *ed = NULL;
    EdInfo info;
    const wchar_t *line = NULL;
    wchar_t replacement;
    wchar_t prev_ch;
    int prev_is_word;

#ifdef HAVE_HUNSPELL
    struct spell *sp = NULL;
#endif

    if (just_typed != L'\'' && just_typed != L'"')
        return 0;

    ed = te_app_get_editor(app);
    ed_get_info(ed, &info);

    /* Cursor is at info.col, right AFTER the typed char which is at col-1 */
    if (info.col < 1)
        return 0;

    line = ed_line_wcs(ed, info.row);

    if (!line)
        return 0;

    /* Look at what is BEFORE the just-typed quote (col - 2 if exists, or start of line) */
    prev_ch = (info.col >= 2) ? line[info.col - 2] : L' ';

    /* Use spell's Unicode-aware classifier if available, else iswalnum */
#ifdef HAVE_HUNSPELL
    sp = (struct spell *)app->spell_handle;

    if (sp)
        prev_is_word = te_is_word_char_ex(sp, (wint_t)prev_ch);
    else
#endif
        prev_is_word = (iswalnum((wint_t)prev_ch) != 0);

    if (just_typed == L'\'')
        replacement = prev_is_word ? (wchar_t)0x2019 : (wchar_t)0x2018;
    else
        replacement = prev_is_word ? (wchar_t)0x201D : (wchar_t)0x201C;

    /* Replace: position cursor before the just-typed char, delete it, insert the replacement */
    ed_set_pos(ed, info.row, info.col - 1);
    ed_delete(ed);
    ed_insert_char(ed, replacement);

    return 1;
}

/* AUTO-CAPITALIZATION: Capitalize letter after sentence enders (. ! ?) with spaces, or at doc start */
static int do_auto_cap(TeApp *app, wchar_t just_typed)
{
    Ed *ed;
    EdInfo info;
    const wchar_t *line;
    int col;
    wchar_t upper;
    int is_start;
    int k;

    if (!iswlower((wint_t)just_typed))
        return 0;

    ed = te_app_get_editor(app);
    ed_get_info(ed, &info);

    if (info.col < 1)
        return 0;

    line = ed_line_wcs(ed, info.row);

    if (!line)
        return 0;

    col = info.col - 1; /* position of just_typed */

    /* Start of sentence: doc start, or after space(s) following .!?, or line start after .!?/empty line */
    is_start = 0;
    k = col - 1;

    /* Skip whitespace going backwards on this line */
    while (k >= 0 && (line[k] == L' ' || line[k] == L'\t'))
        k--;

    if (k < 0)
    {
        /* Col 0 of this line (possibly after spaces). Look at the previous line */
        if (info.row == 0)
        {
            is_start = 1; /* Very first letter of doc */
        }
        else
        {
            const wchar_t *prev = ed_line_wcs(ed, info.row - 1);
            int pl = ed_line_len(ed, info.row - 1);

            /* Trim trailing spaces */
            while (pl > 0 && (prev[pl - 1] == L' ' || prev[pl - 1] == L'\t'))
                pl--;

            /* Paragraph break */
            if (pl == 0)
                is_start = 1;
            else
            {
                wchar_t lc = prev[pl - 1];

                if (lc == L'.' || lc == L'!' || lc == L'?')
                    is_start = 1;
            }
        }
    }
    else
    {
        wchar_t lc = line[k];

        /* Need at least one space between sentence-ender and the letter */
        if ((lc == L'.' || lc == L'!' || lc == L'?') && k + 1 < col)
            is_start = 1;
    }

    if (!is_start)
        return 0;

    /* Capitalize: replace the just-typed char with its upper form */
    upper = (wchar_t)towupper((wint_t)just_typed);

    if (upper == just_typed)
        return 0; /* No upper form (e.g. ß) */

    ed_set_pos(ed, info.row, info.col - 1);
    ed_delete(ed);
    ed_insert_char(ed, upper);

    return 1;
}

int ui_assist_on_char(TeApp *app, wchar_t just_typed)
{
    int did = 0;

    if (!app)
        return 0;

    if (app->cfg.assist_smart_quotes)
        did |= do_smart_quotes(app, just_typed);

    if (app->cfg.assist_auto_cap)
        did |= do_auto_cap(app, just_typed);

    return did;
}

/* REPEAT-WORD CHECK: Given word at (line, col_start, word_len), check if immediately preceding word on same line is identical (separated by whitespace) */
int ui_assist_check_repeat(TeApp *app, int line, int col_start, int word_len)
{
    Ed *ed = NULL;
    const wchar_t *l = NULL;
    int ll;
    int k;
    int prev_end;
    int prev_start;
    int i;

#ifdef HAVE_HUNSPELL
    struct spell *sp = NULL;
#endif

    if (!app || !app->cfg.assist_repeat_check || word_len <= 0)
        return 0;

    ed = te_app_get_editor(app);
    l = ed_line_wcs(ed, line);
    ll = ed_line_len(ed, line);

    if (!l || col_start + word_len > ll)
        return 0;

    /* Skip whitespace backwards from col_start. */
    k = col_start - 1;

    while (k >= 0 && (l[k] == L' ' || l[k] == L'\t'))
        k--;

    if (k < 0)
        return 0; /* nothing on line before the word */

#ifdef HAVE_HUNSPELL
    sp = (struct spell *)app->spell_handle;
#endif

    prev_end = k + 1;
    prev_start = prev_end;

    while (prev_start > 0)
    {
        wint_t wc = (wint_t)l[prev_start - 1];
        int is_word;

#ifdef HAVE_HUNSPELL
        if (sp)
            is_word = te_is_word_char_ex(sp, wc);
        else
#endif
            is_word = (iswalnum(wc) != 0);

        if (!is_word)
            break;

        prev_start--;
    }

    if (prev_end - prev_start != word_len)
        return 0; /* different lengths -> not a repeat */

    /* Case-insensitive compare */
    for (i = 0; i < word_len; i++)
    {
        wchar_t a = (wchar_t)towlower((wint_t)l[prev_start + i]);
        wchar_t b = (wchar_t)towlower((wint_t)l[col_start + i]);

        if (a != b)
            return 0;
    }

    return 1;
}
