/*
 * tinyedit - Text editor for AmigaOS
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 or later.
 */

/* portable.c -- C89, one function per platform, no scope blocks. */

#if !defined(PLATFORM_WIN32) && !defined(PLATFORM_AMIGA)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif
#endif

#include "portable.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PLATFORM_WIN32)
#include <windows.h>
#elif defined(PLATFORM_AMIGA)
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

int port_mkdir_one(const char *p)
{
#ifdef PLATFORM_AMIGA
    BPTR l;

    l = CreateDir((STRPTR)p);

    if (l)
    {
        UnLock(l);
        return 0;
    }

    return -1;
#elif defined(PLATFORM_WIN32)
    return mkdir(p);
#else
    return mkdir(p, 0755);
#endif
}

int port_file_create_empty(const char *p)
{
#ifdef PLATFORM_AMIGA
    BPTR fh;

    fh = Open((STRPTR)p, MODE_NEWFILE);

    if (fh)
    {
        Close(fh);
        return 0;
    }

    return -1;
#else
    FILE *f = NULL;

    f = fopen(p, "w");

    if (f)
    {
        fclose(f);
        return 0;
    }

    return -1;
#endif
}

#ifdef PLATFORM_AMIGA
char *port_sanitize_filename(const char *utf8_name)
{
    static char ascii_buf[512];
    const unsigned char *p = (const unsigned char *)utf8_name;
    char *out = ascii_buf;
    int i = 0;

    if (!utf8_name)
        return NULL;

    while (*p && i < (int)(sizeof(ascii_buf) - 1))
    {
        unsigned char c = *p;

        /* ASCII characters (0-127) pass through */
        if (c < 0x80)
        {
            /* Replace problematic characters with underscore */
            if (c == '/' || c == ':' || c == '\\' || c == '?' || c == '*' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';

            *out++ = (char)c;
            i++;
            p++;
        }
        /* UTF-8 sequences - convert common Spanish characters to ASCII */
        else if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80)
        {
            /* 2-byte UTF-8 */
            unsigned char c2 = p[1];
            uint32_t cp = ((c & 0x1F) << 6) | (c2 & 0x3F);

            /* Convert common Spanish characters */
            switch (cp)
            {
            case 0xE1:
                *out++ = 'a';
                i++;
                break; /* á */
            case 0xE9:
                *out++ = 'e';
                i++;
                break; /* é */
            case 0xED:
                *out++ = 'i';
                i++;
                break; /* í */
            case 0xF3:
                *out++ = 'o';
                i++;
                break; /* ó */
            case 0xFA:
                *out++ = 'u';
                i++;
                break; /* ú */
            case 0xF1:
                *out++ = 'n';
                i++;
                break; /* ñ */
            case 0xC1:
                *out++ = 'A';
                i++;
                break; /* Á */
            case 0xC9:
                *out++ = 'E';
                i++;
                break; /* É */
            case 0xCD:
                *out++ = 'I';
                i++;
                break; /* Í */
            case 0xD3:
                *out++ = 'O';
                i++;
                break; /* Ó */
            case 0xDA:
                *out++ = 'U';
                i++;
                break; /* Ú */
            case 0xD1:
                *out++ = 'N';
                i++;
                break; /* Ñ */
            default:
                *out++ = '_';
                i++;
                break; /* Other UTF-8 -> underscore */
            }
            p += 2;
        }
        else if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
        {
            /* 3-byte UTF-8 - replace with underscore */
            *out++ = '_';
            i++;
            p += 3;
        }
        else if ((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80)
        {
            /* 4-byte UTF-8 - replace with underscore */
            *out++ = '_';
            i++;
            p += 4;
        }
        else
        {
            /* Invalid UTF-8 - replace with underscore */
            *out++ = '_';
            i++;
            p++;
        }
    }

    *out = '\0';
    return ascii_buf;
}
#endif
