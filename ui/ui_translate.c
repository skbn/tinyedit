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

#include "ui_translate.h"
#include "ui_dict.h"
#include "ui_editor_helper.h"
#include "te.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "../components/config.h"
#include "../components/editor.h"
#include "../core/utf8.h"

#ifdef HAVE_TRANSLATE
#include "../translate/translate.h"
#include "../translate/http_client.h"
#include "../translate/translate_stardict.h"
#endif

#ifdef HAVE_TRANSLATE

int ui_translate_load_from_config(TeApp *app)
{
    TranslateOpts opts;
    TranslateHandle *h = NULL;
    const char *backend_name = NULL;

    if (!app)
        return 0;

    /* Tear down existing handle so config changes take effect */
    ui_translate_unload(app);

    if (!app->cfg.translate_enabled)
        return 0;

    memset(&opts, 0, sizeof(opts));

    switch (app->cfg.translate_backend)
    {
    case 0:
        backend_name = "mymemory";
        break;
    case 1:
        backend_name = "libretranslate";
        break;
    case 2:
        backend_name = "lingva";
        break;
    case 4:
        backend_name = "deepl";
        break;
    case 10:
        backend_name = "stardict";
        break;
    default:
        backend_name = "mymemory";
        break;
    }
    opts.backend = translate_backend_parse(backend_name);

    opts.endpoint = app->cfg.translate_endpoint[0] ? app->cfg.translate_endpoint : NULL;
    opts.api_key = app->cfg.translate_api_key[0] ? app->cfg.translate_api_key : NULL;
    opts.email = app->cfg.translate_email[0] ? app->cfg.translate_email : NULL;
    opts.timeout_secs = app->cfg.translate_timeout > 0 ? app->cfg.translate_timeout : 10;
    opts.stardict_path = app->cfg.stardict_path[0] ? app->cfg.stardict_path : NULL;

    if (http_client_init() != HTTP_OK)
    {
        te_status(app, "Translator: cannot init network");
        return 0;
    }

    h = translate_new(&opts);

    if (!h)
    {
        http_client_shutdown();
        te_status(app, "Translator: handle init failed");
        return 0;
    }

    app->translate_handle = h;
    app->translate_http_inited = 1; /* Mark that http_client_init was called */

    return 1;
}

void ui_translate_unload(TeApp *app)
{
    if (!app)
        return;

    if (app->translate_handle)
    {
        translate_free((TranslateHandle *)app->translate_handle);
        app->translate_handle = NULL;
    }

    /* Only shutdown HTTP if we initialized it */
    if (app->translate_http_inited)
    {
        http_client_shutdown();
        app->translate_http_inited = 0;
    }
}

/* If a block selection exists in the editor, fill *out_text (UTF-8 malloc'd) with the selection */
static int grab_source_text(TeApp *app, char **out_text, int *out_row_first, int *out_row_last)
{
    Ed *ed = NULL;
    EdInfo info;
    int first;
    int last;
    wchar_t *joined = NULL;
    size_t cap = 0;
    int used = 0;
    char *utf8 = NULL;
    int row;

    *out_text = NULL;

    if (!app)
        return -1;

    ed = te_app_get_editor(app);

    if (!ed)
        return -1;

    ed_get_info(ed, &info);

    if (info.block.active)
    {
        /* Use exact selection - normalise anchor/cursor into r1<=r2 */
        int ar = info.block.anchor_row;
        int ac = info.block.anchor_col;
        int cr = info.row;
        int cc = info.col;
        int r1, c1, r2, c2;
        char *block_utf8 = NULL;

        if (ar < cr || (ar == cr && ac < cc))
        {
            r1 = ar;
            c1 = ac;
            r2 = cr;
            c2 = cc;
        }
        else
        {
            r1 = cr;
            c1 = cc;
            r2 = ar;
            c2 = ac;
        }

        /* Empty selection -> fall back */
        if (r1 != r2 || c1 != c2)
        {
            block_utf8 = ed_block_to_string(ed, r1, c1, r2, c2);

            if (!block_utf8)
                return -1;

            *out_text = block_utf8;
            *out_row_first = r1;
            *out_row_last = r2;
            return 0;
        }
    }

    /* Paragraph: walk up until blank line, then down */
    first = info.row;

    while (first > 0 && ed_line_len(ed, first - 1) > 0)
        first--;

    last = info.row;

    while (last < info.line_count - 1 && ed_line_len(ed, last + 1) > 0)
        last++;

    /* Join lines with spaces */

    for (row = first; row <= last; row++)
    {
        const wchar_t *line = ed_line_wcs(ed, row);
        int ll = ed_line_len(ed, row);

        if (used + (size_t)ll + 2 >= cap)
        {
            size_t nc = (used + (size_t)ll + 64) * 2;
            wchar_t *nb = (wchar_t *)realloc(joined, nc * sizeof(wchar_t));

            if (!nb)
            {
                free(joined);
                return -1;
            }

            joined = nb;
            cap = nc;
        }

        if (line && ll > 0)
        {
            memcpy(joined + used, line, (size_t)ll * sizeof(wchar_t));
            used += (size_t)ll;
        }

        if (row < last)
            joined[used++] = L' ';
    }

    if (used == 0)
    {
        free(joined);
        return -1;
    }

    joined[used] = L'\0';

    utf8 = wcs_to_utf8(joined, (int)used);
    free(joined);

    if (!utf8)
        return -1;

    *out_text = utf8;
    *out_row_first = first;
    *out_row_last = last;
    return 0;
}

/* Replace lines [first..last] with new_utf8. Used by the "Replace" action of the popup */
static int replace_lines(TeApp *app, int first, int last, const char *new_utf8)
{
    Ed *ed = te_app_get_editor(app);
    char *snapshot_before = NULL;
    char *snapshot_after = NULL;
    int old_count;
    int new_count;
    int cursor_row_before;
    int cursor_col_before;
    int cursor_row_after;
    int cursor_col_after;
    EdInfo info;

    if (!ed)
        return -1;

    if (ed->undo_snapshot_mode)
        return -1;

    /* Open a single undo group covering the whole line-range replacement */
    if (ed_undo_open_group(ed) != 0)
        return -1;

    ed_get_info(ed, &info);
    cursor_row_before = info.row;
    cursor_col_before = info.col;

    old_count = last - first + 1;

    /* Snapshot the original lines so undo can restore them in one step */
    snapshot_before = ed_range_to_string(ed, first, last + 1);

    if (!snapshot_before)
    {
        ed->undo_open = 0;
        return -1;
    }

    /* Replace the whole range in one operation */
    new_count = ed_replace_range_from_utf8(ed, first, old_count, new_utf8);

    if (new_count <= 0)
    {
        free(snapshot_before);

        ed->undo_open = 0;
        return -1;
    }

    /* Snapshot the new lines for redo */
    snapshot_after = ed_range_to_string(ed, first, first + new_count);

    if (!snapshot_after)
    {
        free(snapshot_before);

        ed->undo_open = 0;
        return -1;
    }

    /* Place cursor at end of inserted text, like ed_paste_text does */
    cursor_row_after = first + new_count - 1;
    cursor_col_after = ed_line_len(ed, cursor_row_after);

    ed->undo_open = 0;

    /* Record one OP_SNAPSHOT_RANGE for the entire replacement */
    if (undo_push_snapshot_range(ed, first, 0, snapshot_before, snapshot_after, old_count, new_count, cursor_row_before, cursor_col_before, cursor_row_after, cursor_col_after) != 0)
        return -1;

    ed_set_modified(ed, 1);
    ed_prefix_invalidate_from(ed, first);
    ed_ensure_visible(ed);

    return 0;
}

/* Show the source preview + result in a popup; offer Replace/Insert/Cancel */
static int translate_show_popup(TeApp *app, const char *src, const char *dst, const char *result_utf8, const char *err_msg)
{
    static const char *choices[] =
        {
            "Replace selection",
            "Insert below",
            "Copy to clipboard",
            "Cancel",
            NULL};

    int n_choices;
    int sel;

    if (err_msg && err_msg[0])
    {
        te_status(app, "Translate error: %s", err_msg);
        return 0;
    }

    if (!result_utf8 || !result_utf8[0])
    {
        te_status(app, "Translate: empty result");
        return 0;
    }

    /* Show the result in the status bar (compact) plus offer actions */
    te_status(app, "Translated: %.120s", result_utf8);

    /* Count items */
    for (n_choices = 0; choices[n_choices]; n_choices++)
        ;

    sel = ui_popup_list("Translation - what to do?", choices, n_choices, 0);

    if (sel == 0)
        return 1; /* Replace */

    if (sel == 1)
        return 2; /* Insert below */

    if (sel == 2)
    {
        /* Copy via clipboard if available */
#ifdef HAVE_CLIPBOARD
        extern int clipboard_copy(const char *utf8);

        if (clipboard_copy(result_utf8) == 0)
            te_status(app, "Translation copied to clipboard");
        else
            te_status(app, "Clipboard copy failed");
#else
        te_status(app, "Clipboard not available; translation kept in cache");
#endif

        return 0;
    }

    return 0;
}

int ui_translate_action(TeApp *app)
{
    char *src = NULL;
    int first = 0;
    int last = 0;
    char *result = NULL;
    char err[128];
    char detected[8];
    const char *from_lang = NULL;
    const char *to_lang = NULL;
    int choice;

    if (!app)
        return 1;

    if (!app->translate_active)
    {
        te_status(app, "Translator disabled (press Ctrl+T to enable)");
        return 1;
    }

    if (!app->translate_handle)
    {
        te_status(app, "Translator not loaded (configure TRANSLATE_*)");
        return 1;
    }

    if (grab_source_text(app, &src, &first, &last) != 0 || !src || !src[0])
    {
        te_status(app, "No text to translate");

        if (src)
            free(src);

        return 1;
    }

    from_lang = (app->cfg.translate_from_lang[0]) ? app->cfg.translate_from_lang : "auto";
    to_lang = (app->cfg.translate_to_lang[0]) ? app->cfg.translate_to_lang : "es";

    te_status(app, "Translating %s -> %s...", from_lang, to_lang);
    refresh();

    err[0] = '\0';
    detected[0] = '\0';

    result = translate_text((TranslateHandle *)app->translate_handle, from_lang, to_lang, src, detected, sizeof(detected), err, sizeof(err));
    refresh();

    if (!result)
    {
        te_status(app, "Translate failed: %s", err[0] ? err : "(unknown)");
        free(src);
        return 1;
    }

    if (app->cfg.translate_backend == TRANSLATE_BACKEND_STARDICT)
    {
        char header[128];
        int n = (int)strlen(src);

        if (n > (int)sizeof(header) - 1)
            n = (int)sizeof(header) - 1;

        memcpy(header, src, (size_t)n);
        header[n] = '\0';

        /* trim trailing whitespace for clean header */
        while (n > 0 && (header[n - 1] == ' ' || header[n - 1] == '\n' || header[n - 1] == '\r' || header[n - 1] == '\t'))
            header[--n] = '\0';

        ui_dict_set_result(app, header, result);

        if (app->spell_panel_mode != 2 && app->translate_active)
            app->spell_panel_mode = 2;

        te_status(app, "Dictionary: %d line(s) (Alt+Up/Down to scroll)", (int)(strlen(result) ? 1 : 0));

        free(result);
        free(src);
        return 1;
    }

    choice = translate_show_popup(app, from_lang, to_lang, result, NULL);

    if (choice == 1)
    {
        /* Replace lines */
        ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));

        if (replace_lines(app, first, last, result) == 0)
        {
            te_status(app, "Replaced %d line(s) with translation", last - first + 1);

            ed_block_clear(te_app_get_editor(app));
            ed_auto_rewrap_after_edit(app);
        }
        else
            te_status(app, "Replace failed");
    }
    else if (choice == 2)
    {
        /* Insert a new paragraph below last */
        Ed *ed = te_app_get_editor(app);

        if (!ed)
        {
            te_status(app, "Insert failed: no editor");
        }
        else
        {
            ed_auto_rewrap_capture_pre_snapshot(ed);

            ed_save_undo(ed);

            ed_set_pos(ed, last, ed_line_len(ed, last));
            ed_enter(ed); /* line break */

            if (ed_paste_text_with_undo(ed, result) == 0)
            {
                te_status(app, "Inserted translation below paragraph");

                ed_block_clear(ed);
                ed_auto_rewrap_after_edit(app);
            }
            else
                te_status(app, "Insert failed");
        }
    }

    free(result);
    free(src);

    return 1;
}

#else /* !HAVE_TRANSLATE - inert stubs */

int ui_translate_load_from_config(TeApp *app)
{
    return 0;
}

void ui_translate_unload(TeApp *app)
{
}

int ui_translate_action(TeApp *app)
{
    if (app)
        te_status(app, "Translator not built in (USE_TRANSLATE=1)");

    return 1;
}

#endif /* HAVE_TRANSLATE */
