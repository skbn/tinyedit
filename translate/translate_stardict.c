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

/* Where to get dictionaries:
 *   http://download.huzheng.org/         original archive, HTTP plain
 *   https://freedict.org/downloads/      CC-licensed bilinguals
 *   https://stardict.uber.space/         Wayback-scraped mirror
 */

#include "translate_stardict.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(PLATFORM_AMIGA)
#include <dos/dos.h>
#include <proto/dos.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#if defined(PLATFORM_AMIGA)
#define SD_CACHE_N 16
#define SD_CACHE_KEY_MAX 64
#define SD_CACHE_VAL_MAX 512
#define SD_MAX_IDX_BYTES (4L * 1024L * 1024L)
#else
#define SD_CACHE_N 128
#define SD_CACHE_KEY_MAX 128
#define SD_CACHE_VAL_MAX 2048
#define SD_MAX_IDX_BYTES (32L * 1024L * 1024L)
#endif

#define SD_MAX_DICT_ENTRY_BYTES (256 * 1024)
#define SD_MAX_WORDS_PER_LOOKUP 32

typedef struct
{
    char *word;
    unsigned long offset;
    unsigned long size;
} SdEntry;

/* Synonym entry: word in syn_buf points to a word, target_idx is an index into the main entries[] array */
typedef struct
{
    char *word;
    int target_idx;
} SdSyn;

typedef struct
{
    char key[SD_CACHE_KEY_MAX];
    char val[SD_CACHE_VAL_MAX];
    short prev;
    short next;
} SdCacheEntry;

struct StarDictHandle
{
    char ifo_path[512];
    FILE *dict_fp;
    char *idx_buf;
    long idx_buf_size;
    SdEntry *entries;
    int entry_count;
    int use_64bit_offsets;
    /* Synonyms (.syn). NULL if dictionary has no .syn or it failed to load. The reader treats absence as "no synonyms" silently */
    char *syn_buf;
    long syn_buf_size;
    SdSyn *syns;
    int syn_count;

    /* sametypesequence: empty string if absent. If present, every
     * dict entry contains exactly these type codes in order
     *   "m"   plain UTF-8 text  (most bilingual dicts)
     *   "h"   HTML
     *   "x"   XDXF (XML)
     *   "g"   Pango markup
     * If multi-char (e.g. "tm"), each entry has multiple typed parts */
    char sametypesequence[16];
    char from_lang[16];
    char to_lang[16];
    char bookname[128];
    SdCacheEntry cache[SD_CACHE_N];
    short cache_head;
    short cache_tail;
    short cache_count;
};

typedef struct
{
    unsigned long wordcount;
    unsigned long idxfilesize;
    int idxoffsetbits;
    char sametypesequence[32];
    char bookname[128];
} SdIfo;

/* Decodes dict entry to plain text, handling various formats and stripping HTML/XML markup */
static char *strip_markup_inplace(char *s);
static char *decode_dict_entry(const char *raw, unsigned long raw_size, const char *sametypesequence);

static int sd_strcmp(const char *a, const char *b)
{
    const unsigned char *p1 = (const unsigned char *)a;
    const unsigned char *p2 = (const unsigned char *)b;
    int c1, c2;

    for (;;)
    {
        c1 = *p1++;
        c2 = *p2++;

        if (c1 < 128)
            c1 = tolower(c1);

        if (c2 < 128)
            c2 = tolower(c2);

        if (c1 != c2)
            return (c1 < c2) ? -1 : 1;

        if (c1 == 0)
            return 0;
    }
}

static void cache_init(struct StarDictHandle *h)
{
    int i;

    h->cache_head = -1;
    h->cache_tail = -1;
    h->cache_count = 0;

    for (i = 0; i < SD_CACHE_N; i++)
    {
        h->cache[i].key[0] = '\0';
        h->cache[i].prev = -1;
        h->cache[i].next = -1;
    }
}

static int cache_find(struct StarDictHandle *h, const char *key)
{
    int i;

    for (i = 0; i < SD_CACHE_N; i++)
    {
        if (h->cache[i].key[0] && strcmp(h->cache[i].key, key) == 0)
            return i;
    }

    return -1;
}

static void cache_unlink(struct StarDictHandle *h, int i)
{
    short p = h->cache[i].prev;
    short n = h->cache[i].next;

    if (p >= 0)
        h->cache[p].next = n;
    else
        h->cache_head = n;

    if (n >= 0)
        h->cache[n].prev = p;
    else
        h->cache_tail = p;

    h->cache[i].prev = -1;
    h->cache[i].next = -1;
}

static void cache_push_front(struct StarDictHandle *h, int i)
{
    h->cache[i].prev = -1;
    h->cache[i].next = h->cache_head;

    if (h->cache_head >= 0)
        h->cache[h->cache_head].prev = (short)i;

    h->cache_head = (short)i;

    if (h->cache_tail < 0)
        h->cache_tail = (short)i;
}

static int cache_acquire(struct StarDictHandle *h)
{
    int i;

    if (h->cache_count < SD_CACHE_N)
    {
        for (i = 0; i < SD_CACHE_N; i++)
        {
            if (h->cache[i].key[0] == '\0')
            {
                h->cache_count++;
                return i;
            }
        }
    }

    i = h->cache_tail;

    if (i < 0 || i >= SD_CACHE_N)
    {
        cache_init(h);

        h->cache_count = 1;
        return 0;
    }

    cache_unlink(h, i);
    return i;
}

static int parse_ifo(const char *path, SdIfo *out, char *err, int err_size)
{
    FILE *fp = NULL;
    char line[512];

    memset(out, 0, sizeof(*out));
    out->idxoffsetbits = 32;

    fp = fopen(path, "rb");

    if (!fp)
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "cannot open %s", path);

        return -1;
    }

    if (!fgets(line, sizeof(line), fp))
    {
        fclose(fp);

        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, ".ifo is empty");

        return -1;
    }

    if (strncmp(line, "StarDict's dict ifo file", 24) != 0 && strncmp(line, "StarDict's treedict ifo file", 28) != 0)
    {
        fclose(fp);

        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "not a StarDict .ifo file");

        return -1;
    }

    while (fgets(line, sizeof(line), fp))
    {
        char *eq = NULL;
        char *val = NULL;
        size_t kl;

        kl = strlen(line);

        while (kl > 0 && (line[kl - 1] == '\n' || line[kl - 1] == '\r' || line[kl - 1] == ' ' || line[kl - 1] == '\t'))
            line[--kl] = '\0';

        if (kl == 0)
            continue;

        eq = strchr(line, '=');

        if (!eq)
            continue;

        *eq = '\0';
        val = eq + 1;

        if (strcmp(line, "wordcount") == 0)
            out->wordcount = strtoul(val, NULL, 10);
        else if (strcmp(line, "idxfilesize") == 0)
            out->idxfilesize = strtoul(val, NULL, 10);
        else if (strcmp(line, "idxoffsetbits") == 0)
            out->idxoffsetbits = (int)strtol(val, NULL, 10);
        else if (strcmp(line, "sametypesequence") == 0)
        {
            strncpy(out->sametypesequence, val, sizeof(out->sametypesequence) - 1);

            out->sametypesequence[sizeof(out->sametypesequence) - 1] = '\0';
        }
        else if (strcmp(line, "bookname") == 0)
        {
            strncpy(out->bookname, val, sizeof(out->bookname) - 1);

            out->bookname[sizeof(out->bookname) - 1] = '\0';
        }
    }

    fclose(fp);

    if (out->wordcount == 0 || out->idxfilesize == 0)
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "incomplete .ifo (wordcount=%lu, idxfilesize=%lu)", out->wordcount, out->idxfilesize);
        return -1;
    }

    if (out->idxoffsetbits != 32 && out->idxoffsetbits != 64)
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "unsupported idxoffsetbits=%d", out->idxoffsetbits);
        return -1;
    }

    return 0;
}

/* Returns 1 if path ends in ".ifo" (case insensitive) */
static int has_ifo_suffix(const char *path)
{
    size_t n = strlen(path);

    if (n < 4)
        return 0;

    return (path[n - 4] == '.' &&
            (path[n - 3] == 'i' || path[n - 3] == 'I') &&
            (path[n - 2] == 'f' || path[n - 2] == 'F') &&
            (path[n - 1] == 'o' || path[n - 1] == 'O'));
}

/* Is path a directory? */
static int is_directory(const char *path)
{
#if defined(PLATFORM_AMIGA)
    BPTR lock;
    struct FileInfoBlock *fib = NULL;
    int result = 0;

    lock = Lock((STRPTR)path, ACCESS_READ);

    if (!lock)
        return 0;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

    if (fib)
    {
        if (Examine(lock, fib))
            result = (fib->fib_DirEntryType > 0);

        FreeDosObject(DOS_FIB, fib);
    }

    Close(lock);
    return result;
#elif defined(_WIN32)
    DWORD attrs = GetFileAttributesA(path);

    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;

    return S_ISDIR(st.st_mode);
#endif
}

/* Find first .ifo file in directory, returns path or error */
static int find_ifo_in_dir(const char *dir, char *out, int out_size)
{
#if defined(PLATFORM_AMIGA)
    BPTR lock;
    struct FileInfoBlock *fib = NULL;
    int found = 0;

    lock = Lock((STRPTR)dir, ACCESS_READ);

    if (!lock)
        return -1;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

    if (!fib)
    {
        Close(lock);
        return -1;
    }

    if (Examine(lock, fib))
    {
        while (ExNext(lock, fib))
        {
            if (fib->fib_DirEntryType < 0 && has_ifo_suffix(fib->fib_FileName))
            {
                int n = strlen(dir);

                /* Insert '/' if dir doesn't end in separator */
                char sep = (n > 0 && (dir[n - 1] == '/' || dir[n - 1] == ':')) ? '\0' : '/';

                if (sep)
                    snprintf(out, (size_t)out_size, "%s/%s", dir, fib->fib_FileName);
                else
                    snprintf(out, (size_t)out_size, "%s%s", dir, fib->fib_FileName);

                found = 1;
                break;
            }
        }
    }

    FreeDosObject(DOS_FIB, fib);
    Close(lock);

    return found ? 0 : -1;
#elif defined(_WIN32)
    char pattern[1024];
    HANDLE hFind;
    WIN32_FIND_DATAA fd;
    int found = 0;

    if (snprintf(pattern, sizeof(pattern), "%s\\*", dir) >= (int)sizeof(pattern))
        return -1;

    hFind = FindFirstFileA(pattern, &fd);

    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && has_ifo_suffix(fd.cFileName))
            {
                snprintf(out, (size_t)out_size, "%s\\%s", dir, fd.cFileName);

                found = 1;
                break;
            }
        } while (FindNextFileA(hFind, &fd));

        FindClose(hFind);
    }

    return found ? 0 : -1;
#else
    DIR *d = NULL;
    struct dirent *de = NULL;
    int found = 0;

    d = opendir(dir);

    if (!d)
        return -1;

    while ((de = readdir(d)) != NULL)
    {
        if (de->d_name[0] == '.')
            continue;

        if (has_ifo_suffix(de->d_name))
        {
            snprintf(out, (size_t)out_size, "%s/%s", dir, de->d_name);
            found = 1;
            break;
        }
    }

    closedir(d);
    return found ? 0 : -1;
#endif
}

static void derive_sibling(const char *ifo_path, const char *new_ext, char *out, int out_size)
{
    int n = (int)strlen(ifo_path);
    int dot = n;
    int i;

    for (i = n - 1; i >= 0; i--)
    {
        if (ifo_path[i] == '.')
        {
            dot = i;
            break;
        }

        if (ifo_path[i] == '/' || ifo_path[i] == '\\' || ifo_path[i] == ':')
            break;
    }

    snprintf(out, (size_t)out_size, "%.*s%s", dot, ifo_path, new_ext);
}

static int file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");

    if (!fp)
        return 0;

    fclose(fp);
    return 1;
}

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | ((unsigned long)p[3]);
}

int translate_stardict_open(const char *path, const char *from, const char *to, struct StarDictHandle **out, char *err, int err_size)
{
    struct StarDictHandle *h = NULL;
    SdIfo ifo;
    char ifo_path[512];
    char idx_path[512];
    char dict_path[512];
    char dict_dz_path[512];
    FILE *fp = NULL;
    long sz;
    char *buf = NULL;
    SdEntry *entries = NULL;
    int entry_count = 0;
    int entry_cap = 0;
    unsigned char *p = NULL;
    unsigned char *end = NULL;
    int off_bytes;
    char syn_path[512];
    FILE *sfp = NULL;
    long ssz;
    char *sbuf = NULL;
    SdSyn *syns = NULL;
    int syn_cap;
    int syn_count;
    unsigned char *sp = NULL;
    unsigned char *send = NULL;

    *out = NULL;

    if (err && err_size > 0)
        err[0] = '\0';

    if (!path || !path[0])
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "stardict path is empty");

        return -1;
    }

    /* Auto-discovery: directory -> first .ifo inside */
    if (has_ifo_suffix(path))
    {
        strncpy(ifo_path, path, sizeof(ifo_path) - 1);

        ifo_path[sizeof(ifo_path) - 1] = '\0';
    }
    else if (is_directory(path))
    {
        if (find_ifo_in_dir(path, ifo_path, sizeof(ifo_path)) != 0)
        {
            if (err && err_size > 0)
                snprintf(err, (size_t)err_size, "no .ifo file found in directory %s", path);

            return -1;
        }
    }
    else
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "%s: not a .ifo file and not a directory", path);

        return -1;
    }

    if (parse_ifo(ifo_path, &ifo, err, err_size) < 0)
        return -1;

    derive_sibling(ifo_path, ".idx", idx_path, sizeof(idx_path));
    derive_sibling(ifo_path, ".dict", dict_path, sizeof(dict_path));
    derive_sibling(ifo_path, ".dict.dz", dict_dz_path, sizeof(dict_dz_path));

    /* If .dict is missing but .dict.dz exists, give a helpful message */
    if (!file_exists(dict_path) && file_exists(dict_dz_path))
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "%s exists but %s does not -- run \n`gunzip \"%s\"` to decompress it", dict_dz_path, dict_path, dict_dz_path);

        return -1;
    }

    /* Slurp .idx */
    fp = fopen(idx_path, "rb");

    if (!fp)
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "cannot open %s", idx_path);

        return -1;
    }

    fseek(fp, 0, SEEK_END);
    sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0)
    {
        fclose(fp);

        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, ".idx is empty");

        return -1;
    }

    if (sz > SD_MAX_IDX_BYTES)
    {
        fclose(fp);

        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, ".idx too large (%ld bytes > limit %ld) -- pick a \nsmaller dictionary", sz, (long)SD_MAX_IDX_BYTES);

        return -1;
    }

    buf = (char *)malloc((size_t)sz + 1);

    if (!buf)
    {
        fclose(fp);

        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "out of memory loading .idx");

        return -1;
    }

    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz)
    {
        free(buf);
        fclose(fp);

        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "short read on .idx");

        return -1;
    }

    buf[sz] = '\0';
    fclose(fp);

    /* Build entries array. Each record: word + \0 + offset(N) + size(4) */
    off_bytes = ifo.idxoffsetbits / 8;

    entry_cap = (int)(ifo.wordcount > 0 ? ifo.wordcount : 1024);

    if (entry_cap > 1000000)
        entry_cap = 1000000;

    entries = (SdEntry *)malloc(sizeof(SdEntry) * (size_t)entry_cap);

    if (!entries)
    {
        free(buf);

        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "out of memory allocating entries");

        return -1;
    }

    p = (unsigned char *)buf;
    end = (unsigned char *)buf + sz;

    while (p < end)
    {
        unsigned char *word_start = p;
        unsigned char *zero;
        unsigned long offset;
        unsigned long size;

        zero = (unsigned char *)memchr(p, 0, (size_t)(end - p));

        if (!zero || zero + 1 + off_bytes + 4 > end)
            break;

        if (off_bytes == 4)
            offset = be32(zero + 1);
        else
        {
            /* 64-bit: take low 32 -- most C runtimes can't seek beyond
             * 2 GB on classic Amiga anyway. Real 64-bit dicts are very rare */
            offset = be32(zero + 5);
        }

        size = be32(zero + 1 + off_bytes);

        if (entry_count >= entry_cap)
        {
            int new_cap = entry_cap * 2;
            SdEntry *neu = (SdEntry *)realloc(entries, sizeof(SdEntry) * (size_t)new_cap);

            if (!neu)
            {
                free(entries);
                free(buf);

                if (err && err_size > 0)
                    snprintf(err, (size_t)err_size, "out of memory growing entries");

                return -1;
            }

            entries = neu;
            entry_cap = new_cap;
        }

        entries[entry_count].word = (char *)word_start;
        entries[entry_count].offset = offset;
        entries[entry_count].size = size;
        entry_count++;

        p = zero + 1 + off_bytes + 4;
    }

    /* Open .dict */
    fp = fopen(dict_path, "rb");

    if (!fp)
    {
        free(entries);
        free(buf);

        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "cannot open %s", dict_path);

        return -1;
    }

    h = (struct StarDictHandle *)calloc(1, sizeof(*h));

    if (!h)
    {
        fclose(fp);

        free(entries);
        free(buf);

        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "out of memory");

        return -1;
    }

    strncpy(h->ifo_path, ifo_path, sizeof(h->ifo_path) - 1);

    h->ifo_path[sizeof(h->ifo_path) - 1] = '\0';
    h->dict_fp = fp;
    h->idx_buf = buf;
    h->idx_buf_size = sz;
    h->entries = entries;
    h->entry_count = entry_count;
    h->use_64bit_offsets = (off_bytes == 8);

    strncpy(h->sametypesequence, ifo.sametypesequence, sizeof(h->sametypesequence) - 1);
    h->sametypesequence[sizeof(h->sametypesequence) - 1] = '\0';

    if (from)
    {
        strncpy(h->from_lang, from, sizeof(h->from_lang) - 1);
        h->from_lang[sizeof(h->from_lang) - 1] = '\0';
    }

    if (to)
    {
        strncpy(h->to_lang, to, sizeof(h->to_lang) - 1);
        h->to_lang[sizeof(h->to_lang) - 1] = '\0';
    }

    strncpy(h->bookname, ifo.bookname, sizeof(h->bookname) - 1);
    h->bookname[sizeof(h->bookname) - 1] = '\0';

    cache_init(h);

    /* .syn file: expands word inflections (running->run, books->book) */
    derive_sibling(ifo_path, ".syn", syn_path, sizeof(syn_path));

    sfp = fopen(syn_path, "rb");

    if (sfp)
    {
        fseek(sfp, 0, SEEK_END);
        ssz = ftell(sfp);
        fseek(sfp, 0, SEEK_SET);

        if (ssz > 0 && ssz <= SD_MAX_IDX_BYTES)
        {
            sbuf = (char *)malloc((size_t)ssz + 1);

            if (sbuf && fread(sbuf, 1, (size_t)ssz, sfp) == (size_t)ssz)
            {
                sbuf[ssz] = '\0';

                syn_cap = 256;
                syns = (SdSyn *)malloc(sizeof(SdSyn) * (size_t)syn_cap);
                syn_count = 0;
                sp = (unsigned char *)sbuf;
                send = (unsigned char *)sbuf + ssz;

                while (sp < send && syns)
                {
                    unsigned char *zero = (unsigned char *)memchr(sp, 0, (size_t)(send - sp));
                    unsigned long target;

                    if (!zero || zero + 1 + 4 > send)
                        break;

                    target = be32(zero + 1);

                    if (syn_count >= syn_cap)
                    {
                        SdSyn *neu;
                        syn_cap *= 2;
                        neu = (SdSyn *)realloc(syns, sizeof(SdSyn) * (size_t)syn_cap);

                        if (!neu)
                        {
                            free(syns);

                            syns = NULL;
                            break;
                        }

                        syns = neu;
                    }

                    syns[syn_count].word = (char *)sp;
                    syns[syn_count].target_idx = (int)target;
                    syn_count++;
                    sp = zero + 1 + 4;
                }

                if (syns)
                {
                    h->syn_buf = sbuf;
                    h->syn_buf_size = ssz;
                    h->syns = syns;
                    h->syn_count = syn_count;
                }
                else
                {
                    free(sbuf);
                }
            }
            else if (sbuf)
            {
                free(sbuf);
            }
        }

        fclose(sfp);
    }

    *out = h;
    return 0;
}

void translate_stardict_close(struct StarDictHandle *h)
{
    if (!h)
        return;

    if (h->dict_fp)
        fclose(h->dict_fp);

    if (h->entries)
        free(h->entries);

    if (h->idx_buf)
        free(h->idx_buf);

    if (h->syns)
        free(h->syns);

    if (h->syn_buf)
        free(h->syn_buf);
    free(h);
}

static char *strip_markup_inplace(char *s)
{
    char *src = s;
    char *dst = s;
    int in_tag = 0;
    char tag[16];
    int tag_len = 0;

    if (!s)
        return NULL;

    while (*src)
    {
        if (in_tag)
        {
            if (*src == '>')
            {
                /* Tag finished. If it was a block-level tag, emit \n */
                tag[tag_len] = '\0';
                {
                    /* lowercase tag name for comparison */
                    int i;

                    for (i = 0; i < tag_len; i++)
                    {
                        if (tag[i] >= 'A' && tag[i] <= 'Z')
                            tag[i] = (char)(tag[i] + 'a' - 'A');
                    }
                }

                if (strcmp(tag, "br") == 0 || strcmp(tag, "br/") == 0 ||
                    strcmp(tag, "/p") == 0 || strcmp(tag, "p") == 0 ||
                    strcmp(tag, "/div") == 0 || strcmp(tag, "div") == 0 ||
                    strcmp(tag, "li") == 0 || strcmp(tag, "/li") == 0 ||
                    strcmp(tag, "/tr") == 0 || strcmp(tag, "tr") == 0)
                {
                    *dst++ = '\n';
                }

                in_tag = 0;
            }
            else if (tag_len < (int)sizeof(tag) - 1)
            {
                tag[tag_len++] = *src;
            }

            src++;
            continue;
        }

        if (*src == '<')
        {
            in_tag = 1;
            tag_len = 0;
            src++;
            continue;
        }

        if (*src == '&')
        {
/* Decodes common HTML entities (covers ~95% of bilingual dict markup) */
#define ENT(name, repl)                            \
    if (strncmp(src, name, sizeof(name) - 1) == 0) \
    {                                              \
        const char *r = repl;                      \
        while (*r)                                 \
            *dst++ = *r++;                         \
        src += sizeof(name) - 1;                   \
        continue;                                  \
    }
            ENT("&amp;", "&")
            ENT("&lt;", "<")
            ENT("&gt;", ">")
            ENT("&quot;", "\"")
            ENT("&apos;", "'")
            ENT("&nbsp;", " ")

            /* Spanish/French accented vowels - UTF-8 */
            ENT("&aacute;", "\xc3\xa1")
            ENT("&Aacute;", "\xc3\x81")
            ENT("&eacute;", "\xc3\xa9")
            ENT("&Eacute;", "\xc3\x89")
            ENT("&iacute;", "\xc3\xad")
            ENT("&Iacute;", "\xc3\x8d")
            ENT("&oacute;", "\xc3\xb3")
            ENT("&Oacute;", "\xc3\x93")
            ENT("&uacute;", "\xc3\xba")
            ENT("&Uacute;", "\xc3\x9a")
            ENT("&ntilde;", "\xc3\xb1")
            ENT("&Ntilde;", "\xc3\x91")
            ENT("&uuml;", "\xc3\xbc")
            ENT("&Uuml;", "\xc3\x9c")
            ENT("&iquest;", "\xc2\xbf")
            ENT("&iexcl;", "\xc2\xa1")

            /* German */
            ENT("&auml;", "\xc3\xa4")
            ENT("&ouml;", "\xc3\xb6")
            ENT("&Auml;", "\xc3\x84")
            ENT("&Ouml;", "\xc3\x96")
            ENT("&szlig;", "\xc3\x9f")

            /* Typography */
            ENT("&mdash;", "\xe2\x80\x94")
            ENT("&ndash;", "\xe2\x80\x93")
            ENT("&hellip;", "\xe2\x80\xa6")
            ENT("&rarr;", "\xe2\x86\x92")
            ENT("&larr;", "\xe2\x86\x90")
            ENT("&laquo;", "\xc2\xab")
            ENT("&raquo;", "\xc2\xbb")
            ENT("&copy;", "\xc2\xa9")
            ENT("&reg;", "\xc2\xae")
            ENT("&middot;", "\xc2\xb7")

            /* Numeric entities &#NNN; and &#xHH; - decode if ASCII range */
            if (src[1] == '#')
            {
                int base = 10;
                int sk = 2;
                long code = 0;

                if (src[2] == 'x' || src[2] == 'X')
                {
                    base = 16;
                    sk = 3;
                }

                while (src[sk] && src[sk] != ';')
                {
                    char c = src[sk];
                    int v;

                    if (c >= '0' && c <= '9')
                        v = c - '0';
                    else if (c >= 'a' && c <= 'f')
                        v = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F')
                        v = c - 'A' + 10;
                    else
                    {
                        code = -1;
                        break;
                    }

                    if (v >= base)
                    {
                        code = -1;
                        break;
                    }

                    code = code * base + v;
                    sk++;

                    if (sk > 12)
                    {
                        code = -1;
                        break;
                    }
                }

                if (code > 0 && code < 128 && src[sk] == ';')
                {
                    *dst++ = (char)code;
                    src += sk + 1;
                    continue;
                }

                if (code >= 128 && code <= 0x7FF && src[sk] == ';')
                {
                    *dst++ = (char)(0xC0 | (code >> 6));
                    *dst++ = (char)(0x80 | (code & 0x3F));

                    src += sk + 1;
                    continue;
                }

                if (code >= 0x800 && code <= 0xFFFF && src[sk] == ';')
                {
                    *dst++ = (char)(0xE0 | (code >> 12));
                    *dst++ = (char)(0x80 | ((code >> 6) & 0x3F));
                    *dst++ = (char)(0x80 | (code & 0x3F));
                    src += sk + 1;
                    continue;
                }
            }
#undef ENT
        }

        *dst++ = *src++;
    }

    *dst = '\0';
    return s;
}

/* Appends n bytes to buffer, reallocating if needed. Returns 1 on success */
static int append_n(char **out_buf, size_t *out_cap, size_t *out_len, const char *src, size_t n)
{
    if (*out_len + n + 1 > *out_cap)
    {
        size_t new_cap = *out_cap ? *out_cap * 2 : 256;
        char *neu = NULL;

        while (new_cap < *out_len + n + 1)
            new_cap *= 2;

        neu = (char *)realloc(*out_buf, new_cap);

        if (!neu)
            return 0;

        *out_buf = neu;
        *out_cap = new_cap;
    }

    memcpy(*out_buf + *out_len, src, n);

    *out_len += n;
    (*out_buf)[*out_len] = '\0';

    return 1;
}

/* Checks if type letter expects NUL-terminated UTF-8 text (lowercase) vs binary (uppercase) */
static int sd_type_is_text(int c)
{
    /* Lowercase = NUL-terminated text, uppercase = 32-bit-length-prefixed binary */
    return (c >= 'a' && c <= 'z');
}

/* Checks if type contains human-readable text content (vs binary data) */
static int sd_type_is_readable(int c)
{
    return (c == 'm' || c == 'l' || c == 'g' || c == 't' || c == 'y' ||
            c == 'h' || c == 'x' || c == 'k' || c == 'w' || c == 'n' ||
            c == 'r');
}

static int sd_type_has_markup(int c)
{
    return (c == 'h' || c == 'x' || c == 'g');
}

static char *decode_dict_entry(const char *raw, unsigned long raw_size, const char *sametypesequence)
{
    char *out = NULL;
    size_t out_cap = 0;
    size_t out_len = 0;
    const char *p = raw;
    const char *end = raw + raw_size;

    if (raw_size == 0)
    {
        out = (char *)malloc(1);

        if (out)
            out[0] = '\0';

        return out;
    }

    if (sametypesequence && sametypesequence[0])
    {
        size_t seq_len = strlen(sametypesequence);
        size_t i;
        const char *part_start = p;

        for (i = 0; i < seq_len; i++)
        {
            int type = (unsigned char)sametypesequence[i];
            int is_last = (i == seq_len - 1);

            if (sd_type_is_text(type))
            {
                size_t part_len;

                if (is_last)
                {
                    /* Last part: runs to the end of the entry */
                    part_len = (size_t)(end - part_start);
                }
                else
                {
                    /* Find next NUL */
                    const char *zero = (const char *)memchr(part_start, 0, (size_t)(end - part_start));

                    if (!zero)
                        part_len = (size_t)(end - part_start);
                    else
                        part_len = (size_t)(zero - part_start);
                }

                if (sd_type_is_readable(type) && part_len > 0)
                {
                    if (out_len > 0)
                        append_n(&out, &out_cap, &out_len, "\n\n", 2);

                    if (!append_n(&out, &out_cap, &out_len, part_start, part_len))
                        return out;

                    if (sd_type_has_markup(type))
                        strip_markup_inplace(out);
                }

                part_start += part_len;

                if (!is_last && part_start < end && *part_start == '\0')
                    part_start++;
            }
            else
            {
                /* Binary part: 32-bit length
                 + data. Skip silently */
                unsigned long len;
                if (part_start + 4 > end)
                    break;

                len = be32((const unsigned char *)part_start);

                part_start += 4 + len;

                if (part_start > end)
                    break;
            }
        }
    }
    else
    {
        /* No sametypesequence: each part has its own 1-byte type prefix. Multiple parts may chain end-to-end */
        while (p < end)
        {
            int type;
            size_t part_len = 0;

            type = (unsigned char)*p++;

            if (sd_type_is_text(type))
            {
                const char *zero = (const char *)memchr(p, 0, (size_t)(end - p));

                if (!zero)
                    part_len = (size_t)(end - p);
                else
                    part_len = (size_t)(zero - p);

                if (sd_type_is_readable(type) && part_len > 0)
                {
                    if (out_len > 0)
                        append_n(&out, &out_cap, &out_len, "\n\n", 2);

                    if (!append_n(&out, &out_cap, &out_len, p, part_len))
                        return out;

                    if (sd_type_has_markup(type))
                        strip_markup_inplace(out);
                }

                p += part_len;

                if (p < end && *p == '\0')
                    p++;
            }
            else
            {
                unsigned long len;

                if (p + 4 > end)
                    break;

                len = be32((const unsigned char *)p);
                p += 4 + len;

                if (p > end)
                    break;
            }
        }
    }

    if (!out)
    {
        out = (char *)malloc(1);

        if (out)
            out[0] = '\0';
    }

    return out;
}

static char *read_dict_entry(struct StarDictHandle *h, unsigned long off, unsigned long size)
{
    char *raw = NULL;
    char *decoded = NULL;

    if (size == 0 || size > SD_MAX_DICT_ENTRY_BYTES)
        return NULL;

    if (fseek(h->dict_fp, (long)off, SEEK_SET) != 0)
        return NULL;

    raw = (char *)malloc((size_t)size + 1);

    if (!raw)
        return NULL;

    if (fread(raw, 1, (size_t)size, h->dict_fp) != (size_t)size)
    {
        free(raw);
        return NULL;
    }

    raw[size] = '\0';

    decoded = decode_dict_entry(raw, size, h->sametypesequence[0] ? h->sametypesequence : NULL);
    free(raw);
    return decoded;
}

static int binary_search(struct StarDictHandle *h, const char *word)
{
    int lo = 0;
    int hi = h->entry_count - 1;
    int mid;
    int cmp;

    while (lo <= hi)
    {
        mid = lo + (hi - lo) / 2;
        cmp = sd_strcmp(h->entries[mid].word, word);

        if (cmp == 0)
            return mid;

        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    return -1;
}

/* Searches synonym table for word, returns index into main entries[] or -1 if not found */
static int synonym_search(struct StarDictHandle *h, const char *word)
{
    int lo = 0;
    int hi = h->syn_count - 1;
    int mid;
    int cmp;

    if (!h->syns || h->syn_count == 0)
        return -1;

    while (lo <= hi)
    {
        mid = lo + (hi - lo) / 2;
        cmp = sd_strcmp(h->syns[mid].word, word);

        if (cmp == 0)
        {
            int idx = h->syns[mid].target_idx;

            if (idx >= 0 && idx < h->entry_count)
                return idx;

            return -1;
        }

        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    return -1;
}

static char *lookup_single(struct StarDictHandle *h, const char *word, char *err, int err_size)
{
    int idx;
    int ci;
    char *result = NULL;
    size_t vl;
    SdCacheEntry *e = NULL;

    if (err && err_size > 0)
        err[0] = '\0';

    if (!word || !word[0])
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "empty word");

        return NULL;
    }

    ci = cache_find(h, word);
    if (ci != -1)
    {
        e = &h->cache[ci];
        vl = strlen(e->val);
        result = (char *)malloc(vl + 1);

        if (!result)
        {
            if (err && err_size > 0)
                snprintf(err, (size_t)err_size, "out of memory");

            return NULL;
        }

        memcpy(result, e->val, vl + 1);

        cache_unlink(h, ci);
        cache_push_front(h, ci);

        return result;
    }

    idx = binary_search(h, word);
    if (idx < 0)
    {
        /* Fallback to synonyms table for inflections (running->run, books->book) */
        idx = synonym_search(h, word);

        if (idx < 0)
        {
            if (err && err_size > 0)
                snprintf(err, (size_t)err_size, "not found");

            return NULL;
        }
    }

    result = read_dict_entry(h, h->entries[idx].offset, h->entries[idx].size);

    if (!result)
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "failed to read .dict entry");

        return NULL;
    }

    vl = strlen(result);

    if (strlen(word) < SD_CACHE_KEY_MAX && vl < SD_CACHE_VAL_MAX)
    {
        int slot = cache_acquire(h);

        e = &h->cache[slot];
        strncpy(e->key, word, sizeof(e->key) - 1);
        e->key[sizeof(e->key) - 1] = '\0';

        memcpy(e->val, result, vl + 1);
        cache_push_front(h, slot);
    }

    return result;
}

/* Appends text to growable buffer, returns new buffer or NULL on OOM */
static char *str_append(char *buf, size_t *cap, size_t *len, const char *text, int text_len)
{
    size_t add = (text_len < 0) ? strlen(text) : (size_t)text_len;

    if (*len + add + 1 > *cap)
    {
        size_t new_cap = *cap ? *cap * 2 : 256;
        char *neu = NULL;

        while (new_cap < *len + add + 1)
            new_cap *= 2;

        neu = (char *)realloc(buf, new_cap);

        if (!neu)
        {
            free(buf);
            return NULL;
        }

        buf = neu;
        *cap = new_cap;
    }

    memcpy(buf + *len, text, add);

    *len += add;
    buf[*len] = '\0';

    return buf;
}

char *translate_stardict_lookup(struct StarDictHandle *h, const char *src, char *err, int err_size)
{
    const char *p = NULL;
    const char *word_start = NULL;
    char word_buf[SD_CACHE_KEY_MAX];
    char tmp_err[64];
    char *out_buf = NULL;
    size_t out_cap = 0;
    size_t out_len = 0;
    char *single = NULL;
    int word_idx;
    int word_count;
    size_t wl;
    int got_any;
    int has_ws;
    char *whole = NULL;
    int lead;

    if (err && err_size > 0)
        err[0] = '\0';

    if (!h || !src)
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "invalid args");

        return NULL;
    }

    /* Fast path: source has no internal whitespace -> single lookup */
    has_ws = 0;

    for (p = src; *p; p++)
    {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        {
            has_ws = 1;
            break;
        }
    }

    if (!has_ws)
        return lookup_single(h, src, err, err_size);

    /* Phrase-first: try whole source as single key before splitting into per-word lookups */

    tmp_err[0] = '\0';

    whole = lookup_single(h, src, tmp_err, sizeof(tmp_err));

    if (whole)
        return whole;

    got_any = 0;
    word_count = 0;
    p = src;

    while (*p && word_count < SD_MAX_WORDS_PER_LOOKUP)
    {
        /* skip leading whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;

        if (!*p)
            break;

        word_start = p;

        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            p++;

        wl = (size_t)(p - word_start);

        if (wl == 0)
            continue;

        if (wl >= sizeof(word_buf))
            wl = sizeof(word_buf) - 1;

        memcpy(word_buf, word_start, wl);

        word_buf[wl] = '\0';

        /* Strip trailing punctuation common in selections: . , ; : ! ? but only if it leaves a non-empty word */
        while (wl > 1)
        {
            char c = word_buf[wl - 1];

            if (c == '.' || c == ',' || c == ';' || c == ':' ||
                c == '!' || c == '?' || c == ')' || c == ']' ||
                c == '"' || c == '\'')
                word_buf[--wl] = '\0';
            else
                break;
        }

        /* Leading punctuation */
        lead = 0;

        while (word_buf[lead] == '(' || word_buf[lead] == '[' ||
               word_buf[lead] == '"' || word_buf[lead] == '\'')
            lead++;

        if (lead > 0)
        {
            memmove(word_buf, word_buf + lead, wl - lead + 1);
            wl -= lead;
        }

        if (!word_buf[0])
            continue;

        if (word_idx = 0, word_count > 0)
        {
            out_buf = str_append(out_buf, &out_cap, &out_len, "\n", 1);

            if (!out_buf)
            {
                if (err && err_size > 0)
                    snprintf(err, (size_t)err_size, "out of memory");

                return NULL;
            }
        }

        out_buf = str_append(out_buf, &out_cap, &out_len, word_buf, (int)wl);

        if (!out_buf)
        {
            if (err && err_size > 0)
                snprintf(err, (size_t)err_size, "out of memory");

            return NULL;
        }

        out_buf = str_append(out_buf, &out_cap, &out_len, ": ", 2);

        if (!out_buf)
        {
            if (err && err_size > 0)
                snprintf(err, (size_t)err_size, "out of memory");

            return NULL;
        }

        tmp_err[0] = '\0';
        single = lookup_single(h, word_buf, tmp_err, sizeof(tmp_err));

        if (single)
        {
            /* Replaces internal newlines with " | " to keep one line per word in joined output */
            char *s = NULL;
            const char *prev = single;

            for (s = single; *s; s++)
            {
                if (*s == '\n')
                {
                    *s = '\0';
                    out_buf = str_append(out_buf, &out_cap, &out_len, prev, -1);

                    if (!out_buf)
                    {
                        free(single);

                        if (err && err_size > 0)
                            snprintf(err, (size_t)err_size, "out of memory");

                        return NULL;
                    }

                    out_buf = str_append(out_buf, &out_cap, &out_len, " | ", 3);

                    if (!out_buf)
                    {
                        free(single);

                        if (err && err_size > 0)
                            snprintf(err, (size_t)err_size, "out of memory");

                        return NULL;
                    }

                    prev = s + 1;
                }
            }

            if (*prev)
            {
                out_buf = str_append(out_buf, &out_cap, &out_len, prev, -1);

                if (!out_buf)
                {
                    free(single);

                    if (err && err_size > 0)
                        snprintf(err, (size_t)err_size, "out of memory");

                    return NULL;
                }
            }

            free(single);

            got_any = 1;
        }
        else
        {
            out_buf = str_append(out_buf, &out_cap, &out_len, "[not found]", 11);

            if (!out_buf)
            {
                if (err && err_size > 0)
                    snprintf(err, (size_t)err_size, "out of memory");

                return NULL;
            }
        }

        word_count++;
    }

    if (!out_buf)
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "no words to look up");

        return NULL;
    }

    /* If no word resolved, treat the whole call as a failure so the caller can pass the message through to the user */
    if (!got_any)
    {
        if (err && err_size > 0)
            snprintf(err, (size_t)err_size, "none of the words found");

        free(out_buf);
        return NULL;
    }

    return out_buf;
}

void translate_stardict_cache_clear(struct StarDictHandle *h)
{
    if (!h)
        return;

    cache_init(h);
}

const char *translate_stardict_bookname(struct StarDictHandle *h)
{
    if (!h)
        return "";

    return h->bookname;
}
