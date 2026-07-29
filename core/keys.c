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

    /* Linux/xterm: fold ESC+char to KEY_ALT/KEY_SHIFT/KEY_ALT_CTRL (Amiga/Win32 do this natively) */
    if (wrc != KEY_CODE_YES && wch == 27)
    {
        wint_t wch2;
        int wrc2;

        nodelay(stdscr, TRUE);

        wrc2 = get_wch(&wch2);

        nodelay(stdscr, FALSE);

        if (wrc2 != ERR && wrc2 != KEY_CODE_YES)
        {
            int c = (int)wch2;

            /* ESC + Ctrl+letter -> Alt+Ctrl+letter */
            if (c >= 1 && c <= 26)
            {
                *out_wch = (wint_t)KEY_ALT_CTRL(c + 64);
                return KEY_CODE_YES;
            }

            if (wch2 >= 0x20 && wch2 < 0x7F)
            {
                int letter = (int)wch2;

                /* Case-fold so KEY_ALT('l') == KEY_ALT('L') */
                if (letter >= 'a' && letter <= 'z')
                    letter = letter - 'a' + 'A';

                *out_wch = (wint_t)KEY_ALT(letter);

                return KEY_CODE_YES;
            }
        }

        /* Bare ESC -- caller treats as cancel */
        *out_wch = 27;

        return wrc;
    }

    /* Map kf25-kf72 to our macros (kf25-36=Ctrl+F, kf37-48=Shift+Ctrl, kf49-60=Alt, kf61-72=Shift+Alt); skip standard KEY_* that share the range */
    if (wrc == KEY_CODE_YES && (int)wch >= KEY_F(25) && (int)wch <= KEY_F(72) &&
        (int)wch != KEY_DC && (int)wch != KEY_IC && (int)wch != KEY_HOME &&
        (int)wch != KEY_END && (int)wch != KEY_PPAGE && (int)wch != KEY_NPAGE &&
        (int)wch != KEY_UP && (int)wch != KEY_DOWN &&
        (int)wch != KEY_LEFT && (int)wch != KEY_RIGHT &&
        (int)wch != KEY_BACKSPACE && (int)wch != KEY_ENTER &&
        (int)wch != KEY_BTAB && (int)wch != KEY_CTAB && (int)wch != KEY_STAB)
    {
        int fn = (int)wch - (KEY_F(1) - 1);

        if (fn >= 25 && fn <= 36)
        {
            *out_wch = (wint_t)KEY_F(fn);

            return KEY_CODE_YES;
        }
        else if (fn >= 37 && fn <= 48)
        {
            *out_wch = (wint_t)KEY_SHIFT_CTRL_F(fn - 36);

            return KEY_CODE_YES;
        }
        else if (fn >= 49 && fn <= 60)
        {
            *out_wch = (wint_t)KEY_ALT_F(fn - 48);

            return KEY_CODE_YES;
        }
        else if (fn >= 61 && fn <= 72)
        {
            *out_wch = (wint_t)KEY_SHIFT_ALT_F(fn - 60);

            return KEY_CODE_YES;
        }
    }
#endif

    *out_wch = wch;

    return wrc;
}

int wrapper_getch(void)
{
    wint_t wch;
    int wrc = wrapper_read_key(&wch);

    if (wrc == ERR)
        return ERR;

    return (int)wch;
}
