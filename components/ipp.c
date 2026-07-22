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

/* Build Print-Job IPP requests and post them via the shared HTTP client */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ipp.h"
#include "../core/http_client.h"

/* IPP value tags (RFC 8010, table 5) */
#define IPP_TAG_OP_ATTR 0x01
#define IPP_TAG_JOB_ATTR 0x02
#define IPP_TAG_END 0x03
#define IPP_TAG_PRINTER_ATTR 0x04
#define IPP_TAG_UNSUPP 0x05

#define IPP_TAG_INTEGER 0x21
#define IPP_TAG_BOOLEAN 0x22
#define IPP_TAG_ENUM 0x23
#define IPP_TAG_RANGE 0x33

#define IPP_TAG_URI 0x45
#define IPP_TAG_NAME 0x42
#define IPP_TAG_KEYWORD 0x44
#define IPP_TAG_MIMETYPE 0x49
#define IPP_TAG_TEXT 0x41
#define IPP_TAG_CHARSET 0x47
#define IPP_TAG_LANG 0x48

#define IPP_TIMEOUT_SECS 30

/* Small cursor-based reader; getters set failed on overflow */
struct ipp_reader
{
    const unsigned char *buf;
    int len;
    int pos;
    int failed;
};

static void ipp_seterr(char *err, size_t errsz, const char *msg)
{
    if (err && errsz > 0)
    {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }
}

static void ipp_seterrf(char *err, size_t errsz, const char *fmt, int code)
{
    if (err && errsz > 0)
        snprintf(err, errsz, fmt, code);
}

/* Bounded big-endian writers; overflow zeroes left and makes writes no-ops */
static void bw_u8(unsigned char **p, size_t *left, unsigned int v)
{
    if (*left < 1)
    {
        *left = 0;
        return;
    }

    **p = (unsigned char)(v & 0xFF);

    *p += 1;
    *left -= 1;
}

static void bw_u16(unsigned char **p, size_t *left, unsigned int v)
{
    if (*left < 2)
    {
        *left = 0;
        return;
    }

    (*p)[0] = (unsigned char)((v >> 8) & 0xFF);
    (*p)[1] = (unsigned char)(v & 0xFF);

    *p += 2;
    *left -= 2;
}

static void bw_u32(unsigned char **p, size_t *left, unsigned int v)
{
    if (*left < 4)
    {
        *left = 0;
        return;
    }

    (*p)[0] = (unsigned char)((v >> 24) & 0xFF);
    (*p)[1] = (unsigned char)((v >> 16) & 0xFF);
    (*p)[2] = (unsigned char)((v >> 8) & 0xFF);
    (*p)[3] = (unsigned char)(v & 0xFF);

    *p += 4;
    *left -= 4;
}

static void bw_bytes(unsigned char **p, size_t *left, const void *src, size_t n)
{
    if (*left < n)
    {
        *left = 0;
        return;
    }

    memcpy(*p, src, n);

    *p += n;
    *left -= n;
}

/* Emit one operation-attribute: tag u16 name_len name u16 value_len value */
static void bw_attr(unsigned char **p, size_t *left, unsigned char tag, const char *name, const char *value)
{
    size_t nl = strlen(name);
    size_t vl = strlen(value);

    if (nl > 0xFFFF || vl > 0xFFFF)
    {
        *left = 0;
        return;
    }

    bw_u8(p, left, tag);
    bw_u16(p, left, (unsigned int)nl);

    bw_bytes(p, left, name, nl);

    bw_u16(p, left, (unsigned int)vl);
    bw_bytes(p, left, value, vl);
}

/* Build a Print-Job IPP request, return bytes written or -1 on overflow */
static int ipp_build_print_job(unsigned char *buf, size_t bufsz, const char *printer_uri, const char *user, const char *job_name, const char *doc_format)
{
    unsigned char *p = buf;
    size_t left = bufsz;

    bw_u16(&p, &left, 0x0200); /* IPP version 2.0 */
    bw_u16(&p, &left, 0x0002); /* operation-id: Print-Job */
    bw_u32(&p, &left, 1);      /* request-id */
    bw_u8(&p, &left, IPP_TAG_OP_ATTR);

    bw_attr(&p, &left, IPP_TAG_CHARSET, "attributes-charset", "utf-8");
    bw_attr(&p, &left, IPP_TAG_LANG, "attributes-natural-language", "en");
    bw_attr(&p, &left, IPP_TAG_URI, "printer-uri", printer_uri);
    bw_attr(&p, &left, IPP_TAG_NAME, "requesting-user-name", user);
    bw_attr(&p, &left, IPP_TAG_NAME, "job-name", job_name);
    bw_attr(&p, &left, IPP_TAG_MIMETYPE, "document-format", doc_format);

    bw_u8(&p, &left, IPP_TAG_END);

    if (left == 0)
        return -1;

    return (int)(p - buf);
}

/* Parse IPP status code from response; success is 0x0000..0x00FF */
static int ipp_parse_status(const char *body, int body_len, char *err, size_t errsz)
{
    unsigned int status;

    if (!body || body_len < 8)
    {
        ipp_seterr(err, errsz, "IPP response too short");
        return -1;
    }

    status = ((unsigned int)(unsigned char)body[2] << 8) | (unsigned int)(unsigned char)body[3];

    if (status <= 0x00FF)
        return 0;

    ipp_seterrf(err, errsz, "IPP status 0x%04X", (int)status);

    return -1;
}

int ipp_print_document(const char *uri, const char *job_name, const char *doc_fmt, const unsigned char *doc, size_t doc_len, char *err, size_t errsz)
{
    unsigned char ipp_hdr[1536];
    int ipp_hdr_len;
    unsigned char *body = NULL;
    size_t body_len;
    HttpResponse resp;
    int rc;

    if (err && errsz > 0)
        err[0] = '\0';

    memset(&resp, 0, sizeof(resp));

    if (!uri || !uri[0] || !doc || doc_len == 0)
    {
        ipp_seterr(err, errsz, "invalid arguments");
        return -1;
    }

    /* Build the IPP header */
    ipp_hdr_len = ipp_build_print_job(ipp_hdr, sizeof(ipp_hdr), uri, "tinyedit", (job_name && job_name[0]) ? job_name : "print", (doc_fmt && doc_fmt[0]) ? doc_fmt : "application/pdf");

    if (ipp_hdr_len < 0)
    {
        ipp_seterr(err, errsz, "IPP request too large");
        return -1;
    }

    /* HTTP body = IPP header + document */
    body_len = (size_t)ipp_hdr_len + doc_len;
    body = (unsigned char *)malloc(body_len);

    if (!body)
    {
        ipp_seterr(err, errsz, "out of memory");
        return -1;
    }

    memcpy(body, ipp_hdr, (size_t)ipp_hdr_len);
    memcpy(body + ipp_hdr_len, doc, doc_len);

    /* http_client_init is ref-counted and safe to call again */
    if (http_client_init() != HTTP_OK)
    {
        free(body);

        ipp_seterr(err, errsz, "HTTP client init failed");
        return -1;
    }

    rc = http_post(uri, (const char *)body, (int)body_len, "application/ipp", NULL, IPP_TIMEOUT_SECS, &resp);

    free(body);

    if (rc != HTTP_OK)
    {
        ipp_seterr(err, errsz, http_strerror(rc));

        http_response_free(&resp);
        http_client_shutdown();
        return -1;
    }

    if (resp.status < 200 || resp.status >= 300)
    {
        ipp_seterrf(err, errsz, "printer returned HTTP status %d", resp.status);

        http_response_free(&resp);
        http_client_shutdown();
        return -1;
    }

    rc = ipp_parse_status(resp.body, resp.body_len, err, errsz);

    http_response_free(&resp);
    http_client_shutdown();

    return rc;
}

/* Build a Get-Printer-Attributes request restricted to used attributes */
static int ipp_build_get_attrs(unsigned char *buf, size_t bufsz, const char *printer_uri)
{
    unsigned char *p = buf = NULL;
    size_t left = bufsz;
    static const char *wanted[] =
        {
            "printer-name",
            "printer-state",
            "printer-state-reasons",
            "media-supported",
            "media-default",
            "sides-supported",
            "sides-default",
            "print-color-mode-supported",
            "print-color-mode-default",
            "print-quality-supported",
            "print-quality-default",
            "document-format-supported",
            "copies-supported"};

    int nw = (int)(sizeof(wanted) / sizeof(wanted[0]));
    int i;

    bw_u16(&p, &left, 0x0200); /* IPP 2.0 */
    bw_u16(&p, &left, 0x000B); /* op-id: Get-Printer-Attributes */
    bw_u32(&p, &left, 1);      /* request-id */
    bw_u8(&p, &left, IPP_TAG_OP_ATTR);

    bw_attr(&p, &left, IPP_TAG_CHARSET, "attributes-charset", "utf-8");
    bw_attr(&p, &left, IPP_TAG_LANG, "attributes-natural-language", "en");
    bw_attr(&p, &left, IPP_TAG_URI, "printer-uri", printer_uri);

    /* Encode requested-attributes as a 1setOf keyword with name sharing */
    for (i = 0; i < nw; i++)
    {
        size_t nl = (i == 0) ? strlen("requested-attributes") : 0;
        size_t vl = strlen(wanted[i]);

        bw_u8(&p, &left, IPP_TAG_KEYWORD);
        bw_u16(&p, &left, (unsigned int)nl);

        if (nl)
            bw_bytes(&p, &left, "requested-attributes", nl);

        bw_u16(&p, &left, (unsigned int)vl);
        bw_bytes(&p, &left, wanted[i], vl);
    }

    bw_u8(&p, &left, IPP_TAG_END);

    if (left == 0)
        return -1;

    return (int)(p - buf);
}

static unsigned int rd_u8(struct ipp_reader *c)
{
    if (c->failed || c->pos + 1 > c->len)
    {
        c->failed = 1;
        return 0;
    }

    return c->buf[c->pos++];
}

static unsigned int rd_u16(struct ipp_reader *c)
{
    unsigned int hi, lo;

    if (c->failed || c->pos + 2 > c->len)
    {
        c->failed = 1;
        return 0;
    }

    hi = c->buf[c->pos];
    lo = c->buf[c->pos + 1];
    c->pos += 2;

    return (hi << 8) | lo;
}

static unsigned int rd_u32(struct ipp_reader *c)
{
    unsigned int b0, b1, b2, b3;

    if (c->failed || c->pos + 4 > c->len)
    {
        c->failed = 1;
        return 0;
    }

    b0 = c->buf[c->pos];
    b1 = c->buf[c->pos + 1];
    b2 = c->buf[c->pos + 2];
    b3 = c->buf[c->pos + 3];

    c->pos += 4;

    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

/* Read n bytes into out (max outsz-1, null-terminated). Advances past all n */
static void rd_string(struct ipp_reader *c, int n, char *out, size_t outsz)
{
    int copy;

    if (c->failed || c->pos + n > c->len || n < 0)
    {
        c->failed = 1;

        if (outsz > 0)
            out[0] = '\0';

        return;
    }

    copy = n;

    if ((size_t)copy > outsz - 1)
        copy = (int)outsz - 1;

    if (copy > 0)
        memcpy(out, c->buf + c->pos, (size_t)copy);

    if (outsz > 0)
        out[copy] = '\0';

    c->pos += n;
}

/* Skip n bytes; used for values we do not care about */
static void rd_skip(struct ipp_reader *c, int n)
{
    if (c->failed || c->pos + n > c->len || n < 0)
    {
        c->failed = 1;
        return;
    }

    c->pos += n;
}

/* Add a string to a bounded deduplicated array, return index or -1 */
static int ipp_stringset_add(char (*arr)[IPP_STR_MAX], int *count, int cap, const char *val)
{
    int i;

    if (!val || !val[0])
        return -1;

    for (i = 0; i < *count; i++)
    {
        if (strcmp(arr[i], val) == 0)
            return i;
    }

    if (*count >= cap)
        return -1;

    strncpy(arr[*count], val, IPP_STR_MAX - 1);
    arr[*count][IPP_STR_MAX - 1] = '\0';

    (*count)++;

    return *count - 1;
}

/* Match attribute name; empty name is a 1setOf continuation */
static int ipp_attr_is(const char *name, const char *target)
{
    return name[0] && strcmp(name, target) == 0;
}

/* Take one 'string'-shaped value from cur_name and route it into info */
static void ipp_ingest_string(IppPrinterInfo *info, const char *cur_name, const char *val)
{
    if (ipp_attr_is(cur_name, "printer-name"))
    {
        strncpy(info->printer_name, val, sizeof(info->printer_name) - 1);
        info->printer_name[sizeof(info->printer_name) - 1] = '\0';
    }
    else if (ipp_attr_is(cur_name, "printer-state-reasons"))
    {
        if (!info->state_reason[0])
        {
            strncpy(info->state_reason, val, sizeof(info->state_reason) - 1);
            info->state_reason[sizeof(info->state_reason) - 1] = '\0';
        }
    }
    else if (ipp_attr_is(cur_name, "media-supported"))
    {
        ipp_stringset_add(info->media, &info->n_media, IPP_MAX_MEDIA, val);
    }
    else if (ipp_attr_is(cur_name, "media-default"))
    {
        info->default_media = ipp_stringset_add(info->media, &info->n_media, IPP_MAX_MEDIA, val);
    }
    else if (ipp_attr_is(cur_name, "sides-supported"))
    {
        ipp_stringset_add(info->sides, &info->n_sides, IPP_MAX_SIDES, val);
    }
    else if (ipp_attr_is(cur_name, "sides-default"))
    {
        info->default_sides = ipp_stringset_add(info->sides, &info->n_sides, IPP_MAX_SIDES, val);
    }
    else if (ipp_attr_is(cur_name, "print-color-mode-supported"))
    {
        ipp_stringset_add(info->color_modes, &info->n_color_modes, IPP_MAX_COLOR, val);
    }
    else if (ipp_attr_is(cur_name, "print-color-mode-default"))
    {
        info->default_color_mode = ipp_stringset_add(info->color_modes, &info->n_color_modes, IPP_MAX_COLOR, val);
    }
    else if (ipp_attr_is(cur_name, "document-format-supported"))
    {
        ipp_stringset_add(info->formats, &info->n_formats, IPP_MAX_FORMATS, val);

        if (strcmp(val, "application/pdf") == 0)
            info->supports_pdf = 1;
    }
}

/* Take one integer-shaped value and route it into info */
static void ipp_ingest_integer(IppPrinterInfo *info, const char *cur_name, int val)
{
    if (ipp_attr_is(cur_name, "printer-state"))
    {
        info->state = val;
    }
    else if (ipp_attr_is(cur_name, "print-quality-supported"))
    {
        int i;

        for (i = 0; i < info->n_qualities; i++)
        {
            if (info->qualities[i] == val)
                return;
        }

        if (info->n_qualities < IPP_MAX_QUALITY)
            info->qualities[info->n_qualities++] = val;
    }
    else if (ipp_attr_is(cur_name, "print-quality-default"))
    {
        int i;
        int idx = -1;

        for (i = 0; i < info->n_qualities; i++)
        {
            if (info->qualities[i] == val)
            {
                idx = i;
                break;
            }
        }

        if (idx < 0 && info->n_qualities < IPP_MAX_QUALITY)
        {
            info->qualities[info->n_qualities++] = val;
            idx = info->n_qualities - 1;
        }

        info->default_quality = idx;
    }
    else if (ipp_attr_is(cur_name, "copies-supported"))
    {
        /* Comes as rangeOfInteger normally, but a bare integer is possible */
        if (val > info->max_copies)
            info->max_copies = val;
    }
}

/* Parse printer-attributes group into IppPrinterInfo */
static int ipp_parse_attrs(const unsigned char *body, int body_len, IppPrinterInfo *info)
{
    struct ipp_reader c;
    char cur_name[IPP_STR_MAX];
    unsigned int tag;
    int in_printer_group = 0;
    unsigned int nl;
    unsigned int vl;

    memset(&c, 0, sizeof(c));

    c.buf = body;
    c.len = body_len;

    if (body_len < 8)
        return -1;

    /* Skip the response header: version u16, status u16, request-id u32 */
    c.pos = 8;

    cur_name[0] = '\0';

    while (!c.failed && c.pos < c.len)
    {
        tag = rd_u8(&c);

        if (c.failed)
            break;

        /* Delimiter tags: 0x01-0x05 select a new attribute group */
        if (tag <= 0x05)
        {
            in_printer_group = (tag == IPP_TAG_PRINTER_ATTR);
            cur_name[0] = '\0';

            if (tag == IPP_TAG_END)
                break;

            continue;
        }

        nl = rd_u16(&c);

        if (nl > 0)
            rd_string(&c, (int)nl, cur_name, sizeof(cur_name));

        vl = rd_u16(&c);

        if (!in_printer_group)
        {
            rd_skip(&c, (int)vl);
            continue;
        }

        if (tag == IPP_TAG_KEYWORD || tag == IPP_TAG_NAME || tag == IPP_TAG_TEXT || tag == IPP_TAG_MIMETYPE || tag == IPP_TAG_URI)
        {
            char val[IPP_STR_MAX];

            rd_string(&c, (int)vl, val, sizeof(val));
            ipp_ingest_string(info, cur_name, val);
        }
        else if (tag == IPP_TAG_INTEGER || tag == IPP_TAG_ENUM)
        {
            if (vl == 4)
            {
                int v = (int)rd_u32(&c);

                ipp_ingest_integer(info, cur_name, v);
            }
            else
                rd_skip(&c, (int)vl);
        }
        else if (tag == IPP_TAG_RANGE)
        {
            /* rangeOfInteger: low u32, high u32 */
            if (vl == 8)
            {
                int hi;

                rd_u32(&c);

                hi = (int)rd_u32(&c);

                if (ipp_attr_is(cur_name, "copies-supported"))
                {
                    if (hi > info->max_copies)
                        info->max_copies = hi;
                }
            }
            else
                rd_skip(&c, (int)vl);
        }
        else if (tag == IPP_TAG_BOOLEAN)
        {
            rd_skip(&c, (int)vl);
        }
        else
        {
            rd_skip(&c, (int)vl);
        }
    }

    return c.failed ? -1 : 0;
}

int ipp_get_printer_info(const char *uri, IppPrinterInfo *info, char *err, size_t errsz)
{
    unsigned char req[1024];
    int req_len;
    HttpResponse resp;
    int rc;

    if (err && errsz > 0)
        err[0] = '\0';

    if (!uri || !info)
    {
        ipp_seterr(err, errsz, "invalid arguments");
        return -1;
    }

    memset(info, 0, sizeof(*info));

    info->default_media = -1;
    info->default_sides = -1;
    info->default_color_mode = -1;
    info->default_quality = -1;

    req_len = ipp_build_get_attrs(req, sizeof(req), uri);

    if (req_len < 0)
    {
        ipp_seterr(err, errsz, "IPP request too large");
        return -1;
    }

    memset(&resp, 0, sizeof(resp));

    if (http_client_init() != HTTP_OK)
    {
        ipp_seterr(err, errsz, "HTTP client init failed");
        return -1;
    }

    rc = http_post(uri, (const char *)req, req_len, "application/ipp", NULL, IPP_TIMEOUT_SECS, &resp);

    if (rc != HTTP_OK)
    {
        ipp_seterr(err, errsz, http_strerror(rc));

        http_response_free(&resp);
        http_client_shutdown();
        return -1;
    }

    if (resp.status < 200 || resp.status >= 300)
    {
        ipp_seterrf(err, errsz, "printer returned HTTP status %d", resp.status);

        http_response_free(&resp);
        http_client_shutdown();
        return -1;
    }

    rc = ipp_parse_status(resp.body, resp.body_len, err, errsz);

    if (rc == 0)
    {
        if (ipp_parse_attrs((const unsigned char *)resp.body, resp.body_len, info) != 0)
        {
            ipp_seterr(err, errsz, "malformed IPP response");
            rc = -1;
        }
    }

    http_response_free(&resp);
    http_client_shutdown();

    return rc;
}
