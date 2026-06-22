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

#ifndef TE_WORDCHAR_H
#define TE_WORDCHAR_H

#include <wchar.h>

/* Returns 1 if wc is letter/digit/soft hyphen part of word for spell-checker covers ASCII Latin Extended ranges */
static __inline int te_is_word_char(wint_t wc)
{
    /* ASCII alphanumeric */
    if (wc >= '0' && wc <= '9')
        return 1;

    if (wc >= 'a' && wc <= 'z')
        return 1;

    if (wc >= 'A' && wc <= 'Z')
        return 1;

    /* apostrophe interior don't l'arbre */
    if (wc == '\'' || wc == 0x2019 /* ' */)
        return 1;

    /* Latin-1 Supplement accents n tilde c cedilla etc */
    /* 0x00C0..0x00FF except 0x00D7 multiplication 0x00F7 division */
    if (wc >= 0x00C0 && wc <= 0x00FF)
    {
        if (wc == 0x00D7 || wc == 0x00F7)
            return 0;

        return 1;
    }

    /* Latin Extended-A 256 letters Polish Czech etc */
    if (wc >= 0x0100 && wc <= 0x017F)
        return 1;

    /* Latin Extended-B */
    if (wc >= 0x0180 && wc <= 0x024F)
        return 1;

    /* basic Greek for tinyedit with .el */
    if (wc >= 0x0370 && wc <= 0x03FF)
    {
        if (wc == 0x0378 || wc == 0x0379 || wc == 0x037F)
            return 0;

        return 1;
    }

    /* basic Cyrillic */
    if (wc >= 0x0400 && wc <= 0x04FF)
        return 1;

    return 0;
}

#endif
