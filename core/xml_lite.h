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

#ifndef TE_XML_LITE_H
#define TE_XML_LITE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Event types; text is UTF-8 with entities resolved */
#define XL_START 1
#define XL_END 2
#define XL_TEXT 3
#define XL_SELFCLOSE 4

/* Initial buffer sizes; parser grows on demand, peak persists across events */
#define XL_TAG_MIN 32
#define XL_ATTRS_MIN 8

/* Safe upper hint for fixed-size style buffers in fmt_odt.c; not a parser cap */
#define XL_ATTR_VAL_MAX 128

    /* Start-tag attribute; name/value parser-owned, reused across events, copy to keep */
    typedef struct
    {
        char *name; /* NUL-terminated */
        int name_cap;
        char *value; /* NUL-terminated */
        int value_cap;
    } XlAttr;

    /* Event from xl_parse; buffers parser-owned, valid only inside callback */
    typedef struct
    {
        int type;  /* XL_START / XL_END / XL_TEXT / XL_SELFCLOSE */
        char *tag; /* NUL-terminated, e.g. "w:t" */
        int tag_cap;
        XlAttr *attrs;
        int n_attrs;
        int attrs_cap;
        const char *text;
        int text_len;
    } XlEvent;

    /* Event callback; return 0=continue, non-zero=abort */
    typedef int (*XlEventFn)(void *user, const XlEvent *ev);

    /* Pull-based input; 0=EOF, -1=error */
    typedef int (*XlReadFn)(void *user, unsigned char *buf, int buflen);

    /* Parse XML stream, invoking cb per event; -1 on OOM/parse error */
    int xl_parse(XlReadFn read_fn, void *read_user, XlEventFn cb, void *cb_user);

#ifdef __cplusplus
}
#endif

#endif
