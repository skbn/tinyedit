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

#include "ui_hyph.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifdef HAVE_HYPHEN
#include "../hyph_wrap/hyph_wrap.h"
#include "../core/utf8.h"
#endif

int hyph_load_from_config(TeApp *app)
{
#ifdef HAVE_HYPHEN
    char path[512];
    HyphDict *h;

    if (!app)
        return 0;

    hyph_unload(app);

    if (!app->cfg.hyph_enabled)
        return 0;

    if (!app->cfg.hyph_dict_path[0] || !app->cfg.hyph_dict_name[0])
        return 0;

    /* Standard naming: hyph_<lang>.dic  (LibreOffice convention) */
    snprintf(path, sizeof(path), "%.220s/hyph_%.40s.dic", app->cfg.hyph_dict_path, app->cfg.hyph_dict_name);

    h = hyph_new(path);

    if (!h)
        return 0;

    app->hyph_handle = h;

    return 1;
#else
    return 0;
#endif
}

void hyph_unload(TeApp *app)
{
#ifdef HAVE_HYPHEN
    if (!app)
        return;

    if (app->hyph_handle)
    {
        hyph_free((HyphDict *)app->hyph_handle);
        app->hyph_handle = NULL;
    }
#endif
}

int hyph_split_word(TeApp *app, const char *word, int word_len, int *out_pos, int *out_count)
{
#ifdef HAVE_HYPHEN
    if (out_count)
        *out_count = 0;

    if (!app || !app->hyph_handle || !word || word_len <= 0)
        return 0;

    return hyph_breakpoints((HyphDict *)app->hyph_handle, word, word_len, out_pos, out_count);
#else

    if (out_count)
        *out_count = 0;

    return 0;
#endif
}

/* Auxiliary function to calculate character width */
static int char_width(wchar_t c)
{
    if (c == L'\t')
        return 1;

    /* For wide characters we assume 1 for simplicity. A more sophisticated implementation would use wcwidth */
    return 1;
}

int hyph_find_break(TeApp *app, const wchar_t *word, int word_len, int col_limit)
{
#ifdef HAVE_HYPHEN
    char *utf8_word;
    int utf8_len;
    int hyph_pos[16];
    int hyph_count = 0;
    int k;

    /* Validations */
    if (!app || !word || word_len <= 0)
        return -1;

    if (col_limit <= 0)
        return -1;

    if (word_len < 4 || word_len >= 512)
        return -1;

    if (!app->hyph_handle)
        return -1;

    /* Convert to UTF-8 */
    utf8_word = wcs_to_utf8(word, word_len);

    if (!utf8_word)
        return -1;

    utf8_len = (int)strlen(utf8_word);

    /* Get breakpoints */
    if (!hyph_split_word(app, utf8_word, utf8_len, hyph_pos, &hyph_count))
    {
        free(utf8_word);
        return -1;
    }

    /* Limit hyph_count to array size */
    if (hyph_count > 16)
        hyph_count = 16;

    /* Search for valid breakpoint from right to left */
    for (k = hyph_count - 1; k >= 0; k--)
    {
        int char_off = utf8_charcount(utf8_word, hyph_pos[k]);
        int break_col = 0;
        int m;

        /* Validate position */
        if (char_off <= 0 || char_off >= word_len)
            continue;

        /* Calculate columns up to breakpoint */
        for (m = 0; m < char_off; m++)
            break_col += char_width(word[m]);

        /* Reserve space for '-' */
        if (break_col > col_limit - 1)
            continue;

        free(utf8_word);
        return char_off;
    }

    free(utf8_word);
#endif

    return -1;
}
