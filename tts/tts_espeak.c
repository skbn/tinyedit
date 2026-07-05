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

/* Unix TTS via subprocess, portable, pause via SIGSTOP/SIGCONT */

/* Feature test macros for kill(), usleep() */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

#include "tts.h"
#include "tts_backend.h"

/* Forward declarations for backend vtable */
static void *espeak_open(const TtsOpts *opts);
static void espeak_close(void *backend_data);
static void espeak_apply_opts(void *backend_data, const TtsOpts *opts);
static int espeak_begin(void *backend_data, const char *utf8_chunk, int len);
static int espeak_poll(void *backend_data);
static void espeak_pause(void *backend_data);
static void espeak_resume(void *backend_data);
static void espeak_stop(void *backend_data);
static unsigned long espeak_sigmask(void *backend_data);

const TtsBackendVT tts_backend_espeak_vt =
    {
        espeak_open,
        espeak_close,
        espeak_apply_opts,
        espeak_begin,
        espeak_poll,
        espeak_pause,
        espeak_resume,
        espeak_stop,
        espeak_sigmask,
        "espeak-ng (subprocess)"};

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

/* TTS binary to invoke, detected at open time */
typedef enum
{
    UNIX_TTS_NONE = 0,
    UNIX_TTS_ESPEAK_NG,
    UNIX_TTS_ESPEAK,
    UNIX_TTS_MACOS_SAY,
    UNIX_TTS_FESTIVAL
} UnixTtsBinary;

typedef struct
{
    UnixTtsBinary binary;
    char binary_path[256];

    /* Cached opts, applied on every begin_fn */
    int rate;
    int pitch;
    int volume;
    int voice;

    /* Currently running child */
    pid_t child_pid; /* 0 = none */
    int paused;      /* 1 = SIGSTOPped */
} UnixTtsData;

/* Detect available TTS binary */

static const char *const candidate_paths[][2] =
    {
        /* Display-name and absolute-path pairs, priority order */
        {"espeak-ng", "/usr/bin/espeak-ng"},
        {"espeak-ng", "/usr/local/bin/espeak-ng"},
        {"espeak-ng", "/opt/homebrew/bin/espeak-ng"},
        {"espeak", "/usr/bin/espeak"},
        {"espeak", "/usr/local/bin/espeak"},
        {"say", "/usr/bin/say"}, /* macOS */
        {"festival", "/usr/bin/festival"},
        {"festival", "/usr/local/bin/festival"},
        {NULL, NULL}};

static UnixTtsBinary detect_binary(char *out_path, int out_max)
{
    int i;

    for (i = 0; candidate_paths[i][0] != NULL; i++)
    {
        const char *name = candidate_paths[i][0];
        const char *path = candidate_paths[i][1];

        if (access(path, X_OK) == 0)
        {
            int len = (int)strlen(path);

            if (len >= out_max)
                len = out_max - 1;

            memcpy(out_path, path, (size_t)len);
            out_path[len] = '\0';

            if (strcmp(name, "espeak-ng") == 0)
                return UNIX_TTS_ESPEAK_NG;

            if (strcmp(name, "espeak") == 0)
                return UNIX_TTS_ESPEAK;

            if (strcmp(name, "say") == 0)
                return UNIX_TTS_MACOS_SAY;

            if (strcmp(name, "festival") == 0)
                return UNIX_TTS_FESTIVAL;
        }
    }

    out_path[0] = '\0';

    return UNIX_TTS_NONE;
}

/* open_fn */
static void *espeak_open(const TtsOpts *opts)
{
    UnixTtsData *bd = NULL;

    bd = (UnixTtsData *)calloc(1, sizeof(*bd));

    if (!bd)
        return NULL;

    bd->binary = detect_binary(bd->binary_path, (int)sizeof(bd->binary_path));

    if (bd->binary == UNIX_TTS_NONE)
    {
        free(bd);
        return NULL;
    }

    if (opts)
    {
        bd->rate = opts->rate;
        bd->pitch = opts->pitch;
        bd->volume = opts->volume;
        bd->voice = opts->voice;
    }
    else
    {
        bd->rate = 150;
        bd->pitch = 110;
        bd->volume = 100;
        bd->voice = TTS_VOICE_MALE;
    }

    bd->child_pid = 0;
    bd->paused = 0;

    /* Ignore SIGPIPE so broken child pipe doesn't kill us */
    signal(SIGPIPE, SIG_IGN);

    return bd;
}

/* Wait/kill running child */
static void reap_child(UnixTtsData *bd)
{
    if (bd->child_pid > 0)
    {
        int st;
        int tries;

        /* SIGCONT before kill if paused */
        if (bd->paused)
        {
            kill(bd->child_pid, SIGCONT);
            bd->paused = 0;
        }

        /* SIGTERM first, SIGKILL if doesn't die quickly */
        kill(bd->child_pid, SIGTERM);

        for (tries = 0; tries < 20; tries++)
        {
            pid_t r = waitpid(bd->child_pid, &st, WNOHANG);

            if (r == bd->child_pid || r < 0)
            {
                bd->child_pid = 0;
                return;
            }

            usleep(10000); /* 10 ms */
        }

        /* Still alive, escalate */
        kill(bd->child_pid, SIGKILL);
        waitpid(bd->child_pid, &st, 0);

        bd->child_pid = 0;
    }
}

/* close_fn */
static void espeak_close(void *backend_data)
{
    UnixTtsData *bd = (UnixTtsData *)backend_data;

    if (!bd)
        return;

    reap_child(bd);

    free(bd);
}

/* apply_opts_fn */
static void espeak_apply_opts(void *backend_data, const TtsOpts *opts)
{
    UnixTtsData *bd = (UnixTtsData *)backend_data;

    if (!bd || !opts)
        return;

    bd->rate = opts->rate;
    bd->pitch = opts->pitch;
    bd->volume = opts->volume;
    bd->voice = opts->voice;
}

/* Build argv[] for selected binary */
static void build_argv_espeak(UnixTtsData *bd, char *argv_buf[], char rate_buf[], char pitch_buf[], char amp_buf[], char voice_buf[])
{
    int argi = 0;
    const char *lang = "en";
    int p;
    const char *env = NULL;

    argv_buf[argi++] = bd->binary_path;

    snprintf(rate_buf, 16, "%d", bd->rate);

    argv_buf[argi++] = (char *)"-s";
    argv_buf[argi++] = rate_buf;

    /* Map pitch 65..320 Hz to espeak 0..99 */
    p = (bd->pitch - 65) * 99 / (320 - 65);

    if (p < 0)
        p = 0;

    if (p > 99)
        p = 99;

    snprintf(pitch_buf, 16, "%d", p);

    argv_buf[argi++] = (char *)"-p";
    argv_buf[argi++] = pitch_buf;

    /* Volume 0..100 to espeak 0..200 */
    snprintf(amp_buf, 16, "%d", (bd->volume * 200) / 100);

    argv_buf[argi++] = (char *)"-a";
    argv_buf[argi++] = amp_buf;

    /* Determine language from $LANG, else "en" */
    env = getenv("LANG");

    if (env && env[0])
    {
        /* Extract language code from locale */
        static char langbuf[8];
        int i;

        for (i = 0; i < 7 && env[i] && env[i] != '_' && env[i] != '.'; i++)
            langbuf[i] = env[i];

        langbuf[i] = '\0';

        if (i >= 2)
            lang = langbuf;
    }

    /* Build voice string: lang+gender[+croak] */
    switch (bd->voice)
    {
    case TTS_VOICE_MALE:
    default:
        snprintf(voice_buf, 32, "%s+m3", lang);
        break;
    case TTS_VOICE_FEMALE:
        snprintf(voice_buf, 32, "%s+f3", lang);
        break;
    case TTS_VOICE_MALE_ROBOT:
        snprintf(voice_buf, 32, "%s+m3+croak", lang);
        break;
    case TTS_VOICE_FEMALE_ROBOT:
        snprintf(voice_buf, 32, "%s+f3+croak", lang);
        break;
    }

    argv_buf[argi++] = (char *)"-v";
    argv_buf[argi++] = voice_buf;

    argv_buf[argi++] = NULL;
}

static void build_argv_say(UnixTtsData *bd, char *argv_buf[], char rate_buf[])
{
    int argi = 0;

    argv_buf[argi++] = bd->binary_path;

    snprintf(rate_buf, 16, "%d", bd->rate);

    argv_buf[argi++] = (char *)"-r";
    argv_buf[argi++] = rate_buf;
    argv_buf[argi++] = NULL;
}

static void build_argv_festival(UnixTtsData *bd, char *argv_buf[])
{
    int argi = 0;

    argv_buf[argi++] = bd->binary_path;
    argv_buf[argi++] = (char *)"--tts";
    argv_buf[argi++] = NULL;
}

/* begin_fn */
static int espeak_begin(void *backend_data, const char *utf8_chunk, int len)
{
    UnixTtsData *bd = (UnixTtsData *)backend_data;
    int pipefd[2];
    pid_t pid;
    int written = 0;

    if (!bd || !utf8_chunk || len <= 0)
        return TTS_ERR_INTERNAL;

    if (bd->child_pid > 0)
        return TTS_ERR_BUSY;

    if (bd->binary == UNIX_TTS_NONE)
        return TTS_ERR_UNAVAILABLE;

    if (pipe(pipefd) < 0)
        return TTS_ERR_INTERNAL;

    pid = fork();

    if (pid < 0)
    {
        close(pipefd[0]);
        close(pipefd[1]);

        return TTS_ERR_INTERNAL;
    }

    if (pid == 0)
    {
        /* Child */
        char *argv_buf[16];
        char rate_buf[16];
        char pitch_buf[16];
        char amp_buf[16];
        char voice_buf[32];
        int devnull;

        /* stdin = read-end of pipe */
        if (dup2(pipefd[0], 0) < 0)
            _exit(127);

        close(pipefd[0]);
        close(pipefd[1]);

        /* Suppress child stdout/stderr to avoid terminal corruption */
        devnull = open("/dev/null", O_WRONLY);

        if (devnull >= 0)
        {
            dup2(devnull, 1);
            dup2(devnull, 2);
            close(devnull);
        }

        switch (bd->binary)
        {
        case UNIX_TTS_ESPEAK_NG:
        case UNIX_TTS_ESPEAK:
            build_argv_espeak(bd, argv_buf, rate_buf, pitch_buf, amp_buf, voice_buf);
            break;
        case UNIX_TTS_MACOS_SAY:
            build_argv_say(bd, argv_buf, rate_buf);
            break;
        case UNIX_TTS_FESTIVAL:
            build_argv_festival(bd, argv_buf);
            break;
        default:
            _exit(127);
        }

        execv(argv_buf[0], argv_buf);
        _exit(127);
    }

    /* Parent: write text to pipe, close so child gets EOF and speaks */
    close(pipefd[0]);

    while (written < len)
    {
        ssize_t r = write(pipefd[1], utf8_chunk + written,
                          (size_t)(len - written));

        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        if (r == 0)
            break;

        written += (int)r;
    }

    close(pipefd[1]);

    bd->child_pid = pid;
    bd->paused = 0;

    return TTS_OK;
}

/* poll_fn */
static int espeak_poll(void *backend_data)
{
    UnixTtsData *bd = (UnixTtsData *)backend_data;
    int st;
    pid_t r;

    if (!bd)
        return -1;

    if (bd->child_pid <= 0)
        return 1;

    if (bd->paused)
        return 0;

    r = waitpid(bd->child_pid, &st, WNOHANG);

    if (r == 0)
        return 0; /* still running */

    if (r == bd->child_pid || r < 0)
    {
        bd->child_pid = 0;
        return 1;
    }

    return 0;
}

/* Pause/resume/stop */
static void espeak_pause(void *backend_data)
{
    UnixTtsData *bd = (UnixTtsData *)backend_data;

    if (!bd || bd->child_pid <= 0 || bd->paused)
        return;

    if (kill(bd->child_pid, SIGSTOP) == 0)
        bd->paused = 1;
}

static void espeak_resume(void *backend_data)
{
    UnixTtsData *bd = (UnixTtsData *)backend_data;

    if (!bd || bd->child_pid <= 0 || !bd->paused)
        return;

    if (kill(bd->child_pid, SIGCONT) == 0)
        bd->paused = 0;
}

static void espeak_stop(void *backend_data)
{
    UnixTtsData *bd = (UnixTtsData *)backend_data;

    if (!bd)
        return;

    reap_child(bd);
}

static unsigned long espeak_sigmask(void *backend_data)
{
    return 0;
}

#endif /* !PLATFORM_AMIGA && !PLATFORM_WIN32 */
