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

/* ui_files.c -- Directory browser */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "te.h"
#include "ui_files.h"
#include "ui_editor_helper.h"
#include "../core/keys.h"
#include "../core/portable.h"

/* Per-platform directory listing */
#ifdef PLATFORM_WIN32
#include <windows.h>
#elif defined(PLATFORM_AMIGA)
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* Bounded path/entry sizes (AmigaOS supports long paths, cap at 1024 for stack) */
#define UI_FILES_PATH_MAX 1024
#define UI_FILES_MAX_ENTRIES 4096

/* One directory entry: name + is-directory flag */
typedef struct
{
    char *name;
    int is_dir;
} FileEnt;

/* Free name strings and the array itself */
static void free_entries(FileEnt *ents, int n)
{
    int i;

    if (!ents)
        return;

    for (i = 0; i < n; i++)
        free(ents[i].name);

    free(ents);
}

/* Initialize directory from start_dir or current working directory */
static void init_dir_from_start(const char *start_dir, char *dir_input, int dir_input_sz)
{
    if (start_dir && start_dir[0])
    {
        strncpy(dir_input, start_dir, dir_input_sz - 1);
        dir_input[dir_input_sz - 1] = '\0';
    }
    else
    {
#ifdef PLATFORM_WIN32
        wchar_t wdir[UI_FILES_PATH_MAX];
        if (GetCurrentDirectoryW((DWORD)(sizeof(wdir) / sizeof(wchar_t)), wdir))
        {
            char *u = pf_utf16_to_utf8(wdir);

            if (u)
            {
                strncpy(dir_input, u, dir_input_sz - 1);
                dir_input[dir_input_sz - 1] = '\0';
                free(u);
            }
            else
            {
                dir_input[0] = '.';
                dir_input[1] = '\0';
            }
        }
        else
        {
            dir_input[0] = '.';
            dir_input[1] = '\0';
        }
#elif defined(PLATFORM_AMIGA)
        BPTR cur = Lock((STRPTR) "", ACCESS_READ);
        dir_input[0] = '\0';

        if (cur)
        {
            NameFromLock(cur, (STRPTR)dir_input, (LONG)dir_input_sz);
            UnLock(cur);
        }
#else
        if (!getcwd(dir_input, dir_input_sz))
        {
            dir_input[0] = '.';
            dir_input[1] = '\0';
        }
#endif
    }
}

/* Convert char string (UTF-8) to wchar_t */
static void char_to_wchar(const char *src, wchar_t *dst, int dst_sz)
{
    int i, j;

    for (i = 0; i < dst_sz; i++)
        dst[i] = L'\0';

    for (i = 0, j = 0; src[i] && j < dst_sz - 1; i++)
    {
        unsigned char c = (unsigned char)src[i];

        if (c < 0x80)
        {
            /* ASCII: 1 byte */
            dst[j++] = (wchar_t)c;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            /* 2-byte UTF-8 */
            if (src[i + 1] && j < dst_sz - 1)
            {
                dst[j++] = (wchar_t)(((c & 0x1F) << 6) | (src[i + 1] & 0x3F));
                i++;
            }
        }
        else if ((c & 0xF0) == 0xE0)
        {
            /* 3-byte UTF-8 */
            if (src[i + 1] && src[i + 2] && j < dst_sz - 1)
            {
                dst[j++] = (wchar_t)(((c & 0x0F) << 12) | ((src[i + 1] & 0x3F) << 6) | (src[i + 2] & 0x3F));
                i += 2;
            }
        }
        else if ((c & 0xF8) == 0xF0)
        {
            /* 4-byte UTF-8 */
            if (src[i + 1] && src[i + 2] && src[i + 3] && j < dst_sz - 1)
            {
                dst[j++] = (wchar_t)(((c & 0x07) << 18) | ((src[i + 1] & 0x3F) << 12) | ((src[i + 2] & 0x3F) << 6) | (src[i + 3] & 0x3F));
                i += 3;
            }
        }
    }

    dst[j] = L'\0';
}

/* Convert wchar_t string to char (UTF-8) */
static void wchar_to_char(const wchar_t *src, char *dst, int dst_sz)
{
    int i, j;

    for (i = 0; i < dst_sz; i++)
        dst[i] = '\0';

    for (i = 0, j = 0; src[i] && j < dst_sz - 1; i++)
    {
        wchar_t wc = src[i];

        if (wc < 0x80)
        {
            /* ASCII: 1 byte */
            dst[j++] = (char)wc;
        }
        else if (wc < 0x800)
        {
            /* 2-byte UTF-8 */
            if (j + 2 >= dst_sz - 1)
                break;
            dst[j++] = (char)(0xC0 | (wc >> 6));
            dst[j++] = (char)(0x80 | (wc & 0x3F));
        }
        else if (wc < 0x10000)
        {
            /* 3-byte UTF-8 */
            if (j + 3 >= dst_sz - 1)
                break;
            dst[j++] = (char)(0xE0 | (wc >> 12));
            dst[j++] = (char)(0x80 | ((wc >> 6) & 0x3F));
            dst[j++] = (char)(0x80 | (wc & 0x3F));
        }
        else
        {
            /* 4-byte UTF-8 */
            if (j + 4 >= dst_sz - 1)
                break;
            dst[j++] = (char)(0xF0 | (wc >> 18));
            dst[j++] = (char)(0x80 | ((wc >> 12) & 0x3F));
            dst[j++] = (char)(0x80 | ((wc >> 6) & 0x3F));
            dst[j++] = (char)(0x80 | (wc & 0x3F));
        }
    }

    dst[j] = '\0';
}

/* Draw file list with selection highlighting */
static void draw_file_list(FileEnt *ents, int nents, int sel, int top, int visible, int y, int x, int w, int is_list_active)
{
    int i, j;

    for (i = 0; i < visible && top + i < nents; i++)
    {
        int idx = top + i;
        const FileEnt *en = &ents[idx];
        char line[256];

        /* Clear line with correct background color */
        attrset(COLOR_PAIR(idx == sel && is_list_active ? COL_POPUP_SEL : COL_POPUP));

        for (j = 0; j < w - 4; j++)
            mvaddch(y + i, x + 2 + j, ' ');

        if (en->is_dir)
            snprintf(line, sizeof(line), "[%s]", en->name);
        else
            snprintf(line, sizeof(line), " %s ", en->name);

        mvaddnstr(y + i, x + 2, line, w - 4);

        standend();
    }
}

/* Handle list navigation keys, returns 1 if handled, 0 otherwise */
static int handle_list_navigation(int key, int *sel, int *top, int nents, int visible)
{
    if (key == KEY_UP)
    {
        if (*sel > 0)
        {
            (*sel)--;

            if (*sel < *top)
                *top = *sel;
        }

        return 1;
    }
    else if (key == KEY_DOWN)
    {
        if (*sel < nents - 1)
        {
            (*sel)++;

            if (*sel >= *top + visible)
                *top = *sel - visible + 1;
        }

        return 1;
    }
    else if (key == KEY_PPAGE || key == CTRL('U'))
    {
        *sel -= visible;

        if (*sel < 0)
            *sel = 0;

        if (*sel < *top)
            *top = *sel;

        return 1;
    }
    else if (key == KEY_NPAGE || key == CTRL('D'))
    {
        *sel += visible;

        if (*sel >= nents)
            *sel = nents - 1;

        if (*sel >= *top + visible)
            *top = *sel - visible + 1;

        return 1;
    }
    else if (key == KEY_HOME || key == CTRL('B'))
    {
        *sel = 0;
        *top = 0;

        return 1;
    }
    else if (key == KEY_END || key == CTRL('E'))
    {
        *sel = nents - 1;
        *top = *sel - visible + 1;

        if (*top < 0)
            *top = 0;

        return 1;
    }

    return 0;
}

/* Read dir into malloc'd FileEnt[], *out_n = count (NULL on error/empty, dirs first, alpha sorted) */
static FileEnt *load_dir(const char *dir, int *out_n)
{
    PfDir *d = NULL;
    FileEnt *ents = NULL;
    int n = 0, cap = 0;
    int i, j;
    int at_root = 0;
    int dlen;
    const char *colon;

    *out_n = 0;

    if (!dir || !dir[0])
        dir = ".";

    d = pf_dir_open(dir);
    if (!d)
        return NULL;

    /* Determine if we are at a root directory */
    dlen = (int)strlen(dir);
#ifdef PLATFORM_WIN32
    at_root = (dlen <= 3); /* "C:\" or "C:/" */
#elif defined(PLATFORM_AMIGA)
    colon = strchr(dir, ':');
    if (colon)
    {
        const char *after = colon + 1;

        while (*after == '/')
            after++;

        at_root = (*after == '\0');
    }
#else
    at_root = (strcmp(dir, "/") == 0);
#endif

    if (!at_root)
    {
        FileEnt *nb = (FileEnt *)realloc(ents, sizeof(FileEnt));

        if (nb)
        {
            ents = nb;
            cap = 1;

            ents[0].name = strdup("..");
            ents[0].is_dir = 1;

            n = ents[0].name ? 1 : 0;
        }
    }

    for (;;)
    {
        int is_dir;
        const char *name = pf_dir_next_entry(d, &is_dir);

        if (!name || n >= UI_FILES_MAX_ENTRIES)
            break;

        if (n >= cap)
        {
            int nc = cap ? cap * 2 : 32;
            FileEnt *nb = (FileEnt *)realloc(ents, (size_t)nc * sizeof(FileEnt));

            if (!nb)
            {
                for (i = 0; i < n; i++)
                    free(ents[i].name);

                free(ents);
                pf_dir_close(d);
                return NULL;
            }

            ents = nb;
            cap = nc;
        }

        ents[n].name = strdup(name);

        if (!ents[n].name)
            break;

        ents[n].is_dir = is_dir;
        n++;
    }

    pf_dir_close(d);

    /* Bubble sort: dirs first, then files, O(N^2) but N small */
    for (i = 0; i < n; i++)
    {
        for (j = i + 1; j < n; j++)
        {
            int swap = 0;

            if (ents[i].is_dir != ents[j].is_dir)
            {
                if (ents[j].is_dir && !ents[i].is_dir)
                    swap = 1;
            }
            else if (strcmp(ents[i].name, ents[j].name) > 0)
            {
                swap = 1;
            }

            if (swap)
            {
                FileEnt tmp = ents[i];
                ents[i] = ents[j];
                ents[j] = tmp;
            }
        }
    }

    *out_n = n;

    return ents;
}

/* Navigate to parent directory in-place; handles Windows/POSIX/Amiga paths */
static void path_go_parent(char *path)
{
    int len, i;

    if (!path || !path[0])
        return;

    len = (int)strlen(path);

    /* Strip trailing separator first (e.g. "C:\foo\" -> "C:\foo") */
    while (len > 1 && (path[len - 1] == '/' || path[len - 1] == '\\'))
        path[--len] = '\0';

    /* Scan back for the last separator or Amiga volume colon */
    for (i = len - 1; i >= 0; i--)
    {
        if (path[i] == '/' || path[i] == '\\')
        {
            if (i == 0)
            {
                path[1] = '\0'; /* POSIX root "/" */
            }
            else if (i == 2 && path[1] == ':')
            {
                path[i] = '\\'; /* keep "C:\" */
                path[i + 1] = '\0';
            }
            else
            {
                path[i] = '\0';
            }

            return;
        }

        if (path[i] == ':')
        {
            path[i + 1] = '\0'; /* Amiga: "Work:sub" -> "Work:" */
            return;
        }
    }
}

/* Append /name to base in-place (0=ok, -1=too long) */
static int path_append(char *base, int basesz, const char *name)
{
    int bl = (int)strlen(base);
    int nl = (int)strlen(name);
    char sep;
    int need_sep;

#ifdef PLATFORM_WIN32
    /* Windows: backslash separator, treat '/' as existing sep too, ':' is boundary */
    sep = '\\';
    need_sep = (bl > 0 && base[bl - 1] != '\\' && base[bl - 1] != '/' && base[bl - 1] != ':');
#else
    /* POSIX/Amiga: '/' separator, ':' is volume boundary */
    sep = '/';
    need_sep = (bl > 0 && base[bl - 1] != '/' && base[bl - 1] != ':');
#endif

    if (bl + (need_sep ? 1 : 0) + nl + 1 > basesz)
        return -1;

    if (need_sep)
    {
        base[bl++] = sep;
        base[bl] = '\0';
    }

    memcpy(base + bl, name, (size_t)nl + 1);

    return 0;
}

/* Minimal frame painter: clears interior, draws box, optional title (avoids public draw_popup_frame) */
static void draw_frame(int y, int x, int h, int w, const char *title)
{
    int i, j;

    attrset(COLOR_PAIR(COL_POPUP));

    for (i = 0; i < h; i++)
    {
        move(y + i, x);

        for (j = 0; j < w; j++)
            addch(' ');
    }

    ui_box(y, x, h, w);

    if (title && title[0])
    {
        int tl = (int)strlen(title);
        int tx = x + (w - tl - 2) / 2;

        if (tx < x + 1)
            tx = x + 1;

        mvaddch(y, tx - 1, ' ');
        mvaddnstr(y, tx, title, tl);
        mvaddch(y, tx + tl, ' ');
    }

    standend();
}

int ui_files_pick(const char *title, const char *start_dir, char *out_path, int out_path_sz)
{
    char dir_input[UI_FILES_PATH_MAX];
    wchar_t dir_w[UI_FILES_PATH_MAX];
    InputState dir_state;
    int edit_dir = 0;
    int sel = 0, top = 0;
    FileEnt *ents = NULL;
    int nents = 0;
    int y, x, h, w, visible;
    int key;
    wint_t wch;
    int key_rc;
    int is_key;
    int rc = -1;
    int should_exit = 0;

    if (!out_path || out_path_sz < 2)
        return -2;

    init_dir_from_start(start_dir, dir_input, sizeof(dir_input));
    char_to_wchar(dir_input, dir_w, sizeof(dir_w) / sizeof(wchar_t));

    /* Init dir state */
    dir_state.buf = dir_w;
    dir_state.bufsz = UI_FILES_PATH_MAX;
    dir_state.cursor = (int)wcslen(dir_w);
    dir_state.len = dir_state.cursor;

    for (;;)
    {
        /* Convert wchar_t back to char for load_dir */
        wchar_to_char(dir_w, dir_input, sizeof(dir_input));

        ents = load_dir(dir_input, &nents);

        if (!ents)
        {
            /* Directory unreadable: force edit mode so user can fix path */
            edit_dir = 1;
            nents = 0;
        }

        if (sel >= nents)
            sel = nents - 1;

        if (sel < 0)
            sel = 0;

        ui_popup_center(20, 60, &y, &x, &h, &w);
        visible = h - 6; /* row 2 = dir input, 4..h-3 = list, h-2 = hint */

        for (;;)
        {
            int j;

            draw_frame(y, x, h, w, title ? title : "Select file");

            /* Dir input line */
            attrset(COLOR_PAIR(edit_dir ? COL_POPUP_SEL : COL_POPUP));
            mvaddnstr(y + 2, x + 2, "Dir: ", w - 4);
            standend();

            input_draw(&dir_state, y + 2, x + 7, w - 9, edit_dir);

            /* List */
            draw_file_list(ents, nents, sel, top, visible, y + 4, x, w, !edit_dir);

            /* Status bar */
            attrset(COLOR_PAIR(COL_STATUS));

            for (j = 0; j < w - 4; j++)
                mvaddch(y + h - 2, x + 2 + j, ' ');

            mvaddnstr(y + h - 2, x + 2, "TAB=edit dir  Up/Down=move  Enter=open  ESC=cancel", w - 4);
            standend();

            /* Position cursor - only show when editing dir */
            if (edit_dir)
            {
                curs_set(1);
                input_move_cursor(&dir_state, y + 2, x + 7, w - 9);
            }
            else
            {
                curs_set(0);
            }

            refresh();

            key_rc = wrapper_read_key(&wch);
            is_key = (key_rc == KEY_CODE_YES);

            if (key_rc == ERR)
                continue;

            key = (int)wch;

            if (key == 27)
            {
                rc = -1;
                should_exit = 1;
                break;
            }

            if (key == '\t')
            {
                edit_dir = !edit_dir;
                continue;
            }

            if (edit_dir)
            {
                if (input_handle_key(&dir_state, key, is_key) == 1)
                    continue;

                if (key == '\n' || key == '\r' || key == KEY_ENTER)
                {
                    if (dir_w[0])
                    {
                        sel = 0;
                        top = 0;
                        edit_dir = 0;
                        break; /* reload outer */
                    }
                }

                continue;
            }

            /* List-navigation state */
            if (key == KEY_BACKSPACE || key == 127 || key == 8)
            {
                /* Backspace when not editing = go to parent directory */
                wchar_to_char(dir_w, dir_input, sizeof(dir_input));
                path_go_parent(dir_input);
                char_to_wchar(dir_input, dir_w, sizeof(dir_w) / sizeof(wchar_t));

                dir_state.len = (int)wcslen(dir_w);
                dir_state.cursor = dir_state.len;

                sel = 0;
                top = 0;

                break; /* reload outer */
            }
            else if (!handle_list_navigation(key, &sel, &top, nents, visible) && (key == '\n' || key == '\r' || key == KEY_ENTER))
            {
                if (nents == 0)
                    continue;

                if (ents[sel].is_dir)
                {
                    wchar_to_char(dir_w, dir_input, sizeof(dir_input));

                    if (strcmp(ents[sel].name, "..") == 0)
                    {
                        /* Navigate to parent directory */
                        path_go_parent(dir_input);
                        sel = 0;
                        top = 0;

                        char_to_wchar(dir_input, dir_w, sizeof(dir_w) / sizeof(wchar_t));
                        dir_state.len = (int)wcslen(dir_w);
                        dir_state.cursor = dir_state.len;

                        break; /* reload outer */
                    }
                    else if (path_append(dir_input, sizeof(dir_input), ents[sel].name) == 0)
                    {
                        char_to_wchar(dir_input, dir_w, sizeof(dir_w) / sizeof(wchar_t));
                        dir_state.len = (int)wcslen(dir_w);
                        dir_state.cursor = dir_state.len;

                        sel = 0;
                        top = 0;

                        break; /* reload outer */
                    }
                }
                else
                {
                    /* Compose final path */
                    char tmp[UI_FILES_PATH_MAX];

                    wchar_to_char(dir_w, dir_input, sizeof(dir_input));
                    strncpy(tmp, dir_input, sizeof(tmp) - 1);
                    tmp[sizeof(tmp) - 1] = '\0';

                    if (path_append(tmp, sizeof(tmp), ents[sel].name) == 0)
                    {
                        strncpy(out_path, tmp, (size_t)(out_path_sz - 1));
                        out_path[out_path_sz - 1] = '\0';
                        rc = 0;
                    }
                    else
                    {
                        rc = -2;
                    }

                    should_exit = 1;

                    break;
                }
            }
        }

        if (should_exit)
            break;

        /* Outer reload point */
        free_entries(ents, nents);

        ents = NULL;
        nents = 0;
    }

    free_entries(ents, nents);

    return rc;
}

int ui_files_pick_dir(const char *title, const char *start_dir, char *out_path, int out_path_sz)
{
    char dir_input[UI_FILES_PATH_MAX];
    wchar_t dir_w[UI_FILES_PATH_MAX];
    InputState dir_state;
    int edit_dir = 0;
    int sel = 0, top = 0;
    FileEnt *ents = NULL;
    int nents = 0;
    int y, x, h, w, visible;
    int key;
    wint_t wch;
    int key_rc;
    int is_key;
    int rc = -1;
    int should_exit = 0;
    int dir_count_total;
    int i;

    if (!out_path || out_path_sz < 2)
        return -2;

    if (start_dir && start_dir[0])
    {
        strncpy(dir_input, start_dir, sizeof(dir_input) - 1);
        dir_input[sizeof(dir_input) - 1] = '\0';
    }
    else
    {
#ifdef PLATFORM_WIN32
        wchar_t wdir[UI_FILES_PATH_MAX];
        if (GetCurrentDirectoryW((DWORD)(sizeof(wdir) / sizeof(wchar_t)), wdir))
        {
            char *u = pf_utf16_to_utf8(wdir);

            if (u)
            {
                strncpy(dir_input, u, sizeof(dir_input) - 1);
                dir_input[sizeof(dir_input) - 1] = '\0';
                free(u);
            }
            else
            {
                dir_input[0] = '.';
                dir_input[1] = '\0';
            }
        }
        else
        {
            dir_input[0] = '.';
            dir_input[1] = '\0';
        }

#elif defined(PLATFORM_AMIGA)
        BPTR cur = Lock((STRPTR) "", ACCESS_READ);
        dir_input[0] = '\0';

        if (cur)
        {
            if (!NameFromLock(cur, (STRPTR)dir_input, (LONG)sizeof(dir_input)))
                dir_input[0] = '\0';

            UnLock(cur);
        }
#else
        if (!getcwd(dir_input, sizeof(dir_input)))
        {
            dir_input[0] = '.';
            dir_input[1] = '\0';
        }
#endif
    }

    char_to_wchar(dir_input, dir_w, sizeof(dir_w) / sizeof(wchar_t));

    /* Init dir state */
    dir_state.buf = dir_w;
    dir_state.bufsz = sizeof(dir_w) / sizeof(wchar_t);
    dir_state.len = (int)wcslen(dir_w);
    dir_state.cursor = dir_state.len;

    if (dir_state.len > dir_state.bufsz - 1)
    {
        dir_w[dir_state.bufsz - 1] = L'\0';
        dir_state.len = dir_state.bufsz - 1;
        dir_state.cursor = dir_state.len;
    }

    for (;;)
    {
        /* Convert wchar_t back to char for load_dir */
        wchar_to_char(dir_w, dir_input, sizeof(dir_input));

        ents = load_dir(dir_input, &nents);

        if (!ents)
        {
            edit_dir = 1;
            nents = 0;
        }

        /* Count directories first to adjust sel */
        dir_count_total = 0;

        for (i = 0; i < nents; i++)
        {
            if (ents[i].is_dir)
                dir_count_total++;
        }

        if (sel >= dir_count_total)
            sel = dir_count_total - 1;

        if (sel < 0)
            sel = 0;

        ui_popup_center(20, 60, &y, &x, &h, &w);

        visible = h - 6;

        /* Ensure top is valid before rendering */
        if (top < 0)
            top = 0;

        if (top >= dir_count_total)
            top = dir_count_total > 0 ? dir_count_total - 1 : 0;

        if (sel < top)
            top = sel;

        if (sel >= top + visible)
            top = sel - visible + 1;

        if (top < 0)
            top = 0;

        for (;;)
        {
            int dir_count = 0;

            standend();
            draw_frame(y, x, h, w, title ? title : "Select directory");

            /* Dir input line */
            attrset(COLOR_PAIR(edit_dir ? COL_POPUP_SEL : COL_POPUP));
            mvaddnstr(y + 2, x + 2, "Dir: ", w - 4);

            standend();

            input_draw(&dir_state, y + 2, x + 7, w - 9, edit_dir);

            for (i = 0; i < nents; i++)
            {
                const FileEnt *en = &ents[i];
                char line[256];

                if (!en->is_dir)
                    continue;

                if (dir_count >= top && dir_count < top + visible)
                {
                    attrset(COLOR_PAIR(dir_count == sel && !edit_dir ? COL_POPUP_SEL : COL_POPUP));
                    snprintf(line, sizeof(line), "[%s]", en->name);

                    mvaddnstr(y + 4 + (dir_count - top), x + 2, line, w - 4);

                    standend();
                }

                dir_count++;
            }

            /* Adjust top to keep selection visible */
            if (sel < top)
                top = sel;
            else if (sel >= top + visible)
                top = sel - visible + 1;

            attrset(COLOR_PAIR(COL_POPUP));
            mvaddnstr(y + h - 2, x + 2, "TAB=edit dir  Up/Down=move  Enter=enter  Space=select", w - 4);
            standend();

            /* Position cursor when editing dir */
            if (edit_dir)
            {
                curs_set(1);
                input_move_cursor(&dir_state, y + 2, x + 7, w - 9);
            }
            else
            {
                curs_set(0);
            }

            refresh();

            key_rc = wrapper_read_key(&wch);
            is_key = (key_rc == KEY_CODE_YES);

            if (key_rc == ERR)
                continue;

            key = (int)wch;

            if (key == 27)
            {
                rc = -1;
                should_exit = 1;
                break;
            }

            if (key == '\t')
            {
                edit_dir = !edit_dir;
                dir_state.cursor = dir_state.len;
                continue;
            }

            if (edit_dir)
            {
                if (key == '\n' || key == '\r' || key == KEY_ENTER)
                {
                    if (dir_w[0])
                    {
                        sel = 0;
                        top = 0;
                        edit_dir = 0;
                        break;
                    }
                }

                if (input_handle_key(&dir_state, key, is_key) == 1)
                    continue;

                continue;
            }

            if (key == KEY_BACKSPACE || key == 127 || key == 8)
            {
                wchar_to_char(dir_w, dir_input, sizeof(dir_input));
                path_go_parent(dir_input);
                char_to_wchar(dir_input, dir_w, sizeof(dir_w) / sizeof(wchar_t));

                dir_state.len = (int)wcslen(dir_w);
                dir_state.cursor = dir_state.len;

                sel = 0;
                top = 0;

                break;
            }
            else if (key == KEY_UP)
            {
                if (sel > 0)
                    sel--;

                if (sel < top)
                    top = sel;
            }
            else if (key == KEY_DOWN)
            {
                if (sel < dir_count_total - 1)
                    sel++;

                if (sel >= top + visible)
                    top = sel - visible + 1;
            }
            else if (key == KEY_PPAGE || key == CTRL('U'))
            {
                sel -= visible;

                if (sel < 0)
                    sel = 0;

                if (sel < top)
                    top = sel;
            }
            else if (key == KEY_NPAGE || key == CTRL('D'))
            {
                sel += visible;

                if (sel >= dir_count_total)
                    sel = dir_count_total - 1;

                if (sel >= top + visible)
                    top = sel - visible + 1;
            }
            else if (key == KEY_HOME || key == CTRL('B'))
            {
                sel = 0;
                top = 0;
            }
            else if (key == KEY_END || key == CTRL('E'))
            {
                sel = dir_count_total - 1;
                top = sel - visible + 1;

                if (top < 0)
                    top = 0;
            }
            else if (key == '\n' || key == '\r' || key == KEY_ENTER)
            {
                int dir_idx = 0;

                wchar_to_char(dir_w, dir_input, sizeof(dir_input));

                for (i = 0; i < nents; i++)
                {
                    if (!ents[i].is_dir)
                        continue;

                    if (dir_idx == sel)
                    {
                        if (strcmp(ents[i].name, "..") == 0)
                        {
                            path_go_parent(dir_input);

                            sel = 0;
                            top = 0;
                        }
                        else if (path_append(dir_input, sizeof(dir_input), ents[i].name) == 0)
                        {
                            sel = 0;
                            top = 0;
                        }

                        char_to_wchar(dir_input, dir_w, sizeof(dir_w) / sizeof(wchar_t));

                        dir_state.len = (int)wcslen(dir_w);
                        dir_state.cursor = dir_state.len;

                        break;
                    }

                    dir_idx++;
                }

                break;
            }
            else if (key == ' ')
            {
                int dir_idx = 0;

                wchar_to_char(dir_w, dir_input, sizeof(dir_input));

                for (i = 0; i < nents; i++)
                {
                    if (!ents[i].is_dir)
                        continue;

                    if (dir_idx == sel)
                    {
                        char tmp[UI_FILES_PATH_MAX];
                        strncpy(tmp, dir_input, sizeof(tmp) - 1);
                        tmp[sizeof(tmp) - 1] = '\0';

                        if (strcmp(ents[i].name, "..") == 0)
                        {
                            path_go_parent(tmp);
                        }
                        else if (path_append(tmp, sizeof(tmp), ents[i].name) == 0)
                        {
                            strncpy(out_path, tmp, (size_t)(out_path_sz - 1));
                            out_path[out_path_sz - 1] = '\0';

                            rc = 0;
                            should_exit = 1;
                        }
                        else
                        {
                            rc = -2;
                        }

                        break;
                    }

                    dir_idx++;
                }

                if (should_exit)
                    break;
            }
        }

        if (should_exit)
            break;

        /* Outer reload point */
        free_entries(ents, nents);
        ents = NULL;
        nents = 0;
    }

    free_entries(ents, nents);

    curs_set(1);

    return rc;
}

int ui_files_save(const char *title, const char *start_dir, const char *init_name, char *out_path, int out_path_sz)
{
    char dir_input[UI_FILES_PATH_MAX];
    char name_input[1024];
    wchar_t dir_w[UI_FILES_PATH_MAX];
    wchar_t name_w[1024];
    InputState dir_state, name_state;
    int field = 0; /* 0=dir, 1=name, 2=list */
    int sel = 0, top = 0;
    FileEnt *ents = NULL;
    int nents = 0;
    int y, x, h, w, visible;
    int key;
    wint_t wch;
    int key_rc;
    int is_key;
    int rc = -1;

    if (!out_path || out_path_sz < 2)
        return -2;

    /* Init dir */
    init_dir_from_start(start_dir, dir_input, sizeof(dir_input));
    char_to_wchar(dir_input, dir_w, sizeof(dir_w) / sizeof(wchar_t));

    /* Init dir state */
    dir_state.buf = dir_w;
    dir_state.bufsz = UI_FILES_PATH_MAX;
    dir_state.cursor = (int)wcslen(dir_w);
    dir_state.len = dir_state.cursor;

    /* Init name */
    if (init_name && init_name[0])
    {
        strncpy(name_input, init_name, sizeof(name_input) - 1);
        name_input[sizeof(name_input) - 1] = '\0';
    }
    else
        name_input[0] = '\0';

    char_to_wchar(name_input, name_w, sizeof(name_w) / sizeof(wchar_t));

    /* Init name state */
    name_state.buf = name_w;
    name_state.bufsz = sizeof(name_w) / sizeof(wchar_t);
    name_state.cursor = (int)wcslen(name_w);
    name_state.len = name_state.cursor;

    ui_popup_center(22, 62, &y, &x, &h, &w);
    visible = h - 8;

    curs_set(1);

    for (;;)
    {
        int j;

        /* Reload directory - convert wchar_t back to char for load_dir */
        wchar_to_char(dir_w, dir_input, sizeof(dir_input));

        ents = load_dir(dir_input, &nents);

        if (!ents)
        {
            nents = 0;
            sel = 0;
            top = 0;
        }

        if (sel >= nents)
            sel = nents > 0 ? nents - 1 : 0;

        if (sel < 0)
            sel = 0;

        draw_frame(y, x, h, w, title ? title : "Save as");

        /* Dir field */
        attrset(COLOR_PAIR(COL_POPUP));

        for (j = 0; j < w - 4; j++)
            mvaddch(y + 2, x + 2 + j, ' ');

        standend();

        attrset(COLOR_PAIR(field == 0 ? COL_POPUP_SEL : COL_POPUP));
        mvaddnstr(y + 2, x + 2, "Dir:  ", 6);
        standend();

        input_draw(&dir_state, y + 2, x + 8, w - 10, field == 0);

        /* Name field */
        attrset(COLOR_PAIR(COL_POPUP));

        for (j = 0; j < w - 4; j++)
            mvaddch(y + 3, x + 2 + j, ' ');

        standend();

        attrset(COLOR_PAIR(field == 1 ? COL_POPUP_SEL : COL_POPUP));
        mvaddnstr(y + 3, x + 2, "Name: ", 6);

        standend();

        input_draw(&name_state, y + 3, x + 8, w - 10, field == 1);

        /* File list */
        draw_file_list(ents, nents, sel, top, visible, y + 5, x, w, field == 2);

        /* Status bar */
        attrset(COLOR_PAIR(COL_STATUS));

        for (j = 0; j < w - 4; j++)
            mvaddch(y + h - 2, x + 2 + j, ' ');

        mvaddnstr(y + h - 2, x + 2, "TAB: field  Enter: save  ESC: cancel", w - 4);

        standend();

        /* Position cursor - only show when in input fields */
        if (field == 0)
        {
            curs_set(1);
            input_move_cursor(&dir_state, y + 2, x + 8, w - 10);
        }
        else if (field == 1)
        {
            curs_set(1);
            input_move_cursor(&name_state, y + 3, x + 8, w - 10);
        }
        else
        {
            curs_set(0);
        }

        refresh();

        key_rc = wrapper_read_key(&wch);
        is_key = (key_rc == KEY_CODE_YES);

        if (key_rc == ERR)
            continue;

        key = (int)wch;

        if (key == 27)
        {
            curs_set(0);
            rc = -1;
            break;
        }

        if (key == '\n' || key == '\r' || key == KEY_ENTER)
        {
            /* Only save when NOT in the file list (field 2) */
            if (field != 2)
            {
                /* Convert wchar_t back to char for output */
                wchar_to_char(dir_w, dir_input, sizeof(dir_input));
                wchar_to_char(name_w, name_input, sizeof(name_input));

                if (name_input[0])
                {
                    char tmp[UI_FILES_PATH_MAX];
                    strncpy(tmp, dir_input, sizeof(tmp) - 1);
                    tmp[sizeof(tmp) - 1] = '\0';

                    if (path_append(tmp, sizeof(tmp), name_input) == 0)
                    {
                        strncpy(out_path, tmp, (size_t)(out_path_sz - 1));
                        out_path[out_path_sz - 1] = '\0';
                        rc = 0;
                    }
                    else
                        rc = -2;
                }

                curs_set(0);
                break;
            }
        }

        if (key == '\t')
        {
            field = (field + 1) % 3;
            continue;
        }

        /* Handle text input for dir field */
        if (field == 0)
        {
            if (input_handle_key(&dir_state, key, is_key) == 1)
                continue;
        }

        /* Handle text input for name field */
        if (field == 1)
        {
            if (input_handle_key(&name_state, key, is_key) == 1)
                continue;
        }

        /* Handle list navigation */
        if (field == 2)
        {
            if (!handle_list_navigation(key, &sel, &top, nents, visible) && (key == '\n' || key == '\r' || key == KEY_ENTER))
            {
                if (nents == 0)
                    continue;

                if (ents[sel].is_dir)
                {
                    if (strcmp(ents[sel].name, "..") == 0)
                        path_go_parent(dir_input);
                    else
                        path_append(dir_input, sizeof(dir_input), ents[sel].name);

                    /* Convert updated dir to wchar_t */
                    char_to_wchar(dir_input, dir_w, sizeof(dir_w) / sizeof(wchar_t));

                    /* Update dir state */
                    dir_state.len = (int)wcslen(dir_w);
                    dir_state.cursor = dir_state.len;

                    sel = 0;
                    top = 0;
                }
                else
                {
                    /* Copy file name to name field */
                    strncpy(name_input, ents[sel].name, sizeof(name_input) - 1);
                    name_input[sizeof(name_input) - 1] = '\0';

                    /* Convert to wchar_t */
                    char_to_wchar(name_input, name_w, sizeof(name_w) / sizeof(wchar_t));

                    /* Update name state */
                    name_state.len = (int)wcslen(name_w);
                    name_state.cursor = name_state.len;

                    field = 1;
                }
            }
        }
    }

    curs_set(0);
    free_entries(ents, nents);

    return rc;
}

int ui_files_open_path(TeApp *app, const char *path)
{
    FILE *fp = NULL;
    long size;
    int is_utf8;
    int detected;
    int recovered;

    if (!app || !path || !path[0])
        return -1;

    recovered = ui_editor_swp_recover(app, path);

    if (recovered == 1)
    {
        detected = ui_editor_detect_wrap_hyphens(app);

        if (detected > 0)
            te_status(app, "Recovered: %s (%d wrap-hyphens)", path, detected);
        else
            te_status(app, "Recovered: %s", path);

        return 0;
    }

    if (recovered < 0)
        return -1;

    /* Read file */
    fp = fopen(path, "rb");

    if (!fp)
    {
        te_status(app, "Cannot read: %s", path);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0)
    {
        fclose(fp);
        te_status(app, "Cannot read: %s", path);
        return -1;
    }

    /* Fast path for UTF-8: stream directly from FILE*. Avoids holding the whole file plus the wchar_t copy at once */
    is_utf8 = (!app->charset_in[0] || strcasecmp(app->charset_in, "UTF-8") == 0 || strcasecmp(app->charset_in, "UTF8") == 0);

    if (is_utf8)
    {
        int rc;

        fseek(fp, 0, SEEK_SET);

        ed_clear_undo_redo(te_app_get_editor(app));
        rc = ed_load_stream(te_app_get_editor(app), fp);

        fclose(fp);

        port_mem_release();

        if (rc != 0)
        {
            te_status(app, "Memory error during load");
            return -1;
        }

        te_app_set_filename(app, path);

        detected = ui_editor_detect_wrap_hyphens(app);

        if (detected > 0)
            te_status(app, "Loaded: %s (%d wrap-hyphens)", path, detected);
        else
            te_status(app, "Loaded: %s", path);

        ui_editor_recent_add(path);

        return 0;
    }

    /* Non-UTF8 charset: stream and convert per line (handles huge files). */
    ed_clear_undo_redo(te_app_get_editor(app));

    if (ed_load_stream_charset(te_app_get_editor(app), fp, app->charset_in) != 0)
    {
        fclose(fp);
        te_status(app, "Load error");
        return -1;
    }

    fclose(fp);

    /* Hand back heap freed by the reload before continuing. */
    port_mem_release();

    /* Update filename */
    te_app_set_filename(app, path);

    detected = ui_editor_detect_wrap_hyphens(app);

    if (detected > 0)
        te_status(app, "Loaded: %s (%d wrap-hyphens)", path, detected);
    else
        te_status(app, "Loaded: %s", path);

    ui_editor_recent_add(path);

    return 0;
}

int ui_files_open(TeApp *app)
{
    char path[UI_FILES_PATH_MAX];
    char start_dir_buf[UI_FILES_PATH_MAX];
    const char *start_dir = NULL;

    if (!app)
        return -1;

    /* Start from current file's directory, or CWD */
    if (te_app_get_filename(app)[0])
    {
        /* Extract directory from current filename */
        const char *last_slash = strrchr(te_app_get_filename(app), '/');

        if (!last_slash)
            last_slash = strrchr(te_app_get_filename(app), '\\');

        if (last_slash)
        {
            int dir_len = (int)(last_slash - te_app_get_filename(app));

            if (dir_len > (int)sizeof(start_dir_buf) - 1)
                dir_len = (int)sizeof(start_dir_buf) - 1;

            strncpy(start_dir_buf, te_app_get_filename(app), dir_len);
            start_dir_buf[dir_len] = '\0';
            start_dir = start_dir_buf;
        }
    }

    /* Show file picker */
    if (ui_files_pick("Open file", start_dir, path, sizeof(path)) != 0)
        return -1; /* User cancelled or error */

    return ui_files_open_path(app, path);
}
