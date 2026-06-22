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

#include "spell_priv.h"

char *ms_strdup(const char *s)
{
    size_t n;
    char *r = NULL;

    if (!s)
        return NULL;

    n = strlen(s) + 1;
    r = (char *)malloc(n);

    if (r)
        memcpy(r, s, n);

    return r;
}

int ms_readline(FILE *fp, char *buf, size_t sz)
{
    int c = 0;
    int n = 0;

    if (sz == 0)
        return -1;

    for (;;)
    {
        c = fgetc(fp);

        if (c == EOF)
        {
            if (n == 0)
                return -1;

            break;
        }

        if (c == '\n')
            break;

        if (c == '\r')
            continue;

        if ((size_t)n < sz - 1)
            buf[n++] = (char)c;
    }

    buf[n] = '\0';

    return n;
}

void ms_rstrip(char *s)
{
    int n = (int)strlen(s);

    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
}

/* Decode UTF-8 character at p, returns byte length (1-4) */
int utf8_decode(const char *p, ms_cp *cp)
{
    unsigned char c = (unsigned char)*p;

    if (c < 0x80)
    {
        *cp = c;
        return 1;
    }

    if ((c & 0xE0) == 0xC0)
    {
        *cp = ((ms_cp)(c & 0x1F) << 6) | ((unsigned char)p[1] & 0x3F);
        return 2;
    }

    if ((c & 0xF0) == 0xE0)
    {
        *cp = ((ms_cp)(c & 0x0F) << 12) | (((unsigned char)p[1] & 0x3F) << 6) | ((unsigned char)p[2] & 0x3F);
        return 3;
    }

    if ((c & 0xF8) == 0xF0)
    {
        *cp = ((ms_cp)(c & 0x07) << 18) | (((unsigned char)p[1] & 0x3F) << 12) | (((unsigned char)p[2] & 0x3F) << 6) | ((unsigned char)p[3] & 0x3F);
        return 4;
    }

    *cp = c;

    return 1;
}

/* Parse flags string: 0=ASCII, 1=UTF-8, 2=NUM, 3=LONG */
ms_cp *parse_flags(const char *s, int flag_type, int *n_out)
{
    int count = 0;
    int cap = 8;
    ms_cp *result = NULL;
    const char *p = NULL;

    if (!s || !*s)
    {
        *n_out = 0;
        return NULL;
    }

    result = (ms_cp *)malloc((size_t)cap * sizeof(ms_cp));

    if (!result)
    {
        *n_out = 0;
        return NULL;
    }

    p = s;

    if (flag_type == 2 || flag_type == 3)
    {
        /* FLAG NUM or LONG: numbers separated by commas */
        while (*p)
        {
            unsigned long num = 0;
            char *end = NULL;

            num = strtoul(p, &end, 10);

            if (end == p)
                break; /* not a number */

            if (count == cap)
            {
                int new_cap = cap * 2;
                ms_cp *ng = (ms_cp *)realloc(result, (size_t)new_cap * sizeof(ms_cp));

                if (!ng)
                    break;

                result = ng;
                cap = new_cap;
            }

            result[count++] = (ms_cp)num;
            p = end;

            if (*p == ',')
                p++;
            else
                break;
        }
    }
    else
    {
        /* FLAG ASCII or UTF-8: characters/codepoints */
        while (*p)
        {
            ms_cp cp;
            int adv;

            if (flag_type == 1)
                adv = utf8_decode(p, &cp);
            else
            {
                cp = (unsigned char)*p;
                adv = 1;
            }

            if (count == cap)
            {
                int new_cap = cap * 2;
                ms_cp *ng = (ms_cp *)realloc(result, (size_t)new_cap * sizeof(ms_cp));

                if (!ng)
                    break;

                result = ng;
                cap = new_cap;
            }

            result[count++] = cp;
            p += adv;
        }
    }

    *n_out = count;

    return result;
}

int has_flag(const ms_cp *flags, int n, ms_cp f)
{
    int i;

    for (i = 0; i < n; i++)
    {
        if (flags[i] == f)
            return 1;
    }

    return 0;
}

/* FNV-1a hash over string bytes */
unsigned long ms_hash(const char *s)
{
    unsigned long h = 2166136261UL;

    while (*s)
    {
        h ^= (unsigned char)*s++;
        h *= 16777619UL;
    }

    return h;
}

/* Check if UTF-8 character at byte position is uppercase (returns 1 if yes, 0 if no) */
int utf8_char_is_upper(const char *str, int byte_pos)
{
    wchar_t *wcs;
    int len;
    int char_pos = 0;
    const char *p = str;
    int result = 0;

    /* Find character position from byte position */
    while (*p && p < str + byte_pos)
    {
        unsigned char c = (unsigned char)*p;

        if ((c & 0xC0) != 0x80)
            char_pos++;

        p++;
    }

    wcs = utf8_to_wcs(str, &len);

    if (!wcs)
        return 0;

    if (char_pos < len)
        result = iswupper((wint_t)wcs[char_pos]) ? 1 : 0;

    free(wcs);

    return result;
}

/* Convert UTF-8 character to match case of reference character */
int utf8_match_char_case(const char *ref_char, char *target_char, int target_len)
{
    wchar_t *ref_wcs = NULL;
    wchar_t *target_wcs = NULL;
    int ref_len;
    int target_len_wcs;
    int ref_is_upper;

    ref_wcs = utf8_to_wcs(ref_char, &ref_len);

    if (!ref_wcs || ref_len == 0)
    {
        if (ref_wcs)
            free(ref_wcs);

        return 1; /* No reference, keep as-is */
    }

    target_wcs = utf8_to_wcs(target_char, &target_len_wcs);

    if (!target_wcs || target_len_wcs == 0)
    {
        free(ref_wcs);

        if (target_wcs)
            free(target_wcs);

        return 1; /* Can't convert, keep as-is */
    }

    ref_is_upper = iswupper((wint_t)ref_wcs[0]) ? 1 : 0;

    /* Convert target to match reference case */
    if (ref_is_upper)
        target_wcs[0] = (wchar_t)towupper((wint_t)target_wcs[0]);
    else
        target_wcs[0] = (wchar_t)towlower((wint_t)target_wcs[0]);

    /* Check if case matches reference */
    if (ref_is_upper && !iswupper((wint_t)target_wcs[0]))
    {
        free(ref_wcs);
        free(target_wcs);

        return 0; /* Skip: can't make uppercase */
    }
    if (!ref_is_upper && iswupper((wint_t)target_wcs[0]))
    {
        free(ref_wcs);
        free(target_wcs);

        return 0; /* Skip: can't make lowercase */
    }

    /* Convert back to UTF-8 */
    {
        char *converted = wcs_to_utf8(target_wcs, target_len_wcs);

        if (converted)
        {
            int conv_len = (int)strlen(converted);

            if (conv_len <= target_len)
            {
                memcpy(target_char, converted, (size_t)conv_len);

                if (conv_len < target_len)
                    target_char[conv_len] = '\0';
            }

            free(converted);
        }
    }

    free(ref_wcs);
    free(target_wcs);

    return 1;
}

/* Returns 1 if wc is letter/digit/soft hyphen part of word for spell-checker covers ASCII Latin Extended ranges */
int te_is_word_char(wint_t wc)
{
    /* ASCII alphanumeric */
    if (wc >= '0' && wc <= '9')
        return 1;

    if (wc >= 'a' && wc <= 'z')
        return 1;

    if (wc >= 'A' && wc <= 'Z')
        return 1;

    /* Apostrophe interior don't l'arbre */
    if (wc == '\'' || wc == 0x2019 /* ' */)
        return 1;

    /* Latin-1 Supplement accents n tilde c cedilla etc */
    /* 0x00C0..0x00FF except 0x00D7 multiplication 0x00F7 division */
    if (wc >= 0x00C0 && wc <= 0x00FF)
    {
        if (wc == 0x00D7 || wc == 0x00F7)
            return 0;

        return 1;
    }

    /* Latin Extended-A 256 letters Polish Czech etc */
    if (wc >= 0x0100 && wc <= 0x017F)
        return 1;

    /* Latin Extended-B */
    if (wc >= 0x0180 && wc <= 0x024F)
        return 1;

    /* Basic Greek for tinyedit with .el */
    if (wc >= 0x0370 && wc <= 0x03FF)
    {
        if (wc == 0x0378 || wc == 0x0379 || wc == 0x037F)
            return 0;

        return 1;
    }

    /* Basic Cyrillic */
    if (wc >= 0x0400 && wc <= 0x04FF)
        return 1;

    return 0;
}
