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

#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <ctype.h>
#include "../core/utf8.h"
#include "../core/charset.h"
#include "../core/clipboard.h"
#include "../core/portable.h"
#include "../components/editor.h"
#include "../components/ed_attr.h"
#include "te.h"
#include "ui_files.h"
#include "../components/fmt_rtf.h"
#include "../components/fmt_wp4.h"
#include "../components/fmt_pdf.h"
#include "ui_editor_helper.h"
#include "ui_view.h"
#include "ui_keys.h"

static const char *HELP_LINES[] =
    {
        "tinyedit -- Key Bindings",
        "",
        "  Navigation:",
        "    Arrows           Move cursor",
        "    Home / Ctrl+B    Line start",
        "    End / Ctrl+E     Line end",
        "    PgUp / Ctrl+U    Page up",
        "    PgDn / Ctrl+D    Page down",
        "    Ctrl+Left/Right  Word left / right",
        "    Ctrl+G           Go to start of document",
        "    Ctrl+K           Go to end of document",
        "",
        "  Editing:",
        "    Ctrl+Y           Delete line",
        "    Ctrl+Z           Undo",
        "    Alt+Z            Redo",
        "    Ins / Alt+I      Toggle insert / overwrite",
        "    Ctrl+W           Rewrap paragraph",
        "    Tab              Insert tab",
        "    Alt+Q            Toggle wrap mode",
        "    Alt+D            Toggle line numbers",
        "    Alt+E            Toggle hyphen wrap",
        "    F3 / Alt+C       Choose output charset",
        "",
        "  Block (selection):",
        "    Ctrl+C           Copy block",
        "    Ctrl+X           Cut block",
        "    Ctrl+V           Paste block (or clipboard)",
        "    BS / Del         Delete block (no clipboard)",
        "    Ctrl+O           Export block to file",
        "    Alt+V            Sort selected lines",
        "    Alt+X            Convert case of selected block",
        "",
        "  Search:",
        "    F5 / Alt+F       Search / Replace current",
        "    Ctrl+R           Find & replace",
        "    Alt+G            Go to line",
        "    F3 / Alt+C       Prev",
        "    F4 / Alt+T       Next",
        "    F6 / Alt+B       Replace all (search mode)",
        "",
        "  Files:",
        "    F7 / Alt+O       Insert file at cursor",
        "    Ctrl+L           Open file (new tab)",
        "    Ctrl+Alt+Y       Open recent file",
        "    Ctrl+N           New file (new tab)",
        "    F2 / Ctrl+S      Save",
        "    ESC / F10        Quit (confirm if modified)",
        "    F1 / Alt+Y       This help",
        "",
        "  Tabs:",
        "    F9 / Alt+L       Toggle tabs panel",
        "    Alt+J            Next tab",
        "    Alt+K            Previous tab",
        "    Alt+W            Close current tab",
#ifdef HAVE_HUNSPELL
        "",
        "  Spell checker:",
        "    Alt+S            Toggle spell/dict panel",
        "    Alt+H            Toggle spell checker",
        "    Alt+P            Spell check word under cursor",
#ifdef HAVE_MYTHES
        "    Alt+A            Thesaurus lookup for word under cursor",
#endif

#ifdef HAVE_HYPHEN
        "    Alt+E            Toggle hyphen wrap",
#endif

#endif /* HAVE_HUNSPELL */

#ifdef HAVE_TRANSLATE
        "",
        "  Translate:",
        "    Alt+R            Translate selected text",
        "    Alt+M            Dictionary popup (pick translation)",
        "    Alt+N            Reverse lookup (scan dict)",
        "    Ctrl+T           Toggle translator",
        "    Alt+B            Exchange languages",
        "    Alt+D            Toggle line numbers / dict panel",
#endif
#ifdef HAVE_TTS
        "",
        "  Text-to-speech:",
#ifdef PLATFORM_AMIGA
        "    Alt+Shift+X     Speak selection / paragraph",
        "    Alt+Shift+K     Speak entire document",
        "    Alt+Shift+P     Pause / resume speech",
        "    Alt+Shift+O     Stop speech",
        "    Alt+Shift+V     Voice settings popup",
#else
        "    Ctrl+Alt+X      Speak selection / paragraph",
        "    Ctrl+Alt+K      Speak entire document",
        "    Ctrl+Alt+P      Pause / resume speech",
        "    Ctrl+Alt+O      Stop speech",
        "    Ctrl+Alt+V      Voice settings popup",
#endif
#endif
        "",
        "  Rich text (WP4/RTF mode):",
#ifdef PLATFORM_AMIGA
        "    Alt+Shift+B      Bold",
        "    Alt+Shift+I      Italic",
        "    Alt+Shift+U      Underline",
        "    Alt+Shift+L      Align left",
        "    Alt+Shift+E      Align center",
        "    Alt+Shift+R      Align right",
        "    Alt+Shift+J      Justify",
#else
        "    Ctrl+Alt+B       Bold",
        "    Ctrl+Alt+I       Italic",
        "    Ctrl+Alt+U       Underline",
        "    Ctrl+Alt+L       Align left",
        "    Ctrl+Alt+E       Align center",
        "    Ctrl+Alt+R       Align right",
        "    Ctrl+Alt+J       Justify",
#endif
        "",
        "  Other:",
        "    F4 / Alt+T       Setup / configuration",
        "    Alt+U            Unicode glyph picker",
#ifdef PLATFORM_AMIGA
        "    Alt+Shift+S      Cycle syntax language",
#else
        "    Ctrl+Alt+S       Cycle syntax language",
#endif
};

#define HELP_N ((int)(sizeof(HELP_LINES) / sizeof(HELP_LINES[0])))
#define INSIDE_BLOCK_COMMENT_LOOKUP_LIMIT 256

#include "ui_setup.h"

#include "ui_syntax.h"
#include "ui_spell.h"
#include "ui_grammar.h"
#include "ui_dict.h"
#include "ui_dict_picker.h"
#include "ui_dict_reverse.h"
#include "ui_glyph_picker.h"
#include "ui_mouse.h"
#include "ui_assist.h"

#ifdef HAVE_TRANSLATE
#include "ui_translate.h"
#endif

#ifdef HAVE_TTS
#include "ui_tts.h"
#endif

#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
#include "ui_hyph.h"
#endif

#if defined(HAVE_HUNSPELL) && defined(HAVE_MYTHES)
#include "ui_thes.h"
#endif

#define SYNTAX_CACHE_STEP 2048

static void editor_insert_cp(Ed *ed, unsigned long cp)
{
    if (cp < 0x10000)
    {
        ed_insert_char(ed, (wchar_t)cp);
    }
    else
    {
#ifdef PLATFORM_WIN32
        unsigned long v = cp - 0x10000;

        ed_insert_char(ed, (wchar_t)(0xD800 + (v >> 10)));
        ed_insert_char(ed, (wchar_t)(0xDC00 + (v & 0x3FF)));
#else
        ed_insert_char(ed, (wchar_t)cp);
#endif
    }
}

static void cycle_syntax_lang(TeApp *app)
{
    static const char *names[] = {"Auto", "C", "C++", "x86 asm", "m68k asm", "Amiga C"};
    TeTab *tab = te_app_get_active_tab(app);

    if (!tab)
        return;

    tab->syntax_lang++;

    if (tab->syntax_lang >= SYNTAX_LANG_COUNT)
        tab->syntax_lang = SYNTAX_LANG_NONE;

    te_status(app, "Syntax: %s", names[tab->syntax_lang + 1]);

    ui_editor_draw_body(app);
    te_draw_statusbar(app);
    doupdate();
}

int do_save(TeApp *app)
{
    int r = 0;
    char dir_input[1024];
    char name_input[1024];
    char filename_buf[TAB_FILENAME_MAX];
    const char *last_slash = NULL;

    /* Extract directory and name from current filename */
    if (te_app_get_filename(app)[0])
    {
        last_slash = strrchr(te_app_get_filename(app), '/');

        if (!last_slash)
            last_slash = strrchr(te_app_get_filename(app), '\\');

        if (last_slash)
        {
            int dir_len = (int)(last_slash - te_app_get_filename(app));

            strncpy(dir_input, te_app_get_filename(app), dir_len);
            dir_input[dir_len] = '\0';

            strncpy(name_input, last_slash + 1, sizeof(name_input) - 1);
            name_input[sizeof(name_input) - 1] = '\0';
        }
        else
        {
            dir_input[0] = '\0';
            strncpy(name_input, te_app_get_filename(app), sizeof(name_input) - 1);
            name_input[sizeof(name_input) - 1] = '\0';
        }
    }
    else
    {
        dir_input[0] = '\0';
        name_input[0] = '\0';
    }

    /* Always show save dialog */
    strncpy(filename_buf, te_app_get_filename(app), sizeof(filename_buf) - 1);
    filename_buf[sizeof(filename_buf) - 1] = '\0';

    if (ui_files_save("Save as", dir_input, name_input, filename_buf, sizeof(filename_buf)) != 0)
        return -1;

    if (!filename_buf[0])
        return -1;

    te_app_set_filename(app, filename_buf);

    /* RTF filenames save through the RTF exporter, keeping the styles */
    if (ui_files_is_rtf(te_app_get_filename(app)))
    {
        FILE *fp = fopen(te_app_get_filename(app), "wb");
        int rc = -1;

        if (fp)
        {
            rc = rtf_export(te_app_get_editor(app), fp);
            fclose(fp);
        }

        if (rc != 0)
        {
            te_status(app, "Cannot write: %s", te_app_get_filename(app));
            return -1;
        }

        app->rich_mode = 1;

        if (te_app_get_active_tab(app))
            te_app_get_active_tab(app)->rich_mode = 1;
    }
    else if (ui_files_is_wp4(te_app_get_filename(app)))
    {
        FILE *fp = NULL;
        char werr[128];
        char wwarn[128];
        int rc = -1;

        werr[0] = '\0';
        wwarn[0] = '\0';

        /* WP 4.2 is an 8-bit format; refuse before truncating the file */
        if (charset_bits(app->charset_out) != 8)
        {
            snprintf(werr, sizeof(werr), "charset %s is not an 8-bit charset; WP 4.2 requires an 8-bit charset", app->charset_out[0] ? app->charset_out : "(none)");
            te_status(app, "WP error: %s", werr);
            return -1;
        }

        fp = fopen(te_app_get_filename(app), "wb");

        if (fp)
        {
            rc = wp4_export(te_app_get_editor(app), fp, app->charset_out, werr, sizeof(werr), wwarn, sizeof(wwarn));
            fclose(fp);
        }

        if (rc != 0)
        {
            te_status(app, "WP error: %s", werr[0] ? werr : "cannot write");
            return -1;
        }

        app->rich_mode = 1;

        if (te_app_get_active_tab(app))
            te_app_get_active_tab(app)->rich_mode = 1;

        if (wwarn[0])
            te_status(app, "Saved: %s (%s)", te_app_get_filename(app), wwarn);
    }
    else if (ui_files_is_pdf(te_app_get_filename(app)))
    {
        /* PDF export: no round-trip charset handled inside pdf_export */
        FILE *fp = NULL;
        char perr[128];
        char pwarn[128];
        int rc = -1;

        perr[0] = '\0';
        pwarn[0] = '\0';

        fp = fopen(te_app_get_filename(app), "wb");

        if (fp)
        {
            rc = pdf_export(te_app_get_editor(app), fp, &app->cfg, perr, sizeof(perr), pwarn, sizeof(pwarn));
            fclose(fp);
        }

        if (rc != 0)
        {
            te_status(app, "PDF error: %s", perr[0] ? perr : "cannot write");
            return -1;
        }

        if (pwarn[0])
            te_status(app, "Saved: %s (%s)", te_app_get_filename(app), pwarn);
    }
    else if (ed_save_to_file(te_app_get_editor(app), te_app_get_filename(app), app->charset_out) != 0)
    {
        te_status(app, "Cannot write: %s", te_app_get_filename(app));
        return -1;
    }

    /* Clear modified flag */
    ed_set_modified(te_app_get_editor(app), 0);

    /* Swap is no longer needed after a successful save */
    ui_editor_swp_remove(te_app_get_filename(app));

    ui_editor_recent_add(te_app_get_filename(app));

    te_status(app, "Saved: %s", te_app_get_filename(app));

#ifdef HAVE_HUNSPELL
    /* If the user edited the custom dictionary file directly, reload it so new words are recognized immediately */
    if (app->cfg.spell_custom_dict[0] && strcmp(te_app_get_filename(app), app->cfg.spell_custom_dict) == 0)
        spell_load_from_config(app);
#endif

    return r;
}

static int inside_block_comment(Ed *ed, int row, int col)
{
    int r;
    int needed = 1;

    int r_min = row - INSIDE_BLOCK_COMMENT_LOOKUP_LIMIT;

    if (r_min < 0)
        r_min = 0;

    for (r = row; r >= r_min; r--)
    {
        const wchar_t *rl = ed_line_wcs(ed, r);
        int rl_len = ed_line_len(ed, r);
        int c;
        int start;

        if (!rl)
            continue;

        start = (r == row) ? col - 1 : rl_len - 1;

        for (c = start; c >= 0; c--)
        {
            if (c + 1 >= rl_len)
                continue;

            if (rl[c] == L'*' && rl[c + 1] == L'/')
                needed++;
            else if (rl[c] == L'/' && rl[c + 1] == L'*')
            {
                /* The current row's opener must be fully before the cursor */
                if (r == row && c + 1 >= col)
                    continue;

                needed--;

                if (needed <= 0)
                    return 1;
            }
        }
    }

    return 0;
}

static int insert_block_comment_prefix(Ed *ed, const wchar_t *prev_line, int prev_llen, int prev_col, int skip_leading_ws)
{
    int leading_ws = 0;
    int k;
    int line_starts_with_opener = 0;
    int line_has_star_prefix = 0;
    int line_has_closer = 0;

    if (!prev_line || prev_llen <= 0)
        return 0;

    for (k = 0; k < prev_llen; k++)
    {
        if (prev_line[k] == L' ' || prev_line[k] == L'\t')
            leading_ws++;
        else
            break;
    }

    if (leading_ws + 1 < prev_llen && prev_line[leading_ws] == L'/' && prev_line[leading_ws + 1] == L'*')
        line_starts_with_opener = 1;

    if (leading_ws < prev_llen && prev_line[leading_ws] == L'*')
        line_has_star_prefix = 1;

    if (prev_llen >= 2 && prev_line[prev_llen - 2] == L'*' && prev_line[prev_llen - 1] == L'/')
        line_has_closer = 1;

    if (line_has_closer)
        return 0;

    if (!line_starts_with_opener && !line_has_star_prefix)
        return 0;

    if (!skip_leading_ws)
    {
        for (k = 0; k < leading_ws; k++)
            ed_insert_char(ed, L' ');
    }

    if (line_starts_with_opener)
        ed_insert_char(ed, L' ');

    ed_insert_char(ed, L'*');
    ed_insert_char(ed, L' ');

    return 1;
}

static void do_smart_enter(TeApp *app)
{
    Ed *ed = te_app_get_editor(app);
    EdInfo si_info;
    const wchar_t *si_line = NULL;
    int si_llen;
    wchar_t indent[64];
    int indent_n = 0;
    int k;
    int k2;

    ed_get_info(ed, &si_info);

    si_line = ed_line_wcs(ed, si_info.row);
    si_llen = ed_line_len(ed, si_info.row);

    if (app->cfg.smart_indent)
    {
        if (si_line)
        {
            for (k = 0; k < si_llen && k < (int)(sizeof(indent) / sizeof(wchar_t)) - 1; k++)
            {
                if (si_line[k] == L' ' || si_line[k] == L'\t')
                    indent[indent_n++] = si_line[k];
                else
                    break;
            }
        }

        ed_enter(ed);

        for (k2 = 0; k2 < indent_n; k2++)
            ed_insert_char(ed, indent[k2]);
    }
    else
    {
        ed_enter(ed);
    }

    /* Continue C-style block comments */
    if (inside_block_comment(ed, si_info.row, si_info.col))
        insert_block_comment_prefix(ed, si_line, si_llen, si_info.col, app->cfg.smart_indent);
}

int handle_function_keys(TeApp *app, int ch, int is_key)
{
    /* F1 / ? : help */
    if ((is_key && ch == KEY_F(1)) || (is_key && ch == KEY_ALT('Y')))
    {
        ui_popup_help("Help", HELP_LINES, HELP_N);
        return 1;
    }

    /* F9 : toggle tabs panel */
    if (is_key && ch == KEY_F(9))
    {
        app->show_tabs = !app->show_tabs;
        return 1;
    }

    /* F2 / Ctrl+S : save */
    if ((is_key && ch == KEY_F(2)) || (!is_key && ch == CTRL('S')))
    {
        do_save(app);
        return 1;
    }

    /* F3 / Alt+C : charset or Previous match */
    if ((is_key && ch == KEY_F(3)) || (ch == KEY_ALT('C')))
    {
        if (app->search.only_mode || (app->search.is_mode && app->search.count > 0))
            return search_prev(app);
        else
            return charset_select(app);
    }

    /* F4 / Alt+T : Setup or Next match */
    if ((is_key && ch == KEY_F(4)) || (ch == KEY_ALT('T')))
    {
        if (app->search.only_mode || (app->search.is_mode && app->search.count > 0))
        {
            return search_next(app);
        }
        else
        {
            /* Normal setup functionality */
            char old_charset_in[TE_CFG_STR_MAX];
            char old_charset_out[TE_CFG_STR_MAX];
            int rewrap_doc;
            TeTab *tab = NULL;

            strncpy(old_charset_in, app->cfg.charset_in, sizeof(old_charset_in) - 1);
            old_charset_in[sizeof(old_charset_in) - 1] = '\0';

            strncpy(old_charset_out, app->cfg.charset_out, sizeof(old_charset_out) - 1);
            old_charset_out[sizeof(old_charset_out) - 1] = '\0';

            if (ui_setup_run(app, &app->cfg, app->cfg_path) == 1)
            {
                /* Config saved: apply changes */
                if (app->cfg.undo_levels > 0)
                    ed_set_undo_levels(te_app_get_editor(app), app->cfg.undo_levels);

                /* Refit when hard is on and the mode or column changed */
                rewrap_doc = app->cfg.hard_wrap && (!app->hard_wrap || app->wrap_col != app->cfg.autowrap_col);

                app->hard_wrap = app->cfg.hard_wrap;

                ed_set_hard_wrap(te_app_get_editor(app), app->cfg.hard_wrap);

                app->wrap_col = app->cfg.autowrap_col;

                if (rewrap_doc)
                    ui_editor_rewrap_docwide(app);

                te_app_set_show_line_numbers(app, app->cfg.show_line_numbers);

                /* If charset_in changed in setup, update charset_in */
                if (strcasecmp(old_charset_in, app->cfg.charset_in) != 0)
                {
                    strncpy(app->charset_in, app->cfg.charset_in, sizeof(app->charset_in) - 1);
                    app->charset_in[sizeof(app->charset_in) - 1] = '\0';
                }

                /* If charset_out changed in setup, update charset_out */
                if (strcasecmp(old_charset_out, app->cfg.charset_out) != 0)
                {
                    strncpy(app->charset_out, app->cfg.charset_out, sizeof(app->charset_out) - 1);
                    app->charset_out[sizeof(app->charset_out) - 1] = '\0';
                }

                /* Update active tab charset values for status bar display */
                tab = te_app_get_active_tab(app);

                if (tab)
                {
                    strncpy(tab->charset_in, app->charset_in, sizeof(tab->charset_in) - 1);
                    tab->charset_in[sizeof(tab->charset_in) - 1] = '\0';

                    strncpy(tab->charset_out, app->charset_out, sizeof(tab->charset_out) - 1);
                    tab->charset_out[sizeof(tab->charset_out) - 1] = '\0';
                }

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
                /* Reconfigure mouse if setting changed */
                if (app->cfg.mouse_enabled)
                {
                    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
                    mouseinterval(0);
                    printf("\033[?1002h");
                    printf("\033[?1006h");
                    fflush(stdout);
                }
                else
                {
                    mousemask(0, NULL);
                    printf("\033[?1002l");
                    printf("\033[?1006l");
                    fflush(stdout);
                }
#endif

                te_init_colors(&app->cfg);
            }

            return 1;
        }
    }

    /* F5 / Alt+F : search */
    if ((is_key && ch == KEY_F(5)) || (ch == KEY_ALT('F')))
    {
        if (app->search.is_mode && app->search.count > 0)
        {
            return replace_current(app);
        }
        else if (!app->search.only_mode && !app->search.is_mode)
            return do_search(app);
    }

    /* F6 / Alt+B : Replace All in search mode, or swap translate from/to */
    if ((is_key && ch == KEY_F(6)) || (ch == KEY_ALT('B')))
    {
        if (app->search.is_mode && app->search.count > 0)
            return replace_all(app);
#ifdef HAVE_TRANSLATE
        else
        {
            char tmp[16];

            snprintf(tmp, sizeof(tmp), "%s", app->cfg.translate_from_lang);

            snprintf(app->cfg.translate_from_lang, sizeof(app->cfg.translate_from_lang), "%s", app->cfg.translate_to_lang);

            snprintf(app->cfg.translate_to_lang, sizeof(app->cfg.translate_to_lang), "%s", tmp);

            te_status(app, "Swapped: %s <-> %s", app->cfg.translate_from_lang, app->cfg.translate_to_lang);
            return 1;
        }
#endif
    }

    /* F7 / Alt+O : insert file */
    if ((is_key && ch == KEY_F(7)) || (ch == KEY_ALT('O')))
        return insert_file(app);

    /* F8 / Alt+U : Unicode glyph picker */
    if ((is_key && ch == KEY_F(8)) || (ch == KEY_ALT('U')))
    {
        long cp = ui_glyph_pick();

        if (cp >= 0)
        {
            ed_block_clear(te_app_get_editor(app));
            editor_insert_cp(te_app_get_editor(app), (unsigned long)cp);

            clear_search_highlights(app);
        }

        return 1;
    }

#ifdef HAVE_TRANSLATE
    /* Alt+R : translate selected text */
    if (ch == KEY_ALT('R'))
    {
        ui_translate_action(app);
        return 1;
    }

#ifdef HAVE_TRANSLATE_STARDICT
    if (ch == KEY_ALT('M'))
    {
        ui_dict_picker(app);
        return 1;
    }

    /* Alt+N: reverse dictionary lookup */
    if (ch == KEY_ALT('N'))
    {
        if (app->spell_panel_mode == 2)
        {
            ui_dict_reverse(app);
            return 1;
        }
        else
        {
            return do_search_in_files(app);
        }
    }

#endif
#endif

    /* Alt+V : sort selected lines alphabetically (case-insensitive) */
    if (ch == KEY_ALT('V'))
    {
        Ed *ed = te_app_get_editor(app);

        if (!ed || !ed->block.active)
        {
            te_status(app, "Sort: no block selected (use Shift+arrows)");
            return 1;
        }

        if (ed_sort_block_lines(ed) == 0)
        {
            te_status(app, "Lines sorted");
            ed_block_clear(ed);
        }
        else
        {
            te_status(app, "Sort: needs 2+ lines in the block");
        }

        return 1;
    }

    /* Alt+X : convert case (popup: U=UPPER / L=lower / T=Title) */
    if (ch == KEY_ALT('X'))
    {
        Ed *ed = te_app_get_editor(app);
        const char *items[3];
        int choice;

        if (!ed || !ed->block.active)
        {
            te_status(app, "Convert case: no block selected");
            return 1;
        }

        items[0] = "UPPER CASE";
        items[1] = "lower case";
        items[2] = "Title Case";

        choice = ui_popup_list("Convert case", items, 3, 0);

        if (choice < 0 || choice > 2)
            return 1;

        if (ed_convert_block_case(ed, choice) == 0)
            te_status(app, "Case converted");
        else
            te_status(app, "Convert case: error");

        return 1;
    }

    /* Ctrl+Alt+Y : open recent files */
    if (is_key && ch == KEY_ALT_CTRL('Y'))
    {
        ui_editor_recent_open(app);

        return 1;
    }

#ifdef PLATFORM_AMIGA
    /* Alt+Shift+S : cycle syntax language (Ctrl+Alt+S is used by AmigaOS) */
    if (is_key && ch == KEY_SHIFT('S'))
#else
    /* Ctrl+Alt+S : cycle syntax language */
    if (is_key && ch == KEY_ALT_CTRL('S'))
#endif
    {
        cycle_syntax_lang(app);
        return 1;
    }

    /* Rich-text: bold/italic/underline/alignment -- only in rich_mode */
    if (app->rich_mode)
    {
#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('B'))
#else
        if (is_key && ch == KEY_ALT_CTRL('B'))
#endif
        {
            ui_rich_attr_toggle(app, EA_BOLD);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('I'))
#else
        if (is_key && ch == KEY_ALT_CTRL('I'))
#endif
        {
            ui_rich_attr_toggle(app, EA_ITALIC);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('U'))
#else
        if (is_key && ch == KEY_ALT_CTRL('U'))
#endif
        {
            ui_rich_attr_toggle(app, EA_UNDERLINE);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('L'))
#else
        if (is_key && ch == KEY_ALT_CTRL('L'))
#endif
        {
            ui_rich_align_set(app, EA_ALIGN_LEFT);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('E'))
#else
        if (is_key && ch == KEY_ALT_CTRL('E'))
#endif
        {
            ui_rich_align_set(app, EA_ALIGN_CENTER);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('R'))
#else
        if (is_key && ch == KEY_ALT_CTRL('R'))
#endif
        {
            ui_rich_align_set(app, EA_ALIGN_RIGHT);
            return 1;
        }

#ifdef PLATFORM_AMIGA
        if (is_key && ch == KEY_SHIFT('J'))
#else
        if (is_key && ch == KEY_ALT_CTRL('J'))
#endif
        {
            ui_rich_align_set(app, EA_ALIGN_JUST);
            return 1;
        }
    }

#ifdef HAVE_TTS
    /* TTS shortcuts: Alt+Shift+X (Amiga) / Ctrl+Alt+X (rest) speak, Ctrl+Alt+P/O/V pause/stop/popup */
#ifdef PLATFORM_AMIGA
    if (is_key && ch == KEY_SHIFT('X'))
#else
    if (is_key && ch == KEY_ALT_CTRL('X'))
#endif
    {
        ui_tts_speak_action(app);
        return 1;
    }

    /* Whole-document dictation from line 0, Amiga: Alt+Shift+K, else: Ctrl+Alt+K */
#ifdef PLATFORM_AMIGA
    if (is_key && ch == KEY_SHIFT('K'))
#else
    if (is_key && ch == KEY_ALT_CTRL('K'))
#endif
    {
        ui_tts_speak_doc_action(app);
        return 1;
    }

#ifdef PLATFORM_AMIGA
    if (is_key && ch == KEY_SHIFT('P'))
#else
    if (is_key && ch == KEY_ALT_CTRL('P'))
#endif
    {
        ui_tts_pause_toggle(app);
        return 1;
    }

#ifdef PLATFORM_AMIGA
    if (is_key && ch == KEY_SHIFT('O'))
#else
    if (is_key && ch == KEY_ALT_CTRL('O'))
#endif
    {
        ui_tts_stop(app);
        return 1;
    }

#ifdef PLATFORM_AMIGA
    if (is_key && ch == KEY_SHIFT('V'))
#else
    if (is_key && ch == KEY_ALT_CTRL('V'))
#endif
    {
        ui_tts_popup(app);
        return 1;
    }
#endif /* HAVE_TTS */

    return 0;
}

int handle_control_keys(TeApp *app, int ch, int is_key)
{
    /* Ctrl+V : paste from internal buffer or system clipboard */
    if (!is_key && ch == CTRL('V'))
        return paste(app);

    /* Alt+G : goto line */
    if (ch == KEY_ALT('G'))
        return ui_editor_goto_line(app);

#ifdef HAVE_HUNSPELL
    /* Alt+P : spell check word under cursor */
    if (ch == KEY_ALT('P'))
    {
        spell_check_word(app);
        return 1; /* Force redraw */
    }

    /* Alt+H : toggle spell checker */
    if (ch == KEY_ALT('H'))
    {
        if (!app->spell_handle && app->cfg.spell_enabled && app->cfg.spell_dict_path[0] && app->cfg.spell_dict_name[0])
            spell_load_from_config(app);

        if (app->spell_handle)
        {
            app->spell_active = !app->spell_active;
            te_status(app, "Spell checker %s", app->spell_active ? "enabled" : "disabled");
        }
        else if (!app->cfg.spell_enabled)
        {
            te_status(app, "Spell disabled in config (enable in Setup F2)");
        }
        else if (!app->cfg.spell_dict_path[0] || !app->cfg.spell_dict_name[0])
        {
            te_status(app, "Set dict path and name in Setup F2");
        }
        else
        {
            te_status(app, "Cannot load %s/%s.aff ", app->cfg.spell_dict_path, app->cfg.spell_dict_name);
        }

        return 1;
    }
#endif /* HAVE_HUNSPELL */

#ifdef HAVE_TRANSLATE
    /* Ctrl+T : toggle translator */
    if (!is_key && ch == CTRL('T'))
    {
        if (!app->translate_handle && app->cfg.translate_enabled)
            ui_translate_load_from_config(app);

        if (app->translate_handle)
        {
            app->translate_active = !app->translate_active;
            te_status(app, "Translator %s", app->translate_active ? "enabled" : "disabled");
        }
        else if (!app->cfg.translate_enabled)
        {
            te_status(app, "Translator disabled in config (enable in Setup F2)");
        }
        else
        {
            te_status(app, "Cannot load translator");
        }

        return 1;
    }
#endif

#if defined(HAVE_HUNSPELL) && defined(HAVE_MYTHES)
    /* Alt+A : thesaurus lookup for word under cursor */
    if (ch == KEY_ALT('A'))
    {
        ui_thes_lookup_word(app);
        return 1;
    }
#endif

#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
    /* Alt+E : toggle hyphen wrap */
    if (ch == KEY_ALT('E'))
    {
        if (!app->hyph_handle)
        {
            te_status(app, "No hyphenation dictionary loaded (configure HYPH_DICT_*)");
            return 1;
        }

        /*if (app->hard_wrap)
        {*/
        app->hyph_wrap_enabled = !app->hyph_wrap_enabled;
        te_status(app, "Hyphen wrap %s", app->hyph_wrap_enabled ? "ON" : "OFF");

        ed_auto_rewrap_after_edit(app);
        /*}
        else
        {
            te_status(app, "Hyphen wrap requires hard-wrap mode");
        }*/

        return 1;
    }
#endif

    /* Alt+J : next tab */
    if (ch == KEY_ALT('J'))
        return ui_tabs_switch_next(app);

    /* Alt+K : previous tab */
    if (ch == KEY_ALT('K'))
        return ui_tabs_switch_prev(app);

    /* Alt+S : toggle spell/translate panel */
    if (ch == KEY_ALT('S'))
        return toggle_spell_panel(app);

    /* Alt+W : close current tab */
    if (ch == KEY_ALT('W'))
    {
        TeTab *tab = NULL;
        EdInfo info;
        char closed_filename[TAB_FILENAME_MAX];

        tab = app->tabs[app->active_tab];
        closed_filename[0] = '\0';

        if (tab)
        {
            ed_get_info(tab->editor, &info);

            if (info.modified)
            {
                if (ui_popup_confirm("Close tab", "Tab has unsaved changes. Close anyway?") != 1)
                    return 1;
            }

            strncpy(closed_filename, tab->filename, sizeof(closed_filename) - 1);
            closed_filename[sizeof(closed_filename) - 1] = '\0';
        }

        te_app_close_tab(app, app->active_tab);

        if (closed_filename[0])
            ui_editor_swp_remove(closed_filename);

        /* If no tabs left, create a new empty tab */
        if (app->tab_count == 0)
        {
            TeTab *new_tab = te_tab_new();

            if (new_tab)
            {
                ed_set_word_move_mode(new_tab->editor, app->cfg.word_move_mode);
                te_app_add_tab(app, new_tab);
                te_app_switch_tab(app, 0);
            }
        }

        return 1;
    }

    /* Alt+L : toggle tabs panel */
    if (ch == KEY_ALT('L'))
    {
        app->show_tabs = !app->show_tabs;
        return 1;
    }

    /* Ctrl+R : find & replace */
    if (!is_key && ch == CTRL('R'))
        return replace(app);

    /* Ctrl+C : copy block */
    if (!is_key && ch == CTRL('C'))
        return copy(app);

    /* Ctrl+X : cut block */
    if (!is_key && ch == CTRL('X'))
        return cut(app);

    /* Ctrl+O : export block to file */
    if (!is_key && ch == CTRL('O'))
        return ui_editor_export(app);

    /* Ctrl+L : open file (create new tab) */
    if (!is_key && ch == CTRL('L'))
    {
        TeTab *new_tab = NULL;
        int result;

        new_tab = te_tab_new();

        if (!new_tab)
            return 1;

        ed_set_word_move_mode(new_tab->editor, app->cfg.word_move_mode);
        te_app_add_tab(app, new_tab);
        te_app_switch_tab(app, app->tab_count - 1);

        result = ui_files_open(app);

        if (result != 0)
        {
            /* User cancelled or error - close the tab we just created */
            te_app_close_tab(app, app->tab_count - 1);
            return result;
        }

        /* Re-apply configuration after loading file */
        ed_set_undo_levels(te_app_get_editor(app), app->cfg.undo_levels);
        ed_set_hard_wrap(te_app_get_editor(app), app->cfg.hard_wrap);
        ed_set_word_move_mode(te_app_get_editor(app), app->cfg.word_move_mode);

        return result;
    }

    /* Ctrl+N : new file (create new tab) */
    if (!is_key && ch == CTRL('N'))
    {
        TeTab *new_tab = NULL;

        new_tab = te_tab_new();

        if (!new_tab)
            return 1;

        ed_set_word_move_mode(new_tab->editor, app->cfg.word_move_mode);
        te_app_add_tab(app, new_tab);
        te_app_switch_tab(app, app->tab_count - 1);

        te_status(app, "[No Name]");

        return 1;
    }

    /* Ctrl+W : rewrap FTN reply quote block */
    if (!is_key && ch == CTRL('W'))
        return ftn_reply(app);

    /* go to start */
    if (!is_key && ch == CTRL('G'))
        return ui_editor_goto_start(app);

    /* go to end */
    if (!is_key && ch == CTRL('K'))
        return ui_editor_goto_end(app);

    return 0;
}

int handle_navigation_keys(TeApp *app, int ch, int soft, int width, int body_rows, int *preserve_desired)
{
    switch (ch)
    {
    case KEY_ALT_UP:
        if (ed_move_line_up(te_app_get_editor(app)) == 0)
            te_status(app, "Line moved up");
        else
            te_status(app, "Cannot move line up");
        return 1;

    case KEY_ALT_DOWN:
        if (ed_move_line_down(te_app_get_editor(app)) == 0)
            te_status(app, "Line moved down");
        else
            te_status(app, "Cannot move line down");
        return 1;

    case KEY_UP:
        ed_block_clear(te_app_get_editor(app));

        if (soft)
        {
            view_move_up(app, width);
            *preserve_desired = 1;
        }
        else
            ed_move_up(te_app_get_editor(app));

        return 1;

    case KEY_DOWN:
        ed_block_clear(te_app_get_editor(app));

        if (soft)
        {
            view_move_down(app, width);
            *preserve_desired = 1;
        }
        else
            ed_move_down(te_app_get_editor(app));

        return 1;

    case KEY_LEFT:
        ed_block_clear(te_app_get_editor(app));
        ed_move_left(te_app_get_editor(app));
        return 1;

    case KEY_RIGHT:
        ed_block_clear(te_app_get_editor(app));
        ed_move_right(te_app_get_editor(app));
        return 1;

    case KEY_HOME:
        ed_block_clear(te_app_get_editor(app));

        if (soft)
            view_move_home(app, width);
        else
            ed_move_home(te_app_get_editor(app));

        return 1;

    case KEY_END:
        ed_block_clear(te_app_get_editor(app));

        if (soft)
            view_move_end(app, width);
        else
            ed_move_end(te_app_get_editor(app));

        return 1;

    case KEY_PPAGE:
#ifdef HAVE_TRANSLATE_STARDICT
        if (app->spell_panel_mode == 2 && app->dict_result && app->dict_result[0])
        {
            int i;
            int rows = DICT_PANEL_ROWS;

            for (i = 0; i < rows; i++)
            {
                if (!ui_dict_scroll_up(app))
                    break;
            }

            return 1;
        }
#endif
        ed_block_clear(te_app_get_editor(app));

        if (soft)
        {
            view_move_pgup(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgup(te_app_get_editor(app), body_rows);

        return 1;

    case KEY_NPAGE:
#ifdef HAVE_TRANSLATE_STARDICT
        if (app->spell_panel_mode == 2 && app->dict_result && app->dict_result[0])
        {
            int i;
            int rows = DICT_PANEL_ROWS;

            for (i = 0; i < rows; i++)
            {
                if (!ui_dict_scroll_down(app))
                    break;
            }

            return 1;
        }
#endif
        ed_block_clear(te_app_get_editor(app));

        if (soft)
        {
            view_move_pgdn(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgdn(te_app_get_editor(app), body_rows);

        return 1;

    case KEY_ENTER:
        ed_block_clear(te_app_get_editor(app));
        do_smart_enter(app);
        clear_search_highlights(app);
        return 1;

    case KEY_BACKSPACE:
    {
        EdInfo i2;

        ed_get_info(te_app_get_editor(app), &i2);

        if (i2.block.active)
        {
            ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));
            ed_block_delete(te_app_get_editor(app));

            /* Deleted lines invalidate the paragraph breaks, the reflow rebuilds them */
            ed_auto_rewrap_after_edit(app);

            te_status(app, "Block deleted");
        }
        else
        {
            ed_block_clear(te_app_get_editor(app));

            ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));
            ed_backspace(te_app_get_editor(app));
            ed_auto_rewrap_after_edit(app);
        }

        clear_search_highlights(app);

        return 1;
    }

    case KEY_DC:
    {
        EdInfo i2;

        ed_get_info(te_app_get_editor(app), &i2);

        if (i2.block.active)
        {
            ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));
            ed_block_delete(te_app_get_editor(app));

            /* Deleted lines invalidate the paragraph breaks, the reflow rebuilds them */
            ed_auto_rewrap_after_edit(app);

            te_status(app, "Block deleted");
        }
        else
        {
            ed_block_clear(te_app_get_editor(app));

            ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));
            ed_delete(te_app_get_editor(app));
            ed_auto_rewrap_after_edit(app);
        }

        clear_search_highlights(app);

        return 1;
    }

    case KEY_IC: /* Ins */
    case KEY_ALT('I'):
        ed_toggle_insert(te_app_get_editor(app));
        return 1;

    case KEY_CLEFT:
        ed_block_clear(te_app_get_editor(app));
        ed_word_left(te_app_get_editor(app));
        return 1;

    case KEY_CRIGHT:
        ed_block_clear(te_app_get_editor(app));
        ed_word_right(te_app_get_editor(app));
        return 1;

    case KEY_CUP:
        ed_move_up(te_app_get_editor(app));
        return 1;

    case KEY_CDOWN:
        ed_move_down(te_app_get_editor(app));
        return 1;

    case KEY_CSLEFT:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_word_left(te_app_get_editor(app));
        return 1;

    case KEY_CSRIGHT:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_word_right(te_app_get_editor(app));
        return 1;

    case KEY_CSUP:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_up(te_app_get_editor(app));
        return 1;

    case KEY_CSDOWN:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_down(te_app_get_editor(app));
        return 1;

    case KEY_SPPAGE:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_pgup(te_app_get_editor(app), body_rows);
        return 1;

    case KEY_SNPAGE:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_pgdn(te_app_get_editor(app), body_rows);
        return 1;

    case KEY_CSUPD:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_pgdn(te_app_get_editor(app), body_rows);
        return 1;

    case KEY_CSDOWNU:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_pgup(te_app_get_editor(app), body_rows);
        return 1;

    case KEY_SLEFT:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_left(te_app_get_editor(app));
        return 1;

    case KEY_SRIGHT:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_right(te_app_get_editor(app));
        return 1;

    case KEY_SUP:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_up(te_app_get_editor(app));
        return 1;

    case KEY_SDOWN:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_down(te_app_get_editor(app));
        return 1;

    case KEY_SHOME:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_home(te_app_get_editor(app));
        return 1;

    case KEY_SEND:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_end(te_app_get_editor(app));
        return 1;

    case KEY_CSHOME:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_top(te_app_get_editor(app));
        return 1;

    case KEY_CSEND:
        if (!te_app_get_editor(app)->block.active)
            ed_block_anchor(te_app_get_editor(app));

        ed_move_bottom(te_app_get_editor(app));
        return 1;

    case KEY_ALT('Z'):
        ed_redo(te_app_get_editor(app));
        app->hard_wrap = ed_get_hard_wrap(te_app_get_editor(app));
        clear_search_highlights(app);

        return 1;

    case KEY_ALT('D'):
        /* If dictionary panel is visible, Alt+D hides it; otherwise toggle line numbers */
        if (app->spell_panel_mode == 2)
        {
            toggle_spell_panel(app);
            te_status(app, "Dictionary panel hidden");
            return 1;
        }

        te_app_set_show_line_numbers(app, !te_app_get_show_line_numbers(app));
        te_status(app, "Line numbers: %s", te_app_get_show_line_numbers(app) ? "ON" : "OFF");
        return 1;

    case KEY_ALT('Q'):
    {
        app->hard_wrap = !app->hard_wrap;
        ed_set_hard_wrap(te_app_get_editor(app), app->hard_wrap);

        /* Entering hard mode refits the document to the column */
        if (app->hard_wrap)
        {
            ui_editor_rewrap_docwide(app);
        }
        else
        {
            ed_join_breaks(te_app_get_editor(app));
            ed_ensure_visible(te_app_get_editor(app));
        }

        /* Ensure cursor stays within visible area when dict panel is active */
#ifdef HAVE_TRANSLATE_STARDICT
        if (app->spell_panel_mode == 2)
            ed_ensure_visible(te_app_get_editor(app));
#endif

        return 1;
    }

    default:
        return 0;
    }
}

int handle_editing_keys(TeApp *app, int ch, wint_t wch, int soft, int width, int body_rows, int *preserve_desired)
{
    switch (ch)
    {
    case '\n':
    case '\r':
        ed_block_clear(te_app_get_editor(app));
        do_smart_enter(app);
        clear_search_highlights(app);
        return 1;

    case 8:
    case 127:
        ed_block_clear(te_app_get_editor(app));
        ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));
        ed_backspace(te_app_get_editor(app));
        ed_auto_rewrap_after_edit(app);
        clear_search_highlights(app);
        return 1;

    case CTRL('B'):
        if (soft)
            view_move_home(app, width);
        else
            ed_move_home(te_app_get_editor(app));

        return 1;

    case CTRL('E'):
        if (soft)
            view_move_end(app, width);
        else
            ed_move_end(te_app_get_editor(app));

        return 1;

    case CTRL('U'):
#ifdef HAVE_TRANSLATE_STARDICT
        if (app->spell_panel_mode == 2 && app->dict_result && app->dict_result[0])
        {
            int i;
            int rows = DICT_PANEL_ROWS;

            for (i = 0; i < rows; i++)
            {
                if (!ui_dict_scroll_up(app))
                    break;
            }

            return 1;
        }
#endif
        if (soft)
        {
            view_move_pgup(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgup(te_app_get_editor(app), body_rows);

        return 1;

    case CTRL('D'):
#ifdef HAVE_TRANSLATE_STARDICT
        if (app->spell_panel_mode == 2 && app->dict_result && app->dict_result[0])
        {
            int i;
            int rows = DICT_PANEL_ROWS;

            for (i = 0; i < rows; i++)
            {
                if (!ui_dict_scroll_down(app))
                    break;
            }

            return 1;
        }
#endif
        if (soft)
        {
            view_move_pgdn(app, width, body_rows);
            *preserve_desired = 1;
        }
        else
            ed_move_pgdn(te_app_get_editor(app), body_rows);

        return 1;

    case CTRL('Y'):
        ed_block_clear(te_app_get_editor(app));

        ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));

        ed_delete_line(te_app_get_editor(app));
        ed_auto_rewrap_after_edit(app);
        clear_search_highlights(app);
        return 1;

    case CTRL('Z'):
        ed_undo(te_app_get_editor(app));
        app->hard_wrap = ed_get_hard_wrap(te_app_get_editor(app));
        clear_search_highlights(app);
        return 1;

#ifdef HAVE_TRANSLATE
    case CTRL('T'):
        if (!app->translate_handle && app->cfg.translate_enabled)
            ui_translate_load_from_config(app);

        if (app->translate_handle)
        {
            app->translate_active = !app->translate_active;
            te_status(app, "Translator %s", app->translate_active ? "enabled" : "disabled");
        }
        else if (!app->cfg.translate_enabled)
        {
            te_status(app, "Translator disabled in config (enable in Setup)");
        }
        else
        {
            te_status(app, "Cannot load translator");
        }
        return 1;
#endif

    case '\t':
        if (app->hard_wrap)
            ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));

        ed_insert_tab(te_app_get_editor(app), 4);

        clear_search_highlights(app);
        ed_auto_rewrap_after_edit(app);
        return 1;

    default:
        if (wch >= 0x20 && wch != 127)
        {
            EdInfo ac_info;
            const wchar_t *ac_line = NULL;
            wchar_t next = L'\0';
            wchar_t close = L'\0';
            int ac_llen;

            ed_block_clear(te_app_get_editor(app));

            /* Capture pre-snapshot before the edit so rewrap undo works */
            if (app->hard_wrap && (wch == L' ' || wch == L'\t'))
                ed_auto_rewrap_capture_pre_snapshot(te_app_get_editor(app));

            ed_insert_char(te_app_get_editor(app), (wchar_t)wch);
            clear_search_highlights(app);

            /* Auto-close open brackets and Spanish opening marks */
            if (app->cfg.autoclose && (wch == L'(' || wch == L'[' || wch == L'{' || wch == L'"' || wch == L'\'' || wch == L'¿' || wch == L'¡'))
            {
                ed_get_info(te_app_get_editor(app), &ac_info);

                ac_line = ed_line_wcs(te_app_get_editor(app), ac_info.row);
                ac_llen = ed_line_len(te_app_get_editor(app), ac_info.row);

                if (ac_line && ac_info.col < ac_llen)
                    next = ac_line[ac_info.col];

                if (wch == L'(')
                    close = L')';
                else if (wch == L'[')
                    close = L']';
                else if (wch == L'{')
                    close = L'}';
                else if (wch == L'"')
                    close = L'"';
                else if (wch == L'\'')
                    close = L'\'';
                else if (wch == L'¿')
                    close = L'?';
                else if (wch == L'¡')
                    close = L'!';

                if (close &&
                    (next == L'\0' || next == L' ' || next == L'\t' ||
                     next == L')' || next == L']' || next == L'}' ||
                     next == L'"' || next == L'\'' || next == L',' ||
                     next == L';' || next == L'.' || next == L':' ||
                     next == L'!' || next == L'?'))
                {
                    ed_insert_char(te_app_get_editor(app), close);

                    /* Step cursor back so it sits between the pair */
                    ed_set_pos(te_app_get_editor(app), ac_info.row, ac_info.col);
                }
            }

            /* Auto-close a block comment opener with its matching closer */
            if (app->cfg.autoclose && wch == L'*')
            {
                wchar_t slash = L'\0';

                ed_get_info(te_app_get_editor(app), &ac_info);

                ac_line = ed_line_wcs(te_app_get_editor(app), ac_info.row);
                ac_llen = ed_line_len(te_app_get_editor(app), ac_info.row);

                if (ac_line && ac_info.col >= 2 && ac_info.col - 2 < ac_llen)
                    slash = ac_line[ac_info.col - 2];

                next = L'\0';

                if (ac_line && ac_info.col < ac_llen)
                    next = ac_line[ac_info.col];

                if (slash == L'/' && next != L'/')
                {
                    ed_insert_char(te_app_get_editor(app), L'*');
                    ed_insert_char(te_app_get_editor(app), L'/');

                    ed_set_pos(te_app_get_editor(app), ac_info.row, ac_info.col);
                }
            }

            /* Editor assists: smart quotes, auto-cap. Independent of wrap mode they only touch the buffer */
            ui_assist_on_char(app, (wchar_t)wch);

            /* Hard-wrap: reflow paragraph when a word separator is inserted */
            if (wch == L' ' || wch == L'\t')
                ed_auto_rewrap_after_edit(app);

            return 1;
        }

        return 0;
    }

    return 0;
}
