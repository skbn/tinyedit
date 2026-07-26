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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor.h"
#include "ed_attr.h"
#include "layout.h"
#include "config.h"
#include "fmt_pcl.h"
#include "../core/utf8.h"
#include "../core/charset.h"
#include "../core/portable.h"

/* Page geometry defaults: A4 (210x297mm), 1" margins, 10pt, 12pt leading at 6 LPI */
#define PCL_DEFAULT_PAGE_W_IN (210.0 / 25.4)
#define PCL_DEFAULT_PAGE_H_IN (297.0 / 25.4)
#define PCL_MARGIN_IN 1.0
#define PCL_FONT_SIZE_PT 10.0
#define PCL_LEADING_MUL 1.2 /* Line height = size * 1.2 */
#define PCL_CPI 10          /* Characters per inch for Courier fixed-pitch */

/* PCL paper codes for Esc&l#A, indexed by media keyword */
#define PCL_PAPER_LETTER 2
#define PCL_PAPER_LEGAL 3
#define PCL_PAPER_A3 27
#define PCL_PAPER_A4 26
#define PCL_PAPER_A5 25

/* Paragraph model: chars with attribute masks, alignment, forced break points */
typedef struct
{
    unsigned int cp;
    unsigned short mask;
} pcl_wchar;

typedef struct
{
    pcl_wchar *chars;
    int len;
    int cap;
    unsigned char align; /* EA_ALIGN_* from the paragraph's first EdLine */
    int has_content;

    /* Forced break points: char offsets where the editor split a line. breaks_pos[] are offsets into chars[], breaks_hyph[] is 1 for LB_HYPHEN, 0 for LB_WORD */
    int *breaks_pos;
    unsigned char *breaks_hyph;
    int n_breaks;
    int cap_breaks;
} pcl_para;

/* Pager: page management, paragraph wrapping, alignment, PCL emission */
typedef struct
{
    FILE *fp;
    const char *cs; /* Output charset name */
    int *warned;    /* Pointer to the lossy flag in pcl_export_ex */
    char *warn;     /* Lossy warning buffer */
    size_t warnsz;

    /* Page geometry in decipoints (1/720 inch), initialised from cfg->print_media and cfg->print_orientation */
    double page_w_dp;
    double page_h_dp;
    double margin_l_dp;
    double margin_r_dp;
    double margin_t_dp;
    double margin_b_dp;

    /* Font metrics derived from font_size_pt and PCL_CPI, all in decipoints */
    double font_size_pt;
    double leading_dp;
    double char_w_dp;  /* Advance of one Courier cell */
    double space_w_dp; /* Same as char_w_dp for Courier */
    int tab_width;
    int lpi; /* Lines per inch for Esc&l#D, derived from leading */

    /* Cursor state in decipoints, y_baseline tracks the current line baseline */
    double y_baseline;
    int in_page;
    int lines_on_page;
    int lines_per_page;

    /* Optional hyphenator. NULL disables wrap-hyphenation */
    LayoutHyphenFn hyph;
    void *hyph_user;
} pcl_pager;

static int pcl_seterr(char *err, size_t errsz, const char *msg)
{
    if (err && errsz > 0)
    {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }

    return -1;
}

/* Pick the PCL paper code for Esc&l#A from a media name like iso_a4_210x297mm */
static int pcl_paper_code(const char *media)
{
    if (!media || !media[0])
        return PCL_PAPER_A4;

    if (strstr(media, "a5"))
        return PCL_PAPER_A5;

    if (strstr(media, "a4"))
        return PCL_PAPER_A4;

    if (strstr(media, "a3"))
        return PCL_PAPER_A3;

    if (strstr(media, "letter"))
        return PCL_PAPER_LETTER;

    if (strstr(media, "legal"))
        return PCL_PAPER_LEGAL;

    return PCL_PAPER_A4;
}

/* Parse "name_WxHunit" into inches. Returns 0 on success, -1 on fallback to Letter */
static int pcl_parse_media(const char *media, double *out_w, double *out_h)
{
    const char *x = NULL;
    const char *start = NULL;
    double tw = 0.0;
    double th = 0.0;

    if (!media || !media[0])
    {
        *out_w = PCL_DEFAULT_PAGE_W_IN;
        *out_h = PCL_DEFAULT_PAGE_H_IN;

        return 0;
    }

    /* Format: "name_WxHunit", e.g. "iso_a4_210x297mm", "na_letter_8.5x11in" */
    x = strrchr(media, 'x');

    if (!x || x == media)
    {
        *out_w = PCL_DEFAULT_PAGE_W_IN;
        *out_h = PCL_DEFAULT_PAGE_H_IN;

        return 0;
    }

    /* Parse width: scan backwards from 'x' to the last '_' or start */
    start = x;

    while (start > media && start[-1] != '_')
        start--;

    if (start >= x)
    {
        *out_w = PCL_DEFAULT_PAGE_W_IN;
        *out_h = PCL_DEFAULT_PAGE_H_IN;

        return 0;
    }

    tw = pf_atof_dot(start, NULL);
    th = pf_atof_dot(x + 1, NULL);

    if (tw <= 0.0 || th <= 0.0)
    {
        *out_w = PCL_DEFAULT_PAGE_W_IN;
        *out_h = PCL_DEFAULT_PAGE_H_IN;

        return 0;
    }

    /* Unit suffix: mm -> /25.4, in -> 1, anything else -> Letter fallback */
    if (strstr(media, "mm"))
    {
        tw /= 25.4;
        th /= 25.4;
    }
    else if (!strstr(media, "in"))
    {
        *out_w = PCL_DEFAULT_PAGE_W_IN;
        *out_h = PCL_DEFAULT_PAGE_H_IN;

        return 0;
    }

    *out_w = tw;
    *out_h = th;

    return 0;
}

/* Initialise page geometry from config: media size + orientation + margins. Units are decipoints (1/720 inch) */
static void pcl_pager_init_geometry(pcl_pager *p, const TeConfig *cfg)
{
    double pw = PCL_DEFAULT_PAGE_W_IN;
    double ph = PCL_DEFAULT_PAGE_H_IN;
    double m = PCL_MARGIN_IN;
    double t;

    if (cfg)
        pcl_parse_media(cfg->print_media, &pw, &ph);

    /* Orientation: 3=portrait 4=landscape 5=rev-land 6=rev-port. For landscape variants, swap width and height */
    if (cfg && (cfg->print_orientation == 4 || cfg->print_orientation == 5))
    {
        t = pw;
        pw = ph;
        ph = t;
    }

    p->page_w_dp = pw * 720.0;
    p->page_h_dp = ph * 720.0;
    p->margin_l_dp = m * 720.0;
    p->margin_r_dp = m * 720.0;
    p->margin_t_dp = m * 720.0;
    p->margin_b_dp = m * 720.0;
}

/* Paragraph model (ported from fmt_pdf.c / fmt_urf.c, adapted for PCL output) */
static void pcl_para_init(pcl_para *p)
{
    memset(p, 0, sizeof(*p));
}

static void pcl_para_free(pcl_para *p)
{
    free(p->chars);
    free(p->breaks_pos);
    free(p->breaks_hyph);

    memset(p, 0, sizeof(*p));
}

static void pcl_para_reset(pcl_para *p)
{
    p->len = 0;
    p->align = EA_ALIGN_LEFT;
    p->has_content = 0;
    p->n_breaks = 0;
}

static int pcl_para_push(pcl_para *p, unsigned int cp, unsigned short mask)
{
    if (p->len >= p->cap)
    {
        int nc;
        pcl_wchar *nb = NULL;

        nc = p->cap ? p->cap * 2 : 128;
        nb = (pcl_wchar *)realloc(p->chars, (size_t)nc * sizeof(*nb));

        if (!nb)
            return -1;

        p->chars = nb;
        p->cap = nc;
    }

    p->chars[p->len].cp = cp;
    p->chars[p->len].mask = mask;
    p->len++;

    return 0;
}

static int pcl_para_add_break(pcl_para *p, int is_hyphen)
{
    if (p->n_breaks >= p->cap_breaks)
    {
        int nc;
        int *np = NULL;
        unsigned char *nh = NULL;

        nc = p->cap_breaks ? p->cap_breaks * 2 : 16;

        np = (int *)realloc(p->breaks_pos, (size_t)nc * sizeof(*np));

        if (!np)
            return -1;

        nh = (unsigned char *)realloc(p->breaks_hyph, (size_t)nc * sizeof(*nh));

        if (!nh)
        {
            p->breaks_pos = np;
            return -1;
        }

        p->breaks_pos = np;
        p->breaks_hyph = nh;
        p->cap_breaks = nc;
    }

    p->breaks_pos[p->n_breaks] = p->len;
    p->breaks_hyph[p->n_breaks] = (unsigned char)(is_hyphen ? 1 : 0);
    p->n_breaks++;

    return 0;
}

static int pcl_para_find_break(const pcl_para *p, int start)
{
    int i;

    for (i = 0; i < p->n_breaks; i++)
    {
        if (p->breaks_pos[i] > start)
            return i;
    }

    return -1;
}

static int pcl_para_append_edline(pcl_para *p, const EdLine *ln, const EdAttrRun *runs, int n_runs)
{
    int i;
    int r;

    r = 0;

    for (i = 0; i < ln->len; i++)
    {
        unsigned int cp;
        unsigned short mask;

        cp = ed_line_char(ln, i);

        mask = 0;

        while (r < n_runs && runs[r].end <= i)
            r++;

        if (r < n_runs && i >= runs[r].start && i < runs[r].end)
            mask = runs[r].mask;

        if (pcl_para_push(p, cp, mask) != 0)
            return -1;
    }

    return 0;
}

/* Advance of one character in decipoints at the current column. Tabs expand to the next tab_width boundary */
static double pcl_char_advance(const pcl_pager *pg, int col, unsigned int cp)
{
    int spaces;

    if (cp == '\t')
    {
        spaces = pg->tab_width - (col % pg->tab_width);

        if (spaces <= 0)
            spaces = pg->tab_width;

        return (double)spaces * pg->char_w_dp;
    }

    if (cp < 0x20 || cp == 0x7F)
        return pg->space_w_dp;

    /* Courier is fixed-pitch: every printable glyph has the same advance */
    return pg->char_w_dp;
}

/* Measure the decipoint width of para chars [start, end) */
static double pcl_measure_range(const pcl_para *p, const pcl_pager *pg, int start, int end)
{
    double w;
    int col;
    int i;

    if (end > p->len)
        end = p->len;

    w = 0.0;
    col = 0;

    for (i = start; i < end; i++)
    {
        unsigned int cp = p->chars[i].cp;
        double adv;

        adv = pcl_char_advance(pg, col, cp);
        w += adv;

        /* Advance col by the actual number of cells consumed, not just 1. A tab advances to the next tab stop (tab_width cells), not 1 cell */
        if (cp == '\t')
            col += (int)(adv / pg->char_w_dp + 0.5);
        else
            col++;
    }

    return w;
}

/* Find the longest wrapping substring that fits max_w decipoints, optionally hyphenating the overflow */
static int pcl_wrap_next(const pcl_para *p, const pcl_pager *pg, int start, double max_w, LayoutHyphenFn hyph, void *hyph_user, int *out_hyphenated)
{
    double w;
    int col;
    int last_space_at;
    int i;

    if (out_hyphenated)
        *out_hyphenated = 0;

    w = 0.0;
    col = 0;
    last_space_at = -1;

    for (i = start; i < p->len; i++)
    {
        unsigned int cp = p->chars[i].cp;
        double gw = pcl_char_advance(pg, col, cp);

        if (w + gw > max_w && i > start)
        {
            /* Overflow: try hyphenation before falling back to space-break or hard cut */
            if (hyph)
            {
                int word_start = i;
                int word_end = i;
                int wlen;
                int k;

                while (word_start > start && p->chars[word_start - 1].cp != ' ' && p->chars[word_start - 1].cp != '\t')
                    word_start--;

                while (word_end < p->len && p->chars[word_end].cp != ' ' && p->chars[word_end].cp != '\t')
                    word_end++;

                /* Skip leading punctuation so min_word counts real letters */
                while (word_start < word_end - 1 &&
                       (p->chars[word_start].cp == 0xBF || /* ¿ */
                        p->chars[word_start].cp == 0xA1 || /* ¡ */
                        p->chars[word_start].cp == '(' ||
                        p->chars[word_start].cp == '[' ||
                        p->chars[word_start].cp == '{' ||
                        p->chars[word_start].cp == '"' ||
                        p->chars[word_start].cp == '\''))
                    word_start++;

                wlen = word_end - word_start;

                /* Match layout_paragraph defaults: min_word=5 */
                if (wlen >= 5)
                {
                    wchar_t wbuf[128];
                    int splits[64];
                    int nsplits;
                    double hy_width = pg->char_w_dp; /* '-' is one Courier cell */
                    double word_prefix_w = 0.0;
                    int cap = (int)(sizeof(wbuf) / sizeof(wbuf[0])) - 1;
                    int j;
                    int pcol;

                    if (wlen > cap)
                        wlen = cap;

                    for (k = 0; k < wlen; k++)
                        wbuf[k] = (wchar_t)p->chars[word_start + k].cp;

                    wbuf[wlen] = 0;

                    nsplits = hyph(hyph_user, wbuf, wlen, splits, (int)(sizeof(splits) / sizeof(splits[0])));

                    /* Width already accumulated for range [start, word_start) */
                    pcol = 0;

                    for (j = start; j < word_start; j++)
                    {
                        word_prefix_w += pcl_char_advance(pg, pcol, p->chars[j].cp);
                        pcol++;
                    }

                    /* Try the largest split that still fits (with the trailing hyphen) */
                    for (k = nsplits - 1; k >= 0; k--)
                    {
                        int split = splits[k];
                        double frag_w = 0.0;
                        int fcol = 0;
                        int j;

                        /* Match layout_paragraph: min_left=2, min_right=3 */
                        if (split < 2 || wlen - split < 3)
                            continue;

                        for (j = 0; j < split; j++)
                        {
                            frag_w += pcl_char_advance(pg, fcol, p->chars[word_start + j].cp);
                            fcol++;
                        }

                        if (word_prefix_w + frag_w + hy_width <= max_w)
                        {
                            if (out_hyphenated)
                                *out_hyphenated = 1;

                            return word_start + split;
                        }
                    }
                }
            }

            /* Fall back: last space, else hard cut */
            if (last_space_at >= start)
                return last_space_at + 1;

            return i;
        }

        w += gw;

        /* Advance col by the actual number of cells consumed, not just 1 */
        if (cp == '\t')
            col += (int)(gw / pg->char_w_dp + 0.5);
        else
            col++;

        if (cp == ' ')
            last_space_at = i;
    }

    return p->len;
}

/* Emit one character to the PCL stream, mapping UTF-8 codepoints to the output charset */
static int pcl_emit_char(FILE *fp, unsigned int cp, const char *cs, int *warned, char *warn, size_t warnsz)
{
    char u8[8];
    char b[8];
    int nu;
    int nb;
    int i;

    if (cp < 0x20)
    {
        /* Control characters are not emitted as text, the caller handles tabs */
        if (fputc('?', fp) == EOF)
            return -1;

        return 0;
    }

    if (cp < 0x80)
    {
        if (fputc((int)cp, fp) == EOF)
            return -1;

        return 0;
    }

    nu = utf8_encode(cp, u8);

    if (nu <= 0)
    {
        if (fputc('?', fp) == EOF)
            return -1;

        return 0;
    }

    nb = utf8_to_charset(cs, u8, nu, b, (int)sizeof(b));

    if (nb <= 0)
    {
        if (fputc('?', fp) == EOF)
            return -1;

        if (!(*warned) && warn && warnsz > 0)
        {
            strncpy(warn, "some characters could not be encoded", warnsz - 1);
            warn[warnsz - 1] = '\0';

            *warned = 1;
        }

        return 0;
    }

    for (i = 0; i < nb; i++)
    {
        if (fputc((int)(unsigned char)b[i], fp) == EOF)
            return -1;
    }

    return 0;
}

/* Emit attribute on/off sequences when the mask changes between runs */
static int pcl_emit_attr(FILE *fp, unsigned short prev, unsigned short cur)
{
    int prev_b = (prev & EA_BOLD) != 0;
    int prev_i = (prev & EA_ITALIC) != 0;
    int prev_u = (prev & EA_UNDERLINE) != 0;
    int cur_b = (cur & EA_BOLD) != 0;
    int cur_i = (cur & EA_ITALIC) != 0;
    int cur_u = (cur & EA_UNDERLINE) != 0;

    /* Bold: Esc(s3B on, Esc(s0B off */
    if (cur_b && !prev_b)
    {
        if (fputs("\033(s3B", fp) == EOF)
            return -1;
    }
    else if (!cur_b && prev_b)
    {
        if (fputs("\033(s0B", fp) == EOF)
            return -1;
    }

    /* Italic: Esc(s1S on, Esc(s0S off */
    if (cur_i && !prev_i)
    {
        if (fputs("\033(s1S", fp) == EOF)
            return -1;
    }
    else if (!cur_i && prev_i)
    {
        if (fputs("\033(s0S", fp) == EOF)
            return -1;
    }

    /* Underline: Esc&dD on, Esc&d@ off */
    if (cur_u && !prev_u)
    {
        if (fputs("\033&dD", fp) == EOF)
            return -1;
    }
    else if (!cur_u && prev_u)
    {
        if (fputs("\033&d@", fp) == EOF)
            return -1;
    }

    return 0;
}

/* Render para chars [start, end) at column x_dp (decipoints from left edge) with attributes and optional word_space for justify. Esc&a#H is absolute, so for justify we track the running column and reposition after each expanded space */
static int pcl_pager_render_line(pcl_pager *pg, const pcl_para *para, int start, int end, double x_dp, double word_space_dp)
{
    FILE *fp = pg->fp;
    unsigned short cur_mask = 0xFFFF; /* Sentinel so the first char always emits its attr */
    unsigned short prev_mask = 0;
    double cur_x = x_dp;
    int col;
    int i;

    /* Position the cursor at the absolute column in decipoints: Esc&a#H */
    if (fprintf(fp, "\033&a%.0fH", x_dp) < 0)
        return -1;

    col = 0;

    for (i = start; i < end; i++)
    {
        unsigned int cp = para->chars[i].cp;
        unsigned short mask = para->chars[i].mask;
        double gw;

        if (mask != cur_mask)
        {
            if (pcl_emit_attr(fp, prev_mask, mask) != 0)
                return -1;

            prev_mask = mask;
            cur_mask = mask;
        }

        if (cp == '\t')
        {
            int spaces = pg->tab_width - (col % pg->tab_width);
            int s;

            if (spaces <= 0)
                spaces = pg->tab_width;

            for (s = 0; s < spaces; s++)
            {
                if (fputc(' ', fp) == EOF)
                    return -1;
            }

            col += spaces;
            cur_x += (double)spaces * pg->char_w_dp;

            continue;
        }

        if (pcl_emit_char(fp, cp, pg->cs, pg->warned, pg->warn, pg->warnsz) != 0)
            return -1;

        gw = pcl_char_advance(pg, col, cp);
        col++;
        cur_x += gw;

        /* Justify: after a word space, push the cursor forward by word_space_dp before the next char */
        if (word_space_dp > 0.0 && cp == ' ')
        {
            cur_x += word_space_dp;

            if (fprintf(fp, "\033&a%.0fH", cur_x) < 0)
                return -1;
        }
    }

    /* Turn off any active attributes before the line break */
    if (cur_mask != 0)
    {
        if (pcl_emit_attr(fp, prev_mask, 0) != 0)
            return -1;
    }

    if (fputs("\r\n", fp) == EOF)
        return -1;

    return 0;
}

/* Map a charset name to its PCL 5 symbol set ID */
static const char *pcl_symbol_set(const char *cs)
{
    if (!cs || !cs[0])
        return NULL;

    if (strcasecmp(cs, "UTF-8") == 0 || strcasecmp(cs, "UTF8") == 0)
        return NULL;

    if (strcasecmp(cs, "CP1252") == 0 || strcasecmp(cs, "WINDOWS-1252") == 0)
        return "9U";

    if (strcasecmp(cs, "CP437") == 0 || strcasecmp(cs, "IBMPC") == 0 || strcasecmp(cs, "PC-8") == 0)
        return "10U";

    if (strcasecmp(cs, "CP850") == 0)
        return "12U";

    if (strcasecmp(cs, "CP865") == 0)
        return "25U";

    if (strcasecmp(cs, "CP866") == 0)
        return "3R";

    if (strcasecmp(cs, "LATIN-1") == 0 || strcasecmp(cs, "ISO-8859-1") == 0)
        return "0N";

    if (strcasecmp(cs, "LATIN-2") == 0 || strcasecmp(cs, "ISO-8859-2") == 0)
        return "2N";

    /* Roman-8 is the HP default, no need to emit it explicitly */
    return NULL;
}

/* Start a new page: emit the per-page setup (paper, orientation, LPI, margins, font) */
static int pcl_pager_start_page(pcl_pager *pg, int paper)
{
    FILE *fp = pg->fp;
    const char *symset = NULL;

    /* Reset to known state, then paper size, orientation (portrait), LPI */
    if (fprintf(fp, "\033E\033&l%dA\033&l0O\033&l%dD", paper, pg->lpi) < 0)
        return -1;

    /* Top margin in rows at the current LPI and text length in rows, so the printer page-breaks for us */
    if (fprintf(fp, "\033&l%dE\033&l%dF", (int)(pg->margin_t_dp / 720.0 * pg->lpi + 0.5), pg->lines_per_page) < 0)
        return -1;

    /* Left and right margins in decipoints: Esc&a#L and Esc&a#M */
    if (fprintf(fp, "\033&a%.0fL\033&a%.0fM", pg->margin_l_dp, pg->page_w_dp - pg->margin_r_dp) < 0)
        return -1;

    /* Symbol set: Esc(<id>M. Must come before the font selection so the font uses it. Without this, PCL interpreters default to CP437 and accented chars are wrong */
    symset = pcl_symbol_set(pg->cs);

    if (symset)
    {
        if (fprintf(fp, "\033(%sM", symset) < 0)
            return -1;
    }

    /* Courier fixed-pitch: Esc(s0p#h0s0b4099T sets pitch (cpi), upright, medium, Courier */
    if (fprintf(fp, "\033(s0p%dh0s0b4099T", PCL_CPI) < 0)
        return -1;

    /* Font size in points: Esc(s#V */
    if (fprintf(fp, "\033(s%.0fV", pg->font_size_pt) < 0)
        return -1;

    pg->y_baseline = pg->margin_t_dp + pg->font_size_pt * 10.0; /* baseline = top margin + ascent (~ font size) */
    pg->in_page = 1;
    pg->lines_on_page = 0;

    return 0;
}

/* Close the current page with a form feed */
static int pcl_pager_close_page(pcl_pager *pg)
{
    if (!pg->in_page)
        return 0;

    if (fputc('\f', pg->fp) == EOF)
        return -1;

    pg->in_page = 0;

    return 0;
}

/* Ensure there is room for one more line; page-break if needed */
static int pcl_pager_ensure_line(pcl_pager *pg, int paper)
{
    if (!pg->in_page)
        return pcl_pager_start_page(pg, paper);

    if (pg->lines_on_page >= pg->lines_per_page)
    {
        if (pcl_pager_close_page(pg) != 0)
            return -1;

        return pcl_pager_start_page(pg, paper);
    }

    return 0;
}

static int pcl_pager_blank_line(pcl_pager *pg, int paper)
{
    if (pcl_pager_ensure_line(pg, paper) != 0)
        return -1;

    if (fputs("\r\n", pg->fp) == EOF)
        return -1;

    pg->y_baseline += pg->leading_dp;
    pg->lines_on_page++;

    return 0;
}

/* Flush a paragraph: wrap into visual lines, apply alignment, page-break as needed */
static int pcl_pager_emit_para(pcl_pager *pg, const pcl_para *para, int paper)
{
    int start;
    int use_forced;
    double avail_dp;

    if (para->len == 0)
        return pcl_pager_blank_line(pg, paper);

    start = 0;
    use_forced = 1;
    avail_dp = pg->page_w_dp - pg->margin_l_dp - pg->margin_r_dp;

    while (start < para->len)
    {
        int end;
        int trim_end;
        double line_w;
        double x_dp;
        int align;
        int is_last_line;
        int n_spaces;
        int j;
        double word_space_dp = 0.0;
        int hyphenated = 0;
        pcl_wchar saved_char = {0, 0};
        int saved_at = -1;
        int bi;

        /* Try forced break from editor */
        bi = use_forced ? pcl_para_find_break(para, start) : -1;

        if (bi >= 0)
        {
            int fb_end = para->breaks_pos[bi];
            double fb_w = pcl_measure_range(para, pg, start, fb_end);

            if (fb_w <= avail_dp)
            {
                end = fb_end;
                hyphenated = (para->breaks_hyph[bi] == 1);
            }
            else
            {
                use_forced = 0;
                end = pcl_wrap_next(para, pg, start, avail_dp, pg->hyph, pg->hyph_user, &hyphenated);
            }
        }
        else
        {
            end = pcl_wrap_next(para, pg, start, avail_dp, pg->hyph, pg->hyph_user, &hyphenated);
        }

        if (end <= start)
            end = start + 1;

        is_last_line = (end >= para->len);

        /* Trim trailing spaces */
        trim_end = end;

        while (trim_end > start && para->chars[trim_end - 1].cp == ' ')
            trim_end--;

        /* Insert a hyphen at the wrap point so the renderer emits it inline */
        if (hyphenated && end < para->len && trim_end == end)
        {
            pcl_para *mp = (pcl_para *)para;
            unsigned short mask_at = trim_end > start ? mp->chars[trim_end - 1].mask : 0;

            saved_at = end;
            saved_char = mp->chars[end];
            mp->chars[end].cp = '-';
            mp->chars[end].mask = mask_at;
            trim_end = end + 1;
        }

        if (pcl_pager_ensure_line(pg, paper) != 0)
        {
            if (saved_at >= 0)
                ((pcl_para *)para)->chars[saved_at] = saved_char;

            return -1;
        }

        align = (int)para->align;

        if (trim_end > start && align == EA_ALIGN_CENTER)
        {
            line_w = pcl_measure_range(para, pg, start, trim_end);
            x_dp = pg->margin_l_dp + (avail_dp - line_w) * 0.5;

            if (x_dp < pg->margin_l_dp)
                x_dp = pg->margin_l_dp;
        }
        else if (trim_end > start && align == EA_ALIGN_RIGHT)
        {
            line_w = pcl_measure_range(para, pg, start, trim_end);
            x_dp = pg->page_w_dp - pg->margin_r_dp - line_w;

            if (x_dp < pg->margin_l_dp)
                x_dp = pg->margin_l_dp;
        }
        else if (trim_end > start && align == EA_ALIGN_JUST && (!is_last_line || start == 0))
        {
            line_w = pcl_measure_range(para, pg, start, trim_end);
            n_spaces = 0;

            for (j = start; j < trim_end; j++)
            {
                if (para->chars[j].cp == ' ')
                    n_spaces++;
            }

            if (n_spaces > 0 && line_w < avail_dp)
                word_space_dp = (avail_dp - line_w) / (double)n_spaces;

            x_dp = pg->margin_l_dp;
        }
        else
        {
            x_dp = pg->margin_l_dp;
        }

        if (trim_end > start)
        {
            if (pcl_pager_render_line(pg, para, start, trim_end, x_dp, word_space_dp) != 0)
            {
                if (saved_at >= 0)
                    ((pcl_para *)para)->chars[saved_at] = saved_char;

                return -1;
            }
        }
        else
        {
            /* Empty visual line (all spaces trimmed): just emit a line break */
            if (fputs("\r\n", pg->fp) == EOF)
                return -1;
        }

        /* Restore the char we swapped for '-' so the next line reads the original */
        if (saved_at >= 0)
            ((pcl_para *)para)->chars[saved_at] = saved_char;

        pg->y_baseline += pg->leading_dp;
        pg->lines_on_page++;

        start = end;

        /* Skip leading spaces on the next visual line (they were the wrap boundary that got consumed) */
        while (start < para->len && para->chars[start].cp == ' ')
            start++;
    }

    return 0;
}

int pcl_export_ex(const struct Ed *ed, FILE *fp, const TeConfig *cfg, LayoutHyphenFn hyph, void *hyph_user, char *err, size_t errsz, char *warn, size_t warnsz)
{
    pcl_para para;
    pcl_pager pg;
    const char *media = NULL;
    const char *cs = NULL;
    int paper;
    int row;
    int rc;
    int warned = 0;

    if (err && errsz > 0)
        err[0] = '\0';

    if (warn && warnsz > 0)
        warn[0] = '\0';

    if (!ed || !fp || !cfg)
        return pcl_seterr(err, errsz, "invalid arguments");

    media = cfg->print_media[0] ? cfg->print_media : "iso_a4_210x297mm";

    /* PCL 5 is a single-byte format: pick the output charset, falling back to CP1252 when the configured charset is UTF-8 */
    cs = cfg->charset_out[0] ? charset_resolve(cfg->charset_out) : charset_resolve("CP1252");

    if (!cs)
        cs = "CP1252";

    if (strcasecmp(cs, "UTF-8") == 0 || strcasecmp(cs, "UTF8") == 0)
        cs = "CP1252";

    paper = pcl_paper_code(media);

    pcl_para_init(&para);

    memset(&pg, 0, sizeof(pg));

    pg.fp = fp;
    pg.cs = cs;
    pg.warned = &warned;
    pg.warn = warn;
    pg.warnsz = warnsz;
    pg.tab_width = (cfg->tab_width > 0) ? cfg->tab_width : 4;
    pg.font_size_pt = (cfg->print_font_size > 0) ? (double)cfg->print_font_size : PCL_FONT_SIZE_PT;
    pg.leading_dp = pg.font_size_pt * PCL_LEADING_MUL * 10.0; /* points * 10 = decipoints */
    pg.char_w_dp = 720.0 / (double)PCL_CPI;                   /* one Courier cell in decipoints */
    pg.space_w_dp = pg.char_w_dp;
    pg.lpi = (int)(720.0 / pg.leading_dp + 0.5); /* LPI = 72pt / leading_pt */
    pg.hyph = hyph;
    pg.hyph_user = hyph_user;
    pg.in_page = 0;
    pg.lines_on_page = 0;

    pcl_pager_init_geometry(&pg, cfg);

    /* Lines per page from the printable height and the leading */
    pg.lines_per_page = (int)((pg.page_h_dp - pg.margin_t_dp - pg.margin_b_dp) / pg.leading_dp);

    if (pg.lines_per_page < 1)
        pg.lines_per_page = 1;

    rc = 0;

    /* Build paragraphs from consecutive non-empty lines, re-wrapping to page width */
    for (row = 0; row < ed->count && rc == 0; row++)
    {
        const EdLine *ln = NULL;
        const EdAttrRun *runs = NULL;
        int n_runs;
        int brk;
        int is_last;

        ln = ed->lines[row];
        n_runs = ed_attr_runs(ln, &runs);
        is_last = (row == ed->count - 1);
        brk = is_last ? (int)LB_PARA : (int)ln->brk;

        /* Empty line = paragraph break */
        if (ln->len == 0)
        {
            if (para.has_content)
            {
                if (pcl_pager_emit_para(&pg, &para, paper) != 0)
                    rc = -1;

                pcl_para_reset(&para);
            }

            if (rc == 0)
            {
                if (pcl_pager_blank_line(&pg, paper) != 0)
                    rc = -1;
            }

            continue;
        }

        /* First line of a paragraph? Adopt its alignment */
        if (!para.has_content)
        {
            para.align = ln->para_align;
            para.has_content = 1;
        }

        if (pcl_para_append_edline(&para, ln, runs, n_runs) != 0)
        {
            rc = -1;
            break;
        }

        /* Join consecutive non-empty lines within a paragraph */
        if (brk == LB_SPACE)
        {
            if (pcl_para_push(&para, ' ', 0) != 0)
            {
                rc = -1;
                break;
            }
        }
        else if (brk == LB_HYPHEN || brk == LB_WORD)
        {
            if (pcl_para_add_break(&para, brk == LB_HYPHEN ? 1 : 0) != 0)
            {
                rc = -1;
                break;
            }
        }
        else if (brk == LB_PARA)
        {
            if (pcl_pager_emit_para(&pg, &para, paper) != 0)
                rc = -1;

            pcl_para_reset(&para);
        }
    }

    /* Flush any pending paragraph */
    if (rc == 0 && para.has_content)
    {
        if (pcl_pager_emit_para(&pg, &para, paper) != 0)
            rc = -1;
    }

    /* Close the last page (or emit a blank page if document is empty) */
    if (rc == 0)
    {
        if (pg.in_page)
        {
            if (pcl_pager_close_page(&pg) != 0)
                rc = -1;
        }
        else
        {
            /* Document was empty: emit one blank page so the printer ejects a sheet */
            if (pcl_pager_start_page(&pg, paper) != 0)
                rc = -1;
            else if (pcl_pager_close_page(&pg) != 0)
                rc = -1;
        }
    }

    pcl_para_free(&para);

    if (rc != 0)
        return pcl_seterr(err, errsz, "write error");

    return 0;
}

int pcl_export(const struct Ed *ed, FILE *fp, const TeConfig *cfg, char *err, size_t errsz, char *warn, size_t warnsz)
{
    return pcl_export_ex(ed, fp, cfg, NULL, NULL, err, errsz, warn, warnsz);
}
