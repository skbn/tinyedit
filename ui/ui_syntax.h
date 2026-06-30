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

#ifndef UI_SYNTAX_H
#define UI_SYNTAX_H

#include <wchar.h>

typedef enum
{
    SYNTAX_LANG_NONE = -1,
    SYNTAX_LANG_C,
    SYNTAX_LANG_CPP,
    SYNTAX_LANG_X86_ASM,
    SYNTAX_LANG_M68K_ASM,
    SYNTAX_LANG_M68K_C,
    SYNTAX_LANG_COUNT
} SyntaxLang;

typedef enum
{
    SYNTAX_CLASS_NORMAL,
    SYNTAX_CLASS_KEYWORD,
    SYNTAX_CLASS_STRING,
    SYNTAX_CLASS_CHAR,
    SYNTAX_CLASS_COMMENT,
    SYNTAX_CLASS_NUMBER,
    SYNTAX_CLASS_PREPROC
} SyntaxClass;

typedef enum
{
    SYNTAX_STATE_NORMAL,
    SYNTAX_STATE_COMMENT,
    SYNTAX_STATE_STRING,
    SYNTAX_STATE_CHAR
} SyntaxState;

/* Helpers: accept straight and typographic (curly) quotes */
int ui_syntax_is_str_quote(wchar_t ch);
int ui_syntax_is_char_quote(wchar_t ch);

/* Determine language from filename extension */
SyntaxLang ui_syntax_lang_from_filename(const char *filename);

/* Classify a single line of text. start_state is the parser state inheritedfrom the previous line (for multi-line comments/strings) */
SyntaxState ui_syntax_classify(const wchar_t *line, int len, SyntaxClass *classes, SyntaxState start_state, SyntaxLang lang);

/* Return the COL_* color pair for a syntax class */
int ui_syntax_color_pair(SyntaxClass cls);

#endif /* UI_SYNTAX_H */
