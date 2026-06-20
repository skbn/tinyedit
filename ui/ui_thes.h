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

#ifndef UI_THES_H
#define UI_THES_H

#include "ui_thes_glue.h" /* project-specific bindings (see above) */

#ifdef __cplusplus
extern "C"
{
#endif

    /* Load thesaurus from cfg. Returns 1 on success, 0 otherwise */
    int ui_thes_load_from_config(UI_APP_T *app);

    /* Free the thesaurus handle.  NULL-safe */
    void ui_thes_unload(UI_APP_T *app);

    /* Look up word under cursor, show popup with meanings/synonyms, replace if picked */
    int ui_thes_lookup_word(UI_APP_T *app);

#ifdef __cplusplus
}
#endif

#endif
