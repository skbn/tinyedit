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

#ifdef PLATFORM_AMIGA
const char __attribute__((used)) binkd_stack_size[] = "$STACK:65536";
#endif

#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "core/portable.h"

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
#include "core/clipboard.h"

#ifdef HAVE_HUNSPELL
#include "ui/ui_spell.h"

#ifdef HAVE_HYPHEN
#include "ui/ui_hyph.h"
#endif

#ifdef HAVE_MYTHES
#include "ui/ui_thes.h"
#endif

#endif /* HAVE_HUNSPELL */

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

    if (size > SIZE_MAX - 1)
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
    char *new_bytes = (char *)malloc(r + 1);

    if (new_bytes)
    {
        memcpy(new_bytes, buf, r + 1);
        te_app_set_raw_bytes(app, new_bytes, (int)r);
    }
    else
    {
        te_app_set_raw_bytes(app, NULL, 0);
    }

    /* Convert to UTF-8 if needed */
    if (app->charset_in[0] && strcasecmp(app->charset_in, "UTF-8") != 0 && strcasecmp(app->charset_in, "UTF8") != 0)
    {
        size_t outsz = (size_t)r * 4 + 16;
        char *utf8 = NULL;

        if (r > (SIZE_MAX - 16) / 4)
        {
            free(buf);
            return NULL;
        }

        utf8 = (char *)malloc(outsz);

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
    int fi;

    setlocale(LC_ALL, "");

#ifdef PLATFORM_AMIGA
    /* Open iffparse.library for clipboard operations */
    IFFParseBase = OpenLibrary("iffparse.library", 0L);
#endif

    /* Resolve config path - use ~/.tinyedit/ directory */
#if defined(PLATFORM_WIN32)
    home = getenv("APPDATA");

    if (home && home[0])
        snprintf(cfg_path_buf, sizeof(cfg_path_buf), "%s\\tinyedit\\config", home);
    else
        snprintf(cfg_path_buf, sizeof(cfg_path_buf), "tinyedit\\config");

#elif defined(PLATFORM_AMIGA)
    snprintf(cfg_path_buf, sizeof(cfg_path_buf), "ENVARC:tinyedit/config");
#else

    home = getenv("HOME");

    if (home && home[0])
        snprintf(cfg_path_buf, sizeof(cfg_path_buf), "%s/.tinyedit/config", home);
    else
        snprintf(cfg_path_buf, sizeof(cfg_path_buf), ".tinyedit/config");

#endif

    cfg_path = cfg_path_buf;

    /* Create config directory if it doesn't exist */
#if defined(PLATFORM_WIN32)
    last_sep = strrchr(cfg_path, '\\');
#else
    last_sep = strrchr(cfg_path, '/');
#endif

    if (last_sep)
    {
        size_t len = last_sep - cfg_path;

        strncpy(dir_path, cfg_path, len);
        dir_path[len] = '\0';

        port_mkdir_one(dir_path);
    }

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

#ifdef AMIGA_TTF_TE
        /* Pass any TTF_FALLBACK<N> entries to the engine. Empty slots are skipped by amiga_add_ttf_fallback() */
        amiga_clear_ttf_fallbacks();

        for (fi = 0; fi < TE_CFG_TTF_FALLBACKS; fi++)
        {
            if (cfg.ttf_fallback[fi][0])
            {
                int sz = cfg.ttf_fallback_size[fi] > 0 ? cfg.ttf_fallback_size[fi] : cfg.ttf_size;

                amiga_add_ttf_fallback(cfg.ttf_fallback[fi], sz);
            }
        }
#endif
    }
    else
    {
        /* Explicitly disable TTF when ttf_enabled=0 */
        amiga_set_ttf(NULL, 0, 0);
#ifdef AMIGA_TTF_TE
        amiga_clear_ttf_fallbacks();
#endif
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
    define_key("\033[200~", KEY_PASTE_START);
    define_key("\033[201~", KEY_PASTE_END);

    /* Register Ctrl+arrow key sequences for word navigation */
    define_key("\033[1;5D", KEY_CLEFT);
    define_key("\033[1;5C", KEY_CRIGHT);
    define_key("\033y", KEY_ALT('Y'));

    /* Register Alt+key sequences for editor functions */
    define_key("\033g", KEY_ALT('G'));
    define_key("\033G", KEY_ALT('G'));
    define_key("\033d", KEY_ALT('D'));
    define_key("\033D", KEY_ALT('D'));
    define_key("\033s", KEY_ALT('S'));
    define_key("\033S", KEY_ALT('S'));
    define_key("\033t", KEY_ALT('T'));
    define_key("\033T", KEY_ALT('T'));
    define_key("\033c", KEY_ALT('C'));
    define_key("\033C", KEY_ALT('C'));
    define_key("\033f", KEY_ALT('F'));
    define_key("\033F", KEY_ALT('F'));
    define_key("\033b", KEY_ALT('B'));
    define_key("\033B", KEY_ALT('B'));
    define_key("\033o", KEY_ALT('O'));
    define_key("\033O", KEY_ALT('O'));
    define_key("\033p", KEY_ALT('P'));
    define_key("\033P", KEY_ALT('P'));
    define_key("\033q", KEY_ALT('Q'));
    define_key("\033Q", KEY_ALT('Q'));
    define_key("\033z", KEY_ALT('Z'));
    define_key("\033Z", KEY_ALT('Z'));
    define_key("\033h", KEY_ALT('H'));
    define_key("\033H", KEY_ALT('H'));
    define_key("\033a", KEY_ALT('A'));
    define_key("\033A", KEY_ALT('A'));
    define_key("\033e", KEY_ALT('E'));
    define_key("\033E", KEY_ALT('E'));
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
        ed_set_undo_levels(te_app_get_editor(app), cfg.undo_levels);

    app->hard_wrap = cfg.hard_wrap;
    ed_set_hard_wrap(te_app_get_editor(app), cfg.hard_wrap);
    app->wrap_col = cfg.autowrap_col;
    te_app_set_show_line_numbers(app, cfg.show_line_numbers);

    strncpy(app->cfg_path, cfg_path, sizeof(app->cfg_path) - 1);

    app->cfg_path[sizeof(app->cfg_path) - 1] = '\0';
    app->cfg = cfg;

#ifdef HAVE_HUNSPELL
    /* Initialize spell_enabled from config */
    app->spell_enabled = cfg.spell_enabled;
    app->spell_active = 0; /* Disabled by default, user must activate with Alt+H */

    /* Load spell checker from config */
    spell_load_from_config(app);

#ifdef HAVE_HYPHEN
    hyph_load_from_config(app);
    app->hyph_wrap_enabled = cfg.hyph_wrap_enabled;
#endif

#ifdef HAVE_MYTHES
    ui_thes_load_from_config(app);
#endif

#endif /* HAVE_HUNSPELL */

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
        TeTab *tab;
        char *raw_bytes;
        int raw_len;

        content = load_file(argv[1], app);
        raw_bytes = te_app_get_raw_bytes(app);
        raw_len = te_app_get_raw_len(app);

        tab = te_tab_new_with_content(argv[1], content, raw_bytes, raw_len);

        if (tab)
        {
            tab->show_line_numbers = cfg.show_line_numbers;
            te_app_add_tab(app, tab);
            te_app_switch_tab(app, 0);
        }

        /* Clear app->raw_bytes since tab took ownership */
        te_app_set_raw_bytes(app, NULL, 0);

        if (content)
            free(content);
    }
    else
    {
        TeTab *tab = te_tab_new();

        if (tab)
        {
            tab->show_line_numbers = cfg.show_line_numbers;

            te_app_add_tab(app, tab);
            te_app_switch_tab(app, 0);
        }
    }

    app->show_tabs = 0;

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

#ifdef PLATFORM_AMIGA
    /* Close iffparse.library */
    if (IFFParseBase)
        CloseLibrary(IFFParseBase);
#endif

    return 0;
}
