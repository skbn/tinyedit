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
const char __attribute__((used)) tinyedit_stack_size[] = "$STACK:131072";
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

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
#include <langinfo.h> /* nl_langinfo(CODESET) for UTF-8 locale detection (fix BSD) */
#endif

#ifdef PLATFORM_AMIGA
#include <proto/exec.h>
#include "ncursesw_amiga.h"
extern void amiga_clear_ttf_fallbacks(void);
extern int amiga_add_ttf_fallback(const char *path, int size);
#elif defined(PLATFORM_WIN32)
#include "ncursesw_win32.h"
#else
#include <ncurses.h>
#endif

#include "ui/te.h"
#include "ui/ui_editor_helper.h"
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

#ifdef HAVE_TRANSLATE
#include "ui/ui_translate.h"
#endif

/* Read a whole file into a malloc'd UTF-8 buffer with charset conversion */
static char *load_file(const char *path, TeApp *app)
{
    FILE *fp = NULL;
    long size;
    char *buf = NULL;
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

    if ((size_t)size > SIZE_MAX - 1)
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

static void ui_init_locale(void)
{
#if defined(PLATFORM_AMIGA) || defined(PLATFORM_WIN32)
    /* Wrapper layers handle encoding */
    setlocale(LC_ALL, "");
#else

    static const char *utf8_fallbacks[] =
        {
            "C.UTF-8",
            "en_US.UTF-8",
            "POSIX.UTF-8",
            NULL};

    const char *codeset;
    int i;

    /* First honour the environment */
    setlocale(LC_ALL, "");

    codeset = nl_langinfo(CODESET);

    if (codeset && (strcmp(codeset, "UTF-8") == 0 || strcmp(codeset, "utf8") == 0 || strcmp(codeset, "UTF8") == 0))
        return; /* environment already UTF-8 */

    /* Try to upgrade LC_CTYPE to UTF-8 locale */
    for (i = 0; utf8_fallbacks[i]; i++)
    {
        if (setlocale(LC_CTYPE, utf8_fallbacks[i]))
        {
            codeset = nl_langinfo(CODESET);

            if (codeset && (strcmp(codeset, "UTF-8") == 0 || strcmp(codeset, "utf8") == 0 || strcmp(codeset, "UTF8") == 0))
                return; /* success */
        }
    }
#endif
}

int main(int argc, char **argv)
{
    TeApp *app = NULL;
    TeConfig cfg;
    char *content = NULL;
    static char cfg_path_buf[512];
    const char *cfg_path = NULL;
    FILE *tty = NULL;
    char cfg_dir[512];
    int detected;

#ifdef PLATFORM_AMIGA
    /* Redirect stdout/stderr to avoid Amiga CLI console */
    freopen("NIL:", "w", stdout);
    freopen("NIL:", "w", stderr);
#endif

    /* Locale init for wide-character ncursesw */
    ui_init_locale();

#ifdef PLATFORM_AMIGA
    /* Open iffparse.library for clipboard operations */
    IFFParseBase = OpenLibrary("iffparse.library", 0L);
#endif

    /* Resolve config path using portable config directory */
    port_get_config_dir(cfg_dir, sizeof(cfg_dir));
    pf_ensure_dir(cfg_dir);
    pf_path_join(cfg_path_buf, sizeof(cfg_path_buf), cfg_dir, "config");

    cfg_path = cfg_path_buf;

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
        int fi;
        int sz;

        amiga_set_ttf(cfg.ttf_font, cfg.ttf_size, cfg.ttf_antialias);
        amiga_set_ttf_encoding(cfg.ttf_use_utf8);

        /* Pass any TTF_FALLBACK<N> entries to the engine. Empty slots are skipped by amiga_add_ttf_fallback() */
        amiga_clear_ttf_fallbacks();

        for (fi = 0; fi < TE_CFG_TTF_FALLBACKS; fi++)
        {
            if (cfg.ttf_fallback[fi][0])
            {
                sz = cfg.ttf_fallback_size[fi] > 0 ? cfg.ttf_fallback_size[fi] : cfg.ttf_size;

                amiga_add_ttf_fallback(cfg.ttf_fallback[fi], sz);
            }
        }
    }
    else
    {
        /* Explicitly disable TTF when ttf_enabled=0 */
        amiga_set_ttf(NULL, 0, 0);
        amiga_clear_ttf_fallbacks();
    }
#endif

#ifdef PLATFORM_WIN32
    /* Windows setup (before initscr): font */
    if (cfg.font[0] && (strchr(cfg.font, '\\') || strchr(cfg.font, '/') || strchr(cfg.font, ':')))
    {
        /* cfg.font contains a file path (legacy misconfiguration or File mode in setup)
         * Extract the real family name and move the path to the TTF font slot */
        char family[256];
        char path[TE_CFG_STR_MAX];

        strncpy(path, cfg.font, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';

        if (win32_get_font_family_name(path, family, sizeof(family)) == 0)
        {
            strncpy(cfg.font, family, TE_CFG_STR_MAX - 1);
            cfg.font[TE_CFG_STR_MAX - 1] = '\0';
        }
        else
        {
            cfg.font[0] = '\0';
        }

        if (!cfg.ttf_enabled || !cfg.ttf_font[0])
        {
            strncpy(cfg.ttf_font, path, TE_CFG_STR_MAX - 1);
            cfg.ttf_font[TE_CFG_STR_MAX - 1] = '\0';
            cfg.ttf_enabled = 1;
        }
    }

    win32_set_font_name(cfg.font[0] ? cfg.font : NULL);

    /* Windows cursor color */
    win32_set_cursor_pen(cfg.cursor_color >= 0 ? cfg.cursor_color : COLOR_WHITE);

    /* Optional TrueType fonts loaded from files (TTF_FONT + TTF_FALLBACK<N>)
     * FONT must be set to the family name of the loaded TTF */
    if (cfg.ttf_enabled)
    {
        int fi;

        win32_set_font_size(cfg.ttf_size);
        win32_add_font_file(cfg.ttf_font);

        for (fi = 0; fi < TE_CFG_TTF_FALLBACKS; fi++)
        {
            if (cfg.ttf_fallback[fi][0])
                win32_add_font_file(cfg.ttf_fallback[fi]);
        }
    }
#endif

    /* Init ncurses */
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);

    set_tabsize(cfg.tab_width > 0 ? cfg.tab_width : 4);

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
    /* Enable mouse if configured */
    if (cfg.mouse_enabled)
    {
        mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
        mouseinterval(0);

        /* Button event tracking: motion while button held */
        printf("\033[?1002h");
        fflush(stdout);

        /* Enable SGR mouse mode for better SSH support */
        printf("\033[?1006h");
        fflush(stdout);
    }

    set_escdelay(25);
#endif

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
    /* Register SGR mouse escape sequence for SSH */
    define_key("\033[<", KEY_MOUSE_SGR);

    /* Register bracketed paste sequences */
    define_key("\033[200~", KEY_PASTE_START);
    define_key("\033[201~", KEY_PASTE_END);

    /* Register Ctrl+arrow key sequences for word navigation */
    define_key("\033[1;5D", KEY_CLEFT);
    define_key("\033[1;5C", KEY_CRIGHT);
    define_key("\033[1;5A", KEY_CUP);
    define_key("\033[1;5B", KEY_CDOWN);
    define_key("\033[1;5H", KEY_CHOME);
    define_key("\033[1;5F", KEY_CEND);

    /* Register Ctrl+Shift+arrow key sequences for word selection */
    define_key("\033[1;6D", KEY_CSLEFT);
    define_key("\033[1;6C", KEY_CSRIGHT);
    define_key("\033[1;6A", KEY_CSUP);
    define_key("\033[1;6B", KEY_CSDOWN);
    define_key("\033[5;2~", KEY_SPPAGE);
    define_key("\033[6;2~", KEY_SNPAGE);
    define_key("\033[1;6d", KEY_CSUPD);
    define_key("\033[1;6u", KEY_CSDOWNU);
    define_key("\033[1;6H", KEY_CSHOME);
    define_key("\033[1;6F", KEY_CSEND);

    /* Register Shift+arrow key sequences for character/line selection */
    define_key("\033[1;2D", KEY_SLEFT);
    define_key("\033[1;2C", KEY_SRIGHT);
    define_key("\033[1;2A", KEY_SUP);
    define_key("\033[1;2B", KEY_SDOWN);
    define_key("\033[1;2H", KEY_SHOME);
    define_key("\033[1;2F", KEY_SEND);

    /* Register Alt+arrow key sequences for move line (xterm / rxvt) */
    define_key("\033[1;3A", KEY_ALT_UP);
    define_key("\033[1;3B", KEY_ALT_DOWN);
    define_key("\033[1;3C", KEY_ALT_RIGHT);
    define_key("\033[1;3D", KEY_ALT_LEFT);
    define_key("\033\033[A", KEY_ALT_UP);
    define_key("\033\033[B", KEY_ALT_DOWN);
    define_key("\033\033[C", KEY_ALT_RIGHT);
    define_key("\033\033[D", KEY_ALT_LEFT);

    /* Register Alt+key sequences for editor functions */
    define_key("\033y", KEY_ALT('Y'));
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
    define_key("\033i", KEY_ALT('I'));
    define_key("\033I", KEY_ALT('I'));
    define_key("\033j", KEY_ALT('J'));
    define_key("\033J", KEY_ALT('J'));
    define_key("\033k", KEY_ALT('K'));
    define_key("\033K", KEY_ALT('K'));
    define_key("\033l", KEY_ALT('L'));
    define_key("\033L", KEY_ALT('L'));
    define_key("\033w", KEY_ALT('W'));
    define_key("\033W", KEY_ALT('W'));
    define_key("\033u", KEY_ALT('U'));
    define_key("\033U", KEY_ALT('U'));
    define_key("\033r", KEY_ALT('R'));
    define_key("\033R", KEY_ALT('R'));
    define_key("\033m", KEY_ALT('M'));
    define_key("\033M", KEY_ALT('M'));
    define_key("\033n", KEY_ALT('N'));
    define_key("\033N", KEY_ALT('N'));
    define_key("\033v", KEY_ALT('V'));
    define_key("\033V", KEY_ALT('V'));
    define_key("\033x", KEY_ALT('X'));
    define_key("\033X", KEY_ALT('X'));
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
#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
        /* Disable mouse if it was enabled */
        if (cfg.mouse_enabled)
        {
            printf("\033[?1002l");
            printf("\033[?1006l");
            fflush(stdout);
        }
#endif
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
    app->spell_active = 0; /* Disabled by default */

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

#ifdef HAVE_TRANSLATE
    /* Initialize translate_enabled from config */
    app->translate_enabled = cfg.translate_enabled;
    app->translate_active = cfg.translate_enabled;

    /* Load translator from config */
    ui_translate_load_from_config(app);
#endif

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
        TeTab *tab = NULL;

        content = load_file(argv[1], app);

        tab = te_tab_new_with_content(argv[1], content);

        if (tab)
        {
            tab->show_line_numbers = cfg.show_line_numbers;

            ed_set_word_move_mode(tab->editor, cfg.word_move_mode);
            te_app_add_tab(app, tab);
            te_app_switch_tab(app, 0);

            detected = ui_editor_detect_wrap_hyphens(app);

            if (detected > 0)
                te_status(app, "Detected %d wrap-hyphens", detected);
        }

        if (content)
            free(content);
    }
    else
    {
        TeTab *tab = te_tab_new();

        if (tab)
        {
            tab->show_line_numbers = cfg.show_line_numbers;
            ed_set_word_move_mode(tab->editor, cfg.word_move_mode);

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

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
    /* Disable button event tracking if mouse was enabled */
    printf("\033[?1002l");
    printf("\033[?1006l");
    fflush(stdout);
#endif

    endwin();

#ifdef PLATFORM_AMIGA
    /* Close iffparse.library */
    if (IFFParseBase)
        CloseLibrary(IFFParseBase);
#endif

    return 0;
}
