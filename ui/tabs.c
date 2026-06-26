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
TeTab *te_tab_new(void)
{
    TeTab *tab = NULL;

    tab = (TeTab *)calloc(1, sizeof(TeTab));

    if (!tab)
        return NULL;

    tab->editor = ed_new();

    if (!tab->editor)
    {
        free(tab);
        return NULL;
    }

    tab->modified = 0;
    tab->show_line_numbers = 0;
    tab->filename[0] = '\0';
    tab->charset_in[0] = '\0';
    tab->charset_out[0] = '\0';

    return tab;
}

/* Create new tab with filename and content */
TeTab *te_tab_new_with_content(const char *filename, const char *content)
{
    TeTab *tab = NULL;

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
