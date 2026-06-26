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

#include "translate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Backends - each compiled in independently */
#ifdef HAVE_TRANSLATE_HTTP
extern char *translate_http_mymemory(const char *endpoint, const char *api_key, const char *email, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size);

extern char *translate_http_libretranslate(const char *endpoint, const char *api_key, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size);

extern char *translate_http_lingva(const char *endpoint, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size);

extern char *translate_http_deepl(const char *endpoint, const char *api_key, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size);
#endif

#ifdef HAVE_TRANSLATE_STARDICT
#include "translate_stardict.h"
#endif

#ifdef HAVE_TRANSLATE_APERTIUM
extern char *translate_apertium(const char *apertium_bin, const char *from, const char *to, const char *src, int timeout_secs, char *err, int err_size);
#endif

typedef struct
{
    char key[TRANSLATE_CACHE_KEY_MAX]; /* '\0' = empty slot. Format: "from|to|src..." */
    char val[TRANSLATE_CACHE_VAL_MAX]; /* translation text (truncated if too long) */
    char detected_from[8];             /* detected source language, or "" */
    short prev;
    short next;
} TransCacheEntry;

struct TranslateHandle
{
    TranslateOpts opts;
    char endpoint_owned[512];
    char api_key_owned[256];
    char email_owned[128];
    char stardict_path_owned[512];
    char apertium_bin_owned[256];

    short cache_head;
    short cache_tail;
    short cache_count;
    TransCacheEntry cache[TRANSLATE_CACHE_N];

#ifdef HAVE_TRANSLATE_STARDICT
    StarDictHandle *stardict_handle;
    char stardict_from[8];
    char stardict_to[8];
#endif
};

static void cache_init(TranslateHandle *h)
{
    int i;

    h->cache_head = -1;
    h->cache_tail = -1;
    h->cache_count = 0;

    for (i = 0; i < TRANSLATE_CACHE_N; i++)
    {
        h->cache[i].key[0] = '\0';
        h->cache[i].prev = -1;
        h->cache[i].next = -1;
    }
}

static void cache_unlink(TranslateHandle *h, int idx)
{
    TransCacheEntry *e = &h->cache[idx];

    if (e->prev != -1)
        h->cache[e->prev].next = e->next;
    else
        h->cache_head = e->next;

    if (e->next != -1)
        h->cache[e->next].prev = e->prev;
    else
        h->cache_tail = e->prev;

    e->prev = -1;
    e->next = -1;
}

static void cache_push_front(TranslateHandle *h, int idx)
{
    TransCacheEntry *e = &h->cache[idx];

    e->prev = -1;
    e->next = h->cache_head;

    if (h->cache_head != -1)
        h->cache[h->cache_head].prev = (short)idx;

    h->cache_head = (short)idx;

    if (h->cache_tail == -1)
        h->cache_tail = (short)idx;
}

static int cache_find(const TranslateHandle *h, const char *key)
{
    int i;

    for (i = h->cache_head; i != -1; i = h->cache[i].next)
    {
        if (strcmp(h->cache[i].key, key) == 0)
            return i;
    }

    return -1;
}

/* Acquire a cache slot. Prefers an empty one; otherwise evicts the LRU tail */
static int cache_acquire(TranslateHandle *h)
{
    int i;

    if (h->cache_count < TRANSLATE_CACHE_N)
    {
        for (i = 0; i < TRANSLATE_CACHE_N; i++)
        {
            if (h->cache[i].key[0] == '\0')
            {
                h->cache_count++;
                return i;
            }
        }
    }

    i = h->cache_tail;

    if (i < 0 || i >= TRANSLATE_CACHE_N)
    {
        /* Lost track of the list - re-init and use slot 0 */
        cache_init(h);

        h->cache_count = 1;
        return 0;
    }

    cache_unlink(h, i);

    return i;
}

/* Build cache key. Truncates src to fit. Returns 1 if key is usable */
static int build_cache_key(char *out, int out_size, const char *from, const char *to, const char *src)
{
    int n;
    int room;
    int sl;

    n = snprintf(out, (size_t)out_size, "%s|%s|", from, to);

    if (n < 0 || n >= out_size - 1)
        return 0;

    room = out_size - n - 1;
    sl = (int)strlen(src);

    if (sl > room)
        sl = room;

    memcpy(out + n, src, (size_t)sl);
    out[n + sl] = '\0';

    return 1;
}

void translate_cache_clear(TranslateHandle *h)
{
    if (h)
        cache_init(h);
}

static void copy_opts(TranslateHandle *h, const TranslateOpts *src)
{
    h->opts.backend = src ? src->backend : TRANSLATE_BACKEND_NONE;
    h->opts.timeout_secs = (src && src->timeout_secs > 0) ? src->timeout_secs : 10;

    h->endpoint_owned[0] = '\0';
    h->api_key_owned[0] = '\0';
    h->email_owned[0] = '\0';
    h->stardict_path_owned[0] = '\0';
    h->apertium_bin_owned[0] = '\0';

    if (src && src->endpoint)
    {
        strncpy(h->endpoint_owned, src->endpoint, sizeof(h->endpoint_owned) - 1);
        h->endpoint_owned[sizeof(h->endpoint_owned) - 1] = '\0';
    }

    if (src && src->api_key)
    {
        strncpy(h->api_key_owned, src->api_key, sizeof(h->api_key_owned) - 1);
        h->api_key_owned[sizeof(h->api_key_owned) - 1] = '\0';
    }

    if (src && src->email)
    {
        strncpy(h->email_owned, src->email, sizeof(h->email_owned) - 1);
        h->email_owned[sizeof(h->email_owned) - 1] = '\0';
    }

    if (src && src->stardict_path)
    {
        strncpy(h->stardict_path_owned, src->stardict_path, sizeof(h->stardict_path_owned) - 1);
        h->stardict_path_owned[sizeof(h->stardict_path_owned) - 1] = '\0';
    }

    if (src && src->apertium_bin)
    {
        strncpy(h->apertium_bin_owned, src->apertium_bin, sizeof(h->apertium_bin_owned) - 1);
        h->apertium_bin_owned[sizeof(h->apertium_bin_owned) - 1] = '\0';
    }

    /* Refresh borrowed string pointers to point at our owned copies */
    h->opts.endpoint = h->endpoint_owned[0] ? h->endpoint_owned : NULL;
    h->opts.api_key = h->api_key_owned[0] ? h->api_key_owned : NULL;
    h->opts.email = h->email_owned[0] ? h->email_owned : NULL;
    h->opts.stardict_path = h->stardict_path_owned[0] ? h->stardict_path_owned : NULL;
    h->opts.apertium_bin = h->apertium_bin_owned[0] ? h->apertium_bin_owned : NULL;
}

TranslateHandle *translate_new(const TranslateOpts *opts)
{
    TranslateHandle *h = NULL;

    h = (TranslateHandle *)calloc(1, sizeof(*h));

    if (!h)
        return NULL;

    copy_opts(h, opts);
    cache_init(h);

    return h;
}

void translate_free(TranslateHandle *h)
{
    if (!h)
        return;

#ifdef HAVE_TRANSLATE_STARDICT
    if (h->stardict_handle)
    {
        translate_stardict_close(h->stardict_handle);
        h->stardict_handle = NULL;
    }
#endif

    free(h);
}

int translate_set_opts(TranslateHandle *h, const TranslateOpts *opts)
{
    int backend_changed;

    if (!h)
        return -1;

    backend_changed = (!opts) || h->opts.backend != opts->backend || strcmp(h->endpoint_owned, opts->endpoint ? opts->endpoint : "") != 0 || strcmp(h->stardict_path_owned, opts->stardict_path ? opts->stardict_path : "") != 0;

    copy_opts(h, opts);

    if (backend_changed)
    {
        translate_cache_clear(h);

#ifdef HAVE_TRANSLATE_STARDICT
        if (h->stardict_handle)
        {
            translate_stardict_close(h->stardict_handle);

            h->stardict_handle = NULL;
            h->stardict_from[0] = '\0';
            h->stardict_to[0] = '\0';
        }
#endif
    }

    return 0;
}

/* Lowercase ASCII in-place into dst. Returns dst */
static char *lang_normalize(const char *lang, char *dst, int dst_size)
{
    int i = 0;

    if (!lang)
        lang = "";

    while (lang[i] && i < dst_size - 1)
    {
        char c = lang[i];

        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);

        dst[i] = c;
        i++;
    }

    dst[i] = '\0';

    return dst;
}

/* Per-backend safe chunk budget (headroom for URL-encoding overhead) */
static int backend_max_chunk(TranslateBackend b)
{
    switch (b)
    {
    case TRANSLATE_BACKEND_MYMEMORY:
        return 450; /* hard 500 limit */
    case TRANSLATE_BACKEND_LIBRETRANSLATE:
        return 5000;
    case TRANSLATE_BACKEND_LINGVA:
        return 1500; /* path-based URL */
    case TRANSLATE_BACKEND_DEEPL:
        return 5000; /* hard 128 KiB, chunk for latency */
    case TRANSLATE_BACKEND_STARDICT:
        return 64;
    case TRANSLATE_BACKEND_APERTIUM:
        return 4096;
    default:
        return 256;
    }
}

/* Find last separator in window at priority, return chunk/sep lengths */
static int find_break_at_prio(const char *src, int offset, int span, int prio, int *out_chunk_len, int *out_sep_len)
{
    int i;
    int last_chunk = -1;
    int last_sep = 0;
    char c;
    char next;

    for (i = 0; i < span; i++)
    {
        c = src[offset + i];
        next = (i + 1 < span) ? src[offset + i + 1] : '\0';

        switch (prio)
        {
        case 5: /* \n\n */
            if (c == '\n' && next == '\n')
            {
                last_chunk = i;
                last_sep = 2;
            }

            break;
        case 4: /* \n */
            if (c == '\n')
            {
                last_chunk = i;
                last_sep = 1;
            }

            break;
        case 3: /* sentence end */
            if ((c == '.' || c == '!' || c == '?') && (next == ' ' || next == '\t'))
            {
                last_chunk = i + 1;
                last_sep = 1;
            }

            break;
        case 2: /* clause */
            if ((c == ',' || c == ';') && next == ' ')
            {
                last_chunk = i + 1;
                last_sep = 1;
            }

            break;
        case 1: /* word */
            if (c == ' ')
            {
                last_chunk = i;
                last_sep = 1;
            }

            break;
        }
    }

    if (last_chunk < 0)
        return 0;

    *out_chunk_len = last_chunk;
    *out_sep_len = last_sep;

    return 1;
}

/* Find next chunk end, write chunk/sep lengths, return next offset */
static int next_chunk(const char *src, int src_len, int offset, int max_chunk, int *out_chunk_len, int *out_sep_len)
{
    int remaining = src_len - offset;
    int span;
    int prio;

    if (remaining <= 0)
    {
        *out_chunk_len = 0;
        *out_sep_len = 0;
        return offset;
    }

    if (remaining <= max_chunk)
    {
        *out_chunk_len = remaining;
        *out_sep_len = 0;
        return offset + remaining;
    }

    span = max_chunk;

    /* Try priorities high to low, take first break point in window */
    for (prio = 5; prio >= 1; prio--)
    {
        if (find_break_at_prio(src, offset, span, prio, out_chunk_len, out_sep_len))
        {
            return offset + *out_chunk_len + *out_sep_len;
        }
    }

    /* No natural break, hard truncate at max_chunk (rare) */
    *out_chunk_len = max_chunk;
    *out_sep_len = 0;

    return offset + max_chunk;
}

/* Dispatch one backend call for one chunk, return malloc'd result or NULL */
static char *call_backend_once(TranslateHandle *h, const char *from, const char *to, const char *src, char *detected, int detected_size, char *err, int err_size)
{
    char *result = NULL;

    if (detected_size > 0)
        detected[0] = '\0';

    if (err_size > 0)
        err[0] = '\0';

    switch (h->opts.backend)
    {
    case TRANSLATE_BACKEND_MYMEMORY:
#ifdef HAVE_TRANSLATE_HTTP
        result = translate_http_mymemory(h->opts.endpoint, h->opts.api_key, h->opts.email, from, to, src, h->opts.timeout_secs, detected, detected_size, err, err_size);
#else
        snprintf(err, err_size, "HTTP backends not built in");
#endif
        break;

    case TRANSLATE_BACKEND_LIBRETRANSLATE:
#ifdef HAVE_TRANSLATE_HTTP
        result = translate_http_libretranslate(h->opts.endpoint, h->opts.api_key, from, to, src, h->opts.timeout_secs, detected, detected_size, err, err_size);
#else
        snprintf(err, err_size, "HTTP backends not built in");
#endif
        break;

    case TRANSLATE_BACKEND_LINGVA:
#ifdef HAVE_TRANSLATE_HTTP
        result = translate_http_lingva(h->opts.endpoint, from, to, src, h->opts.timeout_secs, detected, detected_size, err, err_size);
#else
        snprintf(err, err_size, "HTTP backends not built in");
#endif
        break;

    case TRANSLATE_BACKEND_DEEPL:
#ifdef HAVE_TRANSLATE_HTTP
        result = translate_http_deepl(h->opts.endpoint, h->opts.api_key, from, to, src, h->opts.timeout_secs, detected, detected_size, err, err_size);
#else
        snprintf(err, err_size, "HTTP backends not built in");
#endif
        break;

    case TRANSLATE_BACKEND_STARDICT:
#ifdef HAVE_TRANSLATE_STARDICT
        if (!h->stardict_handle || strcmp(h->stardict_from, from) != 0 || strcmp(h->stardict_to, to) != 0)
        {
            if (h->stardict_handle)
            {
                translate_stardict_close(h->stardict_handle);
                h->stardict_handle = NULL;
            }
            if (translate_stardict_open(h->opts.stardict_path, from, to, &h->stardict_handle, err, err_size) == 0)
            {
                strncpy(h->stardict_from, from, sizeof(h->stardict_from) - 1);
                h->stardict_from[sizeof(h->stardict_from) - 1] = '\0';

                strncpy(h->stardict_to, to, sizeof(h->stardict_to) - 1);
                h->stardict_to[sizeof(h->stardict_to) - 1] = '\0';
            }
        }
        if (h->stardict_handle)
            result = translate_stardict_lookup(h->stardict_handle, src, err, err_size);
        else if (!err[0])
            snprintf(err, err_size, "stardict not opened");
        if (detected_size > 0)
        {
            strncpy(detected, from, detected_size - 1);
            detected[detected_size - 1] = '\0';
        }
#else
        snprintf(err, err_size, "StarDict backend not built in");
#endif
        break;

    case TRANSLATE_BACKEND_APERTIUM:
#ifdef HAVE_TRANSLATE_APERTIUM
        result = translate_apertium(h->opts.apertium_bin, from, to, src, h->opts.timeout_secs, err, err_size);
        if (detected_size > 0)
        {
            strncpy(detected, from, detected_size - 1);
            detected[detected_size - 1] = '\0';
        }
#else
        snprintf(err, err_size, "Apertium backend not built in");
#endif
        break;

    case TRANSLATE_BACKEND_NONE:
    default:
        snprintf(err, err_size, "no translator backend configured");
        break;
    }

    return result;
}

/* Translate src in chunks, join with separators, return malloc'd result or NULL */
static char *translate_chunked(TranslateHandle *h, const char *from, const char *to, const char *src, int src_len, int max_chunk, char *detected, int detected_size, char *err, int err_size)
{
    char *out = NULL;
    size_t out_cap = 0;
    size_t out_len = 0;
    char chunk_buf[8192];
    char chunk_from[8];
    char chunk_err[128];
    int offset = 0;
    int chunk_len = 0;
    int sep_len = 0;
    int next_off;
    char *piece = NULL;
    size_t piece_len;
    int n_chunks = 0;

    if (max_chunk > (int)sizeof(chunk_buf) - 1)
        max_chunk = (int)sizeof(chunk_buf) - 1;

    while (offset < src_len)
    {
        next_off = next_chunk(src, src_len, offset, max_chunk, &chunk_len, &sep_len);

        if (chunk_len <= 0)
            break;

        memcpy(chunk_buf, src + offset, (size_t)chunk_len);
        chunk_buf[chunk_len] = '\0';

        chunk_from[0] = '\0';
        chunk_err[0] = '\0';

        piece = call_backend_once(h, from, to, chunk_buf, chunk_from, sizeof(chunk_from), chunk_err, sizeof(chunk_err));

        if (!piece)
        {
            if (out)
                free(out);

            if (err_size > 0)
            {
                snprintf(err, (size_t)err_size, "chunk %d failed: %s", n_chunks + 1, chunk_err[0] ? chunk_err : "no error");
            }

            return NULL;
        }

        if (n_chunks == 0 && detected_size > 0 && chunk_from[0])
        {
            strncpy(detected, chunk_from, detected_size - 1);
            detected[detected_size - 1] = '\0';
        }

        piece_len = strlen(piece);

        /* Grow output buffer for piece + separator + NUL */
        if (out_len + piece_len + sep_len + 1 > out_cap)
        {
            size_t new_cap = out_cap ? out_cap * 2 : 4096;
            char *new_out = NULL;

            while (new_cap < out_len + piece_len + sep_len + 1)
                new_cap *= 2;

            new_out = (char *)realloc(out, new_cap);

            if (!new_out)
            {
                free(piece);

                if (out)
                    free(out);

                if (err_size > 0)
                    snprintf(err, (size_t)err_size, "out of memory");

                return NULL;
            }

            out = new_out;
            out_cap = new_cap;
        }

        memcpy(out + out_len, piece, piece_len);
        out_len += piece_len;

        free(piece);

        if (sep_len > 0)
        {
            memcpy(out + out_len, src + offset + chunk_len, (size_t)sep_len);
            out_len += sep_len;
        }

        out[out_len] = '\0';

        offset = next_off;
        n_chunks++;
    }

    if (!out)
    {
        /* Empty source (shouldn't reach here because translate_text guards on src[0]) - return empty string */
        out = (char *)malloc(1);

        if (out)
            out[0] = '\0';
    }

    return out;
}

char *translate_text(TranslateHandle *h, const char *from_lang, const char *to_lang, const char *src, char *out_from_lang_buf, int out_from_lang_buf_size, char *err_buf, int err_buf_size)
{
    char from[8], to[8];
    char key[TRANSLATE_CACHE_KEY_MAX];
    int cache_idx;
    char *result = NULL;
    char local_err[128];
    char detected[8];
    size_t vl;
    int src_len;
    int max_chunk;

    if (err_buf && err_buf_size > 0)
        err_buf[0] = '\0';

    if (out_from_lang_buf && out_from_lang_buf_size > 0)
        out_from_lang_buf[0] = '\0';

    if (!h || !to_lang || !to_lang[0] || !src || !src[0])
    {
        if (err_buf && err_buf_size > 0)
            snprintf(err_buf, (size_t)err_buf_size, "invalid args");

        return NULL;
    }

    lang_normalize(from_lang ? from_lang : "auto", from, sizeof(from));
    lang_normalize(to_lang, to, sizeof(to));

    if (strcmp(from, to) == 0 && strcmp(from, "auto") != 0)
    {
        /* No-op - just dup the source */
        size_t sl = strlen(src);

        result = (char *)malloc(sl + 1);

        if (!result)
        {
            if (err_buf && err_buf_size > 0)
                snprintf(err_buf, (size_t)err_buf_size, "out of memory");

            return NULL;
        }

        memcpy(result, src, sl + 1);

        if (out_from_lang_buf && out_from_lang_buf_size > 0)
        {
            strncpy(out_from_lang_buf, from, (size_t)out_from_lang_buf_size - 1);
            out_from_lang_buf[out_from_lang_buf_size - 1] = '\0';
        }

        return result;
    }

    /* Cache lookup */
    if (build_cache_key(key, sizeof(key), from, to, src))
    {
        cache_idx = cache_find(h, key);

        if (cache_idx != -1)
        {
            TransCacheEntry *e = &h->cache[cache_idx];
            size_t vl = strlen(e->val);

            cache_unlink(h, cache_idx);
            cache_push_front(h, cache_idx);

            result = (char *)malloc(vl + 1);

            if (!result)
            {
                if (err_buf && err_buf_size > 0)
                    snprintf(err_buf, (size_t)err_buf_size, "out of memory");

                return NULL;
            }

            memcpy(result, e->val, vl + 1);

            if (out_from_lang_buf && out_from_lang_buf_size > 0)
            {
                strncpy(out_from_lang_buf, e->detected_from, (size_t)out_from_lang_buf_size - 1);
                out_from_lang_buf[out_from_lang_buf_size - 1] = '\0';
            }

            return result;
        }
    }

    detected[0] = '\0';
    local_err[0] = '\0';

    /* Chunker dispatch */
    src_len = (int)strlen(src);
    max_chunk = backend_max_chunk(h->opts.backend);

    if (src_len <= max_chunk)
        result = call_backend_once(h, from, to, src, detected, sizeof(detected), local_err, sizeof(local_err));
    else

        result = translate_chunked(h, from, to, src, src_len, max_chunk, detected, sizeof(detected), local_err, sizeof(local_err));

    if (!result)
    {
        if (err_buf && err_buf_size > 0)
        {
            strncpy(err_buf, local_err[0] ? local_err : "translation failed", (size_t)err_buf_size - 1);
            err_buf[err_buf_size - 1] = '\0';
        }

        return NULL;
    }

    if (out_from_lang_buf && out_from_lang_buf_size > 0)
    {
        strncpy(out_from_lang_buf, detected[0] ? detected : from, (size_t)out_from_lang_buf_size - 1);
        out_from_lang_buf[out_from_lang_buf_size - 1] = '\0';
    }

    /* Cache the result if it fits */
    vl = strlen(result);

    if (vl < TRANSLATE_CACHE_VAL_MAX && build_cache_key(key, sizeof(key), from, to, src))
    {
        int idx = cache_acquire(h);
        TransCacheEntry *e = &h->cache[idx];

        strncpy(e->key, key, sizeof(e->key) - 1);
        e->key[sizeof(e->key) - 1] = '\0';

        memcpy(e->val, result, vl + 1);

        strncpy(e->detected_from, detected[0] ? detected : from, sizeof(e->detected_from) - 1);

        e->detected_from[sizeof(e->detected_from) - 1] = '\0';

        cache_push_front(h, idx);
    }

    return result;
}

int translate_is_available(void)
{
#if defined(HAVE_TRANSLATE_HTTP) || defined(HAVE_TRANSLATE_STARDICT) || defined(HAVE_TRANSLATE_APERTIUM)
    return 1;
#else
    return 0;
#endif
}

const char *translate_backend_name(TranslateBackend backend)
{
    switch (backend)
    {
    case TRANSLATE_BACKEND_MYMEMORY:
        return "mymemory";
    case TRANSLATE_BACKEND_LIBRETRANSLATE:
        return "libretranslate";
    case TRANSLATE_BACKEND_LINGVA:
        return "lingva";
    case TRANSLATE_BACKEND_DEEPL:
        return "deepl";
    case TRANSLATE_BACKEND_STARDICT:
        return "stardict";
    case TRANSLATE_BACKEND_APERTIUM:
        return "apertium";
    case TRANSLATE_BACKEND_NONE:
    default:
        return "none";
    }
}

TranslateBackend translate_backend_parse(const char *name)
{
    if (!name)
        return TRANSLATE_BACKEND_NONE;

#ifdef _WIN32
#define CI_EQ(a, b) (_stricmp((a), (b)) == 0)
#else
#define CI_EQ(a, b) (strcasecmp((a), (b)) == 0)
#endif

    if (CI_EQ(name, "mymemory"))
        return TRANSLATE_BACKEND_MYMEMORY;

    if (CI_EQ(name, "libretranslate"))
        return TRANSLATE_BACKEND_LIBRETRANSLATE;

    if (CI_EQ(name, "lingva"))
        return TRANSLATE_BACKEND_LINGVA;

    if (CI_EQ(name, "deepl"))
        return TRANSLATE_BACKEND_DEEPL;

    if (CI_EQ(name, "stardict"))
        return TRANSLATE_BACKEND_STARDICT;

    if (CI_EQ(name, "apertium"))
        return TRANSLATE_BACKEND_APERTIUM;

#undef CI_EQ

    return TRANSLATE_BACKEND_NONE;
}

const char *const *translate_supported_langs(TranslateHandle *h)
{
    /* Common ISO 639-1 codes supported by all 3 HTTP backends. StarDict
     * and Apertium depend on which dictionaries are installed */
    static const char *const langs[] = {
        "auto", "en", "es", "fr", "de", "it", "pt", "ca", "gl", "eu",
        "nl", "pl", "ru", "uk", "sv", "da", "no", "fi", "tr", "ar",
        "ja", "ko", "zh", "hi", "ro", "cs", "el", "he", NULL};

    return langs;
}

int translate_suggest(TranslateHandle *h, const char *word, char **items, int max)
{
#ifdef HAVE_TRANSLATE_STARDICT
    if (!h || !word || !items || max <= 0)
        return 0;

    if (h->opts.backend != TRANSLATE_BACKEND_STARDICT)
        return 0;

    if (!h->stardict_handle)
        return 0;

    return translate_stardict_suggest(h->stardict_handle, word, items, max);
#else

    return 0;
#endif
}

int translate_reverse(TranslateHandle *h, const char *target, char **items, int max, int max_scan)
{
#ifdef HAVE_TRANSLATE_STARDICT
    if (!h || !target || !items || max <= 0)
        return 0;

    if (h->opts.backend != TRANSLATE_BACKEND_STARDICT)
        return 0;

    if (!h->stardict_handle)
        return 0;

    return translate_stardict_reverse(h->stardict_handle, target, items, max, max_scan);
#else

    return 0;
#endif
}
