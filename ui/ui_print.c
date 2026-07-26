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
#include <time.h>

#include "te.h"
#include "ui_print.h"
#include "ui_hyph.h"
#include "../components/config.h"
#include "../components/print.h"

#ifdef HAVE_IPP
#include "../components/fmt_pdf.h"
#include "../components/fmt_pcl.h"
#include "../components/fmt_urf.h"
#include "../components/ipp.h"
#include "../components/print_amiga.h"
#include "../components/print_win32.h"
#include "../core/mdns.h"
#include "../core/printer_cache.h"
#endif

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

#ifdef HAVE_IPP

/* Recognize common PCL MIME types so one exporter handles them all */
static int fmt_is_pcl(const char *fmt)
{
    static const char *kinds[] =
        {
            "application/vnd.hp-PCL",
            "application/vnd.hp-PCL3",
            "application/vnd.hp-PCL3GUI",
            "application/vnd.hp-PCL5",
            "application/vnd.hp-PCL5e",
            "application/vnd.hp-PCL6",
            "application/x-pcl"};

    int i;
    int n;

    n = (int)(sizeof(kinds) / sizeof(kinds[0]));

    for (i = 0; i < n; i++)
    {
        if (strcasecmp(fmt, kinds[i]) == 0)
            return 1;
    }

    return 0;
}

/* Case-sensitive string match; empty strings never match */
static int wiz_selected(const char *cfg_val, const char *candidate)
{
    if (!cfg_val || !cfg_val[0] || !candidate)
        return 0;

    return strcmp(cfg_val, candidate) == 0;
}

/* Popup picker for string options with "(keep printer default)" first */
static int wiz_pick_string(const char *title, const char (*options)[64], int n_options, int default_idx, const char *cfg_val, char *out_val, size_t out_sz)
{
    const char *items[32];
    int initial = 0;
    int choice;
    int i;

    if (n_options < 0)
        n_options = 0;

    if (n_options + 1 > (int)(sizeof(items) / sizeof(items[0])))
        n_options = (int)(sizeof(items) / sizeof(items[0])) - 1;

    items[0] = "(keep printer default)";

    for (i = 0; i < n_options; i++)
        items[i + 1] = options[i];

    /* Preselect: match cfg_val first, else fall back to the printer default */
    if (cfg_val && cfg_val[0])
    {
        for (i = 0; i < n_options; i++)
        {
            if (wiz_selected(cfg_val, options[i]))
            {
                initial = i + 1;
                break;
            }
        }
    }
    else if (default_idx >= 0 && default_idx < n_options)
    {
        initial = default_idx + 1;
    }

    choice = ui_popup_list(title, items, n_options + 1, initial);

    if (choice < 0)
        return -1;

    if (choice == 0)
    {
        if (out_val && out_sz > 0)
            out_val[0] = '\0';

        return (cfg_val && cfg_val[0]) ? 1 : 0;
    }

    if (out_val && out_sz > 0)
    {
        strncpy(out_val, options[choice - 1], out_sz - 1);
        out_val[out_sz - 1] = '\0';
    }

    return wiz_selected(cfg_val, options[choice - 1]) ? 0 : 1;
}

/* Popup picker for print-quality enum (3/4/5) with human labels */
static int wiz_pick_quality(const int *values, int n_values, int default_idx, int cfg_val, int *out_val)
{
    static const char *labels[3] = {"draft", "normal", "high"};
    const char *items[4];
    int initial = 0;
    int choice;
    int i;

    if (n_values < 0)
        n_values = 0;

    if (n_values > 3)
        n_values = 3;

    items[0] = "(keep printer default)";

    for (i = 0; i < n_values; i++)
    {
        /* IPP enum: 3=draft 4=normal 5=high */
        if (values[i] >= 3 && values[i] <= 5)
            items[i + 1] = labels[values[i] - 3];
        else
            items[i + 1] = "?";
    }

    if (cfg_val > 0)
    {
        for (i = 0; i < n_values; i++)
        {
            if (values[i] == cfg_val)
            {
                initial = i + 1;
                break;
            }
        }
    }
    else if (default_idx >= 0 && default_idx < n_values)
    {
        initial = default_idx + 1;
    }

    choice = ui_popup_list("Quality", items, n_values + 1, initial);

    if (choice < 0)
        return -1;

    if (choice == 0)
    {
        *out_val = 0;
        return (cfg_val > 0) ? 1 : 0;
    }

    *out_val = values[choice - 1];

    return (values[choice - 1] == cfg_val) ? 0 : 1;
}

/* Copies picker with preset ladder, bounded by printer's max copies-supported */
static int wiz_pick_copies(int max_copies, int cfg_val, int *out_val)
{
    static const int presets[6] = {1, 2, 3, 5, 10, 20};
    const char *items[8];
    char labels[8][8];
    int n_choices = 0;
    int initial = 0;
    int choice;
    int i;

    items[n_choices++] = "(keep printer default)";

    for (i = 0; i < (int)(sizeof(presets) / sizeof(presets[0])); i++)
    {
        if (max_copies > 0 && presets[i] > max_copies)
            break;

        snprintf(labels[n_choices], sizeof(labels[n_choices]), "%d", presets[i]);

        items[n_choices] = labels[n_choices];
        n_choices++;
    }

    if (cfg_val > 0)
    {
        for (i = 0; i < (int)(sizeof(presets) / sizeof(presets[0])); i++)
        {
            if (presets[i] == cfg_val && i + 1 < n_choices)
            {
                initial = i + 1;
                break;
            }
        }
    }

    choice = ui_popup_list("Copies", items, n_choices, initial);

    if (choice < 0)
        return -1;

    if (choice == 0)
    {
        *out_val = 0;
        return (cfg_val > 0) ? 1 : 0;
    }

    *out_val = presets[choice - 1];

    return (presets[choice - 1] == cfg_val) ? 0 : 1;
}

/* Store into cfg only when the wizard reported a change. Sum into dirty */
static void wiz_commit_string(char *dst, size_t dst_sz, const char *src, int changed, int *dirty)
{
    if (!changed)
        return;

    strncpy(dst, src ? src : "", dst_sz - 1);
    dst[dst_sz - 1] = '\0';

    *dirty = 1;
}

/* Discover mDNS printers on the LAN and merge into cache; refreshes timestamp */
static int wiz_discover_into_cache(TeApp *app, PrinterCache *pc)
{
    MdnsPrinter net[MDNS_MAX_RESULTS];
    char uri[256];
    char kind[8];
    int n_net;
    int n_local = 0;
    int added = 0;
    int i;

#if defined(PLATFORM_WIN32)
    char lp_names[16][64];
#elif defined(PLATFORM_AMIGA)
    AmigaPrinterPrefs prefs;
#else
    char cups_queues[32][128];
    FILE *pp = NULL;
    char line[256];
    int qlen;
    const char *q = NULL;
#endif

    te_status(app, "Discovering printers...");

    n_net = mdns_discover_printers(net, MDNS_MAX_RESULTS, 2000);

    for (i = 0; i < n_net; i++)
    {
        strcpy(kind, net[i].is_tls ? "ipps" : "ipp");
        snprintf(uri, sizeof(uri), "%s://%s:%d/%s", kind, net[i].host, net[i].port > 0 ? net[i].port : 631, net[i].path[0] ? net[i].path : "ipp/print");

        if (printer_cache_add(pc, net[i].name, uri, kind))
            added++;
    }

#if defined(PLATFORM_WIN32)
    n_local = win32_list_printers(lp_names, 16);

    for (i = 0; i < n_local; i++)
    {
        snprintf(uri, sizeof(uri), "local:%s", lp_names[i]);

        if (printer_cache_add(pc, lp_names[i], uri, "local"))
            added++;
    }
#elif defined(PLATFORM_AMIGA)
    if (amiga_read_printer_prefs(&prefs) == 0 && prefs.driver[0])
    {
        snprintf(uri, sizeof(uri), "local:%s", prefs.driver);

        if (printer_cache_add(pc, prefs.driver, uri, "local"))
            added++;

        n_local = 1;
    }
#else
    /* Unix: enumerate all CUPS queues via lpstat -e, fall back to $PRINTER/$LPDEST */
    pp = popen("lpstat -e 2>/dev/null", "r");

    if (pp)
    {
        while (n_local < (int)(sizeof(cups_queues) / sizeof(cups_queues[0])) && fgets(line, sizeof(line), pp))
        {
            qlen = (int)strlen(line);

            while (qlen > 0 && (line[qlen - 1] == '\n' || line[qlen - 1] == '\r' || line[qlen - 1] == ' ' || line[qlen - 1] == '\t'))
                line[--qlen] = '\0';

            if (qlen <= 0)
                continue;

            strncpy(cups_queues[n_local], line, sizeof(cups_queues[0]) - 1);
            cups_queues[n_local][sizeof(cups_queues[0]) - 1] = '\0';

            n_local++;
        }

        pclose(pp);
    }

    /* Fall back to $PRINTER / $LPDEST if lpstat found nothing */
    if (n_local == 0)
    {
        q = getenv("PRINTER");

        if (!q || !q[0])
            q = getenv("LPDEST");

        if (q && q[0])
        {
            strncpy(cups_queues[0], q, sizeof(cups_queues[0]) - 1);
            cups_queues[0][sizeof(cups_queues[0]) - 1] = '\0';
            n_local = 1;
        }
    }

    for (i = 0; i < n_local; i++)
    {
        snprintf(uri, sizeof(uri), "ipp://localhost:631/printers/%s", cups_queues[i]);

        if (printer_cache_add(pc, cups_queues[i], uri, "local"))
            added++;
    }
#endif

    pc->last_discovery = (long)time(NULL);
    printer_cache_save(pc);

    te_status(app, "Discovered: %d network, %d local (+%d new)", n_net, n_local, added);
    return added;
}

/* Present unified printer selector; fills out_uri + out_kind on success. Returns 1 chosen, 0 canceled */
static int wiz_pick_printer(TeApp *app, TeConfig *cfg, PrinterCache *pc, char *out_uri, size_t uri_sz, char *out_kind, size_t kind_sz, char *out_name, size_t name_sz)
{
    const char *items[PCACHE_MAX + 5];
    char labels[PCACHE_MAX][160];
    char title[96];
    char age[32];
    int n;
    int idx_discover;
    int idx_manual;
    int idx_clear;
    int idx_cancel;
    int choice;
    int i;

    for (;;)
    {
        n = 0;

        for (i = 0; i < pc->count; i++)
        {
            snprintf(labels[i], sizeof(labels[i]), "%s  (%s)", pc->entries[i].name, pc->entries[i].kind);
            items[n++] = labels[i];
        }

        idx_discover = n;
        items[n++] = "Discover printers now...";

        idx_manual = n;
        items[n++] = "Add manually (host + queue)...";

        idx_clear = n;
        items[n++] = pc->count > 0 ? "Clear cache" : "Clear cache (empty)";

        idx_cancel = n;
        items[n++] = "Cancel";

        printer_cache_pretty_age(pc, age, sizeof(age));
        snprintf(title, sizeof(title), "Select printer  (cache: %s)", age);

        choice = ui_popup_list(title, items, n, 0);

        if (choice < 0 || choice == idx_cancel)
            return 0;

        if (choice == idx_discover)
        {
            wiz_discover_into_cache(app, pc);
            continue;
        }

        if (choice == idx_clear)
        {
            printer_cache_clear(pc);
            printer_cache_save(pc);

            te_status(app, "Printer cache cleared");
            continue;
        }

        if (choice == idx_manual)
        {
            char host[TE_CFG_STR_MAX];
            char queue[TE_CFG_STR_MAX];
            char nu[256];
            const char *proto_items[2];
            int is_tls;

            proto_items[0] = "IPP";
            proto_items[1] = "IPPS";

            is_tls = ui_popup_list("Protocol", proto_items, 2, 0) == 1;

            if (!ui_print_ask_ipp(cfg, is_tls ? "Add IPPS printer" : "Add IPP printer", host, sizeof(host), queue, sizeof(queue)))
                continue;

            snprintf(nu, sizeof(nu), "%s://%s:631/%s", is_tls ? "ipps" : "ipp", host, queue);

            if (printer_cache_add(pc, host, nu, is_tls ? "ipps" : "ipp"))
                printer_cache_save(pc);

            /* Fall through by picking the entry we just added */
            for (i = 0; i < pc->count; i++)
            {
                if (strcmp(pc->entries[i].uri, nu) == 0)
                {
                    choice = i;
                    break;
                }
            }

            if (i >= pc->count)
                continue;
        }

        /* choice now indexes an entry in pc->entries */
        strncpy(out_uri, pc->entries[choice].uri, uri_sz - 1);
        out_uri[uri_sz - 1] = '\0';

        strncpy(out_kind, pc->entries[choice].kind, kind_sz - 1);
        out_kind[kind_sz - 1] = '\0';

        strncpy(out_name, pc->entries[choice].name, name_sz - 1);
        out_name[name_sz - 1] = '\0';

        return 1;
    }
}

/* Format one IPP orientation enum as a human label */
static const char *wiz_orient_label(int enum_val)
{
    switch (enum_val)
    {
    case 3:
        return "Portrait";
    case 4:
        return "Landscape";
    case 5:
        return "Reverse landscape";
    case 6:
        return "Reverse portrait";
    default:
        return "?";
    }
}

/* Walk the media/sides/color/quality/copies sub-menus after info is populated */
static int wiz_run_submenus(TeApp *app, TeConfig *cfg, const IppPrinterInfo *info, const char *uri)
{
    char pick[64];
    int changed;
    int dirty = 0;
    const char *ident = NULL;
    const char *st = NULL;
    int cp;

    /* Show identity + state + queue length before options so the user has context */
    ident = info->info[0] ? info->info : (info->make_and_model[0] ? info->make_and_model : info->printer_name);
    st = info->state == 3 ? "idle" : (info->state == 4 ? "processing" : (info->state == 5 ? "stopped" : "unknown"));

    if (info->state_message[0])
        te_status(app, "%s [%s]  %s%s", ident, st, info->state_message, info->queued_job_count > 0 ? "" : "");
    else if (info->location[0])
        te_status(app, "%s [%s]  location: %s  queue: %d", ident, st, info->location, info->queued_job_count > 0 ? info->queued_job_count : 0);
    else
        te_status(app, "%s [%s]  queue: %d", ident, st, info->queued_job_count > 0 ? info->queued_job_count : 0);

    /* Refuse to configure if the printer explicitly says it will not accept jobs */
    if (info->accepting_jobs == 0)
    {
        te_status(app, "Printer is not accepting jobs (%s)", info->state_reason[0] ? info->state_reason : "unknown");
        return 0;
    }

    if (info->n_formats > 0)
    {
        pick[0] = '\0';
        changed = wiz_pick_string("Document format", info->formats, info->n_formats, -1, cfg->print_document_format, pick, sizeof(pick));

        if (changed < 0)
            return 0;

        wiz_commit_string(cfg->print_document_format, sizeof(cfg->print_document_format), pick, changed, &dirty);
    }

    if (info->n_media > 0)
    {
        pick[0] = '\0';
        changed = wiz_pick_string("Media", info->media, info->n_media, info->default_media, cfg->print_media, pick, sizeof(pick));

        if (changed < 0)
            return 0;

        wiz_commit_string(cfg->print_media, sizeof(cfg->print_media), pick, changed, &dirty);
    }

    if (info->n_sides > 0)
    {
        pick[0] = '\0';
        changed = wiz_pick_string("Sides", info->sides, info->n_sides, info->default_sides, cfg->print_sides, pick, sizeof(pick));

        if (changed < 0)
            return 0;

        wiz_commit_string(cfg->print_sides, sizeof(cfg->print_sides), pick, changed, &dirty);
    }

    if (info->n_color_modes > 0)
    {
        pick[0] = '\0';
        changed = wiz_pick_string("Color mode", info->color_modes, info->n_color_modes, info->default_color_mode, cfg->print_color_mode, pick, sizeof(pick));

        if (changed < 0)
            return 0;

        wiz_commit_string(cfg->print_color_mode, sizeof(cfg->print_color_mode), pick, changed, &dirty);
    }

    if (info->n_qualities > 0)
    {
        int q = cfg->print_quality;

        changed = wiz_pick_quality(info->qualities, info->n_qualities, info->default_quality, cfg->print_quality, &q);

        if (changed < 0)
            return 0;

        if (changed)
        {
            cfg->print_quality = q;
            dirty = 1;
        }
    }

    cp = cfg->print_copies;

    changed = wiz_pick_copies(info->max_copies, cfg->print_copies, &cp);

    if (changed < 0)
        return 0;

    if (changed)
    {
        cfg->print_copies = cp;
        dirty = 1;
    }

    /* Orientation: labels mapped to IPP enum values 3/4/5/6 */
    if (info->n_orientations > 0)
    {
        const char *labels[IPP_MAX_ORIENTATIONS];
        char buf[IPP_MAX_ORIENTATIONS][IPP_STR_MAX];
        char store_str[16];
        char pick2[32];
        int i;
        int cur_idx = -1;
        int changed2;

        for (i = 0; i < info->n_orientations; i++)
        {
            snprintf(buf[i], sizeof(buf[i]), "%s", wiz_orient_label(info->orientations[i]));

            labels[i] = buf[i];

            if (info->orientations[i] == cfg->print_orientation)
                cur_idx = i;
        }

        store_str[0] = '\0';

        if (cur_idx >= 0)
            snprintf(store_str, sizeof(store_str), "%s", labels[cur_idx]);

        pick2[0] = '\0';
        changed2 = wiz_pick_string("Orientation", (const char (*)[64])buf, info->n_orientations, info->default_orientation, store_str, pick2, sizeof(pick2));

        if (changed2 < 0)
            return 0;

        if (changed2 > 0)
        {
            for (i = 0; i < info->n_orientations; i++)
            {
                if (strcmp(labels[i], pick2) == 0)
                {
                    cfg->print_orientation = info->orientations[i];
                    dirty = 1;
                    break;
                }
            }
        }
    }

    /* Number-up: choices come in as ints, present as strings */
    if (info->n_number_ups > 0)
    {
        char buf[IPP_MAX_NUMBER_UP][IPP_STR_MAX];
        char store_str[16];
        char pick2[32];
        int i;
        int cur_idx = -1;
        int changed2;

        for (i = 0; i < info->n_number_ups; i++)
        {
            snprintf(buf[i], sizeof(buf[i]), "%d", info->number_ups[i]);

            if (info->number_ups[i] == cfg->print_number_up)
                cur_idx = i;
        }

        store_str[0] = '\0';

        if (cur_idx >= 0)
            snprintf(store_str, sizeof(store_str), "%d", cfg->print_number_up);

        pick2[0] = '\0';
        changed2 = wiz_pick_string("Pages per sheet", (const char (*)[64])buf, info->n_number_ups, info->default_number_up, store_str, pick2, sizeof(pick2));

        if (changed2 < 0)
            return 0;

        if (changed2 > 0)
            cfg->print_number_up = atoi(pick2);

        if (changed2 > 0)
            dirty = 1;
    }

    /* Media source (paper tray) */
    if (info->n_media_sources > 0)
    {
        pick[0] = '\0';
        changed = wiz_pick_string("Paper source", info->media_sources, info->n_media_sources, info->default_media_source, cfg->print_media_source, pick, sizeof(pick));

        if (changed < 0)
            return 0;

        wiz_commit_string(cfg->print_media_source, sizeof(cfg->print_media_source), pick, changed, &dirty);
    }

    /* Media type (paper kind) */
    if (info->n_media_types > 0)
    {
        pick[0] = '\0';
        changed = wiz_pick_string("Paper type", info->media_types, info->n_media_types, info->default_media_type, cfg->print_media_type, pick, sizeof(pick));

        if (changed < 0)
            return 0;

        wiz_commit_string(cfg->print_media_type, sizeof(cfg->print_media_type), pick, changed, &dirty);
    }

    /* Resolution: build labels from the tuple set */
    if (info->n_resolutions > 0)
    {
        char buf[IPP_MAX_RESOLUTIONS][IPP_STR_MAX];
        char store_str[32];
        char pick2[32];
        int i;
        int cur_idx = -1;
        int changed2;

        for (i = 0; i < info->n_resolutions; i++)
        {
            const IppResolution *r = &info->resolutions[i];
            const char *unit = r->units == 3 ? "dpi" : (r->units == 4 ? "dpc" : "?");

            if (r->cross_feed == r->feed)
                snprintf(buf[i], sizeof(buf[i]), "%d %s", r->cross_feed, unit);
            else
                snprintf(buf[i], sizeof(buf[i]), "%d x %d %s", r->cross_feed, r->feed, unit);

            if (r->cross_feed == cfg->print_resolution_x && r->feed == cfg->print_resolution_y && r->units == cfg->print_resolution_units)
                cur_idx = i;
        }

        store_str[0] = '\0';

        if (cur_idx >= 0)
            snprintf(store_str, sizeof(store_str), "%s", buf[cur_idx]);

        pick2[0] = '\0';
        changed2 = wiz_pick_string("Resolution", (const char (*)[64])buf, info->n_resolutions, info->default_resolution, store_str, pick2, sizeof(pick2));

        if (changed2 < 0)
            return 0;

        if (changed2 > 0)
        {
            for (i = 0; i < info->n_resolutions; i++)
            {
                if (strcmp(buf[i], pick2) == 0)
                {
                    cfg->print_resolution_x = info->resolutions[i].cross_feed;
                    cfg->print_resolution_y = info->resolutions[i].feed;
                    cfg->print_resolution_units = info->resolutions[i].units;

                    dirty = 1;
                    break;
                }
            }
        }
    }

    if (dirty && app->cfg_path[0])
        te_cfg_save(cfg, app->cfg_path);

    te_status(app, "Options saved for %s", info->printer_name[0] ? info->printer_name : uri);

    return 1;
}

/* Full wizard: pick a target, resolve URI, query attributes, walk options */
void ui_editor_print_options(TeApp *app)
{
    IppPrinterInfo info;
    PrinterCache pc;
    TeConfig *cfg = NULL;
    char uri[TE_CFG_STR_MAX * 2];
    char kind[16];
    char printer_name[96];
    char err[256];
    int rc;

    if (!app)
        return;

    cfg = &app->cfg;

    /* Cache-driven selector: cached entries + discover + manual + clear */
    memset(&pc, 0, sizeof(pc));
    printer_cache_load(&pc);

    if (!wiz_pick_printer(app, cfg, &pc, uri, sizeof(uri), kind, sizeof(kind), printer_name, sizeof(printer_name)))
        return;

    /* Update last_choice from picked entry kind */
    if (strcmp(kind, "ipps") == 0)
        cfg->print_last_choice = 2;
    else if (strcmp(kind, "ipp") == 0)
        cfg->print_last_choice = 1;
    else
        cfg->print_last_choice = 0;

    /* Amiga local target uses Prefs, not IPP; handled here and returns early */
#if defined(PLATFORM_AMIGA)
    if (strcmp(kind, "local") == 0)
    {
        AmigaPrinterPrefs prefs;
        int mapped_qual;
        const char *mapped_paper = NULL;
        const char *mapped_shade = NULL;
        int changed_any = 0;

        memset(&prefs, 0, sizeof(prefs));

        if (amiga_read_printer_prefs(&prefs) != 0)
        {
            te_status(app, "Options: cannot read printer.prefs (Prefs never saved?)");
            return;
        }

        mapped_paper = amiga_map_paper_to_ipp(prefs.paper_size);
        mapped_qual = amiga_map_quality_to_ipp(prefs.quality);
        mapped_shade = amiga_map_shade_to_ipp(prefs.shade);

        te_status(app, "Prefs: driver=%s paper=%s quality=%d shade=%s", prefs.driver[0] ? prefs.driver : "(none)", mapped_paper[0] ? mapped_paper : "?", mapped_qual, mapped_shade[0] ? mapped_shade : "?");

        if (mapped_paper[0] && strcmp(cfg->print_media, mapped_paper) != 0)
        {
            strncpy(cfg->print_media, mapped_paper, sizeof(cfg->print_media) - 1);
            cfg->print_media[sizeof(cfg->print_media) - 1] = '\0';

            changed_any = 1;
        }

        if (mapped_qual > 0 && cfg->print_quality != mapped_qual)
        {
            cfg->print_quality = mapped_qual;
            changed_any = 1;
        }

        if (mapped_shade[0] && strcmp(cfg->print_color_mode, mapped_shade) != 0)
        {
            strncpy(cfg->print_color_mode, mapped_shade, sizeof(cfg->print_color_mode) - 1);
            cfg->print_color_mode[sizeof(cfg->print_color_mode) - 1] = '\0';

            changed_any = 1;
        }

        if (changed_any && app->cfg_path[0])
            te_cfg_save(cfg, app->cfg_path);

        return;
    }
#endif

    /* Windows local target: use WinSpool caps directly, no IPP query */
#if defined(PLATFORM_WIN32)
    if (strcmp(kind, "local") == 0)
    {
        Win32PrinterInfo w;
        const char *nm = uri;
        int i;

        if (strncmp(uri, "local:", 6) == 0)
            nm = uri + 6;

        strncpy(cfg->print_local_name, nm, sizeof(cfg->print_local_name) - 1);
        cfg->print_local_name[sizeof(cfg->print_local_name) - 1] = '\0';

        if (win32_get_printer_info(nm, &w) != 0)
        {
            te_status(app, "Options: cannot query Windows printer %s", nm);
            return;
        }

        /* Adapt Win32PrinterInfo -> IppPrinterInfo so the sub-menus below reuse */
        memset(&info, 0, sizeof(info));

        strncpy(info.printer_name, w.printer_name, sizeof(info.printer_name) - 1);

        for (i = 0; i < w.n_media && i < (int)(sizeof(info.media) / sizeof(info.media[0])); i++)
        {
            strncpy(info.media[i], w.media[i], sizeof(info.media[0]) - 1);
            info.media[i][sizeof(info.media[0]) - 1] = '\0';
        }

        info.n_media = w.n_media;
        info.default_media = w.default_media;

        for (i = 0; i < w.n_sides; i++)
        {
            strncpy(info.sides[i], w.sides[i], sizeof(info.sides[0]) - 1);
            info.sides[i][sizeof(info.sides[0]) - 1] = '\0';
        }

        info.n_sides = w.n_sides;
        info.default_sides = w.default_sides;

        for (i = 0; i < w.n_color_modes; i++)
        {
            strncpy(info.color_modes[i], w.color_modes[i], sizeof(info.color_modes[0]) - 1);
            info.color_modes[i][sizeof(info.color_modes[0]) - 1] = '\0';
        }

        info.n_color_modes = w.n_color_modes;
        info.default_color_mode = w.default_color_mode;

        for (i = 0; i < w.n_qualities; i++)
            info.qualities[i] = w.qualities[i];

        info.n_qualities = w.n_qualities;
        info.default_quality = w.default_quality;

        info.max_copies = w.max_copies;

        wiz_run_submenus(app, cfg, &info, uri);

        te_status(app, "Note: on Windows, options apply from the print dialog, not from tinyedit");

        return;
    }
#endif

    /* Unix local (or fallback): CUPS on localhost with the queue from URI */
    if (strcmp(kind, "local") == 0)
    {
        if (strncmp(uri, "local:", 6) == 0)
        {
            char q[64];

            strncpy(q, uri + 6, sizeof(q) - 1);
            q[sizeof(q) - 1] = '\0';

            snprintf(uri, sizeof(uri), "ipp://localhost:631/printers/%s", q);
        }

        strncpy(cfg->print_local_name, printer_name, sizeof(cfg->print_local_name) - 1);
        cfg->print_local_name[sizeof(cfg->print_local_name) - 1] = '\0';
    }
    else
    {
        /* Extract host + queue from the picked IPP URI and remember them in cfg */
        const char *scheme_end = strstr(uri, "://");

        if (scheme_end)
        {
            const char *host_start = scheme_end + 3;
            const char *slash = strchr(host_start, '/');
            const char *colon = strchr(host_start, ':');
            size_t host_len;

            if (colon && (!slash || colon < slash))
                host_len = (size_t)(colon - host_start);
            else if (slash)
                host_len = (size_t)(slash - host_start);
            else
                host_len = strlen(host_start);

            if (host_len >= sizeof(cfg->print_ipp_host))
                host_len = sizeof(cfg->print_ipp_host) - 1;

            memcpy(cfg->print_ipp_host, host_start, host_len);
            cfg->print_ipp_host[host_len] = '\0';

            if (slash)
            {
                strncpy(cfg->print_ipp_queue, slash + 1, sizeof(cfg->print_ipp_queue) - 1);
                cfg->print_ipp_queue[sizeof(cfg->print_ipp_queue) - 1] = '\0';
            }
        }
    }

    /* Query printer capabilities */
    te_status(app, "Querying %s ...", uri);

    err[0] = '\0';

    rc = ipp_get_printer_info(uri, &info, err, sizeof(err));

    if (rc != 0)
    {
        /* For local printers, save the queue name so direct printing still works */
        if (strcmp(kind, "local") == 0 && cfg->print_local_name[0])
        {
            if (app->cfg_path[0])
                te_cfg_save(cfg, app->cfg_path);

            te_status(app, "Local queue '%s' saved (IPP query failed: %s). Use Print > Local printer.", cfg->print_local_name, err[0] ? err : "no response");
        }
        else
        {
            te_status(app, "Options: %s", err[0] ? err : "printer did not respond");
        }

        return;
    }

    wiz_run_submenus(app, cfg, &info, uri);
}

#endif /* HAVE_IPP */

void ui_editor_print(TeApp *app)
{
    const char *items[4];
    int choice;
    int rc;
    int dirty;
    char err[256];
    char warn[256];
    char host[TE_CFG_STR_MAX];
    char queue[TE_CFG_STR_MAX];
    Ed *ed = NULL;
    TeConfig *cfg = NULL;

#ifdef HAVE_IPP
    /* Render PDF to memory buffer, then post to printer */
    FILE *mem = NULL;
    unsigned char *pdf_buf = NULL;
    size_t pdf_len = 0;
    int prc;
    char uri[TE_CFG_STR_MAX * 2];
    const char *q = NULL;
    int port;
    IppJobAttrs attrs;
    const char *filename = NULL;
    const char *jobn = NULL;
    char doc_fmt[TE_CFG_STR_MAX];
    LayoutHyphenFn hy = NULL;
    void *hy_user = NULL;
#endif

    if (!app)
        return;

    ed = te_app_get_editor(app);

    if (!ed)
        return;

    cfg = &app->cfg;

    items[0] = "Local printer";
    items[1] = "Network (IPP)";
    items[2] = "Network (IPPS)";
    items[3] = "Configure printer options...";

    choice = ui_popup_list("Print", items, 4, cfg->print_last_choice);

    if (choice < 0 || choice > 3)
        return;

    /* Options wizard branch: no print attempt, returns early */
#ifdef HAVE_IPP
    if (choice == 3)
    {
        ui_editor_print_options(app);
        return;
    }
#else
    if (choice == 3)
    {
        te_status(app, "Options wizard requires network printing (USE_IPP=1)");
        return;
    }
#endif

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
        LayoutHyphenFn hy = NULL;
        void *hy_user = NULL;

        if (dirty && app->cfg_path[0])
            te_cfg_save(cfg, app->cfg_path);

#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
        if (app->hyph_wrap_enabled && app->hyph_handle)
        {
            hy = ui_layout_hyphen;
            hy_user = app;
        }
#endif

        rc = te_print_document_ex(ed, cfg, app->charset_out, te_app_get_filename(app), hy, hy_user, err, sizeof(err), warn, sizeof(warn));

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

#ifdef HAVE_IPP

    /* Compose URI. Bare queue name gets the CUPS /printers/ prefix */
    q = cfg->print_ipp_queue;
    port = cfg->print_ipp_port > 0 ? cfg->print_ipp_port : 631;

    if (!q[0])
        snprintf(uri, sizeof(uri), "%s://%s:%d/ipp/print", choice == 2 ? "ipps" : "ipp", cfg->print_ipp_host, port);
    else if (q[0] == '/')
        snprintf(uri, sizeof(uri), "%s://%s:%d%s", choice == 2 ? "ipps" : "ipp", cfg->print_ipp_host, port, q);
    else if (strchr(q, '/'))
        snprintf(uri, sizeof(uri), "%s://%s:%d/%s", choice == 2 ? "ipps" : "ipp", cfg->print_ipp_host, port, q);
    else
        snprintf(uri, sizeof(uri), "%s://%s:%d/printers/%s", choice == 2 ? "ipps" : "ipp", cfg->print_ipp_host, port, q);

    /* Job attributes from config. Empty strings / 0 mean "use printer default" */
    memset(&attrs, 0, sizeof(attrs));

    attrs.media = cfg->print_media[0] ? cfg->print_media : NULL;
    attrs.sides = cfg->print_sides[0] ? cfg->print_sides : NULL;
    attrs.color_mode = cfg->print_color_mode[0] ? cfg->print_color_mode : NULL;
    attrs.quality = cfg->print_quality;
    attrs.copies = cfg->print_copies;
    attrs.orientation = cfg->print_orientation;
    attrs.number_up = cfg->print_number_up;
    attrs.media_source = cfg->print_media_source[0] ? cfg->print_media_source : NULL;
    attrs.media_type = cfg->print_media_type[0] ? cfg->print_media_type : NULL;
    attrs.resolution_x = cfg->print_resolution_x;
    attrs.resolution_y = cfg->print_resolution_y;
    attrs.resolution_units = cfg->print_resolution_units;

    /* Use current filename basename as job name for meaningful queue label */
    filename = te_app_get_filename(app);
    jobn = NULL;

    if (filename && filename[0])
    {
        const char *slash = NULL;

#if defined(PLATFORM_WIN32) || defined(PLATFORM_AMIGA)
        const char *bs = NULL;
#endif

        slash = strrchr(filename, '/');

#if defined(PLATFORM_WIN32) || defined(PLATFORM_AMIGA)
        bs = strrchr(filename, '\\');

        if (bs && (!slash || bs > slash))
            slash = bs;
#endif

        jobn = slash ? slash + 1 : filename;
    }

    /* Pick document format before generating anything */
    doc_fmt[0] = '\0';

    if (cfg->print_document_format[0])
    {
        strncpy(doc_fmt, cfg->print_document_format, sizeof(doc_fmt) - 1);
        doc_fmt[sizeof(doc_fmt) - 1] = '\0';
    }
    else
    {
        IppPrinterInfo probe;
        char probe_err[256];
        int i;

        memset(&probe, 0, sizeof(probe));
        probe_err[0] = '\0';

        if (ipp_get_printer_info(uri, &probe, probe_err, sizeof(probe_err)) == 0)
        {
            for (i = 0; i < probe.n_formats; i++)
            {
                if (strcmp(probe.formats[i], "application/pdf") == 0)
                {
                    strncpy(doc_fmt, probe.formats[i], sizeof(doc_fmt) - 1);
                    doc_fmt[sizeof(doc_fmt) - 1] = '\0';
                    break;
                }
            }

            if (!doc_fmt[0])
            {
                for (i = 0; i < probe.n_formats; i++)
                {
                    if (fmt_is_pcl(probe.formats[i]))
                    {
                        strncpy(doc_fmt, probe.formats[i], sizeof(doc_fmt) - 1);
                        doc_fmt[sizeof(doc_fmt) - 1] = '\0';
                        break;
                    }
                }
            }

            if (!doc_fmt[0] && probe.n_formats > 0)
            {
                strncpy(doc_fmt, probe.formats[0], sizeof(doc_fmt) - 1);
                doc_fmt[sizeof(doc_fmt) - 1] = '\0';
            }
        }

        if (!doc_fmt[0])
        {
            strncpy(doc_fmt, "application/pdf", sizeof(doc_fmt) - 1);
            doc_fmt[sizeof(doc_fmt) - 1] = '\0';
        }
    }

    if (strcmp(doc_fmt, "application/pdf") != 0 && !fmt_is_pcl(doc_fmt) && strcmp(doc_fmt, "image/urf") != 0)
    {
        te_status(app, "Cannot print: printer wants %s, tinyedit only generates application/pdf, PCL and URF", doc_fmt);
        return;
    }

#if defined(PLATFORM_UNIX)
    mem = open_memstream((char **)&pdf_buf, &pdf_len);
#else
    mem = tmpfile();
#endif

    if (!mem)
    {
        te_status(app, "Print failed: cannot buffer document");
        return;
    }

#if defined(HAVE_HUNSPELL) && defined(HAVE_HYPHEN)
    if (app->hyph_wrap_enabled && app->hyph_handle)
    {
        hy = ui_layout_hyphen;
        hy_user = app;
    }
#endif

    if (strcmp(doc_fmt, "application/pdf") == 0)
        prc = pdf_export_ex(ed, mem, cfg, hy, hy_user, err, sizeof(err), warn, sizeof(warn));
    else if (strcmp(doc_fmt, "image/urf") == 0)
        prc = urf_export_ex(ed, mem, cfg, hy, hy_user, err, sizeof(err), warn, sizeof(warn));
    else
        prc = pcl_export_ex(ed, mem, cfg, hy, hy_user, err, sizeof(err), warn, sizeof(warn));

#if defined(PLATFORM_UNIX)
    fclose(mem);
#else

    if (prc == 0)
    {
        long sz;

        fflush(mem);
        fseek(mem, 0, SEEK_END);
        sz = ftell(mem);

        if (sz > 0)
        {
            pdf_buf = (unsigned char *)malloc((size_t)sz);

            if (pdf_buf)
            {
                fseek(mem, 0, SEEK_SET);

                if (fread(pdf_buf, 1, (size_t)sz, mem) == (size_t)sz)
                    pdf_len = (size_t)sz;
                else
                {
                    free(pdf_buf);
                    pdf_buf = NULL;
                }
            }
        }
    }

    fclose(mem);
#endif

    if (prc != 0 || !pdf_buf || pdf_len == 0)
    {
        free(pdf_buf);

        te_status(app, "Print failed: %s", err[0] ? err : "document export failed");
        return;
    }

    err[0] = '\0';

    rc = ipp_print_document(uri, jobn, doc_fmt, &attrs, pdf_buf, pdf_len, err, sizeof(err));

    free(pdf_buf);

    if (rc == 0)
        te_status(app, "Sent to %s", uri);
    else
        te_status(app, "Print failed: %s", err[0] ? err : "unknown error");
#else
    te_status(app, "%s to %s/%s: network printing not compiled in", choice == 2 ? "IPPS" : "IPP", cfg->print_ipp_host, cfg->print_ipp_queue);
#endif
}
