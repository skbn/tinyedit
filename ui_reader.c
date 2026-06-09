dom 01 mar 2026 20:48:47 WET

Primer blog en gopher

--

Buenas tardes. Es la primera vez que utilizo "vee" para crear un blog en gopher y, a decir verdad, también es mi primer blog algo serio, los otros han salido bastante mal parados. Habrá que ver cómo las genera este script descargado desde << https://git.sdf.org/pifty/vee >>

Después de probar varias veces sin leer con traquilidad el famoso "readme.txt", me he dado cuenta de que crea las páginas en ASCII, y, yo las necesito en UTF-8 o Latin-1, sino, esto será una aberración. Si ya me cuesta recordar dónde iban las tildes, ¿Cómo sería en ascii? Las cosas de la edad y la poca cultura.

Ha sido un día de prueba y error, pero al final ha salido bien. Tema de permisos solucionados, y, ya debería de estar funcionando el phlob, blog, o como quieran llamarlo a día de hoy.

Para ser el primer post... no está mal. Creo que ya debería de funcionar bien.

Voy a probar con un emoticono 💪



    UTF-8 Test File for tinyedit
=============================

Latin Extended:
á é í ó ú Á É Í Ó Ú
ñ Ñ ü Ü ¿ ¡
ç Ç ß ø å

Greek:
α β γ δ ε ζ η θ ι κ λ μ ν ξ ο π ρ σ τ υ φ χ ψ ω
Α Β Γ Δ Ε Ζ Η Θ Ι Κ Λ Μ Ν Ξ Ο Π Ρ Σ Τ Υ Φ Χ Ψ Ω

Cyrillic:
а б в г д е ж з и й к л м н о п р с т у ф х ц ч ш щ ъ ы ь э ю я
А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я

Arabic:
مرحبا بالعالم
السلام عليكم

Hebrew:
שלום עולם
תודה

Chinese (Simplified):
你好世界
谢谢
电脑

Japanese:
こんにちは世界
ありがとう
コンピュータ

Korean:
안녕하세요 세계
감사합니다
컴퓨터

Emoticons/Emojis:
☺ ☻ ☹ ❤ ♠ ♣ ♦ ♣ ★ ☆
→ ← ↑ ↓ ↔
© ® ™ € £ ¥
✓ ✗ ✕ ✖
⚠ ⚡ ☀ ☁ ☂
❄ ❅ ❆ ☃

Mathematical:
∞ ∑ ∏ √ ∫ ∂
≈ ≠ ≤ ≥ < > ±
× ÷ = + - ( )
² ³ ¹ °

Box Drawing:
┌─┬─┐
│ │ │
├─┼─┤
│ │ │
└─┴─┘

║ ╔ ╗ ╚ ╝
╠ ╣ ╦ ╩ ╬

/*
 * crashedit - Message area editor for AmigaOS
 *
 * This file is part of the crashedit project.
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This program uses JAMLIB, which is licensed under the GNU Lesser
 * General Public License v2.1. See src/jamlib/LICENSE for details.
 */

/* ui_reader.c -- Message reader view */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"
#include "ui_attr.h"
#include "ui_files.h"
#include "../../src/jamlib/jam.h"
#include "../core/msghdr.h"
#include "../core/ftn.h"
#include "../core/ansi.h"
#include "../core/keys.h"

static const char *READER_HELP[] =
    {
        "Message Reader - Key Bindings:",
        "",
        "  Up/Down,j/k    Scroll body up/down",
#ifdef PLATFORM_AMIGA
        "  Ctrl+U/D       Page scroll",
        "  Ctrl+B/E       First / last line",
#else
        "  PgUp/PgDn      Page scroll",
        "  Home/End       First / last line",
#endif
        "  Left/Right,p/n Previous / next message",
        "",
        "  b/Space        Page up/down",
        "  Enter          Page down or advance to next message",
        "  < >            First / last line (scroll)",
        "  , .            First / last message",
        "  Ctrl-J         Jump to original (follow reply chain)",
        "  Ctrl+Left      Jump to original (follow reply chain)",
        "  Ctrl+Right     Jump to reply (pick if multiple)",
        "",
        "  k, v           Toggle kludges (^A, SEEN-BY, Via)",
        "  a              Toggle ANSI color rendering",
        "  c              Change DISPLAY charset (re-render)",
        "  r              Reply to this message",
        "  e              Edit this message",
        "  n, Ins         New message (in current area)",
        "  l, L           Show message list overlay (ESC returns here)",
        "  g              Goto message number",
        "  Ctrl+H         Scroll to top of message",
        "  Ctrl+K         Scroll to bottom of message",
        "  Ctrl+G         Goto line number",
        "  Alt+J          Follow reply chain to original",
        "  Alt+G          Clear search highlights",
        "  F5, /          Search in message body",
        "  F3 Alt+P       Previous match (search mode)",
        "  F4 Alt+N       Next match (search mode)",
        "  w, F7          Write message to text file",
        "  d, Del         Delete this message",
        "  Ctrl+F         File request",
        "",
        "  ESC, q         Back to message list",
        "  F1, ?          This help"};
#define READER_HELP_N ((int)(sizeof(READER_HELP) / sizeof(READER_HELP[0])))

void ui_editor_prep_new(UiApp *app);
void ui_editor_prep_reply(UiApp *app, uint32_t orig_msgnum);
void ui_editor_prep_edit(UiApp *app, uint32_t msgnum);

/* Update area's cached counts after reader moves lastread/lastseen */
static void update_area_counts_in_memory(UiApp *app)
{
    UiSession *s;
    AreaEntry *ae;
    int i, unread, new_count;

    if (!app)
        return;

    s = &app->sess;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return;

    ae = &app->areas->entries[s->area_idx];

    ae->lastread = s->lastread;
    ae->lastseen = s->lastseen;

    /* Recompute unread (above lastread) and new_count (above lastseen) */
    unread = 0;
    new_count = 0;

    for (i = 0; i < s->msg_count; i++)
    {
        if (s->msgs[i].msgnum > s->lastread)
            unread++;

        if (s->msgs[i].msgnum > s->lastseen)
            new_count++;
    }

    ae->unread = unread;
    ae->new_count = new_count;
    ae->total_msgs = s->msg_count;
}

/* Update lastread and persist to JAM; skip if from search (browsing shouldn't mutate) */
static void reader_save_lastread(UiApp *app)
{
    UiSession *s;
    uint32_t msgnum;

    if (!app)
        return;

    if (app->from_search)
        return;

    s = &app->sess;
    msgnum = app->cur_msgnum;

    s->lastread = msgnum;

    if (msgnum > s->lastseen)
        s->lastseen = msgnum;

    if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
    {
        jam_write_lastread(&s->jam, s->user_crc, s->lastread, s->lastseen);
        jam_unlock(&s->jam);
    }

    update_area_counts_in_memory(app);
}

/* Exit target: msglist (normal) or search results (if from search) */
static UiView reader_exit_view(const UiApp *app)
{
    return app->from_search ? VIEW_SEARCH_RESULTS : VIEW_MSGLIST;
}

/* Strip FTS-1 kludge lines and SEEN-BY/PATH trailers for ANSI rendering
 * Returns malloc'd buffer (caller frees), NULL on failure */
static char *strip_kludges_for_ansi(const char *raw, int raw_len, int *out_len)
{
    char *out;
    int i = 0;
    int j = 0;
    int at_line_start = 1;

    if (out_len)
        *out_len = 0;

    if (!raw || raw_len <= 0)
        return NULL;

    out = (char *)malloc((size_t)raw_len + 1);
    if (!out)
        return NULL;

    while (i < raw_len)
    {
        if (at_line_start)
        {
            const char *p = raw + i;
            int rem = raw_len - i;
            int is_kludge = 0;

            if (rem >= 1 && (unsigned char)*p == 0x01)
                is_kludge = 1;
            else if (rem >= 8 && memcmp(p, "SEEN-BY:", 8) == 0)
                is_kludge = 1;
            else if (rem >= 5 && memcmp(p, "PATH:", 5) == 0)
                is_kludge = 1;

            if (is_kludge)
            {
                /* Skip to end of this line (eat optional \r and \n) */
                while (i < raw_len && raw[i] != '\r' && raw[i] != '\n')
                    i++;

                if (i < raw_len && raw[i] == '\r')
                    i++;
                if (i < raw_len && raw[i] == '\n')
                    i++;

                /* Still at line start for the next iteration */
                continue;
            }

            at_line_start = 0;
        }

        out[j++] = raw[i];

        if (raw[i] == '\r' || raw[i] == '\n')
            at_line_start = 1;

        i++;
    }

    out[j] = '\0';

    if (out_len)
        *out_len = j;

    return out;
}

/* Load current message into reader */
static int load_msg(UiApp *app, uint32_t msgnum)
{
    UiSession *s;
    int idx;
    char *body_utf8 = NULL;
    int wrap_w;
    const wchar_t *cur;
    char *cur_utf8;
    int needs_fallback;
    int ansi_on_outer = 0;
    char detected[CHARSET_NAME_MAX];
    const char *override_enc;
    char chosen[CHARSET_NAME_MAX];
    int ansi_on;
    uint32_t raw_len = 0;
    char *raw_bytes = NULL;
    int has_chrs = 0;
    const char *p;
    int oi;

    if (!app)
        return -1;

    s = &app->sess;

    idx = jam_find_by_msgnum(s->msgs, s->msg_count, msgnum);

    if (idx < 0)
        return -1;

    /* Sync msg_sel to current message for correct navigation after search/jump entry */
    for (oi = 0; oi < s->order_count; oi++)
    {
        if (s->order[oi] == idx)
        {
            s->msg_sel = oi + 1;
            break;
        }
    }

    /* Fill header */
    msghdr_load(app->hdr, &s->msgs[idx], app->areas->entries[s->area_idx].name, idx + 1, s->msg_count, ftn_effective_tz_offset(app->cfg->timezone_offset, app->cfg->timezone_is_manual));

    /* Body: ANSI->canvas, text->UTF-8. Charset: CHRS if present, else default */
    override_enc = app->view_charset[0] ? app->view_charset : NULL;

    detected[0] = '\0';
    chosen[0] = '\0';

    /* Read and decode body using wrapper_read_utf8_ex with proper fallback logic */
    body_utf8 = wrapper_read_utf8_ex(&s->jam, msgnum, override_enc, NULL, detected, sizeof(detected));

    if (!body_utf8)
        return -1;

    /* Read raw body for ANSI mode (needs original bytes, not UTF-8) */
    raw_bytes = jam_read_body(&s->jam, msgnum, &raw_len);

    if (!raw_bytes)
    {
        free(body_utf8);
        return -1;
    }

    /* Set charset variables based on wrapper_read_utf8_ex results */
    strncpy(chosen, detected, sizeof(chosen) - 1);
    chosen[sizeof(chosen) - 1] = '\0';

    /* Check if we actually have a CHRS kludge */
    has_chrs = (detected[0] != '\0');

    if (has_chrs)
    {
        strncpy(app->msg_charset, detected, sizeof(app->msg_charset) - 1);
        app->msg_charset[sizeof(app->msg_charset) - 1] = '\0';
    }
    else
    {
        app->msg_charset[0] = '\0';
    }

    /* ANSI mode: force CP437 if UTF-8 came from default/fallback (not CHRS/override)
     * Exception: when TTF is active, keep detected charset to preserve Unicode characters */
    ansi_on = (app->cfg && app->cfg->viewansi);
    ansi_on_outer = ansi_on;

    if (ansi_on && (strcasecmp(chosen, "UTF-8") == 0 || strcasecmp(chosen, "UTF8") == 0))
    {
        int user_wants_utf8 = (override_enc && (strcasecmp(override_enc, "UTF-8") == 0 || strcasecmp(override_enc, "UTF8") == 0));
        int chrs_says_utf8 = (app->msg_charset[0] && (strcasecmp(app->msg_charset, "UTF-8") == 0 || strcasecmp(app->msg_charset, "UTF8") == 0));

        if (!user_wants_utf8 && !chrs_says_utf8)
        {
            strncpy(chosen, "CP437", sizeof(chosen) - 1);
            chosen[sizeof(chosen) - 1] = '\0';
        }
    }

    /* Record actual decode charset for status bar (may differ from view_charset) */
    if (override_enc && override_enc[0])
        strncpy(app->decoded_charset, override_enc, sizeof(app->decoded_charset) - 1);
    else
        strncpy(app->decoded_charset, chosen, sizeof(app->decoded_charset) - 1);

    app->decoded_charset[sizeof(app->decoded_charset) - 1] = '\0';

    /*wrap_w = COLS - 2;*/
    wrap_w = COLS;

    if (wrap_w < 20)
        wrap_w = 20;

    if (ansi_on)
    {
        /* Strip kludge/SEEN-BY/PATH lines. Fall back to raw bytes on failure */
        int stripped_len = 0;
        char *stripped = strip_kludges_for_ansi(raw_bytes, (int)raw_len, &stripped_len);

        if (stripped)
        {
            rd_load_ansi(app->reader, stripped, stripped_len, chosen, wrap_w);
            free(stripped);
        }
        else
        {
            rd_load_ansi(app->reader, raw_bytes, (int)raw_len, chosen, wrap_w);
        }

        /* Free unused UTF-8 buffer; ANSI mode uses raw bytes. Prevents leak */
        free(body_utf8);
        body_utf8 = NULL; /* not used in this path */
    }

    /* ANSI mode: convert kludges/SEEN-BY/Origin to UTF-8 for MSGID/origin lookup */
    if (ansi_on)
    {
        int srclen = (int)raw_len;
        int dstmax = srclen * 4 + 256;

        body_utf8 = (char *)malloc((size_t)dstmax);

        if (body_utf8)
            charset_body_to_utf8(chosen, raw_bytes, srclen, body_utf8, dstmax);
    }

    free(raw_bytes);

    if (!body_utf8)
        return -1;

    /* Extract MSGID / REPLY for header display */
    app->cur_msgid[0] = '\0';
    app->cur_reply[0] = '\0';

    ftn_get_kludge_value(body_utf8, "MSGID", app->cur_msgid, sizeof(app->cur_msgid));
    ftn_get_kludge_value(body_utf8, "REPLY", app->cur_reply, sizeof(app->cur_reply));

    /* Get sender address from MSGID kludge, fallback to Origin line */

    cur = msghdr_get(app->hdr, HDR_OADDR);
    cur_utf8 = cur ? wcs_to_utf8(cur, (int)wcslen(cur)) : NULL;

    if (cur && !cur_utf8)
    {
        free(body_utf8); /* would otherwise leak on this OOM path */
        return -1;
    }

    needs_fallback = (!cur_utf8 || !cur_utf8[0] || strcmp(cur_utf8, "0:0/0") == 0);

    if (needs_fallback)
    {
        char addr[64];
        int got = -1;

        if (app->cur_msgid[0])
        {
            /* Extract address from MSGID (format: "addr serial") */
            int i;

            addr[0] = '\0';

            for (i = 0; app->cur_msgid[i] && app->cur_msgid[i] != ' ' && app->cur_msgid[i] != '\t' && i < (int)sizeof(addr) - 1; i++)
                addr[i] = app->cur_msgid[i];

            addr[i] = '\0';

            if (addr[0])
                got = 0;
        }

        if (got != 0)
            got = ftn_find_origin_address(body_utf8, addr, sizeof(addr));

        if (got == 0)
            msghdr_set_utf8(app->hdr, HDR_OADDR, addr);
    }

    free(cur_utf8);

    wrap_w = COLS; /* full width, match editor soft-wrap */

    if (wrap_w < 20)
        wrap_w = 20;

    /* ANSI: canvas loaded above, skip rd_load. Non-ANSI: rd_load handles wrap */
    if (!ansi_on_outer)
        rd_load(app->reader, body_utf8, wrap_w);

    free(body_utf8);

    rd_set_page(app->reader, LINES - 8);

    /* Apply view-config toggles (kludges visibility) */
    if (app->cfg->viewkludge != rd_kludges_visible(app->reader))
        rd_toggle_kludges(app->reader);

    return 0;
}

/* Try to go to next area in area_order. Returns 1 on success, 0 otherwise */
static int try_goto_next_area(UiApp *app)
{
    UiSession *s;
    int current_area_idx;
    int current_order_idx = -1;
    int next_area_idx = -1;
    int i;
    AreaEntry *next_ae;
    char prompt[256];

    if (!app)
        return 0;

    s = &app->sess;

    if (app->area_order_count <= 0)
        return 0;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return 0;

    current_area_idx = s->area_idx;

    /* Find current area in area_order */
    for (i = 0; i < app->area_order_count; i++)
    {
        if (app->area_order[i] == current_area_idx)
        {
            current_order_idx = i;
            break;
        }
    }

    /* Find next area in order */
    if (current_order_idx >= 0 && current_order_idx < app->area_order_count - 1)
    {
        next_area_idx = app->area_order[current_order_idx + 1];
    }

    if (next_area_idx < 0)
        return 0;

    next_ae = &app->areas->entries[next_area_idx];
    snprintf(prompt, sizeof(prompt), "Go to next area '%s'?", next_ae->name ? next_ae->name : "(area)");

    if (ui_popup_confirm("Next area", prompt) != 1)
        return 0;

    /* Save lastread before changing area */
    reader_save_lastread(app);

    /* Close current session and open next area */
    ui_session_close(app);

    if (ui_session_open(app, next_area_idx) != 0)
    {
        ui_status(app, "Cannot open area");
        return 0;
    }

    /* Load message from new area */
    if (s->order_count > 0)
    {
        /* Check if current position is on an unread message */
        uint32_t current_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        /* If not on unread message, go to last message in list */
        if (current_msgnum <= s->lastread)
            s->msg_sel = s->order_count;

        app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        if (load_msg(app, app->cur_msgnum) != 0)
        {
            ui_status(app, "Cannot load message");
            return 0;
        }
    }

    return 1;
}

/* Header bar */
static void draw_header_bar(UiApp *app)
{
    int i;
    const wchar_t *from, *to, *subj, *date;
    char attr_str[40];
    static const char SPACES[256] =
        "                                                                "
Buenas tardes. Es la primera vez que utilizo "vee" para crear un 
blog en gopher y, a decir verdad, también es mi primer blog algo 
serio, los otros han salido bastante mal parados. Habrá que ver 
cómo las genera este script descargado desde << 
https://git.sdf.org/pifty/vee >>

Después de probar varias veces sin leer con traquilidad el famoso 
"readme.txt", me he dado cuenta de que crea las páginas en ASCII, 
y, yo las necesito en UTF-8 o Latin-1, sino, esto será una 
aberración. Si ya me cuesta recordar dónde iban las tildes, ¿Cómo 
sería en ascii? Las cosas de la edad y la poca cultura.
Buenas tardes. Es la primera vez que utilizo "vee" para crear un 
blog en gopher y, a decir verdad, también es mi primer blog algo 
serio, los otros han salido bastante mal parados. Habrá que ver 
cómo las genera este script descargado desde << 
https://git.sdf.org/pifty/vee >>

Después de probar varias veces sin leer con traquilidad el famoso 
"readme.txt", me he dado cuenta de que crea las páginas en ASCII, 
y, yo las necesito en UTF-8 o Latin-1, sino, esto será una 
aberración. Si ya me cuesta recordar dónde iban las tildes, ¿Cómo 
sería en ascii? Las cosas de la edad y la poca cultura.
Buenas tardes. Es la primera vez que utilizo "vee" para crear un 
blog en gopher y, a decir verdad, también es mi primer blog algo 
serio, los otros han salido bastante mal parados. Habrá que ver 
cómo las genera este script descargado desde << 
https://git.sdf.org/pifty/vee >>

Después de probar varias veces sin leer con traquilidad el famoso 
"readme.txt", me he dado cuenta de que crea las páginas en ASCII, 
y, yo las necesito en UTF-8 o Latin-1, sino, esto será una 
aberración. Si ya me cuesta recordar dónde iban las tildes, ¿Cómo 
sería en ascii? Las cosas de la edad y la poca cultura.
Buenas tardes. Es la primera vez que utilizo "vee" para crear un 
blog en gopher y, a decir verdad, también es mi primer blog algo 
serio, los otros han salido bastante mal parados. Habrá que ver 
cómo las genera este script descargado desde << 
https://git.sdf.org/pifty/vee >>

Después de probar varias veces sin leer con traquilidad el famoso 
"readme.txt", me he dado cuenta de que crea las páginas en ASCII, 
y, yo las necesito en UTF-8 o Latin-1, sino, esto será una 
aberración. Si ya me cuesta recordar dónde iban las tildes, ¿Cómo 
sería en ascii? Las cosas de la edad y la poca cultura.
Buenas tardes. Es la primera vez que utilizo "vee" para crear un 
blog en gopher y, a decir verdad, también es mi primer blog algo 
serio, los otros han salido bastante mal parados. Habrá que ver 
cómo las genera este script descargado desde << 
https://git.sdf.org/pifty/vee >>

Después de probar varias veces sin leer con traquilidad el famoso 
"readme.txt", me he dado cuenta de que crea las páginas en ASCII, 
y, yo las necesito en UTF-8 o Latin-1, sino, esto será una 
aberración. Si ya me cuesta recordar dónde iban las tildes, ¿Cómo 
sería en ascii? Las cosas de la edad y la poca cultura.
Buenas tardes. Es la primera vez que utilizo "vee" para crear un 
blog en gopher y, a decir verdad, también es mi primer blog algo 
serio, los otros han salido bastante mal parados. Habrá que ver 
cómo las genera este script descargado desde << 
https://git.sdf.org/pifty/vee >>

Después de probar varias veces sin leer con traquilidad el famoso 
"readme.txt", me he dado cuenta de que crea las páginas en ASCII, 
y, yo las necesito en UTF-8 o Latin-1, sino, esto será una 
aberración. Si ya me cuesta recordar dónde iban las tildes, ¿Cómo 
sería en ascii? Las cosas de la edad y la poca cultura.
Buenas tardes. Es la primera vez que utilizo "vee" para crear un 
blog en gopher y, a decir verdad, también es mi primer blog algo 
serio, los otros han salido bastante mal parados. Habrá que ver 
cómo las genera este script descargado desde << 
https://git.sdf.org/pifty/vee >>

Después de probar varias veces sin leer con traquilidad el famoso 
"readme.txt", me he dado cuenta de que crea las páginas en ASCII, 
y, yo las necesito en UTF-8 o Latin-1, sino, esto será una 
aberración. Si ya me cuesta recordar dónde iban las tildes, ¿Cómo 
sería en ascii? Las cosas de la edad y la poca cultura.
Buenas tardes. Es la primera vez que utilizo "vee" para crear un 
blog en gopher y, a decir verdad, también es mi primer blog algo 
serio, los otros han salido bastante mal parados. Habrá que ver 
cómo las genera este script descargado desde << 
https://git.sdf.org/pifty/vee >>

Después de probar varias veces sin leer con traquilidad el famoso 
"readme.txt", me he dado cuenta de que crea las páginas en ASCII, 
y, yo las necesito en UTF-8 o Latin-1, sino, esto será una 
aberración. Si ya me cuesta recordar dónde iban las tildes, ¿Cómo 
sería en ascii? Las cosas de la edad y la poca cultura.
Buenas tardes. Es la primera vez que utilizo "vee" para crear un 
blog en gopher y, a decir verdad, también es mi primer blog algo 
serio, los otros han salido bastante mal parados. Habrá que ver 
cómo las genera este script descargado desde << 
https://git.sdf.org/pifty/vee >>

Después de probar varias veces sin leer con traquilidad el famoso 
"readme.txt", me he dado cuenta de que crea las páginas en ASCII, 
y, yo las necesito en UTF-8 o Latin-1, sino, esto será una 
aberración. Si ya me cuesta recordar dónde iban las tildes, ¿Cómo 
sería en ascii? Las cosas de la edad y la poca cultura.
Buenas tardes. Es la primera vez que utilizo "vee" para crear un 
blog en gopher y, a decir verdad, también es mi primer blog algo 
serio, los otros han salido bastante mal parados. Habrá que ver 
cómo las genera este script descargado desde << 
https://git.sdf.org/pifty/vee >>

Después de probar varias veces sin leer con traquilidad el famoso dsfsdfsfdsds
"readme.txt", me he dado cuenta de que crea las páginas en ASCII, 
y, yo las necesito en UTF-8 o Latin-1, sino, esto será una 
aberración. Si ya me cuesta recordar dónde iban las tildes, ¿Cómo 
sería en ascii? Las cosas de la edad y la poca cultura.




        "                                                                "
        "                                                                "
        "                                                                ";

    from = msghdr_get(app->hdr, HDR_FROM);
    to = msghdr_get(app->hdr, HDR_TO);
    subj = msghdr_get(app->hdr, HDR_SUBJECT);
    date = msghdr_get(app->hdr, HDR_DATE);
    ui_attr_build(msghdr_attr(app->hdr), attr_str, sizeof(attr_str));

    attron(COLOR_PAIR(COL_HEADER));

    for (i = 1; i < 7; i++)
    {
        int written = 0;
        move(i, 0);
        clrtoeol();

        while (written < COLS)
        {
            int chunk = COLS - written;

            if (chunk > (int)sizeof(SPACES))
                chunk = (int)sizeof(SPACES);

            mvaddnstr(i, written, SPACES, chunk);
            written += chunk;
        }
    }

    mvprintw(1, 1, "Area: %-30.30s  Msg %d/%d", ui_wcs2u8(msghdr_get(app->hdr, HDR_AREA)), msghdr_msgnum(app->hdr), msghdr_msgtotal(app->hdr));
    mvprintw(2, 1, "From: %-25.25s  %-20.20s  %s", ui_wcs2u8(from), ui_wcs2u8(msghdr_get(app->hdr, HDR_OADDR)), ui_wcs2u8(date));
    mvprintw(3, 1, "  To: %-25.25s  %s", ui_wcs2u8(to), ui_wcs2u8(msghdr_get(app->hdr, HDR_DADDR)));
    mvprintw(4, 1, "Subj: %s", ui_wcs2u8(subj));

    /* Attr on left, MSGID on right */
    mvprintw(5, 1, "Attr: %s", attr_str);

    if (app->cur_msgid[0])
    {
        int show = (int)strlen(app->cur_msgid);
        int max_room = COLS - 22;

        if (max_room < 10)
            max_room = 10;

        if (show > max_room)
            show = max_room;

        mvprintw(5, COLS - show - 8, "MSGID: %.*s", show, app->cur_msgid);
    }

    attroff(COLOR_PAIR(COL_HEADER));

    attron(COLOR_PAIR(COL_BORDER));
    ui_hline(6, 0, COLS);
    attroff(COLOR_PAIR(COL_BORDER));
}

/* Body rendering */
/* Convert one wchar_t to UTF-8 in buf. Returns bytes written */
static int wc_to_utf8_bytes(wchar_t wc, char *buf)
{
    unsigned long cp = (unsigned long)wc;

    if (cp < 0x80)
    {
        buf[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));

        return 2;
    }

    if (cp < 0x10000)
    {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));

        return 3;
    }

    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));

    return 4;
}

/* Draw ANSI line: collect runs of same attrs and emit each run in one call */
static void draw_ansi_line(int row_y, const wchar_t *wcs, int len, const struct AnsiCell *cells, int max_cols)
{
    char run_buf[1024];
    int i = 0;
    int x = 0;

    while (i < len && x < max_cols)
    {
        int run_color = cells[i].color_pair;
        int run_attrs = cells[i].attrs;
        int run_start = i;
        int run_chars = 0;
        int run_bytes = 0;

        /* Find extent of run with identical (color, attrs) */
        while (i < len && x + run_chars < max_cols && cells[i].color_pair == run_color && cells[i].attrs == run_attrs)
        {
            int n;

            if (run_bytes + 4 >= (int)sizeof(run_buf))
                break; /* Don't overflow run buffer; emit and continue */

            n = wc_to_utf8_bytes(wcs[i], &run_buf[run_bytes]);
            run_bytes += n;
            run_chars++;
            i++;
        }

        if (run_chars == 0)
            break; /* safety */

        run_buf[run_bytes] = '\0';

        if (run_color >= 0)
            attron(COLOR_PAIR(run_color) | run_attrs);

        mvaddnstr(row_y, x, run_buf, run_bytes);

        if (run_color >= 0)
            attroff(COLOR_PAIR(run_color) | run_attrs);

        x += run_chars;
    }
}

static void draw_body(UiApp *app)
{
    int rows = LINES - 8;
    int i;
    const wchar_t *wcs;
    int wlen;

    if (rows < 1)
        return;

    rd_set_page(app->reader, rows);

    for (i = 0; i < rows; i++)
    {
        int vi = rd_top(app->reader) + i;
        int type, col;
        int ansi_color, ansi_attrs;
        const struct AnsiCell *cells;
        int row_y = 7 + i;
        const wchar_t *ln;
        int lnlen;

        /* Skip if terminal shrunk since rd_set_page */
        if (row_y < 0 || row_y >= LINES - 1)
            continue;

        move(row_y, 0);
        clrtoeol();

        if (vi >= rd_total(app->reader))
            continue;

        type = rd_get_type(app->reader, vi);
        ansi_color = rd_get_ansi_color(app->reader, vi);
        ansi_attrs = rd_get_ansi_attrs(app->reader, vi);
        cells = rd_get_ansi_cells(app->reader, vi);

        /* ANSI with per-cell attrs: draw via canvas (preserves BBS art colors) */
        if (rd_ansi_visible(app->reader) && cells)
        {
            ln = rd_get_line(app->reader, vi);
            lnlen = rd_get_len(app->reader, vi);

            if (ln && lnlen > 0)
                draw_ansi_line(row_y, ln, lnlen, cells, (COLS < 80) ? COLS : 80);

            continue; /* attrs handled inside draw_ansi_line */
        }

        if (rd_ansi_visible(app->reader) && ansi_color >= 0)
        {
            col = ansi_color;
            attron(COLOR_PAIR(col) | ansi_attrs);
        }
        else
        {
            col = ui_color_for_type(type);
            attron(COLOR_PAIR(col));
            ansi_attrs = 0;
        }

        /* Plain or ANSI without per-cell attrs: draw with line color */
        ln = rd_get_line(app->reader, vi);
        lnlen = rd_get_len(app->reader, vi);

        /* Skip leading ^A on kludge lines */
        if (ln && lnlen > 0 && (type & FTN_LT_KLUDGE) && ln[0] == L'\x01')
        {
            ln++;
            lnlen--;
        }

        if (ln && lnlen > 0)
            mvaddnwstr(row_y, 0, ln, lnlen);

        /* Highlight search matches (word-level) */
        if (app->reader_search.rows && app->reader_search.cols && app->reader_search.match_count > 0)
        {
            int line_idx = rd_get_line_idx(app->reader, vi);
            int j;

            if (line_idx >= 0)
            {
                for (j = 0; j < app->reader_search.match_count; j++)
                {
                    if (app->reader_search.rows[j] == line_idx)
                    {
                        int match_col = app->reader_search.cols[j];
                        int match_len = (int)wcslen(app->reader_search.query);

                        if (match_col >= 0 && match_col + match_len <= lnlen)
                        {
                            if (j == app->reader_search.current_match)
                                attron(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attron(COLOR_PAIR(COL_SEARCH_MATCH));

                            mvaddnwstr(row_y, match_col, &ln[match_col], match_len);

                            if (j == app->reader_search.current_match)
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
            }
        }

        attroff(COLOR_PAIR(col) | ansi_attrs);
    }
}

/* Clear search state in reader */
static void reader_clear_search(UiApp *app)
{
    if (app->reader_search.rows)
    {
        free(app->reader_search.rows);
        app->reader_search.rows = NULL;
    }

    if (app->reader_search.cols)
    {
        free(app->reader_search.cols);
        app->reader_search.cols = NULL;
    }

    app->reader_search.match_count = 0;
    app->reader_search.is_mode = 0;
    app->reader_search.only_mode = 0;
    app->reader_search.current_match = 0;
    app->reader_search.match_current = 0;
}

/* Navigate to a raw msgs[] index, honouring the current sort order */
static int reader_goto_raw(UiApp *app, int raw_idx)
{
    UiSession *s = &app->sess;
    uint32_t target_mn;
    int i;

    if (raw_idx < 0 || raw_idx >= s->msg_count)
        return -1; /* invalid index */

    target_mn = s->msgs[raw_idx].msgnum;

    for (i = 0; i < s->order_count; i++)
    {
        if (s->msgs[s->order[i]].msgnum == target_mn)
        {
            s->msg_sel = i + 1;
            app->cur_msgnum = target_mn;

            return load_msg(app, target_mn);
        }
    }

    return -2;
}

/* Main loop */
UiView ui_reader_run(UiApp *app)
{
    int ch;
    UiSession *s;
    int saved_viewansi;

    if (!app)
        return VIEW_QUIT;

    s = &app->sess;
    saved_viewansi = app->cfg->viewansi;

    if (load_msg(app, app->cur_msgnum) != 0)
    {
        ui_status(app, "Cannot load message");
        app->cfg->viewansi = saved_viewansi;
        return reader_exit_view(app);
    }

    /* Apply ANSI visibility from config on first load */
    if (app->cfg->viewansi != rd_ansi_visible(app->reader))
    {
        rd_toggle_ansi(app->reader);

#ifdef PLATFORM_AMIGA
        amiga_change_font(rd_ansi_visible(app->reader));
#endif
    }

    for (;;)
    {
        erase();

        ui_draw_menubar(app, "Reader");
        draw_header_bar(app);
        draw_body(app);

        /* Status: msg_charset (CHRS or ?) -> decoded_charset (actual decode) */
        ui_status(app, "Charset: %s -> %s | c = Change charset%s | Ln %d/%d",
                  app->msg_charset[0] ? app->msg_charset : "?",
                  (app->view_charset[0]) ? app->decoded_charset : "Auto",
                  rd_ansi_visible(app->reader) ? " | ANSI" : "",
                  rd_top(app->reader) + 1, rd_total(app->reader));

        ui_draw_statusbar(app);
        refresh();

        ch = wrapper_getch();

        switch (ch)
        {
        case KEY_UP:
            rd_scroll_up(app->reader, 1);
            break;
        case KEY_DOWN:
            rd_scroll_down(app->reader, 1);
            break;
        case KEY_PPAGE:
        case 'b':
        case 'B':
        case CTRL('U'): /* Page Up (Amiga) */
            rd_page_up(app->reader);
            break;
        case KEY_NPAGE:
        case CTRL('D'): /* Page Down (Amiga) */
        case ' ':
            rd_page_down(app->reader);
            break;
        case KEY_HOME:
        case CTRL('B'): /* Home (Amiga) */
            rd_home(app->reader);
            break;
        case KEY_END:
        case CTRL('E'): /* End (Amiga) */
            rd_end(app->reader);
            break;

        case KEY_RIGHT:
        case 'n': /* Next message */
            if (s->msg_sel < s->order_count)
            {
                s->msg_sel++;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load next");
            }
            else
            {
                /* At last message: try to go to next area */
                try_goto_next_area(app);
            }
            break;
        case KEY_LEFT:
        case 'p': /* Previous message */
            if (s->msg_sel > 1)
            {
                s->msg_sel--;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load prev");
            }
            break;

        case CTRL('H'):
        case '<':
            /* Scroll to top of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) > 0)
                rd_scroll_up(app->reader, 1);

            break;

        case CTRL('K'):
        case '>':
            /* Scroll to bottom of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) + rd_visible(app->reader) < rd_total(app->reader))
                rd_scroll_down(app->reader, 1);

            break;

        case ',':
            /* First message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = 1;
                app->cur_msgnum = s->msgs[s->order[0]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load first");
            }
            break;

        case '.':
            /* Last message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = s->order_count;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load last");
            }
            break;
        case KEY_ALT('J'): /* Follow REPLY chain to original */
        case KEY_CLEFT:
        {
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int orig = (idx >= 0) ? ftn_find_original(s->msgs, s->msg_count, idx) : -1;

            /* Fallback: search by MSGID string from REPLY kludge */
            if (orig < 0 && app->cur_reply[0])
                orig = ftn_find_original_by_msgid(s->msgs, s->msg_count, app->cur_reply);

            if (orig < 0)
                ui_status(app, "No original message in this area");
            else if (orig >= s->msg_count)
                ui_status(app, "Invalid original message index");
            else if (s->msgs[orig].from[0] == '\0' || s->msgs[orig].subject[0] == '\0')
                ui_status(app, "Original message is empty or corrupted");
            else
            {
                int rc = reader_goto_raw(app, orig);

                if (rc == -2)
                    ui_status(app, "Original not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load original");
            }

            break;
        }
        case KEY_CRIGHT: /* Follow thread to reply (with picker if branched) */
        {
#define MAX_REPLIES 256
            int raw_replies[MAX_REPLIES];
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int nreplies = (idx >= 0) ? ftn_find_all_replies(s->msgs, s->msg_count, idx, raw_replies, MAX_REPLIES) : 0;

            if (nreplies == 0)
            {
                ui_status(app, "No replies to this message");
            }
            else if (nreplies == 1)
            {
                int rc = reader_goto_raw(app, raw_replies[0]);
                if (rc == -2)
                    ui_status(app, "Reply not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load reply");
            }
            else
            {
                /* Multiple replies: build label list and show picker */
                const char *items[MAX_REPLIES];
                char labels[MAX_REPLIES][80];
                int i, choice;

                for (i = 0; i < nreplies; i++)
                {
                    const JamMsgInfo *m = &s->msgs[raw_replies[i]];

                    snprintf(labels[i], sizeof(labels[i]), "#%-5u  %-20.20s  %.28s", (unsigned)m->msgnum, m->from, m->subject);
                    items[i] = labels[i];
                }

                choice = ui_popup_list("Select reply", items, nreplies, 0);

                if (choice >= 0)
                {
                    int rc = reader_goto_raw(app, raw_replies[choice]);

                    if (rc == -2)
                        ui_status(app, "Reply not visible in current filter");
                    else if (rc != 0)
                        ui_status(app, "Cannot load reply");
                }
            }
#undef MAX_REPLIES
            break;
        }

        case 'c':
        case 'C':
        {
            char new_view[32], new_out[32];

            new_view[0] = '\0';
            new_out[0] = '\0';

            if (ui_popup_charset_pair(app->view_charset, app->edit_charset, CHARSET_READ_DEFAULT, app->cfg->charset, new_view, sizeof(new_view), new_out, sizeof(new_out)) == 0)
            {
                /* Empty string = Auto (CHARSET_READ_DEFAULT); load_msg treats "" as no override */
                strncpy(app->view_charset, new_view, sizeof(app->view_charset) - 1);
                app->view_charset[sizeof(app->view_charset) - 1] = '\0';

                strncpy(app->edit_charset, new_out, sizeof(app->edit_charset) - 1);
                app->edit_charset[sizeof(app->edit_charset) - 1] = '\0';

                /* Mark that user manually changed charset (from reader) - only if NOT Auto */
                if (new_out[0] != '\0')
                    app->edit_charset_manually_changed = 1;

                /* Reload with new charset; preserves headers, lastread, ANSI, kludges */
                load_msg(app, app->cur_msgnum);

                ui_status(app, "View: %s | Save: %s", new_view[0] ? new_view : CHARSET_READ_DEFAULT, new_out[0] ? new_out : (app->cfg->charset[0] ? app->cfg->charset : "UTF-8"));
            }

            break;
        }

        case 'k':
        case 'K':
            rd_toggle_kludges(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case 'v':
        case 'V':
            rd_toggle_hiddklud(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Hidden+Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case KEY_F(5): /* Search in message body */
        case '/':
        {
            wchar_t tmp[64];
            int *rows = NULL, *cols = NULL;
            int match_count;
            int i;
            const char **contexts = NULL;
            char **context_bufs = NULL;
            int *line_nums = NULL;

            wcsncpy(tmp, app->reader_search.query, 63);
            tmp[63] = L'\0';

            if (ui_popup_input("Search", "Text:", tmp, 64) == 0 && tmp[0])
            {
                wcsncpy(app->reader_search.query, tmp, 63);
                app->reader_search.query[63] = L'\0';

                /* Find all matches in all lines (no limit) */
                match_count = rd_search_all(app->reader, app->reader_search.query, &rows, &cols);

                /* Free previous search matches */
                reader_clear_search(app);

                if (match_count == 0)
                {
                    ui_status(app, "Not found");
                }
                else if (match_count == 1)
                {
                    /* Single match: scroll to it */
                    rd_set_page(app->reader, LINES - 8);

                    if (rows[0] < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - rows[0]);
                    else if (rows[0] >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, rows[0] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Found at line %d", rows[0] + 1);

                    /* Save matches for highlighting and activate search mode */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
                else
                {
                    /* Allocate arrays for display */
                    contexts = (const char **)malloc((size_t)match_count * sizeof(const char *));
                    context_bufs = (char **)malloc((size_t)match_count * sizeof(char *));
                    line_nums = (int *)malloc((size_t)match_count * sizeof(int));

                    if (!contexts || !context_bufs || !line_nums)
                    {
                        if (contexts)
                            free(contexts);

                        if (context_bufs)
                            free(context_bufs);

                        if (line_nums)
                            free(line_nums);

                        free(rows);
                        free(cols);
                        ui_status(app, "Memory error");

                        break;
                    }

                    /* Multiple matches: show list */
                    for (i = 0; i < match_count; i++)
                    {
                        const wchar_t *line = rd_get_line(app->reader, rows[i]);
                        int line_len = rd_get_len(app->reader, rows[i]);

                        context_bufs[i] = (char *)malloc(128);

                        if (!context_bufs[i])
                        {
                            context_bufs[i] = NULL;
                            contexts[i] = "";
                        }
                        else
                        {
                            /* Convert context to UTF-8 for display (max 60 chars) */
                            if (line && line_len > 0)
                            {
                                int copy_len = line_len;

                                if (copy_len > 60)
                                    copy_len = 60;

                                char *utf8 = wcs_to_utf8(line, copy_len);

                                if (utf8)
                                {
                                    snprintf(context_bufs[i], 128, "%s", utf8);
                                    free(utf8);
                                }
                                else
                                {
                                    context_bufs[i][0] = '\0';
                                }
                            }
                            else
                            {
                                context_bufs[i][0] = '\0';
                            }
                        }

                        contexts[i] = context_bufs[i];
                        line_nums[i] = rows[i] + 1; /* 1-based for display */
                    }

                    /* Show popup with results */
                    int choice = ui_popup_search_results("Search Results", line_nums, contexts, match_count, 0);

                    if (choice >= 0)
                    {
                        rd_set_page(app->reader, LINES - 8);

                        if (rows[choice] < rd_top(app->reader))
                            rd_scroll_up(app->reader, rd_top(app->reader) - rows[choice]);
                        else if (rows[choice] >= rd_top(app->reader) + rd_visible(app->reader))
                            rd_scroll_down(app->reader, rows[choice] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                        app->reader_search.current_match = choice;
                        app->reader_search.match_current = choice + 1;
                        ui_status(app, "Jumped to line %d", rows[choice] + 1);
                    }
                    else
                    {
                        ui_status(app, "Search cancelled");
                    }

                    /* Free allocated memory */
                    for (i = 0; i < match_count; i++)
                    {
                        if (context_bufs[i])
                            free(context_bufs[i]);
                    }

                    free(context_bufs);
                    free(contexts);
                    free(line_nums);

                    /* Save matches for highlighting and activate search mode (don't free rows/cols) */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
            }
            break;
        }

        case 'a':
        case 'A':
            rd_toggle_ansi(app->reader);
            app->cfg->viewansi = rd_ansi_visible(app->reader);
#ifdef PLATFORM_AMIGA
            amiga_change_font(rd_ansi_visible(app->reader));
#endif
            /* Reload message to apply/re-apply ANSI parsing with new font */
            load_msg(app, app->cur_msgnum);
            /* Continue to next iteration to redraw with new font */
            continue;

        case 'r':
        case 'R':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_reply(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'e':
        case 'E':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_edit(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'N':
        case KEY_IC: /* New message */
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_new(app);
            return VIEW_EDITOR;

        case '\r':
        case '\n':
        case KEY_ENTER:
            /* Page down or advance to next message at bottom */
            if (rd_top(app->reader) + rd_visible(app->reader) >= rd_total(app->reader))
            {
                if (s->msg_sel < s->order_count)
                {
                    s->msg_sel++;
                    app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load next");
                }
                else
                {
                    /* At last message: try to go to next area */
                    try_goto_next_area(app);
                }
            }
            else
                rd_page_down(app->reader);
            break;

        case CTRL('G'): /* Goto line number */
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Line number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long line_num = 0;
                int target_line;

                if (u)
                {
                    line_num = strtol(u, NULL, 10);
                    free(u);
                }

                target_line = (int)(line_num - 1); /* Convert to 0-based */

                if (target_line >= 0 && target_line < rd_total(app->reader))
                {
                    rd_set_page(app->reader, LINES - 8);

                    /* Scroll to target line */
                    if (target_line < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - target_line);
                    else if (target_line >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, target_line - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Jumped to line %ld", line_num);
                }
                else
                {
                    ui_status(app, "Invalid line number");
                }
            }
            break;
        }

        case KEY_ALT('G'): /* Clear search highlights */
        {
            reader_clear_search(app);
            ui_status(app, "Search highlights cleared");

            continue; /* Force redraw */
        }

        /* Navigate to previous match in reader */
        case KEY_F(3):
        case KEY_ALT('P'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match - 1 + app->reader_search.match_count) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        /* Navigate to next match in reader */
        case KEY_F(4):
        case KEY_ALT('N'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match + 1) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        case 'g': /* Goto message number */
        case 'G':
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Message number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long mn = 0;

                if (u)
                {
                    mn = strtol(u, NULL, 10);
                    free(u);
                }

                int i, found = -1;

                for (i = 0; i < s->order_count; i++)
                {
                    if ((long)s->msgs[s->order[i]].msgnum == mn)
                    {
                        found = i;
                        break;
                    }
                }

                if (found >= 0)
                {
                    s->msg_sel = found + 1;
                    app->cur_msgnum = (uint32_t)mn;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load message");
                }
                else
                    ui_status(app, "No such message in this view");
            }

            break;
        }

        case 'w': /* Write message to text file */
        case 'W':
        case KEY_F(7):
        {
            char path[256];
            char out_cs[CHARSET_NAME_MAX];
            UiSession *s;
            char *body_utf8;
            int written;

            path[0] = '\0';
            out_cs[0] = '\0';

            snprintf(path, sizeof(path), "msg_%u.txt", (unsigned)app->cur_msgnum);

            /*if (ui_popup_input("Write message", "File path:", path, sizeof(path)) != 0 || !path[0])
                break;
            */

            if (ui_files_save("Write message", NULL, path, path, sizeof(path)) != 0)
                break;

            /* Charset of the output file. Empty == AUTO == write UTF-8
             * verbatim; the user can type "CP437", "LATIN-1", etc */
            if (ui_popup_charset("Output charset", "", out_cs, sizeof(out_cs)) != 0)
                break;

            s = &app->sess;
            body_utf8 = wrapper_read_utf8_ex(&s->jam, app->cur_msgnum, app->view_charset[0] ? app->view_charset : NULL, NULL, app->msg_charset, sizeof(app->msg_charset));

            if (!body_utf8)
            {
                ui_status(app, "Cannot read message body");
                break;
            }

            written = rd_export_to_file(app->reader, body_utf8, path, out_cs[0] ? out_cs : NULL);
            free(body_utf8);

            if (written < 0)
                ui_status(app, "Cannot write %s", path);
            else
                ui_status(app, "Wrote %d lines to %s (%s)", written, path, out_cs[0] ? out_cs : "UTF-8");

            break;
        }
        case 'd':
        case 'D':
        case KEY_DC: /* Delete message */
            if (ui_popup_confirm("Delete", "Mark this message as deleted?") == 1)
            {
                if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
                {
                    if (jam_delete_msg(&s->jam, app->cur_msgnum) == 0)
                        ui_status(app, "Message deleted");

                    jam_unlock(&s->jam);
                    free(s->msgs);

                    s->msgs = NULL;
                    s->msgs = jam_load_headers(&s->jam, &s->msg_count, 0, (uint32_t)app->cfg->msglistmax);

                    if (!s->msgs)
                    {
                        s->msg_count = 0;
                        ui_status(app, "Reload failed after delete");
                    }
                    else
                    {
                        ui_session_rebuild_order(app);
                    }
                }
            }

            reader_save_lastread(app);

            /* reader_save_lastread() already updates area counts; skip redundant call */
            app->cfg->viewansi = saved_viewansi;

            return reader_exit_view(app);

            break;

        case KEY_F(1):
        case '?':
            ui_popup_help("Reader Help", READER_HELP, READER_HELP_N);
            break;

        case 'l':
        case 'L':
            /* Show msglist overlay; from search go to search browser instead */
            if (app->from_search)
            {
                reader_clear_search(app);
                app->cfg->viewansi = saved_viewansi;
                return VIEW_SEARCH_RESULTS;
            }

            reader_clear_search(app);
            reader_save_lastread(app);
            update_area_counts_in_memory(app);

            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 1;

            return VIEW_MSGLIST;

        case 27:
            /* ESC: exit search mode first, otherwise quit */
            if (app->reader_search.only_mode)
            {
                reader_clear_search(app);
                ui_status(app, "Search mode exited");

                continue; /* Force redraw */
            }
            /* Fall through to quit */
        case 'q':
        case 'Q':
            reader_save_lastread(app);
            update_area_counts_in_memory(app);
            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 0;

            reader_clear_search(app);

            return reader_exit_view(app);

        case KEY_RESIZE:
            flushinp();
            load_msg(app, app->cur_msgnum);
            break;
        case CTRL('F'):
            ui_popup_freq(app);
            break;
        default:
            break;
        }
    }
}

/*
 * crashedit - Message area editor for AmigaOS
 *
 * This file is part of the crashedit project.
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This program uses JAMLIB, which is licensed under the GNU Lesser
 * General Public License v2.1. See src/jamlib/LICENSE for details.
 */

/* ui_reader.c -- Message reader view */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"
#include "ui_attr.h"
#include "ui_files.h"
#include "../../src/jamlib/jam.h"
#include "../core/msghdr.h"
#include "../core/ftn.h"
#include "../core/ansi.h"
#include "../core/keys.h"

static const char *READER_HELP[] =
    {
        "Message Reader - Key Bindings:",
        "",
        "  Up/Down,j/k    Scroll body up/down",
#ifdef PLATFORM_AMIGA
        "  Ctrl+U/D       Page scroll",
        "  Ctrl+B/E       First / last line",
#else
        "  PgUp/PgDn      Page scroll",
        "  Home/End       First / last line",
#endif
        "  Left/Right,p/n Previous / next message",
        "",
        "  b/Space        Page up/down",
        "  Enter          Page down or advance to next message",
        "  < >            First / last line (scroll)",
        "  , .            First / last message",
        "  Ctrl-J         Jump to original (follow reply chain)",
        "  Ctrl+Left      Jump to original (follow reply chain)",
        "  Ctrl+Right     Jump to reply (pick if multiple)",
        "",
        "  k, v           Toggle kludges (^A, SEEN-BY, Via)",
        "  a              Toggle ANSI color rendering",
        "  c              Change DISPLAY charset (re-render)",
        "  r              Reply to this message",
        "  e              Edit this message",
        "  n, Ins         New message (in current area)",
        "  l, L           Show message list overlay (ESC returns here)",
        "  g              Goto message number",
        "  Ctrl+H         Scroll to top of message",
        "  Ctrl+K         Scroll to bottom of message",
        "  Ctrl+G         Goto line number",
        "  Alt+J          Follow reply chain to original",
        "  Alt+G          Clear search highlights",
        "  F5, /          Search in message body",
        "  F3 Alt+P       Previous match (search mode)",
        "  F4 Alt+N       Next match (search mode)",
        "  w, F7          Write message to text file",
        "  d, Del         Delete this message",
        "  Ctrl+F         File request",
        "",
        "  ESC, q         Back to message list",
        "  F1, ?          This help"};
#define READER_HELP_N ((int)(sizeof(READER_HELP) / sizeof(READER_HELP[0])))

void ui_editor_prep_new(UiApp *app);
void ui_editor_prep_reply(UiApp *app, uint32_t orig_msgnum);
void ui_editor_prep_edit(UiApp *app, uint32_t msgnum);

/* Update area's cached counts after reader moves lastread/lastseen */
static void update_area_counts_in_memory(UiApp *app)
{
    UiSession *s;
    AreaEntry *ae;
    int i, unread, new_count;

    if (!app)
        return;

    s = &app->sess;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return;

    ae = &app->areas->entries[s->area_idx];

    ae->lastread = s->lastread;
    ae->lastseen = s->lastseen;

    /* Recompute unread (above lastread) and new_count (above lastseen) */
    unread = 0;
    new_count = 0;

    for (i = 0; i < s->msg_count; i++)
    {
        if (s->msgs[i].msgnum > s->lastread)
            unread++;

        if (s->msgs[i].msgnum > s->lastseen)
            new_count++;
    }

    ae->unread = unread;
    ae->new_count = new_count;
    ae->total_msgs = s->msg_count;
}

/* Update lastread and persist to JAM; skip if from search (browsing shouldn't mutate) */
static void reader_save_lastread(UiApp *app)
{
    UiSession *s;
    uint32_t msgnum;

    if (!app)
        return;

    if (app->from_search)
        return;

    s = &app->sess;
    msgnum = app->cur_msgnum;

    s->lastread = msgnum;

    if (msgnum > s->lastseen)
        s->lastseen = msgnum;

    if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
    {
        jam_write_lastread(&s->jam, s->user_crc, s->lastread, s->lastseen);
        jam_unlock(&s->jam);
    }

    update_area_counts_in_memory(app);
}

/* Exit target: msglist (normal) or search results (if from search) */
static UiView reader_exit_view(const UiApp *app)
{
    return app->from_search ? VIEW_SEARCH_RESULTS : VIEW_MSGLIST;
}

/* Strip FTS-1 kludge lines and SEEN-BY/PATH trailers for ANSI rendering
 * Returns malloc'd buffer (caller frees), NULL on failure */
static char *strip_kludges_for_ansi(const char *raw, int raw_len, int *out_len)
{
    char *out;
    int i = 0;
    int j = 0;
    int at_line_start = 1;

    if (out_len)
        *out_len = 0;

    if (!raw || raw_len <= 0)
        return NULL;

    out = (char *)malloc((size_t)raw_len + 1);
    if (!out)
        return NULL;

    while (i < raw_len)
    {
        if (at_line_start)
        {
            const char *p = raw + i;
            int rem = raw_len - i;
            int is_kludge = 0;

            if (rem >= 1 && (unsigned char)*p == 0x01)
                is_kludge = 1;
            else if (rem >= 8 && memcmp(p, "SEEN-BY:", 8) == 0)
                is_kludge = 1;
            else if (rem >= 5 && memcmp(p, "PATH:", 5) == 0)
                is_kludge = 1;

            if (is_kludge)
            {
                /* Skip to end of this line (eat optional \r and \n) */
                while (i < raw_len && raw[i] != '\r' && raw[i] != '\n')
                    i++;

                if (i < raw_len && raw[i] == '\r')
                    i++;
                if (i < raw_len && raw[i] == '\n')
                    i++;

                /* Still at line start for the next iteration */
                continue;
            }

            at_line_start = 0;
        }

        out[j++] = raw[i];

        if (raw[i] == '\r' || raw[i] == '\n')
            at_line_start = 1;

        i++;
    }

    out[j] = '\0';

    if (out_len)
        *out_len = j;

    return out;
}

/* Load current message into reader */
static int load_msg(UiApp *app, uint32_t msgnum)
{
    UiSession *s;
    int idx;
    char *body_utf8 = NULL;
    int wrap_w;
    const wchar_t *cur;
    char *cur_utf8;
    int needs_fallback;
    int ansi_on_outer = 0;
    char detected[CHARSET_NAME_MAX];
    const char *override_enc;
    char chosen[CHARSET_NAME_MAX];
    int ansi_on;
    uint32_t raw_len = 0;
    char *raw_bytes = NULL;
    int has_chrs = 0;
    const char *p;
    int oi;

    if (!app)
        return -1;

    s = &app->sess;

    idx = jam_find_by_msgnum(s->msgs, s->msg_count, msgnum);

    if (idx < 0)
        return -1;

    /* Sync msg_sel to current message for correct navigation after search/jump entry */
    for (oi = 0; oi < s->order_count; oi++)
    {
        if (s->order[oi] == idx)
        {
            s->msg_sel = oi + 1;
            break;
        }
    }

    /* Fill header */
    msghdr_load(app->hdr, &s->msgs[idx], app->areas->entries[s->area_idx].name, idx + 1, s->msg_count, ftn_effective_tz_offset(app->cfg->timezone_offset, app->cfg->timezone_is_manual));

    /* Body: ANSI->canvas, text->UTF-8. Charset: CHRS if present, else default */
    override_enc = app->view_charset[0] ? app->view_charset : NULL;

    detected[0] = '\0';
    chosen[0] = '\0';

    /* Read and decode body using wrapper_read_utf8_ex with proper fallback logic */
    body_utf8 = wrapper_read_utf8_ex(&s->jam, msgnum, override_enc, NULL, detected, sizeof(detected));

    if (!body_utf8)
        return -1;

    /* Read raw body for ANSI mode (needs original bytes, not UTF-8) */
    raw_bytes = jam_read_body(&s->jam, msgnum, &raw_len);

    if (!raw_bytes)
    {
        free(body_utf8);
        return -1;
    }

    /* Set charset variables based on wrapper_read_utf8_ex results */
    strncpy(chosen, detected, sizeof(chosen) - 1);
    chosen[sizeof(chosen) - 1] = '\0';

    /* Check if we actually have a CHRS kludge */
    has_chrs = (detected[0] != '\0');

    if (has_chrs)
    {
        strncpy(app->msg_charset, detected, sizeof(app->msg_charset) - 1);
        app->msg_charset[sizeof(app->msg_charset) - 1] = '\0';
    }
    else
    {
        app->msg_charset[0] = '\0';
    }

    /* ANSI mode: force CP437 if UTF-8 came from default/fallback (not CHRS/override)
     * Exception: when TTF is active, keep detected charset to preserve Unicode characters */
    ansi_on = (app->cfg && app->cfg->viewansi);
    ansi_on_outer = ansi_on;

    if (ansi_on && (strcasecmp(chosen, "UTF-8") == 0 || strcasecmp(chosen, "UTF8") == 0))
    {
        int user_wants_utf8 = (override_enc && (strcasecmp(override_enc, "UTF-8") == 0 || strcasecmp(override_enc, "UTF8") == 0));
        int chrs_says_utf8 = (app->msg_charset[0] && (strcasecmp(app->msg_charset, "UTF-8") == 0 || strcasecmp(app->msg_charset, "UTF8") == 0));

        if (!user_wants_utf8 && !chrs_says_utf8)
        {
            strncpy(chosen, "CP437", sizeof(chosen) - 1);
            chosen[sizeof(chosen) - 1] = '\0';
        }
    }

    /* Record actual decode charset for status bar (may differ from view_charset) */
    if (override_enc && override_enc[0])
        strncpy(app->decoded_charset, override_enc, sizeof(app->decoded_charset) - 1);
    else
        strncpy(app->decoded_charset, chosen, sizeof(app->decoded_charset) - 1);

    app->decoded_charset[sizeof(app->decoded_charset) - 1] = '\0';

    /*wrap_w = COLS - 2;*/
    wrap_w = COLS;

    if (wrap_w < 20)
        wrap_w = 20;

    if (ansi_on)
    {
        /* Strip kludge/SEEN-BY/PATH lines. Fall back to raw bytes on failure */
        int stripped_len = 0;
        char *stripped = strip_kludges_for_ansi(raw_bytes, (int)raw_len, &stripped_len);

        if (stripped)
        {
            rd_load_ansi(app->reader, stripped, stripped_len, chosen, wrap_w);
            free(stripped);
        }
        else
        {
            rd_load_ansi(app->reader, raw_bytes, (int)raw_len, chosen, wrap_w);
        }

        /* Free unused UTF-8 buffer; ANSI mode uses raw bytes. Prevents leak */
        free(body_utf8);
        body_utf8 = NULL; /* not used in this path */
    }

    /* ANSI mode: convert kludges/SEEN-BY/Origin to UTF-8 for MSGID/origin lookup */
    if (ansi_on)
    {
        int srclen = (int)raw_len;
        int dstmax = srclen * 4 + 256;

        body_utf8 = (char *)malloc((size_t)dstmax);

        if (body_utf8)
            charset_body_to_utf8(chosen, raw_bytes, srclen, body_utf8, dstmax);
    }

    free(raw_bytes);

    if (!body_utf8)
        return -1;

    /* Extract MSGID / REPLY for header display */
    app->cur_msgid[0] = '\0';
    app->cur_reply[0] = '\0';

    ftn_get_kludge_value(body_utf8, "MSGID", app->cur_msgid, sizeof(app->cur_msgid));
    ftn_get_kludge_value(body_utf8, "REPLY", app->cur_reply, sizeof(app->cur_reply));

    /* Get sender address from MSGID kludge, fallback to Origin line */

    cur = msghdr_get(app->hdr, HDR_OADDR);
    cur_utf8 = cur ? wcs_to_utf8(cur, (int)wcslen(cur)) : NULL;

    if (cur && !cur_utf8)
    {
        free(body_utf8); /* would otherwise leak on this OOM path */
        return -1;
    }

    needs_fallback = (!cur_utf8 || !cur_utf8[0] || strcmp(cur_utf8, "0:0/0") == 0);

    if (needs_fallback)
    {
        char addr[64];
        int got = -1;

        if (app->cur_msgid[0])
        {
            /* Extract address from MSGID (format: "addr serial") */
            int i;

            addr[0] = '\0';

            for (i = 0; app->cur_msgid[i] && app->cur_msgid[i] != ' ' && app->cur_msgid[i] != '\t' && i < (int)sizeof(addr) - 1; i++)
                addr[i] = app->cur_msgid[i];

            addr[i] = '\0';

            if (addr[0])
                got = 0;
        }

        if (got != 0)
            got = ftn_find_origin_address(body_utf8, addr, sizeof(addr));

        if (got == 0)
            msghdr_set_utf8(app->hdr, HDR_OADDR, addr);
    }

    free(cur_utf8);

    wrap_w = COLS; /* full width, match editor soft-wrap */

    if (wrap_w < 20)
        wrap_w = 20;

    /* ANSI: canvas loaded above, skip rd_load. Non-ANSI: rd_load handles wrap */
    if (!ansi_on_outer)
        rd_load(app->reader, body_utf8, wrap_w);

    free(body_utf8);

    rd_set_page(app->reader, LINES - 8);

    /* Apply view-config toggles (kludges visibility) */
    if (app->cfg->viewkludge != rd_kludges_visible(app->reader))
        rd_toggle_kludges(app->reader);

    return 0;
}

/* Try to go to next area in area_order. Returns 1 on success, 0 otherwise */
static int try_goto_next_area(UiApp *app)
{
    UiSession *s;
    int current_area_idx;
    int current_order_idx = -1;
    int next_area_idx = -1;
    int i;
    AreaEntry *next_ae;
    char prompt[256];

    if (!app)
        return 0;

    s = &app->sess;

    if (app->area_order_count <= 0)
        return 0;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return 0;

    current_area_idx = s->area_idx;

    /* Find current area in area_order */
    for (i = 0; i < app->area_order_count; i++)
    {
        if (app->area_order[i] == current_area_idx)
        {
            current_order_idx = i;
            break;
        }
    }

    /* Find next area in order */
    if (current_order_idx >= 0 && current_order_idx < app->area_order_count - 1)
    {
        next_area_idx = app->area_order[current_order_idx + 1];
    }

    if (next_area_idx < 0)
        return 0;

    next_ae = &app->areas->entries[next_area_idx];
    snprintf(prompt, sizeof(prompt), "Go to next area '%s'?", next_ae->name ? next_ae->name : "(area)");

    if (ui_popup_confirm("Next area", prompt) != 1)
        return 0;

    /* Save lastread before changing area */
    reader_save_lastread(app);

    /* Close current session and open next area */
    ui_session_close(app);

    if (ui_session_open(app, next_area_idx) != 0)
    {
        ui_status(app, "Cannot open area");
        return 0;
    }

    /* Load message from new area */
    if (s->order_count > 0)
    {
        /* Check if current position is on an unread message */
        uint32_t current_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        /* If not on unread message, go to last message in list */
        if (current_msgnum <= s->lastread)
            s->msg_sel = s->order_count;

        app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        if (load_msg(app, app->cur_msgnum) != 0)
        {
            ui_status(app, "Cannot load message");
            return 0;
        }
    }

    return 1;
}

/* Header bar */
static void draw_header_bar(UiApp *app)
{
    int i;
    const wchar_t *from, *to, *subj, *date;
    char attr_str[40];
    static const char SPACES[256] =
        "                                                                "
        "                                                                "
        "                                                                "
        "                                                                ";

    from = msghdr_get(app->hdr, HDR_FROM);
    to = msghdr_get(app->hdr, HDR_TO);
    subj = msghdr_get(app->hdr, HDR_SUBJECT);
    date = msghdr_get(app->hdr, HDR_DATE);
    ui_attr_build(msghdr_attr(app->hdr), attr_str, sizeof(attr_str));

    attron(COLOR_PAIR(COL_HEADER));

    for (i = 1; i < 7; i++)
    {
        int written = 0;
        move(i, 0);
        clrtoeol();

        while (written < COLS)
        {
            int chunk = COLS - written;

            if (chunk > (int)sizeof(SPACES))
                chunk = (int)sizeof(SPACES);

            mvaddnstr(i, written, SPACES, chunk);
            written += chunk;
        }
    }

    mvprintw(1, 1, "Area: %-30.30s  Msg %d/%d", ui_wcs2u8(msghdr_get(app->hdr, HDR_AREA)), msghdr_msgnum(app->hdr), msghdr_msgtotal(app->hdr));
    mvprintw(2, 1, "From: %-25.25s  %-20.20s  %s", ui_wcs2u8(from), ui_wcs2u8(msghdr_get(app->hdr, HDR_OADDR)), ui_wcs2u8(date));
    mvprintw(3, 1, "  To: %-25.25s  %s", ui_wcs2u8(to), ui_wcs2u8(msghdr_get(app->hdr, HDR_DADDR)));
    mvprintw(4, 1, "Subj: %s", ui_wcs2u8(subj));

    /* Attr on left, MSGID on right */
    mvprintw(5, 1, "Attr: %s", attr_str);

    if (app->cur_msgid[0])
    {
        int show = (int)strlen(app->cur_msgid);
        int max_room = COLS - 22;

        if (max_room < 10)
            max_room = 10;

        if (show > max_room)
            show = max_room;

        mvprintw(5, COLS - show - 8, "MSGID: %.*s", show, app->cur_msgid);
    }

    attroff(COLOR_PAIR(COL_HEADER));

    attron(COLOR_PAIR(COL_BORDER));
    ui_hline(6, 0, COLS);
    attroff(COLOR_PAIR(COL_BORDER));
}

/* Body rendering */
/* Convert one wchar_t to UTF-8 in buf. Returns bytes written */
static int wc_to_utf8_bytes(wchar_t wc, char *buf)
{
    unsigned long cp = (unsigned long)wc;

    if (cp < 0x80)
    {
        buf[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));

        return 2;
    }

    if (cp < 0x10000)
    {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));

        return 3;
    }

    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));

    return 4;
}

/* Draw ANSI line: collect runs of same attrs and emit each run in one call */
static void draw_ansi_line(int row_y, const wchar_t *wcs, int len, const struct AnsiCell *cells, int max_cols)
{
    char run_buf[1024];
    int i = 0;
    int x = 0;

    while (i < len && x < max_cols)
    {
        int run_color = cells[i].color_pair;
        int run_attrs = cells[i].attrs;
        int run_start = i;
        int run_chars = 0;
        int run_bytes = 0;

        /* Find extent of run with identical (color, attrs) */
        while (i < len && x + run_chars < max_cols && cells[i].color_pair == run_color && cells[i].attrs == run_attrs)
        {
            int n;

            if (run_bytes + 4 >= (int)sizeof(run_buf))
                break; /* Don't overflow run buffer; emit and continue */

            n = wc_to_utf8_bytes(wcs[i], &run_buf[run_bytes]);
            run_bytes += n;
            run_chars++;
            i++;
        }

        if (run_chars == 0)
            break; /* safety */

        run_buf[run_bytes] = '\0';

        if (run_color >= 0)
            attron(COLOR_PAIR(run_color) | run_attrs);

        mvaddnstr(row_y, x, run_buf, run_bytes);

        if (run_color >= 0)
            attroff(COLOR_PAIR(run_color) | run_attrs);

        x += run_chars;
    }
}

static void draw_body(UiApp *app)
{
    int rows = LINES - 8;
    int i;
    const wchar_t *wcs;
    int wlen;

    if (rows < 1)
        return;

    rd_set_page(app->reader, rows);

    for (i = 0; i < rows; i++)
    {
        int vi = rd_top(app->reader) + i;
        int type, col;
        int ansi_color, ansi_attrs;
        const struct AnsiCell *cells;
        int row_y = 7 + i;
        const wchar_t *ln;
        int lnlen;

        /* Skip if terminal shrunk since rd_set_page */
        if (row_y < 0 || row_y >= LINES - 1)
            continue;

        move(row_y, 0);
        clrtoeol();

        if (vi >= rd_total(app->reader))
            continue;

        type = rd_get_type(app->reader, vi);
        ansi_color = rd_get_ansi_color(app->reader, vi);
        ansi_attrs = rd_get_ansi_attrs(app->reader, vi);
        cells = rd_get_ansi_cells(app->reader, vi);

        /* ANSI with per-cell attrs: draw via canvas (preserves BBS art colors) */
        if (rd_ansi_visible(app->reader) && cells)
        {
            ln = rd_get_line(app->reader, vi);
            lnlen = rd_get_len(app->reader, vi);

            if (ln && lnlen > 0)
                draw_ansi_line(row_y, ln, lnlen, cells, (COLS < 80) ? COLS : 80);

            continue; /* attrs handled inside draw_ansi_line */
        }

        if (rd_ansi_visible(app->reader) && ansi_color >= 0)
        {
            col = ansi_color;
            attron(COLOR_PAIR(col) | ansi_attrs);
        }
        else
        {
            col = ui_color_for_type(type);
            attron(COLOR_PAIR(col));
            ansi_attrs = 0;
        }

        /* Plain or ANSI without per-cell attrs: draw with line color */
        ln = rd_get_line(app->reader, vi);
        lnlen = rd_get_len(app->reader, vi);

        /* Skip leading ^A on kludge lines */
        if (ln && lnlen > 0 && (type & FTN_LT_KLUDGE) && ln[0] == L'\x01')
        {
            ln++;
            lnlen--;
        }

        if (ln && lnlen > 0)
            mvaddnwstr(row_y, 0, ln, lnlen);

        /* Highlight search matches (word-level) */
        if (app->reader_search.rows && app->reader_search.cols && app->reader_search.match_count > 0)
        {
            int line_idx = rd_get_line_idx(app->reader, vi);
            int j;

            if (line_idx >= 0)
            {
                for (j = 0; j < app->reader_search.match_count; j++)
                {
                    if (app->reader_search.rows[j] == line_idx)
                    {
                        int match_col = app->reader_search.cols[j];
                        int match_len = (int)wcslen(app->reader_search.query);

                        if (match_col >= 0 && match_col + match_len <= lnlen)
                        {
                            if (j == app->reader_search.current_match)
                                attron(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attron(COLOR_PAIR(COL_SEARCH_MATCH));

                            mvaddnwstr(row_y, match_col, &ln[match_col], match_len);

                            if (j == app->reader_search.current_match)
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
            }
        }

        attroff(COLOR_PAIR(col) | ansi_attrs);
    }
}

/* Clear search state in reader */
static void reader_clear_search(UiApp *app)
{
    if (app->reader_search.rows)
    {
        free(app->reader_search.rows);
        app->reader_search.rows = NULL;
    }

    if (app->reader_search.cols)
    {
        free(app->reader_search.cols);
        app->reader_search.cols = NULL;
    }

    app->reader_search.match_count = 0;
    app->reader_search.is_mode = 0;
    app->reader_search.only_mode = 0;
    app->reader_search.current_match = 0;
    app->reader_search.match_current = 0;
}

/* Navigate to a raw msgs[] index, honouring the current sort order */
static int reader_goto_raw(UiApp *app, int raw_idx)
{
    UiSession *s = &app->sess;
    uint32_t target_mn;
    int i;

    if (raw_idx < 0 || raw_idx >= s->msg_count)
        return -1; /* invalid index */

    target_mn = s->msgs[raw_idx].msgnum;

    for (i = 0; i < s->order_count; i++)
    {
        if (s->msgs[s->order[i]].msgnum == target_mn)
        {
            s->msg_sel = i + 1;
            app->cur_msgnum = target_mn;

            return load_msg(app, target_mn);
        }
    }

    return -2;
}

/* Main loop */
UiView ui_reader_run(UiApp *app)
{
    int ch;
    UiSession *s;
    int saved_viewansi;

    if (!app)
        return VIEW_QUIT;

    s = &app->sess;
    saved_viewansi = app->cfg->viewansi;

    if (load_msg(app, app->cur_msgnum) != 0)
    {
        ui_status(app, "Cannot load message");
        app->cfg->viewansi = saved_viewansi;
        return reader_exit_view(app);
    }

    /* Apply ANSI visibility from config on first load */
    if (app->cfg->viewansi != rd_ansi_visible(app->reader))
    {
        rd_toggle_ansi(app->reader);

#ifdef PLATFORM_AMIGA
        amiga_change_font(rd_ansi_visible(app->reader));
#endif
    }

    for (;;)
    {
        erase();

        ui_draw_menubar(app, "Reader");
        draw_header_bar(app);
        draw_body(app);

        /* Status: msg_charset (CHRS or ?) -> decoded_charset (actual decode) */
        ui_status(app, "Charset: %s -> %s | c = Change charset%s | Ln %d/%d",
                  app->msg_charset[0] ? app->msg_charset : "?",
                  (app->view_charset[0]) ? app->decoded_charset : "Auto",
                  rd_ansi_visible(app->reader) ? " | ANSI" : "",
                  rd_top(app->reader) + 1, rd_total(app->reader));

        ui_draw_statusbar(app);
        refresh();

        ch = wrapper_getch();

        switch (ch)
        {
        case KEY_UP:
            rd_scroll_up(app->reader, 1);
            break;
        case KEY_DOWN:
            rd_scroll_down(app->reader, 1);
            break;
        case KEY_PPAGE:
        case 'b':
        case 'B':
        case CTRL('U'): /* Page Up (Amiga) */
            rd_page_up(app->reader);
            break;
        case KEY_NPAGE:
        case CTRL('D'): /* Page Down (Amiga) */
        case ' ':
            rd_page_down(app->reader);
            break;
        case KEY_HOME:
        case CTRL('B'): /* Home (Amiga) */
            rd_home(app->reader);
            break;
        case KEY_END:
        case CTRL('E'): /* End (Amiga) */
            rd_end(app->reader);
            break;

        case KEY_RIGHT:
        case 'n': /* Next message */
            if (s->msg_sel < s->order_count)
            {
                s->msg_sel++;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load next");
            }
            else
            {
                /* At last message: try to go to next area */
                try_goto_next_area(app);
            }
            break;
        case KEY_LEFT:
        case 'p': /* Previous message */
            if (s->msg_sel > 1)
            {
                s->msg_sel--;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load prev");
            }
            break;

        case CTRL('H'):
        case '<':
            /* Scroll to top of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) > 0)
                rd_scroll_up(app->reader, 1);

            break;

        case CTRL('K'):
        case '>':
            /* Scroll to bottom of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) + rd_visible(app->reader) < rd_total(app->reader))
                rd_scroll_down(app->reader, 1);

            break;

        case ',':
            /* First message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = 1;
                app->cur_msgnum = s->msgs[s->order[0]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load first");
            }
            break;

        case '.':
            /* Last message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = s->order_count;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load last");
            }
            break;
        case KEY_ALT('J'): /* Follow REPLY chain to original */
        case KEY_CLEFT:
        {
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int orig = (idx >= 0) ? ftn_find_original(s->msgs, s->msg_count, idx) : -1;

            /* Fallback: search by MSGID string from REPLY kludge */
            if (orig < 0 && app->cur_reply[0])
                orig = ftn_find_original_by_msgid(s->msgs, s->msg_count, app->cur_reply);

            if (orig < 0)
                ui_status(app, "No original message in this area");
            else if (orig >= s->msg_count)
                ui_status(app, "Invalid original message index");
            else if (s->msgs[orig].from[0] == '\0' || s->msgs[orig].subject[0] == '\0')
                ui_status(app, "Original message is empty or corrupted");
            else
            {
                int rc = reader_goto_raw(app, orig);

                if (rc == -2)
                    ui_status(app, "Original not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load original");
            }

            break;
        }
        case KEY_CRIGHT: /* Follow thread to reply (with picker if branched) */
        {
#define MAX_REPLIES 256
            int raw_replies[MAX_REPLIES];
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int nreplies = (idx >= 0) ? ftn_find_all_replies(s->msgs, s->msg_count, idx, raw_replies, MAX_REPLIES) : 0;

            if (nreplies == 0)
            {
                ui_status(app, "No replies to this message");
            }
            else if (nreplies == 1)
            {
                int rc = reader_goto_raw(app, raw_replies[0]);
                if (rc == -2)
                    ui_status(app, "Reply not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load reply");
            }
            else
            {
                /* Multiple replies: build label list and show picker */
                const char *items[MAX_REPLIES];
                char labels[MAX_REPLIES][80];
                int i, choice;

                for (i = 0; i < nreplies; i++)
                {
                    const JamMsgInfo *m = &s->msgs[raw_replies[i]];

                    snprintf(labels[i], sizeof(labels[i]), "#%-5u  %-20.20s  %.28s", (unsigned)m->msgnum, m->from, m->subject);
                    items[i] = labels[i];
                }

                choice = ui_popup_list("Select reply", items, nreplies, 0);

                if (choice >= 0)
                {
                    int rc = reader_goto_raw(app, raw_replies[choice]);

                    if (rc == -2)
                        ui_status(app, "Reply not visible in current filter");
                    else if (rc != 0)
                        ui_status(app, "Cannot load reply");
                }
            }
#undef MAX_REPLIES
            break;
        }

        case 'c':
        case 'C':
        {
            char new_view[32], new_out[32];

            new_view[0] = '\0';
            new_out[0] = '\0';

            if (ui_popup_charset_pair(app->view_charset, app->edit_charset, CHARSET_READ_DEFAULT, app->cfg->charset, new_view, sizeof(new_view), new_out, sizeof(new_out)) == 0)
            {
                /* Empty string = Auto (CHARSET_READ_DEFAULT); load_msg treats "" as no override */
                strncpy(app->view_charset, new_view, sizeof(app->view_charset) - 1);
                app->view_charset[sizeof(app->view_charset) - 1] = '\0';

                strncpy(app->edit_charset, new_out, sizeof(app->edit_charset) - 1);
                app->edit_charset[sizeof(app->edit_charset) - 1] = '\0';

                /* Mark that user manually changed charset (from reader) - only if NOT Auto */
                if (new_out[0] != '\0')
                    app->edit_charset_manually_changed = 1;

                /* Reload with new charset; preserves headers, lastread, ANSI, kludges */
                load_msg(app, app->cur_msgnum);

                ui_status(app, "View: %s | Save: %s", new_view[0] ? new_view : CHARSET_READ_DEFAULT, new_out[0] ? new_out : (app->cfg->charset[0] ? app->cfg->charset : "UTF-8"));
            }

            break;
        }

        case 'k':
        case 'K':
            rd_toggle_kludges(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case 'v':
        case 'V':
            rd_toggle_hiddklud(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Hidden+Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case KEY_F(5): /* Search in message body */
        case '/':
        {
            wchar_t tmp[64];
            int *rows = NULL, *cols = NULL;
            int match_count;
            int i;
            const char **contexts = NULL;
            char **context_bufs = NULL;
            int *line_nums = NULL;

            wcsncpy(tmp, app->reader_search.query, 63);
            tmp[63] = L'\0';

            if (ui_popup_input("Search", "Text:", tmp, 64) == 0 && tmp[0])
            {
                wcsncpy(app->reader_search.query, tmp, 63);
                app->reader_search.query[63] = L'\0';

                /* Find all matches in all lines (no limit) */
                match_count = rd_search_all(app->reader, app->reader_search.query, &rows, &cols);

                /* Free previous search matches */
                reader_clear_search(app);

                if (match_count == 0)
                {
                    ui_status(app, "Not found");
                }
                else if (match_count == 1)
                {
                    /* Single match: scroll to it */
                    rd_set_page(app->reader, LINES - 8);

                    if (rows[0] < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - rows[0]);
                    else if (rows[0] >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, rows[0] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Found at line %d", rows[0] + 1);

                    /* Save matches for highlighting and activate search mode */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
                else
                {
                    /* Allocate arrays for display */
                    contexts = (const char **)malloc((size_t)match_count * sizeof(const char *));
                    context_bufs = (char **)malloc((size_t)match_count * sizeof(char *));
                    line_nums = (int *)malloc((size_t)match_count * sizeof(int));

                    if (!contexts || !context_bufs || !line_nums)
                    {
                        if (contexts)
                            free(contexts);

                        if (context_bufs)
                            free(context_bufs);

                        if (line_nums)
                            free(line_nums);

                        free(rows);
                        free(cols);
                        ui_status(app, "Memory error");

                        break;
                    }

                    /* Multiple matches: show list */
                    for (i = 0; i < match_count; i++)
                    {
                        const wchar_t *line = rd_get_line(app->reader, rows[i]);
                        int line_len = rd_get_len(app->reader, rows[i]);

                        context_bufs[i] = (char *)malloc(128);

                        if (!context_bufs[i])
                        {
                            context_bufs[i] = NULL;
                            contexts[i] = "";
                        }
                        else
                        {
                            /* Convert context to UTF-8 for display (max 60 chars) */
                            if (line && line_len > 0)
                            {
                                int copy_len = line_len;

                                if (copy_len > 60)
                                    copy_len = 60;

                                char *utf8 = wcs_to_utf8(line, copy_len);

                                if (utf8)
                                {
                                    snprintf(context_bufs[i], 128, "%s", utf8);
                                    free(utf8);
                                }
                                else
                                {
                                    context_bufs[i][0] = '\0';
                                }
                            }
                            else
                            {
                                context_bufs[i][0] = '\0';
                            }
                        }

                        contexts[i] = context_bufs[i];
                        line_nums[i] = rows[i] + 1; /* 1-based for display */
                    }

                    /* Show popup with results */
                    int choice = ui_popup_search_results("Search Results", line_nums, contexts, match_count, 0);

                    if (choice >= 0)
                    {
                        rd_set_page(app->reader, LINES - 8);

                        if (rows[choice] < rd_top(app->reader))
                            rd_scroll_up(app->reader, rd_top(app->reader) - rows[choice]);
                        else if (rows[choice] >= rd_top(app->reader) + rd_visible(app->reader))
                            rd_scroll_down(app->reader, rows[choice] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                        app->reader_search.current_match = choice;
                        app->reader_search.match_current = choice + 1;
                        ui_status(app, "Jumped to line %d", rows[choice] + 1);
                    }
                    else
                    {
                        ui_status(app, "Search cancelled");
                    }

                    /* Free allocated memory */
                    for (i = 0; i < match_count; i++)
                    {
                        if (context_bufs[i])
                            free(context_bufs[i]);
                    }

                    free(context_bufs);
                    free(contexts);
                    free(line_nums);

                    /* Save matches for highlighting and activate search mode (don't free rows/cols) */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
            }
            break;
        }

        case 'a':
        case 'A':
            rd_toggle_ansi(app->reader);
            app->cfg->viewansi = rd_ansi_visible(app->reader);
#ifdef PLATFORM_AMIGA
            amiga_change_font(rd_ansi_visible(app->reader));
#endif
            /* Reload message to apply/re-apply ANSI parsing with new font */
            load_msg(app, app->cur_msgnum);
            /* Continue to next iteration to redraw with new font */
            continue;

        case 'r':
        case 'R':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_reply(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'e':
        case 'E':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_edit(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'N':
        case KEY_IC: /* New message */
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_new(app);
            return VIEW_EDITOR;

        case '\r':
        case '\n':
        case KEY_ENTER:
            /* Page down or advance to next message at bottom */
            if (rd_top(app->reader) + rd_visible(app->reader) >= rd_total(app->reader))
            {
                if (s->msg_sel < s->order_count)
                {
                    s->msg_sel++;
                    app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load next");
                }
                else
                {
                    /* At last message: try to go to next area */
                    try_goto_next_area(app);
                }
            }
            else
                rd_page_down(app->reader);
            break;

        case CTRL('G'): /* Goto line number */
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Line number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long line_num = 0;
                int target_line;

                if (u)
                {
                    line_num = strtol(u, NULL, 10);
                    free(u);
                }

                target_line = (int)(line_num - 1); /* Convert to 0-based */

                if (target_line >= 0 && target_line < rd_total(app->reader))
                {
                    rd_set_page(app->reader, LINES - 8);

                    /* Scroll to target line */
                    if (target_line < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - target_line);
                    else if (target_line >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, target_line - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Jumped to line %ld", line_num);
                }
                else
                {
                    ui_status(app, "Invalid line number");
                }
            }
            break;
        }

        case KEY_ALT('G'): /* Clear search highlights */
        {
            reader_clear_search(app);
            ui_status(app, "Search highlights cleared");

            continue; /* Force redraw */
        }

        /* Navigate to previous match in reader */
        case KEY_F(3):
        case KEY_ALT('P'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match - 1 + app->reader_search.match_count) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        /* Navigate to next match in reader */
        case KEY_F(4):
        case KEY_ALT('N'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match + 1) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        case 'g': /* Goto message number */
        case 'G':
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Message number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long mn = 0;

                if (u)
                {
                    mn = strtol(u, NULL, 10);
                    free(u);
                }

                int i, found = -1;

                for (i = 0; i < s->order_count; i++)
                {
                    if ((long)s->msgs[s->order[i]].msgnum == mn)
                    {
                        found = i;
                        break;
                    }
                }

                if (found >= 0)
                {
                    s->msg_sel = found + 1;
                    app->cur_msgnum = (uint32_t)mn;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load message");
                }
                else
                    ui_status(app, "No such message in this view");
            }

            break;
        }

        case 'w': /* Write message to text file */
        case 'W':
        case KEY_F(7):
        {
            char path[256];
            char out_cs[CHARSET_NAME_MAX];
            UiSession *s;
            char *body_utf8;
            int written;

            path[0] = '\0';
            out_cs[0] = '\0';

            snprintf(path, sizeof(path), "msg_%u.txt", (unsigned)app->cur_msgnum);

            /*if (ui_popup_input("Write message", "File path:", path, sizeof(path)) != 0 || !path[0])
                break;
            */

            if (ui_files_save("Write message", NULL, path, path, sizeof(path)) != 0)
                break;

            /* Charset of the output file. Empty == AUTO == write UTF-8
             * verbatim; the user can type "CP437", "LATIN-1", etc */
            if (ui_popup_charset("Output charset", "", out_cs, sizeof(out_cs)) != 0)
                break;

            s = &app->sess;
            body_utf8 = wrapper_read_utf8_ex(&s->jam, app->cur_msgnum, app->view_charset[0] ? app->view_charset : NULL, NULL, app->msg_charset, sizeof(app->msg_charset));

            if (!body_utf8)
            {
                ui_status(app, "Cannot read message body");
                break;
            }

            written = rd_export_to_file(app->reader, body_utf8, path, out_cs[0] ? out_cs : NULL);
            free(body_utf8);

            if (written < 0)
                ui_status(app, "Cannot write %s", path);
            else
                ui_status(app, "Wrote %d lines to %s (%s)", written, path, out_cs[0] ? out_cs : "UTF-8");

            break;
        }
        case 'd':
        case 'D':
        case KEY_DC: /* Delete message */
            if (ui_popup_confirm("Delete", "Mark this message as deleted?") == 1)
            {
                if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
                {
                    if (jam_delete_msg(&s->jam, app->cur_msgnum) == 0)
                        ui_status(app, "Message deleted");

                    jam_unlock(&s->jam);
                    free(s->msgs);

                    s->msgs = NULL;
                    s->msgs = jam_load_headers(&s->jam, &s->msg_count, 0, (uint32_t)app->cfg->msglistmax);

                    if (!s->msgs)
                    {
                        s->msg_count = 0;
                        ui_status(app, "Reload failed after delete");
                    }
                    else
                    {
                        ui_session_rebuild_order(app);
                    }
                }
            }

            reader_save_lastread(app);

            /* reader_save_lastread() already updates area counts; skip redundant call */
            app->cfg->viewansi = saved_viewansi;

            return reader_exit_view(app);

            break;

        case KEY_F(1):
        case '?':
            ui_popup_help("Reader Help", READER_HELP, READER_HELP_N);
            break;

        case 'l':
        case 'L':
            /* Show msglist overlay; from search go to search browser instead */
            if (app->from_search)
            {
                reader_clear_search(app);
                app->cfg->viewansi = saved_viewansi;
                return VIEW_SEARCH_RESULTS;
            }

            reader_clear_search(app);
            reader_save_lastread(app);
            update_area_counts_in_memory(app);

            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 1;

            return VIEW_MSGLIST;

        case 27:
            /* ESC: exit search mode first, otherwise quit */
            if (app->reader_search.only_mode)
            {
                reader_clear_search(app);
                ui_status(app, "Search mode exited");

                continue; /* Force redraw */
            }
            /* Fall through to quit */
        case 'q':
        case 'Q':
            reader_save_lastread(app);
            update_area_counts_in_memory(app);
            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 0;

            reader_clear_search(app);

            return reader_exit_view(app);

        case KEY_RESIZE:
            flushinp();
            load_msg(app, app->cur_msgnum);
            break;
        case CTRL('F'):
            ui_popup_freq(app);
            break;
        default:
            break;
        }
    }
}

/*
 * crashedit - Message area editor for AmigaOS
 *
 * This file is part of the crashedit project.
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This program uses JAMLIB, which is licensed under the GNU Lesser
 * General Public License v2.1. See src/jamlib/LICENSE for details.
 */

/* ui_reader.c -- Message reader view */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"
#include "ui_attr.h"
#include "ui_files.h"
#include "../../src/jamlib/jam.h"
#include "../core/msghdr.h"
#include "../core/ftn.h"
#include "../core/ansi.h"
#include "../core/keys.h"

static const char *READER_HELP[] =
    {
        "Message Reader - Key Bindings:",
        "",
        "  Up/Down,j/k    Scroll body up/down",
#ifdef PLATFORM_AMIGA
        "  Ctrl+U/D       Page scroll",
        "  Ctrl+B/E       First / last line",
#else
        "  PgUp/PgDn      Page scroll",
        "  Home/End       First / last line",
#endif
        "  Left/Right,p/n Previous / next message",
        "",
        "  b/Space        Page up/down",
        "  Enter          Page down or advance to next message",
        "  < >            First / last line (scroll)",
        "  , .            First / last message",
        "  Ctrl-J         Jump to original (follow reply chain)",
        "  Ctrl+Left      Jump to original (follow reply chain)",
        "  Ctrl+Right     Jump to reply (pick if multiple)",
        "",
        "  k, v           Toggle kludges (^A, SEEN-BY, Via)",
        "  a              Toggle ANSI color rendering",
        "  c              Change DISPLAY charset (re-render)",
        "  r              Reply to this message",
        "  e              Edit this message",
        "  n, Ins         New message (in current area)",
        "  l, L           Show message list overlay (ESC returns here)",
        "  g              Goto message number",
        "  Ctrl+H         Scroll to top of message",
        "  Ctrl+K         Scroll to bottom of message",
        "  Ctrl+G         Goto line number",
        "  Alt+J          Follow reply chain to original",
        "  Alt+G          Clear search highlights",
        "  F5, /          Search in message body",
        "  F3 Alt+P       Previous match (search mode)",
        "  F4 Alt+N       Next match (search mode)",
        "  w, F7          Write message to text file",
        "  d, Del         Delete this message",
        "  Ctrl+F         File request",
        "",
        "  ESC, q         Back to message list",
        "  F1, ?          This help"};
#define READER_HELP_N ((int)(sizeof(READER_HELP) / sizeof(READER_HELP[0])))

void ui_editor_prep_new(UiApp *app);
void ui_editor_prep_reply(UiApp *app, uint32_t orig_msgnum);
void ui_editor_prep_edit(UiApp *app, uint32_t msgnum);

/* Update area's cached counts after reader moves lastread/lastseen */
static void update_area_counts_in_memory(UiApp *app)
{
    UiSession *s;
    AreaEntry *ae;
    int i, unread, new_count;

    if (!app)
        return;

    s = &app->sess;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return;

    ae = &app->areas->entries[s->area_idx];

    ae->lastread = s->lastread;
    ae->lastseen = s->lastseen;

    /* Recompute unread (above lastread) and new_count (above lastseen) */
    unread = 0;
    new_count = 0;

    for (i = 0; i < s->msg_count; i++)
    {
        if (s->msgs[i].msgnum > s->lastread)
            unread++;

        if (s->msgs[i].msgnum > s->lastseen)
            new_count++;
    }

    ae->unread = unread;
    ae->new_count = new_count;
    ae->total_msgs = s->msg_count;
}

/* Update lastread and persist to JAM; skip if from search (browsing shouldn't mutate) */
static void reader_save_lastread(UiApp *app)
{
    UiSession *s;
    uint32_t msgnum;

    if (!app)
        return;

    if (app->from_search)
        return;

    s = &app->sess;
    msgnum = app->cur_msgnum;

    s->lastread = msgnum;

    if (msgnum > s->lastseen)
        s->lastseen = msgnum;

    if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
    {
        jam_write_lastread(&s->jam, s->user_crc, s->lastread, s->lastseen);
        jam_unlock(&s->jam);
    }

    update_area_counts_in_memory(app);
}

/* Exit target: msglist (normal) or search results (if from search) */
static UiView reader_exit_view(const UiApp *app)
{
    return app->from_search ? VIEW_SEARCH_RESULTS : VIEW_MSGLIST;
}

/* Strip FTS-1 kludge lines and SEEN-BY/PATH trailers for ANSI rendering
 * Returns malloc'd buffer (caller frees), NULL on failure */
static char *strip_kludges_for_ansi(const char *raw, int raw_len, int *out_len)
{
    char *out;
    int i = 0;
    int j = 0;
    int at_line_start = 1;

    if (out_len)
        *out_len = 0;

    if (!raw || raw_len <= 0)
        return NULL;

    out = (char *)malloc((size_t)raw_len + 1);
    if (!out)
        return NULL;

    while (i < raw_len)
    {
        if (at_line_start)
        {
            const char *p = raw + i;
            int rem = raw_len - i;
            int is_kludge = 0;

            if (rem >= 1 && (unsigned char)*p == 0x01)
                is_kludge = 1;
            else if (rem >= 8 && memcmp(p, "SEEN-BY:", 8) == 0)
                is_kludge = 1;
            else if (rem >= 5 && memcmp(p, "PATH:", 5) == 0)
                is_kludge = 1;

            if (is_kludge)
            {
                /* Skip to end of this line (eat optional \r and \n) */
                while (i < raw_len && raw[i] != '\r' && raw[i] != '\n')
                    i++;

                if (i < raw_len && raw[i] == '\r')
                    i++;
                if (i < raw_len && raw[i] == '\n')
                    i++;

                /* Still at line start for the next iteration */
                continue;
            }

            at_line_start = 0;
        }

        out[j++] = raw[i];

        if (raw[i] == '\r' || raw[i] == '\n')
            at_line_start = 1;

        i++;
    }

    out[j] = '\0';

    if (out_len)
        *out_len = j;

    return out;
}

/* Load current message into reader */
static int load_msg(UiApp *app, uint32_t msgnum)
{
    UiSession *s;
    int idx;
    char *body_utf8 = NULL;
    int wrap_w;
    const wchar_t *cur;
    char *cur_utf8;
    int needs_fallback;
    int ansi_on_outer = 0;
    char detected[CHARSET_NAME_MAX];
    const char *override_enc;
    char chosen[CHARSET_NAME_MAX];
    int ansi_on;
    uint32_t raw_len = 0;
    char *raw_bytes = NULL;
    int has_chrs = 0;
    const char *p;
    int oi;

    if (!app)
        return -1;

    s = &app->sess;

    idx = jam_find_by_msgnum(s->msgs, s->msg_count, msgnum);

    if (idx < 0)
        return -1;

    /* Sync msg_sel to current message for correct navigation after search/jump entry */
    for (oi = 0; oi < s->order_count; oi++)
    {
        if (s->order[oi] == idx)
        {
            s->msg_sel = oi + 1;
            break;
        }
    }

    /* Fill header */
    msghdr_load(app->hdr, &s->msgs[idx], app->areas->entries[s->area_idx].name, idx + 1, s->msg_count, ftn_effective_tz_offset(app->cfg->timezone_offset, app->cfg->timezone_is_manual));

    /* Body: ANSI->canvas, text->UTF-8. Charset: CHRS if present, else default */
    override_enc = app->view_charset[0] ? app->view_charset : NULL;

    detected[0] = '\0';
    chosen[0] = '\0';

    /* Read and decode body using wrapper_read_utf8_ex with proper fallback logic */
    body_utf8 = wrapper_read_utf8_ex(&s->jam, msgnum, override_enc, NULL, detected, sizeof(detected));

    if (!body_utf8)
        return -1;

    /* Read raw body for ANSI mode (needs original bytes, not UTF-8) */
    raw_bytes = jam_read_body(&s->jam, msgnum, &raw_len);

    if (!raw_bytes)
    {
        free(body_utf8);
        return -1;
    }

    /* Set charset variables based on wrapper_read_utf8_ex results */
    strncpy(chosen, detected, sizeof(chosen) - 1);
    chosen[sizeof(chosen) - 1] = '\0';

    /* Check if we actually have a CHRS kludge */
    has_chrs = (detected[0] != '\0');

    if (has_chrs)
    {
        strncpy(app->msg_charset, detected, sizeof(app->msg_charset) - 1);
        app->msg_charset[sizeof(app->msg_charset) - 1] = '\0';
    }
    else
    {
        app->msg_charset[0] = '\0';
    }

    /* ANSI mode: force CP437 if UTF-8 came from default/fallback (not CHRS/override)
     * Exception: when TTF is active, keep detected charset to preserve Unicode characters */
    ansi_on = (app->cfg && app->cfg->viewansi);
    ansi_on_outer = ansi_on;

    if (ansi_on && (strcasecmp(chosen, "UTF-8") == 0 || strcasecmp(chosen, "UTF8") == 0))
    {
        int user_wants_utf8 = (override_enc && (strcasecmp(override_enc, "UTF-8") == 0 || strcasecmp(override_enc, "UTF8") == 0));
        int chrs_says_utf8 = (app->msg_charset[0] && (strcasecmp(app->msg_charset, "UTF-8") == 0 || strcasecmp(app->msg_charset, "UTF8") == 0));

        if (!user_wants_utf8 && !chrs_says_utf8)
        {
            strncpy(chosen, "CP437", sizeof(chosen) - 1);
            chosen[sizeof(chosen) - 1] = '\0';
        }
    }

    /* Record actual decode charset for status bar (may differ from view_charset) */
    if (override_enc && override_enc[0])
        strncpy(app->decoded_charset, override_enc, sizeof(app->decoded_charset) - 1);
    else
        strncpy(app->decoded_charset, chosen, sizeof(app->decoded_charset) - 1);

    app->decoded_charset[sizeof(app->decoded_charset) - 1] = '\0';

    /*wrap_w = COLS - 2;*/
    wrap_w = COLS;

    if (wrap_w < 20)
        wrap_w = 20;

    if (ansi_on)
    {
        /* Strip kludge/SEEN-BY/PATH lines. Fall back to raw bytes on failure */
        int stripped_len = 0;
        char *stripped = strip_kludges_for_ansi(raw_bytes, (int)raw_len, &stripped_len);

        if (stripped)
        {
            rd_load_ansi(app->reader, stripped, stripped_len, chosen, wrap_w);
            free(stripped);
        }
        else
        {
            rd_load_ansi(app->reader, raw_bytes, (int)raw_len, chosen, wrap_w);
        }

        /* Free unused UTF-8 buffer; ANSI mode uses raw bytes. Prevents leak */
        free(body_utf8);
        body_utf8 = NULL; /* not used in this path */
    }

    /* ANSI mode: convert kludges/SEEN-BY/Origin to UTF-8 for MSGID/origin lookup */
    if (ansi_on)
    {
        int srclen = (int)raw_len;
        int dstmax = srclen * 4 + 256;

        body_utf8 = (char *)malloc((size_t)dstmax);

        if (body_utf8)
            charset_body_to_utf8(chosen, raw_bytes, srclen, body_utf8, dstmax);
    }

    free(raw_bytes);

    if (!body_utf8)
        return -1;

    /* Extract MSGID / REPLY for header display */
    app->cur_msgid[0] = '\0';
    app->cur_reply[0] = '\0';

    ftn_get_kludge_value(body_utf8, "MSGID", app->cur_msgid, sizeof(app->cur_msgid));
    ftn_get_kludge_value(body_utf8, "REPLY", app->cur_reply, sizeof(app->cur_reply));

    /* Get sender address from MSGID kludge, fallback to Origin line */

    cur = msghdr_get(app->hdr, HDR_OADDR);
    cur_utf8 = cur ? wcs_to_utf8(cur, (int)wcslen(cur)) : NULL;

    if (cur && !cur_utf8)
    {
        free(body_utf8); /* would otherwise leak on this OOM path */
        return -1;
    }

    needs_fallback = (!cur_utf8 || !cur_utf8[0] || strcmp(cur_utf8, "0:0/0") == 0);

    if (needs_fallback)
    {
        char addr[64];
        int got = -1;

        if (app->cur_msgid[0])
        {
            /* Extract address from MSGID (format: "addr serial") */
            int i;

            addr[0] = '\0';

            for (i = 0; app->cur_msgid[i] && app->cur_msgid[i] != ' ' && app->cur_msgid[i] != '\t' && i < (int)sizeof(addr) - 1; i++)
                addr[i] = app->cur_msgid[i];

            addr[i] = '\0';

            if (addr[0])
                got = 0;
        }

        if (got != 0)
            got = ftn_find_origin_address(body_utf8, addr, sizeof(addr));

        if (got == 0)
            msghdr_set_utf8(app->hdr, HDR_OADDR, addr);
    }

    free(cur_utf8);

    wrap_w = COLS; /* full width, match editor soft-wrap */

    if (wrap_w < 20)
        wrap_w = 20;

    /* ANSI: canvas loaded above, skip rd_load. Non-ANSI: rd_load handles wrap */
    if (!ansi_on_outer)
        rd_load(app->reader, body_utf8, wrap_w);

    free(body_utf8);

    rd_set_page(app->reader, LINES - 8);

    /* Apply view-config toggles (kludges visibility) */
    if (app->cfg->viewkludge != rd_kludges_visible(app->reader))
        rd_toggle_kludges(app->reader);

    return 0;
}

/* Try to go to next area in area_order. Returns 1 on success, 0 otherwise */
static int try_goto_next_area(UiApp *app)
{
    UiSession *s;
    int current_area_idx;
    int current_order_idx = -1;
    int next_area_idx = -1;
    int i;
    AreaEntry *next_ae;
    char prompt[256];

    if (!app)
        return 0;

    s = &app->sess;

    if (app->area_order_count <= 0)
        return 0;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return 0;

    current_area_idx = s->area_idx;

    /* Find current area in area_order */
    for (i = 0; i < app->area_order_count; i++)
    {
        if (app->area_order[i] == current_area_idx)
        {
            current_order_idx = i;
            break;
        }
    }

    /* Find next area in order */
    if (current_order_idx >= 0 && current_order_idx < app->area_order_count - 1)
    {
        next_area_idx = app->area_order[current_order_idx + 1];
    }

    if (next_area_idx < 0)
        return 0;

    next_ae = &app->areas->entries[next_area_idx];
    snprintf(prompt, sizeof(prompt), "Go to next area '%s'?", next_ae->name ? next_ae->name : "(area)");

    if (ui_popup_confirm("Next area", prompt) != 1)
        return 0;

    /* Save lastread before changing area */
    reader_save_lastread(app);

    /* Close current session and open next area */
    ui_session_close(app);

    if (ui_session_open(app, next_area_idx) != 0)
    {
        ui_status(app, "Cannot open area");
        return 0;
    }

    /* Load message from new area */
    if (s->order_count > 0)
    {
        /* Check if current position is on an unread message */
        uint32_t current_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        /* If not on unread message, go to last message in list */
        if (current_msgnum <= s->lastread)
            s->msg_sel = s->order_count;

        app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        if (load_msg(app, app->cur_msgnum) != 0)
        {
            ui_status(app, "Cannot load message");
            return 0;
        }
    }

    return 1;
}

/* Header bar */
static void draw_header_bar(UiApp *app)
{
    int i;
    const wchar_t *from, *to, *subj, *date;
    char attr_str[40];
    static const char SPACES[256] =
        "                                                                "
        "                                                                "
        "                                                                "
        "                                                                ";

    from = msghdr_get(app->hdr, HDR_FROM);
    to = msghdr_get(app->hdr, HDR_TO);
    subj = msghdr_get(app->hdr, HDR_SUBJECT);
    date = msghdr_get(app->hdr, HDR_DATE);
    ui_attr_build(msghdr_attr(app->hdr), attr_str, sizeof(attr_str));

    attron(COLOR_PAIR(COL_HEADER));

    for (i = 1; i < 7; i++)
    {
        int written = 0;
        move(i, 0);
        clrtoeol();

        while (written < COLS)
        {
            int chunk = COLS - written;

            if (chunk > (int)sizeof(SPACES))
                chunk = (int)sizeof(SPACES);

            mvaddnstr(i, written, SPACES, chunk);
            written += chunk;
        }
    }

    mvprintw(1, 1, "Area: %-30.30s  Msg %d/%d", ui_wcs2u8(msghdr_get(app->hdr, HDR_AREA)), msghdr_msgnum(app->hdr), msghdr_msgtotal(app->hdr));
    mvprintw(2, 1, "From: %-25.25s  %-20.20s  %s", ui_wcs2u8(from), ui_wcs2u8(msghdr_get(app->hdr, HDR_OADDR)), ui_wcs2u8(date));
    mvprintw(3, 1, "  To: %-25.25s  %s", ui_wcs2u8(to), ui_wcs2u8(msghdr_get(app->hdr, HDR_DADDR)));
    mvprintw(4, 1, "Subj: %s", ui_wcs2u8(subj));

    /* Attr on left, MSGID on right */
    mvprintw(5, 1, "Attr: %s", attr_str);

    if (app->cur_msgid[0])
    {
        int show = (int)strlen(app->cur_msgid);
        int max_room = COLS - 22;

        if (max_room < 10)
            max_room = 10;

        if (show > max_room)
            show = max_room;

        mvprintw(5, COLS - show - 8, "MSGID: %.*s", show, app->cur_msgid);
    }

    attroff(COLOR_PAIR(COL_HEADER));

    attron(COLOR_PAIR(COL_BORDER));
    ui_hline(6, 0, COLS);
    attroff(COLOR_PAIR(COL_BORDER));
}

/* Body rendering */
/* Convert one wchar_t to UTF-8 in buf. Returns bytes written */
static int wc_to_utf8_bytes(wchar_t wc, char *buf)
{
    unsigned long cp = (unsigned long)wc;

    if (cp < 0x80)
    {
        buf[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));

        return 2;
    }

    if (cp < 0x10000)
    {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));

        return 3;
    }

    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));

    return 4;
}

/* Draw ANSI line: collect runs of same attrs and emit each run in one call */
static void draw_ansi_line(int row_y, const wchar_t *wcs, int len, const struct AnsiCell *cells, int max_cols)
{
    char run_buf[1024];
    int i = 0;
    int x = 0;

    while (i < len && x < max_cols)
    {
        int run_color = cells[i].color_pair;
        int run_attrs = cells[i].attrs;
        int run_start = i;
        int run_chars = 0;
        int run_bytes = 0;

        /* Find extent of run with identical (color, attrs) */
        while (i < len && x + run_chars < max_cols && cells[i].color_pair == run_color && cells[i].attrs == run_attrs)
        {
            int n;

            if (run_bytes + 4 >= (int)sizeof(run_buf))
                break; /* Don't overflow run buffer; emit and continue */

            n = wc_to_utf8_bytes(wcs[i], &run_buf[run_bytes]);
            run_bytes += n;
            run_chars++;
            i++;
        }

        if (run_chars == 0)
            break; /* safety */

        run_buf[run_bytes] = '\0';

        if (run_color >= 0)
            attron(COLOR_PAIR(run_color) | run_attrs);

        mvaddnstr(row_y, x, run_buf, run_bytes);

        if (run_color >= 0)
            attroff(COLOR_PAIR(run_color) | run_attrs);

        x += run_chars;
    }
}

static void draw_body(UiApp *app)
{
    int rows = LINES - 8;
    int i;
    const wchar_t *wcs;
    int wlen;

    if (rows < 1)
        return;

    rd_set_page(app->reader, rows);

    for (i = 0; i < rows; i++)
    {
        int vi = rd_top(app->reader) + i;
        int type, col;
        int ansi_color, ansi_attrs;
        const struct AnsiCell *cells;
        int row_y = 7 + i;
        const wchar_t *ln;
        int lnlen;

        /* Skip if terminal shrunk since rd_set_page */
        if (row_y < 0 || row_y >= LINES - 1)
            continue;

        move(row_y, 0);
        clrtoeol();

        if (vi >= rd_total(app->reader))
            continue;

        type = rd_get_type(app->reader, vi);
        ansi_color = rd_get_ansi_color(app->reader, vi);
        ansi_attrs = rd_get_ansi_attrs(app->reader, vi);
        cells = rd_get_ansi_cells(app->reader, vi);

        /* ANSI with per-cell attrs: draw via canvas (preserves BBS art colors) */
        if (rd_ansi_visible(app->reader) && cells)
        {
            ln = rd_get_line(app->reader, vi);
            lnlen = rd_get_len(app->reader, vi);

            if (ln && lnlen > 0)
                draw_ansi_line(row_y, ln, lnlen, cells, (COLS < 80) ? COLS : 80);

            continue; /* attrs handled inside draw_ansi_line */
        }

        if (rd_ansi_visible(app->reader) && ansi_color >= 0)
        {
            col = ansi_color;
            attron(COLOR_PAIR(col) | ansi_attrs);
        }
        else
        {
            col = ui_color_for_type(type);
            attron(COLOR_PAIR(col));
            ansi_attrs = 0;
        }

        /* Plain or ANSI without per-cell attrs: draw with line color */
        ln = rd_get_line(app->reader, vi);
        lnlen = rd_get_len(app->reader, vi);

        /* Skip leading ^A on kludge lines */
        if (ln && lnlen > 0 && (type & FTN_LT_KLUDGE) && ln[0] == L'\x01')
        {
            ln++;
            lnlen--;
        }

        if (ln && lnlen > 0)
            mvaddnwstr(row_y, 0, ln, lnlen);

        /* Highlight search matches (word-level) */
        if (app->reader_search.rows && app->reader_search.cols && app->reader_search.match_count > 0)
        {
            int line_idx = rd_get_line_idx(app->reader, vi);
            int j;

            if (line_idx >= 0)
            {
                for (j = 0; j < app->reader_search.match_count; j++)
                {
                    if (app->reader_search.rows[j] == line_idx)
                    {
                        int match_col = app->reader_search.cols[j];
                        int match_len = (int)wcslen(app->reader_search.query);

                        if (match_col >= 0 && match_col + match_len <= lnlen)
                        {
                            if (j == app->reader_search.current_match)
                                attron(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attron(COLOR_PAIR(COL_SEARCH_MATCH));

                            mvaddnwstr(row_y, match_col, &ln[match_col], match_len);

                            if (j == app->reader_search.current_match)
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
            }
        }

        attroff(COLOR_PAIR(col) | ansi_attrs);
    }
}

/* Clear search state in reader */
static void reader_clear_search(UiApp *app)
{
    if (app->reader_search.rows)
    {
        free(app->reader_search.rows);
        app->reader_search.rows = NULL;
    }

    if (app->reader_search.cols)
    {
        free(app->reader_search.cols);
        app->reader_search.cols = NULL;
    }

    app->reader_search.match_count = 0;
    app->reader_search.is_mode = 0;
    app->reader_search.only_mode = 0;
    app->reader_search.current_match = 0;
    app->reader_search.match_current = 0;
}

/* Navigate to a raw msgs[] index, honouring the current sort order */
static int reader_goto_raw(UiApp *app, int raw_idx)
{
    UiSession *s = &app->sess;
    uint32_t target_mn;
    int i;

    if (raw_idx < 0 || raw_idx >= s->msg_count)
        return -1; /* invalid index */

    target_mn = s->msgs[raw_idx].msgnum;

    for (i = 0; i < s->order_count; i++)
    {
        if (s->msgs[s->order[i]].msgnum == target_mn)
        {
            s->msg_sel = i + 1;
            app->cur_msgnum = target_mn;

            return load_msg(app, target_mn);
        }
    }

    return -2;
}

/* Main loop */
UiView ui_reader_run(UiApp *app)
{
    int ch;
    UiSession *s;
    int saved_viewansi;

    if (!app)
        return VIEW_QUIT;

    s = &app->sess;
    saved_viewansi = app->cfg->viewansi;

    if (load_msg(app, app->cur_msgnum) != 0)
    {
        ui_status(app, "Cannot load message");
        app->cfg->viewansi = saved_viewansi;
        return reader_exit_view(app);
    }

    /* Apply ANSI visibility from config on first load */
    if (app->cfg->viewansi != rd_ansi_visible(app->reader))
    {
        rd_toggle_ansi(app->reader);

#ifdef PLATFORM_AMIGA
        amiga_change_font(rd_ansi_visible(app->reader));
#endif
    }

    for (;;)
    {
        erase();

        ui_draw_menubar(app, "Reader");
        draw_header_bar(app);
        draw_body(app);

        /* Status: msg_charset (CHRS or ?) -> decoded_charset (actual decode) */
        ui_status(app, "Charset: %s -> %s | c = Change charset%s | Ln %d/%d",
                  app->msg_charset[0] ? app->msg_charset : "?",
                  (app->view_charset[0]) ? app->decoded_charset : "Auto",
                  rd_ansi_visible(app->reader) ? " | ANSI" : "",
                  rd_top(app->reader) + 1, rd_total(app->reader));

        ui_draw_statusbar(app);
        refresh();

        ch = wrapper_getch();

        switch (ch)
        {
        case KEY_UP:
            rd_scroll_up(app->reader, 1);
            break;
        case KEY_DOWN:
            rd_scroll_down(app->reader, 1);
            break;
        case KEY_PPAGE:
        case 'b':
        case 'B':
        case CTRL('U'): /* Page Up (Amiga) */
            rd_page_up(app->reader);
            break;
        case KEY_NPAGE:
        case CTRL('D'): /* Page Down (Amiga) */
        case ' ':
            rd_page_down(app->reader);
            break;
        case KEY_HOME:
        case CTRL('B'): /* Home (Amiga) */
            rd_home(app->reader);
            break;
        case KEY_END:
        case CTRL('E'): /* End (Amiga) */
            rd_end(app->reader);
            break;

        case KEY_RIGHT:
        case 'n': /* Next message */
            if (s->msg_sel < s->order_count)
            {
                s->msg_sel++;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load next");
            }
            else
            {
                /* At last message: try to go to next area */
                try_goto_next_area(app);
            }
            break;
        case KEY_LEFT:
        case 'p': /* Previous message */
            if (s->msg_sel > 1)
            {
                s->msg_sel--;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load prev");
            }
            break;

        case CTRL('H'):
        case '<':
            /* Scroll to top of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) > 0)
                rd_scroll_up(app->reader, 1);

            break;

        case CTRL('K'):
        case '>':
            /* Scroll to bottom of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) + rd_visible(app->reader) < rd_total(app->reader))
                rd_scroll_down(app->reader, 1);

            break;

        case ',':
            /* First message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = 1;
                app->cur_msgnum = s->msgs[s->order[0]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load first");
            }
            break;

        case '.':
            /* Last message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = s->order_count;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load last");
            }
            break;
        case KEY_ALT('J'): /* Follow REPLY chain to original */
        case KEY_CLEFT:
        {
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int orig = (idx >= 0) ? ftn_find_original(s->msgs, s->msg_count, idx) : -1;

            /* Fallback: search by MSGID string from REPLY kludge */
            if (orig < 0 && app->cur_reply[0])
                orig = ftn_find_original_by_msgid(s->msgs, s->msg_count, app->cur_reply);

            if (orig < 0)
                ui_status(app, "No original message in this area");
            else if (orig >= s->msg_count)
                ui_status(app, "Invalid original message index");
            else if (s->msgs[orig].from[0] == '\0' || s->msgs[orig].subject[0] == '\0')
                ui_status(app, "Original message is empty or corrupted");
            else
            {
                int rc = reader_goto_raw(app, orig);

                if (rc == -2)
                    ui_status(app, "Original not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load original");
            }

            break;
        }
        case KEY_CRIGHT: /* Follow thread to reply (with picker if branched) */
        {
#define MAX_REPLIES 256
            int raw_replies[MAX_REPLIES];
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int nreplies = (idx >= 0) ? ftn_find_all_replies(s->msgs, s->msg_count, idx, raw_replies, MAX_REPLIES) : 0;

            if (nreplies == 0)
            {
                ui_status(app, "No replies to this message");
            }
            else if (nreplies == 1)
            {
                int rc = reader_goto_raw(app, raw_replies[0]);
                if (rc == -2)
                    ui_status(app, "Reply not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load reply");
            }
            else
            {
                /* Multiple replies: build label list and show picker */
                const char *items[MAX_REPLIES];
                char labels[MAX_REPLIES][80];
                int i, choice;

                for (i = 0; i < nreplies; i++)
                {
                    const JamMsgInfo *m = &s->msgs[raw_replies[i]];

                    snprintf(labels[i], sizeof(labels[i]), "#%-5u  %-20.20s  %.28s", (unsigned)m->msgnum, m->from, m->subject);
                    items[i] = labels[i];
                }

                choice = ui_popup_list("Select reply", items, nreplies, 0);

                if (choice >= 0)
                {
                    int rc = reader_goto_raw(app, raw_replies[choice]);

                    if (rc == -2)
                        ui_status(app, "Reply not visible in current filter");
                    else if (rc != 0)
                        ui_status(app, "Cannot load reply");
                }
            }
#undef MAX_REPLIES
            break;
        }

        case 'c':
        case 'C':
        {
            char new_view[32], new_out[32];

            new_view[0] = '\0';
            new_out[0] = '\0';

            if (ui_popup_charset_pair(app->view_charset, app->edit_charset, CHARSET_READ_DEFAULT, app->cfg->charset, new_view, sizeof(new_view), new_out, sizeof(new_out)) == 0)
            {
                /* Empty string = Auto (CHARSET_READ_DEFAULT); load_msg treats "" as no override */
                strncpy(app->view_charset, new_view, sizeof(app->view_charset) - 1);
                app->view_charset[sizeof(app->view_charset) - 1] = '\0';

                strncpy(app->edit_charset, new_out, sizeof(app->edit_charset) - 1);
                app->edit_charset[sizeof(app->edit_charset) - 1] = '\0';

                /* Mark that user manually changed charset (from reader) - only if NOT Auto */
                if (new_out[0] != '\0')
                    app->edit_charset_manually_changed = 1;

                /* Reload with new charset; preserves headers, lastread, ANSI, kludges */
                load_msg(app, app->cur_msgnum);

                ui_status(app, "View: %s | Save: %s", new_view[0] ? new_view : CHARSET_READ_DEFAULT, new_out[0] ? new_out : (app->cfg->charset[0] ? app->cfg->charset : "UTF-8"));
            }

            break;
        }

        case 'k':
        case 'K':
            rd_toggle_kludges(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case 'v':
        case 'V':
            rd_toggle_hiddklud(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Hidden+Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case KEY_F(5): /* Search in message body */
        case '/':
        {
            wchar_t tmp[64];
            int *rows = NULL, *cols = NULL;
            int match_count;
            int i;
            const char **contexts = NULL;
            char **context_bufs = NULL;
            int *line_nums = NULL;

            wcsncpy(tmp, app->reader_search.query, 63);
            tmp[63] = L'\0';

            if (ui_popup_input("Search", "Text:", tmp, 64) == 0 && tmp[0])
            {
                wcsncpy(app->reader_search.query, tmp, 63);
                app->reader_search.query[63] = L'\0';

                /* Find all matches in all lines (no limit) */
                match_count = rd_search_all(app->reader, app->reader_search.query, &rows, &cols);

                /* Free previous search matches */
                reader_clear_search(app);

                if (match_count == 0)
                {
                    ui_status(app, "Not found");
                }
                else if (match_count == 1)
                {
                    /* Single match: scroll to it */
                    rd_set_page(app->reader, LINES - 8);

                    if (rows[0] < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - rows[0]);
                    else if (rows[0] >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, rows[0] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Found at line %d", rows[0] + 1);

                    /* Save matches for highlighting and activate search mode */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
                else
                {
                    /* Allocate arrays for display */
                    contexts = (const char **)malloc((size_t)match_count * sizeof(const char *));
                    context_bufs = (char **)malloc((size_t)match_count * sizeof(char *));
                    line_nums = (int *)malloc((size_t)match_count * sizeof(int));

                    if (!contexts || !context_bufs || !line_nums)
                    {
                        if (contexts)
                            free(contexts);

                        if (context_bufs)
                            free(context_bufs);

                        if (line_nums)
                            free(line_nums);

                        free(rows);
                        free(cols);
                        ui_status(app, "Memory error");

                        break;
                    }

                    /* Multiple matches: show list */
                    for (i = 0; i < match_count; i++)
                    {
                        const wchar_t *line = rd_get_line(app->reader, rows[i]);
                        int line_len = rd_get_len(app->reader, rows[i]);

                        context_bufs[i] = (char *)malloc(128);

                        if (!context_bufs[i])
                        {
                            context_bufs[i] = NULL;
                            contexts[i] = "";
                        }
                        else
                        {
                            /* Convert context to UTF-8 for display (max 60 chars) */
                            if (line && line_len > 0)
                            {
                                int copy_len = line_len;

                                if (copy_len > 60)
                                    copy_len = 60;

                                char *utf8 = wcs_to_utf8(line, copy_len);

                                if (utf8)
                                {
                                    snprintf(context_bufs[i], 128, "%s", utf8);
                                    free(utf8);
                                }
                                else
                                {
                                    context_bufs[i][0] = '\0';
                                }
                            }
                            else
                            {
                                context_bufs[i][0] = '\0';
                            }
                        }

                        contexts[i] = context_bufs[i];
                        line_nums[i] = rows[i] + 1; /* 1-based for display */
                    }

                    /* Show popup with results */
                    int choice = ui_popup_search_results("Search Results", line_nums, contexts, match_count, 0);

                    if (choice >= 0)
                    {
                        rd_set_page(app->reader, LINES - 8);

                        if (rows[choice] < rd_top(app->reader))
                            rd_scroll_up(app->reader, rd_top(app->reader) - rows[choice]);
                        else if (rows[choice] >= rd_top(app->reader) + rd_visible(app->reader))
                            rd_scroll_down(app->reader, rows[choice] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                        app->reader_search.current_match = choice;
                        app->reader_search.match_current = choice + 1;
                        ui_status(app, "Jumped to line %d", rows[choice] + 1);
                    }
                    else
                    {
                        ui_status(app, "Search cancelled");
                    }

                    /* Free allocated memory */
                    for (i = 0; i < match_count; i++)
                    {
                        if (context_bufs[i])
                            free(context_bufs[i]);
                    }

                    free(context_bufs);
                    free(contexts);
                    free(line_nums);

                    /* Save matches for highlighting and activate search mode (don't free rows/cols) */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
            }
            break;
        }

        case 'a':
        case 'A':
            rd_toggle_ansi(app->reader);
            app->cfg->viewansi = rd_ansi_visible(app->reader);
#ifdef PLATFORM_AMIGA
            amiga_change_font(rd_ansi_visible(app->reader));
#endif
            /* Reload message to apply/re-apply ANSI parsing with new font */
            load_msg(app, app->cur_msgnum);
            /* Continue to next iteration to redraw with new font */
            continue;

        case 'r':
        case 'R':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_reply(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'e':
        case 'E':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_edit(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'N':
        case KEY_IC: /* New message */
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_new(app);
            return VIEW_EDITOR;

        case '\r':
        case '\n':
        case KEY_ENTER:
            /* Page down or advance to next message at bottom */
            if (rd_top(app->reader) + rd_visible(app->reader) >= rd_total(app->reader))
            {
                if (s->msg_sel < s->order_count)
                {
                    s->msg_sel++;
                    app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load next");
                }
                else
                {
                    /* At last message: try to go to next area */
                    try_goto_next_area(app);
                }
            }
            else
                rd_page_down(app->reader);
            break;

        case CTRL('G'): /* Goto line number */
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Line number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long line_num = 0;
                int target_line;

                if (u)
                {
                    line_num = strtol(u, NULL, 10);
                    free(u);
                }

                target_line = (int)(line_num - 1); /* Convert to 0-based */

                if (target_line >= 0 && target_line < rd_total(app->reader))
                {
                    rd_set_page(app->reader, LINES - 8);

                    /* Scroll to target line */
                    if (target_line < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - target_line);
                    else if (target_line >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, target_line - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Jumped to line %ld", line_num);
                }
                else
                {
                    ui_status(app, "Invalid line number");
                }
            }
            break;
        }

        case KEY_ALT('G'): /* Clear search highlights */
        {
            reader_clear_search(app);
            ui_status(app, "Search highlights cleared");

            continue; /* Force redraw */
        }

        /* Navigate to previous match in reader */
        case KEY_F(3):
        case KEY_ALT('P'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match - 1 + app->reader_search.match_count) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        /* Navigate to next match in reader */
        case KEY_F(4):
        case KEY_ALT('N'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match + 1) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        case 'g': /* Goto message number */
        case 'G':
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Message number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long mn = 0;

                if (u)
                {
                    mn = strtol(u, NULL, 10);
                    free(u);
                }

                int i, found = -1;

                for (i = 0; i < s->order_count; i++)
                {
                    if ((long)s->msgs[s->order[i]].msgnum == mn)
                    {
                        found = i;
                        break;
                    }
                }

                if (found >= 0)
                {
                    s->msg_sel = found + 1;
                    app->cur_msgnum = (uint32_t)mn;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load message");
                }
                else
                    ui_status(app, "No such message in this view");
            }

            break;
        }

        case 'w': /* Write message to text file */
        case 'W':
        case KEY_F(7):
        {
            char path[256];
            char out_cs[CHARSET_NAME_MAX];
            UiSession *s;
            char *body_utf8;
            int written;

            path[0] = '\0';
            out_cs[0] = '\0';

            snprintf(path, sizeof(path), "msg_%u.txt", (unsigned)app->cur_msgnum);

            /*if (ui_popup_input("Write message", "File path:", path, sizeof(path)) != 0 || !path[0])
                break;
            */

            if (ui_files_save("Write message", NULL, path, path, sizeof(path)) != 0)
                break;

            /* Charset of the output file. Empty == AUTO == write UTF-8
             * verbatim; the user can type "CP437", "LATIN-1", etc */
            if (ui_popup_charset("Output charset", "", out_cs, sizeof(out_cs)) != 0)
                break;

            s = &app->sess;
            body_utf8 = wrapper_read_utf8_ex(&s->jam, app->cur_msgnum, app->view_charset[0] ? app->view_charset : NULL, NULL, app->msg_charset, sizeof(app->msg_charset));

            if (!body_utf8)
            {
                ui_status(app, "Cannot read message body");
                break;
            }

            written = rd_export_to_file(app->reader, body_utf8, path, out_cs[0] ? out_cs : NULL);
            free(body_utf8);

            if (written < 0)
                ui_status(app, "Cannot write %s", path);
            else
                ui_status(app, "Wrote %d lines to %s (%s)", written, path, out_cs[0] ? out_cs : "UTF-8");

            break;
        }
        case 'd':
        case 'D':
        case KEY_DC: /* Delete message */
            if (ui_popup_confirm("Delete", "Mark this message as deleted?") == 1)
            {
                if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
                {
                    if (jam_delete_msg(&s->jam, app->cur_msgnum) == 0)
                        ui_status(app, "Message deleted");

                    jam_unlock(&s->jam);
                    free(s->msgs);

                    s->msgs = NULL;
                    s->msgs = jam_load_headers(&s->jam, &s->msg_count, 0, (uint32_t)app->cfg->msglistmax);

                    if (!s->msgs)
                    {
                        s->msg_count = 0;
                        ui_status(app, "Reload failed after delete");
                    }
                    else
                    {
                        ui_session_rebuild_order(app);
                    }
                }
            }

            reader_save_lastread(app);

            /* reader_save_lastread() already updates area counts; skip redundant call */
            app->cfg->viewansi = saved_viewansi;

            return reader_exit_view(app);

            break;

        case KEY_F(1):
        case '?':
            ui_popup_help("Reader Help", READER_HELP, READER_HELP_N);
            break;

        case 'l':
        case 'L':
            /* Show msglist overlay; from search go to search browser instead */
            if (app->from_search)
            {
                reader_clear_search(app);
                app->cfg->viewansi = saved_viewansi;
                return VIEW_SEARCH_RESULTS;
            }

            reader_clear_search(app);
            reader_save_lastread(app);
            update_area_counts_in_memory(app);

            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 1;

            return VIEW_MSGLIST;

        case 27:
            /* ESC: exit search mode first, otherwise quit */
            if (app->reader_search.only_mode)
            {
                reader_clear_search(app);
                ui_status(app, "Search mode exited");

                continue; /* Force redraw */
            }
            /* Fall through to quit */
        case 'q':
        case 'Q':
            reader_save_lastread(app);
            update_area_counts_in_memory(app);
            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 0;

            reader_clear_search(app);

            return reader_exit_view(app);

        case KEY_RESIZE:
            flushinp();
            load_msg(app, app->cur_msgnum);
            break;
        case CTRL('F'):
            ui_popup_freq(app);
            break;
        default:
            break;
        }
    }
}
/*
 * crashedit - Message area editor for AmigaOS
 *
 * This file is part of the crashedit project.
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This program uses JAMLIB, which is licensed under the GNU Lesser
 * General Public License v2.1. See src/jamlib/LICENSE for details.
 */

/* ui_reader.c -- Message reader view */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"
#include "ui_attr.h"
#include "ui_files.h"
#include "../../src/jamlib/jam.h"
#include "../core/msghdr.h"
#include "../core/ftn.h"
#include "../core/ansi.h"
#include "../core/keys.h"

static const char *READER_HELP[] =
    {
        "Message Reader - Key Bindings:",
        "",
        "  Up/Down,j/k    Scroll body up/down",
#ifdef PLATFORM_AMIGA
        "  Ctrl+U/D       Page scroll",
        "  Ctrl+B/E       First / last line",
#else
        "  PgUp/PgDn      Page scroll",
        "  Home/End       First / last line",
#endif
        "  Left/Right,p/n Previous / next message",
        "",
        "  b/Space        Page up/down",
        "  Enter          Page down or advance to next message",
        "  < >            First / last line (scroll)",
        "  , .            First / last message",
        "  Ctrl-J         Jump to original (follow reply chain)",
        "  Ctrl+Left      Jump to original (follow reply chain)",
        "  Ctrl+Right     Jump to reply (pick if multiple)",
        "",
        "  k, v           Toggle kludges (^A, SEEN-BY, Via)",
        "  a              Toggle ANSI color rendering",
        "  c              Change DISPLAY charset (re-render)",
        "  r              Reply to this message",
        "  e              Edit this message",
        "  n, Ins         New message (in current area)",
        "  l, L           Show message list overlay (ESC returns here)",
        "  g              Goto message number",
        "  Ctrl+H         Scroll to top of message",
        "  Ctrl+K         Scroll to bottom of message",
        "  Ctrl+G         Goto line number",
        "  Alt+J          Follow reply chain to original",
        "  Alt+G          Clear search highlights",
        "  F5, /          Search in message body",
        "  F3 Alt+P       Previous match (search mode)",
        "  F4 Alt+N       Next match (search mode)",
        "  w, F7          Write message to text file",
        "  d, Del         Delete this message",
        "  Ctrl+F         File request",
        "",
        "  ESC, q         Back to message list",
        "  F1, ?          This help"};
#define READER_HELP_N ((int)(sizeof(READER_HELP) / sizeof(READER_HELP[0])))

void ui_editor_prep_new(UiApp *app);
void ui_editor_prep_reply(UiApp *app, uint32_t orig_msgnum);
void ui_editor_prep_edit(UiApp *app, uint32_t msgnum);

/* Update area's cached counts after reader moves lastread/lastseen */
static void update_area_counts_in_memory(UiApp *app)
{
    UiSession *s;
    AreaEntry *ae;
    int i, unread, new_count;

    if (!app)
        return;

    s = &app->sess;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return;

    ae = &app->areas->entries[s->area_idx];

    ae->lastread = s->lastread;
    ae->lastseen = s->lastseen;

    /* Recompute unread (above lastread) and new_count (above lastseen) */
    unread = 0;
    new_count = 0;

    for (i = 0; i < s->msg_count; i++)
    {
        if (s->msgs[i].msgnum > s->lastread)
            unread++;

        if (s->msgs[i].msgnum > s->lastseen)
            new_count++;
    }

    ae->unread = unread;
    ae->new_count = new_count;
    ae->total_msgs = s->msg_count;
}

/* Update lastread and persist to JAM; skip if from search (browsing shouldn't mutate) */
static void reader_save_lastread(UiApp *app)
{
    UiSession *s;
    uint32_t msgnum;

    if (!app)
        return;

    if (app->from_search)
        return;

    s = &app->sess;
    msgnum = app->cur_msgnum;

    s->lastread = msgnum;

    if (msgnum > s->lastseen)
        s->lastseen = msgnum;

    if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
    {
        jam_write_lastread(&s->jam, s->user_crc, s->lastread, s->lastseen);
        jam_unlock(&s->jam);
    }

    update_area_counts_in_memory(app);
}

/* Exit target: msglist (normal) or search results (if from search) */
static UiView reader_exit_view(const UiApp *app)
{
    return app->from_search ? VIEW_SEARCH_RESULTS : VIEW_MSGLIST;
}

/* Strip FTS-1 kludge lines and SEEN-BY/PATH trailers for ANSI rendering
 * Returns malloc'd buffer (caller frees), NULL on failure */
static char *strip_kludges_for_ansi(const char *raw, int raw_len, int *out_len)
{
    char *out;
    int i = 0;
    int j = 0;
    int at_line_start = 1;

    if (out_len)
        *out_len = 0;

    if (!raw || raw_len <= 0)
        return NULL;

    out = (char *)malloc((size_t)raw_len + 1);
    if (!out)
        return NULL;

    while (i < raw_len)
    {
        if (at_line_start)
        {
            const char *p = raw + i;
            int rem = raw_len - i;
            int is_kludge = 0;

            if (rem >= 1 && (unsigned char)*p == 0x01)
                is_kludge = 1;
            else if (rem >= 8 && memcmp(p, "SEEN-BY:", 8) == 0)
                is_kludge = 1;
            else if (rem >= 5 && memcmp(p, "PATH:", 5) == 0)
                is_kludge = 1;

            if (is_kludge)
            {
                /* Skip to end of this line (eat optional \r and \n) */
                while (i < raw_len && raw[i] != '\r' && raw[i] != '\n')
                    i++;

                if (i < raw_len && raw[i] == '\r')
                    i++;
                if (i < raw_len && raw[i] == '\n')
                    i++;

                /* Still at line start for the next iteration */
                continue;
            }

            at_line_start = 0;
        }

        out[j++] = raw[i];

        if (raw[i] == '\r' || raw[i] == '\n')
            at_line_start = 1;

        i++;
    }

    out[j] = '\0';

    if (out_len)
        *out_len = j;

    return out;
}

/* Load current message into reader */
static int load_msg(UiApp *app, uint32_t msgnum)
{
    UiSession *s;
    int idx;
    char *body_utf8 = NULL;
    int wrap_w;
    const wchar_t *cur;
    char *cur_utf8;
    int needs_fallback;
    int ansi_on_outer = 0;
    char detected[CHARSET_NAME_MAX];
    const char *override_enc;
    char chosen[CHARSET_NAME_MAX];
    int ansi_on;
    uint32_t raw_len = 0;
    char *raw_bytes = NULL;
    int has_chrs = 0;
    const char *p;
    int oi;

    if (!app)
        return -1;

    s = &app->sess;

    idx = jam_find_by_msgnum(s->msgs, s->msg_count, msgnum);

    if (idx < 0)
        return -1;

    /* Sync msg_sel to current message for correct navigation after search/jump entry */
    for (oi = 0; oi < s->order_count; oi++)
    {
        if (s->order[oi] == idx)
        {
            s->msg_sel = oi + 1;
            break;
        }
    }

    /* Fill header */
    msghdr_load(app->hdr, &s->msgs[idx], app->areas->entries[s->area_idx].name, idx + 1, s->msg_count, ftn_effective_tz_offset(app->cfg->timezone_offset, app->cfg->timezone_is_manual));

    /* Body: ANSI->canvas, text->UTF-8. Charset: CHRS if present, else default */
    override_enc = app->view_charset[0] ? app->view_charset : NULL;

    detected[0] = '\0';
    chosen[0] = '\0';

    /* Read and decode body using wrapper_read_utf8_ex with proper fallback logic */
    body_utf8 = wrapper_read_utf8_ex(&s->jam, msgnum, override_enc, NULL, detected, sizeof(detected));

    if (!body_utf8)
        return -1;

    /* Read raw body for ANSI mode (needs original bytes, not UTF-8) */
    raw_bytes = jam_read_body(&s->jam, msgnum, &raw_len);

    if (!raw_bytes)
    {
        free(body_utf8);
        return -1;
    }

    /* Set charset variables based on wrapper_read_utf8_ex results */
    strncpy(chosen, detected, sizeof(chosen) - 1);
    chosen[sizeof(chosen) - 1] = '\0';

    /* Check if we actually have a CHRS kludge */
    has_chrs = (detected[0] != '\0');

    if (has_chrs)
    {
        strncpy(app->msg_charset, detected, sizeof(app->msg_charset) - 1);
        app->msg_charset[sizeof(app->msg_charset) - 1] = '\0';
    }
    else
    {
        app->msg_charset[0] = '\0';
    }

    /* ANSI mode: force CP437 if UTF-8 came from default/fallback (not CHRS/override)
     * Exception: when TTF is active, keep detected charset to preserve Unicode characters */
    ansi_on = (app->cfg && app->cfg->viewansi);
    ansi_on_outer = ansi_on;

    if (ansi_on && (strcasecmp(chosen, "UTF-8") == 0 || strcasecmp(chosen, "UTF8") == 0))
    {
        int user_wants_utf8 = (override_enc && (strcasecmp(override_enc, "UTF-8") == 0 || strcasecmp(override_enc, "UTF8") == 0));
        int chrs_says_utf8 = (app->msg_charset[0] && (strcasecmp(app->msg_charset, "UTF-8") == 0 || strcasecmp(app->msg_charset, "UTF8") == 0));

        if (!user_wants_utf8 && !chrs_says_utf8)
        {
            strncpy(chosen, "CP437", sizeof(chosen) - 1);
            chosen[sizeof(chosen) - 1] = '\0';
        }
    }

    /* Record actual decode charset for status bar (may differ from view_charset) */
    if (override_enc && override_enc[0])
        strncpy(app->decoded_charset, override_enc, sizeof(app->decoded_charset) - 1);
    else
        strncpy(app->decoded_charset, chosen, sizeof(app->decoded_charset) - 1);

    app->decoded_charset[sizeof(app->decoded_charset) - 1] = '\0';

    /*wrap_w = COLS - 2;*/
    wrap_w = COLS;

    if (wrap_w < 20)
        wrap_w = 20;

    if (ansi_on)
    {
        /* Strip kludge/SEEN-BY/PATH lines. Fall back to raw bytes on failure */
        int stripped_len = 0;
        char *stripped = strip_kludges_for_ansi(raw_bytes, (int)raw_len, &stripped_len);

        if (stripped)
        {
            rd_load_ansi(app->reader, stripped, stripped_len, chosen, wrap_w);
            free(stripped);
        }
        else
        {
            rd_load_ansi(app->reader, raw_bytes, (int)raw_len, chosen, wrap_w);
        }

        /* Free unused UTF-8 buffer; ANSI mode uses raw bytes. Prevents leak */
        free(body_utf8);
        body_utf8 = NULL; /* not used in this path */
    }

    /* ANSI mode: convert kludges/SEEN-BY/Origin to UTF-8 for MSGID/origin lookup */
    if (ansi_on)
    {
        int srclen = (int)raw_len;
        int dstmax = srclen * 4 + 256;

        body_utf8 = (char *)malloc((size_t)dstmax);

        if (body_utf8)
            charset_body_to_utf8(chosen, raw_bytes, srclen, body_utf8, dstmax);
    }

    free(raw_bytes);

    if (!body_utf8)
        return -1;

    /* Extract MSGID / REPLY for header display */
    app->cur_msgid[0] = '\0';
    app->cur_reply[0] = '\0';

    ftn_get_kludge_value(body_utf8, "MSGID", app->cur_msgid, sizeof(app->cur_msgid));
    ftn_get_kludge_value(body_utf8, "REPLY", app->cur_reply, sizeof(app->cur_reply));

    /* Get sender address from MSGID kludge, fallback to Origin line */

    cur = msghdr_get(app->hdr, HDR_OADDR);
    cur_utf8 = cur ? wcs_to_utf8(cur, (int)wcslen(cur)) : NULL;

    if (cur && !cur_utf8)
    {
        free(body_utf8); /* would otherwise leak on this OOM path */
        return -1;
    }

    needs_fallback = (!cur_utf8 || !cur_utf8[0] || strcmp(cur_utf8, "0:0/0") == 0);

    if (needs_fallback)
    {
        char addr[64];
        int got = -1;

        if (app->cur_msgid[0])
        {
            /* Extract address from MSGID (format: "addr serial") */
            int i;

            addr[0] = '\0';

            for (i = 0; app->cur_msgid[i] && app->cur_msgid[i] != ' ' && app->cur_msgid[i] != '\t' && i < (int)sizeof(addr) - 1; i++)
                addr[i] = app->cur_msgid[i];

            addr[i] = '\0';

            if (addr[0])
                got = 0;
        }

        if (got != 0)
            got = ftn_find_origin_address(body_utf8, addr, sizeof(addr));

        if (got == 0)
            msghdr_set_utf8(app->hdr, HDR_OADDR, addr);
    }

    free(cur_utf8);

    wrap_w = COLS; /* full width, match editor soft-wrap */

    if (wrap_w < 20)
        wrap_w = 20;

    /* ANSI: canvas loaded above, skip rd_load. Non-ANSI: rd_load handles wrap */
    if (!ansi_on_outer)
        rd_load(app->reader, body_utf8, wrap_w);

    free(body_utf8);

    rd_set_page(app->reader, LINES - 8);

    /* Apply view-config toggles (kludges visibility) */
    if (app->cfg->viewkludge != rd_kludges_visible(app->reader))
        rd_toggle_kludges(app->reader);

    return 0;
}

/* Try to go to next area in area_order. Returns 1 on success, 0 otherwise */
static int try_goto_next_area(UiApp *app)
{
    UiSession *s;
    int current_area_idx;
    int current_order_idx = -1;
    int next_area_idx = -1;
    int i;
    AreaEntry *next_ae;
    char prompt[256];

    if (!app)
        return 0;

    s = &app->sess;

    if (app->area_order_count <= 0)
        return 0;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return 0;

    current_area_idx = s->area_idx;

    /* Find current area in area_order */
    for (i = 0; i < app->area_order_count; i++)
    {
        if (app->area_order[i] == current_area_idx)
        {
            current_order_idx = i;
            break;
        }
    }

    /* Find next area in order */
    if (current_order_idx >= 0 && current_order_idx < app->area_order_count - 1)
    {
        next_area_idx = app->area_order[current_order_idx + 1];
    }

    if (next_area_idx < 0)
        return 0;

    next_ae = &app->areas->entries[next_area_idx];
    snprintf(prompt, sizeof(prompt), "Go to next area '%s'?", next_ae->name ? next_ae->name : "(area)");

    if (ui_popup_confirm("Next area", prompt) != 1)
        return 0;

    /* Save lastread before changing area */
    reader_save_lastread(app);

    /* Close current session and open next area */
    ui_session_close(app);

    if (ui_session_open(app, next_area_idx) != 0)
    {
        ui_status(app, "Cannot open area");
        return 0;
    }

    /* Load message from new area */
    if (s->order_count > 0)
    {
        /* Check if current position is on an unread message */
        uint32_t current_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        /* If not on unread message, go to last message in list */
        if (current_msgnum <= s->lastread)
            s->msg_sel = s->order_count;

        app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        if (load_msg(app, app->cur_msgnum) != 0)
        {
            ui_status(app, "Cannot load message");
            return 0;
        }
    }

    return 1;
}

/* Header bar */
static void draw_header_bar(UiApp *app)
{
    int i;
    const wchar_t *from, *to, *subj, *date;
    char attr_str[40];
    static const char SPACES[256] =
        "                                                                "
        "                                                                "
        "                                                                "
        "                                                                ";

    from = msghdr_get(app->hdr, HDR_FROM);
    to = msghdr_get(app->hdr, HDR_TO);
    subj = msghdr_get(app->hdr, HDR_SUBJECT);
    date = msghdr_get(app->hdr, HDR_DATE);
    ui_attr_build(msghdr_attr(app->hdr), attr_str, sizeof(attr_str));

    attron(COLOR_PAIR(COL_HEADER));

    for (i = 1; i < 7; i++)
    {
        int written = 0;
        move(i, 0);
        clrtoeol();

        while (written < COLS)
        {
            int chunk = COLS - written;

            if (chunk > (int)sizeof(SPACES))
                chunk = (int)sizeof(SPACES);

            mvaddnstr(i, written, SPACES, chunk);
            written += chunk;
        }
    }

    mvprintw(1, 1, "Area: %-30.30s  Msg %d/%d", ui_wcs2u8(msghdr_get(app->hdr, HDR_AREA)), msghdr_msgnum(app->hdr), msghdr_msgtotal(app->hdr));
    mvprintw(2, 1, "From: %-25.25s  %-20.20s  %s", ui_wcs2u8(from), ui_wcs2u8(msghdr_get(app->hdr, HDR_OADDR)), ui_wcs2u8(date));
    mvprintw(3, 1, "  To: %-25.25s  %s", ui_wcs2u8(to), ui_wcs2u8(msghdr_get(app->hdr, HDR_DADDR)));
    mvprintw(4, 1, "Subj: %s", ui_wcs2u8(subj));

    /* Attr on left, MSGID on right */
    mvprintw(5, 1, "Attr: %s", attr_str);

    if (app->cur_msgid[0])
    {
        int show = (int)strlen(app->cur_msgid);
        int max_room = COLS - 22;

        if (max_room < 10)
            max_room = 10;

        if (show > max_room)
            show = max_room;

        mvprintw(5, COLS - show - 8, "MSGID: %.*s", show, app->cur_msgid);
    }

    attroff(COLOR_PAIR(COL_HEADER));

    attron(COLOR_PAIR(COL_BORDER));
    ui_hline(6, 0, COLS);
    attroff(COLOR_PAIR(COL_BORDER));
}

/* Body rendering */
/* Convert one wchar_t to UTF-8 in buf. Returns bytes written */
static int wc_to_utf8_bytes(wchar_t wc, char *buf)
{
    unsigned long cp = (unsigned long)wc;

    if (cp < 0x80)
    {
        buf[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));

        return 2;
    }

    if (cp < 0x10000)
    {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));

        return 3;
    }

    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));

    return 4;
}

/* Draw ANSI line: collect runs of same attrs and emit each run in one call */
static void draw_ansi_line(int row_y, const wchar_t *wcs, int len, const struct AnsiCell *cells, int max_cols)
{
    char run_buf[1024];
    int i = 0;
    int x = 0;

    while (i < len && x < max_cols)
    {
        int run_color = cells[i].color_pair;
        int run_attrs = cells[i].attrs;
        int run_start = i;
        int run_chars = 0;
        int run_bytes = 0;

        /* Find extent of run with identical (color, attrs) */
        while (i < len && x + run_chars < max_cols && cells[i].color_pair == run_color && cells[i].attrs == run_attrs)
        {
            int n;

            if (run_bytes + 4 >= (int)sizeof(run_buf))
                break; /* Don't overflow run buffer; emit and continue */

            n = wc_to_utf8_bytes(wcs[i], &run_buf[run_bytes]);
            run_bytes += n;
            run_chars++;
            i++;
        }

        if (run_chars == 0)
            break; /* safety */

        run_buf[run_bytes] = '\0';

        if (run_color >= 0)
            attron(COLOR_PAIR(run_color) | run_attrs);

        mvaddnstr(row_y, x, run_buf, run_bytes);

        if (run_color >= 0)
            attroff(COLOR_PAIR(run_color) | run_attrs);

        x += run_chars;
    }
}

static void draw_body(UiApp *app)
{
    int rows = LINES - 8;
    int i;
    const wchar_t *wcs;
    int wlen;

    if (rows < 1)
        return;

    rd_set_page(app->reader, rows);

    for (i = 0; i < rows; i++)
    {
        int vi = rd_top(app->reader) + i;
        int type, col;
        int ansi_color, ansi_attrs;
        const struct AnsiCell *cells;
        int row_y = 7 + i;
        const wchar_t *ln;
        int lnlen;

        /* Skip if terminal shrunk since rd_set_page */
        if (row_y < 0 || row_y >= LINES - 1)
            continue;

        move(row_y, 0);
        clrtoeol();

        if (vi >= rd_total(app->reader))
            continue;

        type = rd_get_type(app->reader, vi);
        ansi_color = rd_get_ansi_color(app->reader, vi);
        ansi_attrs = rd_get_ansi_attrs(app->reader, vi);
        cells = rd_get_ansi_cells(app->reader, vi);

        /* ANSI with per-cell attrs: draw via canvas (preserves BBS art colors) */
        if (rd_ansi_visible(app->reader) && cells)
        {
            ln = rd_get_line(app->reader, vi);
            lnlen = rd_get_len(app->reader, vi);

            if (ln && lnlen > 0)
                draw_ansi_line(row_y, ln, lnlen, cells, (COLS < 80) ? COLS : 80);

            continue; /* attrs handled inside draw_ansi_line */
        }

        if (rd_ansi_visible(app->reader) && ansi_color >= 0)
        {
            col = ansi_color;
            attron(COLOR_PAIR(col) | ansi_attrs);
        }
        else
        {
            col = ui_color_for_type(type);
            attron(COLOR_PAIR(col));
            ansi_attrs = 0;
        }

        /* Plain or ANSI without per-cell attrs: draw with line color */
        ln = rd_get_line(app->reader, vi);
        lnlen = rd_get_len(app->reader, vi);

        /* Skip leading ^A on kludge lines */
        if (ln && lnlen > 0 && (type & FTN_LT_KLUDGE) && ln[0] == L'\x01')
        {
            ln++;
            lnlen--;
        }

        if (ln && lnlen > 0)
            mvaddnwstr(row_y, 0, ln, lnlen);

        /* Highlight search matches (word-level) */
        if (app->reader_search.rows && app->reader_search.cols && app->reader_search.match_count > 0)
        {
            int line_idx = rd_get_line_idx(app->reader, vi);
            int j;

            if (line_idx >= 0)
            {
                for (j = 0; j < app->reader_search.match_count; j++)
                {
                    if (app->reader_search.rows[j] == line_idx)
                    {
                        int match_col = app->reader_search.cols[j];
                        int match_len = (int)wcslen(app->reader_search.query);

                        if (match_col >= 0 && match_col + match_len <= lnlen)
                        {
                            if (j == app->reader_search.current_match)
                                attron(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attron(COLOR_PAIR(COL_SEARCH_MATCH));

                            mvaddnwstr(row_y, match_col, &ln[match_col], match_len);

                            if (j == app->reader_search.current_match)
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
            }
        }

        attroff(COLOR_PAIR(col) | ansi_attrs);
    }
}

/* Clear search state in reader */
static void reader_clear_search(UiApp *app)
{
    if (app->reader_search.rows)
    {
        free(app->reader_search.rows);
        app->reader_search.rows = NULL;
    }

    if (app->reader_search.cols)
    {
        free(app->reader_search.cols);
        app->reader_search.cols = NULL;
    }

    app->reader_search.match_count = 0;
    app->reader_search.is_mode = 0;
    app->reader_search.only_mode = 0;
    app->reader_search.current_match = 0;
    app->reader_search.match_current = 0;
}

/* Navigate to a raw msgs[] index, honouring the current sort order */
static int reader_goto_raw(UiApp *app, int raw_idx)
{
    UiSession *s = &app->sess;
    uint32_t target_mn;
    int i;

    if (raw_idx < 0 || raw_idx >= s->msg_count)
        return -1; /* invalid index */

    target_mn = s->msgs[raw_idx].msgnum;

    for (i = 0; i < s->order_count; i++)
    {
        if (s->msgs[s->order[i]].msgnum == target_mn)
        {
            s->msg_sel = i + 1;
            app->cur_msgnum = target_mn;

            return load_msg(app, target_mn);
        }
    }

    return -2;
}

/* Main loop */
UiView ui_reader_run(UiApp *app)
{
    int ch;
    UiSession *s;
    int saved_viewansi;

    if (!app)
        return VIEW_QUIT;

    s = &app->sess;
    saved_viewansi = app->cfg->viewansi;

    if (load_msg(app, app->cur_msgnum) != 0)
    {
        ui_status(app, "Cannot load message");
        app->cfg->viewansi = saved_viewansi;
        return reader_exit_view(app);
    }

    /* Apply ANSI visibility from config on first load */
    if (app->cfg->viewansi != rd_ansi_visible(app->reader))
    {
        rd_toggle_ansi(app->reader);

#ifdef PLATFORM_AMIGA
        amiga_change_font(rd_ansi_visible(app->reader));
#endif
    }

    for (;;)
    {
        erase();

        ui_draw_menubar(app, "Reader");
        draw_header_bar(app);
        draw_body(app);

        /* Status: msg_charset (CHRS or ?) -> decoded_charset (actual decode) */
        ui_status(app, "Charset: %s -> %s | c = Change charset%s | Ln %d/%d",
                  app->msg_charset[0] ? app->msg_charset : "?",
                  (app->view_charset[0]) ? app->decoded_charset : "Auto",
                  rd_ansi_visible(app->reader) ? " | ANSI" : "",
                  rd_top(app->reader) + 1, rd_total(app->reader));

        ui_draw_statusbar(app);
        refresh();

        ch = wrapper_getch();

        switch (ch)
        {
        case KEY_UP:
            rd_scroll_up(app->reader, 1);
            break;
        case KEY_DOWN:
            rd_scroll_down(app->reader, 1);
            break;
        case KEY_PPAGE:
        case 'b':
        case 'B':
        case CTRL('U'): /* Page Up (Amiga) */
            rd_page_up(app->reader);
            break;
        case KEY_NPAGE:
        case CTRL('D'): /* Page Down (Amiga) */
        case ' ':
            rd_page_down(app->reader);
            break;
        case KEY_HOME:
        case CTRL('B'): /* Home (Amiga) */
            rd_home(app->reader);
            break;
        case KEY_END:
        case CTRL('E'): /* End (Amiga) */
            rd_end(app->reader);
            break;

        case KEY_RIGHT:
        case 'n': /* Next message */
            if (s->msg_sel < s->order_count)
            {
                s->msg_sel++;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load next");
            }
            else
            {
                /* At last message: try to go to next area */
                try_goto_next_area(app);
            }
            break;
        case KEY_LEFT:
        case 'p': /* Previous message */
            if (s->msg_sel > 1)
            {
                s->msg_sel--;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load prev");
            }
            break;

        case CTRL('H'):
        case '<':
            /* Scroll to top of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) > 0)
                rd_scroll_up(app->reader, 1);

            break;

        case CTRL('K'):
        case '>':
            /* Scroll to bottom of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) + rd_visible(app->reader) < rd_total(app->reader))
                rd_scroll_down(app->reader, 1);

            break;

        case ',':
            /* First message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = 1;
                app->cur_msgnum = s->msgs[s->order[0]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load first");
            }
            break;

        case '.':
            /* Last message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = s->order_count;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load last");
            }
            break;
        case KEY_ALT('J'): /* Follow REPLY chain to original */
        case KEY_CLEFT:
        {
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int orig = (idx >= 0) ? ftn_find_original(s->msgs, s->msg_count, idx) : -1;

            /* Fallback: search by MSGID string from REPLY kludge */
            if (orig < 0 && app->cur_reply[0])
                orig = ftn_find_original_by_msgid(s->msgs, s->msg_count, app->cur_reply);

            if (orig < 0)
                ui_status(app, "No original message in this area");
            else if (orig >= s->msg_count)
                ui_status(app, "Invalid original message index");
            else if (s->msgs[orig].from[0] == '\0' || s->msgs[orig].subject[0] == '\0')
                ui_status(app, "Original message is empty or corrupted");
            else
            {
                int rc = reader_goto_raw(app, orig);

                if (rc == -2)
                    ui_status(app, "Original not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load original");
            }

            break;
        }
        case KEY_CRIGHT: /* Follow thread to reply (with picker if branched) */
        {
#define MAX_REPLIES 256
            int raw_replies[MAX_REPLIES];
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int nreplies = (idx >= 0) ? ftn_find_all_replies(s->msgs, s->msg_count, idx, raw_replies, MAX_REPLIES) : 0;

            if (nreplies == 0)
            {
                ui_status(app, "No replies to this message");
            }
            else if (nreplies == 1)
            {
                int rc = reader_goto_raw(app, raw_replies[0]);
                if (rc == -2)
                    ui_status(app, "Reply not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load reply");
            }
            else
            {
                /* Multiple replies: build label list and show picker */
                const char *items[MAX_REPLIES];
                char labels[MAX_REPLIES][80];
                int i, choice;

                for (i = 0; i < nreplies; i++)
                {
                    const JamMsgInfo *m = &s->msgs[raw_replies[i]];

                    snprintf(labels[i], sizeof(labels[i]), "#%-5u  %-20.20s  %.28s", (unsigned)m->msgnum, m->from, m->subject);
                    items[i] = labels[i];
                }

                choice = ui_popup_list("Select reply", items, nreplies, 0);

                if (choice >= 0)
                {
                    int rc = reader_goto_raw(app, raw_replies[choice]);

                    if (rc == -2)
                        ui_status(app, "Reply not visible in current filter");
                    else if (rc != 0)
                        ui_status(app, "Cannot load reply");
                }
            }
#undef MAX_REPLIES
            break;
        }

        case 'c':
        case 'C':
        {
            char new_view[32], new_out[32];

            new_view[0] = '\0';
            new_out[0] = '\0';

            if (ui_popup_charset_pair(app->view_charset, app->edit_charset, CHARSET_READ_DEFAULT, app->cfg->charset, new_view, sizeof(new_view), new_out, sizeof(new_out)) == 0)
            {
                /* Empty string = Auto (CHARSET_READ_DEFAULT); load_msg treats "" as no override */
                strncpy(app->view_charset, new_view, sizeof(app->view_charset) - 1);
                app->view_charset[sizeof(app->view_charset) - 1] = '\0';

                strncpy(app->edit_charset, new_out, sizeof(app->edit_charset) - 1);
                app->edit_charset[sizeof(app->edit_charset) - 1] = '\0';

                /* Mark that user manually changed charset (from reader) - only if NOT Auto */
                if (new_out[0] != '\0')
                    app->edit_charset_manually_changed = 1;

                /* Reload with new charset; preserves headers, lastread, ANSI, kludges */
                load_msg(app, app->cur_msgnum);

                ui_status(app, "View: %s | Save: %s", new_view[0] ? new_view : CHARSET_READ_DEFAULT, new_out[0] ? new_out : (app->cfg->charset[0] ? app->cfg->charset : "UTF-8"));
            }

            break;
        }

        case 'k':
        case 'K':
            rd_toggle_kludges(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case 'v':
        case 'V':
            rd_toggle_hiddklud(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Hidden+Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case KEY_F(5): /* Search in message body */
        case '/':
        {
            wchar_t tmp[64];
            int *rows = NULL, *cols = NULL;
            int match_count;
            int i;
            const char **contexts = NULL;
            char **context_bufs = NULL;
            int *line_nums = NULL;

            wcsncpy(tmp, app->reader_search.query, 63);
            tmp[63] = L'\0';

            if (ui_popup_input("Search", "Text:", tmp, 64) == 0 && tmp[0])
            {
                wcsncpy(app->reader_search.query, tmp, 63);
                app->reader_search.query[63] = L'\0';

                /* Find all matches in all lines (no limit) */
                match_count = rd_search_all(app->reader, app->reader_search.query, &rows, &cols);

                /* Free previous search matches */
                reader_clear_search(app);

                if (match_count == 0)
                {
                    ui_status(app, "Not found");
                }
                else if (match_count == 1)
                {
                    /* Single match: scroll to it */
                    rd_set_page(app->reader, LINES - 8);

                    if (rows[0] < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - rows[0]);
                    else if (rows[0] >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, rows[0] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Found at line %d", rows[0] + 1);

                    /* Save matches for highlighting and activate search mode */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
                else
                {
                    /* Allocate arrays for display */
                    contexts = (const char **)malloc((size_t)match_count * sizeof(const char *));
                    context_bufs = (char **)malloc((size_t)match_count * sizeof(char *));
                    line_nums = (int *)malloc((size_t)match_count * sizeof(int));

                    if (!contexts || !context_bufs || !line_nums)
                    {
                        if (contexts)
                            free(contexts);

                        if (context_bufs)
                            free(context_bufs);

                        if (line_nums)
                            free(line_nums);

                        free(rows);
                        free(cols);
                        ui_status(app, "Memory error");

                        break;
                    }

                    /* Multiple matches: show list */
                    for (i = 0; i < match_count; i++)
                    {
                        const wchar_t *line = rd_get_line(app->reader, rows[i]);
                        int line_len = rd_get_len(app->reader, rows[i]);

                        context_bufs[i] = (char *)malloc(128);

                        if (!context_bufs[i])
                        {
                            context_bufs[i] = NULL;
                            contexts[i] = "";
                        }
                        else
                        {
                            /* Convert context to UTF-8 for display (max 60 chars) */
                            if (line && line_len > 0)
                            {
                                int copy_len = line_len;

                                if (copy_len > 60)
                                    copy_len = 60;

                                char *utf8 = wcs_to_utf8(line, copy_len);

                                if (utf8)
                                {
                                    snprintf(context_bufs[i], 128, "%s", utf8);
                                    free(utf8);
                                }
                                else
                                {
                                    context_bufs[i][0] = '\0';
                                }
                            }
                            else
                            {
                                context_bufs[i][0] = '\0';
                            }
                        }

                        contexts[i] = context_bufs[i];
                        line_nums[i] = rows[i] + 1; /* 1-based for display */
                    }

                    /* Show popup with results */
                    int choice = ui_popup_search_results("Search Results", line_nums, contexts, match_count, 0);

                    if (choice >= 0)
                    {
                        rd_set_page(app->reader, LINES - 8);

                        if (rows[choice] < rd_top(app->reader))
                            rd_scroll_up(app->reader, rd_top(app->reader) - rows[choice]);
                        else if (rows[choice] >= rd_top(app->reader) + rd_visible(app->reader))
                            rd_scroll_down(app->reader, rows[choice] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                        app->reader_search.current_match = choice;
                        app->reader_search.match_current = choice + 1;
                        ui_status(app, "Jumped to line %d", rows[choice] + 1);
                    }
                    else
                    {
                        ui_status(app, "Search cancelled");
                    }

                    /* Free allocated memory */
                    for (i = 0; i < match_count; i++)
                    {
                        if (context_bufs[i])
                            free(context_bufs[i]);
                    }

                    free(context_bufs);
                    free(contexts);
                    free(line_nums);

                    /* Save matches for highlighting and activate search mode (don't free rows/cols) */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
            }
            break;
        }

        case 'a':
        case 'A':
            rd_toggle_ansi(app->reader);
            app->cfg->viewansi = rd_ansi_visible(app->reader);
#ifdef PLATFORM_AMIGA
            amiga_change_font(rd_ansi_visible(app->reader));
#endif
            /* Reload message to apply/re-apply ANSI parsing with new font */
            load_msg(app, app->cur_msgnum);
            /* Continue to next iteration to redraw with new font */
            continue;

        case 'r':
        case 'R':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_reply(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'e':
        case 'E':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_edit(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'N':
        case KEY_IC: /* New message */
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_new(app);
            return VIEW_EDITOR;

        case '\r':
        case '\n':
        case KEY_ENTER:
            /* Page down or advance to next message at bottom */
            if (rd_top(app->reader) + rd_visible(app->reader) >= rd_total(app->reader))
            {
                if (s->msg_sel < s->order_count)
                {
                    s->msg_sel++;
                    app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load next");
                }
                else
                {
                    /* At last message: try to go to next area */
                    try_goto_next_area(app);
                }
            }
            else
                rd_page_down(app->reader);
            break;

        case CTRL('G'): /* Goto line number */
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Line number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long line_num = 0;
                int target_line;

                if (u)
                {
                    line_num = strtol(u, NULL, 10);
                    free(u);
                }

                target_line = (int)(line_num - 1); /* Convert to 0-based */

                if (target_line >= 0 && target_line < rd_total(app->reader))
                {
                    rd_set_page(app->reader, LINES - 8);

                    /* Scroll to target line */
                    if (target_line < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - target_line);
                    else if (target_line >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, target_line - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Jumped to line %ld", line_num);
                }
                else
                {
                    ui_status(app, "Invalid line number");
                }
            }
            break;
        }

        case KEY_ALT('G'): /* Clear search highlights */
        {
            reader_clear_search(app);
            ui_status(app, "Search highlights cleared");

            continue; /* Force redraw */
        }

        /* Navigate to previous match in reader */
        case KEY_F(3):
        case KEY_ALT('P'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match - 1 + app->reader_search.match_count) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        /* Navigate to next match in reader */
        case KEY_F(4):
        case KEY_ALT('N'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match + 1) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        case 'g': /* Goto message number */
        case 'G':
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Message number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long mn = 0;

                if (u)
                {
                    mn = strtol(u, NULL, 10);
                    free(u);
                }

                int i, found = -1;

                for (i = 0; i < s->order_count; i++)
                {
                    if ((long)s->msgs[s->order[i]].msgnum == mn)
                    {
                        found = i;
                        break;
                    }
                }

                if (found >= 0)
                {
                    s->msg_sel = found + 1;
                    app->cur_msgnum = (uint32_t)mn;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load message");
                }
                else
                    ui_status(app, "No such message in this view");
            }

            break;
        }

        case 'w': /* Write message to text file */
        case 'W':
        case KEY_F(7):
        {
            char path[256];
            char out_cs[CHARSET_NAME_MAX];
            UiSession *s;
            char *body_utf8;
            int written;

            path[0] = '\0';
            out_cs[0] = '\0';

            snprintf(path, sizeof(path), "msg_%u.txt", (unsigned)app->cur_msgnum);

            /*if (ui_popup_input("Write message", "File path:", path, sizeof(path)) != 0 || !path[0])
                break;
            */

            if (ui_files_save("Write message", NULL, path, path, sizeof(path)) != 0)
                break;

            /* Charset of the output file. Empty == AUTO == write UTF-8
             * verbatim; the user can type "CP437", "LATIN-1", etc */
            if (ui_popup_charset("Output charset", "", out_cs, sizeof(out_cs)) != 0)
                break;

            s = &app->sess;
            body_utf8 = wrapper_read_utf8_ex(&s->jam, app->cur_msgnum, app->view_charset[0] ? app->view_charset : NULL, NULL, app->msg_charset, sizeof(app->msg_charset));

            if (!body_utf8)
            {
                ui_status(app, "Cannot read message body");
                break;
            }

            written = rd_export_to_file(app->reader, body_utf8, path, out_cs[0] ? out_cs : NULL);
            free(body_utf8);

            if (written < 0)
                ui_status(app, "Cannot write %s", path);
            else
                ui_status(app, "Wrote %d lines to %s (%s)", written, path, out_cs[0] ? out_cs : "UTF-8");

            break;
        }
        case 'd':
        case 'D':
        case KEY_DC: /* Delete message */
            if (ui_popup_confirm("Delete", "Mark this message as deleted?") == 1)
            {
                if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
                {
                    if (jam_delete_msg(&s->jam, app->cur_msgnum) == 0)
                        ui_status(app, "Message deleted");

                    jam_unlock(&s->jam);
                    free(s->msgs);

                    s->msgs = NULL;
                    s->msgs = jam_load_headers(&s->jam, &s->msg_count, 0, (uint32_t)app->cfg->msglistmax);

                    if (!s->msgs)
                    {
                        s->msg_count = 0;
                        ui_status(app, "Reload failed after delete");
                    }
                    else
                    {
                        ui_session_rebuild_order(app);
                    }
                }
            }

            reader_save_lastread(app);

            /* reader_save_lastread() already updates area counts; skip redundant call */
            app->cfg->viewansi = saved_viewansi;

            return reader_exit_view(app);

            break;

        case KEY_F(1):
        case '?':
            ui_popup_help("Reader Help", READER_HELP, READER_HELP_N);
            break;

        case 'l':
        case 'L':
            /* Show msglist overlay; from search go to search browser instead */
            if (app->from_search)
            {
                reader_clear_search(app);
                app->cfg->viewansi = saved_viewansi;
                return VIEW_SEARCH_RESULTS;
            }

            reader_clear_search(app);
            reader_save_lastread(app);
            update_area_counts_in_memory(app);

            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 1;

            return VIEW_MSGLIST;

        case 27:
            /* ESC: exit search mode first, otherwise quit */
            if (app->reader_search.only_mode)
            {
                reader_clear_search(app);
                ui_status(app, "Search mode exited");

                continue; /* Force redraw */
            }
            /* Fall through to quit */
        case 'q':
        case 'Q':
            reader_save_lastread(app);
            update_area_counts_in_memory(app);
            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 0;

            reader_clear_search(app);

            return reader_exit_view(app);

        case KEY_RESIZE:
            flushinp();
            load_msg(app, app->cur_msgnum);
            break;
        case CTRL('F'):
            ui_popup_freq(app);
            break;
        default:
            break;
        }
    }
}

/*
 * crashedit - Message area editor for AmigaOS
 *
 * This file is part of the crashedit project.
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This program uses JAMLIB, which is licensed under the GNU Lesser
 * General Public License v2.1. See src/jamlib/LICENSE for details.
 */

/* ui_reader.c -- Message reader view */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"
#include "ui_attr.h"
#include "ui_files.h"
#include "../../src/jamlib/jam.h"
#include "../core/msghdr.h"
#include "../core/ftn.h"
#include "../core/ansi.h"
#include "../core/keys.h"

static const char *READER_HELP[] =
    {
        "Message Reader - Key Bindings:",
        "",
        "  Up/Down,j/k    Scroll body up/down",
#ifdef PLATFORM_AMIGA
        "  Ctrl+U/D       Page scroll",
        "  Ctrl+B/E       First / last line",
#else
        "  PgUp/PgDn      Page scroll",
        "  Home/End       First / last line",
#endif
        "  Left/Right,p/n Previous / next message",
        "",
        "  b/Space        Page up/down",
        "  Enter          Page down or advance to next message",
        "  < >            First / last line (scroll)",
        "  , .            First / last message",
        "  Ctrl-J         Jump to original (follow reply chain)",
        "  Ctrl+Left      Jump to original (follow reply chain)",
        "  Ctrl+Right     Jump to reply (pick if multiple)",
        "",
        "  k, v           Toggle kludges (^A, SEEN-BY, Via)",
        "  a              Toggle ANSI color rendering",
        "  c              Change DISPLAY charset (re-render)",
        "  r              Reply to this message",
        "  e              Edit this message",
        "  n, Ins         New message (in current area)",
        "  l, L           Show message list overlay (ESC returns here)",
        "  g              Goto message number",
        "  Ctrl+H         Scroll to top of message",
        "  Ctrl+K         Scroll to bottom of message",
        "  Ctrl+G         Goto line number",
        "  Alt+J          Follow reply chain to original",
        "  Alt+G          Clear search highlights",
        "  F5, /          Search in message body",
        "  F3 Alt+P       Previous match (search mode)",
        "  F4 Alt+N       Next match (search mode)",
        "  w, F7          Write message to text file",
        "  d, Del         Delete this message",
        "  Ctrl+F         File request",
        "",
        "  ESC, q         Back to message list",
        "  F1, ?          This help"};
#define READER_HELP_N ((int)(sizeof(READER_HELP) / sizeof(READER_HELP[0])))

void ui_editor_prep_new(UiApp *app);
void ui_editor_prep_reply(UiApp *app, uint32_t orig_msgnum);
void ui_editor_prep_edit(UiApp *app, uint32_t msgnum);

/* Update area's cached counts after reader moves lastread/lastseen */
static void update_area_counts_in_memory(UiApp *app)
{
    UiSession *s;
    AreaEntry *ae;
    int i, unread, new_count;

    if (!app)
        return;

    s = &app->sess;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return;

    ae = &app->areas->entries[s->area_idx];

    ae->lastread = s->lastread;
    ae->lastseen = s->lastseen;

    /* Recompute unread (above lastread) and new_count (above lastseen) */
    unread = 0;
    new_count = 0;

    for (i = 0; i < s->msg_count; i++)
    {
        if (s->msgs[i].msgnum > s->lastread)
            unread++;

        if (s->msgs[i].msgnum > s->lastseen)
            new_count++;
    }

    ae->unread = unread;
    ae->new_count = new_count;
    ae->total_msgs = s->msg_count;
}

/* Update lastread and persist to JAM; skip if from search (browsing shouldn't mutate) */
static void reader_save_lastread(UiApp *app)
{
    UiSession *s;
    uint32_t msgnum;

    if (!app)
        return;

    if (app->from_search)
        return;

    s = &app->sess;
    msgnum = app->cur_msgnum;

    s->lastread = msgnum;

    if (msgnum > s->lastseen)
        s->lastseen = msgnum;

    if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
    {
        jam_write_lastread(&s->jam, s->user_crc, s->lastread, s->lastseen);
        jam_unlock(&s->jam);
    }

    update_area_counts_in_memory(app);
}

/* Exit target: msglist (normal) or search results (if from search) */
static UiView reader_exit_view(const UiApp *app)
{
    return app->from_search ? VIEW_SEARCH_RESULTS : VIEW_MSGLIST;
}

/* Strip FTS-1 kludge lines and SEEN-BY/PATH trailers for ANSI rendering
 * Returns malloc'd buffer (caller frees), NULL on failure */
static char *strip_kludges_for_ansi(const char *raw, int raw_len, int *out_len)
{
    char *out;
    int i = 0;
    int j = 0;
    int at_line_start = 1;

    if (out_len)
        *out_len = 0;

    if (!raw || raw_len <= 0)
        return NULL;

    out = (char *)malloc((size_t)raw_len + 1);
    if (!out)
        return NULL;

    while (i < raw_len)
    {
        if (at_line_start)
        {
            const char *p = raw + i;
            int rem = raw_len - i;
            int is_kludge = 0;

            if (rem >= 1 && (unsigned char)*p == 0x01)
                is_kludge = 1;
            else if (rem >= 8 && memcmp(p, "SEEN-BY:", 8) == 0)
                is_kludge = 1;
            else if (rem >= 5 && memcmp(p, "PATH:", 5) == 0)
                is_kludge = 1;

            if (is_kludge)
            {
                /* Skip to end of this line (eat optional \r and \n) */
                while (i < raw_len && raw[i] != '\r' && raw[i] != '\n')
                    i++;

                if (i < raw_len && raw[i] == '\r')
                    i++;
                if (i < raw_len && raw[i] == '\n')
                    i++;

                /* Still at line start for the next iteration */
                continue;
            }

            at_line_start = 0;
        }

        out[j++] = raw[i];

        if (raw[i] == '\r' || raw[i] == '\n')
            at_line_start = 1;

        i++;
    }

    out[j] = '\0';

    if (out_len)
        *out_len = j;

    return out;
}

/* Load current message into reader */
static int load_msg(UiApp *app, uint32_t msgnum)
{
    UiSession *s;
    int idx;
    char *body_utf8 = NULL;
    int wrap_w;
    const wchar_t *cur;
    char *cur_utf8;
    int needs_fallback;
    int ansi_on_outer = 0;
    char detected[CHARSET_NAME_MAX];
    const char *override_enc;
    char chosen[CHARSET_NAME_MAX];
    int ansi_on;
    uint32_t raw_len = 0;
    char *raw_bytes = NULL;
    int has_chrs = 0;
    const char *p;
    int oi;

    if (!app)
        return -1;

    s = &app->sess;

    idx = jam_find_by_msgnum(s->msgs, s->msg_count, msgnum);

    if (idx < 0)
        return -1;

    /* Sync msg_sel to current message for correct navigation after search/jump entry */
    for (oi = 0; oi < s->order_count; oi++)
    {
        if (s->order[oi] == idx)
        {
            s->msg_sel = oi + 1;
            break;
        }
    }

    /* Fill header */
    msghdr_load(app->hdr, &s->msgs[idx], app->areas->entries[s->area_idx].name, idx + 1, s->msg_count, ftn_effective_tz_offset(app->cfg->timezone_offset, app->cfg->timezone_is_manual));

    /* Body: ANSI->canvas, text->UTF-8. Charset: CHRS if present, else default */
    override_enc = app->view_charset[0] ? app->view_charset : NULL;

    detected[0] = '\0';
    chosen[0] = '\0';

    /* Read and decode body using wrapper_read_utf8_ex with proper fallback logic */
    body_utf8 = wrapper_read_utf8_ex(&s->jam, msgnum, override_enc, NULL, detected, sizeof(detected));

    if (!body_utf8)
        return -1;

    /* Read raw body for ANSI mode (needs original bytes, not UTF-8) */
    raw_bytes = jam_read_body(&s->jam, msgnum, &raw_len);

    if (!raw_bytes)
    {
        free(body_utf8);
        return -1;
    }

    /* Set charset variables based on wrapper_read_utf8_ex results */
    strncpy(chosen, detected, sizeof(chosen) - 1);
    chosen[sizeof(chosen) - 1] = '\0';

    /* Check if we actually have a CHRS kludge */
    has_chrs = (detected[0] != '\0');

    if (has_chrs)
    {
        strncpy(app->msg_charset, detected, sizeof(app->msg_charset) - 1);
        app->msg_charset[sizeof(app->msg_charset) - 1] = '\0';
    }
    else
    {
        app->msg_charset[0] = '\0';
    }

    /* ANSI mode: force CP437 if UTF-8 came from default/fallback (not CHRS/override)
     * Exception: when TTF is active, keep detected charset to preserve Unicode characters */
    ansi_on = (app->cfg && app->cfg->viewansi);
    ansi_on_outer = ansi_on;

    if (ansi_on && (strcasecmp(chosen, "UTF-8") == 0 || strcasecmp(chosen, "UTF8") == 0))
    {
        int user_wants_utf8 = (override_enc && (strcasecmp(override_enc, "UTF-8") == 0 || strcasecmp(override_enc, "UTF8") == 0));
        int chrs_says_utf8 = (app->msg_charset[0] && (strcasecmp(app->msg_charset, "UTF-8") == 0 || strcasecmp(app->msg_charset, "UTF8") == 0));

        if (!user_wants_utf8 && !chrs_says_utf8)
        {
            strncpy(chosen, "CP437", sizeof(chosen) - 1);
            chosen[sizeof(chosen) - 1] = '\0';
        }
    }

    /* Record actual decode charset for status bar (may differ from view_charset) */
    if (override_enc && override_enc[0])
        strncpy(app->decoded_charset, override_enc, sizeof(app->decoded_charset) - 1);
    else
        strncpy(app->decoded_charset, chosen, sizeof(app->decoded_charset) - 1);

    app->decoded_charset[sizeof(app->decoded_charset) - 1] = '\0';

    /*wrap_w = COLS - 2;*/
    wrap_w = COLS;

    if (wrap_w < 20)
        wrap_w = 20;

    if (ansi_on)
    {
        /* Strip kludge/SEEN-BY/PATH lines. Fall back to raw bytes on failure */
        int stripped_len = 0;
        char *stripped = strip_kludges_for_ansi(raw_bytes, (int)raw_len, &stripped_len);

        if (stripped)
        {
            rd_load_ansi(app->reader, stripped, stripped_len, chosen, wrap_w);
            free(stripped);
        }
        else
        {
            rd_load_ansi(app->reader, raw_bytes, (int)raw_len, chosen, wrap_w);
        }

        /* Free unused UTF-8 buffer; ANSI mode uses raw bytes. Prevents leak */
        free(body_utf8);
        body_utf8 = NULL; /* not used in this path */
    }

    /* ANSI mode: convert kludges/SEEN-BY/Origin to UTF-8 for MSGID/origin lookup */
    if (ansi_on)
    {
        int srclen = (int)raw_len;
        int dstmax = srclen * 4 + 256;

        body_utf8 = (char *)malloc((size_t)dstmax);

        if (body_utf8)
            charset_body_to_utf8(chosen, raw_bytes, srclen, body_utf8, dstmax);
    }

    free(raw_bytes);

    if (!body_utf8)
        return -1;

    /* Extract MSGID / REPLY for header display */
    app->cur_msgid[0] = '\0';
    app->cur_reply[0] = '\0';

    ftn_get_kludge_value(body_utf8, "MSGID", app->cur_msgid, sizeof(app->cur_msgid));
    ftn_get_kludge_value(body_utf8, "REPLY", app->cur_reply, sizeof(app->cur_reply));

    /* Get sender address from MSGID kludge, fallback to Origin line */

    cur = msghdr_get(app->hdr, HDR_OADDR);
    cur_utf8 = cur ? wcs_to_utf8(cur, (int)wcslen(cur)) : NULL;

    if (cur && !cur_utf8)
    {
        free(body_utf8); /* would otherwise leak on this OOM path */
        return -1;
    }

    needs_fallback = (!cur_utf8 || !cur_utf8[0] || strcmp(cur_utf8, "0:0/0") == 0);

    if (needs_fallback)
    {
        char addr[64];
        int got = -1;

        if (app->cur_msgid[0])
        {
            /* Extract address from MSGID (format: "addr serial") */
            int i;

            addr[0] = '\0';

            for (i = 0; app->cur_msgid[i] && app->cur_msgid[i] != ' ' && app->cur_msgid[i] != '\t' && i < (int)sizeof(addr) - 1; i++)
                addr[i] = app->cur_msgid[i];

            addr[i] = '\0';

            if (addr[0])
                got = 0;
        }

        if (got != 0)
            got = ftn_find_origin_address(body_utf8, addr, sizeof(addr));

        if (got == 0)
            msghdr_set_utf8(app->hdr, HDR_OADDR, addr);
    }

    free(cur_utf8);

    wrap_w = COLS; /* full width, match editor soft-wrap */

    if (wrap_w < 20)
        wrap_w = 20;

    /* ANSI: canvas loaded above, skip rd_load. Non-ANSI: rd_load handles wrap */
    if (!ansi_on_outer)
        rd_load(app->reader, body_utf8, wrap_w);

    free(body_utf8);

    rd_set_page(app->reader, LINES - 8);

    /* Apply view-config toggles (kludges visibility) */
    if (app->cfg->viewkludge != rd_kludges_visible(app->reader))
        rd_toggle_kludges(app->reader);

    return 0;
}

/* Try to go to next area in area_order. Returns 1 on success, 0 otherwise */
static int try_goto_next_area(UiApp *app)
{
    UiSession *s;
    int current_area_idx;
    int current_order_idx = -1;
    int next_area_idx = -1;
    int i;
    AreaEntry *next_ae;
    char prompt[256];

    if (!app)
        return 0;

    s = &app->sess;

    if (app->area_order_count <= 0)
        return 0;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return 0;

    current_area_idx = s->area_idx;

    /* Find current area in area_order */
    for (i = 0; i < app->area_order_count; i++)
    {
        if (app->area_order[i] == current_area_idx)
        {
            current_order_idx = i;
            break;
        }
    }

    /* Find next area in order */
    if (current_order_idx >= 0 && current_order_idx < app->area_order_count - 1)
    {
        next_area_idx = app->area_order[current_order_idx + 1];
    }

    if (next_area_idx < 0)
        return 0;

    next_ae = &app->areas->entries[next_area_idx];
    snprintf(prompt, sizeof(prompt), "Go to next area '%s'?", next_ae->name ? next_ae->name : "(area)");

    if (ui_popup_confirm("Next area", prompt) != 1)
        return 0;

    /* Save lastread before changing area */
    reader_save_lastread(app);

    /* Close current session and open next area */
    ui_session_close(app);

    if (ui_session_open(app, next_area_idx) != 0)
    {
        ui_status(app, "Cannot open area");
        return 0;
    }

    /* Load message from new area */
    if (s->order_count > 0)
    {
        /* Check if current position is on an unread message */
        uint32_t current_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        /* If not on unread message, go to last message in list */
        if (current_msgnum <= s->lastread)
            s->msg_sel = s->order_count;

        app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        if (load_msg(app, app->cur_msgnum) != 0)
        {
            ui_status(app, "Cannot load message");
            return 0;
        }
    }

    return 1;
}

/* Header bar */
static void draw_header_bar(UiApp *app)
{
    int i;
    const wchar_t *from, *to, *subj, *date;
    char attr_str[40];
    static const char SPACES[256] =
        "                                                                "
        "                                                                "
        "                                                                "
        "                                                                ";

    from = msghdr_get(app->hdr, HDR_FROM);
    to = msghdr_get(app->hdr, HDR_TO);
    subj = msghdr_get(app->hdr, HDR_SUBJECT);
    date = msghdr_get(app->hdr, HDR_DATE);
    ui_attr_build(msghdr_attr(app->hdr), attr_str, sizeof(attr_str));

    attron(COLOR_PAIR(COL_HEADER));

    for (i = 1; i < 7; i++)
    {
        int written = 0;
        move(i, 0);
        clrtoeol();

        while (written < COLS)
        {
            int chunk = COLS - written;

            if (chunk > (int)sizeof(SPACES))
                chunk = (int)sizeof(SPACES);

            mvaddnstr(i, written, SPACES, chunk);
            written += chunk;
        }
    }

    mvprintw(1, 1, "Area: %-30.30s  Msg %d/%d", ui_wcs2u8(msghdr_get(app->hdr, HDR_AREA)), msghdr_msgnum(app->hdr), msghdr_msgtotal(app->hdr));
    mvprintw(2, 1, "From: %-25.25s  %-20.20s  %s", ui_wcs2u8(from), ui_wcs2u8(msghdr_get(app->hdr, HDR_OADDR)), ui_wcs2u8(date));
    mvprintw(3, 1, "  To: %-25.25s  %s", ui_wcs2u8(to), ui_wcs2u8(msghdr_get(app->hdr, HDR_DADDR)));
    mvprintw(4, 1, "Subj: %s", ui_wcs2u8(subj));

    /* Attr on left, MSGID on right */
    mvprintw(5, 1, "Attr: %s", attr_str);

    if (app->cur_msgid[0])
    {
        int show = (int)strlen(app->cur_msgid);
        int max_room = COLS - 22;

        if (max_room < 10)
            max_room = 10;

        if (show > max_room)
            show = max_room;

        mvprintw(5, COLS - show - 8, "MSGID: %.*s", show, app->cur_msgid);
    }

    attroff(COLOR_PAIR(COL_HEADER));

    attron(COLOR_PAIR(COL_BORDER));
    ui_hline(6, 0, COLS);
    attroff(COLOR_PAIR(COL_BORDER));
}

/* Body rendering */
/* Convert one wchar_t to UTF-8 in buf. Returns bytes written */
static int wc_to_utf8_bytes(wchar_t wc, char *buf)
{
    unsigned long cp = (unsigned long)wc;

    if (cp < 0x80)
    {
        buf[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));

        return 2;
    }

    if (cp < 0x10000)
    {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));

        return 3;
    }

    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));

    return 4;
}

/* Draw ANSI line: collect runs of same attrs and emit each run in one call */
static void draw_ansi_line(int row_y, const wchar_t *wcs, int len, const struct AnsiCell *cells, int max_cols)
{
    char run_buf[1024];
    int i = 0;
    int x = 0;

    while (i < len && x < max_cols)
    {
        int run_color = cells[i].color_pair;
        int run_attrs = cells[i].attrs;
        int run_start = i;
        int run_chars = 0;
        int run_bytes = 0;

        /* Find extent of run with identical (color, attrs) */
        while (i < len && x + run_chars < max_cols && cells[i].color_pair == run_color && cells[i].attrs == run_attrs)
        {
            int n;

            if (run_bytes + 4 >= (int)sizeof(run_buf))
                break; /* Don't overflow run buffer; emit and continue */

            n = wc_to_utf8_bytes(wcs[i], &run_buf[run_bytes]);
            run_bytes += n;
            run_chars++;
            i++;
        }

        if (run_chars == 0)
            break; /* safety */

        run_buf[run_bytes] = '\0';

        if (run_color >= 0)
            attron(COLOR_PAIR(run_color) | run_attrs);

        mvaddnstr(row_y, x, run_buf, run_bytes);

        if (run_color >= 0)
            attroff(COLOR_PAIR(run_color) | run_attrs);

        x += run_chars;
    }
}

static void draw_body(UiApp *app)
{
    int rows = LINES - 8;
    int i;
    const wchar_t *wcs;
    int wlen;

    if (rows < 1)
        return;

    rd_set_page(app->reader, rows);

    for (i = 0; i < rows; i++)
    {
        int vi = rd_top(app->reader) + i;
        int type, col;
        int ansi_color, ansi_attrs;
        const struct AnsiCell *cells;
        int row_y = 7 + i;
        const wchar_t *ln;
        int lnlen;

        /* Skip if terminal shrunk since rd_set_page */
        if (row_y < 0 || row_y >= LINES - 1)
            continue;

        move(row_y, 0);
        clrtoeol();

        if (vi >= rd_total(app->reader))
            continue;

        type = rd_get_type(app->reader, vi);
        ansi_color = rd_get_ansi_color(app->reader, vi);
        ansi_attrs = rd_get_ansi_attrs(app->reader, vi);
        cells = rd_get_ansi_cells(app->reader, vi);

        /* ANSI with per-cell attrs: draw via canvas (preserves BBS art colors) */
        if (rd_ansi_visible(app->reader) && cells)
        {
            ln = rd_get_line(app->reader, vi);
            lnlen = rd_get_len(app->reader, vi);

            if (ln && lnlen > 0)
                draw_ansi_line(row_y, ln, lnlen, cells, (COLS < 80) ? COLS : 80);

            continue; /* attrs handled inside draw_ansi_line */
        }

        if (rd_ansi_visible(app->reader) && ansi_color >= 0)
        {
            col = ansi_color;
            attron(COLOR_PAIR(col) | ansi_attrs);
        }
        else
        {
            col = ui_color_for_type(type);
            attron(COLOR_PAIR(col));
            ansi_attrs = 0;
        }

        /* Plain or ANSI without per-cell attrs: draw with line color */
        ln = rd_get_line(app->reader, vi);
        lnlen = rd_get_len(app->reader, vi);

        /* Skip leading ^A on kludge lines */
        if (ln && lnlen > 0 && (type & FTN_LT_KLUDGE) && ln[0] == L'\x01')
        {
            ln++;
            lnlen--;
        }

        if (ln && lnlen > 0)
            mvaddnwstr(row_y, 0, ln, lnlen);

        /* Highlight search matches (word-level) */
        if (app->reader_search.rows && app->reader_search.cols && app->reader_search.match_count > 0)
        {
            int line_idx = rd_get_line_idx(app->reader, vi);
            int j;

            if (line_idx >= 0)
            {
                for (j = 0; j < app->reader_search.match_count; j++)
                {
                    if (app->reader_search.rows[j] == line_idx)
                    {
                        int match_col = app->reader_search.cols[j];
                        int match_len = (int)wcslen(app->reader_search.query);

                        if (match_col >= 0 && match_col + match_len <= lnlen)
                        {
                            if (j == app->reader_search.current_match)
                                attron(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attron(COLOR_PAIR(COL_SEARCH_MATCH));

                            mvaddnwstr(row_y, match_col, &ln[match_col], match_len);

                            if (j == app->reader_search.current_match)
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
            }
        }

        attroff(COLOR_PAIR(col) | ansi_attrs);
    }
}

/* Clear search state in reader */
static void reader_clear_search(UiApp *app)
{
    if (app->reader_search.rows)
    {
        free(app->reader_search.rows);
        app->reader_search.rows = NULL;
    }

    if (app->reader_search.cols)
    {
        free(app->reader_search.cols);
        app->reader_search.cols = NULL;
    }

    app->reader_search.match_count = 0;
    app->reader_search.is_mode = 0;
    app->reader_search.only_mode = 0;
    app->reader_search.current_match = 0;
    app->reader_search.match_current = 0;
}

/* Navigate to a raw msgs[] index, honouring the current sort order */
static int reader_goto_raw(UiApp *app, int raw_idx)
{
    UiSession *s = &app->sess;
    uint32_t target_mn;
    int i;

    if (raw_idx < 0 || raw_idx >= s->msg_count)
        return -1; /* invalid index */

    target_mn = s->msgs[raw_idx].msgnum;

    for (i = 0; i < s->order_count; i++)
    {
        if (s->msgs[s->order[i]].msgnum == target_mn)
        {
            s->msg_sel = i + 1;
            app->cur_msgnum = target_mn;

            return load_msg(app, target_mn);
        }
    }

    return -2;
}

/* Main loop */
UiView ui_reader_run(UiApp *app)
{
    int ch;
    UiSession *s;
    int saved_viewansi;

    if (!app)
        return VIEW_QUIT;

    s = &app->sess;
    saved_viewansi = app->cfg->viewansi;

    if (load_msg(app, app->cur_msgnum) != 0)
    {
        ui_status(app, "Cannot load message");
        app->cfg->viewansi = saved_viewansi;
        return reader_exit_view(app);
    }

    /* Apply ANSI visibility from config on first load */
    if (app->cfg->viewansi != rd_ansi_visible(app->reader))
    {
        rd_toggle_ansi(app->reader);

#ifdef PLATFORM_AMIGA
        amiga_change_font(rd_ansi_visible(app->reader));
#endif
    }

    for (;;)
    {
        erase();

        ui_draw_menubar(app, "Reader");
        draw_header_bar(app);
        draw_body(app);

        /* Status: msg_charset (CHRS or ?) -> decoded_charset (actual decode) */
        ui_status(app, "Charset: %s -> %s | c = Change charset%s | Ln %d/%d",
                  app->msg_charset[0] ? app->msg_charset : "?",
                  (app->view_charset[0]) ? app->decoded_charset : "Auto",
                  rd_ansi_visible(app->reader) ? " | ANSI" : "",
                  rd_top(app->reader) + 1, rd_total(app->reader));

        ui_draw_statusbar(app);
        refresh();

        ch = wrapper_getch();

        switch (ch)
        {
        case KEY_UP:
            rd_scroll_up(app->reader, 1);
            break;
        case KEY_DOWN:
            rd_scroll_down(app->reader, 1);
            break;
        case KEY_PPAGE:
        case 'b':
        case 'B':
        case CTRL('U'): /* Page Up (Amiga) */
            rd_page_up(app->reader);
            break;
        case KEY_NPAGE:
        case CTRL('D'): /* Page Down (Amiga) */
        case ' ':
            rd_page_down(app->reader);
            break;
        case KEY_HOME:
        case CTRL('B'): /* Home (Amiga) */
            rd_home(app->reader);
            break;
        case KEY_END:
        case CTRL('E'): /* End (Amiga) */
            rd_end(app->reader);
            break;

        case KEY_RIGHT:
        case 'n': /* Next message */
            if (s->msg_sel < s->order_count)
            {
                s->msg_sel++;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load next");
            }
            else
            {
                /* At last message: try to go to next area */
                try_goto_next_area(app);
            }
            break;
        case KEY_LEFT:
        case 'p': /* Previous message */
            if (s->msg_sel > 1)
            {
                s->msg_sel--;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load prev");
            }
            break;

        case CTRL('H'):
        case '<':
            /* Scroll to top of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) > 0)
                rd_scroll_up(app->reader, 1);

            break;

        case CTRL('K'):
        case '>':
            /* Scroll to bottom of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) + rd_visible(app->reader) < rd_total(app->reader))
                rd_scroll_down(app->reader, 1);

            break;

        case ',':
            /* First message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = 1;
                app->cur_msgnum = s->msgs[s->order[0]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load first");
            }
            break;

        case '.':
            /* Last message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = s->order_count;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load last");
            }
            break;
        case KEY_ALT('J'): /* Follow REPLY chain to original */
        case KEY_CLEFT:
        {
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int orig = (idx >= 0) ? ftn_find_original(s->msgs, s->msg_count, idx) : -1;

            /* Fallback: search by MSGID string from REPLY kludge */
            if (orig < 0 && app->cur_reply[0])
                orig = ftn_find_original_by_msgid(s->msgs, s->msg_count, app->cur_reply);

            if (orig < 0)
                ui_status(app, "No original message in this area");
            else if (orig >= s->msg_count)
                ui_status(app, "Invalid original message index");
            else if (s->msgs[orig].from[0] == '\0' || s->msgs[orig].subject[0] == '\0')
                ui_status(app, "Original message is empty or corrupted");
            else
            {
                int rc = reader_goto_raw(app, orig);

                if (rc == -2)
                    ui_status(app, "Original not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load original");
            }

            break;
        }
        case KEY_CRIGHT: /* Follow thread to reply (with picker if branched) */
        {
#define MAX_REPLIES 256
            int raw_replies[MAX_REPLIES];
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int nreplies = (idx >= 0) ? ftn_find_all_replies(s->msgs, s->msg_count, idx, raw_replies, MAX_REPLIES) : 0;

            if (nreplies == 0)
            {
                ui_status(app, "No replies to this message");
            }
            else if (nreplies == 1)
            {
                int rc = reader_goto_raw(app, raw_replies[0]);
                if (rc == -2)
                    ui_status(app, "Reply not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load reply");
            }
            else
            {
                /* Multiple replies: build label list and show picker */
                const char *items[MAX_REPLIES];
                char labels[MAX_REPLIES][80];
                int i, choice;

                for (i = 0; i < nreplies; i++)
                {
                    const JamMsgInfo *m = &s->msgs[raw_replies[i]];

                    snprintf(labels[i], sizeof(labels[i]), "#%-5u  %-20.20s  %.28s", (unsigned)m->msgnum, m->from, m->subject);
                    items[i] = labels[i];
                }

                choice = ui_popup_list("Select reply", items, nreplies, 0);

                if (choice >= 0)
                {
                    int rc = reader_goto_raw(app, raw_replies[choice]);

                    if (rc == -2)
                        ui_status(app, "Reply not visible in current filter");
                    else if (rc != 0)
                        ui_status(app, "Cannot load reply");
                }
            }
#undef MAX_REPLIES
            break;
        }

        case 'c':
        case 'C':
        {
            char new_view[32], new_out[32];

            new_view[0] = '\0';
            new_out[0] = '\0';

            if (ui_popup_charset_pair(app->view_charset, app->edit_charset, CHARSET_READ_DEFAULT, app->cfg->charset, new_view, sizeof(new_view), new_out, sizeof(new_out)) == 0)
            {
                /* Empty string = Auto (CHARSET_READ_DEFAULT); load_msg treats "" as no override */
                strncpy(app->view_charset, new_view, sizeof(app->view_charset) - 1);
                app->view_charset[sizeof(app->view_charset) - 1] = '\0';

                strncpy(app->edit_charset, new_out, sizeof(app->edit_charset) - 1);
                app->edit_charset[sizeof(app->edit_charset) - 1] = '\0';

                /* Mark that user manually changed charset (from reader) - only if NOT Auto */
                if (new_out[0] != '\0')
                    app->edit_charset_manually_changed = 1;

                /* Reload with new charset; preserves headers, lastread, ANSI, kludges */
                load_msg(app, app->cur_msgnum);

                ui_status(app, "View: %s | Save: %s", new_view[0] ? new_view : CHARSET_READ_DEFAULT, new_out[0] ? new_out : (app->cfg->charset[0] ? app->cfg->charset : "UTF-8"));
            }

            break;
        }

        case 'k':
        case 'K':
            rd_toggle_kludges(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case 'v':
        case 'V':
            rd_toggle_hiddklud(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Hidden+Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case KEY_F(5): /* Search in message body */
        case '/':
        {
            wchar_t tmp[64];
            int *rows = NULL, *cols = NULL;
            int match_count;
            int i;
            const char **contexts = NULL;
            char **context_bufs = NULL;
            int *line_nums = NULL;

            wcsncpy(tmp, app->reader_search.query, 63);
            tmp[63] = L'\0';

            if (ui_popup_input("Search", "Text:", tmp, 64) == 0 && tmp[0])
            {
                wcsncpy(app->reader_search.query, tmp, 63);
                app->reader_search.query[63] = L'\0';

                /* Find all matches in all lines (no limit) */
                match_count = rd_search_all(app->reader, app->reader_search.query, &rows, &cols);

                /* Free previous search matches */
                reader_clear_search(app);

                if (match_count == 0)
                {
                    ui_status(app, "Not found");
                }
                else if (match_count == 1)
                {
                    /* Single match: scroll to it */
                    rd_set_page(app->reader, LINES - 8);

                    if (rows[0] < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - rows[0]);
                    else if (rows[0] >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, rows[0] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Found at line %d", rows[0] + 1);

                    /* Save matches for highlighting and activate search mode */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
                else
                {
                    /* Allocate arrays for display */
                    contexts = (const char **)malloc((size_t)match_count * sizeof(const char *));
                    context_bufs = (char **)malloc((size_t)match_count * sizeof(char *));
                    line_nums = (int *)malloc((size_t)match_count * sizeof(int));

                    if (!contexts || !context_bufs || !line_nums)
                    {
                        if (contexts)
                            free(contexts);

                        if (context_bufs)
                            free(context_bufs);

                        if (line_nums)
                            free(line_nums);

                        free(rows);
                        free(cols);
                        ui_status(app, "Memory error");

                        break;
                    }

                    /* Multiple matches: show list */
                    for (i = 0; i < match_count; i++)
                    {
                        const wchar_t *line = rd_get_line(app->reader, rows[i]);
                        int line_len = rd_get_len(app->reader, rows[i]);

                        context_bufs[i] = (char *)malloc(128);

                        if (!context_bufs[i])
                        {
                            context_bufs[i] = NULL;
                            contexts[i] = "";
                        }
                        else
                        {
                            /* Convert context to UTF-8 for display (max 60 chars) */
                            if (line && line_len > 0)
                            {
                                int copy_len = line_len;

                                if (copy_len > 60)
                                    copy_len = 60;

                                char *utf8 = wcs_to_utf8(line, copy_len);

                                if (utf8)
                                {
                                    snprintf(context_bufs[i], 128, "%s", utf8);
                                    free(utf8);
                                }
                                else
                                {
                                    context_bufs[i][0] = '\0';
                                }
                            }
                            else
                            {
                                context_bufs[i][0] = '\0';
                            }
                        }

                        contexts[i] = context_bufs[i];
                        line_nums[i] = rows[i] + 1; /* 1-based for display */
                    }

                    /* Show popup with results */
                    int choice = ui_popup_search_results("Search Results", line_nums, contexts, match_count, 0);

                    if (choice >= 0)
                    {
                        rd_set_page(app->reader, LINES - 8);

                        if (rows[choice] < rd_top(app->reader))
                            rd_scroll_up(app->reader, rd_top(app->reader) - rows[choice]);
                        else if (rows[choice] >= rd_top(app->reader) + rd_visible(app->reader))
                            rd_scroll_down(app->reader, rows[choice] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                        app->reader_search.current_match = choice;
                        app->reader_search.match_current = choice + 1;
                        ui_status(app, "Jumped to line %d", rows[choice] + 1);
                    }
                    else
                    {
                        ui_status(app, "Search cancelled");
                    }

                    /* Free allocated memory */
                    for (i = 0; i < match_count; i++)
                    {
                        if (context_bufs[i])
                            free(context_bufs[i]);
                    }

                    free(context_bufs);
                    free(contexts);
                    free(line_nums);

                    /* Save matches for highlighting and activate search mode (don't free rows/cols) */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
            }
            break;
        }

        case 'a':
        case 'A':
            rd_toggle_ansi(app->reader);
            app->cfg->viewansi = rd_ansi_visible(app->reader);
#ifdef PLATFORM_AMIGA
            amiga_change_font(rd_ansi_visible(app->reader));
#endif
            /* Reload message to apply/re-apply ANSI parsing with new font */
            load_msg(app, app->cur_msgnum);
            /* Continue to next iteration to redraw with new font */
            continue;

        case 'r':
        case 'R':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_reply(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'e':
        case 'E':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_edit(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'N':
        case KEY_IC: /* New message */
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_new(app);
            return VIEW_EDITOR;

        case '\r':
        case '\n':
        case KEY_ENTER:
            /* Page down or advance to next message at bottom */
            if (rd_top(app->reader) + rd_visible(app->reader) >= rd_total(app->reader))
            {
                if (s->msg_sel < s->order_count)
                {
                    s->msg_sel++;
                    app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load next");
                }
                else
                {
                    /* At last message: try to go to next area */
                    try_goto_next_area(app);
                }
            }
            else
                rd_page_down(app->reader);
            break;

        case CTRL('G'): /* Goto line number */
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Line number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long line_num = 0;
                int target_line;

                if (u)
                {
                    line_num = strtol(u, NULL, 10);
                    free(u);
                }

                target_line = (int)(line_num - 1); /* Convert to 0-based */

                if (target_line >= 0 && target_line < rd_total(app->reader))
                {
                    rd_set_page(app->reader, LINES - 8);

                    /* Scroll to target line */
                    if (target_line < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - target_line);
                    else if (target_line >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, target_line - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Jumped to line %ld", line_num);
                }
                else
                {
                    ui_status(app, "Invalid line number");
                }
            }
            break;
        }

        case KEY_ALT('G'): /* Clear search highlights */
        {
            reader_clear_search(app);
            ui_status(app, "Search highlights cleared");

            continue; /* Force redraw */
        }

        /* Navigate to previous match in reader */
        case KEY_F(3):
        case KEY_ALT('P'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match - 1 + app->reader_search.match_count) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        /* Navigate to next match in reader */
        case KEY_F(4):
        case KEY_ALT('N'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match + 1) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        case 'g': /* Goto message number */
        case 'G':
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Message number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long mn = 0;

                if (u)
                {
                    mn = strtol(u, NULL, 10);
                    free(u);
                }

                int i, found = -1;

                for (i = 0; i < s->order_count; i++)
                {
                    if ((long)s->msgs[s->order[i]].msgnum == mn)
                    {
                        found = i;
                        break;
                    }
                }

                if (found >= 0)
                {
                    s->msg_sel = found + 1;
                    app->cur_msgnum = (uint32_t)mn;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load message");
                }
                else
                    ui_status(app, "No such message in this view");
            }

            break;
        }

        case 'w': /* Write message to text file */
        case 'W':
        case KEY_F(7):
        {
            char path[256];
            char out_cs[CHARSET_NAME_MAX];
            UiSession *s;
            char *body_utf8;
            int written;

            path[0] = '\0';
            out_cs[0] = '\0';

            snprintf(path, sizeof(path), "msg_%u.txt", (unsigned)app->cur_msgnum);

            /*if (ui_popup_input("Write message", "File path:", path, sizeof(path)) != 0 || !path[0])
                break;
            */

            if (ui_files_save("Write message", NULL, path, path, sizeof(path)) != 0)
                break;

            /* Charset of the output file. Empty == AUTO == write UTF-8
             * verbatim; the user can type "CP437", "LATIN-1", etc */
            if (ui_popup_charset("Output charset", "", out_cs, sizeof(out_cs)) != 0)
                break;

            s = &app->sess;
            body_utf8 = wrapper_read_utf8_ex(&s->jam, app->cur_msgnum, app->view_charset[0] ? app->view_charset : NULL, NULL, app->msg_charset, sizeof(app->msg_charset));

            if (!body_utf8)
            {
                ui_status(app, "Cannot read message body");
                break;
            }

            written = rd_export_to_file(app->reader, body_utf8, path, out_cs[0] ? out_cs : NULL);
            free(body_utf8);

            if (written < 0)
                ui_status(app, "Cannot write %s", path);
            else
                ui_status(app, "Wrote %d lines to %s (%s)", written, path, out_cs[0] ? out_cs : "UTF-8");

            break;
        }
        case 'd':
        case 'D':
        case KEY_DC: /* Delete message */
            if (ui_popup_confirm("Delete", "Mark this message as deleted?") == 1)
            {
                if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
                {
                    if (jam_delete_msg(&s->jam, app->cur_msgnum) == 0)
                        ui_status(app, "Message deleted");

                    jam_unlock(&s->jam);
                    free(s->msgs);

                    s->msgs = NULL;
                    s->msgs = jam_load_headers(&s->jam, &s->msg_count, 0, (uint32_t)app->cfg->msglistmax);

                    if (!s->msgs)
                    {
                        s->msg_count = 0;
                        ui_status(app, "Reload failed after delete");
                    }
                    else
                    {
                        ui_session_rebuild_order(app);
                    }
                }
            }

            reader_save_lastread(app);

            /* reader_save_lastread() already updates area counts; skip redundant call */
            app->cfg->viewansi = saved_viewansi;

            return reader_exit_view(app);

            break;

        case KEY_F(1):
        case '?':
            ui_popup_help("Reader Help", READER_HELP, READER_HELP_N);
            break;

        case 'l':
        case 'L':
            /* Show msglist overlay; from search go to search browser instead */
            if (app->from_search)
            {
                reader_clear_search(app);
                app->cfg->viewansi = saved_viewansi;
                return VIEW_SEARCH_RESULTS;
            }

            reader_clear_search(app);
            reader_save_lastread(app);
            update_area_counts_in_memory(app);

            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 1;

            return VIEW_MSGLIST;

        case 27:
            /* ESC: exit search mode first, otherwise quit */
            if (app->reader_search.only_mode)
            {
                reader_clear_search(app);
                ui_status(app, "Search mode exited");

                continue; /* Force redraw */
            }
            /* Fall through to quit */
        case 'q':
        case 'Q':
            reader_save_lastread(app);
            update_area_counts_in_memory(app);
            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 0;

            reader_clear_search(app);

            return reader_exit_view(app);

        case KEY_RESIZE:
            flushinp();
            load_msg(app, app->cur_msgnum);
            break;
        case CTRL('F'):
            ui_popup_freq(app);
            break;
        default:
            break;
        }
    }
}

/*
 * crashedit - Message area editor for AmigaOS
 *
 * This file is part of the crashedit project.
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This program uses JAMLIB, which is licensed under the GNU Lesser
 * General Public License v2.1. See src/jamlib/LICENSE for details.
 */

/* ui_reader.c -- Message reader view */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"
#include "ui_attr.h"
#include "ui_files.h"
#include "../../src/jamlib/jam.h"
#include "../core/msghdr.h"
#include "../core/ftn.h"
#include "../core/ansi.h"
#include "../core/keys.h"

static const char *READER_HELP[] =
    {
        "Message Reader - Key Bindings:",
        "",
        "  Up/Down,j/k    Scroll body up/down",
#ifdef PLATFORM_AMIGA
        "  Ctrl+U/D       Page scroll",
        "  Ctrl+B/E       First / last line",
#else
        "  PgUp/PgDn      Page scroll",
        "  Home/End       First / last line",
#endif
        "  Left/Right,p/n Previous / next message",
        "",
        "  b/Space        Page up/down",
        "  Enter          Page down or advance to next message",
        "  < >            First / last line (scroll)",
        "  , .            First / last message",
        "  Ctrl-J         Jump to original (follow reply chain)",
        "  Ctrl+Left      Jump to original (follow reply chain)",
        "  Ctrl+Right     Jump to reply (pick if multiple)",
        "",
        "  k, v           Toggle kludges (^A, SEEN-BY, Via)",
        "  a              Toggle ANSI color rendering",
        "  c              Change DISPLAY charset (re-render)",
        "  r              Reply to this message",
        "  e              Edit this message",
        "  n, Ins         New message (in current area)",
        "  l, L           Show message list overlay (ESC returns here)",
        "  g              Goto message number",
        "  Ctrl+H         Scroll to top of message",
        "  Ctrl+K         Scroll to bottom of message",
        "  Ctrl+G         Goto line number",
        "  Alt+J          Follow reply chain to original",
        "  Alt+G          Clear search highlights",
        "  F5, /          Search in message body",
        "  F3 Alt+P       Previous match (search mode)",
        "  F4 Alt+N       Next match (search mode)",
        "  w, F7          Write message to text file",
        "  d, Del         Delete this message",
        "  Ctrl+F         File request",
        "",
        "  ESC, q         Back to message list",
        "  F1, ?          This help"};
#define READER_HELP_N ((int)(sizeof(READER_HELP) / sizeof(READER_HELP[0])))

void ui_editor_prep_new(UiApp *app);
void ui_editor_prep_reply(UiApp *app, uint32_t orig_msgnum);
void ui_editor_prep_edit(UiApp *app, uint32_t msgnum);

/* Update area's cached counts after reader moves lastread/lastseen */
static void update_area_counts_in_memory(UiApp *app)
{
    UiSession *s;
    AreaEntry *ae;
    int i, unread, new_count;

    if (!app)
        return;

    s = &app->sess;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return;

    ae = &app->areas->entries[s->area_idx];

    ae->lastread = s->lastread;
    ae->lastseen = s->lastseen;

    /* Recompute unread (above lastread) and new_count (above lastseen) */
    unread = 0;
    new_count = 0;

    for (i = 0; i < s->msg_count; i++)
    {
        if (s->msgs[i].msgnum > s->lastread)
            unread++;

        if (s->msgs[i].msgnum > s->lastseen)
            new_count++;
    }

    ae->unread = unread;
    ae->new_count = new_count;
    ae->total_msgs = s->msg_count;
}

/* Update lastread and persist to JAM; skip if from search (browsing shouldn't mutate) */
static void reader_save_lastread(UiApp *app)
{
    UiSession *s;
    uint32_t msgnum;

    if (!app)
        return;

    if (app->from_search)
        return;

    s = &app->sess;
    msgnum = app->cur_msgnum;

    s->lastread = msgnum;

    if (msgnum > s->lastseen)
        s->lastseen = msgnum;

    if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
    {
        jam_write_lastread(&s->jam, s->user_crc, s->lastread, s->lastseen);
        jam_unlock(&s->jam);
    }

    update_area_counts_in_memory(app);
}

/* Exit target: msglist (normal) or search results (if from search) */
static UiView reader_exit_view(const UiApp *app)
{
    return app->from_search ? VIEW_SEARCH_RESULTS : VIEW_MSGLIST;
}

/* Strip FTS-1 kludge lines and SEEN-BY/PATH trailers for ANSI rendering
 * Returns malloc'd buffer (caller frees), NULL on failure */
static char *strip_kludges_for_ansi(const char *raw, int raw_len, int *out_len)
{
    char *out;
    int i = 0;
    int j = 0;
    int at_line_start = 1;

    if (out_len)
        *out_len = 0;

    if (!raw || raw_len <= 0)
        return NULL;

    out = (char *)malloc((size_t)raw_len + 1);
    if (!out)
        return NULL;

    while (i < raw_len)
    {
        if (at_line_start)
        {
            const char *p = raw + i;
            int rem = raw_len - i;
            int is_kludge = 0;

            if (rem >= 1 && (unsigned char)*p == 0x01)
                is_kludge = 1;
            else if (rem >= 8 && memcmp(p, "SEEN-BY:", 8) == 0)
                is_kludge = 1;
            else if (rem >= 5 && memcmp(p, "PATH:", 5) == 0)
                is_kludge = 1;

            if (is_kludge)
            {
                /* Skip to end of this line (eat optional \r and \n) */
                while (i < raw_len && raw[i] != '\r' && raw[i] != '\n')
                    i++;

                if (i < raw_len && raw[i] == '\r')
                    i++;
                if (i < raw_len && raw[i] == '\n')
                    i++;

                /* Still at line start for the next iteration */
                continue;
            }

            at_line_start = 0;
        }

        out[j++] = raw[i];

        if (raw[i] == '\r' || raw[i] == '\n')
            at_line_start = 1;

        i++;
    }

    out[j] = '\0';

    if (out_len)
        *out_len = j;

    return out;
}

/* Load current message into reader */
static int load_msg(UiApp *app, uint32_t msgnum)
{
    UiSession *s;
    int idx;
    char *body_utf8 = NULL;
    int wrap_w;
    const wchar_t *cur;
    char *cur_utf8;
    int needs_fallback;
    int ansi_on_outer = 0;
    char detected[CHARSET_NAME_MAX];
    const char *override_enc;
    char chosen[CHARSET_NAME_MAX];
    int ansi_on;
    uint32_t raw_len = 0;
    char *raw_bytes = NULL;
    int has_chrs = 0;
    const char *p;
    int oi;

    if (!app)
        return -1;

    s = &app->sess;

    idx = jam_find_by_msgnum(s->msgs, s->msg_count, msgnum);

    if (idx < 0)
        return -1;

    /* Sync msg_sel to current message for correct navigation after search/jump entry */
    for (oi = 0; oi < s->order_count; oi++)
    {
        if (s->order[oi] == idx)
        {
            s->msg_sel = oi + 1;
            break;
        }
    }

    /* Fill header */
    msghdr_load(app->hdr, &s->msgs[idx], app->areas->entries[s->area_idx].name, idx + 1, s->msg_count, ftn_effective_tz_offset(app->cfg->timezone_offset, app->cfg->timezone_is_manual));

    /* Body: ANSI->canvas, text->UTF-8. Charset: CHRS if present, else default */
    override_enc = app->view_charset[0] ? app->view_charset : NULL;

    detected[0] = '\0';
    chosen[0] = '\0';

    /* Read and decode body using wrapper_read_utf8_ex with proper fallback logic */
    body_utf8 = wrapper_read_utf8_ex(&s->jam, msgnum, override_enc, NULL, detected, sizeof(detected));

    if (!body_utf8)
        return -1;

    /* Read raw body for ANSI mode (needs original bytes, not UTF-8) */
    raw_bytes = jam_read_body(&s->jam, msgnum, &raw_len);

    if (!raw_bytes)
    {
        free(body_utf8);
        return -1;
    }

    /* Set charset variables based on wrapper_read_utf8_ex results */
    strncpy(chosen, detected, sizeof(chosen) - 1);
    chosen[sizeof(chosen) - 1] = '\0';

    /* Check if we actually have a CHRS kludge */
    has_chrs = (detected[0] != '\0');

    if (has_chrs)
    {
        strncpy(app->msg_charset, detected, sizeof(app->msg_charset) - 1);
        app->msg_charset[sizeof(app->msg_charset) - 1] = '\0';
    }
    else
    {
        app->msg_charset[0] = '\0';
    }

    /* ANSI mode: force CP437 if UTF-8 came from default/fallback (not CHRS/override)
     * Exception: when TTF is active, keep detected charset to preserve Unicode characters */
    ansi_on = (app->cfg && app->cfg->viewansi);
    ansi_on_outer = ansi_on;

    if (ansi_on && (strcasecmp(chosen, "UTF-8") == 0 || strcasecmp(chosen, "UTF8") == 0))
    {
        int user_wants_utf8 = (override_enc && (strcasecmp(override_enc, "UTF-8") == 0 || strcasecmp(override_enc, "UTF8") == 0));
        int chrs_says_utf8 = (app->msg_charset[0] && (strcasecmp(app->msg_charset, "UTF-8") == 0 || strcasecmp(app->msg_charset, "UTF8") == 0));

        if (!user_wants_utf8 && !chrs_says_utf8)
        {
            strncpy(chosen, "CP437", sizeof(chosen) - 1);
            chosen[sizeof(chosen) - 1] = '\0';
        }
    }

    /* Record actual decode charset for status bar (may differ from view_charset) */
    if (override_enc && override_enc[0])
        strncpy(app->decoded_charset, override_enc, sizeof(app->decoded_charset) - 1);
    else
        strncpy(app->decoded_charset, chosen, sizeof(app->decoded_charset) - 1);

    app->decoded_charset[sizeof(app->decoded_charset) - 1] = '\0';

    /*wrap_w = COLS - 2;*/
    wrap_w = COLS;

    if (wrap_w < 20)
        wrap_w = 20;

    if (ansi_on)
    {
        /* Strip kludge/SEEN-BY/PATH lines. Fall back to raw bytes on failure */
        int stripped_len = 0;
        char *stripped = strip_kludges_for_ansi(raw_bytes, (int)raw_len, &stripped_len);

        if (stripped)
        {
            rd_load_ansi(app->reader, stripped, stripped_len, chosen, wrap_w);
            free(stripped);
        }
        else
        {
            rd_load_ansi(app->reader, raw_bytes, (int)raw_len, chosen, wrap_w);
        }

        /* Free unused UTF-8 buffer; ANSI mode uses raw bytes. Prevents leak */
        free(body_utf8);
        body_utf8 = NULL; /* not used in this path */
    }

    /* ANSI mode: convert kludges/SEEN-BY/Origin to UTF-8 for MSGID/origin lookup */
    if (ansi_on)
    {
        int srclen = (int)raw_len;
        int dstmax = srclen * 4 + 256;

        body_utf8 = (char *)malloc((size_t)dstmax);

        if (body_utf8)
            charset_body_to_utf8(chosen, raw_bytes, srclen, body_utf8, dstmax);
    }

    free(raw_bytes);

    if (!body_utf8)
        return -1;

    /* Extract MSGID / REPLY for header display */
    app->cur_msgid[0] = '\0';
    app->cur_reply[0] = '\0';

    ftn_get_kludge_value(body_utf8, "MSGID", app->cur_msgid, sizeof(app->cur_msgid));
    ftn_get_kludge_value(body_utf8, "REPLY", app->cur_reply, sizeof(app->cur_reply));

    /* Get sender address from MSGID kludge, fallback to Origin line */

    cur = msghdr_get(app->hdr, HDR_OADDR);
    cur_utf8 = cur ? wcs_to_utf8(cur, (int)wcslen(cur)) : NULL;

    if (cur && !cur_utf8)
    {
        free(body_utf8); /* would otherwise leak on this OOM path */
        return -1;
    }

    needs_fallback = (!cur_utf8 || !cur_utf8[0] || strcmp(cur_utf8, "0:0/0") == 0);

    if (needs_fallback)
    {
        char addr[64];
        int got = -1;

        if (app->cur_msgid[0])
        {
            /* Extract address from MSGID (format: "addr serial") */
            int i;

            addr[0] = '\0';

            for (i = 0; app->cur_msgid[i] && app->cur_msgid[i] != ' ' && app->cur_msgid[i] != '\t' && i < (int)sizeof(addr) - 1; i++)
                addr[i] = app->cur_msgid[i];

            addr[i] = '\0';

            if (addr[0])
                got = 0;
        }

        if (got != 0)
            got = ftn_find_origin_address(body_utf8, addr, sizeof(addr));

        if (got == 0)
            msghdr_set_utf8(app->hdr, HDR_OADDR, addr);
    }

    free(cur_utf8);

    wrap_w = COLS; /* full width, match editor soft-wrap */

    if (wrap_w < 20)
        wrap_w = 20;

    /* ANSI: canvas loaded above, skip rd_load. Non-ANSI: rd_load handles wrap */
    if (!ansi_on_outer)
        rd_load(app->reader, body_utf8, wrap_w);

    free(body_utf8);

    rd_set_page(app->reader, LINES - 8);

    /* Apply view-config toggles (kludges visibility) */
    if (app->cfg->viewkludge != rd_kludges_visible(app->reader))
        rd_toggle_kludges(app->reader);

    return 0;
}

/* Try to go to next area in area_order. Returns 1 on success, 0 otherwise */
static int try_goto_next_area(UiApp *app)
{
    UiSession *s;
    int current_area_idx;
    int current_order_idx = -1;
    int next_area_idx = -1;
    int i;
    AreaEntry *next_ae;
    char prompt[256];

    if (!app)
        return 0;

    s = &app->sess;

    if (app->area_order_count <= 0)
        return 0;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return 0;

    current_area_idx = s->area_idx;

    /* Find current area in area_order */
    for (i = 0; i < app->area_order_count; i++)
    {
        if (app->area_order[i] == current_area_idx)
        {
            current_order_idx = i;
            break;
        }
    }

    /* Find next area in order */
    if (current_order_idx >= 0 && current_order_idx < app->area_order_count - 1)
    {
        next_area_idx = app->area_order[current_order_idx + 1];
    }

    if (next_area_idx < 0)
        return 0;

    next_ae = &app->areas->entries[next_area_idx];
    snprintf(prompt, sizeof(prompt), "Go to next area '%s'?", next_ae->name ? next_ae->name : "(area)");

    if (ui_popup_confirm("Next area", prompt) != 1)
        return 0;

    /* Save lastread before changing area */
    reader_save_lastread(app);

    /* Close current session and open next area */
    ui_session_close(app);

    if (ui_session_open(app, next_area_idx) != 0)
    {
        ui_status(app, "Cannot open area");
        return 0;
    }

    /* Load message from new area */
    if (s->order_count > 0)
    {
        /* Check if current position is on an unread message */
        uint32_t current_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        /* If not on unread message, go to last message in list */
        if (current_msgnum <= s->lastread)
            s->msg_sel = s->order_count;

        app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        if (load_msg(app, app->cur_msgnum) != 0)
        {
            ui_status(app, "Cannot load message");
            return 0;
        }
    }

    return 1;
}

/* Header bar */
static void draw_header_bar(UiApp *app)
{
    int i;
    const wchar_t *from, *to, *subj, *date;
    char attr_str[40];
    static const char SPACES[256] =
        "                                                                "
        "                                                                "
        "                                                                "
        "                                                                ";

    from = msghdr_get(app->hdr, HDR_FROM);
    to = msghdr_get(app->hdr, HDR_TO);
    subj = msghdr_get(app->hdr, HDR_SUBJECT);
    date = msghdr_get(app->hdr, HDR_DATE);
    ui_attr_build(msghdr_attr(app->hdr), attr_str, sizeof(attr_str));

    attron(COLOR_PAIR(COL_HEADER));

    for (i = 1; i < 7; i++)
    {
        int written = 0;
        move(i, 0);
        clrtoeol();

        while (written < COLS)
        {
            int chunk = COLS - written;

            if (chunk > (int)sizeof(SPACES))
                chunk = (int)sizeof(SPACES);

            mvaddnstr(i, written, SPACES, chunk);
            written += chunk;
        }
    }

    mvprintw(1, 1, "Area: %-30.30s  Msg %d/%d", ui_wcs2u8(msghdr_get(app->hdr, HDR_AREA)), msghdr_msgnum(app->hdr), msghdr_msgtotal(app->hdr));
    mvprintw(2, 1, "From: %-25.25s  %-20.20s  %s", ui_wcs2u8(from), ui_wcs2u8(msghdr_get(app->hdr, HDR_OADDR)), ui_wcs2u8(date));
    mvprintw(3, 1, "  To: %-25.25s  %s", ui_wcs2u8(to), ui_wcs2u8(msghdr_get(app->hdr, HDR_DADDR)));
    mvprintw(4, 1, "Subj: %s", ui_wcs2u8(subj));

    /* Attr on left, MSGID on right */
    mvprintw(5, 1, "Attr: %s", attr_str);

    if (app->cur_msgid[0])
    {
        int show = (int)strlen(app->cur_msgid);
        int max_room = COLS - 22;

        if (max_room < 10)
            max_room = 10;

        if (show > max_room)
            show = max_room;

        mvprintw(5, COLS - show - 8, "MSGID: %.*s", show, app->cur_msgid);
    }

    attroff(COLOR_PAIR(COL_HEADER));

    attron(COLOR_PAIR(COL_BORDER));
    ui_hline(6, 0, COLS);
    attroff(COLOR_PAIR(COL_BORDER));
}

/* Body rendering */
/* Convert one wchar_t to UTF-8 in buf. Returns bytes written */
static int wc_to_utf8_bytes(wchar_t wc, char *buf)
{
    unsigned long cp = (unsigned long)wc;

    if (cp < 0x80)
    {
        buf[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));

        return 2;
    }

    if (cp < 0x10000)
    {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));

        return 3;
    }

    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));

    return 4;
}

/* Draw ANSI line: collect runs of same attrs and emit each run in one call */
static void draw_ansi_line(int row_y, const wchar_t *wcs, int len, const struct AnsiCell *cells, int max_cols)
{
    char run_buf[1024];
    int i = 0;
    int x = 0;

    while (i < len && x < max_cols)
    {
        int run_color = cells[i].color_pair;
        int run_attrs = cells[i].attrs;
        int run_start = i;
        int run_chars = 0;
        int run_bytes = 0;

        /* Find extent of run with identical (color, attrs) */
        while (i < len && x + run_chars < max_cols && cells[i].color_pair == run_color && cells[i].attrs == run_attrs)
        {
            int n;

            if (run_bytes + 4 >= (int)sizeof(run_buf))
                break; /* Don't overflow run buffer; emit and continue */

            n = wc_to_utf8_bytes(wcs[i], &run_buf[run_bytes]);
            run_bytes += n;
            run_chars++;
            i++;
        }

        if (run_chars == 0)
            break; /* safety */

        run_buf[run_bytes] = '\0';

        if (run_color >= 0)
            attron(COLOR_PAIR(run_color) | run_attrs);

        mvaddnstr(row_y, x, run_buf, run_bytes);

        if (run_color >= 0)
            attroff(COLOR_PAIR(run_color) | run_attrs);

        x += run_chars;
    }
}

static void draw_body(UiApp *app)
{
    int rows = LINES - 8;
    int i;
    const wchar_t *wcs;
    int wlen;

    if (rows < 1)
        return;

    rd_set_page(app->reader, rows);

    for (i = 0; i < rows; i++)
    {
        int vi = rd_top(app->reader) + i;
        int type, col;
        int ansi_color, ansi_attrs;
        const struct AnsiCell *cells;
        int row_y = 7 + i;
        const wchar_t *ln;
        int lnlen;

        /* Skip if terminal shrunk since rd_set_page */
        if (row_y < 0 || row_y >= LINES - 1)
            continue;

        move(row_y, 0);
        clrtoeol();

        if (vi >= rd_total(app->reader))
            continue;

        type = rd_get_type(app->reader, vi);
        ansi_color = rd_get_ansi_color(app->reader, vi);
        ansi_attrs = rd_get_ansi_attrs(app->reader, vi);
        cells = rd_get_ansi_cells(app->reader, vi);

        /* ANSI with per-cell attrs: draw via canvas (preserves BBS art colors) */
        if (rd_ansi_visible(app->reader) && cells)
        {
            ln = rd_get_line(app->reader, vi);
            lnlen = rd_get_len(app->reader, vi);

            if (ln && lnlen > 0)
                draw_ansi_line(row_y, ln, lnlen, cells, (COLS < 80) ? COLS : 80);

            continue; /* attrs handled inside draw_ansi_line */
        }

        if (rd_ansi_visible(app->reader) && ansi_color >= 0)
        {
            col = ansi_color;
            attron(COLOR_PAIR(col) | ansi_attrs);
        }
        else
        {
            col = ui_color_for_type(type);
            attron(COLOR_PAIR(col));
            ansi_attrs = 0;
        }

        /* Plain or ANSI without per-cell attrs: draw with line color */
        ln = rd_get_line(app->reader, vi);
        lnlen = rd_get_len(app->reader, vi);

        /* Skip leading ^A on kludge lines */
        if (ln && lnlen > 0 && (type & FTN_LT_KLUDGE) && ln[0] == L'\x01')
        {
            ln++;
            lnlen--;
        }

        if (ln && lnlen > 0)
            mvaddnwstr(row_y, 0, ln, lnlen);

        /* Highlight search matches (word-level) */
        if (app->reader_search.rows && app->reader_search.cols && app->reader_search.match_count > 0)
        {
            int line_idx = rd_get_line_idx(app->reader, vi);
            int j;

            if (line_idx >= 0)
            {
                for (j = 0; j < app->reader_search.match_count; j++)
                {
                    if (app->reader_search.rows[j] == line_idx)
                    {
                        int match_col = app->reader_search.cols[j];
                        int match_len = (int)wcslen(app->reader_search.query);

                        if (match_col >= 0 && match_col + match_len <= lnlen)
                        {
                            if (j == app->reader_search.current_match)
                                attron(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attron(COLOR_PAIR(COL_SEARCH_MATCH));

                            mvaddnwstr(row_y, match_col, &ln[match_col], match_len);

                            if (j == app->reader_search.current_match)
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
            }
        }

        attroff(COLOR_PAIR(col) | ansi_attrs);
    }
}

/* Clear search state in reader */
static void reader_clear_search(UiApp *app)
{
    if (app->reader_search.rows)
    {
        free(app->reader_search.rows);
        app->reader_search.rows = NULL;
    }

    if (app->reader_search.cols)
    {
        free(app->reader_search.cols);
        app->reader_search.cols = NULL;
    }

    app->reader_search.match_count = 0;
    app->reader_search.is_mode = 0;
    app->reader_search.only_mode = 0;
    app->reader_search.current_match = 0;
    app->reader_search.match_current = 0;
}

/* Navigate to a raw msgs[] index, honouring the current sort order */
static int reader_goto_raw(UiApp *app, int raw_idx)
{
    UiSession *s = &app->sess;
    uint32_t target_mn;
    int i;

    if (raw_idx < 0 || raw_idx >= s->msg_count)
        return -1; /* invalid index */

    target_mn = s->msgs[raw_idx].msgnum;

    for (i = 0; i < s->order_count; i++)
    {
        if (s->msgs[s->order[i]].msgnum == target_mn)
        {
            s->msg_sel = i + 1;
            app->cur_msgnum = target_mn;

            return load_msg(app, target_mn);
        }
    }

    return -2;
}

/* Main loop */
UiView ui_reader_run(UiApp *app)
{
    int ch;
    UiSession *s;
    int saved_viewansi;

    if (!app)
        return VIEW_QUIT;

    s = &app->sess;
    saved_viewansi = app->cfg->viewansi;

    if (load_msg(app, app->cur_msgnum) != 0)
    {
        ui_status(app, "Cannot load message");
        app->cfg->viewansi = saved_viewansi;
        return reader_exit_view(app);
    }

    /* Apply ANSI visibility from config on first load */
    if (app->cfg->viewansi != rd_ansi_visible(app->reader))
    {
        rd_toggle_ansi(app->reader);

#ifdef PLATFORM_AMIGA
        amiga_change_font(rd_ansi_visible(app->reader));
#endif
    }

    for (;;)
    {
        erase();

        ui_draw_menubar(app, "Reader");
        draw_header_bar(app);
        draw_body(app);

        /* Status: msg_charset (CHRS or ?) -> decoded_charset (actual decode) */
        ui_status(app, "Charset: %s -> %s | c = Change charset%s | Ln %d/%d",
                  app->msg_charset[0] ? app->msg_charset : "?",
                  (app->view_charset[0]) ? app->decoded_charset : "Auto",
                  rd_ansi_visible(app->reader) ? " | ANSI" : "",
                  rd_top(app->reader) + 1, rd_total(app->reader));

        ui_draw_statusbar(app);
        refresh();

        ch = wrapper_getch();

        switch (ch)
        {
        case KEY_UP:
            rd_scroll_up(app->reader, 1);
            break;
        case KEY_DOWN:
            rd_scroll_down(app->reader, 1);
            break;
        case KEY_PPAGE:
        case 'b':
        case 'B':
        case CTRL('U'): /* Page Up (Amiga) */
            rd_page_up(app->reader);
            break;
        case KEY_NPAGE:
        case CTRL('D'): /* Page Down (Amiga) */
        case ' ':
            rd_page_down(app->reader);
            break;
        case KEY_HOME:
        case CTRL('B'): /* Home (Amiga) */
            rd_home(app->reader);
            break;
        case KEY_END:
        case CTRL('E'): /* End (Amiga) */
            rd_end(app->reader);
            break;

        case KEY_RIGHT:
        case 'n': /* Next message */
            if (s->msg_sel < s->order_count)
            {
                s->msg_sel++;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load next");
            }
            else
            {
                /* At last message: try to go to next area */
                try_goto_next_area(app);
            }
            break;
        case KEY_LEFT:
        case 'p': /* Previous message */
            if (s->msg_sel > 1)
            {
                s->msg_sel--;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load prev");
            }
            break;

        case CTRL('H'):
        case '<':
            /* Scroll to top of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) > 0)
                rd_scroll_up(app->reader, 1);

            break;

        case CTRL('K'):
        case '>':
            /* Scroll to bottom of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) + rd_visible(app->reader) < rd_total(app->reader))
                rd_scroll_down(app->reader, 1);

            break;

        case ',':
            /* First message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = 1;
                app->cur_msgnum = s->msgs[s->order[0]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load first");
            }
            break;

        case '.':
            /* Last message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = s->order_count;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load last");
            }
            break;
        case KEY_ALT('J'): /* Follow REPLY chain to original */
        case KEY_CLEFT:
        {
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int orig = (idx >= 0) ? ftn_find_original(s->msgs, s->msg_count, idx) : -1;

            /* Fallback: search by MSGID string from REPLY kludge */
            if (orig < 0 && app->cur_reply[0])
                orig = ftn_find_original_by_msgid(s->msgs, s->msg_count, app->cur_reply);

            if (orig < 0)
                ui_status(app, "No original message in this area");
            else if (orig >= s->msg_count)
                ui_status(app, "Invalid original message index");
            else if (s->msgs[orig].from[0] == '\0' || s->msgs[orig].subject[0] == '\0')
                ui_status(app, "Original message is empty or corrupted");
            else
            {
                int rc = reader_goto_raw(app, orig);

                if (rc == -2)
                    ui_status(app, "Original not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load original");
            }

            break;
        }
        case KEY_CRIGHT: /* Follow thread to reply (with picker if branched) */
        {
#define MAX_REPLIES 256
            int raw_replies[MAX_REPLIES];
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int nreplies = (idx >= 0) ? ftn_find_all_replies(s->msgs, s->msg_count, idx, raw_replies, MAX_REPLIES) : 0;

            if (nreplies == 0)
            {
                ui_status(app, "No replies to this message");
            }
            else if (nreplies == 1)
            {
                int rc = reader_goto_raw(app, raw_replies[0]);
                if (rc == -2)
                    ui_status(app, "Reply not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load reply");
            }
            else
            {
                /* Multiple replies: build label list and show picker */
                const char *items[MAX_REPLIES];
                char labels[MAX_REPLIES][80];
                int i, choice;

                for (i = 0; i < nreplies; i++)
                {
                    const JamMsgInfo *m = &s->msgs[raw_replies[i]];

                    snprintf(labels[i], sizeof(labels[i]), "#%-5u  %-20.20s  %.28s", (unsigned)m->msgnum, m->from, m->subject);
                    items[i] = labels[i];
                }

                choice = ui_popup_list("Select reply", items, nreplies, 0);

                if (choice >= 0)
                {
                    int rc = reader_goto_raw(app, raw_replies[choice]);

                    if (rc == -2)
                        ui_status(app, "Reply not visible in current filter");
                    else if (rc != 0)
                        ui_status(app, "Cannot load reply");
                }
            }
#undef MAX_REPLIES
            break;
        }

        case 'c':
        case 'C':
        {
            char new_view[32], new_out[32];

            new_view[0] = '\0';
            new_out[0] = '\0';

            if (ui_popup_charset_pair(app->view_charset, app->edit_charset, CHARSET_READ_DEFAULT, app->cfg->charset, new_view, sizeof(new_view), new_out, sizeof(new_out)) == 0)
            {
                /* Empty string = Auto (CHARSET_READ_DEFAULT); load_msg treats "" as no override */
                strncpy(app->view_charset, new_view, sizeof(app->view_charset) - 1);
                app->view_charset[sizeof(app->view_charset) - 1] = '\0';

                strncpy(app->edit_charset, new_out, sizeof(app->edit_charset) - 1);
                app->edit_charset[sizeof(app->edit_charset) - 1] = '\0';

                /* Mark that user manually changed charset (from reader) - only if NOT Auto */
                if (new_out[0] != '\0')
                    app->edit_charset_manually_changed = 1;

                /* Reload with new charset; preserves headers, lastread, ANSI, kludges */
                load_msg(app, app->cur_msgnum);

                ui_status(app, "View: %s | Save: %s", new_view[0] ? new_view : CHARSET_READ_DEFAULT, new_out[0] ? new_out : (app->cfg->charset[0] ? app->cfg->charset : "UTF-8"));
            }

            break;
        }

        case 'k':
        case 'K':
            rd_toggle_kludges(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case 'v':
        case 'V':
            rd_toggle_hiddklud(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Hidden+Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case KEY_F(5): /* Search in message body */
        case '/':
        {
            wchar_t tmp[64];
            int *rows = NULL, *cols = NULL;
            int match_count;
            int i;
            const char **contexts = NULL;
            char **context_bufs = NULL;
            int *line_nums = NULL;

            wcsncpy(tmp, app->reader_search.query, 63);
            tmp[63] = L'\0';

            if (ui_popup_input("Search", "Text:", tmp, 64) == 0 && tmp[0])
            {
                wcsncpy(app->reader_search.query, tmp, 63);
                app->reader_search.query[63] = L'\0';

                /* Find all matches in all lines (no limit) */
                match_count = rd_search_all(app->reader, app->reader_search.query, &rows, &cols);

                /* Free previous search matches */
                reader_clear_search(app);

                if (match_count == 0)
                {
                    ui_status(app, "Not found");
                }
                else if (match_count == 1)
                {
                    /* Single match: scroll to it */
                    rd_set_page(app->reader, LINES - 8);

                    if (rows[0] < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - rows[0]);
                    else if (rows[0] >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, rows[0] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Found at line %d", rows[0] + 1);

                    /* Save matches for highlighting and activate search mode */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
                else
                {
                    /* Allocate arrays for display */
                    contexts = (const char **)malloc((size_t)match_count * sizeof(const char *));
                    context_bufs = (char **)malloc((size_t)match_count * sizeof(char *));
                    line_nums = (int *)malloc((size_t)match_count * sizeof(int));

                    if (!contexts || !context_bufs || !line_nums)
                    {
                        if (contexts)
                            free(contexts);

                        if (context_bufs)
                            free(context_bufs);

                        if (line_nums)
                            free(line_nums);

                        free(rows);
                        free(cols);
                        ui_status(app, "Memory error");

                        break;
                    }

                    /* Multiple matches: show list */
                    for (i = 0; i < match_count; i++)
                    {
                        const wchar_t *line = rd_get_line(app->reader, rows[i]);
                        int line_len = rd_get_len(app->reader, rows[i]);

                        context_bufs[i] = (char *)malloc(128);

                        if (!context_bufs[i])
                        {
                            context_bufs[i] = NULL;
                            contexts[i] = "";
                        }
                        else
                        {
                            /* Convert context to UTF-8 for display (max 60 chars) */
                            if (line && line_len > 0)
                            {
                                int copy_len = line_len;

                                if (copy_len > 60)
                                    copy_len = 60;

                                char *utf8 = wcs_to_utf8(line, copy_len);

                                if (utf8)
                                {
                                    snprintf(context_bufs[i], 128, "%s", utf8);
                                    free(utf8);
                                }
                                else
                                {
                                    context_bufs[i][0] = '\0';
                                }
                            }
                            else
                            {
                                context_bufs[i][0] = '\0';
                            }
                        }

                        contexts[i] = context_bufs[i];
                        line_nums[i] = rows[i] + 1; /* 1-based for display */
                    }

                    /* Show popup with results */
                    int choice = ui_popup_search_results("Search Results", line_nums, contexts, match_count, 0);

                    if (choice >= 0)
                    {
                        rd_set_page(app->reader, LINES - 8);

                        if (rows[choice] < rd_top(app->reader))
                            rd_scroll_up(app->reader, rd_top(app->reader) - rows[choice]);
                        else if (rows[choice] >= rd_top(app->reader) + rd_visible(app->reader))
                            rd_scroll_down(app->reader, rows[choice] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                        app->reader_search.current_match = choice;
                        app->reader_search.match_current = choice + 1;
                        ui_status(app, "Jumped to line %d", rows[choice] + 1);
                    }
                    else
                    {
                        ui_status(app, "Search cancelled");
                    }

                    /* Free allocated memory */
                    for (i = 0; i < match_count; i++)
                    {
                        if (context_bufs[i])
                            free(context_bufs[i]);
                    }

                    free(context_bufs);
                    free(contexts);
                    free(line_nums);

                    /* Save matches for highlighting and activate search mode (don't free rows/cols) */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
            }
            break;
        }

        case 'a':
        case 'A':
            rd_toggle_ansi(app->reader);
            app->cfg->viewansi = rd_ansi_visible(app->reader);
#ifdef PLATFORM_AMIGA
            amiga_change_font(rd_ansi_visible(app->reader));
#endif
            /* Reload message to apply/re-apply ANSI parsing with new font */
            load_msg(app, app->cur_msgnum);
            /* Continue to next iteration to redraw with new font */
            continue;

        case 'r':
        case 'R':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_reply(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'e':
        case 'E':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_edit(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'N':
        case KEY_IC: /* New message */
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_new(app);
            return VIEW_EDITOR;

        case '\r':
        case '\n':
        case KEY_ENTER:
            /* Page down or advance to next message at bottom */
            if (rd_top(app->reader) + rd_visible(app->reader) >= rd_total(app->reader))
            {
                if (s->msg_sel < s->order_count)
                {
                    s->msg_sel++;
                    app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load next");
                }
                else
                {
                    /* At last message: try to go to next area */
                    try_goto_next_area(app);
                }
            }
            else
                rd_page_down(app->reader);
            break;

        case CTRL('G'): /* Goto line number */
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Line number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long line_num = 0;
                int target_line;

                if (u)
                {
                    line_num = strtol(u, NULL, 10);
                    free(u);
                }

                target_line = (int)(line_num - 1); /* Convert to 0-based */

                if (target_line >= 0 && target_line < rd_total(app->reader))
                {
                    rd_set_page(app->reader, LINES - 8);

                    /* Scroll to target line */
                    if (target_line < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - target_line);
                    else if (target_line >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, target_line - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Jumped to line %ld", line_num);
                }
                else
                {
                    ui_status(app, "Invalid line number");
                }
            }
            break;
        }

        case KEY_ALT('G'): /* Clear search highlights */
        {
            reader_clear_search(app);
            ui_status(app, "Search highlights cleared");

            continue; /* Force redraw */
        }

        /* Navigate to previous match in reader */
        case KEY_F(3):
        case KEY_ALT('P'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match - 1 + app->reader_search.match_count) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        /* Navigate to next match in reader */
        case KEY_F(4):
        case KEY_ALT('N'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match + 1) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        case 'g': /* Goto message number */
        case 'G':
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Message number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long mn = 0;

                if (u)
                {
                    mn = strtol(u, NULL, 10);
                    free(u);
                }

                int i, found = -1;

                for (i = 0; i < s->order_count; i++)
                {
                    if ((long)s->msgs[s->order[i]].msgnum == mn)
                    {
                        found = i;
                        break;
                    }
                }

                if (found >= 0)
                {
                    s->msg_sel = found + 1;
                    app->cur_msgnum = (uint32_t)mn;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load message");
                }
                else
                    ui_status(app, "No such message in this view");
            }

            break;
        }

        case 'w': /* Write message to text file */
        case 'W':
        case KEY_F(7):
        {
            char path[256];
            char out_cs[CHARSET_NAME_MAX];
            UiSession *s;
            char *body_utf8;
            int written;

            path[0] = '\0';
            out_cs[0] = '\0';

            snprintf(path, sizeof(path), "msg_%u.txt", (unsigned)app->cur_msgnum);

            /*if (ui_popup_input("Write message", "File path:", path, sizeof(path)) != 0 || !path[0])
                break;
            */

            if (ui_files_save("Write message", NULL, path, path, sizeof(path)) != 0)
                break;

            /* Charset of the output file. Empty == AUTO == write UTF-8
             * verbatim; the user can type "CP437", "LATIN-1", etc */
            if (ui_popup_charset("Output charset", "", out_cs, sizeof(out_cs)) != 0)
                break;

            s = &app->sess;
            body_utf8 = wrapper_read_utf8_ex(&s->jam, app->cur_msgnum, app->view_charset[0] ? app->view_charset : NULL, NULL, app->msg_charset, sizeof(app->msg_charset));

            if (!body_utf8)
            {
                ui_status(app, "Cannot read message body");
                break;
            }

            written = rd_export_to_file(app->reader, body_utf8, path, out_cs[0] ? out_cs : NULL);
            free(body_utf8);

            if (written < 0)
                ui_status(app, "Cannot write %s", path);
            else
                ui_status(app, "Wrote %d lines to %s (%s)", written, path, out_cs[0] ? out_cs : "UTF-8");

            break;
        }
        case 'd':
        case 'D':
        case KEY_DC: /* Delete message */
            if (ui_popup_confirm("Delete", "Mark this message as deleted?") == 1)
            {
                if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
                {
                    if (jam_delete_msg(&s->jam, app->cur_msgnum) == 0)
                        ui_status(app, "Message deleted");

                    jam_unlock(&s->jam);
                    free(s->msgs);

                    s->msgs = NULL;
                    s->msgs = jam_load_headers(&s->jam, &s->msg_count, 0, (uint32_t)app->cfg->msglistmax);

                    if (!s->msgs)
                    {
                        s->msg_count = 0;
                        ui_status(app, "Reload failed after delete");
                    }
                    else
                    {
                        ui_session_rebuild_order(app);
                    }
                }
            }

            reader_save_lastread(app);

            /* reader_save_lastread() already updates area counts; skip redundant call */
            app->cfg->viewansi = saved_viewansi;

            return reader_exit_view(app);

            break;

        case KEY_F(1):
        case '?':
            ui_popup_help("Reader Help", READER_HELP, READER_HELP_N);
            break;

        case 'l':
        case 'L':
            /* Show msglist overlay; from search go to search browser instead */
            if (app->from_search)
            {
                reader_clear_search(app);
                app->cfg->viewansi = saved_viewansi;
                return VIEW_SEARCH_RESULTS;
            }

            reader_clear_search(app);
            reader_save_lastread(app);
            update_area_counts_in_memory(app);

            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 1;

            return VIEW_MSGLIST;

        case 27:
            /* ESC: exit search mode first, otherwise quit */
            if (app->reader_search.only_mode)
            {
                reader_clear_search(app);
                ui_status(app, "Search mode exited");

                continue; /* Force redraw */
            }
            /* Fall through to quit */
        case 'q':
        case 'Q':
            reader_save_lastread(app);
            update_area_counts_in_memory(app);
            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 0;

            reader_clear_search(app);

            return reader_exit_view(app);

        case KEY_RESIZE:
            flushinp();
            load_msg(app, app->cur_msgnum);
            break;
        case CTRL('F'):
            ui_popup_freq(app);
            break;
        default:
            break;
        }
    }
}

/*
 * crashedit - Message area editor for AmigaOS
 *
 * This file is part of the crashedit project.
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This program uses JAMLIB, which is licensed under the GNU Lesser
 * General Public License v2.1. See src/jamlib/LICENSE for details.
 */

/* ui_reader.c -- Message reader view */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"
#include "ui_attr.h"
#include "ui_files.h"
#include "../../src/jamlib/jam.h"
#include "../core/msghdr.h"
#include "../core/ftn.h"
#include "../core/ansi.h"
#include "../core/keys.h"

static const char *READER_HELP[] =
    {
        "Message Reader - Key Bindings:",
        "",
        "  Up/Down,j/k    Scroll body up/down",
#ifdef PLATFORM_AMIGA
        "  Ctrl+U/D       Page scroll",
        "  Ctrl+B/E       First / last line",
#else
        "  PgUp/PgDn      Page scroll",
        "  Home/End       First / last line",
#endif
        "  Left/Right,p/n Previous / next message",
        "",
        "  b/Space        Page up/down",
        "  Enter          Page down or advance to next message",
        "  < >            First / last line (scroll)",
        "  , .            First / last message",
        "  Ctrl-J         Jump to original (follow reply chain)",
        "  Ctrl+Left      Jump to original (follow reply chain)",
        "  Ctrl+Right     Jump to reply (pick if multiple)",
        "",
        "  k, v           Toggle kludges (^A, SEEN-BY, Via)",
        "  a              Toggle ANSI color rendering",
        "  c              Change DISPLAY charset (re-render)",
        "  r              Reply to this message",
        "  e              Edit this message",
        "  n, Ins         New message (in current area)",
        "  l, L           Show message list overlay (ESC returns here)",
        "  g              Goto message number",
        "  Ctrl+H         Scroll to top of message",
        "  Ctrl+K         Scroll to bottom of message",
        "  Ctrl+G         Goto line number",
        "  Alt+J          Follow reply chain to original",
        "  Alt+G          Clear search highlights",
        "  F5, /          Search in message body",
        "  F3 Alt+P       Previous match (search mode)",
        "  F4 Alt+N       Next match (search mode)",
        "  w, F7          Write message to text file",
        "  d, Del         Delete this message",
        "  Ctrl+F         File request",
        "",
        "  ESC, q         Back to message list",
        "  F1, ?          This help"};
#define READER_HELP_N ((int)(sizeof(READER_HELP) / sizeof(READER_HELP[0])))

void ui_editor_prep_new(UiApp *app);
void ui_editor_prep_reply(UiApp *app, uint32_t orig_msgnum);
void ui_editor_prep_edit(UiApp *app, uint32_t msgnum);

/* Update area's cached counts after reader moves lastread/lastseen */
static void update_area_counts_in_memory(UiApp *app)
{
    UiSession *s;
    AreaEntry *ae;
    int i, unread, new_count;

    if (!app)
        return;

    s = &app->sess;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return;

    ae = &app->areas->entries[s->area_idx];

    ae->lastread = s->lastread;
    ae->lastseen = s->lastseen;

    /* Recompute unread (above lastread) and new_count (above lastseen) */
    unread = 0;
    new_count = 0;

    for (i = 0; i < s->msg_count; i++)
    {
        if (s->msgs[i].msgnum > s->lastread)
            unread++;

        if (s->msgs[i].msgnum > s->lastseen)
            new_count++;
    }

    ae->unread = unread;
    ae->new_count = new_count;
    ae->total_msgs = s->msg_count;
}

/* Update lastread and persist to JAM; skip if from search (browsing shouldn't mutate) */
static void reader_save_lastread(UiApp *app)
{
    UiSession *s;
    uint32_t msgnum;

    if (!app)
        return;

    if (app->from_search)
        return;

    s = &app->sess;
    msgnum = app->cur_msgnum;

    s->lastread = msgnum;

    if (msgnum > s->lastseen)
        s->lastseen = msgnum;

    if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
    {
        jam_write_lastread(&s->jam, s->user_crc, s->lastread, s->lastseen);
        jam_unlock(&s->jam);
    }

    update_area_counts_in_memory(app);
}

/* Exit target: msglist (normal) or search results (if from search) */
static UiView reader_exit_view(const UiApp *app)
{
    return app->from_search ? VIEW_SEARCH_RESULTS : VIEW_MSGLIST;
}

/* Strip FTS-1 kludge lines and SEEN-BY/PATH trailers for ANSI rendering
 * Returns malloc'd buffer (caller frees), NULL on failure */
static char *strip_kludges_for_ansi(const char *raw, int raw_len, int *out_len)
{
    char *out;
    int i = 0;
    int j = 0;
    int at_line_start = 1;

    if (out_len)
        *out_len = 0;

    if (!raw || raw_len <= 0)
        return NULL;

    out = (char *)malloc((size_t)raw_len + 1);
    if (!out)
        return NULL;

    while (i < raw_len)
    {
        if (at_line_start)
        {
            const char *p = raw + i;
            int rem = raw_len - i;
            int is_kludge = 0;

            if (rem >= 1 && (unsigned char)*p == 0x01)
                is_kludge = 1;
            else if (rem >= 8 && memcmp(p, "SEEN-BY:", 8) == 0)
                is_kludge = 1;
            else if (rem >= 5 && memcmp(p, "PATH:", 5) == 0)
                is_kludge = 1;

            if (is_kludge)
            {
                /* Skip to end of this line (eat optional \r and \n) */
                while (i < raw_len && raw[i] != '\r' && raw[i] != '\n')
                    i++;

                if (i < raw_len && raw[i] == '\r')
                    i++;
                if (i < raw_len && raw[i] == '\n')
                    i++;

                /* Still at line start for the next iteration */
                continue;
            }

            at_line_start = 0;
        }

        out[j++] = raw[i];

        if (raw[i] == '\r' || raw[i] == '\n')
            at_line_start = 1;

        i++;
    }

    out[j] = '\0';

    if (out_len)
        *out_len = j;

    return out;
}

/* Load current message into reader */
static int load_msg(UiApp *app, uint32_t msgnum)
{
    UiSession *s;
    int idx;
    char *body_utf8 = NULL;
    int wrap_w;
    const wchar_t *cur;
    char *cur_utf8;
    int needs_fallback;
    int ansi_on_outer = 0;
    char detected[CHARSET_NAME_MAX];
    const char *override_enc;
    char chosen[CHARSET_NAME_MAX];
    int ansi_on;
    uint32_t raw_len = 0;
    char *raw_bytes = NULL;
    int has_chrs = 0;
    const char *p;
    int oi;

    if (!app)
        return -1;

    s = &app->sess;

    idx = jam_find_by_msgnum(s->msgs, s->msg_count, msgnum);

    if (idx < 0)
        return -1;

    /* Sync msg_sel to current message for correct navigation after search/jump entry */
    for (oi = 0; oi < s->order_count; oi++)
    {
        if (s->order[oi] == idx)
        {
            s->msg_sel = oi + 1;
            break;
        }
    }

    /* Fill header */
    msghdr_load(app->hdr, &s->msgs[idx], app->areas->entries[s->area_idx].name, idx + 1, s->msg_count, ftn_effective_tz_offset(app->cfg->timezone_offset, app->cfg->timezone_is_manual));

    /* Body: ANSI->canvas, text->UTF-8. Charset: CHRS if present, else default */
    override_enc = app->view_charset[0] ? app->view_charset : NULL;

    detected[0] = '\0';
    chosen[0] = '\0';

    /* Read and decode body using wrapper_read_utf8_ex with proper fallback logic */
    body_utf8 = wrapper_read_utf8_ex(&s->jam, msgnum, override_enc, NULL, detected, sizeof(detected));

    if (!body_utf8)
        return -1;

    /* Read raw body for ANSI mode (needs original bytes, not UTF-8) */
    raw_bytes = jam_read_body(&s->jam, msgnum, &raw_len);

    if (!raw_bytes)
    {
        free(body_utf8);
        return -1;
    }

    /* Set charset variables based on wrapper_read_utf8_ex results */
    strncpy(chosen, detected, sizeof(chosen) - 1);
    chosen[sizeof(chosen) - 1] = '\0';

    /* Check if we actually have a CHRS kludge */
    has_chrs = (detected[0] != '\0');

    if (has_chrs)
    {
        strncpy(app->msg_charset, detected, sizeof(app->msg_charset) - 1);
        app->msg_charset[sizeof(app->msg_charset) - 1] = '\0';
    }
    else
    {
        app->msg_charset[0] = '\0';
    }

    /* ANSI mode: force CP437 if UTF-8 came from default/fallback (not CHRS/override)
     * Exception: when TTF is active, keep detected charset to preserve Unicode characters */
    ansi_on = (app->cfg && app->cfg->viewansi);
    ansi_on_outer = ansi_on;

    if (ansi_on && (strcasecmp(chosen, "UTF-8") == 0 || strcasecmp(chosen, "UTF8") == 0))
    {
        int user_wants_utf8 = (override_enc && (strcasecmp(override_enc, "UTF-8") == 0 || strcasecmp(override_enc, "UTF8") == 0));
        int chrs_says_utf8 = (app->msg_charset[0] && (strcasecmp(app->msg_charset, "UTF-8") == 0 || strcasecmp(app->msg_charset, "UTF8") == 0));

        if (!user_wants_utf8 && !chrs_says_utf8)
        {
            strncpy(chosen, "CP437", sizeof(chosen) - 1);
            chosen[sizeof(chosen) - 1] = '\0';
        }
    }

    /* Record actual decode charset for status bar (may differ from view_charset) */
    if (override_enc && override_enc[0])
        strncpy(app->decoded_charset, override_enc, sizeof(app->decoded_charset) - 1);
    else
        strncpy(app->decoded_charset, chosen, sizeof(app->decoded_charset) - 1);

    app->decoded_charset[sizeof(app->decoded_charset) - 1] = '\0';

    /*wrap_w = COLS - 2;*/
    wrap_w = COLS;

    if (wrap_w < 20)
        wrap_w = 20;

    if (ansi_on)
    {
        /* Strip kludge/SEEN-BY/PATH lines. Fall back to raw bytes on failure */
        int stripped_len = 0;
        char *stripped = strip_kludges_for_ansi(raw_bytes, (int)raw_len, &stripped_len);

        if (stripped)
        {
            rd_load_ansi(app->reader, stripped, stripped_len, chosen, wrap_w);
            free(stripped);
        }
        else
        {
            rd_load_ansi(app->reader, raw_bytes, (int)raw_len, chosen, wrap_w);
        }

        /* Free unused UTF-8 buffer; ANSI mode uses raw bytes. Prevents leak */
        free(body_utf8);
        body_utf8 = NULL; /* not used in this path */
    }

    /* ANSI mode: convert kludges/SEEN-BY/Origin to UTF-8 for MSGID/origin lookup */
    if (ansi_on)
    {
        int srclen = (int)raw_len;
        int dstmax = srclen * 4 + 256;

        body_utf8 = (char *)malloc((size_t)dstmax);

        if (body_utf8)
            charset_body_to_utf8(chosen, raw_bytes, srclen, body_utf8, dstmax);
    }

    free(raw_bytes);

    if (!body_utf8)
        return -1;

    /* Extract MSGID / REPLY for header display */
    app->cur_msgid[0] = '\0';
    app->cur_reply[0] = '\0';

    ftn_get_kludge_value(body_utf8, "MSGID", app->cur_msgid, sizeof(app->cur_msgid));
    ftn_get_kludge_value(body_utf8, "REPLY", app->cur_reply, sizeof(app->cur_reply));

    /* Get sender address from MSGID kludge, fallback to Origin line */

    cur = msghdr_get(app->hdr, HDR_OADDR);
    cur_utf8 = cur ? wcs_to_utf8(cur, (int)wcslen(cur)) : NULL;

    if (cur && !cur_utf8)
    {
        free(body_utf8); /* would otherwise leak on this OOM path */
        return -1;
    }

    needs_fallback = (!cur_utf8 || !cur_utf8[0] || strcmp(cur_utf8, "0:0/0") == 0);

    if (needs_fallback)
    {
        char addr[64];
        int got = -1;

        if (app->cur_msgid[0])
        {
            /* Extract address from MSGID (format: "addr serial") */
            int i;

            addr[0] = '\0';

            for (i = 0; app->cur_msgid[i] && app->cur_msgid[i] != ' ' && app->cur_msgid[i] != '\t' && i < (int)sizeof(addr) - 1; i++)
                addr[i] = app->cur_msgid[i];

            addr[i] = '\0';

            if (addr[0])
                got = 0;
        }

        if (got != 0)
            got = ftn_find_origin_address(body_utf8, addr, sizeof(addr));

        if (got == 0)
            msghdr_set_utf8(app->hdr, HDR_OADDR, addr);
    }

    free(cur_utf8);

    wrap_w = COLS; /* full width, match editor soft-wrap */

    if (wrap_w < 20)
        wrap_w = 20;

    /* ANSI: canvas loaded above, skip rd_load. Non-ANSI: rd_load handles wrap */
    if (!ansi_on_outer)
        rd_load(app->reader, body_utf8, wrap_w);

    free(body_utf8);

    rd_set_page(app->reader, LINES - 8);

    /* Apply view-config toggles (kludges visibility) */
    if (app->cfg->viewkludge != rd_kludges_visible(app->reader))
        rd_toggle_kludges(app->reader);

    return 0;
}

/* Try to go to next area in area_order. Returns 1 on success, 0 otherwise */
static int try_goto_next_area(UiApp *app)
{
    UiSession *s;
    int current_area_idx;
    int current_order_idx = -1;
    int next_area_idx = -1;
    int i;
    AreaEntry *next_ae;
    char prompt[256];

    if (!app)
        return 0;

    s = &app->sess;

    if (app->area_order_count <= 0)
        return 0;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return 0;

    current_area_idx = s->area_idx;

    /* Find current area in area_order */
    for (i = 0; i < app->area_order_count; i++)
    {
        if (app->area_order[i] == current_area_idx)
        {
            current_order_idx = i;
            break;
        }
    }

    /* Find next area in order */
    if (current_order_idx >= 0 && current_order_idx < app->area_order_count - 1)
    {
        next_area_idx = app->area_order[current_order_idx + 1];
    }

    if (next_area_idx < 0)
        return 0;

    next_ae = &app->areas->entries[next_area_idx];
    snprintf(prompt, sizeof(prompt), "Go to next area '%s'?", next_ae->name ? next_ae->name : "(area)");

    if (ui_popup_confirm("Next area", prompt) != 1)
        return 0;

    /* Save lastread before changing area */
    reader_save_lastread(app);

    /* Close current session and open next area */
    ui_session_close(app);

    if (ui_session_open(app, next_area_idx) != 0)
    {
        ui_status(app, "Cannot open area");
        return 0;
    }

    /* Load message from new area */
    if (s->order_count > 0)
    {
        /* Check if current position is on an unread message */
        uint32_t current_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        /* If not on unread message, go to last message in list */
        if (current_msgnum <= s->lastread)
            s->msg_sel = s->order_count;

        app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        if (load_msg(app, app->cur_msgnum) != 0)
        {
            ui_status(app, "Cannot load message");
            return 0;
        }
    }

    return 1;
}

/* Header bar */
static void draw_header_bar(UiApp *app)
{
    int i;
    const wchar_t *from, *to, *subj, *date;
    char attr_str[40];
    static const char SPACES[256] =
        "                                                                "
        "                                                                "
        "                                                                "
        "                                                                ";

    from = msghdr_get(app->hdr, HDR_FROM);
    to = msghdr_get(app->hdr, HDR_TO);
    subj = msghdr_get(app->hdr, HDR_SUBJECT);
    date = msghdr_get(app->hdr, HDR_DATE);
    ui_attr_build(msghdr_attr(app->hdr), attr_str, sizeof(attr_str));

    attron(COLOR_PAIR(COL_HEADER));

    for (i = 1; i < 7; i++)
    {
        int written = 0;
        move(i, 0);
        clrtoeol();

        while (written < COLS)
        {
            int chunk = COLS - written;

            if (chunk > (int)sizeof(SPACES))
                chunk = (int)sizeof(SPACES);

            mvaddnstr(i, written, SPACES, chunk);
            written += chunk;
        }
    }

    mvprintw(1, 1, "Area: %-30.30s  Msg %d/%d", ui_wcs2u8(msghdr_get(app->hdr, HDR_AREA)), msghdr_msgnum(app->hdr), msghdr_msgtotal(app->hdr));
    mvprintw(2, 1, "From: %-25.25s  %-20.20s  %s", ui_wcs2u8(from), ui_wcs2u8(msghdr_get(app->hdr, HDR_OADDR)), ui_wcs2u8(date));
    mvprintw(3, 1, "  To: %-25.25s  %s", ui_wcs2u8(to), ui_wcs2u8(msghdr_get(app->hdr, HDR_DADDR)));
    mvprintw(4, 1, "Subj: %s", ui_wcs2u8(subj));

    /* Attr on left, MSGID on right */
    mvprintw(5, 1, "Attr: %s", attr_str);

    if (app->cur_msgid[0])
    {
        int show = (int)strlen(app->cur_msgid);
        int max_room = COLS - 22;

        if (max_room < 10)
            max_room = 10;

        if (show > max_room)
            show = max_room;

        mvprintw(5, COLS - show - 8, "MSGID: %.*s", show, app->cur_msgid);
    }

    attroff(COLOR_PAIR(COL_HEADER));

    attron(COLOR_PAIR(COL_BORDER));
    ui_hline(6, 0, COLS);
    attroff(COLOR_PAIR(COL_BORDER));
}

/* Body rendering */
/* Convert one wchar_t to UTF-8 in buf. Returns bytes written */
static int wc_to_utf8_bytes(wchar_t wc, char *buf)
{
    unsigned long cp = (unsigned long)wc;

    if (cp < 0x80)
    {
        buf[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));

        return 2;
    }

    if (cp < 0x10000)
    {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));

        return 3;
    }

    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));

    return 4;
}

/* Draw ANSI line: collect runs of same attrs and emit each run in one call */
static void draw_ansi_line(int row_y, const wchar_t *wcs, int len, const struct AnsiCell *cells, int max_cols)
{
    char run_buf[1024];
    int i = 0;
    int x = 0;

    while (i < len && x < max_cols)
    {
        int run_color = cells[i].color_pair;
        int run_attrs = cells[i].attrs;
        int run_start = i;
        int run_chars = 0;
        int run_bytes = 0;

        /* Find extent of run with identical (color, attrs) */
        while (i < len && x + run_chars < max_cols && cells[i].color_pair == run_color && cells[i].attrs == run_attrs)
        {
            int n;

            if (run_bytes + 4 >= (int)sizeof(run_buf))
                break; /* Don't overflow run buffer; emit and continue */

            n = wc_to_utf8_bytes(wcs[i], &run_buf[run_bytes]);
            run_bytes += n;
            run_chars++;
            i++;
        }

        if (run_chars == 0)
            break; /* safety */

        run_buf[run_bytes] = '\0';

        if (run_color >= 0)
            attron(COLOR_PAIR(run_color) | run_attrs);

        mvaddnstr(row_y, x, run_buf, run_bytes);

        if (run_color >= 0)
            attroff(COLOR_PAIR(run_color) | run_attrs);

        x += run_chars;
    }
}

static void draw_body(UiApp *app)
{
    int rows = LINES - 8;
    int i;
    const wchar_t *wcs;
    int wlen;

    if (rows < 1)
        return;

    rd_set_page(app->reader, rows);

    for (i = 0; i < rows; i++)
    {
        int vi = rd_top(app->reader) + i;
        int type, col;
        int ansi_color, ansi_attrs;
        const struct AnsiCell *cells;
        int row_y = 7 + i;
        const wchar_t *ln;
        int lnlen;

        /* Skip if terminal shrunk since rd_set_page */
        if (row_y < 0 || row_y >= LINES - 1)
            continue;

        move(row_y, 0);
        clrtoeol();

        if (vi >= rd_total(app->reader))
            continue;

        type = rd_get_type(app->reader, vi);
        ansi_color = rd_get_ansi_color(app->reader, vi);
        ansi_attrs = rd_get_ansi_attrs(app->reader, vi);
        cells = rd_get_ansi_cells(app->reader, vi);

        /* ANSI with per-cell attrs: draw via canvas (preserves BBS art colors) */
        if (rd_ansi_visible(app->reader) && cells)
        {
            ln = rd_get_line(app->reader, vi);
            lnlen = rd_get_len(app->reader, vi);

            if (ln && lnlen > 0)
                draw_ansi_line(row_y, ln, lnlen, cells, (COLS < 80) ? COLS : 80);

            continue; /* attrs handled inside draw_ansi_line */
        }

        if (rd_ansi_visible(app->reader) && ansi_color >= 0)
        {
            col = ansi_color;
            attron(COLOR_PAIR(col) | ansi_attrs);
        }
        else
        {
            col = ui_color_for_type(type);
            attron(COLOR_PAIR(col));
            ansi_attrs = 0;
        }

        /* Plain or ANSI without per-cell attrs: draw with line color */
        ln = rd_get_line(app->reader, vi);
        lnlen = rd_get_len(app->reader, vi);

        /* Skip leading ^A on kludge lines */
        if (ln && lnlen > 0 && (type & FTN_LT_KLUDGE) && ln[0] == L'\x01')
        {
            ln++;
            lnlen--;
        }

        if (ln && lnlen > 0)
            mvaddnwstr(row_y, 0, ln, lnlen);

        /* Highlight search matches (word-level) */
        if (app->reader_search.rows && app->reader_search.cols && app->reader_search.match_count > 0)
        {
            int line_idx = rd_get_line_idx(app->reader, vi);
            int j;

            if (line_idx >= 0)
            {
                for (j = 0; j < app->reader_search.match_count; j++)
                {
                    if (app->reader_search.rows[j] == line_idx)
                    {
                        int match_col = app->reader_search.cols[j];
                        int match_len = (int)wcslen(app->reader_search.query);

                        if (match_col >= 0 && match_col + match_len <= lnlen)
                        {
                            if (j == app->reader_search.current_match)
                                attron(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attron(COLOR_PAIR(COL_SEARCH_MATCH));

                            mvaddnwstr(row_y, match_col, &ln[match_col], match_len);

                            if (j == app->reader_search.current_match)
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
            }
        }

        attroff(COLOR_PAIR(col) | ansi_attrs);
    }
}

/* Clear search state in reader */
static void reader_clear_search(UiApp *app)
{
    if (app->reader_search.rows)
    {
        free(app->reader_search.rows);
        app->reader_search.rows = NULL;
    }

    if (app->reader_search.cols)
    {
        free(app->reader_search.cols);
        app->reader_search.cols = NULL;
    }

    app->reader_search.match_count = 0;
    app->reader_search.is_mode = 0;
    app->reader_search.only_mode = 0;
    app->reader_search.current_match = 0;
    app->reader_search.match_current = 0;
}

/* Navigate to a raw msgs[] index, honouring the current sort order */
static int reader_goto_raw(UiApp *app, int raw_idx)
{
    UiSession *s = &app->sess;
    uint32_t target_mn;
    int i;

    if (raw_idx < 0 || raw_idx >= s->msg_count)
        return -1; /* invalid index */

    target_mn = s->msgs[raw_idx].msgnum;

    for (i = 0; i < s->order_count; i++)
    {
        if (s->msgs[s->order[i]].msgnum == target_mn)
        {
            s->msg_sel = i + 1;
            app->cur_msgnum = target_mn;

            return load_msg(app, target_mn);
        }
    }

    return -2;
}

/* Main loop */
UiView ui_reader_run(UiApp *app)
{
    int ch;
    UiSession *s;
    int saved_viewansi;

    if (!app)
        return VIEW_QUIT;

    s = &app->sess;
    saved_viewansi = app->cfg->viewansi;

    if (load_msg(app, app->cur_msgnum) != 0)
    {
        ui_status(app, "Cannot load message");
        app->cfg->viewansi = saved_viewansi;
        return reader_exit_view(app);
    }

    /* Apply ANSI visibility from config on first load */
    if (app->cfg->viewansi != rd_ansi_visible(app->reader))
    {
        rd_toggle_ansi(app->reader);

#ifdef PLATFORM_AMIGA
        amiga_change_font(rd_ansi_visible(app->reader));
#endif
    }

    for (;;)
    {
        erase();

        ui_draw_menubar(app, "Reader");
        draw_header_bar(app);
        draw_body(app);

        /* Status: msg_charset (CHRS or ?) -> decoded_charset (actual decode) */
        ui_status(app, "Charset: %s -> %s | c = Change charset%s | Ln %d/%d",
                  app->msg_charset[0] ? app->msg_charset : "?",
                  (app->view_charset[0]) ? app->decoded_charset : "Auto",
                  rd_ansi_visible(app->reader) ? " | ANSI" : "",
                  rd_top(app->reader) + 1, rd_total(app->reader));

        ui_draw_statusbar(app);
        refresh();

        ch = wrapper_getch();

        switch (ch)
        {
        case KEY_UP:
            rd_scroll_up(app->reader, 1);
            break;
        case KEY_DOWN:
            rd_scroll_down(app->reader, 1);
            break;
        case KEY_PPAGE:
        case 'b':
        case 'B':
        case CTRL('U'): /* Page Up (Amiga) */
            rd_page_up(app->reader);
            break;
        case KEY_NPAGE:
        case CTRL('D'): /* Page Down (Amiga) */
        case ' ':
            rd_page_down(app->reader);
            break;
        case KEY_HOME:
        case CTRL('B'): /* Home (Amiga) */
            rd_home(app->reader);
            break;
        case KEY_END:
        case CTRL('E'): /* End (Amiga) */
            rd_end(app->reader);
            break;

        case KEY_RIGHT:
        case 'n': /* Next message */
            if (s->msg_sel < s->order_count)
            {
                s->msg_sel++;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load next");
            }
            else
            {
                /* At last message: try to go to next area */
                try_goto_next_area(app);
            }
            break;
        case KEY_LEFT:
        case 'p': /* Previous message */
            if (s->msg_sel > 1)
            {
                s->msg_sel--;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load prev");
            }
            break;

        case CTRL('H'):
        case '<':
            /* Scroll to top of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) > 0)
                rd_scroll_up(app->reader, 1);

            break;

        case CTRL('K'):
        case '>':
            /* Scroll to bottom of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) + rd_visible(app->reader) < rd_total(app->reader))
                rd_scroll_down(app->reader, 1);

            break;

        case ',':
            /* First message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = 1;
                app->cur_msgnum = s->msgs[s->order[0]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load first");
            }
            break;

        case '.':
            /* Last message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = s->order_count;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load last");
            }
            break;
        case KEY_ALT('J'): /* Follow REPLY chain to original */
        case KEY_CLEFT:
        {
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int orig = (idx >= 0) ? ftn_find_original(s->msgs, s->msg_count, idx) : -1;

            /* Fallback: search by MSGID string from REPLY kludge */
            if (orig < 0 && app->cur_reply[0])
                orig = ftn_find_original_by_msgid(s->msgs, s->msg_count, app->cur_reply);

            if (orig < 0)
                ui_status(app, "No original message in this area");
            else if (orig >= s->msg_count)
                ui_status(app, "Invalid original message index");
            else if (s->msgs[orig].from[0] == '\0' || s->msgs[orig].subject[0] == '\0')
                ui_status(app, "Original message is empty or corrupted");
            else
            {
                int rc = reader_goto_raw(app, orig);

                if (rc == -2)
                    ui_status(app, "Original not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load original");
            }

            break;
        }
        case KEY_CRIGHT: /* Follow thread to reply (with picker if branched) */
        {
#define MAX_REPLIES 256
            int raw_replies[MAX_REPLIES];
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int nreplies = (idx >= 0) ? ftn_find_all_replies(s->msgs, s->msg_count, idx, raw_replies, MAX_REPLIES) : 0;

            if (nreplies == 0)
            {
                ui_status(app, "No replies to this message");
            }
            else if (nreplies == 1)
            {
                int rc = reader_goto_raw(app, raw_replies[0]);
                if (rc == -2)
                    ui_status(app, "Reply not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load reply");
            }
            else
            {
                /* Multiple replies: build label list and show picker */
                const char *items[MAX_REPLIES];
                char labels[MAX_REPLIES][80];
                int i, choice;

                for (i = 0; i < nreplies; i++)
                {
                    const JamMsgInfo *m = &s->msgs[raw_replies[i]];

                    snprintf(labels[i], sizeof(labels[i]), "#%-5u  %-20.20s  %.28s", (unsigned)m->msgnum, m->from, m->subject);
                    items[i] = labels[i];
                }

                choice = ui_popup_list("Select reply", items, nreplies, 0);

                if (choice >= 0)
                {
                    int rc = reader_goto_raw(app, raw_replies[choice]);

                    if (rc == -2)
                        ui_status(app, "Reply not visible in current filter");
                    else if (rc != 0)
                        ui_status(app, "Cannot load reply");
                }
            }
#undef MAX_REPLIES
            break;
        }

        case 'c':
        case 'C':
        {
            char new_view[32], new_out[32];

            new_view[0] = '\0';
            new_out[0] = '\0';

            if (ui_popup_charset_pair(app->view_charset, app->edit_charset, CHARSET_READ_DEFAULT, app->cfg->charset, new_view, sizeof(new_view), new_out, sizeof(new_out)) == 0)
            {
                /* Empty string = Auto (CHARSET_READ_DEFAULT); load_msg treats "" as no override */
                strncpy(app->view_charset, new_view, sizeof(app->view_charset) - 1);
                app->view_charset[sizeof(app->view_charset) - 1] = '\0';

                strncpy(app->edit_charset, new_out, sizeof(app->edit_charset) - 1);
                app->edit_charset[sizeof(app->edit_charset) - 1] = '\0';

                /* Mark that user manually changed charset (from reader) - only if NOT Auto */
                if (new_out[0] != '\0')
                    app->edit_charset_manually_changed = 1;

                /* Reload with new charset; preserves headers, lastread, ANSI, kludges */
                load_msg(app, app->cur_msgnum);

                ui_status(app, "View: %s | Save: %s", new_view[0] ? new_view : CHARSET_READ_DEFAULT, new_out[0] ? new_out : (app->cfg->charset[0] ? app->cfg->charset : "UTF-8"));
            }

            break;
        }

        case 'k':
        case 'K':
            rd_toggle_kludges(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case 'v':
        case 'V':
            rd_toggle_hiddklud(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Hidden+Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case KEY_F(5): /* Search in message body */
        case '/':
        {
            wchar_t tmp[64];
            int *rows = NULL, *cols = NULL;
            int match_count;
            int i;
            const char **contexts = NULL;
            char **context_bufs = NULL;
            int *line_nums = NULL;

            wcsncpy(tmp, app->reader_search.query, 63);
            tmp[63] = L'\0';

            if (ui_popup_input("Search", "Text:", tmp, 64) == 0 && tmp[0])
            {
                wcsncpy(app->reader_search.query, tmp, 63);
                app->reader_search.query[63] = L'\0';

                /* Find all matches in all lines (no limit) */
                match_count = rd_search_all(app->reader, app->reader_search.query, &rows, &cols);

                /* Free previous search matches */
                reader_clear_search(app);

                if (match_count == 0)
                {
                    ui_status(app, "Not found");
                }
                else if (match_count == 1)
                {
                    /* Single match: scroll to it */
                    rd_set_page(app->reader, LINES - 8);

                    if (rows[0] < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - rows[0]);
                    else if (rows[0] >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, rows[0] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Found at line %d", rows[0] + 1);

                    /* Save matches for highlighting and activate search mode */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
                else
                {
                    /* Allocate arrays for display */
                    contexts = (const char **)malloc((size_t)match_count * sizeof(const char *));
                    context_bufs = (char **)malloc((size_t)match_count * sizeof(char *));
                    line_nums = (int *)malloc((size_t)match_count * sizeof(int));

                    if (!contexts || !context_bufs || !line_nums)
                    {
                        if (contexts)
                            free(contexts);

                        if (context_bufs)
                            free(context_bufs);

                        if (line_nums)
                            free(line_nums);

                        free(rows);
                        free(cols);
                        ui_status(app, "Memory error");

                        break;
                    }

                    /* Multiple matches: show list */
                    for (i = 0; i < match_count; i++)
                    {
                        const wchar_t *line = rd_get_line(app->reader, rows[i]);
                        int line_len = rd_get_len(app->reader, rows[i]);

                        context_bufs[i] = (char *)malloc(128);

                        if (!context_bufs[i])
                        {
                            context_bufs[i] = NULL;
                            contexts[i] = "";
                        }
                        else
                        {
                            /* Convert context to UTF-8 for display (max 60 chars) */
                            if (line && line_len > 0)
                            {
                                int copy_len = line_len;

                                if (copy_len > 60)
                                    copy_len = 60;

                                char *utf8 = wcs_to_utf8(line, copy_len);

                                if (utf8)
                                {
                                    snprintf(context_bufs[i], 128, "%s", utf8);
                                    free(utf8);
                                }
                                else
                                {
                                    context_bufs[i][0] = '\0';
                                }
                            }
                            else
                            {
                                context_bufs[i][0] = '\0';
                            }
                        }

                        contexts[i] = context_bufs[i];
                        line_nums[i] = rows[i] + 1; /* 1-based for display */
                    }

                    /* Show popup with results */
                    int choice = ui_popup_search_results("Search Results", line_nums, contexts, match_count, 0);

                    if (choice >= 0)
                    {
                        rd_set_page(app->reader, LINES - 8);

                        if (rows[choice] < rd_top(app->reader))
                            rd_scroll_up(app->reader, rd_top(app->reader) - rows[choice]);
                        else if (rows[choice] >= rd_top(app->reader) + rd_visible(app->reader))
                            rd_scroll_down(app->reader, rows[choice] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                        app->reader_search.current_match = choice;
                        app->reader_search.match_current = choice + 1;
                        ui_status(app, "Jumped to line %d", rows[choice] + 1);
                    }
                    else
                    {
                        ui_status(app, "Search cancelled");
                    }

                    /* Free allocated memory */
                    for (i = 0; i < match_count; i++)
                    {
                        if (context_bufs[i])
                            free(context_bufs[i]);
                    }

                    free(context_bufs);
                    free(contexts);
                    free(line_nums);

                    /* Save matches for highlighting and activate search mode (don't free rows/cols) */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
            }
            break;
        }

        case 'a':
        case 'A':
            rd_toggle_ansi(app->reader);
            app->cfg->viewansi = rd_ansi_visible(app->reader);
#ifdef PLATFORM_AMIGA
            amiga_change_font(rd_ansi_visible(app->reader));
#endif
            /* Reload message to apply/re-apply ANSI parsing with new font */
            load_msg(app, app->cur_msgnum);
            /* Continue to next iteration to redraw with new font */
            continue;

        case 'r':
        case 'R':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_reply(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'e':
        case 'E':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_edit(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'N':
        case KEY_IC: /* New message */
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_new(app);
            return VIEW_EDITOR;

        case '\r':
        case '\n':
        case KEY_ENTER:
            /* Page down or advance to next message at bottom */
            if (rd_top(app->reader) + rd_visible(app->reader) >= rd_total(app->reader))
            {
                if (s->msg_sel < s->order_count)
                {
                    s->msg_sel++;
                    app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load next");
                }
                else
                {
                    /* At last message: try to go to next area */
                    try_goto_next_area(app);
                }
            }
            else
                rd_page_down(app->reader);
            break;

        case CTRL('G'): /* Goto line number */
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Line number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long line_num = 0;
                int target_line;

                if (u)
                {
                    line_num = strtol(u, NULL, 10);
                    free(u);
                }

                target_line = (int)(line_num - 1); /* Convert to 0-based */

                if (target_line >= 0 && target_line < rd_total(app->reader))
                {
                    rd_set_page(app->reader, LINES - 8);

                    /* Scroll to target line */
                    if (target_line < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - target_line);
                    else if (target_line >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, target_line - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Jumped to line %ld", line_num);
                }
                else
                {
                    ui_status(app, "Invalid line number");
                }
            }
            break;
        }

        case KEY_ALT('G'): /* Clear search highlights */
        {
            reader_clear_search(app);
            ui_status(app, "Search highlights cleared");

            continue; /* Force redraw */
        }

        /* Navigate to previous match in reader */
        case KEY_F(3):
        case KEY_ALT('P'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match - 1 + app->reader_search.match_count) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        /* Navigate to next match in reader */
        case KEY_F(4):
        case KEY_ALT('N'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match + 1) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        case 'g': /* Goto message number */
        case 'G':
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Message number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long mn = 0;

                if (u)
                {
                    mn = strtol(u, NULL, 10);
                    free(u);
                }

                int i, found = -1;

                for (i = 0; i < s->order_count; i++)
                {
                    if ((long)s->msgs[s->order[i]].msgnum == mn)
                    {
                        found = i;
                        break;
                    }
                }

                if (found >= 0)
                {
                    s->msg_sel = found + 1;
                    app->cur_msgnum = (uint32_t)mn;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load message");
                }
                else
                    ui_status(app, "No such message in this view");
            }

            break;
        }

        case 'w': /* Write message to text file */
        case 'W':
        case KEY_F(7):
        {
            char path[256];
            char out_cs[CHARSET_NAME_MAX];
            UiSession *s;
            char *body_utf8;
            int written;

            path[0] = '\0';
            out_cs[0] = '\0';

            snprintf(path, sizeof(path), "msg_%u.txt", (unsigned)app->cur_msgnum);

            /*if (ui_popup_input("Write message", "File path:", path, sizeof(path)) != 0 || !path[0])
                break;
            */

            if (ui_files_save("Write message", NULL, path, path, sizeof(path)) != 0)
                break;

            /* Charset of the output file. Empty == AUTO == write UTF-8
             * verbatim; the user can type "CP437", "LATIN-1", etc */
            if (ui_popup_charset("Output charset", "", out_cs, sizeof(out_cs)) != 0)
                break;

            s = &app->sess;
            body_utf8 = wrapper_read_utf8_ex(&s->jam, app->cur_msgnum, app->view_charset[0] ? app->view_charset : NULL, NULL, app->msg_charset, sizeof(app->msg_charset));

            if (!body_utf8)
            {
                ui_status(app, "Cannot read message body");
                break;
            }

            written = rd_export_to_file(app->reader, body_utf8, path, out_cs[0] ? out_cs : NULL);
            free(body_utf8);

            if (written < 0)
                ui_status(app, "Cannot write %s", path);
            else
                ui_status(app, "Wrote %d lines to %s (%s)", written, path, out_cs[0] ? out_cs : "UTF-8");

            break;
        }
        case 'd':
        case 'D':
        case KEY_DC: /* Delete message */
            if (ui_popup_confirm("Delete", "Mark this message as deleted?") == 1)
            {
                if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
                {
                    if (jam_delete_msg(&s->jam, app->cur_msgnum) == 0)
                        ui_status(app, "Message deleted");

                    jam_unlock(&s->jam);
                    free(s->msgs);

                    s->msgs = NULL;
                    s->msgs = jam_load_headers(&s->jam, &s->msg_count, 0, (uint32_t)app->cfg->msglistmax);

                    if (!s->msgs)
                    {
                        s->msg_count = 0;
                        ui_status(app, "Reload failed after delete");
                    }
                    else
                    {
                        ui_session_rebuild_order(app);
                    }
                }
            }

            reader_save_lastread(app);

            /* reader_save_lastread() already updates area counts; skip redundant call */
            app->cfg->viewansi = saved_viewansi;

            return reader_exit_view(app);

            break;

        case KEY_F(1):
        case '?':
            ui_popup_help("Reader Help", READER_HELP, READER_HELP_N);
            break;

        case 'l':
        case 'L':
            /* Show msglist overlay; from search go to search browser instead */
            if (app->from_search)
            {
                reader_clear_search(app);
                app->cfg->viewansi = saved_viewansi;
                return VIEW_SEARCH_RESULTS;
            }

            reader_clear_search(app);
            reader_save_lastread(app);
            update_area_counts_in_memory(app);

            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 1;

            return VIEW_MSGLIST;

        case 27:
            /* ESC: exit search mode first, otherwise quit */
            if (app->reader_search.only_mode)
            {
                reader_clear_search(app);
                ui_status(app, "Search mode exited");

                continue; /* Force redraw */
            }
            /* Fall through to quit */
        case 'q':
        case 'Q':
            reader_save_lastread(app);
            update_area_counts_in_memory(app);
            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 0;

            reader_clear_search(app);

            return reader_exit_view(app);

        case KEY_RESIZE:
            flushinp();
            load_msg(app, app->cur_msgnum);
            break;
        case CTRL('F'):
            ui_popup_freq(app);
            break;
        default:
            break;
        }
    }
}

/*
 * crashedit - Message area editor for AmigaOS
 *
 * This file is part of the crashedit project.
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This program uses JAMLIB, which is licensed under the GNU Lesser
 * General Public License v2.1. See src/jamlib/LICENSE for details.
 */

/* ui_reader.c -- Message reader view */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"
#include "ui_attr.h"
#include "ui_files.h"
#include "../../src/jamlib/jam.h"
#include "../core/msghdr.h"
#include "../core/ftn.h"
#include "../core/ansi.h"
#include "../core/keys.h"

static const char *READER_HELP[] =
    {
        "Message Reader - Key Bindings:",
        "",
        "  Up/Down,j/k    Scroll body up/down",
#ifdef PLATFORM_AMIGA
        "  Ctrl+U/D       Page scroll",
        "  Ctrl+B/E       First / last line",
#else
        "  PgUp/PgDn      Page scroll",
        "  Home/End       First / last line",
#endif
        "  Left/Right,p/n Previous / next message",
        "",
        "  b/Space        Page up/down",
        "  Enter          Page down or advance to next message",
        "  < >            First / last line (scroll)",
        "  , .            First / last message",
        "  Ctrl-J         Jump to original (follow reply chain)",
        "  Ctrl+Left      Jump to original (follow reply chain)",
        "  Ctrl+Right     Jump to reply (pick if multiple)",
        "",
        "  k, v           Toggle kludges (^A, SEEN-BY, Via)",
        "  a              Toggle ANSI color rendering",
        "  c              Change DISPLAY charset (re-render)",
        "  r              Reply to this message",
        "  e              Edit this message",
        "  n, Ins         New message (in current area)",
        "  l, L           Show message list overlay (ESC returns here)",
        "  g              Goto message number",
        "  Ctrl+H         Scroll to top of message",
        "  Ctrl+K         Scroll to bottom of message",
        "  Ctrl+G         Goto line number",
        "  Alt+J          Follow reply chain to original",
        "  Alt+G          Clear search highlights",
        "  F5, /          Search in message body",
        "  F3 Alt+P       Previous match (search mode)",
        "  F4 Alt+N       Next match (search mode)",
        "  w, F7          Write message to text file",
        "  d, Del         Delete this message",
        "  Ctrl+F         File request",
        "",
        "  ESC, q         Back to message list",
        "  F1, ?          This help"};
#define READER_HELP_N ((int)(sizeof(READER_HELP) / sizeof(READER_HELP[0])))

void ui_editor_prep_new(UiApp *app);
void ui_editor_prep_reply(UiApp *app, uint32_t orig_msgnum);
void ui_editor_prep_edit(UiApp *app, uint32_t msgnum);

/* Update area's cached counts after reader moves lastread/lastseen */
static void update_area_counts_in_memory(UiApp *app)
{
    UiSession *s;
    AreaEntry *ae;
    int i, unread, new_count;

    if (!app)
        return;

    s = &app->sess;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return;

    ae = &app->areas->entries[s->area_idx];

    ae->lastread = s->lastread;
    ae->lastseen = s->lastseen;

    /* Recompute unread (above lastread) and new_count (above lastseen) */
    unread = 0;
    new_count = 0;

    for (i = 0; i < s->msg_count; i++)
    {
        if (s->msgs[i].msgnum > s->lastread)
            unread++;

        if (s->msgs[i].msgnum > s->lastseen)
            new_count++;
    }

    ae->unread = unread;
    ae->new_count = new_count;
    ae->total_msgs = s->msg_count;
}

/* Update lastread and persist to JAM; skip if from search (browsing shouldn't mutate) */
static void reader_save_lastread(UiApp *app)
{
    UiSession *s;
    uint32_t msgnum;

    if (!app)
        return;

    if (app->from_search)
        return;

    s = &app->sess;
    msgnum = app->cur_msgnum;

    s->lastread = msgnum;

    if (msgnum > s->lastseen)
        s->lastseen = msgnum;

    if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
    {
        jam_write_lastread(&s->jam, s->user_crc, s->lastread, s->lastseen);
        jam_unlock(&s->jam);
    }

    update_area_counts_in_memory(app);
}

/* Exit target: msglist (normal) or search results (if from search) */
static UiView reader_exit_view(const UiApp *app)
{
    return app->from_search ? VIEW_SEARCH_RESULTS : VIEW_MSGLIST;
}

/* Strip FTS-1 kludge lines and SEEN-BY/PATH trailers for ANSI rendering
 * Returns malloc'd buffer (caller frees), NULL on failure */
static char *strip_kludges_for_ansi(const char *raw, int raw_len, int *out_len)
{
    char *out;
    int i = 0;
    int j = 0;
    int at_line_start = 1;

    if (out_len)
        *out_len = 0;

    if (!raw || raw_len <= 0)
        return NULL;

    out = (char *)malloc((size_t)raw_len + 1);
    if (!out)
        return NULL;

    while (i < raw_len)
    {
        if (at_line_start)
        {
            const char *p = raw + i;
            int rem = raw_len - i;
            int is_kludge = 0;

            if (rem >= 1 && (unsigned char)*p == 0x01)
                is_kludge = 1;
            else if (rem >= 8 && memcmp(p, "SEEN-BY:", 8) == 0)
                is_kludge = 1;
            else if (rem >= 5 && memcmp(p, "PATH:", 5) == 0)
                is_kludge = 1;

            if (is_kludge)
            {
                /* Skip to end of this line (eat optional \r and \n) */
                while (i < raw_len && raw[i] != '\r' && raw[i] != '\n')
                    i++;

                if (i < raw_len && raw[i] == '\r')
                    i++;
                if (i < raw_len && raw[i] == '\n')
                    i++;

                /* Still at line start for the next iteration */
                continue;
            }

            at_line_start = 0;
        }

        out[j++] = raw[i];

        if (raw[i] == '\r' || raw[i] == '\n')
            at_line_start = 1;

        i++;
    }

    out[j] = '\0';

    if (out_len)
        *out_len = j;

    return out;
}

/* Load current message into reader */
static int load_msg(UiApp *app, uint32_t msgnum)
{
    UiSession *s;
    int idx;
    char *body_utf8 = NULL;
    int wrap_w;
    const wchar_t *cur;
    char *cur_utf8;
    int needs_fallback;
    int ansi_on_outer = 0;
    char detected[CHARSET_NAME_MAX];
    const char *override_enc;
    char chosen[CHARSET_NAME_MAX];
    int ansi_on;
    uint32_t raw_len = 0;
    char *raw_bytes = NULL;
    int has_chrs = 0;
    const char *p;
    int oi;

    if (!app)
        return -1;

    s = &app->sess;

    idx = jam_find_by_msgnum(s->msgs, s->msg_count, msgnum);

    if (idx < 0)
        return -1;

    /* Sync msg_sel to current message for correct navigation after search/jump entry */
    for (oi = 0; oi < s->order_count; oi++)
    {
        if (s->order[oi] == idx)
        {
            s->msg_sel = oi + 1;
            break;
        }
    }

    /* Fill header */
    msghdr_load(app->hdr, &s->msgs[idx], app->areas->entries[s->area_idx].name, idx + 1, s->msg_count, ftn_effective_tz_offset(app->cfg->timezone_offset, app->cfg->timezone_is_manual));

    /* Body: ANSI->canvas, text->UTF-8. Charset: CHRS if present, else default */
    override_enc = app->view_charset[0] ? app->view_charset : NULL;

    detected[0] = '\0';
    chosen[0] = '\0';

    /* Read and decode body using wrapper_read_utf8_ex with proper fallback logic */
    body_utf8 = wrapper_read_utf8_ex(&s->jam, msgnum, override_enc, NULL, detected, sizeof(detected));

    if (!body_utf8)
        return -1;

    /* Read raw body for ANSI mode (needs original bytes, not UTF-8) */
    raw_bytes = jam_read_body(&s->jam, msgnum, &raw_len);

    if (!raw_bytes)
    {
        free(body_utf8);
        return -1;
    }

    /* Set charset variables based on wrapper_read_utf8_ex results */
    strncpy(chosen, detected, sizeof(chosen) - 1);
    chosen[sizeof(chosen) - 1] = '\0';

    /* Check if we actually have a CHRS kludge */
    has_chrs = (detected[0] != '\0');

    if (has_chrs)
    {
        strncpy(app->msg_charset, detected, sizeof(app->msg_charset) - 1);
        app->msg_charset[sizeof(app->msg_charset) - 1] = '\0';
    }
    else
    {
        app->msg_charset[0] = '\0';
    }

    /* ANSI mode: force CP437 if UTF-8 came from default/fallback (not CHRS/override)
     * Exception: when TTF is active, keep detected charset to preserve Unicode characters */
    ansi_on = (app->cfg && app->cfg->viewansi);
    ansi_on_outer = ansi_on;

    if (ansi_on && (strcasecmp(chosen, "UTF-8") == 0 || strcasecmp(chosen, "UTF8") == 0))
    {
        int user_wants_utf8 = (override_enc && (strcasecmp(override_enc, "UTF-8") == 0 || strcasecmp(override_enc, "UTF8") == 0));
        int chrs_says_utf8 = (app->msg_charset[0] && (strcasecmp(app->msg_charset, "UTF-8") == 0 || strcasecmp(app->msg_charset, "UTF8") == 0));

        if (!user_wants_utf8 && !chrs_says_utf8)
        {
            strncpy(chosen, "CP437", sizeof(chosen) - 1);
            chosen[sizeof(chosen) - 1] = '\0';
        }
    }

    /* Record actual decode charset for status bar (may differ from view_charset) */
    if (override_enc && override_enc[0])
        strncpy(app->decoded_charset, override_enc, sizeof(app->decoded_charset) - 1);
    else
        strncpy(app->decoded_charset, chosen, sizeof(app->decoded_charset) - 1);

    app->decoded_charset[sizeof(app->decoded_charset) - 1] = '\0';

    /*wrap_w = COLS - 2;*/
    wrap_w = COLS;

    if (wrap_w < 20)
        wrap_w = 20;

    if (ansi_on)
    {
        /* Strip kludge/SEEN-BY/PATH lines. Fall back to raw bytes on failure */
        int stripped_len = 0;
        char *stripped = strip_kludges_for_ansi(raw_bytes, (int)raw_len, &stripped_len);

        if (stripped)
        {
            rd_load_ansi(app->reader, stripped, stripped_len, chosen, wrap_w);
            free(stripped);
        }
        else
        {
            rd_load_ansi(app->reader, raw_bytes, (int)raw_len, chosen, wrap_w);
        }

        /* Free unused UTF-8 buffer; ANSI mode uses raw bytes. Prevents leak */
        free(body_utf8);
        body_utf8 = NULL; /* not used in this path */
    }

    /* ANSI mode: convert kludges/SEEN-BY/Origin to UTF-8 for MSGID/origin lookup */
    if (ansi_on)
    {
        int srclen = (int)raw_len;
        int dstmax = srclen * 4 + 256;

        body_utf8 = (char *)malloc((size_t)dstmax);

        if (body_utf8)
            charset_body_to_utf8(chosen, raw_bytes, srclen, body_utf8, dstmax);
    }

    free(raw_bytes);

    if (!body_utf8)
        return -1;

    /* Extract MSGID / REPLY for header display */
    app->cur_msgid[0] = '\0';
    app->cur_reply[0] = '\0';

    ftn_get_kludge_value(body_utf8, "MSGID", app->cur_msgid, sizeof(app->cur_msgid));
    ftn_get_kludge_value(body_utf8, "REPLY", app->cur_reply, sizeof(app->cur_reply));

    /* Get sender address from MSGID kludge, fallback to Origin line */

    cur = msghdr_get(app->hdr, HDR_OADDR);
    cur_utf8 = cur ? wcs_to_utf8(cur, (int)wcslen(cur)) : NULL;

    if (cur && !cur_utf8)
    {
        free(body_utf8); /* would otherwise leak on this OOM path */
        return -1;
    }

    needs_fallback = (!cur_utf8 || !cur_utf8[0] || strcmp(cur_utf8, "0:0/0") == 0);

    if (needs_fallback)
    {
        char addr[64];
        int got = -1;

        if (app->cur_msgid[0])
        {
            /* Extract address from MSGID (format: "addr serial") */
            int i;

            addr[0] = '\0';

            for (i = 0; app->cur_msgid[i] && app->cur_msgid[i] != ' ' && app->cur_msgid[i] != '\t' && i < (int)sizeof(addr) - 1; i++)
                addr[i] = app->cur_msgid[i];

            addr[i] = '\0';

            if (addr[0])
                got = 0;
        }

        if (got != 0)
            got = ftn_find_origin_address(body_utf8, addr, sizeof(addr));

        if (got == 0)
            msghdr_set_utf8(app->hdr, HDR_OADDR, addr);
    }

    free(cur_utf8);

    wrap_w = COLS; /* full width, match editor soft-wrap */

    if (wrap_w < 20)
        wrap_w = 20;

    /* ANSI: canvas loaded above, skip rd_load. Non-ANSI: rd_load handles wrap */
    if (!ansi_on_outer)
        rd_load(app->reader, body_utf8, wrap_w);

    free(body_utf8);

    rd_set_page(app->reader, LINES - 8);

    /* Apply view-config toggles (kludges visibility) */
    if (app->cfg->viewkludge != rd_kludges_visible(app->reader))
        rd_toggle_kludges(app->reader);

    return 0;
}

/* Try to go to next area in area_order. Returns 1 on success, 0 otherwise */
static int try_goto_next_area(UiApp *app)
{
    UiSession *s;
    int current_area_idx;
    int current_order_idx = -1;
    int next_area_idx = -1;
    int i;
    AreaEntry *next_ae;
    char prompt[256];

    if (!app)
        return 0;

    s = &app->sess;

    if (app->area_order_count <= 0)
        return 0;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return 0;

    current_area_idx = s->area_idx;

    /* Find current area in area_order */
    for (i = 0; i < app->area_order_count; i++)
    {
        if (app->area_order[i] == current_area_idx)
        {
            current_order_idx = i;
            break;
        }
    }

    /* Find next area in order */
    if (current_order_idx >= 0 && current_order_idx < app->area_order_count - 1)
    {
        next_area_idx = app->area_order[current_order_idx + 1];
    }

    if (next_area_idx < 0)
        return 0;

    next_ae = &app->areas->entries[next_area_idx];
    snprintf(prompt, sizeof(prompt), "Go to next area '%s'?", next_ae->name ? next_ae->name : "(area)");

    if (ui_popup_confirm("Next area", prompt) != 1)
        return 0;

    /* Save lastread before changing area */
    reader_save_lastread(app);

    /* Close current session and open next area */
    ui_session_close(app);

    if (ui_session_open(app, next_area_idx) != 0)
    {
        ui_status(app, "Cannot open area");
        return 0;
    }

    /* Load message from new area */
    if (s->order_count > 0)
    {
        /* Check if current position is on an unread message */
        uint32_t current_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        /* If not on unread message, go to last message in list */
        if (current_msgnum <= s->lastread)
            s->msg_sel = s->order_count;

        app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        if (load_msg(app, app->cur_msgnum) != 0)
        {
            ui_status(app, "Cannot load message");
            return 0;
        }
    }

    return 1;
}

/* Header bar */
static void draw_header_bar(UiApp *app)
{
    int i;
    const wchar_t *from, *to, *subj, *date;
    char attr_str[40];
    static const char SPACES[256] =
        "                                                                "
        "                                                                "
        "                                                                "
        "                                                                ";

    from = msghdr_get(app->hdr, HDR_FROM);
    to = msghdr_get(app->hdr, HDR_TO);
    subj = msghdr_get(app->hdr, HDR_SUBJECT);
    date = msghdr_get(app->hdr, HDR_DATE);
    ui_attr_build(msghdr_attr(app->hdr), attr_str, sizeof(attr_str));

    attron(COLOR_PAIR(COL_HEADER));

    for (i = 1; i < 7; i++)
    {
        int written = 0;
        move(i, 0);
        clrtoeol();

        while (written < COLS)
        {
            int chunk = COLS - written;

            if (chunk > (int)sizeof(SPACES))
                chunk = (int)sizeof(SPACES);

            mvaddnstr(i, written, SPACES, chunk);
            written += chunk;
        }
    }

    mvprintw(1, 1, "Area: %-30.30s  Msg %d/%d", ui_wcs2u8(msghdr_get(app->hdr, HDR_AREA)), msghdr_msgnum(app->hdr), msghdr_msgtotal(app->hdr));
    mvprintw(2, 1, "From: %-25.25s  %-20.20s  %s", ui_wcs2u8(from), ui_wcs2u8(msghdr_get(app->hdr, HDR_OADDR)), ui_wcs2u8(date));
    mvprintw(3, 1, "  To: %-25.25s  %s", ui_wcs2u8(to), ui_wcs2u8(msghdr_get(app->hdr, HDR_DADDR)));
    mvprintw(4, 1, "Subj: %s", ui_wcs2u8(subj));

    /* Attr on left, MSGID on right */
    mvprintw(5, 1, "Attr: %s", attr_str);

    if (app->cur_msgid[0])
    {
        int show = (int)strlen(app->cur_msgid);
        int max_room = COLS - 22;

        if (max_room < 10)
            max_room = 10;

        if (show > max_room)
            show = max_room;

        mvprintw(5, COLS - show - 8, "MSGID: %.*s", show, app->cur_msgid);
    }

    attroff(COLOR_PAIR(COL_HEADER));

    attron(COLOR_PAIR(COL_BORDER));
    ui_hline(6, 0, COLS);
    attroff(COLOR_PAIR(COL_BORDER));
}

/* Body rendering */
/* Convert one wchar_t to UTF-8 in buf. Returns bytes written */
static int wc_to_utf8_bytes(wchar_t wc, char *buf)
{
    unsigned long cp = (unsigned long)wc;

    if (cp < 0x80)
    {
        buf[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));

        return 2;
    }

    if (cp < 0x10000)
    {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));

        return 3;
    }

    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));

    return 4;
}

/* Draw ANSI line: collect runs of same attrs and emit each run in one call */
static void draw_ansi_line(int row_y, const wchar_t *wcs, int len, const struct AnsiCell *cells, int max_cols)
{
    char run_buf[1024];
    int i = 0;
    int x = 0;

    while (i < len && x < max_cols)
    {
        int run_color = cells[i].color_pair;
        int run_attrs = cells[i].attrs;
        int run_start = i;
        int run_chars = 0;
        int run_bytes = 0;

        /* Find extent of run with identical (color, attrs) */
        while (i < len && x + run_chars < max_cols && cells[i].color_pair == run_color && cells[i].attrs == run_attrs)
        {
            int n;

            if (run_bytes + 4 >= (int)sizeof(run_buf))
                break; /* Don't overflow run buffer; emit and continue */

            n = wc_to_utf8_bytes(wcs[i], &run_buf[run_bytes]);
            run_bytes += n;
            run_chars++;
            i++;
        }

        if (run_chars == 0)
            break; /* safety */

        run_buf[run_bytes] = '\0';

        if (run_color >= 0)
            attron(COLOR_PAIR(run_color) | run_attrs);

        mvaddnstr(row_y, x, run_buf, run_bytes);

        if (run_color >= 0)
            attroff(COLOR_PAIR(run_color) | run_attrs);

        x += run_chars;
    }
}

static void draw_body(UiApp *app)
{
    int rows = LINES - 8;
    int i;
    const wchar_t *wcs;
    int wlen;

    if (rows < 1)
        return;

    rd_set_page(app->reader, rows);

    for (i = 0; i < rows; i++)
    {
        int vi = rd_top(app->reader) + i;
        int type, col;
        int ansi_color, ansi_attrs;
        const struct AnsiCell *cells;
        int row_y = 7 + i;
        const wchar_t *ln;
        int lnlen;

        /* Skip if terminal shrunk since rd_set_page */
        if (row_y < 0 || row_y >= LINES - 1)
            continue;

        move(row_y, 0);
        clrtoeol();

        if (vi >= rd_total(app->reader))
            continue;

        type = rd_get_type(app->reader, vi);
        ansi_color = rd_get_ansi_color(app->reader, vi);
        ansi_attrs = rd_get_ansi_attrs(app->reader, vi);
        cells = rd_get_ansi_cells(app->reader, vi);

        /* ANSI with per-cell attrs: draw via canvas (preserves BBS art colors) */
        if (rd_ansi_visible(app->reader) && cells)
        {
            ln = rd_get_line(app->reader, vi);
            lnlen = rd_get_len(app->reader, vi);

            if (ln && lnlen > 0)
                draw_ansi_line(row_y, ln, lnlen, cells, (COLS < 80) ? COLS : 80);

            continue; /* attrs handled inside draw_ansi_line */
        }

        if (rd_ansi_visible(app->reader) && ansi_color >= 0)
        {
            col = ansi_color;
            attron(COLOR_PAIR(col) | ansi_attrs);
        }
        else
        {
            col = ui_color_for_type(type);
            attron(COLOR_PAIR(col));
            ansi_attrs = 0;
        }

        /* Plain or ANSI without per-cell attrs: draw with line color */
        ln = rd_get_line(app->reader, vi);
        lnlen = rd_get_len(app->reader, vi);

        /* Skip leading ^A on kludge lines */
        if (ln && lnlen > 0 && (type & FTN_LT_KLUDGE) && ln[0] == L'\x01')
        {
            ln++;
            lnlen--;
        }

        if (ln && lnlen > 0)
            mvaddnwstr(row_y, 0, ln, lnlen);

        /* Highlight search matches (word-level) */
        if (app->reader_search.rows && app->reader_search.cols && app->reader_search.match_count > 0)
        {
            int line_idx = rd_get_line_idx(app->reader, vi);
            int j;

            if (line_idx >= 0)
            {
                for (j = 0; j < app->reader_search.match_count; j++)
                {
                    if (app->reader_search.rows[j] == line_idx)
                    {
                        int match_col = app->reader_search.cols[j];
                        int match_len = (int)wcslen(app->reader_search.query);

                        if (match_col >= 0 && match_col + match_len <= lnlen)
                        {
                            if (j == app->reader_search.current_match)
                                attron(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attron(COLOR_PAIR(COL_SEARCH_MATCH));

                            mvaddnwstr(row_y, match_col, &ln[match_col], match_len);

                            if (j == app->reader_search.current_match)
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
            }
        }

        attroff(COLOR_PAIR(col) | ansi_attrs);
    }
}

/* Clear search state in reader */
static void reader_clear_search(UiApp *app)
{
    if (app->reader_search.rows)
    {
        free(app->reader_search.rows);
        app->reader_search.rows = NULL;
    }

    if (app->reader_search.cols)
    {
        free(app->reader_search.cols);
        app->reader_search.cols = NULL;
    }

    app->reader_search.match_count = 0;
    app->reader_search.is_mode = 0;
    app->reader_search.only_mode = 0;
    app->reader_search.current_match = 0;
    app->reader_search.match_current = 0;
}

/* Navigate to a raw msgs[] index, honouring the current sort order */
static int reader_goto_raw(UiApp *app, int raw_idx)
{
    UiSession *s = &app->sess;
    uint32_t target_mn;
    int i;

    if (raw_idx < 0 || raw_idx >= s->msg_count)
        return -1; /* invalid index */

    target_mn = s->msgs[raw_idx].msgnum;

    for (i = 0; i < s->order_count; i++)
    {
        if (s->msgs[s->order[i]].msgnum == target_mn)
        {
            s->msg_sel = i + 1;
            app->cur_msgnum = target_mn;

            return load_msg(app, target_mn);
        }
    }

    return -2;
}

/* Main loop */
UiView ui_reader_run(UiApp *app)
{
    int ch;
    UiSession *s;
    int saved_viewansi;

    if (!app)
        return VIEW_QUIT;

    s = &app->sess;
    saved_viewansi = app->cfg->viewansi;

    if (load_msg(app, app->cur_msgnum) != 0)
    {
        ui_status(app, "Cannot load message");
        app->cfg->viewansi = saved_viewansi;
        return reader_exit_view(app);
    }

    /* Apply ANSI visibility from config on first load */
    if (app->cfg->viewansi != rd_ansi_visible(app->reader))
    {
        rd_toggle_ansi(app->reader);

#ifdef PLATFORM_AMIGA
        amiga_change_font(rd_ansi_visible(app->reader));
#endif
    }

    for (;;)
    {
        erase();

        ui_draw_menubar(app, "Reader");
        draw_header_bar(app);
        draw_body(app);

        /* Status: msg_charset (CHRS or ?) -> decoded_charset (actual decode) */
        ui_status(app, "Charset: %s -> %s | c = Change charset%s | Ln %d/%d",
                  app->msg_charset[0] ? app->msg_charset : "?",
                  (app->view_charset[0]) ? app->decoded_charset : "Auto",
                  rd_ansi_visible(app->reader) ? " | ANSI" : "",
                  rd_top(app->reader) + 1, rd_total(app->reader));

        ui_draw_statusbar(app);
        refresh();

        ch = wrapper_getch();

        switch (ch)
        {
        case KEY_UP:
            rd_scroll_up(app->reader, 1);
            break;
        case KEY_DOWN:
            rd_scroll_down(app->reader, 1);
            break;
        case KEY_PPAGE:
        case 'b':
        case 'B':
        case CTRL('U'): /* Page Up (Amiga) */
            rd_page_up(app->reader);
            break;
        case KEY_NPAGE:
        case CTRL('D'): /* Page Down (Amiga) */
        case ' ':
            rd_page_down(app->reader);
            break;
        case KEY_HOME:
        case CTRL('B'): /* Home (Amiga) */
            rd_home(app->reader);
            break;
        case KEY_END:
        case CTRL('E'): /* End (Amiga) */
            rd_end(app->reader);
            break;

        case KEY_RIGHT:
        case 'n': /* Next message */
            if (s->msg_sel < s->order_count)
            {
                s->msg_sel++;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load next");
            }
            else
            {
                /* At last message: try to go to next area */
                try_goto_next_area(app);
            }
            break;
        case KEY_LEFT:
        case 'p': /* Previous message */
            if (s->msg_sel > 1)
            {
                s->msg_sel--;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load prev");
            }
            break;

        case CTRL('H'):
        case '<':
            /* Scroll to top of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) > 0)
                rd_scroll_up(app->reader, 1);

            break;

        case CTRL('K'):
        case '>':
            /* Scroll to bottom of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) + rd_visible(app->reader) < rd_total(app->reader))
                rd_scroll_down(app->reader, 1);

            break;

        case ',':
            /* First message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = 1;
                app->cur_msgnum = s->msgs[s->order[0]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load first");
            }
            break;

        case '.':
            /* Last message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = s->order_count;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load last");
            }
            break;
        case KEY_ALT('J'): /* Follow REPLY chain to original */
        case KEY_CLEFT:
        {
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int orig = (idx >= 0) ? ftn_find_original(s->msgs, s->msg_count, idx) : -1;

            /* Fallback: search by MSGID string from REPLY kludge */
            if (orig < 0 && app->cur_reply[0])
                orig = ftn_find_original_by_msgid(s->msgs, s->msg_count, app->cur_reply);

            if (orig < 0)
                ui_status(app, "No original message in this area");
            else if (orig >= s->msg_count)
                ui_status(app, "Invalid original message index");
            else if (s->msgs[orig].from[0] == '\0' || s->msgs[orig].subject[0] == '\0')
                ui_status(app, "Original message is empty or corrupted");
            else
            {
                int rc = reader_goto_raw(app, orig);

                if (rc == -2)
                    ui_status(app, "Original not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load original");
            }

            break;
        }
        case KEY_CRIGHT: /* Follow thread to reply (with picker if branched) */
        {
#define MAX_REPLIES 256
            int raw_replies[MAX_REPLIES];
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int nreplies = (idx >= 0) ? ftn_find_all_replies(s->msgs, s->msg_count, idx, raw_replies, MAX_REPLIES) : 0;

            if (nreplies == 0)
            {
                ui_status(app, "No replies to this message");
            }
            else if (nreplies == 1)
            {
                int rc = reader_goto_raw(app, raw_replies[0]);
                if (rc == -2)
                    ui_status(app, "Reply not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load reply");
            }
            else
            {
                /* Multiple replies: build label list and show picker */
                const char *items[MAX_REPLIES];
                char labels[MAX_REPLIES][80];
                int i, choice;

                for (i = 0; i < nreplies; i++)
                {
                    const JamMsgInfo *m = &s->msgs[raw_replies[i]];

                    snprintf(labels[i], sizeof(labels[i]), "#%-5u  %-20.20s  %.28s", (unsigned)m->msgnum, m->from, m->subject);
                    items[i] = labels[i];
                }

                choice = ui_popup_list("Select reply", items, nreplies, 0);

                if (choice >= 0)
                {
                    int rc = reader_goto_raw(app, raw_replies[choice]);

                    if (rc == -2)
                        ui_status(app, "Reply not visible in current filter");
                    else if (rc != 0)
                        ui_status(app, "Cannot load reply");
                }
            }
#undef MAX_REPLIES
            break;
        }

        case 'c':
        case 'C':
        {
            char new_view[32], new_out[32];

            new_view[0] = '\0';
            new_out[0] = '\0';

            if (ui_popup_charset_pair(app->view_charset, app->edit_charset, CHARSET_READ_DEFAULT, app->cfg->charset, new_view, sizeof(new_view), new_out, sizeof(new_out)) == 0)
            {
                /* Empty string = Auto (CHARSET_READ_DEFAULT); load_msg treats "" as no override */
                strncpy(app->view_charset, new_view, sizeof(app->view_charset) - 1);
                app->view_charset[sizeof(app->view_charset) - 1] = '\0';

                strncpy(app->edit_charset, new_out, sizeof(app->edit_charset) - 1);
                app->edit_charset[sizeof(app->edit_charset) - 1] = '\0';

                /* Mark that user manually changed charset (from reader) - only if NOT Auto */
                if (new_out[0] != '\0')
                    app->edit_charset_manually_changed = 1;

                /* Reload with new charset; preserves headers, lastread, ANSI, kludges */
                load_msg(app, app->cur_msgnum);

                ui_status(app, "View: %s | Save: %s", new_view[0] ? new_view : CHARSET_READ_DEFAULT, new_out[0] ? new_out : (app->cfg->charset[0] ? app->cfg->charset : "UTF-8"));
            }

            break;
        }

        case 'k':
        case 'K':
            rd_toggle_kludges(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case 'v':
        case 'V':
            rd_toggle_hiddklud(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Hidden+Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case KEY_F(5): /* Search in message body */
        case '/':
        {
            wchar_t tmp[64];
            int *rows = NULL, *cols = NULL;
            int match_count;
            int i;
            const char **contexts = NULL;
            char **context_bufs = NULL;
            int *line_nums = NULL;

            wcsncpy(tmp, app->reader_search.query, 63);
            tmp[63] = L'\0';

            if (ui_popup_input("Search", "Text:", tmp, 64) == 0 && tmp[0])
            {
                wcsncpy(app->reader_search.query, tmp, 63);
                app->reader_search.query[63] = L'\0';

                /* Find all matches in all lines (no limit) */
                match_count = rd_search_all(app->reader, app->reader_search.query, &rows, &cols);

                /* Free previous search matches */
                reader_clear_search(app);

                if (match_count == 0)
                {
                    ui_status(app, "Not found");
                }
                else if (match_count == 1)
                {
                    /* Single match: scroll to it */
                    rd_set_page(app->reader, LINES - 8);

                    if (rows[0] < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - rows[0]);
                    else if (rows[0] >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, rows[0] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Found at line %d", rows[0] + 1);

                    /* Save matches for highlighting and activate search mode */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
                else
                {
                    /* Allocate arrays for display */
                    contexts = (const char **)malloc((size_t)match_count * sizeof(const char *));
                    context_bufs = (char **)malloc((size_t)match_count * sizeof(char *));
                    line_nums = (int *)malloc((size_t)match_count * sizeof(int));

                    if (!contexts || !context_bufs || !line_nums)
                    {
                        if (contexts)
                            free(contexts);

                        if (context_bufs)
                            free(context_bufs);

                        if (line_nums)
                            free(line_nums);

                        free(rows);
                        free(cols);
                        ui_status(app, "Memory error");

                        break;
                    }

                    /* Multiple matches: show list */
                    for (i = 0; i < match_count; i++)
                    {
                        const wchar_t *line = rd_get_line(app->reader, rows[i]);
                        int line_len = rd_get_len(app->reader, rows[i]);

                        context_bufs[i] = (char *)malloc(128);

                        if (!context_bufs[i])
                        {
                            context_bufs[i] = NULL;
                            contexts[i] = "";
                        }
                        else
                        {
                            /* Convert context to UTF-8 for display (max 60 chars) */
                            if (line && line_len > 0)
                            {
                                int copy_len = line_len;

                                if (copy_len > 60)
                                    copy_len = 60;

                                char *utf8 = wcs_to_utf8(line, copy_len);

                                if (utf8)
                                {
                                    snprintf(context_bufs[i], 128, "%s", utf8);
                                    free(utf8);
                                }
                                else
                                {
                                    context_bufs[i][0] = '\0';
                                }
                            }
                            else
                            {
                                context_bufs[i][0] = '\0';
                            }
                        }

                        contexts[i] = context_bufs[i];
                        line_nums[i] = rows[i] + 1; /* 1-based for display */
                    }

                    /* Show popup with results */
                    int choice = ui_popup_search_results("Search Results", line_nums, contexts, match_count, 0);

                    if (choice >= 0)
                    {
                        rd_set_page(app->reader, LINES - 8);

                        if (rows[choice] < rd_top(app->reader))
                            rd_scroll_up(app->reader, rd_top(app->reader) - rows[choice]);
                        else if (rows[choice] >= rd_top(app->reader) + rd_visible(app->reader))
                            rd_scroll_down(app->reader, rows[choice] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                        app->reader_search.current_match = choice;
                        app->reader_search.match_current = choice + 1;
                        ui_status(app, "Jumped to line %d", rows[choice] + 1);
                    }
                    else
                    {
                        ui_status(app, "Search cancelled");
                    }

                    /* Free allocated memory */
                    for (i = 0; i < match_count; i++)
                    {
                        if (context_bufs[i])
                            free(context_bufs[i]);
                    }

                    free(context_bufs);
                    free(contexts);
                    free(line_nums);

                    /* Save matches for highlighting and activate search mode (don't free rows/cols) */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
            }
            break;
        }

        case 'a':
        case 'A':
            rd_toggle_ansi(app->reader);
            app->cfg->viewansi = rd_ansi_visible(app->reader);
#ifdef PLATFORM_AMIGA
            amiga_change_font(rd_ansi_visible(app->reader));
#endif
            /* Reload message to apply/re-apply ANSI parsing with new font */
            load_msg(app, app->cur_msgnum);
            /* Continue to next iteration to redraw with new font */
            continue;

        case 'r':
        case 'R':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_reply(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'e':
        case 'E':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_edit(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'N':
        case KEY_IC: /* New message */
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_new(app);
            return VIEW_EDITOR;

        case '\r':
        case '\n':
        case KEY_ENTER:
            /* Page down or advance to next message at bottom */
            if (rd_top(app->reader) + rd_visible(app->reader) >= rd_total(app->reader))
            {
                if (s->msg_sel < s->order_count)
                {
                    s->msg_sel++;
                    app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load next");
                }
                else
                {
                    /* At last message: try to go to next area */
                    try_goto_next_area(app);
                }
            }
            else
                rd_page_down(app->reader);
            break;

        case CTRL('G'): /* Goto line number */
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Line number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long line_num = 0;
                int target_line;

                if (u)
                {
                    line_num = strtol(u, NULL, 10);
                    free(u);
                }

                target_line = (int)(line_num - 1); /* Convert to 0-based */

                if (target_line >= 0 && target_line < rd_total(app->reader))
                {
                    rd_set_page(app->reader, LINES - 8);

                    /* Scroll to target line */
                    if (target_line < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - target_line);
                    else if (target_line >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, target_line - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Jumped to line %ld", line_num);
                }
                else
                {
                    ui_status(app, "Invalid line number");
                }
            }
            break;
        }

        case KEY_ALT('G'): /* Clear search highlights */
        {
            reader_clear_search(app);
            ui_status(app, "Search highlights cleared");

            continue; /* Force redraw */
        }

        /* Navigate to previous match in reader */
        case KEY_F(3):
        case KEY_ALT('P'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match - 1 + app->reader_search.match_count) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        /* Navigate to next match in reader */
        case KEY_F(4):
        case KEY_ALT('N'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match + 1) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        case 'g': /* Goto message number */
        case 'G':
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Message number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long mn = 0;

                if (u)
                {
                    mn = strtol(u, NULL, 10);
                    free(u);
                }

                int i, found = -1;

                for (i = 0; i < s->order_count; i++)
                {
                    if ((long)s->msgs[s->order[i]].msgnum == mn)
                    {
                        found = i;
                        break;
                    }
                }

                if (found >= 0)
                {
                    s->msg_sel = found + 1;
                    app->cur_msgnum = (uint32_t)mn;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load message");
                }
                else
                    ui_status(app, "No such message in this view");
            }

            break;
        }

        case 'w': /* Write message to text file */
        case 'W':
        case KEY_F(7):
        {
            char path[256];
            char out_cs[CHARSET_NAME_MAX];
            UiSession *s;
            char *body_utf8;
            int written;

            path[0] = '\0';
            out_cs[0] = '\0';

            snprintf(path, sizeof(path), "msg_%u.txt", (unsigned)app->cur_msgnum);

            /*if (ui_popup_input("Write message", "File path:", path, sizeof(path)) != 0 || !path[0])
                break;
            */

            if (ui_files_save("Write message", NULL, path, path, sizeof(path)) != 0)
                break;

            /* Charset of the output file. Empty == AUTO == write UTF-8
             * verbatim; the user can type "CP437", "LATIN-1", etc */
            if (ui_popup_charset("Output charset", "", out_cs, sizeof(out_cs)) != 0)
                break;

            s = &app->sess;
            body_utf8 = wrapper_read_utf8_ex(&s->jam, app->cur_msgnum, app->view_charset[0] ? app->view_charset : NULL, NULL, app->msg_charset, sizeof(app->msg_charset));

            if (!body_utf8)
            {
                ui_status(app, "Cannot read message body");
                break;
            }

            written = rd_export_to_file(app->reader, body_utf8, path, out_cs[0] ? out_cs : NULL);
            free(body_utf8);

            if (written < 0)
                ui_status(app, "Cannot write %s", path);
            else
                ui_status(app, "Wrote %d lines to %s (%s)", written, path, out_cs[0] ? out_cs : "UTF-8");

            break;
        }
        case 'd':
        case 'D':
        case KEY_DC: /* Delete message */
            if (ui_popup_confirm("Delete", "Mark this message as deleted?") == 1)
            {
                if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
                {
                    if (jam_delete_msg(&s->jam, app->cur_msgnum) == 0)
                        ui_status(app, "Message deleted");

                    jam_unlock(&s->jam);
                    free(s->msgs);

                    s->msgs = NULL;
                    s->msgs = jam_load_headers(&s->jam, &s->msg_count, 0, (uint32_t)app->cfg->msglistmax);

                    if (!s->msgs)
                    {
                        s->msg_count = 0;
                        ui_status(app, "Reload failed after delete");
                    }
                    else
                    {
                        ui_session_rebuild_order(app);
                    }
                }
            }

            reader_save_lastread(app);

            /* reader_save_lastread() already updates area counts; skip redundant call */
            app->cfg->viewansi = saved_viewansi;

            return reader_exit_view(app);

            break;

        case KEY_F(1):
        case '?':
            ui_popup_help("Reader Help", READER_HELP, READER_HELP_N);
            break;

        case 'l':
        case 'L':
            /* Show msglist overlay; from search go to search browser instead */
            if (app->from_search)
            {
                reader_clear_search(app);
                app->cfg->viewansi = saved_viewansi;
                return VIEW_SEARCH_RESULTS;
            }

            reader_clear_search(app);
            reader_save_lastread(app);
            update_area_counts_in_memory(app);

            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 1;

            return VIEW_MSGLIST;

        case 27:
            /* ESC: exit search mode first, otherwise quit */
            if (app->reader_search.only_mode)
            {
                reader_clear_search(app);
                ui_status(app, "Search mode exited");

                continue; /* Force redraw */
            }
            /* Fall through to quit */
        case 'q':
        case 'Q':
            reader_save_lastread(app);
            update_area_counts_in_memory(app);
            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 0;

            reader_clear_search(app);

            return reader_exit_view(app);

        case KEY_RESIZE:
            flushinp();
            load_msg(app, app->cur_msgnum);
            break;
        case CTRL('F'):
            ui_popup_freq(app);
            break;
        default:
            break;
        }
    }
}

/*
 * crashedit - Message area editor for AmigaOS
 *
 * This file is part of the crashedit project.
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This program uses JAMLIB, which is licensed under the GNU Lesser
 * General Public License v2.1. See src/jamlib/LICENSE for details.
 */

/* ui_reader.c -- Message reader view */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"
#include "ui_attr.h"
#include "ui_files.h"
#include "../../src/jamlib/jam.h"
#include "../core/msghdr.h"
#include "../core/ftn.h"
#include "../core/ansi.h"
#include "../core/keys.h"

static const char *READER_HELP[] =
    {
        "Message Reader - Key Bindings:",
        "",
        "  Up/Down,j/k    Scroll body up/down",
#ifdef PLATFORM_AMIGA
        "  Ctrl+U/D       Page scroll",
        "  Ctrl+B/E       First / last line",
#else
        "  PgUp/PgDn      Page scroll",
        "  Home/End       First / last line",
#endif
        "  Left/Right,p/n Previous / next message",
        "",
        "  b/Space        Page up/down",
        "  Enter          Page down or advance to next message",
        "  < >            First / last line (scroll)",
        "  , .            First / last message",
        "  Ctrl-J         Jump to original (follow reply chain)",
        "  Ctrl+Left      Jump to original (follow reply chain)",
        "  Ctrl+Right     Jump to reply (pick if multiple)",
        "",
        "  k, v           Toggle kludges (^A, SEEN-BY, Via)",
        "  a              Toggle ANSI color rendering",
        "  c              Change DISPLAY charset (re-render)",
        "  r              Reply to this message",
        "  e              Edit this message",
        "  n, Ins         New message (in current area)",
        "  l, L           Show message list overlay (ESC returns here)",
        "  g              Goto message number",
        "  Ctrl+H         Scroll to top of message",
        "  Ctrl+K         Scroll to bottom of message",
        "  Ctrl+G         Goto line number",
        "  Alt+J          Follow reply chain to original",
        "  Alt+G          Clear search highlights",
        "  F5, /          Search in message body",
        "  F3 Alt+P       Previous match (search mode)",
        "  F4 Alt+N       Next match (search mode)",
        "  w, F7          Write message to text file",
        "  d, Del         Delete this message",
        "  Ctrl+F         File request",
        "",
        "  ESC, q         Back to message list",
        "  F1, ?          This help"};
#define READER_HELP_N ((int)(sizeof(READER_HELP) / sizeof(READER_HELP[0])))

void ui_editor_prep_new(UiApp *app);
void ui_editor_prep_reply(UiApp *app, uint32_t orig_msgnum);
void ui_editor_prep_edit(UiApp *app, uint32_t msgnum);

/* Update area's cached counts after reader moves lastread/lastseen */
static void update_area_counts_in_memory(UiApp *app)
{
    UiSession *s;
    AreaEntry *ae;
    int i, unread, new_count;

    if (!app)
        return;

    s = &app->sess;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return;

    ae = &app->areas->entries[s->area_idx];

    ae->lastread = s->lastread;
    ae->lastseen = s->lastseen;

    /* Recompute unread (above lastread) and new_count (above lastseen) */
    unread = 0;
    new_count = 0;

    for (i = 0; i < s->msg_count; i++)
    {
        if (s->msgs[i].msgnum > s->lastread)
            unread++;

        if (s->msgs[i].msgnum > s->lastseen)
            new_count++;
    }

    ae->unread = unread;
    ae->new_count = new_count;
    ae->total_msgs = s->msg_count;
}

/* Update lastread and persist to JAM; skip if from search (browsing shouldn't mutate) */
static void reader_save_lastread(UiApp *app)
{
    UiSession *s;
    uint32_t msgnum;

    if (!app)
        return;

    if (app->from_search)
        return;

    s = &app->sess;
    msgnum = app->cur_msgnum;

    s->lastread = msgnum;

    if (msgnum > s->lastseen)
        s->lastseen = msgnum;

    if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
    {
        jam_write_lastread(&s->jam, s->user_crc, s->lastread, s->lastseen);
        jam_unlock(&s->jam);
    }

    update_area_counts_in_memory(app);
}

/* Exit target: msglist (normal) or search results (if from search) */
static UiView reader_exit_view(const UiApp *app)
{
    return app->from_search ? VIEW_SEARCH_RESULTS : VIEW_MSGLIST;
}

/* Strip FTS-1 kludge lines and SEEN-BY/PATH trailers for ANSI rendering
 * Returns malloc'd buffer (caller frees), NULL on failure */
static char *strip_kludges_for_ansi(const char *raw, int raw_len, int *out_len)
{
    char *out;
    int i = 0;
    int j = 0;
    int at_line_start = 1;

    if (out_len)
        *out_len = 0;

    if (!raw || raw_len <= 0)
        return NULL;

    out = (char *)malloc((size_t)raw_len + 1);
    if (!out)
        return NULL;

    while (i < raw_len)
    {
        if (at_line_start)
        {
            const char *p = raw + i;
            int rem = raw_len - i;
            int is_kludge = 0;

            if (rem >= 1 && (unsigned char)*p == 0x01)
                is_kludge = 1;
            else if (rem >= 8 && memcmp(p, "SEEN-BY:", 8) == 0)
                is_kludge = 1;
            else if (rem >= 5 && memcmp(p, "PATH:", 5) == 0)
                is_kludge = 1;

            if (is_kludge)
            {
                /* Skip to end of this line (eat optional \r and \n) */
                while (i < raw_len && raw[i] != '\r' && raw[i] != '\n')
                    i++;

                if (i < raw_len && raw[i] == '\r')
                    i++;
                if (i < raw_len && raw[i] == '\n')
                    i++;

                /* Still at line start for the next iteration */
                continue;
            }

            at_line_start = 0;
        }

        out[j++] = raw[i];

        if (raw[i] == '\r' || raw[i] == '\n')
            at_line_start = 1;

        i++;
    }

    out[j] = '\0';

    if (out_len)
        *out_len = j;

    return out;
}

/* Load current message into reader */
static int load_msg(UiApp *app, uint32_t msgnum)
{
    UiSession *s;
    int idx;
    char *body_utf8 = NULL;
    int wrap_w;
    const wchar_t *cur;
    char *cur_utf8;
    int needs_fallback;
    int ansi_on_outer = 0;
    char detected[CHARSET_NAME_MAX];
    const char *override_enc;
    char chosen[CHARSET_NAME_MAX];
    int ansi_on;
    uint32_t raw_len = 0;
    char *raw_bytes = NULL;
    int has_chrs = 0;
    const char *p;
    int oi;

    if (!app)
        return -1;

    s = &app->sess;

    idx = jam_find_by_msgnum(s->msgs, s->msg_count, msgnum);

    if (idx < 0)
        return -1;

    /* Sync msg_sel to current message for correct navigation after search/jump entry */
    for (oi = 0; oi < s->order_count; oi++)
    {
        if (s->order[oi] == idx)
        {
            s->msg_sel = oi + 1;
            break;
        }
    }

    /* Fill header */
    msghdr_load(app->hdr, &s->msgs[idx], app->areas->entries[s->area_idx].name, idx + 1, s->msg_count, ftn_effective_tz_offset(app->cfg->timezone_offset, app->cfg->timezone_is_manual));

    /* Body: ANSI->canvas, text->UTF-8. Charset: CHRS if present, else default */
    override_enc = app->view_charset[0] ? app->view_charset : NULL;

    detected[0] = '\0';
    chosen[0] = '\0';

    /* Read and decode body using wrapper_read_utf8_ex with proper fallback logic */
    body_utf8 = wrapper_read_utf8_ex(&s->jam, msgnum, override_enc, NULL, detected, sizeof(detected));

    if (!body_utf8)
        return -1;

    /* Read raw body for ANSI mode (needs original bytes, not UTF-8) */
    raw_bytes = jam_read_body(&s->jam, msgnum, &raw_len);

    if (!raw_bytes)
    {
        free(body_utf8);
        return -1;
    }

    /* Set charset variables based on wrapper_read_utf8_ex results */
    strncpy(chosen, detected, sizeof(chosen) - 1);
    chosen[sizeof(chosen) - 1] = '\0';

    /* Check if we actually have a CHRS kludge */
    has_chrs = (detected[0] != '\0');

    if (has_chrs)
    {
        strncpy(app->msg_charset, detected, sizeof(app->msg_charset) - 1);
        app->msg_charset[sizeof(app->msg_charset) - 1] = '\0';
    }
    else
    {
        app->msg_charset[0] = '\0';
    }

    /* ANSI mode: force CP437 if UTF-8 came from default/fallback (not CHRS/override)
     * Exception: when TTF is active, keep detected charset to preserve Unicode characters */
    ansi_on = (app->cfg && app->cfg->viewansi);
    ansi_on_outer = ansi_on;

    if (ansi_on && (strcasecmp(chosen, "UTF-8") == 0 || strcasecmp(chosen, "UTF8") == 0))
    {
        int user_wants_utf8 = (override_enc && (strcasecmp(override_enc, "UTF-8") == 0 || strcasecmp(override_enc, "UTF8") == 0));
        int chrs_says_utf8 = (app->msg_charset[0] && (strcasecmp(app->msg_charset, "UTF-8") == 0 || strcasecmp(app->msg_charset, "UTF8") == 0));

        if (!user_wants_utf8 && !chrs_says_utf8)
        {
            strncpy(chosen, "CP437", sizeof(chosen) - 1);
            chosen[sizeof(chosen) - 1] = '\0';
        }
    }

    /* Record actual decode charset for status bar (may differ from view_charset) */
    if (override_enc && override_enc[0])
        strncpy(app->decoded_charset, override_enc, sizeof(app->decoded_charset) - 1);
    else
        strncpy(app->decoded_charset, chosen, sizeof(app->decoded_charset) - 1);

    app->decoded_charset[sizeof(app->decoded_charset) - 1] = '\0';

    /*wrap_w = COLS - 2;*/
    wrap_w = COLS;

    if (wrap_w < 20)
        wrap_w = 20;

    if (ansi_on)
    {
        /* Strip kludge/SEEN-BY/PATH lines. Fall back to raw bytes on failure */
        int stripped_len = 0;
        char *stripped = strip_kludges_for_ansi(raw_bytes, (int)raw_len, &stripped_len);

        if (stripped)
        {
            rd_load_ansi(app->reader, stripped, stripped_len, chosen, wrap_w);
            free(stripped);
        }
        else
        {
            rd_load_ansi(app->reader, raw_bytes, (int)raw_len, chosen, wrap_w);
        }

        /* Free unused UTF-8 buffer; ANSI mode uses raw bytes. Prevents leak */
        free(body_utf8);
        body_utf8 = NULL; /* not used in this path */
    }

    /* ANSI mode: convert kludges/SEEN-BY/Origin to UTF-8 for MSGID/origin lookup */
    if (ansi_on)
    {
        int srclen = (int)raw_len;
        int dstmax = srclen * 4 + 256;

        body_utf8 = (char *)malloc((size_t)dstmax);

        if (body_utf8)
            charset_body_to_utf8(chosen, raw_bytes, srclen, body_utf8, dstmax);
    }

    free(raw_bytes);

    if (!body_utf8)
        return -1;

    /* Extract MSGID / REPLY for header display */
    app->cur_msgid[0] = '\0';
    app->cur_reply[0] = '\0';

    ftn_get_kludge_value(body_utf8, "MSGID", app->cur_msgid, sizeof(app->cur_msgid));
    ftn_get_kludge_value(body_utf8, "REPLY", app->cur_reply, sizeof(app->cur_reply));

    /* Get sender address from MSGID kludge, fallback to Origin line */

    cur = msghdr_get(app->hdr, HDR_OADDR);
    cur_utf8 = cur ? wcs_to_utf8(cur, (int)wcslen(cur)) : NULL;

    if (cur && !cur_utf8)
    {
        free(body_utf8); /* would otherwise leak on this OOM path */
        return -1;
    }

    needs_fallback = (!cur_utf8 || !cur_utf8[0] || strcmp(cur_utf8, "0:0/0") == 0);

    if (needs_fallback)
    {
        char addr[64];
        int got = -1;

        if (app->cur_msgid[0])
        {
            /* Extract address from MSGID (format: "addr serial") */
            int i;

            addr[0] = '\0';

            for (i = 0; app->cur_msgid[i] && app->cur_msgid[i] != ' ' && app->cur_msgid[i] != '\t' && i < (int)sizeof(addr) - 1; i++)
                addr[i] = app->cur_msgid[i];

            addr[i] = '\0';

            if (addr[0])
                got = 0;
        }

        if (got != 0)
            got = ftn_find_origin_address(body_utf8, addr, sizeof(addr));

        if (got == 0)
            msghdr_set_utf8(app->hdr, HDR_OADDR, addr);
    }

    free(cur_utf8);

    wrap_w = COLS; /* full width, match editor soft-wrap */

    if (wrap_w < 20)
        wrap_w = 20;

    /* ANSI: canvas loaded above, skip rd_load. Non-ANSI: rd_load handles wrap */
    if (!ansi_on_outer)
        rd_load(app->reader, body_utf8, wrap_w);

    free(body_utf8);

    rd_set_page(app->reader, LINES - 8);

    /* Apply view-config toggles (kludges visibility) */
    if (app->cfg->viewkludge != rd_kludges_visible(app->reader))
        rd_toggle_kludges(app->reader);

    return 0;
}

/* Try to go to next area in area_order. Returns 1 on success, 0 otherwise */
static int try_goto_next_area(UiApp *app)
{
    UiSession *s;
    int current_area_idx;
    int current_order_idx = -1;
    int next_area_idx = -1;
    int i;
    AreaEntry *next_ae;
    char prompt[256];

    if (!app)
        return 0;

    s = &app->sess;

    if (app->area_order_count <= 0)
        return 0;

    if (s->area_idx < 0 || s->area_idx >= app->areas->count)
        return 0;

    current_area_idx = s->area_idx;

    /* Find current area in area_order */
    for (i = 0; i < app->area_order_count; i++)
    {
        if (app->area_order[i] == current_area_idx)
        {
            current_order_idx = i;
            break;
        }
    }

    /* Find next area in order */
    if (current_order_idx >= 0 && current_order_idx < app->area_order_count - 1)
    {
        next_area_idx = app->area_order[current_order_idx + 1];
    }

    if (next_area_idx < 0)
        return 0;

    next_ae = &app->areas->entries[next_area_idx];
    snprintf(prompt, sizeof(prompt), "Go to next area '%s'?", next_ae->name ? next_ae->name : "(area)");

    if (ui_popup_confirm("Next area", prompt) != 1)
        return 0;

    /* Save lastread before changing area */
    reader_save_lastread(app);

    /* Close current session and open next area */
    ui_session_close(app);

    if (ui_session_open(app, next_area_idx) != 0)
    {
        ui_status(app, "Cannot open area");
        return 0;
    }

    /* Load message from new area */
    if (s->order_count > 0)
    {
        /* Check if current position is on an unread message */
        uint32_t current_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        /* If not on unread message, go to last message in list */
        if (current_msgnum <= s->lastread)
            s->msg_sel = s->order_count;

        app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

        if (load_msg(app, app->cur_msgnum) != 0)
        {
            ui_status(app, "Cannot load message");
            return 0;
        }
    }

    return 1;
}

/* Header bar */
static void draw_header_bar(UiApp *app)
{
    int i;
    const wchar_t *from, *to, *subj, *date;
    char attr_str[40];
    static const char SPACES[256] =
        "                                                                "
        "                                                                "
        "                                                                "
        "                                                                ";

    from = msghdr_get(app->hdr, HDR_FROM);
    to = msghdr_get(app->hdr, HDR_TO);
    subj = msghdr_get(app->hdr, HDR_SUBJECT);
    date = msghdr_get(app->hdr, HDR_DATE);
    ui_attr_build(msghdr_attr(app->hdr), attr_str, sizeof(attr_str));

    attron(COLOR_PAIR(COL_HEADER));

    for (i = 1; i < 7; i++)
    {
        int written = 0;
        move(i, 0);
        clrtoeol();

        while (written < COLS)
        {
            int chunk = COLS - written;

            if (chunk > (int)sizeof(SPACES))
                chunk = (int)sizeof(SPACES);

            mvaddnstr(i, written, SPACES, chunk);
            written += chunk;
        }
    }

    mvprintw(1, 1, "Area: %-30.30s  Msg %d/%d", ui_wcs2u8(msghdr_get(app->hdr, HDR_AREA)), msghdr_msgnum(app->hdr), msghdr_msgtotal(app->hdr));
    mvprintw(2, 1, "From: %-25.25s  %-20.20s  %s", ui_wcs2u8(from), ui_wcs2u8(msghdr_get(app->hdr, HDR_OADDR)), ui_wcs2u8(date));
    mvprintw(3, 1, "  To: %-25.25s  %s", ui_wcs2u8(to), ui_wcs2u8(msghdr_get(app->hdr, HDR_DADDR)));
    mvprintw(4, 1, "Subj: %s", ui_wcs2u8(subj));

    /* Attr on left, MSGID on right */
    mvprintw(5, 1, "Attr: %s", attr_str);

    if (app->cur_msgid[0])
    {
        int show = (int)strlen(app->cur_msgid);
        int max_room = COLS - 22;

        if (max_room < 10)
            max_room = 10;

        if (show > max_room)
            show = max_room;

        mvprintw(5, COLS - show - 8, "MSGID: %.*s", show, app->cur_msgid);
    }

    attroff(COLOR_PAIR(COL_HEADER));

    attron(COLOR_PAIR(COL_BORDER));
    ui_hline(6, 0, COLS);
    attroff(COLOR_PAIR(COL_BORDER));
}

/* Body rendering */
/* Convert one wchar_t to UTF-8 in buf. Returns bytes written */
static int wc_to_utf8_bytes(wchar_t wc, char *buf)
{
    unsigned long cp = (unsigned long)wc;

    if (cp < 0x80)
    {
        buf[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800)
    {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));

        return 2;
    }

    if (cp < 0x10000)
    {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));

        return 3;
    }

    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));

    return 4;
}

/* Draw ANSI line: collect runs of same attrs and emit each run in one call */
static void draw_ansi_line(int row_y, const wchar_t *wcs, int len, const struct AnsiCell *cells, int max_cols)
{
    char run_buf[1024];
    int i = 0;
    int x = 0;

    while (i < len && x < max_cols)
    {
        int run_color = cells[i].color_pair;
        int run_attrs = cells[i].attrs;
        int run_start = i;
        int run_chars = 0;
        int run_bytes = 0;

        /* Find extent of run with identical (color, attrs) */
        while (i < len && x + run_chars < max_cols && cells[i].color_pair == run_color && cells[i].attrs == run_attrs)
        {
            int n;

            if (run_bytes + 4 >= (int)sizeof(run_buf))
                break; /* Don't overflow run buffer; emit and continue */

            n = wc_to_utf8_bytes(wcs[i], &run_buf[run_bytes]);
            run_bytes += n;
            run_chars++;
            i++;
        }

        if (run_chars == 0)
            break; /* safety */

        run_buf[run_bytes] = '\0';

        if (run_color >= 0)
            attron(COLOR_PAIR(run_color) | run_attrs);

        mvaddnstr(row_y, x, run_buf, run_bytes);

        if (run_color >= 0)
            attroff(COLOR_PAIR(run_color) | run_attrs);

        x += run_chars;
    }
}

static void draw_body(UiApp *app)
{
    int rows = LINES - 8;
    int i;
    const wchar_t *wcs;
    int wlen;

    if (rows < 1)
        return;

    rd_set_page(app->reader, rows);

    for (i = 0; i < rows; i++)
    {
        int vi = rd_top(app->reader) + i;
        int type, col;
        int ansi_color, ansi_attrs;
        const struct AnsiCell *cells;
        int row_y = 7 + i;
        const wchar_t *ln;
        int lnlen;

        /* Skip if terminal shrunk since rd_set_page */
        if (row_y < 0 || row_y >= LINES - 1)
            continue;

        move(row_y, 0);
        clrtoeol();

        if (vi >= rd_total(app->reader))
            continue;

        type = rd_get_type(app->reader, vi);
        ansi_color = rd_get_ansi_color(app->reader, vi);
        ansi_attrs = rd_get_ansi_attrs(app->reader, vi);
        cells = rd_get_ansi_cells(app->reader, vi);

        /* ANSI with per-cell attrs: draw via canvas (preserves BBS art colors) */
        if (rd_ansi_visible(app->reader) && cells)
        {
            ln = rd_get_line(app->reader, vi);
            lnlen = rd_get_len(app->reader, vi);

            if (ln && lnlen > 0)
                draw_ansi_line(row_y, ln, lnlen, cells, (COLS < 80) ? COLS : 80);

            continue; /* attrs handled inside draw_ansi_line */
        }

        if (rd_ansi_visible(app->reader) && ansi_color >= 0)
        {
            col = ansi_color;
            attron(COLOR_PAIR(col) | ansi_attrs);
        }
        else
        {
            col = ui_color_for_type(type);
            attron(COLOR_PAIR(col));
            ansi_attrs = 0;
        }

        /* Plain or ANSI without per-cell attrs: draw with line color */
        ln = rd_get_line(app->reader, vi);
        lnlen = rd_get_len(app->reader, vi);

        /* Skip leading ^A on kludge lines */
        if (ln && lnlen > 0 && (type & FTN_LT_KLUDGE) && ln[0] == L'\x01')
        {
            ln++;
            lnlen--;
        }

        if (ln && lnlen > 0)
            mvaddnwstr(row_y, 0, ln, lnlen);

        /* Highlight search matches (word-level) */
        if (app->reader_search.rows && app->reader_search.cols && app->reader_search.match_count > 0)
        {
            int line_idx = rd_get_line_idx(app->reader, vi);
            int j;

            if (line_idx >= 0)
            {
                for (j = 0; j < app->reader_search.match_count; j++)
                {
                    if (app->reader_search.rows[j] == line_idx)
                    {
                        int match_col = app->reader_search.cols[j];
                        int match_len = (int)wcslen(app->reader_search.query);

                        if (match_col >= 0 && match_col + match_len <= lnlen)
                        {
                            if (j == app->reader_search.current_match)
                                attron(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attron(COLOR_PAIR(COL_SEARCH_MATCH));

                            mvaddnwstr(row_y, match_col, &ln[match_col], match_len);

                            if (j == app->reader_search.current_match)
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH) | A_REVERSE);
                            else
                                attroff(COLOR_PAIR(COL_SEARCH_MATCH));
                        }
                    }
                }
            }
        }

        attroff(COLOR_PAIR(col) | ansi_attrs);
    }
}

/* Clear search state in reader */
static void reader_clear_search(UiApp *app)
{
    if (app->reader_search.rows)
    {
        free(app->reader_search.rows);
        app->reader_search.rows = NULL;
    }

    if (app->reader_search.cols)
    {
        free(app->reader_search.cols);
        app->reader_search.cols = NULL;
    }

    app->reader_search.match_count = 0;
    app->reader_search.is_mode = 0;
    app->reader_search.only_mode = 0;
    app->reader_search.current_match = 0;
    app->reader_search.match_current = 0;
}

/* Navigate to a raw msgs[] index, honouring the current sort order */
static int reader_goto_raw(UiApp *app, int raw_idx)
{
    UiSession *s = &app->sess;
    uint32_t target_mn;
    int i;

    if (raw_idx < 0 || raw_idx >= s->msg_count)
        return -1; /* invalid index */

    target_mn = s->msgs[raw_idx].msgnum;

    for (i = 0; i < s->order_count; i++)
    {
        if (s->msgs[s->order[i]].msgnum == target_mn)
        {
            s->msg_sel = i + 1;
            app->cur_msgnum = target_mn;

            return load_msg(app, target_mn);
        }
    }

    return -2;
}

/* Main loop */
UiView ui_reader_run(UiApp *app)
{
    int ch;
    UiSession *s;
    int saved_viewansi;

    if (!app)
        return VIEW_QUIT;

    s = &app->sess;
    saved_viewansi = app->cfg->viewansi;

    if (load_msg(app, app->cur_msgnum) != 0)
    {
        ui_status(app, "Cannot load message");
        app->cfg->viewansi = saved_viewansi;
        return reader_exit_view(app);
    }

    /* Apply ANSI visibility from config on first load */
    if (app->cfg->viewansi != rd_ansi_visible(app->reader))
    {
        rd_toggle_ansi(app->reader);

#ifdef PLATFORM_AMIGA
        amiga_change_font(rd_ansi_visible(app->reader));
#endif
    }

    for (;;)
    {
        erase();

        ui_draw_menubar(app, "Reader");
        draw_header_bar(app);
        draw_body(app);

        /* Status: msg_charset (CHRS or ?) -> decoded_charset (actual decode) */
        ui_status(app, "Charset: %s -> %s | c = Change charset%s | Ln %d/%d",
                  app->msg_charset[0] ? app->msg_charset : "?",
                  (app->view_charset[0]) ? app->decoded_charset : "Auto",
                  rd_ansi_visible(app->reader) ? " | ANSI" : "",
                  rd_top(app->reader) + 1, rd_total(app->reader));

        ui_draw_statusbar(app);
        refresh();

        ch = wrapper_getch();

        switch (ch)
        {
        case KEY_UP:
            rd_scroll_up(app->reader, 1);
            break;
        case KEY_DOWN:
            rd_scroll_down(app->reader, 1);
            break;
        case KEY_PPAGE:
        case 'b':
        case 'B':
        case CTRL('U'): /* Page Up (Amiga) */
            rd_page_up(app->reader);
            break;
        case KEY_NPAGE:
        case CTRL('D'): /* Page Down (Amiga) */
        case ' ':
            rd_page_down(app->reader);
            break;
        case KEY_HOME:
        case CTRL('B'): /* Home (Amiga) */
            rd_home(app->reader);
            break;
        case KEY_END:
        case CTRL('E'): /* End (Amiga) */
            rd_end(app->reader);
            break;

        case KEY_RIGHT:
        case 'n': /* Next message */
            if (s->msg_sel < s->order_count)
            {
                s->msg_sel++;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load next");
            }
            else
            {
                /* At last message: try to go to next area */
                try_goto_next_area(app);
            }
            break;
        case KEY_LEFT:
        case 'p': /* Previous message */
            if (s->msg_sel > 1)
            {
                s->msg_sel--;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load prev");
            }
            break;

        case CTRL('H'):
        case '<':
            /* Scroll to top of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) > 0)
                rd_scroll_up(app->reader, 1);

            break;

        case CTRL('K'):
        case '>':
            /* Scroll to bottom of message */
            rd_set_page(app->reader, LINES - 8);

            while (rd_top(app->reader) + rd_visible(app->reader) < rd_total(app->reader))
                rd_scroll_down(app->reader, 1);

            break;

        case ',':
            /* First message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = 1;
                app->cur_msgnum = s->msgs[s->order[0]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load first");
            }
            break;

        case '.':
            /* Last message in sort order */
            if (s->order_count > 0)
            {
                s->msg_sel = s->order_count;
                app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                if (load_msg(app, app->cur_msgnum) != 0)
                    ui_status(app, "Cannot load last");
            }
            break;
        case KEY_ALT('J'): /* Follow REPLY chain to original */
        case KEY_CLEFT:
        {
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int orig = (idx >= 0) ? ftn_find_original(s->msgs, s->msg_count, idx) : -1;

            /* Fallback: search by MSGID string from REPLY kludge */
            if (orig < 0 && app->cur_reply[0])
                orig = ftn_find_original_by_msgid(s->msgs, s->msg_count, app->cur_reply);

            if (orig < 0)
                ui_status(app, "No original message in this area");
            else if (orig >= s->msg_count)
                ui_status(app, "Invalid original message index");
            else if (s->msgs[orig].from[0] == '\0' || s->msgs[orig].subject[0] == '\0')
                ui_status(app, "Original message is empty or corrupted");
            else
            {
                int rc = reader_goto_raw(app, orig);

                if (rc == -2)
                    ui_status(app, "Original not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load original");
            }

            break;
        }
        case KEY_CRIGHT: /* Follow thread to reply (with picker if branched) */
        {
#define MAX_REPLIES 256
            int raw_replies[MAX_REPLIES];
            int idx = jam_find_by_msgnum(s->msgs, s->msg_count, app->cur_msgnum);
            int nreplies = (idx >= 0) ? ftn_find_all_replies(s->msgs, s->msg_count, idx, raw_replies, MAX_REPLIES) : 0;

            if (nreplies == 0)
            {
                ui_status(app, "No replies to this message");
            }
            else if (nreplies == 1)
            {
                int rc = reader_goto_raw(app, raw_replies[0]);
                if (rc == -2)
                    ui_status(app, "Reply not visible in current filter");
                else if (rc != 0)
                    ui_status(app, "Cannot load reply");
            }
            else
            {
                /* Multiple replies: build label list and show picker */
                const char *items[MAX_REPLIES];
                char labels[MAX_REPLIES][80];
                int i, choice;

                for (i = 0; i < nreplies; i++)
                {
                    const JamMsgInfo *m = &s->msgs[raw_replies[i]];

                    snprintf(labels[i], sizeof(labels[i]), "#%-5u  %-20.20s  %.28s", (unsigned)m->msgnum, m->from, m->subject);
                    items[i] = labels[i];
                }

                choice = ui_popup_list("Select reply", items, nreplies, 0);

                if (choice >= 0)
                {
                    int rc = reader_goto_raw(app, raw_replies[choice]);

                    if (rc == -2)
                        ui_status(app, "Reply not visible in current filter");
                    else if (rc != 0)
                        ui_status(app, "Cannot load reply");
                }
            }
#undef MAX_REPLIES
            break;
        }

        case 'c':
        case 'C':
        {
            char new_view[32], new_out[32];

            new_view[0] = '\0';
            new_out[0] = '\0';

            if (ui_popup_charset_pair(app->view_charset, app->edit_charset, CHARSET_READ_DEFAULT, app->cfg->charset, new_view, sizeof(new_view), new_out, sizeof(new_out)) == 0)
            {
                /* Empty string = Auto (CHARSET_READ_DEFAULT); load_msg treats "" as no override */
                strncpy(app->view_charset, new_view, sizeof(app->view_charset) - 1);
                app->view_charset[sizeof(app->view_charset) - 1] = '\0';

                strncpy(app->edit_charset, new_out, sizeof(app->edit_charset) - 1);
                app->edit_charset[sizeof(app->edit_charset) - 1] = '\0';

                /* Mark that user manually changed charset (from reader) - only if NOT Auto */
                if (new_out[0] != '\0')
                    app->edit_charset_manually_changed = 1;

                /* Reload with new charset; preserves headers, lastread, ANSI, kludges */
                load_msg(app, app->cur_msgnum);

                ui_status(app, "View: %s | Save: %s", new_view[0] ? new_view : CHARSET_READ_DEFAULT, new_out[0] ? new_out : (app->cfg->charset[0] ? app->cfg->charset : "UTF-8"));
            }

            break;
        }

        case 'k':
        case 'K':
            rd_toggle_kludges(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case 'v':
        case 'V':
            rd_toggle_hiddklud(app->reader);
            app->cfg->viewkludge = rd_kludges_visible(app->reader);
            ui_status(app, "Hidden+Kludges %s", rd_kludges_visible(app->reader) ? "visible" : "hidden");
            break;

        case KEY_F(5): /* Search in message body */
        case '/':
        {
            wchar_t tmp[64];
            int *rows = NULL, *cols = NULL;
            int match_count;
            int i;
            const char **contexts = NULL;
            char **context_bufs = NULL;
            int *line_nums = NULL;

            wcsncpy(tmp, app->reader_search.query, 63);
            tmp[63] = L'\0';

            if (ui_popup_input("Search", "Text:", tmp, 64) == 0 && tmp[0])
            {
                wcsncpy(app->reader_search.query, tmp, 63);
                app->reader_search.query[63] = L'\0';

                /* Find all matches in all lines (no limit) */
                match_count = rd_search_all(app->reader, app->reader_search.query, &rows, &cols);

                /* Free previous search matches */
                reader_clear_search(app);

                if (match_count == 0)
                {
                    ui_status(app, "Not found");
                }
                else if (match_count == 1)
                {
                    /* Single match: scroll to it */
                    rd_set_page(app->reader, LINES - 8);

                    if (rows[0] < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - rows[0]);
                    else if (rows[0] >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, rows[0] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Found at line %d", rows[0] + 1);

                    /* Save matches for highlighting and activate search mode */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
                else
                {
                    /* Allocate arrays for display */
                    contexts = (const char **)malloc((size_t)match_count * sizeof(const char *));
                    context_bufs = (char **)malloc((size_t)match_count * sizeof(char *));
                    line_nums = (int *)malloc((size_t)match_count * sizeof(int));

                    if (!contexts || !context_bufs || !line_nums)
                    {
                        if (contexts)
                            free(contexts);

                        if (context_bufs)
                            free(context_bufs);

                        if (line_nums)
                            free(line_nums);

                        free(rows);
                        free(cols);
                        ui_status(app, "Memory error");

                        break;
                    }

                    /* Multiple matches: show list */
                    for (i = 0; i < match_count; i++)
                    {
                        const wchar_t *line = rd_get_line(app->reader, rows[i]);
                        int line_len = rd_get_len(app->reader, rows[i]);

                        context_bufs[i] = (char *)malloc(128);

                        if (!context_bufs[i])
                        {
                            context_bufs[i] = NULL;
                            contexts[i] = "";
                        }
                        else
                        {
                            /* Convert context to UTF-8 for display (max 60 chars) */
                            if (line && line_len > 0)
                            {
                                int copy_len = line_len;

                                if (copy_len > 60)
                                    copy_len = 60;

                                char *utf8 = wcs_to_utf8(line, copy_len);

                                if (utf8)
                                {
                                    snprintf(context_bufs[i], 128, "%s", utf8);
                                    free(utf8);
                                }
                                else
                                {
                                    context_bufs[i][0] = '\0';
                                }
                            }
                            else
                            {
                                context_bufs[i][0] = '\0';
                            }
                        }

                        contexts[i] = context_bufs[i];
                        line_nums[i] = rows[i] + 1; /* 1-based for display */
                    }

                    /* Show popup with results */
                    int choice = ui_popup_search_results("Search Results", line_nums, contexts, match_count, 0);

                    if (choice >= 0)
                    {
                        rd_set_page(app->reader, LINES - 8);

                        if (rows[choice] < rd_top(app->reader))
                            rd_scroll_up(app->reader, rd_top(app->reader) - rows[choice]);
                        else if (rows[choice] >= rd_top(app->reader) + rd_visible(app->reader))
                            rd_scroll_down(app->reader, rows[choice] - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                        app->reader_search.current_match = choice;
                        app->reader_search.match_current = choice + 1;
                        ui_status(app, "Jumped to line %d", rows[choice] + 1);
                    }
                    else
                    {
                        ui_status(app, "Search cancelled");
                    }

                    /* Free allocated memory */
                    for (i = 0; i < match_count; i++)
                    {
                        if (context_bufs[i])
                            free(context_bufs[i]);
                    }

                    free(context_bufs);
                    free(contexts);
                    free(line_nums);

                    /* Save matches for highlighting and activate search mode (don't free rows/cols) */
                    app->reader_search.rows = rows;
                    app->reader_search.cols = cols;
                    app->reader_search.match_count = match_count;
                    app->reader_search.only_mode = 1;
                    app->reader_search.current_match = 0;
                    app->reader_search.match_current = 1;

                    continue; /* Force redraw */
                }
            }
            break;
        }

        case 'a':
        case 'A':
            rd_toggle_ansi(app->reader);
            app->cfg->viewansi = rd_ansi_visible(app->reader);
#ifdef PLATFORM_AMIGA
            amiga_change_font(rd_ansi_visible(app->reader));
#endif
            /* Reload message to apply/re-apply ANSI parsing with new font */
            load_msg(app, app->cur_msgnum);
            /* Continue to next iteration to redraw with new font */
            continue;

        case 'r':
        case 'R':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_reply(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'e':
        case 'E':
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_edit(app, app->cur_msgnum);
            return VIEW_EDITOR;

        case 'N':
        case KEY_IC: /* New message */
            reader_clear_search(app);
            app->edit_return_view = VIEW_READER;
            ui_editor_prep_new(app);
            return VIEW_EDITOR;

        case '\r':
        case '\n':
        case KEY_ENTER:
            /* Page down or advance to next message at bottom */
            if (rd_top(app->reader) + rd_visible(app->reader) >= rd_total(app->reader))
            {
                if (s->msg_sel < s->order_count)
                {
                    s->msg_sel++;
                    app->cur_msgnum = s->msgs[s->order[s->msg_sel - 1]].msgnum;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load next");
                }
                else
                {
                    /* At last message: try to go to next area */
                    try_goto_next_area(app);
                }
            }
            else
                rd_page_down(app->reader);
            break;

        case CTRL('G'): /* Goto line number */
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Line number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long line_num = 0;
                int target_line;

                if (u)
                {
                    line_num = strtol(u, NULL, 10);
                    free(u);
                }

                target_line = (int)(line_num - 1); /* Convert to 0-based */

                if (target_line >= 0 && target_line < rd_total(app->reader))
                {
                    rd_set_page(app->reader, LINES - 8);

                    /* Scroll to target line */
                    if (target_line < rd_top(app->reader))
                        rd_scroll_up(app->reader, rd_top(app->reader) - target_line);
                    else if (target_line >= rd_top(app->reader) + rd_visible(app->reader))
                        rd_scroll_down(app->reader, target_line - (rd_top(app->reader) + rd_visible(app->reader)) + 1);

                    ui_status(app, "Jumped to line %ld", line_num);
                }
                else
                {
                    ui_status(app, "Invalid line number");
                }
            }
            break;
        }

        case KEY_ALT('G'): /* Clear search highlights */
        {
            reader_clear_search(app);
            ui_status(app, "Search highlights cleared");

            continue; /* Force redraw */
        }

        /* Navigate to previous match in reader */
        case KEY_F(3):
        case KEY_ALT('P'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match - 1 + app->reader_search.match_count) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        /* Navigate to next match in reader */
        case KEY_F(4):
        case KEY_ALT('N'):
        {
            if (app->reader_search.rows && app->reader_search.match_count > 0)
            {
                int match_line;
                int visible_lines = rd_visible(app->reader);
                int center_offset = visible_lines / 2;

                app->reader_search.current_match = (app->reader_search.current_match + 1) % app->reader_search.match_count;
                app->reader_search.match_current = app->reader_search.current_match + 1;
                match_line = app->reader_search.rows[app->reader_search.current_match];

                /* Center the match on screen (always ensure visible like te) */
                int target_top = match_line - center_offset;

                if (target_top < 0)
                    target_top = 0;

                if (target_top > rd_top(app->reader))
                    rd_scroll_down(app->reader, target_top - rd_top(app->reader));
                else
                    rd_scroll_up(app->reader, rd_top(app->reader) - target_top);

                continue; /* Force redraw */
            }

            break;
        }

        case 'g': /* Goto message number */
        case 'G':
        {
            wchar_t wbuf[16];
            wbuf[0] = L'\0';

            if (ui_popup_input("Goto", "Message number:", wbuf, 16) == 0 && wbuf[0])
            {
                char *u = wcs_to_utf8(wbuf, (int)wcslen(wbuf));
                long mn = 0;

                if (u)
                {
                    mn = strtol(u, NULL, 10);
                    free(u);
                }

                int i, found = -1;

                for (i = 0; i < s->order_count; i++)
                {
                    if ((long)s->msgs[s->order[i]].msgnum == mn)
                    {
                        found = i;
                        break;
                    }
                }

                if (found >= 0)
                {
                    s->msg_sel = found + 1;
                    app->cur_msgnum = (uint32_t)mn;

                    if (load_msg(app, app->cur_msgnum) != 0)
                        ui_status(app, "Cannot load message");
                }
                else
                    ui_status(app, "No such message in this view");
            }

            break;
        }

        case 'w': /* Write message to text file */
        case 'W':
        case KEY_F(7):
        {
            char path[256];
            char out_cs[CHARSET_NAME_MAX];
            UiSession *s;
            char *body_utf8;
            int written;

            path[0] = '\0';
            out_cs[0] = '\0';

            snprintf(path, sizeof(path), "msg_%u.txt", (unsigned)app->cur_msgnum);

            /*if (ui_popup_input("Write message", "File path:", path, sizeof(path)) != 0 || !path[0])
                break;
            */

            if (ui_files_save("Write message", NULL, path, path, sizeof(path)) != 0)
                break;

            /* Charset of the output file. Empty == AUTO == write UTF-8
             * verbatim; the user can type "CP437", "LATIN-1", etc */
            if (ui_popup_charset("Output charset", "", out_cs, sizeof(out_cs)) != 0)
                break;

            s = &app->sess;
            body_utf8 = wrapper_read_utf8_ex(&s->jam, app->cur_msgnum, app->view_charset[0] ? app->view_charset : NULL, NULL, app->msg_charset, sizeof(app->msg_charset));

            if (!body_utf8)
            {
                ui_status(app, "Cannot read message body");
                break;
            }

            written = rd_export_to_file(app->reader, body_utf8, path, out_cs[0] ? out_cs : NULL);
            free(body_utf8);

            if (written < 0)
                ui_status(app, "Cannot write %s", path);
            else
                ui_status(app, "Wrote %d lines to %s (%s)", written, path, out_cs[0] ? out_cs : "UTF-8");

            break;
        }
        case 'd':
        case 'D':
        case KEY_DC: /* Delete message */
            if (ui_popup_confirm("Delete", "Mark this message as deleted?") == 1)
            {
                if (jam_lock(&s->jam, JAM_LOCK_RETRIES) == 0)
                {
                    if (jam_delete_msg(&s->jam, app->cur_msgnum) == 0)
                        ui_status(app, "Message deleted");

                    jam_unlock(&s->jam);
                    free(s->msgs);

                    s->msgs = NULL;
                    s->msgs = jam_load_headers(&s->jam, &s->msg_count, 0, (uint32_t)app->cfg->msglistmax);

                    if (!s->msgs)
                    {
                        s->msg_count = 0;
                        ui_status(app, "Reload failed after delete");
                    }
                    else
                    {
                        ui_session_rebuild_order(app);
                    }
                }
            }

            reader_save_lastread(app);

            /* reader_save_lastread() already updates area counts; skip redundant call */
            app->cfg->viewansi = saved_viewansi;

            return reader_exit_view(app);

            break;

        case KEY_F(1):
        case '?':
            ui_popup_help("Reader Help", READER_HELP, READER_HELP_N);
            break;

        case 'l':
        case 'L':
            /* Show msglist overlay; from search go to search browser instead */
            if (app->from_search)
            {
                reader_clear_search(app);
                app->cfg->viewansi = saved_viewansi;
                return VIEW_SEARCH_RESULTS;
            }

            reader_clear_search(app);
            reader_save_lastread(app);
            update_area_counts_in_memory(app);

            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 1;

            return VIEW_MSGLIST;

        case 27:
            /* ESC: exit search mode first, otherwise quit */
            if (app->reader_search.only_mode)
            {
                reader_clear_search(app);
                ui_status(app, "Search mode exited");

                continue; /* Force redraw */
            }
            /* Fall through to quit */
        case 'q':
        case 'Q':
            reader_save_lastread(app);
            update_area_counts_in_memory(app);
            app->cfg->viewansi = saved_viewansi;
            app->msglist_overlay_from_reader = 0;

            reader_clear_search(app);

            return reader_exit_view(app);

        case KEY_RESIZE:
            flushinp();
            load_msg(app, app->cur_msgnum);
            break;
        case CTRL('F'):
            ui_popup_freq(app);
            break;
        default:
            break;
        }
    }
}

