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

/* Amiga TTS backend using translator.library and narrator.device */

#include "tts.h"
#include "tts_backend.h"

#ifdef PLATFORM_AMIGA

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <exec/exec.h>
#include <devices/narrator.h>
#include <libraries/translator.h>
#include <proto/exec.h>
#include <proto/translator.h>
#include <proto/dos.h>
#include <clib/alib_protos.h>

#include <string.h>
#include <stdlib.h>

/* Phoneme buffer size, 500 typical, larger for worst case */
#define PHONBUF_SIZE 640

/* Channel allocation mask, any left/right pair */
static UBYTE audio_chan[] = {3, 5, 10, 12};

/* LRU cache maps ASCII-folded input to phoneme output, invalidated on voice change */
#define AMIGA_CACHE_N 8
#define AMIGA_CACHE_KEY_MAX 220
#define AMIGA_CACHE_VAL_MAX 512

/* Forward declarations for dispatch table */
static void *amiga_open(const TtsOpts *opts);
static void amiga_close(void *backend_data);
static void amiga_apply_opts(void *backend_data, const TtsOpts *opts);
static int amiga_begin(void *backend_data, const char *utf8_chunk, int len);
static int amiga_poll(void *backend_data);
static void amiga_pause(void *backend_data);
static void amiga_resume(void *backend_data);
static void amiga_stop(void *backend_data);
static unsigned long amiga_sigmask(void *backend_data);

/* Dispatch table */
const TtsBackendVT tts_backend_amiga_vt =
    {
        amiga_open,
        amiga_close,
        amiga_apply_opts,
        amiga_begin,
        amiga_poll,
        amiga_pause,
        amiga_resume,
        amiga_stop,
        amiga_sigmask,
        "narrator.device + translator.library"};

typedef struct
{
    char key[AMIGA_CACHE_KEY_MAX];
    char val[AMIGA_CACHE_VAL_MAX];
    short prev;
    short next;
} AmigaCacheEntry;

/* Backend state owned by TtsHandle via backend_data pointer */
typedef struct
{
    struct Library *translator_base; /* borrowed reference; free with CloseLibrary */
    struct MsgPort *voice_port;      /* reply port for narrator_rb */
    struct narrator_rb *voice_io;    /* extended IORequest */

    int device_open;  /* 1 = OpenDevice succeeded */
    int io_in_flight; /* 1 = a SendIO is pending */
    int is_v37;       /* 1 if narrator.device is V37 or later */
    int paused;       /* 1 = CMD_STOP was issued and we haven't CMD_START'd */

    /* Pending opts to apply on next begin_fn (never mid-utterance) */
    UWORD pending_rate;
    UWORD pending_pitch;
    UWORD pending_sex;
    UWORD pending_mode;
    UWORD pending_volume;

    /* Phoneme scratch used by begin_fn */
    UBYTE phon_buf[PHONBUF_SIZE];

    /* ASCII fold scratch */
    char ascii_buf[AMIGA_CACHE_KEY_MAX];

    /* LRU cache */
    short cache_head;
    short cache_tail;
    short cache_count;
    AmigaCacheEntry cache[AMIGA_CACHE_N];
} AmigaTtsData;

/* LRU cache functions */

static void ac_init(AmigaTtsData *bd)
{
    int i;

    bd->cache_head = -1;
    bd->cache_tail = -1;
    bd->cache_count = 0;

    for (i = 0; i < AMIGA_CACHE_N; i++)
    {
        bd->cache[i].key[0] = '\0';
        bd->cache[i].prev = -1;
        bd->cache[i].next = -1;
    }
}

static void ac_unlink(AmigaTtsData *bd, int idx)
{
    AmigaCacheEntry *e = &bd->cache[idx];

    if (e->prev != -1)
        bd->cache[e->prev].next = e->next;
    else
        bd->cache_head = e->next;

    if (e->next != -1)
        bd->cache[e->next].prev = e->prev;
    else
        bd->cache_tail = e->prev;

    e->prev = -1;
    e->next = -1;
}

static void ac_push_front(AmigaTtsData *bd, int idx)
{
    AmigaCacheEntry *e = &bd->cache[idx];

    e->prev = -1;
    e->next = bd->cache_head;

    if (bd->cache_head != -1)
        bd->cache[bd->cache_head].prev = (short)idx;

    bd->cache_head = (short)idx;

    if (bd->cache_tail == -1)
        bd->cache_tail = (short)idx;
}

static int ac_find(const AmigaTtsData *bd, const char *key)
{
    int i;

    for (i = bd->cache_head; i != -1; i = bd->cache[i].next)
    {
        if (strcmp(bd->cache[i].key, key) == 0)
            return i;
    }

    return -1;
}

static int ac_acquire(AmigaTtsData *bd)
{
    int i;

    if (bd->cache_count < AMIGA_CACHE_N)
    {
        for (i = 0; i < AMIGA_CACHE_N; i++)
        {
            if (bd->cache[i].key[0] == '\0')
            {
                bd->cache_count++;
                return i;
            }
        }
    }

    i = bd->cache_tail;

    if (i < 0 || i >= AMIGA_CACHE_N)
    {
        ac_init(bd);

        bd->cache_count = 1;
        return 0;
    }

    ac_unlink(bd, i);

    return i;
}

/* Return val on hit and promote to head, NULL on miss */
static const char *ac_get(AmigaTtsData *bd, const char *key)
{
    int idx;

    if (!key || !key[0])
        return NULL;

    idx = ac_find(bd, key);

    if (idx < 0)
        return NULL;

    ac_unlink(bd, idx);
    ac_push_front(bd, idx);

    return bd->cache[idx].val;
}

static void ac_put(AmigaTtsData *bd, const char *key, const char *val)
{
    int idx;
    int klen;
    int vlen;
    AmigaCacheEntry *e = NULL;

    if (!key || !val || !key[0])
        return;

    idx = ac_acquire(bd);

    if (idx < 0 || idx >= AMIGA_CACHE_N)
        return;

    e = &bd->cache[idx];

    klen = (int)strlen(key);

    if (klen >= AMIGA_CACHE_KEY_MAX)
        klen = AMIGA_CACHE_KEY_MAX - 1;

    memcpy(e->key, key, (size_t)klen);
    e->key[klen] = '\0';

    vlen = (int)strlen(val);

    if (vlen >= AMIGA_CACHE_VAL_MAX)
        vlen = AMIGA_CACHE_VAL_MAX - 1;

    memcpy(e->val, val, (size_t)vlen);
    e->val[vlen] = '\0';

    ac_push_front(bd, idx);
}

/* Map TtsVoice enum to narrator_rb sex and mode fields */
static void voice_to_narrator_fields(int voice, UWORD *sex, UWORD *mode)
{
    switch (voice)
    {
    case TTS_VOICE_MALE:
    default:
        *sex = MALE;
        *mode = NATURALF0;
        break;

    case TTS_VOICE_FEMALE:
        *sex = FEMALE;
        *mode = NATURALF0;
        break;

    case TTS_VOICE_MALE_ROBOT:
        *sex = MALE;
        *mode = ROBOTICF0;
        break;

    case TTS_VOICE_FEMALE_ROBOT:
        *sex = FEMALE;
        *mode = ROBOTICF0;
        break;
    }
}

/* Open translator.library, prefer v43 over v37 */
static struct Library *open_translator(void)
{
    struct Library *lib = NULL;

    lib = OpenLibrary((STRPTR) "translator.library", 43);

    if (lib)
        return lib;

    lib = OpenLibrary((STRPTR) "translator.library", 37);

    return lib;
}

static void *amiga_open(const TtsOpts *opts)
{
    AmigaTtsData *bd = NULL;
    long err;
    UWORD sex;
    UWORD mode;
    int voice;
    int rate;
    int pitch;
    int volume;

    bd = (AmigaTtsData *)AllocMem(sizeof(*bd), MEMF_PUBLIC | MEMF_CLEAR);

    if (!bd)
        return NULL;

    /* Open translator.library */
    bd->translator_base = open_translator();

    if (!bd->translator_base)
    {
        FreeMem(bd, sizeof(*bd));
        return NULL;
    }

    /* Create reply MsgPort */
    bd->voice_port = CreateMsgPort();

    if (!bd->voice_port)
    {
        CloseLibrary(bd->translator_base);

        FreeMem(bd, sizeof(*bd));
        return NULL;
    }

    /* Create extended IORequest of type narrator_rb */
    bd->voice_io = (struct narrator_rb *)CreateIORequest(bd->voice_port, sizeof(struct narrator_rb));

    if (!bd->voice_io)
    {
        DeleteMsgPort(bd->voice_port);

        CloseLibrary(bd->translator_base);
        FreeMem(bd, sizeof(*bd));
        return NULL;
    }

    /* Set NDF_NEWIORB before OpenDevice to request V37 features */
    bd->voice_io->flags = NDF_NEWIORB;

    /* Open narrator.device */
    err = OpenDevice((STRPTR) "narrator.device", 0, (struct IORequest *)bd->voice_io, 0);

    if (err != 0)
    {
        /* Not installed, user may not have copied narrator.device to DEVS: */
        DeleteIORequest((struct IORequest *)bd->voice_io);
        DeleteMsgPort(bd->voice_port);

        CloseLibrary(bd->translator_base);
        FreeMem(bd, sizeof(*bd));
        return NULL;
    }

    bd->device_open = 1;

    /* Initialize LRU cache */
    ac_init(bd);

    /* Detect V37 by checking if NDF_NEWIORB survived OpenDevice */
    bd->is_v37 = (bd->voice_io->flags & NDF_NEWIORB) ? 1 : 0;

    /* Fixed audio-channel allocation */
    bd->voice_io->ch_masks = audio_chan;
    bd->voice_io->nm_masks = sizeof(audio_chan);

    /* Apply initial options */
    voice = opts ? opts->voice : TTS_VOICE_MALE;
    rate = opts ? opts->rate : 150;
    pitch = opts ? opts->pitch : 110;
    volume = opts ? opts->volume : 100;

    voice_to_narrator_fields(voice, &sex, &mode);

    bd->pending_rate = (UWORD)rate;
    bd->pending_pitch = (UWORD)pitch;
    bd->pending_sex = sex;
    bd->pending_mode = mode;

    /* Scale volume from 0-100 to 0-64 for narrator */
    bd->pending_volume = (UWORD)((volume * 64) / 100);

    bd->voice_io->rate = bd->pending_rate;
    bd->voice_io->pitch = bd->pending_pitch;
    bd->voice_io->sex = bd->pending_sex;
    bd->voice_io->mode = bd->pending_mode;
    bd->voice_io->volume = bd->pending_volume;
    bd->voice_io->sampfreq = DEFFREQ;
    bd->voice_io->mouths = 0; /* don't harvest mouth shapes */

    return bd;
}

static void amiga_close(void *backend_data)
{
    AmigaTtsData *bd = (AmigaTtsData *)backend_data;

    if (!bd)
        return;

    /* Cancel in-flight request cleanly */
    if (bd->io_in_flight)
    {
        AbortIO((struct IORequest *)bd->voice_io);
        WaitIO((struct IORequest *)bd->voice_io);
        bd->io_in_flight = 0;
    }

    if (bd->device_open)
    {
        CloseDevice((struct IORequest *)bd->voice_io);
        bd->device_open = 0;
    }

    if (bd->voice_io)
    {
        DeleteIORequest((struct IORequest *)bd->voice_io);
        bd->voice_io = NULL;
    }

    if (bd->voice_port)
    {
        DeleteMsgPort(bd->voice_port);
        bd->voice_port = NULL;
    }

    if (bd->translator_base)
    {
        CloseLibrary(bd->translator_base);
        bd->translator_base = NULL;
    }

    FreeMem(bd, sizeof(*bd));
}

static void amiga_apply_opts(void *backend_data, const TtsOpts *opts)
{
    AmigaTtsData *bd = (AmigaTtsData *)backend_data;
    UWORD sex;
    UWORD mode;
    int voice_changed;

    if (!bd || !opts)
        return;

    voice_to_narrator_fields(opts->voice, &sex, &mode);

    voice_changed = (sex != bd->pending_sex) || (mode != bd->pending_mode);

    /* Store as pending, begin_fn copies to narrator_rb between utterances */
    bd->pending_rate = (UWORD)opts->rate;
    bd->pending_pitch = (UWORD)opts->pitch;
    bd->pending_sex = sex;
    bd->pending_mode = mode;
    bd->pending_volume = (UWORD)((opts->volume * 64) / 100);

    /* Voice change invalidates cache, pitch/rate/volume do not */
    if (voice_changed)
        ac_init(bd);
}

/* Translate ASCII sentence to phonemes, fill phon_buf, return length or 0 on error */
static int translate_full(AmigaTtsData *bd, const char *ascii, int ascii_len)
{
    long rc;
    int consumed = 0;
    int phon_len = 0;
    int guard = 32;

    /* Loop if Translate() returns partial due to small buffer */
    while (consumed < ascii_len && phon_len < PHONBUF_SIZE - 1 && guard-- > 0)
    {
        int remaining_out = PHONBUF_SIZE - phon_len - 1;

        rc = Translate((STRPTR)(ascii + consumed), (LONG)(ascii_len - consumed), (STRPTR)(bd->phon_buf + phon_len), (LONG)remaining_out);

        if (rc == 0)
        {
            /* All consumed */
            phon_len += (int)strlen((char *)(bd->phon_buf + phon_len));
            consumed = ascii_len;
            break;
        }
        else if (rc < 0)
        {
            /* Partial, -rc is position where it stopped */
            int stopped_at = (int)(-rc);

            if (stopped_at <= consumed || stopped_at > ascii_len)
                break; /* Something odd, bail */

            phon_len += (int)strlen((char *)(bd->phon_buf + phon_len));
            consumed = stopped_at;
        }
        else
        {
            /* Positive return undocumented, treat as error */
            break;
        }
    }

    bd->phon_buf[phon_len] = '\0';

    return phon_len;
}

/* begin_fn -- start speaking ASCII/UTF-8 chunk */
static int amiga_begin(void *backend_data, const char *utf8_chunk, int len)
{
    AmigaTtsData *bd = (AmigaTtsData *)backend_data;
    const char *cached = NULL;
    int phon_len;
    int ascii_len;

    if (!bd || !utf8_chunk || len <= 0)
        return TTS_ERR_INTERNAL;

    if (bd->io_in_flight)
        return TTS_ERR_BUSY;

    if (!bd->device_open || !bd->translator_base)
        return TTS_ERR_UNAVAILABLE;

    /* Convert UTF-8 to ASCII fold, narrator only speaks phonetic English */
    ascii_len = tts_utf8_to_ascii_fold(utf8_chunk, bd->ascii_buf, sizeof(bd->ascii_buf));

    if (ascii_len <= 0)
        return TTS_ERR_INTERNAL;

    /* Check cache */
    cached = ac_get(bd, bd->ascii_buf);

    if (cached)
    {
        int cl = (int)strlen(cached);

        if (cl >= PHONBUF_SIZE)
            cl = PHONBUF_SIZE - 1;

        memcpy(bd->phon_buf, cached, (size_t)cl);
        bd->phon_buf[cl] = '\0';

        phon_len = cl;
    }
    else
    {
        phon_len = translate_full(bd, bd->ascii_buf, ascii_len);

        if (phon_len <= 0)
            return TTS_ERR_INTERNAL;

        ac_put(bd, bd->ascii_buf, (const char *)bd->phon_buf);
    }

    /* Apply pending opts to narrator_rb before request, safe since io_in_flight is 0 */
    bd->voice_io->rate = bd->pending_rate;
    bd->voice_io->pitch = bd->pending_pitch;
    bd->voice_io->sex = bd->pending_sex;
    bd->voice_io->mode = bd->pending_mode;
    bd->voice_io->volume = bd->pending_volume;

    if (bd->is_v37)
    {
        /* Reset V37-only tuning knobs to safe defaults */
        bd->voice_io->flags = NDF_NEWIORB;
        bd->voice_io->F0enthusiasm = DEFF0ENTHUS;
        bd->voice_io->F0perturb = DEFF0PERT;
        bd->voice_io->F1adj = 0;
        bd->voice_io->F2adj = 0;
        bd->voice_io->F3adj = 0;
        bd->voice_io->A1adj = 0;
        bd->voice_io->A2adj = 0;
        bd->voice_io->A3adj = 0;
        bd->voice_io->articulate = DEFARTIC;
        bd->voice_io->centralize = DEFCENTRAL;
        bd->voice_io->centphon = NULL;
        bd->voice_io->AVbias = 0;
        bd->voice_io->AFbias = 0;
        bd->voice_io->priority = DEFPRIORITY;
    }

    /* Fire async request */
    bd->voice_io->message.io_Command = CMD_WRITE;
    bd->voice_io->message.io_Offset = 0;
    bd->voice_io->message.io_Data = (APTR)bd->phon_buf;
    bd->voice_io->message.io_Length = (ULONG)phon_len;

    SendIO((struct IORequest *)bd->voice_io);

    bd->io_in_flight = 1;
    bd->paused = 0;

    return TTS_OK;
}

/* poll_fn -- check if CMD_WRITE finished */
static int amiga_poll(void *backend_data)
{
    AmigaTtsData *bd = (AmigaTtsData *)backend_data;

    if (!bd)
        return -1;

    if (!bd->io_in_flight)
        return 1; /* nothing in flight, done */

    if (bd->paused)
        return 0; /* still in flight */

    if (CheckIO((struct IORequest *)bd->voice_io))
    {
        /* Finished, reap request */
        WaitIO((struct IORequest *)bd->voice_io);

        bd->io_in_flight = 0;
        return 1;
    }

    return 0;
}

/* pause_fn / resume_fn / stop_fn */
static void amiga_pause(void *backend_data)
{
    AmigaTtsData *bd = (AmigaTtsData *)backend_data;
    struct IOStdReq cmd;

    if (!bd || !bd->io_in_flight || bd->paused)
        return;

    /* CMD_STOP is synchronous quick IO via temporary request to same device/unit */
    memset(&cmd, 0, sizeof(cmd));

    cmd.io_Device = bd->voice_io->message.io_Device;
    cmd.io_Unit = bd->voice_io->message.io_Unit;
    cmd.io_Message.mn_ReplyPort = bd->voice_port;
    cmd.io_Command = CMD_STOP;

    DoIO((struct IORequest *)&cmd);

    bd->paused = 1;
}

static void amiga_resume(void *backend_data)
{
    AmigaTtsData *bd = (AmigaTtsData *)backend_data;
    struct IOStdReq cmd;

    if (!bd || !bd->paused)
        return;

    memset(&cmd, 0, sizeof(cmd));

    cmd.io_Device = bd->voice_io->message.io_Device;
    cmd.io_Unit = bd->voice_io->message.io_Unit;
    cmd.io_Message.mn_ReplyPort = bd->voice_port;
    cmd.io_Command = CMD_START;

    DoIO((struct IORequest *)&cmd);

    bd->paused = 0;
}

static void amiga_stop(void *backend_data)
{
    AmigaTtsData *bd = (AmigaTtsData *)backend_data;

    if (!bd)
        return;

    if (bd->io_in_flight)
    {
        /* Resume first if paused so AbortIO can return cleanly */
        if (bd->paused)
            amiga_resume(bd);

        AbortIO((struct IORequest *)bd->voice_io);
        WaitIO((struct IORequest *)bd->voice_io);

        bd->io_in_flight = 0;
    }

    /* CMD_FLUSH clears queued requests */
    if (bd->device_open)
    {
        struct IOStdReq cmd;

        memset(&cmd, 0, sizeof(cmd));

        cmd.io_Device = bd->voice_io->message.io_Device;
        cmd.io_Unit = bd->voice_io->message.io_Unit;
        cmd.io_Message.mn_ReplyPort = bd->voice_port;
        cmd.io_Command = CMD_FLUSH;

        DoIO((struct IORequest *)&cmd);
    }

    bd->paused = 0;
}

/* sigmask_fn -- expose port signal for Wait() in event loop */
static unsigned long amiga_sigmask(void *backend_data)
{
    AmigaTtsData *bd = (AmigaTtsData *)backend_data;

    if (!bd || !bd->voice_port || !bd->io_in_flight)
        return 0UL;

    return 1UL << bd->voice_port->mp_SigBit;
}

#endif /* PLATFORM_AMIGA */
