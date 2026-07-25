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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "config.h"
#include "fmt_pcl.h"
#include "../core/utf8.h"
#include "../core/charset.h"
#include "../core/portable.h"

static void pcl_seterr(char *err, size_t errsz, const char *msg)
{
    if (err && errsz > 0 && !err[0])
    {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }
}

static int pcl_paper_code(const char *media)
{
    if (!media || !media[0])
        return 26;

    if (strstr(media, "a5"))
        return 25;

    if (strstr(media, "a4"))
        return 26;

    if (strstr(media, "a3"))
        return 27;

    if (strstr(media, "letter"))
        return 2;

    if (strstr(media, "legal"))
        return 3;

    return 26;
}

/* Page height in lines at 6 LPI, parsed from media names like 210x297mm or 8.5x11in */
static int pcl_media_lines(const char *media)
{
    const char *x = NULL;
    const char *p = NULL;
    double h;
    int mm;
    int inch;

    if (!media || !media[0])
        return 66;

    x = strrchr(media, 'x');

    if (!x)
        return 66;

    p = x + 1;

    while (*p && !((*p >= '0' && *p <= '9') || *p == '.' || *p == '-' || *p == '+'))
        p++;

    if (!*p)
        return 66;

    h = pf_atof_dot(p, NULL);

    mm = (strstr(p, "mm") != NULL);
    inch = (strstr(p, "in") != NULL);

    if (mm)
        h = h / 25.4;
    else if (!inch)
        h = 11.0;

    return (int)(h * 6.0 + 0.5);
}

static int pcl_emit_char(FILE *fp, unsigned int cp, const char *cs, int *warned, char *warn, size_t warnsz)
{
    char u8[8];
    char b[8];
    int nu;
    int nb;
    int i;

    if (cp < 0x20)
    {
        if (fputc('?', fp) == EOF)
            return -1;

        return 0;
    }

    if (cp < 0x80)
    {
        if (fputc((int)cp, fp) == EOF)
            return -1;

        return 0;
    }

    nu = utf8_encode(cp, u8);

    if (nu <= 0)
    {
        if (fputc('?', fp) == EOF)
            return -1;

        return 0;
    }

    nb = utf8_to_charset(cs, u8, nu, b, (int)sizeof(b));

    if (nb <= 0)
    {
        if (fputc('?', fp) == EOF)
            return -1;

        if (!(*warned) && warn && warnsz > 0)
        {
            strncpy(warn, "some characters could not be encoded", warnsz - 1);
            warn[warnsz - 1] = '\0';

            *warned = 1;
        }

        return 0;
    }

    for (i = 0; i < nb; i++)
    {
        if (fputc((int)(unsigned char)b[i], fp) == EOF)
            return -1;
    }

    return 0;
}

static int pcl_line(FILE *fp, const EdLine *ln, const char *cs, int tab_width, int *warned, char *warn, size_t warnsz)
{
    int i;
    int col;
    int spaces;
    int s;
    unsigned int cp;

    col = 0;

    for (i = 0; i < ln->len; i++)
    {
        cp = ed_line_char(ln, i);

        if (cp == '\t')
        {
            spaces = tab_width - (col % tab_width);

            for (s = 0; s < spaces; s++)
            {
                if (fputc(' ', fp) == EOF)
                    return -1;
            }

            col += spaces;

            continue;
        }

        if (pcl_emit_char(fp, cp, cs, warned, warn, warnsz) != 0)
            return -1;

        col++;
    }

    if (fputs("\r\n", fp) == EOF)
        return -1;

    return 0;
}

int pcl_export(const struct Ed *ed, FILE *fp, const TeConfig *cfg, char *err, size_t errsz, char *warn, size_t warnsz)
{
    int row;
    int lines;
    int warned;
    int lines_per_page;
    const char *media = NULL;
    const char *cs = NULL;
    int paper;

    if (err && errsz > 0)
        err[0] = '\0';

    if (warn && warnsz > 0)
        warn[0] = '\0';

    if (!ed || !fp || !cfg)
    {
        pcl_seterr(err, errsz, "invalid arguments");
        return -1;
    }

    media = cfg->print_media[0] ? cfg->print_media : "iso_a4_210x297mm";

    cs = cfg->charset_out[0] ? charset_resolve(cfg->charset_out) : charset_resolve(CHARSET_WRITE_DEFAULT);

    if (!cs)
        cs = "UTF-8";

    paper = pcl_paper_code(media);
    lines_per_page = pcl_media_lines(media);

    warned = 0;
    lines = 0;

    if (fprintf(fp, "\033E\033&l%dA\033&l0O\033&l6D", paper) < 0)
    {
        pcl_seterr(err, errsz, "write error");
        return -1;
    }

    for (row = 0; row < ed->count; row++)
    {
        if (pcl_line(fp, ed->lines[row], cs, cfg->tab_width, &warned, warn, warnsz) != 0)
        {
            pcl_seterr(err, errsz, "write error");
            return -1;
        }

        lines++;

        if (lines >= lines_per_page)
        {
            if (fputc('\f', fp) == EOF)
            {
                pcl_seterr(err, errsz, "write error");
                return -1;
            }

            lines = 0;
        }
    }

    if (lines > 0)
    {
        if (fputc('\f', fp) == EOF)
        {
            pcl_seterr(err, errsz, "write error");
            return -1;
        }
    }

    return 0;
}
