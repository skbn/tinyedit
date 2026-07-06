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

#include "ui_tts.h"

#ifdef HAVE_TTS

#include "../tts/tts.h"
#include "../components/editor.h"
#include "../components/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static const char *field_labels[] =
    {
        "Voice",
        "Rate ",
        "Pitch",
        "Vol. "};

/* Convert wchar_t to UTF-8, declared here to avoid header cross-dependency */
extern char *wcs_to_utf8(const wchar_t *wcs, int len);

/* Load/unload called at startup and after Setup */
void ui_tts_load_from_config(TeApp *app)
{
    TtsOpts opts;

    if (!app)
        return;

    app->tts_enabled = app->cfg.tts_enabled;

    /* Free handle if TTS disabled */
    if (!app->tts_enabled)
    {
        if (app->tts_handle)
        {
            tts_free((TtsHandle *)app->tts_handle);
            app->tts_handle = NULL;
        }

        return;
    }

    /* Build opts from config */
    tts_opts_defaults(&opts);

    opts.voice = app->cfg.tts_voice;
    opts.rate = app->cfg.tts_rate;
    opts.pitch = app->cfg.tts_pitch;
    opts.volume = app->cfg.tts_volume;

    tts_opts_clamp(&opts);

    /* Update existing handle or create new one */
    if (app->tts_handle)
    {
        /* Stop playback before changing options */
        tts_stop((TtsHandle *)app->tts_handle);
        tts_set_opts((TtsHandle *)app->tts_handle, &opts);
    }
    else
    {
        app->tts_handle = tts_new(&opts);

        if (!app->tts_handle)
        {
            /* Mark disabled if backend unavailable, keep cfg preference */
            app->tts_enabled = 0;
            te_status(app, "TTS backend unavailable (espeak-ng not found?)");
        }
        else
        {
            te_status(app, "TTS enabled");
        }
    }
}

void ui_tts_unload(TeApp *app)
{
    if (!app)
        return;

    if (app->tts_handle)
    {
        tts_free((TtsHandle *)app->tts_handle);
        app->tts_handle = NULL;
    }
}

/* Extract text: block selection or current paragraph */
static char *grab_text_for_speech(TeApp *app)
{
    Ed *ed = NULL;
    EdInfo info;
    int first;
    int last;
    wchar_t *joined = NULL;
    size_t cap = 0;
    size_t used = 0;
    char *utf8;
    int row;

    if (!app)
        return NULL;

    ed = te_app_get_editor(app);

    if (!ed)
        return NULL;

    ed_get_info(ed, &info);

    /* Prefer block selection */
    if (info.block.active)
    {
        int ar = info.block.anchor_row;
        int ac = info.block.anchor_col;
        int cr = info.row;
        int cc = info.col;
        int r1, c1, r2, c2;

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

        if (r1 != r2 || c1 != c2)
            return ed_block_to_string(ed, r1, c1, r2, c2);
    }

    /* Otherwise extract paragraph */
    first = info.row;

    while (first > 0 && ed_line_len(ed, first - 1) > 0)
        first--;

    last = info.row;

    while (last < info.line_count - 1 && ed_line_len(ed, last + 1) > 0)
        last++;

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
                return NULL;
            }

            joined = nb;
            cap = nc;
        }

        if (!joined)
            return NULL;

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
        return NULL;
    }

    joined[used] = L'\0';
    utf8 = wcs_to_utf8(joined, (int)used);

    free(joined);

    return utf8;
}

/* Grab from starting row to end */
static char *grab_text_from_row(TeApp *app, int from_start)
{
    Ed *ed = NULL;
    EdInfo info;
    wchar_t *joined = NULL;
    size_t cap = 0;
    size_t used = 0;
    char *utf8;
    int row;
    int start_row;

    if (!app)
        return NULL;

    ed = te_app_get_editor(app);

    if (!ed)
        return NULL;

    ed_get_info(ed, &info);

    start_row = from_start ? 0 : info.row;

    for (row = start_row; row < info.line_count; row++)
    {
        const wchar_t *line = ed_line_wcs(ed, row);
        int ll = ed_line_len(ed, row);

        if (used + (size_t)ll + 2 >= cap)
        {
            size_t nc = (used + (size_t)ll + 128) * 2;
            wchar_t *nb = (wchar_t *)realloc(joined, nc * sizeof(wchar_t));

            if (!nb)
            {
                free(joined);
                return NULL;
            }

            joined = nb;
            cap = nc;
        }

        if (!joined)
            return NULL;

        if (line && ll > 0)
        {
            memcpy(joined + used, line, (size_t)ll * sizeof(wchar_t));
            used += (size_t)ll;
        }

        /* Newline preserves blank lines for paragraph pauses */
        if (row < info.line_count - 1)
            joined[used++] = L'\n';
    }

    if (used == 0)
    {
        free(joined);
        return NULL;
    }

    joined[used] = L'\0';
    utf8 = wcs_to_utf8(joined, (int)used);

    free(joined);

    return utf8;
}

void ui_tts_speak_action(TeApp *app)
{
    char *text;

    if (!app || !app->tts_handle)
    {
        te_status(app, "TTS not available");
        return;
    }

    text = grab_text_for_speech(app);

    if (!text || !text[0])
    {
        free(text);
        te_status(app, "Nothing to speak");
        return;
    }

    tts_speak((TtsHandle *)app->tts_handle, text);
    te_status(app, "Speaking...");

    free(text);
}

/* Dictate entire document from first line, bound to Alt+Shift+K */
void ui_tts_speak_doc_action(TeApp *app)
{
    char *text = NULL;

    if (!app || !app->tts_handle)
    {
        te_status(app, "TTS not available");
        return;
    }

    /* Restart from scratch, drop queued text */
    tts_stop((TtsHandle *)app->tts_handle);

    text = grab_text_from_row(app, 1);

    if (!text || !text[0])
    {
        free(text);

        te_status(app, "Nothing to speak");
        return;
    }

    tts_speak((TtsHandle *)app->tts_handle, text);
    te_status(app, "Speaking whole document...");

    free(text);
}

void ui_tts_speak_all_action(TeApp *app)
{
    char *text = NULL;

    if (!app || !app->tts_handle)
    {
        te_status(app, "TTS not available");
        return;
    }

    text = grab_text_from_row(app, 0);

    if (!text || !text[0])
    {
        free(text);

        te_status(app, "Nothing to speak");
        return;
    }

    tts_speak((TtsHandle *)app->tts_handle, text);
    te_status(app, "Speaking from cursor to end...");

    free(text);
}

void ui_tts_pause_toggle(TeApp *app)
{
    TtsState st;

    if (!app || !app->tts_handle)
    {
        te_status(app, "TTS not available");
        return;
    }

    st = tts_get_state((TtsHandle *)app->tts_handle);

    if (st == TTS_STATE_PLAYING)
    {
        tts_pause((TtsHandle *)app->tts_handle);
        te_status(app, "Speech paused");
    }
    else if (st == TTS_STATE_PAUSED)
    {
        tts_resume((TtsHandle *)app->tts_handle);
        te_status(app, "Speech resumed");
    }
    else
    {
        te_status(app, "Not speaking");
    }
}

void ui_tts_stop(TeApp *app)
{
    if (!app || !app->tts_handle)
        return;

    tts_stop((TtsHandle *)app->tts_handle);
    te_status(app, "Speech stopped");
}

/* Poll from main loop */
int ui_tts_tick(TeApp *app)
{
    if (!app || !app->tts_handle)
        return 0;

    return tts_poll((TtsHandle *)app->tts_handle);
}

int ui_tts_is_busy(TeApp *app)
{
    if (!app || !app->tts_handle)
        return 0;

    return tts_get_state((TtsHandle *)app->tts_handle) == TTS_STATE_PLAYING;
}

unsigned long ui_tts_amiga_sigmask(TeApp *app)
{
    if (!app || !app->tts_handle)
        return 0;

    return tts_amiga_sigmask((TtsHandle *)app->tts_handle);
}

static void draw_field(int y, int x, int w, int highlighted, const char *label, const char *value)
{
    int cur = getcurx(stdscr);
    int stop = x + w - 1;

    if (highlighted)
        attron(A_REVERSE);

    mvaddstr(y, x, label);
    addstr(":  ");
    addstr(value);

    /* Pad to width */
    cur = getcurx(stdscr);
    stop = x + w - 1;

    while (cur < stop)
    {
        addch(' ');
        cur++;
    }

    if (highlighted)
        attroff(A_REVERSE);
}

void ui_tts_popup(TeApp *app)
{
    TtsOpts orig;
    TtsOpts cur;
    int y, x, h, w;
    int field = 0;
    int result_persist = -1; /* -1=cancel, 0=session, 1=persist */
    int ch;
    int i;
    const char *hlp = NULL;
    int hlen;

    if (!app)
        return;

    /* Initial values from config, popup works even if handle disabled */
    tts_opts_defaults(&orig);

    orig.voice = app->cfg.tts_voice;
    orig.rate = app->cfg.tts_rate;
    orig.pitch = app->cfg.tts_pitch;
    orig.volume = app->cfg.tts_volume;

    tts_opts_clamp(&orig);

    cur = orig;

    ui_popup_center(9, 42, &y, &x, &h, &w);

    /* Draw and interact loop */
    while (1)
    {
        char buf[64];

        standend();

        ui_draw_popup_frame(y, x, h, w, " Speech ");

        attron(COLOR_PAIR(COL_POPUP));

        /* Draw 4 fields, one per line */
        for (i = 0; i < 4; i++)
        {
            const char *val = "";

            switch (i)
            {
            case 0:
                val = tts_voice_name(cur.voice);
                break;
            case 1:
                snprintf(buf, sizeof(buf), "%d wpm", cur.rate);
                val = buf;
                break;
            case 2:
                snprintf(buf, sizeof(buf), "%d Hz", cur.pitch);
                val = buf;
                break;
            case 3:
                snprintf(buf, sizeof(buf), "%d%%", cur.volume);
                val = buf;
                break;
            }

            draw_field(y + 2 + i, x + 2, w - 4, field == i, field_labels[i], val);
        }

        /* Help line at bottom */
        hlp = " Up/Dn=field  <-/->=adjust  T=test  Enter=save  Esc=cancel ";
        hlen = (int)strlen(hlp);

        if (hlen > w - 2)
            hlen = w - 2;

        mvaddnstr(y + h - 2, x + 1, hlp, hlen);
        move(y + 2 + field, x + 2);
        refresh();

        ch = wrapper_getch();

        if (ch == 27) /* ESC */
        {
            result_persist = -1;
            break;
        }

        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER)
        {
            result_persist = 1; /* save + persist */
            break;
        }

        if (ch == 's' || ch == 'S')
        {
            result_persist = 0; /* save session only */
            break;
        }

        if (ch == 't' || ch == 'T')
        {
            /* Apply temporarily and speak test phrase */
            if (app->tts_handle)
            {
                tts_set_opts((TtsHandle *)app->tts_handle, &cur);
                tts_stop((TtsHandle *)app->tts_handle);
                tts_speak((TtsHandle *)app->tts_handle, "This is a test of the current voice settings.");
            }

            continue;
        }

        if (ch == KEY_UP)
        {
            field = (field + 3) % 4;
            continue;
        }

        if (ch == KEY_DOWN || ch == '\t')
        {
            field = (field + 1) % 4;
            continue;
        }

        if (ch == KEY_LEFT || ch == '-')
        {
            switch (field)
            {
            case 0:
                cur.voice = (cur.voice + 3) % 4;
                break;
            case 1:
                cur.rate -= 10;
                break;
            case 2:
                cur.pitch -= 5;
                break;
            case 3:
                cur.volume -= 5;
                break;
            }
            tts_opts_clamp(&cur);
            continue;
        }

        if (ch == KEY_RIGHT || ch == '+' || ch == '=')
        {
            switch (field)
            {
            case 0:
                cur.voice = (cur.voice + 1) % 4;
                break;
            case 1:
                cur.rate += 10;
                break;
            case 2:
                cur.pitch += 5;
                break;
            case 3:
                cur.volume += 5;
                break;
            }
            tts_opts_clamp(&cur);
            continue;
        }
    }

    if (result_persist < 0)
    {
        /* Cancel: revert if test was applied */
        if (app->tts_handle)
        {
            tts_stop((TtsHandle *)app->tts_handle);
            tts_set_opts((TtsHandle *)app->tts_handle, &orig);
        }

        te_status(app, "Speech settings unchanged");
        return;
    }

    /* Save to config */
    app->cfg.tts_voice = cur.voice;
    app->cfg.tts_rate = cur.rate;
    app->cfg.tts_pitch = cur.pitch;
    app->cfg.tts_volume = cur.volume;

    /* Apply to handle */
    if (app->tts_handle)
        tts_set_opts((TtsHandle *)app->tts_handle, &cur);

    if (result_persist == 1)
    {
        /* Persist to disk */
        te_cfg_save(&app->cfg, app->cfg_path);
        te_status(app, "Speech settings saved");
    }
    else
    {
        te_status(app, "Speech settings applied (session only)");
    }
}

#endif /* HAVE_TTS */
