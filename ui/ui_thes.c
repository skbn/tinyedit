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
#include "ui_editor_helper.h"
#include "../components/undo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#ifdef HAVE_MYTHES
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/thes.h"
#else
#include "../thes/thes.h"
#endif
#endif

#include "../core/utf8.h"
#include "../core/charset.h"

/* Provided by the project's UI module (ui_popup.c) */
extern int ui_popup_list(const char *title, const char **items, int count, int initial);

#ifdef HAVE_MYTHES
/* Information about a word split across two lines by a hyphen */
typedef struct
{
    int first_row;
    int first_start;
    int first_end; /* Column of '-' on first_row */
    int second_row;
    int second_start;
    int second_end;
    wchar_t joined[256];
    int joined_len;
} ThesHyphenSplit;

static int thes_is_word_char(wchar_t c)
{
    if (iswalnum((wint_t)c))
        return 1;

    if (c == L'\'' || c == L'-')
        return 1;

    return 0;
}

/* Detect if the word under the cursor is one half of a hyphen-split word */
static int thes_hyphen_split_find(Ed *ed, EdInfo *info, const wchar_t *line, int line_len, int ws, int we, ThesHyphenSplit *hs)
{
    int word_len = we - ws;
    int hyphen_col = -1;
    int first_word_len = 0;

    if (!ed || !info || !line || !hs)
        return 0;

    if (word_len <= 0 || word_len >= 128)
        return 0;

    memset(hs, 0, sizeof(*hs));

    /* The word runs to the end of a line the layout broke inside a word: there is no hyphen in the text, the break kind is what says the word continues */
    if (we == line_len && word_len > 0 && ed_line_break(ed, info->row) == LB_HYPHEN && info->row + 1 < info->line_count)
    {
        hyphen_col = we;
        first_word_len = word_len;
    }

    if (hyphen_col >= 0 && first_word_len > 0)
    {
        const wchar_t *next_line = ed_line_wcs(ed, info->row + 1);

        if (next_line)
        {
            int next_len = ed_line_len(ed, info->row + 1);
            int next_end = 0;

            while (next_end < next_len && thes_is_word_char(next_line[next_end]))
                next_end++;

            /* Continuation must start lowercase */
            if (next_end > 0 && iswlower(next_line[0]))
            {
                int i, j;

                hs->first_row = info->row;
                hs->first_start = ws;
                hs->first_end = hyphen_col;
                hs->second_row = info->row + 1;
                hs->second_start = 0;
                hs->second_end = next_end;

                for (i = 0; i < first_word_len && hs->joined_len < 254; i++)
                    hs->joined[hs->joined_len++] = line[ws + i];

                for (j = 0; j < next_end && hs->joined_len < 254; j++)
                    hs->joined[hs->joined_len++] = next_line[j];

                hs->joined[hs->joined_len] = L'\0';

                return 1;
            }
        }

        return 0;
    }

    /* Word at column 0 preceded by "word-" on previous line */
    if (ws == 0 && info->row > 0 && we > 0 && iswlower(line[0]))
    {
        const wchar_t *prev_line = ed_line_wcs(ed, info->row - 1);

        if (prev_line)
        {
            int prev_len = ed_line_len(ed, info->row - 1);

            if (prev_len >= 1 && ed_line_break(ed, info->row - 1) == LB_HYPHEN)
            {
                int prev_word_end = prev_len;
                int prev_word_start = prev_word_end;

                while (prev_word_start > 0 && thes_is_word_char(prev_line[prev_word_start - 1]))
                    prev_word_start--;

                if (prev_word_end > prev_word_start)
                {
                    int i, j;

                    hs->first_row = info->row - 1;
                    hs->first_start = prev_word_start;
                    hs->first_end = prev_word_end;
                    hs->second_row = info->row;
                    hs->second_start = ws;
                    hs->second_end = we;

                    for (j = prev_word_start; j < prev_word_end && hs->joined_len < 254; j++)
                        hs->joined[hs->joined_len++] = prev_line[j];

                    for (i = 0; i < word_len && hs->joined_len < 254; i++)
                        hs->joined[hs->joined_len++] = line[ws + i];

                    hs->joined[hs->joined_len] = L'\0';

                    return 1;
                }
            }
        }
    }

    return 0;
}

int ui_thes_load_from_config(UI_APP_T *app)
{
    char idx_path[512];
    char dat_path[512];
    const char *base_path = NULL;
    const char *base_name = NULL;

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
    Ed *ed = NULL;
    EdInfo info;
    const wchar_t *line = NULL;
    int line_len, ws, we, i;
    wchar_t wbuf[128];
    char *u8 = NULL;
    ThesMeaning *meanings = NULL;
    int nmeanings, total_items, idx, sel;
    char **items = NULL;
    int *item_to_syn = NULL; /* Maps popup index -> (meaning, syn) pair */
    int m, k;
    const char *thes_encoding = NULL;
    char utf8_buf[1024];
    char defn_utf8_buf[1024];
    ThesHyphenSplit hs;
    int is_hyphen_split = 0;
    char *joined_u8 = NULL;
    const char *check_u8 = NULL;

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

    check_u8 = u8;

    /* Join hyphen-split halves for thesaurus lookup */
    if (thes_hyphen_split_find(ed, &info, line, line_len, ws, we, &hs))
    {
        joined_u8 = wcs_to_utf8(hs.joined, hs.joined_len);

        if (joined_u8)
        {
            is_hyphen_split = 1;
            check_u8 = joined_u8;
        }
    }

    nmeanings = thes_lookup(UI_APP_THES_HANDLE(app), check_u8, &meanings);

    if (nmeanings <= 0 || !meanings)
    {
        UI_APP_STATUS_FN(app, "No synonyms for '%s'", check_u8);

        free(joined_u8);
        free(u8);

        return 0;
    }

    /* We no longer need the joined lookup string; thesaurus owns meanings now */
    free(joined_u8);

    joined_u8 = NULL;

    /* Flatten: count total non-NULL synonyms across all meanings */
    total_items = 0;

    for (i = 0; i < nmeanings; i++)
    {
        int kk;

        for (kk = 0; kk < meanings[i].nsyns; kk++)
        {
            if (meanings[i].syns[kk])
                total_items++;
        }
    }

    if (total_items == 0)
    {
        thes_free_meanings(UI_APP_THES_HANDLE(app), meanings, nmeanings);
        UI_APP_STATUS_FN(app, "No synonyms for '%s'", check_u8);

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
            const char *defn = NULL;
            const char *syn_utf8 = NULL;
            const char *defn_utf8 = NULL;

            if (!meanings[m].syns[k])
                continue;

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
            else
            {
                items[idx] = (char *)malloc(2);

                if (items[idx])
                    strcpy(items[idx], "?");
            }

            item_to_syn[idx * 2 + 0] = m;
            item_to_syn[idx * 2 + 1] = k;

            idx++;
        }
    }

    sel = ui_popup_list("Synonyms", (const char **)items, idx, 0);

    if (sel >= 0 && sel < idx)
    {
        int sm = item_to_syn[sel * 2 + 0];
        int sk = item_to_syn[sel * 2 + 1];
        const char *chosen_orig = meanings[sm].syns[sk];
        const char *thes_enc = thes_get_encoding(UI_APP_THES_HANDLE(app));
        char chosen_utf8[1024];
        const char *chosen = NULL;
        wchar_t *wsyn = NULL;
        int wlen;

        if (chosen_orig)
        {
            if (thes_enc && thes_enc[0] && strcmp(thes_enc, "UTF-8") != 0)
            {
                chosen_utf8[0] = '\0';

                charset_to_utf8(thes_enc, chosen_orig, (int)strlen(chosen_orig), chosen_utf8, sizeof(chosen_utf8));
                chosen = chosen_utf8;
            }
            else
            {
                chosen = chosen_orig;
            }

            wsyn = utf8_to_wcs(chosen, &wlen);

            if (wsyn)
            {
                if (is_hyphen_split)
                {
                    if (ed->hard_wrap)
                        ed_auto_rewrap_capture_pre_snapshot(ed);
                    else
                    {
                        undo_abort(ed);
                        undo_begin(ed, hs.first_row, 2);
                    }

                    ed->undo_snapshot_mode = 1;
                    ed_set_pos(ed, hs.second_row, hs.second_start);

                    for (i = hs.second_start; i < hs.second_end; i++)
                        ed_delete(ed);

                    ed_set_pos(ed, hs.first_row, hs.first_start);

                    for (i = hs.first_start; i < hs.first_end; i++)
                        ed_delete(ed);

                    for (i = 0; i < wlen; i++)
                        ed_insert_char(ed, wsyn[i]);

                    ed_set_pos(ed, hs.first_row, hs.first_start + wlen);
                    ed_delete(ed);

                    if (ed->hard_wrap)
                        ed_auto_rewrap_after_edit(app);
                    else
                    {
                        ed->undo_snapshot_mode = 0;
                        undo_commit(ed, 1);
                        ed_save_undo(ed);
                    }
                }
                else
                {
                    if (ed->hard_wrap)
                    {
                        /* Capture paragraph, replace silently, reflow commits */
                        ed_auto_rewrap_capture_pre_snapshot(ed);

                        ed->undo_snapshot_mode = 1;

                        ed_set_pos(ed, info.row, ws);

                        for (i = 0; i < (we - ws); i++)
                            ed_delete(ed);

                        for (i = 0; i < wlen; i++)
                            ed_insert_char(ed, wsyn[i]);

                        ed_auto_rewrap_after_edit(app);
                    }
                    else
                    {
                        /* Single-row replacement, one undo entry */
                        ed_replace_word_with_undo(ed, info.row, ws, we, wsyn, wlen);
                    }
                }

                free(wsyn);

                UI_APP_STATUS_FN(app, "Replaced with '%s'", chosen);
            }
        }
    }

    /* Tear-down: free EVERYTHING we allocated */
    for (i = 0; i < idx; i++)
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
