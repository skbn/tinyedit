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

#include "../core/http_client.h"
#include "json_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#define MYMEMORY_DEFAULT_ENDPOINT "https://api.mymemory.translated.net"
#define LIBRETRANSLATE_DEFAULT_ENDPOINT "https://libretranslate.com"
#define LINGVA_DEFAULT_ENDPOINT "https://lingva.lunar.icu"
#define DEEPL_DEFAULT_ENDPOINT "https://api-free.deepl.com"

/* MyMemory rejects requests >500 bytes per call. Keep headroom for langpair, key, etc */
#define HTTP_TR_MAX_SRC_BYTES 480

/* Lingva uses URL-based API (GET) with text in path. URL encoding expands significantly (%20 for space, etc). Allow more bytes for Lingva */
#define LINGVA_MAX_SRC_BYTES 2048

/* Exported (no header for these yet; declared here matches the extern decls in translate.c) */
char *translate_http_mymemory(const char *endpoint, const char *api_key, const char *email, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size);

char *translate_http_libretranslate(const char *endpoint, const char *api_key, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size);

char *translate_http_lingva(const char *endpoint, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size);

char *translate_http_deepl(const char *endpoint, const char *api_key, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size);

static void set_err(char *err, int err_size, const char *fmt, ...)
{
    va_list ap;

    if (!err || err_size <= 0)
        return;

    va_start(ap, fmt);
    vsnprintf(err, (size_t)err_size, fmt, ap);
    va_end(ap);
}

char *translate_http_mymemory(const char *endpoint, const char *api_key, const char *email, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size)
{
    char url[2048];
    char src_enc[HTTP_TR_MAX_SRC_BYTES * 3 + 1];
    char key_enc[256];
    char email_enc[160];
    HttpResponse r;
    int rc;
    int n;
    int m;
    char *translated = NULL;
    char *details = NULL;
    const char *base = NULL;
    char *status_str = NULL;
    int rs = 0;

    memset(&r, 0, sizeof(r));

    if (!from || !to || !src)
    {
        set_err(err, err_size, "invalid args");
        return NULL;
    }

    if (http_url_encode(src, src_enc, sizeof(src_enc)) < 0)
    {
        set_err(err, err_size, "URL encoding failed");
        return NULL;
    }

    base = (endpoint && endpoint[0]) ? endpoint : MYMEMORY_DEFAULT_ENDPOINT;

    n = snprintf(url, sizeof(url), "%s/get?q=%s&langpair=%s|%s", base, src_enc, strcmp(from, "auto") == 0 ? "Autodetect" : from, to);

    if (n < 0 || n >= (int)sizeof(url))
    {
        set_err(err, err_size, "URL too long");
        return NULL;
    }

    if (email && email[0])
    {
        if (http_url_encode(email, email_enc, sizeof(email_enc)) >= 0)
        {
            m = snprintf(url + n, sizeof(url) - n, "&de=%s", email_enc);

            if (m > 0 && m < (int)sizeof(url) - n)
                n += m;
        }
    }

    if (api_key && api_key[0])
    {
        if (http_url_encode(api_key, key_enc, sizeof(key_enc)) >= 0)
        {
            m = snprintf(url + n, sizeof(url) - n, "&key=%s", key_enc);

            if (m > 0 && m < (int)sizeof(url) - n)
                n += m;
        }
    }

    rc = http_get(url, NULL, timeout_secs, &r);

    if (rc != HTTP_OK)
    {
        set_err(err, err_size, "HTTP error: %s", http_strerror(rc));
        http_response_free(&r);
        return NULL;
    }

    if (r.status > 0 && (r.status < 200 || r.status >= 300))
    {
        set_err(err, err_size, "MyMemory HTTP %d", r.status);
        http_response_free(&r);
        return NULL;
    }

    if (!r.body || !r.body[0])
    {
        set_err(err, err_size, "empty response");
        http_response_free(&r);
        return NULL;
    }

    /* MyMemory always returns HTTP 200 even for quota/lang errors */
    details = json_extract_string(r.body, "responseDetails");

    status_str = json_extract_string(r.body, "responseStatus");
    rs = status_str ? atoi(status_str) : 0;

    free(status_str);

    if (rs && (rs < 200 || rs >= 300))
    {
        set_err(err, err_size, "MyMemory: %s (responseStatus=%d)", (details && details[0]) ? details : "error", rs);

        free(details);

        http_response_free(&r);
        return NULL;
    }

    /* Now read the actual translation. It lives at responseData.translatedText */
    translated = json_extract_string(r.body, "responseData.translatedText");

    if (!translated || !translated[0])
    {
        if (details && details[0])
            set_err(err, err_size, "MyMemory: %s", details);
        else
            set_err(err, err_size, "no translation in response");

        free(translated);
        free(details);
        http_response_free(&r);
        return NULL;
    }

    free(details);

    /* MyMemory doesn't echo a detected source language - keep the user's from-lang as-is (or empty if "auto") */
    if (out_from && out_from_size > 0)
    {
        strncpy(out_from, from, (size_t)out_from_size - 1);
        out_from[out_from_size - 1] = '\0';
    }

    http_response_free(&r);
    return translated;
}

char *translate_http_libretranslate(const char *endpoint, const char *api_key, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size)
{
    char url[512];
    char *req_body = NULL;
    HttpResponse r;
    int rc;
    char *translated = NULL;
    const char *base = NULL;
    const char *keys[5];
    const char *values[5];
    int key_count;
    char *detected_lang = NULL;

    memset(&r, 0, sizeof(r));

    if (!from || !to || !src)
    {
        set_err(err, err_size, "invalid args");
        return NULL;
    }

    base = (endpoint && endpoint[0]) ? endpoint : LIBRETRANSLATE_DEFAULT_ENDPOINT;

    snprintf(url, sizeof(url), "%s/translate", base);

    key_count = 0;
    keys[key_count] = "q";
    values[key_count] = src;

    key_count++;
    keys[key_count] = "source";
    values[key_count] = from;

    key_count++;
    keys[key_count] = "target";
    values[key_count] = to;

    key_count++;
    keys[key_count] = "format";
    values[key_count] = "text";

    key_count++;

    if (api_key && api_key[0])
    {
        keys[key_count] = "api_key";
        values[key_count] = api_key;
        key_count++;
    }

    req_body = json_build_simple_request(keys, values, key_count);

    if (!req_body)
    {
        set_err(err, err_size, "JSON build failed");
        return NULL;
    }

    rc = http_post(url, req_body, (int)strlen(req_body), "application/json", NULL, timeout_secs, &r);

    free(req_body);

    if (rc != HTTP_OK)
    {
        set_err(err, err_size, "HTTP error: %s", http_strerror(rc));
        http_response_free(&r);
        return NULL;
    }

    if (r.status > 0 && (r.status < 200 || r.status >= 300))
    {
        char *err_msg = r.body ? json_extract_string(r.body, "error") : NULL;

        if (err_msg)
        {
            set_err(err, err_size, "LibreTranslate %d: %s", r.status, err_msg);
            free(err_msg);
        }
        else
        {
            set_err(err, err_size, "LibreTranslate HTTP %d", r.status);
        }

        http_response_free(&r);
        return NULL;
    }

    if (!r.body || !r.body[0])
    {
        set_err(err, err_size, "empty response");
        http_response_free(&r);
        return NULL;
    }

    translated = json_extract_string(r.body, "translatedText");

    if (!translated || !translated[0])
    {
        char *err_msg = json_extract_string(r.body, "error");

        if (err_msg && err_msg[0])
            set_err(err, err_size, "LibreTranslate: %s", err_msg);
        else
            set_err(err, err_size, "no translation in response");

        free(err_msg);
        free(translated);
        http_response_free(&r);
        return NULL;
    }

    if (out_from && out_from_size > 0)
    {
        /* Works now that json_extract_string handles dot-paths */
        detected_lang = json_extract_string(r.body, "detectedLanguage.language");

        if (detected_lang && detected_lang[0])
        {
            strncpy(out_from, detected_lang, (size_t)out_from_size - 1);
            out_from[out_from_size - 1] = '\0';
        }
        else
        {
            strncpy(out_from, from, (size_t)out_from_size - 1);
            out_from[out_from_size - 1] = '\0';
        }

        free(detected_lang);
    }

    http_response_free(&r);
    return translated;
}

char *translate_http_lingva(const char *endpoint, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size)
{
    char url[8192];
    char body[LINGVA_MAX_SRC_BYTES * 3 + 1];
    HttpResponse r;
    int rc;
    int n;
    char *translated = NULL;
    char *detected = NULL;
    const char *base = NULL;

    memset(&r, 0, sizeof(r));

    if (!from || !to || !src)
    {
        set_err(err, err_size, "invalid args");
        return NULL;
    }

    /* Chunker handles length/newlines, encode src verbatim */
    if (http_url_encode(src, body, sizeof(body)) < 0)
    {
        set_err(err, err_size, "URL encoding failed");
        return NULL;
    }

    base = (endpoint && endpoint[0]) ? endpoint : LINGVA_DEFAULT_ENDPOINT;

    n = snprintf(url, sizeof(url), "%s/api/v1/%s/%s/%s", base, from, to, body);

    if (n < 0 || n >= (int)sizeof(url))
    {
        set_err(err, err_size, "URL too long");
        return NULL;
    }

    rc = http_get(url, NULL, timeout_secs, &r);

    if (rc != HTTP_OK)
    {
        set_err(err, err_size, "HTTP error: %s", http_strerror(rc));
        http_response_free(&r);
        return NULL;
    }

    if (r.status > 0 && (r.status < 200 || r.status >= 300))
    {
        set_err(err, err_size, "Lingva HTTP %d", r.status);
        http_response_free(&r);
        return NULL;
    }

    if (!r.body || !r.body[0])
    {
        set_err(err, err_size, "empty response");
        http_response_free(&r);
        return NULL;
    }

    translated = json_extract_string(r.body, "translation");

    if (!translated || !translated[0])
    {
        set_err(err, err_size, "no translation in response");

        free(translated);
        http_response_free(&r);
        return NULL;
    }

    if (out_from && out_from_size > 0)
    {
        /* Lingva returns info.detectedSource when source was "auto" */
        detected = json_extract_string(r.body, "info.detectedSource");

        if (detected && detected[0])
        {
            strncpy(out_from, detected, (size_t)out_from_size - 1);
            out_from[out_from_size - 1] = '\0';
        }
        else
        {
            strncpy(out_from, from, (size_t)out_from_size - 1);
            out_from[out_from_size - 1] = '\0';
        }

        free(detected);
    }

    http_response_free(&r);
    return translated;
}

static int deepl_lang_to_target(const char *iso, char *out, int out_size)
{
    int i;

    if (!iso || !out || out_size < 8)
        return -1;

    /* Common short codes that need a regional variant on target */
    if (strcasecmp(iso, "en") == 0)
    {
        strcpy(out, "EN-US");
        return 0;
    }

    if (strcasecmp(iso, "en-us") == 0)
    {
        strcpy(out, "EN-US");
        return 0;
    }

    if (strcasecmp(iso, "en-gb") == 0)
    {
        strcpy(out, "EN-GB");
        return 0;
    }

    if (strcasecmp(iso, "pt") == 0)
    {
        strcpy(out, "PT-PT");
        return 0;
    }

    if (strcasecmp(iso, "pt-pt") == 0)
    {
        strcpy(out, "PT-PT");
        return 0;
    }

    if (strcasecmp(iso, "pt-br") == 0)
    {
        strcpy(out, "PT-BR");
        return 0;
    }

    if (strcasecmp(iso, "zh") == 0)
    {
        strcpy(out, "ZH-HANS");
        return 0;
    }

    if (strcasecmp(iso, "zh-hans") == 0)
    {
        strcpy(out, "ZH-HANS");
        return 0;
    }

    if (strcasecmp(iso, "zh-hant") == 0)
    {
        strcpy(out, "ZH-HANT");
        return 0;
    }

    /* Otherwise uppercase the input (up to 7 chars + NUL) */
    for (i = 0; iso[i] && i < out_size - 1; i++)
        out[i] = (char)toupper((unsigned char)iso[i]);

    out[i] = '\0';

    return 0;
}

static int deepl_lang_to_source(const char *iso, char *out, int out_size)
{
    int i;

    if (!iso || !out || out_size < 8)
        return -1;

    /* "auto" means: omit source_lang from request */
    if (strcasecmp(iso, "auto") == 0)
    {
        out[0] = '\0';
        return 0;
    }

    /* Source: strip regional suffix if present, just take the first 2 */
    for (i = 0; iso[i] && iso[i] != '-' && i < out_size - 1; i++)
        out[i] = (char)toupper((unsigned char)iso[i]);

    out[i] = '\0';
    return 0;
}

char *translate_http_deepl(const char *endpoint, const char *api_key, const char *from, const char *to, const char *src, int timeout_secs, char *out_from, int out_from_size, char *err, int err_size)
{
    char url[512];
    char auth_header[160];
    char deepl_from[8];
    char deepl_to[8];
    char *req_body = NULL;
    HttpResponse r;
    int rc;
    char *translated = NULL;
    const char *base = NULL;
    const char *keys[5];
    const char *values[5];
    size_t bound;
    char *body = NULL;
    char *p = NULL;
    const char *kk = NULL;
    const char *vs = NULL;
    const char *ve = NULL;

    memset(&r, 0, sizeof(r));

    if (!from || !to || !src)
    {
        set_err(err, err_size, "invalid args");
        return NULL;
    }

    if (!api_key || !api_key[0])
    {
        set_err(err, err_size, "DeepL: TRANSLATE_API_KEY is empty");
        return NULL;
    }

    if (deepl_lang_to_source(from, deepl_from, sizeof(deepl_from)) < 0 || deepl_lang_to_target(to, deepl_to, sizeof(deepl_to)) < 0)
    {
        set_err(err, err_size, "DeepL: invalid lang code");
        return NULL;
    }

    base = (endpoint && endpoint[0]) ? endpoint : DEEPL_DEFAULT_ENDPOINT;
    snprintf(url, sizeof(url), "%s/v2/translate", base);

    /* DeepL expects "text" as an array, so we wrap the escaped string manually */
    bound = 256 + strlen(src) * 6;
    body = (char *)malloc(bound);

    if (!body)
    {
        set_err(err, err_size, "out of memory");
        return NULL;
    }

    /* Escape the source text and then wrap it in an array format */
    keys[0] = "text";
    values[0] = src;
    req_body = json_build_simple_request(keys, values, 1);

    if (!req_body)
    {
        free(body);
        set_err(err, err_size, "JSON build failed");
        return NULL;
    }

    /* Wrap the text value in brackets to convert it to an array format. */
    p = body;
    *p++ = '{';
    kk = "\"text\":[";

    while (*kk)
        *p++ = *kk++;

    /* Copy the escaped value (with quotes) from req_body */
    vs = strchr(req_body, ':');

    if (!vs)
    {
        free(body);
        free(req_body);

        set_err(err, err_size, "JSON build malformed");
        return NULL;
    }

    vs++;
    ve = req_body + strlen(req_body) - 1; /* points to '}' */

    while (ve > vs && *ve != '"')
        ve--;

    /* copy from vs to ve inclusive */
    while (vs <= ve)
        *p++ = *vs++;

    *p++ = ']';
    *p++ = ',';
    *p++ = ' ';

    kk = "\"target_lang\":\"";

    while (*kk)
        *p++ = *kk++;

    kk = deepl_to;

    while (*kk)
        *p++ = *kk++;

    *p++ = '"';

    if (deepl_from[0])
    {
        *p++ = ',';
        *p++ = ' ';
        kk = "\"source_lang\":\"";

        while (*kk)
            *p++ = *kk++;

        kk = deepl_from;

        while (*kk)
            *p++ = *kk++;

        *p++ = '"';
    }

    *p++ = '}';
    *p = '\0';

    free(req_body);

    req_body = body;

    snprintf(auth_header, sizeof(auth_header), "Authorization: DeepL-Auth-Key %s", api_key);

    rc = http_post(url, req_body, (int)strlen(req_body), "application/json", auth_header, timeout_secs, &r);
    free(req_body);

    if (rc != HTTP_OK)
    {
        set_err(err, err_size, "HTTP error: %s", http_strerror(rc));
        http_response_free(&r);
        return NULL;
    }

    if (r.status > 0 && (r.status < 200 || r.status >= 300))
    {
        char *msg = r.body ? json_extract_string(r.body, "message") : NULL;

        switch (r.status)
        {
        case 403:
            set_err(err, err_size, "DeepL: invalid API key (403)");
            break;
        case 429:
            set_err(err, err_size, "DeepL: rate limited (429), retry later");
            break;
        case 456:
            set_err(err, err_size, "DeepL: monthly quota exceeded (456)");
            break;
        default:
            if (msg && msg[0])
                set_err(err, err_size, "DeepL %d: %s", r.status, msg);
            else
                set_err(err, err_size, "DeepL HTTP %d", r.status);

            break;
        }
        free(msg);
        http_response_free(&r);
        return NULL;
    }

    if (!r.body || !r.body[0])
    {
        set_err(err, err_size, "empty response");
        http_response_free(&r);
        return NULL;
    }

    /* Jump past the array opener to parse the single translation element */
    p = strstr(r.body, "\"translations\"");

    if (!p)
    {
        set_err(err, err_size, "no translations in response");
        http_response_free(&r);
        return NULL;
    }

    p = strchr(p, '[');

    if (!p)
    {
        set_err(err, err_size, "malformed translations array");
        http_response_free(&r);
        return NULL;
    }

    p++;

    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
        p++;

    if (*p != '{')
    {
        set_err(err, err_size, "malformed translations entry");
        http_response_free(&r);
        return NULL;
    }

    /* Extract the translated text and detected language from the object */
    translated = json_extract_string(p, "text");

    if (out_from && out_from_size > 0)
    {
        char *det = json_extract_string(p, "detected_source_language");

        if (det && det[0])
        {
            int i;

            /* Lowercase for our internal convention */
            for (i = 0; det[i] && i < out_from_size - 1; i++)
                out_from[i] = (char)tolower((unsigned char)det[i]);

            out_from[i] = '\0';
        }
        else
        {
            strncpy(out_from, from, (size_t)out_from_size - 1);
            out_from[out_from_size - 1] = '\0';
        }

        free(det);
    }

    if (!translated || !translated[0])
    {
        set_err(err, err_size, "no translation in response");
        free(translated);
        http_response_free(&r);
        return NULL;
    }

    http_response_free(&r);

    return translated;
}
