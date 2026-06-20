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

#include "ui_thes.h"
#include "ui_thes_glue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#ifdef HAVE_MYTHES
#include "../thes/thes.h"
#endif

#include "../core/utf8.h"
#include "../core/charset.h"

/* Provided by the project's UI module (ui_popup.c) */
extern int ui_popup_list(const char *title, const char **items, int count, int initial);

#ifdef HAVE_MYTHES

static int thes_is_word_char(wchar_t c)
{
    if (iswalnum((wint_t)c))
        return 1;

    if (c == L'\'' || c == L'-')
        return 1;

    return 0;
}

int ui_thes_load_from_config(UI_APP_T *app)
{
    char idx_path[512];
    char dat_path[512];
    const char *base_path;
    const char *base_name;

    if (!app)
        return 0;

    ui_thes_unload(app);

    if (!UI_APP_CFG(app).thes_enabled)
        return 0;

    base_path = UI_APP_CFG(app).thes_dict_path;
    base_name = UI_APP_CFG(app).thes_dict_name;

    if (!base_path[0] || !base_name[0])
        return 0;

    /* Use dictionary name as-is (user may include th_ prefix and _v2 suffix) */
    snprintf(idx_path, sizeof(idx_path), "%.220s/%.40s.idx", base_path, base_name);
    snprintf(dat_path, sizeof(dat_path), "%.220s/%.40s.dat", base_path, base_name);

    UI_APP_THES_HANDLE(app) = thes_new(idx_path, dat_path);

    if (!UI_APP_THES_HANDLE(app))
    {
        /* Try fallback naming without the _v2 suffix */
        snprintf(idx_path, sizeof(idx_path), "%.220s/th_%.40s.idx", base_path, base_name);
        snprintf(dat_path, sizeof(dat_path), "%.220s/th_%.40s.dat", base_path, base_name);

        UI_APP_THES_HANDLE(app) = thes_new(idx_path, dat_path);
    }

    /* Attach speller so MyThes can fall back through Hunspell_stem for non-direct entries. Safe with NULL speller */
    if (UI_APP_THES_HANDLE(app))
        thes_set_speller(UI_APP_THES_HANDLE(app), UI_APP_SPELL_HANDLE(app));

    return UI_APP_THES_HANDLE(app) ? 1 : 0;
}

void ui_thes_unload(UI_APP_T *app)
{
    if (!app)
        return;

    if (UI_APP_THES_HANDLE(app))
    {
        thes_free(UI_APP_THES_HANDLE(app));
        UI_APP_THES_HANDLE(app) = NULL;
    }
}

int ui_thes_lookup_word(UI_APP_T *app)
{
    Ed *ed;
    EdInfo info;
    const wchar_t *line;
    int line_len, ws, we, i;
    wchar_t wbuf[128];
    char *u8;
    ThesMeaning *meanings;
    int nmeanings, total_items, idx, sel;
    char **items;
    int *item_to_syn; /* maps popup index -> (meaning, syn) pair */
    int m, k;
    const char *thes_encoding = NULL;
    char utf8_buf[1024];
    char defn_utf8_buf[1024];

    if (!app)
        return 0;

    if (!UI_APP_THES_HANDLE(app))
    {
        UI_APP_STATUS_FN(app, "No thesaurus loaded");
        return 0;
    }

    ed = UI_APP_EDITOR(app);

    if (!ed)
        return 0;

    ed_get_info(ed, &info);
    line = ed_line_wcs(ed, info.row);

    if (!line)
        return 0;

    line_len = ed_line_len(ed, info.row);

    ws = info.col;

    while (ws > 0 && thes_is_word_char(line[ws - 1]))
        ws--;

    we = info.col;

    while (we < line_len && thes_is_word_char(line[we]))
        we++;

    if (ws == we)
    {
        UI_APP_STATUS_FN(app, "No word under cursor");
        return 0;
    }

    if ((we - ws) >= (int)(sizeof(wbuf) / sizeof(wbuf[0])))
        return 0;

    for (i = 0; i < (we - ws); i++)
        wbuf[i] = line[ws + i];

    wbuf[we - ws] = L'\0';

    u8 = wcs_to_utf8(wbuf, we - ws);

    if (!u8)
        return 0;

    nmeanings = thes_lookup(UI_APP_THES_HANDLE(app), u8, &meanings);

    if (nmeanings <= 0 || !meanings)
    {
        UI_APP_STATUS_FN(app, "No synonyms for '%s'", u8);

        free(u8);

        return 0;
    }

    /* Flatten: count total synonyms across all meanings */
    total_items = 0;

    for (i = 0; i < nmeanings; i++)
        total_items += meanings[i].nsyns;

    if (total_items == 0)
    {
        thes_free_meanings(UI_APP_THES_HANDLE(app), meanings, nmeanings);
        UI_APP_STATUS_FN(app, "No synonyms for '%s'", u8);

        free(u8);

        return 0;
    }

    items = (char **)calloc((size_t)total_items, sizeof(char *));
    item_to_syn = (int *)calloc((size_t)total_items * 2, sizeof(int));

    if (!items || !item_to_syn)
    {
        free(items);
        free(item_to_syn);

        thes_free_meanings(UI_APP_THES_HANDLE(app), meanings, nmeanings);

        free(u8);
        return 0;
    }

    idx = 0;

    thes_encoding = thes_get_encoding(UI_APP_THES_HANDLE(app));

    for (m = 0; m < nmeanings; m++)
    {
        for (k = 0; k < meanings[m].nsyns; k++)
        {
            /* Format: "<syn>  (<defn>)" */
            size_t need;
            const char *defn;
            const char *syn_utf8;
            const char *defn_utf8;

            defn = meanings[m].def ? meanings[m].def : "";

            /* Convert synonym from thesaurus encoding to UTF-8 */
            if (thes_encoding && thes_encoding[0] && strcmp(thes_encoding, "UTF-8") != 0)
            {
                utf8_buf[0] = '\0';

                charset_to_utf8(thes_encoding, meanings[m].syns[k], (int)strlen(meanings[m].syns[k]), utf8_buf, sizeof(utf8_buf));
                syn_utf8 = utf8_buf;
            }
            else
            {
                syn_utf8 = meanings[m].syns[k];
            }

            /* Convert definition from thesaurus encoding to UTF-8 */
            if (defn[0] && thes_encoding && thes_encoding[0] && strcmp(thes_encoding, "UTF-8") != 0)
            {
                defn_utf8_buf[0] = '\0';

                charset_to_utf8(thes_encoding, defn, (int)strlen(defn), defn_utf8_buf, sizeof(defn_utf8_buf));
                defn_utf8 = defn_utf8_buf;
            }
            else
            {
                defn_utf8 = defn;
            }

            need = strlen(syn_utf8) + strlen(defn_utf8) + 16;
            items[idx] = (char *)malloc(need);

            if (items[idx])
            {
                if (defn_utf8[0])
                    snprintf(items[idx], need, "%s  (%s)", syn_utf8, defn_utf8);
                else
                    snprintf(items[idx], need, "%s", syn_utf8);
            }

            item_to_syn[idx * 2 + 0] = m;
            item_to_syn[idx * 2 + 1] = k;

            idx++;
        }
    }

    sel = ui_popup_list("Synonyms", (const char **)items, total_items, 0);

    if (sel >= 0 && sel < total_items)
    {
        int m = item_to_syn[sel * 2 + 0];
        int k = item_to_syn[sel * 2 + 1];
        const char *chosen_orig = meanings[m].syns[k];
        const char *thes_encoding = thes_get_encoding(UI_APP_THES_HANDLE(app));
        char chosen_utf8[1024];
        const char *chosen;
        wchar_t *wsyn;
        int wlen;

        /* Convert chosen synonym from thesaurus encoding to UTF-8 */
        if (thes_encoding && thes_encoding[0] && strcmp(thes_encoding, "UTF-8") != 0)
        {
            chosen_utf8[0] = '\0';

            charset_to_utf8(thes_encoding, chosen_orig, (int)strlen(chosen_orig), chosen_utf8, sizeof(chosen_utf8));
            chosen = chosen_utf8;
        }
        else
        {
            chosen = chosen_orig;
        }

        wsyn = utf8_to_wcs(chosen, &wlen);

        if (wsyn)
        {
            ed_save_undo(ed);
            ed_set_pos(ed, info.row, ws);

            for (i = 0; i < (we - ws); i++)
                ed_delete(ed);

            for (i = 0; i < wlen; i++)
                ed_insert_char(ed, wsyn[i]);

            free(wsyn);

            UI_APP_STATUS_FN(app, "Replaced with '%s'", chosen);
        }
    }

    /* Tear-down: free EVERYTHING we allocated */
    for (i = 0; i < total_items; i++)
        free(items[i]);

    free(items);
    free(item_to_syn);

    thes_free_meanings(UI_APP_THES_HANDLE(app), meanings, nmeanings);

    free(u8);

    return 1;
}

#else /* !HAVE_MYTHES -- inert stubs */

int ui_thes_load_from_config(UI_APP_T *app)
{
    return 0;
}

void ui_thes_unload(UI_APP_T *app)
{
}

int ui_thes_lookup_word(UI_APP_T *app)
{
    if (app)
        UI_APP_STATUS_FN(app, "Thesaurus support not built in");

    return 0;
}

#endif
