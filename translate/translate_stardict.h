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

#ifndef TRANSLATE_STARDICT_H
#define TRANSLATE_STARDICT_H

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct StarDictHandle StarDictHandle;

    /* Open dictionary from .ifo file, returns handle or error */
    int translate_stardict_open(const char *ifo_path, const char *from, const char *to, StarDictHandle **out, char *err, int err_size);

    /* Look up word, returns malloc'd definition or NULL */
    char *translate_stardict_lookup(StarDictHandle *h, const char *word, char *err, int err_size);

    /* Clear LRU cache, keep files open */
    void translate_stardict_cache_clear(StarDictHandle *h);

    /* Close and free all resources */
    void translate_stardict_close(StarDictHandle *h);

    /* Get dictionary name from .ifo */
    const char *translate_stardict_bookname(StarDictHandle *h);

#ifdef __cplusplus
}
#endif

#endif /* TRANSLATE_STARDICT_H */
