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

/* search.h -- Text search for editor */

#ifndef CRASHEDIT_SEARCH_H
#define CRASHEDIT_SEARCH_H

#include <stdint.h>

#define SEARCH_HIT_TEXT 0x01u
#define SEARCH_DEFAULT_MAX 2048
#define SEARCH_HITS_HARD_MAX 1000000
#define SEARCH_PATTERN_MAX 256
#define SEARCH_PREVIEW_MAX 64
#define SEARCH_FROM_MAX 32

typedef struct
{
    int line;                         /* line number in text */
    int col;                          /* column position */
    uint16_t flags;                   /* OR of SEARCH_HIT_* */
    char preview[SEARCH_PREVIEW_MAX]; /* context around match */
} SearchHit;

typedef struct
{
    /* Search parameters (read-only after creation) */
    char pattern[SEARCH_PATTERN_MAX];
    int pat_len; /* cached strlen(pattern), -1 if empty */
    int case_sensitive;

    /* Results buffer: malloc'd to max_hits, clamped by SEARCH_HITS_HARD_MAX */
    SearchHit *hits;
    int n_hits;
    int max_hits; /* allocated capacity of hits[] */
    int hit_limit_reached;

    /* Progress/cancellation: cancel set by UI, polled during scan */
    int cancel;
    int scanned_lines;
    int total_lines;
} SearchSession;

/* Allocate session, copy pattern, return NULL on bad args or OOM */
SearchSession *search_new(const char *pattern, int case_sensitive, int max_hits);

void search_free(SearchSession *s);

/* Search text buffer, record hits, return count or -1 if cancelled */
int search_text(SearchSession *s, const char **text_lines, int nlines, int preview_chars);

#endif /* CRASHEDIT_SEARCH_H */
