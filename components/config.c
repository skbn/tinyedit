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

/* config.c -- tinyedit configuration loader/saver */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"
#include "../core/charset.h"

/* Color pair indices (must match te.h) */
#ifndef COL_NORMAL
#define COL_NORMAL 1
#define COL_STATUS 2
#define COL_TITLEBAR 3
#define COL_POPUP 4
#define COL_POPUP_SEL 5
#define COL_BORDER 6
#define COL_SEARCH_MATCH 7
#endif

static void strip_trailing(char *s)
{
    int len = (int)strlen(s);

    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'))
        s[--len] = '\0';
}

static void strip_quotes(char *s)
{
    int len = (int)strlen(s);

    if (len >= 2 && s[0] == '"' && s[len - 1] == '"')
    {
        memmove(s, s + 1, len - 1);
        s[len - 2] = '\0';
    }
}

static void get_token(const char *src, char *dst, int dstlen)
{
    int i = 0;

    while (*src == ' ' || *src == '\t')
        src++;

    while (*src && *src != ' ' && *src != '\t' && *src != '\r' && *src != '\n' && i < dstlen - 1)
        dst[i++] = *src++;

    dst[i] = '\0';
}

static const char *skip_token(const char *p)
{
    while (*p && *p != ' ' && *p != '\t')
        p++;

    while (*p == ' ' || *p == '\t')
        p++;

    return p;
}

static void copy_rest(const char *src, char *dst, int dstlen)
{
    int i = 0;
    int in_quote = 0;

    while (*src == ' ' || *src == '\t')
        src++;

    if (*src == '"')
    {
        in_quote = 1;
        src++;
    }

    while (*src && *src != '\r' && *src != '\n' && i < dstlen - 1)
    {
        if (in_quote && *src == '"')
        {
            src++;
            break;
        }

        dst[i++] = *src++;
    }

    while (i > 0 && (dst[i - 1] == ' ' || dst[i - 1] == '\t'))
        i--;

    dst[i] = '\0';
}

static int parse_yesno(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;

    if (strncasecmp(s, "YES", 3) == 0)
        return 1;

    if (strncasecmp(s, "ON", 2) == 0)
        return 1;

    if (strncasecmp(s, "TRUE", 4) == 0)
        return 1;

    if (*s == '1')
        return 1;

    return 0;
}

/* Map color name -> ncurses index (0..15), or -1 */
static int color_by_name(const char *s, TeConfig *cfg)
{
    /* If custom mapping is enabled, use it */
    if (cfg && cfg->color_map_initialized)
    {
        if (strcasecmp(s, "black") == 0)
            return cfg->color_map[0];

        if (strcasecmp(s, "red") == 0)
            return cfg->color_map[1];

        if (strcasecmp(s, "green") == 0)
            return cfg->color_map[2];

        if (strcasecmp(s, "yellow") == 0)
            return cfg->color_map[3];

        if (strcasecmp(s, "blue") == 0)
            return cfg->color_map[4];

        if (strcasecmp(s, "magenta") == 0)
            return cfg->color_map[5];

        if (strcasecmp(s, "cyan") == 0)
            return cfg->color_map[6];

        if (strcasecmp(s, "white") == 0)
            return cfg->color_map[7];

        if (strcasecmp(s, "brightblack") == 0)
            return cfg->color_map[8];

        if (strcasecmp(s, "brightred") == 0)
            return cfg->color_map[9];

        if (strcasecmp(s, "brightgreen") == 0)
            return cfg->color_map[10];

        if (strcasecmp(s, "brightyellow") == 0)
            return cfg->color_map[11];

        if (strcasecmp(s, "brightblue") == 0)
            return cfg->color_map[12];

        if (strcasecmp(s, "brightmagenta") == 0)
            return cfg->color_map[13];

        if (strcasecmp(s, "brightcyan") == 0)
            return cfg->color_map[14];

        if (strcasecmp(s, "brightwhite") == 0)
            return cfg->color_map[15];
    }

    /* Default mapping (no COLORMAP configured) */
    if (strcasecmp(s, "black") == 0)
        return 0;

    if (strcasecmp(s, "red") == 0)
        return 1;

    if (strcasecmp(s, "green") == 0)
        return 2;

    if (strcasecmp(s, "yellow") == 0)
        return 3;

    if (strcasecmp(s, "blue") == 0)
        return 4;

    if (strcasecmp(s, "magenta") == 0)
        return 5;

    if (strcasecmp(s, "cyan") == 0)
        return 6;

    if (strcasecmp(s, "white") == 0)
        return 7;

    if (strcasecmp(s, "brightblack") == 0)
        return 8;

    if (strcasecmp(s, "brightred") == 0)
        return 9;

    if (strcasecmp(s, "brightgreen") == 0)
        return 10;

    if (strcasecmp(s, "brightyellow") == 0)
        return 11;

    if (strcasecmp(s, "brightblue") == 0)
        return 12;

    if (strcasecmp(s, "brightmagenta") == 0)
        return 13;

    if (strcasecmp(s, "brightcyan") == 0)
        return 14;

    if (strcasecmp(s, "brightwhite") == 0)
        return 15;

    return -1;
}

/* Map pair name -> TE_COL_* index (1..9), or -1 */
static int pair_by_name(const char *s)
{
    if (strcasecmp(s, "NORMAL") == 0)
        return COL_NORMAL;

    if (strcasecmp(s, "STATUS") == 0)
        return COL_STATUS;

    if (strcasecmp(s, "TITLEBAR") == 0)
        return COL_TITLEBAR;

    if (strcasecmp(s, "BORDER") == 0)
        return COL_BORDER;

    if (strcasecmp(s, "POPUP") == 0)
        return COL_POPUP;

    if (strcasecmp(s, "POPUPSEL") == 0)
        return COL_POPUP_SEL;

    if (strcasecmp(s, "SEARCHMATCH") == 0)
        return COL_SEARCH_MATCH;

    return -1;
}

void te_cfg_defaults(TeConfig *cfg)
{
    int i;

    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->charset, CHARSET_WRITE_DEFAULT, sizeof(cfg->charset) - 1);
    cfg->charset[sizeof(cfg->charset) - 1] = '\0';

    cfg->undo_levels = 50;
    cfg->autowrap_col = 75;
    cfg->hard_wrap = 0;         /* soft-wrap by default */
    cfg->show_line_numbers = 0; /* line numbers disabled by default */
    cfg->cursor_color = -1;
    cfg->default_bg_color = 0;

    /* Initialize color_map as identity mapping (pen 0=black, 1=red, etc) */
    for (i = 0; i < 16; i++)
        cfg->color_map[i] = i;

    cfg->color_map_initialized = 0;

    strncpy(cfg->font, "topaz.font", sizeof(cfg->font) - 1);
    cfg->font[sizeof(cfg->font) - 1] = '\0';

    /* TTF defaults: disabled (empty path) — bitmap font is used */
    cfg->ttf_enabled = 0;
    cfg->ttf_font[0] = '\0';
    cfg->ttf_size = 14;
    cfg->ttf_antialias = 0; /* auto */
    cfg->ttf_use_utf8 = 1;  /* UTF-8 for full Unicode/emoji support */

    /* Fallback slots empty by default */
    for (i = 0; i < TE_CFG_TTF_FALLBACKS; i++)
    {
        cfg->ttf_fallback[i][0] = '\0';
        cfg->ttf_fallback_size[i] = 0;
    }

    /* White-on-black fallback */
    for (i = 0; i < TE_CFG_COLOR_MAX; i++)
    {
        cfg->color_fg[i] = 7;
        cfg->color_bg[i] = 0;
        cfg->color_explicit[i] = 0;
    }

    /* COL_NORMAL    (1) */
    cfg->color_fg[COL_NORMAL] = 7;
    cfg->color_bg[COL_NORMAL] = 0;

    /* COL_STATUS    (2) */
    cfg->color_fg[COL_STATUS] = 0;
    cfg->color_bg[COL_STATUS] = 7;

    /* COL_TITLEBAR  (3) */
    cfg->color_fg[COL_TITLEBAR] = 0;
    cfg->color_bg[COL_TITLEBAR] = 6;

    /* COL_POPUP     (4) */
    cfg->color_fg[COL_POPUP] = 7;
    cfg->color_bg[COL_POPUP] = 4;

    /* COL_POPUP_SEL (5) */
    cfg->color_fg[COL_POPUP_SEL] = 0;
    cfg->color_bg[COL_POPUP_SEL] = 6;

    /* COL_BORDER    (6) */
    cfg->color_fg[COL_BORDER] = 6;
    cfg->color_bg[COL_BORDER] = 0;

    /* COL_SEARCH_MATCH (7) */
    cfg->color_fg[COL_SEARCH_MATCH] = 0;
    cfg->color_bg[COL_SEARCH_MATCH] = 3;
}

/* Load */

int te_cfg_load(TeConfig *cfg, const char *path)
{
    FILE *f;
    char line[512];
    char word[64];
    const char *rest;

    te_cfg_defaults(cfg);

    f = fopen(path, "r");

    if (!f)
    {
        /* No config file: write defaults and continue */
        if (te_cfg_save(cfg, path) != 0)
            return -1;

        fprintf(stderr, "Note: config not found, created default: %s\n", path);
        return 0;
    }

    while (fgets(line, sizeof(line), f))
    {
        char *p = line;
        int in_quote = 0;
        int j;

        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '#' || *p == ';' || *p == '\0' || *p == '\r' || *p == '\n')
            continue;

        /* Strip inline comments */
        for (j = 0; p[j]; j++)
        {
            if (p[j] == '"')
            {
                in_quote = !in_quote;
                continue;
            }

            if (!in_quote && (p[j] == ';' || p[j] == '#'))
            {
                p[j] = '\0';
                break;
            }
        }

        strip_trailing(p);

        get_token(p, word, sizeof(word));
        rest = skip_token(p);

        if (strcasecmp(word, "CHARSET") == 0)
        {
            char cs[CHARSET_NAME_MAX];
            get_token(rest, cs, sizeof(cs));

            if (strcasecmp(cs, "AUTO") != 0)
            {
                const char *canon = charset_resolve(cs);

                if (canon)
                {
                    strncpy(cfg->charset, canon, sizeof(cfg->charset) - 1);
                    cfg->charset[sizeof(cfg->charset) - 1] = '\0';
                }
                else
                {
                    strncpy(cfg->charset, cs, sizeof(cfg->charset) - 1);
                    cfg->charset[sizeof(cfg->charset) - 1] = '\0';
                }
            }
        }
        else if (strcasecmp(word, "UNDOLEVELS") == 0)
        {
            cfg->undo_levels = atoi(rest);

            if (cfg->undo_levels < 1)
                cfg->undo_levels = 1;

            if (cfg->undo_levels > 1000)
                cfg->undo_levels = 1000;
        }
        else if (strcasecmp(word, "AUTOWRAP") == 0)
        {
            cfg->autowrap_col = atoi(rest);

            if (cfg->autowrap_col < 0)
                cfg->autowrap_col = 0;

            if (cfg->autowrap_col > 0 && cfg->autowrap_col < 20)
                cfg->autowrap_col = 75;
        }
        else if (strcasecmp(word, "HARDWRAP") == 0)
        {
            cfg->hard_wrap = parse_yesno(rest);
        }
        else if (strcasecmp(word, "LINENUMBERS") == 0)
        {
            cfg->show_line_numbers = parse_yesno(rest);
        }
        else if (strcasecmp(word, "FONT") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);
            strncpy(cfg->font, tmp, sizeof(cfg->font) - 1);

            cfg->font[sizeof(cfg->font) - 1] = '\0';
        }
        else if (strcasecmp(word, "TTF_ENABLED") == 0)
        {
            cfg->ttf_enabled = parse_yesno(rest);
        }
        else if (strcasecmp(word, "TTF_FONT") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);
            strncpy(cfg->ttf_font, tmp, sizeof(cfg->ttf_font) - 1);

            cfg->ttf_font[sizeof(cfg->ttf_font) - 1] = '\0';
        }
        else if (strcasecmp(word, "TTF_SIZE") == 0)
        {
            char val[16];

            get_token(rest, val, sizeof(val));
            cfg->ttf_size = atoi(val);

            if (cfg->ttf_size < 6 || cfg->ttf_size > 96)
                cfg->ttf_size = 14;
        }
        else if (strcasecmp(word, "TTF_ANTIALIAS") == 0)
        {
            char val[16];

            get_token(rest, val, sizeof(val));

            if (strcasecmp(val, "ON") == 0 || strcasecmp(val, "YES") == 0)
                cfg->ttf_antialias = 2;
            else if (strcasecmp(val, "OFF") == 0 || strcasecmp(val, "NO") == 0)
                cfg->ttf_antialias = 1;
            else
                cfg->ttf_antialias = 0; /* auto */
        }
        else if (strcasecmp(word, "TTF_USE_UTF8") == 0)
        {
            char val[16];

            get_token(rest, val, sizeof(val));

            if (strcasecmp(val, "ON") == 0 || strcasecmp(val, "YES") == 0 || strcasecmp(val, "1") == 0)
                cfg->ttf_use_utf8 = 1;
            else if (strcasecmp(val, "OFF") == 0 || strcasecmp(val, "NO") == 0 || strcasecmp(val, "0") == 0)
                cfg->ttf_use_utf8 = 0;
            else
                cfg->ttf_use_utf8 = 1; /* default to UTF-8 */
        }
        else if (strncasecmp(word, "TTF_FALLBACK", 12) == 0)
        {
            const char *suffix = word + 12;
            int is_size = 0;
            int slot;

            if (strncasecmp(suffix, "_SIZE", 5) == 0)
            {
                is_size = 1;
                suffix += 5;
            }

            slot = atoi(suffix);

            if (slot >= 1 && slot <= TE_CFG_TTF_FALLBACKS)
            {
                int idx = slot - 1;

                if (is_size)
                {
                    char val[16];
                    int v;

                    get_token(rest, val, sizeof(val));
                    v = atoi(val);

                    if (v >= 6 && v <= 96)
                        cfg->ttf_fallback_size[idx] = v;
                    else
                        cfg->ttf_fallback_size[idx] = 0;
                }
                else
                {
                    char tmp[TE_CFG_STR_MAX];

                    copy_rest(rest, tmp, sizeof(tmp));
                    strip_quotes(tmp);
                    strncpy(cfg->ttf_fallback[idx], tmp, sizeof(cfg->ttf_fallback[idx]) - 1);

                    cfg->ttf_fallback[idx][sizeof(cfg->ttf_fallback[idx]) - 1] = '\0';
                }
            }
        }
        else if (strcasecmp(word, "DEFAULT_BG_COLOR") == 0)
        {
            char val[24];

            get_token(rest, val, sizeof(val));

            if (val[0] >= '0' && val[0] <= '9')
                cfg->default_bg_color = atoi(val);
            else
            {
                int c = color_by_name(val, cfg);

                if (c >= 0)
                    cfg->default_bg_color = c;
            }
        }
        else if (strcasecmp(word, "CURSORCOLOR") == 0)
        {
            char val[24];

            get_token(rest, val, sizeof(val));

            if (val[0] == '#')
            {
                strncpy(cfg->cursor_color_rgb, val, sizeof(cfg->cursor_color_rgb) - 1);

                cfg->cursor_color_rgb[sizeof(cfg->cursor_color_rgb) - 1] = '\0';
                cfg->cursor_color = -1;
            }
            else if ((val[0] >= '0' && val[0] <= '9') || val[0] == '-')
            {
                cfg->cursor_color = atoi(val);
                cfg->cursor_color_rgb[0] = '\0';
            }
            else
            {
                int c = color_by_name(val, cfg);
                if (c >= 0)
                {
                    cfg->cursor_color = c;
                    cfg->cursor_color_rgb[0] = '\0';
                }
            }
        }
        else if (strcasecmp(word, "COLORMAP") == 0)
        {
            char cname[24], penstr[8];
            int ci, pen;
            const char *q = rest;

            while (*q == ' ' || *q == '\t')
                q++;

            ci = 0;

            while (*q && *q != ' ' && *q != '\t' && ci < 23)
                cname[ci++] = *q++;

            cname[ci] = '\0';

            while (*q == ' ' || *q == '\t')
                q++;

            ci = 0;

            while (*q && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n' && ci < 7)
                penstr[ci++] = *q++;

            penstr[ci] = '\0';

            ci = color_by_name(cname, cfg);
            pen = atoi(penstr);

            if (ci >= 0 && ci < 16 && pen >= 0 && pen <= 255)
            {
                cfg->color_map[ci] = pen;
                cfg->color_map_initialized = 1;
            }
        }
        else if (strcasecmp(word, "COLOR") == 0)
        {
            char pname[32], fgname[16], bgname[16];
            const char *q = rest;
            int np = 0, nf = 0, nb = 0;
            int pi, fi, bi;

            while (*q == ' ' || *q == '\t')
                q++;

            while (*q && *q != ' ' && *q != '\t' && np < 31)
                pname[np++] = *q++;

            while (*q == ' ' || *q == '\t')
                q++;

            while (*q && *q != ' ' && *q != '\t' && nf < 15)
                fgname[nf++] = *q++;

            while (*q == ' ' || *q == '\t')
                q++;

            while (*q && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n' && nb < 15)
                bgname[nb++] = *q++;

            pname[np] = fgname[nf] = bgname[nb] = '\0';

            pi = pair_by_name(pname);
            fi = color_by_name(fgname, cfg);
            bi = color_by_name(bgname, cfg);

            if (pi >= 1 && pi < TE_CFG_COLOR_MAX && fi >= 0 && bi >= 0)
            {
                cfg->color_fg[pi] = fi;
                cfg->color_bg[pi] = bi;
            }
        }
    }

    fclose(f);

    return 0;
}

/* Save */

static const char *color_name(int c)
{
    switch (c)
    {
    case 0:
        return "black";
    case 1:
        return "red";
    case 2:
        return "green";
    case 3:
        return "yellow";
    case 4:
        return "blue";
    case 5:
        return "magenta";
    case 6:
        return "cyan";
    case 7:
        return "white";
    case 8:
        return "brightblack";
    case 9:
        return "brightred";
    case 10:
        return "brightgreen";
    case 11:
        return "brightyellow";
    case 12:
        return "brightblue";
    case 13:
        return "brightmagenta";
    case 14:
        return "brightcyan";
    case 15:
        return "brightwhite";
    default:
        return "white";
    }
}

static const char *pair_name(int idx)
{
    switch (idx)
    {
    case COL_NORMAL:
        return "NORMAL";
    case COL_STATUS:
        return "STATUS";
    case COL_TITLEBAR:
        return "TITLEBAR";
    case COL_POPUP:
        return "POPUP";
    case COL_POPUP_SEL:
        return "POPUPSEL";
    case COL_BORDER:
        return "BORDER";
    case COL_SEARCH_MATCH:
        return "SEARCHMATCH";
    default:
        return NULL;
    }
}

int te_cfg_save(const TeConfig *cfg, const char *path)
{
    FILE *f;
    int i;
    int fi;
    int wrote = 0;

    f = fopen(path, "w");

    if (!f)
        return -1;

    fprintf(f, "# tinyedit configuration\n");
    fprintf(f, "# Lines starting with # or ; are comments.\n\n");

    fprintf(f, "# Default charset for reading/writing files\n");
    fprintf(f, "CHARSET    %s\n\n", cfg->charset[0] ? cfg->charset : "UTF-8");

    fprintf(f, "# Undo stack depth (1-1000)\n");
    fprintf(f, "UNDOLEVELS %d\n\n", cfg->undo_levels);

    fprintf(f, "# Auto-wrap column (0 = disabled)\n");
    fprintf(f, "AUTOWRAP   %d\n\n", cfg->autowrap_col);

    fprintf(f, "# Hard wrap: YES = insert CR at wrap column; NO = soft-wrap (visual only)\n");
    fprintf(f, "HARDWRAP   %s\n\n", cfg->hard_wrap ? "YES" : "NO");

    fprintf(f, "# Show line numbers: YES = enabled, NO = disabled\n");
    fprintf(f, "LINENUMBERS %s\n\n", cfg->show_line_numbers ? "YES" : "NO");

    fprintf(f, "# UI font (Amiga/Windows only, e.g. topaz.font)\n");
    fprintf(f, "FONT       %s\n\n", cfg->font[0] ? cfg->font : "topaz.font");

    fprintf(f, "# TrueType font (Amiga only, via ttengine.library v6+)\n");
    fprintf(f, "# TTF_ENABLED = YES to use TTF, NO to use bitmap font\n");
    fprintf(f, "TTF_ENABLED  %s\n", cfg->ttf_enabled ? "YES" : "NO");

    fprintf(f, "# TTF_FONT path (only used if TTF_ENABLED = YES)\n");
    fprintf(f, "# Recommended: a monospace TTF (DejaVu Sans Mono, Inconsolata, JetBrains Mono)\n");

    if (cfg->ttf_font[0])
        fprintf(f, "TTF_FONT      %s\n", cfg->ttf_font);
    else
        fprintf(f, "# TTF_FONT      FONTS:_ttf/DejaVuSansMono.ttf\n");

    fprintf(f, "TTF_SIZE      %d\n", cfg->ttf_size);
    fprintf(f, "TTF_ANTIALIAS %s\n", cfg->ttf_antialias == 2 ? "ON" : cfg->ttf_antialias == 1 ? "OFF"
                                                                                              : "AUTO");
    fprintf(f, "TTF_USE_UTF8 %s\n\n", cfg->ttf_use_utf8 ? "ON" : "OFF");

    fprintf(f, "# Fallback TTF fonts.  Up to %d slots.  Codepoints missing\n",
            TE_CFG_TTF_FALLBACKS);
    fprintf(f, "# from TTF_FONT are looked up here in order.  Typical use:\n");
    fprintf(f, "#   TTF_FALLBACK1   FONTS:_ttf/NotoSansCJK-Regular.ttf\n");
    fprintf(f, "#   TTF_FALLBACK2   FONTS:_ttf/NotoColorEmoji.ttf\n");
    fprintf(f, "# Per-slot size override (default = TTF_SIZE):\n");
    fprintf(f, "#   TTF_FALLBACK_SIZE1   16\n");

    for (fi = 0; fi < TE_CFG_TTF_FALLBACKS; fi++)
    {
        if (cfg->ttf_fallback[fi][0])
        {
            fprintf(f, "TTF_FALLBACK%d   %s\n", fi + 1, cfg->ttf_fallback[fi]);

            if (cfg->ttf_fallback_size[fi] > 0)
                fprintf(f, "TTF_FALLBACK_SIZE%d  %d\n", fi + 1, cfg->ttf_fallback_size[fi]);

            wrote = 1;
        }
    }

    if (!wrote)
        fprintf(f, "# (no fallback fonts configured)\n");

    fprintf(f, "\n");

    fprintf(f, "# Default background color for COLOR_PAIR(0) (Amiga, 0-7)\n");
    fprintf(f, "DEFAULT_BG_COLOR %d\n\n", cfg->default_bg_color);

    fprintf(f, "# Cursor color: pen index (Amiga) or color name; -1 = default\n");

    if (cfg->cursor_color_rgb[0])
        fprintf(f, "CURSORCOLOR %s\n\n", cfg->cursor_color_rgb);
    else
        fprintf(f, "CURSORCOLOR %d\n\n", cfg->cursor_color);

    fprintf(f, "# COLOR -- Customize color pairs. Syntax: COLOR <pair> <fg> <bg>\n");
    fprintf(f, "#          <pair> is one of: NORMAL, STATUS, TITLEBAR, POPUP, POPUPSEL,\n");
    fprintf(f, "#          BORDER, SEARCHMATCH\n");
    fprintf(f, "#          <fg> and <bg> are color names: black, red, green, yellow,\n");
    fprintf(f, "#          blue, magenta, cyan, white (these will use your COLORMAP if defined)\n");
    fprintf(f, "# Default: built-in palette (see below for defaults)\n");
    fprintf(f, "#COLOR TAGLINE cyan black\n");
    fprintf(f, "#COLOR QUOTE1 green black\n");
    fprintf(f, "#COLOR STATUS black white\n\n");

    fprintf(f, "# Built-in default color palette (these are used if you do not override):\n");
    fprintf(f, "#   NORMAL       white on black\n");
    fprintf(f, "#   STATUS       black on white\n");
    fprintf(f, "#   TITLEBAR     black on cyan\n");
    fprintf(f, "#   POPUP        white on blue\n");
    fprintf(f, "#   POPUPSEL     black on cyan\n");
    fprintf(f, "#   BORDER       cyan on black\n");
    fprintf(f, "#   SEARCH_MATCH black on yellow\n\n");

    for (i = 1; i < TE_CFG_COLOR_MAX; i++)
    {
        const char *pn = pair_name(i);

        if (pn)
            fprintf(f, "COLOR      %-10s %-14s %s\n", pn, color_name(cfg->color_fg[i]), color_name(cfg->color_bg[i]));
    }

    /* Save COLORMAP if user configured it explicitly (Amiga palette mapping) */
    if (cfg->color_map_initialized)
    {
        int ci;

        fprintf(f, "\n# COLORMAP: physical pen numbers for each logical color (Amiga)\n");

        for (ci = 0; ci < 16; ci++)
            fprintf(f, "COLORMAP   %s %d\n", color_name(ci), cfg->color_map[ci]);
    }

    fclose(f);

    return 0;
}
