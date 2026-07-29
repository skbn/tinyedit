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

/* PWG Raster format exporter (image/pwg-raster) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "editor.h"
#include "config.h"
#include "fmt_pwg.h"
#include "../core/utf8.h"
#include "../core/portable.h"

#ifndef HAVE_PRINTER

static int pwg_seterr(char *err, size_t errsz, const char *msg)
{
    if (err && errsz > 0)
    {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }

    return -1;
}

int pwg_export(const struct Ed *ed, FILE *fp, const TeConfig *cfg, char *err, size_t errsz, char *warn, size_t warnsz)
{
    return pwg_seterr(err, errsz, "PWG raster not available on this platform");
}

int pwg_export_ex(const struct Ed *ed, FILE *fp, const TeConfig *cfg, LayoutHyphenFn hyph, void *hyph_user, char *err, size_t errsz, char *warn, size_t warnsz)
{
    return pwg_seterr(err, errsz, "PWG raster not available on this platform");
}

#else

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

#include "ed_attr.h"
#include "layout.h"

#define PWG_DPI 300
#define PWG_MARGIN_IN 1.0
#define PWG_FONT_SIZE_PT 12
#define PWG_MAX_PAGE_PIXELS (64 * 1024 * 1024)
#define PWG_UNDERLINE_OFF_PT 1.5
#define PWG_UNDERLINE_TH_PT 0.6
#define PWG_STRIKE_OFF_PT 3.5
#define PWG_STRIKE_TH_PT 0.6
#define PWG_BOLD_STRENGTH_MUL 0.03
#define PWG_ITALIC_SHEAR 0.21
#define PWG_LEADING_MUL 1.2

/* CUPS Raster v2 header size */
#define CUPS_RASTER_HEADER_SIZE 1796

/* CUPS color space for 8-bit grayscale */
#define CUPS_CSPACE_W 18

/* CUPS compression: 1 = RLE */
#define CUPS_COMP_RLE 1

typedef struct
{
    FT_Library lib;
    FT_Face faces[TE_CFG_TTF_FALLBACKS + 1];
    int n_faces;
} PwgFont;

typedef struct
{
    unsigned int cp;
    unsigned short mask;
} pwg_wchar;

typedef struct
{
    pwg_wchar *chars;
    int len;
    int cap;
    unsigned char align;
    int has_content;
    int *breaks_pos;
    unsigned char *breaks_hyph;
    int n_breaks;
    int cap_breaks;
} pwg_para;

typedef struct
{
    FILE *fp;
    unsigned char *buf;
    unsigned int page_w, page_h;
    int dpi;
    int margin;
    int printable_w;
    int line_height;
    int ascender;
    int space_width;
    int tab_width;
    int font_size_px;
    int y;
    PwgFont *uf;
    LayoutHyphenFn hyph;
    void *hyph_user;
    int page_count;
    int in_page;
} pwg_pager;

static int pwg_seterr(char *err, size_t errsz, const char *msg)
{
    if (err && errsz > 0)
    {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }

    return -1;
}

static const char *pwg_default_font(void)
{
#ifdef PLATFORM_AMIGA
    return "FONTS:_ttf/DejaVuSansMono.ttf";
#else
    return "fonts/DejaVuSansMono.ttf";
#endif
}

static void pwg_font_close(PwgFont *uf)
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

static void pwg_para_init(pwg_para *p)
{
    memset(p, 0, sizeof(*p));
}

static void pwg_para_free(pwg_para *p)
{
    free(p->chars);
    free(p->breaks_pos);
    free(p->breaks_hyph);

    memset(p, 0, sizeof(*p));
}

static void pwg_para_reset(pwg_para *p)
{
    p->len = 0;
    p->align = EA_ALIGN_LEFT;
    p->has_content = 0;
    p->n_breaks = 0;
}

static int pwg_para_push(pwg_para *p, unsigned int cp, unsigned short mask)
{
    if (p->len >= p->cap)
    {
        int nc;
        pwg_wchar *nb = NULL;

        nc = p->cap ? p->cap * 2 : 128;
        nb = (pwg_wchar *)realloc(p->chars, (size_t)nc * sizeof(*nb));

        if (!nb)
            return -1;

        p->chars = nb;
        p->cap = nc;
    }

    p->chars[p->len].cp = cp;
    p->chars[p->len].mask = mask;
    p->len++;

    return 0;
}

static int pwg_para_add_break(pwg_para *p, int is_hyphen)
{
    if (p->n_breaks >= p->cap_breaks)
    {
        int nc;
        int *np = NULL;
        unsigned char *nh = NULL;

        nc = p->cap_breaks ? p->cap_breaks * 2 : 16;

        np = (int *)realloc(p->breaks_pos, (size_t)nc * sizeof(*np));

        if (!np)
            return -1;

        nh = (unsigned char *)realloc(p->breaks_hyph, (size_t)nc * sizeof(*nh));

        if (!nh)
        {
            p->breaks_pos = np;
            return -1;
        }

        p->breaks_pos = np;
        p->breaks_hyph = nh;
        p->cap_breaks = nc;
    }

    p->breaks_pos[p->n_breaks] = p->len;
    p->breaks_hyph[p->n_breaks] = (unsigned char)(is_hyphen ? 1 : 0);
    p->n_breaks++;

    return 0;
}

static int pwg_para_find_break(const pwg_para *p, int start)
{
    int i;

    for (i = 0; i < p->n_breaks; i++)
    {
        if (p->breaks_pos[i] > start)
            return i;
    }

    return -1;
}

static int pwg_para_append_edline(pwg_para *p, const EdLine *ln, const EdAttrRun *runs, int n_runs)
{
    int i;
    int r;

    r = 0;

    for (i = 0; i < ln->len; i++)
    {
        unsigned int cp;
        unsigned short mask;

        cp = ed_line_char(ln, i);

        mask = 0;

        while (r < n_runs && runs[r].end <= i)
            r++;

        if (r < n_runs && i >= runs[r].start && i < runs[r].end)
            mask = runs[r].mask;

        if (pwg_para_push(p, cp, mask) != 0)
            return -1;
    }

    return 0;
}

static int pwg_parse_media(const char *media, double *w, double *h)
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

static int pwg_write_u32_be(FILE *fp, unsigned int v)
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

/* CUPS Raster v2 page header (1796B, big-endian); essential fields only */
static int pwg_page_header(FILE *fp, unsigned int w, unsigned int h, unsigned int dpi)
{
    unsigned char hdr[CUPS_RASTER_HEADER_SIZE];
    unsigned int bytes_per_line;

    memset(hdr, 0, sizeof(hdr));

    bytes_per_line = w; /* 8-bit grayscale: 1 byte per pixel */

    /* Key fields at their CUPS Raster v2 offsets (big-endian) */

    /* cupsWidth at offset 812 */
    hdr[812] = (unsigned char)(w >> 24);
    hdr[813] = (unsigned char)(w >> 16);
    hdr[814] = (unsigned char)(w >> 8);
    hdr[815] = (unsigned char)w;

    /* cupsHeight at offset 816 */
    hdr[816] = (unsigned char)(h >> 24);
    hdr[817] = (unsigned char)(h >> 16);
    hdr[818] = (unsigned char)(h >> 8);
    hdr[819] = (unsigned char)h;

    /* cupsBitsPerColor at offset 824 */
    hdr[824] = 0;
    hdr[825] = 0;
    hdr[826] = 0;
    hdr[827] = 8;

    /* cupsBitsPerPixel at offset 828 */
    hdr[828] = 0;
    hdr[829] = 0;
    hdr[830] = 0;
    hdr[831] = 8;

    /* cupsBytesPerLine at offset 832 */
    hdr[832] = (unsigned char)(bytes_per_line >> 24);
    hdr[833] = (unsigned char)(bytes_per_line >> 16);
    hdr[834] = (unsigned char)(bytes_per_line >> 8);
    hdr[835] = (unsigned char)bytes_per_line;

    /* cupsColorSpace at offset 840: CUPS_CSPACE_W = 18 (grayscale) */
    hdr[840] = 0;
    hdr[841] = 0;
    hdr[842] = 0;
    hdr[843] = CUPS_CSPACE_W;

    /* cupsCompression at offset 844: 1 = RLE */
    hdr[844] = 0;
    hdr[845] = 0;
    hdr[846] = 0;
    hdr[847] = CUPS_COMP_RLE;

    /* cupsNumColors at offset 860 */
    hdr[860] = 0;
    hdr[861] = 0;
    hdr[862] = 0;
    hdr[863] = 1;

    if (fwrite(hdr, 1, CUPS_RASTER_HEADER_SIZE, fp) != CUPS_RASTER_HEADER_SIZE)
        return -1;

    return 0;
}

/* Write a zero-filled CUPS Raster header to signal end-of-document */
static int pwg_end_header(FILE *fp)
{
    unsigned char hdr[CUPS_RASTER_HEADER_SIZE];

    memset(hdr, 0, sizeof(hdr));

    if (fwrite(hdr, 1, CUPS_RASTER_HEADER_SIZE, fp) != CUPS_RASTER_HEADER_SIZE)
        return -1;

    return 0;
}

/* RLE encode a single scanline (identical to URF encoding) */
static int pwg_encode_line(FILE *fp, const unsigned char *line, unsigned int w)
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

static int pwg_encode_page(FILE *fp, const unsigned char *buf, unsigned int w, unsigned int h)
{
    unsigned int y;

    for (y = 0; y < h; y++)
    {
        if (pwg_encode_line(fp, buf + y * w, w) != 0)
            return -1;
    }

    return 0;
}

static unsigned char *pwg_page_new(unsigned int w, unsigned int h)
{
    unsigned char *buf = NULL;
    size_t n;

    n = (size_t)w * (size_t)h;

    if (n == 0 || n > PWG_MAX_PAGE_PIXELS)
        return NULL;

    buf = (unsigned char *)malloc(n);

    if (buf)
        memset(buf, 255, n);

    return buf;
}

static int pwg_font_advance(const PwgFont *uf, unsigned int cp)
{
    FT_Error e;
    int i;

    if (!uf)
        return 0;

    for (i = 0; i < uf->n_faces; i++)
    {
        if (FT_Get_Char_Index(uf->faces[i], (FT_ULong)cp) == 0)
            continue;

        e = FT_Load_Char(uf->faces[i], (FT_ULong)cp, FT_LOAD_DEFAULT);

        if (e)
            continue;

        return (int)(uf->faces[i]->glyph->metrics.horiAdvance >> 6);
    }

    return 0;
}

static int pwg_char_advance(const PwgFont *uf, int x, int margin, int space_width, int tab_width, unsigned int cp)
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

    return pwg_font_advance(uf, cp);
}

static int pwg_measure_range(const pwg_para *p, const PwgFont *uf, int start, int end, int margin, int space_width, int tab_width)
{
    int x;
    int i;

    if (end > p->len)
        end = p->len;

    if (tab_width <= 0)
        tab_width = 4;

    x = margin;

    for (i = start; i < end; i++)
    {
        unsigned int cp = p->chars[i].cp;
        x += pwg_char_advance(uf, x, margin, space_width, tab_width, cp);
    }

    return x - margin;
}

static int pwg_wrap_next(const pwg_para *p, const PwgFont *uf, int start, int max_w, int margin, int space_width, int tab_width, LayoutHyphenFn hyph, void *hyph_user, int *out_hyphenated)
{
    int x;
    int last_space_at;
    int i;

    if (out_hyphenated)
        *out_hyphenated = 0;

    if (tab_width <= 0)
        tab_width = 4;

    if (start >= p->len)
        return p->len;

    x = margin;
    last_space_at = -1;

    for (i = start; i < p->len; i++)
    {
        unsigned int cp = p->chars[i].cp;
        int gw = pwg_char_advance(uf, x, margin, space_width, tab_width, cp);

        if (x - margin + gw > max_w && i > start)
        {
            if (hyph)
            {
                int word_start = i;
                int word_end = i;
                int wlen;
                int k;

                while (word_start > start && p->chars[word_start - 1].cp != ' ' && p->chars[word_start - 1].cp != '\t')
                    word_start--;

                while (word_end < p->len && p->chars[word_end].cp != ' ' && p->chars[word_end].cp != '\t')
                    word_end++;

                while (word_start < word_end - 1 &&
                       (p->chars[word_start].cp == 0xBF ||
                        p->chars[word_start].cp == 0xA1 ||
                        p->chars[word_start].cp == '(' ||
                        p->chars[word_start].cp == '[' ||
                        p->chars[word_start].cp == '{' ||
                        p->chars[word_start].cp == '"' ||
                        p->chars[word_start].cp == '\''))
                    word_start++;

                wlen = word_end - word_start;

                if (wlen >= 5)
                {
                    wchar_t wbuf[128];
                    int splits[64];
                    int nsplits;
                    int hy_width;
                    int word_prefix_w = 0;
                    int cap = (int)(sizeof(wbuf) / sizeof(wbuf[0])) - 1;
                    int j;
                    int xx;

                    if (wlen > cap)
                        wlen = cap;

                    for (k = 0; k < wlen; k++)
                        wbuf[k] = (wchar_t)p->chars[word_start + k].cp;

                    wbuf[wlen] = 0;

                    nsplits = hyph(hyph_user, wbuf, wlen, splits, (int)(sizeof(splits) / sizeof(splits[0])));
                    hy_width = pwg_font_advance(uf, '-');

                    xx = margin;

                    for (j = start; j < word_start; j++)
                        xx += pwg_char_advance(uf, xx, margin, space_width, tab_width, p->chars[j].cp);

                    word_prefix_w = xx - margin;

                    for (k = nsplits - 1; k >= 0; k--)
                    {
                        int split = splits[k];
                        int frag_w = 0;
                        int xx2 = margin;
                        int j2;

                        if (split < 2 || wlen - split < 3)
                            continue;

                        for (j2 = 0; j2 < split; j2++)
                            xx2 += pwg_char_advance(uf, xx2, margin, space_width, tab_width, p->chars[word_start + j2].cp);

                        frag_w = xx2 - margin;

                        if (word_prefix_w + frag_w + hy_width <= max_w)
                        {
                            if (out_hyphenated)
                                *out_hyphenated = 1;

                            return word_start + split;
                        }
                    }
                }
            }

            if (last_space_at >= start)
                return last_space_at + 1;

            return i;
        }

        x += gw;

        if (cp == ' ')
            last_space_at = i;
    }

    return p->len;
}

static int pwg_render_char_attr(const PwgFont *uf, unsigned char *buf, unsigned int w, unsigned int h, int x, int y, unsigned int cp, unsigned short mask, int font_size_px, int space_width)
{
    FT_Error e;
    FT_Face face;
    FT_Bitmap *bm = NULL;
    int bx, by;
    int i, j, k;
    unsigned int px, py;
    unsigned char v;
    int want_bold, want_italic, want_ul, want_strike;
    FT_Matrix shear;
    int did_transform = 0;
    int adv = 0;

    if (!uf)
        return 0;

    want_bold = (mask & EA_BOLD) != 0;
    want_italic = (mask & EA_ITALIC) != 0;
    want_ul = (mask & EA_UNDERLINE) != 0;
    want_strike = (mask & EA_STRIKE) != 0;

    if (want_italic)
    {
        shear.xx = 1 << 16;
        shear.xy = (FT_Fixed)(PWG_ITALIC_SHEAR * (1 << 16));
        shear.yx = 0;
        shear.yy = 1 << 16;
    }

    for (i = 0; i < uf->n_faces; i++)
    {
        face = uf->faces[i];

        if (FT_Get_Char_Index(face, (FT_ULong)cp) == 0)
            continue;

        if (want_italic)
        {
            FT_Set_Transform(face, &shear, NULL);
            did_transform = 1;
        }

        if (want_bold)
            e = FT_Load_Char(face, (FT_ULong)cp, FT_LOAD_NO_BITMAP);
        else
            e = FT_Load_Char(face, (FT_ULong)cp, FT_LOAD_RENDER);

        if (want_italic && did_transform)
        {
            FT_Set_Transform(face, NULL, NULL);
            did_transform = 0;
        }

        if (e)
            continue;

        if (want_bold)
        {
            FT_Pos strength = (FT_Pos)(font_size_px * PWG_BOLD_STRENGTH_MUL * 64);

            if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
            {
                FT_Outline_EmboldenXY(&face->glyph->outline, strength, 0);
                FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
            }
        }

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
                {
                    unsigned char old = buf[py * w + px];
                    unsigned int nv = (unsigned int)(255 - v);

                    if (nv < old)
                        buf[py * w + px] = (unsigned char)nv;
                }
            }
        }

        adv = (int)(face->glyph->metrics.horiAdvance >> 6);
        break;
    }

    if (want_ul && adv > 0)
    {
        int uy = y + (int)(PWG_UNDERLINE_OFF_PT * font_size_px / 12.0 + 0.5);
        int uth = (int)(PWG_UNDERLINE_TH_PT * font_size_px / 12.0 + 0.5);

        if (uth < 1)
            uth = 1;

        for (px = (unsigned int)x; px < (unsigned int)(x + adv) && px < w; px++)
        {
            for (j = 0; j < uth; j++)
            {
                py = (unsigned int)(uy + j);

                if (py < h)
                    buf[py * w + px] = 0;
            }
        }
    }

    /* Strike: horizontal line at mid-height */
    if (want_strike && adv > 0)
    {
        int sy = y - (int)(PWG_STRIKE_OFF_PT * font_size_px / 12.0 + 0.5);
        int sth = (int)(PWG_STRIKE_TH_PT * font_size_px / 12.0 + 0.5);

        if (sth < 1)
            sth = 1;

        for (px = (unsigned int)x; px < (unsigned int)(x + adv) && px < w; px++)
        {
            for (j = 0; j < sth; j++)
            {
                py = (unsigned int)(sy + j);

                if (py < h)
                    buf[py * w + px] = 0;
            }
        }
    }

    return adv;
}

static int pwg_pager_start_page(pwg_pager *pg)
{
    if (pwg_page_header(pg->fp, pg->page_w, pg->page_h, (unsigned int)pg->dpi) != 0)
        return -1;

    memset(pg->buf, 255, (size_t)pg->page_w * pg->page_h);

    pg->y = pg->margin + pg->ascender;
    pg->in_page = 1;

    return 0;
}

static int pwg_pager_close_page(pwg_pager *pg)
{
    if (!pg->in_page)
        return 0;

    if (pwg_encode_page(pg->fp, pg->buf, pg->page_w, pg->page_h) != 0)
        return -1;

    pg->page_count++;
    pg->in_page = 0;

    return 0;
}

static int pwg_pager_ensure_line(pwg_pager *pg)
{
    if (!pg->in_page)
    {
        if (pwg_pager_start_page(pg) != 0)
            return -1;

        return 0;
    }

    if (pg->y + pg->line_height > (int)(pg->page_h - pg->margin))
    {
        if (pwg_pager_close_page(pg) != 0)
            return -1;

        if (pwg_pager_start_page(pg) != 0)
            return -1;
    }

    return 0;
}

static int pwg_pager_blank_line(pwg_pager *pg)
{
    if (pwg_pager_ensure_line(pg) != 0)
        return -1;

    pg->y += pg->line_height;

    return 0;
}

static int pwg_pager_render_line(pwg_pager *pg, const pwg_para *para, int start, int end, int x, int word_space)
{
    int i;
    int px = x;

    for (i = start; i < end; i++)
    {
        unsigned int cp = para->chars[i].cp;
        unsigned short mask = para->chars[i].mask;
        int adv;

        if (cp == '\t')
        {
            int col = (px - pg->margin) / pg->space_width;
            int spaces = pg->tab_width - (col % pg->tab_width);
            int s;

            if (spaces <= 0)
                spaces = pg->tab_width;

            for (s = 0; s < spaces; s++)
                px += pwg_render_char_attr(pg->uf, pg->buf, pg->page_w, pg->page_h, px, pg->y, ' ', 0, pg->font_size_px, pg->space_width);
        }
        else if (cp < 0x20 || cp == 0x7F)
        {
            px += pwg_render_char_attr(pg->uf, pg->buf, pg->page_w, pg->page_h, px, pg->y, ' ', 0, pg->font_size_px, pg->space_width);
        }
        else
        {
            adv = pwg_render_char_attr(pg->uf, pg->buf, pg->page_w, pg->page_h, px, pg->y, cp, mask, pg->font_size_px, pg->space_width);
            px += adv;

            if (word_space > 0 && cp == ' ')
                px += word_space;
        }
    }

    return 0;
}

static int pwg_pager_emit_para(pwg_pager *pg, const pwg_para *para)
{
    int start;
    int use_forced;

    if (para->len == 0)
        return pwg_pager_blank_line(pg);

    start = 0;
    use_forced = 1;

    while (start < para->len)
    {
        int end;
        int trim_end;
        int line_w;
        int x;
        int align;
        int is_last_line;
        int n_spaces;
        int j;
        int word_space = 0;
        int hyphenated = 0;
        pwg_wchar saved_char = {0, 0};
        int saved_at = -1;
        int bi;

        bi = use_forced ? pwg_para_find_break(para, start) : -1;

        if (bi >= 0)
        {
            int fb_end = para->breaks_pos[bi];
            int fb_w = pwg_measure_range(para, pg->uf, start, fb_end, pg->margin, pg->space_width, pg->tab_width);

            if (fb_w <= pg->printable_w)
            {
                end = fb_end;
                hyphenated = (para->breaks_hyph[bi] == 1);
            }
            else
            {
                use_forced = 0;
                end = pwg_wrap_next(para, pg->uf, start, pg->printable_w, pg->margin, pg->space_width, pg->tab_width, pg->hyph, pg->hyph_user, &hyphenated);
            }
        }
        else
        {
            end = pwg_wrap_next(para, pg->uf, start, pg->printable_w, pg->margin, pg->space_width, pg->tab_width, pg->hyph, pg->hyph_user, &hyphenated);
        }

        if (end <= start)
            end = start + 1;

        is_last_line = (end >= para->len);

        trim_end = end;

        while (trim_end > start && para->chars[trim_end - 1].cp == ' ')
            trim_end--;

        if (hyphenated && end < para->len && trim_end == end)
        {
            pwg_para *mp = (pwg_para *)para;
            unsigned short mask_at = trim_end > start ? mp->chars[trim_end - 1].mask : 0;

            saved_at = end;
            saved_char = mp->chars[end];
            mp->chars[end].cp = '-';
            mp->chars[end].mask = mask_at;
            trim_end = end + 1;
        }

        if (pwg_pager_ensure_line(pg) != 0)
        {
            if (saved_at >= 0)
                ((pwg_para *)para)->chars[saved_at] = saved_char;

            return -1;
        }

        align = (int)para->align;

        if (trim_end > start && align == EA_ALIGN_CENTER)
        {
            line_w = pwg_measure_range(para, pg->uf, start, trim_end, pg->margin, pg->space_width, pg->tab_width);

            x = pg->margin + (pg->printable_w - line_w) / 2;

            if (x < pg->margin)
                x = pg->margin;
        }
        else if (trim_end > start && align == EA_ALIGN_RIGHT)
        {
            line_w = pwg_measure_range(para, pg->uf, start, trim_end, pg->margin, pg->space_width, pg->tab_width);

            x = (int)pg->page_w - pg->margin - line_w;

            if (x < pg->margin)
                x = pg->margin;
        }
        else if (trim_end > start && align == EA_ALIGN_JUST && !is_last_line)
        {
            line_w = pwg_measure_range(para, pg->uf, start, trim_end, pg->margin, pg->space_width, pg->tab_width);

            n_spaces = 0;

            for (j = start; j < trim_end; j++)
            {
                if (para->chars[j].cp == ' ')
                    n_spaces++;
            }

            if (n_spaces > 0 && line_w < pg->printable_w)
                word_space = (pg->printable_w - line_w) / n_spaces;

            x = pg->margin;
        }
        else
        {
            x = pg->margin;
        }

        if (trim_end > start)
        {
            if (pwg_pager_render_line(pg, para, start, trim_end, x, word_space) != 0)
            {
                if (saved_at >= 0)
                    ((pwg_para *)para)->chars[saved_at] = saved_char;

                return -1;
            }
        }

        if (saved_at >= 0)
            ((pwg_para *)para)->chars[saved_at] = saved_char;

        pg->y += pg->line_height;

        start = end;

        while (start < para->len && para->chars[start].cp == ' ')
            start++;
    }

    return 0;
}

static int pwg_font_open(const TeConfig *cfg, int dpi, PwgFont *uf)
{
    const char *p = NULL;
    FT_Error e;
    int i;
    int size;

    memset(uf, 0, sizeof(*uf));

    if (!cfg)
        return -1;

    p = cfg->print_font_path[0] ? cfg->print_font_path : cfg->ttf_font[0] ? cfg->ttf_font
                                                                          : pwg_default_font();

    e = FT_Init_FreeType(&uf->lib);

    if (e)
        return -1;

    size = cfg->print_font_size > 0 ? cfg->print_font_size : cfg->ttf_size > 0 ? cfg->ttf_size
                                                                               : PWG_FONT_SIZE_PT;

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
        pwg_font_close(uf);
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

static int pwg_export_cleanup(unsigned char **buf, PwgFont *uf, pwg_para *para, char *err, size_t errsz, const char *msg)
{
    if (buf && *buf)
    {
        free(*buf);
        *buf = NULL;
    }

    if (para)
        pwg_para_free(para);

    pwg_font_close(uf);

    return pwg_seterr(err, errsz, msg);
}

int pwg_export_ex(const struct Ed *ed, FILE *fp, const TeConfig *cfg, LayoutHyphenFn hyph, void *hyph_user, char *err, size_t errsz, char *warn, size_t warnsz)
{
    PwgFont uf;
    pwg_para para;
    pwg_pager pg;
    const char *media = NULL;
    unsigned char *buf = NULL;
    double page_w_in;
    double page_h_in;
    unsigned int page_w;
    unsigned int page_h;
    int dpi;
    int margin;
    int char_width;
    int row;
    int rc;

    if (err && errsz > 0)
        err[0] = '\0';

    if (warn && warnsz > 0)
        warn[0] = '\0';

    memset(&uf, 0, sizeof(uf));

    pwg_para_init(&para);

    memset(&pg, 0, sizeof(pg));

    if (!ed || !fp || !cfg)
        return pwg_seterr(err, errsz, "invalid arguments");

    media = cfg->print_media[0] ? cfg->print_media : "na_letter_8.5x11in";

    pwg_parse_media(media, &page_w_in, &page_h_in);

    if (cfg->print_orientation == 4 || cfg->print_orientation == 5)
    {
        double t = page_w_in;

        page_w_in = page_h_in;
        page_h_in = t;
    }

    dpi = (cfg->print_resolution_x > 0 && cfg->print_resolution_y > 0) ? cfg->print_resolution_x : PWG_DPI;

    page_w = (unsigned int)(page_w_in * dpi + 0.5);
    page_h = (unsigned int)(page_h_in * dpi + 0.5);

    margin = (int)(PWG_MARGIN_IN * dpi);

    if (page_w < (unsigned int)(2 * margin) || page_h < (unsigned int)(2 * margin))
    {
        pwg_font_close(&uf);

        return pwg_seterr(err, errsz, "page too small for margins");
    }

    /* Apply custom margins from config */
    if (cfg->print_margin_left_mm >= 0)
        margin = (int)((double)cfg->print_margin_left_mm / 25.4 * dpi + 0.5);

    if (pwg_font_open(cfg, dpi, &uf) != 0)
        return pwg_seterr(err, errsz, "cannot load font");

    char_width = (int)(uf.faces[0]->size->metrics.max_advance >> 6);

    if (char_width <= 0)
    {
        pwg_font_close(&uf);

        return pwg_seterr(err, errsz, "font has zero advance");
    }

    /* PWG raster: no file-level header, each page starts with a CUPS raster header */
    buf = pwg_page_new(page_w, page_h);

    if (!buf)
        return pwg_export_cleanup(&buf, &uf, &para, err, errsz, "write error");

    pg.fp = fp;
    pg.buf = buf;
    pg.page_w = page_w;
    pg.page_h = page_h;
    pg.dpi = dpi;
    pg.margin = margin;
    pg.printable_w = (int)page_w - 2 * margin;
    pg.line_height = (int)((double)uf.faces[0]->size->metrics.height * PWG_LEADING_MUL / 64.0 + 0.5);
    pg.ascender = (int)(uf.faces[0]->size->metrics.ascender >> 6);
    pg.space_width = pwg_font_advance(&uf, ' ');

    if (pg.space_width <= 0)
        pg.space_width = char_width;

    pg.tab_width = cfg->tab_width > 0 ? cfg->tab_width : 4;
    pg.font_size_px = (int)(uf.faces[0]->size->metrics.height >> 6);
    pg.uf = &uf;
    pg.hyph = hyph;
    pg.hyph_user = hyph_user;
    pg.page_count = 0;
    pg.in_page = 0;

    rc = 0;

    for (row = 0; row < ed->count && rc == 0; row++)
    {
        const EdLine *ln = NULL;
        const EdAttrRun *runs = NULL;
        int n_runs;
        int brk;
        int is_last;

        ln = ed->lines[row];

        n_runs = ed_attr_runs(ln, &runs);

        is_last = (row == ed->count - 1);
        brk = is_last ? (int)LB_PARA : (int)ln->brk;

        if (ln->len == 0)
        {
            if (para.has_content)
            {
                if (pwg_pager_emit_para(&pg, &para) != 0)
                    rc = -1;

                pwg_para_reset(&para);
            }

            if (rc == 0)
            {
                if (pwg_pager_blank_line(&pg) != 0)
                    rc = -1;
            }

            continue;
        }

        if (!para.has_content)
        {
            para.align = ln->para_align;
            para.has_content = 1;
        }

        if (pwg_para_append_edline(&para, ln, runs, n_runs) != 0)
        {
            rc = -1;
            break;
        }

        if (brk == LB_SPACE)
        {
            if (pwg_para_push(&para, ' ', 0) != 0)
            {
                rc = -1;
                break;
            }
        }
        else if (brk == LB_HYPHEN || brk == LB_WORD)
        {
            if (pwg_para_add_break(&para, brk == LB_HYPHEN ? 1 : 0) != 0)
            {
                rc = -1;
                break;
            }
        }
        else if (brk == LB_PARA)
        {
            if (pwg_pager_emit_para(&pg, &para) != 0)
                rc = -1;

            pwg_para_reset(&para);
        }
    }

    if (rc == 0 && para.has_content)
    {
        if (pwg_pager_emit_para(&pg, &para) != 0)
            rc = -1;
    }

    if (rc == 0)
    {
        if (pg.in_page || pg.page_count == 0)
        {
            if (!pg.in_page)
            {
                if (pwg_pager_start_page(&pg) != 0)
                    rc = -1;
            }

            if (rc == 0)
            {
                if (pwg_pager_close_page(&pg) != 0)
                    rc = -1;
            }
        }
    }

    /* Write end-of-document marker (zero-filled CUPS raster header) */
    if (rc == 0)
    {
        if (pwg_end_header(pg.fp) != 0)
            rc = -1;
    }

    pwg_para_free(&para);
    free(buf);

    pwg_font_close(&uf);

    if (rc != 0)
        return pwg_seterr(err, errsz, "write error");

    return 0;
}

int pwg_export(const struct Ed *ed, FILE *fp, const TeConfig *cfg, char *err, size_t errsz, char *warn, size_t warnsz)
{
    return pwg_export_ex(ed, fp, cfg, NULL, NULL, err, errsz, warn, warnsz);
}

#endif
