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
#include <errno.h>
#include "config.h"
#include "../core/charset.h"

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

    if (strcasecmp(s, "SPELLCURRENT") == 0)
        return COL_SPELL_CURRENT;

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
    cfg->mouse_enabled = 1; /* mouse enabled by default */

    /* Editor assists default OFF -- user opts in via setup */
    cfg->assist_smart_quotes = 0;
    cfg->assist_auto_cap = 0;
    cfg->assist_repeat_check = 0;

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

#ifdef HAVE_HUNSPELL
    /* Spell checker defaults: disabled, empty paths */
    cfg->spell_enabled = 0;

    /* Default Hunspell dictionary path (platform-specific) */
#if defined(PLATFORM_BSD)
    strncpy(cfg->spell_dict_path, "/usr/local/share/hunspell", sizeof(cfg->spell_dict_path) - 1);
#elif defined(PLATFORM_UNIX)
    strncpy(cfg->spell_dict_path, "/usr/share/hunspell", sizeof(cfg->spell_dict_path) - 1);
#elif defined(PLATFORM_WIN32)
    strncpy(cfg->spell_dict_path, "C:\\Program Files\\LibreOffice\\share\\extensions\\dict-en", sizeof(cfg->spell_dict_path) - 1);
#elif defined(PLATFORM_AMIGA)
    strncpy(cfg->spell_dict_path, "ENVARC:dictionaries", sizeof(cfg->spell_dict_path) - 1);
#else
    cfg->spell_dict_path[0] = '\0';
#endif
    cfg->spell_dict_path[sizeof(cfg->spell_dict_path) - 1] = '\0';
    cfg->spell_dict_name[0] = '\0';
    /* Custom dict path for user dictionaries (relative to ~/.tinyedit/) */
    cfg->spell_custom_dict[0] = '\0';

#ifdef HAVE_HYPHEN
    cfg->hyph_enabled = 0;

#if defined(PLATFORM_BSD)
    strncpy(cfg->hyph_dict_path, "/usr/local/share/hyphen", sizeof(cfg->hyph_dict_path) - 1);
#elif defined(PLATFORM_UNIX)
    strncpy(cfg->hyph_dict_path, "/usr/share/hyphen", sizeof(cfg->hyph_dict_path) - 1);
#elif defined(PLATFORM_WIN32)
    strncpy(cfg->hyph_dict_path, "C:\\Program Files\\LibreOffice\\share\\extensions\\dict-en", sizeof(cfg->hyph_dict_path) - 1);
#elif defined(PLATFORM_AMIGA)
    strncpy(cfg->hyph_dict_path, "ENVARC:dictionaries", sizeof(cfg->hyph_dict_path) - 1);
#else
    cfg->hyph_dict_path[0] = '\0';
#endif
    cfg->hyph_dict_path[sizeof(cfg->hyph_dict_path) - 1] = '\0';
    cfg->hyph_dict_name[0] = '\0';
    cfg->hyph_wrap_enabled = 0;
#endif

#ifdef HAVE_MYTHES
    cfg->thes_enabled = 0;

#if defined(PLATFORM_BSD)
    strncpy(cfg->thes_dict_path, "/usr/local/share/mythes", sizeof(cfg->thes_dict_path) - 1);
#elif defined(PLATFORM_UNIX)
    strncpy(cfg->thes_dict_path, "/usr/share/mythes", sizeof(cfg->thes_dict_path) - 1);
#elif defined(PLATFORM_WIN32)
    strncpy(cfg->thes_dict_path, "C:\\Program Files\\LibreOffice\\share\\extensions\\dict-en", sizeof(cfg->thes_dict_path) - 1);
#elif defined(PLATFORM_AMIGA)
    strncpy(cfg->thes_dict_path, "ENVARC:dictionaries", sizeof(cfg->thes_dict_path) - 1);
#else
    cfg->thes_dict_path[0] = '\0';
#endif
    cfg->thes_dict_path[sizeof(cfg->thes_dict_path) - 1] = '\0';
    cfg->thes_dict_name[0] = '\0';
#endif

#endif /* HAVE_HUNSPELL */

#ifdef HAVE_TRANSLATE
    /* Translator defaults: disabled, MyMemory backend, default endpoints */
    cfg->translate_enabled = 0;
    cfg->translate_backend = 0;        /* MyMemory */
    cfg->translate_endpoint[0] = '\0'; /* Use default endpoint */
    cfg->translate_api_key[0] = '\0';
    cfg->translate_email[0] = '\0';

    strncpy(cfg->translate_from_lang, "en", sizeof(cfg->translate_from_lang) - 1);
    strncpy(cfg->translate_to_lang, "es", sizeof(cfg->translate_to_lang) - 1);

    cfg->translate_from_lang[sizeof(cfg->translate_from_lang) - 1] = '\0';
    cfg->translate_to_lang[sizeof(cfg->translate_to_lang) - 1] = '\0';
    cfg->translate_timeout = 10;  /* 10 seconds */
    cfg->stardict_path[0] = '\0'; /* Empty by default */
#endif                            /* HAVE_TRANSLATE */

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

    /* COL_SPELL_CURRENT (8) */
    cfg->color_fg[COL_SPELL_CURRENT] = 7;
    cfg->color_bg[COL_SPELL_CURRENT] = 5;
}

/* Load */

int te_cfg_load(TeConfig *cfg, const char *path)
{
    FILE *f = NULL;
    char line[512];
    char word[64];
    const char *rest = NULL;

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
        else if (strcasecmp(word, "MOUSE_ENABLED") == 0)
        {
            char val[16];

            get_token(rest, val, sizeof(val));

            if (strcasecmp(val, "0") == 0 || strcasecmp(val, "OFF") == 0 || strcasecmp(val, "NO") == 0 || strcasecmp(val, "FALSE") == 0)
                cfg->mouse_enabled = 0;
            else
                cfg->mouse_enabled = 1;
        }
        else if (strcasecmp(word, "ASSIST_SMART_QUOTES") == 0)
        {
            cfg->assist_smart_quotes = parse_yesno(rest);
        }
        else if (strcasecmp(word, "ASSIST_AUTO_CAP") == 0)
        {
            cfg->assist_auto_cap = parse_yesno(rest);
        }
        else if (strcasecmp(word, "ASSIST_REPEAT_CHECK") == 0)
        {
            cfg->assist_repeat_check = parse_yesno(rest);
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
#ifdef HAVE_HUNSPELL
        else if (strcasecmp(word, "SPELL_ENABLED") == 0)
        {
            cfg->spell_enabled = parse_yesno(rest);
        }
        else if (strcasecmp(word, "SPELL_DICT_PATH") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);

            /* Only overwrite if not empty, keep default otherwise */
            if (tmp[0] != '\0')
            {
                strncpy(cfg->spell_dict_path, tmp, sizeof(cfg->spell_dict_path) - 1);
                cfg->spell_dict_path[sizeof(cfg->spell_dict_path) - 1] = '\0';
            }
        }
        else if (strcasecmp(word, "SPELL_DICT_NAME") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);
            strncpy(cfg->spell_dict_name, tmp, sizeof(cfg->spell_dict_name) - 1);

            cfg->spell_dict_name[sizeof(cfg->spell_dict_name) - 1] = '\0';
        }
        else if (strcasecmp(word, "SPELL_CUSTOM_DICT") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);
            strncpy(cfg->spell_custom_dict, tmp, sizeof(cfg->spell_custom_dict) - 1);

            cfg->spell_custom_dict[sizeof(cfg->spell_custom_dict) - 1] = '\0';
        }
#ifdef HAVE_HYPHEN
        else if (strcasecmp(word, "HYPH_ENABLED") == 0)
        {
            cfg->hyph_enabled = parse_yesno(rest);
        }
        else if (strcasecmp(word, "HYPH_DICT_PATH") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);

            if (tmp[0] != '\0')
            {
                strncpy(cfg->hyph_dict_path, tmp, sizeof(cfg->hyph_dict_path) - 1);
                cfg->hyph_dict_path[sizeof(cfg->hyph_dict_path) - 1] = '\0';
            }
        }
        else if (strcasecmp(word, "HYPH_DICT_NAME") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);
            strncpy(cfg->hyph_dict_name, tmp, sizeof(cfg->hyph_dict_name) - 1);

            cfg->hyph_dict_name[sizeof(cfg->hyph_dict_name) - 1] = '\0';
        }
        else if (strcasecmp(word, "HYPH_WRAP_ENABLED") == 0)
        {
            cfg->hyph_wrap_enabled = parse_yesno(rest);
        }
#endif
#ifdef HAVE_MYTHES
        else if (strcasecmp(word, "THES_ENABLED") == 0)
        {
            cfg->thes_enabled = parse_yesno(rest);
        }
        else if (strcasecmp(word, "THES_DICT_PATH") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);

            if (tmp[0] != '\0')
            {
                strncpy(cfg->thes_dict_path, tmp, sizeof(cfg->thes_dict_path) - 1);
                cfg->thes_dict_path[sizeof(cfg->thes_dict_path) - 1] = '\0';
            }
        }
        else if (strcasecmp(word, "THES_DICT_NAME") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);
            strncpy(cfg->thes_dict_name, tmp, sizeof(cfg->thes_dict_name) - 1);

            cfg->thes_dict_name[sizeof(cfg->thes_dict_name) - 1] = '\0';
        }
#endif
#endif /* HAVE_HUNSPELL */
#ifdef HAVE_TRANSLATE
        else if (strcasecmp(word, "TRANSLATE_ENABLED") == 0)
        {
            cfg->translate_enabled = parse_yesno(rest);
        }
        else if (strcasecmp(word, "TRANSLATE_BACKEND") == 0)
        {
            char val[16];
            get_token(rest, val, sizeof(val));

            if (strcasecmp(val, "MYMEMORY") == 0)
                cfg->translate_backend = 0;
            else if (strcasecmp(val, "LIBRETRANSLATE") == 0)
                cfg->translate_backend = 1;
            else if (strcasecmp(val, "LINGVA") == 0)
                cfg->translate_backend = 2;
            else if (strcasecmp(val, "DEEPL") == 0)
                cfg->translate_backend = 4;
            else if (strcasecmp(val, "STARDICT") == 0)
                cfg->translate_backend = 10;
            else
                cfg->translate_backend = 0;
        }
        else if (strcasecmp(word, "TRANSLATE_ENDPOINT") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);

            strncpy(cfg->translate_endpoint, tmp, sizeof(cfg->translate_endpoint) - 1);

            cfg->translate_endpoint[sizeof(cfg->translate_endpoint) - 1] = '\0';
        }
        else if (strcasecmp(word, "TRANSLATE_API_KEY") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);

            strncpy(cfg->translate_api_key, tmp, sizeof(cfg->translate_api_key) - 1);

            cfg->translate_api_key[sizeof(cfg->translate_api_key) - 1] = '\0';
        }
        else if (strcasecmp(word, "TRANSLATE_EMAIL") == 0)
        {
            char tmp[TE_CFG_STR_MAX];

            copy_rest(rest, tmp, sizeof(tmp));
            strip_quotes(tmp);

            strncpy(cfg->translate_email, tmp, sizeof(cfg->translate_email) - 1);

            cfg->translate_email[sizeof(cfg->translate_email) - 1] = '\0';
        }
        else if (strcasecmp(word, "TRANSLATE_FROM_LANG") == 0)
        {
            char tmp[16];

            get_token(rest, tmp, sizeof(tmp));

            strncpy(cfg->translate_from_lang, tmp, sizeof(cfg->translate_from_lang) - 1);

            cfg->translate_from_lang[sizeof(cfg->translate_from_lang) - 1] = '\0';
        }
        else if (strcasecmp(word, "TRANSLATE_TO_LANG") == 0)
        {
            char tmp[16];

            get_token(rest, tmp, sizeof(tmp));

            strncpy(cfg->translate_to_lang, tmp, sizeof(cfg->translate_to_lang) - 1);

            cfg->translate_to_lang[sizeof(cfg->translate_to_lang) - 1] = '\0';
        }
        else if (strcasecmp(word, "TRANSLATE_TIMEOUT") == 0)
        {
            cfg->translate_timeout = atoi(rest);

            if (cfg->translate_timeout < 1)
                cfg->translate_timeout = 1;

            if (cfg->translate_timeout > 60)
                cfg->translate_timeout = 60;
        }
        else if (strcasecmp(word, "STARDICT_PATH") == 0)
        {
            strncpy(cfg->stardict_path, rest, sizeof(cfg->stardict_path) - 1);
            cfg->stardict_path[sizeof(cfg->stardict_path) - 1] = '\0';
        }
#endif /* HAVE_TRANSLATE */
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

int te_cfg_save(const TeConfig *cfg, const char *path)
{
    FILE *in, *out;
    char tmp_path[512];
    char line[1024];
    int fi;

    /* Create temporary file */
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    out = fopen(tmp_path, "w");
    if (!out)
        return -1;

    /* Stream original file, filtering TTF_FALLBACK lines */
    in = fopen(path, "r");
    if (in)
    {
        while (fgets(line, sizeof(line), in))
        {
            char word[64];
            char *p = line;
            int wi = 0;
            int skip_line = 0;

            /* Skip leading whitespace */
            while (*p == ' ' || *p == '\t')
                p++;

            /* Copy comments and blank lines verbatim */
            if (*p == '#' || *p == ';' || *p == '\0' || *p == '\r' || *p == '\n')
            {
                fputs(line, out);
                continue;
            }

            /* Extract first word */
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && wi < (int)sizeof(word) - 1)
            {
                word[wi] = *p;
                wi++;
                p++;
            }
            word[wi] = '\0';

            /* Skip all managed keywords - they will be rewritten at end */
            if (strncasecmp(word, "TTF_FALLBACK", 12) == 0 ||
                strcasecmp(word, "FONT") == 0 ||
                strcasecmp(word, "CHARSET") == 0 ||
                strcasecmp(word, "UNDOLEVELS") == 0 ||
                strcasecmp(word, "AUTOWRAP") == 0 ||
                strcasecmp(word, "HARDWRAP") == 0 ||
                strcasecmp(word, "LINENUMBERS") == 0 ||
                strcasecmp(word, "TTF_ENABLED") == 0 ||
                strcasecmp(word, "TTF_FONT") == 0 ||
                strcasecmp(word, "TTF_SIZE") == 0 ||
                strcasecmp(word, "TTF_ANTIALIAS") == 0 ||
                strcasecmp(word, "TTF_USE_UTF8") == 0 ||
                strcasecmp(word, "DEFAULT_BG_COLOR") == 0 ||
                strcasecmp(word, "CURSORCOLOR") == 0 ||
                strcasecmp(word, "MOUSE_ENABLED") == 0 ||
                strcasecmp(word, "COLOR") == 0 ||
                strcasecmp(word, "COLORMAP") == 0
#ifdef HAVE_HUNSPELL
                || strcasecmp(word, "SPELL_ENABLED") == 0 || strcasecmp(word, "SPELL_DICT_PATH") == 0 || strcasecmp(word, "SPELL_DICT_NAME") == 0 || strcasecmp(word, "SPELL_CUSTOM_DICT") == 0
#ifdef HAVE_HYPHEN
                || strcasecmp(word, "HYPH_ENABLED") == 0 || strcasecmp(word, "HYPH_DICT_PATH") == 0 || strcasecmp(word, "HYPH_DICT_NAME") == 0 || strcasecmp(word, "HYPH_WRAP_ENABLED") == 0
#endif
#ifdef HAVE_MYTHES
                || strcasecmp(word, "THES_ENABLED") == 0 || strcasecmp(word, "THES_DICT_PATH") == 0 || strcasecmp(word, "THES_DICT_NAME") == 0
#endif
#endif
#ifdef HAVE_TRANSLATE
                || strcasecmp(word, "TRANSLATE_ENABLED") == 0 || strcasecmp(word, "TRANSLATE_BACKEND") == 0 || strcasecmp(word, "TRANSLATE_ENDPOINT") == 0 || strcasecmp(word, "TRANSLATE_API_KEY") == 0 || strcasecmp(word, "TRANSLATE_EMAIL") == 0 || strcasecmp(word, "TRANSLATE_FROM_LANG") == 0 || strcasecmp(word, "TRANSLATE_TO_LANG") == 0 || strcasecmp(word, "TRANSLATE_TIMEOUT") == 0
#endif
            )
            {
                skip_line = 1;
            }

            if (!skip_line)
                fputs(line, out);
        }
        fclose(in);
    }
    else
    {
        /* No existing file - write header */
        fprintf(out, "# tinyedit configuration\n");
        fprintf(out, "# Lines starting with # or ; are comments.\n\n");
    }

    /* Write/update other config fields */
    fprintf(out, "FONT %s\n", cfg->font);
    fprintf(out, "CHARSET %s\n", cfg->charset);
    fprintf(out, "UNDOLEVELS %d\n", cfg->undo_levels);
    fprintf(out, "AUTOWRAP %d\n", cfg->autowrap_col);
    fprintf(out, "HARDWRAP %s\n", cfg->hard_wrap ? "YES" : "NO");
    fprintf(out, "LINENUMBERS %s\n", cfg->show_line_numbers ? "YES" : "NO");

    /* TTF settings */
    fprintf(out, "TTF_ENABLED %s\n", cfg->ttf_enabled ? "YES" : "NO");
    fprintf(out, "TTF_FONT %s\n", cfg->ttf_font);
    fprintf(out, "TTF_SIZE %d\n", cfg->ttf_size);
    fprintf(out, "TTF_ANTIALIAS %s\n", cfg->ttf_antialias == 2 ? "ON" : (cfg->ttf_antialias == 1 ? "OFF" : "AUTO"));
    fprintf(out, "TTF_USE_UTF8 %s\n", cfg->ttf_use_utf8 ? "YES" : "NO");

    /* Write TTF fallback section */
    for (fi = 0; fi < TE_CFG_TTF_FALLBACKS; fi++)
    {
        if (cfg->ttf_fallback[fi][0])
        {
            fprintf(out, "TTF_FALLBACK%d   %s\n", fi + 1, cfg->ttf_fallback[fi]);

            if (cfg->ttf_fallback_size[fi] > 0)
                fprintf(out, "TTF_FALLBACK_SIZE%d  %d\n", fi + 1, cfg->ttf_fallback_size[fi]);
        }
    }

    /* Background color */
    fprintf(out, "DEFAULT_BG_COLOR %d\n", cfg->default_bg_color);

    /* Cursor color */
    if (cfg->cursor_color_rgb[0])
        fprintf(out, "CURSORCOLOR \"%s\"\n", cfg->cursor_color_rgb);
    else
        fprintf(out, "CURSORCOLOR %d\n", cfg->cursor_color);

    /* Mouse support */
    fprintf(out, "MOUSE_ENABLED %d\n", cfg->mouse_enabled);

    /* Editor assists */
    fprintf(out, "ASSIST_SMART_QUOTES %s\n", cfg->assist_smart_quotes ? "YES" : "NO");
    fprintf(out, "ASSIST_AUTO_CAP %s\n", cfg->assist_auto_cap ? "YES" : "NO");
    fprintf(out, "ASSIST_REPEAT_CHECK %s\n", cfg->assist_repeat_check ? "YES" : "NO");

    /* Color pairs */
    fprintf(out, "COLOR NORMAL %s %s\n", color_name(cfg->color_fg[COL_NORMAL]), color_name(cfg->color_bg[COL_NORMAL]));
    fprintf(out, "COLOR STATUS %s %s\n", color_name(cfg->color_fg[COL_STATUS]), color_name(cfg->color_bg[COL_STATUS]));
    fprintf(out, "COLOR TITLEBAR %s %s\n", color_name(cfg->color_fg[COL_TITLEBAR]), color_name(cfg->color_bg[COL_TITLEBAR]));
    fprintf(out, "COLOR POPUP %s %s\n", color_name(cfg->color_fg[COL_POPUP]), color_name(cfg->color_bg[COL_POPUP]));
    fprintf(out, "COLOR POPUPSEL %s %s\n", color_name(cfg->color_fg[COL_POPUP_SEL]), color_name(cfg->color_bg[COL_POPUP_SEL]));
    fprintf(out, "COLOR BORDER %s %s\n", color_name(cfg->color_fg[COL_BORDER]), color_name(cfg->color_bg[COL_BORDER]));
    fprintf(out, "COLOR SEARCH %s %s\n", color_name(cfg->color_fg[COL_SEARCH_MATCH]), color_name(cfg->color_bg[COL_SEARCH_MATCH]));
    fprintf(out, "COLOR SPELLCURRENT %s %s\n", color_name(cfg->color_fg[COL_SPELL_CURRENT]), color_name(cfg->color_bg[COL_SPELL_CURRENT]));

    /* COLORMAP for Amiga */
    if (cfg->color_map_initialized)
    {
        const char *color_names[16] = {"black", "red", "green", "yellow", "blue", "magenta", "cyan", "white", "brightblack", "brightred", "brightgreen", "brightyellow", "brightblue", "brightmagenta", "brightcyan", "brightwhite"};
        int ci;

        for (ci = 0; ci < 16; ci++)
            fprintf(out, "COLORMAP %s %d\n", color_names[ci], cfg->color_map[ci]);
    }

#ifdef HAVE_HUNSPELL
    /* Spell checker settings */
    fprintf(out, "SPELL_ENABLED %s\n", cfg->spell_enabled ? "YES" : "NO");
    fprintf(out, "SPELL_DICT_PATH %s\n", cfg->spell_dict_path);
    fprintf(out, "SPELL_DICT_NAME %s\n", cfg->spell_dict_name);
    fprintf(out, "SPELL_CUSTOM_DICT %s\n", cfg->spell_custom_dict);

#ifdef HAVE_HYPHEN
    fprintf(out, "HYPH_ENABLED %s\n", cfg->hyph_enabled ? "YES" : "NO");
    fprintf(out, "HYPH_DICT_PATH %s\n", cfg->hyph_dict_path);
    fprintf(out, "HYPH_DICT_NAME %s\n", cfg->hyph_dict_name);
    fprintf(out, "HYPH_WRAP_ENABLED %s\n", cfg->hyph_wrap_enabled ? "YES" : "NO");
#endif

#ifdef HAVE_MYTHES
    fprintf(out, "THES_ENABLED %s\n", cfg->thes_enabled ? "YES" : "NO");
    fprintf(out, "THES_DICT_PATH %s\n", cfg->thes_dict_path);
    fprintf(out, "THES_DICT_NAME %s\n", cfg->thes_dict_name);
#endif

#endif /* HAVE_HUNSPELL */

#ifdef HAVE_TRANSLATE
    const char *backend_name = "MYMEMORY";

    /* Translator settings */
    fprintf(out, "TRANSLATE_ENABLED %s\n", cfg->translate_enabled ? "YES" : "NO");

    if (cfg->translate_backend == 1)
        backend_name = "LIBRETRANSLATE";
    else if (cfg->translate_backend == 2)
        backend_name = "LINGVA";
    else if (cfg->translate_backend == 4)
        backend_name = "DEEPL";
    else if (cfg->translate_backend == 10)
        backend_name = "STARDICT";
    else
        backend_name = "MYMEMORY";

    fprintf(out, "TRANSLATE_BACKEND %s\n", backend_name);
    fprintf(out, "TRANSLATE_ENDPOINT %s\n", cfg->translate_endpoint);
    fprintf(out, "TRANSLATE_API_KEY %s\n", cfg->translate_api_key);
    fprintf(out, "TRANSLATE_EMAIL %s\n", cfg->translate_email);
    fprintf(out, "TRANSLATE_FROM_LANG %s\n", cfg->translate_from_lang);
    fprintf(out, "TRANSLATE_TO_LANG %s\n", cfg->translate_to_lang);
    fprintf(out, "TRANSLATE_TIMEOUT %d\n", cfg->translate_timeout);
    fprintf(out, "STARDICT_PATH %s\n", cfg->stardict_path);
#endif /* HAVE_TRANSLATE */

    fclose(out);

    /* Replace original file with temporary file */
    if (remove(path) != 0 && errno != ENOENT)
    {
        remove(tmp_path);
        return -1;
    }

    if (rename(tmp_path, path) != 0)
    {
        remove(tmp_path);
        return -1;
    }

    return 0;
}
