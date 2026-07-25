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
#define IPP_MAX_ORIENTATIONS 4
#define IPP_MAX_NUMBER_UP 6
#define IPP_MAX_RESOLUTIONS 16
#define IPP_MAX_REASONS 8
#define IPP_MAX_SOURCES 8
#define IPP_MAX_TYPES 8
#define IPP_STR_MAX 64

    typedef struct
    {
        int cross_feed;
        int feed;
        int units; /* 3=dpi 4=dpc */
    } IppResolution;

    typedef struct
    {
        char printer_name[IPP_STR_MAX];
        int state; /* 3=idle 4=processing 5=stopped */
        char state_reason[IPP_STR_MAX];
        char state_reasons[IPP_MAX_REASONS][IPP_STR_MAX];
        int n_state_reasons;
        char state_message[IPP_STR_MAX];
        int accepting_jobs;     /* -1=unknown, 0=no, 1=yes */
        int queued_job_count;   /* -1=unknown */
        char info[IPP_STR_MAX]; /* printer-info */
        char make_and_model[IPP_STR_MAX];
        char location[IPP_STR_MAX];

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
        int default_copies;

        int orientations[IPP_MAX_ORIENTATIONS]; /* 3=portrait 4=landscape 5=reverse-landscape 6=reverse-portrait */
        int n_orientations;
        int default_orientation;

        int number_ups[IPP_MAX_NUMBER_UP]; /* 1, 2, 4, 6, 9, 16 */
        int n_number_ups;
        int default_number_up;

        char media_sources[IPP_MAX_SOURCES][IPP_STR_MAX];
        int n_media_sources;
        int default_media_source;

        char media_types[IPP_MAX_TYPES][IPP_STR_MAX];
        int n_media_types;
        int default_media_type;

        IppResolution resolutions[IPP_MAX_RESOLUTIONS];
        int n_resolutions;
        int default_resolution;
    } IppPrinterInfo;

    /* Optional job attributes; unset fields use printer defaults, NULL means none */
    typedef struct
    {
        const char *media;      /* e.g. "iso_a4_210x297mm" */
        const char *sides;      /* e.g. "one-sided" */
        const char *color_mode; /* "auto" / "color" / "monochrome" */
        int quality;            /* 0=unset, 3=draft, 4=normal, 5=high */
        int copies;             /* 0=unset, 1..N */

        int orientation; /* 0=unset, 3=portrait, 4=landscape, 5=reverse-landscape, 6=reverse-portrait */
        int number_up;   /* 0=unset, pages per sheet */
        const char *media_source;
        const char *media_type;
        int resolution_x;
        int resolution_y;
        int resolution_units; /* 3=dpi, 4=dpc */
    } IppJobAttrs;

    /* Query printer attributes and fill info */
    int ipp_get_printer_info(const char *uri, IppPrinterInfo *info, char *err, size_t errsz);

    /* Send a document to a network printer via IPP. attrs may be NULL */
    int ipp_print_document(const char *uri, const char *job_name, const char *doc_fmt, const IppJobAttrs *attrs, const unsigned char *doc, size_t doc_len, char *err, size_t errsz);

#ifdef __cplusplus
}
#endif

#endif
