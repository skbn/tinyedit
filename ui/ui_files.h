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

/* ui_files.h -- Modal file browser */
#ifndef UI_FILES_H
#define UI_FILES_H

/* Browse for file starting at start_dir (or CWD if NULL), return 0=ok, -1=cancel, -2=error */
int ui_files_pick(const char *title, const char *start_dir, char *out_path, int out_path_sz);

/* Browse for directory starting at start_dir (or CWD if NULL), fills out_path (0=ok, -1=cancel, -2=error) */
int ui_files_pick_dir(const char *title, const char *start_dir, char *out_path, int out_path_sz);

/* File picker with editable filename field for save-as, init_name pre-fills field */
int ui_files_save(const char *title, const char *start_dir, const char *init_name, char *out_path, int out_path_sz);

/* Open file with picker: show dialog, load into editor, clear previous content */
int ui_files_open(TeApp *app);
int ui_files_open_path(TeApp *app, const char *path);

#endif /* UI_FILES_H */
