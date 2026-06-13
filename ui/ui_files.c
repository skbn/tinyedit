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
#include "../core/keys.h"

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
        if (!GetCurrentDirectoryA(dir_input_sz, dir_input))
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

/* Convert char string to wchar_t */
static void char_to_wchar(const char *src, wchar_t *dst, int dst_sz)
{
    int i;

    for (i = 0; i < dst_sz; i++)
        dst[i] = L'\0';

    for (i = 0; src[i] && i < dst_sz - 1; i++)
        dst[i] = (wchar_t)(unsigned char)src[i];

    dst[i] = L'\0';
}

/* Convert wchar_t string to char (ASCII only) */
static void wchar_to_char(const wchar_t *src, char *dst, int dst_sz)
{
    int i;

    for (i = 0; i < dst_sz; i++)
        dst[i] = '\0';

    for (i = 0; src[i] && i < dst_sz - 1; i++)
        dst[i] = (src[i] < 0x80) ? (char)src[i] : '?';

    dst[i] = '\0';
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
        if (idx == sel && is_list_active)
            attron(COLOR_PAIR(COL_POPUP_SEL));
        else
            attron(COLOR_PAIR(COL_POPUP));

        for (j = 0; j < w - 4; j++)
            mvaddch(y + i, x + 2 + j, ' ');

        if (en->is_dir)
            snprintf(line, sizeof(line), "[%s]", en->name);
        else
            snprintf(line, sizeof(line), " %s ", en->name);

        mvaddnstr(y + i, x + 2, line, w - 4);

        if (idx == sel && is_list_active)
            attroff(COLOR_PAIR(COL_POPUP_SEL));
        else
            attroff(COLOR_PAIR(COL_POPUP));
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
#ifdef PLATFORM_WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pattern[UI_FILES_PATH_MAX];
#elif defined(PLATFORM_AMIGA)
    BPTR lock;
    struct FileInfoBlock *fib;
#else
    DIR *dp;
    struct dirent *e;
    struct stat st;
    char full[UI_FILES_PATH_MAX];
#endif
    FileEnt *ents = NULL;
    int n = 0, cap = 0;
    int i, j;
    int at_root;
    const char *colon;

    *out_n = 0;

#ifdef PLATFORM_WIN32
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    h = FindFirstFileA(pattern, &fd);

    if (h == INVALID_HANDLE_VALUE)
        return NULL;

    /* Prepend ".." entry unless at root (C:\ or C:/) */
    int dlen = (int)strlen(dir);
    at_root = (dlen <= 3); /* "C:\" or "C:/" */

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

    while (n < UI_FILES_MAX_ENTRIES)
    {
        /* Skip "." and ".." from FindFirstFile (we added our own "..") */
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
        {
            if (!FindNextFileA(h, &fd))
                break;

            continue;
        }

        if (n >= cap)
        {
            int nc = cap ? cap * 2 : 32;
            FileEnt *nb = (FileEnt *)realloc(ents, (size_t)nc * sizeof(FileEnt));

            if (!nb)
                break;

            ents = nb;
            cap = nc;
        }

        ents[n].name = NULL;
        ents[n].is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;

        ents[n].name = strdup(fd.cFileName);

        if (!ents[n].name)
            break;

        n++;

        if (!FindNextFileA(h, &fd))
            break;
    }

    FindClose(h);
#elif defined(PLATFORM_AMIGA)
    /* AmigaDOS: Lock/Examine/ExNext, fib_DirEntryType >0=dir, <0=file */
    lock = Lock((STRPTR)(dir && dir[0] ? dir : ""), ACCESS_READ);

    if (!lock)
        return NULL;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

    if (!fib)
    {
        UnLock(lock);
        return NULL;
    }

    /* Examine directory first (required before ExNext, early bail if not dir) */
    if (!Examine(lock, fib) || fib->fib_DirEntryType <= 0)
    {
        FreeDosObject(DOS_FIB, fib);
        UnLock(lock);
        return NULL;
    }

    /* Prepend ".." entry unless at volume root (path ends with colon, e.g., "Work:") */
    colon = strchr(dir, ':');
    at_root = 0;

    if (!colon)
    {
        /* No colon = relative path, treat as not-root for navigation */
        at_root = 0;
    }
    else
    {
        /* Skip the colon and any trailing slashes */
        const char *after = colon + 1;

        while (*after == '/')
            after++;

        at_root = (*after == '\0');
    }

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

    while (ExNext(lock, fib) && n < UI_FILES_MAX_ENTRIES)
    {
        if (n >= cap)
        {
            int nc = cap ? cap * 2 : 32;

            FileEnt *nb = (FileEnt *)realloc(ents, (size_t)nc * sizeof(FileEnt));

            if (!nb)
                break;

            ents = nb;
            cap = nc;
        }

        ents[n].is_dir = (fib->fib_DirEntryType > 0) ? 1 : 0;
        ents[n].name = strdup(fib->fib_FileName);

        if (!ents[n].name)
            break;

        n++;
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
#else
    dp = opendir(dir);

    if (!dp)
        return NULL;

    /* Prepend ".." entry unless at filesystem root "/" */
    at_root = (strcmp(dir, "/") == 0);

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

    while ((e = readdir(dp)) != NULL && n < UI_FILES_MAX_ENTRIES)
    {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;

        if (n >= cap)
        {
            int nc = cap ? cap * 2 : 32;
            FileEnt *nb = (FileEnt *)realloc(ents, (size_t)nc * sizeof(FileEnt));

            if (!nb)
                break;

            ents = nb;
            cap = nc;
        }

        ents[n].name = NULL;
        ents[n].is_dir = 0;

        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);

        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
            ents[n].is_dir = 1;

        ents[n].name = strdup(e->d_name);

        if (!ents[n].name)
            break;

        n++;
    }

    closedir(dp);
#endif

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

    attron(COLOR_PAIR(COL_POPUP));

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

    attroff(COLOR_PAIR(COL_POPUP));
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
            attron(COLOR_PAIR(COL_POPUP));

            /* Dir input line */
            if (edit_dir)
                attron(COLOR_PAIR(COL_POPUP_SEL));

            mvaddnstr(y + 2, x + 2, "Dir: ", w - 4);

            if (edit_dir)
                attroff(COLOR_PAIR(COL_POPUP_SEL));

            input_draw(&dir_state, y + 2, x + 7, w - 9, edit_dir);

            /* List */
            draw_file_list(ents, nents, sel, top, visible, y + 4, x, w, !edit_dir);

            /* Status bar */
            attron(COLOR_PAIR(COL_STATUS));

            for (j = 0; j < w - 4; j++)
                mvaddch(y + h - 2, x + 2 + j, ' ');

            mvaddnstr(y + h - 2, x + 2, "TAB=edit dir  Up/Down=move  Enter=open  ESC=cancel", w - 4);
            attroff(COLOR_PAIR(COL_STATUS));

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

            key = wrapper_getch();

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
                if (input_handle_key(&dir_state, key) == 1)
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
        attron(COLOR_PAIR(COL_POPUP));

        /* Dir field */
        /* Clear the dir field row with popup background */
        attron(COLOR_PAIR(COL_POPUP));

        for (j = 0; j < w - 4; j++)
            mvaddch(y + 2, x + 2 + j, ' ');

        if (field == 0)
        {
            attroff(COLOR_PAIR(COL_POPUP));
            attron(COLOR_PAIR(COL_POPUP_SEL));
        }

        mvaddnstr(y + 2, x + 2, "Dir:  ", 6);

        if (field == 0)
        {
            attroff(COLOR_PAIR(COL_POPUP_SEL));
            attron(COLOR_PAIR(COL_POPUP));
        }

        input_draw(&dir_state, y + 2, x + 8, w - 10, field == 0);

        /* Name field */
        /* Clear the name field row with popup background */
        attron(COLOR_PAIR(COL_POPUP));

        for (j = 0; j < w - 4; j++)
            mvaddch(y + 3, x + 2 + j, ' ');

        if (field == 1)
        {
            attroff(COLOR_PAIR(COL_POPUP));
            attron(COLOR_PAIR(COL_POPUP_SEL));
        }

        mvaddnstr(y + 3, x + 2, "Name: ", 6);

        if (field == 1)
        {
            attroff(COLOR_PAIR(COL_POPUP_SEL));
            attron(COLOR_PAIR(COL_POPUP));
        }

        input_draw(&name_state, y + 3, x + 8, w - 10, field == 1);

        /* File list */
        draw_file_list(ents, nents, sel, top, visible, y + 5, x, w, field == 2);

        /* Status bar */
        attron(COLOR_PAIR(COL_STATUS));

        for (j = 0; j < w - 4; j++)
            mvaddch(y + h - 2, x + 2 + j, ' ');

        mvaddnstr(y + h - 2, x + 2, "TAB: field  Enter: save  ESC: cancel", w - 4);
        attroff(COLOR_PAIR(COL_STATUS));

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

        key = wrapper_getch();

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
            if (input_handle_key(&dir_state, key) == 1)
                continue;
        }

        /* Handle text input for name field */
        if (field == 1)
        {
            if (input_handle_key(&name_state, key) == 1)
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

int ui_files_open(TeApp *app)
{
    char path[UI_FILES_PATH_MAX];
    char start_dir_buf[UI_FILES_PATH_MAX];
    FILE *fp;
    long size;
    char *buf;
    size_t r;
    char *content = NULL;
    const char *start_dir = NULL;

    if (!app)
        return -1;

    /* Start from current file's directory, or CWD */
    if (app->filename[0])
    {
        /* Extract directory from current filename */
        const char *last_slash = strrchr(app->filename, '/');

        if (!last_slash)
            last_slash = strrchr(app->filename, '\\');

        if (last_slash)
        {
            int dir_len = (int)(last_slash - app->filename);

            strncpy(start_dir_buf, app->filename, dir_len);
            start_dir_buf[dir_len] = '\0';
            start_dir = start_dir_buf;
        }
    }

    /* Show file picker */
    if (ui_files_pick("Open file", start_dir, path, sizeof(path)) != 0)
        return -1; /* User cancelled or error */

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

    buf = (char *)malloc((size_t)size + 1);

    if (!buf)
    {
        fclose(fp);
        te_status(app, "Memory error");
        return -1;
    }

    r = fread(buf, 1, (size_t)size, fp);

    fclose(fp);

    buf[r] = '\0';

    /* Keep raw bytes for live charset re-decode */
    free(app->raw_bytes);

    app->raw_bytes = (char *)malloc(r + 1);

    if (app->raw_bytes)
    {
        memcpy(app->raw_bytes, buf, r + 1);
        app->raw_len = (int)r;
    }
    else
    {
        app->raw_len = 0;
    }

    /* Convert to UTF-8 if needed */
    if (app->charset_in[0] && strcasecmp(app->charset_in, "UTF-8") != 0 && strcasecmp(app->charset_in, "UTF8") != 0)
    {
        size_t outsz = (size_t)r * 4 + 16;
        char *utf8 = (char *)malloc(outsz);

        if (utf8)
        {
            int wrote = charset_body_to_utf8(app->charset_in, buf, (int)r, utf8, (int)outsz);

            if (wrote >= 0)
            {
                utf8[wrote] = '\0';
                content = utf8;

                free(buf);
            }
            else
            {
                free(utf8);

                content = buf;
            }
        }
        else
        {
            content = buf;
        }
    }
    else
    {
        content = buf;
    }

    /* Load into editor (ed_load clears previous content) */
    ed_load(app->editor, content);
    free(content);

    /* Update filename */
    strncpy(app->filename, path, sizeof(app->filename) - 1);
    app->filename[sizeof(app->filename) - 1] = '\0';

    te_status(app, "Loaded: %s", path);

    return 0;
}