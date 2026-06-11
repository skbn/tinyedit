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

/* core/keys.c -- See keys.h */

#ifndef _XOPEN_SOURCE_EXTENDED
#define _XOPEN_SOURCE_EXTENDED 1
#endif

#ifdef PLATFORM_AMIGA
#include "../ncursesw_amiga.h"
#elif defined(PLATFORM_WIN32)
#include "../ncursesw_win32.h"
#else
#include <ncurses.h>
#endif

#include "keys.h"

int wrapper_read_key(wint_t *out_wch)
{
    int wrc;
    wint_t wch;

    if (!out_wch)
        return ERR;

    wrc = get_wch(&wch);
    if (wrc == ERR)
        return ERR;

#if !defined(PLATFORM_AMIGA) && !defined(PLATFORM_WIN32)
    /* Linux/xterm path: ESC + immediate next char -> KEY_ALT(char)
     * The Amiga and Win32 shims return KEY_ALT(c) natively, no folding needed */
    if (wrc != KEY_CODE_YES && wch == 27)
    {
        wint_t wch2;
        int wrc2;

        nodelay(stdscr, TRUE);
        wrc2 = get_wch(&wch2);
        nodelay(stdscr, FALSE);

        if (wrc2 != ERR && wrc2 != KEY_CODE_YES && wch2 >= 0x20 && wch2 < 0x7F)
        {
            int letter = (int)wch2;

            /* Case-fold so KEY_ALT('l') == KEY_ALT('L') */
            if (letter >= 'a' && letter <= 'z')
                letter = letter - 'a' + 'A';

            *out_wch = (wint_t)KEY_ALT(letter);

            return KEY_CODE_YES;
        }

        /* Bare ESC -- caller treats as cancel */
        *out_wch = 27;

        return wrc;
    }
#endif

    *out_wch = wch;

    return wrc;
}

int wrapper_getch()
{
    wint_t wch;
    int wrc = wrapper_read_key(&wch);

    if (wrc == ERR)
        return ERR;

    return (int)wch;
}
