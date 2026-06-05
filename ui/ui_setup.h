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

/* Run the setup screen.
 * cfg       : live config struct (modified in place on save)
 * cfg_path  : path to write the config file, or NULL.
 * Returns 1 if saved (caller should re-apply colors), 0 if cancelled */
int ui_setup_run(TeConfig *cfg, const char *cfg_path);

#endif /* TINYEDIT_UI_SETUP_H */
