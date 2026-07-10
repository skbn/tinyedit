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

/* https://ia800501.us.archive.org/view_archive.php?archive=/26/items/wordperfectsdkperfectfit1994/WordPerfect_SDK_PerfectFit1994.iso&file=51PCSDK%2FWP42FF.TXT */

#ifndef FMT_WP4_H
#define FMT_WP4_H

#include <stdio.h>

struct Ed;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Import a WP 4.2 file into ed, replacing its content. Returns 0 on success or -1 on error */
    int wp4_import(struct Ed *ed, FILE *fp, const char *charset, char *err, size_t errsz, char *warn, size_t warnsz);

    /* Write ed as WP 4.2. Returns 0 on success, -1 with err filled */
    int wp4_export(const struct Ed *ed, FILE *fp, const char *charset, char *err, size_t errsz, char *warn, size_t warnsz);

#ifdef __cplusplus
}
#endif

#endif /* FMT_WP4_H */
