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

/* clipboard.c -- System clipboard paste (Linux + AmigaOS + Windows) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clipboard.h"
#include "portable.h"

#ifdef PLATFORM_WIN32
#include <windows.h>
#endif

#ifdef PLATFORM_UNIX
#include <unistd.h>
#include <sys/wait.h>
#include <ncurses.h>
#include <stdlib.h>
#endif

#ifdef PLATFORM_UNIX
/* Simple base64 encoder */
static char *base64_encode(const char *src)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *out = NULL;
    int len;
    int i;
    int j;
    unsigned int a;
    unsigned int b;
    unsigned int c;
    unsigned int triple;

    if (!src)
        return NULL;

    len = strlen(src);
    out = (char *)malloc(((len + 2) / 3) * 4 + 1);

    if (!out)
        return NULL;

    for (i = 0, j = 0; i < len;)
    {
        a = (unsigned char)src[i++];
        b = (i < len) ? (unsigned char)src[i++] : 0;
        c = (i < len) ? (unsigned char)src[i++] : 0;

        triple = (a << 16) | (b << 8) | c;

        out[j++] = table[(triple >> 18) & 0x3F];
        out[j++] = table[(triple >> 12) & 0x3F];
        out[j++] = table[(triple >> 6) & 0x3F];
        out[j++] = table[triple & 0x3F];
    }

    while (j % 4)
        out[j++] = '=';

    out[j] = '\0';

    return out;
}

/* Copy to terminal clipboard using OSC 52 (works over SSH) */
static int clipboard_copy_osc52(const char *utf8)
{
    char *b64 = NULL;

    if (!utf8 || !utf8[0])
        return -1;

    b64 = base64_encode(utf8);

    if (!b64)
        return -1;

    printf("\033]52;c;%s\007", b64);
    fflush(stdout);

    free(b64);

    return 0;
}
#endif /* PLATFORM_UNIX */

static char *normalise_newlines(char *s)
{
    /* Convert CRLF / CR to LF in place, strip trailing NULs */
    char *r = NULL;
    char *w = NULL;

    if (!s)
        return NULL;

    r = w = s;

    while (*r)
    {
        if (r[0] == '\r' && r[1] == '\n')
        {
            *w++ = '\n';
            r += 2;
        }
        else if (r[0] == '\r')
        {
            *w++ = '\n';
            r++;
        }
        else
        {
            *w++ = *r++;
        }
    }

    *w = '\0';

    return s;
}

#ifdef PLATFORM_UNIX

/* Detect if running in SSH session (no X11/Wayland clipboard available) */
static int is_ssh_session(void)
{
    /* If SSH_TTY or SSH_CONNECTION are set, we're likely in SSH */
    if (getenv("SSH_TTY") || getenv("SSH_CONNECTION"))
        return 1;

    /* If DISPLAY and WAYLAND_DISPLAY are both not set, likely SSH or headless */
    if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY"))
        return 1;

    return 0;
}

#endif /* PLATFORM_UNIX */

#ifdef PLATFORM_AMIGA

/* AmigaOS clipboard via iffparse.library (minimal, no Delay/retry) */

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/clipboard.h>
#include <libraries/iffparse.h>
#include <proto/exec.h>
#include <proto/iffparse.h>

extern struct Library *IFFParseBase;

#ifndef MAKE_ID
#define MAKE_ID(a, b, c, d) \
    ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#endif

#define ID_FTXT MAKE_ID('F', 'T', 'X', 'T')
#define ID_CHRS MAKE_ID('C', 'H', 'R', 'S')

#define CLIPBOARD_MAX_BYTES (16UL * 1024UL * 1024UL)

char *clipboard_paste(void)
{
    struct IFFHandle *iff = NULL;
    struct ClipboardHandle *clip = NULL;
    char *out = NULL;
    char *malloc_copy = NULL;
    ULONG total = 0;
    ULONG alloc = 0;
    LONG error;

    if (!IFFParseBase)
        return NULL;

    iff = AllocIFF();

    if (!iff)
        return NULL;

    clip = OpenClipboard(0);

    if (!clip)
    {
        FreeIFF(iff);
        return NULL;
    }

    iff->iff_Stream = (ULONG)clip;

    InitIFFasClip(iff);

    if (OpenIFF(iff, IFFF_READ) != 0)
    {
        CloseClipboard(clip);
        FreeIFF(iff);
        return NULL;
    }

    StopChunk(iff, ID_FTXT, ID_CHRS);

    while ((error = ParseIFF(iff, IFFPARSE_SCAN)) == 0)
    {
        struct ContextNode *cn = CurrentChunk(iff);
        ULONG size;
        char *grown = NULL;
        ULONG new_alloc;
        LONG got;

        if (!cn || cn->cn_ID != ID_CHRS)
            continue;

        size = (ULONG)cn->cn_Size;

        if (size == 0)
            continue;

        if (size > CLIPBOARD_MAX_BYTES)
            break;

        if (total > CLIPBOARD_MAX_BYTES - size - 1)
            break;

        if (total + size + 1 > alloc)
        {
            new_alloc = alloc ? alloc * 2 : 1024;

            while (new_alloc < total + size + 1)
                new_alloc *= 2;

            if (new_alloc > CLIPBOARD_MAX_BYTES + 1)
                new_alloc = CLIPBOARD_MAX_BYTES + 1;

            grown = (char *)AllocVec(new_alloc, MEMF_PUBLIC);

            if (!grown)
                break;

            if (out)
            {
                memcpy(grown, out, total);
                FreeVec(out);
            }

            out = grown;
            alloc = new_alloc;
        }

        got = ReadChunkBytes(iff, out + total, (LONG)size);

        if (got < 0)
            break;

        total += (ULONG)got;

        if ((ULONG)got != size)
            break;
    }

    CloseIFF(iff);
    CloseClipboard(clip);

    FreeIFF(iff);

    if (!out)
        return NULL;

    out[total] = '\0';

    malloc_copy = (char *)malloc(total + 1);

    if (malloc_copy)
    {
        memcpy(malloc_copy, out, total + 1);
        FreeVec(out);

        return normalise_newlines(malloc_copy);
    }

    FreeVec(out);

    return NULL;
}

int clipboard_copy(const char *utf8)
{
    struct IFFHandle *iff = NULL;
    struct ClipboardHandle *clip = NULL;
    LONG len;
    int result = -1;

    if (!utf8 || !utf8[0])
        return -1;

    if (!IFFParseBase)
        return -1;

    len = (LONG)strlen(utf8);

    if ((ULONG)len > CLIPBOARD_MAX_BYTES)
        len = (LONG)CLIPBOARD_MAX_BYTES;

    clip = OpenClipboard(0);

    if (!clip)
        return -1;

    iff = AllocIFF();

    if (!iff)
    {
        CloseClipboard(clip);
        return -1;
    }

    iff->iff_Stream = (ULONG)clip;
    InitIFFasClip(iff);

    if (OpenIFF(iff, IFFF_WRITE) != 0)
    {
        CloseClipboard(clip);
        FreeIFF(iff);
        return -1;
    }

    if (PushChunk(iff, ID_FTXT, ID_FORM, IFFSIZE_UNKNOWN) == 0)
    {
        if (PushChunk(iff, 0, ID_CHRS, len) == 0)
        {
            if (WriteChunkBytes(iff, (APTR)utf8, len) == len)
                result = 0;

            PopChunk(iff);
        }

        PopChunk(iff);
    }

    CloseIFF(iff);
    CloseClipboard(clip);
    FreeIFF(iff);

    return result;
}

#endif /* PLATFORM_AMIGA */

#ifdef PLATFORM_WIN32

int clipboard_copy(const char *utf8)
{
    HGLOBAL hglb;
    char *lptstr = NULL;

    if (!utf8 || !utf8[0])
        return -1;

    if (!OpenClipboard(NULL))
        return -1;

    EmptyClipboard();

    hglb = GlobalAlloc(GMEM_MOVEABLE, strlen(utf8) + 1);

    if (!hglb)
    {
        CloseClipboard();
        return -1;
    }

    lptstr = (char *)GlobalLock(hglb);
    memcpy(lptstr, utf8, strlen(utf8) + 1);
    GlobalUnlock(hglb);

    SetClipboardData(CF_TEXT, hglb);
    CloseClipboard();

    return 0;
}

char *clipboard_paste(void)
{
    HGLOBAL hglb;
    char *lptstr = NULL;
    char *out = NULL;

    if (!IsClipboardFormatAvailable(CF_TEXT))
        return NULL;

    if (!OpenClipboard(NULL))
        return NULL;

    hglb = GetClipboardData(CF_TEXT);

    if (hglb)
    {
        lptstr = (char *)GlobalLock(hglb);

        if (lptstr)
        {
            out = strdup(lptstr);
            GlobalUnlock(hglb);
        }
    }

    CloseClipboard();

    return normalise_newlines(out);
}

#endif /* PLATFORM_WIN32 */

#ifdef PLATFORM_UNIX

/* POSIX implementation: try xclip, xsel, wl-paste, pbpaste */
#include <unistd.h>

static char *slurp(FILE *fp)
{
    char *buf = NULL;
    size_t len = 0, cap = 0;
    int c;

    while ((c = fgetc(fp)) != EOF)
    {
        if (len + 1 >= cap)
        {
            size_t ncap = cap ? cap * 2 : 1024;
            char *nb = (char *)realloc(buf, ncap);

            if (!nb)
            {
                free(buf);
                return NULL;
            }

            buf = nb;
            cap = ncap;
        }

        buf[len++] = (char)c;
    }

    if (!buf)
        return NULL;

    buf[len] = '\0';

    return buf;
}

static char *try_cmd(const char *cmd)
{
    FILE *fp = NULL;
    char *out = NULL;
    fp = popen(cmd, "r");

    if (!fp)
        return NULL;

    out = slurp(fp);

    if (pclose(fp) != 0)
    {
        /* Tool failed or not installed */
        free(out);
        return NULL;
    }

    if (out && !out[0])
    {
        free(out);
        return NULL;
    }

    return out;
}

char *clipboard_paste(void)
{
    /* Try Wayland, X11 (xclip/xsel), MacOS in order, stderr suppressed */
    static const char *const cmds[] =
        {
            "wl-paste --no-newline 2>/dev/null",
            "xclip -selection clipboard -o 2>/dev/null",
            "xsel --clipboard --output 2>/dev/null",
            "pbpaste 2>/dev/null",
            NULL};

    int i;

    for (i = 0; cmds[i]; i++)
    {
        char *r = try_cmd(cmds[i]);

        if (r)
            return normalise_newlines(r);
    }

    return NULL;
}

static int try_copy_cmd(const char *cmd, const char *data)
{
    size_t len;
    char tmp_file[256];
    int tmp_fd;
    FILE *tmp_fp = NULL;
    pid_t pid;
    int status;

    /* Create temp file with mkstemp */
    snprintf(tmp_file, sizeof(tmp_file), "/tmp/tinyedit_clipboard_XXXXXX");
    tmp_fd = mkstemp(tmp_file);

    if (tmp_fd == -1)
        return -1;

    tmp_fp = fdopen(tmp_fd, "w");

    if (!tmp_fp)
    {
        close(tmp_fd);
        return -1;
    }

    len = strlen(data);

    if (fwrite(data, 1, len, tmp_fp) != len)
    {
        fclose(tmp_fp);
        pf_remove_file(tmp_file);
        return -1;
    }

    fclose(tmp_fp);

    /* Save ncurses state before fork */
    def_prog_mode();
    endwin();

    /* Fork and exec */
    pid = fork();

    if (pid == -1)
    {
        reset_prog_mode();
        refresh();
        pf_remove_file(tmp_file);
        return -1;
    }

    if (pid == 0)
    {
        /* Child: redirect file to stdin of clipboard tool */
        char cmd_with_file[512];

        snprintf(cmd_with_file, sizeof(cmd_with_file), "%s < %s", cmd, tmp_file);
        execl("/bin/sh", "sh", "-c", cmd_with_file, (char *)NULL);
        _exit(1);
    }

    /* Parent: wait for child */
    waitpid(pid, &status, 0);

    /* Restore ncurses state */
    reset_prog_mode();
    refresh();

    pf_remove_file(tmp_file);

    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

#endif /* PLATFORM_UNIX */

/* Returns 1 if external clipboard should be used, 0 for internal only */
int clipboard_use_external(void)
{
#ifdef PLATFORM_AMIGA
    /* Amiga: always use external clipboard */
    return 1;
#elif defined(PLATFORM_WIN32)
    /* Windows: always use external clipboard */
    return 1;
#elif defined(PLATFORM_UNIX)
    /* Unix: use external only if not in SSH session */
    return !is_ssh_session();
#else
    /* Unknown platform: use external */
    return 1;
#endif
}

#ifdef PLATFORM_UNIX

int clipboard_copy(const char *utf8)
{
    /* Fallback to Wayland, X11 (xclip/xsel), MacOS */
    static const char *const cmds[] =
        {
            "wl-copy 2>/dev/null",
            "xclip -selection clipboard -i 2>/dev/null",
            "xsel --clipboard --input 2>/dev/null",
            "pbcopy 2>/dev/null",
            NULL};

    int i;

    if (!utf8 || !utf8[0])
        return -1;

    for (i = 0; cmds[i]; i++)
    {
        if (try_copy_cmd(cmds[i], utf8) == 0)
            return 0;
    }

    /* Try OSC 52 first (works over SSH) */
    if (clipboard_copy_osc52(utf8) == 0)
        return 0;

    return -1;
}

#endif /* PLATFORM_UNIX */
