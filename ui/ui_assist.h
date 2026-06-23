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

#ifndef UI_ASSIST_H
#define UI_ASSIST_H

#include "te.h"
#include <wchar.h>

/* Called after inserting char; may replace via ed_delete/ed_insert_char */
int ui_assist_on_char(TeApp *app, wchar_t just_typed);

/* Returns 1 if word at (line, col_start) repeats previous word on same line (whitespace-separated). For rendering highlight */
int ui_assist_check_repeat(TeApp *app, int line, int col_start, int word_len);

#endif
