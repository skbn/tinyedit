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

/* DOCX (Office Open XML) import/export
 * Spec: ECMA-376 / ISO/IEC 29500-1 (WordprocessingML)
 * https://ecma-international.org/publications-and-standards/standards/ecma-376/ */

#ifndef FMT_DOCX_H
#define FMT_DOCX_H

#include <stdio.h>
#include <stddef.h>

#include "config.h"

struct Ed;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Import .docx; *hyph_out=1 if autoHyphenation in settings.xml */
    int docx_import(struct Ed *ed, const char *path, char *err, size_t errsz, int *hyph_out);

    /* Export .docx; emit <w:autoHyphenation/> when hyph is non-zero */
    int docx_export(const struct Ed *ed, const char *path, const TeConfig *cfg, int hyph, char *err, size_t errsz);

#ifdef __cplusplus
}
#endif

#endif