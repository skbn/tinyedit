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

#ifndef TE_RASTPORT_H
#define TE_RASTPORT_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>

struct Screen;

/* Preference flags  (TE_SetFlags) */
#define TE_FLAG_ANTIALIAS (1UL << 0)
#define TE_FLAG_CLUT_NOMASK (1UL << 1)
#define TE_FLAG_HIGHQUALITY (1UL << 2)
#define TE_FLAG_FIXEDWIDTH (1UL << 3)

/* Style flags  (TE_SetStyle) */
#define TE_STY_NORMAL 0
#define TE_STY_BOLD (1UL << 0)
#define TE_STY_ITALIC (1UL << 1)

/* Metric / position structs */
struct TEGlyphMetrics
{
    WORD width;
    WORD height;
    WORD baseX;
    WORD baseY;
};

struct TEDrawPosition
{
    WORD x;
    WORD y;
};

/* Opaque DrawContext */
struct TERenderContext;

struct TERenderContext *TE_ContextCreate(void);
void TE_ContextRelease(struct TERenderContext *dc);

/* Font management.  AddFont appends to the fallback chain */
LONG TE_FontAdd(struct TERenderContext *dc, CONST_STRPTR fontPath, LONG pointSize, ULONG flags);
void TE_FontRemove(struct TERenderContext *dc, CONST_STRPTR fontPath, LONG pointSize);
void TE_FontFlush(struct TERenderContext *dc);
void TE_FontResize(struct TERenderContext *dc, ULONG nPointSize, ULONG fontMask);

/* Preferences and style */
void TE_SetFlags(struct TERenderContext *dc, ULONG flags);
ULONG TE_GetFlags(const struct TERenderContext *dc);
void TE_CacheFlush(struct TERenderContext *dc);
void TE_SetStyle(struct TERenderContext *dc, ULONG styleBits);
void TE_SetTabWidth(struct TERenderContext *dc, ULONG nSpaces);

/* Colour txtRGB / bgRGB are 0x00RRGGBB packed values
 * Use ~0UL for a transparent background on RTG screens */
void TE_SetColorRGB(struct TERenderContext *dc, ULONG textRGB, ULONG bgRGB);
void TE_SetColorPen(struct TERenderContext *dc, struct Screen *screen, LONG txtPen, LONG bgPen);

/* Measurement */
void TE_GetMetrics(struct TERenderContext *dc, struct TEGlyphMetrics *out);
void TE_MeasureText(struct TERenderContext *dc, CONST_STRPTR utf8, LONG maxChars, struct TEGlyphMetrics *out);
void TE_GetCharOffsets(struct TERenderContext *dc, CONST_STRPTR utf8, LONG maxChars, LONG *arrayout);

/* Drawing */
void TE_RenderText(struct RastPort *rp, struct TERenderContext *dc, struct TEDrawPosition *pos, CONST_STRPTR utf8, ULONG maxChars);

/* Screen / palette association. Required before drawing
 * GRAY or RGBA glyphs on indexed-colour screens */
void TE_SetScreen(struct TERenderContext *dc, struct Screen *screen);
void TE_UpdatePalette(struct TERenderContext *dc, struct Screen *screen);

/* Optional global cleanup. Closes CyberGraphX if the engine opened it
 * Call once at app shutdown after the last TE_ContextRelease() */
void TE_GlobalCleanup(void);

/* Debug control - set debug level (0=none, 1=error, 2=warn, 3=info, 4=verbose, 5=all) */
void TE_SetDebugLevel(int level);

#endif /* TE_RASTPORT_H */
