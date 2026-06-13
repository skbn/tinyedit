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

/* ncursesw_amiga.h - ncursesw for AmigaOS 3 */

#ifndef NCURSESW_AMIGA_H
#define NCURSESW_AMIGA_H

#ifdef PLATFORM_AMIGA

#include <stdio.h>
#include <wchar.h>

/* ncursesw types */
typedef unsigned int wint_t;
typedef unsigned long chtype;

/* attr_t type */
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

/* Screen cell */
typedef struct
{
    chtype ch;
    int attrs;
} Cell;

typedef struct _win_st
{
    int _cury, _curx; /* Cursor position */
    int _maxy, _maxx; /* Window size */
    int _begy, _begx; /* Window origin */
    int _flags;
    chtype **_line;          /* Line content */
    struct _win_st *_parent; /* Parent window */

    /* AmigaOS extensions */
    Cell *cells;    /* cell buffer */
    int attrs;      /* current attrs */
    int color_pair; /* current color pair */
} WINDOW;

/* Standard window */
extern WINDOW *stdscr;
extern WINDOW *curscr;

/* Globals */
extern int LINES;
extern int COLS;
extern int COLOR_PAIRS;
extern int COLORS;

/* Result constants */
#define ERR (-1)
#define OK 0

/* Key constants */
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
#define KEY_EVENT 0x19B
#define KEY_MAX 0x1FF

/* Modifier-key codes from wgetch() above KEY_MAX. See ui/ui_internal.h for cross-platform definitions */
#ifndef KEY_ALT
#define KEY_ALT(c) (0x800 + ((unsigned int)(c) & 0xFF))
#endif
#ifndef KEY_SHIFT
#define KEY_SHIFT(c) (0x900 + ((unsigned int)(c) & 0xFF))
#endif

/* Ctrl+Arrow keycodes (reply navigation in reader) */
#ifndef KEY_CLEFT
#define KEY_CLEFT 0x7F3
#endif
#ifndef KEY_CRIGHT
#define KEY_CRIGHT 0x7F4
#endif

/* Alt+Arrow keycodes (word movement in editor) */
#ifndef KEY_ALEFT
#define KEY_ALEFT 0x7F5
#endif
#ifndef KEY_ARIGHT
#define KEY_ARIGHT 0x7F6
#endif

/* Shift+Arrow keycodes */
#ifndef KEY_SLEFT
#define KEY_SLEFT 0x7F7
#endif
#ifndef KEY_SRIGHT
#define KEY_SRIGHT 0x7F8
#endif

/* Attributes */
#define A_NORMAL 0
#define A_ATTRIBUTES 0xffff0000UL
#define A_CHARTEXT 0x0000ffffUL
#define A_COLOR 0x0000ff00UL
#define A_STANDOUT 0x00010000UL
#define A_UNDERLINE 0x00020000UL
#define A_REVERSE 0x00040000UL
#define A_BLINK 0x00080000UL
#define A_DIM 0x00100000UL
#define A_BOLD 0x00200000UL
#define A_ALTCHARSET 0x00400000UL
#define A_INVIS 0x00800000UL
#define A_PROTECT 0x01000000UL
#define A_HORIZONTAL 0x02000000UL
#define A_LEFT 0x04000000UL
#define A_LOW 0x08000000UL
#define A_RIGHT 0x10000000UL
#define A_TOP 0x20000000UL
#define A_VERTICAL 0x40000000UL

/* Attribute macros */
#define COLOR_PAIR(n) (((unsigned long)(n)) << 8)
#define PAIR_NUMBER(a) (((unsigned long)(a) & A_COLOR) >> 8)

/* Colors */
#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7

/* Init */
WINDOW *initscr();
int endwin();
bool isendwin();

/* Window create/destroy */
WINDOW *newwin(int nlines, int ncols, int begin_y, int begin_x);
int delwin(WINDOW *win);
WINDOW *subwin(WINDOW *orig, int nlines, int ncols, int begin_y, int begin_x);
WINDOW *derwin(WINDOW *orig, int nlines, int ncols, int begin_y, int begin_x);
int mvwin(WINDOW *win, int y, int x);
int mvderwin(WINDOW *win, int par_y, int par_x);
WINDOW *dupwin(WINDOW *win);

/* Size/position macros - write to variables via pointers */
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

#define getparyx(win, y, x)                             \
    do                                                  \
    {                                                   \
        if (win && (win)->_parent)                      \
        {                                               \
            (y) = (win)->_begy - (win)->_parent->_begy; \
            (x) = (win)->_begx - (win)->_parent->_begx; \
        }                                               \
        else                                            \
        {                                               \
            (y) = 0;                                    \
            (x) = 0;                                    \
        }                                               \
    } while (0)

/* Refresh functions */
int wrefresh(WINDOW *win);
int refresh();
int wnoutrefresh(WINDOW *win);
int doupdate();
int redrawwin(WINDOW *win);
int wredrawln(WINDOW *win, int beg_line, int num_lines);

/* Clear functions */
int clear();
int wclear(WINDOW *win);
int erase();
int werase(WINDOW *win);
int clrtobot();
int wclrtobot(WINDOW *win);
int clrtoeol();
int wclrtoeol(WINDOW *win);

/* Cursor movement */
int move(int y, int x);
int wmove(WINDOW *win, int y, int x);

/* Text output */
int addch(const chtype ch);
int waddch(WINDOW *win, const chtype ch);
int mvaddch(int y, int x, const chtype ch);
int mvwaddch(WINDOW *win, int y, int x, const chtype ch);

/* Read character at position */
chtype mvinch(int y, int x);
chtype mvwinch(WINDOW *win, int y, int x);

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

/* Printf-style formatting */
int printw(const char *fmt, ...);
int wprintw(WINDOW *win, const char *fmt, ...);
int mvprintw(int y, int x, const char *fmt, ...);
int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...);
int vw_printw(WINDOW *win, const char *fmt, va_list ap);

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

/* Macros for stdscr versions (ncurses compatibility) */
#define attr_get(a, b, c) wattr_get(stdscr, a, b, c)
#define standend() wstandend(stdscr)
#define standout() wstandout(stdscr)
#define wstandend(win) wattrset(win, A_NORMAL)
#define wstandout(win) wattron(win, A_STANDOUT)
#define leaveok(win, bf) ((win)->_flags = ((bf) ? ((win)->_flags | 1) : ((win)->_flags & ~1)))
#define scrollok(win, bf) ((win)->_flags = ((bf) ? ((win)->_flags | 2) : ((win)->_flags & ~2)))
#define immedok(win, bf) ((win)->_flags = ((bf) ? ((win)->_flags | 4) : ((win)->_flags & ~4)))
#define syncok(win, bf) ((win)->_flags = ((bf) ? ((win)->_flags | 8) : ((win)->_flags & ~8)))
#define touchwin(win) wtouchln(win, 0, (win)->_maxy, 1)

/* Colors */
bool has_colors();
bool can_change_color();
int start_color();
int init_pair(short pair, short fg, short bg);
int init_color(short color, short r, short g, short b);
int use_default_colors();
int pair_content(short pair, short *fg, short *bg);
int color_content(short color, short *r, short *g, short *b);
int assume_default_colors(int fg, int bg);

/* Amiga-specific extensions */
int amiga_set_cursor_pen(int pen);
int amiga_set_default_colors(short fg, short bg);
int amiga_set_default_bg_color(int color);
int amiga_set_font_name(const char *font_name);
int amiga_set_ansi_font_name(const char *font_name);

/* TrueType font support (ttengine.library v6+). Call BEFORE initscr() */
int amiga_set_ttf(const char *ttf_file, int size, int antialias);

/* Optional: register extra TTF fonts for missing codepoints */
int amiga_add_ttf_fallback(const char *path, int size);
void amiga_clear_ttf_fallbacks(void);
int amiga_set_ttf_encoding(int use_utf8);
int amiga_reload_ttf_size(int new_size);
int amiga_reload_ttf(const char *font_path, int new_size);
int amiga_change_font(int use_ansi);
void amiga_force_redraw();

/* Control characters */
int beep();
int flash();

/* Keyboard input */
int getch();
int wgetch(WINDOW *win);
int mvgetch(int y, int x);
int mvwgetch(WINDOW *win, int y, int x);
int ungetch(int ch);
int flushinp();

/* Portable key modifier macros - ASCII control characters are 0-31, Ctrl+letter = letter - 64 */
#define CTRL(ch) ((ch) - 64)
#define IS_CTRL_KEY(ch, letter) ((ch) == CTRL(letter))

/* Alt sequences: Alt+letter is ESC (0x1B) followed by letter, ALT_PREFIX is the ESC character */
#define ALT_PREFIX 0x1B
#define IS_ALT_PREFIX(ch) ((ch) == ALT_PREFIX)

/* Wide char input */
int get_wch(wint_t *wch);
int wget_wch(WINDOW *win, wint_t *wch);
int mvget_wch(int y, int x, wint_t *wch);
int mvwget_wch(WINDOW *win, int y, int x, wint_t *wch);
int unget_wch(const wchar_t wch);

/* Input strings */
int getstr(char *str);
int getnstr(char *str, int n);
int wgetstr(WINDOW *win, char *str);
int wgetnstr(WINDOW *win, char *str, int n);
int mvgetstr(int y, int x, char *str);
int mvwgetstr(WINDOW *win, int y, int x, char *str);
int mvgetnstr(int y, int x, char *str, int n);
int mvwgetnstr(WINDOW *win, int y, int x, char *str, int n);

/* Timeout */
int nodelay(WINDOW *win, bool bf);
int notimeout(WINDOW *win, bool bf);
int timeout(int delay);
int wtimeout(WINDOW *win, int delay);

/* Keyboard mode */
int cbreak();
int nocbreak();
int echo();
int noecho();
int halfdelay(int tenths);
int intrflush(WINDOW *win, bool bf);
int keypad(WINDOW *win, bool bf);
int meta(WINDOW *win, bool bf);
int raw();
int noraw();
int nl();
int nonl();

/* Cursor */
int curs_set(int visibility);

/* Special chars */
unsigned long getmouse();
int ungetmouse(unsigned long m);

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

/* ACS line drawing characters (for portability) */
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

/* Overlay/copy */
int overlay(const WINDOW *src, WINDOW *dst);
int overwrite(const WINDOW *src, WINDOW *dst);
int copywin(const WINDOW *src, WINDOW *dst, int sminrow, int smincol, int dminrow, int dmincol, int dmaxrow, int dmaxcol, int overlay);

/* Touch */
void wtouchln(WINDOW *win, int y, int n, int changed);
int is_linetouched(WINDOW *win, int line);
int is_wintouched(WINDOW *win);
void untouchwin(WINDOW *win);

/* string.h helpers (Amiga libc bugs) */
int amiga_strncasecmp(const char *s1, const char *s2, size_t n);
int amiga_snprintf(char *str, size_t size, const char *format, ...);

/* wchar_t helpers (missing from Amiga libc, wcsstr is a macro in modern libc so we use amiga_wcsstr) */
wchar_t *amiga_wcsstr(const wchar_t *haystack, const wchar_t *needle);
int amiga_wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
int amiga_iswalpha(wint_t wc);
int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wcsncpy(wchar_t *dst, const wchar_t *src, size_t n);
int iswalpha(wint_t wc);
int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wmemcpy(wchar_t *dst, const wchar_t *src, size_t n);
wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n);

#endif /* PLATFORM_AMIGA */

#endif /* NCURSESW_AMIGA_H */
