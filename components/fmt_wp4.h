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

/* WP 4.x dialect to emit on export. The importer is tolerant of both */
enum
{
    WP4_VARIANT_DOS42 = 0,  /* WP 4.2 DOS: 0x09 tabs, 0x81 justify, 0x9F at BOF, scol=left margin */
    WP4_VARIANT_AMIGA41 = 1 /* WP 4.1.12 Amiga: 0x09 tabs, no justify marker, scol=right_margin-text_len */
};

#ifdef __cplusplus
extern "C"
{
#endif

    /* Import WP 4.x (tolerant of both DOS 4.2 and Amiga 4.1.12); *hyph_out=1 if 0x9F present, 0 otherwise */
    int wp4_import(struct Ed *ed, FILE *fp, const char *charset, char *err, size_t errsz, char *warn, size_t warnsz, int *hyph_out);

    /* Write ed as WP 4.x; variant=WP4_VARIANT_*, hyph emits 0x9F */
    int wp4_export(const struct Ed *ed, FILE *fp, const char *charset, int hyph, int variant, char *err, size_t errsz, char *warn, size_t warnsz);

    /* Pick the export variant from a filename: .wp4 -> DOS 4.2, .wp -> Amiga 4.1.12 */
    int wp4_variant_for_path(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* FMT_WP4_H */