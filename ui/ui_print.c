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

static void wiz_profile_load(TeConfig *cfg, const PrinterProfile *profile)
{
    if (!cfg || !profile)
        return;

    strncpy(cfg->print_document_format, profile->document_format, sizeof(cfg->print_document_format) - 1);
    strncpy(cfg->print_media, profile->media, sizeof(cfg->print_media) - 1);
    strncpy(cfg->print_sides, profile->sides, sizeof(cfg->print_sides) - 1);
    strncpy(cfg->print_color_mode, profile->color_mode, sizeof(cfg->print_color_mode) - 1);
    strncpy(cfg->print_media_source, profile->media_source, sizeof(cfg->print_media_source) - 1);
    strncpy(cfg->print_media_type, profile->media_type, sizeof(cfg->print_media_type) - 1);

    if (profile->font_path[0])
    {
        strncpy(cfg->print_font_path, profile->font_path, sizeof(cfg->print_font_path) - 1);
        cfg->print_font_path[sizeof(cfg->print_font_path) - 1] = '\0';
    }

    cfg->print_document_format[sizeof(cfg->print_document_format) - 1] = '\0';
    cfg->print_media[sizeof(cfg->print_media) - 1] = '\0';
    cfg->print_sides[sizeof(cfg->print_sides) - 1] = '\0';
    cfg->print_color_mode[sizeof(cfg->print_color_mode) - 1] = '\0';
    cfg->print_media_source[sizeof(cfg->print_media_source) - 1] = '\0';
    cfg->print_media_type[sizeof(cfg->print_media_type) - 1] = '\0';
    cfg->print_quality = profile->quality;
    cfg->print_copies = profile->copies;
    cfg->print_orientation = profile->orientation;
    cfg->print_number_up = profile->number_up;
    cfg->print_resolution_x = profile->resolution_x;
    cfg->print_resolution_y = profile->resolution_y;
    cfg->print_resolution_units = profile->resolution_units;

    if (profile->font_size > 0)
        cfg->print_font_size = profile->font_size;
}

static void wiz_profile_save(PrinterProfile *profile, const TeConfig *cfg)
{
    if (!profile || !cfg)
        return;

    strncpy(profile->document_format, cfg->print_document_format, sizeof(profile->document_format) - 1);
    strncpy(profile->media, cfg->print_media, sizeof(profile->media) - 1);
    strncpy(profile->sides, cfg->print_sides, sizeof(profile->sides) - 1);
    strncpy(profile->color_mode, cfg->print_color_mode, sizeof(profile->color_mode) - 1);
    strncpy(profile->media_source, cfg->print_media_source, sizeof(profile->media_source) - 1);
    strncpy(profile->media_type, cfg->print_media_type, sizeof(profile->media_type) - 1);
    strncpy(profile->font_path, cfg->print_font_path, sizeof(profile->font_path) - 1);

    profile->document_format[sizeof(profile->document_format) - 1] = '\0';
    profile->media[sizeof(profile->media) - 1] = '\0';
    profile->sides[sizeof(profile->sides) - 1] = '\0';
    profile->color_mode[sizeof(profile->color_mode) - 1] = '\0';
    profile->media_source[sizeof(profile->media_source) - 1] = '\0';
    profile->media_type[sizeof(profile->media_type) - 1] = '\0';
    profile->font_path[sizeof(profile->font_path) - 1] = '\0';
    profile->quality = cfg->print_quality;
    profile->copies = cfg->print_copies;
    profile->orientation = cfg->print_orientation;
    profile->number_up = cfg->print_number_up;
    profile->resolution_x = cfg->print_resolution_x;
    profile->resolution_y = cfg->print_resolution_y;
    profile->resolution_units = cfg->print_resolution_units;
    profile->font_size = cfg->print_font_size;
}

/* Discover mDNS printers on the LAN and merge into cache; refreshes timestamp */
static int wiz_discover_into_cache(TeApp *app, PrinterCache *pc, int *pcache_dirty)
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

    if (pcache_dirty)
        *pcache_dirty = 1;

    te_status(app, "Discovered: %d network, %d local (+%d new)", n_net, n_local, added);

    return added;
}

/* Present unified printer selector; fills out_uri + out_kind on success. Returns 1 chosen, 0 canceled */
static int wiz_pick_printer(TeApp *app, TeConfig *cfg, PrinterCache *pc, char *out_uri, size_t uri_sz, char *out_kind, size_t kind_sz, char *out_name, size_t name_sz, int *pcache_dirty)
{
    const char *items[PCACHE_MAX + 5];
    static char labels[PCACHE_MAX][160];
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
            const PrinterProfile *profile = &pc->entries[i].profile;

            if (profile->media[0])
                snprintf(labels[i], sizeof(labels[i]), "%s  (%s, %s)", pc->entries[i].name, pc->entries[i].kind, profile->media);
            else
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
            wiz_discover_into_cache(app, pc, pcache_dirty);
            continue;
        }

        if (choice == idx_clear)
        {
            if (pc->count > 0)
            {
                printer_cache_clear(pc);

                if (pcache_dirty)
                    *pcache_dirty = 1;
            }

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
            {
                if (pcache_dirty)
                    *pcache_dirty = 1;
            }

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

        wiz_profile_load(cfg, &pc->entries[choice].profile);

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

static void wiz_make_label(char *out, size_t outsz, const char *name, const char *cur, const char *def)
{
    if (cur && cur[0])
        snprintf(out, outsz, "%s: %s", name, cur);
    else if (def && def[0])
        snprintf(out, outsz, "%s: %s (default)", name, def);
    else
        snprintf(out, outsz, "%s: (printer default)", name);
}

static const char *wiz_quality_label(int q)
{
    switch (q)
    {
    case 3:
        return "draft";
    case 4:
        return "normal";
    case 5:
        return "high";
    default:
        return "?";
    }
}

static void wiz_resolution_str(const IppResolution *r, char *out, size_t outsz)
{
    const char *unit = r->units == 3 ? "dpi" : (r->units == 4 ? "dpc" : "?");

    if (r->cross_feed == r->feed)
        snprintf(out, outsz, "%d %s", r->cross_feed, unit);
    else
        snprintf(out, outsz, "%d x %d %s", r->cross_feed, r->feed, unit);
}

static int wiz_edit_string_attr(const char *title, const char (*options)[64], int n_options, int default_idx, const char *cfg_val, char *dst, size_t dst_sz, int *dirty)
{
    char pick[64];
    int changed;

    pick[0] = '\0';

    changed = wiz_pick_string(title, options, n_options, default_idx, cfg_val, pick, sizeof(pick));

    if (changed < 0)
        return -1;

    wiz_commit_string(dst, dst_sz, pick, changed, dirty);

    return changed;
}

static int wiz_edit_quality(const int *values, int n_values, int default_idx, int cfg_val, int *dst, int *dirty)
{
    int out;
    int changed;

    changed = wiz_pick_quality(values, n_values, default_idx, cfg_val, &out);

    if (changed < 0)
        return -1;

    if (changed)
    {
        *dst = out;
        *dirty = 1;
    }

    return changed;
}

static int wiz_edit_copies(int max_copies, int cfg_val, int *dst, int *dirty)
{
    int out;
    int changed;

    changed = wiz_pick_copies(max_copies, cfg_val, &out);

    if (changed < 0)
        return -1;

    if (changed)
    {
        *dst = out;
        *dirty = 1;
    }

    return changed;
}

static int wiz_edit_orientation(const IppPrinterInfo *info, int cfg_val, int *dst, int *dirty)
{
    const char *labels[IPP_MAX_ORIENTATIONS];
    char buf[IPP_MAX_ORIENTATIONS][IPP_STR_MAX];
    char store_str[16];
    char pick[32];
    int cur_idx = -1;
    int changed;
    int i;

    for (i = 0; i < info->n_orientations; i++)
    {
        snprintf(buf[i], sizeof(buf[i]), "%s", wiz_orient_label(info->orientations[i]));

        labels[i] = buf[i];

        if (info->orientations[i] == cfg_val)
            cur_idx = i;
    }

    store_str[0] = '\0';

    if (cur_idx >= 0)
        snprintf(store_str, sizeof(store_str), "%s", labels[cur_idx]);

    pick[0] = '\0';

    changed = wiz_pick_string("Orientation", (const char (*)[64])buf, info->n_orientations, info->default_orientation, store_str, pick, sizeof(pick));

    if (changed < 0)
        return -1;

    if (changed > 0)
    {
        if (pick[0] == '\0')
        {
            *dst = 0;
            *dirty = 1;
        }
        else
        {
            for (i = 0; i < info->n_orientations; i++)
            {
                if (strcmp(labels[i], pick) == 0)
                {
                    *dst = info->orientations[i];
                    *dirty = 1;
                    break;
                }
            }
        }
    }

    return changed;
}

static int wiz_edit_number_up(const IppPrinterInfo *info, int cfg_val, int *dst, int *dirty)
{
    char buf[IPP_MAX_NUMBER_UP][IPP_STR_MAX];
    char store_str[16];
    char pick[32];
    int cur_idx = -1;
    int changed;
    int i;

    for (i = 0; i < info->n_number_ups; i++)
    {
        snprintf(buf[i], sizeof(buf[i]), "%d", info->number_ups[i]);

        if (info->number_ups[i] == cfg_val)
            cur_idx = i;
    }

    store_str[0] = '\0';

    if (cur_idx >= 0)
        snprintf(store_str, sizeof(store_str), "%d", cfg_val);

    pick[0] = '\0';

    changed = wiz_pick_string("Pages per sheet", (const char (*)[64])buf, info->n_number_ups, info->default_number_up, store_str, pick, sizeof(pick));

    if (changed < 0)
        return -1;

    if (changed > 0)
    {
        if (pick[0] == '\0')
        {
            *dst = 0;
            *dirty = 1;
        }
        else
        {
            *dst = atoi(pick);
            *dirty = 1;
        }
    }

    return changed;
}

static int wiz_edit_resolution(const IppPrinterInfo *info, int cfg_x, int cfg_y, int cfg_units, int *dst_x, int *dst_y, int *dst_units, int *dirty)
{
    char buf[IPP_MAX_RESOLUTIONS][IPP_STR_MAX];
    char store_str[32];
    char pick[32];
    int cur_idx = -1;
    int changed;
    int i;

    for (i = 0; i < info->n_resolutions; i++)
    {
        const IppResolution *r = &info->resolutions[i];

        wiz_resolution_str(r, buf[i], sizeof(buf[i]));

        if (r->cross_feed == cfg_x && r->feed == cfg_y && r->units == cfg_units)
            cur_idx = i;
    }

    store_str[0] = '\0';

    if (cur_idx >= 0)
        snprintf(store_str, sizeof(store_str), "%s", buf[cur_idx]);

    pick[0] = '\0';

    changed = wiz_pick_string("Resolution", (const char (*)[64])buf, info->n_resolutions, info->default_resolution, store_str, pick, sizeof(pick));

    if (changed < 0)
        return -1;

    if (changed > 0)
    {
        if (pick[0] == '\0')
        {
            *dst_x = 0;
            *dst_y = 0;
            *dst_units = 0;
            *dirty = 1;
        }
        else
        {
            for (i = 0; i < info->n_resolutions; i++)
            {
                if (strcmp(buf[i], pick) == 0)
                {
                    *dst_x = info->resolutions[i].cross_feed;
                    *dst_y = info->resolutions[i].feed;
                    *dst_units = info->resolutions[i].units;
                    *dirty = 1;
                    break;
                }
            }
        }
    }

    return changed;
}

static int wiz_run_submenus(TeApp *app, TeConfig *cfg, const IppPrinterInfo *info, const char *uri)
{
    static TeConfig work;
    int dirty = 0;
    const char *ident = NULL;
    const char *st = NULL;
    char title[128];
    char labels[13][96];
    const char *items[14];
    int idx_fmt = -1;
    int idx_media = -1;
    int idx_sides = -1;
    int idx_color = -1;
    int idx_quality = -1;
    int idx_copies = -1;
    int idx_orient = -1;
    int idx_number_up = -1;
    int idx_source = -1;
    int idx_type = -1;
    int idx_resolution = -1;
    int idx_back = -1;
    int idx_save = -1;
    int n = 0;
    int choice = -1;
    int initial = 0;
    int changed = 0;
    char curbuf[8];
    char defbuf[8];
    char curres[32];
    char defres[32];
    IppResolution cur_r;
    const char *cur = NULL;
    const char *def = NULL;

    ident = info->info[0] ? info->info : (info->make_and_model[0] ? info->make_and_model : info->printer_name);
    st = info->state == 3 ? "idle" : (info->state == 4 ? "processing" : (info->state == 5 ? "stopped" : "unknown"));

    if (info->state_message[0])
        te_status(app, "%s [%s]  %s%s", ident, st, info->state_message, info->queued_job_count > 0 ? "" : "");
    else if (info->location[0])
        te_status(app, "%s [%s]  location: %s  queue: %d", ident, st, info->location, info->queued_job_count > 0 ? info->queued_job_count : 0);
    else
        te_status(app, "%s [%s]  queue: %d", ident, st, info->queued_job_count > 0 ? info->queued_job_count : 0);

    if (info->accepting_jobs == 0)
    {
        te_status(app, "Printer is not accepting jobs (%s)", info->state_reason[0] ? info->state_reason : "unknown");
        return 0;
    }

    work = *cfg;

    for (;;)
    {
        idx_fmt = -1;
        idx_media = -1;
        idx_sides = -1;
        idx_color = -1;
        idx_quality = -1;
        idx_copies = -1;
        idx_orient = -1;
        idx_number_up = -1;
        idx_source = -1;
        idx_type = -1;
        idx_resolution = -1;
        idx_back = -1;
        idx_save = -1;
        n = 0;

        if (info->n_formats > 0)
        {
            idx_fmt = n;
            cur = work.print_document_format[0] ? work.print_document_format : NULL;

            wiz_make_label(labels[n], sizeof(labels[n]), "Document format", cur, NULL);

            items[n] = labels[n];
            n++;
        }

        if (info->n_media > 0)
        {
            idx_media = n;
            cur = work.print_media[0] ? work.print_media : NULL;
            def = (info->default_media >= 0 && info->default_media < info->n_media) ? info->media[info->default_media] : NULL;

            wiz_make_label(labels[n], sizeof(labels[n]), "Media", cur, def);

            items[n] = labels[n];
            n++;
        }

        if (info->n_sides > 0)
        {
            idx_sides = n;
            cur = work.print_sides[0] ? work.print_sides : NULL;
            def = (info->default_sides >= 0 && info->default_sides < info->n_sides) ? info->sides[info->default_sides] : NULL;

            wiz_make_label(labels[n], sizeof(labels[n]), "Sides", cur, def);

            items[n] = labels[n];
            n++;
        }

        if (info->n_color_modes > 0)
        {
            idx_color = n;
            cur = work.print_color_mode[0] ? work.print_color_mode : NULL;
            def = (info->default_color_mode >= 0 && info->default_color_mode < info->n_color_modes) ? info->color_modes[info->default_color_mode] : NULL;

            wiz_make_label(labels[n], sizeof(labels[n]), "Color mode", cur, def);

            items[n] = labels[n];
            n++;
        }

        if (info->n_qualities > 0)
        {
            idx_quality = n;
            cur = work.print_quality > 0 ? wiz_quality_label(work.print_quality) : NULL;
            def = (info->default_quality >= 0 && info->default_quality < info->n_qualities) ? wiz_quality_label(info->qualities[info->default_quality]) : NULL;

            wiz_make_label(labels[n], sizeof(labels[n]), "Quality", cur, def);

            items[n] = labels[n];
            n++;
        }

        idx_copies = n;
        curbuf[0] = '\0';
        defbuf[0] = '\0';

        if (work.print_copies > 0)
            snprintf(curbuf, sizeof(curbuf), "%d", work.print_copies);

        if (info->default_copies > 0)
            snprintf(defbuf, sizeof(defbuf), "%d", info->default_copies);

        wiz_make_label(labels[n], sizeof(labels[n]), "Copies", curbuf[0] ? curbuf : NULL, defbuf[0] ? defbuf : NULL);

        items[n] = labels[n];
        n++;

        if (info->n_orientations > 0)
        {
            idx_orient = n;
            cur = work.print_orientation > 0 ? wiz_orient_label(work.print_orientation) : NULL;
            def = (info->default_orientation >= 0 && info->default_orientation < info->n_orientations) ? wiz_orient_label(info->orientations[info->default_orientation]) : NULL;

            wiz_make_label(labels[n], sizeof(labels[n]), "Orientation", cur, def);

            items[n] = labels[n];
            n++;
        }

        if (info->n_number_ups > 0)
        {
            idx_number_up = n;
            curbuf[0] = '\0';
            defbuf[0] = '\0';

            if (work.print_number_up > 0)
                snprintf(curbuf, sizeof(curbuf), "%d", work.print_number_up);

            if (info->default_number_up >= 0 && info->default_number_up < info->n_number_ups)
                snprintf(defbuf, sizeof(defbuf), "%d", info->number_ups[info->default_number_up]);

            wiz_make_label(labels[n], sizeof(labels[n]), "Pages per sheet", curbuf[0] ? curbuf : NULL, defbuf[0] ? defbuf : NULL);

            items[n] = labels[n];
            n++;
        }

        if (info->n_media_sources > 0)
        {
            idx_source = n;
            cur = work.print_media_source[0] ? work.print_media_source : NULL;
            def = (info->default_media_source >= 0 && info->default_media_source < info->n_media_sources) ? info->media_sources[info->default_media_source] : NULL;

            wiz_make_label(labels[n], sizeof(labels[n]), "Paper source", cur, def);

            items[n] = labels[n];
            n++;
        }

        if (info->n_media_types > 0)
        {
            idx_type = n;
            cur = work.print_media_type[0] ? work.print_media_type : NULL;
            def = (info->default_media_type >= 0 && info->default_media_type < info->n_media_types) ? info->media_types[info->default_media_type] : NULL;

            wiz_make_label(labels[n], sizeof(labels[n]), "Paper type", cur, def);

            items[n] = labels[n];
            n++;
        }

        if (info->n_resolutions > 0)
        {
            idx_resolution = n;
            curres[0] = '\0';
            defres[0] = '\0';

            if (work.print_resolution_x > 0 && work.print_resolution_y > 0 && work.print_resolution_units > 0)
            {
                cur_r.cross_feed = work.print_resolution_x;
                cur_r.feed = work.print_resolution_y;
                cur_r.units = work.print_resolution_units;

                wiz_resolution_str(&cur_r, curres, sizeof(curres));
            }
            if (info->default_resolution >= 0 && info->default_resolution < info->n_resolutions)
                wiz_resolution_str(&info->resolutions[info->default_resolution], defres, sizeof(defres));

            wiz_make_label(labels[n], sizeof(labels[n]), "Resolution", curres[0] ? curres : NULL, defres[0] ? defres : NULL);

            items[n] = labels[n];
            n++;
        }

        idx_back = n;
        items[n] = "Back";
        n++;

        idx_save = n;
        items[n] = "Save";
        n++;

        snprintf(title, sizeof(title), "Options for %s", ident);

        initial = 0;

        choice = ui_popup_list(title, items, n, initial);

        if (choice < 0 || choice == idx_back)
            return 0;

        if (choice == idx_save)
        {
            if (dirty)
            {
                *cfg = work;

                if (app->cfg_path[0])
                    te_cfg_save(cfg, app->cfg_path);
            }

            te_status(app, "Options saved for %s", info->printer_name[0] ? info->printer_name : uri);

            return 1;
        }

        if (choice == idx_fmt)
        {
            changed = wiz_edit_string_attr("Document format", info->formats, info->n_formats, -1, work.print_document_format, work.print_document_format, sizeof(work.print_document_format), &dirty);
        }
        else if (choice == idx_media)
        {
            changed = wiz_edit_string_attr("Media", info->media, info->n_media, info->default_media, work.print_media, work.print_media, sizeof(work.print_media), &dirty);
        }
        else if (choice == idx_sides)
        {
            changed = wiz_edit_string_attr("Sides", info->sides, info->n_sides, info->default_sides, work.print_sides, work.print_sides, sizeof(work.print_sides), &dirty);
        }
        else if (choice == idx_color)
        {
            changed = wiz_edit_string_attr("Color mode", info->color_modes, info->n_color_modes, info->default_color_mode, work.print_color_mode, work.print_color_mode, sizeof(work.print_color_mode), &dirty);
        }
        else if (choice == idx_quality)
        {
            changed = wiz_edit_quality(info->qualities, info->n_qualities, info->default_quality, work.print_quality, &work.print_quality, &dirty);
        }
        else if (choice == idx_copies)
        {
            changed = wiz_edit_copies(info->max_copies, work.print_copies, &work.print_copies, &dirty);
        }
        else if (choice == idx_orient)
        {
            changed = wiz_edit_orientation(info, work.print_orientation, &work.print_orientation, &dirty);
        }
        else if (choice == idx_number_up)
        {
            changed = wiz_edit_number_up(info, work.print_number_up, &work.print_number_up, &dirty);
        }
        else if (choice == idx_source)
        {
            changed = wiz_edit_string_attr("Paper source", info->media_sources, info->n_media_sources, info->default_media_source, work.print_media_source, work.print_media_source, sizeof(work.print_media_source), &dirty);
        }
        else if (choice == idx_type)
        {
            changed = wiz_edit_string_attr("Paper type", info->media_types, info->n_media_types, info->default_media_type, work.print_media_type, work.print_media_type, sizeof(work.print_media_type), &dirty);
        }
        else if (choice == idx_resolution)
        {
            changed = wiz_edit_resolution(info, work.print_resolution_x, work.print_resolution_y, work.print_resolution_units, &work.print_resolution_x, &work.print_resolution_y, &work.print_resolution_units, &dirty);
        }

        if (changed < 0)
            continue;
    }
}

/* Full wizard: pick a target, resolve URI, query attributes, walk options */
void ui_editor_print_options(TeApp *app)
{
    static IppPrinterInfo info;
    static PrinterCache pc;
    PrinterCacheEntry *entry = NULL;
    TeConfig *cfg = NULL;
    char uri[TE_CFG_STR_MAX * 2];
    char kind[16];
    char printer_name[96];
    char err[256];
    int rc;
    int pcache_dirty = 0;

    if (!app)
        return;

    cfg = &app->cfg;

    /* Cache-driven selector: cached entries + discover + manual + clear */
    memset(&pc, 0, sizeof(pc));

    printer_cache_load(&pc);

    if (!wiz_pick_printer(app, cfg, &pc, uri, sizeof(uri), kind, sizeof(kind), printer_name, sizeof(printer_name), &pcache_dirty))
    {
        if (pcache_dirty)
            printer_cache_save(&pc);

        return;
    }

    entry = printer_cache_find(&pc, uri);

    /* Update last_choice from picked entry kind */
    if (strcmp(kind, "ipps") == 0)
        cfg->print_last_choice = 2;
    else if (strcmp(kind, "ipp") == 0)
        cfg->print_last_choice = 1;
    else
        cfg->print_last_choice = 0;

    /* Amiga local target uses Prefs; present editable attributes and write back */
#if defined(PLATFORM_AMIGA)
    if (strcmp(kind, "local") == 0)
    {
        AmigaPrinterPrefs prefs;
        int mapped_qual;
        const char *mapped_paper = NULL;
        const char *mapped_shade = NULL;
        int i;
        int j;
        int rc;

        memset(&prefs, 0, sizeof(prefs));
        memset(&info, 0, sizeof(info));

        if (amiga_read_printer_prefs(&prefs) != 0)
        {
            te_status(app, "Options: cannot read printer.prefs (Prefs never saved?)");
            return;
        }

        mapped_paper = amiga_map_paper_to_ipp(prefs.paper_size);
        mapped_qual = amiga_map_quality_to_ipp(prefs.quality);
        mapped_shade = amiga_map_shade_to_ipp(prefs.shade);

        if (mapped_paper[0] && strcmp(cfg->print_media, mapped_paper) != 0)
        {
            strncpy(cfg->print_media, mapped_paper, sizeof(cfg->print_media) - 1);
            cfg->print_media[sizeof(cfg->print_media) - 1] = '\0';
        }

        if (mapped_qual > 0 && cfg->print_quality != mapped_qual)
            cfg->print_quality = mapped_qual;

        if (mapped_shade[0] && strcmp(cfg->print_color_mode, mapped_shade) != 0)
        {
            strncpy(cfg->print_color_mode, mapped_shade, sizeof(cfg->print_color_mode) - 1);
            cfg->print_color_mode[sizeof(cfg->print_color_mode) - 1] = '\0';
        }

        /* Amiga Prefs defines paper sizes 0..14. Load every enum that maps to an IPP keyword */
        for (i = 0; i <= 14; i++)
        {
            const char *ipp = amiga_map_paper_to_ipp(i);

            if (ipp && ipp[0] && info.n_media < (int)(sizeof(info.media) / sizeof(info.media[0])))
            {
                strncpy(info.media[info.n_media], ipp, sizeof(info.media[0]) - 1);
                info.media[info.n_media][sizeof(info.media[0]) - 1] = '\0';

                if (i == prefs.paper_size)
                    info.default_media = info.n_media;

                info.n_media++;
            }
        }

        /* Amiga Prefs only knows one-sided */
        strncpy(info.sides[0], "one-sided", sizeof(info.sides[0]) - 1);
        info.sides[0][sizeof(info.sides[0]) - 1] = '\0';

        info.n_sides = 1;
        info.default_sides = 0;

        /* Amiga Prefs defines shades 0..3. Load unique IPP keywords */
        for (i = 0; i <= 3; i++)
        {
            const char *ipp = amiga_map_shade_to_ipp(i);
            int duplicate = 0;

            if (!ipp || !ipp[0])
                continue;

            for (j = 0; j < info.n_color_modes; j++)
            {
                if (strcmp(info.color_modes[j], ipp) == 0)
                {
                    duplicate = 1;
                    break;
                }
            }

            if (duplicate)
                continue;

            if (info.n_color_modes < (int)(sizeof(info.color_modes) / sizeof(info.color_modes[0])))
            {
                strncpy(info.color_modes[info.n_color_modes], ipp, sizeof(info.color_modes[0]) - 1);
                info.color_modes[info.n_color_modes][sizeof(info.color_modes[0]) - 1] = '\0';

                if (i == prefs.shade)
                    info.default_color_mode = info.n_color_modes;

                info.n_color_modes++;
            }
        }

        /* Amiga Prefs defines qualities 0..1. Load valid IPP qualities */
        for (i = 0; i <= 1; i++)
        {
            int q = amiga_map_quality_to_ipp(i);

            if (q > 0 && info.n_qualities < (int)(sizeof(info.qualities) / sizeof(info.qualities[0])))
            {
                info.qualities[info.n_qualities] = q;

                if (i == prefs.quality)
                    info.default_quality = info.n_qualities;

                info.n_qualities++;
            }
        }

        info.max_copies = 1;
        info.accepting_jobs = 1;

        te_status(app, "Options: %s (%d papers, %d qualities, %d shades)", prefs.driver[0] ? prefs.driver : "no driver", info.n_media, info.n_qualities, info.n_color_modes);

        rc = wiz_run_submenus(app, cfg, &info, uri);

        if (rc > 0 && entry)
        {
            wiz_profile_save(&entry->profile, cfg);
            pcache_dirty = 1;
        }

        if (rc > 0)
        {
            if (app->cfg_path[0])
                te_cfg_save(cfg, app->cfg_path);

            if (amiga_apply_printer_prefs(cfg->print_media, cfg->print_color_mode, cfg->print_quality) != 0)
                te_status(app, "Options: could not write printer.prefs");
            else
                te_status(app, "Options: printer.prefs updated");
        }

        if (pcache_dirty)
            printer_cache_save(&pc);

        return;
    }
#endif

    /* Windows local target: use WinSpool caps directly, no IPP query */
#if defined(PLATFORM_WIN32)
    if (strcmp(kind, "local") == 0)
    {
        Win32PrinterInfo w;
        const char *nm = uri;
        int local_changed;
        int i;

        if (strncmp(uri, "local:", 6) == 0)
            nm = uri + 6;

        local_changed = strcmp(cfg->print_local_name, nm) != 0;

        strncpy(cfg->print_local_name, nm, sizeof(cfg->print_local_name) - 1);
        cfg->print_local_name[sizeof(cfg->print_local_name) - 1] = '\0';

        if (local_changed && app->cfg_path[0])
            te_cfg_save(cfg, app->cfg_path);

        if (win32_get_printer_info(nm, &w) != 0)
        {
            te_status(app, "Options: cannot query Windows printer %s", nm);
            return;
        }

        /* Adapt Win32PrinterInfo -> IppPrinterInfo so the sub-menus below reuse */
        memset(&info, 0, sizeof(info));

        info.accepting_jobs = -1;
        info.queued_job_count = -1;

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

        for (i = 0; i < w.n_resolutions && i < (int)(sizeof(info.resolutions) / sizeof(info.resolutions[0])); i++)
        {
            info.resolutions[i].cross_feed = w.resolutions[i].x_dpi;
            info.resolutions[i].feed = w.resolutions[i].y_dpi;
            info.resolutions[i].units = 3;
        }

        info.n_resolutions = i;
        info.default_resolution = w.default_resolution < i ? w.default_resolution : -1;
        info.max_copies = w.max_copies;

        rc = wiz_run_submenus(app, cfg, &info, uri);

        if (rc > 0 && entry)
        {
            wiz_profile_save(&entry->profile, cfg);
            pcache_dirty = 1;
        }

        if (pcache_dirty)
            printer_cache_save(&pc);

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

    rc = wiz_run_submenus(app, cfg, &info, uri);

    if (rc > 0 && entry)
    {
        wiz_profile_save(&entry->profile, cfg);
        pcache_dirty = 1;
    }

    if (pcache_dirty)
        printer_cache_save(&pc);
}

static int wiz_pick_printer_for_print(TeApp *app, PrinterCache *pc, PrinterCacheEntry **out_entry, int *pcache_dirty)
{
    const char *items[PCACHE_MAX + 6];
    static char labels[PCACHE_MAX][192];
    char title[96];
    char age[32];
    int n;
    int idx_set_default;
    int idx_discover;
    int idx_manual;
    int idx_options;
    int idx_cancel;
    int choice;
    int initial;
    int i;

    for (;;)
    {
        n = 0;

        for (i = 0; i < pc->count; i++)
        {
            const PrinterProfile *profile = &pc->entries[i].profile;
            int is_default = (pc->default_uri[0] && strcmp(pc->entries[i].uri, pc->default_uri) == 0);

            if (profile->media[0])
                snprintf(labels[i], sizeof(labels[i]), "%s%s  (%s, %s)", is_default ? "* " : "", pc->entries[i].name, pc->entries[i].kind, profile->media);
            else
                snprintf(labels[i], sizeof(labels[i]), "%s%s  (%s)", is_default ? "* " : "", pc->entries[i].name, pc->entries[i].kind);

            items[n++] = labels[i];
        }

        idx_set_default = n;
        items[n++] = "Set default printer...";

        idx_discover = n;
        items[n++] = "Discover printers...";

        idx_manual = n;
        items[n++] = "Add manually (host + queue)...";

        idx_options = n;
        items[n++] = "Configure printer options...";

        idx_cancel = n;
        items[n++] = "Cancel";

        initial = 0;
        for (i = 0; i < pc->count; i++)
        {
            if (strcmp(pc->entries[i].uri, pc->default_uri) == 0)
            {
                initial = i;
                break;
            }
        }

        printer_cache_pretty_age(pc, age, sizeof(age));

        snprintf(title, sizeof(title), "Print  (cache: %s)", age);

        choice = ui_popup_list(title, items, n, initial);

        if (choice < 0 || choice == idx_cancel)
            return 0;

        if (choice >= 0 && choice < pc->count)
        {
            *out_entry = &pc->entries[choice];
            return 1;
        }

        if (choice == idx_discover)
        {
            wiz_discover_into_cache(app, pc, pcache_dirty);
            continue;
        }

        if (choice == idx_set_default)
        {
            const char *def_items[PCACHE_MAX + 1];
            char def_labels[PCACHE_MAX + 1][160];
            int def_n;
            int def_choice;

            if (pc->count == 0)
            {
                te_status(app, "No printers to set as default");
                continue;
            }

            def_n = 0;

            for (i = 0; i < pc->count; i++)
            {
                snprintf(def_labels[def_n], sizeof(def_labels[def_n]), "%s  (%s)", pc->entries[i].name, pc->entries[i].kind);

                def_items[def_n] = def_labels[def_n];
                def_n++;
            }

            snprintf(def_labels[def_n], sizeof(def_labels[def_n]), "(none)");

            def_items[def_n] = def_labels[def_n];
            def_n++;

            def_choice = ui_popup_list("Set default printer", def_items, def_n, 0);

            if (def_choice < 0)
                continue;

            if (def_choice < pc->count)
            {
                if (strcmp(pc->default_uri, pc->entries[def_choice].uri) != 0)
                {
                    strncpy(pc->default_uri, pc->entries[def_choice].uri, sizeof(pc->default_uri) - 1);

                    pc->default_uri[sizeof(pc->default_uri) - 1] = '\0';
                    *pcache_dirty = 1;
                }
            }
            else
            {
                if (pc->default_uri[0] != '\0')
                {
                    pc->default_uri[0] = '\0';
                    *pcache_dirty = 1;
                }
            }

            continue;
        }

        if (choice == idx_manual)
        {
            TeConfig *cfg = &app->cfg;
            char host[TE_CFG_STR_MAX];
            char queue[TE_CFG_STR_MAX];
            char nu[256];
            const char *proto_items[2];
            int is_tls;
            int added;

            proto_items[0] = "IPP";
            proto_items[1] = "IPPS";

            is_tls = ui_popup_list("Protocol", proto_items, 2, 0) == 1;

            if (!ui_print_ask_ipp(cfg, is_tls ? "Add IPPS printer" : "Add IPP printer", host, sizeof(host), queue, sizeof(queue)))
                continue;

            snprintf(nu, sizeof(nu), "%s://%s:631/%s", is_tls ? "ipps" : "ipp", host, queue);

            added = printer_cache_add(pc, host, nu, is_tls ? "ipps" : "ipp");

            if (added && pcache_dirty)
                *pcache_dirty = 1;

            for (i = 0; i < pc->count; i++)
            {
                if (strcmp(pc->entries[i].uri, nu) == 0)
                {
                    *out_entry = &pc->entries[i];
                    return 1;
                }
            }

            continue;
        }

        if (choice == idx_options)
        {
            if (*pcache_dirty)
            {
                printer_cache_save(pc);
                *pcache_dirty = 0;
            }

            ui_editor_print_options(app);
            printer_cache_load(pc);
            continue;
        }
    }
}

#ifdef HAVE_IPP
static void ui_print_ipp_job(TeApp *app, Ed *ed, TeConfig *cfg, const char *uri)
{
    FILE *mem = NULL;
    unsigned char *pdf_buf = NULL;
    size_t pdf_len = 0;
    int prc;
    IppJobAttrs attrs;
    const char *filename = NULL;
    const char *jobn = NULL;
    char doc_fmt[TE_CFG_STR_MAX];
    LayoutHyphenFn hy = NULL;
    void *hy_user = NULL;
    char err[256];
    char warn[256];
    int rc;
    int i;

    err[0] = '\0';
    warn[0] = '\0';

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
        static IppPrinterInfo probe;
        char probe_err[256];

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
}
#endif

#endif /* HAVE_IPP */

void ui_editor_print(TeApp *app)
{
    TeConfig *cfg = NULL;
    Ed *ed = NULL;

    if (!app)
        return;

    ed = te_app_get_editor(app);
    if (!ed)
        return;

    cfg = &app->cfg;

#ifdef HAVE_IPP
    {
        PrinterCache pc;
        PrinterCacheEntry *entry = NULL;
        TeConfig before;
        int pcache_dirty;
        int cfg_dirty;
        int rc;

        memset(&pc, 0, sizeof(pc));

        printer_cache_load(&pc);

        pcache_dirty = 0;

        if (!wiz_pick_printer_for_print(app, &pc, &entry, &pcache_dirty))
        {
            if (pcache_dirty)
                printer_cache_save(&pc);

            return;
        }

        before = *cfg;

        wiz_profile_load(cfg, &entry->profile);

        if (strcmp(entry->kind, "local") == 0)
        {
            const char *nm = entry->name;

            if (strcmp(cfg->print_local_name, nm) != 0)
            {
                strncpy(cfg->print_local_name, nm, sizeof(cfg->print_local_name) - 1);
                cfg->print_local_name[sizeof(cfg->print_local_name) - 1] = '\0';
            }
        }
        else
        {
            const char *scheme_end = strstr(entry->uri, "://");
            int want_choice;

            want_choice = (strncmp(entry->uri, "ipps", 4) == 0) ? 2 : 1;

            if (cfg->print_last_choice != want_choice)
                cfg->print_last_choice = want_choice;

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

                if (strncmp(cfg->print_ipp_host, host_start, host_len) != 0 || cfg->print_ipp_host[host_len] != '\0')
                {
                    memcpy(cfg->print_ipp_host, host_start, host_len);
                    cfg->print_ipp_host[host_len] = '\0';
                }

                if (slash)
                {
                    if (strcmp(cfg->print_ipp_queue, slash + 1) != 0)
                    {
                        strncpy(cfg->print_ipp_queue, slash + 1, sizeof(cfg->print_ipp_queue) - 1);
                        cfg->print_ipp_queue[sizeof(cfg->print_ipp_queue) - 1] = '\0';
                    }
                }
                else
                {
                    cfg->print_ipp_queue[0] = '\0';
                }
            }
        }

        cfg_dirty = (memcmp(&before, cfg, sizeof(*cfg)) != 0);

        if (cfg_dirty && app->cfg_path[0])
            te_cfg_save(cfg, app->cfg_path);

        if (pcache_dirty)
            printer_cache_save(&pc);

        if (strcmp(entry->kind, "local") == 0)
        {
            char err[256];
            char warn[256];
            LayoutHyphenFn hy = NULL;
            void *hy_user = NULL;

            err[0] = '\0';
            warn[0] = '\0';

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

        ui_print_ipp_job(app, ed, cfg, entry->uri);
    }
#else
    te_print_document_ex(ed, cfg, app->charset_out, te_app_get_filename(app), NULL, NULL, NULL, 0, NULL, 0);
#endif
}
