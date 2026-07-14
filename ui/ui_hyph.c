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
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef HAVE_HYPHEN
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/hyph.h"
#else
#include "../hyph_wrap/hyph_wrap.h"
#endif
#include "../core/utf8.h"
#endif

int hyph_load_from_config(TeApp *app)
{
#ifdef HAVE_HYPHEN
    char path[512];
    HyphDict *h = NULL;

    if (!app)
        return 0;

    hyph_unload(app);

    if (!app->cfg.hyph_enabled)
        return 0;

    if (!app->cfg.hyph_dict_path[0] || !app->cfg.hyph_dict_name[0])
        return 0;

    /* Use dictionary name as-is (user may include hyph_ prefix) */
    snprintf(path, sizeof(path), "%.220s/%.40s.dic", app->cfg.hyph_dict_path, app->cfg.hyph_dict_name);

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

/* Dict bridge: encode to utf8, ask for breaks, map byte offsets to char indices */
int ui_layout_hyphen(void *user, const wchar_t *word, int len, int *out, int max)
{
#ifdef HAVE_HYPHEN
    TeApp *app = (TeApp *)user;
    char utf8[512];
    int byte_of[512];
    int pos[HYPH_MAX_BREAKS];
    int count = 0;
    int used = 0;
    int n = 0;
    int i;

    if (!app || !word || len <= 0 || !out || max <= 0)
        return 0;

    /* Encode the word and remember where every character starts */
    for (i = 0; i < len; i++)
    {
        int k;

        if (used + 4 >= (int)sizeof(utf8))
            return 0;

        byte_of[i] = used;

        k = utf8_encode((uint32_t)word[i], utf8 + used);

        if (k <= 0)
            return 0;

        used += k;
    }

    utf8[used] = '\0';
    byte_of[len] = used;

    if (!hyph_split_word(app, utf8, used, pos, &count) || count <= 0)
        return 0;

    /* Byte offsets back to character indices */
    for (i = 0; i < count && n < max; i++)
    {
        int j;

        for (j = 1; j < len; j++)
        {
            if (byte_of[j] == pos[i])
            {
                out[n++] = j;
                break;
            }
        }
    }

    return n;
#else

    return 0;
#endif
}
