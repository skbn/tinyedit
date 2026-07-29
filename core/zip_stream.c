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

/* Streaming ZIP: stored on Amiga, deflate elsewhere */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "zip_stream.h"

/* ZIP file signatures */
#define SIG_LOCAL 0x04034b50UL
#define SIG_CENTRAL 0x02014b50UL
#define SIG_EOCD 0x06054b50UL
#define SIG_DDESC 0x08074b50UL

/* ZIP version for deflate with data descriptor */
#define ZIP_VER_MADE 20
#define ZIP_VER_NEED 20

/* Data descriptor flag (GPF bit 3) */
#define GPF_DATA_DESCRIPTOR 0x0008

/* Central directory entry */
typedef struct
{
    char name[ZIP_NAME_MAX];
    unsigned long local_offset;
    unsigned long csize;
    unsigned long usize;
    unsigned int method;
} ZipEntry;

struct ZipReaderStruct
{
    FILE *fp;
    ZipEntry *entries;
    int n_entries;
    int cap_entries;
    int cur_idx;               /* Next entry to return; -1 = before first */
    int entry_open;            /* 1 = currently mid-read of an entry */
    unsigned long remaining_c; /* Compressed bytes left in current entry */
    unsigned long remaining_u; /* Uncompressed bytes left (stored) or hint (deflate) */
    unsigned int cur_method;
    z_stream zs;
    int zs_active;
    unsigned char inbuf[4096];
};

typedef struct
{
    char name[ZIP_NAME_MAX];
    unsigned long local_offset;
    unsigned long csize;
    unsigned long usize;
    unsigned long crc;
    unsigned int method;
    unsigned int flags;
} ZipWriteEntry;

struct ZipWriterStruct
{
    FILE *fp;
    unsigned long pos; /* Bytes written so far */
    ZipWriteEntry *entries;
    int n_entries;
    int cap_entries;

    /* Current entry state */
    int entry_open;
    ZipWriteEntry cur;
    z_stream zs;
    int zs_active;
    unsigned char outbuf[4096];
};

/* CRC-32 table, lazily initialised */
static unsigned long zip_crc_table[256];
static int zip_crc_initialised = 0;

static void zip_crc_init(void)
{
    unsigned long c;
    int n, k;

    if (zip_crc_initialised)
        return;

    for (n = 0; n < 256; n++)
    {
        c = (unsigned long)n;

        for (k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);

        zip_crc_table[n] = c;
    }

    zip_crc_initialised = 1;
}

static unsigned long zip_crc_update(unsigned long crc, const unsigned char *buf, int n)
{
    int i;

    crc ^= 0xFFFFFFFFUL;

    for (i = 0; i < n; i++)
        crc = zip_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);

    return crc ^ 0xFFFFFFFFUL;
}

/* Little-endian I/O helpers */
static void zip_put16(FILE *fp, unsigned int v)
{
    unsigned char b[2];

    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);

    fwrite(b, 1, 2, fp);
}

static void zip_put32(FILE *fp, unsigned long v)
{
    unsigned char b[4];

    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    b[2] = (unsigned char)((v >> 16) & 0xFF);
    b[3] = (unsigned char)((v >> 24) & 0xFF);

    fwrite(b, 1, 4, fp);
}

static unsigned int zip_get16(const unsigned char *b)
{
    return (unsigned int)b[0] | ((unsigned int)b[1] << 8);
}

static unsigned long zip_get32(const unsigned char *b)
{
    return (unsigned long)b[0] | ((unsigned long)b[1] << 8) | ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}

/* Find EOCD by scanning back from EOF */
static int zip_find_eocd(FILE *fp, unsigned long *out_cd_offset, unsigned int *out_n)
{
    unsigned char buf[4096];
    long fsize;
    long scan_start;
    long read_from;
    int rd;
    int i;

    if (fseek(fp, 0, SEEK_END) != 0)
        return -1;

    fsize = ftell(fp);

    if (fsize < 22)
        return -1;

    /* EOCD is within 65557 bytes of EOF */
    scan_start = fsize - (long)sizeof(buf);

    if (scan_start < 0)
        scan_start = 0;

    read_from = scan_start;

    if (fseek(fp, read_from, SEEK_SET) != 0)
        return -1;

    rd = (int)fread(buf, 1, sizeof(buf), fp);

    for (i = rd - 22; i >= 0; i--)
    {
        if (zip_get32(buf + i) == SIG_EOCD)
        {
            *out_n = zip_get16(buf + i + 10);
            *out_cd_offset = zip_get32(buf + i + 16);
            return 0;
        }
    }

    return -1;
}

/* Read one central directory entry */
static int zip_read_cd_entry(FILE *fp, ZipEntry *e)
{
    unsigned char hdr[46];
    unsigned int nlen, xlen, clen;
    unsigned int copy_n;

    if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr))
        return -1;

    if (zip_get32(hdr) != SIG_CENTRAL)
        return -1;

    e->method = zip_get16(hdr + 10);
    e->csize = zip_get32(hdr + 20);
    e->usize = zip_get32(hdr + 24);
    e->local_offset = zip_get32(hdr + 42);

    nlen = zip_get16(hdr + 28);
    xlen = zip_get16(hdr + 30);
    clen = zip_get16(hdr + 32);

    /* Read filename */
    copy_n = nlen;

    if (copy_n >= sizeof(e->name))
        copy_n = sizeof(e->name) - 1;

    if (fread(e->name, 1, copy_n, fp) != copy_n)
        return -1;

    e->name[copy_n] = '\0';

    if (nlen > copy_n)
    {
        if (fseek(fp, (long)(nlen - copy_n), SEEK_CUR) != 0)
            return -1;
    }

    /* Skip extra and comment */
    if (fseek(fp, (long)(xlen + clen), SEEK_CUR) != 0)
        return -1;

    return 0;
}

ZipReader *zip_open_read(FILE *fp)
{
    ZipReader *zr = NULL;
    unsigned long cd_offset = 0;
    unsigned int n_total = 0;
    unsigned int i;

    if (!fp)
        return NULL;

    zip_crc_init();

    if (zip_find_eocd(fp, &cd_offset, &n_total) != 0)
        return NULL;

    zr = (ZipReader *)calloc(1, sizeof(*zr));

    if (!zr)
        return NULL;

    zr->fp = fp;
    zr->cur_idx = -1;

    if (n_total > 0)
    {
        zr->entries = (ZipEntry *)calloc(n_total, sizeof(ZipEntry));

        if (!zr->entries)
        {
            free(zr);
            return NULL;
        }

        zr->cap_entries = (int)n_total;
    }

    if (fseek(fp, (long)cd_offset, SEEK_SET) != 0)
    {
        free(zr->entries);
        free(zr);

        return NULL;
    }

    for (i = 0; i < n_total; i++)
    {
        if (zip_read_cd_entry(fp, &zr->entries[i]) != 0)
        {
            free(zr->entries);
            free(zr);

            return NULL;
        }

        zr->n_entries++;
    }

    return zr;
}

/* Seek to compressed data of current entry */
static int zip_open_entry_data(ZipReader *zr, ZipEntry *e)
{
    unsigned char hdr[30];
    unsigned int nlen, xlen;

    if (fseek(zr->fp, (long)e->local_offset, SEEK_SET) != 0)
        return -1;

    if (fread(hdr, 1, sizeof(hdr), zr->fp) != sizeof(hdr))
        return -1;

    if (zip_get32(hdr) != SIG_LOCAL)
        return -1;

    nlen = zip_get16(hdr + 26);
    xlen = zip_get16(hdr + 28);

    if (fseek(zr->fp, (long)(nlen + xlen), SEEK_CUR) != 0)
        return -1;

    zr->cur_method = e->method;
    zr->remaining_c = e->csize;
    zr->remaining_u = e->usize;

    if (e->method == ZIP_METHOD_DEFLATE)
    {
        memset(&zr->zs, 0, sizeof(zr->zs));

        /* Raw deflate, no zlib header */
        if (inflateInit2(&zr->zs, -MAX_WBITS) != Z_OK)
            return -1;

        zr->zs_active = 1;
    }
    else if (e->method != ZIP_METHOD_STORED)
    {
        /* Unsupported method */
        return -1;
    }

    zr->entry_open = 1;
    return 0;
}

/* Release inflate resources for current entry */
static void zip_close_current_entry(ZipReader *zr)
{
    if (zr->zs_active)
    {
        inflateEnd(&zr->zs);

        zr->zs_active = 0;
    }

    zr->entry_open = 0;
    zr->remaining_c = 0;
    zr->remaining_u = 0;
}

int zip_next_entry(ZipReader *zr, char *name_out, size_t name_sz)
{
    if (!zr)
        return -1;

    if (zr->entry_open)
        zip_skip_entry(zr);

    zr->cur_idx++;

    if (zr->cur_idx >= zr->n_entries)
        return 0;

    if (name_out && name_sz > 0)
    {
        strncpy(name_out, zr->entries[zr->cur_idx].name, name_sz - 1);
        name_out[name_sz - 1] = '\0';
    }

    if (zip_open_entry_data(zr, &zr->entries[zr->cur_idx]) != 0)
        return -1;

    return 1;
}

int zip_read_entry(ZipReader *zr, unsigned char *buf, int buflen)
{
    if (!zr || !zr->entry_open || buflen <= 0)
        return 0;

    if (zr->cur_method == ZIP_METHOD_STORED)
    {
        unsigned long want = zr->remaining_u;
        int n;

        if (want == 0)
        {
            zip_close_current_entry(zr);

            return 0;
        }

        if (want > (unsigned long)buflen)
            want = (unsigned long)buflen;

        n = (int)fread(buf, 1, (size_t)want, zr->fp);

        if (n <= 0)
        {
            zip_close_current_entry(zr);

            return n == 0 ? 0 : -1;
        }

        zr->remaining_u -= (unsigned long)n;
        zr->remaining_c -= (unsigned long)n;

        return n;
    }

    /* Deflate: feed inflate and return decoded bytes */
    zr->zs.next_out = buf;
    zr->zs.avail_out = (unsigned int)buflen;

    while (zr->zs.avail_out > 0)
    {
        int ret;

        if (zr->zs.avail_in == 0 && zr->remaining_c > 0)
        {
            unsigned long want = zr->remaining_c;
            int n;

            if (want > (unsigned long)sizeof(zr->inbuf))
                want = (unsigned long)sizeof(zr->inbuf);

            n = (int)fread(zr->inbuf, 1, (size_t)want, zr->fp);

            if (n <= 0)
            {
                zip_close_current_entry(zr);

                return -1;
            }

            zr->zs.next_in = zr->inbuf;
            zr->zs.avail_in = (unsigned int)n;
            zr->remaining_c -= (unsigned long)n;
        }

        ret = inflate(&zr->zs, Z_NO_FLUSH);

        if (ret == Z_STREAM_END)
        {
            int produced = buflen - (int)zr->zs.avail_out;

            zip_close_current_entry(zr);

            return produced;
        }

        if (ret != Z_OK)
        {
            zip_close_current_entry(zr);

            return -1;
        }

        if (zr->zs.avail_in == 0 && zr->remaining_c == 0)
        {
            /* Compressed input exhausted without stream end */
            int produced = buflen - (int)zr->zs.avail_out;

            zip_close_current_entry(zr);

            return produced;
        }
    }

    return buflen;
}

int zip_skip_entry(ZipReader *zr)
{
    unsigned char scratch[1024];

    if (!zr || !zr->entry_open)
        return 0;

    for (;;)
    {
        int n = zip_read_entry(zr, scratch, (int)sizeof(scratch));

        if (n <= 0)
            return n;
    }
}

void zip_close_read(ZipReader *zr)
{
    if (!zr)
        return;

    if (zr->entry_open)
        zip_close_current_entry(zr);

    free(zr->entries);
    free(zr);
}

ZipWriter *zip_open_write(FILE *fp)
{
    ZipWriter *zw = NULL;

    if (!fp)
        return NULL;

    zip_crc_init();

    zw = (ZipWriter *)calloc(1, sizeof(*zw));

    if (!zw)
        return NULL;

    zw->fp = fp;
    return zw;
}

static int zw_grow_entries(ZipWriter *zw)
{
    int new_cap;
    ZipWriteEntry *ne = NULL;

    if (zw->n_entries < zw->cap_entries)
        return 0;

    new_cap = zw->cap_entries ? zw->cap_entries * 2 : 8;
    ne = (ZipWriteEntry *)realloc(zw->entries, (size_t)new_cap * sizeof(ZipWriteEntry));

    if (!ne)
        return -1;

    zw->entries = ne;
    zw->cap_entries = new_cap;

    return 0;
}

int zip_start_entry(ZipWriter *zw, const char *name, int method)
{
    size_t nlen;

    if (!zw || !name || zw->entry_open)
        return -1;

    nlen = strlen(name);

    if (nlen >= ZIP_NAME_MAX)
        return -1;

    memset(&zw->cur, 0, sizeof(zw->cur));

    zw->cur.local_offset = zw->pos;
    zw->cur.method = (unsigned int)method;
    zw->cur.flags = GPF_DATA_DESCRIPTOR;

    strncpy(zw->cur.name, name, sizeof(zw->cur.name) - 1);
    zw->cur.name[sizeof(zw->cur.name) - 1] = '\0';

    /* Local header with data-descriptor; sizes+crc filled later */
    zip_put32(zw->fp, SIG_LOCAL);
    zip_put16(zw->fp, ZIP_VER_NEED);
    zip_put16(zw->fp, GPF_DATA_DESCRIPTOR);
    zip_put16(zw->fp, (unsigned int)method);
    zip_put16(zw->fp, 0); /* mod time */
    zip_put16(zw->fp, 0); /* mod date */
    zip_put32(zw->fp, 0); /* crc placeholder */
    zip_put32(zw->fp, 0); /* csize placeholder */
    zip_put32(zw->fp, 0); /* usize placeholder */
    zip_put16(zw->fp, (unsigned int)nlen);
    zip_put16(zw->fp, 0); /* extra field length */

    fwrite(name, 1, nlen, zw->fp);

    zw->pos += 30 + nlen;

    if (method == ZIP_METHOD_DEFLATE)
    {
        memset(&zw->zs, 0, sizeof(zw->zs));

        /* Raw deflate, no zlib header */
        if (deflateInit2(&zw->zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            return -1;

        zw->zs_active = 1;
    }
    else if (method != ZIP_METHOD_STORED)
    {
        return -1;
    }

    zw->entry_open = 1;

    return 0;
}

static int zw_flush_deflate(ZipWriter *zw, int flush_mode)
{
    for (;;)
    {
        int ret;
        int produced;

        zw->zs.next_out = zw->outbuf;
        zw->zs.avail_out = (unsigned int)sizeof(zw->outbuf);

        ret = deflate(&zw->zs, flush_mode);

        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
            return -1;

        produced = (int)sizeof(zw->outbuf) - (int)zw->zs.avail_out;

        if (produced > 0)
        {
            if ((int)fwrite(zw->outbuf, 1, (size_t)produced, zw->fp) != produced)
                return -1;

            zw->pos += (unsigned long)produced;
            zw->cur.csize += (unsigned long)produced;
        }

        if (ret == Z_STREAM_END)
            return 0;

        if (zw->zs.avail_out != 0 && flush_mode == Z_NO_FLUSH)
            return 0;

        if (produced == 0 && ret == Z_BUF_ERROR)
            return 0;
    }
}

int zip_write_entry(ZipWriter *zw, const unsigned char *buf, int buflen)
{
    if (!zw || !zw->entry_open || buflen <= 0)
        return 0;

    zw->cur.usize += (unsigned long)buflen;
    zw->cur.crc = zip_crc_update(zw->cur.crc, buf, buflen);

    if (zw->cur.method == ZIP_METHOD_STORED)
    {
        int n = (int)fwrite(buf, 1, (size_t)buflen, zw->fp);

        if (n != buflen)
            return -1;

        zw->pos += (unsigned long)n;
        zw->cur.csize += (unsigned long)n;

        return n;
    }

    zw->zs.next_in = (unsigned char *)buf;
    zw->zs.avail_in = (unsigned int)buflen;

    if (zw_flush_deflate(zw, Z_NO_FLUSH) != 0)
        return -1;

    return buflen;
}

int zip_end_entry(ZipWriter *zw)
{
    if (!zw || !zw->entry_open)
        return -1;

    if (zw->cur.method == ZIP_METHOD_DEFLATE)
    {
        if (zw_flush_deflate(zw, Z_FINISH) != 0)
        {
            deflateEnd(&zw->zs);

            zw->zs_active = 0;
            zw->entry_open = 0;

            return -1;
        }

        deflateEnd(&zw->zs);
        zw->zs_active = 0;
    }

    /* Data descriptor: signature + crc + csize + usize */
    zip_put32(zw->fp, SIG_DDESC);
    zip_put32(zw->fp, zw->cur.crc);
    zip_put32(zw->fp, zw->cur.csize);
    zip_put32(zw->fp, zw->cur.usize);

    zw->pos += 16;

    if (zw_grow_entries(zw) != 0)
    {
        zw->entry_open = 0;
        return -1;
    }

    zw->entries[zw->n_entries++] = zw->cur;
    zw->entry_open = 0;

    return 0;
}

int zip_write_stored_entry(ZipWriter *zw, const char *name, const unsigned char *data, int len)
{
    size_t nlen;
    unsigned long crc;

    if (!zw || !name || zw->entry_open)
        return -1;

    nlen = strlen(name);

    if (nlen >= ZIP_NAME_MAX)
        return -1;

    zip_crc_init();

    crc = zip_crc_update(0, data, len);

    memset(&zw->cur, 0, sizeof(zw->cur));

    zw->cur.local_offset = zw->pos;
    zw->cur.method = ZIP_METHOD_STORED;
    zw->cur.flags = 0;
    zw->cur.crc = crc;
    zw->cur.csize = (unsigned long)len;
    zw->cur.usize = (unsigned long)len;

    strncpy(zw->cur.name, name, sizeof(zw->cur.name) - 1);
    zw->cur.name[sizeof(zw->cur.name) - 1] = '\0';

    /* Local header with real sizes and CRC; no data descriptor */
    zip_put32(zw->fp, SIG_LOCAL);
    zip_put16(zw->fp, ZIP_VER_NEED);
    zip_put16(zw->fp, 0); /* no flags */
    zip_put16(zw->fp, ZIP_METHOD_STORED);
    zip_put16(zw->fp, 0); /* mod time */
    zip_put16(zw->fp, 0); /* mod date */
    zip_put32(zw->fp, crc);
    zip_put32(zw->fp, (unsigned long)len);
    zip_put32(zw->fp, (unsigned long)len);
    zip_put16(zw->fp, (unsigned int)nlen);
    zip_put16(zw->fp, 0); /* extra field length */

    fwrite(name, 1, nlen, zw->fp);

    zw->pos += 30 + (unsigned long)nlen;

    if (len > 0)
    {
        if ((int)fwrite(data, 1, (size_t)len, zw->fp) != len)
            return -1;

        zw->pos += (unsigned long)len;
    }

    if (zw_grow_entries(zw) != 0)
        return -1;

    zw->entries[zw->n_entries++] = zw->cur;

    return 0;
}

int zip_close_write(ZipWriter *zw)
{
    unsigned long cd_offset;
    unsigned long cd_size = 0;
    int i;

    if (!zw)
        return -1;

    if (zw->entry_open)
    {
        if (zip_end_entry(zw) != 0)
        {
            free(zw->entries);
            free(zw);

            return -1;
        }
    }

    cd_offset = zw->pos;

    for (i = 0; i < zw->n_entries; i++)
    {
        ZipWriteEntry *e = &zw->entries[i];
        size_t nlen = strlen(e->name);
        unsigned long before = zw->pos;

        zip_put32(zw->fp, SIG_CENTRAL);
        zip_put16(zw->fp, ZIP_VER_MADE);
        zip_put16(zw->fp, ZIP_VER_NEED);
        zip_put16(zw->fp, e->flags);
        zip_put16(zw->fp, e->method);
        zip_put16(zw->fp, 0); /* mod time */
        zip_put16(zw->fp, 0); /* mod date */
        zip_put32(zw->fp, e->crc);
        zip_put32(zw->fp, e->csize);
        zip_put32(zw->fp, e->usize);
        zip_put16(zw->fp, (unsigned int)nlen);
        zip_put16(zw->fp, 0); /* extra */
        zip_put16(zw->fp, 0); /* comment */
        zip_put16(zw->fp, 0); /* disk number start */
        zip_put16(zw->fp, 0); /* internal attrs */
        zip_put32(zw->fp, 0); /* external attrs */
        zip_put32(zw->fp, e->local_offset);

        fwrite(e->name, 1, nlen, zw->fp);

        zw->pos += 46 + nlen;
        cd_size += zw->pos - before;
    }

    /* EOCD */
    zip_put32(zw->fp, SIG_EOCD);
    zip_put16(zw->fp, 0); /* this disk */
    zip_put16(zw->fp, 0); /* disk with CD start */
    zip_put16(zw->fp, (unsigned int)zw->n_entries);
    zip_put16(zw->fp, (unsigned int)zw->n_entries);
    zip_put32(zw->fp, cd_size);
    zip_put32(zw->fp, cd_offset);
    zip_put16(zw->fp, 0); /* comment length */

    free(zw->entries);
    free(zw);

    return 0;
}
