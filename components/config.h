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

/* Color pairs */
#define COL_NORMAL 1
#define COL_STATUS 2
#define COL_TITLEBAR 3
#define COL_POPUP 4
#define COL_POPUP_SEL 5
#define COL_BORDER 6
#define COL_SEARCH_MATCH 7
#define COL_SPELL_CURRENT 8
#define COL_BRACKET_MATCH 9
#define COL_CURRENT_LINE 10 /* Background for current line highlight (universal: works on any ncurses) */
#define COL_GUIDE 11        /* Dim color for column ruler, indent guides, wrap indicator */
#define COL_SYNTAX_KEYWORD 12
#define COL_SYNTAX_STRING 13
#define COL_SYNTAX_COMMENT 14
#define COL_SYNTAX_NUMBER 15
#define COL_SYNTAX_PREPROC 16
#define COL_SYNTAX_OPERATOR 17

#define TE_CFG_COLOR_MAX 64 /* COL_* are 1..17; slot 0 unused */

typedef struct
{
    /* Default charset for reading files */
    char charset_in[CHARSET_NAME_MAX];

    /* Default charset for writing files */
    char charset_out[CHARSET_NAME_MAX];

    /* Undo stack depth (default 50) */
    int undo_levels;

    /* Auto-wrap column; 0 = disabled (default 75) */
    int autowrap_col;

    /* Number of visual columns for a tab character (tab stop width) */
    int tab_width;

    /* 0=soft-wrap (visual only), 1=hard-wrap (CR inserted at autowrap_col) */
    int hard_wrap;

    /* Show line numbers: 0 = disabled, 1 = enabled */
    int show_line_numbers;

    /* Show whitespace markers = paint tabs as "→" and trailing spaces as "·" */
    int show_whitespace;

    /* Bracket matching highlight. Same-line only for now */
    int show_brackets;

    /* Syntax highlighting: 0 = disabled, 1 = enabled */
    int syntax_enabled;

    /* Highlight current line: */
    int highlight_line;

    /* Word count in status bar: 0 = off, 1 = show "Words: N" */
    int word_count;

    /* Bracket auto-close: 0 = off, 1 = typing ( [ { " ' also inserts the matching close and leaves cursor between */
    int autoclose;

    /* Smart indent on Enter: 0 = off, 1 = new line copies the leading whitespace from the previous line */
    int smart_indent;

    /* Auto-save .swp file: 0 = off, 1 = every autosave_interval seconds write an atomic .swp copy next to the original */
    int autosave;
    int autosave_interval; /* in seconds, default 30 */

    /* Column ruler: 0 = off, N > 0 = draw '|' at column N (only where text doesn't reach) */
    int ruler_col;

    /* Indent guides: 0 = off, 1 = '|' every tab_width spaces in leading whitespace */
    int indent_guides;

    /* Soft-wrap indicator: 0 = off, 1 = marker at end of continued sub-rows */
    int wrap_indicator;

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

    /* Printing: last destination and IPP endpoint remembered across sessions */
    int print_last_choice;                /* 0=local 1=IPP 2=IPPS */
    char print_ipp_host[TE_CFG_STR_MAX];  /* hostname or IP */
    char print_ipp_queue[TE_CFG_STR_MAX]; /* queue name */
    int print_ipp_port;                   /* 0 = protocol default (631) */

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
    int hyph_wrap_enabled;   /* Use hyphenation in hard-wrap mode */
    int hyph_detect_on_load; /* Detect wrap-hyphens when loading files */
#endif

#ifdef HAVE_MYTHES
    /* Thesaurus configuration */
    int thes_enabled;
    char thes_dict_path[TE_CFG_STR_MAX];
    char thes_dict_name[TE_CFG_STR_MAX];
#endif
#endif /* HAVE_HUNSPELL */

#ifdef HAVE_GRAMMAR
    /* Grammar checker configuration */
    int grammar_enabled;
    char grammar_dict_path[TE_CFG_STR_MAX]; /* Directory holding .rul files */
    char grammar_dict_name[TE_CFG_STR_MAX]; /* Base name (e.g. "en", "es")   */
#endif

#ifdef HAVE_TRANSLATE
    /* Translator configuration */
    int translate_enabled;
    int translate_backend; /* 0=MyMemory, 1=LibreTranslate, 2=Lingva, 4=DeepL, 10=StarDict */

    char translate_endpoint[TE_CFG_STR_MAX];
    char translate_api_key[TE_CFG_STR_MAX];
    char translate_email[TE_CFG_STR_MAX];
    char translate_from_lang[16];       /* ISO language code, e.g. "en" */
    char translate_to_lang[16];         /* ISO language code, e.g. "es" */
    int translate_timeout;              /* HTTP timeout in seconds */
    char stardict_path[TE_CFG_STR_MAX]; /* Directory or .ifo file for StarDict */
#endif                                  /* HAVE_TRANSLATE */

#ifdef HAVE_TTS
    /* Language set by OS, no tts_language field needed */
    int tts_enabled;
    int tts_voice;  /* 0=male, 1=female, 2=male_robot, 3=female_robot */
    int tts_rate;   /* WPM, 40..400. Default 150 */
    int tts_pitch;  /* Hz-like, 65..320. Default 110 */
    int tts_volume; /* 0..100. Default 100 */
#endif              /* HAVE_TTS */

    /* Editor assistance toggles (independent of spell support) */
    int assist_smart_quotes;
    int assist_auto_cap;
    int assist_repeat_check;

    /* Word movement style: 0=standard (alnum+underscore), 1=vim-like (non-space blocks) */
    int word_move_mode;

} TeConfig;

/* Fill cfg with safe built-in defaults */
void te_cfg_defaults(TeConfig *cfg);

/* Load config from path (0=ok, -1=error; creates default file if missing) */
int te_cfg_load(TeConfig *cfg, const char *path);

/* Save config to path (0=ok, -1=error) */
int te_cfg_save(const TeConfig *cfg, const char *path);

#endif /* TINYEDIT_CONFIG_H */
