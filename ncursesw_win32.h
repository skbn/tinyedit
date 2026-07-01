/*
 * crashedit - Message area editor for AmigaOS
 *
 * This file is part of the crashedit project.
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This program uses JAMLIB, which is licensed under the GNU Lesser
 * General Public License v2.1. See src/jamlib/LICENSE for details.
 */

/* ncursesw_win32.h - ncursesw for Windows using GDI */

#ifndef NCURSESW_WIN32_H
#define NCURSESW_WIN32_H

#ifdef PLATFORM_WIN32

#include <stdio.h>
#include <windows.h>

/* ncursesw types */
typedef unsigned long chtype;
typedef chtype attr_t;

/* bool type */
#ifndef bool
#define bool int
#define true 1
#define false 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Cell structure */
typedef struct
{
    chtype ch;
    attr_t attrs;
    unsigned long full_cp; /* codepoint > 0xFFFF, used on Windows where wchar_t is 16-bit */
} Cell;

/* Window structure */
typedef struct _win_st
{
    int _cury, _curx;
    int _maxy, _maxx;
    int _begy, _begx;
    int _flags;
    Cell *cells;
    attr_t attrs;
    int color_pair;
    struct _win_st *_parent;
} WINDOW;

/* Global windows */
extern WINDOW *stdscr;
extern WINDOW *curscr;

/* Screen dimensions */
extern int LINES;
extern int COLS;
extern int COLOR_PAIRS;
extern int COLORS;

/* Result constants */
#define ERR (-1)
#define OK 0

/* Key codes */
#define KEY_CODE_YES 0x100
#define KEY_MIN 0x101
#define KEY_BREAK 0x101
#define KEY_DOWN 0x102
#define KEY_UP 0x103
#define KEY_LEFT 0x104
#define KEY_RIGHT 0x105
#define KEY_HOME 0x106
#define KEY_BACKSPACE 0x108
#define KEY_F0 0x109
#define KEY_F(n) (KEY_F0 + (n))
#define KEY_DL 0x148
#define KEY_IL 0x149
#define KEY_DC 0x14A
#define KEY_IC 0x14B
#define KEY_EIC 0x14C
#define KEY_CLEAR 0x14D
#define KEY_EOS 0x14E
#define KEY_EOL 0x14F
#define KEY_SF 0x150
#define KEY_SR 0x151

#ifndef KEY_CUP
#define KEY_CUP 0x7F7
#endif
#ifndef KEY_CDOWN
#define KEY_CDOWN 0x7F8
#endif
#ifndef KEY_SUP
#define KEY_SUP 0x7FD
#endif
#ifndef KEY_SDOWN
#define KEY_SDOWN 0x7FE
#endif
#ifndef KEY_CSLEFT
#define KEY_CSLEFT 0x801
#endif
#ifndef KEY_CSRIGHT
#define KEY_CSRIGHT 0x802
#endif
#ifndef KEY_CSUP
#define KEY_CSUP 0x803
#endif
#ifndef KEY_CSDOWN
#define KEY_CSDOWN 0x804
#endif
#ifndef KEY_ALT_UP
#define KEY_ALT_UP 0xA00
#define KEY_ALT_DOWN 0xA01
#define KEY_ALT_LEFT 0xA02
#define KEY_ALT_RIGHT 0xA03
#endif
#ifndef KEY_CSHOME
#define KEY_CSHOME 0x805
#endif
#ifndef KEY_CSEND
#define KEY_CSEND 0x806
#endif
#ifndef KEY_SPPAGE
#define KEY_SPPAGE 0x807
#endif
#ifndef KEY_SNPAGE
#define KEY_SNPAGE 0x808
#endif
#ifndef KEY_CSUPD
#define KEY_CSUPD 0x809
#endif
#ifndef KEY_CSDOWNU
#define KEY_CSDOWNU 0x80A
#endif
#define KEY_NPAGE 0x152
#define KEY_PPAGE 0x153
#define KEY_STAB 0x154
#define KEY_CTAB 0x155
#define KEY_CATAB 0x156
#define KEY_ENTER 0x157
#define KEY_PRINT 0x15A
#define KEY_LL 0x15B
#define KEY_A1 0x15C
#define KEY_A3 0x15D
#define KEY_B2 0x15E
#define KEY_C1 0x15F
#define KEY_C3 0x160
#define KEY_BTAB 0x161
#define KEY_BEG 0x162
#define KEY_CANCEL 0x163
#define KEY_CLOSE 0x164
#define KEY_COMMAND 0x165
#define KEY_COPY 0x166
#define KEY_CREATE 0x167
#define KEY_END 0x168
#define KEY_EXIT 0x169
#define KEY_FIND 0x16A
#define KEY_HELP 0x16B
#define KEY_MARK 0x16C
#define KEY_MESSAGE 0x16D
#define KEY_MOVE 0x16E
#define KEY_NEXT 0x16F
#define KEY_OPEN 0x170
#define KEY_OPTIONS 0x171
#define KEY_PREVIOUS 0x172
#define KEY_REDO 0x173
#define KEY_REFERENCE 0x174
#define KEY_REFRESH 0x175
#define KEY_REPLACE 0x176
#define KEY_RESTART 0x177
#define KEY_RESUME 0x178
#define KEY_SAVE 0x179
#define KEY_SBEG 0x17A
#define KEY_SCANCEL 0x17B
#define KEY_SCOMMAND 0x17C
#define KEY_SCOPY 0x17D
#define KEY_SCREATE 0x17E
#define KEY_SDC 0x17F
#define KEY_SDL 0x180
#define KEY_SELECT 0x181
#define KEY_SEND 0x182
#define KEY_SEOL 0x183
#define KEY_SEXIT 0x184
#define KEY_SFIND 0x185
#define KEY_SHELP 0x186
#define KEY_SHOME 0x187
#define KEY_SIC 0x188
#define KEY_SLEFT 0x189
#define KEY_SMESSAGE 0x18A
#define KEY_SMOVE 0x18B
#define KEY_MOUSE_SGR 0x80B
#define KEY_SNEXT 0x18C
#define KEY_SOPTIONS 0x18D
#define KEY_SPREVIOUS 0x18E
#define KEY_SPRINT 0x18F
#define KEY_SREDO 0x190
#define KEY_SREPLACE 0x191
#define KEY_SRIGHT 0x192
#define KEY_SRSUME 0x193
#define KEY_SSAVE 0x194
#define KEY_SSUSPEND 0x195
#define KEY_SUNDO 0x196
#define KEY_SUSPEND 0x197
#define KEY_UNDO 0x198
#define KEY_MOUSE 0x199
#define KEY_RESIZE 0x19A
#define KEY_EVENT_NCURSES 0x19B
#define KEY_MAX 0x1FF

/* Extended keycodes */
#ifndef KEY_ALT
#define KEY_ALT(c) (0x800 + ((unsigned int)(c) & 0xFF))
#endif
#ifndef KEY_SHIFT
#define KEY_SHIFT(c) (0x900 + ((unsigned int)(c) & 0xFF))
#endif
#ifndef KEY_ALT_CTRL
#define KEY_ALT_CTRL(c) (0xB00 + ((unsigned int)(c) & 0xFF))
#endif
#ifndef KEY_CLEFT
#define KEY_CLEFT 0x7F3
#endif
#ifndef KEY_CRIGHT
#define KEY_CRIGHT 0x7F4
#endif
#ifndef KEY_AUP
#define KEY_AUP 0x7F1
#endif
#ifndef KEY_ADOWN
#define KEY_ADOWN 0x7F2
#endif
#ifndef KEY_ALEFT
#define KEY_ALEFT 0x7F5
#endif
#ifndef KEY_ARIGHT
#define KEY_ARIGHT 0x7F6
#endif

/* Attributes */
#define A_NORMAL 0
#define A_CHARTEXT 0x0000ffffUL
#define A_STANDOUT 0x00010000UL
#define A_UNDERLINE 0x00020000UL
#define A_REVERSE 0x00040000UL
#define A_BLINK 0x00080000UL
#define A_DIM 0x00100000UL
#define A_BOLD 0x00200000UL
#define A_COLOR 0xff000000UL
#define A_ATTRIBUTES 0xffff0000UL
#define A_ALTCHARSET 0x00400000UL
#define A_INVIS 0x00800000UL

#define COLOR_PAIR(n) (((unsigned long)(n)) << 24)
#define PAIR_NUMBER(a) (((unsigned long)(a) & A_COLOR) >> 24)

/* Colors */
#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7
#define COLOR_BRIGHT_BLACK 8
#define COLOR_BRIGHT_RED 9
#define COLOR_BRIGHT_GREEN 10
#define COLOR_BRIGHT_YELLOW 11
#define COLOR_BRIGHT_BLUE 12
#define COLOR_BRIGHT_MAGENTA 13
#define COLOR_BRIGHT_CYAN 14
#define COLOR_BRIGHT_WHITE 15

/* ACS line drawing characters */
#define ACS_BLOCK '#'
#define ACS_VLINE '|'
#define ACS_HLINE '-'
#define ACS_ULCORNER '+'
#define ACS_URCORNER '+'
#define ACS_LLCORNER '+'
#define ACS_LRCORNER '+'
#define ACS_LTEE '+'
#define ACS_RTEE '+'
#define ACS_TTEE '+'
#define ACS_BTEE '+'
#define ACS_PLUS '+'
#define ACS_DIAMOND '+'
#define ACS_BOARD '#'
#define ACS_BULLET '*'

/* Mouse support (stub for Windows) */
#define BUTTON1_PRESSED 0x01
#define BUTTON1_RELEASED 0x02
#define BUTTON1_CLICKED 0x04
#define BUTTON4_PRESSED 0x08
#define BUTTON5_PRESSED 0x10
#define REPORT_MOUSE_POSITION 0x20

typedef struct
{
    short x;
    short y;
    unsigned long bstate;
} MEVENT;

/* Initialization */
WINDOW *initscr(void);
int endwin(void);
bool isendwin(void);

/* Window management */
WINDOW *newwin(int nlines, int ncols, int begin_y, int begin_x);
int delwin(WINDOW *win);
WINDOW *subwin(WINDOW *orig, int nlines, int ncols, int begin_y, int begin_x);
int copywin(const WINDOW *src, WINDOW *dst, int sminrow, int smincol,
            int dminrow, int dmincol, int dmaxrow, int dmaxcol, int overlay);
WINDOW *derwin(WINDOW *orig, int nlines, int ncols, int begin_y, int begin_x);
int mvwin(WINDOW *win, int y, int x);

/* Position macros */
#define getmaxyx(win, y, x)     \
    do                          \
    {                           \
        if (win)                \
        {                       \
            (y) = (win)->_maxy; \
            (x) = (win)->_maxx; \
        }                       \
        else                    \
        {                       \
            (y) = 0;            \
            (x) = 0;            \
        }                       \
    } while (0)
#define getmaxy(win) ((win) ? (win)->_maxy : 0)
#define getmaxx(win) ((win) ? (win)->_maxx : 0)
#define getyx(win, y, x)        \
    do                          \
    {                           \
        if (win)                \
        {                       \
            (y) = (win)->_cury; \
            (x) = (win)->_curx; \
        }                       \
        else                    \
        {                       \
            (y) = 0;            \
            (x) = 0;            \
        }                       \
    } while (0)
#define getbegyx(win, y, x)     \
    do                          \
    {                           \
        if (win)                \
        {                       \
            (y) = (win)->_begy; \
            (x) = (win)->_begx; \
        }                       \
        else                    \
        {                       \
            (y) = 0;            \
            (x) = 0;            \
        }                       \
    } while (0)

/* Refresh */
int wrefresh(WINDOW *win);
int refresh(void);
int wnoutrefresh(WINDOW *win);
int doupdate(void);
int redrawwin(WINDOW *win);

/* Clear */
int clear(void);
int wclear(WINDOW *win);
int erase(void);
int werase(WINDOW *win);
int clrtobot(void);
int wclrtobot(WINDOW *win);
int clrtoeol(void);
int wclrtoeol(WINDOW *win);

/* Cursor movement */
int move(int y, int x);
int wmove(WINDOW *win, int y, int x);

/* Character output */
int addch(const chtype ch);
int waddch(WINDOW *win, const chtype ch);
int mvaddch(int y, int x, const chtype ch);
int mvwaddch(WINDOW *win, int y, int x, const chtype ch);
int waddch32(WINDOW *win, unsigned long cp);
int mvaddch32(int y, int x, unsigned long cp);
chtype mvinch(int y, int x);
chtype mvwinch(WINDOW *win, int y, int x);

/* String output */
int addstr(const char *str);
int addnstr(const char *str, int n);
int waddstr(WINDOW *win, const char *str);
int waddnstr(WINDOW *win, const char *str, int n);
int mvaddstr(int y, int x, const char *str);
int mvaddnstr(int y, int x, const char *str, int n);
int mvwaddstr(WINDOW *win, int y, int x, const char *str);
int mvwaddnstr(WINDOW *win, int y, int x, const char *str, int n);

/* Wide character output */
int addwstr(const wchar_t *wstr);
int addnwstr(const wchar_t *wstr, int n);
int waddwstr(WINDOW *win, const wchar_t *wstr);
int waddnwstr(WINDOW *win, const wchar_t *wstr, int n);
int mvaddwstr(int y, int x, const wchar_t *wstr);
int mvaddnwstr(int y, int x, const wchar_t *wstr, int n);
int mvwaddwstr(WINDOW *win, int y, int x, const wchar_t *wstr);
int mvwaddnwstr(WINDOW *win, int y, int x, const wchar_t *wstr, int n);

/* Attribute change */
int mvchgat(int y, int x, int n, attr_t attr, short color, const void *opts);
int mvwchgat(WINDOW *win, int y, int x, int n, attr_t attr, short color, const void *opts);

/* Printf-style output */
int printw(const char *fmt, ...);
int wprintw(WINDOW *win, const char *fmt, ...);
int mvprintw(int y, int x, const char *fmt, ...);
int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...);

/* Attributes */
int attron(int attrs);
int wattroff(WINDOW *win, int attrs);
int wattron(WINDOW *win, int attrs);
int attroff(int attrs);
int attrset(int attrs);
int wattrset(WINDOW *win, int attrs);
int color_set(short color_pair_number, void *opts);
int wcolor_set(WINDOW *win, short color_pair_number, void *opts);
chtype getattrs(WINDOW *win);
int attr_on(attr_t attrs, void *opts);
int attr_off(attr_t attrs, void *opts);
int attr_set(attr_t attrs, short color_pair, void *opts);
int wattr_get(WINDOW *win, attr_t *attrs, short *color_pair, void *opts);
#define attr_get(a, b, c) wattr_get(stdscr, a, b, c)
#define standend() wstandend(stdscr)
#define standout() wstandout(stdscr)
#define wstandend(win) wattrset(win, A_NORMAL)
#define wstandout(win) wattron(win, A_STANDOUT)
#define leaveok(win, bf) ((win)->_flags = ((bf) ? ((win)->_flags | 1) : ((win)->_flags & ~1)))
#define scrollok(win, bf) ((win)->_flags = ((bf) ? ((win)->_flags | 2) : ((win)->_flags & ~2)))
#define idlok(win, bf) ((void)(win), (void)(bf)) /* No-op for Windows GDI */
#define immedok(win, bf) ((win)->_flags = ((bf) ? ((win)->_flags | 4) : ((win)->_flags & ~4)))
#define clearok(win, bf) ((win)->_flags = ((bf) ? ((win)->_flags | 8) : ((win)->_flags & ~8)))
#define touchwin(win) wtouchln(win, 0, (win)->_maxy, 1)

/* Colors */
bool has_colors(void);
bool can_change_color(void);
int start_color(void);
int init_pair(short pair, short fg, short bg);
int use_default_colors(void);
int pair_content(short pair, short *fg, short *bg);

/* Input */
int getch(void);
int wgetch(WINDOW *win);
int mvgetch(int y, int x);
int mvwgetch(WINDOW *win, int y, int x);
int ungetch(int ch);
int flushinp(void);

/* Wide char input */
int get_wch(wint_t *wch);
int wget_wch(WINDOW *win, wint_t *wch);
int mvget_wch(int y, int x, wint_t *wch);
int mvwget_wch(WINDOW *win, int y, int x, wint_t *wch);

/* String input */
int getstr(char *str);
int getnstr(char *str, int n);
int wgetstr(WINDOW *win, char *str);
int wgetnstr(WINDOW *win, char *str, int n);
int mvgetstr(int y, int x, char *str);
int mvgetnstr(int y, int x, char *str, int n);
int mvwgetstr(WINDOW *win, int y, int x, char *str);
int mvwgetnstr(WINDOW *win, int y, int x, char *str, int n);

/* Timeout */
int nodelay(WINDOW *win, bool bf);
int timeout(int delay);
int wtimeout(WINDOW *win, int delay);

/* Terminal mode */
int cbreak(void);
int nocbreak(void);
int echo(void);
int noecho(void);
int raw(void);
int noraw(void);
int nl(void);
int nonl(void);
int keypad(WINDOW *win, bool bf);

/* Cursor */
int curs_set(int visibility);
int set_tabsize(int n);

/* Windows-specific extensions */
int win32_set_font_name(const char *font_name);
int win32_set_font_size(int size);
int win32_add_font_file(const char *path);
void win32_clear_font_files(void);
int win32_get_font_family_name(const char *path, char *out, int out_sz);
int win32_set_cursor_pen(int color);
void win32_set_ansi_mode(int use_ansi);
void win32_drain_messages(void);
void win32_force_redraw(void);
int win32_reload_ttf(const char *font_path, int new_size);
int win32_reinit_window(void);

/* Border/line */
int border(chtype ls, chtype rs, chtype ts, chtype bs, chtype tl, chtype tr, chtype bl, chtype br);
int wborder(WINDOW *win, chtype ls, chtype rs, chtype ts, chtype bs, chtype tl, chtype tr, chtype bl, chtype br);
int box(WINDOW *win, chtype verch, chtype horch);
int hline(chtype ch, int n);
int whline(WINDOW *win, chtype ch, int n);
int vline(chtype ch, int n);
int wvline(WINDOW *win, chtype ch, int n);
int mvhline(int y, int x, chtype ch, int n);
int mvwhline(WINDOW *win, int y, int x, chtype ch, int n);
int mvvline(int y, int x, chtype ch, int n);
int mvwvline(WINDOW *win, int y, int x, chtype ch, int n);

/* Background */
int bkgd(chtype ch);
void bkgdset(chtype ch);
void wbkgd(WINDOW *win, chtype ch);
void wbkgdset(WINDOW *win, chtype ch);
chtype getbkgd(WINDOW *win);

/* Scroll */
int scroll(WINDOW *win);
int scrl(int n);
int wscrl(WINDOW *win, int n);
int setscrreg(int top, int bot);
int wsetscrreg(WINDOW *win, int top, int bot);

/* Touch */
void wtouchln(WINDOW *win, int y, int n, int changed);

/* Resize support */
int resize_term(int nlines, int ncols);

/* Control */
int beep(void);
int flash(void);

int getmouse(MEVENT *ev);

#endif /* PLATFORM_WIN32 */

#endif /* NCURSESW_WIN32_H */
