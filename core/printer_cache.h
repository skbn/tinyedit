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

#ifndef TE_PRINTER_CACHE_H
#define TE_PRINTER_CACHE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef PCACHE_MAX
#define PCACHE_MAX 32
#endif

#define PCACHE_STALE_SECONDS (24 * 60 * 60)

    typedef struct
    {
        char document_format[256];
        char media[256];
        char sides[256];
        char color_mode[256];
        int quality;
        int copies;
        int orientation;
        int number_up;
        char media_source[256];
        char media_type[256];
        int resolution_x;
        int resolution_y;
        int resolution_units;
        char font_path[256];
        int font_size;
    } PrinterProfile;

    /* One printer entry, either discovered or added manually */
    typedef struct
    {
        char name[96];
        char uri[192]; /* ipp://host:port/path or local:name */
        char kind[16]; /* "local" / "ipp" / "ipps" */
        PrinterProfile profile;
    } PrinterCacheEntry;

    typedef struct
    {
        PrinterCacheEntry entries[PCACHE_MAX];
        int count;
        long last_discovery; /* unix time; 0 = never */
        char default_uri[192];
    } PrinterCache;

    /* Load cache from ~/.tinyedit_printers or ENVARC:tinyedit/printers */
    int printer_cache_load(PrinterCache *pc);

    /* Persist cache; timestamps last_discovery only when refresh=1 */
    int printer_cache_save(const PrinterCache *pc);

    /* Add entry if not already present (by uri); returns 1 if added, 0 if dup */
    int printer_cache_add(PrinterCache *pc, const char *name, const char *uri, const char *kind);

    PrinterCacheEntry *printer_cache_find(PrinterCache *pc, const char *uri);

    /* Clear all entries and reset timestamp */
    void printer_cache_clear(PrinterCache *pc);

    /* 1 if last_discovery is older than PCACHE_STALE_SECONDS or missing */
    int printer_cache_is_stale(const PrinterCache *pc);

    /* Approximate "N hours ago" / "just now" for display; buf sized ~32 */
    void printer_cache_pretty_age(const PrinterCache *pc, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif
