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

/* ui_setup.c -- Full-screen tabbed configuration editor for tinyedit */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "te.h"
#include "ui_setup.h"
#include "ui_files.h"
#include "core/portable.h"
#include "../components/config.h"
#include "../core/charset.h"
#include "../core/keys.h"
#include "../core/utf8.h"

#ifdef HAVE_HUNSPELL
#include "../spell/spell.h"
#endif

/* Tabs */
#if defined(PLATFORM_AMIGA)
#ifdef AMIGA_TTF_TE
#ifdef HAVE_HUNSPELL
#define ST_TAB_COUNT 4
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font", "TTF Fallbacks", "Dictionary"};
#else
#define ST_TAB_COUNT 3
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font", "TTF Fallbacks"};
#endif
#else
#ifdef HAVE_HUNSPELL
#define ST_TAB_COUNT 3
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font", "Dictionary"};
#else
#define ST_TAB_COUNT 2
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font"};
#endif
#endif
#else
#ifdef HAVE_HUNSPELL
#define ST_TAB_COUNT 3
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font", "Dictionary"};
#else
#define ST_TAB_COUNT 2
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font"};
#endif
#endif

/* Field types */
typedef enum
{
    FT_STR,      /* free text */
    FT_INT,      /* integer */
    FT_BOOL,     /* yes/no toggle */
    FT_COLORMAP, /* integer 0-255, edit also marks color_map_initialized */
    FT_CHARSET,  /* cycle through available charsets */
    FT_CYCLE,    /* cycle through predefined string options */
    FT_COLORPAIR /* color pair configuration (fg/bg) */
#ifdef HAVE_HUNSPELL
    ,
    FT_DICTLIST,  /* cycle through available Hunspell dictionaries */
    FT_CUSTOMDICT /* select or create custom dictionary */
#endif
} FieldType;

typedef struct
{
    int tab;
    const char *label;
    FieldType type;
    size_t off; /* offsetof into TeConfig */
    int maxlen; /* for FT_STR */
} SetupField;

#define F_OFF(m) offsetof(TeConfig, m)

static const SetupField st_fields[] =
    {
        /* Editor */
        {0, "Charset", FT_CHARSET, F_OFF(charset), 0},
        {0, "Auto-wrap col", FT_INT, F_OFF(autowrap_col), 0},
        {0, "Hard wrap", FT_BOOL, F_OFF(hard_wrap), 0},
        {0, "Line numbers", FT_BOOL, F_OFF(show_line_numbers), 0},
        {0, "Undo levels", FT_INT, F_OFF(undo_levels), 0},

/* Colour/Font */
#if defined(PLATFORM_AMIGA) || defined(PLATFORM_WIN32)
        {1, "Font", FT_STR, F_OFF(font), TE_CFG_STR_MAX},
#endif
#ifdef PLATFORM_AMIGA
        {1, "TTF Enabled", FT_BOOL, F_OFF(ttf_enabled), 0},
        {1, "TTF Font", FT_STR, F_OFF(ttf_font), TE_CFG_STR_MAX},
        {1, "TTF Size", FT_INT, F_OFF(ttf_size), 0},
        {1, "TTF Antialias", FT_CYCLE, F_OFF(ttf_antialias), 0},
        {1, "TTF Encoding", FT_CYCLE, F_OFF(ttf_use_utf8), 0},
#endif
        {1, "Pen 0 (black)", FT_COLORMAP, F_OFF(color_map) + 0 * sizeof(int), 0},
        {1, "Pen 1 (red)", FT_COLORMAP, F_OFF(color_map) + 1 * sizeof(int), 0},
        {1, "Pen 2 (green)", FT_COLORMAP, F_OFF(color_map) + 2 * sizeof(int), 0},
        {1, "Pen 3 (yellow)", FT_COLORMAP, F_OFF(color_map) + 3 * sizeof(int), 0},
        {1, "Pen 4 (blue)", FT_COLORMAP, F_OFF(color_map) + 4 * sizeof(int), 0},
        {1, "Pen 5 (magenta)", FT_COLORMAP, F_OFF(color_map) + 5 * sizeof(int), 0},
        {1, "Pen 6 (cyan)", FT_COLORMAP, F_OFF(color_map) + 6 * sizeof(int), 0},
        {1, "Pen 7 (white)", FT_COLORMAP, F_OFF(color_map) + 7 * sizeof(int), 0},
        {1, "Normal", FT_COLORPAIR, F_OFF(color_fg) + COL_NORMAL * sizeof(int), 0},
        {1, "Status", FT_COLORPAIR, F_OFF(color_fg) + COL_STATUS * sizeof(int), 0},
        {1, "Titlebar", FT_COLORPAIR, F_OFF(color_fg) + COL_TITLEBAR * sizeof(int), 0},
        {1, "Popup", FT_COLORPAIR, F_OFF(color_fg) + COL_POPUP * sizeof(int), 0},
        {1, "Popup Sel", FT_COLORPAIR, F_OFF(color_fg) + COL_POPUP_SEL * sizeof(int), 0},
        {1, "Border", FT_COLORPAIR, F_OFF(color_fg) + COL_BORDER * sizeof(int), 0},
        {1, "Search", FT_COLORPAIR, F_OFF(color_fg) + COL_SEARCH_MATCH * sizeof(int), 0},
        {1, "Spell Current", FT_COLORPAIR, F_OFF(color_fg) + COL_SPELL_CURRENT * sizeof(int), 0},
        {1, "Cursor color", FT_INT, F_OFF(cursor_color), 0},
#if defined(PLATFORM_AMIGA) || defined(PLATFORM_WIN32)
        {1, "Default BG", FT_INT, F_OFF(default_bg_color), 0},
#endif

/* TTF Fallbacks */
#if defined(PLATFORM_AMIGA) && defined(AMIGA_TTF_TE)
        {2, "Fallback 1", FT_STR, F_OFF(ttf_fallback[0]), TE_CFG_STR_MAX},
        {2, "Fallback 1 Size", FT_INT, F_OFF(ttf_fallback_size[0]), 0},
        {2, "Fallback 2", FT_STR, F_OFF(ttf_fallback[1]), TE_CFG_STR_MAX},
        {2, "Fallback 2 Size", FT_INT, F_OFF(ttf_fallback_size[1]), 0},
        {2, "Fallback 3", FT_STR, F_OFF(ttf_fallback[2]), TE_CFG_STR_MAX},
        {2, "Fallback 3 Size", FT_INT, F_OFF(ttf_fallback_size[2]), 0},
        {2, "Fallback 4", FT_STR, F_OFF(ttf_fallback[3]), TE_CFG_STR_MAX},
        {2, "Fallback 4 Size", FT_INT, F_OFF(ttf_fallback_size[3]), 0},
        {2, "Fallback 5", FT_STR, F_OFF(ttf_fallback[4]), TE_CFG_STR_MAX},
        {2, "Fallback 5 Size", FT_INT, F_OFF(ttf_fallback_size[4]), 0},
        {2, "Fallback 6", FT_STR, F_OFF(ttf_fallback[5]), TE_CFG_STR_MAX},
        {2, "Fallback 6 Size", FT_INT, F_OFF(ttf_fallback_size[5]), 0},
        {2, "Fallback 7", FT_STR, F_OFF(ttf_fallback[6]), TE_CFG_STR_MAX},
        {2, "Fallback 7 Size", FT_INT, F_OFF(ttf_fallback_size[6]), 0},
        {2, "Fallback 8", FT_STR, F_OFF(ttf_fallback[7]), TE_CFG_STR_MAX},
        {2, "Fallback 8 Size", FT_INT, F_OFF(ttf_fallback_size[7]), 0},
#endif

/* Dictionary */
#ifdef HAVE_HUNSPELL
#if defined(PLATFORM_AMIGA)
        {3, "Spell Enabled", FT_BOOL, F_OFF(spell_enabled), 0},
        {3, "Dict Path", FT_STR, F_OFF(spell_dict_path), TE_CFG_STR_MAX},
        {3, "Dictionary", FT_DICTLIST, F_OFF(spell_dict_name), 0},
        {3, "Custom Dict", FT_CUSTOMDICT, F_OFF(spell_custom_dict), 0},
#else
        {2, "Spell Enabled", FT_BOOL, F_OFF(spell_enabled), 0},
        {2, "Dict Path", FT_STR, F_OFF(spell_dict_path), TE_CFG_STR_MAX},
        {2, "Dictionary", FT_DICTLIST, F_OFF(spell_dict_name), 0},
        {2, "Custom Dict", FT_CUSTOMDICT, F_OFF(spell_custom_dict), 0},
#endif
#endif
};

#define ST_FIELD_COUNT ((int)(sizeof(st_fields) / sizeof(st_fields[0])))

/* Get tinyedit base directory (platform-specific) */
static void get_tinyedit_base_dir(char *buf, size_t bufsz)
{
    char *home;

#if defined(PLATFORM_WIN32)
    home = getenv("APPDATA");

    if (home && home[0])
        snprintf(buf, bufsz, "%s\\tinyedit", home);
    else
        snprintf(buf, bufsz, "tinyedit");

#elif defined(PLATFORM_AMIGA)
    snprintf(buf, bufsz, "ENVARC:tinyedit");
#else

    home = getenv("HOME");

    if (home && home[0])
        snprintf(buf, bufsz, "%s/.tinyedit", home);
    else
        snprintf(buf, bufsz, ".tinyedit");
#endif
}

/* Format a field value into buf for display */
static void st_format_value(const TeConfig *w, const SetupField *fld, char *buf, int bufsz)
{
    const char *base = (const char *)w;

    switch (fld->type)
    {
    case FT_STR:
    {
        const char *s = (const char *)(base + fld->off);
        snprintf(buf, bufsz, "%s", s[0] ? s : "(empty)");
        break;
    }
    case FT_INT:
    {
        int v = *(const int *)(base + fld->off);
        snprintf(buf, bufsz, "%d", v);
        break;
    }
    case FT_BOOL:
    {
        int v = *(const int *)(base + fld->off);
        snprintf(buf, bufsz, "%s", v ? "yes" : "no");
        break;
    }
    case FT_COLORMAP:
    {
        int v = *(const int *)(base + fld->off);
        snprintf(buf, bufsz, "%d", v);
        break;
    }
    case FT_CHARSET:
    {
        const char *s = (const char *)(base + fld->off);
        snprintf(buf, bufsz, "%s", s[0] ? s : "(empty)");
        break;
    }
    case FT_CYCLE:
    {
        int v = *(const int *)(base + fld->off);
        const char *label = "";

        if (strcmp(fld->label, "TTF Encoding") == 0)
        {
            /* TTF encoding: 0=UTF-16 BE (BMP only), 1=UTF-8 (full Unicode) */
            if (v == 0)
                label = "UTF-16";
            else
                label = "UTF-8";
        }
        else
        {
            /* TTF antialias: 0=AUTO, 1=OFF, 2=ON */
            if (v == 0)
                label = "AUTO";
            else if (v == 1)
                label = "OFF";
            else if (v == 2)
                label = "ON";
        }

        snprintf(buf, bufsz, "%s", label);
        break;
    }
#ifdef HAVE_HUNSPELL
    case FT_DICTLIST:
    {
        const char *s = (const char *)(base + fld->off);

        snprintf(buf, bufsz, "%s", s[0] ? s : "(none)");
        break;
    }
    case FT_CUSTOMDICT:
    {
        const char *s = (const char *)(base + fld->off);

        snprintf(buf, bufsz, "%s", s[0] ? s : "(none)");
        break;
    }
#endif
    case FT_COLORPAIR:
    {
        int pair_index = (fld->off - F_OFF(color_fg)) / sizeof(int);
        int fg = *(const int *)(base + fld->off);
        int bg = *(const int *)(base + F_OFF(color_bg) + pair_index * sizeof(int));

        /* Color names for display */
        const char *fg_name = "unknown";
        const char *bg_name = "unknown";

        switch (fg)
        {
        case 0:
            fg_name = "black";
            break;
        case 1:
            fg_name = "red";
            break;
        case 2:
            fg_name = "green";
            break;
        case 3:
            fg_name = "yellow";
            break;
        case 4:
            fg_name = "blue";
            break;
        case 5:
            fg_name = "magenta";
            break;
        case 6:
            fg_name = "cyan";
            break;
        case 7:
            fg_name = "white";
            break;
        case 8:
            fg_name = "br-black";
            break;
        case 9:
            fg_name = "br-red";
            break;
        case 10:
            fg_name = "br-green";
            break;
        case 11:
            fg_name = "br-yellow";
            break;
        case 12:
            fg_name = "br-blue";
            break;
        case 13:
            fg_name = "br-magenta";
            break;
        case 14:
            fg_name = "br-cyan";
            break;
        case 15:
            fg_name = "br-white";
            break;
        }

        switch (bg)
        {
        case 0:
            bg_name = "black";
            break;
        case 1:
            bg_name = "red";
            break;
        case 2:
            bg_name = "green";
            break;
        case 3:
            bg_name = "yellow";
            break;
        case 4:
            bg_name = "blue";
            break;
        case 5:
            bg_name = "magenta";
            break;
        case 6:
            bg_name = "cyan";
            break;
        case 7:
            bg_name = "white";
            break;
        case 8:
            bg_name = "br-black";
            break;
        case 9:
            bg_name = "br-red";
            break;
        case 10:
            bg_name = "br-green";
            break;
        case 11:
            bg_name = "br-yellow";
            break;
        case 12:
            bg_name = "br-blue";
            break;
        case 13:
            bg_name = "br-magenta";
            break;
        case 14:
            bg_name = "br-cyan";
            break;
        case 15:
            bg_name = "br-white";
            break;
        }

        snprintf(buf, bufsz, "%s on %s", fg_name, bg_name);
        break;
    }
    }
}

/* Edit one field in place on the working copy */
static void st_edit_field(TeConfig *w, const SetupField *fld)
{
    char *base = (char *)w;

    switch (fld->type)
    {
    case FT_STR:
    {
        char *s = (char *)(base + fld->off);

        /* Special case: Font field allows TAB toggle between Memory and File modes */
        if (strcmp(fld->label, "Font") == 0)
        {
            char tmp[TE_CFG_STR_MAX];
            int use_file_mode = 0; /* 0 = Memory (text input), 1 = File (file picker) */
            int key;

            strncpy(tmp, s, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';

            /* Check if current value looks like a path (contains /, \, or :) */
            if (strchr(tmp, '/') || strchr(tmp, '\\') || strchr(tmp, ':'))
                use_file_mode = 1;

            /* Show mode selection popup */
            for (;;)
            {
                int y;
                int x;
                int h;
                int w;
                int i;

                ui_popup_center(9, 50, &y, &x, &h, &w);

                standend();

                ui_draw_popup_frame(y, x, h, w, fld->label);
                attron(COLOR_PAIR(COL_POPUP));

                mvaddnstr(y + 2, x + 2, "Select font source:", w - 4);

                /* Memory option */
                if (!use_file_mode)
                    attron(COLOR_PAIR(COL_POPUP_SEL));

                mvaddnstr(y + 4, x + 4, "[ Memory ]  Type font name directly", w - 8);

                if (!use_file_mode)
                    attroff(COLOR_PAIR(COL_POPUP_SEL));

                /* File option */
                if (use_file_mode)
                    attron(COLOR_PAIR(COL_POPUP_SEL));

                mvaddnstr(y + 5, x + 4, "[ File   ]  Select from disk", w - 8);

                if (use_file_mode)
                    attroff(COLOR_PAIR(COL_POPUP_SEL));

                /* Status */
                attron(COLOR_PAIR(COL_STATUS));

                for (i = 0; i < w - 4; i++)
                    mvaddch(y + h - 2, x + 2 + i, ' ');

                mvaddnstr(y + h - 2, x + 2, "Up/Dn=select  Enter=confirm  ESC=cancel", w - 4);
                attroff(COLOR_PAIR(COL_STATUS));
                attroff(COLOR_PAIR(COL_POPUP));

                refresh();
                key = wrapper_getch();

                if (key == 27) /* ESC */
                    break;

                if (key == KEY_UP || key == KEY_DOWN)
                {
                    use_file_mode = !use_file_mode;
                    continue;
                }

                if (key == '\n' || key == '\r' || key == KEY_ENTER)
                {
                    if (use_file_mode)
                    {
                        /* File mode: use file picker */
                        if (ui_files_pick(fld->label, "", tmp, sizeof(tmp)) == 0)
                        {
                            strncpy(s, tmp, TE_CFG_STR_MAX - 1);
                            s[TE_CFG_STR_MAX - 1] = '\0';
                        }
                    }
                    else
                    {
                        /* Memory mode: text input */
                        wchar_t wtmp[TE_CFG_STR_MAX];

                        mbstowcs(wtmp, tmp, TE_CFG_STR_MAX - 1);
                        wtmp[TE_CFG_STR_MAX - 1] = L'\0';

                        if (ui_popup_input_wcs(fld->label, "Font name:", wtmp, TE_CFG_STR_MAX) == 0)
                        {
                            wcstombs(s, wtmp, TE_CFG_STR_MAX - 1);
                            s[TE_CFG_STR_MAX - 1] = '\0';
                        }
                    }

                    break;
                }
            }
        }
        /* TTF Font and Fallbacks use file picker directly */
        else if ((strcmp(fld->label, "TTF Font") == 0) || strncmp(fld->label, "Fallback", 8) == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            strncpy(tmp, s, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';

            if (ui_files_pick(fld->label, "", tmp, sizeof(tmp)) == 0)
            {
                strncpy(s, tmp, TE_CFG_STR_MAX - 1);
                s[TE_CFG_STR_MAX - 1] = '\0';
            }
        }
        else if (strcmp(fld->label, "Dict Path") == 0)
        {
            /* Use directory picker for Dict Path */
            char tmp[TE_CFG_STR_MAX];

            strncpy(tmp, s, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';

            if (ui_files_pick_dir(fld->label, "", tmp, sizeof(tmp)) == 0)
            {
                strncpy(s, tmp, TE_CFG_STR_MAX - 1);
                s[TE_CFG_STR_MAX - 1] = '\0';
            }
        }
        else
        {
            wchar_t tmp[TE_CFG_STR_MAX];

            int cap = (fld->maxlen > 0 && fld->maxlen < (int)sizeof(tmp)) ? fld->maxlen : (int)sizeof(tmp);

            /* Convert char to wchar_t correctly using mbstowcs */
            mbstowcs(tmp, s, cap - 1);
            tmp[cap - 1] = L'\0';

            if (ui_popup_input_wcs(fld->label, "New value:", tmp, cap) == 0)
            {
                /* Convert wchar_t back to char correctly using wcstombs */
                wcstombs(s, tmp, cap - 1);
                s[cap - 1] = '\0';
            }
        }

        break;
    }
    case FT_INT:
    {
        int *v = (int *)(base + fld->off);
        wchar_t tmp[32];

        swprintf(tmp, sizeof(tmp) / sizeof(wchar_t), L"%d", *v);

        if (ui_popup_input_wcs(fld->label, "New value (integer):", tmp, sizeof(tmp) / sizeof(wchar_t)) == 0)
        {
            int parsed;
#ifdef PLATFORM_AMIGA
            /* wcstol is broken on AmigaOS: convert to char* first, then use strtol */
            char char_buf[32];

            wcstombs(char_buf, tmp, sizeof(char_buf));
            char_buf[31] = '\0';

            parsed = (int)strtol(char_buf, NULL, 10);
#else
            parsed = (int)wcstol(tmp, NULL, 10);
#endif
            /* Special validation for TTF_SIZE (6-96) */
            if (fld->off == F_OFF(ttf_size))
            {
                if (parsed < 6 || parsed > 96)
                    parsed = 14;
            }

            *v = parsed;
        }

        break;
    }
    case FT_BOOL:
    {
        int *v = (int *)(base + fld->off);
        *v = !*v;
        break;
    }
    case FT_COLORMAP:
    {
        int *v = (int *)(base + fld->off);
        wchar_t tmp[32];

        swprintf(tmp, sizeof(tmp) / sizeof(wchar_t), L"%d", *v);

        if (ui_popup_input_wcs(fld->label, "New value (integer):", tmp, sizeof(tmp) / sizeof(wchar_t)) == 0)
        {
            int parsed;
#ifdef PLATFORM_AMIGA
            /* wcstol is broken on AmigaOS: convert to char* first, then use strtol */
            char char_buf[32];

            wcstombs(char_buf, tmp, sizeof(char_buf));
            char_buf[31] = '\0';

            parsed = (int)strtol(char_buf, NULL, 10);
#else
            parsed = (int)wcstol(tmp, NULL, 10);
#endif
            if (parsed < 0)
                parsed = 0;

            *v = parsed;
            w->color_map_initialized = 1;
        }

        break;
    }
    case FT_CHARSET:
    {
        /* Cycle through available charsets */
        char *s = (char *)(base + fld->off);
        const char **charsets;
        int count, i, current = -1;

        charsets = charset_get_list(&count);

        if (charsets && count > 0)
        {
            /* Find current charset in list */
            for (i = 0; i < count; i++)
            {
                if (strcasecmp(s, charsets[i]) == 0)
                {
                    current = i;
                    break;
                }
            }

            /* Move to next (or first if not found/empty) */
            current++;

            if (current >= count)
                current = 0;

            strncpy(s, charsets[current], CHARSET_NAME_MAX - 1);
            s[CHARSET_NAME_MAX - 1] = '\0';
        }

        break;
    }
    case FT_CYCLE:
    {
        /* Cycle through predefined options */
        int *v = (int *)(base + fld->off);

        if (strcmp(fld->label, "TTF Encoding") == 0)
            /* TTF encoding: 0=UTF-16, 1=UTF-8 (toggle between 2 options) */
            *v = (*v == 0) ? 1 : 0;
        else
            /* TTF antialias: 0=AUTO, 1=OFF, 2=ON (cycle through 3 options) */
            *v = (*v + 1) % 3;

        break;
    }
    case FT_COLORPAIR:
    {
        int pair_index = (fld->off - F_OFF(color_fg)) / sizeof(int);
        int *fg = (int *)(base + fld->off);
        int *bg = (int *)(base + F_OFF(color_bg) + pair_index * sizeof(int));

        /* Color names and their values */
        static const char *color_names[] =
            {
                "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
                "bright black", "bright red", "bright green", "bright yellow",
                "bright blue", "bright magenta", "bright cyan", "bright white"};

        /* Select foreground color */
        int fg_result = ui_popup_list("Select Foreground", color_names, 16, *fg);

        if (fg_result < 0)
            return; /* User canceled */

        /* Select background color */
        int bg_result = ui_popup_list("Select Background", color_names, 16, *bg);

        if (bg_result < 0)
            return; /* User canceled */

        /* Apply colors only if both selections were confirmed */
        *fg = fg_result;
        *bg = bg_result;

        break;
    }
#ifdef HAVE_HUNSPELL
    case FT_DICTLIST:
    {
        char *s = (char *)(base + fld->off);
        char **dicts;
        int n_dicts;
        int i;
        int current = -1;
        int selected;

        /* Get dictionary path from config */
        char *dict_path = w->spell_dict_path;

        if (!dict_path || !dict_path[0])
        {
            mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"Error: Dict Path not set", 24);
            refresh();
            wrapper_getch();
            break;
        }

        /* List available dictionaries */
        dicts = spell_list_dictionaries(dict_path, &n_dicts);

        if (!dicts || n_dicts == 0)
        {
            mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"No dictionaries found", 20);
            refresh();
            wrapper_getch();
            break;
        }

        /* Find current dictionary in list */
        for (i = 0; i < n_dicts; i++)
        {
            if (strcmp(s, dicts[i]) == 0)
            {
                current = i;
                break;
            }
        }

        /* Show popup to select dictionary */
        selected = ui_popup_list("Select Dictionary", (const char **)dicts, n_dicts, current);

        if (selected >= 0 && selected < n_dicts)
        {
            strncpy(s, dicts[selected], TE_CFG_STR_MAX - 1);
            s[TE_CFG_STR_MAX - 1] = '\0';
        }

        spell_free_dictionaries(dicts, n_dicts);
        break;
    }
    case FT_CUSTOMDICT:
    {
        char *s = (char *)(base + fld->off);
        const char *options[] = {"Select existing", "Create new"};
        int selected;

        selected = ui_popup_list("Custom Dictionary", options, 2, -1);

        if (selected == 0)
        {
            /* Select existing */
            char dicts_dir[TE_CFG_STR_MAX];
            char parent_dir[TE_CFG_STR_MAX];
            char selected_file[TE_CFG_STR_MAX];

            /* Get custom dicts directory */
            get_tinyedit_base_dir(parent_dir, sizeof(parent_dir));

#if defined(PLATFORM_WIN32)
            snprintf(dicts_dir, sizeof(dicts_dir), "%s\\dicts", parent_dir);
#else
            snprintf(dicts_dir, sizeof(dicts_dir), "%s/dicts", parent_dir);
#endif

            /* Create parent directory if it doesn't exist */
            port_mkdir_one(parent_dir);
            port_mkdir_one(dicts_dir);

            /* Select file */
            selected_file[0] = '\0';

            if (ui_files_pick("Select custom dictionary", dicts_dir, selected_file, sizeof(selected_file)) == 0 && selected_file[0])
            {
                /* Check if selected file ends with .dic */
                size_t len = strlen(selected_file);

                if (len >= 4 && strcmp(selected_file + len - 4, ".dic") == 0)
                {
                    strncpy(s, selected_file, TE_CFG_STR_MAX - 1);
                    s[TE_CFG_STR_MAX - 1] = '\0';
                }
            }
        }
        else if (selected == 1)
        {
            /* Create new */
            char dicts_dir[TE_CFG_STR_MAX];
            char parent_dir[TE_CFG_STR_MAX];
            wchar_t dict_name[TE_CFG_STR_MAX];
            char dict_path[TE_CFG_STR_MAX];

            /* Get custom dicts directory */
            get_tinyedit_base_dir(parent_dir, sizeof(parent_dir));

#if defined(PLATFORM_WIN32)
            snprintf(dicts_dir, sizeof(dicts_dir), "%s\\dicts", parent_dir);
#else
            snprintf(dicts_dir, sizeof(dicts_dir), "%s/dicts", parent_dir);
#endif

            /* Create parent directory if it doesn't exist */
            port_mkdir_one(parent_dir);
            port_mkdir_one(dicts_dir);

            /* Ask for dictionary name */
            dict_name[0] = L'\0';

            if (ui_popup_input_wcs("Create custom dictionary", "Dictionary name (without .dic):", dict_name, TE_CFG_STR_MAX) == 0 && dict_name[0])
            {
                /* Convert wchar_t to UTF-8 for file path */
                int wcs_len = (int)wcslen(dict_name);
                char *utf8_name = wcs_to_utf8(dict_name, wcs_len);

                if (utf8_name)
                {
                    /* Build full path */
                    snprintf(dict_path, sizeof(dict_path), "%s/%s.dic", dicts_dir, utf8_name);

                    /* Create empty file using portable function */
                    if (port_file_create_empty(dict_path) == 0)
                    {
                        strncpy(s, dict_path, TE_CFG_STR_MAX - 1);
                        s[TE_CFG_STR_MAX - 1] = '\0';
                    }
                    else
                    {
                        mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"Error: Cannot create dictionary", 30);
                        refresh();
                        wrapper_getch();
                    }

                    free(utf8_name);
                }
            }
        }
        break;
    }
#endif
    }
}

/* Clear one field (for DELETE key) - only works on FT_STR and FT_INT */
static void st_clear_field(TeConfig *w, const SetupField *fld)
{
    char *base = (char *)w;

    switch (fld->type)
    {
    case FT_STR:
    {
        char *s = (char *)(base + fld->off);
        s[0] = '\0'; /* Empty string */
        break;
    }
    case FT_INT:
    {
        int *v = (int *)(base + fld->off);
        *v = 0; /* Zero as default */
        break;
    }
#ifdef HAVE_HUNSPELL
    case FT_CUSTOMDICT:
    {
        char *s = (char *)(base + fld->off);
        s[0] = '\0'; /* Empty string */
        break;
    }
#endif
    /* FT_CHARSET, FT_CYCLE, FT_COLORPAIR: ignore DELETE */
    default:
        break;
    }
}

static int st_field_on_tab(int tab, int within)
{
    int i, c = 0;

    for (i = 0; i < ST_FIELD_COUNT; i++)
    {
        if (st_fields[i].tab == tab)
        {
            if (c == within)
                return i;

            c++;
        }
    }

    return -1;
}

static int st_tab_field_count(int tab)
{
    int i, c = 0;

    for (i = 0; i < ST_FIELD_COUNT; i++)
    {
        if (st_fields[i].tab == tab)
            c++;
    }

    return c;
}

int ui_setup_run(TeConfig *cfg, const char *cfg_path)
{
    TeConfig work;
    int tab = 0;
    int sel = 0;
    int scroll_offset = 0;
    int key;
    int dirty = 0;
    int i;
    int nfields;
    int visible_fields;

    if (!cfg)
        return 0;

    work = *cfg;

    for (;;)
    {
        int row, c, tabx;
        nfields = st_tab_field_count(tab);

        erase();
        standend();

        /* Title bar */
        attron(COLOR_PAIR(COL_TITLEBAR));

        for (i = 0; i < COLS; i++)
            mvaddch(0, i, ' ');

        mvprintw(0, 1, "tinyedit Setup%s   (%s)", dirty ? " *" : "", cfg_path ? cfg_path : "?");

        attroff(COLOR_PAIR(COL_TITLEBAR));

        /* Tab row */
        tabx = 1;

        for (i = 0; i < ST_TAB_COUNT; i++)
        {
            if (i == tab)
                attron(COLOR_PAIR(COL_POPUP_SEL));
            else
                attron(COLOR_PAIR(COL_NORMAL));

            mvprintw(2, tabx, " %s ", st_tab_names[i]);
            tabx += (int)strlen(st_tab_names[i]) + 3;

            attroff(COLOR_PAIR(COL_POPUP_SEL));
            attroff(COLOR_PAIR(COL_NORMAL));
        }

        /* Fields */
        visible_fields = LINES - 6;

        if (visible_fields < 1)
            visible_fields = 1;

        row = 4;
        c = 0;

        for (i = 0; i < ST_FIELD_COUNT; i++)
        {
            char val[TE_CFG_STR_MAX + 16];
            int valw;

            if (st_fields[i].tab != tab)
                continue;

            if (c < scroll_offset || c >= scroll_offset + visible_fields)
            {
                c++;
                continue;
            }

            st_format_value(&work, &st_fields[i], val, sizeof(val));

            if (c == sel)
                attron(COLOR_PAIR(COL_POPUP_SEL));
            else
                attron(COLOR_PAIR(COL_NORMAL));

            valw = COLS - 24;

            if (valw < 1)
                valw = 1;

#ifdef PLATFORM_AMIGA
            /* Workaround for AmigaOS mvprintw bug with "0" value */
            if (strcmp(val, "0") == 0)
                mvprintw(row, 2, "%-20s : %s", st_fields[i].label, "0");
            else
                mvprintw(row, 2, "%-20s : %-.*s", st_fields[i].label, valw, val);
#else
            mvprintw(row, 2, "%-20s : %-.*s", st_fields[i].label, valw, val);
#endif

            attroff(COLOR_PAIR(COL_POPUP_SEL));
            attroff(COLOR_PAIR(COL_NORMAL));

            row++;
            c++;
        }

        if (nfields > visible_fields)
        {
            /* Show up arrow if not at top */
            if (scroll_offset > 0)
                mvprintw(LINES - 2, COLS - 3, "↑");

            /* Show down arrow if not at bottom */
            if (scroll_offset + visible_fields < nfields)
                mvprintw(LINES - 2, COLS - 2, "↓");
        }

        /* Status bar */
        attron(COLOR_PAIR(COL_STATUS));

        for (i = 0; i < COLS; i++)
            mvaddch(LINES - 1, i, ' ');

        mvprintw(LINES - 1, 1, "Up/Dn field  Left/Right tab  Enter/Spc edit  F10/S save  ESC cancel");
        move(0, 0);

        attroff(COLOR_PAIR(COL_STATUS));

        refresh();

        key = wrapper_getch();

        if (key == 27) /* ESC */
        {
            if (dirty && ui_popup_confirm("Setup", "Discard unsaved changes?") != 1)
                continue;

            return 0;
        }

        if (key == KEY_F(10) || key == 'S' || key == 's')
        {
            if (!cfg_path)
            {
                ui_popup_confirm("Setup", "No config path; cannot save.");
                continue;
            }

            if (te_cfg_save(&work, cfg_path) != 0)
            {
                ui_popup_confirm("Setup", "Save failed.");
                continue;
            }

#ifdef PLATFORM_AMIGA
            /* If TTF_FONT, TTF_SIZE, or any fallback changed, reload fonts without restarting */
            if (work.ttf_enabled)
            {
                int fi;
                int fallbacks_changed = 0;

                for (fi = 0; fi < TE_CFG_TTF_FALLBACKS; fi++)
                {
                    if (strcmp(cfg->ttf_fallback[fi], work.ttf_fallback[fi]) != 0 || cfg->ttf_fallback_size[fi] != work.ttf_fallback_size[fi])
                    {
                        fallbacks_changed = 1;
                        break;
                    }
                }

                if (strcmp(cfg->ttf_font, work.ttf_font) != 0 || cfg->ttf_size != work.ttf_size || fallbacks_changed)
                {
                    extern int amiga_reload_ttf(const char *font_path, int new_size);
#ifdef AMIGA_TTF_TE
                    extern int amiga_add_ttf_fallback(const char *path, int size);
                    extern void amiga_clear_ttf_fallbacks(void);

                    /* Push new fallback set into backend before reload to avoid stale list */
                    amiga_clear_ttf_fallbacks();

                    for (fi = 0; fi < TE_CFG_TTF_FALLBACKS; fi++)
                    {
                        if (work.ttf_fallback[fi][0])
                        {
                            int sz = work.ttf_fallback_size[fi] > 0 ? work.ttf_fallback_size[fi] : work.ttf_size;
                            amiga_add_ttf_fallback(work.ttf_fallback[fi], sz);
                        }
                    }
#endif
                    amiga_reload_ttf(work.ttf_font, work.ttf_size);
                }
            }
#endif

            *cfg = work;

            /* Reinitialize colors with new configuration */
            te_init_colors(cfg);

            return 1;
        }

        if (key == KEY_LEFT)
        {
            tab = (tab > 0) ? tab - 1 : ST_TAB_COUNT - 1;
            sel = 0;
            scroll_offset = 0;

            continue;
        }

        if (key == KEY_RIGHT || key == '\t')
        {
            tab = (tab + 1) % ST_TAB_COUNT;
            sel = 0;
            scroll_offset = 0;

            continue;
        }

        if (key == KEY_UP)
        {
            if (sel > 0)
            {
                sel--;

                if (sel < scroll_offset)
                    scroll_offset = sel;
            }

            continue;
        }

        if (key == KEY_DOWN)
        {
            if (sel < nfields - 1)
            {
                sel++;

                if (sel >= scroll_offset + visible_fields)
                    scroll_offset = sel - visible_fields + 1;
            }

            continue;
        }

        if (key == KEY_HOME || key == CTRL('B'))
        {
            sel = 0;
            scroll_offset = 0;
            continue;
        }

        if (key == KEY_END || key == CTRL('E'))
        {
            sel = nfields - 1;

            if (sel >= scroll_offset + visible_fields)
                scroll_offset = sel - visible_fields + 1;

            continue;
        }

        if (key == KEY_PPAGE || key == CTRL('U'))
        {
            sel -= visible_fields;

            if (sel < 0)
                sel = 0;

            if (sel < scroll_offset)
                scroll_offset = sel;

            continue;
        }

        if (key == KEY_NPAGE || key == CTRL('D'))
        {
            sel += visible_fields;

            if (sel >= nfields)
                sel = nfields - 1;

            if (sel >= scroll_offset + visible_fields)
                scroll_offset = sel - visible_fields + 1;

            continue;
        }

        if (key == '\n' || key == '\r' || key == KEY_ENTER || key == ' ')
        {
            int gi = st_field_on_tab(tab, sel);

            if (gi >= 0)
            {
                st_edit_field(&work, &st_fields[gi]);
                dirty = 1;
            }

            continue;
        }

        /* DELETE key: clear field (only for FT_STR and FT_INT) */
        if (key == KEY_DC)
        {
            int gi = st_field_on_tab(tab, sel);

            if (gi >= 0 && (st_fields[gi].type == FT_STR || st_fields[gi].type == FT_INT))
            {
                st_clear_field(&work, &st_fields[gi]);
                dirty = 1;
            }

            continue;
        }
    }
}
