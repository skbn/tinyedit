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

#include "json_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Worst case: every byte -> "\uXXXX" (6 chars). Plus '\0' */
static int json_escape_into(const char *src, char *dst, int dst_size)
{
    int o = 0;
    unsigned char c;

    if (!src || !dst || dst_size <= 0)
        return -1;

    while (*src)
    {
        c = (unsigned char)*src++;

        switch (c)
        {
        case '"':
            if (o + 2 >= dst_size)
                return -1;

            dst[o++] = '\\';
            dst[o++] = '"';
            break;
        case '\\':
            if (o + 2 >= dst_size)
                return -1;

            dst[o++] = '\\';
            dst[o++] = '\\';
            break;
        case '\b':
            if (o + 2 >= dst_size)
                return -1;

            dst[o++] = '\\';
            dst[o++] = 'b';
            break;
        case '\f':
            if (o + 2 >= dst_size)
                return -1;

            dst[o++] = '\\';
            dst[o++] = 'f';
            break;
        case '\n':
            if (o + 2 >= dst_size)
                return -1;

            dst[o++] = '\\';
            dst[o++] = 'n';
            break;
        case '\r':
            if (o + 2 >= dst_size)
                return -1;

            dst[o++] = '\\';
            dst[o++] = 'r';
            break;
        case '\t':
            if (o + 2 >= dst_size)
                return -1;

            dst[o++] = '\\';
            dst[o++] = 't';
            break;
        default:
            if (c < 0x20)
            {
                static const char hex[] = "0123456789ABCDEF";

                /* Other C0 controls -> \u00XX */
                if (o + 6 >= dst_size)
                    return -1;

                dst[o++] = '\\';
                dst[o++] = 'u';
                dst[o++] = '0';
                dst[o++] = '0';
                dst[o++] = hex[(c >> 4) & 0xF];
                dst[o++] = hex[c & 0xF];
            }
            else
            {
                /* High bytes pass through; valid UTF-8 stays valid */
                if (o + 1 >= dst_size)
                    return -1;

                dst[o++] = (char)c;
            }
            break;
        }
    }

    dst[o] = '\0';
    return o;
}

/* Estimate maximum encoded size for `src` so caller can size a buffer */
static size_t json_escape_bound(const char *src)
{
    /* Each byte -> at most 6 chars (\u00XX). Plus terminator */
    return src ? (strlen(src) * 6 + 1) : 1;
}

void json_unescape_string(char *str)
{
    char *src = NULL;
    char *dst = NULL;
    int i;

    if (!str)
        return;

    src = str;
    dst = str;

    while (*src)
    {
        if (*src == '\\' && *(src + 1))
        {
            src++;

            switch (*src)
            {
            case 'n':
                *dst++ = '\n';
                break;
            case 'r':
                *dst++ = '\r';
                break;
            case 't':
                *dst++ = '\t';
                break;
            case 'b':
                *dst++ = '\b';
                break;
            case 'f':
                *dst++ = '\f';
                break;
            case '\\':
                *dst++ = '\\';
                break;
            case '"':
                *dst++ = '"';
                break;
            case '/':
                *dst++ = '/';
                break;
            case 'u':
                /* Unicode escape \uXXXX -> UTF-8 (BMP only, no surrogate pairs) */
                if (strlen(src) >= 5)
                {
                    unsigned int codepoint = 0;
                    src++;

                    for (i = 0; i < 4 && *src; i++)
                    {
                        char c = *src++;

                        codepoint <<= 4;

                        if (c >= '0' && c <= '9')
                            codepoint |= (c - '0');
                        else if (c >= 'a' && c <= 'f')
                            codepoint |= (c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F')
                            codepoint |= (c - 'A' + 10);
                    }

                    src--; /* outer loop does src++ */

                    if (codepoint <= 0x7F)
                    {
                        *dst++ = (char)codepoint;
                    }
                    else if (codepoint <= 0x7FF)
                    {
                        *dst++ = (char)(0xC0 | (codepoint >> 6));
                        *dst++ = (char)(0x80 | (codepoint & 0x3F));
                    }
                    else
                    {
                        *dst++ = (char)(0xE0 | (codepoint >> 12));
                        *dst++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        *dst++ = (char)(0x80 | (codepoint & 0x3F));
                    }
                }
                break;
            default:
                /* Unknown escape - keep the char raw (lenient) */
                *dst++ = *src;
                break;
            }

            src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
}

/* Scan past whitespace */
static const char *skip_ws(const char *p)
{
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;

    return p;
}

/* Given a pointer somewhere on/after a "key": pair, scan forward to the start of the value */
static const char *value_after_colon(const char *p)
{
    p = skip_ws(p);
    if (*p != ':')
        return NULL;
    p++;
    return skip_ws(p);
}

/* Match the literal `"key"` at `p`, skipping the key escape rules */
static const char *match_key(const char *p, const char *key)
{
    int i = 0;

    if (*p != '"')
        return NULL;
    p++;

    while (key[i])
    {
        /* Keys never contain escapes in our usage */
        if (*p == '\\' && p[1])
            p += 2;
        else if (*p != key[i])
            return NULL;
        else
            p++, i++;

        continue;
    }

    if (*p != '"')
        return NULL;

    return p + 1;
}

/* Skip a JSON value starting at *pp (could be string/number/object/array/literal) */
static int skip_value(const char **pp)
{
    const char *p = skip_ws(*pp);
    int depth;
    int escaped;

    if (*p == '"')
    {
        p++;
        escaped = 0;

        while (*p)
        {
            if (*p == '\\' && !escaped)
            {
                escaped = 1;
                p++;
                continue;
            }

            if (*p == '"' && !escaped)
            {
                p++;
                break;
            }

            escaped = 0;
            p++;
        }
    }
    else if (*p == '{' || *p == '[')
    {
        char open = *p, close = (open == '{') ? '}' : ']';

        depth = 1;
        p++;

        while (*p && depth > 0)
        {
            if (*p == '"')
            {
                p++;
                escaped = 0;

                while (*p)
                {
                    if (*p == '\\' && !escaped)
                    {
                        escaped = 1;
                        p++;
                        continue;
                    }

                    if (*p == '"' && !escaped)
                    {
                        p++;
                        break;
                    }

                    escaped = 0;
                    p++;
                }
            }
            else
            {
                if (*p == open)
                    depth++;
                else if (*p == close)
                    depth--;

                p++;
            }
        }
    }
    else
    {
        /* number / true / false / null - read until comma/closer/ws */
        while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            p++;
    }

    *pp = p;
    return 0;
}

/* Find the value position for `key` directly inside object starting at *pp */
static int find_key_in_object(const char **pp, const char *key)
{
    const char *p = skip_ws(*pp);
    const char *after_key = NULL;
    const char *val = NULL;

    if (*p != '{')
        return -1;

    p++;
    p = skip_ws(p);

    while (*p && *p != '}')
    {
        after_key = match_key(p, key);

        if (after_key)
        {
            val = value_after_colon(after_key);

            if (!val)
                return -1;

            *pp = val;
            return 1;
        }

        /* skip key string */
        if (*p == '"')
        {
            int escaped = 0;

            p++;

            while (*p)
            {
                if (*p == '\\' && !escaped)
                {
                    escaped = 1;
                    p++;
                    continue;
                }

                if (*p == '"' && !escaped)
                {
                    p++;
                    break;
                }

                escaped = 0;
                p++;
            }
        }
        else
            return -1;

        p = skip_ws(p);

        if (*p != ':')
            return -1;
        p++;

        if (skip_value(&p) < 0)
            return -1;

        p = skip_ws(p);

        if (*p == ',')
        {
            p++;
            p = skip_ws(p);
        }
    }

    return 0;
}

char *json_extract_string(const char *json, const char *key)
{
    const char *p = NULL;
    const char *seg_start = NULL;
    const char *seg_end = NULL;
    char seg[128];
    size_t seg_len;
    int rc;
    const char *val_start = NULL;
    const char *val_end = NULL;
    int escaped;
    size_t val_len;
    char *result = NULL;

    if (!json || !key)
        return NULL;

    p = skip_ws(json);

    /* Walk each path segment ("a.b.c" -> "a","b","c") and descend into the object hierarchy */
    seg_start = key;

    for (;;)
    {
        /* End-of-segment is '.' or end-of-string */
        seg_end = seg_start;

        while (*seg_end && *seg_end != '.')
            seg_end++;

        seg_len = (size_t)(seg_end - seg_start);

        if (seg_len == 0 || seg_len >= sizeof(seg))
            return NULL;

        memcpy(seg, seg_start, seg_len);
        seg[seg_len] = '\0';

        rc = find_key_in_object(&p, seg);

        if (rc <= 0)
            return NULL;

        if (*seg_end == '.')
        {
            /* Need to descend into another object */
            seg_start = seg_end + 1;
            p = skip_ws(p);

            if (*p != '{')
                return NULL;

            continue;
        }

        /* Last segment - value must be a string */
        if (*p != '"')
            return NULL;

        val_start = p + 1;
        break;
    }

    /* Scan to the closing quote (respecting escapes) */
    val_end = val_start;
    escaped = 0;

    while (*val_end)
    {
        if (*val_end == '\\' && !escaped)
        {
            escaped = 1;
            val_end++;
            continue;
        }

        if (*val_end == '"' && !escaped)
            break;

        escaped = 0;
        val_end++;
    }

    if (*val_end != '"')
        return NULL;

    val_len = (size_t)(val_end - val_start);

    result = (char *)malloc(val_len + 1);

    if (!result)
        return NULL;

    memcpy(result, val_start, val_len);
    result[val_len] = '\0';

    json_unescape_string(result);

    return result;
}

char *json_build_simple_request(const char **keys, const char **values, int count)
{
    char *result;
    char *p;
    size_t total_bound;
    int i;
    int n;

    if (!keys || !values || count <= 0)
        return NULL;

    /* Upper bound: 2 (braces) + sum of (key + ":" + escaped(value) + quotes + comma) */
    total_bound = 2 + 1; /* {} + NUL */

    for (i = 0; i < count; i++)
    {
        if (!keys[i] || !values[i])
            return NULL;

        total_bound += 1 + strlen(keys[i]) + 1;              /* "key"  */
        total_bound += 1;                                    /* :      */
        total_bound += 1 + json_escape_bound(values[i]) + 1; /* "esc"  */

        if (i < count - 1)
            total_bound += 1; /* ,      */
    }

    result = (char *)malloc(total_bound);

    if (!result)
        return NULL;

    p = result;
    *p++ = '{';

    for (i = 0; i < count; i++)
    {
        const char *k = keys[i];

        *p++ = '"';

        while (*k)
            *p++ = *k++;

        *p++ = '"';
        *p++ = ':';
        *p++ = '"';

        n = json_escape_into(values[i], p, (int)(total_bound - (size_t)(p - result) - 4));

        if (n < 0)
        {
            free(result);
            return NULL;
        }

        p += n;

        *p++ = '"';
        if (i < count - 1)
            *p++ = ',';
    }

    *p++ = '}';
    *p = '\0';

    return result;
}
