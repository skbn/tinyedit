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

/* charset.h -- FTN charset detection and conversion */
#ifndef WRAPPER_CHARSET_H
#define WRAPPER_CHARSET_H

#include <stddef.h>

#define CHARSET_NAME_MAX 32
#define CHARSET_READ_DEFAULT "CP437"
#define CHARSET_WRITE_DEFAULT "UTF-8"
#define CHARSET_DEFAULT CHARSET_READ_DEFAULT

/* Detect charset from ^ACHRS: or ^ACHARSET: kludge, fallback to CHARSET_DEFAULT */
void charset_detect(const char *body, char *out, int outsz);

/* Convert raw body (in 'enc' charset) to UTF-8 */
int charset_body_to_utf8(const char *enc, const char *src, int srclen, char *dst, int dstsz);

/* Convert UTF-8 body to output charset for JAM storage */
int charset_body_from_utf8(const char *enc, const char *src, int srclen, char *dst, int dstsz);

/* Build CHRS kludge line (always null-terminates) */
void charset_build_kludge(const char *enc, char *out, int outsz);

/* Resolve alias to canonical name (e.g. "IBMPC" -> "CP437") */
const char *charset_resolve(const char *name);

/* Count UTF-8 characters lost when converted to out_cs */
int charset_count_lossy(const char *utf8, const char *out_cs);

/* Get list of unique charset names for popup (It should not be released, it returns a static array) */
const char **charset_get_list(int *count);

#endif /* WRAPPER_CHARSET_H */
