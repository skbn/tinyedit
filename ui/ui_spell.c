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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>
#include "te.h"
#include "wm.h"
#include "../core/utf8.h"
#include "../components/editor.h"

#ifdef HAVE_HUNSPELL
#include "../spell/spell.h"
#ifdef HAVE_MYTHES
#include "../thes/thes.h"
#endif
#endif

/* Draw spell/translate panel */
void ui_spell_draw_panel(TeApp *app)
{
    TeWindow *win;
    int x, y;

    if (!app)
        return;

    if (app->spell_panel_mode < 0)
        return;

    win = wm_get_window_by_type(app->wm, WIN_SPELL);

    if (!win || !win->visible)
        return;

    standend();

    /* Draw panel background */
    attron(COLOR_PAIR(COL_NORMAL));

    for (y = 0; y < win->h; y++)
    {
        for (x = 0; x < win->w; x++)
            mvaddch(win->y + y, win->x + x, ' ');
    }

    /* Draw panel title */
    attron(COLOR_PAIR(COL_TITLEBAR));
    mvaddnwstr(win->y, win->x + 1, app->spell_panel_mode == 0 ? L"[Spell Checker]" : L"[Translator]", 16);
    attron(COLOR_PAIR(COL_NORMAL));

    /* Draw current word or placeholder text */
    if (app->spell_panel_mode == 0)
    {
        if (app->spell_current_word[0])
        {
            const char *word_utf8;

            mvaddnwstr(win->y + 1, win->x + 1, L"Current word: ", 14);
            attron(COLOR_PAIR(COL_SPELL_CURRENT));

            word_utf8 = te_wcs2u8(app->spell_current_word);

            if (word_utf8)
                mvaddstr(win->y + 1, win->x + 15, word_utf8);

            attron(COLOR_PAIR(COL_NORMAL));

            /* Show word status */
            if (app->spell_word_status == 1)
            {
                attron(COLOR_PAIR(COL_NORMAL));
                mvaddnwstr(win->y + 2, win->x + 1, L"Status: CORRECT", 15);
            }
            else if (app->spell_word_status == 2)
            {
                attron(COLOR_PAIR(COL_SEARCH_MATCH));
                mvaddnwstr(win->y + 2, win->x + 1, L"Status: INCORRECT", 17);
                attron(COLOR_PAIR(COL_NORMAL));

                /* Show no suggestions message */
                if (app->spell_suggestion_count == 0)
                {
                    attron(COLOR_PAIR(COL_NORMAL));
                    mvaddnwstr(win->y + 3, win->x + 1, L"Not in database?", 16);
                }
            }
            else
            {
                /* Not checked yet */
                mvaddnwstr(win->y + 2, win->x + 1, L"Press Alt+P to check", 20);
            }
        }
        else
        {
            mvaddnwstr(win->y + 1, win->x + 1, L"Press Alt+P to check word", 25);
        }
    }
    else
    {
        mvaddnwstr(win->y + 1, win->x + 1, L"Translator not yet implemented", 27);
    }
}

#ifdef HAVE_HUNSPELL
/* Free suggestions stored in TeApp */
void ui_spell_free_app_suggestions(TeApp *app)
{
    if (app && app->spell_suggestions)
    {
        spell_free_suggestions(app->spell_handle, app->spell_suggestions, app->spell_suggestion_count);

        app->spell_suggestions = NULL;
        app->spell_suggestion_count = 0;
    }
}

/* Simple word check for highlighting - returns 1 if incorrect, 0 if correct */
int ui_spell_check_word_simple(TeApp *app, const wchar_t *word, int word_len)
{
    char *word_utf8;
    int result;

    if (!app || !app->spell_handle || !app->spell_active)
        return 0;

    if (word_len <= 0 || word_len >= 256)
        return 0;

    word_utf8 = wcs_to_utf8(word, word_len);

    if (!word_utf8)
        return 0;

    result = spell_check(app->spell_handle, word_utf8);

    free(word_utf8);

    return !result; /* Return 1 if incorrect, 0 if correct */
}

/* Show spell correction suggestions popup */
int ui_spell_suggest(TeApp *app, const char *word, char **suggestions, int count)
{
    int selected;
    int i;

    if (!suggestions || count == 0)
        return -1;

    /* Show popup with suggestions */
    selected = ui_popup_list("Spelling Suggestions", (const char **)suggestions, count, 0);

    if (selected >= 0 && selected < count)
    {
        /* User selected a suggestion - return it */
        return selected;
    }

    return -1;
}

/* Load dictionary from configuration */
int spell_load_from_config(TeApp *app)
{
    char dict_path[512];
    char dict_name[128];
    char aff_path[512];
    char dic_path[512];

    if (!app)
        return 0;

#ifdef HAVE_MYTHES
    /* Detach speller from thesaurus so concurrent thes_lookup() can't reach freed Hunhandle */
    if (app->thes_handle)
        thes_set_speller((ThesHandle *)app->thes_handle, NULL);
#endif

    /* Free existing handle */
    if (app->spell_handle)
    {
        spell_free(app->spell_handle);
        app->spell_handle = NULL;
    }

    /* Check if spell checker is enabled in config */
    if (!app->cfg.spell_enabled)
        return 0;

    /* Get dictionary path and name from config */
    if (app->cfg.spell_dict_path[0] == '\0' || app->cfg.spell_dict_name[0] == '\0')
        return 0;

    /* Build full paths to .aff and .dic files */
    strncpy(dict_path, app->cfg.spell_dict_path, sizeof(dict_path) - 1);
    dict_path[sizeof(dict_path) - 1] = '\0';

    strncpy(dict_name, app->cfg.spell_dict_name, sizeof(dict_name) - 1);
    dict_name[sizeof(dict_name) - 1] = '\0';

    snprintf(aff_path, sizeof(aff_path), "%s/%s.aff", dict_path, dict_name);
    snprintf(dic_path, sizeof(dic_path), "%s/%s.dic", dict_path, dict_name);

    /* Load dictionary */
    app->spell_handle = spell_new(aff_path, dic_path);

    if (!app->spell_handle)
    {
        app->spell_enabled = 0;
        return 0;
    }

#ifdef HAVE_MYTHES
    /* Reattach fresh speller to thesaurus so stem fallback resumes working */
    if (app->thes_handle)
        thes_set_speller((ThesHandle *)app->thes_handle, (SpellChecker *)app->spell_handle);
#endif

    /* Set spell_enabled from config */
    app->spell_enabled = app->cfg.spell_enabled;
    return 1;
}

/* Check and correct word under cursor */
int spell_check_word(TeApp *app)
{
    Ed *ed;
    EdInfo info;
    const wchar_t *line;
    int line_len;
    int word_start, word_end;
    int i;
    wchar_t word_wbuf[256];
    char *word_buf;
    char **suggestions;
    int n_suggestions;
    int selected;
    int word_len;
    wchar_t *suggestion_wcs;
    int suggestion_len;

    if (!app || !app->spell_active || !app->spell_handle)
        return 0;

    ed = te_app_get_editor(app);

    if (!ed)
        return 0;

    /* Get cursor position and info */
    ed_get_info(ed, &info);
    line = ed_line_wcs(ed, info.row);

    if (!line)
        return 0;

    line_len = ed_line_len(ed, info.row);

    /* Find word boundaries */
    word_start = info.col;

    while (word_start > 0 && iswalnum(line[word_start - 1]))
        word_start--;

    word_end = info.col;

    while (word_end < line_len && iswalnum(line[word_end]))
        word_end++;

    if (word_start == word_end)
        return 0;

    /* Extract word to wchar_t buffer */
    word_len = word_end - word_start;

    if (word_len >= (int)(sizeof(word_wbuf) / sizeof(wchar_t)))
        return 0;

    for (i = 0; i < word_len; i++)
        word_wbuf[i] = line[word_start + i];

    word_wbuf[word_len] = L'\0';

    /* Convert wchar_t to UTF-8 for Hunspell */
    word_buf = wcs_to_utf8(word_wbuf, word_len);

    if (!word_buf)
        return 0;

    /* Save current word (wchar_t) for display in spell panel */
    if (word_len < (int)(sizeof(app->spell_current_word) / sizeof(wchar_t)))
    {
        for (i = 0; i < word_len; i++)
            app->spell_current_word[i] = word_wbuf[i];

        app->spell_current_word[word_len] = L'\0';
    }

    /* Show WIN_SPELL panel when checking a word */
    app->show_spell = 1;

    /* Check spelling */
    if (spell_check(app->spell_handle, word_buf))
    {
        te_status(app, "Word '%s' is correct", word_buf);

        app->spell_word_status = 1; /* Correct */
        app->spell_suggestion_count = 0;
        app->spell_suggestions = NULL;

        free(word_buf);
        return 0; /* Word is correct */
    }

    app->spell_word_status = 2; /* Incorrect */

    /* Get suggestions */
    suggestions = spell_suggest(app->spell_handle, word_buf, &n_suggestions);

    /* Save suggestions in TeApp */
    app->spell_suggestions = suggestions;
    app->spell_suggestion_count = n_suggestions;

    if (!suggestions || n_suggestions == 0)
    {
        if (suggestions)
            spell_free_suggestions(app->spell_handle, suggestions, n_suggestions);

        te_status(app, "No suggestions for '%s'", word_buf);

        free(word_buf);
        return 0;
    }

    /* Show suggestions popup */
    selected = ui_spell_suggest(app, word_buf, suggestions, n_suggestions);

    if (selected >= 0 && selected < n_suggestions)
    {
        /* Convert suggestion UTF-8 back to wchar_t */
        suggestion_wcs = utf8_to_wcs(suggestions[selected], &suggestion_len);

        if (suggestion_wcs)
        {
            /* Replace word with suggestion */
            ed_save_undo(ed);
            ed_set_pos(ed, info.row, word_start);

            /* Delete old word */
            for (i = 0; i < word_len; i++)
                ed_delete(ed);

            /* Insert new word */
            for (i = 0; i < suggestion_len; i++)
                ed_insert_char(ed, suggestion_wcs[i]);

            te_status(app, "Replaced with '%s'", suggestions[selected]);
            free(suggestion_wcs);
        }
    }

    free(word_buf);

    spell_free_suggestions(app->spell_handle, suggestions, n_suggestions);

    return 1;
}
#endif
