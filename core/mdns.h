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

#ifndef TE_MDNS_H
#define TE_MDNS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef MDNS_MAX_RESULTS
#define MDNS_MAX_RESULTS 24
#endif

    /* One discovered service instance */
    typedef struct
    {
        char name[96];  /* Instance name, human-readable */
        char host[128]; /* Hostname or IP */
        int port;
        int is_tls;    /* 1 for ipps/_ipps._tcp, 0 for ipp */
        char path[64]; /* rp= TXT record, e.g. "printers/office" */
    } MdnsPrinter;

    /* Discover IPP and IPPS printers on the local network up to max_results */
    int mdns_discover_printers(MdnsPrinter *out, int max_results, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
