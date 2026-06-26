/*
 * tinyedit - Text editor for AmigaOS / Windows
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 */

/* te_rastport_win32.h - FreeType text rendering for Windows GDI
 *
 * Windows counterpart of te_rastport.h / te_rastport.c. Loads a chain
 * of TrueType/OpenType fonts and renders UTF-8 text with fallback so
 * emojis/CJK display even if the primary monospace font lacks them.
 */

#ifndef TE_RASTPORT_WIN32_H
#define TE_RASTPORT_WIN32_H

#ifdef PLATFORM_WIN32

#include <windows.h>

/* Preference flags (same values as the Amiga TE_SetFlags) */
#define TE_FLAG_ANTIALIAS (1UL << 0)
#define TE_FLAG_CLUT_NOMASK (1UL << 1)
#define TE_FLAG_HIGHQUALITY (1UL << 2)
#define TE_FLAG_FIXEDWIDTH (1UL << 3)

/* Style flags (same values as the Amiga TE_SetStyle) */
#define TE_STY_NORMAL 0
#define TE_STY_BOLD (1UL << 0)
#define TE_STY_ITALIC (1UL << 1)

/* Opaque rendering context */
struct TERenderContext;

/* Metrics returned by TE_GetMetrics / TE_MeasureText */
struct TEGlyphMetrics
{
    int width;
    int height;
    int baseX;
    int baseY;
};

struct TERenderContext *TE_ContextCreate(void);
void TE_ContextRelease(struct TERenderContext *dc);

/* Font management. Add appends to the fallback chain. Returns 0 on failure */
LONG TE_FontAdd(struct TERenderContext *dc, const char *fontPath, LONG pointSize, ULONG flags);
void TE_FontRemove(struct TERenderContext *dc, const char *fontPath, LONG pointSize);
void TE_FontFlush(struct TERenderContext *dc);
void TE_FontResize(struct TERenderContext *dc, ULONG nPointSize, ULONG fontMask);

/* Preferences and style */
void TE_SetFlags(struct TERenderContext *dc, ULONG flags);
ULONG TE_GetFlags(const struct TERenderContext *dc);
void TE_CacheFlush(struct TERenderContext *dc);
void TE_SetStyle(struct TERenderContext *dc, ULONG styleBits);
void TE_SetTabWidth(struct TERenderContext *dc, ULONG nSpaces);

/* Colours are 0x00RRGGBB packed values. Background is used when a
 * glyph is not RGBA (colour emoji) and has to be blended */
void TE_SetColorRGB(struct TERenderContext *dc, ULONG textRGB, ULONG bgRGB);

/* Measurement */
void TE_GetMetrics(struct TERenderContext *dc, struct TEGlyphMetrics *out);
void TE_MeasureText(struct TERenderContext *dc, const char *utf8, LONG maxChars, struct TEGlyphMetrics *out);
void TE_GetCharOffsets(struct TERenderContext *dc, const char *utf8, LONG maxChars, LONG *arrayout);

/* Drawing to a GDI device context. (x, y) is the baseline origin */
void TE_RenderText(struct TERenderContext *dc, HDC hdc, int x, int y, const char *utf8, ULONG maxChars);

/* Debug control (0=none, 1=error, 2=warn, 3=info, 4=verbose, 5=all) */
void TE_SetDebugLevel(int level);

#endif /* PLATFORM_WIN32 */
#endif /* TE_RASTPORT_WIN32_H */
