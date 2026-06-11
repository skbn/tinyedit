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

#include "te_rastport.h"
#include "core/utf8.h"

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
#include <graphics/text.h>
#include <graphics/view.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H
#include FT_OUTLINE_H

#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>

struct Library *CyberGfxBase = NULL;
static int s_cgx_tried = 0;

/* graphics.library v39+ probe & scratch RastPort helper for WritePixelArray8 */
extern struct GfxBase *GfxBase;

/* Tuneables */
#define TE_GLYPH_HASH_BUCKETS 64
#define TE_CACHE_BUDGET_BYTES (256 * 1024)
#define TE_REPLACEMENT_CP 0xFFFDUL
#define TE_PATH_MAX 256
#define TE_DPI 72
#define TE_MAX_FALLBACKS 16

/* Glyph cache pixel formats */
#define FMT_MONO 0
#define FMT_GRAY 1
#define FMT_RGBA 2

struct TEGlyph
{
    struct TEGlyph *next; /* hash chain */
    ULONG codepoint;
    UBYTE format; /* FMT_MONO / FMT_GRAY / FMT_RGBA */
    UBYTE style;  /* TE_STY_* snapshot */
    UBYTE pad[2];

    /* Glyph metrics in pixels */
    WORD width;    /* bitmap width  in pixels */
    WORD height;   /* bitmap height in pixels */
    WORD bearingX; /* bitmap_left */
    WORD bearingY; /* bitmap_top */
    WORD advance;  /* pen advance (fixed integer pixels) */
    WORD pitch;    /* bytes per row (MONO: padded to byte) */

    /* Bitmap data (allocated separately). Format-dependent:
     *   MONO: pitch bytes/row, 1 bpp, MSB-first
     *   GRAY: pitch == width, 1 byte per pixel = alpha
     *   RGBA: pitch == width*4 (R,G,B,A) */
    UBYTE *data;

    /* CLUT-remapped mirror for indexed-colour screens. Allocated lazily
     * the first time the glyph is drawn on an indexed screen; freed on
     * UpdateColorMap() or palette change */
    UBYTE *clut_mirror;   /* pitch == width bytes, 1 pen index/pixel */
    UBYTE *clut_mask;     /* 1 bit/pixel mask matching clut_mirror */
    WORD clut_pitch_mask; /* pitch in bytes for clut_mask */

    ULONG bytes_held; /* bytes accounted for in cache budget */
    ULONG lastUse;    /* simple LRU counter */
};

struct TEFont
{
    struct TEFont *next; /* singly linked list */

    FT_Face face;
    char path[TE_PATH_MAX];
    int pointSize;
    ULONG flags; /* TE_FLAG_FIXEDWIDTH inheritance, etc */

    /* Cached metrics (in pixels at pointSize) */
    WORD ascender;
    WORD descender;
    WORD height;
    WORD monospace_advance; /* advance of 'M' or first ASCII glyph */
    UBYTE has_color;        /* 1 if face has color (CBDT/CBLC etc) */
    UBYTE is_bitmap_only;   /* 1 if FT_FACE_FLAG_FIXED_SIZES only */
    UBYTE reserved[2];

    /* Per-font glyph cache */
    struct TEGlyph *buckets[TE_GLYPH_HASH_BUCKETS];
    int glyphCount;
    ULONG bytesHeld;
};

struct TERenderContext
{
    FT_Library ft;

    struct TEFont *fonts; /* head of fallback chain */
    int fontCount;

    ULONG prefs;     /* TE_FLAG_* bits */
    ULONG style;     /* TE_STY_* bits */
    ULONG tabSpaces; /* default 4 */

    /* Colour state */
    ULONG fgRGB; /* 0x00RRGGBB */
    ULONG bgRGB; /* 0x00RRGGBB or ~0UL == transparent */
    LONG fgPen;  /* last pen used, or -1 */
    LONG bgPen;  /* last pen used, or -1 */

    /* Screen association for CLUT remap */
    struct Screen *screen;
    UBYTE colorMap[4096]; /* 12-bit RGB -> closest CLUT pen */
    UBYTE colorMapValid;
    UBYTE screenIsRTG;  /* 1 if depth > 8 or CGX truecolor */
    UBYTE screenPixFmt; /* CGX PIXFMT_* or 0 if unknown */
    UBYTE reserved[1];
    ULONG screenDepth;

    /* LRU counter (simple monotonic) */
    ULONG lruClock;

    /* Premixed grey-level pens for indexed GRAY rendering: 17 levels of
     * blend between bgPen and fgPen colours. Recomputed on color/palette
     * change. The mapping is alpha (0..255) -> pen index */
    UBYTE grayPens[17];
    UBYTE grayPensValid;

    /* Scratch RastPort/BitMap for WritePixelArray8 (graphics.library v39+)
     * Allocated lazily on first use; sized to fit the largest glyph drawn
     * If allocation fails or graphics is pre-v39, te_draw_clut falls back
     * to the RectFill-per-span path which works on every Amiga (OCS/ECS
     * included) */
    struct RastPort *scratch_rp;
    struct BitMap *scratch_bm;
    int scratch_w;
    int scratch_h;
    UBYTE scratch_depth;
    UBYTE scratch_tried; /* 0 not tried, 1 in progress, 2 unsupported */

    /* Reusable ARGB32 buffer for the read-modify-write AA blend path on
     * RTG screens. Grown lazily to the largest glyph we encounter, kept
     * alive across calls so the per-string cost is one realloc at most */
    ULONG *aa_buf;
    ULONG aa_buf_size; /* in pixels (w*h), not bytes */
    UBYTE aa_disabled; /* 1 = ReadPixelArray failed once, stay on fallback */
};

static struct TEGlyph *te_glyph_get(struct TERenderContext *dc, struct TEFont *fnt, ULONG cp);
static void te_glyph_free(struct TEGlyph *g);
static void te_font_flush_cache(struct TEFont *fnt);
static void te_glyph_flush_clut_mirrors(struct TEFont *fnt);
static int te_decode_utf8(const UBYTE *p, const UBYTE *end, ULONG *cp_out, int *len_out);
static int te_rasterise(struct TERenderContext *dc, struct TEFont *fnt, ULONG cp, struct TEGlyph **out);
static void te_draw_glyph(struct TERenderContext *dc, struct RastPort *rp, struct TEGlyph *g, int penX, int baseY);
static void te_recompute_grays(struct TERenderContext *dc);
static int te_screen_is_rtg(struct Screen *screen, UBYTE *fmt_out, ULONG *depth_out);
static void te_evict_lru(struct TERenderContext *dc);

static void cgx_probe(void)
{
    if (s_cgx_tried)
        return;

    s_cgx_tried = 1;

    /* CyberGfxBase may stay NULL -- that's fine, we degrade to MONO */
    CyberGfxBase = OpenLibrary("cybergraphics.library", 40L);
}

static void cgx_close(void)
{
    if (CyberGfxBase)
    {
        CloseLibrary(CyberGfxBase);
        CyberGfxBase = NULL;
    }

    s_cgx_tried = 0;
}

/* UTF-8 decoder: returns 1 on valid sequence, 0 on error */
static int te_decode_utf8(const UBYTE *p, const UBYTE *end, ULONG *cp_out, int *len_out)
{
    ULONG cp;
    int n;
    int i;

    if (p >= end)
    {
        *cp_out = 0;
        *len_out = 0;

        return 0;
    }

    if (p[0] < 0x80)
    {
        *cp_out = (ULONG)p[0];
        *len_out = 1;

        return 1;
    }

    if ((p[0] & 0xE0) == 0xC0)
    {
        n = 2;
        cp = p[0] & 0x1F;
    }
    else if ((p[0] & 0xF0) == 0xE0)
    {
        n = 3;
        cp = p[0] & 0x0F;
    }
    else if ((p[0] & 0xF8) == 0xF0)
    {
        n = 4;
        cp = p[0] & 0x07;
    }
    else
    {
        *cp_out = TE_REPLACEMENT_CP;
        *len_out = 1;

        return 1;
    }

    if (p + n > end)
    {
        *cp_out = TE_REPLACEMENT_CP;
        *len_out = 1;

        return 1;
    }

    for (i = 1; i < n; i++)
    {
        if ((p[i] & 0xC0) != 0x80)
        {
            *cp_out = TE_REPLACEMENT_CP;
            *len_out = 1;

            return 1;
        }

        cp = (cp << 6) | (p[i] & 0x3F);
    }

    /* surrogates and >0x10FFFF are not valid UTF-8 codepoints */
    if (cp > 0x10FFFFUL || (cp >= 0xD800 && cp <= 0xDFFF))
        cp = TE_REPLACEMENT_CP;

    *cp_out = cp;
    *len_out = n;

    return 1;
}

/* Hash a codepoint to a bucket index */
static int te_hash_cp(ULONG cp)
{
    /* FNV-ish mix, restricted to bucket count */
    ULONG h = cp;

    h ^= (h >> 16);
    h *= 0x9E370001UL;
    h ^= (h >> 13);

    return (int)(h & (TE_GLYPH_HASH_BUCKETS - 1));
}

static int te_screen_is_rtg(struct Screen *screen, UBYTE *fmt_out, ULONG *depth_out)
{
    ULONG depth = 0;
    UBYTE fmt = 0;

    *fmt_out = 0;
    *depth_out = 0;

    if (!screen || !screen->RastPort.BitMap)
        return 0;

    depth = (ULONG)GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH);

    cgx_probe();

    if (CyberGfxBase)
    {
        ULONG isCgx = GetCyberMapAttr(screen->RastPort.BitMap, CYBRMATTR_ISCYBERGFX);

        if (isCgx)
        {
            ULONG cdepth = GetCyberMapAttr(screen->RastPort.BitMap, CYBRMATTR_DEPTH);
            ULONG cfmt = GetCyberMapAttr(screen->RastPort.BitMap, CYBRMATTR_PIXFMT);

            if (cdepth > 0)
                depth = cdepth;

            if (cdepth > 8)
            {
                fmt = (UBYTE)cfmt;
                *fmt_out = fmt;
                *depth_out = depth;

                return 1;
            }
        }
    }

    *depth_out = depth;

    return 0;
}

struct TERenderContext *TE_ContextCreate(void)
{
    struct TERenderContext *dc;
    FT_Error err;

    dc = (struct TERenderContext *)AllocVec(sizeof(*dc), MEMF_CLEAR | MEMF_PUBLIC);

    if (!dc)
        return NULL;

    err = FT_Init_FreeType(&dc->ft);

    if (err)
    {
        FreeVec(dc);

        return NULL;
    }

    dc->prefs = 0;
    dc->style = TE_STY_NORMAL;
    dc->tabSpaces = 4;
    dc->fgRGB = 0x00FFFFFFUL;
    dc->bgRGB = 0;
    dc->fgPen = -1;
    dc->bgPen = -1;

    return dc;
}

void TE_ContextRelease(struct TERenderContext *dc)
{
    if (!dc)
        return;

    while (dc->fonts)
    {
        struct TEFont *f = dc->fonts;

        dc->fonts = f->next;
        te_font_flush_cache(f);

        if (f->face)
            FT_Done_Face(f->face);

        FreeVec(f);
    }

    /* Free WPA8 scratch RP/BM if we allocated them */
    if (dc->scratch_rp)
    {
        FreeVec(dc->scratch_rp);
        dc->scratch_rp = NULL;
    }

    if (dc->scratch_bm)
    {
        FreeBitMap(dc->scratch_bm);
        dc->scratch_bm = NULL;
    }

    /* Free the AA blend staging buffer if we ever grew one */
    if (dc->aa_buf)
    {
        FreeVec(dc->aa_buf);

        dc->aa_buf = NULL;
        dc->aa_buf_size = 0;
    }

    if (dc->ft)
        FT_Done_FreeType(dc->ft);

    FreeVec(dc);
}

/* Font management */
LONG TE_FontAdd(struct TERenderContext *dc, CONST_STRPTR fontPath, LONG pointSize, ULONG flags)
{
    struct TEFont *f;
    FT_Error err;
    FT_Face face = NULL;
    int n;

    if (!dc || !fontPath || pointSize <= 0)
        return 0;

    if (dc->fontCount >= TE_MAX_FALLBACKS)
        return 0;

    err = FT_New_Face(dc->ft, (const char *)fontPath, 0, &face);

    if (err || !face)
        return 0;

    /* For scalable faces use Set_Char_Size; for fixed-size
     * bitmaps select the closest strike */
    if (FT_IS_SCALABLE(face))
    {
        err = FT_Set_Char_Size(face, 0, pointSize * 64, TE_DPI, TE_DPI);

        if (err)
        {
            FT_Done_Face(face);
            return 0;
        }
    }
    else
    {
        /* Fixed strike: pick closest available */
        int i, best = 0;
        int target = pointSize;
        int diff = 0x7FFFFFFF;

        for (i = 0; i < face->num_fixed_sizes; i++)
        {
            int sz = face->available_sizes[i].height;
            int d = (sz > target) ? (sz - target) : (target - sz);

            if (d < diff)
            {
                diff = d;
                best = i;
            }
        }

        err = FT_Select_Size(face, best);

        if (err)
        {
            FT_Done_Face(face);
            return 0;
        }
    }

    f = (struct TEFont *)AllocVec(sizeof(*f), MEMF_CLEAR | MEMF_PUBLIC);

    if (!f)
    {
        FT_Done_Face(face);
        return 0;
    }

    f->face = face;
    f->pointSize = pointSize;
    f->flags = flags;

    n = (int)strlen((const char *)fontPath);

    if (n >= TE_PATH_MAX)
        n = TE_PATH_MAX - 1;

    memcpy(f->path, fontPath, n);

    f->path[n] = 0;

    f->has_color = FT_HAS_COLOR(face) ? 1 : 0;
    f->is_bitmap_only = (FT_HAS_FIXED_SIZES(face) && !FT_IS_SCALABLE(face)) ? 1 : 0;

    /* Cache the line metrics in pixels */
    if (face->size && face->size->metrics.y_ppem)
    {
        f->ascender = (WORD)(face->size->metrics.ascender >> 6);
        f->descender = (WORD)(-(face->size->metrics.descender) >> 6);
        f->height = (WORD)((face->size->metrics.height) >> 6);

        if (f->height == 0)
            f->height = f->ascender + f->descender;
    }
    else
    {
        f->ascender = (WORD)pointSize;
        f->descender = (WORD)(pointSize / 4);
        f->height = f->ascender + f->descender;
    }

    /* Estimate monospace advance from 'M' */
    FT_UInt gi = FT_Get_Char_Index(face, 'M');

    if (gi && FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) == 0)
        f->monospace_advance = (WORD)(face->glyph->advance.x >> 6);

    if (f->monospace_advance <= 0)
        f->monospace_advance = (WORD)(pointSize * 6 / 10);

    /* Append to end of list (preserves fallback order) */
    if (!dc->fonts)
    {
        dc->fonts = f;
    }
    else
    {
        struct TEFont *t = dc->fonts;

        while (t->next)
            t = t->next;

        t->next = f;
    }

    dc->fontCount++;

    return 1;
}

static void te_font_flush_cache(struct TEFont *fnt)
{
    int i;

    if (!fnt)
        return;

    for (i = 0; i < TE_GLYPH_HASH_BUCKETS; i++)
    {
        struct TEGlyph *g = fnt->buckets[i];

        while (g)
        {
            struct TEGlyph *n = g->next;

            te_glyph_free(g);
            g = n;
        }

        fnt->buckets[i] = NULL;
    }

    fnt->glyphCount = 0;
    fnt->bytesHeld = 0;
}

static void te_glyph_flush_clut_mirrors(struct TEFont *fnt)
{
    int i;

    if (!fnt)
        return;

    for (i = 0; i < TE_GLYPH_HASH_BUCKETS; i++)
    {
        struct TEGlyph *g = fnt->buckets[i];

        while (g)
        {
            if (g->clut_mirror)
            {
                FreeVec(g->clut_mirror);
                g->clut_mirror = NULL;
            }

            if (g->clut_mask)
            {
                FreeVec(g->clut_mask);
                g->clut_mask = NULL;
            }

            g->clut_pitch_mask = 0;
            g = g->next;
        }
    }
}

static void te_glyph_free(struct TEGlyph *g)
{
    if (!g)
        return;

    if (g->data)
        FreeVec(g->data);

    if (g->clut_mirror)
        FreeVec(g->clut_mirror);

    if (g->clut_mask)
        FreeVec(g->clut_mask);

    FreeVec(g);
}

void TE_FontRemove(struct TERenderContext *dc, CONST_STRPTR fontPath, LONG pointSize)
{
    struct TEFont *prev = NULL, *f;

    if (!dc || !fontPath)
        return;

    f = dc->fonts;

    while (f)
    {
        if (f->pointSize == pointSize && strcmp(f->path, (const char *)fontPath) == 0)
        {
            if (prev)
                prev->next = f->next;
            else
                dc->fonts = f->next;

            te_font_flush_cache(f);

            if (f->face)
                FT_Done_Face(f->face);

            FreeVec(f);

            dc->fontCount--;

            return;
        }

        prev = f;
        f = f->next;
    }
}

void TE_FontFlush(struct TERenderContext *dc)
{
    if (!dc)
        return;

    while (dc->fonts)
    {
        struct TEFont *f = dc->fonts;

        dc->fonts = f->next;
        te_font_flush_cache(f);

        if (f->face)
            FT_Done_Face(f->face);

        FreeVec(f);
    }

    dc->fontCount = 0;
}

void TE_FontResize(struct TERenderContext *dc, ULONG nPointSize, ULONG fontMask)
{
    struct TEFont *f;
    int idx = 0;

    if (!dc || nPointSize == 0)
        return;

    for (f = dc->fonts; f; f = f->next, idx++)
    {
        if (fontMask != 0 && !(fontMask & (1UL << idx)))
            continue;

        if (!f->face)
            continue;

        if (FT_IS_SCALABLE(f->face))
        {
            FT_Set_Char_Size(f->face, 0, nPointSize * 64, TE_DPI, TE_DPI);
        }
        else
        {
            int i, best = 0, diff = 0x7FFFFFFF;
            int target = (int)nPointSize;

            for (i = 0; i < f->face->num_fixed_sizes; i++)
            {
                int sz = f->face->available_sizes[i].height;
                int d = (sz > target) ? (sz - target) : (target - sz);

                if (d < diff)
                {
                    diff = d;
                    best = i;
                }
            }

            FT_Select_Size(f->face, best);
        }

        f->pointSize = (int)nPointSize;

        if (f->face->size && f->face->size->metrics.y_ppem)
        {
            f->ascender = (WORD)(f->face->size->metrics.ascender >> 6);
            f->descender = (WORD)(-(f->face->size->metrics.descender) >> 6);
            f->height = (WORD)((f->face->size->metrics.height) >> 6);

            if (f->height == 0)
                f->height = f->ascender + f->descender;
        }

        te_font_flush_cache(f);
    }

    dc->grayPensValid = 0;
}

/* Preferences and style */
void TE_SetFlags(struct TERenderContext *dc, ULONG flags)
{
    struct TEFont *f;
    ULONG old;

    if (!dc)
        return;

    if (dc->prefs == flags)
        return;

    old = dc->prefs;
    dc->prefs = flags;

    /* AA toggle invalidates the cache (MONO vs GRAY rasterisation differs)
     * Also invalidate the CLUT mirrors and grayPens so the new AA mode
     * picks them up properly on the next render */
    for (f = dc->fonts; f; f = f->next)
    {
        te_font_flush_cache(f);
        te_glyph_flush_clut_mirrors(f);
    }

    /* If AA just turned ON, recompute grayPens defensively so that any
     * CLUT-path glyph rendered next has a valid greyscale palette */
    if ((flags & TE_FLAG_ANTIALIAS) && !(old & TE_FLAG_ANTIALIAS))
    {
        dc->grayPensValid = 0;

        if (dc->colorMapValid)
            te_recompute_grays(dc);
    }
}

ULONG TE_GetFlags(const struct TERenderContext *dc)
{
    return dc ? dc->prefs : 0;
}

void TE_CacheFlush(struct TERenderContext *dc)
{
    struct TEFont *f;

    if (!dc)
        return;

    for (f = dc->fonts; f; f = f->next)
        te_font_flush_cache(f);

    dc->grayPensValid = 0;
}

void TE_SetStyle(struct TERenderContext *dc, ULONG styleBits)
{
    if (!dc)
        return;

    dc->style = styleBits & (TE_STY_BOLD | TE_STY_ITALIC);
}

void TE_SetTabWidth(struct TERenderContext *dc, ULONG n)
{
    if (!dc)
        return;

    if (n < 1)
        n = 1;

    if (n > 12)
        n = 12;

    dc->tabSpaces = n;
}

/* LRU eviction: drops oldest glyphs until below budget */
static void te_evict_lru(struct TERenderContext *dc)
{
    ULONG total = 0;
    struct TEFont *f;

    for (f = dc->fonts; f; f = f->next)
        total += f->bytesHeld;

    if (total < TE_CACHE_BUDGET_BYTES)
        return;

    /* Drop ~25% by repeatedly evicting the single oldest glyph */
    while (total > (TE_CACHE_BUDGET_BYTES * 3) / 4)
    {
        struct TEFont *bestF = NULL;
        struct TEGlyph *bestG = NULL;
        struct TEGlyph *prevOfBest = NULL;
        int bucketOfBest = -1;
        ULONG bestLRU = ~0UL;
        int i;

        for (f = dc->fonts; f; f = f->next)
        {
            for (i = 0; i < TE_GLYPH_HASH_BUCKETS; i++)
            {
                struct TEGlyph *prev = NULL, *g = f->buckets[i];

                while (g)
                {
                    if (g->lastUse <= bestLRU)
                    {
                        bestLRU = g->lastUse;
                        bestF = f;
                        bestG = g;
                        prevOfBest = prev;
                        bucketOfBest = i;
                    }

                    prev = g;
                    g = g->next;
                }
            }
        }

        if (!bestG || !bestF)
            break;

        /* Unlink */
        if (prevOfBest)
            prevOfBest->next = bestG->next;
        else
            bestF->buckets[bucketOfBest] = bestG->next;

        bestF->bytesHeld -= bestG->bytes_held;

        if (total >= bestG->bytes_held)
            total -= bestG->bytes_held;
        else
            total = 0;

        bestF->glyphCount--;
        te_glyph_free(bestG);
    }
}

/* Glyph fetch: hash lookup + rasterise on miss */
static struct TEGlyph *te_glyph_get(struct TERenderContext *dc, struct TEFont *fnt, ULONG cp)
{
    int bi;
    struct TEGlyph *g;

    if (!dc || !fnt)
        return NULL;

    bi = te_hash_cp(cp);

    for (g = fnt->buckets[bi]; g; g = g->next)
    {
        if (g->codepoint == cp && g->style == (UBYTE)dc->style)
        {
            g->lastUse = ++dc->lruClock;
            return g;
        }
    }

    /* Miss -- rasterise */
    if (te_rasterise(dc, fnt, cp, &g) != 0 || !g)
        return NULL;

    g->next = fnt->buckets[bi];
    fnt->buckets[bi] = g;
    fnt->glyphCount++;
    fnt->bytesHeld += g->bytes_held;
    g->lastUse = ++dc->lruClock;

    te_evict_lru(dc);
    return g;
}

/* Glyph rasterisation */
static int te_rasterise(struct TERenderContext *dc, struct TEFont *fnt, ULONG cp, struct TEGlyph **out)
{
    FT_Face face;
    FT_UInt gi;
    FT_Error err;
    FT_GlyphSlot slot;
    FT_Int32 load_flags;
    int want_aa;
    int want_color;
    struct TEGlyph *g;
    UBYTE format;
    int w, h, pitch;
    ULONG bytes;
    int i, j;

    *out = NULL;

    if (!dc || !fnt || !fnt->face)
        return -1;

    face = fnt->face;

    gi = FT_Get_Char_Index(face, (FT_ULong)cp);

    if (gi == 0)
        return -1; /* Font does not have this glyph */

    want_aa = (dc->prefs & TE_FLAG_ANTIALIAS) ? 1 : 0;
    want_color = fnt->has_color ? 1 : 0;

    /* On screens with very shallow palettes (OCS/ECS-style 4-16 colour
     * displays), color emoji rasterise to RGBA but then get nearest-colour
     * remapped to a tiny palette: the result looks terrible and the
     * memory waste isn't worth it.  Force monochrome / grayscale on
     * depth < 8 if we've been told the screen depth */
    if (want_color && dc->screenDepth > 0 && dc->screenDepth < 8)
        want_color = 0;

    if (want_aa && dc->screenDepth > 0 && dc->screenDepth < 4)
        want_aa = 0;

    load_flags = FT_LOAD_DEFAULT;

    if (want_color)
        load_flags |= FT_LOAD_COLOR;

    if (!want_aa && !want_color)
        load_flags |= FT_LOAD_MONOCHROME | FT_LOAD_TARGET_MONO;
    else if (want_aa)
        load_flags |= FT_LOAD_TARGET_NORMAL;

    err = FT_Load_Glyph(face, gi, load_flags);

    if (err)
        return -1;

    /* Synthetic style transforms (only meaningful for outline fonts) */
    if ((dc->style & TE_STY_ITALIC) && face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
    {
        FT_Matrix shear;

        shear.xx = 0x10000;
        shear.xy = (FT_Fixed)(0.2 * 0x10000);
        shear.yx = 0;
        shear.yy = 0x10000;

        FT_Outline_Transform(&face->glyph->outline, &shear);
    }

    if ((dc->style & TE_STY_BOLD) && face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
        FT_Outline_Embolden(&face->glyph->outline, (FT_Pos)(fnt->pointSize * 64 / 24));

    /* Render to bitmap if it isn't already one */
    if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP)
    {
        FT_Render_Mode rmode = want_aa ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO;

        err = FT_Render_Glyph(face->glyph, rmode);

        if (err)
            return -1;
    }

    slot = face->glyph;

    /* Pick output format from the rendered bitmap's pixel mode */
    switch (slot->bitmap.pixel_mode)
    {
    case FT_PIXEL_MODE_MONO:
        format = FMT_MONO;
        break;
    case FT_PIXEL_MODE_GRAY:
        format = FMT_GRAY;
        break;
    case FT_PIXEL_MODE_BGRA:
        format = FMT_RGBA;
        break;
    default:
        return -1;
    }

    w = (int)slot->bitmap.width;
    h = (int)slot->bitmap.rows;

    /* Still a valid glyph (e.g. SPACE) -- emit a 0-sized entry */
    if (w == 0 || h == 0)
    {
        w = 0;
        h = 0;
    }

    /* Compute pitch for our packed storage */
    if (format == FMT_MONO)
    {
        /* BltTemplate's source modulo MUST be even (word-aligned) for the
         * blitter on AGA. Round up to multiples of 2 bytes */
        pitch = ((w + 15) >> 4) << 1;
    }
    else if (format == FMT_GRAY)
    {
        pitch = w;
    }
    else
    {
        pitch = w * 4;
    }

    bytes = (ULONG)pitch * (ULONG)h + sizeof(struct TEGlyph);

    g = (struct TEGlyph *)AllocVec(sizeof(*g), MEMF_CLEAR | MEMF_PUBLIC);

    if (!g)
        return -1;

    g->codepoint = cp;
    g->format = format;
    g->style = (UBYTE)dc->style;
    g->width = (WORD)w;
    g->height = (WORD)h;
    g->bearingX = (WORD)slot->bitmap_left;
    g->bearingY = (WORD)slot->bitmap_top;
    g->advance = (WORD)(slot->advance.x >> 6);
    g->pitch = (WORD)pitch;
    g->bytes_held = bytes;

    if (w > 0 && h > 0)
    {
        /* MONO data is consumed by BltTemplate -> blitter on AGA needs
         * CHIP RAM. GRAY/RGBA never touch the blitter, FAST is fine */
        ULONG memflags = (format == FMT_MONO) ? (MEMF_CHIP | MEMF_CLEAR) : (MEMF_PUBLIC | MEMF_CLEAR);

        g->data = (UBYTE *)AllocVec((ULONG)pitch * (ULONG)h, memflags);

        if (!g->data)
        {
            FreeVec(g);
            return -1;
        }

        if (format == FMT_MONO)
        {
            /* FT MONO bitmap has its own pitch (sometimes negative). Copy
             * row by row, MSB-first byte order (BltTemplate-compatible) */
            int src_pitch = slot->bitmap.pitch;
            const UBYTE *src = slot->bitmap.buffer;
            int sgn = (src_pitch < 0) ? -1 : 1;
            int abs_pitch = sgn < 0 ? -src_pitch : src_pitch;
            int copybytes = pitch < abs_pitch ? pitch : abs_pitch;

            if (sgn < 0)
                src += (h - 1) * abs_pitch;

            for (i = 0; i < h; i++)
            {
                memcpy(g->data + i * pitch, src, copybytes);
                src += sgn * abs_pitch;
            }
        }
        else if (format == FMT_GRAY)
        {
            int src_pitch = slot->bitmap.pitch;
            const UBYTE *src = slot->bitmap.buffer;
            int sgn = (src_pitch < 0) ? -1 : 1;
            int abs_pitch = sgn < 0 ? -src_pitch : src_pitch;
            int copybytes = pitch < abs_pitch ? pitch : abs_pitch;

            if (sgn < 0)
                src += (h - 1) * abs_pitch;

            for (i = 0; i < h; i++)
            {
                memcpy(g->data + i * pitch, src, copybytes);
                src += sgn * abs_pitch;
            }
        }
        else
        {
            /* FMT_RGBA -- FT emits BGRA, we store RGBA */
            int src_pitch = slot->bitmap.pitch;
            const UBYTE *src = slot->bitmap.buffer;
            int sgn = (src_pitch < 0) ? -1 : 1;
            int abs_pitch = sgn < 0 ? -src_pitch : src_pitch;

            if (sgn < 0)
                src += (h - 1) * abs_pitch;

            for (i = 0; i < h; i++)
            {
                UBYTE *d = g->data + i * pitch;
                const UBYTE *s = src;

                for (j = 0; j < w; j++)
                {
                    UBYTE B = s[0], G = s[1], R = s[2], A = s[3];

                    d[0] = R;
                    d[1] = G;
                    d[2] = B;
                    d[3] = A;
                    d += 4;
                    s += 4;
                }

                src += sgn * abs_pitch;
            }
        }
    }

    *out = g;

    return 0;
}

/* Build the 12-bit RGB -> 8-bit pen index lookup table from the screen's
 * colour map. Iterates 4096 entries and finds the closest match in the
 * screen's first 256 pens */
static void te_build_color_map(struct TERenderContext *dc, struct Screen *screen)
{
    static ULONG palette[256 * 3];
    int ncol;
    int r4, g4, b4;
    int i;

    if (!screen || !screen->ViewPort.ColorMap)
    {
        dc->colorMapValid = 0;
        return;
    }

    ncol = (1 << (int)dc->screenDepth);

    if (ncol > 256)
        ncol = 256;

    if (ncol <= 0)
        ncol = 16;

    /* GetRGB32 returns 32-bit R, G, B fixed for each pen (left-justified) */
    GetRGB32(screen->ViewPort.ColorMap, 0, (ULONG)ncol, palette);

    for (r4 = 0; r4 < 16; r4++)
    {
        for (g4 = 0; g4 < 16; g4++)
        {
            for (b4 = 0; b4 < 16; b4++)
            {
                int idx = (r4 << 8) | (g4 << 4) | b4;
                int rr = (r4 << 4) | r4;
                int gg = (g4 << 4) | g4;
                int bb = (b4 << 4) | b4;
                ULONG bestd = ~0UL;
                int bestp = 0;

                for (i = 0; i < ncol; i++)
                {
                    int pr = (int)((palette[i * 3 + 0] >> 24) & 0xFF);
                    int pg = (int)((palette[i * 3 + 1] >> 24) & 0xFF);
                    int pb = (int)((palette[i * 3 + 2] >> 24) & 0xFF);
                    int dr = pr - rr, dg = pg - gg, db = pb - bb;

                    ULONG d = (ULONG)(dr * dr) + (ULONG)(dg * dg) + (ULONG)(db * db);

                    if (d < bestd)
                    {
                        bestd = d;
                        bestp = i;
                    }
                }

                dc->colorMap[idx] = (UBYTE)bestp;
            }
        }
    }

    dc->colorMapValid = 1;
}

/* Premixed greyscale pen ramp for indexed GRAY rendering. Computes
 * 17 levels from bgRGB to fgRGB and looks each one up in colorMap[] */
static void te_recompute_grays(struct TERenderContext *dc)
{
    int i;
    UBYTE fr, fg_, fb, br, bg_, bb;

    if (!dc->colorMapValid)
    {
        dc->grayPensValid = 0;
        return;
    }

    fr = (UBYTE)((dc->fgRGB >> 16) & 0xFF);
    fg_ = (UBYTE)((dc->fgRGB >> 8) & 0xFF);
    fb = (UBYTE)(dc->fgRGB & 0xFF);
    br = (UBYTE)((dc->bgRGB >> 16) & 0xFF);
    bg_ = (UBYTE)((dc->bgRGB >> 8) & 0xFF);
    bb = (UBYTE)(dc->bgRGB & 0xFF);

    for (i = 0; i <= 16; i++)
    {
        int alpha = (i * 255) / 16;
        int rr = (br * (255 - alpha) + fr * alpha) / 255;
        int gg = (bg_ * (255 - alpha) + fg_ * alpha) / 255;
        int b = (bb * (255 - alpha) + fb * alpha) / 255;
        int idx = ((rr >> 4) << 8) | ((gg >> 4) << 4) | (b >> 4);

        dc->grayPens[i] = dc->colorMap[idx & 0xFFF];
    }

    dc->grayPensValid = 1;
}

void TE_SetColorRGB(struct TERenderContext *dc, ULONG fgRGB, ULONG bgRGB)
{
    struct TEFont *f;

    if (!dc)
        return;

    if (dc->fgRGB == fgRGB && dc->bgRGB == bgRGB)
        return;

    /* Both FG and BG affect CLUT mirrors -- GRAY mirrors use premixed
     * pens between BG and FG, RGBA mirrors are independent of FG/BG but
     * we flush them too for simplicity (cheap to rebuild lazily) */
    for (f = dc->fonts; f; f = f->next)
        te_glyph_flush_clut_mirrors(f);

    dc->fgRGB = fgRGB;
    dc->bgRGB = bgRGB;
    dc->fgPen = -1;
    dc->bgPen = -1;

    te_recompute_grays(dc);
}

void TE_SetColorPen(struct TERenderContext *dc, struct Screen *screen, LONG txtPen, LONG bgPen)
{
    ULONG fg, bg;
    ULONG pal[3];

    if (!dc || !screen)
        return;

    if (!screen->ViewPort.ColorMap)
        return;

    if (txtPen >= 0 && txtPen < 256)
    {
        GetRGB32(screen->ViewPort.ColorMap, (ULONG)txtPen, 1, pal);
        fg = ((pal[0] >> 8) & 0xFF0000UL) | ((pal[1] >> 16) & 0xFF00UL) | ((pal[2] >> 24) & 0xFFUL);
    }
    else
    {
        fg = dc->fgRGB;
    }

    if (bgPen >= 0 && bgPen < 256)
    {
        GetRGB32(screen->ViewPort.ColorMap, (ULONG)bgPen, 1, pal);
        bg = ((pal[0] >> 8) & 0xFF0000UL) | ((pal[1] >> 16) & 0xFF00UL) | ((pal[2] >> 24) & 0xFFUL);
    }
    else
    {
        bg = dc->bgRGB;
    }

    /* Fast path: same pens -> avoid full SetDrawColor work */
    if (txtPen == dc->fgPen && bgPen == dc->bgPen)
    {
        dc->fgRGB = fg;
        dc->bgRGB = bg;
        return;
    }

    /* Different pens but maybe same colors -- still update */
    TE_SetColorRGB(dc, fg, bg);

    dc->fgPen = txtPen;
    dc->bgPen = bgPen;
}

/* Screen / palette association */
void TE_SetScreen(struct TERenderContext *dc, struct Screen *screen)
{
    if (!dc || !screen)
        return;

    if (dc->screen == screen && dc->colorMapValid)
        return;

    dc->screen = screen;
    dc->screenIsRTG = (UBYTE)te_screen_is_rtg(screen, &dc->screenPixFmt, &dc->screenDepth);

    if (!dc->screenIsRTG)
    {
        te_build_color_map(dc, screen);
        te_recompute_grays(dc);
    }
    else
    {
        dc->colorMapValid = 1; /* RTG doesn't need the LUT but mark ready */
    }
}

void TE_UpdatePalette(struct TERenderContext *dc, struct Screen *screen)
{
    struct TEFont *f;

    if (!dc || !screen)
        return;

    dc->screen = screen;
    dc->screenIsRTG = (UBYTE)te_screen_is_rtg(screen, &dc->screenPixFmt, &dc->screenDepth);

    if (!dc->screenIsRTG)
    {
        te_build_color_map(dc, screen);
        te_recompute_grays(dc);
    }
    else
    {
        dc->colorMapValid = 1;
    }

    /* Palette changed -- discard CLUT-remapped mirrors so they get rebuilt */
    for (f = dc->fonts; f; f = f->next)
        te_glyph_flush_clut_mirrors(f);
}

/* Build CLUT-remapped mirror for indexed screen */
static void te_build_clut_mirror(struct TERenderContext *dc, struct TEGlyph *g)
{
    int x, y;
    int w = g->width, h = g->height;
    int mask_pitch;

    if (g->clut_mirror)
        return;

    if (w == 0 || h == 0)
        return;

    if (!dc->colorMapValid)
        return;

    mask_pitch = ((w + 15) >> 4) << 1; /* word-aligned bytes per mask row */

    g->clut_mirror = (UBYTE *)AllocVec((ULONG)w * (ULONG)h, MEMF_CLEAR | MEMF_PUBLIC);

    if (!g->clut_mirror)
        return;

    g->clut_mask = (UBYTE *)AllocVec((ULONG)mask_pitch * (ULONG)h, MEMF_CLEAR | MEMF_PUBLIC);

    if (!g->clut_mask)
    {
        FreeVec(g->clut_mirror);
        g->clut_mirror = NULL;
        return;
    }

    g->clut_pitch_mask = (WORD)mask_pitch;

    if (g->format == FMT_GRAY)
    {
        if (!dc->grayPensValid)
            te_recompute_grays(dc);

        for (y = 0; y < h; y++)
        {
            const UBYTE *src = g->data + y * g->pitch;
            UBYTE *dst = g->clut_mirror + y * w;
            UBYTE *msk = g->clut_mask + y * mask_pitch;

            for (x = 0; x < w; x++)
            {
                UBYTE a = src[x];

                if (a >= 8)
                {
                    int idx = a >> 4; /* 0..15 */

                    dst[x] = dc->grayPensValid ? dc->grayPens[idx] : 1;
                    msk[x >> 3] |= (UBYTE)(0x80 >> (x & 7));
                }
            }
        }
    }
    else
    {
        /* FMT_RGBA */
        for (y = 0; y < h; y++)
        {
            const UBYTE *src = g->data + y * g->pitch;
            UBYTE *dst = g->clut_mirror + y * w;
            UBYTE *msk = g->clut_mask + y * mask_pitch;

            for (x = 0; x < w; x++)
            {
                UBYTE R = src[x * 4 + 0];
                UBYTE G = src[x * 4 + 1];
                UBYTE B = src[x * 4 + 2];
                UBYTE A = src[x * 4 + 3];

                if (A >= 16)
                {
                    int idx = ((R >> 4) << 8) | ((G >> 4) << 4) | (B >> 4);

                    dst[x] = dc->colorMap[idx & 0xFFF];
                    msk[x >> 3] |= (UBYTE)(0x80 >> (x & 7));
                }
            }
        }
    }
}

/* Per-glyph draw paths */
static void te_draw_mono(struct TEGlyph *g, struct RastPort *rp, int penX, int baseY)
{
    int dx = penX + g->bearingX;
    int dy = baseY - g->bearingY;

    if (g->width <= 0 || g->height <= 0 || !g->data)
        return;

    /* BltTemplate paints with FgPen wherever the template
     * has 1-bits srcX = 0, modulo = pitch */
    BltTemplate((PLANEPTR)g->data, 0, g->pitch, rp, dx, dy, g->width, g->height);
}

/* Bullet-proof GRAY-to-MONO drawer: builds a temporary 1-bit mask from the
 * 8-bit alpha mask (threshold 128) and paints with BltTemplate. Same path
 * as plain MONO -- known to work on every Amiga -- but driven from a GRAY
 * (anti-aliased) source. Loses the smooth AA edges but guarantees the
 * glyph is visible. Used as the GRAY fallback when the "pretty" AA paths
 * (RTG WritePixelArray, CLUT grayPens) fail silently due to driver bugs
 * or missing palette data */
static void te_draw_gray_as_mono(struct TEGlyph *g, struct RastPort *rp, int penX, int baseY)
{
    int dx = penX + g->bearingX;
    int dy = baseY - g->bearingY;
    int w = g->width, h = g->height;
    int x, y;
    int mono_pitch;
    UBYTE *mono;

    if (w <= 0 || h <= 0 || !g->data)
        return;

    /* Word-aligned per-row pitch in bytes -- BltTemplate requirement */
    mono_pitch = ((w + 15) >> 4) << 1;

    mono = (UBYTE *)AllocVec((ULONG)mono_pitch * (ULONG)h, MEMF_CHIP | MEMF_CLEAR);

    if (!mono)
        return;

    for (y = 0; y < h; y++)
    {
        const UBYTE *src = g->data + y * g->pitch;
        UBYTE *dst = mono + y * mono_pitch;

        for (x = 0; x < w; x++)
        {
            if (src[x] >= 128)
                dst[x >> 3] |= (UBYTE)(0x80 >> (x & 7));
        }
    }

    BltTemplate((PLANEPTR)mono, 0, mono_pitch, rp, dx, dy, w, h);
    FreeVec(mono);
}

static int te_have_wpa8(void)
{
    return GfxBase && ((struct Library *)GfxBase)->lib_Version >= 39;
}

/* Ensure scratch RP/BM exist and are at least (w, h) pixels. Returns the
 * scratch RastPort on success, NULL if allocation fails or wpa8 unsupported
 * The amiga BitMap is taken from the destination rp so depth/format match */
static struct RastPort *te_ensure_scratch(struct TERenderContext *dc, int w, int h, struct BitMap *friend_bm)
{
    UBYTE depth;

    if (!te_have_wpa8())
        return NULL;

    if (!friend_bm)
        return NULL;

    if (dc->scratch_tried == 2)
        return NULL;

    depth = (UBYTE)GetBitMapAttr(friend_bm, BMA_DEPTH);

    if (depth < 1 || depth > 8)
    {
        /* WPA8 only sensible on planar 1..8-bit screens. For deeper screens
         * we have other paths (cgx). Mark unsupported */
        dc->scratch_tried = 2;

        return NULL;
    }

    /* Existing scratch big enough and right depth? Reuse it */
    if (dc->scratch_rp && dc->scratch_bm && dc->scratch_depth == depth && dc->scratch_w >= w && dc->scratch_h >= h)
        return dc->scratch_rp;

    /* Need to (re)allocate. Free any previous */
    if (dc->scratch_rp)
    {
        FreeVec(dc->scratch_rp);
        dc->scratch_rp = NULL;
    }

    if (dc->scratch_bm)
    {
        FreeBitMap(dc->scratch_bm);
        dc->scratch_bm = NULL;
    }

    /* Round up to multiples of 16 for blitter friendliness; min 64x64 */
    if (w < 64)
        w = 64;

    if (h < 64)
        h = 64;

    w = (w + 15) & ~15;
    h = (h + 15) & ~15;

    dc->scratch_bm = AllocBitMap((ULONG)w, (ULONG)h, (ULONG)depth, BMF_CLEAR, friend_bm);

    if (!dc->scratch_bm)
    {
        dc->scratch_tried = 2;
        return NULL;
    }

    dc->scratch_rp = (struct RastPort *)AllocVec(sizeof(struct RastPort), MEMF_CLEAR | MEMF_PUBLIC);

    if (!dc->scratch_rp)
    {
        FreeBitMap(dc->scratch_bm);

        dc->scratch_bm = NULL;
        dc->scratch_tried = 2;
        return NULL;
    }

    InitRastPort(dc->scratch_rp);

    dc->scratch_rp->BitMap = dc->scratch_bm;
    dc->scratch_w = w;
    dc->scratch_h = h;
    dc->scratch_depth = depth;
    dc->scratch_tried = 1;

    return dc->scratch_rp;
}

/* Build a chunky-8bpp buffer for the whole glyph, with bg pen filling the
 * transparent positions. Used by the WPA8 fast path so we can blit the
 * full rectangle in one call (no mask needed -- transparent areas paint
 * the same bg pen the caller already painted, no visual change) */
static void te_build_solid_chunky(struct TERenderContext *dc, struct TEGlyph *g, UBYTE *out, int stride, UBYTE bg)
{
    int x, y, w = g->width, h = g->height;

    if (!g->clut_mirror || !g->clut_mask)
    {
        /* No CLUT mirror yet -- fill with bg */
        for (y = 0; y < h; y++)
            memset(out + y * stride, bg, w);

        return;
    }

    for (y = 0; y < h; y++)
    {
        const UBYTE *mrow = g->clut_mirror + y * w;
        const UBYTE *krow = g->clut_mask + y * g->clut_pitch_mask;
        UBYTE *drow = out + y * stride;

        for (x = 0; x < w; x++)
        {
            int opaque = (krow[x >> 3] & (UBYTE)(0x80 >> (x & 7)));

            drow[x] = opaque ? mrow[x] : bg;
        }
    }
}

/* row left-to-right grouping consecutive pixels with the same pen into a
 * single horizontal RectFill (a one-pixel-tall rectangle). This is one
 * blitter op per span instead of one per pixel, roughly an order of
 * magnitude faster than per-pixel WritePixel for typical text glyphs
 * which average 3-5 spans per row
 *
 * Note: this path only paints opaque pixels (mask-controlled). The
 * caller is responsible for clearing the cell background beforehand
 * the editor does that with a RectFill before calling TE */
static void te_draw_clut(struct TERenderContext *dc, struct TEGlyph *g, struct RastPort *rp, int penX, int baseY)
{
    int dx, dy, w, h;
    int x, y;
    UBYTE savedPen;
    struct RastPort *scratch;

    if (g->width <= 0 || g->height <= 0)
        return;

    if (!g->clut_mirror || !g->clut_mask)
        return;

    dx = penX + g->bearingX;
    dy = baseY - g->bearingY;
    w = g->width;
    h = g->height;

    /* Fast path: WritePixelArray8 (graphics.library v39+) */
    scratch = te_ensure_scratch(dc, w, h, rp->BitMap);

    if (scratch)
    {
        UBYTE *chunky = (UBYTE *)AllocVec((ULONG)w * (ULONG)h, MEMF_PUBLIC);

        if (chunky)
        {
            te_build_solid_chunky(dc, g, chunky, w, rp->BgPen);
            WritePixelArray8(rp, (UWORD)dx, (UWORD)dy, (UWORD)(dx + w - 1), (UWORD)(dy + h - 1), chunky, scratch);

            FreeVec(chunky);
            return;
        }
    }

    /* Slow fallback: per-span RectFill (works on every Amiga) */
    savedPen = rp->FgPen;

    for (y = 0; y < h; y++)
    {
        const UBYTE *mrow = g->clut_mirror + y * w;
        const UBYTE *krow = g->clut_mask + y * g->clut_pitch_mask;
        int row_y = dy + y;
        int span_start = -1;
        UBYTE span_pen = 0;

        for (x = 0; x <= w; x++)
        {
            int opaque = (x < w) && (krow[x >> 3] & (UBYTE)(0x80 >> (x & 7)));
            UBYTE p = opaque ? mrow[x] : 0;

            if (span_start < 0)
            {
                if (opaque)
                {
                    span_start = x;
                    span_pen = p;
                }
            }
            else if (!opaque || p != span_pen)
            {
                /* Flush span [span_start, x) */
                SetAPen(rp, span_pen);

                if (x - span_start == 1)
                    WritePixel(rp, dx + span_start, row_y);
                else
                    RectFill(rp, dx + span_start, row_y, dx + x - 1, row_y);

                if (opaque)
                {
                    span_start = x;
                    span_pen = p;
                }
                else
                {
                    span_start = -1;
                }
            }
        }
    }

    SetAPen(rp, savedPen);
}

/* Direct RGBA blit for RTG screens using WritePixelArray (CGX)
 * Falls back to CLUT path if CyberGfx is not available */
static void te_draw_rgba_rtg(struct TERenderContext *dc, struct TEGlyph *g, struct RastPort *rp, int penX, int baseY)
{
    int dx = penX + g->bearingX;
    int dy = baseY - g->bearingY;
    int w = g->width, h = g->height;
    ULONG *line = NULL;
    int x;
    int y;

    if (w <= 0 || h <= 0)
        return;

    cgx_probe();

    if (!CyberGfxBase)
    {
        te_build_clut_mirror(dc, g);
        te_draw_clut(dc, g, rp, penX, baseY);

        return;
    }

    /* Glyph stored as RGBA; WritePixelArray with PIXFMT_ARGB32 wants
     * 0xAARRGGBB in machine word order.  Repack in place is destructive
     * instead allocate a transient line buffer */
    line = (ULONG *)AllocVec((ULONG)w * 4UL, MEMF_PUBLIC);

    if (!line)
        return;

    for (y = 0; y < h; y++)
    {
        UBYTE *s = g->data + y * g->pitch;

        for (x = 0; x < w; x++)
        {
            UBYTE R = s[x * 4 + 0], G = s[x * 4 + 1], B = s[x * 4 + 2], A = s[x * 4 + 3];

            /* If the pixel is fully transparent, write the current BG
             * color so the glyph rectangle is solid. This matches
             * Move()+Text() semantics where the cell is filled */
            if (A == 0)
            {
                line[x] = (((ULONG)0xFF) << 24) | (((ULONG)((dc->bgRGB >> 16) & 0xFF)) << 16) | (((ULONG)((dc->bgRGB >> 8) & 0xFF)) << 8) | ((ULONG)((dc->bgRGB) & 0xFF));
            }
            else if (A == 0xFF)
            {
                line[x] = (((ULONG)0xFF) << 24) | (((ULONG)R) << 16) | (((ULONG)G) << 8) | (ULONG)B;
            }
            else
            {
                /* alpha blend over bgRGB */
                UBYTE bR = (UBYTE)((dc->bgRGB >> 16) & 0xFF);
                UBYTE bG = (UBYTE)((dc->bgRGB >> 8) & 0xFF);
                UBYTE bB = (UBYTE)(dc->bgRGB & 0xFF);
                UBYTE oR = (UBYTE)((R * A + bR * (255 - A)) / 255);
                UBYTE oG = (UBYTE)((G * A + bG * (255 - A)) / 255);
                UBYTE oB = (UBYTE)((B * A + bB * (255 - A)) / 255);

                line[x] = (((ULONG)0xFF) << 24) | (((ULONG)oR) << 16) | (((ULONG)oG) << 8) | (ULONG)oB;
            }
        }

        WritePixelArray((APTR)line, 0, 0, (UWORD)(w * 4), rp, (UWORD)dx, (UWORD)(dy + y), (UWORD)w, 1, PIXFMT_ARGB32);
    }

    FreeVec(line);
}

static int te_aa_ensure_buf(struct TERenderContext *dc, int w, int h)
{
    ULONG need = (ULONG)w * (ULONG)h;
    ULONG *t;

    if (need <= dc->aa_buf_size && dc->aa_buf)
        return 1;

    /* Grow (never shrink) so subsequent glyphs reuse the allocation. */
    if (dc->aa_buf)
    {
        FreeVec(dc->aa_buf);

        dc->aa_buf = NULL;
        dc->aa_buf_size = 0;
    }

    t = (ULONG *)AllocVec(need * 4UL, MEMF_PUBLIC);

    if (!t)
        return 0;

    dc->aa_buf = t;
    dc->aa_buf_size = need;
    return 1;
}

/* TODO */
static int te_aa_lock_begin(struct TERenderContext *dc, struct RastPort *rp)
{
    if (!dc)
        return 0;

    return 0;
}

static void te_aa_lock_end(struct TERenderContext *dc)
{
}

/* Alpha-blend a GRAY glyph into the RastPort using Read+Blend+Write
 * dx/dy are RastPort-local coordinates as the caller already computes
 * them (penX + bearingX, baseY - bearingY). Layer translation and
 * clipping are handled by the CGX driver */
static int te_aa_blend_gray(struct TERenderContext *dc, struct RastPort *rp, struct TEGlyph *g, int dx, int dy)
{
    int x, y;
    int w, h;
    LONG read_ok;
    UBYTE fR, fG, fB;
    ULONG *buf;

    if (!dc || !rp || !g || !g->data)
        return 0;

    if (dc->aa_disabled)
        return 0;

    if (!CyberGfxBase)
    {
        cgx_probe();
        if (!CyberGfxBase)
            return 0;
    }

    w = g->width;
    h = g->height;

    if (w <= 0 || h <= 0)
        return 1; /* SPACE etc., nothing to draw but not a failure */

    if (!te_aa_ensure_buf(dc, w, h))
        return 0;

    buf = dc->aa_buf;

    /* Read current screen pixels under the glyph rectangle into
     * the ARGB32 buffer.  The driver clips automatically to the Layer's
     * visible region, so pixels outside the window are left untouched
     * (the read may return them zeroed -- that's fine since we'll only
     * write pixels we actually modify) */
    read_ok = ReadPixelArray((APTR)buf, 0, 0, (UWORD)(w * 4), rp, (UWORD)dx, (UWORD)dy, (UWORD)w, (UWORD)h, PIXFMT_ARGB32);

    if (!read_ok)
    {
        /* Driver doesn't support ReadPixelArray with ARGB32 on this
         * screen. Disable AA so we don't try again */
        dc->aa_disabled = 1;
        return 0;
    }

    fR = (UBYTE)((dc->fgRGB >> 16) & 0xFF);
    fG = (UBYTE)((dc->fgRGB >> 8) & 0xFF);
    fB = (UBYTE)(dc->fgRGB & 0xFF);

    /* Blend foreground colour into buffer using the glyph alpha
     * Pixels with alpha==0 are skipped entirely (preserves whatever was
     * read for the background). Pixels with alpha==255 take fg
     * directly. Everything else is interpolated */
    for (y = 0; y < h; y++)
    {
        const UBYTE *srow = g->data + (ULONG)y * (ULONG)g->pitch;
        ULONG *drow = buf + (ULONG)y * (ULONG)w;

        for (x = 0; x < w; x++)
        {
            UBYTE a = srow[x];
            ULONG pix;

            if (a == 0)
                continue;

            if (a == 255)
            {
                drow[x] = 0xFF000000UL | ((ULONG)fR << 16) | ((ULONG)fG << 8) | (ULONG)fB;
                continue;
            }

            /* Existing pixel: ARGB32 -> extract bytes */
            pix = drow[x];
            {
                UBYTE dr = (UBYTE)((pix >> 16) & 0xFF);
                UBYTE dg = (UBYTE)((pix >> 8) & 0xFF);
                UBYTE db = (UBYTE)(pix & 0xFF);
                ULONG ap1 = (ULONG)a + 1UL;
                ULONG inv = (ULONG)(255 - a);

                dr = (UBYTE)(((ULONG)fR * ap1 + (ULONG)dr * inv) >> 8);
                dg = (UBYTE)(((ULONG)fG * ap1 + (ULONG)dg * inv) >> 8);
                db = (UBYTE)(((ULONG)fB * ap1 + (ULONG)db * inv) >> 8);

                drow[x] = 0xFF000000UL | ((ULONG)dr << 16) | ((ULONG)dg << 8) | (ULONG)db;
            }
        }
    }

    /* Write the modified rectangle back.  CGX handles
     * format conversion and Layer clipping. */
    WritePixelArray((APTR)buf, 0, 0, (UWORD)(w * 4), rp, (UWORD)dx, (UWORD)dy, (UWORD)w, (UWORD)h, PIXFMT_ARGB32);

    return 1;
}

static void te_draw_glyph(struct TERenderContext *dc, struct RastPort *rp, struct TEGlyph *g, int penX, int baseY)
{
    if (!g)
        return;

    /* SPACE etc. no ink */
    if (g->width == 0 || g->height == 0)
        return;

    if (g->format == FMT_MONO)
    {
        te_draw_mono(g, rp, penX, baseY);
        return;
    }

    /* RGBA (color emoji) on RTG: keep the dedicated CGX path */
    if (g->format == FMT_RGBA && dc->screenIsRTG)
    {
        te_draw_rgba_rtg(dc, g, rp, penX, baseY);
        return;
    }

    /* GRAY (anti-aliased) format. Three-tier fallback:
     *   1. Read-Modify-Write blend (RTG, ARGB32) -- real AA over the
     *      current screen contents. Driver-agnostic, uses public CGX
     *      APIs only.
     *   2. CLUT path with grayPens for indexed (AGA) screens
     *   3. MONO-threshold via BltTemplate as last-resort so something
     *      always shows even if both fast paths refuse */
    if (g->format == FMT_GRAY)
    {
        if (dc->screenIsRTG && !dc->aa_disabled)
        {
            int dx = penX + g->bearingX;
            int dy = baseY - g->bearingY;

            if (te_aa_blend_gray(dc, rp, g, dx, dy))
                return;
        }

        if (dc->colorMapValid && !dc->screenIsRTG)
        {
            te_build_clut_mirror(dc, g);
            te_draw_clut(dc, g, rp, penX, baseY);
            return;
        }

        /* Safety net */
        te_draw_gray_as_mono(g, rp, penX, baseY);
        return;
    }

    /* Indexed colour path -- CLUT-remapped mirror for non-MONO, non-GRAY
     * formats that didn't take the RTG fast path above */
    if (!dc->colorMapValid)
        return;

    te_build_clut_mirror(dc, g);
    te_draw_clut(dc, g, rp, penX, baseY);
}

/* Fallback chain glyph fetch */
static struct TEGlyph *te_glyph_from_chain(struct TERenderContext *dc, ULONG cp, struct TEFont **font_used)
{
    struct TEFont *f;
    struct TEGlyph *g;

    *font_used = NULL;

    for (f = dc->fonts; f; f = f->next)
    {
        if (!f->face)
            continue;

        if (FT_Get_Char_Index(f->face, (FT_ULong)cp) == 0)
            continue;

        g = te_glyph_get(dc, f, cp);

        if (g)
        {
            *font_used = f;
            return g;
        }
    }

    /* try U+FFFD as a last resort */
    for (f = dc->fonts; f; f = f->next)
    {
        if (!f->face)
            continue;

        if (FT_Get_Char_Index(f->face, TE_REPLACEMENT_CP) == 0)
            continue;

        g = te_glyph_get(dc, f, TE_REPLACEMENT_CP);

        if (g)
        {
            *font_used = f;
            return g;
        }
    }

    return NULL;
}

/* Measurement */
void TE_GetMetrics(struct TERenderContext *dc, struct TEGlyphMetrics *out)
{
    struct TEFont *f;
    WORD maxA = 0, maxD = 0;

    if (!out)
        return;

    out->width = 0;
    out->height = 0;
    out->baseX = 0;
    out->baseY = 0;

    if (!dc)
        return;

    for (f = dc->fonts; f; f = f->next)
    {
        if (f->ascender > maxA)
            maxA = f->ascender;

        if (f->descender > maxD)
            maxD = f->descender;
    }

    out->height = (WORD)(maxA + maxD);
    out->baseY = maxA;
}

void TE_MeasureText(struct TERenderContext *dc, CONST_STRPTR utf8, LONG maxChars, struct TEGlyphMetrics *out)
{
    const UBYTE *p, *end;
    LONG count = 0;
    LONG w = 0, lineW = 0, lines = 1;
    struct TEGlyphMetrics lm;

    if (!out)
        return;

    out->width = 0;
    out->height = 0;
    out->baseX = 0;
    out->baseY = 0;

    if (!dc || !utf8)
        return;

    TE_GetMetrics(dc, &lm);

    p = (const UBYTE *)utf8;
    end = p + strlen((const char *)utf8);

    while (p < end)
    {
        ULONG cp;
        int n;
        struct TEGlyph *g;
        struct TEFont *uf;

        if (maxChars >= 0 && count >= maxChars)
            break;

        if (!te_decode_utf8(p, end, &cp, &n))
            break;

        p += n;
        count++;

        if (cp == '\n')
        {
            if (lineW > w)
                w = lineW;

            lineW = 0;
            lines++;

            continue;
        }

        if (cp == '\t')
        {
            int adv = (int)dc->tabSpaces * (dc->fonts ? dc->fonts->monospace_advance : 8);

            lineW += adv;
            continue;
        }

        g = te_glyph_from_chain(dc, cp, &uf);

        if (g)
            lineW += g->advance;
        else
        {
            /* No glyph available -- use a default width consistent with the
             * fallback advance in TE_RenderText (1 or 2 cells based on
             * codepoint range) */
            int adv = dc->fonts ? dc->fonts->monospace_advance : 8;
            /*int wide = ((cp >= 0x1100 && cp <= 0x115F) ||
                        (cp >= 0x2190 && cp <= 0x21FF) ||
                        (cp >= 0x2600 && cp <= 0x26FF) ||
                        (cp >= 0x2700 && cp <= 0x27BF) ||
                        (cp >= 0x2B00 && cp <= 0x2BFF) ||
                        (cp >= 0x2E80 && cp <= 0xA4CF) ||
                        (cp >= 0xAC00 && cp <= 0xD7A3) ||
                        (cp >= 0xF900 && cp <= 0xFAFF) ||
                        (cp >= 0xFE30 && cp <= 0xFE6F) ||
                        (cp >= 0xFF00 && cp <= 0xFF60) ||
                        (cp >= 0x1F300UL && cp <= 0x1F9FFUL) ||
                        (cp >= 0x20000UL && cp <= 0x3FFFDUL));*/

            wchar_t wc = (wchar_t)cp;
            int wide = (wcswidth(&wc, 1) == 2);

            lineW += wide ? (2 * adv) : adv;
        }
    }

    if (lineW > w)
        w = lineW;

    out->width = (WORD)w;
    out->height = (WORD)(lm.height * lines);
    out->baseY = lm.baseY;
}

void TE_GetCharOffsets(struct TERenderContext *dc, CONST_STRPTR utf8, LONG maxChars, LONG *arrayout)
{
    const UBYTE *p, *end;
    LONG count = 0;
    LONG acc = 0;

    if (!arrayout || !dc || !utf8)
        return;

    arrayout[0] = 0;

    p = (const UBYTE *)utf8;
    end = p + strlen((const char *)utf8);

    while (p < end)
    {
        ULONG cp;
        int n;
        struct TEGlyph *g;
        struct TEFont *uf;

        if (maxChars >= 0 && count + 1 >= maxChars)
            break;

        if (!te_decode_utf8(p, end, &cp, &n))
            break;

        p += n;

        if (cp == '\t')
        {
            acc += (LONG)dc->tabSpaces * (dc->fonts ? dc->fonts->monospace_advance : 8);
        }
        else if (cp == '\n')
        {
            /* Offsets across newlines are not really meaningful; reset */
            acc = 0;
        }
        else
        {
            g = te_glyph_from_chain(dc, cp, &uf);

            if (g)
                acc += g->advance;
        }

        count++;
        arrayout[count] = acc;
    }
}

/* Public draw entry point */
void TE_RenderText(struct RastPort *rp, struct TERenderContext *dc, struct TEDrawPosition *pos, CONST_STRPTR utf8, ULONG maxChars)
{
    const UBYTE *p, *end;
    ULONG count = 0;
    int penX, baseY;

    if (!rp || !dc || !pos || !utf8)
        return;

    if (!dc->fonts)
        return;

    /* Update CLUT once per draw call in case the screen/palette has
     * changed since SetDrawScreen() was last called */
    if (rp->BitMap)
    {
        struct Screen *sc = NULL;

        if (rp->Layer && rp->Layer->LayerInfo)
            /* layer has no direct screen pointer; rely on dc->screen */
            sc = dc->screen;
        else
            sc = dc->screen;

        if (sc && !dc->colorMapValid)
            TE_SetScreen(dc, sc);
    }

    /* AA path: try to lock the destination bitmap once for the whole
     * string. If it works, te_draw_glyph() will blend each GRAY glyph
     * directly into bitmap memory in the screen's native pixel format
     * If the lock fails (unsupported format, planar bitmap, etc.) the
     * per-glyph fallback chain (CLUT or MONO threshold) is used */
    te_aa_lock_begin(dc, rp);

    penX = pos->x;
    baseY = pos->y;

    p = (const UBYTE *)utf8;
    end = p + strlen((const char *)utf8);

    while (p < end)
    {
        ULONG cp;
        int n;
        struct TEGlyph *g;
        struct TEFont *uf;

        if (maxChars != (ULONG)-1 && count >= maxChars)
            break;

        if (!te_decode_utf8(p, end, &cp, &n))
            break;

        p += n;
        count++;

        if (cp == '\t')
        {
            int adv = (int)dc->tabSpaces * (dc->fonts ? dc->fonts->monospace_advance : 8);

            penX += adv;
            continue;
        }

        /* Drawing newlines is a no-op -- caller positions the next line */
        if (cp == '\n' || cp == '\r')
            continue;

        g = te_glyph_from_chain(dc, cp, &uf);

        if (!g)
        {
            /* Font has no glyph for this codepoint -- not even U+FFFD
             * Advance the pen by the expected visual width so subsequent
             * glyphs stay aligned with the editor's cell grid.  Use 2*fw
             * for codepoints that the editor would have placed as a wide
             * (lead+trailing) cell.  This duplicates the wcswidth wide
             * ranges from ui_editor_helper.c -- keep them in sync */
            int adv = dc->fonts ? dc->fonts->monospace_advance : 8;
            /*int wide = ((cp >= 0x1100 && cp <= 0x115F) ||
                        (cp >= 0x2190 && cp <= 0x21FF) ||
                        (cp >= 0x2600 && cp <= 0x26FF) ||
                        (cp >= 0x2700 && cp <= 0x27BF) ||
                        (cp >= 0x2B00 && cp <= 0x2BFF) ||
                        (cp >= 0x2E80 && cp <= 0xA4CF) ||
                        (cp >= 0xAC00 && cp <= 0xD7A3) ||
                        (cp >= 0xF900 && cp <= 0xFAFF) ||
                        (cp >= 0xFE30 && cp <= 0xFE6F) ||
                        (cp >= 0xFF00 && cp <= 0xFF60) ||
                        (cp >= 0x1F300UL && cp <= 0x1F9FFUL) ||
                        (cp >= 0x20000UL && cp <= 0x3FFFDUL));*/

            wchar_t wc = (wchar_t)cp;
            int wide = (wcswidth(&wc, 1) == 2);

            penX += wide ? (2 * adv) : adv;

            continue;
        }

        te_draw_glyph(dc, rp, g, penX, baseY);

        /* Advance.  FIXEDWIDTH preferred; defensively fall back to
         * monospace_advance if g->advance is zero or absurdly small
         * (some fonts -- notably GNU Unifont in GRAY mode -- have been
         * observed to return 0 for valid glyphs, which would stack every
         * letter on the same X and produce an invisible run). */
        if (dc->prefs & TE_FLAG_FIXEDWIDTH)
        {
            penX += dc->fonts->monospace_advance;
        }
        else if (g->advance > 0)
        {
            penX += g->advance;
        }
        else
        {
            penX += dc->fonts->monospace_advance;
        }
    }

    /* Release the bitmap lock taken at the start of the function */
    te_aa_lock_end(dc);

    pos->x = (WORD)penX;
}

/* Cleanup helper exposed for the app's atexit hook (closes CGX if open) */
void TE_GlobalCleanup(void)
{
    cgx_close();
}
