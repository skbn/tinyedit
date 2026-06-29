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
#include "ui_spell.h"
#include "../core/utf8.h"
#include "../spellchecker/spell.h"
#include "../components/editor.h"
#include "ui_editor_helper.h"

#ifdef HAVE_HUNSPELL
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/spell.h"
#else
#include "../spell/spell.h"
#endif
#ifdef HAVE_MYTHES
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/thes.h"
#else
#include "../thes/thes.h"
#endif
#endif
#endif

/* Describes the two halves of a hyphen-split word across two lines */
typedef struct
{
    int first_row;
    int first_start;
    int first_hyphen; /* column of the '-' on first_row */
    int second_row;
    int second_start;
    int second_end;
    wchar_t joined[512];
    int joined_len;
} HyphenSplit;

/* Truncate UTF-8 string to max_cols display columns, appending "..." if needed */
static char *truncate_utf8_cols(const char *s, int max_cols)
{
    wchar_t *w = NULL;
    char *out = NULL;
    int wlen;
    int width;
    int i;

    if (!s || !*s || max_cols <= 0)
        return NULL;

    w = utf8_to_wcs(s, &wlen);

    if (!w)
        return NULL;

    width = wcswidth(w, wlen);

    if (width <= max_cols)
    {
        out = wcs_to_utf8(w, wlen);
        free(w);
        return out;
    }

    if (max_cols < 3)
        max_cols = 3;

    for (i = wlen; i > 0; i--)
    {
        if (wcswidth(w, i) <= max_cols - 3)
            break;
    }

    w[i] = L'.';
    w[i + 1] = L'.';
    w[i + 2] = L'.';
    w[i + 3] = L'\0';

    out = wcs_to_utf8(w, (int)wcslen(w));

    free(w);
    return out;
}

/* Draw spell/translate panel */
void ui_spell_draw_panel(TeApp *app)
{
    TeWindow *win = NULL;
    int x, y;
    int row;
    int i;

    if (!app)
        return;

    if (app->spell_panel_mode < 0)
        return;

    win = wm_get_window_by_type(app->wm, WIN_SPELL);

    if (!win || !win->visible)
        return;

    /* Title bar in titlebar color */
    standend();
    attron(COLOR_PAIR(COL_TITLEBAR));

    for (x = 0; x < win->w; x++)
        mvaddch(win->y, win->x + x, ' ');

    mvaddnwstr(win->y, win->x + 1, L"[Spell Checker]", 16);

    /* Content area in normal colors */
    standend();

    for (row = 1; row < win->h; row++)
    {
        for (x = 0; x < win->w; x++)
            mvaddch(win->y + row, win->x + x, ' ');
    }

    /* Draw current word or placeholder text */
    if (app->spell_current_word[0])
    {
        const char *word_utf8 = NULL;
        char *word_disp = NULL;
        int word_max = win->w - 16;

        if (word_max < 1)
            word_max = 1;

        mvaddnwstr(win->y + 1, win->x + 1, L"Current word: ", 14);
        attron(COLOR_PAIR(COL_SPELL_CURRENT));

        word_utf8 = te_wcs2u8(app->spell_current_word);

        if (word_utf8)
            word_disp = truncate_utf8_cols(word_utf8, word_max);

        mvaddstr(win->y + 1, win->x + 15, word_disp ? word_disp : "");

        free(word_disp);

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

    standend();
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

/* Hash a wchar_t word for the spell-check cache */
static unsigned int spell_cache_hash(const wchar_t *word, int word_len)
{
    unsigned int h = 5381;
    int i;

    for (i = 0; i < word_len; i++)
        h = ((h << 5) + h) + (unsigned int)word[i];

    return h;
}

void ui_spell_cache_init(TeSpellCache *cache)
{
    int i;

    if (!cache)
        return;

    for (i = 0; i < TE_SPELL_CACHE_SIZE; i++)
    {
        cache->entries[i].word = NULL;
        cache->entries[i].len = 0;
        cache->entries[i].incorrect = 0;
    }
}

void ui_spell_cache_clear(TeSpellCache *cache)
{
    int i;

    if (!cache)
        return;

    for (i = 0; i < TE_SPELL_CACHE_SIZE; i++)
    {
        if (cache->entries[i].word)
            free(cache->entries[i].word);

        cache->entries[i].word = NULL;
        cache->entries[i].len = 0;
        cache->entries[i].incorrect = 0;
    }
}

int ui_spell_cache_lookup(TeSpellCache *cache, const wchar_t *word, int word_len, int *out_incorrect)
{
    unsigned int h;
    int idx;
    int start;
    TeSpellCacheEntry *e = NULL;

    if (!cache || !word || word_len <= 0 || !out_incorrect)
        return 0;

    h = spell_cache_hash(word, word_len);
    idx = (int)(h % (unsigned int)TE_SPELL_CACHE_SIZE);
    start = idx;

    for (;;)
    {
        e = &cache->entries[idx];

        if (!e->word)
            return 0;

        if (e->len == word_len && wmemcmp(e->word, word, word_len) == 0)
        {
            *out_incorrect = e->incorrect;
            return 1;
        }

        idx = (idx + 1) % TE_SPELL_CACHE_SIZE;

        if (idx == start)
            return 0;
    }
}

void ui_spell_cache_put(TeSpellCache *cache, const wchar_t *word, int word_len, int incorrect)
{
    unsigned int h;
    int idx;
    int start;
    int empty_idx = -1;
    TeSpellCacheEntry *e = NULL;
    wchar_t *copy = NULL;

    if (!cache || !word || word_len <= 0)
        return;

    h = spell_cache_hash(word, word_len);
    idx = (int)(h % (unsigned int)TE_SPELL_CACHE_SIZE);
    start = idx;

    for (;;)
    {
        e = &cache->entries[idx];

        if (!e->word)
        {
            if (empty_idx == -1)
                empty_idx = idx;

            break;
        }

        if (e->len == word_len && wmemcmp(e->word, word, word_len) == 0)
        {
            e->incorrect = incorrect;
            return;
        }

        idx = (idx + 1) % TE_SPELL_CACHE_SIZE;

        if (idx == start)
        {
            empty_idx = start;
            break;
        }
    }

    if (empty_idx == -1)
        empty_idx = start;

    e = &cache->entries[empty_idx];

    if (e->word)
        free(e->word);

    copy = (wchar_t *)malloc(sizeof(wchar_t) * ((size_t)word_len + 1));

    if (!copy)
        return;

    wmemcpy(copy, word, word_len);
    copy[word_len] = L'\0';

    e->word = copy;
    e->len = word_len;
    e->incorrect = incorrect;
}

/* Simple word check for highlighting - returns 1 if incorrect, 0 if correct */
int ui_spell_check_word_simple(TeApp *app, const wchar_t *word, int word_len)
{
    char *word_utf8 = NULL;
    int result;
    int cached;

    if (!app || !app->spell_handle || !app->spell_active)
        return 0;

    if (word_len <= 0 || word_len >= 256)
        return 0;

    cached = ui_spell_cache_lookup(&app->spell_cache, word, word_len, &result);

    if (cached)
        return result;

    word_utf8 = wcs_to_utf8(word, word_len);

    if (!word_utf8)
        return 0;

    result = spell_check(app->spell_handle, word_utf8);

    free(word_utf8);

    result = !result; /* Return 1 if incorrect, 0 if correct */

    ui_spell_cache_put(&app->spell_cache, word, word_len, result);

    return result;
}

/* Show spell correction suggestions popup */
int ui_spell_suggest(TeApp *app, const char *word, char **suggestions, int count)
{
    const char **items = NULL;
    char add_label[160];
    int total;
    int selected;
    int i;

    total = (count > 0 ? count : 0) + 1; /* always offer "Add" */
    items = (const char **)malloc((size_t)total * sizeof(char *));

    if (!items)
        return -1;

    for (i = 0; i < count; i++)
        items[i] = suggestions[i];

    snprintf(add_label, sizeof(add_label), "[+ Add \"%s\" to dictionary]", word ? word : "");

    items[total - 1] = add_label;

    selected = ui_popup_list("Spelling Suggestions", items, total, 0);
    free(items);

    if (selected == total - 1)
        return UI_SPELL_ADD_TO_DICT;

    if (selected >= 0 && selected < count)
        return selected;

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

    /* Free existing handle and clear cached results */
    if (app->spell_handle)
    {
        spell_free(app->spell_handle);
        app->spell_handle = NULL;
    }

    ui_spell_cache_clear(&app->spell_cache);

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

    if (app->cfg.spell_custom_dict[0])
        spell_load_custom(app->spell_handle, app->cfg.spell_custom_dict);

#ifdef HAVE_MYTHES
    /* Reattach fresh speller to thesaurus so stem fallback resumes working */
    if (app->thes_handle)
        thes_set_speller((ThesHandle *)app->thes_handle, (SpellChecker *)app->spell_handle);
#endif

    /* Set spell_enabled from config */
    app->spell_enabled = app->cfg.spell_enabled;
    return 1;
}

/* Detect if the word under the cursor is one half of a hyphen-split word */
static int hyphen_split_find(TeApp *app, Ed *ed, EdInfo *info, const wchar_t *line, int line_len, int word_start, int word_end, HyphenSplit *hs)
{
    int word_len = word_end - word_start;

    if (!app || !app->spell_handle || !ed || !info || !line || !hs)
        return 0;

    if (word_len <= 0 || word_len >= 256)
        return 0;

    memset(hs, 0, sizeof(*hs));

    /* Case A: hyphen at EOL followed by lowercase word */
    if (word_end < line_len && line[word_end] == L'-' && word_end + 1 == line_len && info->row + 1 < info->line_count)
    {
        const wchar_t *next_line = ed_line_wcs(ed, info->row + 1);

        if (next_line)
        {
            int next_len = ed_line_len(ed, info->row + 1);
            int next_end = 0;

            while (next_end < next_len && te_is_word_char_ex(app->spell_handle, next_line[next_end]))
                next_end++;

            /* Continuation must start lowercase */
            if (next_end > 0 && iswlower(next_line[0]))
            {
                int i, j;

                hs->first_row = info->row;
                hs->first_start = word_start;
                hs->first_hyphen = word_end;
                hs->second_row = info->row + 1;
                hs->second_start = 0;
                hs->second_end = next_end;

                for (i = 0; i < word_len && hs->joined_len < 510; i++)
                    hs->joined[hs->joined_len++] = line[word_start + i];

                for (j = 0; j < next_end && hs->joined_len < 510; j++)
                    hs->joined[hs->joined_len++] = next_line[j];

                hs->joined[hs->joined_len] = L'\0';

                return 1;
            }
        }

        return 0;
    }

    /* Case B: word at column 0 preceded by "word-" on previous line */
    if (word_start == 0 && info->row > 0 && word_end > 0 && iswlower(line[0]))
    {
        const wchar_t *prev_line = ed_line_wcs(ed, info->row - 1);

        if (prev_line)
        {
            int prev_len = ed_line_len(ed, info->row - 1);

            if (prev_len >= 2 && prev_line[prev_len - 1] == L'-')
            {
                int prev_word_end = prev_len - 1;
                int prev_word_start = prev_word_end;

                while (prev_word_start > 0 && te_is_word_char_ex(app->spell_handle, prev_line[prev_word_start - 1]))
                    prev_word_start--;

                if (prev_word_end > prev_word_start)
                {
                    int i, j;

                    hs->first_row = info->row - 1;
                    hs->first_start = prev_word_start;
                    hs->first_hyphen = prev_len - 1;
                    hs->second_row = info->row;
                    hs->second_start = word_start;
                    hs->second_end = word_end;

                    for (j = prev_word_start; j < prev_word_end && hs->joined_len < 510; j++)
                        hs->joined[hs->joined_len++] = prev_line[j];

                    for (i = 0; i < word_len && hs->joined_len < 510; i++)
                        hs->joined[hs->joined_len++] = line[word_start + i];

                    hs->joined[hs->joined_len] = L'\0';

                    return 1;
                }
            }
        }
    }

    return 0;
}

/* Replace hyphen-split word with suggestion */
static void hyphen_split_replace(Ed *ed, const HyphenSplit *hs, const wchar_t *suggestion, int suggestion_len)
{
    int i;

    if (!ed || !hs || !suggestion || suggestion_len <= 0)
        return;

    ed_save_undo(ed);

    /* Delete second half on second_row */
    ed_set_pos(ed, hs->second_row, hs->second_start);

    for (i = 0; i < hs->second_end - hs->second_start; i++)
        ed_delete(ed);

    /* Delete first half and hyphen on first_row */
    ed_set_pos(ed, hs->first_row, hs->first_start);

    for (i = 0; i < hs->first_hyphen + 1 - hs->first_start; i++)
        ed_delete(ed);

    /* Insert suggestion at first_row, first_start */
    for (i = 0; i < suggestion_len; i++)
        ed_insert_char(ed, suggestion[i]);

    /* Join first_row with the following line (which now holds the remainder) */
    ed_set_pos(ed, hs->first_row, hs->first_start + suggestion_len);
    ed_delete(ed);
}

/* Check and correct word under cursor */
int spell_check_word(TeApp *app)
{
    Ed *ed = NULL;
    EdInfo info;
    const wchar_t *line = NULL;
    int line_len;
    int word_start, word_end;
    int i;
    wchar_t word_wbuf[256];
    char *word_buf = NULL;
    char *joined_buf = NULL;
    char *check_buf = NULL;
    char **suggestions = NULL;
    int n_suggestions;
    int selected;
    int word_len;
    wchar_t *suggestion_wcs = NULL;
    int suggestion_len;
    HyphenSplit hs;
    int is_hyphen_split = 0;

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

    /* Find word boundaries. Use _ex so WORDCHARS (".", "'", digits)
     * from the loaded .aff also count -- e.g. "etc." extracts whole */
    word_start = info.col;

    while (word_start > 0 && te_is_word_char_ex(app->spell_handle, line[word_start - 1]))
        word_start--;

    word_end = info.col;

    while (word_end < line_len && te_is_word_char_ex(app->spell_handle, line[word_end]))
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

    check_buf = word_buf;

    /* Join hyphen-split halves for spell checking */
    if (hyphen_split_find(app, ed, &info, line, line_len, word_start, word_end, &hs))
    {
        joined_buf = wcs_to_utf8(hs.joined, hs.joined_len);

        if (joined_buf)
        {
            is_hyphen_split = 1;
            check_buf = joined_buf;
        }
    }

    /* Save current word (wchar_t) for display in spell panel */
    if (is_hyphen_split)
    {
        if (hs.joined_len < (int)(sizeof(app->spell_current_word) / sizeof(wchar_t)))
        {
            for (i = 0; i < hs.joined_len; i++)
                app->spell_current_word[i] = hs.joined[i];

            app->spell_current_word[hs.joined_len] = L'\0';
        }
    }
    else
    {
        if (word_len < (int)(sizeof(app->spell_current_word) / sizeof(wchar_t)))
        {
            for (i = 0; i < word_len; i++)
                app->spell_current_word[i] = word_wbuf[i];

            app->spell_current_word[word_len] = L'\0';
        }
    }

    /* Show WIN_SPELL panel when checking a word */
    app->show_spell = 1;

    /* Check spelling. If this is a hyphen-split word, check the joined word */
    if (spell_check(app->spell_handle, check_buf))
    {
        te_status(app, "Word '%s' is correct", check_buf);

        app->spell_word_status = 1; /* Correct */
        app->spell_suggestion_count = 0;
        app->spell_suggestions = NULL;

        free(word_buf);
        free(joined_buf);
        return 0; /* Word is correct */
    }

    app->spell_word_status = 2; /* Incorrect */

    /* Get suggestions */
    suggestions = spell_suggest(app->spell_handle, check_buf, &n_suggestions);

    /* Save suggestions in TeApp */
    app->spell_suggestions = suggestions;
    app->spell_suggestion_count = n_suggestions;

    if (!suggestions || n_suggestions == 0)
    {
        char prompt[256];

        if (suggestions)
            spell_free_suggestions(app->spell_handle, suggestions, n_suggestions);

        app->spell_suggestions = NULL;
        app->spell_suggestion_count = 0;

        snprintf(prompt, sizeof(prompt), "Add \"%s\" to your custom dictionary?", check_buf);

        if (ui_popup_confirm("Unknown word", prompt) == 1)
        {
            if (spell_add_to_custom_dict(app->spell_handle, check_buf, app->cfg.spell_custom_dict) == 0)
            {
                app->spell_word_status = 1;

                ui_spell_cache_clear(&app->spell_cache);
                te_status(app, "Added '%s' to dictionary", check_buf);
            }
            else
            {
                te_status(app, "Failed to add '%s'", check_buf);
            }
        }
        else
        {
            te_status(app, "No suggestions for '%s'", check_buf);
        }

        free(word_buf);
        free(joined_buf);
        return 0;
    }

    /* Show suggestions popup (with implicit "Add to dictionary" entry) */
    selected = ui_spell_suggest(app, check_buf, suggestions, n_suggestions);

    if (selected == UI_SPELL_ADD_TO_DICT)
    {
        if (spell_add_to_custom_dict(app->spell_handle, check_buf, app->cfg.spell_custom_dict) == 0)
        {
            app->spell_word_status = 1; /* now considered correct */

            ui_spell_cache_clear(&app->spell_cache);
            te_status(app, "Added '%s' to dictionary", check_buf);
        }
        else
        {
            te_status(app, "Failed to add '%s'", check_buf);
        }
    }
    else if (selected >= 0 && selected < n_suggestions)
    {
        /* Convert suggestion UTF-8 back to wchar_t */
        suggestion_wcs = utf8_to_wcs(suggestions[selected], &suggestion_len);

        if (suggestion_wcs)
        {
            if (is_hyphen_split)
            {
                /* Replace the whole hyphen-split word with the suggestion */
                hyphen_split_replace(ed, &hs, suggestion_wcs, suggestion_len);
                te_status(app, "Replaced hyphenated word with '%s'", suggestions[selected]);
            }
            else
            {
                /* Replace word with suggestion */
                ed_auto_rewrap_capture_pre_snapshot(ed);

                ed_save_undo(ed);
                ed_set_pos(ed, info.row, word_start);

                for (i = 0; i < word_len; i++)
                    ed_delete(ed);

                for (i = 0; i < suggestion_len; i++)
                    ed_insert_char(ed, suggestion_wcs[i]);

                ed_auto_rewrap_after_edit(app);

                te_status(app, "Replaced with '%s'", suggestions[selected]);
            }

            free(suggestion_wcs);
        }
    }

    free(word_buf);
    free(joined_buf);

    spell_free_suggestions(app->spell_handle, suggestions, n_suggestions);

    return 1;
}
#endif
