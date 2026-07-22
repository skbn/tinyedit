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

#ifndef UI_PRINT_H
#define UI_PRINT_H

#include "te.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Open the print popup: local / IPP / IPPS. Called from the keymap */
    void ui_editor_print(TeApp *app);

#ifdef __cplusplus
}
#endif

#endif
