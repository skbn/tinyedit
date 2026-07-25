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
#include <math.h>

#include "editor.h"
#include "config.h"
#include "fmt_urf.h"
#include "../core/utf8.h"
#include "../core/portable.h"

#ifndef HAVE_URF

static int urf_seterr(char *err, size_t errsz, const char *msg)
{
    if (err && errsz > 0)
    {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }

    return -1;
}

int urf_export(const struct Ed *ed, FILE *fp, const TeConfig *cfg, char *err, size_t errsz, char *warn, size_t warnsz)
{
    return urf_seterr(err, errsz, "URF not available on this platform");
}

#else

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#define URF_DPI 300
#define URF_MARGIN_IN 0.5
#define URF_FONT_SIZE_PT 12
#define URF_MAX_PAGE_PIXELS (64 * 1024 * 1024)

typedef struct
{
    FT_Library lib;
    FT_Face faces[TE_CFG_TTF_FALLBACKS + 1];
    int n_faces;
} UrfFont;

static int urf_seterr(char *err, size_t errsz, const char *msg)
{
    if (err && errsz > 0)
    {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }

    return -1;
}

static const char *urf_default_font(void)
{
#ifdef PLATFORM_AMIGA
    return "FONTS:_ttf/DejaVuSansMono.ttf";
#else
    return "fonts/DejaVuSansMono.ttf";
#endif
}

static void urf_font_close(UrfFont *uf)
{
    int i;

    if (!uf)
        return;

    for (i = 0; i < uf->n_faces; i++)
    {
        if (uf->faces[i])
            FT_Done_Face(uf->faces[i]);
    }

    if (uf->lib)
        FT_Done_FreeType(uf->lib);

    memset(uf, 0, sizeof(*uf));
}

static int urf_parse_media(const char *media, double *w, double *h)
{
    const char *x = NULL;
    const char *p = NULL;
    const char *start = NULL;
    double tw = 0.0;
    double th = 0.0;

    if (!media || !media[0])
    {
        *w = 8.5;
        *h = 11.0;

        return 0;
    }

    x = strrchr(media, 'x');

    if (!x || x == media)
    {
        *w = 8.5;
        *h = 11.0;

        return 0;
    }

    p = x - 1;

    while (p >= media && ((*p >= '0' && *p <= '9') || *p == '.' || *p == '-' || *p == '+'))
        p--;

    start = p + 1;

    if (start >= x)
    {
        *w = 8.5;
        *h = 11.0;

        return 0;
    }

    tw = pf_atof_dot(start, NULL);
    th = pf_atof_dot(x + 1, NULL);

    if (tw <= 0.0 || th <= 0.0)
    {
        *w = 8.5;
        *h = 11.0;

        return 0;
    }

    *w = tw;
    *h = th;

    if (strstr(media, "mm"))
    {
        *w /= 25.4;
        *h /= 25.4;
    }
    else if (!strstr(media, "in"))
    {
        *w = 8.5;
        *h = 11.0;
    }

    return 0;
}

static int urf_write_u32(FILE *fp, unsigned int v)
{
    unsigned char b[4];

    b[0] = (unsigned char)(v >> 24);
    b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);
    b[3] = (unsigned char)v;

    if (fwrite(b, 1, 4, fp) != 4)
        return -1;

    return 0;
}

static int urf_page_header(FILE *fp, unsigned int w, unsigned int h, unsigned int dpi)
{
    unsigned char pad[4] = {0, 0, 0, 0};

    if (fputc(8, fp) == EOF)
        return -1;

    if (fputc(0, fp) == EOF)
        return -1;

    if (fputc(0, fp) == EOF)
        return -1;

    if (fputc(0, fp) == EOF)
        return -1;

    if (fwrite(pad, 1, 4, fp) != 4)
        return -1;

    if (fwrite(pad, 1, 4, fp) != 4)
        return -1;

    if (urf_write_u32(fp, w) != 0)
        return -1;

    if (urf_write_u32(fp, h) != 0)
        return -1;

    if (urf_write_u32(fp, dpi) != 0)
        return -1;

    if (fwrite(pad, 1, 4, fp) != 4)
        return -1;

    if (fwrite(pad, 1, 4, fp) != 4)
        return -1;

    return 0;
}

static int urf_encode_line(FILE *fp, const unsigned char *line, unsigned int w)
{
    unsigned int i;
    unsigned int run;
    unsigned char c;
    unsigned char code;

    if (fputc(0, fp) == EOF)
        return -1;

    i = 0;

    while (i < w)
    {
        c = line[i];
        run = 1;

        while (i + run < w && line[i + run] == c && run < 128)
            run++;

        if (run > 1)
        {
            code = (unsigned char)(run - 1);

            if (fputc((int)code, fp) == EOF)
                return -1;

            if (fputc((int)c, fp) == EOF)
                return -1;

            i += run;
        }
        else
        {
            if (fputc(0, fp) == EOF)
                return -1;

            if (fputc((int)c, fp) == EOF)
                return -1;

            i++;
        }
    }

    return 0;
}

static int urf_encode_page(FILE *fp, const unsigned char *buf, unsigned int w, unsigned int h)
{
    unsigned int y;

    for (y = 0; y < h; y++)
    {
        if (urf_encode_line(fp, buf + y * w, w) != 0)
            return -1;
    }

    return 0;
}

static unsigned char *urf_page_new(unsigned int w, unsigned int h)
{
    unsigned char *buf = NULL;
    size_t n;

    n = (size_t)w * (size_t)h;

    if (n == 0 || n > URF_MAX_PAGE_PIXELS)
        return NULL;

    buf = (unsigned char *)malloc(n);

    if (buf)
        memset(buf, 255, n);

    return buf;
}

static int urf_font_advance(const UrfFont *uf, unsigned int cp)
{
    FT_Error e;
    int i;

    if (!uf)
        return 0;

    for (i = 0; i < uf->n_faces; i++)
    {
        e = FT_Load_Char(uf->faces[i], (FT_ULong)cp, FT_LOAD_DEFAULT);

        if (e)
            continue;

        return (int)(uf->faces[i]->glyph->metrics.horiAdvance >> 6);
    }

    return 0;
}

static int urf_char_advance(const UrfFont *uf, int x, int margin, int space_width, int tab_width, unsigned int cp)
{
    int col;
    int spaces;

    if (cp == '\t')
    {
        col = (x - margin) / space_width;
        spaces = tab_width - (col % tab_width);

        if (spaces <= 0)
            spaces = tab_width;

        return spaces * space_width;
    }

    if (cp < 0x20 || cp == 0x7F)
        return space_width;

    return urf_font_advance(uf, cp);
}

static int urf_font_render(const UrfFont *uf, unsigned char *buf, unsigned int w, unsigned int h, int x, int y, unsigned int cp)
{
    FT_Error e;
    FT_Bitmap *bm = NULL;
    FT_Face face;
    int bx;
    int by;
    int i;
    int j;
    int k;
    unsigned int px;
    unsigned int py;
    unsigned char v;

    if (!uf)
        return 0;

    for (i = 0; i < uf->n_faces; i++)
    {
        face = uf->faces[i];
        e = FT_Load_Char(face, (FT_ULong)cp, FT_LOAD_RENDER);

        if (e)
            continue;

        bm = &face->glyph->bitmap;
        bx = x + face->glyph->bitmap_left;
        by = y - face->glyph->bitmap_top;

        for (j = 0; j < (int)bm->rows; j++)
        {
            const unsigned char *row = bm->buffer + j * bm->pitch;

            py = (unsigned int)(by + j);

            if (py >= h)
                continue;

            for (k = 0; k < (int)bm->width; k++)
            {
                px = (unsigned int)(bx + k);

                if (px >= w)
                    continue;

                v = row[k];

                if (v)
                    buf[py * w + px] = (unsigned char)(255 - v);
            }
        }

        return (int)(face->glyph->metrics.horiAdvance >> 6);
    }

    return 0;
}

static int urf_font_open(const TeConfig *cfg, int dpi, UrfFont *uf)
{
    const char *p = NULL;
    FT_Error e;
    int i;
    int size;

    memset(uf, 0, sizeof(*uf));

    if (!cfg)
        return -1;

    p = cfg->ttf_font[0] ? cfg->ttf_font : urf_default_font();

    e = FT_Init_FreeType(&uf->lib);

    if (e)
        return -1;

    size = cfg->ttf_size > 0 ? cfg->ttf_size : URF_FONT_SIZE_PT;

    e = FT_New_Face(uf->lib, p, 0, &uf->faces[0]);

    if (e)
    {
        FT_Done_FreeType(uf->lib);

        memset(uf, 0, sizeof(*uf));

        return -1;
    }

    uf->n_faces = 1;

    e = FT_Set_Char_Size(uf->faces[0], size * 64, 0, (FT_UInt)dpi, 0);

    if (e)
    {
        urf_font_close(uf);
        return -1;
    }

    for (i = 0; i < TE_CFG_TTF_FALLBACKS && uf->n_faces < TE_CFG_TTF_FALLBACKS + 1; i++)
    {
        int fs;

        if (!cfg->ttf_fallback[i][0])
            continue;

        e = FT_New_Face(uf->lib, cfg->ttf_fallback[i], 0, &uf->faces[uf->n_faces]);

        if (e)
            continue;

        fs = cfg->ttf_fallback_size[i] > 0 ? cfg->ttf_fallback_size[i] : size;

        FT_Set_Char_Size(uf->faces[uf->n_faces], fs * 64, 0, (FT_UInt)dpi, 0);

        uf->n_faces++;
    }

    return 0;
}

static int urf_export_cleanup(unsigned char **buf, UrfFont *uf, char *err, size_t errsz, const char *msg)
{
    if (buf && *buf)
    {
        free(*buf);
        *buf = NULL;
    }

    urf_font_close(uf);

    return urf_seterr(err, errsz, msg);
}

int urf_export(const struct Ed *ed, FILE *fp, const TeConfig *cfg, char *err, size_t errsz, char *warn, size_t warnsz)
{
    UrfFont uf;
    const char *media = NULL;
    unsigned char *buf = NULL;
    double page_w_in;
    double page_h_in;
    unsigned int page_w;
    unsigned int page_h;
    unsigned int printable_w;
    int dpi;
    int margin;
    int line_height;
    int ascender;
    int char_width;
    int space_width;
    int tab_width;
    int row;
    int page_count;
    int y;
    int warned;
    long header_pos;

    if (err && errsz > 0)
        err[0] = '\0';

    if (warn && warnsz > 0)
        warn[0] = '\0';

    memset(&uf, 0, sizeof(uf));

    if (!ed || !fp || !cfg)
        return urf_seterr(err, errsz, "invalid arguments");

    media = cfg->print_media[0] ? cfg->print_media : "iso_a4_210x297mm";

    urf_parse_media(media, &page_w_in, &page_h_in);

    dpi = (cfg->print_resolution_x > 0 && cfg->print_resolution_y > 0) ? cfg->print_resolution_x : URF_DPI;

    page_w = (unsigned int)(page_w_in * dpi + 0.5);
    page_h = (unsigned int)(page_h_in * dpi + 0.5);

    margin = (int)(URF_MARGIN_IN * dpi);

    if (page_w < (unsigned int)(2 * margin) || page_h < (unsigned int)(2 * margin))
    {
        urf_font_close(&uf);

        return urf_seterr(err, errsz, "page too small for margins");
    }

    printable_w = page_w - (unsigned int)(2 * margin);

    if (urf_font_open(cfg, dpi, &uf) != 0)
        return urf_seterr(err, errsz, "cannot load font");

    line_height = (int)(uf.faces[0]->size->metrics.height >> 6);
    ascender = (int)(uf.faces[0]->size->metrics.ascender >> 6);
    char_width = (int)(uf.faces[0]->size->metrics.max_advance >> 6);

    if (char_width <= 0)
    {
        urf_font_close(&uf);

        return urf_seterr(err, errsz, "font has zero advance");
    }

    space_width = urf_font_advance(&uf, ' ');

    if (space_width <= 0)
        space_width = char_width;

    tab_width = cfg->tab_width > 0 ? cfg->tab_width : 4;

    if (fwrite("UNIRAST\0", 1, 8, fp) != 8)
        return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

    header_pos = ftell(fp);

    if (header_pos < 0)
        return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

    if (urf_write_u32(fp, 0) != 0)
        return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

    buf = urf_page_new(page_w, page_h);

    if (!buf)
        return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

    page_count = 0;
    y = margin + ascender;
    warned = 0;

    for (row = 0; row < ed->count; row++)
    {
        const EdLine *ln = ed->lines[row];
        int start;
        int len;

        if (!ln)
            continue;

        len = ln->len;

        if (len <= 0)
        {
            if (y + line_height > (int)(page_h - margin))
            {
                if (urf_page_header(fp, page_w, page_h, dpi) != 0)
                    return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

                if (urf_encode_page(fp, buf, page_w, page_h) != 0)
                    return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

                page_count++;

                memset(buf, 255, (size_t)page_w * page_h);

                y = margin + ascender;
            }

            y += line_height;

            continue;
        }

        start = 0;

        while (start < len)
        {
            int seg_start = start;
            int seg_end = len;
            int k;
            int x = margin;
            int last_space = -1;
            int overflow = 0;
            unsigned int cp;

            for (k = start; k < len; k++)
            {
                int adv;

                cp = ed_line_char(ln, k);

                if (cp == ' ')
                    last_space = k;

                adv = urf_char_advance(&uf, x, margin, space_width, tab_width, cp);

                if (x + adv > (int)(margin + printable_w) && k > start)
                {
                    overflow = 1;

                    if (last_space > start)
                        seg_end = last_space;
                    else
                        seg_end = k;

                    break;
                }

                x += adv;
            }

            if (seg_end <= seg_start)
                seg_end = seg_start + 1;

            if (overflow && last_space > start)
                start = last_space + 1;
            else
                start = seg_end;

            if (y + line_height > (int)(page_h - margin))
            {
                if (urf_page_header(fp, page_w, page_h, dpi) != 0)
                    return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

                if (urf_encode_page(fp, buf, page_w, page_h) != 0)
                    return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

                page_count++;

                memset(buf, 255, (size_t)page_w * page_h);

                y = margin + ascender;
            }

            x = margin;

            for (k = seg_start; k < seg_end; k++)
            {
                int adv;

                cp = ed_line_char(ln, k);

                if (cp == '\t')
                {
                    int col;
                    int spaces;
                    int s;

                    col = (x - margin) / space_width;
                    spaces = tab_width - (col % tab_width);

                    if (spaces <= 0)
                        spaces = tab_width;

                    for (s = 0; s < spaces; s++)
                        x += urf_font_render(&uf, buf, page_w, page_h, x, y, ' ');
                }
                else if (cp < 0x20 || cp == 0x7F)
                {
                    x += urf_font_render(&uf, buf, page_w, page_h, x, y, ' ');
                }
                else
                {
                    adv = urf_font_render(&uf, buf, page_w, page_h, x, y, cp);

                    if (adv == 0 && cp > 0x7F)
                    {
                        if (!warned && warn && warnsz > 0)
                        {
                            snprintf(warn, warnsz, "character U+%04X could not be rendered", cp);
                            warned = 1;
                        }
                    }

                    x += adv;
                }
            }

            y += line_height;
        }
    }

    if (y > margin + ascender || page_count == 0)
    {
        if (urf_page_header(fp, page_w, page_h, dpi) != 0)
            return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

        if (urf_encode_page(fp, buf, page_w, page_h) != 0)
            return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

        page_count++;
    }

    if (fseek(fp, header_pos, SEEK_SET) != 0)
        return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

    if (urf_write_u32(fp, (unsigned int)page_count) != 0)
        return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

    if (fseek(fp, 0, SEEK_END) != 0)
        return urf_export_cleanup(&buf, &uf, err, errsz, "write error");

    free(buf);

    urf_font_close(&uf);

    return 0;
}

#endif
