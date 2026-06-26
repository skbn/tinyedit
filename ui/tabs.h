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

#ifndef TINYEDIT_TABS_H
#define TINYEDIT_TABS_H

#include "../components/editor.h"

#define TAB_FILENAME_MAX 1024
#define TAB_CHARSET_MAX 32

typedef struct
{
    Ed *editor;
    char filename[TAB_FILENAME_MAX];
    char charset_in[TAB_CHARSET_MAX];
    char charset_out[TAB_CHARSET_MAX];
    int modified;
    int show_line_numbers;
} TeTab;

TeTab *te_tab_new(void);
TeTab *te_tab_new_with_content(const char *filename, const char *content);
void te_tab_free(TeTab *tab);
void te_tab_clear_search(TeTab *tab);

#endif
