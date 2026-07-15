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

/* layout.h -- pure line breaking, the wrap hyphen is a break flag, never text */

#ifndef LAYOUT_H
#define LAYOUT_H

#include <wchar.h>

/* How a line ends, that is, how it joins to the next one */
typedef enum
{
    LB_PARA = 0,  /* End of the paragraph, nothing follows */
    LB_SPACE = 1, /* Broken at a space, that space was consumed */
    LB_WORD = 2,  /* Forced cut inside a word, no hyphen fitted */
    LB_HYPHEN = 3 /* Broken inside a word, the painter draws a hyphen */
} LineBreak;

typedef struct
{
    int start; /* First character of the line, index into the paragraph */
    int end;   /* One past the last character */
    LineBreak brk;
} LayoutLine;

/* Display width of one character at a given column, tabs depend on the column */
typedef int (*LayoutWidthFn)(void *user, wchar_t ch, int col);

/* Break offsets inside a word, returns how many were written to out */
typedef int (*LayoutHyphenFn)(void *user, const wchar_t *word, int len, int *out, int max);

typedef struct
{
    int hyphenate;  /* 0 = never hyphenate */
    int min_word;   /* Do not hyphenate words shorter than this */
    int min_left;   /* Characters that must stay before the hyphen */
    int min_right;  /* Characters that must go to the next line */
    int max_ladder; /* How many hyphenated lines may follow each other */

    LayoutWidthFn width;
    void *width_user;

    LayoutHyphenFn hyphen;
    void *hyphen_user;
} LayoutOpts;

/* Typographic defaults, width still has to be filled in by the caller */
void layout_opts_default(LayoutOpts *o);

/* Break the paragraph into lines, returns how many, or -1 if out does not fit */
int layout_paragraph(const wchar_t *text, int len, int width, const LayoutOpts *o, LayoutLine *out, int max_lines);

/* Put the paragraph back together, the inverse of layout_paragraph */
int layout_join(const wchar_t *text, const LayoutLine *lines, int n, wchar_t *out, int cap);

/* Horizontal indent for the alignment */
int layout_align_indent(unsigned char align, int text_vw, int avail);

#endif
