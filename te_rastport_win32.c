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

#ifdef PLATFORM_WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "te_rastport_win32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_SYNTHESIS_H

/* Debug levels */
static int te_debug_level = 0;

#define TE_DEBUG_LEVEL_NONE 0
#define TE_DEBUG_LEVEL_ERROR 1
#define TE_DEBUG_LEVEL_WARN 2
#define TE_DEBUG_LEVEL_INFO 3
#define TE_DEBUG_LEVEL_VERBOSE 4
#define TE_DEBUG_LEVEL_ALL 5

#define TE_DEBUG_ERROR(fmt, ...)                                    \
    do                                                              \
    {                                                               \
        if (te_debug_level >= TE_DEBUG_LEVEL_ERROR)                 \
        {                                                           \
            fprintf(stderr, "[TE_ERROR] " fmt "\n", ##__VA_ARGS__); \
        }                                                           \
    } while (0)

#define TE_DEBUG_WARN(fmt, ...)                                    \
    do                                                             \
    {                                                              \
        if (te_debug_level >= TE_DEBUG_LEVEL_WARN)                 \
        {                                                          \
            fprintf(stderr, "[TE_WARN] " fmt "\n", ##__VA_ARGS__); \
        }                                                          \
    } while (0)

#define TE_DEBUG_INFO(fmt, ...)                                    \
    do                                                             \
    {                                                              \
        if (te_debug_level >= TE_DEBUG_LEVEL_INFO)                 \
        {                                                          \
            fprintf(stderr, "[TE_INFO] " fmt "\n", ##__VA_ARGS__); \
        }                                                          \
    } while (0)

#define TE_DEBUG_VERBOSE(fmt, ...)                                    \
    do                                                                \
    {                                                                 \
        if (te_debug_level >= TE_DEBUG_LEVEL_VERBOSE)                 \
        {                                                             \
            fprintf(stderr, "[TE_VERBOSE] " fmt "\n", ##__VA_ARGS__); \
        }                                                             \
    } while (0)

void TE_SetDebugLevel(int level)
{
    te_debug_level = level;
}

#define TE_MAX_FONTS 16
#define TE_HASH_BUCKETS 65536
#define TE_PATH_MAX 512
#define TE_DPI 72
#define TE_NOTFOUND_CP 0xFFFFFFFFUL
#define TE_REPLACEMENT_CP 0xFFFDUL

/* Cache pixel formats */
#define FMT_MONO 0
#define FMT_GRAY 1
#define FMT_RGBA 2

struct TEGlyph
{
    ULONG codepoint;
    unsigned char style;
    unsigned char format; /* FMT_MONO / FMT_GRAY / FMT_RGBA */

    int width;
    int height;
    int pitch;
    int bearingX;
    int bearingY;
    int advance;

    unsigned char *data; /* pixel cache */

    struct TEGlyph *next; /* hash chain */
};

struct TEFont
{
    FT_Face face;
    LONG pointSize;
    ULONG flags;
    char path[TE_PATH_MAX];

    /* Bitmap-only fonts return strikes at native height; we scale to pointSize */
    int scaleNum;
    int scaleDen;

    int ascender;  /* scaled, in pixels */
    int descender; /* scaled, positive value */
    int height;    /* scaled, line height in pixels */
};

struct TERenderContext
{
    FT_Library library;
    struct TEFont fonts[TE_MAX_FONTS];
    int numFonts;

    /* Cache */
    struct TEGlyph *buckets[TE_HASH_BUCKETS];

    /* Style and prefs */
    unsigned char style;
    ULONG flags;
    ULONG tabSpaces;

    /* Colours. ARGB packed 0x00RRGGBB */
    ULONG textARGB;
    ULONG bgARGB;

    /* Metrics cache */
    int metrics_valid;
    int metrics_width;
    int metrics_height;
    int metrics_base;
};

static int te_set_face_size(struct TEFont *fnt);
static void te_compute_scale(struct TEFont *fnt);
static void te_compute_metrics(struct TEFont *fnt);
static struct TEGlyph *te_make_glyph(struct TERenderContext *dc, struct TEFont *fnt, FT_UInt gi, ULONG cp);
static struct TEGlyph *te_get_glyph(struct TERenderContext *dc, ULONG cp);
static struct TEGlyph *te_make_notfound(struct TERenderContext *dc);
static void te_glyph_free(struct TEGlyph *g);
static void te_cache_flush(struct TERenderContext *dc);
static int te_decode_utf8(const unsigned char *p, const unsigned char *end, ULONG *cp_out, int *consumed_out);

static void te_recompute_metrics(struct TERenderContext *dc);

static unsigned int te_hash_cp_style(ULONG cp, unsigned char style)
{
    ULONG h = cp ^ ((ULONG)style << 21);
    h ^= (h >> 13);
    return (unsigned int)(h & (TE_HASH_BUCKETS - 1));
}

struct TERenderContext *TE_ContextCreate(void)
{
    struct TERenderContext *dc = NULL;
    FT_Error err;

    dc = (struct TERenderContext *)malloc(sizeof(*dc));

    if (!dc)
        return NULL;

    memset(dc, 0, sizeof(*dc));

    err = FT_Init_FreeType(&dc->library);

    if (err)
    {
        free(dc);
        return NULL;
    }

    dc->tabSpaces = 4;
    dc->textARGB = 0x00FFFFFFUL;
    dc->bgARGB = 0x00000000UL;

    return dc;
}

void TE_ContextRelease(struct TERenderContext *dc)
{
    int i;

    if (!dc)
        return;

    te_cache_flush(dc);

    for (i = 0; i < dc->numFonts; i++)
    {
        if (dc->fonts[i].face)
            FT_Done_Face(dc->fonts[i].face);
    }

    if (dc->library)
        FT_Done_FreeType(dc->library);

    free(dc);
}

static int te_set_face_size(struct TEFont *fnt)
{
    FT_Error err;

    if (!fnt || !fnt->face)
        return 0;

    if (!FT_IS_SCALABLE(fnt->face) && fnt->face->num_fixed_sizes > 0)
    {
        int best = 0;
        int bestDiff = abs(fnt->face->available_sizes[0].height - (int)fnt->pointSize);
        int i;

        for (i = 1; i < fnt->face->num_fixed_sizes; i++)
        {
            int sz = (int)fnt->face->available_sizes[i].height;
            int diff = abs(sz - (int)fnt->pointSize);

            if (diff < bestDiff)
            {
                bestDiff = diff;
                best = i;
            }
        }

        err = FT_Select_Size(fnt->face, best);

        return (err == 0);
    }

    err = FT_Set_Char_Size(fnt->face, 0, (FT_F26Dot6)(fnt->pointSize * 64), TE_DPI, TE_DPI);

    return (err == 0);
}

static void te_compute_scale(struct TEFont *fnt)
{
    if (!fnt || !fnt->face)
        return;

    fnt->scaleNum = 1;
    fnt->scaleDen = 1;

    if (!FT_IS_SCALABLE(fnt->face) && fnt->face->num_fixed_sizes > 0)
    {
        int cellH = (int)(fnt->face->size->metrics.height >> 6);

        if (cellH > 0 && cellH != (int)fnt->pointSize)
        {
            fnt->scaleNum = (int)fnt->pointSize;
            fnt->scaleDen = cellH;
        }
    }
}

static void te_compute_metrics(struct TEFont *fnt)
{
    int asc, dsc;

    if (!fnt || !fnt->face)
    {
        if (fnt)
        {
            fnt->ascender = 0;
            fnt->descender = 0;
            fnt->height = 0;
        }
        return;
    }

    asc = (int)(fnt->face->size->metrics.ascender >> 6);
    dsc = (int)(-(fnt->face->size->metrics.descender) >> 6);

    if (!FT_IS_SCALABLE(fnt->face) && fnt->face->num_fixed_sizes > 0)
    {
        int cellH = (int)(fnt->face->size->metrics.height >> 6);

        if (asc <= 0)
        {
            asc = cellH;
            dsc = 0;
        }
    }

    if (dsc < 0)
        dsc = 0;

    asc = (asc * fnt->scaleNum + fnt->scaleDen / 2) / fnt->scaleDen;
    dsc = (dsc * fnt->scaleNum + fnt->scaleDen / 2) / fnt->scaleDen;

    fnt->ascender = asc;
    fnt->descender = dsc;
    fnt->height = asc + dsc;

    if (fnt->height <= 0)
        fnt->height = (int)fnt->pointSize;
}

LONG TE_FontAdd(struct TERenderContext *dc, const char *fontPath, LONG pointSize, ULONG flags)
{
    struct TEFont *fnt = NULL;
    FT_Error err;

    if (!dc || !fontPath || pointSize <= 0)
        return 0;

    if (dc->numFonts >= TE_MAX_FONTS)
        return 0;

    fnt = &dc->fonts[dc->numFonts];
    memset(fnt, 0, sizeof(*fnt));

    err = FT_New_Face(dc->library, fontPath, 0, &fnt->face);

    if (err || !fnt->face)
    {
        fnt->face = NULL;
        TE_DEBUG_WARN("TE_FontAdd: failed to load '%s' (%d)", fontPath, err);
        return 0;
    }

    strncpy(fnt->path, fontPath, TE_PATH_MAX - 1);

    fnt->path[TE_PATH_MAX - 1] = '\0';
    fnt->pointSize = pointSize;
    fnt->flags = flags;

    if (!te_set_face_size(fnt))
    {
        FT_Done_Face(fnt->face);
        fnt->face = NULL;

        TE_DEBUG_WARN("TE_FontAdd: te_set_face_size failed for '%s'", fontPath);
        return 0;
    }

    te_compute_scale(fnt);
    te_compute_metrics(fnt);

    dc->numFonts++;
    dc->metrics_valid = 0;

    TE_DEBUG_INFO("TE_FontAdd: loaded '%s' size=%ld height=%d", fontPath, (long)pointSize, fnt->height);

    return 1;
}

void TE_FontRemove(struct TERenderContext *dc, const char *fontPath, LONG pointSize)
{
    int i;
    int j;

    if (!dc || !fontPath)
        return;

    for (i = 0; i < dc->numFonts; i++)
    {
        if (dc->fonts[i].pointSize == pointSize && strcmp(dc->fonts[i].path, fontPath) == 0)
        {
            if (dc->fonts[i].face)
                FT_Done_Face(dc->fonts[i].face);

            for (j = i; j < dc->numFonts - 1; j++)
                dc->fonts[j] = dc->fonts[j + 1];

            dc->numFonts--;
            memset(&dc->fonts[dc->numFonts], 0, sizeof(dc->fonts[0]));

            te_cache_flush(dc);
            dc->metrics_valid = 0;
            return;
        }
    }
}

void TE_FontFlush(struct TERenderContext *dc)
{
    int i;

    if (!dc)
        return;

    te_cache_flush(dc);

    for (i = 0; i < dc->numFonts; i++)
    {
        if (dc->fonts[i].face)
            FT_Done_Face(dc->fonts[i].face);
    }

    memset(dc->fonts, 0, sizeof(dc->fonts));

    dc->numFonts = 0;
    dc->metrics_valid = 0;
}

void TE_FontResize(struct TERenderContext *dc, ULONG nPointSize, ULONG fontMask)
{
    int i;

    if (!dc || nPointSize == 0)
        return;

    for (i = 0; i < dc->numFonts; i++)
    {
        struct TEFont *fnt = &dc->fonts[i];

        if (!fnt->face)
            continue;

        if (fontMask && !(fontMask & (1UL << i)))
            continue;

        fnt->pointSize = (LONG)nPointSize;

        if (te_set_face_size(fnt))
        {
            te_compute_scale(fnt);
            te_compute_metrics(fnt);
        }
    }

    te_cache_flush(dc);
    dc->metrics_valid = 0;
}

void TE_SetFlags(struct TERenderContext *dc, ULONG flags)
{
    if (!dc)
        return;

    if (dc->flags != flags)
    {
        if ((dc->flags ^ flags) & TE_FLAG_ANTIALIAS)
            te_cache_flush(dc);

        dc->flags = flags;
    }
}

ULONG TE_GetFlags(const struct TERenderContext *dc)
{
    return dc ? dc->flags : 0;
}

void TE_CacheFlush(struct TERenderContext *dc)
{
    if (dc)
        te_cache_flush(dc);
}

void TE_SetStyle(struct TERenderContext *dc, ULONG styleBits)
{
    if (!dc)
        return;

    dc->style = (unsigned char)(styleBits & 0xFF);
}

void TE_SetTabWidth(struct TERenderContext *dc, ULONG nSpaces)
{
    if (!dc)
        return;

    if (nSpaces < 1)
        nSpaces = 1;

    if (nSpaces > 16)
        nSpaces = 16;

    dc->tabSpaces = nSpaces;
}

void TE_SetColorRGB(struct TERenderContext *dc, ULONG textRGB, ULONG bgRGB)
{
    if (!dc)
        return;

    dc->textARGB = textRGB;
    dc->bgARGB = bgRGB;
}

static void te_glyph_free(struct TEGlyph *g)
{
    if (!g)
        return;

    if (g->data)
        free(g->data);

    free(g);
}

static void te_cache_flush(struct TERenderContext *dc)
{
    int i;

    if (!dc)
        return;

    for (i = 0; i < TE_HASH_BUCKETS; i++)
    {
        struct TEGlyph *g = dc->buckets[i];

        while (g)
        {
            struct TEGlyph *next = g->next;

            te_glyph_free(g);
            g = next;
        }

        dc->buckets[i] = NULL;
    }
}

/* Nearest-neighbour scale blit from FreeType bitmap to cache buffer */
static void te_blit_to_cache(FT_Bitmap *bm, unsigned char *dst, int dstW, int dstH, int dstPitch, int format)
{
    int srcW = (int)bm->width;
    int srcH = (int)bm->rows;
    unsigned long dx, dy;
    int x, y;

    if (dstW <= 0 || dstH <= 0 || srcW <= 0 || srcH <= 0)
        return;

    dx = ((unsigned long)srcW << 16) / (unsigned long)dstW;
    dy = ((unsigned long)srcH << 16) / (unsigned long)dstH;

    switch (format)
    {
    case FMT_MONO:
    {
        unsigned long accumY = 0;
        for (y = 0; y < dstH; y++)
        {
            int sy = (int)(accumY >> 16);
            unsigned char *drow = dst + (unsigned long)y * (unsigned long)dstPitch;
            unsigned long accumX = 0;

            if (bm->pixel_mode == FT_PIXEL_MODE_MONO)
            {
                const unsigned char *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);
                    int byte = sx >> 3;
                    int bit = 7 - (sx & 7);

                    if (srow[byte] & (1 << bit))
                        drow[x >> 3] |= (unsigned char)(0x80 >> (x & 7));

                    accumX += dx;
                }
            }
            else if (bm->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                const unsigned char *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);

                    if (srow[sx] >= 128)
                        drow[x >> 3] |= (unsigned char)(0x80 >> (x & 7));

                    accumX += dx;
                }
            }
            else if (bm->pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                const unsigned char *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);
                    const unsigned char *sp = srow + sx * 4;

                    if (sp[3] >= 128)
                        drow[x >> 3] |= (unsigned char)(0x80 >> (x & 7));

                    accumX += dx;
                }
            }

            accumY += dy;
        }

        break;
    }

    case FMT_GRAY:
    {
        unsigned long accumY = 0;

        for (y = 0; y < dstH; y++)
        {
            int sy = (int)(accumY >> 16);
            unsigned char *drow = dst + (unsigned long)y * (unsigned long)dstPitch;
            unsigned long accumX = 0;

            if (bm->pixel_mode == FT_PIXEL_MODE_MONO)
            {
                const unsigned char *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);
                    int byte = sx >> 3;
                    int bit = 7 - (sx & 7);

                    drow[x] = (srow[byte] & (1 << bit)) ? 0xFF : 0;
                    accumX += dx;
                }
            }
            else if (bm->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                const unsigned char *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);

                    drow[x] = srow[sx];
                    accumX += dx;
                }
            }
            else if (bm->pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                const unsigned char *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);
                    const unsigned char *sp = srow + sx * 4;

                    drow[x] = sp[3];
                    accumX += dx;
                }
            }

            accumY += dy;
        }
        break;
    }

    case FMT_RGBA:
    {
        unsigned long accumY = 0;

        for (y = 0; y < dstH; y++)
        {
            int sy = (int)(accumY >> 16);
            unsigned char *drow = dst + (unsigned long)y * (unsigned long)dstPitch;
            unsigned long accumX = 0;

            if (bm->pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                const unsigned char *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);
                    const unsigned char *sp = srow + sx * 4;
                    unsigned char *dp = drow + x * 4;

                    dp[0] = sp[2]; /* R */
                    dp[1] = sp[1]; /* G */
                    dp[2] = sp[0]; /* B */
                    dp[3] = sp[3]; /* A */

                    accumX += dx;
                }
            }
            else if (bm->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                const unsigned char *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);
                    unsigned char *dp = drow + x * 4;

                    dp[0] = 0xFF;
                    dp[1] = 0xFF;
                    dp[2] = 0xFF;
                    dp[3] = srow[sx];
                    accumX += dx;
                }
            }

            accumY += dy;
        }

        break;
    }

    default:
        break;
    }
}

static struct TEGlyph *te_make_glyph(struct TERenderContext *dc, struct TEFont *fnt, FT_UInt gi, ULONG cp)
{
    struct TEGlyph *g = NULL;
    FT_GlyphSlot slot = NULL;
    FT_Bitmap *bm = NULL;
    int format;
    int load_flags;
    int srcW, srcH, dstW, dstH, pitch;
    FT_Render_Mode rmode;
    double cell_scale = 1.0;

    if (!dc || !fnt || !fnt->face)
        return NULL;

    te_set_face_size(fnt);

    load_flags = FT_LOAD_RENDER | FT_LOAD_COLOR;

    if (!(dc->flags & TE_FLAG_ANTIALIAS) && FT_IS_SCALABLE(fnt->face) && !FT_HAS_COLOR(fnt->face))
        load_flags |= FT_LOAD_TARGET_MONO;

    if (FT_HAS_COLOR(fnt->face))
        load_flags &= ~FT_LOAD_TARGET_MONO;

    if (dc->style && FT_IS_SCALABLE(fnt->face))
    {
        int norender = FT_LOAD_COLOR;

        if (!(dc->flags & TE_FLAG_ANTIALIAS) && !FT_HAS_COLOR(fnt->face))
            norender |= FT_LOAD_TARGET_MONO;

        if (FT_Load_Glyph(fnt->face, gi, norender))
            return NULL;

        slot = fnt->face->glyph;

        if (dc->style & TE_STY_BOLD)
            FT_GlyphSlot_Embolden(slot);

        if (dc->style & TE_STY_ITALIC)
            FT_GlyphSlot_Oblique(slot);

        rmode = (norender & FT_LOAD_TARGET_MONO) ? FT_RENDER_MODE_MONO : FT_RENDER_MODE_NORMAL;

        if (FT_Render_Glyph(slot, rmode))
            return NULL;
    }
    else
    {
        if (FT_Load_Glyph(fnt->face, gi, load_flags))
            return NULL;

        slot = fnt->face->glyph;
    }

    bm = &slot->bitmap;

    if (bm->pixel_mode == FT_PIXEL_MODE_BGRA)
        format = FMT_RGBA;
    else if (dc->flags & TE_FLAG_ANTIALIAS)
        format = FMT_GRAY;
    else
        format = FMT_MONO;

    srcW = (int)bm->width;
    srcH = (int)bm->rows;

    if (srcW == 0 || srcH == 0)
    {
        dstW = 0;
        dstH = 0;
    }
    else
    {
        dstW = (srcW * fnt->scaleNum + fnt->scaleDen / 2) / fnt->scaleDen;
        dstH = (srcH * fnt->scaleNum + fnt->scaleDen / 2) / fnt->scaleDen;

        if (dstW < 1)
            dstW = 1;

        if (dstH < 1)
            dstH = 1;

        /* Scale glyphs to fit the monospace cell: up to 2 cells wide, 1 cell high, and aligned to the primary font baseline */
        if (dc->metrics_valid && dc->metrics_width > 0 && dc->metrics_height > 0)
        {
            int maxW = 2 * dc->metrics_width;
            int maxH = dc->metrics_height;
            double baseY = (double)dc->metrics_base;
            double unscaled_bearingY = (double)slot->bitmap_top * (double)fnt->scaleNum / (double)fnt->scaleDen;
            double descender = (double)dstH - unscaled_bearingY;
            double maxDescender = (double)maxH - baseY;
            int needs_scale = 0;

            if (dstW > maxW)
            {
                cell_scale = (double)maxW / (double)dstW;
                needs_scale = 1;
            }

            if (dstH > maxH)
            {
                double scale_y = (double)maxH / (double)dstH;

                if (scale_y < cell_scale)
                {
                    cell_scale = scale_y;
                    needs_scale = 1;
                }
            }

            if (unscaled_bearingY > baseY)
            {
                double scale_b = baseY / unscaled_bearingY;

                if (scale_b < cell_scale)
                {
                    cell_scale = scale_b;
                    needs_scale = 1;
                }
            }

            if (descender > 0.0 && descender > maxDescender)
            {
                double scale_d = maxDescender / descender;

                if (scale_d < cell_scale)
                {
                    cell_scale = scale_d;
                    needs_scale = 1;
                }
            }

            if (needs_scale)
            {
                dstW = (int)((double)dstW * cell_scale + 0.5);
                dstH = (int)((double)dstH * cell_scale + 0.5);

                if (dstW < 1)
                    dstW = 1;

                if (dstH < 1)
                    dstH = 1;
            }
        }
    }

    switch (format)
    {
    case FMT_MONO:
        pitch = ((dstW + 15) >> 4) * 2;
        break;
    case FMT_GRAY:
        pitch = dstW;
        break;
    case FMT_RGBA:
        pitch = dstW * 4;
        break;
    default:
        pitch = dstW;
        break;
    }

    g = (struct TEGlyph *)malloc(sizeof(*g));

    if (!g)
        return NULL;

    memset(g, 0, sizeof(*g));

    if (dstW > 0 && dstH > 0)
    {
        g->data = (unsigned char *)malloc((unsigned long)pitch * (unsigned long)dstH);

        if (!g->data)
        {
            free(g);
            return NULL;
        }

        memset(g->data, 0, (unsigned long)pitch * (unsigned long)dstH);
        te_blit_to_cache(bm, g->data, dstW, dstH, pitch, format);
    }

    g->codepoint = cp;
    g->style = dc->style;
    g->format = (unsigned char)format;
    g->width = dstW;
    g->height = dstH;
    g->pitch = pitch;
    g->bearingX = (int)((double)slot->bitmap_left * (double)fnt->scaleNum * cell_scale / (double)fnt->scaleDen + 0.5);
    g->bearingY = (int)((double)slot->bitmap_top * (double)fnt->scaleNum * cell_scale / (double)fnt->scaleDen + 0.5);
    g->advance = (int)((double)(slot->advance.x >> 6) * (double)fnt->scaleNum * cell_scale / (double)fnt->scaleDen + 0.5);

    /* Make sure the rounded bearing does not push the glyph above the cell */
    if (dc->metrics_valid && g->bearingY > dc->metrics_base)
        g->bearingY = dc->metrics_base;

    if (g->advance <= 0)
        g->advance = dstW;

    return g;
}

static struct TEGlyph *te_make_notfound(struct TERenderContext *dc)
{
    struct TEFont *primary = NULL;
    struct TEGlyph *g = NULL;
    int w, h, pitch;
    int x, y;

    if (!dc || dc->numFonts <= 0)
        return NULL;

    primary = &dc->fonts[0];

    if (!primary->face)
        return NULL;

    h = primary->height;

    if (h <= 0)
        h = (int)primary->pointSize;

    w = (h * 3) / 5;

    if (w < 4)
        w = 4;

    if (h < 6)
        h = 6;

    pitch = ((w + 15) >> 4) * 2;

    g = (struct TEGlyph *)malloc(sizeof(*g));

    if (!g)
        return NULL;

    memset(g, 0, sizeof(*g));

    g->data = (unsigned char *)malloc((unsigned long)pitch * (unsigned long)h);

    if (!g->data)
    {
        free(g);
        return NULL;
    }

    memset(g->data, 0, (unsigned long)pitch * (unsigned long)h);

    for (x = 0; x < w; x++)
    {
        g->data[0 * pitch + (x >> 3)] |= (unsigned char)(0x80 >> (x & 7));
        g->data[(h - 1) * pitch + (x >> 3)] |= (unsigned char)(0x80 >> (x & 7));
    }

    for (y = 0; y < h; y++)
    {
        g->data[y * pitch + (0 >> 3)] |= (unsigned char)(0x80 >> (0 & 7));
        g->data[y * pitch + ((w - 1) >> 3)] |= (unsigned char)(0x80 >> ((w - 1) & 7));
    }

    g->codepoint = TE_NOTFOUND_CP;
    g->style = 0;
    g->format = FMT_MONO;
    g->width = w;
    g->height = h;
    g->pitch = pitch;
    g->bearingX = 0;
    g->bearingY = (primary->ascender > 0 ? primary->ascender : h);
    g->advance = w + 1;

    return g;
}

static struct TEGlyph *te_get_glyph(struct TERenderContext *dc, ULONG cp)
{
    struct TEGlyph *g = NULL;
    struct TEGlyph *first_g = NULL;
    struct TEFont *fnt = NULL;
    struct TEFont *first_fnt = NULL;
    FT_UInt gi = 0;
    FT_UInt first_gi = 0;
    unsigned int h;
    int i;
    int want_color = 0;

    if (!dc)
        return NULL;

    h = te_hash_cp_style(cp, dc->style);

    for (g = dc->buckets[h]; g; g = g->next)
    {
        if (g->codepoint == cp && g->style == dc->style)
            return g;
    }

    first_g = NULL;
    first_fnt = NULL;
    fnt = NULL;
    g = NULL;

    for (i = 0; i < dc->numFonts; i++)
    {
        if (!dc->fonts[i].face)
            continue;

        gi = FT_Get_Char_Index(dc->fonts[i].face, (FT_ULong)cp);

        if (gi != 0)
        {
            fnt = &dc->fonts[i];
            break;
        }
    }

    if (!fnt)
        return NULL;

    first_fnt = fnt;
    first_gi = gi;
    first_g = te_make_glyph(dc, first_fnt, first_gi, cp);

    if (!first_g)
        return NULL;

    /* Prefer an RGBA colour font for supplementary-plane emoji */
    if (cp > 0xFFFF && first_g->format != FMT_RGBA)
        want_color = 1;

    if (want_color)
    {
        for (++i; i < dc->numFonts; i++)
        {
            if (!dc->fonts[i].face)
                continue;

            gi = FT_Get_Char_Index(dc->fonts[i].face, (FT_ULong)cp);

            if (gi == 0)
                continue;

            g = te_make_glyph(dc, &dc->fonts[i], gi, cp);

            if (g && g->format == FMT_RGBA)
            {
                te_glyph_free(first_g);

                first_g = NULL;
                fnt = &dc->fonts[i];
                break;
            }

            te_glyph_free(g);
            g = NULL;
        }
    }

    if (!g)
    {
        g = first_g;
        fnt = first_fnt;
        gi = first_gi;
    }

    if (cp > 0xFFFF)
        fprintf(stderr, "[te_get_glyph] cp=U+%04lX font=%s gi=%u w=%d h=%d format=%d\n", (unsigned long)cp, fnt->path[0] ? fnt->path : "?", (unsigned int)gi, g->width, g->height, g->format);

    g->next = dc->buckets[h];
    dc->buckets[h] = g;

    return g;
}

static struct TEGlyph *te_get_notfound(struct TERenderContext *dc)
{
    struct TEGlyph *g = NULL;
    unsigned int h;

    h = te_hash_cp_style(TE_NOTFOUND_CP, 0);

    for (g = dc->buckets[h]; g; g = g->next)
    {
        if (g->codepoint == TE_NOTFOUND_CP && g->style == 0)
            return g;
    }

    g = te_make_notfound(dc);

    if (!g)
        return NULL;

    g->next = dc->buckets[h];
    dc->buckets[h] = g;

    return g;
}

/* Blend src over dst with alpha (0..255) */
static unsigned char te_blend8(unsigned char s, unsigned char d, unsigned char a)
{
    return (unsigned char)(((unsigned long)s * ((unsigned long)a + 1UL) + (unsigned long)d * (unsigned long)(255 - a)) >> 8);
}

/* Draw one glyph to the GDI HDC by compositing onto the background colour */
static void te_win_draw_glyph(struct TERenderContext *dc, HDC hdc, struct TEGlyph *g, int dx, int dy)
{
    unsigned char *buf = NULL;
    int gw, gh;
    int x, y;
    unsigned char fr, fg, fb;
    unsigned char br, bg, bb;
    HBITMAP hbm = NULL;
    HDC memDC = NULL;
    HBITMAP oldBmp = NULL;
    BITMAPINFO bmi;
    void *bits = NULL;

    if (!g || g->width <= 0 || g->height <= 0)
        return;

    gw = g->width;
    gh = g->height;

    fr = (unsigned char)((dc->textARGB >> 16) & 0xFF);
    fg = (unsigned char)((dc->textARGB >> 8) & 0xFF);
    fb = (unsigned char)(dc->textARGB & 0xFF);
    br = (unsigned char)((dc->bgARGB >> 16) & 0xFF);
    bg = (unsigned char)((dc->bgARGB >> 8) & 0xFF);
    bb = (unsigned char)(dc->bgARGB & 0xFF);

    /* Build a 32-bit BGRA DIB (Windows native order) */
    memset(&bmi, 0, sizeof(bmi));

    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = gw;
    bmi.bmiHeader.biHeight = -gh; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);

    if (hbm && bits)
    {
        buf = (unsigned char *)bits;

        if (g->format == FMT_MONO)
        {
            for (y = 0; y < gh; y++)
            {
                const unsigned char *srow = g->data + (unsigned long)y * (unsigned long)g->pitch;
                unsigned char *drow = buf + (unsigned long)y * gw * 4;

                for (x = 0; x < gw; x++)
                {
                    int byte = x >> 3;
                    int bit = 7 - (x & 7);
                    unsigned char on = (unsigned char)((srow[byte] >> bit) & 1);

                    drow[x * 4 + 0] = on ? fb : bb;
                    drow[x * 4 + 1] = on ? fg : bg;
                    drow[x * 4 + 2] = on ? fr : br;
                    drow[x * 4 + 3] = 0xFF;
                }
            }
        }
        else if (g->format == FMT_GRAY)
        {
            for (y = 0; y < gh; y++)
            {
                const unsigned char *srow = g->data + (unsigned long)y * (unsigned long)g->pitch;
                unsigned char *drow = buf + (unsigned long)y * gw * 4;

                for (x = 0; x < gw; x++)
                {
                    unsigned char a = srow[x];

                    drow[x * 4 + 0] = te_blend8(fb, bb, a);
                    drow[x * 4 + 1] = te_blend8(fg, bg, a);
                    drow[x * 4 + 2] = te_blend8(fr, br, a);
                    drow[x * 4 + 3] = 0xFF;
                }
            }
        }
        else /* FMT_RGBA */
        {
            for (y = 0; y < gh; y++)
            {
                const unsigned char *srow = g->data + (unsigned long)y * (unsigned long)g->pitch;
                unsigned char *drow = buf + (unsigned long)y * gw * 4;

                for (x = 0; x < gw; x++)
                {
                    const unsigned char *sp = srow + x * 4;
                    unsigned char a = sp[3];

                    drow[x * 4 + 0] = te_blend8(sp[2], bb, a);
                    drow[x * 4 + 1] = te_blend8(sp[1], bg, a);
                    drow[x * 4 + 2] = te_blend8(sp[0], br, a);
                    drow[x * 4 + 3] = 0xFF;
                }
            }
        }

        memDC = CreateCompatibleDC(hdc);

        if (memDC)
        {
            oldBmp = (HBITMAP)SelectObject(memDC, hbm);

            {
                BLENDFUNCTION blend;
                blend.BlendOp = AC_SRC_OVER;
                blend.BlendFlags = 0;
                blend.SourceConstantAlpha = 255;
                blend.AlphaFormat = 0;

                if (g->format == FMT_RGBA)
                    BitBlt(hdc, dx, dy, gw, gh, memDC, 0, 0, SRCCOPY);
                else
                    AlphaBlend(hdc, dx, dy, gw, gh, memDC, 0, 0, gw, gh, blend);
            }

            SelectObject(memDC, oldBmp);
        }
    }

    if (memDC)
        DeleteDC(memDC);

    if (hbm)
        DeleteObject(hbm);
}

void TE_RenderText(struct TERenderContext *dc, HDC hdc, int x, int y, const char *utf8, ULONG maxChars)
{
    const unsigned char *p = NULL;
    const unsigned char *end = NULL;
    ULONG count = 0;
    int penX, penY;

    if (!hdc || !dc || !utf8 || dc->numFonts <= 0)
        return;

    p = (const unsigned char *)utf8;
    end = p + strlen((const char *)utf8);

    penX = x;
    penY = y;

    while (p < end)
    {
        ULONG cp;
        int consumed = 0;
        struct TEGlyph *g = NULL;

        if (maxChars != (ULONG)-1 && count >= maxChars)
            break;

        if (!te_decode_utf8(p, end, &cp, &consumed))
            break;

        p += consumed;
        count++;

        if (cp == 0)
            break;

        if (cp == 0x0A || cp == 0x0D)
            continue;

        if (cp == 0x09)
        {
            /* Tab advances by cell width, not font height */
            int tabW;

            if (!dc->metrics_valid)
                te_recompute_metrics(dc);

            tabW = dc->metrics_width * (int)dc->tabSpaces;
            penX += tabW;

            continue;
        }

        g = te_get_glyph(dc, cp);

        if (!g)
        {
            g = te_get_glyph(dc, TE_REPLACEMENT_CP);

            if (!g)
                g = te_get_notfound(dc);
        }

        if (!g)
            continue;

        te_win_draw_glyph(dc, hdc, g, penX + g->bearingX, penY - g->bearingY);

        if (dc->flags & TE_FLAG_FIXEDWIDTH)
        {
            /* Use primary font's M advance as monospace cell width */
            int cellW = dc->metrics_width > 0 ? dc->metrics_width : g->advance;

            penX += cellW;
        }
        else
        {
            penX += g->advance > 0 ? g->advance : g->width;
        }
    }
}

static void te_recompute_metrics(struct TERenderContext *dc)
{
    struct TEFont *primary;
    FT_UInt gi;
    int advance = 0;
    FT_GlyphSlot slot;

    dc->metrics_width = 8;
    dc->metrics_height = 16;
    dc->metrics_base = 12;

    if (!dc || dc->numFonts <= 0)
        return;

    primary = &dc->fonts[0];

    if (!primary->face)
        return;

    te_set_face_size(primary);

    gi = FT_Get_Char_Index(primary->face, 'M');

    if (gi && FT_Load_Glyph(primary->face, gi, FT_LOAD_DEFAULT) == 0)
    {
        slot = primary->face->glyph;
        advance = (int)(slot->metrics.horiAdvance >> 6);

        if (advance <= 0)
            advance = (int)(primary->face->size->metrics.max_advance >> 6);
    }

    if (advance <= 0)
        advance = (int)primary->pointSize / 2;

    advance = (advance * primary->scaleNum + primary->scaleDen / 2) / primary->scaleDen;

    dc->metrics_width = advance;
    dc->metrics_height = primary->height;
    dc->metrics_base = primary->ascender;
    dc->metrics_valid = 1;
}

void TE_GetMetrics(struct TERenderContext *dc, struct TEGlyphMetrics *out)
{
    if (!dc || !out)
        return;

    if (!dc->metrics_valid)
        te_recompute_metrics(dc);

    out->width = (short)dc->metrics_width;
    out->height = (short)dc->metrics_height;
    out->baseX = 0;
    out->baseY = (short)dc->metrics_base;
}

void TE_MeasureText(struct TERenderContext *dc, const char *utf8, LONG maxChars, struct TEGlyphMetrics *out)
{
    const unsigned char *p = NULL;
    const unsigned char *end = NULL;
    LONG count = 0;
    int width = 0;
    int cellW;

    if (!dc || !utf8 || !out)
        return;

    if (!dc->metrics_valid)
        te_recompute_metrics(dc);

    cellW = dc->metrics_width > 0 ? dc->metrics_width : 8;

    p = (const unsigned char *)utf8;
    end = p + strlen((const char *)utf8);

    while (p < end)
    {
        ULONG cp;
        int consumed = 0;
        struct TEGlyph *g = NULL;

        if (maxChars >= 0 && count >= maxChars)
            break;

        if (!te_decode_utf8(p, end, &cp, &consumed))
            break;

        p += consumed;
        count++;

        if (cp == 0 || cp == 0x0A || cp == 0x0D)
            continue;

        g = te_get_glyph(dc, cp);

        if (!g)
            g = te_get_notfound(dc);

        if (g)
        {
            if (dc->flags & TE_FLAG_FIXEDWIDTH)
            {
                /* Fixed-width: use the glyph's own width if it exceeds one cell, otherwise fall back to the standard cell width */
                int glyphW = g->width > 0 ? g->width : 0;
                width += (glyphW > cellW) ? glyphW : cellW;
            }
            else
            {
                width += g->advance > 0 ? g->advance : g->width;
            }
        }
    }

    out->width = (short)width;
    out->height = (short)(dc->numFonts > 0 ? dc->fonts[0].height : 16);
    out->baseX = 0;
    out->baseY = (short)(dc->numFonts > 0 ? dc->fonts[0].ascender : 12);
}

void TE_GetCharOffsets(struct TERenderContext *dc, const char *utf8, LONG maxChars, LONG *arrayout)
{
    const unsigned char *p = NULL;
    const unsigned char *end = NULL;
    LONG count = 0;
    int x = 0;
    int cellW;

    if (!dc || !utf8 || !arrayout)
        return;

    if (!dc->metrics_valid)
        te_recompute_metrics(dc);

    cellW = dc->metrics_width > 0 ? dc->metrics_width : 8;

    p = (const unsigned char *)utf8;
    end = p + strlen((const char *)utf8);

    while (p < end)
    {
        ULONG cp;
        int consumed = 0;
        struct TEGlyph *g = NULL;

        if (maxChars >= 0 && count >= maxChars)
            break;

        arrayout[count] = x;

        if (!te_decode_utf8(p, end, &cp, &consumed))
            break;

        p += consumed;
        count++;

        if (cp == 0 || cp == 0x0A || cp == 0x0D)
            continue;

        g = te_get_glyph(dc, cp);

        if (!g)
            g = te_get_notfound(dc);

        if (g)
        {
            if (dc->flags & TE_FLAG_FIXEDWIDTH)
                x += cellW;
            else
                x += g->advance > 0 ? g->advance : g->width;
        }
    }
}

static int te_decode_utf8(const unsigned char *p, const unsigned char *end, ULONG *cp_out, int *consumed_out)
{
    ULONG cp = 0;
    int n = 0;
    unsigned char b;

    if (!p || p >= end)
        return 0;

    b = *p;

    if (b < 0x80)
    {
        cp = b;
        n = 1;
    }
    else if ((b & 0xE0) == 0xC0)
    {
        if (p + 2 > end)
            return 0;

        cp = ((ULONG)b & 0x1F) << 6;
        cp |= (ULONG)p[1] & 0x3F;
        n = 2;
    }
    else if ((b & 0xF0) == 0xE0)
    {
        if (p + 3 > end)
            return 0;

        cp = ((ULONG)b & 0x0F) << 12;
        cp |= ((ULONG)p[1] & 0x3F) << 6;
        cp |= (ULONG)p[2] & 0x3F;
        n = 3;
    }
    else if ((b & 0xF8) == 0xF0)
    {
        if (p + 4 > end)
            return 0;

        cp = ((ULONG)b & 0x07) << 18;
        cp |= ((ULONG)p[1] & 0x3F) << 12;
        cp |= ((ULONG)p[2] & 0x3F) << 6;
        cp |= (ULONG)p[3] & 0x3F;
        n = 4;
    }
    else
    {
        return 0;
    }

    if (cp_out)
        *cp_out = cp;

    if (consumed_out)
        *consumed_out = n;

    return 1;
}

#endif /* PLATFORM_WIN32 */
