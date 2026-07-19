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

/* https://ia800501.us.archive.org/view_archive.php?archive=/26/items/wordperfectsdkperfectfit1994/WordPerfect_SDK_PerfectFit1994.iso&file=51PCSDK%2FWP42FF.TXT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "editor.h"
#include "ed_attr.h"
#include "fmt_wp4.h"
#include "../core/utf8.h"
#include "../core/charset.h"

/* WP 4.2 default margin settings (from WP 4.2 format documentation and Amiga WP 4.2 files) */
#define WP4_DEFAULT_LEFT_MARGIN 10
#define WP4_DEFAULT_RIGHT_MARGIN 75
#define WP4_CENTER_COL ((WP4_DEFAULT_LEFT_MARGIN + WP4_DEFAULT_RIGHT_MARGIN) / 2)

/* One recorded styled run, document-wide */
struct wp4_run
{
    int line;
    int start;
    int end;
    unsigned short mask;
};

/* Import context */
struct wp4_ctx
{
    FILE *fp;

    /* Document text being accumulated as UTF-8 */
    char *text;
    size_t text_len;
    size_t text_cap;

    /* Current position in characters */
    int line;
    int col;

    /* Break kind of every finished line, same order as the newlines */
    unsigned char *brks;
    int brk_count;
    int brk_cap;

    /* Open styled run on the current line */
    int run_start;
    unsigned short run_mask;
    unsigned short mask;

    /* Recorded runs */
    struct wp4_run *runs;
    int n_runs;
    int cap_runs;

    /* Current paragraph alignment (EA_ALIGN_*) and per-line values */
    unsigned char para_align;
    unsigned char *para_aligns;
    int n_lines;
    int cap_para_aligns;

    const char *charset;

    char *err;
    size_t errsz;
};

/* WP 4.2 code byte lengths: 0=variable, -1=single-byte, per spec with gates */
static const signed char wp4_mb_len[64] =
    {
        6, 4, 3, 5, 5, 6, 4, 6,         /* C0 margin C1 spacing C2 lmar-rel C3 center C4 align C5 hzone C6 pg#pos C7 pg# */
        8, 42, 3, 6, 4, 3, 4, 3,        /* C8 pg#col C9 tabs CA cond-eop CB font CC tmar CD end-tmar CE topmar CF suppress */
        6, 0, 0, 4, 4, 4, 6, 0,         /* D0 formlen D1 hdr/ftr(var) D2 footnote(var) D3 fn# D4 adv-halfline D5 lpi D6 exttab D7 mathcol(var) */
        4, 4, 4, 4, 0, -1, -1, -1,      /* D8 alignchar D9 lmarrel DA ulmode DB binfeed DC eop(var) DD-DF reserved single */
        -1, 0, 0, 4, 0, 0, 0, 6,        /* E0 res E1 extchar(gate) E2 res E3 E4 E5 E6 E7 (handled/var) */
        -1, -1, -1, -1, -1, -1, -1, -1, /* E8..EF */
        0, 0, 0, 0, 0, 0, 0, 0,         /* F0..F7 mostly variable structures */
        0, 0, 0, 0, 0, 0, 0, -1};       /* F8..FE variable, FF is a bare gate/terminator */

static int wp4_prefix_i(const char *s, const char *p)
{
    size_t pl = strlen(p);
    size_t i;
    size_t sl;

    if (!s || !p)
        return 0;

    sl = strlen(s);

    if (sl < pl)
        return 0;

    for (i = 0; i < pl; i++)
    {
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)p[i]))
            return 0;
    }

    return 1;
}

static int wp4_charset_invalid(const char *cs, char *err, size_t errsz)
{
    int bits = charset_bits(cs);

    if (bits != 8)
    {
        if (err && errsz > 0)
        {
            if (bits == 0)
                snprintf(err, errsz, "charset %s is multi-byte; WP 4.2 requires an 8-bit charset", cs ? cs : "");
            else if (bits == 16 || bits == 32)
                snprintf(err, errsz, "charset %s is %d-bit; WP 4.2 requires an 8-bit charset", cs ? cs : "", bits);
            else
                snprintf(err, errsz, "WP 4.2 requires an 8-bit charset; select one in setup");
        }

        return 1;
    }

    return 0;
}

static void wp4_seterr(struct wp4_ctx *c, long off, const char *msg, int code)
{
    if (!c->err || c->errsz == 0)
        return;

    if (code >= 0)
        snprintf(c->err, c->errsz, "offset %ld: %s 0x%02X", off, msg, (unsigned)code);
    else
        snprintf(c->err, c->errsz, "offset %ld: %s", off, msg);
}

static int wp4_text_reserve(struct wp4_ctx *c, size_t n)
{
    char *nt = NULL;
    size_t nc;

    if (c->text_len + n + 1 <= c->text_cap)
        return 0;

    nc = c->text_cap > 0 ? c->text_cap * 2 : 4096;

    while (nc < c->text_len + n + 1)
        nc *= 2;

    nt = (char *)realloc(c->text, nc);

    if (!nt)
        return -1;

    c->text = nt;
    c->text_cap = nc;

    return 0;
}

/* Make sure the per-line alignment array can hold at least n entries */
static int wp4_par_align_reserve(struct wp4_ctx *c, int n)
{
    unsigned char *na = NULL;
    int nc;

    if (n <= c->cap_para_aligns)
        return 0;

    nc = c->cap_para_aligns > 0 ? c->cap_para_aligns * 2 : 32;

    if (nc < n)
        nc = n;

    na = (unsigned char *)realloc(c->para_aligns, (size_t)nc);

    if (!na)
        return -1;

    memset(na + c->cap_para_aligns, 0, (size_t)(nc - c->cap_para_aligns));

    c->para_aligns = na;
    c->cap_para_aligns = nc;

    return 0;
}

/* Store the current paragraph's alignment before the newline */
static int wp4_par_align_store(struct wp4_ctx *c)
{
    if (wp4_par_align_reserve(c, c->line + 1) != 0)
        return -1;

    c->para_aligns[c->line] = c->para_align;

    if (c->line + 1 > c->n_lines)
        c->n_lines = c->line + 1;

    return 0;
}

/* Close the open run if it has any styled width */
static int wp4_flush_run(struct wp4_ctx *c)
{
    struct wp4_run *nr = NULL;
    int nc;

    if (c->col <= c->run_start || c->run_mask == 0)
    {
        c->run_start = c->col;
        c->run_mask = c->mask;
        return 0;
    }

    if (c->n_runs >= c->cap_runs)
    {
        nc = c->cap_runs > 0 ? c->cap_runs * 2 : 32;
        nr = (struct wp4_run *)realloc(c->runs, (size_t)nc * sizeof(struct wp4_run));

        if (!nr)
            return -1;

        c->runs = nr;
        c->cap_runs = nc;
    }

    c->runs[c->n_runs].line = c->line;
    c->runs[c->n_runs].start = c->run_start;
    c->runs[c->n_runs].end = c->col;
    c->runs[c->n_runs].mask = c->run_mask;
    c->n_runs++;

    c->run_start = c->col;
    c->run_mask = c->mask;

    return 0;
}

/* Style bit toggled: close the run under the old state, reopen */
static int wp4_style_edge(struct wp4_ctx *c)
{
    if (c->mask == c->run_mask)
        return 0;

    return wp4_flush_run(c);
}

/* Append UTF-8 bytes counting one character */
static int wp4_put_utf8(struct wp4_ctx *c, const char *u8, size_t n)
{
    if (wp4_text_reserve(c, n) != 0)
        return -1;

    memcpy(c->text + c->text_len, u8, n);

    c->text_len += n;
    c->col++;

    return 0;
}

/* End of paragraph: flush the run, record alignment and emit a newline. Record how the finished line joins to the next one */
static int wp4_brk_push(struct wp4_ctx *c, unsigned char brk)
{
    if (c->brk_count >= c->brk_cap)
    {
        int cap = c->brk_cap > 0 ? c->brk_cap * 2 : 256;
        unsigned char *t = (unsigned char *)realloc(c->brks, (size_t)cap);

        if (!t)
            return -1;

        c->brks = t;
        c->brk_cap = cap;
    }

    c->brks[c->brk_count++] = brk;

    return 0;
}

static int wp4_par_brk(struct wp4_ctx *c, unsigned char brk)
{
    if (wp4_flush_run(c) != 0)
        return -1;

    if (wp4_par_align_store(c) != 0)
        return -1;

    if (wp4_text_reserve(c, 1) != 0)
        return -1;

    if (wp4_brk_push(c, brk) != 0)
        return -1;

    c->text[c->text_len++] = '\n';
    c->line++;
    c->col = 0;
    c->run_start = 0;
    c->run_mask = c->mask;

    /* Alignment is a paragraph attribute; only a hard break clears it so a soft-wrapped line stays inside the same alignment run */
    if (brk == LB_PARA)
        c->para_align = EA_ALIGN_LEFT;

    return 0;
}

static int wp4_par(struct wp4_ctx *c)
{
    return wp4_par_brk(c, LB_PARA);
}

/* Skip uninterpreted multi-byte code data: len>0=fixed bytes, 0=scan to gate, returns 0/-1 */
static int wp4_skip_mb(struct wp4_ctx *c, int code, signed char len)
{
    int i;
    int b;

    if (len > 0)
    {
        /* One gate byte already consumed by the caller */
        for (i = 1; i < len; i++)
        {
            if (fgetc(c->fp) == EOF)
                return -1;
        }

        return 0;
    }

    /* Variable length: bytes run until the closing gate repeats the code */
    for (;;)
    {
        b = fgetc(c->fp);

        if (b == EOF)
            return -1;

        if (b == code)
            return 0;
    }
}

int wp4_import(struct Ed *ed, FILE *fp, const char *charset, char *err, size_t errsz, char *warn, size_t warnsz)
{
    struct wp4_ctx c;
    int ch;
    int ok = 1;
    int i;

    if (err && errsz > 0)
        err[0] = '\0';

    if (warn && warnsz > 0)
        warn[0] = '\0';

    if (!ed || !fp)
        return -1;

    /*if (wp4_charset_invalid(charset, err, errsz))
        return -1;*/

    memset(&c, 0, sizeof(c));

    c.fp = fp;
    c.charset = charset;
    c.err = err;
    c.errsz = errsz;

    while (ok && (ch = fgetc(fp)) != EOF)
    {
#define AT() (ftell(fp) - 1)

        /* Plain ASCII text */
        if (ch >= 0x20 && ch <= 0x7E)
        {
            char b = (char)ch;

            ok = (wp4_put_utf8(&c, &b, 1) == 0);
            continue;
        }

        switch (ch)
        {
        case 0x09:
        {
            char b = '\t';

            ok = (wp4_put_utf8(&c, &b, 1) == 0);
            break;
        }

        case 0x0A: /* Hard return: real end of paragraph */
        case 0x0C: /* Hard page: no pages here, closest is a paragraph */
            ok = (wp4_par(&c) == 0);
            break;

        case 0x0D: /* Soft return: line break inside the paragraph */
        case 0x0B: /* Soft page: same, WP pagination is not content */
            ok = (wp4_par_brk(&c, LB_SPACE) == 0);
            break;

        case 0x9D:
            c.mask |= EA_BOLD;
            ok = (wp4_style_edge(&c) == 0);
            break;
        case 0x9C:
            c.mask &= (unsigned short)~EA_BOLD;
            ok = (wp4_style_edge(&c) == 0);
            break;
        case 0x94:
            c.mask |= EA_UNDERLINE;
            ok = (wp4_style_edge(&c) == 0);
            break;
        case 0x95:
            c.mask &= (unsigned short)~EA_UNDERLINE;
            ok = (wp4_style_edge(&c) == 0);
            break;
        case 0xB2:
            c.mask |= EA_ITALIC;
            ok = (wp4_style_edge(&c) == 0);
            break;
        case 0xB3:
            c.mask &= (unsigned short)~EA_ITALIC;
            ok = (wp4_style_edge(&c) == 0);
            break;

        case 0x81: /* Full justification (WP 4.2 DOS spec) */
            c.para_align = EA_ALIGN_JUST;
            break;
        case 0x82: /* Left justification */
            c.para_align = EA_ALIGN_LEFT;
            break;
        case 0x86: /* Full justification (WP 4.1 Amiga variant of 0x81) */
            c.para_align = EA_ALIGN_JUST;
            break;

        case 0xC3: /* Center the following text: <C3><type><center col><start col><C3> */
        {
            int type = fgetc(fp);
            int center_col = fgetc(fp);
            int start_col = fgetc(fp);
            int close = fgetc(fp);

            if (type == EOF || center_col == EOF || start_col == EOF || close == EOF)
            {
                wp4_seterr(&c, AT(), "truncated center alignment gate", -1);
                ok = 0;
                break;
            }

            if (close != 0xC3)
            {
                wp4_seterr(&c, AT(), "unterminated center alignment gate", -1);
                ok = 0;
                break;
            }

            c.para_align = EA_ALIGN_CENTER;
            break;
        }

        case 0xC4: /* Align or flush right: <C4><align char><align col><start col><C4> */
        {
            int align_char = fgetc(fp);
            int align_col = fgetc(fp);
            int start_col = fgetc(fp);
            int close = fgetc(fp);

            if (align_char == EOF || align_col == EOF || start_col == EOF || close == EOF)
            {
                wp4_seterr(&c, AT(), "truncated right alignment gate", -1);
                ok = 0;
                break;
            }

            if (close != 0xC4)
            {
                wp4_seterr(&c, AT(), "unterminated right alignment gate", -1);
                ok = 0;
                break;
            }

            c.para_align = EA_ALIGN_RIGHT;
            break;
        }

        case 0xE1: /* Extended character gate: E1 <charset byte> E1 */
        {
            int b = fgetc(fp);
            int close = fgetc(fp);
            char raw;
            char u8[8];
            int n;

            if (b == EOF || close == EOF)
            {
                wp4_seterr(&c, AT(), "truncated extended character gate", -1);
                ok = 0;
                break;
            }

            if (close != 0xE1)
            {
                wp4_seterr(&c, AT(), "unterminated extended character gate", -1);
                ok = 0;
                break;
            }

            raw = (char)b;
            n = charset_to_utf8(c.charset, &raw, 1, u8, (int)sizeof(u8));

            if (n <= 0)
            {
                wp4_seterr(&c, AT(), "unmappable extended character", b);
                ok = 0;
                break;
            }

            ok = (wp4_put_utf8(&c, u8, strlen(u8)) == 0);
            break;
        }
        case 0x80: /* No-op (always deleted) */
            break;
        case 0xA0: /* Hard space */
        {
            char b = ' ';
            ok = (wp4_put_utf8(&c, &b, 1) == 0);
            break;
        }
        case 0xA9: /* Hard hyphen in line: a real, always-visible hyphen */
        case 0xAA: /* Hard hyphen at end of line */
        case 0xAB: /* Hard hyphen at end of page */
        {
            char b = '-';

            ok = (wp4_put_utf8(&c, &b, 1) == 0);
            break;
        }

        case 0xAC: /* Soft hyphen inside the line: invisible, we re-hyphenate */
            break;

        case 0xAD: /* Soft hyphen at end of line: our hyphen break exactly */
        case 0xAE: /* Soft hyphen at end of page */
            ok = (wp4_par_brk(&c, LB_HYPHEN) == 0);
            break;

        case 0x9A: /* Cancel hyphenation of following word: no text */
        case 0x9E: /* Hyphenation off */
        case 0x9F: /* Hyphenation on */
            break;

        default:
            /* Single-byte layout/function code with no data: ignore */
            if (ch >= 0x80 && ch <= 0xBF)
                break;

            /* Consume multi-byte data to keep stream aligned on readable docs */
            if (ch >= 0xC0 && ch <= 0xFF)
            {
                signed char len = wp4_mb_len[ch - 0xC0];

                if (len == -1)
                    break;

                if (wp4_skip_mb(&c, ch, len) != 0)
                {
                    wp4_seterr(&c, AT(), "truncated multi-byte function", ch);
                    ok = 0;
                }

                break;
            }

            wp4_seterr(&c, AT(), "unsupported WP 4.2 code", ch);
            ok = 0;
            break;
        }
    }
#undef AT

    /* Close a trailing unterminated paragraph */
    if (ok && (c.col > 0 || c.line == 0))
        ok = (wp4_flush_run(&c) == 0);

    if (ok)
    {
        if (wp4_par_align_reserve(&c, c.line + 1) != 0)
        {
            ok = 0;
        }
        else
        {
            c.para_aligns[c.line] = c.para_align;
            c.n_lines = c.line + 1;
        }
    }

    if (ok)
        ok = (wp4_text_reserve(&c, 1) == 0);

    if (ok)
    {
        EdInfo info;
        int i;

        c.text[c.text_len] = '\0';

        ed_load(ed, c.text);

        /* Stamp the breaks so the painter draws hyphens and reflow rejoins */
        ed_get_info(ed, &info);

        for (i = 0; i < c.brk_count && i < info.line_count - 1; i++)
            ed_line_set_break(ed, i, (int)c.brks[i]);

        for (i = 0; i < c.n_runs; i++)
        {
            if (c.runs[i].line >= ed->count)
                continue;

            ed_attr_line_apply(ed->lines[c.runs[i].line], c.runs[i].start, c.runs[i].end, c.runs[i].mask, 0, -1, 0);
        }

        for (i = 0; i < c.n_lines && i < ed->count; i++)
            ed->lines[i]->para_align = c.para_aligns[i];
    }

    free(c.text);
    free(c.brks);
    free(c.runs);
    free(c.para_aligns);

    return ok ? 0 : -1;
}

/* Emit style toggles between two masks - order matches Amiga WP 4.2: Bold, Italic, Underline */
static int wp4_toggles(FILE *fp, unsigned short cur, unsigned short want)
{
    if ((want & EA_BOLD) != (cur & EA_BOLD))
    {
        if (fputc(want & EA_BOLD ? 0x9D : 0x9C, fp) == EOF)
            return -1;
    }

    if ((want & EA_ITALIC) != (cur & EA_ITALIC))
    {
        if (fputc(want & EA_ITALIC ? 0xB2 : 0xB3, fp) == EOF)
            return -1;
    }

    if ((want & EA_UNDERLINE) != (cur & EA_UNDERLINE))
    {
        if (fputc(want & EA_UNDERLINE ? 0x94 : 0x95, fp) == EOF)
            return -1;
    }

    return 0;
}

int wp4_export(const struct Ed *ed, FILE *fp, const char *charset, char *err, size_t errsz, char *warn, size_t warnsz)
{
    unsigned short cur;
    int dropped_layout = 0;
    int lost_chars = 0;
    int row;
    int i;
    int prev_align = EA_ALIGN_LEFT;
    int align;

    if (err && errsz > 0)
        err[0] = '\0';

    if (warn && warnsz > 0)
        warn[0] = '\0';

    if (!ed || !fp)
        return -1;

    if (wp4_charset_invalid(charset, err, errsz))
        return -1;

    for (row = 0; row < ed->count; row++)
    {
        const EdLine *ln = ed->lines[row];
        const EdAttrRun *runs = NULL;
        int n_runs = ed_attr_runs(ln, &runs);
        int r = 0;

        cur = 0;
        align = (int)ln->para_align;

        if (prev_align == EA_ALIGN_JUST && align != EA_ALIGN_JUST)
        {
            if (fputc(0x82, fp) == EOF)
                return -1;
        }

        if (prev_align != EA_ALIGN_JUST && align == EA_ALIGN_JUST)
        {
            /* 0x86 is the WP 4.1 Amiga justify-on marker, emitted at the transition only, matching how real WP 4.1 files are laid out */
            if (fputc(0x86, fp) == EOF)
                return -1;
        }

        if (align == EA_ALIGN_CENTER)
        {
            /* <C3><type><center col><start col><C3> - type=0, center col=42, start col=left margin */
            if (fputc(0xC3, fp) == EOF || fputc(0x00, fp) == EOF || fputc(WP4_CENTER_COL, fp) == EOF || fputc(WP4_DEFAULT_LEFT_MARGIN, fp) == EOF || fputc(0xC3, fp) == EOF)
                return -1;
        }
        else if (align == EA_ALIGN_RIGHT)
        {
            /* <C4><align char><align col><start col><C4> - 0x0A hard new line, align col=75, start col=left margin */
            if (fputc(0xC4, fp) == EOF || fputc(0x0A, fp) == EOF || fputc(WP4_DEFAULT_RIGHT_MARGIN, fp) == EOF || fputc(WP4_DEFAULT_LEFT_MARGIN, fp) == EOF || fputc(0xC4, fp) == EOF)
                return -1;
        }

        for (i = 0; i < ln->len; i++)
        {
            unsigned short want = 0;
            unsigned long cp = ed_line_char(ln, i);
            unsigned char ascii;
            char u8[8];
            char raw[2];
            int n;

            /* Convert typographic quotes and dashes to plain ASCII before encoding */
            ascii = utf8_quote_ascii_fallback((uint32_t)cp);

            if (ascii)
                cp = ascii;

            ascii = utf8_dash_ascii_fallback((uint32_t)cp);

            if (ascii)
                cp = ascii;

            /* Advance to the run covering this column, if any */
            while (r < n_runs && runs[r].end <= i)
                r++;

            if (r < n_runs && i >= runs[r].start && i < runs[r].end)
            {
                want = runs[r].mask;

                if (runs[r].font_id >= 0 || runs[r].size > 0)
                    dropped_layout = 1;
            }

            if (want != cur)
            {
                if (wp4_toggles(fp, cur, want) != 0)
                    return -1;

                cur = want;
            }

            if (cp == '\t')
            {
                if (fputc(0x09, fp) == EOF)
                    return -1;

                continue;
            }

            if (cp >= 0x20 && cp <= 0x7E)
            {
                if (fputc((int)cp, fp) == EOF)
                    return -1;

                continue;
            }

            /* Emit one target-charset byte inside the 0xE1 gate; '?' means no mapping */
            n = utf8_encode((uint32_t)cp, u8);

            if (n <= 0 || utf8_to_charset(charset, u8, n, raw, (int)sizeof(raw)) != 1 || raw[0] == '?')
            {
                lost_chars++;

                if (fputc('?', fp) == EOF)
                    return -1;

                continue;
            }

            if (raw[0] >= 0x20 && raw[0] <= 0x7E)
            {
                if (fputc((unsigned char)raw[0], fp) == EOF)
                    return -1;

                continue;
            }

            if (fputc(0xE1, fp) == EOF || fputc((unsigned char)raw[0], fp) == EOF || fputc(0xE1, fp) == EOF)
                return -1;
        }

        /* Close any open style before the paragraph mark */
        if (cur != 0)
        {
            if (wp4_toggles(fp, cur, 0) != 0)
                return -1;
        }

        /* Close paragraph alignment wrappers */
        if (align == EA_ALIGN_CENTER)
        {
            if (fputc(0x83, fp) == EOF)
                return -1;
        }
        else if (align == EA_ALIGN_RIGHT)
        {
            if (fputc(0x84, fp) == EOF)
                return -1;
        }

        /* The break kind picks the WP code, wrapped words survive the trip */
        if (ln->brk == LB_HYPHEN && row < ed->count - 1)
        {
            if (fputc(0xAD, fp) == EOF)
                return -1;
        }
        else if (ln->brk == LB_SPACE && row < ed->count - 1)
        {
            if (fputc(0x0D, fp) == EOF)
                return -1;
        }
        else if (ln->brk != LB_WORD || row == ed->count - 1)
        {
            if (fputc(0x0A, fp) == EOF)
                return -1;
        }

        prev_align = align;
    }

    if (warn && warnsz > 0)
    {
        if (lost_chars > 0 && dropped_layout)
            snprintf(warn, warnsz, "%d characters could not be represented; font and size are not written", lost_chars);
        else if (lost_chars > 0)
            snprintf(warn, warnsz, "%d characters could not be represented", lost_chars);
        else if (dropped_layout)
            snprintf(warn, warnsz, "font and size are not written by the WP 4.2 exporter");
    }

    return 0;
}
