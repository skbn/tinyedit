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

#include "spell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef PLATFORM_AMIGA
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#endif

#include "../core/utf8.h"

typedef unsigned long ms_cp; /* UTF-8 codepoint */

struct ms_aff_rule
{
    char *strip; /* "" if "0" */
    char *add;   /* suffix/prefix to apply */
    char *cond;  /* simplified regex condition */
    unsigned short strip_len;
    unsigned short add_len;
};

struct ms_aff_group
{
    ms_cp flag;
    char cross_product; /* 'Y' or 'N' */
    char kind;          /* 'S' (suffix) or 'P' (prefix) */
    struct ms_aff_rule *rules;
    int n_rules;
};

struct ms_hentry
{
    char *word;   /* word (utf-8), NULL if slot free */
    ms_cp *flags; /* array of associated flags */
    int n_flags;
};

struct ms_rep
{
    char *from;
    char *to;
};

struct ms_check_cache
{
    char key[SPELL_MAX_WORD];
    unsigned char result; /* 0 = bad, 1 = good */
    short prev, next;
};

struct spell
{
    char encoding[24];
    int flag_type; /* 0=ASCII, 1=UTF-8, 2=NUM, 3=LONG */
    int flag_utf8; /* 1 if FLAG UTF-8 (deprecated, use flag_type) */

    /* hash table */
    struct ms_hentry *htab;
    int htab_size; /* always power of 2 */
    int htab_count;

    /* affix groups */
    struct ms_aff_group *groups;
    int n_groups;

    /* REP table */
    struct ms_rep *reps;
    int n_reps;

    /* MAP table (each entry is a list of equivalent UTF-8 strings) */
    char ***maps; /* maps[i] = array of strings terminated by NULL */
    int n_maps;

    /* TRY */
    char *try_chars; /* UTF-8 string with characters to try */

    /* check cache */
    struct ms_check_cache cache[SPELL_CACHE_N];
    short head, tail;
    short cache_count;

    /* returned suggestions buffer (owned by spell) */
    char *sugg_buf[SPELL_MAX_SUGGS];
    int sugg_n;
};

static char *ms_strdup(const char *s)
{
    size_t n;
    char *r = NULL;

    if (!s)
        return NULL;

    n = strlen(s) + 1;
    r = (char *)malloc(n);

    if (r)
        memcpy(r, s, n);

    return r;
}

static int ms_readline(FILE *fp, char *buf, size_t sz)
{
    int c = 0;
    int n = 0;

    if (sz == 0)
        return -1;

    for (;;)
    {
        c = fgetc(fp);

        if (c == EOF)
        {
            if (n == 0)
                return -1;

            break;
        }

        if (c == '\n')
            break;

        if (c == '\r')
            continue;

        if ((size_t)n < sz - 1)
            buf[n++] = (char)c;
    }

    buf[n] = '\0';

    return n;
}

static void ms_rstrip(char *s)
{
    int n = (int)strlen(s);

    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
}

/* decode UTF-8 character at p, returns byte length (1-4) */
static int utf8_decode(const char *p, ms_cp *cp)
{
    unsigned char c = (unsigned char)*p;

    if (c < 0x80)
    {
        *cp = c;
        return 1;
    }

    if ((c & 0xE0) == 0xC0)
    {
        *cp = ((ms_cp)(c & 0x1F) << 6) | ((unsigned char)p[1] & 0x3F);
        return 2;
    }

    if ((c & 0xF0) == 0xE0)
    {
        *cp = ((ms_cp)(c & 0x0F) << 12) | (((unsigned char)p[1] & 0x3F) << 6) | ((unsigned char)p[2] & 0x3F);
        return 3;
    }

    if ((c & 0xF8) == 0xF0)
    {
        *cp = ((ms_cp)(c & 0x07) << 18) | (((unsigned char)p[1] & 0x3F) << 12) | (((unsigned char)p[2] & 0x3F) << 6) | ((unsigned char)p[3] & 0x3F);
        return 4;
    }

    *cp = c;

    return 1; /* fallback */
}

/* parse flags string: 0=ASCII, 1=UTF-8, 2=NUM, 3=LONG */
static ms_cp *parse_flags(const char *s, int flag_type, int *n_out)
{
    int count = 0;
    int cap = 8;
    ms_cp *result = NULL;
    const char *p = NULL;

    if (!s || !*s)
    {
        *n_out = 0;
        return NULL;
    }

    result = (ms_cp *)malloc((size_t)cap * sizeof(ms_cp));

    if (!result)
    {
        *n_out = 0;
        return NULL;
    }

    p = s;

    if (flag_type == 2 || flag_type == 3)
    {
        /* FLAG NUM or LONG: numbers separated by commas */
        while (*p)
        {
            unsigned long num = 0;
            char *end = NULL;

            num = strtoul(p, &end, 10);

            if (end == p)
                break; /* not a number */

            if (count == cap)
            {
                ms_cp *ng = NULL;

                cap *= 2;
                ng = (ms_cp *)realloc(result, (size_t)cap * sizeof(ms_cp));

                if (!ng)
                    break;

                result = ng;
            }

            result[count++] = (ms_cp)num;
            p = end;

            if (*p == ',')
                p++;
            else
                break;
        }
    }
    else
    {
        /* FLAG ASCII or UTF-8: characters/codepoints */
        while (*p)
        {
            ms_cp cp;
            int adv;

            if (flag_type == 1)
                adv = utf8_decode(p, &cp);
            else
            {
                cp = (unsigned char)*p;
                adv = 1;
            }

            if (count == cap)
            {
                ms_cp *ng = NULL;

                cap *= 2;
                ng = (ms_cp *)realloc(result, (size_t)cap * sizeof(ms_cp));

                if (!ng)
                    break;

                result = ng;
            }

            result[count++] = cp;
            p += adv;
        }
    }

    *n_out = count;

    return result;
}

static int has_flag(const ms_cp *flags, int n, ms_cp f)
{
    int i;

    for (i = 0; i < n; i++)
    {
        if (flags[i] == f)
            return 1;
    }

    return 0;
}

/* FNV-1a hash over string bytes */
static unsigned long ms_hash(const char *s)
{
    unsigned long h = 2166136261UL;

    while (*s)
    {
        h ^= (unsigned char)*s++;
        h *= 16777619UL;
    }

    return h;
}

static int htab_init(struct spell *m, int initial_pow2)
{
    int sz = 1;

    while (sz < initial_pow2)
        sz <<= 1;

    m->htab = (struct ms_hentry *)calloc((size_t)sz, sizeof(struct ms_hentry));

    if (!m->htab)
        return -1;

    m->htab_size = sz;
    m->htab_count = 0;

    return 0;
}

static void htab_insert_into(struct ms_hentry *tab, int sz, char *word, ms_cp *flags, int n_flags)
{
    unsigned long h = ms_hash(word);
    int mask = sz - 1;
    int i = (int)(h & (unsigned long)mask);

    while (tab[i].word)
    {
        if (strcmp(tab[i].word, word) == 0)
        {
            /* dup: merge flags (accumulate new ones with existing) */
            int k;

            if (n_flags > 0 && flags)
            {
                int new_count = tab[i].n_flags + n_flags;

                unsigned long *merged = (unsigned long *)realloc(tab[i].flags, (size_t)new_count * sizeof(unsigned long));

                if (merged)
                {
                    tab[i].flags = merged;

                    for (k = 0; k < n_flags; k++)
                    {
                        if (!has_flag(tab[i].flags, tab[i].n_flags, flags[k]))
                        {
                            tab[i].flags[tab[i].n_flags++] = flags[k];
                        }
                    }
                }
            }

            free(word);
            free(flags);

            return;
        }

        i = (i + 1) & mask;
    }

    tab[i].word = word;
    tab[i].flags = flags;
    tab[i].n_flags = n_flags;
}

static int htab_resize(struct spell *m, int new_sz)
{
    struct ms_hentry *nt = (struct ms_hentry *)calloc((size_t)new_sz, sizeof(struct ms_hentry));
    int i;

    if (!nt)
        return -1;

    for (i = 0; i < m->htab_size; i++)
    {
        if (m->htab[i].word)
            htab_insert_into(nt, new_sz, m->htab[i].word, m->htab[i].flags, m->htab[i].n_flags);
    }

    free(m->htab);

    m->htab = nt;
    m->htab_size = new_sz;

    return 0;
}

static void htab_insert(struct spell *m, const char *word, const char *flags_str)
{
    char *w = NULL;
    ms_cp *flags = NULL;
    int n_flags = 0;

    /* resize if over 70% */
    if (m->htab_count * 10 >= m->htab_size * 7)
    {
        if (htab_resize(m, m->htab_size * 2) != 0)
            return;
    }

    w = ms_strdup(word);
    flags = parse_flags(flags_str, m->flag_type, &n_flags);

    if (!w)
    {
        free(flags);
        return;
    }

    htab_insert_into(m->htab, m->htab_size, w, flags, n_flags);
    m->htab_count++;
}

static struct ms_hentry *htab_find(struct spell *m, const char *word)
{
    unsigned long h = ms_hash(word);
    int mask = m->htab_size - 1;
    int i = (int)(h & (unsigned long)mask);

    while (m->htab[i].word)
    {
        if (strcmp(m->htab[i].word, word) == 0)
            return &m->htab[i];

        i = (i + 1) & mask;
    }

    return NULL;
}

static void check_cache_init(struct spell *m)
{
    int i;

    m->head = -1;
    m->tail = -1;
    m->cache_count = 0;

    for (i = 0; i < SPELL_CACHE_N; i++)
    {
        m->cache[i].key[0] = '\0';
        m->cache[i].prev = -1;
        m->cache[i].next = -1;
    }
}

static void check_cache_unlink(struct spell *m, int idx)
{
    struct ms_check_cache *e = &m->cache[idx];

    if (e->prev != -1)
        m->cache[e->prev].next = e->next;
    else
        m->head = e->next;

    if (e->next != -1)
        m->cache[e->next].prev = e->prev;
    else
        m->tail = e->prev;

    e->prev = -1;
    e->next = -1;
}

static void check_cache_push_front(struct spell *m, int idx)
{
    struct ms_check_cache *e = &m->cache[idx];

    e->prev = -1;
    e->next = m->head;

    if (m->head != -1)
        m->cache[m->head].prev = (short)idx;

    m->head = (short)idx;

    if (m->tail == -1)
        m->tail = (short)idx;
}

static int check_cache_find(struct spell *m, const char *word)
{
    int i;

    for (i = m->head; i != -1; i = m->cache[i].next)
    {
        if (strcmp(m->cache[i].key, word) == 0)
            return i;
    }

    return -1;
}

static int check_cache_acquire(struct spell *m)
{
    int i;

    if (m->cache_count < SPELL_CACHE_N)
    {
        for (i = 0; i < SPELL_CACHE_N; i++)
        {
            if (m->cache[i].key[0] == '\0')
            {
                m->cache_count++;
                return i;
            }
        }
    }

    i = m->tail;
    check_cache_unlink(m, i);

    return i;
}

static void check_cache_put(struct spell *m, const char *word, int res)
{
    int idx;
    size_t wlen = strlen(word);

    if (wlen == 0 || wlen >= SPELL_MAX_WORD)
        return;

    idx = check_cache_acquire(m);

    memcpy(m->cache[idx].key, word, wlen + 1);

    m->cache[idx].result = (unsigned char)(res ? 1 : 0);

    check_cache_push_front(m, idx);
}

void spell_cache_clear(struct spell *s)
{
    if (s)
        check_cache_init(s);
}

const char *spell_get_encoding(struct spell *s)
{
    return s ? s->encoding : NULL;
}

/* parse line "SFX X N" or "SFX X 0 ado [^aeiou]" */
static int parse_aff_header(const char *line, int flag_type, ms_cp *flag_out, char *cp_out, int *n_out)
{
    /* line starts after "SFX " or "PFX " */
    char tmp[64];
    const char *p = line;
    ms_cp f;
    int i = 0;

    /* skip spaces */
    while (*p == ' ')
        p++;

    /* flag - according to flag_type */
    if (!*p)
        return -1;

    if (flag_type == 2 || flag_type == 3)
    {
        /* FLAG NUM or LONG: number */
        char *end = NULL;

        f = (ms_cp)strtoul(p, &end, 10);

        if (end == p)
            return -1;

        p = end;
    }
    else
    {
        /* FLAG ASCII or UTF-8: character/codepoint */
        int adv = utf8_decode(p, &f);
        p += adv;
    }

    *flag_out = f;

    /* skip spaces */
    while (*p == ' ')
        p++;

    if (!*p)
        return -1;

    /* cross-product Y/N */
    *cp_out = *p++;

    while (*p == ' ')
        p++;

    /* n_rules */
    while (*p && *p != ' ' && i < (int)sizeof(tmp) - 1)
        tmp[i++] = *p++;

    tmp[i] = '\0';
    *n_out = atoi(tmp);

    return 0;
}

/* parse "SFX X strip add cond [morph]" => strip, add, cond */
static int parse_aff_rule(const char *line, ms_cp expected_flag, int flag_type, char **strip, char **add, char **cond)
{
    const char *p = line;
    char tok[256];
    int i;
    ms_cp f;

    /* skip spaces */
    while (*p == ' ')
        p++;

    if (!*p)
        return -1;

    /* flag - according to flag_type */
    if (flag_type == 2 || flag_type == 3)
    {
        /* FLAG NUM or LONG: number */
        char *end = NULL;

        f = (ms_cp)strtoul(p, &end, 10);

        if (end == p)
            return -1;

        p = end;
    }
    else
    {
        /* FLAG ASCII or UTF-8: character/codepoint */
        int adv = utf8_decode(p, &f);

        p += adv;
    }

    if (f != expected_flag)
        return -1;

    while (*p == ' ')
        p++;

    /* strip */
    i = 0;
    while (*p && *p != ' ' && i < (int)sizeof(tok) - 1)
        tok[i++] = *p++;

    tok[i] = '\0';

    if (strcmp(tok, "0") == 0)
        *strip = ms_strdup("");
    else
        *strip = ms_strdup(tok);

    while (*p == ' ')
        p++;

    /* add (may include "/flags" - we ignore that part for our model) */
    i = 0;

    while (*p && *p != ' ' && *p != '/' && i < (int)sizeof(tok) - 1)
        tok[i++] = *p++;

    tok[i] = '\0';

    if (strcmp(tok, "0") == 0)
        *add = ms_strdup("");
    else
        *add = ms_strdup(tok);

    /* skip /flags part if present */
    while (*p && *p != ' ')
        p++;

    while (*p == ' ')
        p++;

    /* cond */
    i = 0;

    while (*p && *p != ' ' && *p != '\t' && i < (int)sizeof(tok) - 1)
        tok[i++] = *p++;

    tok[i] = '\0';

    if (tok[0])
        *cond = ms_strdup(tok);
    else
        *cond = ms_strdup(".");

    return 0;
}

/* parse TRY abc... line */
static void parse_try(struct spell *m, const char *rest)
{
    free(m->try_chars);
    m->try_chars = ms_strdup(rest);
}

/* parse "REP N" header (does nothing, dynamic capacity) */
/* parse "REP from to" line */
static void parse_rep(struct spell *m, const char *line)
{
    /* "REP " already removed */
    const char *sp = strchr(line, ' ');
    char from[128];
    size_t flen;
    struct ms_rep *ng = NULL;

    if (!sp)
        return;

    flen = (size_t)(sp - line);

    if (flen >= sizeof(from))
        flen = sizeof(from) - 1;

    memcpy(from, line, flen);
    from[flen] = '\0';

    /* alloc/append */
    ng = (struct ms_rep *)realloc(m->reps, (size_t)(m->n_reps + 1) * sizeof(*m->reps));

    if (!ng)
        return;

    m->reps = ng;
    m->reps[m->n_reps].from = ms_strdup(from);
    m->reps[m->n_reps].to = ms_strdup(sp + 1);
    m->n_reps++;
}

static void parse_map(struct spell *m, const char *line)
{
    char ***ng = (char ***)realloc(m->maps, (size_t)(m->n_maps + 1) * sizeof(char **));
    char **arr = NULL;
    const char *p = line;
    int cap = 8;
    int n = 0;

    if (!ng)
        return;

    m->maps = ng;

    arr = (char **)malloc((size_t)cap * sizeof(char *));

    if (!arr)
        return;

    while (*p)
    {
        char buf[8];
        ms_cp cp;
        int adv = utf8_decode(p, &cp);

        if (n == cap - 1)
        {
            char **g = (char **)realloc(arr, (size_t)(cap * 2) * sizeof(char *));

            if (!g)
                break;

            arr = g;
            cap *= 2;
        }

        memcpy(buf, p, (size_t)adv);

        buf[adv] = '\0';
        arr[n++] = ms_strdup(buf);
        p += adv;
    }

    arr[n] = NULL;
    m->maps[m->n_maps++] = arr;
}

static int load_aff(struct spell *m, const char *path)
{
    FILE *fp = fopen(path, "rb");
    char line[1024];

    if (!fp)
        return -1;

    /* defaults */
    strcpy(m->encoding, "UTF-8");

    m->flag_utf8 = 0;

    while (ms_readline(fp, line, sizeof(line)) >= 0)
    {
        ms_rstrip(line);

        if (line[0] == '\0' || line[0] == '#')
            continue;

        if (strncmp(line, "SET ", 4) == 0)
        {
            strncpy(m->encoding, line + 4, sizeof(m->encoding) - 1);
            m->encoding[sizeof(m->encoding) - 1] = '\0';
            continue;
        }

        if (strcmp(line, "FLAG UTF-8") == 0)
        {
            m->flag_type = 1;
            m->flag_utf8 = 1;
            continue;
        }

        if (strcmp(line, "FLAG NUM") == 0)
        {
            m->flag_type = 2;
            m->flag_utf8 = 0;
            continue;
        }

        if (strcmp(line, "FLAG LONG") == 0)
        {
            m->flag_type = 3;
            m->flag_utf8 = 0;
            continue;
        }

        if (strncmp(line, "TRY ", 4) == 0)
        {
            parse_try(m, line + 4);
            continue;
        }

        if (strncmp(line, "REP ", 4) == 0)
        {
            /* REP N or REP from to */
            const char *rest = line + 4;
            const char *sp = strchr(rest, ' ');

            if (!sp)
                continue; /* "REP 20" header */

            parse_rep(m, rest);
            continue;
        }

        if (strncmp(line, "MAP ", 4) == 0)
        {
            const char *rest = line + 4;

            /* "MAP N" header se ignora; "MAP letras" es entrada */
            if (rest[0] >= '0' && rest[0] <= '9')
            {
                int n = 0;
                const char *p = rest;

                while (*p && *p != ' ')
                {
                    if (*p < '0' || *p > '9')
                    {
                        n = -1;
                        break;
                    }

                    p++;
                }

                if (n != -1)
                    continue; /* header */
            }

            parse_map(m, rest);
            continue;
        }

        if (strncmp(line, "SFX ", 4) == 0 || strncmp(line, "PFX ", 4) == 0)
        {
            char kind = line[0];
            ms_cp flag;
            char cp_flag;
            int n_rules;
            struct ms_aff_group *ng = NULL;
            struct ms_aff_group *g = NULL;
            int r;

            if (parse_aff_header(line + 4, m->flag_type, &flag, &cp_flag, &n_rules) != 0)
                continue;

            if (n_rules == 0)
                continue; /* no associated rules */

            /* alloc group */
            ng = (struct ms_aff_group *)realloc(m->groups, (size_t)(m->n_groups + 1) * sizeof(*m->groups));

            if (!ng)
                continue;

            m->groups = ng;
            g = &m->groups[m->n_groups++];
            g->flag = flag;
            g->cross_product = cp_flag;
            g->kind = kind;
            g->rules = (struct ms_aff_rule *)calloc((size_t)n_rules, sizeof(*g->rules));
            g->n_rules = 0;

            if (!g->rules)
            {
                m->n_groups--;
                continue;
            }

            for (r = 0; r < n_rules; r++)
            {
                char *strip = NULL;
                char *add = NULL;
                char *cond = NULL;

                if (ms_readline(fp, line, sizeof(line)) < 0)
                    break;

                ms_rstrip(line);

                if (line[0] == '\0' || line[0] == '#')
                {
                    r--;
                    continue;
                }

                /* line: "SFX/PFX X strip add cond" */
                if (strncmp(line, "SFX ", 4) != 0 && strncmp(line, "PFX ", 4) != 0)
                {
                    r--;
                    continue;
                }

                if (parse_aff_rule(line + 4, flag, m->flag_type, &strip, &add, &cond) == 0)
                {
                    g->rules[g->n_rules].strip = strip;
                    g->rules[g->n_rules].add = add;
                    g->rules[g->n_rules].cond = cond;
                    g->rules[g->n_rules].strip_len = strip ? (unsigned short)strlen(strip) : 0;
                    g->rules[g->n_rules].add_len = add ? (unsigned short)strlen(add) : 0;
                    g->n_rules++;
                }
                else
                {
                    free(strip);
                    free(add);
                    free(cond);
                }
            }

            continue;
        }
    }

    fclose(fp);
    return 0;
}

static int load_dic(struct spell *m, const char *path)
{
    FILE *fp = fopen(path, "rb");
    char line[1024];
    char utf8_word[1024];
    int n_expected = 0;

    if (!fp)
        return -1;

    /* line 1: count */
    if (ms_readline(fp, line, sizeof(line)) < 0)
    {
        fclose(fp);
        return -1;
    }

    n_expected = atoi(line);

    if (n_expected <= 0)
        n_expected = 1024;

    if (htab_init(m, n_expected * 2) != 0)
    {
        fclose(fp);
        return -1;
    }

    while (ms_readline(fp, line, sizeof(line)) >= 0)
    {
        char *slash = NULL;
        char *tab = NULL;
        char *word_to_insert = line;

        if (line[0] == '\0' || line[0] == '#')
            continue;

        /* keep only "word" + "flags" */
        /* trim morphology after tab/space */
        tab = line;

        while (*tab && *tab != '\t')
            tab++;

        *tab = '\0';

        /* Convert from dictionary encoding to internal UTF-8 if needed */
        if (strcasecmp(m->encoding, "UTF-8") != 0 && strcasecmp(m->encoding, "UTF8") != 0)
        {
            int converted = charset_to_utf8(m->encoding, line, (int)strlen(line), utf8_word, sizeof(utf8_word));

            if (converted > 0)
                word_to_insert = utf8_word;
        }

        slash = strchr(word_to_insert, '/');

        if (slash)
        {
            *slash = '\0';
            htab_insert(m, word_to_insert, slash + 1);
        }
        else
        {
            htab_insert(m, word_to_insert, "");
        }
    }

    fclose(fp);
    return 0;
}

static int cond_match(const char *cond, const char *word, int is_suffix)
{
    /* Calculate cond length (in "items": each item is '.', [..], or a UTF-8 char) */
    /* To simplify: we count items and then match */
    int items_count = 0;
    const char *p = NULL;
    int wlen = (int)strlen(word);
    int wpos;

    /* count items */
    p = cond;

    while (*p)
    {
        if (*p == '[')
        {
            const char *e = strchr(p, ']');

            if (!e)
                return 0;

            p = e + 1;
        }
        else
        {
            ms_cp cp;
            p += utf8_decode(p, &cp);
        }

        items_count++;
    }

    if (items_count == 0)
        return 1; /* "." or empty => match */

    if (items_count > wlen)
        return 0; /* requires more chars than available */

    /* Start position in word */
    if (is_suffix)
    {
        /* count bytes from end equivalent to 'items_count' UTF-8 chars */
        int bytes = 0;
        int chars = 0;
        int i;

        for (i = wlen - 1; i >= 0 && chars < items_count; i--)
        {
            bytes++;

            if (((unsigned char)word[i] & 0xC0) != 0x80)
                chars++;
        }

        if (chars < items_count)
            return 0;

        wpos = wlen - bytes;
    }
    else
    {
        wpos = 0;
    }

    /* iterate item by item */
    p = cond;

    while (*p)
    {
        const char *q = NULL;

        if (wpos >= wlen)
            return 0;

        if (*p == '[')
        {
            int negate = 0;
            const char *e = NULL;
            ms_cp wcp;
            int wadv;
            int found = 0;

            p++;

            if (*p == '^')
            {
                negate = 1;
                p++;
            }

            e = strchr(p, ']');

            if (!e)
                return 0;

            wadv = utf8_decode(word + wpos, &wcp);
            q = p;

            while (q < e)
            {
                ms_cp scp;
                int sadv = utf8_decode(q, &scp);

                if (scp == wcp)
                {
                    found = 1;
                    break;
                }

                q += sadv;
            }

            if (negate)
                found = !found;

            if (!found)
                return 0;

            p = e + 1;
            wpos += wadv;
        }
        else if (*p == '.')
        {
            ms_cp wcp;
            int wadv = utf8_decode(word + wpos, &wcp);

            p++;
            wpos += wadv;
        }
        else
        {
            /* literal */
            ms_cp ccp, wcp;
            int cadv = utf8_decode(p, &ccp);
            int wadv = utf8_decode(word + wpos, &wcp);

            if (ccp != wcp)
                return 0;

            p += cadv;
            wpos += wadv;
        }
    }

    return 1;
}

/* Apply SFX rule in reverse: remove 'add' from end, put 'strip' */
static int sfx_unapply(const struct ms_aff_rule *r, const char *word, char *out, size_t outsz)
{
    int wlen = (int)strlen(word);
    int alen = r->add_len;
    int slen = r->strip_len;
    int base_len;

    if (alen == 0 || wlen < alen)
        return 0;

    /* must end with 'add' */
    if (memcmp(word + wlen - alen, r->add, (size_t)alen) != 0)
        return 0;

    base_len = wlen - alen + slen;

    if ((size_t)base_len + 1 > outsz)
        return 0;

    memcpy(out, word, (size_t)(wlen - alen));

    if (slen)
        memcpy(out + (wlen - alen), r->strip, (size_t)slen);

    out[base_len] = '\0';

    /* Condition evaluated on BASE word (with strip applied) */
    if (!cond_match(r->cond, out, 1))
        return 0;

    return 1;
}

/* Apply PFX rule in reverse */
static int pfx_unapply(const struct ms_aff_rule *r, const char *word, char *out, size_t outsz)
{
    int wlen = (int)strlen(word);
    int alen = r->add_len;
    int slen = r->strip_len;
    int base_len;

    if (alen == 0 || wlen < alen)
        return 0;

    if (memcmp(word, r->add, (size_t)alen) != 0)
        return 0;

    base_len = wlen - alen + slen;

    if ((size_t)base_len + 1 > outsz)
        return 0;

    if (slen)
        memcpy(out, r->strip, (size_t)slen);

    memcpy(out + slen, word + alen, (size_t)(wlen - alen));
    out[base_len] = '\0';

    if (!cond_match(r->cond, out, 0))
        return 0;

    return 1;
}

/* Check if 'word' is valid applying SFX/PFX rules in reverse */
static int check_with_affixes(struct spell *m, const char *word)
{
    char base[SPELL_MAX_WORD * 2];
    int g, r;
    struct ms_hentry *he;

    /* try removing a suffix */
    for (g = 0; g < m->n_groups; g++)
    {
        struct ms_aff_group *grp = &m->groups[g];

        if (grp->kind != 'S')
            continue;

        for (r = 0; r < grp->n_rules; r++)
        {
            if (sfx_unapply(&grp->rules[r], word, base, sizeof(base)))
            {
                he = htab_find(m, base);

                if (he && has_flag(he->flags, he->n_flags, grp->flag))
                    return 1;
            }
        }
    }

    /* try removing a prefix */
    for (g = 0; g < m->n_groups; g++)
    {
        struct ms_aff_group *grp = &m->groups[g];

        if (grp->kind != 'P')
            continue;

        for (r = 0; r < grp->n_rules; r++)
        {
            if (pfx_unapply(&grp->rules[r], word, base, sizeof(base)))
            {
                he = htab_find(m, base);

                if (he && has_flag(he->flags, he->n_flags, grp->flag))
                    return 1;
            }
        }
    }

    /* try removing prefix + suffix (cross product) */
    for (g = 0; g < m->n_groups; g++)
    {
        struct ms_aff_group *gs = &m->groups[g];
        int g2;
        int r2;
        int r1;

        if (gs->kind != 'S' || gs->cross_product != 'Y')
            continue;

        for (r1 = 0; r1 < gs->n_rules; r1++)
        {
            char mid[SPELL_MAX_WORD * 2];

            if (!sfx_unapply(&gs->rules[r1], word, mid, sizeof(mid)))
                continue;

            for (g2 = 0; g2 < m->n_groups; g2++)
            {
                struct ms_aff_group *gp = &m->groups[g2];

                if (gp->kind != 'P' || gp->cross_product != 'Y')
                    continue;

                for (r2 = 0; r2 < gp->n_rules; r2++)
                {
                    if (pfx_unapply(&gp->rules[r2], mid, base, sizeof(base)))
                    {
                        he = htab_find(m, base);

                        if (he && has_flag(he->flags, he->n_flags, gs->flag) && has_flag(he->flags, he->n_flags, gp->flag))
                            return 1;
                    }
                }
            }
        }
    }

    return 0;
}

struct spell *spell_new(const char *aff_path, const char *dic_path)
{
    struct spell *m = NULL;

    if (!aff_path || !dic_path)
        return NULL;

    m = (struct spell *)calloc(1, sizeof(*m));

    if (!m)
        return NULL;

    strcpy(m->encoding, "UTF-8");

    if (load_aff(m, aff_path) != 0)
    {
        spell_free(m);
        return NULL;
    }

    if (load_dic(m, dic_path) != 0)
    {
        spell_free(m);
        return NULL;
    }

    check_cache_init(m);

    return m;
}

void spell_free(struct spell *s)
{
    int i;
    int j;

    if (!s)
        return;

    /* free sugg buffer */
    for (i = 0; i < SPELL_MAX_SUGGS; i++)
    {
        if (s->sugg_buf[i])
            free(s->sugg_buf[i]);
    }

    /* free hash table */
    if (s->htab)
    {
        for (i = 0; i < s->htab_size; i++)
        {
            if (s->htab[i].word)
            {
                free(s->htab[i].word);
                free(s->htab[i].flags);
            }
        }

        free(s->htab);
    }

    /* free aff groups */
    for (i = 0; i < s->n_groups; i++)
    {
        for (j = 0; j < s->groups[i].n_rules; j++)
        {
            free(s->groups[i].rules[j].strip);
            free(s->groups[i].rules[j].add);
            free(s->groups[i].rules[j].cond);
        }

        free(s->groups[i].rules);
    }

    free(s->groups);

    /* free reps */
    for (i = 0; i < s->n_reps; i++)
    {
        free(s->reps[i].from);
        free(s->reps[i].to);
    }

    free(s->reps);

    /* free maps */
    for (i = 0; i < s->n_maps; i++)
    {
        if (s->maps[i])
        {
            int k = 0;

            while (s->maps[i][k])
            {
                free(s->maps[i][k]);
                k++;
            }

            free(s->maps[i]);
        }
    }

    free(s->maps);
    free(s->try_chars);
    free(s);
}

int spell_check(struct spell *s, const char *word)
{
    int idx;
    struct ms_hentry *he = NULL;
    size_t wlen;
    unsigned char first;
    char utf8_word[SPELL_MAX_WORD];
    const char *word_to_check = word;

    if (!s || !word)
        return -1;

    /* Convert from dictionary encoding to UTF-8 if needed */
    if (strcasecmp(s->encoding, "UTF-8") != 0 && strcasecmp(s->encoding, "UTF8") != 0)
    {
        int converted = charset_to_utf8(s->encoding, word, (int)strlen(word), utf8_word, sizeof(utf8_word));

        if (converted > 0)
            word_to_check = utf8_word;
    }

    wlen = strlen(word_to_check);

    if (wlen == 0)
        return 1;

    if (wlen >= SPELL_MAX_WORD)
        return -1;

    /* cache */
    idx = check_cache_find(s, word_to_check);

    if (idx != -1)
    {
        check_cache_unlink(s, idx);
        check_cache_push_front(s, idx);

        return s->cache[idx].result ? 1 : 0;
    }

    /* lookup directo */
    he = htab_find(s, word_to_check);

    if (he)
    {
        check_cache_put(s, word_to_check, 1);
        return 1;
    }

    /* if starts with uppercase, try lowercase version */
    first = (unsigned char)word_to_check[0];

    if (first >= 'A' && first <= 'Z')
    {
        char lower[SPELL_MAX_WORD];
        size_t k;

        for (k = 0; k < wlen; k++)
        {
            unsigned char c = (unsigned char)word_to_check[k];

            lower[k] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
        }

        lower[wlen] = '\0';
        he = htab_find(s, lower);

        if (he)
        {
            check_cache_put(s, word_to_check, 1);
            return 1;
        }

        if (check_with_affixes(s, lower))
        {
            check_cache_put(s, word_to_check, 1);
            return 1;
        }
    }

    /* try removing affixes */
    if (check_with_affixes(s, word_to_check))
    {
        check_cache_put(s, word_to_check, 1);
        return 1;
    }

    check_cache_put(s, word_to_check, 0);

    return 0;
}

static void sugg_clear(struct spell *s)
{
    int i;

    for (i = 0; i < s->sugg_n; i++)
    {
        if (s->sugg_buf[i])
        {
            free(s->sugg_buf[i]);
            s->sugg_buf[i] = NULL;
        }
    }

    s->sugg_n = 0;
}

static int sugg_has(struct spell *s, const char *w)
{
    int i;

    for (i = 0; i < s->sugg_n; i++)
    {
        if (s->sugg_buf[i] && strcmp(s->sugg_buf[i], w) == 0)
            return 1;
    }

    return 0;
}

/* try 'cand' as suggestion. Returns 1 if added, 0 if not */
static int sugg_try(struct spell *s, const char *cand)
{
    if (!cand || !*cand)
        return 0;

    if (s->sugg_n >= SPELL_MAX_SUGGS)
        return 0;

    if (sugg_has(s, cand))
        return 0;

    if (spell_check(s, cand) != 1)
        return 0;

    s->sugg_buf[s->sugg_n] = ms_strdup(cand);

    if (!s->sugg_buf[s->sugg_n])
        return 0;

    s->sugg_n++;

    return 1;
}

/* generate variants based on REP table */
static void gen_rep(struct spell *s, const char *word)
{
    int i;
    char cand[SPELL_MAX_WORD * 2];

    for (i = 0; i < s->n_reps && s->sugg_n < SPELL_MAX_SUGGS; i++)
    {
        const char *pos = word;
        const char *fnd = NULL;
        size_t flen = strlen(s->reps[i].from);

        if (flen == 0)
            continue;

        while ((fnd = strstr(pos, s->reps[i].from)) != NULL && s->sugg_n < SPELL_MAX_SUGGS)
        {
            size_t prefix = (size_t)(fnd - word);
            size_t tlen = strlen(s->reps[i].to);
            size_t suffix = strlen(fnd + flen);

            if (prefix + tlen + suffix + 1 > sizeof(cand))
                break;

            memcpy(cand, word, prefix);
            memcpy(cand + prefix, s->reps[i].to, tlen);
            memcpy(cand + prefix + tlen, fnd + flen, suffix + 1);

            sugg_try(s, cand);
            pos = fnd + flen;
        }
    }
}

/* count UTF-8 characters in string */
static int utf8_chars(const char *s)
{
    int n = 0;

    while (*s)
    {
        unsigned char c = (unsigned char)*s;

        if ((c & 0xC0) != 0x80)
            n++;

        s++;
    }

    return n;
}

/* byte position of N-th character (0-indexed). returns strlen if N >= chars */
static int char_to_byte(const char *s, int n)
{
    int i = 0, count = 0;

    while (s[i])
    {
        if (((unsigned char)s[i] & 0xC0) != 0x80)
        {
            if (count == n)
                return i;

            count++;
        }

        i++;
    }

    return i;
}

/* length of UTF-8 character starting at byte i */
static int char_len_at(const char *s, int i)
{
    unsigned char c = (unsigned char)s[i];

    if (c < 0x80)
        return 1;

    if ((c & 0xE0) == 0xC0)
        return 2;

    if ((c & 0xF0) == 0xE0)
        return 3;

    if ((c & 0xF8) == 0xF0)
        return 4;

    return 1;
}

/* deletechar: delete one character */
static void gen_delete(struct spell *s, const char *word)
{
    char cand[SPELL_MAX_WORD];
    int chars = utf8_chars(word);
    int i;

    for (i = 0; i < chars && s->sugg_n < SPELL_MAX_SUGGS; i++)
    {
        int b = char_to_byte(word, i);
        int cl = char_len_at(word, b);
        size_t left = (size_t)b;
        size_t right = strlen(word + b + cl);

        if (left + right + 1 > sizeof(cand))
            continue;

        memcpy(cand, word, left);
        memcpy(cand + left, word + b + cl, right + 1);

        sugg_try(s, cand);
    }
}

/* swap: swap two adjacent characters */
static void gen_swap(struct spell *s, const char *word)
{
    char cand[SPELL_MAX_WORD];
    int chars = utf8_chars(word);
    int i;

    for (i = 0; i < chars - 1 && s->sugg_n < SPELL_MAX_SUGGS; i++)
    {
        int b1 = char_to_byte(word, i);
        int b2 = char_to_byte(word, i + 1);
        int l1 = char_len_at(word, b1);
        int l2 = char_len_at(word, b2);
        size_t total = strlen(word);

        if (total + 1 > sizeof(cand))
            continue;

        memcpy(cand, word, (size_t)b1);
        memcpy(cand + b1, word + b2, (size_t)l2);
        memcpy(cand + b1 + l2, word + b1, (size_t)l1);
        memcpy(cand + b2 + l2 - l1 + l1, word + b2 + l2, total - (size_t)(b2 + l2) + 1);

        /* simpler: rebuild */
        memcpy(cand, word, (size_t)b1);
        memcpy(cand + b1, word + b2, (size_t)l2);
        memcpy(cand + b1 + l2, word + b1, (size_t)l1);
        memcpy(cand + b1 + l2 + l1, word + b2 + l2, total - (size_t)(b2 + l2) + 1);

        sugg_try(s, cand);
    }
}

/* insertchar: insert each TRY char at each position */
static void gen_insert(struct spell *s, const char *word)
{
    char cand[SPELL_MAX_WORD];
    int chars;
    int i;
    const char *t = NULL;

    if (!s->try_chars)
        return;

    chars = utf8_chars(word);

    for (i = 0; i <= chars && s->sugg_n < SPELL_MAX_SUGGS; i++)
    {
        int b = char_to_byte(word, i);
        t = s->try_chars;

        while (*t && s->sugg_n < SPELL_MAX_SUGGS)
        {
            ms_cp cp;
            int adv = utf8_decode(t, &cp);
            char ch_buf[8];
            size_t total = strlen(word);

            if (total + adv + 1 > sizeof(cand))
            {
                t += adv;
                continue;
            }

            memcpy(ch_buf, t, (size_t)adv);

            ch_buf[adv] = '\0';

            memcpy(cand, word, (size_t)b);
            memcpy(cand + b, ch_buf, (size_t)adv);
            memcpy(cand + b + adv, word + b, total - (size_t)b + 1);

            sugg_try(s, cand);

            t += adv;
        }
    }
}

/* replacechar: replace each char with each TRY char */
static void gen_replace(struct spell *s, const char *word)
{
    char cand[SPELL_MAX_WORD];
    int chars;
    int i;
    const char *t = NULL;

    if (!s->try_chars)
        return;

    chars = utf8_chars(word);

    for (i = 0; i < chars && s->sugg_n < SPELL_MAX_SUGGS; i++)
    {
        int b = char_to_byte(word, i);
        int cl = char_len_at(word, b);

        t = s->try_chars;

        while (*t && s->sugg_n < SPELL_MAX_SUGGS)
        {
            ms_cp cp;
            int adv = utf8_decode(t, &cp);
            size_t total = strlen(word);

            if ((size_t)b + adv + total - (size_t)(b + cl) + 1 > sizeof(cand))
            {
                t += adv;
                continue;
            }

            memcpy(cand, word, (size_t)b);
            memcpy(cand + b, t, (size_t)adv);
            memcpy(cand + b + adv, word + b + cl, total - (size_t)(b + cl) + 1);

            sugg_try(s, cand);

            t += adv;
        }
    }
}

char **spell_suggest(struct spell *s, const char *word, int *n_suggestions)
{
    char utf8_word[SPELL_MAX_WORD];
    const char *word_to_suggest = word;
    int i;

    if (!s || !word || !n_suggestions)
        return NULL;

    /* Convert from dictionary encoding to UTF-8 if needed */
    if (strcasecmp(s->encoding, "UTF-8") != 0 && strcasecmp(s->encoding, "UTF8") != 0)
    {
        int converted = charset_to_utf8(s->encoding, word, (int)strlen(word), utf8_word, sizeof(utf8_word));

        if (converted > 0)
            word_to_suggest = utf8_word;
    }

    sugg_clear(s);

    /* apply strategies in quality order */
    gen_rep(s, word_to_suggest);
    gen_swap(s, word_to_suggest);
    gen_delete(s, word_to_suggest);
    gen_replace(s, word_to_suggest);
    gen_insert(s, word_to_suggest);

    /* Convert suggestions from UTF-8 to dictionary encoding if needed */
    if (strcasecmp(s->encoding, "UTF-8") != 0 && strcasecmp(s->encoding, "UTF8") != 0)
    {
        for (i = 0; i < s->sugg_n; i++)
        {
            char encoded[SPELL_MAX_WORD];
            int converted = utf8_to_charset(s->encoding, s->sugg_buf[i], (int)strlen(s->sugg_buf[i]), encoded, sizeof(encoded));

            if (converted > 0)
                strcpy(s->sugg_buf[i], encoded);
        }
    }

    *n_suggestions = s->sugg_n;
    return s->sugg_buf;
}

void spell_free_suggestions(struct spell *s, char **suggestions, int n_suggestions)
{
    /* Strings belong to engine; out[] array is borrowed. Nothing to free here. */
    if (suggestions)
        return;
}

int spell_is_available(void)
{
    return 1;
}

static int ends_with_dic(const unsigned char *name)
{
    size_t len;

    if (!name)
        return 0;

    len = strlen((const char *)name);

    if (len < 4)
        return 0;

    return (tolower(name[len - 4]) == '.' &&
            tolower(name[len - 3]) == 'd' &&
            tolower(name[len - 2]) == 'i' &&
            tolower(name[len - 1]) == 'c');
}

static char *extract_dict_name(const unsigned char *name)
{
    size_t len;
    char *base = NULL;

    if (!name)
        return NULL;

    len = strlen((const char *)name);

    if (len < 4)
        return NULL;

    base = (char *)malloc(len - 3);

    if (!base)
        return NULL;

    memcpy(base, name, len - 4);
    base[len - 4] = '\0';

    return base;
}

char **spell_list_dictionaries(const char *dir_path, int *n_dicts)
{
    char **list = NULL;
    int cap;
    int count;

    if (n_dicts)
        *n_dicts = 0;

    if (!dir_path || !n_dicts)
        return NULL;

    cap = 16;
    count = 0;
    list = (char **)malloc((size_t)cap * sizeof(char *));

    if (!list)
        return NULL;

#ifdef PLATFORM_AMIGA
    {
        BPTR lock;
        struct FileInfoBlock *fib = NULL;

        lock = Lock((CONST_STRPTR)dir_path, ACCESS_READ);

        if (lock)
        {
            fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);

            if (fib && Examine(lock, fib))
            {
                while (ExNext(lock, fib))
                {
                    if (fib->fib_DirEntryType < 0 && ends_with_dic(fib->fib_FileName))
                    {
                        char *base = extract_dict_name(fib->fib_FileName);

                        if (base)
                        {
                            if (count >= cap)
                            {
                                char **g;

                                cap *= 2;
                                g = (char **)realloc(list, (size_t)cap * sizeof(char *));

                                if (!g)
                                {
                                    free(base);
                                    continue;
                                }

                                list = g;
                            }

                            list[count++] = base;
                        }
                    }
                }
            }

            if (fib)
                FreeDosObject(DOS_FIB, fib);

            UnLock(lock);
        }
    }
#endif

    if (count == 0)
    {
        free(list);
        return NULL;
    }

    *n_dicts = count;

    return list;
}

void spell_free_dictionaries(char **dicts, int n_dicts)
{
    int i;

    if (!dicts)
        return;

    for (i = 0; i < n_dicts; i++)
        free(dicts[i]);

    free(dicts);
}

int spell_add_word(struct spell *s, const char *word)
{
    return -1;
}

int spell_remove_word(struct spell *s, const char *word)
{
    return -1;
}

int spell_stem(struct spell *s, const char *word, char ***out_list)
{
    if (out_list)
        *out_list = NULL;

    return -1;
}

int spell_generate(struct spell *s, const char *word, const char *example, char ***out_list)
{
    if (out_list)
        *out_list = NULL;

    return -1;
}

void spell_free_list(struct spell *s, char **list, int n)
{
    if (list)
        free(list);
}

int spell_load_custom(struct spell *s, const char *path)
{
    return -1;
}

int spell_add_to_custom_dict(struct spell *s, const char *word, const char *custom_dict_path)
{
    return -1;
}
