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

/* ui_setup.h -- Setup screen for tinyedit */
#ifndef TINYEDIT_UI_SETUP_H
#define TINYEDIT_UI_SETUP_H

#include "../components/config.h"
#include "te.h"

/* Run setup screen: modify cfg in place, return 1 if saved, 0 if cancelled */
int ui_setup_run(TeApp *app, TeConfig *cfg, const char *cfg_path);

#endif /* TINYEDIT_UI_SETUP_H */
