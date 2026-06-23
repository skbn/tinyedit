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

#ifndef DICT_SCAN_H
#define DICT_SCAN_H

typedef int (*dict_filter_fn)(const unsigned char *name);
typedef char *(*dict_xform_fn)(const unsigned char *name);

char **dict_scan(const char *dir_path, int *n_out, dict_filter_fn filter, dict_xform_fn xform);

#endif
