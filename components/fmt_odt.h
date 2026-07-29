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

/* ODT (OpenDocument Text) import/export
 * Spec: OASIS OpenDocument Format v1.3
 * Part 1: https://docs.oasis-open.org/office/OpenDocument/v1.3/os/part1-introduction/OpenDocument-v1.3-os-part1-introduction.html
 * Part 2: https://docs.oasis-open.org/office/OpenDocument/v1.3/os/part2-packages/OpenDocument-v1.3-os-part2-packages.html
 * Part 3: https://docs.oasis-open.org/office/OpenDocument/v1.3/os/part3-schema/OpenDocument-v1.3-os-part3-schema.html */

#ifndef FMT_ODT_H
#define FMT_ODT_H

#include <stdio.h>
#include <stddef.h>

#include "config.h"

struct Ed;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Import .odt: text, run attrs and paragraph alignment only */
    /* hyph_out is set to 1 if fo:hyphenate="true" was detected in styles */
    int odt_import(struct Ed *ed, const char *path, char *err, size_t errsz, int *hyph_out);

    /* Export ed as .odt: mimetype stored, content/styles deflated */
    int odt_export(const struct Ed *ed, const char *path, const TeConfig *cfg, char *err, size_t errsz);

#ifdef __cplusplus
}
#endif

#endif
