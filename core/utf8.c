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

/* utf8.c -- UTF-8 utilities */
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include <stdio.h>
#include "utf8.h"

static const uint16_t cp437_map[128] =
    {
        0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
        0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
        0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
        0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
        0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
        0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
        0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
        0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
        0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
        0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
        0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
        0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
        0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
        0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

static const uint16_t cp850_map[128] =
    {
        0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
        0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
        0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
        0x00FF, 0x00D6, 0x00DC, 0x00F8, 0x00A3, 0x00D8, 0x00D7, 0x0192,
        0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
        0x00BF, 0x00AE, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x00C1, 0x00C2, 0x00C0,
        0x00A9, 0x2563, 0x2551, 0x2557, 0x255D, 0x00A2, 0x00A5, 0x2510,
        0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x00E3, 0x00C3,
        0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x00A4,
        0x00F0, 0x00D0, 0x00CA, 0x00CB, 0x00C8, 0x0131, 0x00CD, 0x00CE,
        0x00CF, 0x2518, 0x250C, 0x2588, 0x2584, 0x00A6, 0x00CC, 0x2580,
        0x00D3, 0x00DF, 0x00D4, 0x00D2, 0x00F5, 0x00D5, 0x00B5, 0x00FE,
        0x00DE, 0x00DA, 0x00DB, 0x00D9, 0x00FD, 0x00DD, 0x00AF, 0x00B4,
        0x00AD, 0x00B1, 0x2017, 0x00BE, 0x00B6, 0x00A7, 0x00F7, 0x00B8,
        0x00B0, 0x00A8, 0x00B7, 0x00B9, 0x00B3, 0x00B2, 0x25A0, 0x00A0};

static const uint16_t cp865_map[128] =
    {
        0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
        0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
        0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
        0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
        0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
        0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
        0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
        0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
        0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
        0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
        0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
        0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
        0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
        0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

static const uint16_t cp866_map[128] =
    {
        0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
        0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
        0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
        0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
        0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
        0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
        0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
        0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
        0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
        0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
        0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
        0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
        0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040E, 0x045E,
        0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0};

static const uint16_t cp1252_map[128] =
    {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
        0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
        0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
        0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
        0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
        0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
        0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
        0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
        0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
        0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
        0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
        0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
        0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF};

static const uint16_t latin2_map[128] =
    {
        0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
        0x0088, 0x0089, 0x008A, 0x008B, 0x008C, 0x008D, 0x008E, 0x008F,
        0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
        0x0098, 0x0099, 0x009A, 0x009B, 0x009C, 0x009D, 0x009E, 0x009F,
        0x00A0, 0x0104, 0x02D8, 0x0141, 0x00A4, 0x013D, 0x015A, 0x00A7,
        0x00A8, 0x0160, 0x015E, 0x0164, 0x0179, 0x00AD, 0x017D, 0x017B,
        0x00B0, 0x0105, 0x02DB, 0x0142, 0x00B4, 0x013E, 0x015B, 0x02C7,
        0x00B8, 0x0161, 0x015F, 0x0165, 0x017A, 0x02DD, 0x017E, 0x017C,
        0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
        0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
        0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
        0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
        0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
        0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
        0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
        0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9};

int utf8_strlen(const char *s)
{
    int n = 0;

    if (!s)
        return 0;

    while (*s)
    {
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;

        s++;
    }

    return n;
}

int utf8_offset(const char *s, int charpos)
{
    const char *p = s;
    int ch = 0;

    if (!s || charpos <= 0)
        return 0;

    while (*p)
    {
        if (((unsigned char)*p & 0xC0) != 0x80)
        {
            if (ch == charpos)
                return (int)(p - s);

            ch++;
        }

        p++;
    }

    return (int)(p - s); /* past end */
}

uint32_t utf8_next(const char **pp)
{
    const unsigned char *s = (const unsigned char *)*pp;
    uint32_t cp;
    int need, got;

    if (!s || !*s)
        return 0;

    if (*s < 0x80)
    {
        *pp = (const char *)(s + 1);
        return *s;
    }

    if ((*s & 0xE0) == 0xC0)
    {
        cp = *s & 0x1F;
        need = 1;
    }
    else if ((*s & 0xF0) == 0xE0)
    {
        cp = *s & 0x0F;
        need = 2;
    }
    else if ((*s & 0xF8) == 0xF0)
    {
        cp = *s & 0x07;
        need = 3;
    }
    else
    {
        *pp = (const char *)(s + 1);
        return 0xFFFD;
    } /* invalid lead */

    s++;

    for (got = 0; got < need; got++)
    {
        if (!*s || (*s & 0xC0) != 0x80)
            break; /* truncated */

        cp = (cp << 6) | (*s & 0x3F);
        s++;
    }

    if (got < need)
        cp = 0xFFFD; /* incomplete sequence */

    *pp = (const char *)s;

    return cp;
}

int utf8_encode(uint32_t cp, char *buf)
{
    if (cp < 0x80)
    {
        buf[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }

    if (cp < 0x10000)
    {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }

    if (cp < 0x110000)
    {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }

    buf[0] = '?';

    return 1; /* out of Unicode range */
}

int latin1_to_utf8(const char *src, int srclen, char *dst, int dstmax)
{
    const unsigned char *s = (const unsigned char *)src;
    int si = 0, di = 0;

    if (!src || !dst || dstmax <= 0)
    {
        if (dst)
            dst[0] = '\0';

        return 0;
    }

    while (si < srclen && di < dstmax - 4)
        di += utf8_encode((uint32_t)s[si++], &dst[di]);

    dst[di] = '\0';

    return di;
}

int utf8_to_latin1(const char *src, int srclen, char *dst, int dstmax)
{
    const char *p;
    const char *end;
    int di = 0;

    if (!src || !dst || dstmax <= 0)
    {
        if (dst)
            dst[0] = '\0';

        return 0;
    }

    p = src;
    end = src + srclen;

    while (p < end && di < dstmax - 1)
    {
        uint32_t cp = utf8_next(&p);

        if (cp == 0)
            break;

        dst[di++] = (cp <= 0xFF) ? (char)cp : '?';
    }

    dst[di] = '\0';

    return di;
}

uint32_t cp437_to_unicode(unsigned char c) { return c < 128 ? (uint32_t)c : (uint32_t)cp437_map[c - 128]; }

uint32_t cp850_to_unicode(unsigned char c) { return c < 128 ? (uint32_t)c : (uint32_t)cp850_map[c - 128]; }

uint32_t cp865_to_unicode(unsigned char c) { return c < 128 ? (uint32_t)c : (uint32_t)cp865_map[c - 128]; }

uint32_t cp866_to_unicode(unsigned char c) { return c < 128 ? (uint32_t)c : (uint32_t)cp866_map[c - 128]; }

uint32_t cp1252_to_unicode(unsigned char c) { return c < 128 ? (uint32_t)c : (uint32_t)cp1252_map[c - 128]; }

uint32_t latin2_to_unicode(unsigned char c) { return c < 128 ? (uint32_t)c : (uint32_t)latin2_map[c - 128]; }

/* Charset-name-aware single-byte -> Unicode. See utf8.h doc comment
 * Order of strcasecmp checks mirrors charset_to_utf8 for consistency */
uint32_t charset_byte_to_unicode(const char *cs, unsigned char b)
{
    if (b < 0x80)
        return (uint32_t)b;

    if (!cs || !cs[0])
        return (uint32_t)b; /* identity = LATIN-1-ish */

    if (strcasecmp(cs, "CP437") == 0 || strcasecmp(cs, "IBMPC") == 0 || strcasecmp(cs, "PC-8") == 0)
        return cp437_to_unicode(b);

    if (strcasecmp(cs, "CP850") == 0)
        return cp850_to_unicode(b);

    if (strcasecmp(cs, "CP865") == 0)
        return cp865_to_unicode(b);

    if (strcasecmp(cs, "CP866") == 0)
        return cp866_to_unicode(b);

    if (strcasecmp(cs, "CP1252") == 0 || strcasecmp(cs, "WINDOWS-1252") == 0)
        return cp1252_to_unicode(b);

    if (strcasecmp(cs, "LATIN-2") == 0 || strcasecmp(cs, "ISO-8859-2") == 0)
        return latin2_to_unicode(b);

    /* LATIN-1, ISO-8859-1, or unknown: identity mapping (high bytes
     * 0xA0-0xFF are already valid Unicode code points) */
    return (uint32_t)b;
}

unsigned char unicode_to_cp437(uint32_t cp)
{
    int i;

    if (cp < 128)
        return (unsigned char)cp;

    for (i = 0; i < 128; i++)
    {
        if (cp437_map[i] == (uint16_t)cp)
            return (unsigned char)(i + 128);
    }

    return 0;
}

unsigned char unicode_to_cp850(uint32_t cp)
{
    int i;

    if (cp < 128)
        return (unsigned char)cp;

    for (i = 0; i < 128; i++)
    {
        if (cp850_map[i] == (uint16_t)cp)
            return (unsigned char)(i + 128);
    }

    return 0;
}

unsigned char unicode_to_cp865(uint32_t cp)
{
    int i;

    if (cp < 128)
        return (unsigned char)cp;

    for (i = 0; i < 128; i++)
    {
        if (cp865_map[i] == (uint16_t)cp)
            return (unsigned char)(i + 128);
    }

    return 0;
}

unsigned char unicode_to_cp866(uint32_t cp)
{
    int i;

    if (cp < 128)
        return (unsigned char)cp;

    for (i = 0; i < 128; i++)
    {
        if (cp866_map[i] == (uint16_t)cp)
            return (unsigned char)(i + 128);
    }

    return 0;
}

unsigned char unicode_to_cp1252(uint32_t cp)
{
    int i;

    if (cp < 128)
        return (unsigned char)cp;

    for (i = 0; i < 128; i++)
    {
        if (cp1252_map[i] == (uint16_t)cp)
            return (unsigned char)(i + 128);
    }

    return 0;
}

unsigned char unicode_to_latin2(uint32_t cp)
{
    int i;

    if (cp < 128)
        return (unsigned char)cp;

    for (i = 0; i < 128; i++)
    {
        if (latin2_map[i] == (uint16_t)cp)
            return (unsigned char)(i + 128);
    }

    return 0;
}

static int cp_to_utf8(const uint16_t *map, const char *src, int sl, char *dst, int dm)
{
    const unsigned char *s = (const unsigned char *)src;
    int si = 0, di = 0;

    if (!src || !dst || dm <= 0)
    {
        if (dst)
            dst[0] = '\0';

        return 0;
    }

    while (si < sl && di < dm - 4)
    {
        uint32_t cp = s[si] < 128 ? (uint32_t)s[si] : (uint32_t)map[s[si] - 128];

        di += utf8_encode(cp, &dst[di]);
        si++;
    }

    dst[di] = '\0';

    return di;
}

static int utf8_to_cp(const uint16_t *map, const char *src, int sl, char *dst, int dm)
{
    const char *p;
    const char *end;
    int di = 0, i;

    if (!src || !dst || dm <= 0)
    {
        if (dst)
            dst[0] = '\0';

        return 0;
    }

    p = src;
    end = src + sl;

    while (p < end && di < dm - 1)
    {
        uint32_t cp = utf8_next(&p);

        if (!cp)
            break;

        if (cp < 128)
        {
            dst[di++] = (char)cp;
            continue;
        }

        for (i = 0; i < 128; i++)
        {
            if (map[i] == (uint16_t)cp)
            {
                dst[di++] = (char)(i + 128);
                break;
            }
        }

        if (i == 128)
            dst[di++] = '?';
    }

    dst[di] = '\0';

    return di;
}

int charset_to_utf8(const char *cs, const char *src, int sl, char *dst, int dm)
{
    if (!cs || !src || dm <= 0)
    {
        if (dst)
            dst[0] = '\0';

        return 0;
    }

    if (strcasecmp(cs, "UTF-8") == 0 || strcasecmp(cs, "UTF8") == 0)
    {
        int n = sl < dm - 1 ? sl : dm - 1;

        memcpy(dst, src, (size_t)n);
        dst[n] = '\0';

        return n;
    }

    if (strcasecmp(cs, "CP437") == 0 || strcasecmp(cs, "IBMPC") == 0 || strcasecmp(cs, "PC-8") == 0)
        return cp_to_utf8(cp437_map, src, sl, dst, dm);

    if (strcasecmp(cs, "CP850") == 0)
        return cp_to_utf8(cp850_map, src, sl, dst, dm);

    if (strcasecmp(cs, "CP865") == 0)
        return cp_to_utf8(cp865_map, src, sl, dst, dm);

    if (strcasecmp(cs, "CP866") == 0)
        return cp_to_utf8(cp866_map, src, sl, dst, dm);

    if (strcasecmp(cs, "CP1252") == 0 || strcasecmp(cs, "WINDOWS-1252") == 0)
        return cp_to_utf8(cp1252_map, src, sl, dst, dm);

    if (strcasecmp(cs, "LATIN-2") == 0 || strcasecmp(cs, "ISO-8859-2") == 0)
        return cp_to_utf8(latin2_map, src, sl, dst, dm);

    return latin1_to_utf8(src, sl, dst, dm); /* default: Latin-1 */
}

int utf8_to_charset(const char *cs, const char *src, int sl, char *dst, int dm)
{
    if (!cs || !src || dm <= 0)
    {
        if (dst)
            dst[0] = '\0';

        return 0;
    }

    if (strcasecmp(cs, "UTF-8") == 0 || strcasecmp(cs, "UTF8") == 0)
    {
        int n = sl < dm - 1 ? sl : dm - 1;

        memcpy(dst, src, (size_t)n);
        dst[n] = '\0';

        return n;
    }

    if (strcasecmp(cs, "CP437") == 0 || strcasecmp(cs, "IBMPC") == 0 || strcasecmp(cs, "PC-8") == 0)
        return utf8_to_cp(cp437_map, src, sl, dst, dm);

    if (strcasecmp(cs, "CP850") == 0)
        return utf8_to_cp(cp850_map, src, sl, dst, dm);

    if (strcasecmp(cs, "CP865") == 0)
        return utf8_to_cp(cp865_map, src, sl, dst, dm);

    if (strcasecmp(cs, "CP866") == 0)
        return utf8_to_cp(cp866_map, src, sl, dst, dm);

    if (strcasecmp(cs, "CP1252") == 0 || strcasecmp(cs, "WINDOWS-1252") == 0)
        return utf8_to_cp(cp1252_map, src, sl, dst, dm);

    if (strcasecmp(cs, "LATIN-2") == 0 || strcasecmp(cs, "ISO-8859-2") == 0)
        return utf8_to_cp(latin2_map, src, sl, dst, dm);

    return utf8_to_latin1(src, sl, dst, dm);
}

wchar_t *utf8_to_wcs(const char *utf8, int *out_len)
{
    const char *p;
    wchar_t *wcs, *tmp;
    int cap, len;
    uint32_t cp;

    if (!utf8 || !*utf8)
    {
        wcs = (wchar_t *)malloc(sizeof(wchar_t));

        if (wcs)
            wcs[0] = L'\0';

        if (out_len)
            *out_len = 0;

        return wcs;
    }

    cap = (int)strlen(utf8) + 1;
    wcs = (wchar_t *)malloc((size_t)cap * sizeof(wchar_t));

    if (!wcs)
        return NULL;

    p = utf8;
    len = 0;

    while (*p)
    {
        cp = utf8_next(&p);

        if (cp == 0)
            break;

        if (len >= cap - 1)
        {
            cap *= 2;
            tmp = (wchar_t *)realloc(wcs, (size_t)cap * sizeof(wchar_t));

            if (!tmp)
            {
                free(wcs);
                return NULL;
            }

            wcs = tmp;
        }

        wcs[len++] = (wchar_t)cp;
    }

    wcs[len] = L'\0';

    if (out_len)
        *out_len = len;

    return wcs;
}

char *wcs_to_utf8(const wchar_t *wcs, int len)
{
    char *utf8, *p;
    size_t cap, i;

    if (len < 0)
        return NULL;

    /* Check for overflow: len * 4 must fit in size_t */
    if (len > 0 && (size_t)len > (SIZE_MAX - 1) / 4)
        return NULL;

    cap = (size_t)len * 4 + 1;
    utf8 = (char *)malloc(cap);

    if (!utf8)
        return NULL;

    p = utf8;

    for (i = 0; i < (size_t)len; i++)
        p += utf8_encode((uint32_t)wcs[i], p);

    *p = '\0';

    return utf8;
}

const wchar_t *wcs_casestr(const wchar_t *hay, const wchar_t *needle)
{
    int nl, hl, i, j;

    if (!needle || !needle[0])
        return hay;

    if (!hay)
        return NULL;

    nl = (int)wcslen(needle);
    hl = (int)wcslen(hay);

    if (nl > hl)
        return NULL;

    for (i = 0; i <= hl - nl; i++)
    {
        for (j = 0; j < nl; j++)
        {
            if (towlower(hay[i + j]) != towlower(needle[j]))
                break;
        }

        if (j == nl)
            return hay + i;
    }

    return NULL;
}

#if (defined(PLATFORM_AMIGA) || defined(PLATFORM_WIN32)) && !defined(wcswidth)

/* wcswidth implementation based on Markus Kuhn's wcwidth.c
 * https://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 * Returns number of column positions needed for wide-character string
 * Compatible with POSIX.1-2001 standard for Unicode terminal display
 * Uses Unicode ranges for consistency across platforms (more reliable than font measurement)
 */
int wcswidth(const wchar_t *wcs, size_t n)
{
    int width = 0;
    size_t i;

    for (i = 0; i < n && wcs[i] != L'\0'; i++)
    {
        /* East Asian Width: wide/full-width/emoji = 2 columns, others = 1 */
        if ((wcs[i] >= 0x1100 && wcs[i] <= 0x115F) ||   /* Hangul Jamo */
            (wcs[i] >= 0x2190 && wcs[i] <= 0x21FF) ||   /* Arrows */
            (wcs[i] >= 0x2600 && wcs[i] <= 0x26FF) ||   /* Miscellaneous Symbols */
            (wcs[i] >= 0x2700 && wcs[i] <= 0x27BF) ||   /* Dingbats */
            (wcs[i] >= 0x2B00 && wcs[i] <= 0x2BFF) ||   /* Misc Symbols and Arrows */
            (wcs[i] >= 0x2E80 && wcs[i] <= 0xA4CF) ||   /* CJK...Yi */
            (wcs[i] >= 0xAC00 && wcs[i] <= 0xD7A3) ||   /* Hangul Syllables */
            (wcs[i] >= 0xF900 && wcs[i] <= 0xFAFF) ||   /* CJK Compatibility */
            (wcs[i] >= 0xFE30 && wcs[i] <= 0xFE6F) ||   /* CJK Compatibility Forms */
            (wcs[i] >= 0xFF00 && wcs[i] <= 0xFF60) ||   /* Fullwidth Forms */
            (wcs[i] >= 0x1F300 && wcs[i] <= 0x1F5FF) || /* Misc Symbols & Pictographs */
            (wcs[i] >= 0x1F600 && wcs[i] <= 0x1F64F) || /* Emoticons */
            (wcs[i] >= 0x1F680 && wcs[i] <= 0x1F6FF) || /* Transport & Map */
            (wcs[i] >= 0x1F700 && wcs[i] <= 0x1F77F) || /* Alchemical Symbols */
            (wcs[i] >= 0x1F780 && wcs[i] <= 0x1F7FF) || /* Geometric Shapes Extended */
            (wcs[i] >= 0x1F800 && wcs[i] <= 0x1F8FF) || /* Supplemental Arrows-C */
            (wcs[i] >= 0x1F900 && wcs[i] <= 0x1F9FF) || /* Supplemental Symbols */
            (wcs[i] >= 0x20000 && wcs[i] <= 0x2FFFD) || /* Supplementary Planes */
            (wcs[i] >= 0x30000 && wcs[i] <= 0x3FFFD))   /* Supplementary Ideographic */
            width += 2;
        else
            width += 1;
    }

    return width;
}

#endif
