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

/* Small text-file cache for discovered printers, keyed by URI */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "printer_cache.h"
#include "portable.h"

/* Resolve the platform-specific path to the cache file */
static const char *pcache_path(void)
{
    static char buf[512];
    char cfg_dir[512];

    port_get_config_dir(cfg_dir, sizeof(cfg_dir));
    pf_path_join(buf, sizeof(buf), cfg_dir, "printers");

    return buf;
}

/* Trim trailing whitespace in place */
static void rstrip(char *s)
{
    size_t n = strlen(s);

    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
}

int printer_cache_load(PrinterCache *pc)
{
    FILE *f = NULL;
    char line[512];

    if (!pc)
        return -1;

    memset(pc, 0, sizeof(*pc));

    f = fopen(pcache_path(), "r");

    if (!f)
        return -1;

    while (fgets(line, sizeof(line), f))
    {
        rstrip(line);

        if (line[0] == '\0')
            continue;

        /* Header: "# discovered <unix_ts>" */
        if (line[0] == '#')
        {
            const char *p = strstr(line, "discovered");

            if (p)
                pc->last_discovery = atol(p + 10);

            continue;
        }

        if (strncmp(line, "PRINTER ", 8) == 0 && pc->count < PCACHE_MAX)
        {
            char *rest = line + 8;
            char *bar1 = strchr(rest, '|');
            char *bar2 = bar1 ? strchr(bar1 + 1, '|') : NULL;

            if (bar1 && bar2)
            {
                size_t nn = (size_t)(bar1 - rest);
                size_t nu = (size_t)(bar2 - (bar1 + 1));

                if (nn >= sizeof(pc->entries[0].name))
                    nn = sizeof(pc->entries[0].name) - 1;

                if (nu >= sizeof(pc->entries[0].uri))
                    nu = sizeof(pc->entries[0].uri) - 1;

                memcpy(pc->entries[pc->count].name, rest, nn);
                pc->entries[pc->count].name[nn] = '\0';

                memcpy(pc->entries[pc->count].uri, bar1 + 1, nu);
                pc->entries[pc->count].uri[nu] = '\0';

                strncpy(pc->entries[pc->count].kind, bar2 + 1, sizeof(pc->entries[0].kind) - 1);
                pc->entries[pc->count].kind[sizeof(pc->entries[0].kind) - 1] = '\0';

                pc->count++;
            }
        }
    }

    fclose(f);

    return 0;
}

int printer_cache_save(const PrinterCache *pc)
{
    FILE *f = NULL;
    int i;
    char cfg_dir[512];

    if (!pc)
        return -1;

    port_get_config_dir(cfg_dir, sizeof(cfg_dir));
    pf_ensure_dir(cfg_dir);

    f = fopen(pcache_path(), "w");

    if (!f)
        return -1;

    fprintf(f, "# tinyedit printer cache\n");
    fprintf(f, "# discovered %ld\n", pc->last_discovery);

    for (i = 0; i < pc->count; i++)
        fprintf(f, "PRINTER %s|%s|%s\n", pc->entries[i].name, pc->entries[i].uri, pc->entries[i].kind);

    fclose(f);
    return 0;
}

int printer_cache_add(PrinterCache *pc, const char *name, const char *uri, const char *kind)
{
    int i;

    if (!pc || !name || !uri || !kind)
        return 0;

    /* Skip duplicates on URI */
    for (i = 0; i < pc->count; i++)
    {
        if (strcmp(pc->entries[i].uri, uri) == 0)
            return 0;
    }

    if (pc->count >= PCACHE_MAX)
        return 0;

    strncpy(pc->entries[pc->count].name, name, sizeof(pc->entries[0].name) - 1);
    pc->entries[pc->count].name[sizeof(pc->entries[0].name) - 1] = '\0';

    strncpy(pc->entries[pc->count].uri, uri, sizeof(pc->entries[0].uri) - 1);
    pc->entries[pc->count].uri[sizeof(pc->entries[0].uri) - 1] = '\0';

    strncpy(pc->entries[pc->count].kind, kind, sizeof(pc->entries[0].kind) - 1);
    pc->entries[pc->count].kind[sizeof(pc->entries[0].kind) - 1] = '\0';

    pc->count++;

    return 1;
}

void printer_cache_clear(PrinterCache *pc)
{
    if (!pc)
        return;

    memset(pc, 0, sizeof(*pc));
}

int printer_cache_is_stale(const PrinterCache *pc)
{
    time_t now;

    if (!pc || pc->last_discovery == 0)
        return 1;

    now = time(NULL);
    return (now - (time_t)pc->last_discovery) >= PCACHE_STALE_SECONDS;
}

void printer_cache_pretty_age(const PrinterCache *pc, char *buf, size_t buflen)
{
    time_t now;
    long diff;

    if (!pc || !buf || buflen == 0)
        return;

    if (pc->last_discovery == 0)
    {
        strncpy(buf, "never", buflen - 1);
        buf[buflen - 1] = '\0';

        return;
    }

    now = time(NULL);
    diff = (long)now - pc->last_discovery;

    if (diff < 60)
        snprintf(buf, buflen, "just now");
    else if (diff < 3600)
        snprintf(buf, buflen, "%ldm ago", diff / 60);
    else if (diff < 86400)
        snprintf(buf, buflen, "%ldh ago", diff / 3600);
    else
        snprintf(buf, buflen, "%ldd ago", diff / 86400);
}
