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

/* Amiga printer.prefs reader using iffparse.library, with IPP-equivalent mappings */

#include <string.h>

#include "print_amiga.h"

#ifdef PLATFORM_AMIGA

#include <exec/types.h>
#include <dos/dos.h>
#include <libraries/iffparse.h>
#include <prefs/prefhdr.h>
#include <prefs/printertxt.h>
#include <prefs/printergfx.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/iffparse.h>

extern struct Library *IFFParseBase;

/* Read one prefs file via IFFParse. Returns 0 if PTXT or PGFX found, -1 otherwise */
static int amiga_read_prefs_file(const char *path, AmigaPrinterPrefs *out)
{
    struct IFFHandle *iff = NULL;
    BPTR fh = 0;
    LONG error;
    int ok = 0;

    if (!IFFParseBase)
        return -1;

    fh = Open((STRPTR)path, MODE_OLDFILE);

    if (!fh)
        return -1;

    iff = AllocIFF();

    if (!iff)
    {
        Close(fh);
        return -1;
    }

    iff->iff_Stream = (ULONG)fh;

    InitIFFasDOS(iff);

    if (OpenIFF(iff, IFFF_READ) != 0)
    {
        FreeIFF(iff);
        Close(fh);
        return -1;
    }

    /* Stop on both PTXT and PGFX so ParseIFF returns for either one */
    StopChunk(iff, ID_PREF, ID_PTXT);
    StopChunk(iff, ID_PREF, ID_PGFX);

    while ((error = ParseIFF(iff, IFFPARSE_SCAN)) == 0)
    {
        struct ContextNode *cn = CurrentChunk(iff);

        if (!cn)
            continue;

        if (cn->cn_ID == ID_PTXT)
        {
            struct PrinterTxtPrefs pt;

            memset(&pt, 0, sizeof(pt));

            if (ReadChunkBytes(iff, &pt, (LONG)sizeof(pt)) > 0)
            {
                size_t n;

                /* Copy driver name up to the first NUL, capped by out size */
                for (n = 0; n < sizeof(pt.pt_Driver) && pt.pt_Driver[n]; n++)
                    ;

                if (n >= sizeof(out->driver))
                    n = sizeof(out->driver) - 1;

                memcpy(out->driver, pt.pt_Driver, n);
                out->driver[n] = '\0';

                out->paper_size = (int)pt.pt_PaperSize;
                out->quality = (int)pt.pt_Quality;
                out->has_txt = 1;

                ok = 1;
            }
        }
        else if (cn->cn_ID == ID_PGFX)
        {
            struct PrinterGfxPrefs pg;

            memset(&pg, 0, sizeof(pg));

            if (ReadChunkBytes(iff, &pg, (LONG)sizeof(pg)) > 0)
            {
                out->shade = (int)pg.pg_Shade;
                out->has_gfx = 1;

                ok = 1;
            }
        }
    }

    CloseIFF(iff);
    FreeIFF(iff);
    Close(fh);

    return ok ? 0 : -1;
}

int amiga_read_printer_prefs(AmigaPrinterPrefs *out)
{
    if (!out)
        return -1;

    memset(out, 0, sizeof(*out));

    /* Try live ENV: first, then persistent ENVARC: for current driver config */
    if (amiga_read_prefs_file("ENV:sys/printer.prefs", out) == 0)
        return 0;

    if (amiga_read_prefs_file("ENVARC:sys/printer.prefs", out) == 0)
        return 0;

    return -1;
}

/* IPP media keyword -> Amiga PAPERSIZE_*; -1 unknown */
static int amiga_map_ipp_to_paper(const char *ipp)
{
    if (!ipp || !ipp[0])
        return -1;

    if (strcmp(ipp, "na_letter_8.5x11in") == 0)
        return 0;

    if (strcmp(ipp, "na_legal_8.5x14in") == 0)
        return 1;

    if (strcmp(ipp, "iso_a0_841x1189mm") == 0)
        return 5;

    if (strcmp(ipp, "iso_a1_594x841mm") == 0)
        return 6;

    if (strcmp(ipp, "iso_a2_420x594mm") == 0)
        return 7;

    if (strcmp(ipp, "iso_a3_297x420mm") == 0)
        return 8;

    if (strcmp(ipp, "iso_a4_210x297mm") == 0)
        return 9;

    if (strcmp(ipp, "iso_a5_148x210mm") == 0)
        return 10;

    if (strcmp(ipp, "iso_a6_105x148mm") == 0)
        return 11;

    if (strcmp(ipp, "iso_a7_74x105mm") == 0)
        return 12;

    if (strcmp(ipp, "iso_a8_52x74mm") == 0)
        return 13;

    if (strcmp(ipp, "na_executive_7.25x10.5in") == 0)
        return 14;

    return -1;
}

/* IPP quality 3/4/5 -> Amiga QUALITY_DRAFT/LETTER; -1 unknown */
static int amiga_map_ipp_to_quality(int ipp_quality)
{
    switch (ipp_quality)
    {
    case 3:
        return 0;
    case 4:
        return 1;
    case 5:
        return 1;
    default:
        return -1;
    }
}

/* IPP color mode -> Amiga SHADE_*; -1 unknown */
static int amiga_map_ipp_to_shade(const char *ipp)
{
    if (!ipp || !ipp[0])
        return -1;

    if (strcmp(ipp, "color") == 0)
        return 2;

    if (strcmp(ipp, "monochrome") == 0)
        return 0;

    return -1;
}

/* Rewrite prefs, copying every chunk over and mutating PTXT/PGFX values */
static int amiga_write_prefs_file(const char *src_path, const char *dst_path, int new_paper, int new_quality, int new_shade)
{
    struct IFFHandle *in = NULL;
    struct IFFHandle *out = NULL;
    BPTR in_fh = 0;
    BPTR out_fh = 0;
    LONG error;
    int ok = 0;

    if (!IFFParseBase)
        return -1;

    in_fh = Open((STRPTR)src_path, MODE_OLDFILE);

    if (!in_fh)
        return -1;

    out_fh = Open((STRPTR)dst_path, MODE_NEWFILE);

    if (!out_fh)
    {
        Close(in_fh);
        return -1;
    }

    in = AllocIFF();
    out = AllocIFF();

    if (!in || !out)
    {
        if (in)
            FreeIFF(in);

        if (out)
            FreeIFF(out);

        Close(in_fh);
        Close(out_fh);

        return -1;
    }

    in->iff_Stream = (ULONG)in_fh;
    out->iff_Stream = (ULONG)out_fh;

    InitIFFasDOS(in);
    InitIFFasDOS(out);

    if (OpenIFF(in, IFFF_READ) != 0)
    {
        FreeIFF(in);
        FreeIFF(out);

        Close(in_fh);
        Close(out_fh);

        return -1;
    }

    if (OpenIFF(out, IFFF_WRITE) != 0)
    {
        CloseIFF(in);

        FreeIFF(in);
        FreeIFF(out);

        Close(in_fh);
        Close(out_fh);

        return -1;
    }

    PropChunk(in, ID_PREF, ID_PRHD);
    StopChunk(in, ID_PREF, ID_PTXT);
    StopChunk(in, ID_PREF, ID_PGFX);
    StopChunk(in, ID_PREF, ID_PUNT);
    StopChunk(in, ID_PREF, ID_PDEV);

    if (PushChunk(out, ID_PREF, ID_FORM, IFFSIZE_UNKNOWN) != 0)
    {
        CloseIFF(in);
        CloseIFF(out);

        FreeIFF(in);
        FreeIFF(out);

        Close(in_fh);
        Close(out_fh);

        return -1;
    }

    {
        struct StoredProperty *sp = FindProp(in, ID_PREF, ID_PRHD);

        if (sp)
        {
            if (PushChunk(out, 0, ID_PRHD, IFFSIZE_UNKNOWN) == 0)
            {
                WriteChunkBytes(out, sp->sp_Data, sp->sp_Size);
                PopChunk(out);
            }
        }
    }

    while ((error = ParseIFF(in, IFFPARSE_SCAN)) == 0)
    {
        struct ContextNode *cn = CurrentChunk(in);

        if (!cn)
            continue;

        if (cn->cn_ID == ID_PTXT)
        {
            struct PrinterTxtPrefs pt;

            memset(&pt, 0, sizeof(pt));

            if (ReadChunkBytes(in, &pt, (LONG)sizeof(pt)) > 0)
            {
                if (new_paper >= 0)
                    pt.pt_PaperSize = (UWORD)new_paper;

                if (new_quality >= 0)
                    pt.pt_Quality = (UBYTE)new_quality;

                if (PushChunk(out, 0, ID_PTXT, IFFSIZE_UNKNOWN) == 0)
                {
                    WriteChunkBytes(out, &pt, (LONG)sizeof(pt));
                    PopChunk(out);
                    ok = 1;
                }
            }
        }
        else if (cn->cn_ID == ID_PGFX)
        {
            struct PrinterGfxPrefs pg;

            memset(&pg, 0, sizeof(pg));

            if (ReadChunkBytes(in, &pg, (LONG)sizeof(pg)) > 0)
            {
                if (new_shade >= 0)
                    pg.pg_Shade = (UWORD)new_shade;

                if (PushChunk(out, 0, ID_PGFX, IFFSIZE_UNKNOWN) == 0)
                {
                    WriteChunkBytes(out, &pg, (LONG)sizeof(pg));
                    PopChunk(out);
                    ok = 1;
                }
            }
        }
        else if (cn->cn_ID == ID_PUNT || cn->cn_ID == ID_PDEV)
        {
            unsigned char buf[256];
            LONG remaining = cn->cn_Size;

            if (PushChunk(out, 0, cn->cn_ID, IFFSIZE_UNKNOWN) == 0)
            {
                while (remaining > 0)
                {
                    LONG want = remaining > (LONG)sizeof(buf) ? (LONG)sizeof(buf) : remaining;
                    LONG got = ReadChunkBytes(in, buf, want);

                    if (got <= 0)
                        break;

                    WriteChunkBytes(out, buf, got);
                    remaining -= got;
                }

                PopChunk(out);
            }
        }
    }

    PopChunk(out);

    CloseIFF(in);
    CloseIFF(out);

    FreeIFF(in);
    FreeIFF(out);

    Close(in_fh);
    Close(out_fh);

    return ok ? 0 : -1;
}

int amiga_apply_printer_prefs(const char *ipp_media, const char *ipp_color_mode, int ipp_quality)
{
    /* Same device as target (ENV: is a single volume), so Rename() cannot fail cross-device */
    const char *tmp_path = "ENV:sys/tinyedit_printer.prefs.tmp";
    int new_paper;
    int new_quality;
    int new_shade;
    int rc;

    new_paper = amiga_map_ipp_to_paper(ipp_media);
    new_quality = amiga_map_ipp_to_quality(ipp_quality);
    new_shade = amiga_map_ipp_to_shade(ipp_color_mode);

    if (new_paper < 0 && new_quality < 0 && new_shade < 0)
        return 0;

    rc = amiga_write_prefs_file("ENV:sys/printer.prefs", tmp_path, new_paper, new_quality, new_shade);

    if (rc != 0)
    {
        rc = amiga_write_prefs_file("ENVARC:sys/printer.prefs", tmp_path, new_paper, new_quality, new_shade);

        if (rc != 0)
            return -1;
    }

    DeleteFile((STRPTR) "ENV:sys/printer.prefs");

    /* Rename() returns non-zero on success, 0 on failure */
    if (Rename((STRPTR)tmp_path, (STRPTR) "ENV:sys/printer.prefs") == 0)
    {
        DeleteFile((STRPTR)tmp_path);
        return -1;
    }

    return 0;
}

#else

int amiga_read_printer_prefs(AmigaPrinterPrefs *out)
{
    if (out)
        memset(out, 0, sizeof(*out));

    return -1;
}

int amiga_apply_printer_prefs(const char *ipp_media, const char *ipp_color_mode, int ipp_quality)
{
    return -1;
}

#endif /* PLATFORM_AMIGA */

/* Enum mappings usable from any platform (they are pure lookups) */
const char *amiga_map_paper_to_ipp(int paper_size_enum)
{
    /* Map Amiga paper size enums to IPP media keywords; codes kept locally for non-Amiga builds */
    switch (paper_size_enum)
    {
    case 0:
        return "na_letter_8.5x11in"; /* PAPERSIZE_US_LETTER */
    case 1:
        return "na_legal_8.5x14in"; /* PAPERSIZE_US_LEGAL */
    /* 2, 3 = N_TRACTOR / W_TRACTOR: no direct IPP equivalent */
    /* 4 = CUSTOM: unknown; caller keeps default */
    case 5:
        return "iso_a0_841x1189mm"; /* PAPERSIZE_EURO_A0 */
    case 6:
        return "iso_a1_594x841mm";
    case 7:
        return "iso_a2_420x594mm";
    case 8:
        return "iso_a3_297x420mm";
    case 9:
        return "iso_a4_210x297mm"; /* the common one */
    case 10:
        return "iso_a5_148x210mm";
    case 11:
        return "iso_a6_105x148mm";
    case 12:
        return "iso_a7_74x105mm";
    case 13:
        return "iso_a8_52x74mm";
    case 14:
        return "na_executive_7.25x10.5in"; /* PAPERSIZE_US_EXECUTIVE */
    default:
        return "";
    }
}

int amiga_map_quality_to_ipp(int quality_enum)
{
    /* QUALITY_DRAFT = 0, QUALITY_LETTER = 1. Amiga has no equivalent for IPP's "high" (5) so QUALITY_LETTER maps to "normal" (4) */
    switch (quality_enum)
    {
    case 0:
        return 3; /* draft */
    case 1:
        return 4; /* normal */
    default:
        return 0;
    }
}

const char *amiga_map_shade_to_ipp(int shade_enum)
{
    /* Map Amiga shade enums to IPP color modes; only SHADE_COLOR (2) maps to "color" */
    switch (shade_enum)
    {
    case 0:
        return "monochrome";
    case 1:
        return "monochrome";
    case 2:
        return "color";
    case 3:
        return "monochrome";
    default:
        return "";
    }
}
