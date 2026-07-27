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

/* Windows printer enumeration and capabilities via WinSpool + DeviceCapabilitiesW */

#include <string.h>
#include <stdio.h>

#include "print_win32.h"
#include "editor.h"
#include "fmt_rtf.h"

#ifdef PLATFORM_WIN32

#include <windows.h>
#include <winspool.h>
#include <shellapi.h>
#include <richedit.h>

#if defined(USE_FREETYPE) || defined(HAVE_URF)
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#ifndef CCHBINNAME
#define CCHBINNAME 24
#endif

#define WIN32_PRINT_MARGIN_TWIPS 1440

typedef struct
{
    const unsigned char *data;
    size_t length;
    size_t offset;
} W32RtfStream;

/* UTF-16 -> UTF-8 with guaranteed NUL terminator */
static void w32_utf16_to_utf8(const wchar_t *w, char *out, size_t outsz)
{
    int n;

    if (!out || outsz == 0)
        return;

    out[0] = '\0';

    if (!w)
        return;

    n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)outsz, NULL, NULL);

    if (n <= 0)
        out[0] = '\0';
    else
        out[outsz - 1] = '\0';
}

/* UTF-8 -> UTF-16 with guaranteed NUL terminator */
static void w32_utf8_to_utf16(const char *s, wchar_t *out, int outcnt)
{
    int n;

    if (!out || outcnt <= 0)
        return;

    out[0] = L'\0';

    if (!s)
        return;

    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, outcnt);

    if (n <= 0)
        out[0] = L'\0';
    else
        out[outcnt - 1] = L'\0';
}

/* Map DMPAPER_* code to IPP media keyword */
static void w32_paper_to_ipp(WORD dmpaper, char *out, size_t outsz)
{
    const char *k = NULL;
    char buf[64];

    switch (dmpaper)
    {
    case DMPAPER_LETTER:
        k = "na_letter_8.5x11in";
        break;
    case DMPAPER_LEGAL:
        k = "na_legal_8.5x14in";
        break;
    case DMPAPER_A3:
        k = "iso_a3_297x420mm";
        break;
    case DMPAPER_A4:
        k = "iso_a4_210x297mm";
        break;
    case DMPAPER_A5:
        k = "iso_a5_148x210mm";
        break;
    case DMPAPER_A6:
        k = "iso_a6_105x148mm";
        break;
    case DMPAPER_B4:
        k = "iso_b4_250x353mm";
        break;
    case DMPAPER_B5:
        k = "iso_b5_176x250mm";
        break;
    case DMPAPER_EXECUTIVE:
        k = "na_executive_7.25x10.5in";
        break;
    case DMPAPER_TABLOID:
        k = "na_ledger_11x17in";
        break;
    case DMPAPER_ENV_10:
        k = "na_number-10_4.125x9.5in";
        break;
    case DMPAPER_ENV_DL:
        k = "iso_dl_110x220mm";
        break;
    case DMPAPER_ENV_C5:
        k = "iso_c5_162x229mm";
        break;
    case DMPAPER_ENV_C6:
        k = "iso_c6_114x162mm";
        break;
    default:
        snprintf(buf, sizeof(buf), "custom_dmpaper_%d", (int)dmpaper);
        k = buf;
        break;
    }

    strncpy(out, k, outsz - 1);
    out[outsz - 1] = '\0';
}

static void w32_bin_to_attr(WORD bin, char *out, size_t outsz)
{
    const char *value = NULL;

    switch (bin)
    {
    case DMBIN_AUTO:
        value = "auto";
        break;
    case DMBIN_UPPER:
        value = "tray-1";
        break;
    case DMBIN_LOWER:
        value = "tray-2";
        break;
    case DMBIN_MIDDLE:
        value = "tray-3";
        break;
    case DMBIN_MANUAL:
        value = "manual";
        break;
    case DMBIN_ENVELOPE:
        value = "envelope";
        break;
    default:
        snprintf(out, outsz, "win32-bin-%u", (unsigned int)bin);
        return;
    }

    strncpy(out, value, outsz - 1);
    out[outsz - 1] = '\0';
}

static WORD w32_attr_to_bin(const char *value)
{
    unsigned int bin;
    char tail;

    if (!value)
        return 0;

    if (strcmp(value, "auto") == 0)
        return DMBIN_AUTO;

    if (strcmp(value, "tray-1") == 0)
        return DMBIN_UPPER;

    if (strcmp(value, "tray-2") == 0)
        return DMBIN_LOWER;

    if (strcmp(value, "tray-3") == 0)
        return DMBIN_MIDDLE;

    if (strcmp(value, "manual") == 0)
        return DMBIN_MANUAL;

    if (strcmp(value, "envelope") == 0)
        return DMBIN_ENVELOPE;

    if (sscanf(value, "win32-bin-%u%c", &bin, &tail) == 1 && bin <= 65535U)
        return (WORD)bin;

    return 0;
}

static void w32_media_type_to_attr(DWORD type, char *out, size_t outsz)
{
    if (type == DMMEDIA_STANDARD)
        strncpy(out, "stationery", outsz - 1);
    else if (type == DMMEDIA_GLOSSY)
        strncpy(out, "photographic", outsz - 1);
    else
    {
        snprintf(out, outsz, "win32-media-%lu", (unsigned long)type);
        return;
    }

    out[outsz - 1] = '\0';
}

static DWORD w32_attr_to_media_type(const char *value)
{
    unsigned long type;
    char tail;

    if (!value)
        return 0;

    if (strcmp(value, "stationery") == 0)
        return DMMEDIA_STANDARD;

    if (strcmp(value, "photographic") == 0)
        return DMMEDIA_GLOSSY;

    if (sscanf(value, "win32-media-%lu%c", &type, &tail) == 1 && type <= 65535UL)
        return (DWORD)type;

    return 0;
}

int win32_list_printers(char names[][64], int max)
{
    DWORD needed = 0;
    DWORD returned = 0;
    BYTE *buf = NULL;
    PRINTER_INFO_4W *pi = NULL;
    int i;
    int n_out = 0;

    if (!names || max <= 0)
        return -1;

    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 4, NULL, 0, &needed, &returned);

    if (needed == 0)
        return 0;

    buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, needed);

    if (!buf)
        return -1;

    if (!EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 4, buf, needed, &needed, &returned))
    {
        HeapFree(GetProcessHeap(), 0, buf);
        return -1;
    }

    pi = (PRINTER_INFO_4W *)buf;

    for (i = 0; i < (int)returned && n_out < max; i++)
    {
        if (pi[i].pPrinterName)
            w32_utf16_to_utf8(pi[i].pPrinterName, names[n_out++], 64);
    }

    HeapFree(GetProcessHeap(), 0, buf);

    return n_out;
}

int win32_default_printer(char *out, size_t outsz)
{
    wchar_t wbuf[512];
    DWORD count;

    if (!out || outsz == 0)
        return -1;

    out[0] = '\0';
    count = (DWORD)(sizeof(wbuf) / sizeof(wbuf[0]));

    if (!GetDefaultPrinterW(wbuf, &count))
        return -1;

    w32_utf16_to_utf8(wbuf, out, outsz);

    return out[0] ? 0 : -1;
}

int win32_get_printer_info(const char *printer_name, Win32PrinterInfo *info)
{
    wchar_t wprinter[256];
    DEVMODEW *dm = NULL;
    HANDLE h = NULL;
    LONG needed;
    LONG n_papers;
    LONG has_color;
    LONG duplex;
    LONG max_copies;
    LONG n_bins;
    LONG n_types;
    int i;
    LONG n_res;

    if (!printer_name || !info)
        return -1;

    memset(info, 0, sizeof(*info));

    info->default_media = -1;
    info->default_sides = -1;
    info->default_color_mode = -1;
    info->default_quality = -1;
    info->default_orientation = -1;
    info->default_media_source = -1;
    info->default_media_type = -1;

    w32_utf8_to_utf16(printer_name, wprinter, (int)(sizeof(wprinter) / sizeof(wprinter[0])));

    strncpy(info->printer_name, printer_name, sizeof(info->printer_name) - 1);
    info->printer_name[sizeof(info->printer_name) - 1] = '\0';

    /* Fetch current DEVMODE for the printer's current defaults */
    if (OpenPrinterW(wprinter, &h, NULL))
    {
        needed = DocumentPropertiesW(NULL, h, wprinter, NULL, NULL, 0);

        if (needed > 0)
        {
            dm = (DEVMODEW *)HeapAlloc(GetProcessHeap(), 0, needed);

            if (dm)
            {
                if (DocumentPropertiesW(NULL, h, wprinter, dm, NULL, DM_OUT_BUFFER) < 0)
                {
                    HeapFree(GetProcessHeap(), 0, dm);
                    dm = NULL;
                }
            }
        }

        ClosePrinter(h);
    }

    /* Paper sizes: DC_PAPERS returns DMPAPER_* codes */
    n_papers = DeviceCapabilitiesW(wprinter, NULL, DC_PAPERS, NULL, NULL);

    if (n_papers > 0)
    {
        WORD *papers = (WORD *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n_papers * sizeof(WORD));

        if (papers)
        {
            if (DeviceCapabilitiesW(wprinter, NULL, DC_PAPERS, (LPWSTR)papers, NULL) == n_papers)
            {
                for (i = 0; i < n_papers && info->n_media < PRWIN_MAX_LIST; i++)
                {
                    w32_paper_to_ipp(papers[i], info->media[info->n_media], sizeof(info->media[0]));

                    if (dm && dm->dmPaperSize == papers[i])
                        info->default_media = info->n_media;

                    info->n_media++;
                }
            }

            HeapFree(GetProcessHeap(), 0, papers);
        }
    }

    /* Color capability: DC_COLORDEVICE == 1 for color, 0 for mono */
    has_color = DeviceCapabilitiesW(wprinter, NULL, DC_COLORDEVICE, NULL, NULL);

    if (has_color >= 0)
    {
        strncpy(info->color_modes[info->n_color_modes], "monochrome", sizeof(info->color_modes[0]) - 1);
        info->color_modes[info->n_color_modes][sizeof(info->color_modes[0]) - 1] = '\0';

        if (dm && dm->dmColor == DMCOLOR_MONOCHROME)
            info->default_color_mode = info->n_color_modes;

        info->n_color_modes++;

        if (has_color > 0)
        {
            strncpy(info->color_modes[info->n_color_modes], "color", sizeof(info->color_modes[0]) - 1);
            info->color_modes[info->n_color_modes][sizeof(info->color_modes[0]) - 1] = '\0';

            if (dm && dm->dmColor == DMCOLOR_COLOR)
                info->default_color_mode = info->n_color_modes;

            info->n_color_modes++;
        }
    }

    /* Duplex: DC_DUPLEX == 1 if two-sided is supported */
    duplex = DeviceCapabilitiesW(wprinter, NULL, DC_DUPLEX, NULL, NULL);

    strncpy(info->sides[info->n_sides], "one-sided", sizeof(info->sides[0]) - 1);
    info->sides[info->n_sides][sizeof(info->sides[0]) - 1] = '\0';

    if (dm && dm->dmDuplex == DMDUP_SIMPLEX)
        info->default_sides = info->n_sides;

    info->n_sides++;

    if (duplex > 0)
    {
        strncpy(info->sides[info->n_sides], "two-sided-long-edge", sizeof(info->sides[0]) - 1);
        info->sides[info->n_sides][sizeof(info->sides[0]) - 1] = '\0';

        if (dm && dm->dmDuplex == DMDUP_VERTICAL)
            info->default_sides = info->n_sides;

        info->n_sides++;

        strncpy(info->sides[info->n_sides], "two-sided-short-edge", sizeof(info->sides[0]) - 1);
        info->sides[info->n_sides][sizeof(info->sides[0]) - 1] = '\0';

        if (dm && dm->dmDuplex == DMDUP_HORIZONTAL)
            info->default_sides = info->n_sides;

        info->n_sides++;
    }

    /* Map DEVMODE print quality to IPP draft, normal and high levels */
    info->qualities[0] = 3;
    info->qualities[1] = 4;
    info->qualities[2] = 5;
    info->n_qualities = 3;
    info->default_quality = 1;

    if (dm)
    {
        switch (dm->dmPrintQuality)
        {
        case DMRES_DRAFT:
            info->default_quality = 0;
            break;
        case DMRES_LOW:
            info->default_quality = 1;
            break;
        case DMRES_MEDIUM:
            info->default_quality = 1;
            break;
        case DMRES_HIGH:
            info->default_quality = 2;
            break;
        default: /* positive DPI: leave normal */
            break;
        }
    }

    if (dm && (dm->dmFields & DM_ORIENTATION))
    {
        info->orientations[0] = 3;
        info->orientations[1] = 4;
        info->n_orientations = 2;

        if (dm->dmOrientation == DMORIENT_PORTRAIT)
            info->default_orientation = 0;
        else if (dm->dmOrientation == DMORIENT_LANDSCAPE)
            info->default_orientation = 1;
    }

    n_bins = DeviceCapabilitiesW(wprinter, NULL, DC_BINS, NULL, NULL);

    if (n_bins > 0)
    {
        WORD *bins = (WORD *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n_bins * sizeof(WORD));

        if (bins)
        {
            if (DeviceCapabilitiesW(wprinter, NULL, DC_BINS, (LPWSTR)bins, NULL) == n_bins)
            {
                for (i = 0; i < n_bins && info->n_media_sources < PRWIN_MAX_BINS; i++)
                {
                    w32_bin_to_attr(bins[i], info->media_sources[info->n_media_sources], sizeof(info->media_sources[0]));

                    if (dm && dm->dmDefaultSource == bins[i])
                        info->default_media_source = info->n_media_sources;

                    info->n_media_sources++;
                }
            }

            HeapFree(GetProcessHeap(), 0, bins);
        }
    }

    n_types = DeviceCapabilitiesW(wprinter, NULL, DC_MEDIATYPES, NULL, NULL);

    if (n_types > 0)
    {
        DWORD *types = (DWORD *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n_types * sizeof(DWORD));

        if (types)
        {
            if (DeviceCapabilitiesW(wprinter, NULL, DC_MEDIATYPES, (LPWSTR)types, NULL) == n_types)
            {
                for (i = 0; i < n_types && info->n_media_types < PRWIN_MAX_TYPES; i++)
                {
                    w32_media_type_to_attr(types[i], info->media_types[info->n_media_types], sizeof(info->media_types[0]));

                    if (dm && dm->dmMediaType == types[i])
                        info->default_media_type = info->n_media_types;

                    info->n_media_types++;
                }
            }

            HeapFree(GetProcessHeap(), 0, types);
        }
    }

    /* Resolutions: DC_ENUMRESOLUTIONS returns pairs of LONGs [x, y] in DPI */
    n_res = DeviceCapabilitiesW(wprinter, NULL, DC_ENUMRESOLUTIONS, NULL, NULL);

    info->default_resolution = -1;

    if (n_res > 0)
    {
        LONG *res_buf = (LONG *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n_res * 2 * sizeof(LONG));

        if (res_buf)
        {
            if (DeviceCapabilitiesW(wprinter, NULL, DC_ENUMRESOLUTIONS, (LPWSTR)res_buf, NULL) == n_res)
            {
                LONG r;

                for (r = 0; r < n_res && info->n_resolutions < PRWIN_MAX_RES; r++)
                {
                    int xd = (int)res_buf[r * 2 + 0];
                    int yd = (int)res_buf[r * 2 + 1];

                    info->resolutions[info->n_resolutions].x_dpi = xd;
                    info->resolutions[info->n_resolutions].y_dpi = yd;

                    /* If DEVMODE has an absolute DPI in dmPrintQuality that matches, remember it */
                    if (dm && dm->dmPrintQuality > 0 && dm->dmPrintQuality == xd)
                        info->default_resolution = info->n_resolutions;

                    info->n_resolutions++;
                }
            }

            HeapFree(GetProcessHeap(), 0, res_buf);
        }
    }

    /* Copies: DC_COPIES gives the driver's upper limit */
    max_copies = DeviceCapabilitiesW(wprinter, NULL, DC_COPIES, NULL, NULL);
    info->max_copies = max_copies > 0 ? (int)max_copies : 1;

    if (dm)
        HeapFree(GetProcessHeap(), 0, dm);

    return 0;
}

static WORD w32_ipp_to_paper(const char *media)
{
    unsigned int paper;
    char tail;

    if (!media || !media[0])
        return 0;

    if (strcmp(media, "na_letter_8.5x11in") == 0)
        return DMPAPER_LETTER;

    if (strcmp(media, "na_legal_8.5x14in") == 0)
        return DMPAPER_LEGAL;

    if (strcmp(media, "iso_a3_297x420mm") == 0)
        return DMPAPER_A3;

    if (strcmp(media, "iso_a4_210x297mm") == 0)
        return DMPAPER_A4;

    if (strcmp(media, "iso_a5_148x210mm") == 0)
        return DMPAPER_A5;

    if (strcmp(media, "iso_a6_105x148mm") == 0)
        return DMPAPER_A6;

    if (strcmp(media, "iso_b4_250x353mm") == 0)
        return DMPAPER_B4;

    if (strcmp(media, "iso_b5_176x250mm") == 0)
        return DMPAPER_B5;

    if (strcmp(media, "na_executive_7.25x10.5in") == 0)
        return DMPAPER_EXECUTIVE;

    if (strcmp(media, "na_ledger_11x17in") == 0)
        return DMPAPER_TABLOID;

    if (strcmp(media, "na_number-10_4.125x9.5in") == 0)
        return DMPAPER_ENV_10;

    if (strcmp(media, "iso_dl_110x220mm") == 0)
        return DMPAPER_ENV_DL;

    if (strcmp(media, "iso_c5_162x229mm") == 0)
        return DMPAPER_ENV_C5;

    if (strcmp(media, "iso_c6_114x162mm") == 0)
        return DMPAPER_ENV_C6;

    if (sscanf(media, "custom_dmpaper_%u%c", &paper, &tail) == 1 && paper <= 65535U)
        return (WORD)paper;

    return 0;
}

static void w32_apply_devmode(DEVMODEW *dm, const TeConfig *cfg)
{
    WORD paper;
    DWORD supported_fields;
    DWORD media_type;
    int dpi_x;
    int dpi_y;

    if (!dm || !cfg)
        return;

    supported_fields = dm->dmFields;
    paper = w32_ipp_to_paper(cfg->print_media);

    if (paper && (supported_fields & DM_PAPERSIZE))
    {
        dm->dmPaperSize = (short)paper;
        dm->dmFields |= DM_PAPERSIZE;
    }

    if ((supported_fields & DM_ORIENTATION) && (cfg->print_orientation == 3 || cfg->print_orientation == 6))
    {
        dm->dmOrientation = DMORIENT_PORTRAIT;
        dm->dmFields |= DM_ORIENTATION;
    }
    else if ((supported_fields & DM_ORIENTATION) && (cfg->print_orientation == 4 || cfg->print_orientation == 5))
    {
        dm->dmOrientation = DMORIENT_LANDSCAPE;
        dm->dmFields |= DM_ORIENTATION;
    }

    if ((supported_fields & DM_COPIES) && cfg->print_copies > 0 && cfg->print_copies <= 32767)
    {
        dm->dmCopies = (short)cfg->print_copies;
        dm->dmFields |= DM_COPIES;
    }

    if ((supported_fields & DM_COLOR) && strcmp(cfg->print_color_mode, "color") == 0)
    {
        dm->dmColor = DMCOLOR_COLOR;
        dm->dmFields |= DM_COLOR;
    }
    else if ((supported_fields & DM_COLOR) && strcmp(cfg->print_color_mode, "monochrome") == 0)
    {
        dm->dmColor = DMCOLOR_MONOCHROME;
        dm->dmFields |= DM_COLOR;
    }

    if ((supported_fields & DM_DUPLEX) && strcmp(cfg->print_sides, "one-sided") == 0)
    {
        dm->dmDuplex = DMDUP_SIMPLEX;
        dm->dmFields |= DM_DUPLEX;
    }
    else if ((supported_fields & DM_DUPLEX) && strcmp(cfg->print_sides, "two-sided-long-edge") == 0)
    {
        dm->dmDuplex = DMDUP_VERTICAL;
        dm->dmFields |= DM_DUPLEX;
    }
    else if ((supported_fields & DM_DUPLEX) && strcmp(cfg->print_sides, "two-sided-short-edge") == 0)
    {
        dm->dmDuplex = DMDUP_HORIZONTAL;
        dm->dmFields |= DM_DUPLEX;
    }

    if ((supported_fields & (DM_PRINTQUALITY | DM_YRESOLUTION)) == (DM_PRINTQUALITY | DM_YRESOLUTION) && cfg->print_resolution_x > 0 && cfg->print_resolution_y > 0)
    {
        dpi_x = cfg->print_resolution_x;
        dpi_y = cfg->print_resolution_y;

        if (cfg->print_resolution_units == 4)
        {
            dpi_x = (dpi_x * 254 + 50) / 100;
            dpi_y = (dpi_y * 254 + 50) / 100;
        }

        if (dpi_x > 0 && dpi_x <= 32767 && dpi_y > 0 && dpi_y <= 32767)
        {
            dm->dmPrintQuality = (short)dpi_x;
            dm->dmYResolution = (short)dpi_y;
            dm->dmFields |= DM_PRINTQUALITY | DM_YRESOLUTION;
        }
    }
    else if ((supported_fields & DM_PRINTQUALITY) && cfg->print_quality == 3)
    {
        dm->dmPrintQuality = DMRES_DRAFT;
        dm->dmFields |= DM_PRINTQUALITY;
    }
    else if ((supported_fields & DM_PRINTQUALITY) && cfg->print_quality == 4)
    {
        dm->dmPrintQuality = DMRES_MEDIUM;
        dm->dmFields |= DM_PRINTQUALITY;
    }
    else if ((supported_fields & DM_PRINTQUALITY) && cfg->print_quality == 5)
    {
        dm->dmPrintQuality = DMRES_HIGH;
        dm->dmFields |= DM_PRINTQUALITY;
    }

    if (supported_fields & DM_DEFAULTSOURCE)
    {
        paper = w32_attr_to_bin(cfg->print_media_source);

        if (paper)
        {
            dm->dmDefaultSource = paper;
            dm->dmFields |= DM_DEFAULTSOURCE;
        }
    }

    if (supported_fields & DM_MEDIATYPE)
    {
        media_type = w32_attr_to_media_type(cfg->print_media_type);

        if (media_type)
        {
            dm->dmMediaType = media_type;
            dm->dmFields |= DM_MEDIATYPE;
        }
    }
}

static DWORD CALLBACK w32_rtf_read(DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG *read)
{
    W32RtfStream *stream = (W32RtfStream *)cookie;
    size_t size;

    if (!stream || !buffer || !read)
        return 1;

    size = stream->length - stream->offset;

    if (size > (size_t)count)
        size = (size_t)count;

    memcpy(buffer, stream->data + stream->offset, size);

    stream->offset += size;
    *read = (LONG)size;

    return 0;
}

static int w32_rtf_cleanup(FILE *fp, unsigned char *rtf, HANDLE printer, DEVMODEW *dm, HDC hdc, HMODULE module, HWND rich, int rc)
{
    if (rich)
    {
        SendMessageW(rich, EM_FORMATRANGE, FALSE, 0);
        DestroyWindow(rich);
    }

    if (module)
        FreeLibrary(module);

    if (hdc)
        DeleteDC(hdc);

    if (dm)
        HeapFree(GetProcessHeap(), 0, dm);

    if (printer)
        ClosePrinter(printer);

    if (rtf)
        HeapFree(GetProcessHeap(), 0, rtf);

    if (fp)
        fclose(fp);

    return rc;
}

#if defined(USE_FREETYPE) || defined(HAVE_URF)
static int w32_get_ttf_family(const char *path, char *out, size_t outsz)
{
    FT_Library lib = NULL;
    FT_Face face = NULL;
    const char *family = NULL;

    if (!path || !path[0] || !out || outsz == 0)
        return -1;

    out[0] = '\0';

    if (FT_Init_FreeType(&lib) != 0)
        return -1;

    if (FT_New_Face(lib, path, 0, &face) != 0)
    {
        FT_Done_FreeType(lib);
        return -1;
    }

    family = face->family_name;

    if (family && family[0])
    {
        strncpy(out, family, outsz - 1);
        out[outsz - 1] = '\0';
    }

    FT_Done_Face(face);
    FT_Done_FreeType(lib);

    return out[0] ? 0 : -1;
}
#endif

int win32_print_rtf_document(const struct Ed *ed, const TeConfig *cfg, const char *job_name)
{
    DOCINFOW doc;
    EDITSTREAM input;
    FORMATRANGE range;
    W32RtfStream stream;
    HANDLE printer = NULL;
    DEVMODEW *dm = NULL;
    HDC hdc = NULL;
    HWND rich = NULL;
    HMODULE module = NULL;
    FILE *fp = NULL;
    unsigned char *rtf = NULL;
    wchar_t wprinter[512];
    wchar_t wjob[TE_CFG_STR_MAX];
    LONG dm_size;
    LONG next;
    long size;
    size_t rtf_length;
    DWORD count;
    int dpi_x;
    int dpi_y;
    int phys_w;
    int phys_h;
    int phys_off_x;
    int phys_off_y;
    int print_w;
    int print_h;
    LONG margin_left;
    LONG margin_right;
    LONG margin_top;
    LONG margin_bottom;
    int rc = -1;

    if (!ed)
        return -1;

    fp = tmpfile();

    if (fp)
    {
        const char *font_path = NULL;
        char font_name[128];
        int font_size_hp = 0;

        font_name[0] = '\0';

#ifdef USE_FREETYPE
        if (cfg)
        {
            font_path = cfg->print_font_path[0] ? cfg->print_font_path : (cfg->ttf_font[0] ? cfg->ttf_font : NULL);

            if (font_path && font_path[0])
                w32_get_ttf_family(font_path, font_name, sizeof(font_name));
        }
#endif

        if (cfg && cfg->print_font_size > 0)
            font_size_hp = cfg->print_font_size * 2;
        else if (cfg && cfg->ttf_size > 0)
            font_size_hp = cfg->ttf_size * 2;

        if (rtf_export_with_font(ed, fp, font_name[0] ? font_name : NULL, font_size_hp) != 0)
        {
            fclose(fp);
            return w32_rtf_cleanup(NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1);
        }

        if (fflush(fp) != 0 || fseek(fp, 0, SEEK_END) != 0)
            return w32_rtf_cleanup(fp, NULL, NULL, NULL, NULL, NULL, NULL, -1);
    }
    else
    {
        return w32_rtf_cleanup(NULL, NULL, NULL, NULL, NULL, NULL, NULL, -1);
    }

    size = ftell(fp);

    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    rtf_length = (size_t)size;
    rtf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, rtf_length);

    if (!rtf || fread(rtf, 1, rtf_length, fp) != rtf_length)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    memset(wprinter, 0, sizeof(wprinter));

    if (cfg && cfg->print_local_name[0])
        w32_utf8_to_utf16(cfg->print_local_name, wprinter, (int)(sizeof(wprinter) / sizeof(wprinter[0])));
    else
    {
        count = (DWORD)(sizeof(wprinter) / sizeof(wprinter[0]));

        if (!GetDefaultPrinterW(wprinter, &count))
            return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);
    }

    if (!OpenPrinterW(wprinter, &printer, NULL))
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    dm_size = DocumentPropertiesW(NULL, printer, wprinter, NULL, NULL, 0);

    if (dm_size <= 0)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    dm = (DEVMODEW *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)dm_size);

    if (!dm || DocumentPropertiesW(NULL, printer, wprinter, dm, NULL, DM_OUT_BUFFER) < 0)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    w32_apply_devmode(dm, cfg);

    if (DocumentPropertiesW(NULL, printer, wprinter, dm, dm, DM_IN_BUFFER | DM_OUT_BUFFER) < 0)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    hdc = CreateDCW(NULL, wprinter, NULL, dm);

    if (!hdc)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    module = LoadLibraryW(L"Msftedit.dll");

    if (!module)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    rich = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_POPUP | ES_MULTILINE, 0, 0, 0, 0, NULL, NULL, module, NULL);

    if (!rich)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    memset(&stream, 0, sizeof(stream));

    stream.data = rtf;
    stream.length = rtf_length;

    memset(&input, 0, sizeof(input));

    input.dwCookie = (DWORD_PTR)&stream;
    input.pfnCallback = w32_rtf_read;

    /* EM_STREAMIN returns the number of characters read on success, 0 on failure */
    if (SendMessageW(rich, EM_STREAMIN, SF_RTF, (LPARAM)&input) == 0)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    memset(&doc, 0, sizeof(doc));
    memset(wjob, 0, sizeof(wjob));
    doc.cbSize = sizeof(doc);

    if (job_name && job_name[0])
    {
        w32_utf8_to_utf16(job_name, wjob, (int)(sizeof(wjob) / sizeof(wjob[0])));
        doc.lpszDocName = wjob;
    }

    dpi_x = GetDeviceCaps(hdc, LOGPIXELSX);
    dpi_y = GetDeviceCaps(hdc, LOGPIXELSY);
    phys_w = GetDeviceCaps(hdc, PHYSICALWIDTH);
    phys_h = GetDeviceCaps(hdc, PHYSICALHEIGHT);
    phys_off_x = GetDeviceCaps(hdc, PHYSICALOFFSETX);
    phys_off_y = GetDeviceCaps(hdc, PHYSICALOFFSETY);
    print_w = GetDeviceCaps(hdc, HORZRES);
    print_h = GetDeviceCaps(hdc, VERTRES);

    if (dpi_x <= 0 || dpi_y <= 0 || phys_w <= 0 || phys_h <= 0 || print_w <= 0 || print_h <= 0)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    memset(&range, 0, sizeof(range));

    range.hdc = hdc;
    range.hdcTarget = hdc;
    range.rcPage.right = MulDiv(phys_w, 1440, dpi_x);
    range.rcPage.bottom = MulDiv(phys_h, 1440, dpi_y);
    range.rc.left = MulDiv(phys_off_x, 1440, dpi_x);
    range.rc.top = MulDiv(phys_off_y, 1440, dpi_y);
    range.rc.right = range.rc.left + MulDiv(print_w, 1440, dpi_x);
    range.rc.bottom = range.rc.top + MulDiv(print_h, 1440, dpi_y);

    margin_left = cfg && cfg->print_margin_left_mm >= 0 ? MulDiv(cfg->print_margin_left_mm, 14400, 254) : WIN32_PRINT_MARGIN_TWIPS;
    margin_right = cfg && cfg->print_margin_right_mm >= 0 ? MulDiv(cfg->print_margin_right_mm, 14400, 254) : WIN32_PRINT_MARGIN_TWIPS;
    margin_top = cfg && cfg->print_margin_top_mm >= 0 ? MulDiv(cfg->print_margin_top_mm, 14400, 254) : WIN32_PRINT_MARGIN_TWIPS;
    margin_bottom = cfg && cfg->print_margin_bottom_mm >= 0 ? MulDiv(cfg->print_margin_bottom_mm, 14400, 254) : WIN32_PRINT_MARGIN_TWIPS;

    if (range.rc.right - range.rc.left > margin_left + margin_right)
    {
        range.rc.left += margin_left;
        range.rc.right -= margin_right;
    }

    if (range.rc.bottom - range.rc.top > margin_top + margin_bottom)
    {
        range.rc.top += margin_top;
        range.rc.bottom -= margin_bottom;
    }

    SendMessageW(rich, EM_SETTARGETDEVICE, (WPARAM)hdc, (LPARAM)(range.rc.right - range.rc.left));
    SendMessageW(rich, EM_SETSEL, 0, (LPARAM)-1);
    SendMessageW(rich, EM_EXGETSEL, 0, (LPARAM)&range.chrg);

    if (range.chrg.cpMax <= range.chrg.cpMin)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    if (StartDocW(hdc, &doc) <= 0)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    while (range.chrg.cpMin < range.chrg.cpMax)
    {
        if (StartPage(hdc) <= 0)
        {
            AbortDoc(hdc);
            return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);
        }

        next = (LONG)SendMessageW(rich, EM_FORMATRANGE, TRUE, (LPARAM)&range);

        if (next <= range.chrg.cpMin || EndPage(hdc) <= 0)
        {
            AbortDoc(hdc);
            return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);
        }

        range.chrg.cpMin = next;
    }

    if (EndDoc(hdc) <= 0)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    rc = 0;
    return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, rc);
}

int win32_print_file(const char *printer_name, const char *file_path)
{
    wchar_t wfile[MAX_PATH];
    wchar_t wparams[512];
    HINSTANCE h;

    if (!file_path || !file_path[0])
        return -1;

    w32_utf8_to_utf16(file_path, wfile, (int)(sizeof(wfile) / sizeof(wfile[0])));

    if (printer_name && printer_name[0])
    {
        wchar_t wprinter[256];

        w32_utf8_to_utf16(printer_name, wprinter, (int)(sizeof(wprinter) / sizeof(wprinter[0])));
        _snwprintf(wparams, sizeof(wparams) / sizeof(wparams[0]), L"\"%s\"", wprinter);

        h = ShellExecuteW(NULL, L"printto", wfile, wparams, NULL, SW_HIDE);
    }
    else
    {
        h = ShellExecuteW(NULL, L"print", wfile, NULL, NULL, SW_HIDE);
    }

    return ((INT_PTR)h > 32) ? 0 : -1;
}

#else

int win32_list_printers(char names[][64], int max)
{
    return -1;
}

int win32_default_printer(char *out, size_t outsz)
{
    if (out && outsz > 0)
        out[0] = '\0';

    return -1;
}

int win32_get_printer_info(const char *printer_name, Win32PrinterInfo *info)
{
    if (info)
        memset(info, 0, sizeof(*info));

    return -1;
}

int win32_print_file(const char *printer_name, const char *file_path)
{
    return -1;
}

#endif /* PLATFORM_WIN32 */
