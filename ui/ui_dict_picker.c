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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <wchar.h>
#include "te.h"
#include "ui_dict_picker.h"
#include "ui_editor_helper.h"
#include "../core/utf8.h"
#include "../components/editor.h"
#include "../spellchecker/spell.h"

#ifdef HAVE_TRANSLATE
#include "../translate/translate.h"
#endif

extern int ui_popup_list(const char *title, const char **items, int count, int initial);
extern void te_status(TeApp *app, const char *fmt, ...);

#define DICT_PICKER_MAX_ITEMS 32
#define DICT_PICKER_MAX_LEN 100

/* List of grammar tags we never want to offer as a candidate to insert. Comparison is case insensitive */
static const char *GRAMMAR_TAGS[] =
    {
        "noun", "verb", "adjective", "adverb", "pronoun", "article",
        "conjunction", "preposition", "interjection", "determiner",
        "numeral", "particle", "auxiliary", "modal", "phrase",
        "phraseologicalunit", "abbreviation", "acronym", "initialism",
        "letter", "symbol", "prefix", "suffix", NULL};

static int line_is_grammar_tag(const char *s, int len)
{
    int i;

    for (i = 0; GRAMMAR_TAGS[i]; i++)
    {
        int taglen = (int)strlen(GRAMMAR_TAGS[i]);
        int j;
        int match = 1;

        if (taglen != len)
            continue;

        for (j = 0; j < len; j++)
        {
            char a = s[j];
            char b = GRAMMAR_TAGS[i][j];

            if (a >= 'A' && a <= 'Z')
                a = (char)(a + 32);

            if (b >= 'A' && b <= 'Z')
                b = (char)(b + 32);

            if (a != b)
            {
                match = 0;
                break;
            }
        }

        if (match)
            return 1;
    }

    return 0;
}

static int line_is_sentence(const char *s, int len)
{
    if (len > 60)
        return 1;

    if (len > 0)
    {
        char last = s[len - 1];

        if (last == '.' || last == '?' || last == '!' || last == ':' || last == ';')
            return 1;
    }

    return 0;
}

static int line_is_pronunciation(const char *s, int len)
{
    if (len < 2)
        return 0;

    return (s[0] == '/' || s[0] == '[');
}

static int line_is_note(const char *s, int len)
{
    if (len < 2)
        return 0;

    return (s[0] == '(' && s[len - 1] == ')');
}

static int extract_candidates(const char *text, char **items, int max)
{
    int n = 0;
    int pass;

    if (!text || !items || max <= 0)
        return 0;

    for (pass = 0; pass < 2 && n == 0; pass++)
    {
        const char *line_start = text;
        const char *p = text;

        n = 0;

        for (;;)
        {
            int len;
            int lead = 0;
            int real_len;

            if (*p == '\n' || *p == '\0')
            {
                len = (int)(p - line_start);

                /* trim trailing whitespace */
                while (len > 0 && (line_start[len - 1] == ' ' || line_start[len - 1] == '\t' || line_start[len - 1] == '\r'))
                    len--;

                /* trim leading whitespace */
                while (lead < len && (line_start[lead] == ' ' || line_start[lead] == '\t'))
                    lead++;

                real_len = len - lead;

                if (real_len > 0 && real_len < DICT_PICKER_MAX_LEN)
                {
                    const char *s = line_start + lead;
                    int keep = 1;

                    if (line_is_pronunciation(s, real_len))
                        keep = 0;

                    if (pass == 0)
                    {
                        /* strict pass: also exclude grammar tags, sentences and notes */
                        if (keep && line_is_grammar_tag(s, real_len))
                            keep = 0;

                        if (keep && line_is_sentence(s, real_len))
                            keep = 0;

                        if (keep && line_is_note(s, real_len))
                            keep = 0;
                    }

                    if (keep && n < max)
                    {
                        items[n] = (char *)malloc((size_t)real_len + 1);

                        if (items[n])
                        {
                            memcpy(items[n], s, (size_t)real_len);
                            items[n][real_len] = '\0';
                            n++;
                        }
                    }
                }

                if (*p == '\0')
                    break;

                p++;
                line_start = p;

                continue;
            }

            p++;
        }
    }

    return n;
}

int ui_dict_picker(TeApp *app)
{
#ifndef HAVE_TRANSLATE
    te_status(app, "Translator support not built in");
    return 1;
#else
    Ed *ed = NULL;
    EdInfo info;
    const wchar_t *line = NULL;
    int line_len;
    int word_start, word_end;
    int i;
    wchar_t word_wbuf[256];
    char *word_buf = NULL;
    char *result = NULL;
    char *items[DICT_PICKER_MAX_ITEMS];
    int n_items = 0;
    int selected;
    int word_len;
    char err[128];
    char detected[8];
    const char *from_lang = NULL;
    const char *to_lang = NULL;
    wchar_t *replace_wcs = NULL;
    int replace_len;
    char title[160];

    if (!app)
        return 1;

    if (!app->translate_handle)
    {
        te_status(app, "Translator not loaded (configure TRANSLATE_*)");
        return 1;
    }

    ed = te_app_get_editor(app);

    if (!ed)
        return 1;

    /* Locate the word under the cursor (same rule as spell_check_word) */
    ed_get_info(ed, &info);
    line = ed_line_wcs(ed, info.row);

    if (!line)
    {
        te_status(app, "No text under cursor");
        return 1;
    }

    line_len = ed_line_len(ed, info.row);

    word_start = info.col;

    while (word_start > 0 && te_is_word_char(line[word_start - 1]))
        word_start--;

    word_end = info.col;

    while (word_end < line_len && te_is_word_char(line[word_end]))
        word_end++;

    if (word_start == word_end)
    {
        te_status(app, "Place the cursor on a word and try again");
        return 1;
    }

    word_len = word_end - word_start;

    if (word_len >= (int)(sizeof(word_wbuf) / sizeof(wchar_t)))
    {
        te_status(app, "Word too long");
        return 1;
    }

    for (i = 0; i < word_len; i++)
        word_wbuf[i] = line[word_start + i];

    word_wbuf[word_len] = L'\0';

    word_buf = wcs_to_utf8(word_wbuf, word_len);

    if (!word_buf)
    {
        te_status(app, "Word encoding failed");
        return 1;
    }

    /* Run the lookup */
    from_lang = app->cfg.translate_from_lang[0] ? app->cfg.translate_from_lang : "auto";
    to_lang = app->cfg.translate_to_lang[0] ? app->cfg.translate_to_lang : "es";

    err[0] = '\0';
    detected[0] = '\0';

    result = translate_text((TranslateHandle *)app->translate_handle, from_lang, to_lang, word_buf, detected, sizeof(detected), err, sizeof(err));

    if (!result)
    {
        /* No exact match: offer similar suggestions, then retry with the chosen one */
        char *sugg_items[DICT_PICKER_MAX_ITEMS];
        int n_sugg = 0;
        int sugg_sel;
        char title[160];
        int j;
        char *chosen = NULL;

        n_sugg = translate_suggest((TranslateHandle *)app->translate_handle, word_buf, sugg_items, DICT_PICKER_MAX_ITEMS);

        if (n_sugg <= 0)
        {
            te_status(app, "No translation for '%s'", word_buf);

            free(word_buf);
            return 1;
        }

        snprintf(title, sizeof(title), "Not found: %s -- did you mean?", word_buf);
        sugg_sel = ui_popup_list(title, (const char **)sugg_items, n_sugg, 0);

        if (sugg_sel < 0 || sugg_sel >= n_sugg)
        {
            /* User cancelled */
            for (j = 0; j < n_sugg; j++)
                free(sugg_items[j]);

            free(word_buf);
            return 1;
        }

        /* Replace input with the chosen suggestion and retry the lookup */
        chosen = sugg_items[sugg_sel];

        sugg_items[sugg_sel] = NULL;

        free(word_buf);
        word_buf = chosen;

        for (j = 0; j < n_sugg; j++)
        {
            if (sugg_items[j])
                free(sugg_items[j]);
        }

        err[0] = '\0';
        result = translate_text((TranslateHandle *)app->translate_handle, from_lang, to_lang, word_buf, detected, sizeof(detected), err, sizeof(err));

        if (!result)
        {
            te_status(app, "Lookup of '%s' failed: %s", word_buf, err[0] ? err : "(unknown)");

            free(word_buf);
            return 1;
        }
    }

    /* Extract candidates */
    n_items = extract_candidates(result, items, DICT_PICKER_MAX_ITEMS);

    if (n_items == 0)
    {
        te_status(app, "No usable translation for '%s'", word_buf);

        free(result);
        free(word_buf);
        return 1;
    }

    /* Build label "Translations: word" */
    snprintf(title, sizeof(title), "Translations for: %s", word_buf);

    selected = ui_popup_list(title, (const char **)items, n_items, 0);

    /* Replace word with picked translation */
    if (selected >= 0 && selected < n_items)
    {
        replace_wcs = utf8_to_wcs(items[selected], &replace_len);

        if (replace_wcs)
        {
            ed_auto_rewrap_capture_pre_snapshot(ed);

            ed_save_undo(ed);
            ed->undo_snapshot_mode = 1;
            ed_set_pos(ed, info.row, word_start);

            for (i = 0; i < word_len; i++)
                ed_delete(ed);

            for (i = 0; i < replace_len; i++)
                ed_insert_char(ed, replace_wcs[i]);

            ed->undo_snapshot_mode = 0;
            ed_auto_rewrap_after_edit(app);

            te_status(app, "Replaced with '%s'", items[selected]);
            free(replace_wcs);
        }
    }

    /* Cleanup */
    for (i = 0; i < n_items; i++)
        free(items[i]);

    free(result);
    free(word_buf);

    return 1;
#endif /* HAVE_TRANSLATE */
}
