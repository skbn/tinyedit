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
#include "../components/config.h"
#include "../core/charset.h"
#include "../core/keys.h"

/* Tabs */
#define ST_TAB_COUNT 2
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font"};

/* Field types */
typedef enum
{
    FT_STR,      /* free text */
    FT_INT,      /* integer */
    FT_BOOL,     /* yes/no toggle */
    FT_COLORMAP, /* integer 0-255, edit also marks color_map_initialized */
    FT_CHARSET   /* cycle through available charsets */
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
        {0, "Undo levels", FT_INT, F_OFF(undo_levels), 0},

/* Colour/Font */
#if defined(PLATFORM_AMIGA) || defined(PLATFORM_WIN32)
        {1, "Font", FT_STR, F_OFF(font), TE_CFG_STR_MAX},
#endif
        {1, "Pen 0 (black)", FT_COLORMAP, F_OFF(color_map) + 0 * sizeof(int), 0},
        {1, "Pen 1 (red)", FT_COLORMAP, F_OFF(color_map) + 1 * sizeof(int), 0},
        {1, "Pen 2 (green)", FT_COLORMAP, F_OFF(color_map) + 2 * sizeof(int), 0},
        {1, "Pen 3 (yellow)", FT_COLORMAP, F_OFF(color_map) + 3 * sizeof(int), 0},
        {1, "Pen 4 (blue)", FT_COLORMAP, F_OFF(color_map) + 4 * sizeof(int), 0},
        {1, "Pen 5 (magenta)", FT_COLORMAP, F_OFF(color_map) + 5 * sizeof(int), 0},
        {1, "Pen 6 (cyan)", FT_COLORMAP, F_OFF(color_map) + 6 * sizeof(int), 0},
        {1, "Pen 7 (white)", FT_COLORMAP, F_OFF(color_map) + 7 * sizeof(int), 0},
        {1, "Cursor color", FT_INT, F_OFF(cursor_color), 0},
        {1, "Default BG", FT_INT, F_OFF(default_bg_color), 0},
};

#define ST_FIELD_COUNT ((int)(sizeof(st_fields) / sizeof(st_fields[0])))

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

        break;
    }
    case FT_INT:
    {
        int *v = (int *)(base + fld->off);
        wchar_t tmp[32];

        swprintf(tmp, sizeof(tmp) / sizeof(wchar_t), L"%d", *v);

        if (ui_popup_input_wcs(fld->label, "New value (integer):", tmp, sizeof(tmp) / sizeof(wchar_t)) == 0)
            *v = (int)wcstol(tmp, NULL, 10);

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
            int parsed = (int)wcstol(tmp, NULL, 10);

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
    int key;
    int dirty = 0;
    int i;

    if (!cfg)
        return 0;

    work = *cfg;

    for (;;)
    {
        int row, c, tabx;
        int nfields = st_tab_field_count(tab);

        erase();

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
        row = 4;
        c = 0;

        for (i = 0; i < ST_FIELD_COUNT; i++)
        {
            char val[TE_CFG_STR_MAX + 16];
            int valw;

            if (st_fields[i].tab != tab)
                continue;

            st_format_value(&work, &st_fields[i], val, sizeof(val));

            if (c == sel)
                attron(COLOR_PAIR(COL_POPUP_SEL));
            else
                attron(COLOR_PAIR(COL_NORMAL));

            valw = COLS - 24;

            if (valw < 1)
                valw = 1;

            mvprintw(row, 2, "%-20s : %-.*s", st_fields[i].label, valw, val);

            attroff(COLOR_PAIR(COL_POPUP_SEL));
            attroff(COLOR_PAIR(COL_NORMAL));

            row++;
            c++;
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

            *cfg = work;
            return 1;
        }

        if (key == KEY_LEFT)
        {
            tab = (tab > 0) ? tab - 1 : ST_TAB_COUNT - 1;
            sel = 0;

            continue;
        }

        if (key == KEY_RIGHT || key == '\t')
        {
            tab = (tab + 1) % ST_TAB_COUNT;
            sel = 0;

            continue;
        }

        if (key == KEY_UP)
        {
            if (sel > 0)
                sel--;

            continue;
        }

        if (key == KEY_DOWN)
        {
            if (sel < nfields - 1)
                sel++;

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
    }
}
