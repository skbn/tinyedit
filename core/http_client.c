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

#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef PLATFORM_AMIGA
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#ifdef WITH_AMISSL
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <proto/amisslmaster.h>
#include <proto/amissl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif
#else
#include <curl/curl.h>
#endif

static int g_init_count = 0;

#ifdef PLATFORM_AMIGA
struct Library *SocketBase = NULL;
#ifdef WITH_AMISSL
struct Library *AmiSSLMasterBase = NULL;
struct Library *AmiSSLBase = NULL;
struct Library *AmiSSLExtBase = NULL;
static BOOL AmiSSLInitialized = FALSE;
#endif
#endif

int http_client_init(void)
{
    g_init_count++;

#ifndef PLATFORM_AMIGA
    if (g_init_count == 1)
        curl_global_init(CURL_GLOBAL_DEFAULT);
#else
    if (g_init_count == 1)
    {
        SocketBase = OpenLibrary("bsdsocket.library", 4);

        if (!SocketBase)
            return HTTP_ERR_CONNECT;

#ifdef WITH_AMISSL
        /* AmiSSL initialization sequence (from binkd_ssl) */
        AmiSSLMasterBase = OpenLibrary("amisslmaster.library", AMISSLMASTER_MIN_VERSION);

        if (AmiSSLMasterBase)
        {
            if (!InitAmiSSLMaster(AMISSL_CURRENT_VERSION, TRUE))
            {
                if (!InitAmiSSLMaster(AMISSL_V3xx, TRUE))
                {
                    CloseLibrary(AmiSSLMasterBase);
                    AmiSSLMasterBase = NULL;
                }
            }

            if (AmiSSLMasterBase)
            {
                AmiSSLBase = OpenAmiSSL();
                if (!AmiSSLBase)
                {
                    CloseLibrary(AmiSSLMasterBase);
                    AmiSSLMasterBase = NULL;
                }
                else
                {
                    if (InitAmiSSL(AmiSSL_ErrNoPtr, (LONG)&errno,
                                   AmiSSL_UsesOpenSSLStructs, (LONG)TRUE,
                                   AmiSSL_GetAmiSSLExtBase, (LONG)&AmiSSLExtBase,
                                   AmiSSL_SocketBase, (ULONG)SocketBase,
                                   TAG_DONE) != 0)
                    {
                        AmiSSLExtBase = NULL;
                        CloseAmiSSL();
                        AmiSSLBase = NULL;
                        CloseLibrary(AmiSSLMasterBase);
                        AmiSSLMasterBase = NULL;
                    }
                    else
                    {
                        AmiSSLInitialized = TRUE;
                    }
                }
            }
        }
#endif
    }
#endif
    return HTTP_OK;
}

void http_client_shutdown(void)
{
    if (g_init_count > 0)
        g_init_count--;

    if (g_init_count == 0)
    {
#ifndef PLATFORM_AMIGA
        curl_global_cleanup();
#else
#ifdef WITH_AMISSL
        if (AmiSSLInitialized)
        {
            CleanupAmiSSLA(NULL);
            AmiSSLInitialized = FALSE;
        }

        if (AmiSSLBase)
        {
            CloseAmiSSL();
            AmiSSLBase = NULL;
            AmiSSLExtBase = NULL;
        }

        if (AmiSSLMasterBase)
        {
            CloseLibrary(AmiSSLMasterBase);
            AmiSSLMasterBase = NULL;
        }
#endif
        if (SocketBase)
        {
            CloseLibrary(SocketBase);
            SocketBase = NULL;
        }
#endif
    }
}

static int hex_digit(int v)
{
    return v < 10 ? '0' + v : 'A' + v - 10;
}

int http_url_encode(const char *src, char *dst, int dst_size)
{
    int o = 0;
    unsigned char c;
    int is_safe;
    int count = 0;

    if (!src || !dst || dst_size <= 0)
        return -1;

    while (*src)
    {
        c = (unsigned char)*src++;
        is_safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';

        if (is_safe)
        {
            if (o + 1 >= dst_size)
                return -1;

            dst[o++] = (char)c;
        }
        else
        {
            if (o + 3 >= dst_size)
                return -1;

            dst[o++] = '%';
            dst[o++] = (char)hex_digit((c >> 4) & 0xF);
            dst[o++] = (char)hex_digit(c & 0xF);
        }

        count++;
    }

    dst[o] = '\0';
    return o;
}

#ifndef PLATFORM_AMIGA

typedef struct
{
    char *data;
    size_t size;
    size_t capacity;
} CurlResponse;

static size_t http_curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    CurlResponse *resp = (CurlResponse *)userp;
    size_t new_cap;
    char *new_data;

    if (resp->size + total > HTTP_MAX_BODY_BYTES)
        return 0;

    if (resp->size + total + 1 > resp->capacity)
    {
        new_cap = resp->capacity * 2;

        if (new_cap < resp->size + total + 1)
            new_cap = resp->size + total + 4096;

        new_data = (char *)realloc(resp->data, new_cap);

        if (!new_data)
            return 0;

        resp->data = new_data;
        resp->capacity = new_cap;
    }

    memcpy(resp->data + resp->size, contents, total);

    resp->size += total;
    resp->data[resp->size] = '\0';

    return total;
}

static int do_request_libcurl(const char *method, const char *url, const char *body, int body_len, const char *content_type, const char *extra_headers, int timeout_secs, HttpResponse *out)
{
    CURL *curl = NULL;
    CURLcode res;
    struct curl_slist *headers = NULL;
    CurlResponse resp;
    long http_code = 0;
    int ret;

    if (!out)
        return HTTP_ERR_URL;

    memset(out, 0, sizeof(*out));
    memset(&resp, 0, sizeof(resp));

    curl = curl_easy_init();

    if (!curl)
        return HTTP_ERR_OOM;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_secs > 0 ? (long)timeout_secs : 10L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);

#ifdef PLATFORM_WIN32
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif

    if (content_type && content_type[0])
    {
        char header[256];

        snprintf(header, sizeof(header), "Content-Type: %s", content_type);
        headers = curl_slist_append(headers, header);
    }

    if (extra_headers && extra_headers[0])
        headers = curl_slist_append(headers, extra_headers);

    if (strcmp(method, "POST") == 0)
    {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);

        if (body && body_len > 0)
        {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
        }
    }

    if (headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    res = curl_easy_perform(curl);

    if (res == CURLE_OK)
    {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        out->status = (int)http_code;
        out->body = resp.data;
        out->body_len = (int)resp.size;

        ret = HTTP_OK;
    }
    else
    {
        if (resp.data)
            free(resp.data);

        switch (res)
        {
        case CURLE_COULDNT_RESOLVE_HOST:
            ret = HTTP_ERR_DNS;
            break;
        case CURLE_COULDNT_CONNECT:
            ret = HTTP_ERR_CONNECT;
            break;
        case CURLE_OPERATION_TIMEDOUT:
            ret = HTTP_ERR_TIMEOUT;
            break;

#ifdef CURLE_PEER_FAILED_VERIFICATION
        case CURLE_PEER_FAILED_VERIFICATION:
#endif
#ifdef CURLE_SSL_CONNECT_ERROR
        case CURLE_SSL_CONNECT_ERROR:
            ret = HTTP_ERR_TLS;
            break;
#endif
        default:
            ret = HTTP_ERR_CONNECT;
            break;
        }
    }

    if (headers)
        curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return ret;
}

#endif /* !PLATFORM_AMIGA */

#ifdef PLATFORM_AMIGA

/* Simple URL parser: extracts host, port, and path from URL */
static int parse_url(const char *url, char *host, int host_size, int *port, char *path, int path_size, int *use_tls)
{
    const char *p = NULL;
    const char *host_start = NULL;
    const char *host_end = NULL;
    const char *path_start = NULL;
    int default_port;
    int i;

    if (!url || !host || !port || !path || !use_tls)
        return -1;

    *port = 0;
    *use_tls = 0;
    path[0] = '\0';

    /* Check for http:// or https:// */
    if (strncmp(url, "http://", 7) == 0)
    {
        host_start = url + 7;
        default_port = 80;
        *use_tls = 0;
    }
    else if (strncmp(url, "https://", 8) == 0)
    {
        host_start = url + 8;
        default_port = 443;
        *use_tls = 1;
    }
    else
    {
        return -1;
    }

    /* Find end of host (first : or / after host start) */
    host_end = host_start;

    while (*host_end && *host_end != ':' && *host_end != '/')
        host_end++;

    /* Copy host */
    i = 0;
    p = host_start;

    while (p < host_end && i < host_size - 1)
        host[i++] = *p++;

    host[i] = '\0';

    /* Check for port */
    if (*host_end == ':')
    {
        p = host_end + 1;
        *port = 0;

        while (*p >= '0' && *p <= '9')
        {
            *port = (*port) * 10 + (*p - '0');
            p++;
        }

        if (*port == 0)
            *port = default_port;

        path_start = p;
    }
    else
    {
        *port = default_port;
        path_start = host_end;
    }

    /* Copy path (default to / if none) */
    if (*path_start == '\0' || *path_start == '/')
    {
        strncpy(path, path_start, path_size - 1);

        path[path_size - 1] = '\0';
    }
    else
    {
        strncpy(path, "/", path_size - 1);

        path[path_size - 1] = '\0';
    }

    return 0;
}

/* Parse HTTP status code from response headers */
static int parse_http_status(const char *headers)
{
    const char *p = NULL;
    int status;

    if (!headers)
        return 0;

    p = strstr(headers, "HTTP/");

    if (!p)
        return 0;

    p += 5;

    while (*p && *p != ' ')
        p++;

    while (*p == ' ')
        p++;

    status = 0;

    while (*p >= '0' && *p <= '9')
    {
        status = status * 10 + (*p - '0');
        p++;
    }

    return status;
}

/* Find end of headers (double CRLF) */
static const char *find_body_start(const char *data, int len)
{
    int i;

    for (i = 0; i < len - 3; i++)
    {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n')
            return data + i + 4;

        if (data[i] == '\n' && data[i + 1] == '\n')
            return data + i + 2;
    }

    return NULL;
}

/* Extract Location header for redirects */
static int extract_location(const char *headers, char *location, int loc_size)
{
    const char *p = NULL;
    const char *loc_start = NULL;
    const char *loc_end = NULL;
    int len;

    p = strstr(headers, "Location:");

    if (!p)
        p = strstr(headers, "location:");

    if (!p)
        return -1;

    p += 9; /* Skip "Location:" */

    while (*p == ' ' || *p == '\t')
        p++;

    loc_start = p;

    while (*p && *p != '\r' && *p != '\n')
        p++;

    loc_end = p;

    len = (int)(loc_end - loc_start);

    if (len >= loc_size)
        return -1;

    memcpy(location, loc_start, (size_t)len);

    location[len] = '\0';

    return 0;
}

#endif /* PLATFORM_AMIGA */

static int do_request(const char *method, const char *url, const char *body, int body_len, const char *content_type, const char *extra_headers, int timeout_secs, HttpResponse *out)
{
#ifndef PLATFORM_AMIGA
    return do_request_libcurl(method, url, body, body_len, content_type, extra_headers, timeout_secs, out);
#else

    char host[256];
    char path[512];
    int port;
    int use_tls;
    int sock;
    struct hostent *he = NULL;
    struct sockaddr_in sa;
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    char request[4096];
    char *response_buf = NULL;
    char *grown = NULL;
    int response_len;
    int response_cap;
    int n;
    const char *body_start = NULL;
    int rc;
    int redirect_count;
    char current_url[512];
    char location[512];
    int status;
    struct timeval tv;

    if (!out)
        return HTTP_ERR_URL;

    memset(out, 0, sizeof(*out));

    /* Copy URL for redirects */
    strncpy(current_url, url, sizeof(current_url) - 1);
    current_url[sizeof(current_url) - 1] = '\0';

    redirect_count = 0;

    while (redirect_count < 5)
    {
        /* Parse URL */
        if (parse_url(current_url, host, sizeof(host), &port, path, sizeof(path), &use_tls) != 0)
            return HTTP_ERR_URL;

        /* DNS lookup */
        he = gethostbyname(host);

        if (!he || !he->h_addr_list[0])
            return HTTP_ERR_DNS;

        /* Create TCP socket */
        sock = socket(AF_INET, SOCK_STREAM, 0);

        if (sock < 0)
            return HTTP_ERR_CONNECT;

        /* Connect */
        memset(&sa, 0, sizeof(sa));

        sa.sin_family = AF_INET;
        sa.sin_port = htons((unsigned short)port);

        memcpy(&sa.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

        if (connect(sock, (struct sockaddr *)&sa, sizeof(sa)) != 0)
        {
            CloseSocket(sock);
            return HTTP_ERR_CONNECT;
        }

        /* Set socket timeout (30 seconds) */
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));

        /* TLS handshake if needed */
        ctx = NULL;
        ssl = NULL;

        if (use_tls)
        {
#ifdef WITH_AMISSL
            if (!AmiSSLBase)
            {
                CloseSocket(sock);
                return HTTP_ERR_NO_TLS;
            }

            ctx = SSL_CTX_new(TLS_client_method());

            if (!ctx)
            {
                CloseSocket(sock);
                return HTTP_ERR_TLS;
            }

            /* Disable old protocols */
            SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);

            /* Don't verify chain by default - on Amiga the CA bundle is often absent */
            SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

            ssl = SSL_new(ctx);

            if (!ssl)
            {
                SSL_CTX_free(ctx);

                CloseSocket(sock);
                return HTTP_ERR_TLS;
            }

            SSL_set_fd(ssl, (int)sock);
            SSL_set_tlsext_host_name(ssl, host);

            if (SSL_connect(ssl) != 1)
            {
                SSL_free(ssl);
                SSL_CTX_free(ctx);

                CloseSocket(sock);
                return HTTP_ERR_TLS;
            }
#else
            CloseSocket(sock);
            return HTTP_ERR_NO_TLS;
#endif
        }
        else
        {
            /* For HTTP, skip TLS */
            ssl = NULL;
            ctx = NULL;
        }

        /* Build HTTP request */
        if (body && body_len > 0 && strcmp(method, "POST") == 0)
        {
            snprintf(request, sizeof(request),
                     "%s %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %d\r\n"
                     "%s%s"
                     "Connection: close\r\n"
                     "\r\n",
                     method, path, host,
                     (content_type && content_type[0]) ? content_type : "application/octet-stream",
                     body_len,
                     (extra_headers && extra_headers[0]) ? extra_headers : "",
                     (extra_headers && extra_headers[0]) ? "\r\n" : "");
        }
        else
        {
            snprintf(request, sizeof(request),
                     "%s %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "%s%s"
                     "Connection: close\r\n"
                     "\r\n",
                     method, path, host,
                     (extra_headers && extra_headers[0]) ? extra_headers : "",
                     (extra_headers && extra_headers[0]) ? "\r\n" : "");
        }

        /* Send request headers */
        if (use_tls)
        {
            if (SSL_write(ssl, request, (int)strlen(request)) <= 0)
            {
                SSL_free(ssl);
                SSL_CTX_free(ctx);

                CloseSocket(sock);
                return HTTP_ERR_SEND;
            }
        }
        else
        {
            if (send(sock, request, (int)strlen(request), 0) < 0)
            {
                CloseSocket(sock);
                return HTTP_ERR_SEND;
            }
        }

        /* Send body if POST */
        if (body && body_len > 0 && strcmp(method, "POST") == 0)
        {
            if (use_tls)
            {
                if (SSL_write(ssl, body, body_len) <= 0)
                {
                    SSL_free(ssl);
                    SSL_CTX_free(ctx);

                    CloseSocket(sock);
                    return HTTP_ERR_SEND;
                }
            }
            else
            {
                if (send(sock, body, body_len, 0) < 0)
                {
                    CloseSocket(sock);
                    return HTTP_ERR_SEND;
                }
            }
        }

        /* Read response */
        response_cap = 8192;
        response_buf = (char *)malloc(response_cap);

        if (!response_buf)
        {
            if (ssl)
                SSL_free(ssl);

            if (ctx)
                SSL_CTX_free(ctx);

            CloseSocket(sock);
            return HTTP_ERR_OOM;
        }
        response_len = 0;

        while (1)
        {
            if (response_len + 1024 > response_cap)
            {
                response_cap *= 2;

                if (response_cap > HTTP_MAX_BODY_BYTES)
                {
                    free(response_buf);

                    if (ssl)
                        SSL_free(ssl);

                    if (ctx)
                        SSL_CTX_free(ctx);

                    CloseSocket(sock);
                    return HTTP_ERR_TOO_LARGE;
                }

                grown = (char *)realloc(response_buf, response_cap);

                if (!grown)
                {
                    /* Keep old buffer valid so it can be freed */
                    free(response_buf);

                    if (ssl)
                        SSL_free(ssl);

                    if (ctx)
                        SSL_CTX_free(ctx);

                    CloseSocket(sock);
                    return HTTP_ERR_OOM;
                }

                response_buf = grown;
            }

            if (use_tls)
                n = SSL_read(ssl, response_buf + response_len, 1024);
            else
                n = recv(sock, response_buf + response_len, 1024, 0);

            if (n <= 0)
                break;

            response_len += n;
        }

        if (response_len == 0)
        {
            free(response_buf);

            if (ssl)
                SSL_free(ssl);

            if (ctx)
                SSL_CTX_free(ctx);

            CloseSocket(sock);
            return HTTP_ERR_RECV;
        }

        response_buf[response_len] = '\0';

        /* Cleanup */
        if (ssl)
            SSL_free(ssl);

        if (ctx)
            SSL_CTX_free(ctx);

        CloseSocket(sock);

        /* Parse response status */
        status = parse_http_status(response_buf);

        /* Check for redirect */
        if (status == 301 || status == 302 || status == 307 || status == 308)
        {
            if (extract_location(response_buf, location, sizeof(location)) == 0)
            {
                free(response_buf);

                strncpy(current_url, location, sizeof(current_url) - 1);
                current_url[sizeof(current_url) - 1] = '\0';

                redirect_count++;
                continue; /* Follow redirect */
            }
        }

        /* Not a redirect - return response */
        out->status = status;
        body_start = find_body_start(response_buf, response_len);

        if (body_start)
        {
            int body_len = response_len - (int)(body_start - response_buf);

            out->body = (char *)malloc((size_t)body_len + 1);

            if (!out->body)
            {
                free(response_buf);
                return HTTP_ERR_OOM;
            }

            memcpy(out->body, body_start, (size_t)body_len);

            out->body[body_len] = '\0';
            out->body_len = body_len;
        }
        else
        {
            out->body = response_buf;
            out->body_len = response_len;
        }

        if (out->status == 0)
        {
            if (out->body)
            {
                free(out->body);

                out->body = NULL;
                out->body_len = 0;
            }
            return HTTP_ERR_BAD_RESPONSE;
        }

        return HTTP_OK;
    }

    /* Too many redirects */
    return HTTP_ERR_CONNECT;
#endif
}

int http_get(const char *url, const char *extra_headers, int timeout_secs, HttpResponse *out)
{
    return do_request("GET", url, NULL, 0, NULL, extra_headers, timeout_secs, out);
}

int http_post(const char *url, const char *body, int body_len, const char *content_type, const char *extra_headers, int timeout_secs, HttpResponse *out)
{
    return do_request("POST", url, body, body_len, content_type, extra_headers, timeout_secs, out);
}

void http_response_free(HttpResponse *r)
{
    if (!r)
        return;

    if (r->body)
    {
        free(r->body);
        r->body = NULL;
    }

    if (r->content_type)
    {
        free(r->content_type);
        r->content_type = NULL;
    }

    r->status = 0;
    r->body_len = 0;
}

const char *http_strerror(int rc)
{
    switch (rc)
    {
    case HTTP_OK:
        return "OK";
    case HTTP_ERR_URL:
        return "invalid URL";
    case HTTP_ERR_DNS:
        return "DNS lookup failed";
    case HTTP_ERR_CONNECT:
        return "connection failed";
    case HTTP_ERR_SEND:
        return "send failed";
    case HTTP_ERR_RECV:
        return "recv failed";
    case HTTP_ERR_TIMEOUT:
        return "timeout";
    case HTTP_ERR_OOM:
        return "out of memory";
    case HTTP_ERR_NO_TLS:
        return "TLS not available in this build";
    case HTTP_ERR_BAD_RESPONSE:
        return "malformed HTTP response";
    case HTTP_ERR_TLS:
        return "TLS handshake failed";
    case HTTP_ERR_NOT_INIT:
        return "http_client not initialised";
    case HTTP_ERR_TOO_LARGE:
        return "response too large";
    default:
        return "unknown HTTP error";
    }
}
