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

/* config.h -- tinyedit configuration */
#ifndef TINYEDIT_CONFIG_H
#define TINYEDIT_CONFIG_H

#include "../core/charset.h"

#define TE_CFG_STR_MAX 256
#define TE_CFG_COLOR_MAX 32 /* COL_* are 1..9; slot 0 unused */

/* Color pairs */
#define COL_NORMAL 1
#define COL_STATUS 2
#define COL_TITLEBAR 3
#define COL_POPUP 4
#define COL_POPUP_SEL 5
#define COL_BORDER 6
#define COL_SEARCH_MATCH 7
#define COL_SPELL_CURRENT 8

typedef struct
{
    /* Default charset for reading/writing files */
    char charset[CHARSET_NAME_MAX];

    /* Undo stack depth (default 50) */
    int undo_levels;

    /* Auto-wrap column; 0 = disabled (default 75) */
    int autowrap_col;

    /* 0=soft-wrap (visual only), 1=hard-wrap (CR inserted at autowrap_col) */
    int hard_wrap;

    /* Show line numbers: 0 = disabled, 1 = enabled */
    int show_line_numbers;

    /* UI font name (Amiga: "topaz.font"; unused on Linux) */
    char font[TE_CFG_STR_MAX];

    int ttf_enabled;               /* 0=disabled, 1=enabled */
    char ttf_font[TE_CFG_STR_MAX]; /* e.g. "FONTS:_ttf/DejaVuSansMono.ttf" */
    int ttf_size;                  /* point size, default 14 */
    int ttf_antialias;             /* 0=auto, 1=off, 2=on */
    int ttf_use_utf8;              /* 0=UTF-16 BE (BMP only), 1=UTF-8 (full Unicode/emojis) */

#define TE_CFG_TTF_FALLBACKS 8
    char ttf_fallback[TE_CFG_TTF_FALLBACKS][TE_CFG_STR_MAX];
    int ttf_fallback_size[TE_CFG_TTF_FALLBACKS]; /* 0 = inherit ttf_size */

    /* Default background color for COLOR_PAIR(0) on Amiga */
    int default_bg_color;

    /* Color pairs: fg/bg indexed by COL_* (1..9); slot 0 unused */
    int color_fg[TE_CFG_COLOR_MAX];
    int color_bg[TE_CFG_COLOR_MAX];
    int color_explicit[TE_CFG_COLOR_MAX]; /* 1 if set by COLOR keyword */

    /* Cursor color override: -1 = use terminal default */
    int cursor_color;
    char cursor_color_rgb[16]; /* "#RRGGBB" form for OSC 12 */

    /* Mouse support: 0=disabled, 1=enabled */
    int mouse_enabled;

    /* Amiga color palette mapping (COLORMAP): physical pen for each logical color */
    int color_map[16];
    int color_map_initialized; /* 1 if user configured COLORMAP explicitly */

#ifdef HAVE_HUNSPELL
    /* Spell checker configuration */
    int spell_enabled;                      /* 0=disabled, 1=enabled */
    char spell_dict_path[TE_CFG_STR_MAX];   /* Directory to search for .dic files */
    char spell_dict_name[TE_CFG_STR_MAX];   /* Selected dictionary name (e.g. "en_US") */
    char spell_custom_dict[TE_CFG_STR_MAX]; /* Path to custom dictionary */

#ifdef HAVE_HYPHEN
    /* Hyphenation configuration */
    int hyph_enabled;
    char hyph_dict_path[TE_CFG_STR_MAX];
    char hyph_dict_name[TE_CFG_STR_MAX];
    int hyph_wrap_enabled; /* Use hyphenation in hard-wrap mode */
#endif

#ifdef HAVE_MYTHES
    /* Thesaurus configuration */
    int thes_enabled;
    char thes_dict_path[TE_CFG_STR_MAX];
    char thes_dict_name[TE_CFG_STR_MAX];
#endif
#endif /* HAVE_HUNSPELL */

} TeConfig;

/* Fill cfg with safe built-in defaults */
void te_cfg_defaults(TeConfig *cfg);

/* Load config from path (0=ok, -1=error; creates default file if missing) */
int te_cfg_load(TeConfig *cfg, const char *path);

/* Save config to path (0=ok, -1=error) */
int te_cfg_save(const TeConfig *cfg, const char *path);

#endif /* TINYEDIT_CONFIG_H */
