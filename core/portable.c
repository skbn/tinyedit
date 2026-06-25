/*
 * tinyedit - Text editor for AmigaOS
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 or later.
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

    if (!path || !path[0])
        return 0;

    attr = GetFileAttributesA(path);
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
    struct FileInfoBlock *fib;
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

    if (!path || !path[0])
        return 0;

    attr = GetFileAttributesA(path);

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
    char *p;
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

    if (!path || !path[0])
        return -1;

    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info))
        return -1;

    size.LowPart = info.nFileSizeLow;
    size.HighPart = info.nFileSizeHigh;

    return (long)size.QuadPart;
#elif defined(PLATFORM_AMIGA)
    BPTR lock;
    struct FileInfoBlock *fib;
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

    if (!path || !path[0])
        return 0;

    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info))
        return 0;

    {
        FILETIME ft;
        ULARGE_INTEGER ull;

        ft = info.ftLastWriteTime;
        ull.LowPart = ft.dwLowDateTime;
        ull.HighPart = ft.dwHighDateTime;

        return (long)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
    }
#elif defined(PLATFORM_AMIGA)
    BPTR lock;
    struct FileInfoBlock *fib;
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
    WIN32_FIND_DATAA fd;
    int started;
    int eod;
    char name[260];
};

PfDir *pf_dir_open(const char *path)
{
    PfDir *d = NULL;
    char pattern[300];

    if (!path || !path[0])
        return NULL;

    if ((size_t)snprintf(pattern, sizeof(pattern), "%s\\*", path) >= sizeof(pattern))
        return NULL;

    d = (PfDir *)malloc(sizeof(*d));

    if (!d)
        return NULL;

    d->h = FindFirstFileA(pattern, &d->fd);

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

const char *pf_dir_next(PfDir *d)
{
    if (!d || d->eod)
        return NULL;

    for (;;)
    {
        if (d->started)
        {
            if (!FindNextFileA(d->h, &d->fd))
            {
                d->eod = 1;
                return NULL;
            }
        }
        else
        {
            d->started = 1;
        }

        if (d->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        if (d->fd.cFileName[0] == '.' && (d->fd.cFileName[1] == '\0' || (d->fd.cFileName[1] == '.' && d->fd.cFileName[2] == '\0')))
            continue;

        strncpy(d->name, d->fd.cFileName, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = '\0';

        return d->name;
    }
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

    if (!path || !path[0])
        return 0;

    attr = GetFileAttributesA(path);

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

const char *pf_dir_next(PfDir *d)
{
    if (!d)
        return NULL;

    while (ExNext(d->lock, d->fib))
    {
        if (d->fib->fib_DirEntryType > 0)
            continue;

        strncpy(d->name, d->fib->fib_FileName, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = '\0';

        return d->name;
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

const char *pf_dir_next(PfDir *d)
{
    struct dirent *e;
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

        if (!S_ISREG(st.st_mode))
            continue;

        strncpy(d->name, e->d_name, sizeof(d->name) - 1);
        d->name[sizeof(d->name) - 1] = '\0';

        return d->name;
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

    if (!path)
        return NULL;

    lk = (PfLockFile *)malloc(sizeof(*lk));

    if (!lk)
        return NULL;

    strncpy(lk->path, path, sizeof(lk->path) - 1);
    lk->path[sizeof(lk->path) - 1] = '\0';

    lk->handle = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

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
    if (!lk)
        return;

    if (lk->handle != INVALID_HANDLE_VALUE)
        CloseHandle(lk->handle);

    DeleteFileA(lk->path);
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
    {
        w = write(lk->fd, buf, (size_t)n);
        (void)w;
    }

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
    if (!from || !to)
        return -1;

    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
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
    return (path && DeleteFileA(path)) ? 0 : -1;
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
