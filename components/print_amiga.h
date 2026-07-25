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

#ifndef TE_PRINT_AMIGA_H
#define TE_PRINT_AMIGA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Printer prefs from PTXT/PGFX chunks; unread fields are 0, enums are raw Amiga codes */
    typedef struct
    {
        char driver[64]; /* PrinterTxtPrefs.pt_Driver, e.g. "EpsonQ" */
        int paper_size;  /* PrinterTxtPrefs.pt_PaperSize (PAPERSIZE_*) */
        int quality;     /* PrinterTxtPrefs.pt_Quality (DRAFT / LETTER) */
        int shade;       /* PrinterGfxPrefs.pg_Shade (SHADE_*) */
        int has_txt;     /* 1 if PTXT chunk was found */
        int has_gfx;     /* 1 if PGFX chunk was found */
    } AmigaPrinterPrefs;

    /* Read printer.prefs (ENV: then ENVARC:); returns 0 on success, -1 on failure */
    int amiga_read_printer_prefs(AmigaPrinterPrefs *out);

    /* Apply IPP-style choices back to ENV:sys/printer.prefs; empty/0 = keep prior; -1 on failure */
    int amiga_apply_printer_prefs(const char *ipp_media, const char *ipp_color_mode, int ipp_quality);

    /* Map Amiga PAPERSIZE_* to IPP keyword; returns "" for unknown */
    const char *amiga_map_paper_to_ipp(int paper_size_enum);

    /* Map Amiga QUALITY_* to IPP quality enum (3=draft,4=normal,5=high); 0 for unknown */
    int amiga_map_quality_to_ipp(int quality_enum);

    /* Map Amiga SHADE_* to IPP color-mode keyword; returns "" for unknown */
    const char *amiga_map_shade_to_ipp(int shade_enum);

#ifdef __cplusplus
}
#endif

#endif
