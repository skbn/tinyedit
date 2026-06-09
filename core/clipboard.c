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

#ifdef PLATFORM_WIN32
#include <windows.h>
#endif

#ifdef PLATFORM_UNIX
#include <unistd.h>
#include <sys/wait.h>
#include <ncurses.h>
#endif

static char *normalise_newlines(char *s)
{
    /* Convert CRLF / CR to LF in place, strip trailing NULs */
    char *r, *w;

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

#ifdef PLATFORM_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/clipboard.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>

/* IFF FOURCC helpers */
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#define ID_FORM MAKE_ID('F', 'O', 'R', 'M')
#define ID_FTXT MAKE_ID('F', 'T', 'X', 'T')
#define ID_CHRS MAKE_ID('C', 'H', 'R', 'S')

static LONG clip_write(struct IOClipReq *io, APTR data, ULONG length)
{
    io->io_Command = CMD_WRITE;
    io->io_Data = (STRPTR)data;
    io->io_Length = length;

    DoIO((struct IORequest *)io);

    return io->io_Actual;
}

static LONG clip_write_long(struct IOClipReq *io, ULONG val)
{
    return clip_write(io, &val, sizeof(ULONG));
}

int clipboard_copy(const char *utf8)
{
    struct MsgPort *mp = NULL;
    struct IOClipReq *io = NULL;
    ULONG form_size;
    ULONG chrs_size;
    int result = -1;

    if (!utf8 || !utf8[0])
        return -1;

    mp = CreatePort(NULL, 0);

    if (!mp)
        return -1;

    io = (struct IOClipReq *)CreateExtIO(mp, sizeof(struct IOClipReq));

    if (!io)
    {
        DeletePort(mp);
        return -1;
    }

    if (OpenDevice((STRPTR) "clipboard.device", 0, (struct IORequest *)io, 0))
    {
        DeleteExtIO((struct IORequest *)io);
        DeletePort(mp);
        return -1;
    }

    io->io_ClipID = 0;
    io->io_Offset = 0;
    io->io_Error = 0;

    chrs_size = (ULONG)strlen(utf8);

    if (chrs_size == 0)
    {
        result = 0;
    }
    else
    {
        /* IFF FORM/FTXT/CHRS: form_size = 4 (FTXT) + 8 (CHRS hdr) + chrs_size + pad */
        ULONG chrs_pad = (chrs_size & 1) ? 1 : 0;

        form_size = 4 /* FTXT */ + 8 /* CHRS hdr */ + chrs_size + chrs_pad;

        /* Write to clipboard via sequential clip_write calls */
        io->io_ClipID = 0;
        io->io_Offset = 0;

        if (clip_write_long(io, ID_FORM) == sizeof(ULONG) &&
            clip_write_long(io, form_size) == sizeof(ULONG) &&
            clip_write_long(io, ID_FTXT) == sizeof(ULONG) &&
            clip_write_long(io, ID_CHRS) == sizeof(ULONG) &&
            clip_write_long(io, chrs_size) == sizeof(ULONG) &&
            clip_write(io, (APTR)utf8, chrs_size) == (LONG)chrs_size)
        {
            /* Odd-length CHRS chunk needs a pad byte */
            if (chrs_pad)
            {
                UBYTE pad = 0;
                clip_write(io, &pad, 1);
            }

            result = 0;
        }
    }

    CloseDevice((struct IORequest *)io);
    DeleteExtIO((struct IORequest *)io);
    DeletePort(mp);

    return result;
}

static LONG clip_read(struct IOClipReq *io, APTR data, ULONG length)
{
    /* Sequential read: device tracks position by io_ClipID, don't reset */
    io->io_Command = CMD_READ;
    io->io_Data = (STRPTR)data;
    io->io_Length = length;

    DoIO((struct IORequest *)io);

    return io->io_Actual;
}

static void clip_skip(struct IOClipReq *io, ULONG n)
{
    char dump[64];

    while (n > 0)
    {
        ULONG step = n > sizeof(dump) ? sizeof(dump) : n;

        if (clip_read(io, dump, step) != (LONG)step)
            return;

        n -= step;
    }
}

/* Walk IFF FORM/FTXT and concatenate CHRS payloads
 * Returns malloc'd UTF-8 buffer or NULL */
static char *read_ftxt_payload(struct IOClipReq *io)
{
    ULONG hdr[3]; /* FORM, size, FTXT */
    ULONG type, size;
    char *out = NULL;
    size_t out_len = 0;
    LONG remaining;
    int ok = 1;

    if (clip_read(io, hdr, sizeof(hdr)) != (LONG)sizeof(hdr))
        return NULL;

    if (hdr[0] != ID_FORM || hdr[2] != ID_FTXT)
        return NULL;

    remaining = (LONG)hdr[1] - 4;

    while (ok && remaining >= 8)
    {
        ULONG chdr[2];

        if (clip_read(io, chdr, sizeof(chdr)) != (LONG)sizeof(chdr))
            break;

        type = chdr[0];
        size = chdr[1];
        remaining -= 8;

        if (size > (ULONG)remaining)
            size = (ULONG)remaining;

        if (type == ID_CHRS && size > 0)
        {
            size_t new_cap = out_len + size + 1;

            /* Use exponential growth to reduce number of reallocs */
            if (new_cap < out_len * 2)
                new_cap = out_len * 2;

            char *grown = (char *)realloc(out, new_cap);

            if (!grown)
            {
                free(out);
                out = NULL;
                ok = 0;
            }
            else
            {
                out = grown;

                if (clip_read(io, out + out_len, size) != (LONG)size)
                {
                    ok = 0;
                }
                else
                {
                    out_len += size;
                    out[out_len] = '\0';
                }
            }
        }
        else if (size > 0)
        {
            clip_skip(io, size);
        }

        remaining -= size;

        if ((size & 1) && remaining > 0)
        {
            clip_skip(io, 1);
            remaining--;
        }
    }

    return out;
}

char *clipboard_paste()
{
    struct MsgPort *mp;
    struct IOClipReq *io;
    char *out;

    mp = CreatePort(NULL, 0);

    if (!mp)
        return NULL;

    io = (struct IOClipReq *)CreateExtIO(mp, sizeof(struct IOClipReq));

    if (!io)
    {
        DeletePort(mp);
        return NULL;
    }

    if (OpenDevice((STRPTR) "clipboard.device", 0, (struct IORequest *)io, 0))
    {
        DeleteExtIO((struct IORequest *)io);
        DeletePort(mp);
        return NULL;
    }

    io->io_ClipID = 0;
    io->io_Offset = 0;
    io->io_Error = 0;
    out = read_ftxt_payload(io);

    CloseDevice((struct IORequest *)io);
    DeleteExtIO((struct IORequest *)io);
    DeletePort(mp);

    return normalise_newlines(out);
}

#elif defined(PLATFORM_WIN32)

int clipboard_copy(const char *utf8)
{
    HGLOBAL hglb;
    char *lptstr;

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

char *clipboard_paste()
{
    HGLOBAL hglb;
    char *lptstr;
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

#else

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
    FILE *fp;
    char *out;
    fp = popen(cmd, "r");

    if (!fp)
        return NULL;

    out = slurp(fp);

    if (pclose(fp) != 0)
    {
        /* Tool failed (not installed, or empty selection) */
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

char *clipboard_paste()
{
    /* Order: Wayland, X11 (xclip), X11 (xsel), MacOS
     * stderr suppressed so users without one of these don't see noise */
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
    FILE *tmp_fp;
    pid_t pid;
    int status;

    /* Create temporary file with mkstemp */
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
        unlink(tmp_file);
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
        unlink(tmp_file);
        return -1;
    }

    if (pid == 0)
    {
        /* Child process - redirect file to stdin of clipboard tool */
        char cmd_with_file[512];

        snprintf(cmd_with_file, sizeof(cmd_with_file), "%s < %s", cmd, tmp_file);
        execl("/bin/sh", "sh", "-c", cmd_with_file, (char *)NULL);
        _exit(1);
    }

    /* Parent process - wait for child */
    waitpid(pid, &status, 0);

    /* Restore ncurses state */
    reset_prog_mode();
    refresh();

    unlink(tmp_file);

    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

int clipboard_copy(const char *utf8)
{
    if (!utf8 || !utf8[0])
        return -1;

    /* Order: Wayland, X11 (xclip), X11 (xsel), MacOS
     * stderr suppressed so users without one of these don't see noise */
    static const char *const cmds[] =
        {
            "wl-copy 2>/dev/null",
            "xclip -selection clipboard -i 2>/dev/null",
            "xsel --clipboard --input 2>/dev/null",
            "pbcopy 2>/dev/null",
            NULL};

    int i;

    for (i = 0; cmds[i]; i++)
    {
        if (try_copy_cmd(cmds[i], utf8) == 0)
            return 0;
    }

    return -1;
}

#endif /* PLATFORM_AMIGA */
