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

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include "te.h"
#include "../core/utf8.h"
#include "../components/config.h"
#include "../components/editor.h"
#include "../components/ed_attr.h"
#include "ui_editor_helper.h"
#include "ui_spell.h"
#include "ui_grammar.h"
#include "../wrapper.h"

#ifdef HAVE_TRANSLATE
#include "../translate/translate.h"
#endif

#ifdef HAVE_TRANSLATE_STARDICT
#include "ui_dict.h"
#endif

#ifdef HAVE_TTS
#include "ui_tts.h"
#endif

#ifdef HAVE_HUNSPELL
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/spell.h"
#else
#include "../spell/spell.h"
#endif
#include "ui_spell.h"
#endif

#ifdef HAVE_MYTHES
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/thes.h"
#else
#include "../thes/thes.h"
#endif
#endif

#ifdef HAVE_HYPHEN
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/hyph.h"
#else
#include "../hyph_wrap/hyph_wrap.h"
#include "core/portable.h"
#endif
#endif

TeApp *te_app_new(void)
{
    TeApp *app = NULL;

    app = (TeApp *)calloc(1, sizeof(TeApp));

    if (!app)
        return NULL;

    app->wm = wm_new();

    if (!app->wm)
    {
        free(app);
        return NULL;
    }

    /* Create windows for layout */
    wm_add_window(app->wm, WIN_TABLIST, 0, 1, 20, LINES - 1);
    wm_add_window(app->wm, WIN_EDITOR, 20, 1, COLS - 20, LINES - 1);
    wm_add_window(app->wm, WIN_TRANSLATE, 20, LINES - SPELL_PANEL_HEIGHT - 1, COLS - 20, SPELL_PANEL_HEIGHT);
    wm_add_window(app->wm, WIN_SPELL, 20, LINES - SPELL_PANEL_HEIGHT - 1, COLS - 20, SPELL_PANEL_HEIGHT);

#ifdef HAVE_TRANSLATE_STARDICT
    wm_add_window(app->wm, WIN_DICT, 20, LINES - DICT_PANEL_HEIGHT - 1, COLS - 20, DICT_PANEL_HEIGHT);
#else
    wm_add_window(app->wm, WIN_DICT, 20, LINES - SPELL_PANEL_HEIGHT - 1, COLS - 20, SPELL_PANEL_HEIGHT);
#endif

    app->tab_cap = 8;
    app->tabs = (TeTab **)calloc(app->tab_cap, sizeof(TeTab *));

    if (!app->tabs)
    {
        wm_free(app->wm);
        free(app);

        return NULL;
    }

    app->tab_count = 0;
    app->active_tab = -1;
    app->show_line_numbers = 0;
    app->show_tabs = 1;
    app->show_translate = 0;
    app->show_spell = 0;
    app->spell_panel_mode = -1; /* -1=hidden, 0=spell, 1=translate, 2=dict */
    app->tabs_panel_active = 0;
    app->tabs_panel_selected = 0;

    app->dict_result = NULL;
    app->dict_word[0] = '\0';
    app->dict_scroll = 0;
    app->bracket_match_row = -1;
    app->bracket_match_col = -1;
    app->hard_wrap = 0;
    app->wrap_col = 75;
    app->rich_mode = 0;

    app->search.rows = NULL;
    app->search.cols = NULL;
    app->search.count = 0;
    app->search.is_mode = 0;
    app->search.only_mode = 0;
    app->search.match_current = 0;
    app->search.match_total = 0;
    app->search.query[0] = L'\0';

#ifdef HAVE_HUNSPELL
    app->spell_handle = NULL;
    app->spell_enabled = 0; /* Will be set from config after loading */
    app->spell_active = 0;  /* Will be set from config after loading */
    app->spell_current_word[0] = L'\0';
    app->spell_word_status = 0;
    app->spell_suggestions = NULL;
    app->spell_suggestion_count = 0;
    app->spell_scroll_offset = 0;

    ui_spell_cache_init(&app->spell_cache);

#ifdef HAVE_HYPHEN
    app->hyph_handle = NULL;
#endif

#ifdef HAVE_MYTHES
    app->thes_handle = NULL;
#endif
#endif

    strncpy(app->charset_in, "UTF-8", sizeof(app->charset_in) - 1);
    app->charset_in[sizeof(app->charset_in) - 1] = '\0';

    strncpy(app->charset_out, "UTF-8", sizeof(app->charset_out) - 1);
    app->charset_out[sizeof(app->charset_out) - 1] = '\0';

    return app;
}

void te_app_free(TeApp *app)
{
    int i;

    if (!app)
        return;

    for (i = 0; i < app->tab_count; i++)
    {
        if (app->tabs[i])
        {
            te_tab_free(app->tabs[i]);
            app->tabs[i] = NULL;
        }
    }

    free(app->tabs);

    app->tabs = NULL;

    if (app->search.rows)
    {
        free(app->search.rows);
        app->search.rows = NULL;
    }

    if (app->dict_result)
    {
        free(app->dict_result);
        app->dict_result = NULL;
    }

    if (app->search.cols)
    {
        free(app->search.cols);
        app->search.cols = NULL;
    }

#ifdef HAVE_MYTHES
    if (app->thes_handle)
    {
        thes_free((ThesHandle *)app->thes_handle);
        app->thes_handle = NULL;
    }
#endif

#ifdef HAVE_GRAMMAR
    ui_grammar_free_app(app);
#endif

#ifdef HAVE_HYPHEN
    if (app->hyph_handle)
    {
        hyph_free((HyphDict *)app->hyph_handle);
        app->hyph_handle = NULL;
    }
#endif

#ifdef HAVE_HUNSPELL
    ui_spell_cache_clear(&app->spell_cache);

    if (app->spell_handle)
    {
        spell_free((SpellChecker *)app->spell_handle);
        app->spell_handle = NULL;
    }
#endif

#ifdef HAVE_TRANSLATE
    if (app->translate_handle)
    {
        translate_free((TranslateHandle *)app->translate_handle);
        app->translate_handle = NULL;
    }
#endif

#ifdef HAVE_TTS
    /* Stop in-flight speech before freeing TtsHandle */
    ui_tts_unload(app);
#endif

    if (app->wm)
    {
        wm_free(app->wm);
        app->wm = NULL;
    }

    free(app);
}

/* Get currently active tab */
TeTab *te_app_get_active_tab(TeApp *app)
{
    if (!app)
        return NULL;

    if (app->active_tab < 0 || app->active_tab >= app->tab_count)
        return NULL;

    return app->tabs[app->active_tab];
}

#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
/* Returns the active tab's hyphen-wrap toggle (0 if no tab) */
int te_app_hyph_wrap_enabled(TeApp *app)
{
    TeTab *tab = te_app_get_active_tab(app);

    return tab ? tab->hyph_wrap_enabled : 0;
}
#endif

/* Add tab to app */
int te_app_add_tab(TeApp *app, TeTab *tab)
{
    TeTab **new_tabs;
    int new_cap;

    if (!app || !tab)
        return -1;

    if (app->tab_count >= app->tab_cap)
    {
        new_cap = app->tab_cap * 2;
        new_tabs = (TeTab **)realloc(app->tabs, new_cap * sizeof(TeTab *));

        if (!new_tabs)
            return -1;

        app->tabs = new_tabs;
        app->tab_cap = new_cap;
    }

    /* Apply app configuration to new tab */
    ed_set_undo_levels(tab->editor, app->cfg.undo_levels);
    ed_set_hard_wrap(tab->editor, app->cfg.hard_wrap);

    tab->show_line_numbers = app->show_line_numbers;

    strncpy(tab->charset_in, app->cfg.charset_in, sizeof(tab->charset_in) - 1);
    tab->charset_in[sizeof(tab->charset_in) - 1] = '\0';

    strncpy(tab->charset_out, app->cfg.charset_out, sizeof(tab->charset_out) - 1);
    tab->charset_out[sizeof(tab->charset_out) - 1] = '\0';

#ifdef HAVE_HYPHEN
    /* Inherit hyphen-wrap default from config */
    tab->hyph_wrap_enabled = app->cfg.hyph_wrap_enabled;
#endif

    app->tabs[app->tab_count] = tab;
    app->tab_count++;

    if (app->active_tab < 0)
        app->active_tab = 0;

    return 0;
}

/* Close tab by index */
int te_app_close_tab(TeApp *app, int index)
{
    int i;

    if (!app)
        return -1;

    if (index < 0 || index >= app->tab_count)
        return -1;

    if (app->tabs[index])
    {
        te_tab_free(app->tabs[index]);
        app->tabs[index] = NULL;
    }

    for (i = index; i < app->tab_count - 1; i++)
        app->tabs[i] = app->tabs[i + 1];

    /* Clear the now-unused slot so the last tab is not duplicated and leaked */
    app->tabs[app->tab_count - 1] = NULL;

    app->tab_count--;

    if (app->active_tab >= app->tab_count)
        app->active_tab = app->tab_count - 1;

    if (app->active_tab >= 0 && app->active_tab < app->tab_count)
    {
        TeTab *active = app->tabs[app->active_tab];

        if (active)
        {
            app->rich_mode = active->rich_mode;

            if (active->editor)
                app->hard_wrap = ed_get_hard_wrap(active->editor);
        }
    }

    te_app_switch_tab(app, app->active_tab);

    return 0;
}

/* Switch to tab by index */
void te_app_switch_tab(TeApp *app, int index)
{
    if (!app)
        return;

    if (index < 0 || index >= app->tab_count)
        return;

    app->active_tab = index;

    if (app->tabs[index])
    {
        app->rich_mode = app->tabs[index]->rich_mode;

        if (app->tabs[index]->editor)
            app->hard_wrap = ed_get_hard_wrap(app->tabs[index]->editor);

        if (app->tabs[index]->charset_in[0])
        {
            strncpy(app->charset_in, app->tabs[index]->charset_in, sizeof(app->charset_in) - 1);
            app->charset_in[sizeof(app->charset_in) - 1] = '\0';
        }
        else
        {
            strncpy(app->charset_in, app->cfg.charset_in, sizeof(app->charset_in) - 1);
            app->charset_in[sizeof(app->charset_in) - 1] = '\0';
        }

        if (app->tabs[index]->charset_out[0])
        {
            strncpy(app->charset_out, app->tabs[index]->charset_out, sizeof(app->charset_out) - 1);
            app->charset_out[sizeof(app->charset_out) - 1] = '\0';
        }
        else
        {
            strncpy(app->charset_out, app->cfg.charset_out, sizeof(app->charset_out) - 1);
            app->charset_out[sizeof(app->charset_out) - 1] = '\0';
        }
    }
}

/* Return the index of the first tab with unsaved changes, or -1 if none */
int te_app_first_modified_tab(TeApp *app)
{
    int i;

    if (!app)
        return -1;

    for (i = 0; i < app->tab_count; i++)
    {
        TeTab *tab = app->tabs[i];

        if (tab && tab->editor)
        {
            EdInfo info;

            ed_get_info(tab->editor, &info);

            if (info.modified)
                return i;
        }
    }

    return -1;
}

/* Helper functions to access active tab data */
Ed *te_app_get_editor(TeApp *app)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return NULL;

    return tab->editor;
}

const char *te_app_get_filename(TeApp *app)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return "";

    return tab->filename;
}

int te_app_get_show_line_numbers(TeApp *app)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return 0;

    /* Line numbers are suppressed in rich mode — the WordStar ruler and margins define the layout, not a line-number gutter */
    if (tab->rich_mode)
        return 0;

    return tab->show_line_numbers;
}

void te_app_set_filename(TeApp *app, const char *val)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return;

    strncpy(tab->filename, val, TAB_FILENAME_MAX - 1);
    tab->filename[TAB_FILENAME_MAX - 1] = '\0';

    /* Sync charset values from app to tab for status bar display */
    strncpy(tab->charset_in, app->charset_in, sizeof(tab->charset_in) - 1);
    tab->charset_in[sizeof(tab->charset_in) - 1] = '\0';

    strncpy(tab->charset_out, app->charset_out, sizeof(tab->charset_out) - 1);
    tab->charset_out[sizeof(tab->charset_out) - 1] = '\0';
}

void te_app_clear_filename(TeApp *app)
{
    TeTab *tab = NULL;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return;

    tab->filename[0] = '\0';
}

void te_app_set_show_line_numbers(TeApp *app, int val)
{
    TeTab *tab = NULL;

    app->show_line_numbers = val;

    tab = te_app_get_active_tab(app);

    if (!tab)
        return;

    tab->show_line_numbers = val;
}

/* Initialize ncurses colors from config */
void te_init_colors(const TeConfig *cfg)
{
    int i;

    if (!has_colors())
        return;

    start_color();
    use_default_colors();

    /* Use config colors if provided, otherwise use defaults */
    if (cfg)
    {
        /* Apply colors; remap non-explicit slots via color_map */
        for (i = 1; i < TE_CFG_COLOR_MAX; i++)
        {
            int fg = cfg->color_fg[i];
            int bg = cfg->color_bg[i];

            if (!cfg->color_explicit[i] && cfg->color_map_initialized)
            {
                if (fg >= 0 && fg < 16)
                    fg = cfg->color_map[fg];

                if (bg >= 0 && bg < 16)
                    bg = cfg->color_map[bg];
            }

            init_pair((short)i, (short)fg, (short)bg);
        }

        /* Apply default background to entire screen */
        bkgd(COLOR_PAIR(COL_NORMAL) | ' ');

        erase();
        refresh();
    }
    else
    {
        /* Default colors */
        init_pair(COL_NORMAL, COLOR_WHITE, COLOR_BLACK);
        init_pair(COL_STATUS, COLOR_BLACK, COLOR_WHITE);
        init_pair(COL_TITLEBAR, COLOR_BLACK, COLOR_CYAN);
        init_pair(COL_POPUP, COLOR_WHITE, COLOR_BLUE);
        init_pair(COL_POPUP_SEL, COLOR_BLACK, COLOR_CYAN);
        init_pair(COL_BORDER, COLOR_CYAN, COLOR_BLACK);
        init_pair(COL_SEARCH_MATCH, COLOR_BLACK, COLOR_YELLOW);
        init_pair(COL_SPELL_CURRENT, COLOR_WHITE, COLOR_MAGENTA);
        init_pair(COL_BRACKET_MATCH, COLOR_BLACK, COLOR_YELLOW);
        init_pair(COL_CURRENT_LINE, COLOR_WHITE, COLOR_BLUE);
        init_pair(COL_GUIDE, COLOR_CYAN, COLOR_BLACK);
        init_pair(COL_SYNTAX_KEYWORD, COLOR_YELLOW, COLOR_BLACK);
        init_pair(COL_SYNTAX_STRING, COLOR_GREEN, COLOR_BLACK);
        init_pair(COL_SYNTAX_COMMENT, COLOR_CYAN, COLOR_BLACK);
        init_pair(COL_SYNTAX_NUMBER, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(COL_SYNTAX_PREPROC, COLOR_BLUE, COLOR_BLACK);
        init_pair(COL_SYNTAX_OPERATOR, COLOR_RED, COLOR_BLACK);
    }
}

/* Set status bar message */
void te_status(TeApp *app, const char *fmt, ...)
{
    va_list ap;

    if (!app)
        return;

    va_start(ap, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, ap);
    va_end(ap);
}

/* Draw title bar with filename and cursor info */
void te_draw_titlebar(TeApp *app)
{
    int x;
    char left[128], right[64];
    EdInfo info;
    const char *fn = NULL;
    int mod = 0;
    int prefix_len, suffix_len, max_fn_len, fn_len;
    char truncated[128];
    int avail;
    TeTab *tab = NULL;

    standend();
    attron(COLOR_PAIR(COL_TITLEBAR));

    move(0, 0);

    for (x = 0; x < COLS; x++)
        addch(' ');

    tab = te_app_get_active_tab(app);

    if (tab && tab->filename[0])
    {
        const char *last_slash = strrchr(tab->filename, '/');

        if (!last_slash)
            last_slash = strrchr(tab->filename, '\\');

        fn = last_slash ? last_slash + 1 : tab->filename;
    }
    else
    {
        fn = "[No Name]";
    }

    if (tab && tab->editor)
    {
        ed_get_info(tab->editor, &info);
        mod = info.modified;
    }

    /* On Amiga/Windows the window title already shows the app name, so the top bar only needs the filename */
    /*#if defined(PLATFORM_AMIGA) || defined(PLATFORM_WIN32)*/
    prefix_len = 0;
    /*#else
        prefix_len = (int)strlen(WRAPPER_PID) + 2;
    #endif*/

    suffix_len = mod ? 4 : 0;
    max_fn_len = COLS - prefix_len - suffix_len - 30;

    fn_len = (int)strlen(fn);

    if (fn_len > max_fn_len)
    {
        avail = max_fn_len - 3;

        if (avail < 0)
            avail = 0;

        if (avail > (int)sizeof(truncated) - 4)
            avail = (int)sizeof(truncated) - 4;

        strncpy(truncated, fn, avail);

        truncated[avail] = '\0';

        strcat(truncated, "...");
        fn = truncated;
    }

    /*#if defined(PLATFORM_AMIGA) || defined(PLATFORM_WIN32)*/
    snprintf(left, sizeof(left), "%s%s", fn, mod ? " [+]" : "");
    /*#else
        snprintf(left, sizeof(left), "%s  %s%s", WRAPPER_PID, fn, mod ? " [+]" : "");
    #endif*/

    if (tab && tab->editor)
    {
        char sp_buf[32];
        char hy_buf[32];
        char tr_buf[32];
        char syntax_buf[32];
        static const char *syntax_names[] = {"Auto", "C", "C++", "x86 asm", "m68k asm", "Amiga C"};

        sp_buf[0] = '\0';
        hy_buf[0] = '\0';
        tr_buf[0] = '\0';
        syntax_buf[0] = '\0';

        ed_get_info(tab->editor, &info);

        if (app->cfg.syntax_enabled)
            snprintf(syntax_buf, sizeof(syntax_buf), "[%s] ", syntax_names[tab->syntax_lang + 1]);

#ifdef HAVE_HUNSPELL
        if (app->spell_enabled && app->spell_active && app->spell_handle)
            snprintf(sp_buf, sizeof(sp_buf), "SP ");
#endif

#ifdef HAVE_HYPHEN
        if (te_app_hyph_wrap_enabled(app) && app->hyph_handle)
            snprintf(hy_buf, sizeof(hy_buf), "HY ");
#endif

#ifdef HAVE_TRANSLATE
        if (app->translate_active && app->translate_handle)
            snprintf(tr_buf, sizeof(tr_buf), "TR [%.7s]->[%.7s] ", app->cfg.translate_from_lang, app->cfg.translate_to_lang);
#endif
        snprintf(right, sizeof(right), "Ln %d/%d  Col %d  %s %s%s%s%s%s", info.row + 1, info.line_count, info.col + 1, app->hard_wrap ? "HARD" : "SOFT", syntax_buf, sp_buf, hy_buf, tr_buf, info.insert_mode ? "INS" : "OVR");
    }
    else
    {
        right[0] = '\0';
    }

    mvaddnstr(0, 0, left, COLS);

    if (right[0])
    {
        int rlen = (int)strlen(right);
        int rx = COLS - rlen;

        if (rx > (int)strlen(left) + 2)
            mvaddnstr(0, rx, right, rlen);
    }

    attroff(COLOR_PAIR(COL_TITLEBAR));
}

/* Draw rich-text bar below the titlebar with bold/italic/underline/alignment state */
void te_draw_richbar(TeApp *app)
{
    int x;
    char buf[128];
    const char *align_name;
    unsigned short mask = 0;
    unsigned char align = EA_ALIGN_LEFT;
    TeTab *tab = NULL;
    Ed *ed = NULL;
    EdInfo info;

    if (!app || !app->rich_mode)
        return;

    tab = te_app_get_active_tab(app);
    if (tab && tab->editor)
    {
        ed = tab->editor;
        ed_get_info(ed, &info);

        if (info.row >= 0 && info.row < ed->count)
        {
            EdLine *ln = ed->lines[info.row];

            align = ln->para_align;

            if (info.col >= 0 && info.col < ln->len)
                mask = ed_attr_mask_at(ln, info.col, NULL, NULL) | ed->input_mask;
            else
                mask = ed->input_mask;
        }
    }

    switch (align)
    {
    case EA_ALIGN_CENTER:
        align_name = "Center";
        break;
    case EA_ALIGN_RIGHT:
        align_name = "Right";
        break;
    case EA_ALIGN_JUST:
        align_name = "Justify";
        break;
    default:
        align_name = "Left";
        break;
    }

    snprintf(buf, sizeof(buf), " [B]old %s  [I]talic %s  [U]nderline %s  [T]Strike %s  Align: %s ", (mask & EA_BOLD) ? "ON " : "off", (mask & EA_ITALIC) ? "ON " : "off", (mask & EA_UNDERLINE) ? "ON " : "off", (mask & EA_STRIKE) ? "ON " : "off", align_name);

    standend();
    attron(COLOR_PAIR(COL_STATUS));

    move(1, 0);

    for (x = 0; x < COLS; x++)
        addch(' ');

    mvaddnstr(1, 0, buf, COLS);

    attroff(COLOR_PAIR(COL_STATUS));
}

/* WordStar ruler below richbar; ticks every 5/10 cols, marks margins */
void te_draw_ruler(TeApp *app)
{
    TeTab *tab = NULL;
    Ed *ed = NULL;
    EdInfo info;
    int y;
    int x;
    int ln_offset = 0;
    int body_cols;
    int ml;
    int mr;
    int screen_x;
    char ch;
    int tpc;
    int tw;
    int prev_tw;
    int cm;
    int prev_cm;
    int half_cm;
    int prev_half_cm;

    if (!app)
        return;

    tab = te_app_get_active_tab(app);

    if (!tab || !tab->rich_mode || !tab->ruler_visible || !tab->editor)
        return;

    ed = tab->editor;

    /* Ruler sits right below the richbar which is on row 1 */
    y = 2;

    ed_get_info(ed, &info);

    /* Line-number width only, do not include margin_left here or the ticks would follow the margin instead of showing where it lands */
    if (te_app_get_show_line_numbers(app))
    {
        int n = info.line_count > 0 ? info.line_count : 1;
        int mg = 1;

        while (n >= 10)
        {
            n /= 10;
            mg++;
        }

        ln_offset = mg + 1;
    }

    body_cols = COLS - ln_offset;

    if (body_cols < 1)
        return;

    ml = ed->margin_left;
    mr = ed->margin_right;

    if (ml < 0)
        ml = 0;

    if (ml >= body_cols)
        ml = body_cols - 1;

    if (mr < 0)
        mr = 0;

    if (mr >= body_cols)
        mr = body_cols - 1;

    standend();
    attron(COLOR_PAIR(COL_STATUS));

    move(y, 0);

    for (x = 0; x < COLS; x++)
        addch(' ');

    /* Column markers; ticks count from 0 at [ so mm/cm reflect distance from left margin */
    for (x = 0; x < body_cols; x++)
    {
        screen_x = ln_offset + x;

        if (tab->ruler_mm)
        {
            /* mm mode: 1 cm = 567 twips, 5 mm = 283 twips; mark cm with digit, 5mm with ':' */
            tpc = ed->twips_per_col;
            tw = x * tpc;
            prev_tw = (x - 1) * tpc;
            cm = tw / 567;
            prev_cm = prev_tw / 567;
            half_cm = tw / 283;
            prev_half_cm = prev_tw / 283;

            if (x > 0 && cm != prev_cm)
                ch = (char)('0' + (cm % 10));
            else if (x > 0 && half_cm != prev_half_cm)
                ch = ':';
            else
                ch = '.';
        }
        else
        {
            if (x > 0 && (x % 10) == 0)
                ch = (char)('0' + ((x / 10) % 10));
            else if (x > 0 && (x % 5) == 0)
                ch = ':';
            else
                ch = '.';
        }

        mvaddch(y, screen_x, (chtype)ch);
    }

    /* Overlay margin markers; rich mode: [ at body col 0, ] at (mr-ml) */
    if (mr > ml)
    {
        if (tab->rich_mode)
        {
            mvaddch(y, ln_offset + 0, (chtype)'[');
            mvaddch(y, ln_offset + (mr - ml), (chtype)']');
        }
        else
        {
            mvaddch(y, ln_offset + ml, (chtype)'[');
            mvaddch(y, ln_offset + mr, (chtype)']');
        }
    }

    attroff(COLOR_PAIR(COL_STATUS));
}

/* Layout dialog with tabbed popup: margins on tab 1, ruler on tab 2 */
void ui_edit_layout(TeApp *app)
{
    static const char *const page_size_names[] =
        {
            "Letter", "Legal", "Tabloid", "Statement", "Executive",
            "Folio", "Quarto", "10x14", "A3", "A4",
            "A5", "A6", "B4 (ISO)", "B5 (ISO)", "B6 (ISO)",
            "C5 Env", "DL Env", "Monarch Env", "Comm10 Env"};

    static const int page_size_w[] =
        {
            12240, 12240, 15840, 7920, 10440,
            12240, 11520, 14400, 16838, 11906,
            8391, 5953, 14173, 9978, 7087,
            9184, 6236, 5580, 5940};

    static const int page_size_h[] =
        {
            15840, 20160, 24480, 12240, 15120,
            18720, 14400, 20160, 23811, 16838,
            11906, 8391, 20013, 14173, 9978,
            12983, 12472, 10800, 13680};

    static const char *const orient_names[] = {"Portrait", "Landscape"};
    static const char *const ruler_unit_names[] = {"Columns", "Millimetres"};

    TeTab *tab = NULL;
    Ed *ed = NULL;
    PopupField margin_fields[6];
    PopupField ruler_fields[2];
    PopupTab tabs[2];
    int rc;
    int size_idx = 0;
    int orient_idx = 0;
    int i;
    int init_left_mm, init_right_mm;
    int new_size;
    int new_orient;
    int lmm;
    int rmm;

    if (!app)
        return;

    tab = te_app_get_active_tab(app);

    if (!tab || !tab->editor)
        return;

    if (!tab->rich_mode)
    {
        te_status(app, "Layout: only available in rich mode");
        return;
    }

    ed = tab->editor;

    /* Determine current page size index from captured geometry, or from the configured default page size for new documents */
    if (ed->page_w_tw > 0 && ed->page_h_tw > 0)
    {
        int pw = ed->page_w_tw;
        int ph = ed->page_h_tw;

        for (i = 0; i < 19; i++)
        {
            if (page_size_w[i] == pw && page_size_h[i] == ph)
            {
                size_idx = i;
                orient_idx = 0;
                break;
            }
            if (page_size_w[i] == ph && page_size_h[i] == pw)
            {
                size_idx = i;
                orient_idx = 1;
                break;
            }
        }
    }
    else
    {
        size_idx = app->cfg.default_page_size;
        orient_idx = 0;
    }

    margin_fields[0].label = "Page size";
    margin_fields[0].type = POPUP_FIELD_CHOICE;
    margin_fields[0].int_val = size_idx;
    margin_fields[0].int_min = 0;
    margin_fields[0].int_max = 18;
    margin_fields[0].choices = page_size_names;
    margin_fields[0].n_choices = 19;

    margin_fields[1].label = "Orientation";
    margin_fields[1].type = POPUP_FIELD_CHOICE;
    margin_fields[1].int_val = orient_idx;
    margin_fields[1].int_min = 0;
    margin_fields[1].int_max = 1;
    margin_fields[1].choices = orient_names;
    margin_fields[1].n_choices = 2;

    margin_fields[2].label = "Left margin (cols)";
    margin_fields[2].type = POPUP_FIELD_INT;
    margin_fields[2].int_val = ed->margin_left;
    margin_fields[2].int_min = 0;
    margin_fields[2].int_max = 999;
    margin_fields[2].choices = NULL;
    margin_fields[2].n_choices = 0;

    margin_fields[3].label = "Right margin (cols)";
    margin_fields[3].type = POPUP_FIELD_INT;
    margin_fields[3].int_val = ed->margin_right;
    margin_fields[3].int_min = 0;
    margin_fields[3].int_max = 999;
    margin_fields[3].choices = NULL;
    margin_fields[3].n_choices = 0;

    /* mm fields: convert from cols using twips_per_col. mm = cols * tpc * 254 / 14400 (rounded) */
    init_left_mm = (int)((long)ed->margin_left * ed->twips_per_col * 254L + 7200L) / 14400;
    init_right_mm = (int)((long)ed->margin_right * ed->twips_per_col * 254L + 7200L) / 14400;

    margin_fields[4].label = "Left margin (mm)";
    margin_fields[4].type = POPUP_FIELD_INT;
    margin_fields[4].int_val = init_left_mm;
    margin_fields[4].int_min = 0;
    margin_fields[4].int_max = 999;
    margin_fields[4].choices = NULL;
    margin_fields[4].n_choices = 0;

    margin_fields[5].label = "Right margin (mm)";
    margin_fields[5].type = POPUP_FIELD_INT;
    margin_fields[5].int_val = init_right_mm;
    margin_fields[5].int_min = 0;
    margin_fields[5].int_max = 999;
    margin_fields[5].choices = NULL;
    margin_fields[5].n_choices = 0;

    ruler_fields[0].label = "Visible";
    ruler_fields[0].type = POPUP_FIELD_BOOL;
    ruler_fields[0].int_val = tab->ruler_visible;
    ruler_fields[0].int_min = 0;
    ruler_fields[0].int_max = 1;
    ruler_fields[0].choices = NULL;
    ruler_fields[0].n_choices = 0;

    ruler_fields[1].label = "Units";
    ruler_fields[1].type = POPUP_FIELD_CHOICE;
    ruler_fields[1].int_val = tab->ruler_mm;
    ruler_fields[1].int_min = 0;
    ruler_fields[1].int_max = 1;
    ruler_fields[1].choices = ruler_unit_names;
    ruler_fields[1].n_choices = 2;

    tabs[0].name = "Margins";
    tabs[0].fields = margin_fields;
    tabs[0].n_fields = 6;

    tabs[1].name = "Ruler";
    tabs[1].fields = ruler_fields;
    tabs[1].n_fields = 2;

    rc = ui_popup_tabbed("Rich mode layout", tabs, 2, 0);

    /* Commit: page size and orientation */
    new_size = margin_fields[0].int_val;
    new_orient = margin_fields[1].int_val;

    if (new_orient == 0)
    {
        ed->page_w_tw = page_size_w[new_size];
        ed->page_h_tw = page_size_h[new_size];
    }
    else
    {
        ed->page_w_tw = page_size_h[new_size];
        ed->page_h_tw = page_size_w[new_size];
    }

    /* If mm values changed, prefer mm; otherwise use cols */
    if (margin_fields[4].int_val != init_left_mm || margin_fields[5].int_val != init_right_mm)
    {
        /* User edited mm: convert to cols (twips = mm * 14400 / 254) */
        int lt = (int)((long)margin_fields[4].int_val * 14400L / 254L);
        int rt = (int)((long)margin_fields[5].int_val * 14400L / 254L);

        ed->margin_left = lt / ed->twips_per_col;
        ed->margin_right = rt / ed->twips_per_col;
    }
    else
    {
        ed->margin_left = margin_fields[2].int_val;
        ed->margin_right = margin_fields[3].int_val;
    }

    tab->ruler_visible = ruler_fields[0].int_val;
    tab->ruler_mm = ruler_fields[1].int_val;

    /* Invalidate captured twips so export derives from columns */
    ed->margin_left_tw = 0;
    ed->margin_right_tw = 0;

    /* Reflow hard-wrap paragraphs to the new margin span */
    if (app->hard_wrap)
        ed_auto_rewrap_after_edit(app);

    lmm = (int)((long)ed->margin_left * ed->twips_per_col * 254L + 7200L) / 14400;
    rmm = (int)((long)ed->margin_right * ed->twips_per_col * 254L + 7200L) / 14400;

    te_status(app, "Layout: %s %s  L=%dcol/%dmm R=%dcol/%dmm  ruler=%s %s", page_size_names[margin_fields[0].int_val], orient_names[margin_fields[1].int_val], ed->margin_left, lmm, ed->margin_right, rmm, tab->ruler_visible ? "ON" : "OFF", tab->ruler_mm ? "mm" : "cols");
}

/* Draw status bar with hints and charset info */
void te_draw_statusbar(TeApp *app)
{
    int x, y;
    int msg_len, rzone_len, rzone_start, max_left;
    char hint[64];
    char charset_info[192];
    char wc_str[32] = "";
    char right_zone[128];
    TeTab *tab = NULL;

    y = LINES - 1;

    standend();
    attron(COLOR_PAIR(COL_STATUS));

    move(y, 0);

    for (x = 0; x < COLS; x++)
        addch(' ');

    snprintf(hint, sizeof(hint), " F2=Save F3=Charset F1=Help ");

    if (app)
    {
        tab = te_app_get_active_tab(app);

        if (app->cfg.word_count && tab && tab->editor)
            snprintf(wc_str, sizeof(wc_str), "  Words: %d", ed_word_count(tab->editor));
    }

    rzone_len = (int)strlen(hint) + (int)strlen(wc_str);
    rzone_start = COLS - rzone_len - 1;

    if (rzone_start < 0)
        rzone_start = COLS;

    if (rzone_start < COLS)
    {
        snprintf(right_zone, sizeof(right_zone), "%s%s", wc_str, hint);
        mvaddnstr(y, rzone_start, right_zone, rzone_len);
    }

    if (app)
    {
        const char *msg = NULL;

        /* Show the transient status message if one is set */
        if (app->status[0])
        {
            msg = app->status;
        }
        else
        {
            if (tab)
            {
                const char *fn = NULL;
                const char *last_slash = NULL;

                if (tab->filename[0])
                {
                    last_slash = strrchr(tab->filename, '/');

                    if (!last_slash)
                        last_slash = strrchr(tab->filename, '\\');

                    fn = last_slash ? last_slash + 1 : tab->filename;

                    snprintf(charset_info, sizeof(charset_info), "%.80s  View: %.15s  Save: %.15s", fn, tab->charset_in[0] ? tab->charset_in : "UTF-8", tab->charset_out[0] ? tab->charset_out : "UTF-8");
                }
                else
                {
                    snprintf(charset_info, sizeof(charset_info), "View: %s  Save: %s", tab->charset_in[0] ? tab->charset_in : "UTF-8", tab->charset_out[0] ? tab->charset_out : "UTF-8");
                }
            }
            else
            {
                snprintf(charset_info, sizeof(charset_info), "View: UTF-8  Save: UTF-8");
            }

            msg = charset_info;
        }

        msg_len = (int)strlen(msg);
        max_left = (rzone_start < COLS ? rzone_start : COLS) - 2;

        if (max_left > msg_len)
            max_left = msg_len;

        if (max_left > 0)
            mvaddnstr(y, 1, msg, max_left);
    }

    attroff(COLOR_PAIR(COL_STATUS));
}

/* Draw box with borders */
void ui_box(int y, int x, int h, int w)
{
    int i;

    if (h < 2 || w < 2)
        return;

    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);

    for (i = 1; i < w - 1; i++)
    {
        mvaddch(y, x + i, ACS_HLINE);
        mvaddch(y + h - 1, x + i, ACS_HLINE);
    }

    for (i = 1; i < h - 1; i++)
    {
        mvaddch(y + i, x, ACS_VLINE);
        mvaddch(y + i, x + w - 1, ACS_VLINE);
    }
}

/* Draw horizontal line */
void te_hline(int y, int x, int len)
{
    int i;

    for (i = 0; i < len; i++)
        mvaddch(y, x + i, ACS_HLINE);
}

/* wchar_t -> UTF-8 (rotating static pool, safe for multiple calls per printf) */
const char *te_wcs2u8(const wchar_t *wcs)
{
    static char pool[8][512];
    static int slot = 0;
    char *out = NULL;
    int n, i;

    if (!wcs)
        return "";

    out = pool[slot];
    slot = (slot + 1) & 7;
    n = 0;

    for (i = 0; wcs[i] && n < (int)sizeof(pool[0]) - 4; i++)
    {
        unsigned long cp = (unsigned long)wcs[i];

        if (cp < 0x80)
        {
            out[n++] = (char)cp;
        }
        else if (cp < 0x800)
        {
            out[n++] = (char)(0xC0 | (cp >> 6));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out[n++] = (char)(0xE0 | (cp >> 12));
            out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        }
        else
        {
            out[n++] = (char)(0xF0 | (cp >> 18));
            out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (cp & 0x3F));
        }
    }

    out[n] = '\0';

    return out;
}
