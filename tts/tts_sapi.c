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

/* Windows TTS via SAPI 5, C-style COM vtables for MinGW-w64 */

#include "tts.h"
#include "tts_backend.h"

#ifdef PLATFORM_WIN32

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE

#include <windows.h>
#include <objbase.h>
#include <sapi.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const TtsBackendVT tts_backend_sapi_vt =
    {
        sapi_open,
        sapi_close,
        sapi_apply_opts,
        sapi_begin,
        sapi_poll,
        sapi_pause,
        sapi_resume,
        sapi_stop,
        sapi_sigmask,
        "SAPI 5 (Windows Speech API)"};

/* MinGW SAPI header C-macro fallbacks */
#ifndef ISpVoice_Speak
#define ISpVoice_Speak(This, pwcs, dwFlags, pulStreamNumber) (This)->lpVtbl->Speak(This, pwcs, dwFlags, pulStreamNumber)
#endif
#ifndef ISpVoice_Pause
#define ISpVoice_Pause(This) (This)->lpVtbl->Pause(This)
#endif
#ifndef ISpVoice_Resume
#define ISpVoice_Resume(This) (This)->lpVtbl->Resume(This)
#endif
#ifndef ISpVoice_SetRate
#define ISpVoice_SetRate(This, r) (This)->lpVtbl->SetRate(This, r)
#endif
#ifndef ISpVoice_SetVolume
#define ISpVoice_SetVolume(This, v) (This)->lpVtbl->SetVolume(This, v)
#endif
#ifndef ISpVoice_SetVoice
#define ISpVoice_SetVoice(This, t) (This)->lpVtbl->SetVoice(This, t)
#endif
#ifndef ISpVoice_GetVoices
#define ISpVoice_GetVoices(This, reqAttr, optAttr, ppEnum) (This)->lpVtbl->GetVoices(This, reqAttr, optAttr, ppEnum)
#endif
#ifndef ISpVoice_WaitUntilDone
#define ISpVoice_WaitUntilDone(This, ms) (This)->lpVtbl->WaitUntilDone(This, ms)
#endif
#ifndef ISpVoice_Release
#define ISpVoice_Release(This) (This)->lpVtbl->Release(This)
#endif

/* SPF flags: async, plain text, purge before speak */
#ifndef SPF_ASYNC
#define SPF_ASYNC 1
#endif
#ifndef SPF_PURGEBEFORESPEAK
#define SPF_PURGEBEFORESPEAK 2
#endif
#ifndef SPF_IS_NOT_XML
#define SPF_IS_NOT_XML 16
#endif

typedef struct
{
    int com_initialised;
    ISpVoice *voice; /* SAPI voice object */

    /* Cached opts */
    int rate;     /* WPM 40..400 to SAPI rate -10..+10 */
    int pitch;    /* Hz 65..320, ignored by SAPI plain-text */
    int volume;   /* 0..100 to SAPI 0..100 */
    int voice_id; /* TtsVoice enum */

    int speaking; /* 1 = Speak(SPF_ASYNC) outstanding */
    int paused;
} SapiTtsData;

/* Map TtsVoice to SAPI voice, returns 0 on success, -1 to keep current */
static int sapi_select_voice(SapiTtsData *bd)
{
    /* Use system-default voice for all presets, robot via rate/pitch */
    return -1;
}

/* Convert UTF-8 to wide string for Speak, returns malloc'd buffer or NULL */
static WCHAR *utf8_to_wide(const char *utf8, int len)
{
    int wide_len;
    WCHAR *wide = NULL;

    if (!utf8 || len <= 0)
        return NULL;

    wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8, len, NULL, 0);

    if (wide_len <= 0)
        return NULL;

    wide = (WCHAR *)malloc((size_t)(wide_len + 1) * sizeof(WCHAR));

    if (!wide)
        return NULL;

    MultiByteToWideChar(CP_UTF8, 0, utf8, len, wide, wide_len);
    wide[wide_len] = L'\0';

    return wide;
}

/* Convert WPM 40..400 to SAPI rate -10..+10 */
static long wpm_to_sapi_rate(int wpm)
{
    long r;

    if (wpm < 40)
        wpm = 40;

    if (wpm > 400)
        wpm = 400;

    /* 150 WPM -> 0, below negative, above positive */
    r = ((long)wpm - 150) / 15;

    if (r < -10)
        r = -10;

    if (r > 10)
        r = 10;

    return r;
}

/* open_fn */
static void *sapi_open(const TtsOpts *opts)
{
    SapiTtsData *bd = NULL;
    HRESULT hr;

    bd = (SapiTtsData *)calloc(1, sizeof(*bd));

    if (!bd)
        return NULL;

    /* Initialize COM, STA for SAPI main thread */
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (hr == RPC_E_CHANGED_MODE)
    {
        /* Already initialized as MTA, try that */
        hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    }

    if (SUCCEEDED(hr))
    {
        bd->com_initialised = 1;
    }
    else if (hr != S_FALSE && hr != RPC_E_CHANGED_MODE)
    {
        free(bd);
        return NULL;
    }

    /* Create SpVoice */
    hr = CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL, &IID_ISpVoice, (void **)&bd->voice);

    if (FAILED(hr) || !bd->voice)
    {
        if (bd->com_initialised)
            CoUninitialize();

        free(bd);
        return NULL;
    }

    /* Apply initial opts */
    if (opts)
    {
        bd->rate = opts->rate;
        bd->pitch = opts->pitch;
        bd->volume = opts->volume;
        bd->voice_id = opts->voice;
    }
    else
    {
        bd->rate = 150;
        bd->pitch = 110;
        bd->volume = 100;
        bd->voice_id = TTS_VOICE_MALE;
    }

    ISpVoice_SetRate(bd->voice, wpm_to_sapi_rate(bd->rate));
    ISpVoice_SetVolume(bd->voice, (USHORT)bd->volume);
    sapi_select_voice(bd);

    return bd;
}

/* close_fn */
static void sapi_close(void *backend_data)
{
    SapiTtsData *bd = (SapiTtsData *)backend_data;

    if (!bd)
        return;

    if (bd->voice)
    {
        /* Cancel in-flight speech */
        WCHAR empty[1] = {L'\0'};
        ISpVoice_Speak(bd->voice, empty, SPF_PURGEBEFORESPEAK, NULL);

        ISpVoice_Release(bd->voice);
        bd->voice = NULL;
    }

    if (bd->com_initialised)
    {
        CoUninitialize();
        bd->com_initialised = 0;
    }

    free(bd);
}

/* apply_opts_fn */
static void sapi_apply_opts(void *backend_data, const TtsOpts *opts)
{
    SapiTtsData *bd = (SapiTtsData *)backend_data;

    if (!bd || !opts || !bd->voice)
        return;

    bd->rate = opts->rate;
    bd->pitch = opts->pitch;
    bd->volume = opts->volume;
    bd->voice_id = opts->voice;

    /* Take effect on next Speak, harmless during playback */
    ISpVoice_SetRate(bd->voice, wpm_to_sapi_rate(bd->rate));
    ISpVoice_SetVolume(bd->voice, (USHORT)bd->volume);
}

/* begin_fn */
static int sapi_begin(void *backend_data, const char *utf8_chunk, int len)
{
    SapiTtsData *bd = (SapiTtsData *)backend_data;
    WCHAR *wide = NULL;
    HRESULT hr;

    if (!bd || !bd->voice || !utf8_chunk || len <= 0)
        return TTS_ERR_INTERNAL;

    if (bd->speaking)
        return TTS_ERR_BUSY;

    /* Fake robot effect: bump rate, SAPI lacks monotone flag */
    if (bd->voice_id == TTS_VOICE_MALE_ROBOT || bd->voice_id == TTS_VOICE_FEMALE_ROBOT)
        ISpVoice_SetRate(bd->voice, wpm_to_sapi_rate(bd->rate + 40));
    else
        ISpVoice_SetRate(bd->voice, wpm_to_sapi_rate(bd->rate));

    wide = utf8_to_wide(utf8_chunk, len);

    if (!wide)
        return TTS_ERR_INTERNAL;

    hr = ISpVoice_Speak(bd->voice, wide, SPF_ASYNC | SPF_IS_NOT_XML, NULL);

    free(wide);

    if (FAILED(hr))
        return TTS_ERR_INTERNAL;

    bd->speaking = 1;
    bd->paused = 0;

    return TTS_OK;
}

/* poll_fn -- check if async Speak finished */
static int sapi_poll(void *backend_data)
{
    SapiTtsData *bd = (SapiTtsData *)backend_data;
    HRESULT hr;

    if (!bd || !bd->voice)
        return -1;

    if (!bd->speaking)
        return 1;

    if (bd->paused)
        return 0;

    /* WaitUntilDone(0): S_OK if finished, S_FALSE if still speaking */
    hr = ISpVoice_WaitUntilDone(bd->voice, 0);

    if (hr == S_OK)
    {
        bd->speaking = 0;
        return 1;
    }

    return 0;
}

/* Pause/resume/stop */
static void sapi_pause(void *backend_data)
{
    SapiTtsData *bd = (SapiTtsData *)backend_data;

    if (!bd || !bd->voice || !bd->speaking || bd->paused)
        return;

    if (SUCCEEDED(ISpVoice_Pause(bd->voice)))
        bd->paused = 1;
}

static void sapi_resume(void *backend_data)
{
    SapiTtsData *bd = (SapiTtsData *)backend_data;

    if (!bd || !bd->voice || !bd->paused)
        return;

    if (SUCCEEDED(ISpVoice_Resume(bd->voice)))
        bd->paused = 0;
}

static void sapi_stop(void *backend_data)
{
    SapiTtsData *bd = (SapiTtsData *)backend_data;
    WCHAR empty[1] = {L'\0'};

    if (!bd || !bd->voice)
        return;

    /* Resume first if paused so purge can drain queue */
    if (bd->paused)
        sapi_resume(bd);

    /* Speak with SPF_PURGEBEFORESPEAK on empty string cancels pending */
    ISpVoice_Speak(bd->voice, empty, SPF_PURGEBEFORESPEAK, NULL);

    bd->speaking = 0;
    bd->paused = 0;
}

static unsigned long sapi_sigmask(void *backend_data)
{
    (void)backend_data;
    return 0UL;
}

#endif /* PLATFORM_WIN32 */
