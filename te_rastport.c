/*
 * te_rastport.c -- Glyph rendering with FreeType for tinyedit
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

#include "te_rastport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/memory.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <graphics/view.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_SYNTHESIS_H

#include <stdarg.h>

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

#define TE_DEBUG_ALL(fmt, ...)                                    \
    do                                                            \
    {                                                             \
        if (te_debug_level >= TE_DEBUG_LEVEL_ALL)                 \
        {                                                         \
            fprintf(stderr, "[TE_ALL] " fmt "\n", ##__VA_ARGS__); \
        }                                                         \
    } while (0)

void TE_SetDebugLevel(int level)
{
    te_debug_level = level;
}

#define TE_MAX_FONTS 16
#define TE_HASH_BUCKETS 256
#define TE_PATH_MAX 256
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
    UBYTE style;
    UBYTE format;  /* FMT_MONO / FMT_GRAY / FMT_RGBA */
    UBYTE in_chip; /* 1 if data was moved to CHIP for AGA blits */
    UBYTE pad;

    WORD width;
    WORD height;
    WORD pitch;
    WORD bearingX;
    WORD bearingY;
    WORD advance;

    UBYTE *data; /* AllocVec MEMF_CLEAR, in_chip determines region */

    /* Lazy AGA mirrors -- allocated when indexed-colour blit needs them */
    struct BitMap *clutBM;
    struct BitMap *maskBM;

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
    UBYTE style;
    ULONG flags;
    ULONG tabSpaces;

    /* Colours. ARGB packed 0x00RRGGBB. Pens are -1 when never set; once set they take precedence */
    ULONG textARGB;
    ULONG bgARGB;
    LONG txtPen;
    LONG bgPen;
    UBYTE pensValid;

    /* Screen association */
    struct Screen *screen;
    UBYTE screenIsRTG;
    UBYTE screenPixFmt; /* CGX PIXFMT_* if isRTG */
    ULONG screenDepth;

    /* CLUT remap table (RGB444 -> pen) */
    UBYTE clutRemap[4096];
    UBYTE clutValid;

    /* 16-entry AA ramp: pen for each step of fg->bg */
    UBYTE aaRamp[16];
    UBYTE aaRampValid;

    /* Scratch buffer for WritePixelArray() on RTG screens */
    UBYTE *scratch;
    ULONG scratchBytes;
};

static int te_set_face_size(struct TEFont *fnt);
static void te_compute_scale(struct TEFont *fnt);
static void te_compute_metrics(struct TEFont *fnt);
static struct TEGlyph *te_make_glyph(struct TERenderContext *dc, struct TEFont *fnt, FT_UInt gi, ULONG cp);
static struct TEGlyph *te_get_glyph(struct TERenderContext *dc, ULONG cp);
static struct TEGlyph *te_make_notfound(struct TERenderContext *dc);
static void te_glyph_free(struct TEGlyph *g);
static void te_cache_flush(struct TERenderContext *dc);
static void te_flush_clut_mirrors(struct TERenderContext *dc);
static int te_screen_is_rtg(struct Screen *screen, UBYTE *pixfmt_out, ULONG *depth_out);
static void te_rebuild_clut_remap(struct TERenderContext *dc, struct Screen *screen);
static void te_rebuild_aa_ramp(struct TERenderContext *dc);
static int te_pen_for_rgb(struct TERenderContext *dc, UBYTE r, UBYTE g, UBYTE b);
static void te_draw_glyph(struct TERenderContext *dc, struct RastPort *rp, struct TEGlyph *g, int penX, int baseY);
static int te_decode_utf8(const UBYTE *p, const UBYTE *end, ULONG *cp_out, int *consumed_out);

/* CyberGraphX is opened lazily on first need and closed at TE_GlobalCleanup() */
struct Library *CyberGfxBase = NULL;
static int s_cgx_owned = 0;

static int te_open_cgx(void)
{
    if (CyberGfxBase)
        return 1;

    CyberGfxBase = OpenLibrary("cybergraphics.library", 41);

    if (CyberGfxBase)
    {
        s_cgx_owned = 1;
        return 1;
    }

    return 0;
}

void TE_GlobalCleanup(void)
{
    if (s_cgx_owned && CyberGfxBase)
    {
        CloseLibrary(CyberGfxBase);
        CyberGfxBase = NULL;
        s_cgx_owned = 0;
    }
}

static unsigned int te_hash_cp_style(ULONG cp, UBYTE style)
{
    /* Fold style into upper bits so styled variants land in different buckets */
    ULONG h = cp ^ ((ULONG)style << 21);

    h ^= (h >> 13);

    return (unsigned int)(h & (TE_HASH_BUCKETS - 1));
}

struct TERenderContext *TE_ContextCreate(void)
{
    struct TERenderContext *dc;
    FT_Error err;

    dc = (struct TERenderContext *)AllocVec(sizeof(*dc), MEMF_PUBLIC | MEMF_CLEAR);

    if (!dc)
        return NULL;

    err = FT_Init_FreeType(&dc->library);

    if (err)
    {
        FreeVec(dc);
        return NULL;
    }

    dc->tabSpaces = 4;
    dc->textARGB = 0x00FFFFFFUL; /* white */
    dc->bgARGB = 0x00000000UL;   /* black */
    dc->txtPen = -1;
    dc->bgPen = -1;
    dc->screenIsRTG = 0; /* initialized to 0, will be set by TE_SetScreen */

    TE_DEBUG_INFO("TE_ContextCreate - screenIsRTG initialized to %d", dc->screenIsRTG);

    te_open_cgx();

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

    if (dc->scratch)
        FreeVec(dc->scratch);

    FreeVec(dc);
}

static int te_set_face_size(struct TEFont *fnt)
{
    FT_Error err;

    if (!fnt || !fnt->face)
        return 0;

    /* Bitmap-only font: pick the closest available strike */
    if (!FT_IS_SCALABLE(fnt->face) && fnt->face->num_fixed_sizes > 0)
    {
        int best = 0;
        int bestDiff = abs(fnt->face->available_sizes[0].height - (int)fnt->pointSize);
        int i;

        for (i = 1; i < fnt->face->num_fixed_sizes; i++)
        {
            int sz = fnt->face->available_sizes[i].height;
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

    /* Outline font: set the requested size in 26.6 fixed point */
    err = FT_Set_Char_Size(fnt->face, 0, (FT_F26Dot6)(fnt->pointSize * 64), TE_DPI, TE_DPI);

    return (err == 0);
}

static void te_compute_scale(struct TEFont *fnt)
{
    fnt->scaleNum = 1;
    fnt->scaleDen = 1;

    if (!fnt || !fnt->face)
        return;

    /* the metric we have to scale FROM to reach pointSize */
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

    /* Bitmap-only fonts sometimes report ascender==0; fall back to cell height in that case */
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

    /* Apply per-font scale to bring everything to requested point size */
    asc = (asc * fnt->scaleNum + fnt->scaleDen / 2) / fnt->scaleDen;
    dsc = (dsc * fnt->scaleNum + fnt->scaleDen / 2) / fnt->scaleDen;

    fnt->ascender = asc;
    fnt->descender = dsc;
    fnt->height = asc + dsc;

    if (fnt->height <= 0)
        fnt->height = (int)fnt->pointSize;
}

LONG TE_FontAdd(struct TERenderContext *dc, CONST_STRPTR fontPath, LONG pointSize, ULONG flags)
{
    struct TEFont *fnt;
    FT_Error err;

    if (!dc || !fontPath || pointSize <= 0)
        return 0;

    if (dc->numFonts >= TE_MAX_FONTS)
        return 0;

    fnt = &dc->fonts[dc->numFonts];

    memset(fnt, 0, sizeof(*fnt));

    err = FT_New_Face(dc->library, (const char *)fontPath, 0, &fnt->face);

    if (err || !fnt->face)
    {
        fnt->face = NULL;
        return 0;
    }

    strncpy(fnt->path, (const char *)fontPath, TE_PATH_MAX - 1);

    fnt->path[TE_PATH_MAX - 1] = '\0';
    fnt->pointSize = pointSize;
    fnt->flags = flags;

    if (!te_set_face_size(fnt))
    {
        FT_Done_Face(fnt->face);
        fnt->face = NULL;
        return 0;
    }

    te_compute_scale(fnt);
    te_compute_metrics(fnt);

    dc->numFonts++;

    return 1;
}

void TE_FontRemove(struct TERenderContext *dc, CONST_STRPTR fontPath, LONG pointSize)
{
    int i, j;

    if (!dc || !fontPath)
        return;

    for (i = 0; i < dc->numFonts; i++)
    {
        if (dc->fonts[i].pointSize == pointSize &&
            strcmp(dc->fonts[i].path, (const char *)fontPath) == 0)
        {
            if (dc->fonts[i].face)
                FT_Done_Face(dc->fonts[i].face);

            for (j = i; j < dc->numFonts - 1; j++)
                dc->fonts[j] = dc->fonts[j + 1];

            dc->numFonts--;

            memset(&dc->fonts[dc->numFonts], 0, sizeof(dc->fonts[0]));

            te_cache_flush(dc);
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

        fnt->pointSize = (LONG)nPointSize;

        if (te_set_face_size(fnt))
        {
            te_compute_scale(fnt);
            te_compute_metrics(fnt);
        }
    }

    te_cache_flush(dc);
}

void TE_SetFlags(struct TERenderContext *dc, ULONG flags)
{
    if (!dc)
        return;

    if (dc->flags != flags)
    {
        /* Anti-alias on/off changes rasterisation mode, so cache contents are no longer compatible */
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

    dc->style = (UBYTE)(styleBits & 0xFF);
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

    if (dc->textARGB != textRGB || dc->bgARGB != bgRGB)
    {
        /* CLUT mirrors are keyed by current draw colour. Drop them so next blit rebuilds with new colours */
        te_flush_clut_mirrors(dc);

        dc->aaRampValid = 0;
    }

    dc->textARGB = textRGB;
    dc->bgARGB = bgRGB;
    dc->txtPen = -1;
    dc->bgPen = -1;
    dc->pensValid = 0;
}

void TE_SetColorPen(struct TERenderContext *dc, struct Screen *screen, LONG txtPen, LONG bgPen)
{
    UBYTE r, g, b;

    if (!dc)
        return;

    if (dc->pensValid && dc->txtPen == txtPen && dc->bgPen == bgPen)
        return;

    /* Drop AGA mirrors and AA ramp; both depend on current pens */
    te_flush_clut_mirrors(dc);
    dc->aaRampValid = 0;

    dc->txtPen = txtPen;
    dc->bgPen = bgPen;
    dc->pensValid = 1;

    /* Also derive ARGB equivalents from screen palette so RTG path can blend correctly */
    if (screen && txtPen >= 0)
    {
        ULONG rgb[3];

        GetRGB32(screen->ViewPort.ColorMap, (ULONG)txtPen, 1, rgb);

        r = (UBYTE)(rgb[0] >> 24);
        g = (UBYTE)(rgb[1] >> 24);
        b = (UBYTE)(rgb[2] >> 24);

        dc->textARGB = ((ULONG)r << 16) | ((ULONG)g << 8) | (ULONG)b;
    }

    if (screen && bgPen >= 0)
    {
        ULONG rgb[3];

        GetRGB32(screen->ViewPort.ColorMap, (ULONG)bgPen, 1, rgb);

        r = (UBYTE)(rgb[0] >> 24);
        g = (UBYTE)(rgb[1] >> 24);
        b = (UBYTE)(rgb[2] >> 24);

        dc->bgARGB = ((ULONG)r << 16) | ((ULONG)g << 8) | (ULONG)b;
    }
}

static int te_screen_is_rtg(struct Screen *screen, UBYTE *pixfmt_out, ULONG *depth_out)
{
    ULONG depth = 0;
    UBYTE pixfmt = 0;

    if (!screen || !screen->RastPort.BitMap)
        return 0;

    depth = (ULONG)GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH);

    TE_DEBUG_INFO("te_screen_is_rtg - BMA_DEPTH=%lu", depth);

    if (depth_out)
        *depth_out = depth;

    if (CyberGfxBase)
    {
        ULONG d = (ULONG)GetCyberMapAttr(screen->RastPort.BitMap, CYBRMATTR_DEPTH);
        ULONG fmt;

        TE_DEBUG_INFO("te_screen_is_rtg - CYBRMATTR_DEPTH=%lu", d);

        if (d > 8)
        {
            fmt = (ULONG)GetCyberMapAttr(screen->RastPort.BitMap, CYBRMATTR_PIXFMT);

            TE_DEBUG_INFO("te_screen_is_rtg - CYBRMATTR_PIXFMT=%lu", fmt);

            if ((LONG)fmt > 0)
            {
                if (pixfmt_out)
                    *pixfmt_out = (UBYTE)fmt;

                if (depth_out)
                    *depth_out = d;

                return 1;
            }
        }
    }

    if (pixfmt_out)
        *pixfmt_out = pixfmt;

    return 0;
}

void TE_SetScreen(struct TERenderContext *dc, struct Screen *screen)
{
    TE_DEBUG_INFO("TE_SetScreen called - dc->screen=%p, screen=%p", (void *)dc->screen, (void *)screen);

    if (!dc || !screen)
        return;

    if (dc->screen != screen)
    {
        TE_DEBUG_INFO("Screen changed, calling te_flush_clut_mirrors");

        /* New screen: drop all CLUT mirrors pen mapping changes */
        te_flush_clut_mirrors(dc);
        dc->clutValid = 0;
        dc->aaRampValid = 0;
    }

    TE_DEBUG_INFO("Setting screen and calling te_screen_is_rtg");

    dc->screen = screen;
    dc->screenIsRTG = (UBYTE)te_screen_is_rtg(screen, &dc->screenPixFmt, &dc->screenDepth);

    TE_DEBUG_INFO("screenIsRTG=%d", dc->screenIsRTG);

    if (!dc->screenIsRTG)
    {
        TE_DEBUG_INFO("Calling te_rebuild_clut_remap");

        te_rebuild_clut_remap(dc, screen);
    }

    TE_DEBUG_INFO("TE_SetScreen completed");
}

void TE_UpdatePalette(struct TERenderContext *dc, struct Screen *screen)
{
    if (!dc || !screen)
        return;

    te_flush_clut_mirrors(dc);
    dc->aaRampValid = 0;
    dc->clutValid = 0;

    if (!dc->screenIsRTG)
        te_rebuild_clut_remap(dc, screen);
}

static void te_rebuild_clut_remap(struct TERenderContext *dc, struct Screen *screen)
{
    static ULONG palette[256 * 3];
    int ncol;
    int r4, g4, b4;
    int i;

    if (!screen || !screen->ViewPort.ColorMap)
        return;

    ncol = 1 << (int)dc->screenDepth;

    if (ncol > 256)
        ncol = 256;

    if (ncol <= 0)
        ncol = 16;

    GetRGB32(screen->ViewPort.ColorMap, 0, (ULONG)ncol, palette);

    /* For each RGB444 key, find the closest screen pen */
    for (r4 = 0; r4 < 16; r4++)
    {
        int r = (r4 << 4) | r4;

        for (g4 = 0; g4 < 16; g4++)
        {
            int g = (g4 << 4) | g4;

            for (b4 = 0; b4 < 16; b4++)
            {
                int b = (b4 << 4) | b4;
                int bestDiff = 0x7FFFFFFF;
                int bestPen = 0;

                for (i = 0; i < ncol; i++)
                {
                    int pr = (int)(palette[i * 3 + 0] >> 24);
                    int pg = (int)(palette[i * 3 + 1] >> 24);
                    int pb = (int)(palette[i * 3 + 2] >> 24);
                    int dr = pr - r;
                    int dg = pg - g;
                    int db = pb - b;
                    int d = dr * dr + dg * dg + db * db;

                    if (d < bestDiff)
                    {
                        bestDiff = d;
                        bestPen = i;
                    }
                }

                dc->clutRemap[((unsigned)r4 << 8) | ((unsigned)g4 << 4) | (unsigned)b4] = (UBYTE)bestPen;
            }
        }
    }

    dc->clutValid = 1;
}

static int te_pen_for_rgb(struct TERenderContext *dc, UBYTE r, UBYTE g, UBYTE b)
{
    if (!dc->clutValid)
        return 1; /* fallback to pen 1 if no remap yet */

    return (int)dc->clutRemap[(((unsigned)r >> 4) << 8) | (((unsigned)g >> 4) << 4) | ((unsigned)b >> 4)];
}

static void te_rebuild_aa_ramp(struct TERenderContext *dc)
{
    int i;
    UBYTE fr = (UBYTE)((dc->textARGB >> 16) & 0xFF);
    UBYTE fg = (UBYTE)((dc->textARGB >> 8) & 0xFF);
    UBYTE fb = (UBYTE)(dc->textARGB & 0xFF);
    UBYTE br = (UBYTE)((dc->bgARGB >> 16) & 0xFF);
    UBYTE bg = (UBYTE)((dc->bgARGB >> 8) & 0xFF);
    UBYTE bb = (UBYTE)(dc->bgARGB & 0xFF);

    /* 16-step interpolation from bg -> fg. Step 0 ~= bg, step 15 ~= fg */
    for (i = 0; i < 16; i++)
    {
        int t = i;
        int u = 15 - i;
        UBYTE rr = (UBYTE)((fr * t + br * u) / 15);
        UBYTE gg = (UBYTE)((fg * t + bg * u) / 15);
        UBYTE bb_ = (UBYTE)((fb * t + bb * u) / 15);

        dc->aaRamp[i] = (UBYTE)te_pen_for_rgb(dc, rr, gg, bb_);
    }

    dc->aaRampValid = 1;
}

static void te_glyph_free(struct TEGlyph *g)
{
    if (!g)
        return;

    if (g->data)
        FreeVec(g->data);

    if (g->clutBM)
        FreeBitMap(g->clutBM);

    if (g->maskBM)
        FreeBitMap(g->maskBM);

    FreeVec(g);
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

static void te_flush_clut_mirrors(struct TERenderContext *dc)
{
    int i;

    if (!dc)
        return;

    for (i = 0; i < TE_HASH_BUCKETS; i++)
    {
        struct TEGlyph *g;

        for (g = dc->buckets[i]; g; g = g->next)
        {
            if (g->clutBM)
            {
                FreeBitMap(g->clutBM);
                g->clutBM = NULL;
            }
            if (g->maskBM)
            {
                FreeBitMap(g->maskBM);
                g->maskBM = NULL;
            }
        }
    }
}

/* Nearest-neighbour scale blit from FreeType bitmap to cache buffer */
static void te_blit_to_cache(FT_Bitmap *bm, UBYTE *dst, int dstW, int dstH, int dstPitch, int format)
{
    int srcW = (int)bm->width;
    int srcH = (int)bm->rows;
    ULONG dx, dy;
    int x, y;

    if (dstW <= 0 || dstH <= 0 || srcW <= 0 || srcH <= 0)
        return;

    dx = ((ULONG)srcW << 16) / (ULONG)dstW;
    dy = ((ULONG)srcH << 16) / (ULONG)dstH;

    switch (format)
    {
    case FMT_MONO:
    {
        /* MONO destination: 1 bit per pixel, MSB-first, word-aligned rows. Source can be MONO or GRAY */
        ULONG accumY = 0;

        for (y = 0; y < dstH; y++)
        {
            int sy = (int)(accumY >> 16);
            UBYTE *drow = dst + (ULONG)y * (ULONG)dstPitch;
            ULONG accumX = 0;

            if (bm->pixel_mode == FT_PIXEL_MODE_MONO)
            {
                const UBYTE *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);
                    int byte = sx >> 3;
                    int bit = 7 - (sx & 7);

                    if (srow[byte] & (1 << bit))
                        drow[x >> 3] |= (UBYTE)(0x80 >> (x & 7));

                    accumX += dx;
                }
            }
            else if (bm->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                const UBYTE *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);

                    if (srow[sx] >= 128)
                        drow[x >> 3] |= (UBYTE)(0x80 >> (x & 7));

                    accumX += dx;
                }
            }
            else if (bm->pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                const UBYTE *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);
                    const UBYTE *sp = srow + sx * 4;

                    if (sp[3] >= 128)
                        drow[x >> 3] |= (UBYTE)(0x80 >> (x & 7));

                    accumX += dx;
                }
            }

            accumY += dy;
        }

        break;
    }

    case FMT_GRAY:
    {
        ULONG accumY = 0;

        for (y = 0; y < dstH; y++)
        {
            int sy = (int)(accumY >> 16);
            UBYTE *drow = dst + (ULONG)y * (ULONG)dstPitch;
            ULONG accumX = 0;

            if (bm->pixel_mode == FT_PIXEL_MODE_MONO)
            {
                const UBYTE *srow = bm->buffer + sy * bm->pitch;

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
                const UBYTE *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);

                    drow[x] = srow[sx];
                    accumX += dx;
                }
            }
            else if (bm->pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                const UBYTE *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);
                    const UBYTE *sp = srow + sx * 4;

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
        ULONG accumY = 0;

        for (y = 0; y < dstH; y++)
        {
            int sy = (int)(accumY >> 16);
            UBYTE *drow = dst + (ULONG)y * (ULONG)dstPitch;
            ULONG accumX = 0;

            if (bm->pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                const UBYTE *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);
                    const UBYTE *sp = srow + sx * 4;
                    UBYTE *dp = drow + x * 4;

                    /* FT_PIXEL_MODE_BGRA is byte-order B,G,R,A. We store R,G,B,A. Alpha is pre-multiplied by FreeType */
                    dp[0] = sp[2];
                    dp[1] = sp[1];
                    dp[2] = sp[0];
                    dp[3] = sp[3];

                    accumX += dx;
                }
            }
            else if (bm->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                /* GRAY upgraded to RGBA: alpha = coverage, rgb = white */
                const UBYTE *srow = bm->buffer + sy * bm->pitch;

                for (x = 0; x < dstW; x++)
                {
                    int sx = (int)(accumX >> 16);
                    UBYTE *dp = drow + x * 4;

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
    struct TEGlyph *g;
    FT_GlyphSlot slot;
    FT_Bitmap *bm;
    int format;
    int load_flags;
    int srcW, srcH, dstW, dstH, pitch;
    FT_Render_Mode rmode;

    if (!dc || !fnt || !fnt->face)
        return NULL;

    /* Reselect this face's size: when we have multiple bitmap-only fallbacks they all share same FT_Library */
    te_set_face_size(fnt);

    /* Choose FreeType load flags based on AA setting and font type */
    load_flags = FT_LOAD_RENDER | FT_LOAD_COLOR;

    /* FT_LOAD_TARGET_MONO: requests 1-bit bitmap. Only valid for SCALABLE outlines. For bitmap-only colour emoji we MUST NOT set this */
    if (!(dc->flags & TE_FLAG_ANTIALIAS) && FT_IS_SCALABLE(fnt->face) && !FT_HAS_COLOR(fnt->face))
        load_flags |= FT_LOAD_TARGET_MONO;

    /* For colour emoji fonts, always ensure we get colour data by removing any MONO flag */
    if (FT_HAS_COLOR(fnt->face))
        load_flags &= ~FT_LOAD_TARGET_MONO;

    /* Styled glyph (bold/italic) on scalable fonts: render in two steps so transform happens on the outline */
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

    /* Pick cache format. BGRA bitmaps (colour emoji) always use RGBA format to preserve original colours */
    if (bm->pixel_mode == FT_PIXEL_MODE_BGRA)
        format = FMT_RGBA;
    else if (dc->flags & TE_FLAG_ANTIALIAS)
        format = FMT_GRAY;
    else
        format = FMT_MONO;

    srcW = (int)bm->width;
    srcH = (int)bm->rows;

    /* Scale to target size. For outline fonts scale = 1/1 so dst == src */
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
    }

    /* Row pitch -- MONO needs word-aligned rows for BltTemplate */
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

    g = (struct TEGlyph *)AllocVec(sizeof(*g), MEMF_PUBLIC | MEMF_CLEAR);

    if (!g)
        return NULL;

    if (dstW > 0 && dstH > 0)
    {
        g->data = (UBYTE *)AllocVec((ULONG)pitch * (ULONG)dstH, MEMF_PUBLIC | MEMF_CLEAR);

        if (!g->data)
        {
            FreeVec(g);
            return NULL;
        }

        te_blit_to_cache(bm, g->data, dstW, dstH, pitch, format);
    }

    g->codepoint = cp;
    g->style = dc->style;
    g->format = (UBYTE)format;
    g->width = (WORD)dstW;
    g->height = (WORD)dstH;
    g->pitch = (WORD)pitch;
    g->bearingX = (WORD)((slot->bitmap_left * fnt->scaleNum + fnt->scaleDen / 2) / fnt->scaleDen);
    g->bearingY = (WORD)((slot->bitmap_top * fnt->scaleNum + fnt->scaleDen / 2) / fnt->scaleDen);
    g->advance = (WORD)(((slot->advance.x >> 6) * fnt->scaleNum + fnt->scaleDen / 2) / fnt->scaleDen);

    if (g->advance <= 0)
        g->advance = (WORD)dstW;

    return g;
}

static struct TEGlyph *te_make_notfound(struct TERenderContext *dc)
{
    /* Build tofu-style rectangle outline glyph at primary font's cell size. Uses pure FMT_MONO so it always blits cheaply */
    struct TEFont *primary;
    struct TEGlyph *g;
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

    /* Roughly 0.6x cell height for the width (em-square heuristic) */
    w = (h * 3) / 5;

    if (w < 4)
        w = 4;

    if (h < 6)
        h = 6;

    pitch = ((w + 15) >> 4) * 2;

    g = (struct TEGlyph *)AllocVec(sizeof(*g), MEMF_PUBLIC | MEMF_CLEAR);

    if (!g)
        return NULL;

    g->data = (UBYTE *)AllocVec((ULONG)pitch * (ULONG)h, MEMF_PUBLIC | MEMF_CLEAR);

    if (!g->data)
    {
        FreeVec(g);
        return NULL;
    }

    /* Top and bottom edges */
    for (x = 0; x < w; x++)
    {
        g->data[0 * pitch + (x >> 3)] |= (UBYTE)(0x80 >> (x & 7));
        g->data[(h - 1) * pitch + (x >> 3)] |= (UBYTE)(0x80 >> (x & 7));
    }

    /* Left and right edges */
    for (y = 0; y < h; y++)
    {
        g->data[y * pitch + (0 >> 3)] |= (UBYTE)(0x80 >> (0 & 7));
        g->data[y * pitch + ((w - 1) >> 3)] |= (UBYTE)(0x80 >> ((w - 1) & 7));
    }

    g->codepoint = TE_NOTFOUND_CP;
    g->style = 0;
    g->format = FMT_MONO;
    g->width = (WORD)w;
    g->height = (WORD)h;
    g->pitch = (WORD)pitch;
    g->bearingX = 0;
    g->bearingY = (WORD)(primary->ascender > 0 ? primary->ascender : h);
    g->advance = (WORD)(w + 1);

    return g;
}

static struct TEGlyph *te_get_glyph(struct TERenderContext *dc, ULONG cp)
{
    struct TEGlyph *g;
    struct TEFont *fnt;
    FT_UInt gi = 0;
    unsigned int h;
    int i;

    if (!dc)
        return NULL;

    h = te_hash_cp_style(cp, dc->style);

    /* Cache lookup first. Most glyphs are seen many times */
    for (g = dc->buckets[h]; g; g = g->next)
    {
        if (g->codepoint == cp && g->style == dc->style)
            return g;
    }

    /* Cache miss. Iterate font chain to find one that has this cp */
    fnt = NULL;

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

    g = te_make_glyph(dc, fnt, gi, cp);

    if (!g)
        return NULL;

    /* Insert at bucket head */
    g->next = dc->buckets[h];
    dc->buckets[h] = g;

    return g;
}

static struct TEGlyph *te_get_notfound(struct TERenderContext *dc)
{
    struct TEGlyph *g;
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
static UBYTE te_blend8(UBYTE s, UBYTE d, UBYTE a)
{
    /* out = (src * (a+1) + dst * (255-a)) >> 8. Max error 1 LSB */
    return (UBYTE)(((ULONG)s * ((ULONG)a + 1UL) + (ULONG)d * (ULONG)(255 - a)) >> 8);
}

/* Draw one MONO glyph through BltTemplate. Caller must ensure FgPen is text colour and DrMd is JAM1 (or JAM2 + matching BgPen) */
static void te_draw_mono(struct TERenderContext *dc, struct RastPort *rp, struct TEGlyph *g, int dx, int dy)
{
    int srcx = 0, srcy = 0;
    int w, h;

    (void)dc;

    if (!g || !g->data || g->width <= 0 || g->height <= 0)
        return;

    w = g->width;
    h = g->height;

    /* Trivial bounds clip vs rastport extents. Layers handle the rest */
    if (rp->Layer)
    {
        int lw = rp->Layer->bounds.MaxX - rp->Layer->bounds.MinX + 1;
        int lh = rp->Layer->bounds.MaxY - rp->Layer->bounds.MinY + 1;

        if (dx < 0)
        {
            srcx = -dx;
            w += dx;
            dx = 0;
        }
        if (dy < 0)
        {
            srcy = -dy;
            h += dy;
            dy = 0;
        }
        if (dx + w > lw)
            w = lw - dx;
        if (dy + h > lh)
            h = lh - dy;
    }

    if (w <= 0 || h <= 0)
        return;

    BltTemplate((PLANEPTR)(g->data + srcy * g->pitch), srcx, g->pitch, rp, dx, dy, w, h);
}

/* CLUT (AGA) path: draw pixels via SetAPen+WritePixel. Saves/restores rp->FgPen to prevent side effects */
static void te_draw_indexed_pixels(struct TERenderContext *dc, struct RastPort *rp, struct TEGlyph *g, int dx, int dy)
{
    int x, y;
    int gw = g->width;
    int gh = g->height;
    UBYTE saved_pen = rp->FgPen;
    int pen_dirty = 0;

    if (!dc->aaRampValid)
        te_rebuild_aa_ramp(dc);

    for (y = 0; y < gh; y++)
    {
        const UBYTE *srow = g->data + (ULONG)y * (ULONG)g->pitch;

        for (x = 0; x < gw; x++)
        {
            if (g->format == FMT_GRAY)
            {
                UBYTE a = srow[x];
                int level;

                if (!a)
                    continue;

                level = (int)a >> 4; /* 0..15 */

                if (level > 15)
                    level = 15;

                SetAPen(rp, dc->aaRamp[level]);
                WritePixel(rp, dx + x, dy + y);

                pen_dirty = 1;
            }
            else if (g->format == FMT_RGBA)
            {
                const UBYTE *sp = srow + (ULONG)x * 4UL;
                UBYTE a = sp[3];
                int pen;

                if (!a)
                    continue;

                /* For semi-transparent pixels approximate by mixing with bg colour */
                if (a < 0xFF)
                {
                    UBYTE br = (UBYTE)((dc->bgARGB >> 16) & 0xFF);
                    UBYTE bg = (UBYTE)((dc->bgARGB >> 8) & 0xFF);
                    UBYTE bb = (UBYTE)(dc->bgARGB & 0xFF);

                    UBYTE rr = te_blend8(sp[0], br, a);
                    UBYTE gg = te_blend8(sp[1], bg, a);
                    UBYTE bbb = te_blend8(sp[2], bb, a);

                    pen = te_pen_for_rgb(dc, rr, gg, bbb);
                }
                else
                {
                    pen = te_pen_for_rgb(dc, sp[0], sp[1], sp[2]);
                }

                SetAPen(rp, pen);
                WritePixel(rp, dx + x, dy + y);
                pen_dirty = 1;
            }
        }
    }

    /* Restore caller's FgPen so the RastPort state is unchanged on return */
    if (pen_dirty)
        SetAPen(rp, saved_pen);
}

/* RTG path for GRAY / RGBA glyphs. Uses WritePixelArray for truecolor screens. We compose glyph into RGB24 scratch buffer, then single WritePixelArray call to CGX */
static void te_draw_rtg(struct TERenderContext *dc, struct RastPort *rp, struct TEGlyph *g, int dx, int dy)
{
    int gw, gh;
    int sx0, sy0, sx1, sy1;
    int dxc, dyc, dwc, dhc;
    UBYTE fr, fgc, fb;
    UBYTE br, bgc, bb;
    UBYTE *buf;
    ULONG need;
    int x, y;

    if (!g || !g->data || g->width <= 0 || g->height <= 0)
        return;

    gw = g->width;
    gh = g->height;

    /* Source rect inside the glyph; defaults to whole glyph */
    sx0 = 0;
    sy0 = 0;
    sx1 = gw;
    sy1 = gh;
    dxc = dx;
    dyc = dy;

    /* WritePixelArray handles clipping internally via its layer handshake */
    dwc = sx1 - sx0;
    dhc = sy1 - sy0;

    if (dwc <= 0 || dhc <= 0)
        return;

    /* Ensure the scratch buffer is big enough for dwc * dhc RGB24
     * pixels.  Grow only -- subsequent glyphs reuse the allocation */
    need = (ULONG)dwc * (ULONG)dhc * 3UL;

    if (need > dc->scratchBytes)
    {
        if (dc->scratch)
            FreeVec(dc->scratch);

        dc->scratch = (UBYTE *)AllocVec(need, MEMF_PUBLIC);

        if (!dc->scratch)
        {
            dc->scratchBytes = 0;
            return;
        }

        dc->scratchBytes = need;
    }

    buf = dc->scratch;

    /* Pre-resolve fg / bg as 0-255 channels */
    fr = (UBYTE)((dc->textARGB >> 16) & 0xFF);
    fgc = (UBYTE)((dc->textARGB >> 8) & 0xFF);
    fb = (UBYTE)(dc->textARGB & 0xFF);
    br = (UBYTE)((dc->bgARGB >> 16) & 0xFF);
    bgc = (UBYTE)((dc->bgARGB >> 8) & 0xFF);
    bb = (UBYTE)(dc->bgARGB & 0xFF);

    if (g->format == FMT_MONO)
    {
        /* MONO: 1-bit MSB-first input. Expand to RGB24 with fg/bg colours. Paint whole rectangle because BltTemplate-style "leave background alone" doesn't survive WritePixelArray */
        for (y = 0; y < dhc; y++)
        {
            const UBYTE *srow = g->data + (ULONG)(sy0 + y) * (ULONG)g->pitch;
            UBYTE *drow = buf + (ULONG)y * (ULONG)dwc * 3UL;

            for (x = 0; x < dwc; x++)
            {
                int sx = sx0 + x;
                int byte = sx >> 3;
                int bit = 7 - (sx & 7);
                UBYTE on = (UBYTE)((srow[byte] >> bit) & 1);

                drow[x * 3 + 0] = on ? fr : br;
                drow[x * 3 + 1] = on ? fgc : bgc;
                drow[x * 3 + 2] = on ? fb : bb;
            }
        }
    }
    else if (g->format == FMT_GRAY)
    {
        /* GRAY = single-channel coverage; interpolate fg<->bg by alpha */
        for (y = 0; y < dhc; y++)
        {
            const UBYTE *srow = g->data + (ULONG)(sy0 + y) * (ULONG)g->pitch;
            UBYTE *drow = buf + (ULONG)y * (ULONG)dwc * 3UL;

            for (x = 0; x < dwc; x++)
            {
                UBYTE a = srow[sx0 + x];

                drow[x * 3 + 0] = te_blend8(fr, br, a);
                drow[x * 3 + 1] = te_blend8(fgc, bgc, a);
                drow[x * 3 + 2] = te_blend8(fb, bb, a);
            }
        }
    }
    else /* FMT_RGBA */
    {
        for (y = 0; y < dhc; y++)
        {
            const UBYTE *srow = g->data + (ULONG)(sy0 + y) * (ULONG)g->pitch;
            UBYTE *drow = buf + (ULONG)y * (ULONG)dwc * 3UL;

            for (x = 0; x < dwc; x++)
            {
                const UBYTE *sp = srow + (ULONG)(sx0 + x) * 4UL;
                UBYTE a = sp[3];

                drow[x * 3 + 0] = te_blend8(sp[0], br, a);
                drow[x * 3 + 1] = te_blend8(sp[1], bgc, a);
                drow[x * 3 + 2] = te_blend8(sp[2], bb, a);
            }
        }
    }

    /* One WritePixelArray for whole glyph. No lock, no per-pixel SetAPen */
    WritePixelArray(buf,
                    0, 0,
                    dwc * 3,
                    rp,
                    dxc, dyc,
                    dwc, dhc,
                    RECTFMT_RGB);
}

static void te_draw_glyph(struct TERenderContext *dc, struct RastPort *rp, struct TEGlyph *g, int penX, int baseY)
{
    int dx, dy;

    if (!g || g->width <= 0 || g->height <= 0)
        return;

    dx = penX + g->bearingX;
    dy = baseY - g->bearingY;

    /* On RTG truecolor screens, BltTemplate is unreliable -- it depends on planar-style hardware that CyberGraphX/P96 may emulate poorly. So route MONO through te_draw_rtg too */
    if (g->format == FMT_MONO)
    {
        if (dc->screenIsRTG && CyberGfxBase)
        {
            te_draw_rtg(dc, rp, g, dx, dy);
            return;
        }

        te_draw_mono(dc, rp, g, dx, dy);
        return;
    }

    /* GRAY / RGBA: WritePixelArray works on every CGX-managed screen, and crucially locks the Layer internally so Intuition can't race with us mid-glyph */
    if (dc->screenIsRTG && CyberGfxBase)
    {
        te_draw_rtg(dc, rp, g, dx, dy);
        return;
    }

    /* Last-resort fallback: pure AGA without any CGX driver. Per-pixel SetAPen + WritePixel; hold Layer lock around whole glyph */
    if (rp->Layer)
        LockLayer(0, rp->Layer);

    te_draw_indexed_pixels(dc, rp, g, dx, dy);

    if (rp->Layer)
        UnlockLayer(rp->Layer);
}

/* UTF-8 decode (single codepoint) */
static int te_decode_utf8(const UBYTE *p, const UBYTE *end, ULONG *cp_out, int *consumed_out)
{
    ULONG cp = 0;
    int n = 0;
    UBYTE b;

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
        /* Invalid lead byte; consume one and return replacement.*/
        cp = 0xFFFDUL;
        n = 1;
    }

    *cp_out = cp;
    *consumed_out = n;

    return 1;
}

void TE_RenderText(struct RastPort *rp, struct TERenderContext *dc, struct TEDrawPosition *pos, CONST_STRPTR utf8, ULONG maxChars)
{
    const UBYTE *p, *end;
    ULONG count = 0;
    int penX, penY;

    if (!rp || !dc || !pos || !utf8 || dc->numFonts <= 0)
        return;

    p = (const UBYTE *)utf8;
    end = p + strlen((const char *)utf8);

    penX = pos->x;
    penY = pos->y;

    while (p < end)
    {
        ULONG cp;
        int consumed = 0;
        struct TEGlyph *g;

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
            penX += (int)(dc->fonts[0].height / 2) * (int)dc->tabSpaces;
            continue;
        }

        g = te_get_glyph(dc, cp);

        if (!g)
        {
            /* Try replacement char in the chain */
            g = te_get_glyph(dc, TE_REPLACEMENT_CP);

            if (!g)
                g = te_get_notfound(dc);
        }

        if (!g)
            continue;

        te_draw_glyph(dc, rp, g, penX, penY);

        penX += g->advance > 0 ? g->advance : g->width;
    }

    pos->x = (WORD)penX;
    pos->y = (WORD)penY;
}

void TE_GetMetrics(struct TERenderContext *dc, struct TEGlyphMetrics *out)
{
    struct TEFont *primary;
    int advance = 0;
    FT_UInt gi;

    if (!dc || !out)
        return;

    if (dc->numFonts <= 0)
    {
        out->width = 8;
        out->height = 16;
        out->baseX = 0;
        out->baseY = 12;
        return;
    }

    primary = &dc->fonts[0];

    /* Cell advance derived from primary's M glyph (or X as fallback). This is what callers use as monospace cell width */
    if (primary->face)
    {
        gi = FT_Get_Char_Index(primary->face, 'M');

        if (gi && FT_Load_Glyph(primary->face, gi, FT_LOAD_DEFAULT) == 0)
            advance = (int)(primary->face->glyph->metrics.horiAdvance >> 6);

        if (advance <= 0)
            advance = (int)(primary->face->size->metrics.max_advance >> 6);

        if (advance > 0)
            advance = (advance * primary->scaleNum + primary->scaleDen / 2) / primary->scaleDen;
    }

    if (advance <= 0)
        advance = (int)primary->pointSize / 2;

    out->width = (WORD)advance;
    out->height = (WORD)primary->height;
    out->baseX = 0;
    out->baseY = (WORD)primary->ascender;
}

void TE_MeasureText(struct TERenderContext *dc, CONST_STRPTR utf8, LONG maxChars, struct TEGlyphMetrics *out)
{
    const UBYTE *p, *end;
    LONG count = 0;
    int width = 0;

    if (!dc || !utf8 || !out)
        return;

    p = (const UBYTE *)utf8;
    end = p + strlen((const char *)utf8);

    while (p < end)
    {
        ULONG cp;
        int consumed = 0;
        struct TEGlyph *g;

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
            width += g->advance > 0 ? g->advance : g->width;
    }

    out->width = (WORD)width;
    out->height = (WORD)(dc->numFonts > 0 ? dc->fonts[0].height : 16);
    out->baseX = 0;
    out->baseY = (WORD)(dc->numFonts > 0 ? dc->fonts[0].ascender : 12);
}

void TE_GetCharOffsets(struct TERenderContext *dc, CONST_STRPTR utf8, LONG maxChars, LONG *arrayout)
{
    const UBYTE *p, *end;
    LONG count = 0;
    int width = 0;

    if (!dc || !utf8 || !arrayout)
        return;

    p = (const UBYTE *)utf8;
    end = p + strlen((const char *)utf8);

    while (p < end)
    {
        ULONG cp;
        int consumed = 0;
        struct TEGlyph *g;

        if (maxChars >= 0 && count >= maxChars)
            break;

        if (!te_decode_utf8(p, end, &cp, &consumed))
            break;

        p += consumed;

        if (cp == 0)
            break;

        arrayout[count] = width;

        if (cp != 0x0A && cp != 0x0D)
        {
            g = te_get_glyph(dc, cp);

            if (!g)
                g = te_get_notfound(dc);

            if (g)
                width += g->advance > 0 ? g->advance : g->width;
        }

        count++;
    }

    if (maxChars < 0 || count < maxChars)
        arrayout[count] = width;
}
