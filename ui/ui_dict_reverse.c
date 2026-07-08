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
#include <wchar.h>
#include "te.h"
#include "ui_dict_reverse.h"
#include "ui_editor_helper.h"
#include "../core/utf8.h"
#include "../components/editor.h"
#include "../spellchecker/spell.h"

#ifdef HAVE_TRANSLATE
#include "../translate/translate.h"
#endif

extern int ui_popup_list(const char *title, const char **items, int count, int initial);
extern void te_status(TeApp *app, const char *fmt, ...);

#define DICT_REV_MAX_ITEMS 32

/* Amiga has limited CPU. Cap scan to avoid multi-minute hangs. On Unix/Windows we let it run full */
#if defined(PLATFORM_AMIGA)
#define DICT_REV_MAX_SCAN 8000
#else
#define DICT_REV_MAX_SCAN 0 /* unlimited */
#endif

int ui_dict_reverse(TeApp *app)
{
#ifndef HAVE_TRANSLATE
    te_status(app, "Translator support not built in");
    return 1;
#else
    Ed *ed = NULL;
    EdInfo info;
    const wchar_t *line = NULL;
    int line_len;
    int word_start;
    int word_end;
    int i;
    wchar_t word_wbuf[256];
    char *word_buf = NULL;
    char *items[DICT_REV_MAX_ITEMS];
    int n_items = 0;
    int selected;
    int word_len;
    wchar_t *replace_wcs = NULL;
    int replace_len;

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

    te_status(app, "Scanning dictionary for '%s'...", word_buf);
    refresh();

    n_items = translate_reverse((TranslateHandle *)app->translate_handle, word_buf, items, DICT_REV_MAX_ITEMS, DICT_REV_MAX_SCAN);

    if (n_items == 0)
    {
        te_status(app, "Reverse lookup: no entries contain '%s'", word_buf);
        free(word_buf);
        return 1;
    }

    {
        char title[160];

        snprintf(title, sizeof(title), "Source words for '%s' (%d found)", word_buf, n_items);
        selected = ui_popup_list(title, (const char **)items, n_items, 0);
    }

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

    for (i = 0; i < n_items; i++)
        free(items[i]);

    free(word_buf);

    return 1;
#endif /* HAVE_TRANSLATE */
}
