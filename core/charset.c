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

/* charset.c -- FTN charset detection */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "charset.h"
#include "utf8.h"

static const struct
{
    const char *alias;
    const char *canon;
} s_aliases[] =
    {
        {"ASCII", "CP437"},
        {"US-ASCII", "CP437"},
        {"IBMPC", "CP437"},
        {"PC-8", "CP437"},
        {"IBM437", "CP437"},
        {"CP-437", "CP437"},
        {"IBM850", "CP850"},
        {"CP-850", "CP850"},
        {"LATIN1", "LATIN-1"},
        {"LATIN-1", "LATIN-1"},
        {"ISO-8859-1", "LATIN-1"},
        {"ISO8859-1", "LATIN-1"},
        {"ISO_8859-1", "LATIN-1"},
        {"ISO8859_1", "LATIN-1"},
        {"UTF8", "UTF-8"},
        {"CP865", "CP865"},
        {"CP-865", "CP865"},
        {"CP866", "CP866"},
        {"CP-866", "CP866"},
        {"CP1252", "CP1252"},
        {"WINDOWS-1252", "CP1252"},
        {"LATIN2", "LATIN-2"},
        {"LATIN-2", "LATIN-2"},
        {"ISO-8859-2", "LATIN-2"},
        {"ISO8859-2", "LATIN-2"},
        {"ISO_8859-2", "LATIN-2"},
        {"ISO8859_2", "LATIN-2"},
        {NULL, NULL}};

const char *charset_resolve(const char *name)
{
    int i;

    if (!name || !name[0])
        return CHARSET_DEFAULT;

    for (i = 0; s_aliases[i].alias; i++)
    {
        if (strcasecmp(name, s_aliases[i].alias) == 0)
            return s_aliases[i].canon;
    }

    return name;
}

void charset_detect(const char *body, char *out, int outsz)
{
    const char *p = NULL;

    if (!out || outsz <= 0)
        return;

    strncpy(out, CHARSET_READ_DEFAULT, (size_t)(outsz - 1));

    out[outsz - 1] = '\0';

    if (!body)
        return;

    for (p = body; *p;)
    {
        if ((unsigned char)*p == 0x01)
        {
            const char *kw = p + 1;

            int is_chrs = (strncasecmp(kw, "CHRS:", 5) == 0 || strncasecmp(kw, "CHRS ", 5) == 0);
            int is_charset = (!is_chrs && strncasecmp(kw, "CHARSET:", 8) == 0);

            if (is_chrs || is_charset)
            {
                const char *s = kw + (is_chrs ? 5 : 8);
                char name[CHARSET_NAME_MAX];
                int i = 0;

                while (*s == ' ' || *s == '\t')
                    s++;

                while (*s && *s != ' ' && *s != '\r' && *s != '\n' && i < CHARSET_NAME_MAX - 1)
                    name[i++] = *s++;

                name[i] = '\0';

                if (i > 0)
                {
                    strncpy(out, charset_resolve(name), (size_t)(outsz - 1));
                    out[outsz - 1] = '\0';
                }

                return;
            }
        }

        /* Skip to next line (handle CR, LF, CRLF) */
        while (*p && *p != '\r' && *p != '\n')
            p++;

        if (*p == '\r')
            p++;

        if (*p == '\n')
            p++;
    }
}

int charset_body_to_utf8(const char *enc, const char *src, int srclen, char *dst, int dstsz)
{
    if (!src || !dst || dstsz <= 0)
    {
        if (dst)
            dst[0] = '\0';

        return 0;
    }

    return charset_to_utf8(enc ? enc : CHARSET_READ_DEFAULT, src, srclen, dst, dstsz);
}

int charset_body_from_utf8(const char *enc, const char *src, int srclen, char *dst, int dstsz)
{
    if (!src || !dst || dstsz <= 0)
    {
        if (dst)
            dst[0] = '\0';

        return 0;
    }

    return utf8_to_charset(enc ? enc : CHARSET_WRITE_DEFAULT, src, srclen, dst, dstsz);
}

int charset_bits(const char *name)
{
    const char *cs = NULL;
    int i;
    static const char *single_byte[] =
        {
            "CP437", "CP850", "CP865", "CP866", "CP1252",
            "LATIN-1", "LATIN-2", "ISO-8859-1", "ISO-8859-2", NULL};

    if (!name || !name[0])
        return -1;

    cs = charset_resolve(name);

    if (!cs || !cs[0])
        return -1;

    /* UTF-8 is variable-length multi-byte */
    if (strcasecmp(cs, "UTF-8") == 0 || strcasecmp(cs, "UTF8") == 0)
        return 0;

    /* Other UTF variants (UTF-16, UTF-32, UTF-7, etc) */
    if (strncasecmp(cs, "UTF", 3) == 0)
    {
        if (strstr(cs, "16"))
            return 16;

        if (strstr(cs, "32"))
            return 32;

        return 0;
    }

    /* UCS-2 / UCS-4 */
    if (strncasecmp(cs, "UCS", 3) == 0)
    {
        if (strstr(cs, "2") || strstr(cs, "16"))
            return 16;

        if (strstr(cs, "4") || strstr(cs, "32"))
            return 32;

        return 0;
    }

    if (strcasecmp(cs, "UNICODE") == 0)
        return 0;

    /* Known single-byte charsets (canonical names after charset_resolve) */
    for (i = 0; single_byte[i]; i++)
    {
        if (strcasecmp(cs, single_byte[i]) == 0)
            return 8;
    }

    /* Heuristics for other common single-byte names */
    if (cs[0] == 'C' && cs[1] == 'P' && isdigit((unsigned char)cs[2]))
        return 8;

    if (strncasecmp(cs, "LATIN", 5) == 0)
        return 8;

    if (strncasecmp(cs, "ISO-8859", 8) == 0)
        return 8;

    if (strncasecmp(cs, "WINDOWS-125", 11) == 0)
        return 8;

    if (strncasecmp(cs, "MAC", 3) == 0)
        return 8;

    return -1;
}

int charset_count_lossy(const char *utf8, const char *out_cs)
{
    int srclen, dstsz, n, i;
    char *tmp = NULL;
    int src_q = 0, dst_q = 0;

    if (!utf8 || !out_cs || !out_cs[0])
        return 0;

    if (strcasecmp(out_cs, "UTF-8") == 0 || strcasecmp(out_cs, "UTF8") == 0)
        return 0;

    srclen = (int)strlen(utf8);
    dstsz = srclen + 16;
    tmp = (char *)malloc((size_t)dstsz);

    if (!tmp)
        return 0;

    n = charset_body_from_utf8(out_cs, utf8, srclen, tmp, dstsz);

    for (i = 0; i < srclen; i++)
    {
        if (utf8[i] == '?')
            src_q++;
    }

    for (i = 0; i < n; i++)
    {
        if (tmp[i] == '?')
            dst_q++;
    }

    free(tmp);

    return (dst_q > src_q) ? (dst_q - src_q) : 0;
}

void charset_build_kludge(const char *enc, char *out, int outsz)
{
    int level = 2;

    if (!out || outsz <= 0)
        return;

    if (!enc || !enc[0])
        enc = CHARSET_WRITE_DEFAULT;

    if (strcasecmp(enc, "UTF-8") == 0)
        level = 4;

    /* Build manually - \x01 is broken on Amiga */
    out[0] = 1; /* SOH */

    snprintf(out + 1, (size_t)outsz - 1, "CHRS: %s %d\r", enc, level);
}

const char **charset_get_list(int *count)
{
    static const char *charset_list[] = {"UTF-8", "LATIN-1", "CP437", "CP850", "CP865", "CP866", "CP1252", "LATIN-2"};

    if (!count)
        return NULL;

    *count = (int)(sizeof(charset_list) / sizeof(charset_list[0]));

    return charset_list;
}
