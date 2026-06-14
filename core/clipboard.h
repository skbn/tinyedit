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

/* clipboard.h -- System clipboard access (paste only) */

#ifndef WRAPPER_CLIPBOARD_H
#define WRAPPER_CLIPBOARD_H

#ifdef PLATFORM_AMIGA
#include <exec/types.h>
#include <exec/libraries.h>
extern struct Library *IFFParseBase;
#endif

char *clipboard_paste();
int clipboard_copy(const char *utf8);

/* Returns 1 if external clipboard should be used, 0 for internal only */
int clipboard_use_external(void);

#endif
