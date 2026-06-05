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

#ifndef TE_H
#define TE_H

#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif

#include <wchar.h>

#ifdef PLATFORM_AMIGA
#include "../ncursesw_amiga.h"
#elif defined(PLATFORM_WIN32)
#include "../ncursesw_win32.h"
#else
#include <ncurses.h>
#endif

#include "../core/keys.h"
#include "../components/editor.h"
#include "../components/config.h"

/* CTRL(x) */
#ifndef CTRL
#define CTRL(ch) ((ch) - 64)
#endif

/* Extended keycodes (above KEY_MAX) */
#ifndef KEY_PASTE_START
#define KEY_PASTE_START 0x7F1
#endif
#ifndef KEY_PASTE_END
#define KEY_PASTE_END 0x7F2
#endif
#ifndef KEY_CLEFT
#define KEY_CLEFT 0x7F3
#endif
#ifndef KEY_CRIGHT
#define KEY_CRIGHT 0x7F4
#endif
#ifndef KEY_ALEFT
#define KEY_ALEFT 0x7F5
#endif
#ifndef KEY_ARIGHT
#define KEY_ARIGHT 0x7F6
#endif
#ifndef KEY_TAB
#define KEY_TAB 9
#endif
#ifndef KEY_STAB
#define KEY_STAB 0x154
#endif
#ifndef KEY_SIC
#define KEY_SIC 0x188
#endif

/* Color pairs */
#define COL_NORMAL 1
#define COL_STATUS 2
#define COL_TITLEBAR 3
#define COL_POPUP 4
#define COL_POPUP_SEL 5
#define COL_BORDER 6
#define COL_SEARCH_MATCH 7

/* Bracketed paste sequences */
#define TE_BRACKET_PASTE_ON()  \
    do                         \
    {                          \
        printf("\033[?2004h"); \
        fflush(stdout);        \
    } while (0)
#define TE_BRACKET_PASTE_OFF() \
    do                         \
    {                          \
        printf("\033[?2004l"); \
        fflush(stdout);        \
    } while (0)

/* Search state */
typedef struct
{
    wchar_t query[64];        /* last search query */
    wchar_t last_replace[64]; /* last replacement text */
    int *rows;                /* malloc'd match row indices */
    int *cols;                /* malloc'd match col indices */
    int count;                /* number of matches */
    int is_mode;              /* 0=normal, 1=search mode */
    int only_mode;            /* 1=only search mode activated by F5 */
    int current_match;        /* Current match index (0-based) for navigation */
    int match_current;        /* Current match number (1-based) for display */
    int match_total;          /* Total number of matches for display */
    int case_sensitive;       /* Search case sensitivity flag */
    int whole_word;           /* Search whole word flag */
} TeSearch;

/* Generic inline text input widget (wchar_t only) */
typedef struct
{
    wchar_t *buf;
    int bufsz;
    int cursor;
    int len;
} InputState;

/* App state */
typedef struct
{
    Ed *editor;
    char filename[1024];  /* current file path; empty = unnamed buffer */
    char charset_in[32];  /* charset used to read the file (view) */
    char charset_out[32]; /* charset used to write the file (save) */
    char *raw_bytes;      /* original file bytes kept for live charset re-decode */
    int raw_len;
    int hard_wrap;      /* 0=soft-wrap (default), 1=hard-wrap */
    int wrap_col;       /* hard-wrap column (0=disabled) */
    TeSearch search;    /* search state */
    char status[256];   /* status bar message */
    char cfg_path[512]; /* path to config file */
    TeConfig cfg;       /* live config (for setup access) */
} TeApp;

/* te_app.c */
TeApp *te_app_new(void);
void te_app_free(TeApp *app);
void te_init_colors(const TeConfig *cfg);

/* ui_editor.c */
void ui_editor_run(TeApp *app);

/* ui_popup.c */
void ui_popup_help(const char *title, const char *const *lines, int n);
int ui_popup_confirm(const char *title, const char *msg);
int ui_popup_list(const char *title, const char **items, int count, int initial);
int ui_popup_input_wcs(const char *title, const char *prompt, wchar_t *wbuf, int wcap);
int ui_popup_charset(const char *title, const char *cur, char *out, int outsz);
int ui_popup_charset_pair(const char *view_in, const char *save_in, char *view_out, int view_outsz, char *save_out, int save_outsz);
int ui_popup_replace(const wchar_t *search_in, const wchar_t *replace_in, wchar_t *search_out, int search_outsz, wchar_t *replace_out, int replace_outsz, int *case_sensitive, int *whole_word);
int ui_files_open(TeApp *app);
int ui_popup_search_results(const char *title, const int *line_nums, const char **contexts, int count, int initial);
int ui_popup_search_simple(wchar_t *search_out, int search_outsz);
int ui_popup_search_results_popup(TeApp *app, const wchar_t *search, int *rows, int *cols, int match_count);
void ui_popup_center(int want_h, int want_w, int *y, int *x, int *h, int *w);
void ui_draw_popup_frame(int y, int x, int h, int w, const char *title);

int input_handle_key(InputState *state, int ch);
void input_draw(InputState *state, int y, int x, int width, int is_active);
void input_move_cursor(InputState *state, int y, int x, int width);

/* te_draw.c  (helpers used by editor + popups) */
void te_status(TeApp *app, const char *fmt, ...);
void te_draw_statusbar(TeApp *app);
void te_draw_titlebar(TeApp *app);
void ui_box(int y, int x, int h, int w);
void te_hline(int y, int x, int len);
const char *te_wcs2u8(const wchar_t *wcs);

#endif /* TE_H */