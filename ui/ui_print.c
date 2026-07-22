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

/* Print popup dialog: local spooler, or IPP/IPPS network target */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "te.h"
#include "ui_print.h"
#include "../components/config.h"
#include "../components/print.h"

/* wchar_t buffer to ASCII, ignoring non-ASCII codepoints */
static void wcs_to_ascii(const wchar_t *w, char *out, size_t outsz)
{
    size_t i;
    size_t n;

    n = 0;

    for (i = 0; w[i] && n + 1 < outsz; i++)
    {
        if (w[i] < 0x80)
            out[n++] = (char)w[i];
    }

    out[n] = '\0';
}

/* char buffer to wchar_t (ASCII source) */
static void ascii_to_wcs(const char *s, wchar_t *out, size_t outcnt)
{
    size_t i;
    size_t n;

    n = 0;

    for (i = 0; s[i] && n + 1 < outcnt; i++)
        out[n++] = (wchar_t)(unsigned char)s[i];

    out[n] = L'\0';
}

/* Ask for IPP host and queue, return 1 when confirmed */
static int ui_print_ask_ipp(const TeConfig *cfg, const char *title, char *host, size_t hostsz, char *queue, size_t queuesz)
{
    wchar_t wbuf[192];

    ascii_to_wcs(cfg->print_ipp_host, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));

    if (ui_popup_input_wcs(title, "Host or IP:", wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) != 0)
        return 0;

    if (!wbuf[0])
        return 0;

    wcs_to_ascii(wbuf, host, hostsz);

    ascii_to_wcs(cfg->print_ipp_queue, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));

    if (ui_popup_input_wcs(title, "Queue name:", wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0]))) != 0)
        return 0;

    if (!wbuf[0])
        return 0;

    wcs_to_ascii(wbuf, queue, queuesz);

    return 1;
}

void ui_editor_print(TeApp *app)
{
    const char *items[3];
    int choice;
    int rc;
    int dirty;
    char err[256];
    char warn[256];
    char host[TE_CFG_STR_MAX];
    char queue[TE_CFG_STR_MAX];
    Ed *ed = NULL;
    TeConfig *cfg = NULL;

    if (!app)
        return;

    ed = te_app_get_editor(app);

    if (!ed)
        return;

    cfg = &app->cfg;

    items[0] = "Local printer";
    items[1] = "Network (IPP)";
    items[2] = "Network (IPPS)";

    choice = ui_popup_list("Print", items, 3, cfg->print_last_choice);

    if (choice < 0 || choice > 2)
        return;

    /* Track whether any config field actually changed so we only write the config file when we have something new to persist */
    dirty = 0;

    if (choice != cfg->print_last_choice)
    {
        cfg->print_last_choice = choice;
        dirty = 1;
    }

    err[0] = '\0';
    warn[0] = '\0';

    if (choice == 0)
    {
        if (dirty && app->cfg_path[0])
            te_cfg_save(cfg, app->cfg_path);

        rc = te_print_document(ed, cfg, app->charset_out, err, sizeof(err), warn, sizeof(warn));

        if (rc == 0)
            te_status(app, warn[0] ? warn : "Sent to printer");
        else
            te_status(app, "Print failed: %s", err[0] ? err : "unknown error");

        return;
    }

    /* IPP / IPPS */
    host[0] = '\0';
    queue[0] = '\0';

    if (!ui_print_ask_ipp(cfg, choice == 2 ? "Print via IPPS" : "Print via IPP", host, sizeof(host), queue, sizeof(queue)))
    {
        /* User cancelled the host/queue prompt. If the destination choice itself changed, still persist that so next time defaults to it */
        if (dirty && app->cfg_path[0])
            te_cfg_save(cfg, app->cfg_path);

        return;
    }

    if (strcmp(cfg->print_ipp_host, host) != 0)
    {
        strncpy(cfg->print_ipp_host, host, sizeof(cfg->print_ipp_host) - 1);

        cfg->print_ipp_host[sizeof(cfg->print_ipp_host) - 1] = '\0';
        dirty = 1;
    }

    if (strcmp(cfg->print_ipp_queue, queue) != 0)
    {
        strncpy(cfg->print_ipp_queue, queue, sizeof(cfg->print_ipp_queue) - 1);

        cfg->print_ipp_queue[sizeof(cfg->print_ipp_queue) - 1] = '\0';
        dirty = 1;
    }

    if (dirty && app->cfg_path[0])
        te_cfg_save(cfg, app->cfg_path);

    /* Network path lands here once ipp_client.c is wired in; for now report */
    te_status(app, "%s to %s/%s: network printing not yet available", choice == 2 ? "IPPS" : "IPP", cfg->print_ipp_host, cfg->print_ipp_queue);
}
