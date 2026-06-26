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

#include "dict_scan.h"
#include "../core/portable.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef PLATFORM_AMIGA
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/types.h>
#endif

static int push_one(char ***list, int *count, int *cap, char *base)
{
    if (!base)
        return 0;

    if (*count >= *cap)
    {
        int new_cap = (*cap) * 2;
        char **g = (char **)realloc(*list, (size_t)new_cap * sizeof(char *));

        if (!g)
        {
            free(base);
            return 0;
        }

        *list = g;
        *cap = new_cap;
    }

    (*list)[(*count)++] = base;

    return 1;
}

char **dict_scan(const char *dir_path, int *n_out, dict_filter_fn filter, dict_xform_fn xform)
{
    char **list = NULL;
    int cap = 16;
    int count = 0;

#ifdef PLATFORM_AMIGA
    BPTR lock;
#elif defined(_WIN32)
    wchar_t *wdir = NULL;
    wchar_t wpattern[1024];
    HANDLE hFind;
    WIN32_FIND_DATAW fd;
#else
    DIR *dir = NULL;
#endif

    if (n_out)
        *n_out = 0;

    if (!dir_path || !n_out || !filter || !xform)
        return NULL;

    list = (char **)malloc((size_t)cap * sizeof(char *));

    if (!list)
        return NULL;

#ifdef PLATFORM_AMIGA

    lock = Lock((CONST_STRPTR)dir_path, ACCESS_READ);

    if (lock)
    {
        struct FileInfoBlock *fib = (struct FileInfoBlock *)
            AllocDosObject(DOS_FIB, NULL);

        if (fib && Examine(lock, fib))
        {
            while (ExNext(lock, fib))
            {
                if (fib->fib_DirEntryType < 0 &&
                    filter((const unsigned char *)fib->fib_FileName))
                {
                    char *base = xform((const unsigned char *)fib->fib_FileName);
                    push_one(&list, &count, &cap, base);
                }
            }
        }

        if (fib)
            FreeDosObject(DOS_FIB, fib);

        UnLock(lock);
    }

#elif defined(_WIN32)

    wdir = pf_utf8_to_utf16(dir_path);

    if (wdir)
    {
        if (swprintf(wpattern, sizeof(wpattern) / sizeof(wchar_t), L"%s\\*", wdir) < (int)(sizeof(wpattern) / sizeof(wchar_t)))
        {
            hFind = FindFirstFileW(wpattern, &fd);

            if (hFind != INVALID_HANDLE_VALUE)
            {
                do
                {
                    char *name_utf8 = pf_utf16_to_utf8(fd.cFileName);

                    if (!name_utf8)
                        continue;

                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && filter((const unsigned char *)name_utf8))
                    {
                        char *base = xform((const unsigned char *)name_utf8);

                        push_one(&list, &count, &cap, base);
                    }

                    free(name_utf8);
                } while (FindNextFileW(hFind, &fd));

                FindClose(hFind);
            }
        }

        free(wdir);
    }

#else

    dir = opendir(dir_path);

    if (dir)
    {
        struct dirent *ent;

        while ((ent = readdir(dir)) != NULL)
        {
            if (filter((const unsigned char *)ent->d_name))
            {
                char *base = xform((const unsigned char *)ent->d_name);
                push_one(&list, &count, &cap, base);
            }
        }

        closedir(dir);
    }

#endif

    if (count == 0)
    {
        free(list);
        return NULL;
    }

    *n_out = count;
    return list;
}
