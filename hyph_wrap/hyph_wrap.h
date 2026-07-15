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

#ifndef CE_HYPHEN_H
#define CE_HYPHEN_H

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(PLATFORM_AMIGA)
#ifndef HYPH_CACHE_N
#define HYPH_CACHE_N 128
#endif
#ifndef HYPH_CACHE_KEY_MAX
#define HYPH_CACHE_KEY_MAX 512
#endif
#ifndef HYPH_MAX_BREAKS
#define HYPH_MAX_BREAKS 16
#endif
#else
#ifndef HYPH_CACHE_N
#define HYPH_CACHE_N 4096
#endif
#ifndef HYPH_CACHE_KEY_MAX
#define HYPH_CACHE_KEY_MAX 512
#endif
#ifndef HYPH_MAX_BREAKS
#define HYPH_MAX_BREAKS 64
#endif
#endif

    typedef struct HyphDict HyphDict;

    /* Open hyphenation dictionary file */
    HyphDict *hyph_new(const char *dict_path);

    /* Free dictionary + cache */
    void hyph_free(HyphDict *h);

    /* Find hyphenation break points (returns byte offsets) */
    int hyph_breakpoints(HyphDict *h, const char *word, int word_len, int *out_pos, int *out_count);

    /* List available dictionaries in directory */
    char **hyph_list_dictionaries(const char *dir_path, int *n_dicts);

    void hyph_free_dictionaries(char **dicts, int n_dicts);

#ifdef __cplusplus
}
#endif

#endif
