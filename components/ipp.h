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

#ifndef TE_IPP_H
#define TE_IPP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Subset of printer attributes used by the print dialog */
#define IPP_MAX_MEDIA 24
#define IPP_MAX_SIDES 3
#define IPP_MAX_COLOR 4
#define IPP_MAX_QUALITY 3
#define IPP_MAX_FORMATS 10
#define IPP_STR_MAX 64

    typedef struct
    {
        char printer_name[IPP_STR_MAX];
        int state; /* 3=idle 4=processing 5=stopped */
        char state_reason[IPP_STR_MAX];

        char media[IPP_MAX_MEDIA][IPP_STR_MAX];
        int n_media;
        int default_media; /* Index into media[], -1 if unset */

        char sides[IPP_MAX_SIDES][IPP_STR_MAX];
        int n_sides;
        int default_sides;

        char color_modes[IPP_MAX_COLOR][IPP_STR_MAX];
        int n_color_modes;
        int default_color_mode;

        int qualities[IPP_MAX_QUALITY]; /* enum values: 3=draft 4=normal 5=high */
        int n_qualities;
        int default_quality;

        char formats[IPP_MAX_FORMATS][IPP_STR_MAX];
        int n_formats;
        int supports_pdf; /* 1 if application/pdf is in formats */

        int max_copies; /* Upper bound of copies-supported */
    } IppPrinterInfo;

    /* Query printer attributes and fill info */
    int ipp_get_printer_info(const char *uri, IppPrinterInfo *info, char *err, size_t errsz);

    /* Send a document to a network printer via IPP */
    int ipp_print_document(const char *uri, const char *job_name, const char *doc_fmt, const unsigned char *doc, size_t doc_len, char *err, size_t errsz);

#ifdef __cplusplus
}
#endif

#endif
