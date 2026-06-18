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

#include <stdlib.h>
#include <string.h>
#include "tabs.h"

/* Create new empty tab with editor instance */
TeTab *te_tab_new()
{
    TeTab *tab;

    tab = (TeTab *)calloc(1, sizeof(TeTab));

    if (!tab)
        return NULL;

    tab->editor = ed_new();

    if (!tab->editor)
    {
        free(tab);
        return NULL;
    }

    tab->raw_bytes = NULL;
    tab->raw_len = 0;
    tab->modified = 0;
    tab->show_line_numbers = 0;
    tab->filename[0] = '\0';
    tab->charset_in[0] = '\0';
    tab->charset_out[0] = '\0';

    return tab;
}

/* Create new tab with filename, content and raw bytes */
TeTab *te_tab_new_with_content(const char *filename, const char *content, const char *raw_bytes, int raw_len)
{
    TeTab *tab;

    tab = te_tab_new();

    if (!tab)
        return NULL;

    if (filename)
    {
        strncpy(tab->filename, filename, TAB_FILENAME_MAX - 1);
        tab->filename[TAB_FILENAME_MAX - 1] = '\0';
    }

    if (content)
        ed_load(tab->editor, content);

    if (raw_bytes && raw_len > 0)
    {
        tab->raw_bytes = (char *)malloc(raw_len + 1);

        if (!tab->raw_bytes)
        {
            te_tab_free(tab);
            return NULL;
        }

        memcpy(tab->raw_bytes, raw_bytes, raw_len);

        tab->raw_bytes[raw_len] = '\0';
        tab->raw_len = raw_len;
    }

    return tab;
}

/* Free tab and all its resources including editor */
void te_tab_free(TeTab *tab)
{
    if (!tab)
        return;

    if (tab->editor)
    {
        ed_free(tab->editor);
        tab->editor = NULL;
    }

    if (tab->raw_bytes)
    {
        free(tab->raw_bytes);
        tab->raw_bytes = NULL;
    }

    free(tab);
}

/* Clear search state in tab */
void te_tab_clear_search(TeTab *tab)
{
    if (!tab)
        return;

    if (tab->editor)
    {
        EdInfo info;
        ed_get_info(tab->editor, &info);
    }

    tab->modified = 0;
}
