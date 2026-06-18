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
    FILE *f;

    f = fopen(p, "w");

    if (f)
    {
        fclose(f);
        return 0;
    }

    return -1;
#endif
}
