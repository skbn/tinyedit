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

#ifndef FMT_URF_H
#define FMT_URF_H

#include <stdio.h>
#include <stddef.h>

#include "config.h"

struct Ed;

#ifdef __cplusplus
extern "C"
{
#endif

    /* Write ed as an Apple Raster (image/urf) job */
    int urf_export(const struct Ed *ed, FILE *fp, const TeConfig *cfg, char *err, size_t errsz, char *warn, size_t warnsz);

#ifdef __cplusplus
}
#endif

#endif /* FMT_URF_H */
