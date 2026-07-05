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

#include "grammar_priv.h"

void *gc_arena_alloc(struct gc_arena *a, size_t n)
{
    void *p = NULL;
    size_t aligned;

    if (!a || !a->base)
        return NULL;

    /* 4-byte align — m68k requires it for shorts/longs */
    aligned = (n + 3u) & ~(size_t)3u;

    if (a->used + aligned > a->cap)
        return NULL;

    p = a->base + a->used;
    a->used += aligned;

    memset(p, 0, aligned);
    return p;
}

const char *gc_arena_strdup(struct gc_arena *a, const char *s)
{
    size_t n;

    if (!s)
        return NULL;

    n = strlen(s);
    return gc_arena_strndup(a, s, n);
}

const char *gc_arena_strndup(struct gc_arena *a, const char *s, size_t n)
{
    char *p = NULL;

    if (!s)
        return NULL;

    p = (char *)gc_arena_alloc(a, n + 1);

    if (!p)
        return NULL;

    memcpy(p, s, n);
    p[n] = '\0';

    return p;
}

unsigned long gc_utf8_decode(const char *s, int *bytes_used)
{
    const unsigned char *p = NULL;
    unsigned long cp;
    int need;
    int i;

    if (!s || !*s)
    {
        if (bytes_used)
            *bytes_used = 0;

        return 0;
    }

    p = (const unsigned char *)s;

    if (p[0] < 0x80)
    {
        if (bytes_used)
            *bytes_used = 1;

        return (unsigned long)p[0];
    }

    if ((p[0] & 0xE0) == 0xC0)
    {
        cp = (unsigned long)(p[0] & 0x1F);
        need = 1;
    }
    else if ((p[0] & 0xF0) == 0xE0)
    {
        cp = (unsigned long)(p[0] & 0x0F);
        need = 2;
    }
    else if ((p[0] & 0xF8) == 0xF0)
    {
        cp = (unsigned long)(p[0] & 0x07);
        need = 3;
    }
    else
    {
        /* Invalid leading byte — advance one, report replacement */
        if (bytes_used)
            *bytes_used = 1;

        return 0xFFFD;
    }

    for (i = 1; i <= need; i++)
    {
        if ((p[i] & 0xC0) != 0x80)
        {
            if (bytes_used)
                *bytes_used = 1;

            return 0xFFFD;
        }

        cp = (cp << 6) | (unsigned long)(p[i] & 0x3F);
    }

    if (bytes_used)
        *bytes_used = need + 1;

    return cp;
}

int gc_is_space_cp(unsigned long cp)
{
    if (cp == 0x20 || cp == 0x09 || cp == 0x0A || cp == 0x0D || cp == 0x0C || cp == 0x0B)
        return 1;

    /* NBSP, ZWSP, various Unicode spaces */
    if (cp == 0xA0)
        return 1;

    if (cp == 0x2028 || cp == 0x2029)
        return 1;

    if (cp >= 0x2000 && cp <= 0x200A)
        return 1;

    if (cp == 0x202F || cp == 0x205F || cp == 0x3000)
        return 1;

    return 0;
}

int gc_is_str_quote_cp(unsigned long cp)
{
    if (cp == '"' || cp == 0x201C || cp == 0x201D || cp == 0xAB || cp == 0xBB)
        return 1;

    return 0;
}

int gc_is_char_quote_cp(unsigned long cp)
{
    if (cp == '\'' || cp == 0x2018 || cp == 0x2019)
        return 1;

    return 0;
}

int gc_is_letter_cp(unsigned long cp)
{
    /* ASCII fast-path */
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'))
        return 1;

    /* Latin-1 supplement letters */
    if (cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7)
        return 1;

    /* Latin Extended-A / -B */
    if (cp >= 0x0100 && cp <= 0x024F)
        return 1;

    /* Greek + Cyrillic (basic ranges — good enough for the checker) */
    if (cp >= 0x0370 && cp <= 0x03FF)
        return 1;

    if (cp >= 0x0400 && cp <= 0x04FF)
        return 1;

    /* Armenian */
    if (cp >= 0x0530 && cp <= 0x058F)
        return 1;

    /* Hebrew */
    if (cp >= 0x0590 && cp <= 0x05FF)
        return 1;

    /* Arabic */
    if (cp >= 0x0600 && cp <= 0x06FF)
        return 1;

    /* Syriac */
    if (cp >= 0x0700 && cp <= 0x074F)
        return 1;

    /* Thaana */
    if (cp >= 0x0780 && cp <= 0x07BF)
        return 1;

    /* NKo */
    if (cp >= 0x07C0 && cp <= 0x07FF)
        return 1;

    /* Devanagari + Indic scripts */
    if (cp >= 0x0900 && cp <= 0x097F)
        return 1;

    if (cp >= 0x0980 && cp <= 0x09FF)
        return 1;

    if (cp >= 0x0A00 && cp <= 0x0A7F)
        return 1;

    if (cp >= 0x0A80 && cp <= 0x0AFF)
        return 1;

    if (cp >= 0x0B00 && cp <= 0x0B7F)
        return 1;

    if (cp >= 0x0B80 && cp <= 0x0BFF)
        return 1;

    if (cp >= 0x0C00 && cp <= 0x0C7F)
        return 1;

    if (cp >= 0x0C80 && cp <= 0x0CFF)
        return 1;

    if (cp >= 0x0D00 && cp <= 0x0D7F)
        return 1;

    if (cp >= 0x0D80 && cp <= 0x0DFF)
        return 1;

    /* Thai */
    if (cp >= 0x0E00 && cp <= 0x0E7F)
        return 1;

    /* Lao */
    if (cp >= 0x0E80 && cp <= 0x0EFF)
        return 1;

    /* Tibetan */
    if (cp >= 0x0F00 && cp <= 0x0FFF)
        return 1;

    /* Myanmar */
    if (cp >= 0x1000 && cp <= 0x109F)
        return 1;

    /* Georgian */
    if (cp >= 0x10A0 && cp <= 0x10FF)
        return 1;

    /* Hangul Jamo */
    if (cp >= 0x1100 && cp <= 0x11FF)
        return 1;

    /* Ethiopic */
    if (cp >= 0x1200 && cp <= 0x137F)
        return 1;

    /* Cherokee */
    if (cp >= 0x13A0 && cp <= 0x13FF)
        return 1;

    /* Canadian Aboriginal */
    if (cp >= 0x1400 && cp <= 0x167F)
        return 1;

    /* Ogham */
    if (cp >= 0x1680 && cp <= 0x169F)
        return 1;

    /* Runic */
    if (cp >= 0x16A0 && cp <= 0x16FF)
        return 1;

    /* Tagalog */
    if (cp >= 0x1700 && cp <= 0x171F)
        return 1;

    /* Mongolian */
    if (cp >= 0x1800 && cp <= 0x18AF)
        return 1;

    /* Latin Extended Additional */
    if (cp >= 0x1E00 && cp <= 0x1EFF)
        return 1;

    /* Greek Extended */
    if (cp >= 0x1F00 && cp <= 0x1FFF)
        return 1;

    /* General Punctuation (some are letters in some scripts) */
    if (cp >= 0x2000 && cp <= 0x206F)
        return 0;

    /* Hiragana */
    if (cp >= 0x3040 && cp <= 0x309F)
        return 1;

    /* Katakana */
    if (cp >= 0x30A0 && cp <= 0x30FF)
        return 1;

    /* Bopomofo */
    if (cp >= 0x3100 && cp <= 0x312F)
        return 1;

    /* Hangul Compatibility Jamo */
    if (cp >= 0x3130 && cp <= 0x318F)
        return 1;

    /* CJK Unified Ideographs */
    if (cp >= 0x4E00 && cp <= 0x9FFF)
        return 1;

    /* Hangul Syllables */
    if (cp >= 0xAC00 && cp <= 0xD7AF)
        return 1;

    /* CJK Extensions */
    if (cp >= 0x3400 && cp <= 0x4DBF)
        return 1;

    if (cp >= 0x20000 && cp <= 0x2A6DF)
        return 1;

    if (cp >= 0x2A700 && cp <= 0x2B73F)
        return 1;

    if (cp >= 0x2B740 && cp <= 0x2B81F)
        return 1;

    if (cp >= 0x2B820 && cp <= 0x2CEAF)
        return 1;

    if (cp >= 0x2CEB0 && cp <= 0x2EBEF)
        return 1;

    return 0;
}

int gc_is_upper_cp(unsigned long cp)
{
    if (cp >= 'A' && cp <= 'Z')
        return 1;

    /* Latin-1 uppercase block: 0xC0..0xDE, minus 0xD7 (multiplication sign) */
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)
        return 1;

    /* Latin Extended-A uppercase pattern: even codepoints in 0x0100..0x0137 */
    if (cp >= 0x0100 && cp <= 0x0137 && !(cp & 1))
        return 1;

    /* Latin Extended Additional uppercase */
    if (cp >= 0x1E00 && cp <= 0x1EFF && !(cp & 1))
        return 1;

    /* Greek uppercase */
    if (cp >= 0x0391 && cp <= 0x03A9)
        return 1;

    /* Cyrillic uppercase A-Ya */
    if (cp >= 0x0410 && cp <= 0x042F)
        return 1;

    if (cp >= 0x0400 && cp <= 0x040F)
        return 1;

    /* Armenian uppercase */
    if (cp >= 0x0531 && cp <= 0x0556)
        return 1;

    /* Georgian uppercase */
    if (cp >= 0x10A0 && cp <= 0x10C5)
        return 1;

    return 0;
}

unsigned long gc_tolower_cp(unsigned long cp)
{
    if (cp >= 'A' && cp <= 'Z')
        return cp + 32;

    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)
        return cp + 32;

    if (cp >= 0x0100 && cp <= 0x0137 && !(cp & 1))
        return cp + 1;

    if (cp >= 0x0410 && cp <= 0x042F)
        return cp + 32;

    if (cp >= 0x0400 && cp <= 0x040F)
        return cp + 80;

    /* Latin Extended Additional */
    if (cp >= 0x1E00 && cp <= 0x1EFF && !(cp & 1))
        return cp + 1;

    /* Greek uppercase to lowercase */
    if (cp >= 0x0391 && cp <= 0x03A1 && cp != 0x03A2)
        return cp + 32;

    if (cp >= 0x03A3 && cp <= 0x03A9)
        return cp + 32;

    /* Armenian uppercase to lowercase */
    if (cp >= 0x0531 && cp <= 0x0556)
        return cp + 48;

    /* Georgian uppercase to lowercase */
    if (cp >= 0x10A0 && cp <= 0x10C5)
        return cp + 48;

    return cp;
}

/* Encode a codepoint back into UTF-8. Returns bytes written (1..4) */
static int gc_utf8_encode(unsigned long cp, char *out)
{
    if (cp < 0x80)
    {
        out[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }

    if (cp < 0x10000)
    {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }

    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));

    return 4;
}

int gc_utf8_lower(const char *s, char *out, size_t outsz)
{
    const char *p = NULL;
    int used;
    int written;
    unsigned long cp;
    unsigned long lo;
    int n;

    written = 0;

    if (!s || !out || outsz == 0)
        return 0;

    p = s;

    while (*p)
    {
        char tmp[4];

        cp = gc_utf8_decode(p, &used);

        if (used <= 0)
            break;

        lo = gc_tolower_cp(cp);

        n = gc_utf8_encode(lo, tmp);

        if ((size_t)(written + n + 1) > outsz)
            break;

        memcpy(out + written, tmp, (size_t)n);
        written += n;

        p += used;
    }

    out[written] = '\0';
    return written;
}

int gc_charset_contains(const char *charset, unsigned long cp)
{
    const char *p = NULL;
    int used;
    unsigned long c;

    if (!charset)
        return 0;

    p = charset;

    while (*p)
    {
        c = gc_utf8_decode(p, &used);

        if (c == cp)
            return 1;

        if (used <= 0)
            break;

        p += used;
    }

    return 0;
}

unsigned long gc_hash32(const char *s, size_t n)
{
    /* FNV-1a 32-bit */
    unsigned long h;
    size_t i;

    h = 2166136261;

    if (!s)
        return h;

    for (i = 0; i < n; i++)
    {
        h ^= (unsigned long)(unsigned char)s[i];
        h *= 16777619;
    }

    return h;
}

unsigned long gc_hash32b(const char *s, size_t n)
{
    /* djb2 */
    unsigned long h;
    size_t i;

    h = 5381;

    if (!s)
        return h;

    for (i = 0; i < n; i++)
        h = ((h << 5) + h) + (unsigned long)(unsigned char)s[i];

    return h;
}
