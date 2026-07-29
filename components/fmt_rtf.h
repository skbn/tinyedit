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

/* RTF (Rich Text Format) import/export
 * Spec: Microsoft RTF Specification v1.6
 * https://learn.microsoft.com/en-us/previous-versions/office/developer/office2000/aa140277(v=office.10) */

#ifndef FMT_RTF_H
#define FMT_RTF_H

#include <stdio.h>

struct Ed;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Parse RTF into ed; *hyph_out=1 if \hyphauto, 0 otherwise */
    int rtf_import(struct Ed *ed, FILE *fp, char *err, size_t errsz, char *warn, size_t warnsz, int *hyph_out);

    /* Write ed as RTF 1.x; emit \hyphauto1 when hyph is non-zero */
    int rtf_export(const struct Ed *ed, FILE *fp, int hyph);

    /* Same but overrides font table (name + half-points size) */
    int rtf_export_with_font(const struct Ed *ed, FILE *fp, const char *font_name, int font_size_half_pt, int hyph);

#ifdef __cplusplus
}
#endif

#endif /* FMT_RTF_H */