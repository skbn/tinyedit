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

/* mDNS browser for _ipp._tcp and _ipps._tcp services per RFC 6762 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "mdns.h"

#if defined(PLATFORM_WIN32)

#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t_compat;

#define MDNS_CLOSE closesocket
#define MDNS_LAST_ERR WSAGetLastError()

#elif defined(PLATFORM_AMIGA)
#include <exec/types.h>
#include <proto/exec.h>

#ifndef CLIB_BSDSOCKET_PROTOS_H
#define CLIB_BSDSOCKET_PROTOS_H
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <proto/socket.h>
#include <sys/time.h>
#include "http_client.h"

extern struct Library *SocketBase;
typedef int socklen_t_compat;

#define MDNS_CLOSE CloseSocket
#define MDNS_LAST_ERR Errno()

#else

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

typedef socklen_t socklen_t_compat;

#define MDNS_CLOSE close
#define MDNS_LAST_ERR errno

#endif

#define MDNS_ADDR "224.0.0.251"
#define MDNS_PORT 5353
#define MDNS_QNAME_MAX 256

/* DNS record types we care about */
#define DNS_TYPE_PTR 12
#define DNS_TYPE_TXT 16
#define DNS_TYPE_SRV 33
#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1

#if defined(PLATFORM_WIN32)

static int mdns_wsa_up = 0;

static int mdns_net_init(void)
{
    WSADATA d;

    if (mdns_wsa_up)
        return 0;

    if (WSAStartup(MAKEWORD(2, 2), &d) != 0)
        return -1;

    mdns_wsa_up = 1;

    return 0;
}

#else

static int mdns_net_init(void)
{
#if defined(PLATFORM_AMIGA)
    /* http_client_init opens bsdsocket.library and refcounts it */
    if (!SocketBase)
    {
        if (http_client_init() != HTTP_OK)
            return -1;
    }
#endif
    return 0;
}

#endif

/* Encode "_ipp._tcp.local" into DNS wire label form; returns bytes written */
static int mdns_encode_name(const char *name, unsigned char *out, int outsz)
{
    const char *p = name;
    const char *dot = NULL;
    int written = 0;
    int label_len;

    while (*p)
    {
        dot = strchr(p, '.');
        label_len = dot ? (int)(dot - p) : (int)strlen(p);

        if (label_len == 0 || label_len > 63)
            return -1;

        if (written + 1 + label_len + 1 > outsz)
            return -1;

        out[written++] = (unsigned char)label_len;

        memcpy(out + written, p, label_len);

        written += label_len;

        if (!dot)
            break;

        p = dot + 1;
    }

    out[written++] = 0;

    return written;
}

/* Build one PTR query for service type; returns bytes written or -1 */
static int mdns_build_query(unsigned char *buf, int bufsz, const char *service)
{
    unsigned char qname[MDNS_QNAME_MAX];
    int qname_len;

    qname_len = mdns_encode_name(service, qname, sizeof(qname));

    if (qname_len < 0)
        return -1;

    if (12 + qname_len + 4 > bufsz)
        return -1;

    /* DNS header: id=0, flags=0 (standard query), 1 question */
    memset(buf, 0, 12);

    buf[5] = 1;

    memcpy(buf + 12, qname, qname_len);

    /* qtype = PTR, qclass = IN */
    buf[12 + qname_len + 0] = 0;
    buf[12 + qname_len + 1] = DNS_TYPE_PTR;
    buf[12 + qname_len + 2] = 0;
    buf[12 + qname_len + 3] = DNS_CLASS_IN;

    return 12 + qname_len + 4;
}

/* Decode a DNS name at offset, following compression pointers, returning consumed bytes */
static int mdns_decode_name(const unsigned char *pkt, int pktlen, int offset, char *out, int outsz)
{
    int consumed_here = 0;
    int followed = 0;
    int pos = offset;
    int out_pos = 0;
    int written_at_first_ptr = -1;

    if (out && outsz > 0)
        out[0] = '\0';

    while (pos < pktlen)
    {
        unsigned char b = pkt[pos];

        if (b == 0)
        {
            if (written_at_first_ptr < 0)
                consumed_here = pos - offset + 1;

            if (out && out_pos < outsz)
                out[out_pos] = '\0';

            return consumed_here;
        }

        if ((b & 0xC0) == 0xC0)
        {
            int target;

            if (pos + 1 >= pktlen)
                return -1;

            if (written_at_first_ptr < 0)
            {
                consumed_here = pos - offset + 2;
                written_at_first_ptr = 1;
            }

            target = ((b & 0x3F) << 8) | pkt[pos + 1];

            if (target >= pos)
                return -1;

            pos = target;
            followed++;

            if (followed > 16)
                return -1;

            continue;
        }

        if (b > 63)
            return -1;

        if (pos + 1 + b > pktlen)
            return -1;

        if (out_pos > 0 && out_pos + 1 < outsz)
            out[out_pos++] = '.';

        if (out && out_pos + b < outsz)
        {
            memcpy(out + out_pos, pkt + pos + 1, b);
            out_pos += b;
        }

        pos += 1 + b;
    }

    return -1;
}

/* Skip 4 bytes qtype+qclass in a question */
static int mdns_skip_question(const unsigned char *pkt, int pktlen, int offset)
{
    char tmp[MDNS_QNAME_MAX];
    int n = mdns_decode_name(pkt, pktlen, offset, tmp, sizeof(tmp));

    if (n < 0 || offset + n + 4 > pktlen)
        return -1;

    return offset + n + 4;
}

/* Look up an existing entry by instance name; returns index or -1 */
static int mdns_find(MdnsPrinter *list, int n, const char *name)
{
    int i;

    for (i = 0; i < n; i++)
    {
        if (strcmp(list[i].name, name) == 0)
            return i;
    }

    return -1;
}

/* Parse a TXT chunk and pull the rp= field; leaves path empty if absent */
static void mdns_parse_txt(const unsigned char *rdata, int rdlen, char *out_path, int outsz)
{
    int pos = 0;

    while (pos < rdlen)
    {
        int len = rdata[pos];

        if (len == 0 || pos + 1 + len > rdlen)
            return;

        if (len > 3 && rdata[pos + 1] == 'r' && rdata[pos + 2] == 'p' && rdata[pos + 3] == '=')
        {
            int val_len = len - 3;

            if (val_len >= outsz)
                val_len = outsz - 1;

            memcpy(out_path, rdata + pos + 4, val_len);
            out_path[val_len] = '\0';

            return;
        }

        pos += 1 + len;
    }
}

/* Strip a service suffix from an instance name: "MyPrinter._ipp._tcp.local" -> "MyPrinter" */
static void mdns_pretty_name(const char *full, char *out, int outsz)
{
    const char *dot = strchr(full, '.');
    int n;

    if (!dot)
        n = (int)strlen(full);
    else
        n = (int)(dot - full);

    if (n >= outsz)
        n = outsz - 1;

    memcpy(out, full, n);
    out[n] = '\0';
}

/* Parse one reply packet into list. Returns number of entries added */
static int mdns_parse_reply(const unsigned char *pkt, int pktlen, MdnsPrinter *list, int list_n, int list_max, int is_tls_hint)
{
    int qdcount;
    int ancount;
    int arcount;
    int rr_total;
    int pos = 12;
    int i;
    int added = 0;

    if (pktlen < 12)
        return 0;

    qdcount = (pkt[4] << 8) | pkt[5];
    ancount = (pkt[6] << 8) | pkt[7];
    arcount = (pkt[10] << 8) | pkt[11];

    rr_total = ancount + arcount + ((pkt[8] << 8) | pkt[9]);

    for (i = 0; i < qdcount; i++)
    {
        pos = mdns_skip_question(pkt, pktlen, pos);

        if (pos < 0)
            return added;
    }

    for (i = 0; i < rr_total && pos + 10 < pktlen; i++)
    {
        char name[MDNS_QNAME_MAX];
        int name_len;
        int rtype;
        int rdlen;
        int rdata_pos;

        name_len = mdns_decode_name(pkt, pktlen, pos, name, sizeof(name));

        if (name_len < 0)
            return added;

        pos += name_len;

        if (pos + 10 > pktlen)
            return added;

        rtype = (pkt[pos] << 8) | pkt[pos + 1];
        rdlen = (pkt[pos + 8] << 8) | pkt[pos + 9];
        rdata_pos = pos + 10;

        if (rdata_pos + rdlen > pktlen)
            return added;

        if (rtype == DNS_TYPE_PTR)
        {
            char inst[MDNS_QNAME_MAX];
            char pretty[96];
            int idx;

            if (mdns_decode_name(pkt, pktlen, rdata_pos, inst, sizeof(inst)) < 0)
            {
                pos = rdata_pos + rdlen;
                continue;
            }

            mdns_pretty_name(inst, pretty, sizeof(pretty));

            idx = mdns_find(list, list_n + added, pretty);

            if (idx < 0 && list_n + added < list_max)
            {
                strncpy(list[list_n + added].name, pretty, sizeof(list[0].name) - 1);
                list[list_n + added].name[sizeof(list[0].name) - 1] = '\0';

                list[list_n + added].is_tls = is_tls_hint;
                list[list_n + added].port = is_tls_hint ? 631 : 631;

                added++;
            }
        }
        else if (rtype == DNS_TYPE_SRV && rdlen >= 6)
        {
            char target[MDNS_QNAME_MAX];
            int port;
            char inst_pretty[96];
            int idx;

            port = (pkt[rdata_pos + 4] << 8) | pkt[rdata_pos + 5];

            if (mdns_decode_name(pkt, pktlen, rdata_pos + 6, target, sizeof(target)) < 0)
                target[0] = '\0';

            mdns_pretty_name(name, inst_pretty, sizeof(inst_pretty));

            idx = mdns_find(list, list_n + added, inst_pretty);

            if (idx < 0 && list_n + added < list_max)
            {
                strncpy(list[list_n + added].name, inst_pretty, sizeof(list[0].name) - 1);
                list[list_n + added].name[sizeof(list[0].name) - 1] = '\0';

                list[list_n + added].is_tls = is_tls_hint;

                idx = list_n + added;
                added++;
            }

            if (idx >= 0 && idx < list_max)
            {
                list[idx].port = port > 0 ? port : 631;

                strncpy(list[idx].host, target, sizeof(list[0].host) - 1);

                list[idx].host[sizeof(list[0].host) - 1] = '\0';
            }
        }
        else if (rtype == DNS_TYPE_TXT)
        {
            char inst_pretty[96];
            int idx;

            mdns_pretty_name(name, inst_pretty, sizeof(inst_pretty));

            idx = mdns_find(list, list_n + added, inst_pretty);

            if (idx >= 0 && idx < list_max)
                mdns_parse_txt(pkt + rdata_pos, rdlen, list[idx].path, sizeof(list[0].path));
        }

        pos = rdata_pos + rdlen;
    }

    return added;
}

int mdns_discover_printers(MdnsPrinter *out, int max_results, int timeout_ms)
{
    int sock;
    struct sockaddr_in dst;
    struct sockaddr_in bind_addr;
    struct ip_mreq mreq;
    unsigned char qbuf[512];
    unsigned char rbuf[4096];
    int qlen;
    int n_found = 0;
    struct timeval tv;
    int ttl = 255;
    int reuse = 1;
    int i;

#if defined(PLATFORM_AMIGA)
    fd_set readfds;
    int selrc;
    time_t start_time;
    int elapsed_ms;
    int remaining_ms;
#endif

    if (!out || max_results <= 0)
        return -1;

    memset(out, 0, sizeof(*out) * (size_t)max_results);

    if (mdns_net_init() != 0)
        return -1;

    sock = (int)socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0)
        return -1;

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0)
    {
        MDNS_CLOSE(sock);
        return -1;
    }

    memset(&bind_addr, 0, sizeof(bind_addr));

    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(MDNS_PORT);

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0)
    {
        bind_addr.sin_port = htons(0);

        if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0)
        {
            MDNS_CLOSE(sock);
            return -1;
        }
    }

    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl));

    memset(&mreq, 0, sizeof(mreq));

    mreq.imr_multiaddr.s_addr = inet_addr(MDNS_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq));

    memset(&dst, 0, sizeof(dst));

    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr(MDNS_ADDR);
    dst.sin_port = htons(MDNS_PORT);

    /* Send IPP query */
    qlen = mdns_build_query(qbuf, sizeof(qbuf), "_ipp._tcp.local");

    if (qlen > 0)
        sendto(sock, (const char *)qbuf, qlen, 0, (struct sockaddr *)&dst, sizeof(dst));

    /* Send IPPS query */
    qlen = mdns_build_query(qbuf, sizeof(qbuf), "_ipps._tcp.local");

    if (qlen > 0)
        sendto(sock, (const char *)qbuf, qlen, 0, (struct sockaddr *)&dst, sizeof(dst));

    /* Collect responses until timeout */

#if defined(PLATFORM_AMIGA)
    /* Use WaitSelect() on Amiga with a global deadline to avoid SO_RCVTIMEO and infinite loops */
    start_time = time(NULL);

    for (;;)
    {
        struct sockaddr_in from;
        socklen_t_compat from_len = (socklen_t_compat)sizeof(from);
        int r;

        elapsed_ms = (int)(time(NULL) - start_time) * 1000;
        remaining_ms = timeout_ms - elapsed_ms;

        if (remaining_ms <= 0)
            break;

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        tv.tv_sec = remaining_ms / 1000;
        tv.tv_usec = (remaining_ms % 1000) * 1000;

        selrc = WaitSelect(sock + 1, &readfds, NULL, NULL, &tv, NULL);

        if (selrc <= 0)
            break;

        r = (int)recvfrom(sock, (char *)rbuf, sizeof(rbuf), 0, (struct sockaddr *)&from, &from_len);

        if (r <= 0)
            break;

        /* Try IPP first; if the reply has no matches this is essentially a no-op */
        n_found += mdns_parse_reply(rbuf, r, out, n_found, max_results, 0);

        /* Same packet may carry IPPS records too when servers advertise both */
        n_found += mdns_parse_reply(rbuf, r, out, n_found, max_results, 1);

        if (n_found >= max_results)
            break;
    }
#else
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

    for (;;)
    {
        struct sockaddr_in from;
        socklen_t_compat from_len = (socklen_t_compat)sizeof(from);
        int r;

        r = (int)recvfrom(sock, (char *)rbuf, sizeof(rbuf), 0, (struct sockaddr *)&from, &from_len);

        if (r <= 0)
            break;

        /* Try IPP first; if the reply has no matches this is essentially a no-op */
        n_found += mdns_parse_reply(rbuf, r, out, n_found, max_results, 0);

        /* Same packet may carry IPPS records too when servers advertise both */
        n_found += mdns_parse_reply(rbuf, r, out, n_found, max_results, 1);

        if (n_found >= max_results)
            break;
    }
#endif

    MDNS_CLOSE(sock);

    /* Fill in dotted-quad hostname when SRV target is unresolved and we have the sender addr */
    for (i = 0; i < n_found; i++)
    {
        if (!out[i].host[0])
            strncpy(out[i].host, "unknown", sizeof(out[i].host) - 1);
    }

    return n_found;
}
