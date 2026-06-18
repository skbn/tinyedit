/*
 * tinyedit - Text editor for AmigaOS
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 or later.
 */

#ifndef PORTABLE_H
#define PORTABLE_H

/* Portable directory creation function */
int port_mkdir_one(const char *path);

/* Portable empty file creation function */
int port_file_create_empty(const char *path);

#endif
