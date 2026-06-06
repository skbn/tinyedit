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

#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef PLATFORM_AMIGA
#include "ncursesw_amiga.h"
#elif defined(PLATFORM_WIN32)
#include "ncursesw_win32.h"
#else
#include <ncurses.h>
#endif

#include "ui/te.h"
#include "core/charset.h"
#include "components/config.h"

/* Read a whole file into a malloc'd UTF-8 buffer with charset conversion */
static char *load_file(const char *path, TeApp *app)
{
    FILE *fp;
    long size;
    char *buf;
    size_t r;

    fp = fopen(path, "rb");

    if (!fp)
        return NULL;

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0)
    {
        fclose(fp);
        return NULL;
    }

    buf = (char *)malloc((size_t)size + 1);

    if (!buf)
    {
        fclose(fp);
        return NULL;
    }

    r = fread(buf, 1, (size_t)size, fp);

    fclose(fp);

    buf[r] = '\0';

    /* Keep raw bytes for live charset re-decode */
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

                free(buf);

                return utf8;
            }
            else
            {
                free(utf8);
            }
        }
    }

    return buf;
}

int main(int argc, char **argv)
{
    TeApp *app;
    TeConfig cfg;
    char *content = NULL;
    static char cfg_path_buf[512];
    const char *cfg_path;
    FILE *tty;
    const char *home;
    char dir_path[512];
    const char *last_sep;

    setlocale(LC_ALL, "");

    /* Resolve config path */
#if defined(PLATFORM_WIN32)

    home = getenv("APPDATA");

    if (home && home[0])
        snprintf(cfg_path_buf, sizeof(cfg_path_buf), "%s\\tinyedit\\tinyedit.cfg", home);
    else
        snprintf(cfg_path_buf, sizeof(cfg_path_buf), "tinyedit.cfg");

#elif defined(PLATFORM_AMIGA)
    snprintf(cfg_path_buf, sizeof(cfg_path_buf), "ENVARC:tinyedit.cfg");
#else

    home = getenv("HOME");

    if (home && home[0])
        snprintf(cfg_path_buf, sizeof(cfg_path_buf), "%s/.tinyedit.conf", home);
    else
        snprintf(cfg_path_buf, sizeof(cfg_path_buf), ".tinyedit.conf");

#endif

    cfg_path = cfg_path_buf;

#ifdef PLATFORM_WIN32
    /* Create config directory if it doesn't exist */
    last_sep = strrchr(cfg_path, '\\');

    if (last_sep)
    {
        size_t len = last_sep - cfg_path;
        strncpy(dir_path, cfg_path, len);
        dir_path[len] = '\0';
        mkdir(dir_path);
    }

#endif

    /* Load config (creates default file if missing) */
    te_cfg_load(&cfg, cfg_path);

#ifdef PLATFORM_AMIGA
    /* Amiga setup (before initscr): background color, font, cursor */
    amiga_set_default_bg_color(cfg.default_bg_color);
    amiga_set_font_name(cfg.font[0] ? cfg.font : NULL);
    amiga_set_cursor_pen(cfg.cursor_color);

    /* Optional TrueType (ignored if ttf_enabled=0 or path empty or ttengine.library missing) */
    if (cfg.ttf_enabled)
    {
        amiga_set_ttf(cfg.ttf_font, cfg.ttf_size, cfg.ttf_antialias);
        amiga_set_ttf_encoding(cfg.ttf_use_utf8);
    }
    else
    {
        /* Explicitly disable TTF when ttf_enabled=0 */
        amiga_set_ttf(NULL, 0, 0);
    }
#endif

#ifdef PLATFORM_WIN32
    /* Windows setup (before initscr): font */
    win32_set_font_name(cfg.font[0] ? cfg.font : NULL);

    /* Windows cursor color */
    win32_set_cursor_pen(cfg.cursor_color >= 0 ? cfg.cursor_color : COLOR_WHITE);
#endif

    /* Init ncurses */
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
    set_escdelay(25);
#endif

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
    /* Register bracketed paste sequences */
    if (define_key("\033[200~", KEY_PASTE_START) != OK)
        fprintf(stderr, "Warning: define_key(200~) failed\n");

    if (define_key("\033[201~", KEY_PASTE_END) != OK)
        fprintf(stderr, "Warning: define_key(201~) failed\n");

    /* Register Ctrl+arrow key sequences for word navigation */
    if (define_key("\033[1;5D", KEY_CLEFT) != OK)
        fprintf(stderr, "Warning: define_key(Ctrl+Left) failed\n");

    if (define_key("\033[1;5C", KEY_CRIGHT) != OK)
        fprintf(stderr, "Warning: define_key(Ctrl+Right) failed\n");
#endif

    curs_set(1);

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
    /* Set cursor color via OSC 12 escape sequence (Unix/Linux) */

    tty = fopen("/dev/tty", "w");

    if (tty)
    {
        if (cfg.cursor_color_rgb[0])
            fprintf(tty, "\033]12;%s\007", cfg.cursor_color_rgb);
        else if (cfg.cursor_color >= 0 && cfg.cursor_color <= 7)
        {
            const char *names[] = {"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"};
            fprintf(tty, "\033]12;%s\007", names[cfg.cursor_color]);
        }

        fflush(tty);
        fclose(tty);
    }

#endif

    te_init_colors(&cfg);

    app = te_app_new();

    if (!app)
    {
        endwin();
        fprintf(stderr, "tinyedit: out of memory\n");
        return 1;
    }

    /* Apply config to editor */
    if (cfg.undo_levels > 0)
        ed_set_undo_levels(app->editor, cfg.undo_levels);

    app->hard_wrap = cfg.hard_wrap;
    app->wrap_col = cfg.autowrap_col;

    strncpy(app->cfg_path, cfg_path, sizeof(app->cfg_path) - 1);

    app->cfg_path[sizeof(app->cfg_path) - 1] = '\0';
    app->cfg = cfg;

    /* Initialize charsets from config */
    /* View charset is always UTF-8 by default */
    strncpy(app->charset_in, "UTF-8", sizeof(app->charset_in) - 1);
    app->charset_in[sizeof(app->charset_in) - 1] = '\0';

    /* Save charset comes from config */
    strncpy(app->charset_out, cfg.charset[0] ? cfg.charset : "UTF-8", sizeof(app->charset_out) - 1);
    app->charset_out[sizeof(app->charset_out) - 1] = '\0';

    /* Load file if given */
    if (argc >= 2)
    {
        strncpy(app->filename, argv[1], sizeof(app->filename) - 1);
        app->filename[sizeof(app->filename) - 1] = '\0';

        content = load_file(app->filename, app);

        if (content)
        {
            ed_load(app->editor, content);
            free(content);
        }
    }

    te_status(app, argc >= 2 ? argv[1] : "[No Name]");

    ui_editor_run(app);

    te_app_free(app);

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
    /* Restore cursor color to terminal default before exiting */

    tty = fopen("/dev/tty", "w");

    if (tty)
    {
        fprintf(tty, "\033]12\007");
        fflush(tty);
        fclose(tty);
    }

#endif

    endwin();

    return 0;
}
