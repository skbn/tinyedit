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
#include "ui_view.h"
#include "core/portable.h"
#include "../components/config.h"
#include "../core/charset.h"
#include "../core/keys.h"
#include "../core/utf8.h"

#ifdef HAVE_HUNSPELL
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/spell.h"
#else
#include "../spell/spell.h"
#endif
#include "ui_spell.h"

#ifdef HAVE_HYPHEN
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/hyph.h"
#else
#include "../hyph_wrap/hyph_wrap.h"
#endif
#include "ui_hyph.h"
#endif

#ifdef HAVE_MYTHES
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/thes.h"
#else
#include "../thes/thes.h"
#endif
#include "ui_thes.h"
#endif
#endif /* HAVE_HUNSPELL */

#ifdef HAVE_GRAMMAR
#include "../grammar/grammar.h"
#include "ui_grammar.h"
#endif /* HAVE_GRAMMAR */

#ifdef HAVE_TRANSLATE
#include "ui_translate.h"
#endif

#ifdef HAVE_TTS
#include "ui_tts.h"
#endif

#ifdef PLATFORM_WIN32
#include <windows.h>
#endif

/* Return TTF font picker start directory */
static void setup_font_start_dir(char *out, int out_sz)
{
#ifdef PLATFORM_AMIGA
    strncpy(out, "PROGDIR:", out_sz - 1);
    out[out_sz - 1] = '\0';
#elif defined(PLATFORM_WIN32)
    wchar_t exe_path[MAX_PATH];
    char *u = NULL;
    wchar_t *last_slash = NULL;

    if (GetModuleFileNameW(NULL, exe_path, sizeof(exe_path) / sizeof(wchar_t)) == 0)
    {
        strncpy(out, ".", out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }

    last_slash = wcsrchr(exe_path, L'\\');

    if (!last_slash)
        last_slash = wcsrchr(exe_path, L'/');

    if (last_slash)
        *last_slash = L'\0';

    u = pf_utf16_to_utf8(exe_path);

    if (u)
    {
        snprintf(out, out_sz, "%s", u);
        free(u);
    }
    else
    {
        strncpy(out, ".", out_sz - 1);
        out[out_sz - 1] = '\0';
    }
#else
    strncpy(out, ".", out_sz - 1);
    out[out_sz - 1] = '\0';
#endif
}

#define HAVE_TTF_TAB 1

#if defined(HAVE_HUNSPELL) || defined(HAVE_GRAMMAR)
#define HAVE_SPELL_TAB 1
#endif

#ifdef HAVE_TTF_TAB
#ifdef HAVE_SPELL_TAB
#ifdef HAVE_TRANSLATE
#define ST_TAB_COUNT 5
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font", "TTF", "Spell", "X-late"};
#else
#define ST_TAB_COUNT 4
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font", "TTF", "Spell"};
#endif
#else /* !HAVE_SPELL_TAB */
#ifdef HAVE_TRANSLATE
#define ST_TAB_COUNT 4
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font", "TTF", "X-late"};
#else
#define ST_TAB_COUNT 3
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font", "TTF"};
#endif
#endif /* HAVE_SPELL_TAB */
#else  /* !HAVE_TTF_TAB */
#ifdef HAVE_SPELL_TAB
#ifdef HAVE_TRANSLATE
#define ST_TAB_COUNT 4
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font", "Spell", "X-late"};
#else
#define ST_TAB_COUNT 3
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font", "Spell"};
#endif
#else /* !HAVE_SPELL_TAB */
#ifdef HAVE_TRANSLATE
#define ST_TAB_COUNT 3
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font", "X-late"};
#else
#define ST_TAB_COUNT 2
static const char *st_tab_names[ST_TAB_COUNT] = {"Editor", "Colour/Font"};
#endif
#endif /* HAVE_SPELL_TAB */
#endif /* HAVE_TTF_TAB */

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
    FT_DICTLIST,       /* cycle through available Hunspell dictionaries */
    FT_CUSTOMDICT,     /* select or create custom dictionary */
    FT_CUSTOMDICT_EDIT /* edit custom dictionary in new tab */
#ifdef HAVE_HYPHEN
    ,
    FT_HYPHLIST /* cycle through available Hyphen dictionaries */
#endif
#ifdef HAVE_MYTHES
    ,
    FT_THESLIST /* cycle through available MyThes dictionaries */
#endif
#endif /* HAVE_HUNSPELL */
#ifdef HAVE_GRAMMAR
    ,
    FT_GRAMMLIST /* cycle through available grammar rule packs */
#endif
#ifdef HAVE_TRANSLATE
    ,
    FT_TRANSLATE_BACKEND /* cycle through MyMemory/LibreTranslate/Lingva */
#endif                   /* HAVE_TRANSLATE */

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
        /* Display */
        {0, "Line numbers", FT_BOOL, F_OFF(show_line_numbers), 0},
        {0, "Show whitespace", FT_BOOL, F_OFF(show_whitespace), 0},
        {0, "Highlight line", FT_BOOL, F_OFF(highlight_line), 0},
        {0, "Word count", FT_BOOL, F_OFF(word_count), 0},
        {0, "Hard wrap", FT_BOOL, F_OFF(hard_wrap), 0},
        {0, "Column ruler (0=off)", FT_INT, F_OFF(ruler_col), 0},
        {0, "Indent guides", FT_BOOL, F_OFF(indent_guides), 0},
        {0, "Wrap indicator", FT_BOOL, F_OFF(wrap_indicator), 0},

        /* Editing */
        {0, "Tab width", FT_INT, F_OFF(tab_width), 0},
        {0, "Auto-wrap col", FT_INT, F_OFF(autowrap_col), 0},
        {0, "Smart indent", FT_BOOL, F_OFF(smart_indent), 0},
        {0, "Auto-close brackets", FT_BOOL, F_OFF(autoclose), 0},
        {0, "Match brackets", FT_BOOL, F_OFF(show_brackets), 0},
        {0, "Syntax enabled", FT_BOOL, F_OFF(syntax_enabled), 0},
        {0, "Word move mode", FT_CYCLE, F_OFF(word_move_mode), 0},

        /* Text assists */
        {0, "Smart quotes", FT_BOOL, F_OFF(assist_smart_quotes), 0},
        {0, "Auto-cap", FT_BOOL, F_OFF(assist_auto_cap), 0},
        {0, "Repeated words", FT_BOOL, F_OFF(assist_repeat_check), 0},

        /* Persistence */
        {0, "Auto-save", FT_BOOL, F_OFF(autosave), 0},
        {0, "Auto-save interval (s)", FT_INT, F_OFF(autosave_interval), 0},

        /* System */
        {0, "Terminal Mouse", FT_BOOL, F_OFF(mouse_enabled), 0},

#ifdef HAVE_TTS
        {0, "TTS Enabled", FT_BOOL, F_OFF(tts_enabled), 0},
#endif
        {0, "Charset in", FT_CHARSET, F_OFF(charset_in), 0},
        {0, "Charset out", FT_CHARSET, F_OFF(charset_out), 0},
        {0, "Undo levels", FT_INT, F_OFF(undo_levels), 0},

/* Colour/Font */
#if defined(PLATFORM_AMIGA) || defined(PLATFORM_WIN32)
        {1, "Font", FT_STR, F_OFF(font), TE_CFG_STR_MAX},
#endif
#ifdef HAVE_TTF_TAB
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
        {1, "Bracket Match", FT_COLORPAIR, F_OFF(color_fg) + COL_BRACKET_MATCH * sizeof(int), 0},
        {1, "Current Line", FT_COLORPAIR, F_OFF(color_fg) + COL_CURRENT_LINE * sizeof(int), 0},
        {1, "Guides", FT_COLORPAIR, F_OFF(color_fg) + COL_GUIDE * sizeof(int), 0},
        {1, "Syntax Keyword", FT_COLORPAIR, F_OFF(color_fg) + COL_SYNTAX_KEYWORD * sizeof(int), 0},
        {1, "Syntax String", FT_COLORPAIR, F_OFF(color_fg) + COL_SYNTAX_STRING * sizeof(int), 0},
        {1, "Syntax Comment", FT_COLORPAIR, F_OFF(color_fg) + COL_SYNTAX_COMMENT * sizeof(int), 0},
        {1, "Syntax Number", FT_COLORPAIR, F_OFF(color_fg) + COL_SYNTAX_NUMBER * sizeof(int), 0},
        {1, "Syntax Preproc", FT_COLORPAIR, F_OFF(color_fg) + COL_SYNTAX_PREPROC * sizeof(int), 0},
        {1, "Syntax Operator", FT_COLORPAIR, F_OFF(color_fg) + COL_SYNTAX_OPERATOR * sizeof(int), 0},
        {1, "Cursor color", FT_INT, F_OFF(cursor_color), 0},

#if defined(PLATFORM_AMIGA) || defined(PLATFORM_WIN32)
        {1, "Default BG", FT_INT, F_OFF(default_bg_color), 0},
#endif

/* TTF Fallbacks: always on the TTF tab when it exists */
#ifdef HAVE_TTF_TAB
#define TAB_TTF_FB 2
#endif

#ifdef HAVE_TTF_TAB
        {TAB_TTF_FB, "Fallback 1", FT_STR, F_OFF(ttf_fallback[0]), TE_CFG_STR_MAX},
        {TAB_TTF_FB, "Fallback 1 Size", FT_INT, F_OFF(ttf_fallback_size[0]), 0},
        {TAB_TTF_FB, "Fallback 2", FT_STR, F_OFF(ttf_fallback[1]), TE_CFG_STR_MAX},
        {TAB_TTF_FB, "Fallback 2 Size", FT_INT, F_OFF(ttf_fallback_size[1]), 0},
        {TAB_TTF_FB, "Fallback 3", FT_STR, F_OFF(ttf_fallback[2]), TE_CFG_STR_MAX},
        {TAB_TTF_FB, "Fallback 3 Size", FT_INT, F_OFF(ttf_fallback_size[2]), 0},
        {TAB_TTF_FB, "Fallback 4", FT_STR, F_OFF(ttf_fallback[3]), TE_CFG_STR_MAX},
        {TAB_TTF_FB, "Fallback 4 Size", FT_INT, F_OFF(ttf_fallback_size[3]), 0},
        {TAB_TTF_FB, "Fallback 5", FT_STR, F_OFF(ttf_fallback[4]), TE_CFG_STR_MAX},
        {TAB_TTF_FB, "Fallback 5 Size", FT_INT, F_OFF(ttf_fallback_size[4]), 0},
        {TAB_TTF_FB, "Fallback 6", FT_STR, F_OFF(ttf_fallback[5]), TE_CFG_STR_MAX},
        {TAB_TTF_FB, "Fallback 6 Size", FT_INT, F_OFF(ttf_fallback_size[5]), 0},
        {TAB_TTF_FB, "Fallback 7", FT_STR, F_OFF(ttf_fallback[6]), TE_CFG_STR_MAX},
        {TAB_TTF_FB, "Fallback 7 Size", FT_INT, F_OFF(ttf_fallback_size[6]), 0},
        {TAB_TTF_FB, "Fallback 8", FT_STR, F_OFF(ttf_fallback[7]), TE_CFG_STR_MAX},
        {TAB_TTF_FB, "Fallback 8 Size", FT_INT, F_OFF(ttf_fallback_size[7]), 0},
#endif

/* Dictionary tab number: slot 3 when TTF tab exists, slot 2 otherwise */
#ifdef HAVE_TTF_TAB
#define TAB_DICT 3
#else
#define TAB_DICT 2
#endif

/* Translate tab number */
#ifdef HAVE_TTF_TAB
#ifdef HAVE_SPELL_TAB
#define TAB_TRANSLATE 4
#else
#define TAB_TRANSLATE 3
#endif
#else
#ifdef HAVE_SPELL_TAB
#define TAB_TRANSLATE 3
#else
#define TAB_TRANSLATE 2
#endif
#endif

/* Dictionary */
#ifdef HAVE_HUNSPELL
        {TAB_DICT, "Spell Enabled", FT_BOOL, F_OFF(spell_enabled), 0},
        {TAB_DICT, "Dict Path", FT_STR, F_OFF(spell_dict_path), TE_CFG_STR_MAX},
        {TAB_DICT, "Dictionary", FT_DICTLIST, F_OFF(spell_dict_name), 0},
        {TAB_DICT, "Custom Dict", FT_CUSTOMDICT, F_OFF(spell_custom_dict), 0},
        {TAB_DICT, "Edit Custom Dict", FT_CUSTOMDICT_EDIT, F_OFF(spell_custom_dict), 0},
#ifdef HAVE_HYPHEN
        {TAB_DICT, "Hyphen Enabled", FT_BOOL, F_OFF(hyph_enabled), 0},
        {TAB_DICT, "Hyphen Dict Path", FT_STR, F_OFF(hyph_dict_path), TE_CFG_STR_MAX},
        {TAB_DICT, "Hyphen Dictionary", FT_HYPHLIST, F_OFF(hyph_dict_name), 0},
        {TAB_DICT, "Hyphen Wrap", FT_BOOL, F_OFF(hyph_wrap_enabled), 0},
        {TAB_DICT, "Hyphen Detect on Load", FT_BOOL, F_OFF(hyph_detect_on_load), 0},
#endif /* HAVE_HYPHEN */
#ifdef HAVE_MYTHES
        {TAB_DICT, "Thesaurus Enabled", FT_BOOL, F_OFF(thes_enabled), 0},
        {TAB_DICT, "Thesaurus Path", FT_STR, F_OFF(thes_dict_path), TE_CFG_STR_MAX},
        {TAB_DICT, "Thesaurus Dict", FT_THESLIST, F_OFF(thes_dict_name), 0},
#endif /* HAVE_MYTHES */
#endif /* HAVE_HUNSPELL */

#ifdef HAVE_GRAMMAR
        {TAB_DICT, "Grammar Enabled", FT_BOOL, F_OFF(grammar_enabled), 0},
        {TAB_DICT, "Grammar Path", FT_STR, F_OFF(grammar_dict_path), TE_CFG_STR_MAX},
        {TAB_DICT, "Grammar Dictionary", FT_GRAMMLIST, F_OFF(grammar_dict_name), 0},
#endif /* HAVE_GRAMMAR */

/* Translate */
#ifdef HAVE_TRANSLATE
        {TAB_TRANSLATE, "Translate Enabled", FT_BOOL, F_OFF(translate_enabled), 0},
        {TAB_TRANSLATE, "Backend", FT_TRANSLATE_BACKEND, F_OFF(translate_backend), 0},
        {TAB_TRANSLATE, "Endpoint", FT_STR, F_OFF(translate_endpoint), TE_CFG_STR_MAX},
        {TAB_TRANSLATE, "API Key", FT_STR, F_OFF(translate_api_key), TE_CFG_STR_MAX},
        {TAB_TRANSLATE, "Email", FT_STR, F_OFF(translate_email), TE_CFG_STR_MAX},
        {TAB_TRANSLATE, "From Lang", FT_STR, F_OFF(translate_from_lang), 16},
        {TAB_TRANSLATE, "To Lang", FT_STR, F_OFF(translate_to_lang), 16},
        {TAB_TRANSLATE, "Timeout (sec)", FT_INT, F_OFF(translate_timeout), 0},
        {TAB_TRANSLATE, "StarDict Path", FT_STR, F_OFF(stardict_path), TE_CFG_STR_MAX},
#endif /* HAVE_TRANSLATE */

};

#define ST_FIELD_COUNT ((int)(sizeof(st_fields) / sizeof(st_fields[0])))

#ifdef HAVE_HUNSPELL
/* Get tinyedit base directory (platform-specific) */
static void get_tinyedit_base_dir(char *buf, size_t bufsz)
{
    port_get_config_dir(buf, bufsz);
}
#endif

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
        else if (strcmp(fld->label, "Word move mode") == 0)
        {
            /* 0=standard, 1=vim-like (non-space blocks) */
            if (v == 0)
                label = "Standard";
            else
                label = "Vim-like";
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
    case FT_CUSTOMDICT_EDIT:
    {
        const char *s = (const char *)(base + fld->off);

        snprintf(buf, bufsz, "%s", s[0] ? "[Edit]" : "(none)");
        break;
    }
#ifdef HAVE_HYPHEN
    case FT_HYPHLIST:
    {
        const char *s = (const char *)(base + fld->off);

        snprintf(buf, bufsz, "%s", s[0] ? s : "(none)");
        break;
    }
#endif
#ifdef HAVE_MYTHES
    case FT_THESLIST:
    {
        const char *s = (const char *)(base + fld->off);

        snprintf(buf, bufsz, "%s", s[0] ? s : "(none)");
        break;
    }
#endif /* HAVE_MYTHES */
#endif /* HAVE_HUNSPELL */
#ifdef HAVE_GRAMMAR
    case FT_GRAMMLIST:
    {
        const char *s = (const char *)(base + fld->off);

        snprintf(buf, bufsz, "%s", s[0] ? s : "(none)");
        break;
    }
#endif /* HAVE_GRAMMAR */
#ifdef HAVE_TRANSLATE
    case FT_TRANSLATE_BACKEND:
    {
        int v = *(const int *)(base + fld->off);
        const char *label = "";

        if (v == 0)
            label = "MyMemory";
        else if (v == 1)
            label = "LibreTranslate";
        else if (v == 2)
            label = "Lingva";
        else if (v == 4)
            label = "DeepL";
        else if (v == 10)
            label = "StarDict";
        else
            label = "MyMemory";

        snprintf(buf, bufsz, "%s", label);
        break;
    }
#endif /* HAVE_TRANSLATE */
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
static void st_edit_field(TeApp *app, TeConfig *w, const SetupField *fld)
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
#ifdef PLATFORM_WIN32
                            char family[256];
                            char *ttf_font = (char *)(base + F_OFF(ttf_font));
                            int *ttf_enabled = (int *)(base + F_OFF(ttf_enabled));

                            /* Extract font family and move path to TTF slot */
                            if (win32_get_font_family_name(tmp, family, sizeof(family)) == 0)
                            {
                                strncpy(s, family, TE_CFG_STR_MAX - 1);

                                s[TE_CFG_STR_MAX - 1] = '\0';
                                strncpy(ttf_font, tmp, TE_CFG_STR_MAX - 1);
                                ttf_font[TE_CFG_STR_MAX - 1] = '\0';

                                *ttf_enabled = 1;
                            }
                            else
                            {
                                strncpy(s, tmp, TE_CFG_STR_MAX - 1);
                                s[TE_CFG_STR_MAX - 1] = '\0';
                            }

#else
                            {
                                strncpy(s, tmp, TE_CFG_STR_MAX - 1);
                                s[TE_CFG_STR_MAX - 1] = '\0';
                            }
#endif
                        }
                    }
                    else
                    {
                        /* Memory mode: text input */
                        wchar_t wtmp[TE_CFG_STR_MAX];

                        mbstowcs(wtmp, tmp, TE_CFG_STR_MAX - 1);
                        wtmp[TE_CFG_STR_MAX - 1] = L'\0';

#ifdef PLATFORM_WIN32
                        win32_drain_messages();
#endif

                        if (ui_popup_input_wcs(fld->label, "Font name:", wtmp, TE_CFG_STR_MAX) == 0)
                        {
                            wcstombs(s, wtmp, TE_CFG_STR_MAX - 1);
                            s[TE_CFG_STR_MAX - 1] = '\0';

#ifdef PLATFORM_WIN32
                            *(int *)(base + F_OFF(ttf_enabled)) = 0;
#endif
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
            char start_dir[TE_CFG_STR_MAX];

            strncpy(tmp, s, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';

            setup_font_start_dir(start_dir, sizeof(start_dir));

            if (ui_files_pick(fld->label, start_dir, tmp, sizeof(tmp)) == 0)
            {
                strncpy(s, tmp, TE_CFG_STR_MAX - 1);
                s[TE_CFG_STR_MAX - 1] = '\0';
            }
        }
        else if (strcmp(fld->label, "Dict Path") == 0 || strcmp(fld->label, "Hyphen Dict Path") == 0 || strcmp(fld->label, "Thesaurus Path") == 0 || strcmp(fld->label, "Grammar Path") == 0 || strcmp(fld->label, "StarDict Path") == 0)
        {
            /* Use directory picker for Dict Path */
            char tmp[TE_CFG_STR_MAX];

            strncpy(tmp, s, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';

            if (ui_files_pick_dir(fld->label, tmp[0] ? tmp : "", tmp, sizeof(tmp)) == 0)
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
        else if (strcmp(fld->label, "Word move mode") == 0)
            /* Word move mode: 0=Standard, 1=Vim-like (toggle between 2 options) */
            *v = (*v == 0) ? 1 : 0;
        else
            /* TTF antialias: 0=AUTO, 1=OFF, 2=ON (cycle through 3 options) */
            *v = (*v + 1) % 3;

        break;
    }
#ifdef HAVE_TRANSLATE
    case FT_TRANSLATE_BACKEND:
    {
        /* Cycle through MyMemory, LibreTranslate, Lingva, DeepL, StarDict */
        int *v = (int *)(base + fld->off);
        const int backends[] = {0, 1, 2, 4, 10}; /* MyMemory, LibreTranslate, Lingva, DeepL, StarDict */
        int n_backends = 5;
        int i;

        for (i = 0; i < n_backends; i++)
        {
            if (backends[i] == *v)
            {
                *v = backends[(i + 1) % n_backends];
                break;
            }
        }

        if (i == n_backends)
            *v = backends[0]; /* Default to MyMemory */

        break;
    }
#endif /* HAVE_TRANSLATE */
    case FT_COLORPAIR:
    {
        int pair_index = (fld->off - F_OFF(color_fg)) / sizeof(int);
        int *fg = (int *)(base + fld->off);
        int *bg = (int *)(base + F_OFF(color_bg) + pair_index * sizeof(int));
        int fg_result;
        int bg_result;

        /* Color names and their values */
        static const char *color_names[] =
            {
                "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
                "bright black", "bright red", "bright green", "bright yellow",
                "bright blue", "bright magenta", "bright cyan", "bright white"};

        /* Select foreground color */
        fg_result = ui_popup_list("Select Foreground", color_names, 16, *fg);

        if (fg_result < 0)
            return; /* User canceled */

        /* Select background color */
        bg_result = ui_popup_list("Select Background", color_names, 16, *bg);

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
            ui_popup_confirm("No dictionaries", "No Hunspell dictionaries found");
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
#ifdef HAVE_HYPHEN
    case FT_HYPHLIST:
    {
        char *s = (char *)(base + fld->off);
        char **dicts;
        int n_dicts;
        int i;
        int current = -1;
        int selected;

        /* Get hyphen path from config */
        char *hyph_path = w->hyph_dict_path;

        if (!hyph_path || !hyph_path[0])
        {
            mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"Error: Hyphen Path not set", 27);
            refresh();
            wrapper_getch();
            break;
        }

        /* List available hyphen dictionaries */
        dicts = hyph_list_dictionaries(hyph_path, &n_dicts);

        if (!dicts || n_dicts == 0)
        {
            ui_popup_confirm("No dictionaries", "No hyphen dictionaries found");
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
        selected = ui_popup_list("Select Hyphen", (const char **)dicts, n_dicts, current);

        if (selected >= 0 && selected < n_dicts)
        {
            strncpy(s, dicts[selected], TE_CFG_STR_MAX - 1);
            s[TE_CFG_STR_MAX - 1] = '\0';
        }

        hyph_free_dictionaries(dicts, n_dicts);
        break;
    }
#endif /* HAVE_HYPHEN */
#ifdef HAVE_MYTHES
    case FT_THESLIST:
    {
        char *s = (char *)(base + fld->off);
        char **dicts = NULL;
        int n_dicts;
        int i;
        int current = -1;
        int selected;

        /* Get thesaurus path from config */
        char *thes_path = w->thes_dict_path;

        if (!thes_path || !thes_path[0])
        {
            mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"Error: Thesaurus Path not set", 28);
            refresh();
            wrapper_getch();
            break;
        }

        /* List available thesaurus dictionaries */
        dicts = thes_list_dictionaries(thes_path, &n_dicts);

        if (!dicts || n_dicts == 0)
        {
            ui_popup_confirm("No dictionaries", "No thesaurus dictionaries found");
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
        selected = ui_popup_list("Select Thesaurus", (const char **)dicts, n_dicts, current);

        if (selected >= 0 && selected < n_dicts)
        {
            strncpy(s, dicts[selected], TE_CFG_STR_MAX - 1);
            s[TE_CFG_STR_MAX - 1] = '\0';
        }

        thes_free_dictionaries(dicts, n_dicts);
        break;
    }
#endif /* HAVE_MYTHES */

#ifdef HAVE_GRAMMAR
    case FT_GRAMMLIST:
    {
        char *s = (char *)(base + fld->off);
        char **langs = NULL;
        int n_langs;
        int i;
        int current = -1;
        int selected;
        char *gramm_path = w->grammar_dict_path;

        if (!gramm_path || !gramm_path[0])
        {
            mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"Error: Grammar Path not set", 27);
            refresh();
            wrapper_getch();
            break;
        }

        langs = gc_list_langs(gramm_path, &n_langs);

        if (!langs || n_langs == 0)
        {
            mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"No grammar rule packs found", 27);
            refresh();
            wrapper_getch();
            break;
        }

        for (i = 0; i < n_langs; i++)
        {
            if (strcmp(s, langs[i]) == 0)
            {
                current = i;
                break;
            }
        }

        selected = ui_popup_list("Select Grammar", (const char **)langs, n_langs, current);

        if (selected >= 0 && selected < n_langs)
        {
            strncpy(s, langs[selected], TE_CFG_STR_MAX - 1);
            s[TE_CFG_STR_MAX - 1] = '\0';
        }

        gc_free_langs(langs, n_langs);
        break;
    }
#endif /* HAVE_GRAMMAR */

    case FT_CUSTOMDICT_EDIT:
    {
        char *s = (char *)(base + fld->off);
        FILE *fp = NULL;
        long size;
        char *buf = NULL;
        size_t r;
        char *content = NULL;
        TeTab *tab = NULL;

        if (s[0] != '\0')
        {
            /* Read file */
            fp = fopen(s, "rb");

            if (!fp)
            {
                mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"Error: Cannot open dictionary", 30);
                refresh();
                wrapper_getch();
                break;
            }

            fseek(fp, 0, SEEK_END);
            size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            if (size < 0)
            {
                fclose(fp);
                mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"Error: Cannot read dictionary", 30);
                refresh();
                wrapper_getch();
                break;
            }

            buf = (char *)malloc((size_t)size + 1);

            if (!buf)
            {
                fclose(fp);
                mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"Error: Memory error", 30);
                refresh();
                wrapper_getch();
                break;
            }

            r = fread(buf, 1, (size_t)size, fp);
            fclose(fp);

            buf[r] = '\0';

            content = buf;

            tab = te_tab_new_with_content(s, content);

            if (tab)
            {
                tab->show_line_numbers = w->show_line_numbers;
                ed_set_word_move_mode(tab->editor, w->word_move_mode);

                te_app_add_tab(app, tab);
                te_app_switch_tab(app, app->tab_count - 1);
            }
            else
            {
                mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"Error: Cannot open dictionary", 30);
                refresh();
                wrapper_getch();
            }

            free(buf);
        }
        else
        {
            mvaddnwstr(LINES / 2, (COLS - 40) / 2, L"Error: No custom dict selected", 30);
            refresh();
            wrapper_getch();
        }

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
            char dicts_dir[TE_CFG_STR_MAX + 16];
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
            char dicts_dir[TE_CFG_STR_MAX + 16];
            char parent_dir[TE_CFG_STR_MAX];
            wchar_t dict_name[TE_CFG_STR_MAX];
            char dict_path[TE_CFG_STR_MAX + 300];

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
#endif /* HAVE_HUNSPELL */
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

int ui_setup_run(TeApp *app, TeConfig *cfg, const char *cfg_path)
{
    TeConfig work;
    int tab = 0;
    int sel = 0;
    int scroll_offset = 0;
    int key;
    int dirty = 0;
    int i;
    int ti;
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
            TeConfig old_cfg;
#if defined(PLATFORM_AMIGA) || defined(PLATFORM_WIN32)
            int fi;
            int fallbacks_changed = 0;
            int ttf_details_changed = 0;
            int font_mode_changed = 0;
#endif

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

            old_cfg = *cfg;
            *cfg = work;

            view_set_tab_width(cfg->tab_width > 0 ? cfg->tab_width : 4);
            ed_set_tab_width(view_tab_width());

#if defined(PLATFORM_AMIGA) || defined(PLATFORM_WIN32)
            /* Detect changes that require a font backend reload or window restart */
            font_mode_changed = (old_cfg.ttf_enabled != work.ttf_enabled) || (strcmp(old_cfg.font, work.font) != 0) || (old_cfg.ttf_size != work.ttf_size);

            if (work.ttf_enabled)
            {
                for (fi = 0; fi < TE_CFG_TTF_FALLBACKS; fi++)
                {
                    if (strcmp(old_cfg.ttf_fallback[fi], work.ttf_fallback[fi]) != 0 || old_cfg.ttf_fallback_size[fi] != work.ttf_fallback_size[fi])
                    {
                        fallbacks_changed = 1;
                        break;
                    }
                }

                if (strcmp(old_cfg.ttf_font, work.ttf_font) != 0 || old_cfg.ttf_size != work.ttf_size || old_cfg.ttf_antialias != work.ttf_antialias || old_cfg.ttf_use_utf8 != work.ttf_use_utf8 || fallbacks_changed)
                    ttf_details_changed = 1;
            }

#ifdef PLATFORM_AMIGA
            if (font_mode_changed || ttf_details_changed)
            {
                extern int amiga_set_ttf(const char *ttf_file, int size, int antialias);
                extern int amiga_set_font_name(const char *font_name);
                extern int amiga_set_ttf_encoding(int use_utf8);
                extern int amiga_reinit_window(void);
                extern int amiga_reload_ttf(const char *font_path, int new_size);
                extern int amiga_add_ttf_fallback(const char *path, int size);
                extern void amiga_clear_ttf_fallbacks(void);
                int sz;

                /* Push new fallback set into backend so it is current for reinit/reload */
                amiga_clear_ttf_fallbacks();

                for (fi = 0; fi < TE_CFG_TTF_FALLBACKS; fi++)
                {
                    if (work.ttf_fallback[fi][0])
                    {
                        sz = work.ttf_fallback_size[fi] > 0 ? work.ttf_fallback_size[fi] : work.ttf_size;
                        amiga_add_ttf_fallback(work.ttf_fallback[fi], sz);
                    }
                }

                if (font_mode_changed)
                {
                    /* Switching between TTF and system font requires a full window restart */
                    amiga_set_font_name(work.font[0] ? work.font : NULL);

                    if (work.ttf_enabled)
                    {
                        amiga_set_ttf(work.ttf_font, work.ttf_size, work.ttf_antialias);
                        amiga_set_ttf_encoding(work.ttf_use_utf8);
                    }
                    else
                    {
                        amiga_set_ttf(NULL, 0, 0);
                    }

                    amiga_reinit_window();
                }
                else if (ttf_details_changed)
                {
                    /* Already in TTF mode: lightweight reload, no window restart */
                    amiga_set_ttf(work.ttf_font, work.ttf_size, work.ttf_antialias);
                    amiga_set_ttf_encoding(work.ttf_use_utf8);
                    amiga_reload_ttf(work.ttf_font, work.ttf_size);
                }
            }
#endif
#ifdef PLATFORM_WIN32
            if (font_mode_changed || ttf_details_changed)
            {
                extern int win32_set_font_name(const char *font_name);
                extern int win32_set_font_size(int size);
                extern int win32_add_font_file(const char *path);
                extern void win32_clear_font_files(void);
                extern int win32_reinit_window(void);

                win32_clear_font_files();
                win32_set_font_name(work.font[0] ? work.font : NULL);

                if (work.ttf_enabled)
                {
                    win32_set_font_size(work.ttf_size);
                    win32_add_font_file(work.ttf_font);

                    for (fi = 0; fi < TE_CFG_TTF_FALLBACKS; fi++)
                    {
                        if (work.ttf_fallback[fi][0])
                            win32_add_font_file(work.ttf_fallback[fi]);
                    }
                }
                else
                {
                    win32_set_font_size(work.ttf_size > 0 ? work.ttf_size : 16);
                }

                win32_reinit_window();
            }
#endif
#endif

            /* Reload Hunspell dictionary when spell settings changed */
#ifdef HAVE_HUNSPELL
            if (cfg->spell_enabled != work.spell_enabled || strcmp(cfg->spell_dict_path, work.spell_dict_path) != 0 || strcmp(cfg->spell_dict_name, work.spell_dict_name) != 0 || strcmp(cfg->spell_custom_dict, work.spell_custom_dict) != 0)
            {
                *cfg = work;
                spell_load_from_config(app);
            }
#endif

#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
            if (cfg->hyph_enabled != work.hyph_enabled || strcmp(cfg->hyph_dict_path, work.hyph_dict_path) != 0 || strcmp(cfg->hyph_dict_name, work.hyph_dict_name) != 0)
            {
                *cfg = work;
                hyph_load_from_config(app);
            }
#endif

#if defined(HAVE_HUNSPELL) && defined(HAVE_MYTHES)
            if (cfg->thes_enabled != work.thes_enabled || strcmp(cfg->thes_dict_path, work.thes_dict_path) != 0 || strcmp(cfg->thes_dict_name, work.thes_dict_name) != 0)
            {
                *cfg = work;
                ui_thes_load_from_config(app);
            }
#endif

#ifdef HAVE_TRANSLATE
            if (cfg->translate_enabled != work.translate_enabled ||
                cfg->translate_backend != work.translate_backend ||
                strcmp(cfg->translate_endpoint, work.translate_endpoint) != 0 ||
                strcmp(cfg->translate_api_key, work.translate_api_key) != 0 ||
                strcmp(cfg->translate_email, work.translate_email) != 0 ||
                strcmp(cfg->translate_from_lang, work.translate_from_lang) != 0 ||
                strcmp(cfg->translate_to_lang, work.translate_to_lang) != 0 ||
                cfg->translate_timeout != work.translate_timeout ||
                strcmp(cfg->stardict_path, work.stardict_path) != 0)
            {
                *cfg = work;
                ui_translate_load_from_config(app);
            }
#endif

#ifdef HAVE_TTS
            /* Reload TTS backend on any field change to apply settings immediately */
            if (cfg->tts_enabled != work.tts_enabled ||
                cfg->tts_voice != work.tts_voice ||
                cfg->tts_rate != work.tts_rate ||
                cfg->tts_pitch != work.tts_pitch ||
                cfg->tts_volume != work.tts_volume)
            {
                *cfg = work;
                ui_tts_load_from_config(app);
            }
#endif

#ifdef HAVE_GRAMMAR
            /* Reload grammar checker if its settings changed */
            if (cfg->grammar_enabled != work.grammar_enabled || strcmp(cfg->grammar_dict_path, work.grammar_dict_path) != 0 || strcmp(cfg->grammar_dict_name, work.grammar_dict_name) != 0)
            {
                *cfg = work;
                app->grammar_enabled = cfg->grammar_enabled;
                ui_grammar_load_from_config(app);
            }
#endif

            *cfg = work;

            /* Propagate word move mode to all open editors */
            for (ti = 0; ti < app->tab_count; ti++)
                ed_set_word_move_mode(app->tabs[ti]->editor, cfg->word_move_mode);

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
                st_edit_field(app, &work, &st_fields[gi]);
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