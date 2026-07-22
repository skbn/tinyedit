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
#include "fmt_pdf.h"
#include "print.h"
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
static int print_via_cmd(const struct Ed *ed, const TeConfig *cfg, const char *cmd, char *err, size_t errsz, char *warn, size_t warnsz)
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

    rc = pdf_export(ed, p, cfg, err, errsz, warn, warnsz);
    status = pclose(p);

    signal(SIGPIPE, old_pipe);

    if (rc != 0)
        return -1;

    if (status != 0)
        return -1;

    return 0;
}

static int print_platform(const struct Ed *ed, const TeConfig *cfg, const char *charset, char *err, size_t errsz, char *warn, size_t warnsz)
{
    if (print_via_cmd(ed, cfg, "lp -s 2>/dev/null", err, errsz, warn, warnsz) == 0)
        return 0;

    if (err && errsz > 0)
        err[0] = '\0';

    if (print_via_cmd(ed, cfg, "lpr 2>/dev/null", err, errsz, warn, warnsz) == 0)
        return 0;

    if (err && errsz > 0)
        err[0] = '\0';

    print_seterr(err, errsz, "no usable print spooler (tried lp, lpr)");

    return -1;
}

#elif defined(PLATFORM_WIN32)

static int print_platform(const struct Ed *ed, const TeConfig *cfg, const char *charset, char *err, size_t errsz, char *warn, size_t warnsz)
{
    char tmpdir[MAX_PATH];
    char path[MAX_PATH];
    FILE *fp = NULL;
    int rc;
    HINSTANCE h;

    if (GetTempPathA(sizeof(tmpdir), tmpdir) == 0)
    {
        print_seterr(err, errsz, "cannot resolve temp directory");
        return -1;
    }

    snprintf(path, sizeof(path), "%ste_print.pdf", tmpdir);

    fp = fopen(path, "wb");

    if (!fp)
    {
        print_seterr(err, errsz, "cannot create temp PDF");
        return -1;
    }

    rc = pdf_export(ed, fp, cfg, err, errsz, warn, warnsz);
    fclose(fp);

    if (rc != 0)
        return -1;

    h = ShellExecuteA(NULL, "print", path, NULL, NULL, SW_HIDE);

    if ((INT_PTR)h <= 32)
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

static int print_platform(const struct Ed *ed, const TeConfig *cfg, const char *charset, char *err, size_t errsz, char *warn, size_t warnsz)
{
    FILE *fp = NULL;
    int row;
    const char *cs = NULL;

    cs = (charset && charset[0]) ? charset : "LATIN-1";

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

static int print_platform(const struct Ed *ed, const TeConfig *cfg, const char *charset, char *err, size_t errsz, char *warn, size_t warnsz)
{
    print_seterr(err, errsz, "printing not supported on this platform");

    return -1;
}

#endif

int te_print_document(const struct Ed *ed, const TeConfig *cfg, const char *charset, char *err, size_t errsz, char *warn, size_t warnsz)
{
    if (err && errsz > 0)
        err[0] = '\0';

    if (warn && warnsz > 0)
        warn[0] = '\0';

    if (!ed)
        return -1;

    return print_platform(ed, cfg, charset, err, errsz, warn, warnsz);
}
