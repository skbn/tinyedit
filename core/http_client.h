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

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#ifdef __cplusplus
extern "C"
{
#endif

#define HTTP_OK 0
#define HTTP_ERR_URL -1
#define HTTP_ERR_DNS -2
#define HTTP_ERR_CONNECT -3
#define HTTP_ERR_SEND -4
#define HTTP_ERR_RECV -5
#define HTTP_ERR_TIMEOUT -6
#define HTTP_ERR_OOM -7
#define HTTP_ERR_NO_TLS -8
#define HTTP_ERR_BAD_RESPONSE -9
#define HTTP_ERR_TLS -10
#define HTTP_ERR_NOT_INIT -11
#define HTTP_ERR_TOO_LARGE -12

#define HTTP_MAX_BODY_BYTES (1024 * 1024) /* 1MB max */

    typedef struct
    {
        int status;
        char *body;
        int body_len;
        char *content_type;
    } HttpResponse;

    /* URL encoding */
    int http_url_encode(const char *src, char *dst, int dst_size);

    /* Init/shutdown */
    int http_client_init(void);
    void http_client_shutdown(void);

    /* HTTP requests */
    int http_get(const char *url, const char *extra_headers, int timeout_secs, HttpResponse *out);
    int http_post(const char *url, const char *body, int body_len, const char *content_type, const char *extra_headers, int timeout_secs, HttpResponse *out);

    /* Response cleanup */
    void http_response_free(HttpResponse *r);

    /* Error string */
    const char *http_strerror(int rc);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_CLIENT_H */
