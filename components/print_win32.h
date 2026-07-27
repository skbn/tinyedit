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

#ifndef TE_PRINT_WIN32_H
#define TE_PRINT_WIN32_H

#include <stddef.h>

#include "config.h"

struct Ed;

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef PRWIN_MAX_LIST
#define PRWIN_MAX_LIST 12
#endif

#ifndef PRWIN_MAX_RES
#define PRWIN_MAX_RES 8
#endif

#ifndef PRWIN_MAX_BINS
#define PRWIN_MAX_BINS 12
#endif

#ifndef PRWIN_MAX_TYPES
#define PRWIN_MAX_TYPES 12
#endif

    /* One printer resolution pair, values are DPI on Windows */
    typedef struct
    {
        int x_dpi;
        int y_dpi;
    } Win32Resolution;

    /* Capabilities of a local Windows printer, populated from WinSpool + DeviceCapabilitiesW */
    typedef struct
    {
        char printer_name[64];
        char media[PRWIN_MAX_LIST][64];
        int n_media;
        int default_media;
        char sides[3][32];
        int n_sides;
        int default_sides;
        char color_modes[2][16];
        int n_color_modes;
        int default_color_mode;
        int qualities[3];
        int n_qualities;
        int default_quality;
        int orientations[2];
        int n_orientations;
        int default_orientation;
        char media_sources[PRWIN_MAX_BINS][64];
        int n_media_sources;
        int default_media_source;
        char media_types[PRWIN_MAX_TYPES][64];
        int n_media_types;
        int default_media_type;

        /* Resolutions from DC_ENUMRESOLUTIONS, mapped later to IPP resolutions */
        Win32Resolution resolutions[PRWIN_MAX_RES];
        int n_resolutions;
        int default_resolution; /* index; -1 if unknown */

        int max_copies;
    } Win32PrinterInfo;

    /* Enumerate local + network-attached printers into names[]; returns count or -1 */
    int win32_list_printers(char names[][64], int max);

    /* Get system default printer name; returns 0 on success, -1 otherwise */
    int win32_default_printer(char *out, size_t outsz);

    /* Populate info from DeviceCapabilitiesW; returns 0 on success, -1 on failure */
    int win32_get_printer_info(const char *printer_name, Win32PrinterInfo *info);

    /* Shell-print file_path to printer_name (NULL = system default); returns 0 or -1 */
    int win32_print_file(const char *printer_name, const char *file_path);

    int win32_print_rtf_document(const struct Ed *ed, const TeConfig *cfg, const char *job_name);

#ifdef __cplusplus
}
#endif

#endif
