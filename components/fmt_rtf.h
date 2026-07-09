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

#ifndef FMT_RTF_H
#define FMT_RTF_H

#include <stdio.h>

struct Ed;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Parse RTF from fp into ed; return 0 on success, -1 on error */
    int rtf_import(struct Ed *ed, FILE *fp, char *err, size_t errsz, char *warn, size_t warnsz);

    /* Write ed as RTF 1.x. Returns 0 on success, -1 on I/O error */
    int rtf_export(const struct Ed *ed, FILE *fp);

#ifdef __cplusplus
}
#endif

#endif /* FMT_RTF_H */
