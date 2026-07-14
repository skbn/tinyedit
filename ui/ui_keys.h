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

/* ui_keys.h -- key dispatch entry points used by the editor loop */
#ifndef UI_KEYS_H
#define UI_KEYS_H

#include "te.h"

int handle_function_keys(TeApp *app, int ch, int is_key);
int handle_control_keys(TeApp *app, int ch, int is_key);
int handle_navigation_keys(TeApp *app, int ch, int soft, int width, int body_rows, int *preserve_desired);
int handle_editing_keys(TeApp *app, int ch, wint_t wch, int soft, int width, int body_rows, int *preserve_desired);

/* The editor body painter, some handlers repaint mid-action */
void ui_editor_draw_body(TeApp *app);

/* Shared with the editor loop */
int do_save(TeApp *app);

#endif
