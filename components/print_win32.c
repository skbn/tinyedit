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
    int i;
    LONG n_res;

    if (!printer_name || !info)
        return -1;

    memset(info, 0, sizeof(*info));

    info->default_media = -1;
    info->default_sides = -1;
    info->default_color_mode = -1;
    info->default_quality = -1;

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
    DWORD count;
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

    rtf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size);

    if (!rtf || fread(rtf, 1, (size_t)size, fp) != (size_t)size)
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

    hdc = CreateDCW(NULL, wprinter, NULL, dm);

    if (!hdc)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    module = LoadLibraryW(L"Msftedit.dll");

    if (!module)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    rich = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_POPUP, 0, 0, 0, 0, NULL, NULL, module, NULL);

    if (!rich)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    memset(&stream, 0, sizeof(stream));

    stream.data = rtf;
    stream.length = (size_t)size;

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

    if (StartDocW(hdc, &doc) <= 0)
        return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);

    memset(&range, 0, sizeof(range));

    range.hdc = hdc;
    range.hdcTarget = hdc;
    range.rc.right = GetDeviceCaps(hdc, HORZRES) * 1440 / GetDeviceCaps(hdc, LOGPIXELSX);
    range.rc.bottom = GetDeviceCaps(hdc, VERTRES) * 1440 / GetDeviceCaps(hdc, LOGPIXELSY);
    range.rcPage = range.rc;
    range.chrg.cpMin = 0;
    range.chrg.cpMax = -1;

    do
    {
        if (StartPage(hdc) <= 0)
        {
            AbortDoc(hdc);
            return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);
        }

        next = (LONG)SendMessageW(rich, EM_FORMATRANGE, TRUE, (LPARAM)&range);

        if (EndPage(hdc) <= 0 || next <= range.chrg.cpMin)
        {
            AbortDoc(hdc);
            return w32_rtf_cleanup(fp, rtf, printer, dm, hdc, module, rich, -1);
        }

        range.chrg.cpMin = next;
    } while (next < GetWindowTextLengthW(rich));

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
