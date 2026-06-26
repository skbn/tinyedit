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

/* portable.c -- C89, one function per platform, no scope blocks */

#if !defined(PLATFORM_WIN32) && !defined(PLATFORM_AMIGA)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif
#endif

#include "portable.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(PLATFORM_WIN32)
#include <windows.h>
#include <direct.h>
#elif defined(PLATFORM_AMIGA)
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <time.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
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

void port_get_config_dir(char *buf, size_t bufsz)
{
    char *home = NULL;

#if defined(PLATFORM_AMIGA)
    snprintf(buf, bufsz, "ENVARC:tinyedit");
#elif defined(PLATFORM_WIN32)
    home = getenv("APPDATA");

    if (home && home[0])
        snprintf(buf, bufsz, "%s\\tinyedit", home);
    else
        snprintf(buf, bufsz, "tinyedit");
#else
    home = getenv("HOME");

    if (home && home[0])
        snprintf(buf, bufsz, "%s/.tinyedit", home);
    else
        snprintf(buf, bufsz, ".tinyedit");
#endif
}

/* Portable path existence test */
int pf_path_exists(const char *path)
{
#if defined(PLATFORM_AMIGA)
    BPTR lock;

    if (!path || !path[0])
        return 0;

    lock = Lock((STRPTR)path, ACCESS_READ);

    if (lock)
    {
        UnLock(lock);
        return 1;
    }

    return 0;
#elif defined(PLATFORM_WIN32)
    DWORD attr;
    wchar_t *wpath = NULL;

    if (!path || !path[0])
        return 0;

    wpath = pf_utf8_to_utf16(path);

    if (!wpath)
        return 0;

    attr = GetFileAttributesW(wpath);

    free(wpath);

    return (attr != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
#else
    struct stat st;

    if (!path || !path[0])
        return 0;

    return (stat(path, &st) == 0) ? 1 : 0;
#endif
}

/* Portable regular-file test */
int pf_is_regular_file(const char *path)
{
#if defined(PLATFORM_AMIGA)
    BPTR lock;
    struct FileInfoBlock *fib = NULL;
    int res = 0;

    if (!path || !path[0])
        return 0;

    lock = Lock((STRPTR)path, ACCESS_READ);

    if (!lock)
        return 0;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

    if (fib)
    {
        if (Examine(lock, fib))
            res = (fib->fib_DirEntryType < 0) ? 1 : 0;

        FreeDosObject(DOS_FIB, fib);
    }

    UnLock(lock);
    return res;
#elif defined(PLATFORM_WIN32)
    DWORD attr;
    wchar_t *wpath = NULL;

    if (!path || !path[0])
        return 0;

    wpath = pf_utf8_to_utf16(path);

    if (!wpath)
        return 0;

    attr = GetFileAttributesW(wpath);

    free(wpath);

    if (attr == INVALID_FILE_ATTRIBUTES)
        return 0;

    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 1;
#else
    struct stat st;

    if (!path || !path[0])
        return 0;

    if (stat(path, &st) != 0)
        return 0;

    return S_ISREG(st.st_mode) ? 1 : 0;
#endif
}

/* Portable recursive directory creation */
int pf_mkdir_path(const char *path)
{
    char tmp[512];
    char *p = NULL;
    size_t len;

    if (!path || !path[0])
        return -1;

    pf_safe_strncpy(tmp, path, sizeof(tmp));

    len = strlen(tmp);

    while (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\'))
        tmp[--len] = '\0';

    for (p = tmp + 1; *p; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            *p = '\0';

            if (!pf_path_exists(tmp))
                port_mkdir_one(tmp);

            *p = '/';
        }
    }

    if (!pf_path_exists(tmp))
        return port_mkdir_one(tmp);

    return 0;
}

/* Portable ensure directory exists */
int pf_ensure_dir(const char *path)
{
    if (pf_path_exists(path))
        return 1;

    return (pf_mkdir_path(path) == 0) ? 1 : 0;
}

/* Portable safe strncpy (always NUL-terminates) */
void pf_safe_strncpy(char *dst, const char *src, size_t dstsize)
{
    size_t len;

    if (dstsize == 0)
        return;

    if (!src)
        src = "";

    len = strlen(src);

    if (len > dstsize - 1)
        len = dstsize - 1;

    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Portable path join */
void pf_path_join(char *out, size_t outsize, const char *base, const char *sub)
{
    size_t blen;
    char last;

    if (outsize == 0)
        return;

    pf_safe_strncpy(out, base, outsize);

    blen = strlen(out);
    last = (blen > 0) ? out[blen - 1] : '\0';

    if (last != '/' && last != ':' && last != '\\')
    {
        if (outsize - 1 - blen > 0)
        {
            out[blen] = '/';
            out[blen + 1] = '\0';
            blen++;
        }
    }

    pf_safe_strncpy(out + blen, sub, outsize - blen);
}

/* Portable wildcard match (* and ?, case-insensitive) */
int pf_wildmatch(const char *pat, const char *str)
{
    while (*pat)
    {
        if (*pat == '*')
        {
            while (*pat == '*')
                pat++;

            if (!*pat)
                return 1;

            while (*str)
            {
                if (pf_wildmatch(pat, str++))
                    return 1;
            }

            return 0;
        }

        if (*pat == '?')
        {
            if (!*str)
                return 0;

            pat++;
            str++;
        }
        else
        {
            if (toupper((unsigned char)*pat) != toupper((unsigned char)*str))
                return 0;

            pat++;
            str++;
        }
    }

    return (*str == '\0') ? 1 : 0;
}

/* Portable wildcard test */
int pf_is_wildcard(const char *s)
{
    if (!s)
        return 0;

    while (*s)
    {
        if (*s == '*' || *s == '?')
            return 1;

        s++;
    }

    return 0;
}

/* Portable binary file copy */
int pf_copy_file(const char *src, const char *dst)
{
    FILE *in = NULL;
    FILE *out = NULL;
    char buf[4096];
    size_t n;

    if (!src || !dst)
        return 0;

    in = fopen(src, "rb");

    if (!in)
        return 0;

    out = fopen(dst, "wb");

    if (!out)
    {
        fclose(in);
        return 0;
    }

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
        {
            fclose(out);
            fclose(in);
            return 0;
        }
    }

    fclose(out);
    fclose(in);

    return 1;
}

/* Portable file move (rename, with copy+delete fallback) */
int pf_move_file(const char *src, const char *dst)
{
    if (!src || !dst)
        return 0;

    if (pf_atomic_rename(src, dst) == 0)
        return 1;

    if (pf_copy_file(src, dst))
    {
        pf_remove_file(src);
        return 1;
    }

    return 0;
}

/* Portable file size */
long pf_get_file_size(const char *path)
{
#if defined(PLATFORM_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA info;
    LARGE_INTEGER size;
    wchar_t *wpath = NULL;

    if (!path || !path[0])
        return -1;

    wpath = pf_utf8_to_utf16(path);

    if (!wpath)
        return -1;

    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &info))
    {
        free(wpath);
        return -1;
    }

    free(wpath);

    size.LowPart = info.nFileSizeLow;
    size.HighPart = info.nFileSizeHigh;

    return (long)size.QuadPart;
#elif defined(PLATFORM_AMIGA)
    BPTR lock;
    struct FileInfoBlock *fib = NULL;
    long res = -1;

    if (!path || !path[0])
        return -1;

    lock = Lock((STRPTR)path, ACCESS_READ);

    if (!lock)
        return -1;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

    if (fib)
    {
        if (Examine(lock, fib) && fib->fib_DirEntryType < 0)
            res = (long)fib->fib_Size;

        FreeDosObject(DOS_FIB, fib);
    }

    UnLock(lock);
    return res;
#else
    struct stat st;

    if (!path || !path[0])
        return -1;

    if (stat(path, &st) != 0)
        return -1;

    return (long)st.st_size;
#endif
}

/* Portable file modification time */
long pf_get_file_mtime(const char *path)
{
#if defined(PLATFORM_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA info;
    wchar_t *wpath = NULL;
    FILETIME ft;
    ULARGE_INTEGER ull;

    if (!path || !path[0])
        return 0;

    wpath = pf_utf8_to_utf16(path);

    if (!wpath)
        return 0;

    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &info))
    {
        free(wpath);
        return 0;
    }

    free(wpath);

    ft = info.ftLastWriteTime;
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;

    return (long)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);

#elif defined(PLATFORM_AMIGA)
    BPTR lock;
    struct FileInfoBlock *fib = NULL;
    long res = 0;

    if (!path || !path[0])
        return 0;

    lock = Lock((STRPTR)path, ACCESS_READ);

    if (!lock)
        return 0;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

    if (fib)
    {
        if (Examine(lock, fib) && fib->fib_DirEntryType < 0)
            res = (long)fib->fib_Date.ds_Days * 24 * 60 * 60 + (long)fib->fib_Date.ds_Minute * 60 + (long)fib->fib_Date.ds_Tick / 50;

        FreeDosObject(DOS_FIB, fib);
    }

    UnLock(lock);
    return res;
#else
    struct stat st;

    if (!path || !path[0])
        return 0;

    if (stat(path, &st) != 0)
        return 0;

    return (long)st.st_mtime;
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

#if defined(PLATFORM_WIN32)

struct PfDir
{
    HANDLE h;
    WIN32_FIND_DATAW fd;
    int started;
    int eod;
    char name[260];
};

PfDir *pf_dir_open(const char *path)
{
    PfDir *d = NULL;
    wchar_t *wpath = NULL;
    wchar_t pattern[512];

    if (!path || !path[0])
        return NULL;

    wpath = pf_utf8_to_utf16(path);

    if (!wpath)
        return NULL;

    if ((size_t)swprintf(pattern, sizeof(pattern) / sizeof(wchar_t), L"%s\\*", wpath) >= sizeof(pattern) / sizeof(wchar_t))
    {
        free(wpath);
        return NULL;
    }

    free(wpath);

    d = (PfDir *)malloc(sizeof(*d));

    if (!d)
        return NULL;

    d->h = FindFirstFileW(pattern, &d->fd);

    if (d->h == INVALID_HANDLE_VALUE)
    {
        free(d);
        return NULL;
    }

    d->started = 0;
    d->eod = 0;
    d->name[0] = '\0';
    return d;
}

const char *pf_dir_next_entry(PfDir *d, int *is_dir)
{
    if (!d || d->eod)
        return NULL;

    for (;;)
    {
        char *name_utf8 = NULL;

        if (d->started)
        {
            if (!FindNextFileW(d->h, &d->fd))
            {
                d->eod = 1;
                return NULL;
            }
        }
        else
        {
            d->started = 1;
        }

        if (wcscmp(d->fd.cFileName, L".") == 0 || wcscmp(d->fd.cFileName, L"..") == 0)
            continue;

        name_utf8 = pf_utf16_to_utf8(d->fd.cFileName);

        if (!name_utf8)
            continue;

        strncpy(d->name, name_utf8, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = '\0';

        free(name_utf8);

        if (is_dir)
            *is_dir = (d->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;

        return d->name;
    }
}

const char *pf_dir_next(PfDir *d)
{
    int is_dir;
    const char *name = NULL;

    while ((name = pf_dir_next_entry(d, &is_dir)) != NULL)
    {
        if (!is_dir)
            return name;
    }

    return NULL;
}

void pf_dir_close(PfDir *d)
{
    if (!d)
        return;

    if (d->h != INVALID_HANDLE_VALUE)
        FindClose(d->h);

    free(d);
}

int pf_is_directory(const char *path)
{
    DWORD attr;
    wchar_t *wpath = NULL;

    if (!path || !path[0])
        return 0;

    wpath = pf_utf8_to_utf16(path);

    if (!wpath)
        return 0;

    attr = GetFileAttributesW(wpath);

    free(wpath);

    if (attr == INVALID_FILE_ATTRIBUTES)
        return 0;

    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

#elif defined(PLATFORM_AMIGA)

struct PfDir
{
    BPTR lock;
    struct FileInfoBlock *fib;
    char name[108];
};

PfDir *pf_dir_open(const char *path)
{
    PfDir *d = NULL;

    if (!path)
        return NULL;

    d = (PfDir *)AllocMem((LONG)sizeof(*d), MEMF_CLEAR);

    if (!d)
        return NULL;

    d->fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

    if (!d->fib)
    {
        FreeMem(d, (LONG)sizeof(*d));
        return NULL;
    }

    d->lock = Lock((STRPTR)(path[0] ? path : ""), ACCESS_READ);

    if (!d->lock)
    {
        FreeDosObject(DOS_FIB, d->fib);
        FreeMem(d, (LONG)sizeof(*d));
        return NULL;
    }

    if (!Examine(d->lock, d->fib) || d->fib->fib_DirEntryType <= 0)
    {
        UnLock(d->lock);
        FreeDosObject(DOS_FIB, d->fib);
        FreeMem(d, (LONG)sizeof(*d));
        return NULL;
    }

    d->name[0] = '\0';

    return d;
}

const char *pf_dir_next_entry(PfDir *d, int *is_dir)
{
    if (!d)
        return NULL;

    while (ExNext(d->lock, d->fib))
    {
        if (d->fib->fib_DirEntryType == 0)
            continue;

        strncpy(d->name, d->fib->fib_FileName, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = '\0';

        if (is_dir)
            *is_dir = (d->fib->fib_DirEntryType > 0) ? 1 : 0;

        return d->name;
    }

    return NULL;
}

const char *pf_dir_next(PfDir *d)
{
    int is_dir;
    const char *name;

    while ((name = pf_dir_next_entry(d, &is_dir)) != NULL)
    {
        if (!is_dir)
            return name;
    }

    return NULL;
}

void pf_dir_close(PfDir *d)
{
    if (!d)
        return;

    if (d->lock)
        UnLock(d->lock);

    if (d->fib)
        FreeDosObject(DOS_FIB, d->fib);

    FreeMem(d, (LONG)sizeof(*d));
}

int pf_is_directory(const char *path)
{
    BPTR lock;
    struct FileInfoBlock *fib = NULL;
    int is_dir;

    if (!path || !path[0])
        return 0;

    lock = Lock((STRPTR)path, ACCESS_READ);

    if (!lock)
        return 0;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

    if (!fib)
    {
        UnLock(lock);
        return 0;
    }

    is_dir = 0;

    if (Examine(lock, fib) && fib->fib_DirEntryType > 0)
        is_dir = 1;

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);

    return is_dir;
}

#else /* POSIX */

struct PfDir
{
    DIR *dp;
    char base[300];
    char name[300];
};

PfDir *pf_dir_open(const char *path)
{
    PfDir *d = NULL;

    if (!path)
        return NULL;

    d = (PfDir *)malloc(sizeof(*d));

    if (!d)
        return NULL;

    d->dp = opendir(path[0] ? path : ".");

    if (!d->dp)
    {
        free(d);
        return NULL;
    }

    strncpy(d->base, path, sizeof(d->base) - 1);
    d->base[sizeof(d->base) - 1] = '\0';
    d->name[0] = '\0';

    return d;
}

const char *pf_dir_next_entry(PfDir *d, int *is_dir)
{
    struct dirent *e = NULL;
    struct stat st;
    char full[600];
    size_t bl;

    if (!d)
        return NULL;

    while ((e = readdir(d->dp)) != NULL)
    {
        if (e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0')))
            continue;

        bl = strlen(d->base);

        if (bl == 0)
        {
            strncpy(full, e->d_name, sizeof(full) - 1);
            full[sizeof(full) - 1] = '\0';
        }
        else if (d->base[bl - 1] == '/')
        {
            snprintf(full, sizeof(full), "%s%s", d->base, e->d_name);
        }
        else
        {
            snprintf(full, sizeof(full), "%s/%s", d->base, e->d_name);
        }

        if (stat(full, &st) != 0)
            continue;

        if (is_dir)
            *is_dir = S_ISDIR(st.st_mode) ? 1 : 0;

        strncpy(d->name, e->d_name, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = '\0';

        return d->name;
    }

    return NULL;
}

const char *pf_dir_next(PfDir *d)
{
    int is_dir;
    const char *name = NULL;

    while ((name = pf_dir_next_entry(d, &is_dir)) != NULL)
    {
        if (!is_dir)
            return name;
    }

    return NULL;
}

void pf_dir_close(PfDir *d)
{
    if (!d)
        return;

    if (d->dp)
        closedir(d->dp);

    free(d);
}

int pf_is_directory(const char *path)
{
    struct stat st;

    if (!path || !path[0])
        return 0;

    if (stat(path, &st) != 0)
        return 0;

    return S_ISDIR(st.st_mode) ? 1 : 0;
}

#endif

#if defined(PLATFORM_WIN32)

struct PfLockFile
{
    char path[300];
    HANDLE handle;
};

PfLockFile *pf_lock_create(const char *path)
{
    PfLockFile *lk = NULL;
    char buf[32];
    DWORD wrote;
    int n;
    wchar_t *wpath = NULL;

    if (!path)
        return NULL;

    wpath = pf_utf8_to_utf16(path);

    if (!wpath)
        return NULL;

    lk = (PfLockFile *)malloc(sizeof(*lk));

    if (!lk)
    {
        free(wpath);
        return NULL;
    }

    strncpy(lk->path, path, sizeof(lk->path) - 1);
    lk->path[sizeof(lk->path) - 1] = '\0';

    lk->handle = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

    free(wpath);

    if (lk->handle == INVALID_HANDLE_VALUE)
    {
        free(lk);
        return NULL;
    }

    n = snprintf(buf, sizeof(buf), "%lu\n", (unsigned long)GetCurrentProcessId());
    wrote = 0;

    if (n > 0)
        WriteFile(lk->handle, buf, (DWORD)n, &wrote, NULL);

    return lk;
}

void pf_lock_release(PfLockFile *lk)
{
    wchar_t *wpath = NULL;

    if (!lk)
        return;

    if (lk->handle != INVALID_HANDLE_VALUE)
        CloseHandle(lk->handle);

    wpath = pf_utf8_to_utf16(lk->path);

    if (wpath)
    {
        DeleteFileW(wpath);
        free(wpath);
    }

    free(lk);
}

#elif defined(PLATFORM_AMIGA)

struct PfLockFile
{
    char path[300];
    BPTR fileLock;
};

PfLockFile *pf_lock_create(const char *path)
{
    PfLockFile *lk = NULL;
    BPTR probe;
    BPTR fh;

    if (!path)
        return NULL;

    probe = Lock((STRPTR)path, ACCESS_READ);

    if (probe)
    {
        UnLock(probe);
        return NULL;
    }

    lk = (PfLockFile *)AllocMem((LONG)sizeof(*lk), MEMF_CLEAR);

    if (!lk)
        return NULL;

    strncpy(lk->path, path, sizeof(lk->path) - 1);
    lk->path[sizeof(lk->path) - 1] = '\0';

    fh = Open((STRPTR)path, MODE_NEWFILE);

    if (!fh)
    {
        FreeMem(lk, (LONG)sizeof(*lk));
        return NULL;
    }

    Write(fh, (APTR) "0\n", 2);
    Close(fh);

    lk->fileLock = Lock((STRPTR)path, EXCLUSIVE_LOCK);

    if (!lk->fileLock)
    {
        DeleteFile((STRPTR)path);
        FreeMem(lk, (LONG)sizeof(*lk));
        return NULL;
    }

    return lk;
}

void pf_lock_release(PfLockFile *lk)
{
    if (!lk)
        return;

    if (lk->fileLock)
        UnLock(lk->fileLock);

    DeleteFile((STRPTR)lk->path);
    FreeMem(lk, (LONG)sizeof(*lk));
}

#else /* POSIX */

struct PfLockFile
{
    char path[300];
    int fd;
};

PfLockFile *pf_lock_create(const char *path)
{
    PfLockFile *lk = NULL;
    char buf[32];
    int n;
    ssize_t w;

    if (!path)
        return NULL;

    lk = (PfLockFile *)malloc(sizeof(*lk));

    if (!lk)
        return NULL;

    strncpy(lk->path, path, sizeof(lk->path) - 1);
    lk->path[sizeof(lk->path) - 1] = '\0';

    lk->fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);

    if (lk->fd < 0)
    {
        free(lk);
        return NULL;
    }

    n = snprintf(buf, sizeof(buf), "%ld\n", (long)getpid());

    if (n > 0)
        w = write(lk->fd, buf, (size_t)n);

    return lk;
}

void pf_lock_release(PfLockFile *lk)
{
    if (!lk)
        return;

    if (lk->fd >= 0)
        close(lk->fd);

    unlink(lk->path);

    free(lk);
}

#endif

#if defined(PLATFORM_WIN32)

int pf_atomic_rename(const char *from, const char *to)
{
    wchar_t *wfrom = NULL;
    wchar_t *wto = NULL;
    int rc = -1;

    if (!from || !to)
        return -1;

    wfrom = pf_utf8_to_utf16(from);
    wto = pf_utf8_to_utf16(to);

    if (wfrom && wto)
        rc = MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;

    free(wfrom);
    free(wto);

    return rc;
}

#elif defined(PLATFORM_AMIGA)

int pf_atomic_rename(const char *from, const char *to)
{
    if (!from || !to)
        return -1;

    DeleteFile((STRPTR)to);

    return Rename((STRPTR)from, (STRPTR)to) ? 0 : -1;
}

#else

int pf_atomic_rename(const char *from, const char *to)
{
    if (!from || !to)
        return -1;

    return (rename(from, to) == 0) ? 0 : -1;
}

#endif

#if defined(PLATFORM_WIN32)

int pf_remove_file(const char *path)
{
    wchar_t *wpath = NULL;
    int rc;

    if (!path)
        return -1;

    wpath = pf_utf8_to_utf16(path);

    if (!wpath)
        return -1;

    rc = DeleteFileW(wpath) ? 0 : -1;

    free(wpath);
    return rc;
}

#elif defined(PLATFORM_AMIGA)

int pf_remove_file(const char *path)
{
    return (path && DeleteFile((STRPTR)path)) ? 0 : -1;
}

#else

int pf_remove_file(const char *path)
{
    return (path && unlink(path) == 0) ? 0 : -1;
}

#endif

#if defined(PLATFORM_WIN32)
wchar_t *pf_utf8_to_utf16(const char *s)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = NULL;

    if (len <= 0)
        return NULL;

    w = (wchar_t *)malloc((size_t)len * sizeof(wchar_t));

    if (!w)
        return NULL;

    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, w, len) == 0)
    {
        free(w);
        return NULL;
    }

    return w;
}

char *pf_utf16_to_utf8(const wchar_t *w)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = NULL;

    if (len <= 0)
        return NULL;

    s = (char *)malloc((size_t)len);

    if (!s)
        return NULL;

    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, s, len, NULL, NULL) == 0)
    {
        free(s);
        return NULL;
    }

    return s;
}
#endif

#if defined(PLATFORM_WIN32)

void pf_sleep_ms(unsigned ms)
{
    Sleep((DWORD)ms);
}

#elif defined(PLATFORM_AMIGA)

void pf_sleep_ms(unsigned ms)
{
    LONG ticks = (LONG)((ms * 50UL + 999UL) / 1000UL);

    if (ticks < 1)
        ticks = 1;

    Delay(ticks);
}

#else

void pf_sleep_ms(unsigned ms)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;

    nanosleep(&ts, NULL);
}

#endif

#if defined(PLATFORM_WIN32)
#include <windows.h>

unsigned long pf_now_ms(void)
{
    return (unsigned long)GetTickCount();
}
#elif defined(PLATFORM_AMIGA)
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>

extern struct Device *TimerBase;

unsigned long pf_now_ms(void)
{
    struct timeval tv;

    if (TimerBase)
    {
        GetSysTime(&tv);
        return (unsigned long)tv.tv_secs * 1000UL + (unsigned long)tv.tv_micro / 1000UL;
    }

    return (unsigned long)((clock() * 1000UL) / CLOCKS_PER_SEC);
}
#else
#include <sys/time.h>

unsigned long pf_now_ms(void)
{
    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0)
        return 0;

    return (unsigned long)tv.tv_sec * 1000UL + (unsigned long)(tv.tv_usec / 1000);
}
#endif

static int pf_is_word_sep_wc(wchar_t c)
{
    if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\f' || c == L'\v')
        return 1;

    /* ASCII punctuation */
    if (c == L'.' || c == L',' || c == L';' || c == L':' || c == L'!' ||
        c == L'?' || c == L'(' || c == L')' || c == L'[' || c == L']' ||
        c == L'{' || c == L'}' || c == L'"' || c == L'\'' || c == L'<' ||
        c == L'>' || c == L'/' || c == L'\\' || c == L'|' || c == L'`' ||
        c == L'@' || c == L'#' || c == L'%' || c == L'^' || c == L'&' ||
        c == L'*' || c == L'+' || c == L'=' || c == L'~')
        return 1;

    /* Unicode dashes / spaces commonly seen */
    if (c == 0x00A0 || c == 0x2013 || c == 0x2014 || c == 0x2018 ||
        c == 0x2019 || c == 0x201C || c == 0x201D)
        return 1;

    return 0;
}

int pf_count_words_wcs(const wchar_t *s, int n)
{
    int count = 0;
    int in_word = 0;
    int i;

    if (!s || n <= 0)
        return 0;

    for (i = 0; i < n; i++)
    {
        if (pf_is_word_sep_wc(s[i]))
        {
            in_word = 0;
        }
        else if (!in_word)
        {
            in_word = 1;
            count++;
        }
    }

    return count;
}

int pf_count_words_utf8(const char *s)
{
    int count = 0;
    int in_word = 0;

    if (!s)
        return 0;

    while (*s)
    {
        unsigned char c = (unsigned char)*s;

        /* Approximate UTF-8 word counting (high-bit bytes = word chars) */
        if (c < 0x80)
        {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' ||
                c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' ||
                c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' ||
                c == '"' || c == '\'' || c == '<' || c == '>' || c == '/' || c == '\\' ||
                c == '|' || c == '`' || c == '@' || c == '#' || c == '%' || c == '^' ||
                c == '&' || c == '*' || c == '+' || c == '=' || c == '~')
            {
                in_word = 0;
                s++;

                continue;
            }
        }

        if (!in_word)
        {
            in_word = 1;
            count++;
        }

        s++;
    }

    return count;
}

int pf_atomic_write(const char *path, const void *data, long len)
{
    char tmp[1024];
    FILE *f = NULL;
    size_t plen;

    if (!path || !data || len < 0)
        return -1;

    plen = strlen(path);

    if (plen + 5 >= sizeof(tmp))
        return -1;

    memcpy(tmp, path, plen);
    memcpy(tmp + plen, ".tmp", 5); /* includes NUL */

    f = fopen(tmp, "wb");

    if (!f)
        return -1;

    if (len > 0)
    {
        if (fwrite(data, 1, (size_t)len, f) != (size_t)len)
        {
            fclose(f);
            pf_remove_file(tmp);
            return -1;
        }
    }

    if (fclose(f) != 0)
    {
        pf_remove_file(tmp);
        return -1;
    }

    if (pf_atomic_rename(tmp, path) != 0)
    {
        pf_remove_file(tmp);
        return -1;
    }

    return 0;
}

void pf_swap_path(const char *path, char *out, size_t outsize)
{
    const char *base = NULL;
    const char *slash = NULL;
    size_t dir_len;

    if (!out || outsize == 0)
        return;

    out[0] = '\0';

    if (!path)
        return;

    /* Find last path separator */
    slash = NULL;
    {
        const char *p;

        for (p = path; *p; p++)
        {
#if defined(PLATFORM_WIN32)
            if (*p == '/' || *p == '\\')
                slash = p;
#elif defined(PLATFORM_AMIGA)
            if (*p == '/' || *p == ':')
                slash = p;
#else
            if (*p == '/')
                slash = p;
#endif
        }
    }

    if (slash)
    {
        dir_len = (size_t)(slash - path) + 1;
        base = slash + 1;
    }
    else
    {
        dir_len = 0;
        base = path;
    }

    /* Build "<dir>/.<base>.swp" */
    if (dir_len > 0)
    {
        if (dir_len >= outsize)
            return;

        memcpy(out, path, dir_len);
        out[dir_len] = '\0';
    }

    {
        size_t need = strlen(out) + 1 /* . */ + strlen(base) + 4 /* .swp */ + 1;

        if (need >= outsize)
        {
            out[0] = '\0';
            return;
        }

        strcat(out, ".");
        strcat(out, base);
        strcat(out, ".swp");
    }
}

/* Test if `name` ends in any extension from ext_csv. Empty csv = all */
static int pf_grep_ext_match(const char *name, const char *ext_csv)
{
    const char *p = NULL;
    const char *end = NULL;
    size_t name_len;

    if (!ext_csv || !ext_csv[0])
        return 1; /* no filter */

    name_len = strlen(name);
    p = ext_csv;

    while (*p)
    {
        size_t el;

        while (*p == ',' || *p == ' ' || *p == '\t')
            p++;

        end = p;

        while (*end && *end != ',')
            end++;

        el = (size_t)(end - p);

        if (el > 0 && el <= name_len)
        {
            const char *tail = name + name_len - el;
            size_t i;
            int ok = 1;

            for (i = 0; i < el; i++)
            {
                char a = tail[i], b = p[i];

                if (a >= 'A' && a <= 'Z')
                    a = (char)(a + 32);

                if (b >= 'A' && b <= 'Z')
                    b = (char)(b + 32);

                if (a != b)
                {
                    ok = 0;
                    break;
                }
            }

            if (ok)
                return 1;
        }

        p = (*end) ? end + 1 : end;
    }

    return 0;
}

/* Case-insensitive ASCII substring search */
static char *pf_grep_stristr(const char *hay, const char *ned)
{
    size_t nl = strlen(ned);

    if (!nl)
        return (char *)hay;

    for (; *hay; hay++)
    {
        size_t i;
        int ok = 1;

        for (i = 0; i < nl; i++)
        {
            char a = hay[i], b = ned[i];

            if (a == 0)
                return NULL;

            if (a >= 'A' && a <= 'Z')
                a = (char)(a + 32);

            if (b >= 'A' && b <= 'Z')
                b = (char)(b + 32);

            if (a != b)
            {
                ok = 0;
                break;
            }
        }

        if (ok)
            return (char *)hay;
    }

    return NULL;
}

/* Skip dotted and build/SCM noise directories */
static int pf_grep_skip_dir(const char *name)
{
    if (!name || !name[0])
        return 1;

    /* . , .. , .git, .svn, .hg, ... */
    /*if (name[0] == '.')
        return 1;*/

    return 0;
}

/* Scan a single text file for needle */
static int pf_grep_scan_file(const char *path, const char *needle, pf_grep_cb_t cb, void *user, int *stopped)
{
    FILE *f = NULL;
    char line[2048];
    int line_no = 0;
    int hits = 0;

    f = fopen(path, "rb");

    if (!f)
        return 0;

    while (fgets(line, sizeof(line), f))
    {
        size_t n;

        line_no++;

        /* Strip trailing newline */
        n = strlen(line);

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';

        if (pf_grep_stristr(line, needle))
        {
            hits++;

            if (cb && cb(user, path, line_no, line) != 0)
            {
                *stopped = 1;
                break;
            }
        }
    }

    fclose(f);
    return hits;
}

#if defined(PLATFORM_WIN32)
/* Win32: use FindFirstFileA / FindNextFileA. Path separators '\\' allowed */
static int pf_grep_walk(const char *dir, const char *ext_csv, const char *needle, int depth, int max_depth, pf_grep_cb_t cb, void *user, int *stopped)
{
    char pattern[1024];
    char child[1024];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int total = 0;

    if (depth > max_depth)
        return 0;

    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    h = FindFirstFileA(pattern, &fd);

    if (h == INVALID_HANDLE_VALUE)
        return 0;

    do
    {
        if (*stopped)
            break;

        /* Skip ".", "..", ".git", "node_modules", build dirs etc */
        if (pf_grep_skip_dir(fd.cFileName))
            continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            snprintf(child, sizeof(child), "%s\\%s", dir, fd.cFileName);
            total += pf_grep_walk(child, ext_csv, needle, depth + 1, max_depth, cb, user, stopped);
        }
        else
        {
            if (pf_grep_ext_match(fd.cFileName, ext_csv))
            {
                snprintf(child, sizeof(child), "%s\\%s", dir, fd.cFileName);
                total += pf_grep_scan_file(child, needle, cb, user, stopped);
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return total;
}

#elif defined(PLATFORM_AMIGA)
/* Amiga: use dos.library ExNext/Examine via the directory lock */
static int pf_grep_walk(const char *dir, const char *ext_csv, const char *needle, int depth, int max_depth, pf_grep_cb_t cb, void *user, int *stopped)
{
    BPTR lock;
    struct FileInfoBlock *fib = NULL;
    char child[1024];
    int total = 0;

    if (depth > max_depth)
        return 0;

    lock = Lock((STRPTR)(dir[0] ? dir : ""), ACCESS_READ);

    if (!lock)
        return 0;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

    if (!fib)
    {
        UnLock(lock);
        return 0;
    }

    if (!Examine(lock, fib) || fib->fib_DirEntryType <= 0)
    {
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        return 0;
    }

    while (ExNext(lock, fib))
    {
        int is_dir;

        if (*stopped)
            break;

        /* Skip ".", "..", ".git", "node_modules", build dirs etc */
        if (pf_grep_skip_dir(fib->fib_FileName))
            continue;

        snprintf(child, sizeof(child), "%s/%s", dir, fib->fib_FileName);

        is_dir = (fib->fib_DirEntryType > 0) ? 1 : 0;

        if (is_dir)
        {
            total += pf_grep_walk(child, ext_csv, needle, depth + 1, max_depth, cb, user, stopped);
        }
        else
        {
            if (pf_grep_ext_match(fib->fib_FileName, ext_csv))
                total += pf_grep_scan_file(child, needle, cb, user, stopped);
        }
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return total;
}

#else /* POSIX */

static int pf_grep_walk(const char *dir, const char *ext_csv, const char *needle, int depth, int max_depth, pf_grep_cb_t cb, void *user, int *stopped)
{
    DIR *dp = NULL;
    struct dirent *e;
    char child[1024];
    int total = 0;

    if (depth > max_depth)
        return 0;

    dp = opendir(dir[0] ? dir : ".");

    if (!dp)
        return 0;

    while ((e = readdir(dp)) != NULL)
    {
        struct stat st;

        if (*stopped)
            break;

        /* Skip noise directories during recursion */
        if (pf_grep_skip_dir(e->d_name))
            continue;

        snprintf(child, sizeof(child), "%s/%s", dir, e->d_name);

        if (stat(child, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            total += pf_grep_walk(child, ext_csv, needle, depth + 1, max_depth, cb, user, stopped);
        }
        else if (S_ISREG(st.st_mode))
        {
            if (pf_grep_ext_match(e->d_name, ext_csv))
                total += pf_grep_scan_file(child, needle, cb, user, stopped);
        }
    }

    closedir(dp);
    return total;
}
#endif

int pf_grep_files(const char *root_dir, const char *ext_csv, const char *needle, int max_depth, pf_grep_cb_t cb, void *user)
{
    int stopped = 0;

    if (!root_dir || !needle || !needle[0])
        return -1;

    if (max_depth < 0)
        max_depth = 5;

    if (max_depth > 20)
        max_depth = 20;

    return pf_grep_walk(root_dir, ext_csv ? ext_csv : "", needle, 0, max_depth, cb, user, &stopped);
}
