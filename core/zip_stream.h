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

#ifndef TE_ZIP_STREAM_H
#define TE_ZIP_STREAM_H

#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ZIP_METHOD_STORED 0
#define ZIP_METHOD_DEFLATE 8

#define ZIP_NAME_MAX 128

    /* Opaque handle to a ZIP being read entry-by-entry */
    typedef struct ZipReaderStruct ZipReader;

    /* Open ZIP for reading; does NOT close fp */
    ZipReader *zip_open_read(FILE *fp);

    /* Free reader; does NOT close fp */
    void zip_close_read(ZipReader *zr);

    /* Advance to next entry; returns 1=ok, 0=end, -1=error */
    int zip_next_entry(ZipReader *zr, char *name_out, size_t name_sz);

    /* Read uncompressed chunk; returns bytes, 0=end, -1=error */
    int zip_read_entry(ZipReader *zr, unsigned char *buf, int buflen);

    /* Skip remaining bytes of current entry */
    int zip_skip_entry(ZipReader *zr);

    typedef struct ZipWriterStruct ZipWriter;

    /* Open ZIP for writing; does NOT close fp */
    ZipWriter *zip_open_write(FILE *fp);

    /* Begin new entry; returns 0=ok, -1=error */
    int zip_start_entry(ZipWriter *zw, const char *name, int method);

    /* Write bytes to current entry; returns bytes or -1 */
    int zip_write_entry(ZipWriter *zw, const unsigned char *buf, int buflen);

    /* Close current entry, flushing compressor */
    int zip_end_entry(ZipWriter *zw);

    /* Write a complete stored entry in one pass (no data descriptor) */
    int zip_write_stored_entry(ZipWriter *zw, const char *name, const unsigned char *data, int len);

    /* Finish archive: write central directory and EOCD */
    int zip_close_write(ZipWriter *zw);

#ifdef __cplusplus
}
#endif

#endif
