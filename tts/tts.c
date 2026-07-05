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

/* Backend-agnostic core: opts, queue, sentence chunking */

#include "tts.h"
#include "tts_backend.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* Max chars per queue chunk, balance reactivity and natural speech */
#if defined(PLATFORM_AMIGA)
#define TTS_CHUNK_MAX 180
#define TTS_QUEUE_N 16
#else
#define TTS_CHUNK_MAX 240
#define TTS_QUEUE_N 32
#endif

/* Backend table forward declarations, only one linked per platform */
#if defined(PLATFORM_AMIGA)
extern const TtsBackendVT tts_backend_amiga_vt;
#define TTS_ACTIVE_BACKEND (&tts_backend_amiga_vt)
#elif defined(PLATFORM_WIN32)
extern const TtsBackendVT tts_backend_sapi_vt;
#define TTS_ACTIVE_BACKEND (&tts_backend_sapi_vt)
#else
extern const TtsBackendVT tts_backend_espeak_vt;
#define TTS_ACTIVE_BACKEND (&tts_backend_espeak_vt)
#endif

/* Queue: ring of pre-allocated chunks, speak() never allocates */
typedef struct
{
    char text[TTS_CHUNK_MAX + 1];
    int len;
} TtsChunk;

/* Handle structure */

struct TtsHandle
{
    TtsOpts opts;
    void *backend_data; /* opaque; owned by backend VT */

    /* Queue state */
    TtsChunk queue[TTS_QUEUE_N];
    int q_head; /* next chunk to feed to backend */
    int q_tail; /* next free slot */
    int q_count;

    /* Overflow text for whole documents, re-chunked as queue drains */
    char *pending;
    int pending_len;
    int pending_off;

    /* Playback state */
    int state;          /* TtsState */
    int current_active; /* 1 = a begin_fn is in flight */

    /* Change detection for tts_poll return code */
    int last_reported_state;
};

/* Options helpers */

void tts_opts_defaults(TtsOpts *opts)
{
    if (!opts)
        return;

    opts->rate = 150;
    opts->pitch = 110;
    opts->volume = 100;
    opts->voice = TTS_VOICE_MALE;
}

void tts_opts_clamp(TtsOpts *opts)
{
    if (!opts)
        return;

    if (opts->rate < 40)
        opts->rate = 40;

    if (opts->rate > 400)
        opts->rate = 400;

    if (opts->pitch < 65)
        opts->pitch = 65;

    if (opts->pitch > 320)
        opts->pitch = 320;

    if (opts->volume < 0)
        opts->volume = 0;

    if (opts->volume > 100)
        opts->volume = 100;

    if (opts->voice < 0 || opts->voice > 3)
        opts->voice = TTS_VOICE_MALE;
}

/* Cache: no-op, backends own their caches, kept for UI uniformity */
void tts_cache_clear(TtsHandle *h)
{
}

/* Queue functions */
static void queue_init(TtsHandle *h)
{
    int i;

    h->q_head = 0;
    h->q_tail = 0;
    h->q_count = 0;

    for (i = 0; i < TTS_QUEUE_N; i++)
    {
        h->queue[i].text[0] = '\0';
        h->queue[i].len = 0;
    }
}

/* Push chunk, return 1 if pushed, 0 if queue full */
static int queue_push(TtsHandle *h, const char *text, int len)
{
    TtsChunk *c = NULL;

    if (h->q_count >= TTS_QUEUE_N)
        return 0;

    if (len <= 0)
        return 0;

    if (len > TTS_CHUNK_MAX)
        len = TTS_CHUNK_MAX;

    c = &h->queue[h->q_tail];

    memcpy(c->text, text, (size_t)len);

    c->text[len] = '\0';
    c->len = len;

    h->q_tail = (h->q_tail + 1) % TTS_QUEUE_N;
    h->q_count++;

    return 1;
}

/* Peek next chunk without popping, NULL if empty */
static const TtsChunk *queue_peek(const TtsHandle *h)
{
    if (h->q_count == 0)
        return NULL;

    return &h->queue[h->q_head];
}

static void queue_pop(TtsHandle *h)
{
    if (h->q_count == 0)
        return;

    h->queue[h->q_head].text[0] = '\0';
    h->queue[h->q_head].len = 0;

    h->q_head = (h->q_head + 1) % TTS_QUEUE_N;
    h->q_count--;
}

static void queue_clear(TtsHandle *h)
{
    queue_init(h);
}

/* UTF-8 to ASCII fold for narrator.device, shared across backends */
int tts_utf8_to_ascii_fold(const char *utf8_in, char *ascii_out, int out_max);

int tts_utf8_to_ascii_fold(const char *utf8_in, char *ascii_out, int out_max)
{
    /* Small folder for Western European codepoints, others replaced by space */
    static const struct
    {
        unsigned int cp;
        char ascii;
    } fold[] =
        {
            {0xC0, 'A'}, {0xC1, 'A'}, {0xC2, 'A'}, {0xC3, 'A'}, {0xC4, 'A'}, {0xC5, 'A'}, {0xC6, 'A'}, {0xC7, 'C'}, {0xC8, 'E'}, {0xC9, 'E'}, {0xCA, 'E'}, {0xCB, 'E'}, {0xCC, 'I'}, {0xCD, 'I'}, {0xCE, 'I'}, {0xCF, 'I'}, {0xD1, 'N'}, {0xD2, 'O'}, {0xD3, 'O'}, {0xD4, 'O'}, {0xD5, 'O'}, {0xD6, 'O'}, {0xD8, 'O'}, {0xD9, 'U'}, {0xDA, 'U'}, {0xDB, 'U'}, {0xDC, 'U'}, {0xDD, 'Y'}, {0xE0, 'a'}, {0xE1, 'a'}, {0xE2, 'a'}, {0xE3, 'a'}, {0xE4, 'a'}, {0xE5, 'a'}, {0xE6, 'a'}, {0xE7, 'c'}, {0xE8, 'e'}, {0xE9, 'e'}, {0xEA, 'e'}, {0xEB, 'e'}, {0xEC, 'i'}, {0xED, 'i'}, {0xEE, 'i'}, {0xEF, 'i'}, {0xF1, 'n'}, {0xF2, 'o'}, {0xF3, 'o'}, {0xF4, 'o'}, {0xF5, 'o'}, {0xF6, 'o'}, {0xF8, 'o'}, {0xF9, 'u'}, {0xFA, 'u'}, {0xFB, 'u'}, {0xFC, 'u'}, {0xFD, 'y'}, {0xFF, 'y'},
            /* Spanish punctuation */
            {0xA1, '!'}, /* ¡ */
            {0xBF, '?'}, /* ¿ */
            {0, 0}};

    const unsigned char *p;
    int out = 0;
    int i;
    char replacement = ' ';

    if (!utf8_in || !ascii_out || out_max <= 0)
        return 0;

    p = (const unsigned char *)utf8_in;

    while (*p && out < out_max - 1)
    {
        unsigned int cp;
        int extra;

        if (*p < 0x80)
        {
            /* ASCII copy, sanitise control chars */
            unsigned char c = *p;

            if (c < 0x20 && c != '\t' && c != '\n' && c != '\r')
            {
                p++;
                continue;
            }

            ascii_out[out++] = (char)c;
            p++;

            continue;
        }

        /* Decode UTF-8 */
        if ((*p & 0xE0) == 0xC0)
        {
            cp = (unsigned int)(*p & 0x1F);
            extra = 1;
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            cp = (unsigned int)(*p & 0x0F);
            extra = 2;
        }
        else if ((*p & 0xF8) == 0xF0)
        {
            cp = (unsigned int)(*p & 0x07);
            extra = 3;
        }
        else
        {
            /* Invalid lead byte, skip */
            p++;
            continue;
        }

        p++;

        while (extra > 0)
        {
            if ((*p & 0xC0) != 0x80)
                break;
            cp = (cp << 6) | (unsigned int)(*p & 0x3F);
            p++;
            extra--;
        }

        for (i = 0; fold[i].cp != 0; i++)
        {
            if (fold[i].cp == cp)
            {
                replacement = fold[i].ascii;
                break;
            }
        }

        ascii_out[out++] = replacement;
    }

    ascii_out[out] = '\0';
    return out;
}

/* Sentence splitter: break on .!? and \n\n, never mid-word, trim whitespace */
static int is_sentence_end(char c)
{
    return c == '.' || c == '!' || c == '?';
}

static void enqueue_chunk(TtsHandle *h, const char *start, int len)
{
    /* Trim leading whitespace */
    while (len > 0 && (start[0] == ' ' || start[0] == '\t' || start[0] == '\n' || start[0] == '\r'))
    {
        start++;
        len--;
    }

    /* Trim trailing whitespace */
    while (len > 0)
    {
        char c = start[len - 1];

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            len--;
        else
            break;
    }

    if (len <= 0)
        return;

    queue_push(h, start, len);
}

/* Split text into sentence chunks, enqueue as many as fit, return consumed bytes */
static int split_and_enqueue(TtsHandle *h, const char *text, int total_len)
{
    int start;
    int i;

    if (total_len <= 0)
        return 0;

    start = 0;
    i = 0;

    while (i < total_len)
    {
        int at_sentence_end = is_sentence_end(text[i]);
        int at_para = (text[i] == '\n' && i + 1 < total_len && text[i + 1] == '\n');
        int chunk_len = i - start + (at_sentence_end ? 1 : 0);

        if (at_sentence_end || at_para || chunk_len >= TTS_CHUNK_MAX)
        {
            int len_here;

            /* Stop before consuming if queue full, caller saves as pending */
            if (h->q_count >= TTS_QUEUE_N)
                return start;

            if (at_sentence_end)
            {
                len_here = i - start + 1;
                enqueue_chunk(h, text + start, len_here);

                start = i + 1;
                i++;
            }
            else if (at_para)
            {
                len_here = i - start;
                enqueue_chunk(h, text + start, len_here);

                start = i + 2;
                i += 2;
            }
            else
            {
                /* Too long, break on last space before limit */
                int j;
                int cut = -1;

                for (j = i; j > start && j > i - 40; j--)
                {
                    if (text[j] == ' ')
                    {
                        cut = j;
                        break;
                    }
                }

                if (cut < 0)
                    cut = i;

                enqueue_chunk(h, text + start, cut - start);
                start = cut;

                while (start < total_len && text[start] == ' ')
                    start++;

                i = start;
            }
        }
        else
        {
            i++;
        }
    }

    /* Trailing fragment */
    if (start < total_len)
    {
        if (h->q_count >= TTS_QUEUE_N)
            return start;

        enqueue_chunk(h, text + start, total_len - start);
    }

    return total_len;
}

/* Free pending-overflow buffer */
static void pending_clear(TtsHandle *h)
{
    if (h->pending)
    {
        free(h->pending);

        h->pending = NULL;
    }

    h->pending_len = 0;
    h->pending_off = 0;
}

/* Move pending text into queue */
static void pending_refill(TtsHandle *h)
{
    int consumed;

    if (!h->pending || h->pending_off >= h->pending_len)
        return;

    consumed = split_and_enqueue(h, h->pending + h->pending_off, h->pending_len - h->pending_off);

    h->pending_off += consumed;

    if (h->pending_off >= h->pending_len)
        pending_clear(h);
}

int tts_is_available(void)
{
    return TTS_ACTIVE_BACKEND != NULL && TTS_ACTIVE_BACKEND->open_fn != NULL;
}

const char *tts_backend_name(void)
{
    if (!TTS_ACTIVE_BACKEND)
        return "none";

    return TTS_ACTIVE_BACKEND->name ? TTS_ACTIVE_BACKEND->name : "unknown";
}

const char *tts_voice_name(int voice)
{
    switch (voice)
    {
    case TTS_VOICE_MALE:
        return "Male";
    case TTS_VOICE_FEMALE:
        return "Female";
    case TTS_VOICE_MALE_ROBOT:
        return "Male Robot";
    case TTS_VOICE_FEMALE_ROBOT:
        return "Female Robot";
    default:
        return "?";
    }
}

TtsHandle *tts_new(const TtsOpts *opts)
{
    TtsHandle *h = NULL;
    TtsOpts eff;

    if (!TTS_ACTIVE_BACKEND || !TTS_ACTIVE_BACKEND->open_fn)
        return NULL;

    h = (TtsHandle *)calloc(1, sizeof(*h));

    if (!h)
        return NULL;

    if (opts)
    {
        eff = *opts;
    }
    else
    {
        tts_opts_defaults(&eff);
    }

    tts_opts_clamp(&eff);

    h->opts = eff;

    h->backend_data = TTS_ACTIVE_BACKEND->open_fn(&eff);

    if (!h->backend_data)
    {
        free(h);
        return NULL;
    }

    queue_init(h);

    h->state = TTS_STATE_IDLE;
    h->last_reported_state = TTS_STATE_IDLE;
    h->current_active = 0;

    return h;
}

void tts_free(TtsHandle *h)
{
    if (!h)
        return;

    /* Drain in-flight speech synchronously via stop */
    if (TTS_ACTIVE_BACKEND && TTS_ACTIVE_BACKEND->stop_fn && h->backend_data)
        TTS_ACTIVE_BACKEND->stop_fn(h->backend_data);

    if (TTS_ACTIVE_BACKEND && TTS_ACTIVE_BACKEND->close_fn && h->backend_data)
        TTS_ACTIVE_BACKEND->close_fn(h->backend_data);

    h->backend_data = NULL;

    pending_clear(h);
    free(h);
}

int tts_set_opts(TtsHandle *h, const TtsOpts *opts)
{
    TtsOpts eff;

    if (!h || !opts)
        return TTS_ERR_INTERNAL;

    eff = *opts;

    tts_opts_clamp(&eff);

    h->opts = eff;

    if (TTS_ACTIVE_BACKEND && TTS_ACTIVE_BACKEND->apply_opts_fn && h->backend_data)
        TTS_ACTIVE_BACKEND->apply_opts_fn(h->backend_data, &eff);

    return TTS_OK;
}

int tts_speak(TtsHandle *h, const char *utf8_text)
{
    int len;
    int consumed;

    if (!h || !utf8_text)
        return TTS_ERR_INTERNAL;

    len = (int)strlen(utf8_text);

    if (len <= 0)
        return TTS_OK;

    consumed = split_and_enqueue(h, utf8_text, len);

    /* Unfit text goes to pending buffer for whole-document dictation */
    if (consumed < len)
    {
        int tail_len = len - consumed;

        if (!h->pending)
        {
            h->pending = (char *)malloc((size_t)tail_len + 1);

            if (!h->pending)
                return TTS_ERR_NOMEM;

            memcpy(h->pending, utf8_text + consumed, (size_t)tail_len);

            h->pending[tail_len] = '\0';
            h->pending_len = tail_len;
            h->pending_off = 0;
        }
        else
        {
            /* Compact unread part, append new tail using fresh buffer */
            int unread = h->pending_len - h->pending_off;
            char *nb = (char *)malloc((size_t)unread + (size_t)tail_len + 1);

            if (!nb)
                return TTS_ERR_NOMEM; /* old pending stays intact */

            memcpy(nb, h->pending + h->pending_off, (size_t)unread);
            memcpy(nb + unread, utf8_text + consumed, (size_t)tail_len);

            nb[unread + tail_len] = '\0';

            free(h->pending);

            h->pending = nb;
            h->pending_len = unread + tail_len;
            h->pending_off = 0;
        }
    }

    /* Kick off immediately if idle */
    if (h->state == TTS_STATE_IDLE && h->q_count > 0)
        h->state = TTS_STATE_PLAYING;

    return TTS_OK;
}

int tts_pause(TtsHandle *h)
{
    if (!h)
        return TTS_ERR_INTERNAL;

    if (h->state != TTS_STATE_PLAYING)
        return TTS_ERR_BADSTATE;

    if (TTS_ACTIVE_BACKEND && TTS_ACTIVE_BACKEND->pause_fn && h->backend_data)
        TTS_ACTIVE_BACKEND->pause_fn(h->backend_data);

    h->state = TTS_STATE_PAUSED;

    return TTS_OK;
}

int tts_resume(TtsHandle *h)
{
    if (!h)
        return TTS_ERR_INTERNAL;

    if (h->state != TTS_STATE_PAUSED)
        return TTS_ERR_BADSTATE;

    if (TTS_ACTIVE_BACKEND && TTS_ACTIVE_BACKEND->resume_fn && h->backend_data)
        TTS_ACTIVE_BACKEND->resume_fn(h->backend_data);

    h->state = TTS_STATE_PLAYING;

    return TTS_OK;
}

int tts_stop(TtsHandle *h)
{
    if (!h)
        return TTS_ERR_INTERNAL;

    if (TTS_ACTIVE_BACKEND && TTS_ACTIVE_BACKEND->stop_fn && h->backend_data)
        TTS_ACTIVE_BACKEND->stop_fn(h->backend_data);

    queue_clear(h);
    pending_clear(h);

    h->state = TTS_STATE_IDLE;
    h->current_active = 0;

    return TTS_OK;
}

TtsState tts_get_state(TtsHandle *h)
{
    if (!h)
        return TTS_STATE_IDLE;

    return (TtsState)h->state;
}

int tts_poll(TtsHandle *h)
{
    int state_changed = 0;

    if (!h)
        return 0;

    if (!TTS_ACTIVE_BACKEND || !h->backend_data)
        return 0;

    /* Pump backend if request in flight */
    if (h->current_active && h->state == TTS_STATE_PLAYING)
    {
        int r = TTS_ACTIVE_BACKEND->poll_fn ? TTS_ACTIVE_BACKEND->poll_fn(h->backend_data) : 1;

        if (r != 0)
        {
            /* Current chunk finished or errored */
            h->current_active = 0;
            queue_pop(h);
        }
    }

    /* Refill queue from pending buffer as it drains */
    if (h->state == TTS_STATE_PLAYING && h->q_count < TTS_QUEUE_N / 2)
        pending_refill(h);

    /* Launch next chunk if idle with queue or just finished */
    if (!h->current_active && h->state == TTS_STATE_PLAYING && h->q_count > 0 && TTS_ACTIVE_BACKEND->begin_fn)
    {
        const TtsChunk *c = queue_peek(h);

        if (c && c->len > 0)
        {
            int rc = TTS_ACTIVE_BACKEND->begin_fn(h->backend_data, c->text, c->len);

            if (rc == TTS_OK)
                h->current_active = 1;
            else if (rc != TTS_ERR_BUSY)
                queue_pop(h);
        }
    }

    /* Queue and pending drained, back to idle */
    if (!h->current_active && h->q_count == 0 && !h->pending && h->state == TTS_STATE_PLAYING)
        h->state = TTS_STATE_IDLE;

    if (h->state != h->last_reported_state)
    {
        state_changed = 1;
        h->last_reported_state = h->state;
    }

    return state_changed;
}

unsigned long tts_amiga_sigmask(TtsHandle *h)
{
    if (!h || !TTS_ACTIVE_BACKEND || !TTS_ACTIVE_BACKEND->sigmask_fn || !h->backend_data)
        return 0UL;

    return TTS_ACTIVE_BACKEND->sigmask_fn(h->backend_data);
}
