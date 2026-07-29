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

/* Pull-driven SAX XML parser for DOCX/ODT subset; heap-grown buffers, reused event struct */

#include <string.h>
#include <stdlib.h>

#include "xml_lite.h"

/* Lookahead reader ring; kept modest for Amiga stack */
#define XL_RD_BUF 256

typedef struct
{
    XlReadFn read_fn;
    void *read_user;
    unsigned char buf[XL_RD_BUF];
    int pos;
    int len;
    int eof;
} XlIn;

static int xl_refill(XlIn *in)
{
    int n;

    if (in->eof)
        return 0;

    if (in->pos > 0 && in->len > in->pos)
        memmove(in->buf, in->buf + in->pos, (size_t)(in->len - in->pos));

    in->len -= in->pos;
    in->pos = 0;

    n = in->read_fn(in->read_user, in->buf + in->len, XL_RD_BUF - in->len);

    if (n <= 0)
    {
        in->eof = 1;
        return in->len > 0 ? 1 : 0;
    }

    in->len += n;

    return 1;
}

static int xl_peek(XlIn *in)
{
    if (in->pos >= in->len)
    {
        if (!xl_refill(in))
            return -1;
    }

    return (int)in->buf[in->pos];
}

static int xl_get(XlIn *in)
{
    int c = xl_peek(in);

    if (c >= 0)
        in->pos++;

    return c;
}

static int xl_peekn(XlIn *in, int n)
{
    while (in->pos + n >= in->len)
    {
        if (!xl_refill(in))
            return -1;

        if (in->pos + n >= in->len && in->eof)
            return -1;
    }

    return (int)in->buf[in->pos + n];
}

static int xl_is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void xl_skip_space(XlIn *in)
{
    int c;

    while ((c = xl_peek(in)) >= 0 && xl_is_space(c))
        in->pos++;
}

/* Grow char* buffer to need bytes; doubles cap, 0=ok, -1=OOM */
static int xl_grow_buf(char **buf, int *cap, int need)
{
    int new_cap;
    char *nb = NULL;

    if (need <= *cap)
        return 0;

    new_cap = *cap > 0 ? *cap : 16;

    while (new_cap < need)
        new_cap *= 2;

    nb = (char *)realloc(*buf, (size_t)new_cap);

    if (!nb)
        return -1;

    *buf = nb;
    *cap = new_cap;

    return 0;
}

/* Grow attrs array to need slots; new slots zeroed for caller to fill */
static int xl_grow_attrs(XlAttr **attrs, int *cap, int need)
{
    int old_cap = *cap;
    int new_cap;
    XlAttr *na = NULL;
    int i;

    if (need <= old_cap)
        return 0;

    new_cap = old_cap > 0 ? old_cap : XL_ATTRS_MIN;

    while (new_cap < need)
        new_cap *= 2;

    na = (XlAttr *)realloc(*attrs, (size_t)new_cap * sizeof(XlAttr));

    if (!na)
        return -1;

    for (i = old_cap; i < new_cap; i++)
    {
        na[i].name = NULL;
        na[i].name_cap = 0;
        na[i].value = NULL;
        na[i].value_cap = 0;
    }

    *attrs = na;
    *cap = new_cap;

    return 0;
}

/* Encode a Unicode codepoint into UTF-8; returns bytes written or 0 on invalid */
static int xl_encode_utf8(unsigned int cp, char *out)
{
    if (cp < 0x80)
    {
        out[0] = (char)cp;

        return 1;
    }

    if (cp < 0x800)
    {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));

        return 2;
    }

    if (cp < 0x10000)
    {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));

        return 3;
    }

    if (cp < 0x110000)
    {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));

        return 4;
    }

    return 0;
}

/* Read entity ref after '&'; writes 0..4 UTF-8 bytes, 0=unknown (swallowed) */
static int xl_read_entity(XlIn *in, char *out, int outsz)
{
    char name[16];
    int n = 0;
    int c;

    while ((c = xl_get(in)) >= 0 && c != ';' && n < (int)sizeof(name) - 1)
        name[n++] = (char)c;

    name[n] = '\0';

    if (c != ';')
        return 0;

    if (strcmp(name, "amp") == 0)
    {
        if (outsz >= 1)
        {
            out[0] = '&';
            return 1;
        }

        return 0;
    }

    if (strcmp(name, "lt") == 0)
    {
        if (outsz >= 1)
        {
            out[0] = '<';
            return 1;
        }

        return 0;
    }

    if (strcmp(name, "gt") == 0)
    {
        if (outsz >= 1)
        {
            out[0] = '>';
            return 1;
        }

        return 0;
    }

    if (strcmp(name, "quot") == 0)
    {
        if (outsz >= 1)
        {
            out[0] = '"';
            return 1;
        }

        return 0;
    }

    if (strcmp(name, "apos") == 0)
    {
        if (outsz >= 1)
        {
            out[0] = '\'';
            return 1;
        }

        return 0;
    }

    if (name[0] == '#')
    {
        unsigned long cp;

        if (name[1] == 'x' || name[1] == 'X')
            cp = strtoul(name + 2, NULL, 16);
        else
            cp = strtoul(name + 1, NULL, 10);

        if (outsz < 4)
            return 0;

        return xl_encode_utf8((unsigned int)cp, out);
    }

    return 0;
}

/* Read text up to next '<' or EOF, resolving entities; -1 on OOM, '<' unconsumed */
static int xl_read_text(XlIn *in, char **buf, int *cap)
{
    int n = 0;
    int c;

    while ((c = xl_peek(in)) >= 0 && c != '<')
    {
        /* Reserve up to 4 bytes for UTF-8 + terminator */
        if (xl_grow_buf(buf, cap, n + 5) != 0)
            return -1;

        in->pos++;

        if (c == '&')
            n += xl_read_entity(in, *buf + n, *cap - n);
        else
            (*buf)[n++] = (char)c;
    }

    if (xl_grow_buf(buf, cap, n + 1) != 0)
        return -1;

    (*buf)[n] = '\0';

    return n;
}

/* Read name (tag/attr) into growing buffer; stops at space/'/'/'>'/'=', -1 on OOM */
static int xl_read_name(XlIn *in, char **buf, int *cap)
{
    int n = 0;
    int c;

    while ((c = xl_peek(in)) >= 0)
    {
        if (xl_is_space(c) || c == '/' || c == '>' || c == '=')
            break;

        if (xl_grow_buf(buf, cap, n + 2) != 0)
            return -1;

        (*buf)[n++] = (char)c;
        in->pos++;
    }

    if (xl_grow_buf(buf, cap, n + 1) != 0)
        return -1;

    (*buf)[n] = '\0';

    return n;
}

/* Read a quoted attribute value into a growing buffer, resolving entities */
static int xl_read_attr_value(XlIn *in, char **buf, int *cap)
{
    int quote;
    int n = 0;
    int c;

    quote = xl_get(in);

    if (quote != '"' && quote != '\'')
        return -1;

    while ((c = xl_peek(in)) >= 0 && c != quote)
    {
        if (xl_grow_buf(buf, cap, n + 5) != 0)
            return -1;

        in->pos++;

        if (c == '&')
            n += xl_read_entity(in, *buf + n, *cap - n);
        else
            (*buf)[n++] = (char)c;
    }

    if (c == quote)
        in->pos++;

    if (xl_grow_buf(buf, cap, n + 1) != 0)
        return -1;

    (*buf)[n] = '\0';

    return n;
}

/* Skip comment/PI/DOCTYPE/CDATA; 1=CDATA (fills cdata_buf), 0=skipped, -1=OOM */
static int xl_skip_special(XlIn *in, char **cdata_buf, int *cdata_cap, int *cdata_len)
{
    int c;
    int c1, c2;
    int depth;

    if (cdata_len)
        *cdata_len = 0;

    c1 = xl_peekn(in, 0);
    c2 = xl_peekn(in, 1);

    if (c1 == '!' && c2 == '-' && xl_peekn(in, 2) == '-')
    {
        /* Comment: skip until --> */
        in->pos += 3;

        for (;;)
        {
            c = xl_get(in);

            if (c < 0)
                return 0;

            if (c == '-' && xl_peek(in) == '-')
            {
                in->pos++;

                if (xl_peek(in) == '>')
                {
                    in->pos++;
                    return 0;
                }
            }
        }
    }

    if (c1 == '?')
    {
        /* PI or <?xml ?> declaration; skip until ?> */
        in->pos++;

        for (;;)
        {
            c = xl_get(in);

            if (c < 0)
                return 0;

            if (c == '?' && xl_peek(in) == '>')
            {
                in->pos++;
                return 0;
            }
        }
    }

    if (c1 == '!')
    {
        /* CDATA: <![CDATA[ ... ]]> or DOCTYPE: <!DOCTYPE ...> */
        if (xl_peekn(in, 1) == '[' && xl_peekn(in, 2) == 'C' && xl_peekn(in, 3) == 'D')
        {
            int n = 0;

            in->pos += 8;

            for (;;)
            {
                c = xl_get(in);

                if (c < 0)
                    break;

                if (c == ']' && xl_peek(in) == ']')
                {
                    int save = in->pos;

                    in->pos++;

                    if (xl_peek(in) == '>')
                    {
                        in->pos++;
                        break;
                    }

                    in->pos = save;
                }

                if (cdata_buf && xl_grow_buf(cdata_buf, cdata_cap, n + 2) != 0)
                    return -1;

                if (cdata_buf)
                    (*cdata_buf)[n++] = (char)c;
            }

            if (cdata_buf)
            {
                if (xl_grow_buf(cdata_buf, cdata_cap, n + 1) != 0)
                    return -1;

                (*cdata_buf)[n] = '\0';
            }

            if (cdata_len)
                *cdata_len = n;

            return 1;
        }

        /* DOCTYPE or similar: naive skip to '>' handling nested brackets */
        depth = 0;

        in->pos++;

        while ((c = xl_get(in)) >= 0)
        {
            if (c == '<')
                depth++;
            else if (c == '>')
            {
                if (depth == 0)
                    return 0;

                depth--;
            }
        }
    }

    return 0;
}

/* Free everything the event owns; safe to call on a zeroed struct */
static void xl_event_free(XlEvent *ev)
{
    int i;

    if (!ev)
        return;

    if (ev->tag)
    {
        free(ev->tag);
        ev->tag = NULL;
        ev->tag_cap = 0;
    }

    if (ev->attrs)
    {
        for (i = 0; i < ev->attrs_cap; i++)
        {
            if (ev->attrs[i].name)
                free(ev->attrs[i].name);

            if (ev->attrs[i].value)
                free(ev->attrs[i].value);
        }

        free(ev->attrs);

        ev->attrs = NULL;
        ev->attrs_cap = 0;
    }

    ev->n_attrs = 0;
}

/* Reset per-event scalars without freeing bufs; those are reused */
static void xl_event_reset(XlEvent *ev)
{
    ev->type = 0;
    ev->n_attrs = 0;
    ev->text = NULL;
    ev->text_len = 0;

    if (ev->tag && ev->tag_cap > 0)
        ev->tag[0] = '\0';
}

/* Read start-tag attrs; sets XL_SELFCLOSE if self-closing, 0=ok, -1=error */
static int xl_parse_attrs(XlIn *in, XlEvent *ev)
{
    int c;

    for (;;)
    {
        XlAttr *a = NULL;

        xl_skip_space(in);

        c = xl_peek(in);

        if (c < 0)
            return -1;

        if (c == '>')
        {
            in->pos++;
            return 0;
        }

        if (c == '/')
        {
            in->pos++;

            if (xl_peek(in) != '>')
                return -1;

            in->pos++;
            ev->type = XL_SELFCLOSE;

            return 0;
        }

        /* Attribute: name = "value" */
        if (xl_grow_attrs(&ev->attrs, &ev->attrs_cap, ev->n_attrs + 1) != 0)
            return -1;

        a = &ev->attrs[ev->n_attrs];

        if (xl_read_name(in, &a->name, &a->name_cap) < 0)
            return -1;

        xl_skip_space(in);

        if (xl_get(in) != '=')
            return -1;

        xl_skip_space(in);

        if (xl_read_attr_value(in, &a->value, &a->value_cap) < 0)
            return -1;

        ev->n_attrs++;
    }
}

/* Drive parser loop; 0=clean EOF, non-zero=cb abort, -1=parse/OOM error */
static int xl_parse_loop(XlIn *in, XlEvent *ev, char **text_buf, int *text_cap, char **cdata_buf, int *cdata_cap, XlEventFn cb, void *cb_user)
{
    for (;;)
    {
        int c;
        int text_len;
        int rc;

        text_len = xl_read_text(in, text_buf, text_cap);

        if (text_len < 0)
            return -1;

        if (text_len > 0)
        {
            xl_event_reset(ev);

            ev->type = XL_TEXT;
            ev->text = *text_buf;
            ev->text_len = text_len;

            rc = cb(cb_user, ev);

            if (rc != 0)
                return rc;
        }

        c = xl_peek(in);

        if (c < 0)
            return 0;

        /* Consume '<' */
        in->pos++;

        c = xl_peek(in);

        if (c < 0)
            return -1;

        if (c == '/')
        {
            /* End tag </name> */
            in->pos++;

            xl_event_reset(ev);

            ev->type = XL_END;

            if (xl_read_name(in, &ev->tag, &ev->tag_cap) < 0)
                return -1;

            xl_skip_space(in);

            if (xl_get(in) != '>')
                return -1;

            rc = cb(cb_user, ev);

            if (rc != 0)
                return rc;

            continue;
        }

        if (c == '!' || c == '?')
        {
            int cdata_len = 0;
            int is_cdata = xl_skip_special(in, cdata_buf, cdata_cap, &cdata_len);

            if (is_cdata < 0)
                return -1;

            if (is_cdata && cdata_len > 0)
            {
                xl_event_reset(ev);

                ev->type = XL_TEXT;
                ev->text = *cdata_buf;
                ev->text_len = cdata_len;

                rc = cb(cb_user, ev);

                if (rc != 0)
                    return rc;
            }

            continue;
        }

        /* Start or self-closing tag */
        xl_event_reset(ev);

        ev->type = XL_START;

        if (xl_read_name(in, &ev->tag, &ev->tag_cap) < 0)
            return -1;

        if (xl_parse_attrs(in, ev) != 0)
            return -1;

        rc = cb(cb_user, ev);

        if (rc != 0)
            return rc;
    }
}

int xl_parse(XlReadFn read_fn, void *read_user, XlEventFn cb, void *cb_user)
{
    XlIn in;
    XlEvent ev;
    char *text_buf = NULL;
    int text_cap = 0;
    char *cdata_buf = NULL;
    int cdata_cap = 0;
    int rc;

    memset(&in, 0, sizeof(in));
    memset(&ev, 0, sizeof(ev));

    in.read_fn = read_fn;
    in.read_user = read_user;

    /* Pre-alloc the event tag so the common path never allocs mid-tag */
    if (xl_grow_buf(&ev.tag, &ev.tag_cap, XL_TAG_MIN) == 0)
    {
        ev.tag[0] = '\0';
        rc = xl_parse_loop(&in, &ev, &text_buf, &text_cap, &cdata_buf, &cdata_cap, cb, cb_user);
    }
    else
    {
        rc = -1;
    }

    xl_event_free(&ev);
    free(text_buf);
    free(cdata_buf);

    return rc;
}