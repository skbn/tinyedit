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

/*
 * Function call offsets are taken from ttengine-7.2/Developer/fd/ttengine_lib.fd
 * The LP1/LP2/LP3/LPnNR macros come from Bebbo's <inline/macros.h>
 */

#ifndef TTENGINE_AMIGA_H
#define TTENGINE_AMIGA_H

#ifdef PLATFORM_AMIGA

#include <exec/types.h>
#include <exec/libraries.h>
#include <utility/tagitem.h>
#include <graphics/rastport.h>
#include <inline/macros.h>

/* Tags (from libraries/ttengine.h v7.2) */

#define TT_FontFile 0x6EDA0000 /* STRPTR: path to .ttf */
#define TT_FontStyle 0x6EDA0001
#define TT_FontStyle_Regular 0
#define TT_FontStyle_Italic 1
#define TT_FontSize 0x6EDA0003 /* ULONG: point size */
#define TT_FontWeight 0x6EDA0004
#define TT_FontWeight_Normal 400
#define TT_FontWeight_Bold 700
#define TT_FontAscender 0x6EDA0008         /* ULONG (out): design ascender (caps without accents) */
#define TT_FontDescender 0x6EDA0009        /* LONG (out, negative): design descender */
#define TT_FontMaxTop 0x6EDA001E           /* ULONG (out): topmost pixel of ANY glyph in face */
#define TT_FontMaxBottom 0x6EDA001F        /* ULONG (out): bottommost pixel of ANY glyph */
#define TT_FontRealAscender 0x6EDA0021     /* ULONG: max ascender across glyphs */
#define TT_FontRealDescender 0x6EDA0022    /* ULONG: max descender across glyphs */
#define TT_FontAccentedAscender 0x6EDA0023 /* ULONG: includes accents (Á, É, etc.) */
#define TT_FontFixedWidth 0x6EDA0026       /* BOOL (out, GetAttrs after OpenFont): monospaced? */
#define TT_FontHeight 0x6EDA0027           /* ULONG: AccentedAscender + RealDescender */
#define TT_FontWidth 0x6EDA0028            /* ULONG: monospace advance (0 if proportional) */
#define TT_Antialias 0x6EDA000F
#define TT_Antialias_Auto 0
#define TT_Antialias_Off 1
#define TT_Antialias_On 2
#define TT_Encoding 0x6EDA0010
#define TT_Encoding_Default 0     /* matches font's native encoding */
#define TT_Encoding_System 1      /* 8-bit, system locale */
#define TT_Encoding_UTF16_BE 1013 /* 16-bit big-endian = UWORD[] on m68k */
#define TT_Encoding_UTF8 106      /* alternative: UTF-8 byte stream */
#define TT_Window 0x6EDA0007      /* binds font to a window's colormap */

/* Library base */
/* The user (tinyedit) must define this once and OpenLibrary it */
extern struct Library *TTEngineBase;

/* Function call macros (m68k LP* inline assembly) */

#define TT_OpenFontA(taglist) \
    LP1(0x1e, APTR, TT_OpenFontA, struct TagItem *, taglist, a0, , TTEngineBase)

#define TT_SetFont(rp, font) \
    LP2(0x24, BOOL, TT_SetFont, struct RastPort *, rp, a1, APTR, font, a0, , TTEngineBase)

#define TT_CloseFont(font) \
    LP1NR(0x2a, TT_CloseFont, APTR, font, a0, , TTEngineBase)

#define TT_Text(rp, string, count) \
    LP3NR(0x30, TT_Text, struct RastPort *, rp, a1, APTR, string, a0, ULONG, count, d0, , TTEngineBase)

#define TT_SetAttrsA(rp, taglist) \
    LP2(0x36, ULONG, TT_SetAttrsA, struct RastPort *, rp, a1, struct TagItem *, taglist, a0, , TTEngineBase)

#define TT_GetAttrsA(rp, taglist) \
    LP2(0x3c, ULONG, TT_GetAttrsA, struct RastPort *, rp, a1, struct TagItem *, taglist, a0, , TTEngineBase)

#define TT_TextLength(rp, string, count) \
    LP3(0x42, ULONG, TT_TextLength, struct RastPort *, rp, a1, APTR, string, a0, ULONG, count, d0, , TTEngineBase)

#define TT_DoneRastPort(rp) \
    LP1NR(0x60, TT_DoneRastPort, struct RastPort *, rp, a1, , TTEngineBase)

/* Stdarg varargs wrappers, so callers can write
 * TT_OpenFont(TT_FontFile, "FONTS:_ttf/DejaVuSansMono.ttf", TT_FontSize, 14, TAG_END);
 * exactly like with the SDK */
#ifndef NO_INLINE_STDARG
#define TT_OpenFont(tags...) \
    ({ ULONG _tags[] = { tags }; TT_OpenFontA((struct TagItem *)_tags); })

#define TT_SetAttrs(rp, tags...) \
    ({ ULONG _tags[] = { tags }; TT_SetAttrsA((rp), (struct TagItem *)_tags); })

#define TT_GetAttrs(rp, tags...) \
    ({ ULONG _tags[] = { tags }; TT_GetAttrsA((rp), (struct TagItem *)_tags); })
#endif

#endif /* PLATFORM_AMIGA */
#endif /* TTENGINE_AMIGA_H */
