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

#ifndef TRANSLATE_H
#define TRANSLATE_H

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(PLATFORM_AMIGA)
#ifndef TRANSLATE_CACHE_N
#define TRANSLATE_CACHE_N 8
#endif
#ifndef TRANSLATE_CACHE_KEY_MAX
#define TRANSLATE_CACHE_KEY_MAX 384
#endif
#ifndef TRANSLATE_CACHE_VAL_MAX
#define TRANSLATE_CACHE_VAL_MAX 1536
#endif
#else
#ifndef TRANSLATE_CACHE_N
#define TRANSLATE_CACHE_N 128
#endif
#ifndef TRANSLATE_CACHE_KEY_MAX
#define TRANSLATE_CACHE_KEY_MAX 512
#endif
#ifndef TRANSLATE_CACHE_VAL_MAX
#define TRANSLATE_CACHE_VAL_MAX 2048
#endif
#endif

    typedef enum
    {
        TRANSLATE_BACKEND_NONE = 0,
        TRANSLATE_BACKEND_MYMEMORY = 1,
        TRANSLATE_BACKEND_LIBRETRANSLATE = 2,
        TRANSLATE_BACKEND_LINGVA = 3,
        TRANSLATE_BACKEND_STARDICT = 10,
        TRANSLATE_BACKEND_APERTIUM = 20
    } TranslateBackend;

    typedef struct TranslateHandle TranslateHandle;

    typedef struct
    {
        TranslateBackend backend;
        const char *endpoint;
        const char *api_key;
        const char *email;
        const char *stardict_path;
        const char *apertium_bin;
        int timeout_secs;
    } TranslateOpts;

    TranslateHandle *translate_new(const TranslateOpts *opts);
    void translate_free(TranslateHandle *h);

    char *translate_text(TranslateHandle *h, const char *from_lang, const char *to_lang, const char *src, char *out_from_lang_buf, int out_from_lang_buf_size, char *err_buf, int err_buf_size);

    void translate_cache_clear(TranslateHandle *h);
    int translate_set_opts(TranslateHandle *h, const TranslateOpts *opts);
    const char *const *translate_supported_langs(TranslateHandle *h);
    int translate_is_available(void);
    const char *translate_backend_name(TranslateBackend backend);
    TranslateBackend translate_backend_parse(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* TRANSLATE_H */
