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

/* Portable Text-To-Speech API, non-blocking, caller invokes tts_poll() */

#ifndef TE_TTS_H
#define TE_TTS_H

#ifdef __cplusplus
extern "C"
{
#endif

    /* Voice presets, mapped per backend */
    typedef enum
    {
        TTS_VOICE_MALE = 0,
        TTS_VOICE_FEMALE = 1,
        TTS_VOICE_MALE_ROBOT = 2,
        TTS_VOICE_FEMALE_ROBOT = 3
    } TtsVoice;

    typedef enum
    {
        TTS_STATE_IDLE = 0, /* not speaking, queue empty */
        TTS_STATE_PLAYING = 1,
        TTS_STATE_PAUSED = 2
    } TtsState;

    /* Return codes */
    typedef enum
    {
        TTS_OK = 0,
        TTS_ERR_UNAVAILABLE = -1, /* backend not present */
        TTS_ERR_NOMEM = -2,
        TTS_ERR_BUSY = -3,     /* transient, caller may retry */
        TTS_ERR_BADSTATE = -4, /* e.g. pause while idle */
        TTS_ERR_INTERNAL = -99
    } TtsResult;

    /* User-visible options, ranges normalised across backends */
    typedef struct
    {
        int rate;
        int pitch;
        int volume;
        int voice; /* TtsVoice */
    } TtsOpts;

    typedef struct TtsHandle TtsHandle;

    /* Fill opts with defaults, never fails */
    void tts_opts_defaults(TtsOpts *opts);

    /* Clamp fields to legal ranges, silently corrects */
    void tts_opts_clamp(TtsOpts *opts);

    /* Create handle, NULL if backend unavailable, treat as disabled */
    TtsHandle *tts_new(const TtsOpts *opts);

    /* Free handle, stops in-flight speech first, NULL-safe */
    void tts_free(TtsHandle *h);

    /* Apply options, takes effect on next utterance, NULL-safe */
    int tts_set_opts(TtsHandle *h, const TtsOpts *opts);

    /* Enqueue UTF-8 text, split into sentences for quick pause/stop */
    int tts_speak(TtsHandle *h, const char *utf8_text);

    /* Pause speech, no-op if already paused */
    int tts_pause(TtsHandle *h);

    /* Resume paused speech, no-op if already playing */
    int tts_resume(TtsHandle *h);

    /* Stop and clear queue, idempotent */
    int tts_stop(TtsHandle *h);

    /* Current state */
    TtsState tts_get_state(TtsHandle *h);

    /* Non-blocking poll, advances queue, return 1 if state changed */
    int tts_poll(TtsHandle *h);

    /* Amiga: MsgPort signal mask for Wait(), 0 if inactive or non-Amiga */
    unsigned long tts_amiga_sigmask(TtsHandle *h);

    /* Static introspection */
    int tts_is_available(void);
    const char *tts_backend_name(void);
    const char *tts_voice_name(int voice);

    /* Clear cache on voice change, NULL-safe */
    void tts_cache_clear(TtsHandle *h);

#ifdef __cplusplus
}
#endif

#endif /* TE_TTS_H */
