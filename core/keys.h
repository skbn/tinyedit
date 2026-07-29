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

/* core/keys.h -- Cross-platform modifier key macros and wrapper_read_key() */
#ifndef CORE_KEYS_H
#define CORE_KEYS_H

#include <wchar.h>

#ifndef KEY_ALT
#define KEY_ALT(c) (0x800 + ((unsigned int)(c) & 0xFF))
#endif
#ifndef KEY_SHIFT
#define KEY_SHIFT(c) (0x900 + ((unsigned int)(c) & 0xFF))
#endif
#ifndef KEY_ALT_CTRL
#define KEY_ALT_CTRL(c) (0xB00 + ((unsigned int)(c) & 0xFF))
#endif

/* Alt+F(n) combo (n=1..12); distinct range, native on Amiga/Win32, CSI on Unix */
#ifndef KEY_ALT_F
#define KEY_ALT_F(n) (0xD00 + ((unsigned int)(n) & 0xFF))
#endif
/* Shift+Alt+F(n) combo (n=1..12) */
#ifndef KEY_SHIFT_ALT_F
#define KEY_SHIFT_ALT_F(n) (0xE00 + ((unsigned int)(n) & 0xFF))
#endif
/* Shift+Ctrl+F(n) combo (n=1..12). Ctrl+F(n) itself reuses KEY_F(24+n) */
#ifndef KEY_SHIFT_CTRL_F
#define KEY_SHIFT_CTRL_F(n) (0xF00 + ((unsigned int)(n) & 0xFF))
#endif

#define IS_ALT(k) (((unsigned int)(k) & 0xFF00) == 0x800)
#define IS_SHIFT(k) (((unsigned int)(k) & 0xFF00) == 0x900)
#define IS_ALT_CTRL(k) (((unsigned int)(k) & 0xFF00) == 0xB00)
#define IS_ALT_F(k) (((unsigned int)(k) & 0xFF00) == 0xD00)
#define IS_SHIFT_ALT_F(k) (((unsigned int)(k) & 0xFF00) == 0xE00)
#define IS_SHIFT_CTRL_F(k) (((unsigned int)(k) & 0xFF00) == 0xF00)
#define ALT_CHAR(k) ((int)((unsigned int)(k) & 0xFF))
#define SHIFT_CHAR(k) ((int)((unsigned int)(k) & 0xFF))
#define ALT_CTRL_CHAR(k) ((int)((unsigned int)(k) & 0xFF))
#define ALT_F_INDEX(k) ((int)((unsigned int)(k) & 0xFF))

/* Read next keystroke; folds Linux ESC+letter into KEY_ALT(c). Same protocol as get_wch() */
int wrapper_read_key(wint_t *out_wch);

/* Wrapper around wrapper_read_key returning int like getch() */
int wrapper_getch(void);

#endif /* CORE_KEYS_H */
