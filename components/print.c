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

/* Local printing: Unix pipes PDF to lp/lpr, Win32 shells the print verb on a temp PDF, Amiga writes charset-converted text to PRT: */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PLATFORM_UNIX)
#include <signal.h>
#endif

#include "editor.h"
#include "config.h"
#include "layout.h"
#include "fmt_pdf.h"
#include "print.h"
#include "print_amiga.h"
#include "print_win32.h"
#include "../core/utf8.h"
#include "../core/charset.h"

#ifdef PLATFORM_WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static void print_seterr(char *err, size_t errsz, const char *msg)
{
    if (err && errsz > 0 && !err[0])
    {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }
}

#if defined(PLATFORM_UNIX)

/* Export the PDF straight into a print spooler pipe */
static int print_via_cmd(const struct Ed *ed, const TeConfig *cfg, const char *cmd, LayoutHyphenFn hyph, void *hyph_user, char *err, size_t errsz, char *warn, size_t warnsz)
{
    FILE *p = NULL;
    int rc;
    int status;
    void (*old_pipe)(int);

    p = popen(cmd, "w");

    if (!p)
        return -1;

    /* Ignore SIGPIPE so a closed spooler pipe does not kill the process */
    old_pipe = signal(SIGPIPE, SIG_IGN);

    rc = pdf_export_ex(ed, p, cfg, hyph, hyph_user, err, errsz, warn, warnsz);
    status = pclose(p);

    signal(SIGPIPE, old_pipe);

    if (rc != 0)
        return -1;

    if (status != 0)
        return -1;

    return 0;
}

static int print_platform(const struct Ed *ed, const TeConfig *cfg, const char *charset, const char *file_path, LayoutHyphenFn hyph, void *hyph_user, char *err, size_t errsz, char *warn, size_t warnsz)
{
    char cmd[256];

    /* If a specific local queue is configured, target it explicitly */
    if (cfg && cfg->print_local_name[0])
    {
        snprintf(cmd, sizeof(cmd), "lp -d %s -s", cfg->print_local_name);

        if (print_via_cmd(ed, cfg, cmd, hyph, hyph_user, err, errsz, warn, warnsz) == 0)
            return 0;

        if (err && errsz > 0)
            err[0] = '\0';

        snprintf(cmd, sizeof(cmd), "lpr -P %s", cfg->print_local_name);

        if (print_via_cmd(ed, cfg, cmd, hyph, hyph_user, err, errsz, warn, warnsz) == 0)
            return 0;

        if (err && errsz > 0)
            err[0] = '\0';

        print_seterr(err, errsz, "no usable print spooler (tried lp -d, lpr -P)");

        return -1;
    }

    if (print_via_cmd(ed, cfg, "lp -s 2>/dev/null", hyph, hyph_user, err, errsz, warn, warnsz) == 0)
        return 0;

    if (err && errsz > 0)
        err[0] = '\0';

    if (print_via_cmd(ed, cfg, "lpr 2>/dev/null", hyph, hyph_user, err, errsz, warn, warnsz) == 0)
        return 0;

    if (err && errsz > 0)
        err[0] = '\0';

    print_seterr(err, errsz, "no usable print spooler (tried lp, lpr)");

    return -1;
}

#elif defined(PLATFORM_WIN32)

static int print_platform(const struct Ed *ed, const TeConfig *cfg, const char *charset, const char *file_path, LayoutHyphenFn hyph, void *hyph_user, char *err, size_t errsz, char *warn, size_t warnsz)
{
    wchar_t wtmpdir[MAX_PATH];
    wchar_t wpath[MAX_PATH];
    wchar_t wdrive[MAX_PATH];
    wchar_t wdir[MAX_PATH];
    wchar_t wfname[MAX_PATH];
    wchar_t wfile[MAX_PATH];
    char path[MAX_PATH];
    FILE *fp = NULL;
    int rc;
    int have_path = 0;

    if (win32_print_rtf_document(ed, cfg, file_path) == 0)
        return 0;

    /* Stage the PDF next to the source file if known, else in %TEMP% */
    if (file_path && file_path[0])
    {
        MultiByteToWideChar(CP_UTF8, 0, file_path, -1, wfile, MAX_PATH);
        _wsplitpath(wfile, wdrive, wdir, wfname, NULL);
        _wmakepath(wpath, wdrive, wdir, wfname, L".pdf");

        if (wpath[0])
            have_path = 1;
    }

    if (!have_path)
    {
        if (GetTempPathW(MAX_PATH, wtmpdir) == 0)
        {
            print_seterr(err, errsz, "cannot resolve temp directory");
            return -1;
        }

        _snwprintf(wpath, MAX_PATH, L"%ste_print.pdf", wtmpdir);
    }

    /* Convert back to UTF-8 for fopen (we use standard fopen, not _wfopen) */
    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, MAX_PATH, NULL, NULL);

    fp = fopen(path, "wb");

    if (!fp)
    {
        print_seterr(err, errsz, "cannot create temp PDF");
        return -1;
    }

    rc = pdf_export_ex(ed, fp, cfg, hyph, hyph_user, err, errsz, warn, warnsz);

    fclose(fp);

    if (rc != 0)
        return -1;

    /* WinSpool printto with a named printer, or default print verb */
    if (win32_print_file((cfg && cfg->print_local_name[0]) ? cfg->print_local_name : NULL, path) != 0)
    {
        print_seterr(err, errsz, "no print handler registered for PDF");
        return -1;
    }

    return 0;
}

#elif defined(PLATFORM_AMIGA)

/* Write one line converted from codepoints to the target 8-bit charset */
static int print_amiga_line(FILE *fp, const EdLine *ln, const char *cs)
{
    int i;
    char u8[8];
    char b[4];
    int nu;
    int nb;
    unsigned int cp;

    for (i = 0; i < ln->len; i++)
    {
        cp = ed_line_char(ln, i);

        if (cp == '\t')
        {
            if (fputc('\t', fp) == EOF)
                return -1;

            continue;
        }

        if (cp < 0x80)
        {
            if (fputc((int)cp, fp) == EOF)
                return -1;

            continue;
        }

        nu = utf8_encode(cp, u8);

        if (nu <= 0)
            continue;

        nb = utf8_to_charset(cs, u8, nu, b, (int)sizeof(b));

        if (nb == 1)
        {
            if (fputc((int)(unsigned char)b[0], fp) == EOF)
                return -1;
        }
        else
        {
            if (fputc('?', fp) == EOF)
                return -1;
        }
    }

    return 0;
}

static int print_platform(const struct Ed *ed, const TeConfig *cfg, const char *charset, const char *file_path, LayoutHyphenFn hyph, void *hyph_user, char *err, size_t errsz, char *warn, size_t warnsz)
{
    FILE *fp = NULL;
    int row;
    const char *cs = NULL;

    cs = (charset && charset[0]) ? charset : "LATIN-1";

    /* Apply wizard picks back to printer.device via prefs (best-effort) */
    if (cfg)
        amiga_apply_printer_prefs(cfg->print_media, cfg->print_color_mode, cfg->print_quality);

    fp = fopen("PRT:", "w");

    if (!fp)
    {
        print_seterr(err, errsz, "cannot open PRT:");
        return -1;
    }

    for (row = 0; row < ed->count; row++)
    {
        if (print_amiga_line(fp, ed->lines[row], cs) != 0)
        {
            fclose(fp);

            print_seterr(err, errsz, "write error on PRT:");
            return -1;
        }

        if (fputc('\n', fp) == EOF)
        {
            fclose(fp);

            print_seterr(err, errsz, "write error on PRT:");
            return -1;
        }
    }

    fputc('\f', fp);

    fclose(fp);

    return 0;
}

#else

static int print_platform(const struct Ed *ed, const TeConfig *cfg, const char *charset, const char *file_path, LayoutHyphenFn hyph, void *hyph_user, char *err, size_t errsz, char *warn, size_t warnsz)
{
    print_seterr(err, errsz, "printing not supported on this platform");

    return -1;
}

#endif

int te_print_document(const struct Ed *ed, const TeConfig *cfg, const char *charset, char *err, size_t errsz, char *warn, size_t warnsz)
{
    return te_print_document_ex(ed, cfg, charset, NULL, NULL, NULL, err, errsz, warn, warnsz);
}

int te_print_document_ex(const struct Ed *ed, const TeConfig *cfg, const char *charset, const char *file_path, LayoutHyphenFn hyph, void *hyph_user, char *err, size_t errsz, char *warn, size_t warnsz)
{
    if (err && errsz > 0)
        err[0] = '\0';

    if (warn && warnsz > 0)
        warn[0] = '\0';

    if (!ed)
        return -1;

    return print_platform(ed, cfg, charset, file_path, hyph, hyph_user, err, errsz, warn, warnsz);
}
