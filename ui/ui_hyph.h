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

#ifndef UI_HYPH_H
#define UI_HYPH_H

#include "te.h"

#ifdef __cplusplus
extern "C"
{
#endif

    int hyph_load_from_config(TeApp *app);
    void hyph_unload(TeApp *app);

    /* Suggested break points for `word` (ASCII / UTF-8) */
    int hyph_split_word(TeApp *app, const char *word, int word_len, int *out_pos, int *out_count);

    /* Find best hyphenation break point for a word given a column limit */
    int hyph_find_break(TeApp *app, const wchar_t *word, int word_len, int col_limit);

#ifdef __cplusplus
}
#endif

#endif
