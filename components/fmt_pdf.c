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
#include <stdarg.h>

#include "editor.h"
#include "ed_attr.h"
#include "layout.h"
#include "config.h"
#include "fmt_pdf.h"
#include "../core/utf8.h"
#include "../core/charset.h"

/* Page: US Letter, 1" margins, 12pt Helvetica, 14.4pt leading */
#define PDF_PAGE_W 612.0
#define PDF_PAGE_H 792.0
#define PDF_MARGIN_L 72.0
#define PDF_MARGIN_R 72.0
#define PDF_MARGIN_T 72.0
#define PDF_MARGIN_B 72.0
#define PDF_FONT_SIZE 12.0
#define PDF_LEADING_MUL 1.2    /* Line height = size * 1.2 */
#define PDF_UNDERLINE_OFF -1.5 /* Points below baseline */
#define PDF_UNDERLINE_TH 0.6   /* Line thickness */

/* Font resource names inside the content stream, matching object ids 3..6 */
#define PDF_FN_REGULAR "F0"
#define PDF_FN_BOLD "F1"
#define PDF_FN_ITALIC "F2"
#define PDF_FN_BOLDITALIC "F3"

#define PDF_MAX_TTF_FACES 9 /* Primary + up to 8 fallbacks */

typedef struct
{
    char *buf;
    size_t len;
    size_t cap;
} pdf_buf;

/* PDF writer context, tracks file offsets for the xref table */
typedef struct
{
    FILE *fp;
    long pos;      /* Running offset, updated on every write */
    long *offsets; /* File offset of each object, 1-indexed */
    int n_objs;
    int cap_objs;
    char *err;
    size_t errsz;
    char *warn;
    size_t warnsz;
    int lossy; /* Count of chars replaced with '?' */
} pdf_out;

/* Paragraph model */
typedef struct
{
    unsigned int cp;
    unsigned short mask;
} pdf_wchar;

typedef struct
{
    pdf_wchar *chars;
    int len;
    int cap;
    unsigned char align; /* EA_ALIGN_* from the paragraph's first EdLine */
    int has_content;     /* 1 once at least one line has been merged in */
} pdf_para;

/* Emit a slice of a paragraph into a content stream at (x, y_baseline) */
struct pdf_underline_run
{
    double x0;
    double x1;
};

/* Page management */
typedef struct
{
    pdf_out *o;
    int *page_ids;
    int n_pages;
    int cap_pages;
    int pages_root_id;
    int resources_id;
    pdf_buf content;
    double y_baseline;
    double leading;
    double font_size;
    int tab_width;
    int in_page;
    const struct pdf_font_ctx_tag *fc;
} pdf_pager;

struct ttf_face
{
    unsigned char *bytes;
    size_t len;

    /* Locations of required tables */
    unsigned int off_head, len_head;
    unsigned int off_hhea, len_hhea;
    unsigned int off_maxp, len_maxp;
    unsigned int off_hmtx, len_hmtx;
    unsigned int off_cmap, len_cmap;

    /* Parsed head */
    int units_per_em;
    int bbox_x_min, bbox_y_min, bbox_x_max, bbox_y_max;

    /* Parsed hhea */
    int ascent, descent;
    int num_h_metrics;

    /* Parsed maxp */
    int num_glyphs;

    /* 1 if the file is an OpenType/CFF (OTTO) font; 0 for TrueType */
    int is_otf;

    /* Selected cmap subtables (offsets relative to the file, 0 = absent) */
    unsigned int off_cmap4;
    unsigned int off_cmap12;
};

/* Shared font context; mode 0 = base-14 Helvetica, mode 1 = TTF with fallback */
typedef struct pdf_font_ctx_tag
{
    int mode;
    struct ttf_face faces[PDF_MAX_TTF_FACES];
    int n_faces;
    int face_type0_id[PDF_MAX_TTF_FACES]; /* obj id of the Type0 font per face */
} pdf_font_ctx;

/* Helvetica widths at 1000-unit em, WinAnsi 0x20..0xFF, Adobe base-14 */
static const short pdf_w_helv[224] =
    {
        /* 0x20..0x2F space ! " # $ % & ' ( ) * + , - . / */
        278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
        /* 0x30..0x3F 0..9 : ; < = > ? */
        556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
        /* 0x40..0x4F @ A..O  */
        1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
        /* 0x50..0x5F P..Z [ \ ] ^ _ */
        667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
        /* 0x60..0x6F ` a..o  */
        333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
        /* 0x70..0x7F p..z { | } ~ */
        556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584, 350,
        /* 0x80..0x8F Euro .. Zcaron */
        556, 350, 222, 333, 333, 350, 350, 191, 333, 556, 333, 333, 222, 200, 191, 191,
        /* 0x90..0x9F */
        333, 333, 222, 222, 222, 350, 500, 500, 333, 500, 556, 333, 222, 200, 191, 611,
        /* 0xA0..0xAF nbsp ¡ ¢ £ ¤ ¥ ¦ § ¨ © ª « ¬ shy ® ¯ */
        278, 333, 556, 556, 556, 556, 260, 500, 333, 737, 370, 556, 584, 333, 737, 333,
        /* 0xB0..0xBF ° ± ² ³ ´ µ ¶ · ¸ ¹ º » ¼ ½ ¾ ¿ */
        400, 584, 333, 333, 333, 556, 537, 278, 333, 333, 365, 556, 834, 834, 834, 611,
        /* 0xC0..0xCF À..Ï */
        667, 667, 667, 667, 667, 667, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
        /* 0xD0..0xDF Ð..ß */
        722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
        /* 0xE0..0xEF à..ï */
        556, 556, 556, 556, 556, 556, 889, 500, 556, 556, 556, 556, 278, 278, 278, 278,
        /* 0xF0..0xFF ð..ÿ */
        556, 556, 556, 556, 556, 556, 556, 584, 611, 556, 556, 556, 556, 500, 556, 500};

static const short pdf_w_helv_bold[224] =
    {
        278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
        556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611,
        975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
        667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556,
        333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
        611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584, 350,
        556, 350, 278, 556, 556, 350, 350, 238, 500, 556, 333, 333, 278, 200, 238, 238,
        389, 389, 278, 278, 278, 350, 556, 611, 333, 556, 611, 333, 278, 200, 238, 611,
        278, 333, 556, 556, 556, 556, 280, 556, 333, 737, 370, 556, 584, 333, 737, 333,
        400, 584, 333, 333, 333, 611, 556, 278, 333, 333, 365, 556, 834, 834, 834, 611,
        722, 722, 722, 722, 722, 722, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
        722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
        556, 556, 556, 556, 556, 556, 889, 556, 556, 556, 556, 556, 278, 278, 278, 278,
        611, 611, 611, 611, 611, 611, 611, 584, 611, 611, 611, 611, 611, 556, 611, 556};

/* Forward declarations of base-14 helpers, used as fallback path */
static int pdf_glyph_width(unsigned char b, unsigned short mask);
static int pdf_cp_to_winansi(unsigned int cp, unsigned char *out);

static unsigned int ttf_be16(const unsigned char *p)
{
    return ((unsigned int)p[0] << 8) | (unsigned int)p[1];
}

static unsigned int ttf_be32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

static int ttf_bes16(const unsigned char *p)
{
    int v = (int)ttf_be16(p);

    return v < 32768 ? v : v - 65536;
}

static void ttf_face_free(struct ttf_face *f)
{
    if (f && f->bytes)
        free(f->bytes);

    if (f)
        memset(f, 0, sizeof(*f));
}

/* Try to locate a usable cmap subtable in the font, filling off_cmap4/off_cmap12 */
static void ttf_face_pick_cmaps(struct ttf_face *f)
{
    const unsigned char *cmap = NULL;
    unsigned int num_sub;
    unsigned int sub_i;
    unsigned int platf, enc, off;
    unsigned int sub_off;
    unsigned int fmt;

    cmap = f->bytes + f->off_cmap;

    if (f->len_cmap < 4)
        return;

    num_sub = ttf_be16(cmap + 2);

    for (sub_i = 0; sub_i < num_sub; sub_i++)
    {
        if (4 + sub_i * 8 + 8 > f->len_cmap)
            break;

        platf = ttf_be16(cmap + 4 + sub_i * 8);
        enc = ttf_be16(cmap + 4 + sub_i * 8 + 2);
        off = ttf_be32(cmap + 4 + sub_i * 8 + 4);

        if (off + 4 > f->len_cmap)
            continue;

        sub_off = f->off_cmap + off;

        if (sub_off + 4 > f->len)
            continue;

        fmt = ttf_be16(f->bytes + sub_off);

        if (fmt == 4)
        {
            /* Prefer Windows Unicode BMP (3,1), then Unicode (0,x) */
            if (platf == 3 && enc == 1)
                f->off_cmap4 = sub_off;
            else if (!f->off_cmap4 && (platf == 0 || (platf == 3 && enc == 0)))
                f->off_cmap4 = sub_off;
        }
        else if (fmt == 12)
        {
            /* Prefer Windows UCS-4 (3,10) or Unicode full (0,4/0,6) */
            if ((platf == 3 && enc == 10) || (platf == 0 && (enc == 4 || enc == 6)))
                f->off_cmap12 = sub_off;
            else if (!f->off_cmap12)
                f->off_cmap12 = sub_off;
        }
    }
}

/* Load a TTF file from disk, parse required tables */
static int ttf_face_load(struct ttf_face *f, const char *path)
{
    FILE *fp = NULL;
    long fsz;
    unsigned char *buf = NULL;
    unsigned int magic;
    unsigned int num_tables;
    unsigned int i;
    unsigned int tag;
    unsigned int tbl_off, tbl_len;
    const unsigned char *head = NULL;
    const unsigned char *hhea = NULL;
    const unsigned char *maxp = NULL;

    memset(f, 0, sizeof(*f));

    fp = fopen(path, "rb");

    if (!fp)
        return -1;

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return -1;
    }

    fsz = ftell(fp);

    /* Sanity: 12 bytes minimum (sfnt header), 32 MB max */
    if (fsz < 12 || fsz > 32L * 1024L * 1024L)
    {
        fclose(fp);
        return -1;
    }

    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return -1;
    }

    buf = (unsigned char *)malloc((size_t)fsz);

    if (!buf)
    {
        fclose(fp);
        return -1;
    }

    if (fread(buf, 1, (size_t)fsz, fp) != (size_t)fsz)
    {
        free(buf);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    f->bytes = buf;
    f->len = (size_t)fsz;

    /* sfnt magic: 0x00010000 (TTF), 0x74727565 (true), or 0x4F54544F (OTTO/CFF) */
    magic = ttf_be32(buf);

    if (magic != 0x00010000 && magic != 0x74727565 && magic != 0x4F54544F)
    {
        ttf_face_free(f);
        return -1;
    }

    f->is_otf = (magic == 0x4F54544F);

    num_tables = ttf_be16(buf + 4);

    if (num_tables > 64 || 12 + num_tables * 16 > f->len)
    {
        ttf_face_free(f);
        return -1;
    }

    for (i = 0; i < num_tables; i++)
    {
        tag = ttf_be32(buf + 12 + i * 16);
        tbl_off = ttf_be32(buf + 12 + i * 16 + 8);
        tbl_len = ttf_be32(buf + 12 + i * 16 + 12);

        if (tbl_off >= f->len || tbl_off + tbl_len > f->len)
            continue;

        if (tag == 0x68656164)
        {
            f->off_head = tbl_off;
            f->len_head = tbl_len;
        }
        else if (tag == 0x68686561)
        {
            f->off_hhea = tbl_off;
            f->len_hhea = tbl_len;
        }
        else if (tag == 0x6D617870)
        {
            f->off_maxp = tbl_off;
            f->len_maxp = tbl_len;
        }
        else if (tag == 0x686D7478)
        {
            f->off_hmtx = tbl_off;
            f->len_hmtx = tbl_len;
        }
        else if (tag == 0x636D6170)
        {
            f->off_cmap = tbl_off;
            f->len_cmap = tbl_len;
        }
    }

    if (!f->off_head || !f->off_hhea || !f->off_maxp || !f->off_hmtx || !f->off_cmap || f->len_head < 54 || f->len_hhea < 36 || f->len_maxp < 6 || f->len_cmap < 4)
    {
        ttf_face_free(f);
        return -1;
    }

    head = buf + f->off_head;
    hhea = buf + f->off_hhea;
    maxp = buf + f->off_maxp;

    f->units_per_em = (int)ttf_be16(head + 18);
    f->bbox_x_min = ttf_bes16(head + 36);
    f->bbox_y_min = ttf_bes16(head + 38);
    f->bbox_x_max = ttf_bes16(head + 40);
    f->bbox_y_max = ttf_bes16(head + 42);

    f->ascent = ttf_bes16(hhea + 4);
    f->descent = ttf_bes16(hhea + 6);
    f->num_h_metrics = (int)ttf_be16(hhea + 34);

    f->num_glyphs = (int)ttf_be16(maxp + 4);

    if (f->units_per_em <= 0 || f->num_glyphs <= 0 || f->num_h_metrics <= 0 || f->num_h_metrics > f->num_glyphs)
    {
        ttf_face_free(f);
        return -1;
    }

    if (f->off_hmtx + (unsigned int)f->num_h_metrics * 4 > f->len)
    {
        ttf_face_free(f);
        return -1;
    }

    ttf_face_pick_cmaps(f);

    if (!f->off_cmap4 && !f->off_cmap12)
    {
        ttf_face_free(f);
        return -1;
    }

    return 0;
}

/* cmap format 4 lookup (BMP only) */
static int ttf_cmap_lookup_fmt4(const struct ttf_face *f, unsigned int cp)
{
    const unsigned char *sub = NULL;
    unsigned int seg_count_x2;
    unsigned int seg_count;
    unsigned int end_ofs, start_ofs, delta_ofs, range_ofs;
    unsigned int i;
    unsigned int end_val, start_val;
    int delta;
    unsigned int rng;
    unsigned int gid_ofs;
    unsigned int raw;

    if (cp > 0xFFFF)
        return 0;

    sub = f->bytes + f->off_cmap4;
    seg_count_x2 = ttf_be16(sub + 6);
    seg_count = seg_count_x2 / 2;

    end_ofs = 14;
    start_ofs = end_ofs + seg_count_x2 + 2;
    delta_ofs = start_ofs + seg_count_x2;
    range_ofs = delta_ofs + seg_count_x2;

    for (i = 0; i < seg_count; i++)
    {
        end_val = ttf_be16(sub + end_ofs + i * 2);

        if (end_val < cp)
            continue;

        start_val = ttf_be16(sub + start_ofs + i * 2);

        if (start_val > cp)
            return 0;

        delta = ttf_bes16(sub + delta_ofs + i * 2);
        rng = ttf_be16(sub + range_ofs + i * 2);

        if (rng == 0)
            return (int)((cp + (unsigned int)delta) & 0xFFFF);

        gid_ofs = range_ofs + i * 2 + rng + (cp - start_val) * 2;

        if (gid_ofs + 2 > f->len_cmap)
            return 0;

        raw = ttf_be16(sub + gid_ofs);

        if (raw == 0)
            return 0;

        return (int)((raw + (unsigned int)delta) & 0xFFFF);
    }

    return 0;
}

/* cmap format 12 lookup (full Unicode, needed for emoji and rare CJK) */
static int ttf_cmap_lookup_fmt12(const struct ttf_face *f, unsigned int cp)
{
    const unsigned char *sub = NULL;
    unsigned int num_groups;
    unsigned int i;
    unsigned int start_char, end_char, start_gid;

    sub = f->bytes + f->off_cmap12;
    num_groups = ttf_be32(sub + 12);

    if (16 + num_groups * 12 > f->len - f->off_cmap12)
        return 0;

    for (i = 0; i < num_groups; i++)
    {
        start_char = ttf_be32(sub + 16 + i * 12);
        end_char = ttf_be32(sub + 16 + i * 12 + 4);
        start_gid = ttf_be32(sub + 16 + i * 12 + 8);

        if (cp < start_char)
            return 0;

        if (cp <= end_char)
            return (int)(start_gid + (cp - start_char));
    }

    return 0;
}

/* Get glyph id for a codepoint, prefers fmt 12 for supplementary planes */
static int ttf_face_glyph_id(const struct ttf_face *f, unsigned int cp)
{
    int gid;

    if (f->off_cmap12)
    {
        gid = ttf_cmap_lookup_fmt12(f, cp);

        if (gid > 0)
            return gid;
    }

    if (f->off_cmap4 && cp <= 0xFFFF)
    {
        gid = ttf_cmap_lookup_fmt4(f, cp);

        if (gid > 0)
            return gid;
    }

    return 0;
}

/* Horizontal advance in font design units for a given glyph id */
static int ttf_face_advance(const struct ttf_face *f, int gid)
{
    const unsigned char *h = NULL;

    if (gid < 0 || gid >= f->num_glyphs)
        return 0;

    h = f->bytes + f->off_hmtx;

    if (gid < f->num_h_metrics)
        return (int)ttf_be16(h + gid * 4);

    return (int)ttf_be16(h + (f->num_h_metrics - 1) * 4);
}

static void pdf_font_ctx_init(pdf_font_ctx *fc)
{
    memset(fc, 0, sizeof(*fc));
}

static void pdf_font_ctx_free(pdf_font_ctx *fc)
{
    int i;

    if (!fc)
        return;

    for (i = 0; i < fc->n_faces; i++)
        ttf_face_free(&fc->faces[i]);

    memset(fc, 0, sizeof(*fc));
}

/* Load primary TTF and all configured fallbacks */
static int pdf_font_ctx_try_ttf(pdf_font_ctx *fc, const TeConfig *cfg)
{
    int i;

    if (!cfg || !cfg->ttf_font[0])
        return -1;

    if (ttf_face_load(&fc->faces[0], cfg->ttf_font) != 0)
        return -1;

    fc->n_faces = 1;

    for (i = 0; i < TE_CFG_TTF_FALLBACKS && fc->n_faces < PDF_MAX_TTF_FACES; i++)
    {
        if (!cfg->ttf_fallback[i][0])
            continue;

        if (ttf_face_load(&fc->faces[fc->n_faces], cfg->ttf_fallback[i]) == 0)
            fc->n_faces++;
    }

    fc->mode = 1;

    return 0;
}

/* TTF fallback lookup */
static int pdf_font_ctx_lookup(const pdf_font_ctx *fc, unsigned int cp, int *out_face, int *out_gid, int *out_adv_1000)
{
    int i;
    int gid;
    int upem;

    for (i = 0; i < fc->n_faces; i++)
    {
        gid = ttf_face_glyph_id(&fc->faces[i], cp);

        if (gid > 0)
        {
            upem = fc->faces[i].units_per_em;

            if (upem <= 0)
                continue;

            *out_face = i;
            *out_gid = gid;
            *out_adv_1000 = ttf_face_advance(&fc->faces[i], gid) * 1000 / upem;

            return 0;
        }
    }

    return -1;
}

/* Codepoint advance in 1/1000 em, dispatches by mode and records the face index */
static int pdf_ctx_cp_advance(const pdf_font_ctx *fc, unsigned int cp, unsigned short mask, int *out_face_idx)
{
    int fi;
    int gid;
    int adv;
    unsigned char b;
    int upem;

    if (fc && fc->mode == 1)
    {
        if (pdf_font_ctx_lookup(fc, cp, &fi, &gid, &adv) == 0)
        {
            if (out_face_idx)
                *out_face_idx = fi;

            return adv;
        }

        if (out_face_idx)
            *out_face_idx = 0;

        upem = fc->faces[0].units_per_em;

        return upem > 0 ? ttf_face_advance(&fc->faces[0], 0) * 1000 / upem : 500;
    }

    if (out_face_idx)
        *out_face_idx = -1;

    if (!pdf_cp_to_winansi(cp, &b))
        b = '?';

    return pdf_glyph_width(b, mask);
}

/* Look up glyph width at 1000-unit em for a WinAnsi byte and a style mask */
static int pdf_glyph_width(unsigned char b, unsigned short mask)
{
    const short *tbl = NULL;

    if (b < 0x20)
        return 0;

    tbl = (mask & EA_BOLD) ? pdf_w_helv_bold : pdf_w_helv;

    return tbl[b - 0x20];
}

/* UTF-8 codepoint -> WinAnsi byte */
static int pdf_cp_to_winansi(unsigned int cp, unsigned char *out)
{
    char u8[8];
    char dst[4];
    int nu;
    int nd;

    if (cp < 0x20)
    {
        *out = '?';
        return 0;
    }

    if (cp < 0x80)
    {
        *out = (unsigned char)cp;
        return 1;
    }

    nu = utf8_encode(cp, u8);

    if (nu <= 0)
    {
        *out = '?';
        return 0;
    }

    nd = utf8_to_charset("CP1252", u8, nu, dst, (int)sizeof(dst));

    if (nd != 1 || (unsigned char)dst[0] == '?')
    {
        *out = '?';
        return 0;
    }

    *out = (unsigned char)dst[0];

    return 1;
}

static void pdfb_init(pdf_buf *b)
{
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}

static void pdfb_free(pdf_buf *b)
{
    free(b->buf);

    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}

static int pdfb_reserve(pdf_buf *b, size_t extra)
{
    size_t need;
    char *nb = NULL;

    need = b->len + extra + 1;

    if (need <= b->cap)
        return 0;

    if (b->cap < 256)
        b->cap = 256;

    while (b->cap < need)
        b->cap *= 2;

    nb = (char *)realloc(b->buf, b->cap);

    if (!nb)
        return -1;

    b->buf = nb;

    return 0;
}

static int pdfb_write(pdf_buf *b, const void *src, size_t n)
{
    if (pdfb_reserve(b, n) != 0)
        return -1;

    memcpy(b->buf + b->len, src, n);

    b->len += n;
    b->buf[b->len] = '\0';

    return 0;
}

static int pdfb_puts(pdf_buf *b, const char *s)
{
    return pdfb_write(b, s, strlen(s));
}

static int pdfb_printf(pdf_buf *b, const char *fmt, ...)
{
    va_list ap;
    char tmp[256];
    int n;
    int i;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (n < 0)
        return -1;

    if ((size_t)n >= sizeof(tmp))
        n = (int)sizeof(tmp) - 1;

    for (i = 0; i < n; i++)
    {
        if (tmp[i] == ',')
            tmp[i] = '.';
    }

    return pdfb_write(b, tmp, (size_t)n);
}

static void pdf_seterr(pdf_out *o, const char *msg)
{
    if (o->err && o->errsz > 0 && !o->err[0])
    {
        strncpy(o->err, msg, o->errsz - 1);
        o->err[o->errsz - 1] = '\0';
    }
}

static int pdf_grow_objs(pdf_out *o, int n)
{
    int need;
    long *no = NULL;

    need = o->n_objs + n;

    if (need <= o->cap_objs)
        return 0;

    if (o->cap_objs < 16)
        o->cap_objs = 16;

    while (o->cap_objs < need)
        o->cap_objs *= 2;

    no = (long *)realloc(o->offsets, (size_t)o->cap_objs * sizeof(long));

    if (!no)
        return -1;

    o->offsets = no;
    return 0;
}

static int pdf_alloc_id(pdf_out *o)
{
    if (pdf_grow_objs(o, 1) != 0)
        return 0;

    o->offsets[o->n_objs] = 0;
    o->n_objs++;

    return o->n_objs;
}

static int pdf_write_raw(pdf_out *o, const void *buf, size_t n)
{
    if (n == 0)
        return 0;

    if (fwrite(buf, 1, n, o->fp) != n)
    {
        pdf_seterr(o, "cannot write output");
        return -1;
    }

    o->pos += (long)n;
    return 0;
}

static int pdf_puts(pdf_out *o, const char *s)
{
    return pdf_write_raw(o, s, strlen(s));
}

static int pdf_printf(pdf_out *o, const char *fmt, ...)
{
    va_list ap;
    char tmp[512];
    int n;
    int i;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        pdf_seterr(o, "format error");
        return -1;
    }

    if ((size_t)n >= sizeof(tmp))
        n = (int)sizeof(tmp) - 1;

    for (i = 0; i < n; i++)
    {
        if (tmp[i] == ',')
            tmp[i] = '.';
    }

    return pdf_write_raw(o, tmp, (size_t)n);
}

static int pdf_begin_obj(pdf_out *o, int id)
{
    if (id <= 0 || id > o->n_objs)
    {
        pdf_seterr(o, "internal: bad object id");
        return -1;
    }

    o->offsets[id - 1] = o->pos;

    return pdf_printf(o, "%d 0 obj\n", id);
}

static int pdf_end_obj(pdf_out *o)
{
    return pdf_puts(o, "endobj\n");
}

static int pdf_emit_stream(pdf_out *o, int id, const pdf_buf *b)
{
    if (pdf_begin_obj(o, id) != 0)
        return -1;

    if (pdf_printf(o, "<< /Length %lu >>\nstream\n", (unsigned long)b->len) != 0)
        return -1;

    if (pdf_write_raw(o, b->buf, b->len) != 0)
        return -1;

    if (pdf_puts(o, "\nendstream\n") != 0)
        return -1;

    return pdf_end_obj(o);
}

/* Content-stream helpers: escape a WinAnsi byte for a PDF (Tj) string */
static int pdf_esc_byte(pdf_buf *s, unsigned char b)
{
    if (b == '(' || b == ')' || b == '\\')
    {
        char e[2];

        e[0] = '\\';
        e[1] = (char)b;

        return pdfb_write(s, e, 2);
    }

    if (b >= 0x20 && b < 0x7F)
        return pdfb_write(s, &b, 1);

    /* Octal escape for the whole 0x80..0xFF range and any control byte */
    return pdfb_printf(s, "\\%03o", b);
}

/* Pick the font resource name for a given attribute mask */
static const char *pdf_pick_font(unsigned short mask)
{
    int bold = (mask & EA_BOLD) != 0;
    int italic = (mask & EA_ITALIC) != 0;

    if (bold && italic)
        return PDF_FN_BOLDITALIC;

    if (bold)
        return PDF_FN_BOLD;

    if (italic)
        return PDF_FN_ITALIC;

    return PDF_FN_REGULAR;
}

static void pdf_para_init(pdf_para *p)
{
    memset(p, 0, sizeof(*p));
}

static void pdf_para_free(pdf_para *p)
{
    free(p->chars);
    memset(p, 0, sizeof(*p));
}

static void pdf_para_reset(pdf_para *p)
{
    p->len = 0;
    p->align = EA_ALIGN_LEFT;
    p->has_content = 0;
}

static int pdf_para_push(pdf_para *p, unsigned int cp, unsigned short mask)
{
    if (p->len >= p->cap)
    {
        int nc;
        pdf_wchar *nb;

        nc = p->cap ? p->cap * 2 : 128;
        nb = (pdf_wchar *)realloc(p->chars, (size_t)nc * sizeof(*nb));

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

/* Append an EdLine's characters (with their attribute runs resolved to per-char masks) onto the paragraph buffer */
static int pdf_para_append_edline(pdf_para *p, const EdLine *ln, const EdAttrRun *runs, int n_runs)
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

        if (pdf_para_push(p, cp, mask) != 0)
            return -1;
    }

    return 0;
}

/* Width in points of the char range [start, end) at the given font size. A tab counts as tab_width spaces, matching pdf_emit_para_range's own advance */
static double pdf_para_measure_range(const pdf_para *p, const pdf_font_ctx *fc, int start, int end, double font_size, int tab_width)
{
    double units;
    int i;

    if (end > p->len)
        end = p->len;

    if (tab_width <= 0)
        tab_width = 4;

    units = 0.0;

    for (i = start; i < end; i++)
    {
        unsigned int cp;
        unsigned short mask;

        cp = p->chars[i].cp;
        mask = p->chars[i].mask;

        if (cp == '\t')
        {
            units += (double)tab_width * (double)pdf_ctx_cp_advance(fc, ' ', 0, NULL);

            continue;
        }

        units += (double)pdf_ctx_cp_advance(fc, cp, mask, NULL);
    }

    return units * font_size / 1000.0;
}

/* Find the wrap boundary starting at 'start': the highest index E such that the range [start, E) fits within max_w points */
static int pdf_para_wrap_next(const pdf_para *p, const pdf_font_ctx *fc, int start, double max_w, double font_size, int tab_width)
{
    double units_max;
    double units;
    int last_space_at; /* index of last space that still fits (-1 = none) */
    int i;

    if (tab_width <= 0)
        tab_width = 4;

    if (start >= p->len)
        return p->len;

    units_max = max_w * 1000.0 / font_size;
    units = 0.0;
    last_space_at = -1;

    for (i = start; i < p->len; i++)
    {
        unsigned int cp;
        unsigned short mask;
        int gw;

        cp = p->chars[i].cp;
        mask = p->chars[i].mask;

        if (cp == '\t')
            gw = tab_width * pdf_ctx_cp_advance(fc, ' ', 0, NULL);
        else
            gw = pdf_ctx_cp_advance(fc, cp, mask, NULL);

        if (units + (double)gw > units_max && i > start)
        {
            /* Overflow: break after the last accepted space, if any */
            if (last_space_at >= start)
                return last_space_at + 1;

            return i;
        }

        units += (double)gw;

        if (cp == ' ')
            last_space_at = i;
    }

    return p->len;
}

static int pdf_emit_para_range(pdf_buf *s, const pdf_para *p, const pdf_font_ctx *fc, int start, int end, double x, double y_baseline, double font_size, int tab_width, double word_space, int *lossy)
{
    unsigned short cur_mask;
    const char *cur_font = NULL;
    int cur_face = -2; /* TTF face currently selected in the stream */
    int i;
    double x_pen;
    double x_ul_start;
    int in_ul;
    int in_bt;
    struct pdf_underline_run *uls = NULL;
    int n_uls;
    int cap_uls;
    int rc;

    cur_mask = 0xFFFF;
    x_pen = x;
    x_ul_start = 0.0;
    in_ul = 0;
    in_bt = 0;
    n_uls = 0;
    cap_uls = 0;
    rc = 0;

    if (end > p->len)
        end = p->len;

    if (start >= end)
        return 0;

    if (pdfb_puts(s, "BT\n") != 0)
        rc = -1;
    else
        in_bt = 1;

    if (rc == 0)
    {
        double tw;

        /* TTF/CFF emits GIDs, so Tw cannot detect spaces; base-14 can use Tw */
        if (fc && fc->mode == 1)
            tw = 0.0;
        else
            tw = (font_size > 0.0) ? word_space * 1000.0 / font_size : word_space;

        if (pdfb_printf(s, "%.3f Tw\n", tw) != 0)
            rc = -1;
    }

    if (rc == 0)
    {
        if (pdfb_printf(s, "1 0 0 1 %.2f %.2f Tm\n", x, y_baseline) != 0)
            rc = -1;
    }

    if (rc == 0)
    {
        for (i = start; i < end; i++)
        {
            unsigned int cp;
            unsigned short want_mask;
            const char *want_font = NULL;
            int want_face = -1;
            unsigned char b = 0;
            int mapped = 1;
            int gid = 0;
            int adv_1000;
            double advance;
            pdf_buf run_str;

            cp = p->chars[i].cp;
            want_mask = p->chars[i].mask;

            /* Underline toggle: close pending segment or open a new one */
            if (((want_mask & EA_UNDERLINE) != 0) != in_ul)
            {
                if (in_ul)
                {
                    if (n_uls >= cap_uls)
                    {
                        int nc;
                        struct pdf_underline_run *nu;

                        nc = cap_uls ? cap_uls * 2 : 8;
                        nu = (struct pdf_underline_run *)realloc(uls, (size_t)nc * sizeof(*uls));

                        if (!nu)
                        {
                            rc = -1;
                            break;
                        }

                        uls = nu;
                        cap_uls = nc;
                    }

                    uls[n_uls].x0 = x_ul_start;
                    uls[n_uls].x1 = x_pen;
                    n_uls++;
                    in_ul = 0;
                }
                else
                {
                    x_ul_start = x_pen;
                    in_ul = 1;
                }
            }

            if (rc != 0)
                break;

            /* Resolve advance and, in TTF mode, the face+glyph id used to render */
            if (fc && fc->mode == 1)
            {
                unsigned int glyph_cp = (cp == '\t') ? (unsigned int)' ' : cp;
                int face_hit;
                int gid_hit;
                int adv_hit;

                if (pdf_font_ctx_lookup(fc, glyph_cp, &face_hit, &gid_hit, &adv_hit) == 0)
                {
                    want_face = face_hit;
                    gid = gid_hit;
                    adv_1000 = adv_hit;
                }
                else
                {
                    /* Codepoint absent in all faces: use notdef of primary */
                    want_face = 0;
                    gid = 0;
                    adv_1000 = fc->faces[0].units_per_em > 0 ? ttf_face_advance(&fc->faces[0], 0) * 1000 / fc->faces[0].units_per_em : 500;

                    if (lossy)
                        (*lossy)++;
                }
            }
            else
            {
                want_font = pdf_pick_font(want_mask);

                if (cp == '\t')
                {
                    b = ' ';
                    adv_1000 = pdf_glyph_width(b, want_mask);
                }
                else
                {
                    mapped = pdf_cp_to_winansi(cp, &b);

                    if (!mapped)
                    {
                        if (lossy)
                            (*lossy)++;

                        b = '?';
                    }

                    adv_1000 = pdf_glyph_width(b, want_mask);
                }
            }

            /* Font resource switch: base-14 picks by style, TTF picks by face */
            if (fc && fc->mode == 1)
            {
                if (want_face != cur_face)
                {
                    if (pdfb_printf(s, "/TT%d %.2f Tf\n", want_face, font_size) != 0)
                    {
                        rc = -1;
                        break;
                    }

                    cur_face = want_face;
                    cur_font = NULL;
                }
            }
            else
            {
                if (want_font != cur_font)
                {
                    if (pdfb_printf(s, "/%s %.2f Tf\n", want_font, font_size) != 0)
                    {
                        rc = -1;
                        break;
                    }

                    cur_font = want_font;
                }
            }

            cur_mask = want_mask;

            pdfb_init(&run_str);

            if (fc && fc->mode == 1)
            {
                /* TTF: emit <GID>Tj (Identity-H, CID = GID). Tabs expand to N spaces */
                int reps = (cp == '\t') ? (tab_width > 0 ? tab_width : 4) : 1;
                int k;
                int adv_out = adv_1000;
                unsigned int emit_gid = (unsigned int)gid;

                if (pdfb_puts(&run_str, "<") != 0)
                {
                    pdfb_free(&run_str);

                    rc = -1;
                    break;
                }

                for (k = 0; k < reps; k++)
                {
                    if (pdfb_printf(&run_str, "%04X", emit_gid & 0xFFFF) != 0)
                    {
                        pdfb_free(&run_str);

                        rc = -1;
                        break;
                    }
                }

                if (rc != 0)
                    break;

                if (pdfb_puts(&run_str, ">") != 0)
                {
                    pdfb_free(&run_str);

                    rc = -1;
                    break;
                }

                /* Justified TTF/CFF: add extra space as a negative TJ adjustment */
                if (cp == ' ' && word_space != 0.0)
                {
                    int adj = (font_size > 0.0) ? (int)(-word_space * 1000.0 / font_size) : 0;

                    if (pdfb_printf(&run_str, " %d", adj) != 0)
                    {
                        pdfb_free(&run_str);

                        rc = -1;
                        break;
                    }
                }

                if (cp == '\t')
                    adv_out *= reps;

                adv_1000 = adv_out;
            }
            else
            {
                /* Base-14: emit ( bytes ) Tj with WinAnsi octal escapes */
                if (pdfb_puts(&run_str, "(") != 0)
                {
                    pdfb_free(&run_str);

                    rc = -1;
                    break;
                }

                if (cp == '\t')
                {
                    int k;
                    int tw = tab_width > 0 ? tab_width : 4;

                    for (k = 0; k < tw; k++)
                    {
                        if (pdf_esc_byte(&run_str, ' ') != 0)
                        {
                            pdfb_free(&run_str);

                            rc = -1;
                            break;
                        }
                    }

                    if (rc != 0)
                        break;

                    adv_1000 *= tw;
                }
                else
                {
                    if (pdf_esc_byte(&run_str, b) != 0)
                    {
                        pdfb_free(&run_str);

                        rc = -1;
                        break;
                    }
                }

                if (pdfb_puts(&run_str, ") Tj\n") != 0)
                {
                    pdfb_free(&run_str);

                    rc = -1;
                    break;
                }
            }

            if (fc && fc->mode == 1)
            {
                if (pdfb_puts(s, "[") != 0)
                {
                    pdfb_free(&run_str);

                    rc = -1;
                    break;
                }

                if (pdfb_write(s, run_str.buf, run_str.len) != 0)
                {
                    pdfb_free(&run_str);

                    rc = -1;
                    break;
                }

                if (pdfb_puts(s, "] TJ\n") != 0)
                {
                    pdfb_free(&run_str);

                    rc = -1;
                    break;
                }
            }
            else
            {
                if (pdfb_write(s, run_str.buf, run_str.len) != 0)
                {
                    pdfb_free(&run_str);

                    rc = -1;
                    break;
                }
            }

            pdfb_free(&run_str);

            advance = (double)adv_1000 * font_size / 1000.0;

            if (cp == ' ' && word_space != 0.0)
                advance += word_space;

            x_pen += advance;
        }
    }

    if (rc == 0 && in_ul)
    {
        if (n_uls >= cap_uls)
        {
            int nc;
            struct pdf_underline_run *nu = NULL;

            nc = cap_uls ? cap_uls * 2 : 8;
            nu = (struct pdf_underline_run *)realloc(uls, (size_t)nc * sizeof(*uls));

            if (!nu)
            {
                rc = -1;
            }
            else
            {
                uls = nu;
                cap_uls = nc;
            }
        }

        if (rc == 0)
        {
            uls[n_uls].x0 = x_ul_start;
            uls[n_uls].x1 = x_pen;
            n_uls++;
        }
    }

    if (rc == 0)
    {
        if (pdfb_puts(s, "ET\n") != 0)
            rc = -1;
        else
            in_bt = 0;
    }

    if (rc == 0 && n_uls > 0)
    {
        int u;
        double y_ul = y_baseline + PDF_UNDERLINE_OFF;

        for (u = 0; u < n_uls; u++)
        {
            if (pdfb_printf(s, "%.2f %.2f %.2f %.2f re f\n", uls[u].x0, y_ul, uls[u].x1 - uls[u].x0, PDF_UNDERLINE_TH) != 0)
            {
                rc = -1;
                break;
            }
        }
    }

    if (rc != 0 && in_bt)
        pdfb_puts(s, "ET\n");

    free(uls);

    return rc;
}

static int pdf_pager_add_page_id(pdf_pager *p, int id)
{
    if (p->n_pages >= p->cap_pages)
    {
        int nc;
        int *np = NULL;
        nc = p->cap_pages ? p->cap_pages * 2 : 8;
        np = (int *)realloc(p->page_ids, (size_t)nc * sizeof(int));

        if (!np)
        {
            pdf_seterr(p->o, "out of memory");
            return -1;
        }

        p->page_ids = np;
        p->cap_pages = nc;
    }

    p->page_ids[p->n_pages++] = id;

    return 0;
}

static int pdf_pager_open_page(pdf_pager *p)
{
    pdfb_free(&p->content);
    pdfb_init(&p->content);

    p->y_baseline = PDF_PAGE_H - PDF_MARGIN_T - p->font_size;
    p->in_page = 1;

    return 0;
}

static int pdf_pager_close_page(pdf_pager *p)
{
    int stream_id;
    int page_id;

    if (!p->in_page)
        return 0;

    stream_id = pdf_alloc_id(p->o);

    if (stream_id == 0)
        return -1;

    if (pdf_emit_stream(p->o, stream_id, &p->content) != 0)
        return -1;

    pdfb_free(&p->content);

    page_id = pdf_alloc_id(p->o);

    if (page_id == 0)
        return -1;

    if (pdf_begin_obj(p->o, page_id) != 0)
        return -1;

    if (pdf_printf(p->o, "<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.0f %.0f] /Resources %d 0 R /Contents %d 0 R >>\n", p->pages_root_id, PDF_PAGE_W, PDF_PAGE_H, p->resources_id, stream_id) != 0)
        return -1;

    if (pdf_end_obj(p->o) != 0)
        return -1;

    if (pdf_pager_add_page_id(p, page_id) != 0)
        return -1;

    p->in_page = 0;
    return 0;
}

/* Ensure there is room for one more visual line on the current page. If not, close it and open a new one */
static int pdf_pager_ensure_line(pdf_pager *p)
{
    if (!p->in_page)
        return pdf_pager_open_page(p);

    if (p->y_baseline - p->leading < PDF_MARGIN_B)
    {
        if (pdf_pager_close_page(p) != 0)
            return -1;

        return pdf_pager_open_page(p);
    }

    return 0;
}

/* Advance the baseline for one blank line (used for empty paragraphs) */
static int pdf_pager_blank_line(pdf_pager *p)
{
    if (pdf_pager_ensure_line(p) != 0)
        return -1;

    p->y_baseline -= p->leading;
    return 0;
}

/* Flush a paragraph: wrap it into visual lines at printable width, apply alignment per line, and page-break as needed */
static int pdf_pager_emit_para(pdf_pager *p, const pdf_para *para, int *lossy)
{
    double avail;
    int start;

    avail = PDF_PAGE_W - PDF_MARGIN_L - PDF_MARGIN_R;

    /* Empty paragraph: a blank vertical space (mirrors an empty EdLine) */
    if (para->len == 0)
        return pdf_pager_blank_line(p);

    start = 0;

    while (start < para->len)
    {
        int end;
        int trim_end;
        double line_w;
        double x;
        int align;
        int is_last_line;
        int n_spaces;
        int j;
        double word_space;

        end = pdf_para_wrap_next(para, p->fc, start, avail, p->font_size, p->tab_width);

        /* Guard: wrap_next must advance by at least one char */
        if (end <= start)
            end = start + 1;

        is_last_line = (end >= para->len);

        /* Trim trailing spaces from the visual line so measurement, alignment and rendering do not include the wrap boundary */
        trim_end = end;

        while (trim_end > start && para->chars[trim_end - 1].cp == ' ')
            trim_end--;

        if (pdf_pager_ensure_line(p) != 0)
            return -1;

        align = (int)para->align;
        word_space = 0.0;

        if (trim_end > start && align == EA_ALIGN_CENTER)
        {
            line_w = pdf_para_measure_range(para, p->fc, start, trim_end, p->font_size, p->tab_width);
            x = PDF_MARGIN_L + (avail - line_w) * 0.5;

            if (x < PDF_MARGIN_L)
                x = PDF_MARGIN_L;
        }
        else if (trim_end > start && align == EA_ALIGN_RIGHT)
        {
            line_w = pdf_para_measure_range(para, p->fc, start, trim_end, p->font_size, p->tab_width);
            x = PDF_PAGE_W - PDF_MARGIN_R - line_w;

            if (x < PDF_MARGIN_L)
                x = PDF_MARGIN_L;
        }
        else if (trim_end > start && align == EA_ALIGN_JUST && !is_last_line)
        {
            line_w = pdf_para_measure_range(para, p->fc, start, trim_end, p->font_size, p->tab_width);
            n_spaces = 0;

            for (j = start; j < trim_end; j++)
            {
                if (para->chars[j].cp == ' ')
                    n_spaces++;
            }

            if (n_spaces > 0 && line_w < avail)
                word_space = (avail - line_w) / (double)n_spaces;

            x = PDF_MARGIN_L;
        }
        else
        {
            x = PDF_MARGIN_L;
        }

        if (trim_end > start)
        {
            if (pdf_emit_para_range(&p->content, para, p->fc, start, trim_end, x, p->y_baseline, p->font_size, p->tab_width, word_space, lossy) != 0)
                return -1;
        }

        p->y_baseline -= p->leading;

        start = end;

        /* Skip leading spaces on the next visual line (they were the wrap boundary that got consumed) */
        while (start < para->len && para->chars[start].cp == ' ')
            start++;
    }

    return 0;
}

/* Font resource objects (base-14, Path A) */
static int pdf_write_base14(pdf_out *o, int id, const char *base_font)
{
    if (pdf_begin_obj(o, id) != 0)
        return -1;

    if (pdf_printf(o, "<< /Type /Font /Subtype /Type1 /BaseFont /%s /Encoding /WinAnsiEncoding >>\n", base_font) != 0)
        return -1;

    return pdf_end_obj(o);
}

/* xref table + trailer */
static int pdf_write_xref(pdf_out *o, int catalog_id)
{
    long xref_pos;
    int i;

    xref_pos = o->pos;

    if (pdf_printf(o, "xref\n0 %d\n", o->n_objs + 1) != 0)
        return -1;

    if (pdf_puts(o, "0000000000 65535 f \n") != 0)
        return -1;

    for (i = 0; i < o->n_objs; i++)
    {
        if (pdf_printf(o, "%010ld 00000 n \n", o->offsets[i]) != 0)
            return -1;
    }

    if (pdf_printf(o, "trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%ld\n%%%%EOF\n", o->n_objs + 1, catalog_id, xref_pos) != 0)
        return -1;

    return 0;
}

/* Emit the /W array of a CIDFontType2 for one TTF face. Widths in 1/1000 em */
static int pdf_write_ttf_widths(pdf_out *o, const struct ttf_face *f)
{
    int i;
    int upem = f->units_per_em;

    if (pdf_puts(o, "/W [ 0 [") != 0)
        return -1;

    for (i = 0; i < f->num_glyphs; i++)
    {
        int adv;

        adv = ttf_face_advance(f, i) * 1000 / upem;

        if (pdf_printf(o, "%s%d", i > 0 ? " " : "", adv) != 0)
            return -1;
    }

    return pdf_puts(o, "] ]\n");
}

/* Emit the four PDF objects that represent one embedded TrueType or CFF face */
static int pdf_write_ttf_face(pdf_out *o, const struct ttf_face *f, int face_idx, int type0_id, int cid_id, int fd_id, int ff_id)
{
    char name[32];
    int upem;
    int bx0, by0, bx1, by1, asc, dsc;
    const char *cid_subtype = NULL;
    const char *cid_extra = NULL;
    const char *file_key = NULL;

    snprintf(name, sizeof(name), "TinyFace%d", face_idx);

    /* TrueType (.ttf) -> CIDFontType2 + FontFile2; CFF (.otf) -> CIDFontType0 + FontFile3 */
    if (f->is_otf)
    {
        cid_subtype = "CIDFontType0";
        cid_extra = "";
        file_key = "FontFile3";
    }
    else
    {
        cid_subtype = "CIDFontType2";
        cid_extra = " /CIDToGIDMap /Identity";
        file_key = "FontFile2";
    }

    /* Type 0 Font */
    if (pdf_begin_obj(o, type0_id) != 0)
        return -1;

    if (pdf_printf(o, "<< /Type /Font /Subtype /Type0 /BaseFont /%s /Encoding /Identity-H /DescendantFonts [%d 0 R] >>\n", name, cid_id) != 0)
        return -1;

    if (pdf_end_obj(o) != 0)
        return -1;

    /* CIDFont with per-glyph widths */
    if (pdf_begin_obj(o, cid_id) != 0)
        return -1;

    if (pdf_printf(o, "<< /Type /Font /Subtype /%s /BaseFont /%s /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor %d 0 R%s ", cid_subtype, name, fd_id, cid_extra) != 0)
        return -1;

    if (pdf_write_ttf_widths(o, f) != 0)
        return -1;

    if (pdf_puts(o, ">>\n") != 0)
        return -1;

    if (pdf_end_obj(o) != 0)
        return -1;

    /* FontDescriptor */
    if (pdf_begin_obj(o, fd_id) != 0)
        return -1;

    upem = f->units_per_em;
    bx0 = f->bbox_x_min * 1000 / upem;
    by0 = f->bbox_y_min * 1000 / upem;
    bx1 = f->bbox_x_max * 1000 / upem;
    by1 = f->bbox_y_max * 1000 / upem;
    asc = f->ascent * 1000 / upem;
    dsc = f->descent * 1000 / upem;

    /* Flags = 4 (symbolic) so viewers do not try to remap our CIDs */
    if (pdf_printf(o, "<< /Type /FontDescriptor /FontName /%s /Flags 4 /FontBBox [%d %d %d %d] /ItalicAngle 0 /Ascent %d /Descent %d /CapHeight %d /StemV 80 /%s %d 0 R >>\n", name, bx0, by0, bx1, by1, asc, dsc, asc, file_key, ff_id) != 0)
        return -1;

    if (pdf_end_obj(o) != 0)
        return -1;

    /* Font stream: raw TTF/OTF bytes */
    if (pdf_begin_obj(o, ff_id) != 0)
        return -1;

    if (f->is_otf)
    {
        if (pdf_printf(o, "<< /Length %lu /Subtype /OpenType >>\nstream\n", (unsigned long)f->len) != 0)
            return -1;
    }
    else
    {
        if (pdf_printf(o, "<< /Length %lu /Length1 %lu >>\nstream\n", (unsigned long)f->len, (unsigned long)f->len) != 0)
            return -1;
    }

    if (pdf_write_raw(o, f->bytes, f->len) != 0)
        return -1;

    if (pdf_puts(o, "\nendstream\n") != 0)
        return -1;

    return pdf_end_obj(o);
}

int pdf_export(const struct Ed *ed, FILE *fp, const TeConfig *cfg, char *err, size_t errsz, char *warn, size_t warnsz)
{
    pdf_out o;
    pdf_pager pager;
    pdf_para para;
    pdf_font_ctx fc;
    int catalog_id;
    int pages_id;
    int font_reg_id = 0, font_bold_id = 0, font_it_id = 0, font_bi_id = 0;
    int resources_id;
    int row;
    int rc;
    int i;
    int f;

    if (err && errsz > 0)
        err[0] = '\0';

    if (warn && warnsz > 0)
        warn[0] = '\0';

    if (!ed || !fp)
        return -1;

    memset(&o, 0, sizeof(o));

    o.fp = fp;
    o.err = err;
    o.errsz = errsz;
    o.warn = warn;
    o.warnsz = warnsz;

    memset(&pager, 0, sizeof(pager));

    pager.o = &o;
    pager.font_size = (cfg && cfg->ttf_size > 0) ? (double)cfg->ttf_size : PDF_FONT_SIZE;
    pager.leading = pager.font_size * PDF_LEADING_MUL;
    pager.tab_width = (cfg && cfg->tab_width > 0) ? cfg->tab_width : 4;

    pdfb_init(&pager.content);

    pdf_para_init(&para);

    /* Use TTF if configured, otherwise fall back to base-14 Helvetica */
    pdf_font_ctx_init(&fc);

    if (cfg && cfg->ttf_font[0])
        pdf_font_ctx_try_ttf(&fc, cfg);

    pager.fc = &fc;

    rc = 0;

    /* Header */
    if (rc == 0)
    {
        if (pdf_puts(&o, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n") != 0)
            rc = -1;
    }

    /* Allocate all fixed object ids up front so /Parent refs work */
    if (rc == 0)
    {
        catalog_id = pdf_alloc_id(&o);
        pages_id = pdf_alloc_id(&o);

        if (catalog_id == 0 || pages_id == 0)
            rc = -1;
    }

    /* Allocate font object ids before the resource catalog */
    if (rc == 0)
    {
        if (fc.mode == 1)
        {
            for (f = 0; f < fc.n_faces && rc == 0; f++)
            {
                int t0 = pdf_alloc_id(&o);
                int cid = pdf_alloc_id(&o);
                int fd = pdf_alloc_id(&o);
                int ff = pdf_alloc_id(&o);

                if (t0 == 0 || cid == 0 || fd == 0 || ff == 0)
                {
                    rc = -1;
                    break;
                }

                fc.face_type0_id[f] = t0;

                if (pdf_write_ttf_face(&o, &fc.faces[f], f, t0, cid, fd, ff) != 0)
                    rc = -1;
            }
        }
        else
        {
            font_reg_id = pdf_alloc_id(&o);
            font_bold_id = pdf_alloc_id(&o);
            font_it_id = pdf_alloc_id(&o);
            font_bi_id = pdf_alloc_id(&o);

            if (font_reg_id == 0 || font_bold_id == 0 || font_it_id == 0 || font_bi_id == 0)
                rc = -1;
            else if (pdf_write_base14(&o, font_reg_id, "Helvetica") != 0 ||
                     pdf_write_base14(&o, font_bold_id, "Helvetica-Bold") != 0 ||
                     pdf_write_base14(&o, font_it_id, "Helvetica-Oblique") != 0 ||
                     pdf_write_base14(&o, font_bi_id, "Helvetica-BoldOblique") != 0)
                rc = -1;
        }
    }

    if (rc == 0)
    {
        resources_id = pdf_alloc_id(&o);

        if (resources_id == 0)
            rc = -1;
    }

    if (rc == 0)
    {
        pager.pages_root_id = pages_id;
        pager.resources_id = resources_id;
    }

    /* Catalog */
    if (rc == 0)
    {
        if (pdf_begin_obj(&o, catalog_id) != 0)
            rc = -1;
        else if (pdf_printf(&o, "<< /Type /Catalog /Pages %d 0 R >>\n", pages_id) != 0)
            rc = -1;
        else if (pdf_end_obj(&o) != 0)
            rc = -1;
    }

    /* Resources dict: base-14 uses F0..F3, TTF mode uses TT0..TTn */
    if (rc == 0)
    {
        if (pdf_begin_obj(&o, resources_id) != 0)
            rc = -1;
        else if (pdf_puts(&o, "<< /Font << ") != 0)
            rc = -1;
        else
        {
            if (fc.mode == 1)
            {
                for (f = 0; f < fc.n_faces && rc == 0; f++)
                {
                    if (pdf_printf(&o, "/TT%d %d 0 R ", f, fc.face_type0_id[f]) != 0)
                        rc = -1;
                }
            }
            else
            {
                if (pdf_printf(&o, "/%s %d 0 R /%s %d 0 R /%s %d 0 R /%s %d 0 R ", PDF_FN_REGULAR, font_reg_id, PDF_FN_BOLD, font_bold_id, PDF_FN_ITALIC, font_it_id, PDF_FN_BOLDITALIC, font_bi_id) != 0)
                    rc = -1;
            }
        }

        if (rc == 0)
        {
            if (pdf_puts(&o, ">> /ProcSet [/PDF /Text] >>\n") != 0)
                rc = -1;
            else if (pdf_end_obj(&o) != 0)
                rc = -1;
        }
    }

    /* Body: build paragraphs, flush at every LB_PARA */
    if (rc == 0)
    {
        for (row = 0; row < ed->count; row++)
        {
            const EdLine *ln = NULL;
            const EdAttrRun *runs = NULL;
            int n_runs;
            int brk;
            int is_last;

            ln = ed->lines[row];
            n_runs = ed_attr_runs(ln, &runs);

            /* First line of a paragraph? Adopt its alignment */
            if (!para.has_content)
            {
                para.align = ln->para_align;
                para.has_content = 1;
            }

            if (pdf_para_append_edline(&para, ln, runs, n_runs) != 0)
            {
                rc = -1;
                break;
            }

            is_last = (row == ed->count - 1);
            brk = is_last ? (int)LB_PARA : (int)ln->brk;

            if (brk == LB_SPACE)
            {
                /* The wrap consumed a space at the join, put one back so the two visual lines merge into a single paragraph correctly */
                if (pdf_para_push(&para, ' ', 0) != 0)
                {
                    rc = -1;
                    break;
                }
            }
            else if (brk == LB_PARA)
            {
                /* End of paragraph, flush it */
                if (pdf_pager_emit_para(&pager, &para, &o.lossy) != 0)
                {
                    rc = -1;
                    break;
                }

                pdf_para_reset(&para);
            }
        }
    }

    /* Flush any pending paragraph (belt-and-braces: normally the last-line branch above already ran, but if ed->count == 0 we skip that) */
    if (rc == 0 && para.has_content)
    {
        if (pdf_pager_emit_para(&pager, &para, &o.lossy) != 0)
            rc = -1;
    }

    if (rc == 0 && pager.in_page)
    {
        if (pdf_pager_close_page(&pager) != 0)
            rc = -1;
    }

    /* Empty document: emit a blank page so the file is still a valid PDF */
    if (rc == 0 && pager.n_pages == 0)
    {
        if (pdf_pager_open_page(&pager) != 0)
            rc = -1;
        else if (pdf_pager_close_page(&pager) != 0)
            rc = -1;
    }

    /* Pages tree, written after we know all page ids */
    if (rc == 0)
    {
        if (pdf_begin_obj(&o, pages_id) != 0)
            rc = -1;
        else if (pdf_printf(&o, "<< /Type /Pages /Count %d /Kids [", pager.n_pages) != 0)
            rc = -1;
        else
        {
            for (i = 0; i < pager.n_pages; i++)
            {
                if (pdf_printf(&o, "%s%d 0 R", i ? " " : "", pager.page_ids[i]) != 0)
                {
                    rc = -1;
                    break;
                }
            }

            if (rc == 0)
            {
                if (pdf_puts(&o, "] >>\n") != 0)
                    rc = -1;
                else if (pdf_end_obj(&o) != 0)
                    rc = -1;
            }
        }
    }

    /* xref + trailer */
    if (rc == 0)
    {
        if (pdf_write_xref(&o, catalog_id) != 0)
            rc = -1;
    }

    /* Soft advisory when we had to fall back to '?' for missing glyphs */
    if (rc == 0 && o.lossy > 0 && warn && warnsz > 0 && !warn[0])
    {
        if (fc.mode == 1)
            snprintf(warn, warnsz, "%d character(s) missing from configured TTF chain, rendered as .notdef", o.lossy);
        else
            snprintf(warn, warnsz, "%d character(s) outside CP1252 replaced with '?'", o.lossy);
    }

    pdf_para_free(&para);
    pdfb_free(&pager.content);
    free(pager.page_ids);
    free(o.offsets);
    pdf_font_ctx_free(&fc);

    return rc;
}