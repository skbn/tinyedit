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

/* Shared between tts.c and backend files, not public API, use tts.h instead */

#ifndef TE_TTS_BACKEND_H
#define TE_TTS_BACKEND_H

#include "tts.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        /* Open backend, NULL on failure, called from tts_new */
        void *(*open_fn)(const TtsOpts *opts);

        /* Close backend, called from tts_free after queue drain */
        void (*close_fn)(void *bd);

        /* Apply opts for next utterance */
        void (*apply_opts_fn)(void *bd, const TtsOpts *opts);

        /* Begin async speech, TTS_OK if submitted, TTS_ERR_BUSY if previous running */
        int (*begin_fn)(void *bd, const char *utf8_chunk, int len);

        /* Non-blocking check: 0=still speaking, 1=finished, -1=error */
        int (*poll_fn)(void *bd);

        /* Pause/resume/stop current utterance, stop cancels pending async */
        void (*pause_fn)(void *bd);
        void (*resume_fn)(void *bd);
        void (*stop_fn)(void *bd);

        /* Optional: Amiga msg-port signal for Wait(), 0 if inactive or non-Amiga */
        unsigned long (*sigmask_fn)(void *bd);

        /* Backend display name for UI */
        const char *name;
    } TtsBackendVT;

    /* UTF-8 to ASCII fold helper for Amiga narrator.device */
    int tts_utf8_to_ascii_fold(const char *utf8_in, char *ascii_out, int out_max);

#ifdef __cplusplus
}
#endif

#endif /* TE_TTS_BACKEND_H */
