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
#include "fmt_rtf.h"
#include "../core/utf8.h"

#define RTF_MAX_DEPTH 64
#define RTF_WORD_MAX 32

/* Group-scoped character state */
struct rtf_state
{
    unsigned short mask;
    short font_id;
    short size;
    unsigned char align;
    int uc; /* chars to skip after \uN */
};

/* One recorded styled run, document-wide */
struct rtf_run
{
    int line;
    int start;
    int end;
    unsigned short mask;
    short font_id;
    short size;
};

/* Import context */
struct rtf_ctx
{
    FILE *fp;

    /* Document text being accumulated as UTF-8 */
    char *text;
    size_t text_len;
    size_t text_cap;

    /* Current line state in characters */
    int line;
    int col;

    /* Open styled run on the current line */
    int run_start;
    struct rtf_state run_state;

    /* Recorded runs */
    struct rtf_run *runs;
    int n_runs;
    int cap_runs;

    /* Per-line alignment, grown on demand */
    unsigned char *aligns;
    int n_aligns;
    int cap_aligns;

    /* Group stack */
    struct rtf_state stack[RTF_MAX_DEPTH];
    int depth;
    struct rtf_state st;

    int codepage; /* For \'hh bytes */
    int uc_skip;  /* Pending fallback chars to swallow after \uN */
    int dropped_colors;
    long pending_high; /* Stashed UTF-16 high surrogate from \uN, 0 = none */

    char *err;
    size_t errsz;
};

static void rtf_seterr(struct rtf_ctx *c, long off, const char *msg, const char *detail)
{
    if (!c->err || c->errsz == 0)
        return;

    if (detail)
        snprintf(c->err, c->errsz, "offset %ld: %s '%s'", off, msg, detail);
    else
        snprintf(c->err, c->errsz, "offset %ld: %s", off, msg);
}

static int rtf_text_reserve(struct rtf_ctx *c, size_t n)
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

static int rtf_state_default(const struct rtf_state *s)
{
    return s->mask == 0 && s->font_id < 0 && s->size <= 0;
}

static int rtf_state_same(const struct rtf_state *a, const struct rtf_state *b)
{
    return a->mask == b->mask && a->font_id == b->font_id && a->size == b->size;
}

/* Close the open run if it has any styled width */
static int rtf_flush_run(struct rtf_ctx *c)
{
    struct rtf_run *nr = NULL;
    int nc;

    if (c->col <= c->run_start || rtf_state_default(&c->run_state))
    {
        c->run_start = c->col;
        c->run_state = c->st;
        return 0;
    }

    if (c->n_runs >= c->cap_runs)
    {
        nc = c->cap_runs > 0 ? c->cap_runs * 2 : 32;
        nr = (struct rtf_run *)realloc(c->runs, (size_t)nc * sizeof(struct rtf_run));

        if (!nr)
            return -1;

        c->runs = nr;
        c->cap_runs = nc;
    }

    c->runs[c->n_runs].line = c->line;
    c->runs[c->n_runs].start = c->run_start;
    c->runs[c->n_runs].end = c->col;
    c->runs[c->n_runs].mask = c->run_state.mask;
    c->runs[c->n_runs].font_id = c->run_state.font_id;
    c->runs[c->n_runs].size = c->run_state.size;
    c->n_runs++;

    c->run_start = c->col;
    c->run_state = c->st;

    return 0;
}

/* Style changed: close the run under the old state, reopen under the new */
static int rtf_style_edge(struct rtf_ctx *c)
{
    if (rtf_state_same(&c->st, &c->run_state))
        return 0;

    return rtf_flush_run(c);
}

/* Append one codepoint bypassing the \uN fallback skip */
static int rtf_put_cp_raw(struct rtf_ctx *c, unsigned long cp)
{
    char u8[8];
    int n;

    n = utf8_encode((uint32_t)cp, u8);

    if (n <= 0)
        return 0;

    if (rtf_text_reserve(c, (size_t)n) != 0)
        return -1;

    memcpy(c->text + c->text_len, u8, (size_t)n);
    c->text_len += (size_t)n;
    c->col++;

    return 0;
}

/* Append one codepoint of document text, honouring the fallback skip */
static int rtf_put_cp(struct rtf_ctx *c, unsigned long cp)
{
    if (c->uc_skip > 0)
    {
        c->uc_skip--;
        return 0;
    }

    return rtf_put_cp_raw(c, cp);
}

/* Record the alignment of the line being closed */
static int rtf_put_align(struct rtf_ctx *c)
{
    unsigned char *na = NULL;
    int nc;

    if (c->line >= c->cap_aligns)
    {
        nc = c->cap_aligns > 0 ? c->cap_aligns * 2 : 64;

        while (nc <= c->line)
            nc *= 2;

        na = (unsigned char *)realloc(c->aligns, (size_t)nc);

        if (!na)
            return -1;

        memset(na + c->cap_aligns, 0, (size_t)(nc - c->cap_aligns));
        c->aligns = na;
        c->cap_aligns = nc;
    }

    c->aligns[c->line] = c->st.align;

    if (c->line + 1 > c->n_aligns)
        c->n_aligns = c->line + 1;

    return 0;
}

/* End of paragraph: flush the run, store alignment, emit a newline */
static int rtf_par(struct rtf_ctx *c)
{
    if (rtf_flush_run(c) != 0 || rtf_put_align(c) != 0)
        return -1;

    if (rtf_text_reserve(c, 1) != 0)
        return -1;

    c->text[c->text_len++] = '\n';
    c->line++;
    c->col = 0;
    c->run_start = 0;
    c->run_state = c->st;

    return 0;
}

/* Skip the rest of the current { ... } group, honouring nesting and escapes, and rebalance the outer parser state for its closing brace */
static int rtf_skip_group(struct rtf_ctx *c)
{
    int depth = 1;
    int ch;

    while (depth > 0)
    {
        ch = fgetc(c->fp);

        if (ch == EOF)
            return -1;

        if (ch == '\\')
        {
            if (fgetc(c->fp) == EOF)
                return -1;
        }
        else if (ch == '{')
            depth++;
        else if (ch == '}')
            depth--;
    }

    /* The group's opening brace was already counted by the main loop */
    c->depth--;

    if (c->depth > 0)
        c->st = c->stack[c->depth];

    return 0;
}

/* Control words that carry no information for this editor */
static int rtf_is_ignored(const char *w)
{
    static const char *ign[] =
        {
            "rtf", "ansi", "mac", "pc", "pca", "deff", "deflang", "deflangfe",
            "viewkind", "nouicompat", "fnil", "froman", "fswiss", "fmodern",
            "fscript", "fdecor", "ftech", "fbidi", "fcharset", "fprq",
            "lang", "langfe", "langnp", "noproof", "kerning", "expnd", "expndtw",
            "outl", "shad", "widowctrl", "hyphauto", "sl", "slmult",
            "sa", "sb", "li", "ri", "fi", "cb", "highlight", "nowidctlpar",
            "aspalpha", "aspnum", "faauto", "adjustright", "itap",
            "red", "green", "blue", NULL};

    int i;

    for (i = 0; ign[i]; i++)
    {
        if (strcmp(w, ign[i]) == 0)
            return 1;
    }

    return 0;
}

/* One control word with optional numeric parameter */
static int rtf_control(struct rtf_ctx *c)
{
    char word[RTF_WORD_MAX + 1];
    long at = ftell(c->fp) - 1;
    long num = 0;
    int has_num = 0;
    int neg = 0;
    int wl = 0;
    int ch;

    ch = fgetc(c->fp);

    if (ch == EOF)
    {
        rtf_seterr(c, at, "truncated control", NULL);
        return -1;
    }

    /* Control symbols */
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')))
    {
        switch (ch)
        {
        case '\\':
        case '{':
        case '}':
            return rtf_put_cp(c, (unsigned long)ch);
        case '~':
            return rtf_put_cp(c, 0xA0UL);
        case '-':
            /* Optional hyphen is invisible layout, tinyedit re-hyphenates */
            return 0;
        case '_':
            /* Non breaking hyphen is a real visible character */
            return rtf_put_cp(c, (unsigned long)'-');
        case '*':
            /* Ignorable destination: the group is safe to skip whole */
            return rtf_skip_group(c) == 0 ? 1 : -1;
        case '\'':
        {
            char hex[3];
            unsigned int b;
            char cs[16];
            char u8[8];

            hex[0] = (char)fgetc(c->fp);
            hex[1] = (char)fgetc(c->fp);
            hex[2] = '\0';

            if (sscanf(hex, "%x", &b) != 1)
            {
                rtf_seterr(c, at, "bad hex escape", NULL);
                return -1;
            }

            snprintf(cs, sizeof(cs), "CP%d", c->codepage);

            hex[0] = (char)b;

            if (charset_to_utf8(cs, hex, 1, u8, (int)sizeof(u8)) <= 0)
                return 0;

            if (c->uc_skip > 0)
            {
                c->uc_skip--;
                return 0;
            }

            if (rtf_text_reserve(c, strlen(u8)) != 0)
                return -1;

            memcpy(c->text + c->text_len, u8, strlen(u8));

            c->text_len += strlen(u8);
            c->col++;

            return 0;
        }
        case '\r':
        case '\n':
            return rtf_par(c);
        default:
            rtf_seterr(c, at, "unsupported control symbol", NULL);
            return -1;
        }
    }

    /* Collect the word letters */
    while ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
    {
        if (wl < RTF_WORD_MAX)
            word[wl++] = (char)ch;

        ch = fgetc(c->fp);
    }

    word[wl] = '\0';

    /* Optional numeric parameter */
    if (ch == '-')
    {
        neg = 1;
        ch = fgetc(c->fp);
    }

    while (ch >= '0' && ch <= '9')
    {
        num = num * 10 + (ch - '0');
        has_num = 1;
        ch = fgetc(c->fp);
    }

    if (neg)
        num = -num;

    /* A single space delimiter belongs to the control word */
    if (ch != ' ' && ch != EOF)
        ungetc(ch, c->fp);

    /* Formatting we understand */
    if (strcmp(word, "b") == 0)
    {
        if (has_num && num == 0)
            c->st.mask &= (unsigned short)~EA_BOLD;
        else
            c->st.mask |= EA_BOLD;

        return rtf_style_edge(c);
    }

    if (strcmp(word, "i") == 0)
    {
        if (has_num && num == 0)
            c->st.mask &= (unsigned short)~EA_ITALIC;
        else
            c->st.mask |= EA_ITALIC;

        return rtf_style_edge(c);
    }

    if (strcmp(word, "ul") == 0)
    {
        if (has_num && num == 0)
            c->st.mask &= (unsigned short)~EA_UNDERLINE;
        else
            c->st.mask |= EA_UNDERLINE;

        return rtf_style_edge(c);
    }

    if (strcmp(word, "ulnone") == 0)
    {
        c->st.mask &= (unsigned short)~EA_UNDERLINE;

        return rtf_style_edge(c);
    }

    if (strcmp(word, "plain") == 0)
    {
        c->st.mask = 0;
        c->st.font_id = -1;
        c->st.size = 0;

        return rtf_style_edge(c);
    }

    if (strcmp(word, "f") == 0)
    {
        c->st.font_id = (short)num;
        return rtf_style_edge(c);
    }

    if (strcmp(word, "fs") == 0)
    {
        c->st.size = (short)(num / 2);

        return rtf_style_edge(c);
    }

    if (strcmp(word, "ql") == 0 || strcmp(word, "pard") == 0)
    {
        c->st.align = EA_ALIGN_LEFT;

        return 0;
    }

    if (strcmp(word, "qc") == 0)
    {
        c->st.align = EA_ALIGN_CENTER;

        return 0;
    }

    if (strcmp(word, "qr") == 0)
    {
        c->st.align = EA_ALIGN_RIGHT;

        return 0;
    }

    if (strcmp(word, "qj") == 0)
    {
        c->st.align = EA_ALIGN_JUST;

        return 0;
    }

    if (strcmp(word, "par") == 0 || strcmp(word, "line") == 0)
        return rtf_par(c);

    if (strcmp(word, "tab") == 0)
        return rtf_put_cp(c, (unsigned long)'\t');

    if (strcmp(word, "ansicpg") == 0)
    {
        c->codepage = (int)num;
        return 0;
    }

    if (strcmp(word, "uc") == 0)
    {
        c->st.uc = (int)num;
        return 0;
    }

    if (strcmp(word, "u") == 0)
    {
        long cp = num < 0 ? num + 65536 : num;
        int r = 0;

        /* UTF-16 surrogate pairs arrive as two \uN in a row */
        if (cp >= 0xD800 && cp <= 0xDBFF)
        {
            c->pending_high = cp;
            c->uc_skip += c->st.uc;
            return 0;
        }

        if (cp >= 0xDC00 && cp <= 0xDFFF)
        {
            if (c->pending_high != 0)
            {
                cp = 0x10000 + ((c->pending_high - 0xD800) << 10) + (cp - 0xDC00);
                c->pending_high = 0;
                r = rtf_put_cp_raw(c, (unsigned long)cp);
            }

            c->uc_skip += c->st.uc;
            return r;
        }

        r = rtf_put_cp_raw(c, (unsigned long)cp);
        c->uc_skip += c->st.uc;

        return r;
    }

    /* Table destinations: parse enough of fonttbl to stay aligned with font ids, drop colortbl noting it (index 0 is auto, remember) */
    if (strcmp(word, "fonttbl") == 0 || strcmp(word, "stylesheet") == 0 || strcmp(word, "info") == 0)
        return rtf_skip_group(c) == 0 ? 1 : -1;

    if (strcmp(word, "colortbl") == 0)
    {
        c->dropped_colors = 1;
        return rtf_skip_group(c) == 0 ? 1 : -1;
    }

    if (strcmp(word, "cf") == 0)
    {
        c->dropped_colors = 1;
        return 0;
    }

    if (rtf_is_ignored(word))
        return 0;

    /* RTF standard: unknown control words must be ignored */
    return 0;
}

int rtf_import(struct Ed *ed, FILE *fp, char *err, size_t errsz, char *warn, size_t warnsz)
{
    struct rtf_ctx c;
    int ch;
    int rc = -1;
    int i;
    int ok = 1;

    if (err && errsz > 0)
        err[0] = '\0';

    if (warn && warnsz > 0)
        warn[0] = '\0';

    if (!ed || !fp)
        return -1;

    memset(&c, 0, sizeof(c));

    c.fp = fp;
    c.err = err;
    c.errsz = errsz;
    c.codepage = 1252;
    c.st.font_id = -1;
    c.st.uc = 1;
    c.run_state = c.st;

    ch = fgetc(fp);

    if (ch != '{')
    {
        rtf_seterr(&c, 0, "not an RTF file", NULL);
        return -1;
    }

    c.depth = 1;

    while (c.depth > 0 && ok)
    {
        ch = fgetc(fp);

        if (ch == EOF)
        {
            rtf_seterr(&c, ftell(fp), "unexpected end of file", NULL);
            ok = 0;
        }
        else if (ch == '{')
        {
            if (c.depth >= RTF_MAX_DEPTH)
            {
                rtf_seterr(&c, ftell(fp), "groups nested too deep", NULL);
                ok = 0;
            }
            else if (rtf_style_edge(&c) != 0)
            {
                ok = 0;
            }
            else
            {
                c.stack[c.depth++] = c.st;
            }
        }
        else if (ch == '}')
        {
            c.depth--;

            if (c.depth > 0)
            {
                c.st = c.stack[c.depth];

                if (rtf_style_edge(&c) != 0)
                    ok = 0;
            }
        }
        else if (ch == '\\')
        {
            int r = rtf_control(&c);

            if (r < 0)
                ok = 0;
        }
        else if (ch == '\r' || ch == '\n')
        {
            /* Raw newlines in the file are formatting, not content */
        }
        else
        {
            if (rtf_put_cp(&c, (unsigned long)(unsigned char)ch) != 0)
                ok = 0;
        }
    }

    if (ok)
    {
        /* Close a trailing unterminated paragraph */
        if (c.col > 0 || c.line == 0)
        {
            if (rtf_flush_run(&c) != 0 || rtf_put_align(&c) != 0)
                ok = 0;
        }

        if (ok && rtf_text_reserve(&c, 1) != 0)
            ok = 0;
    }

    if (ok)
    {
        c.text[c.text_len] = '\0';

        ed_load(ed, c.text);

        for (i = 0; i < c.n_runs; i++)
        {
            if (c.runs[i].line >= ed->count)
                continue;

            ed_attr_line_apply(ed->lines[c.runs[i].line], c.runs[i].start, c.runs[i].end, c.runs[i].mask, 0, c.runs[i].font_id, c.runs[i].size);
        }

        for (i = 0; i < c.n_aligns && i < ed->count; i++)
            ed->lines[i]->para_align = c.aligns[i];

        if (c.dropped_colors && warn && warnsz > 0)
            snprintf(warn, warnsz, "color information was dropped (not supported yet)");

        rc = 0;
    }

    free(c.text);
    free(c.runs);
    free(c.aligns);

    return rc;
}

/* Escape and write one codepoint of exported text */
static int rtf_write_cp(FILE *fp, unsigned int cp)
{
    if (cp == '\\' || cp == '{' || cp == '}')
        return fprintf(fp, "\\%c", (char)cp) < 0 ? -1 : 0;

    if (cp == '\t')
        return fputs("\\tab ", fp) == EOF ? -1 : 0;

    if (cp >= 0x20 && cp < 0x80)
        return fputc((int)cp, fp) == EOF ? -1 : 0;

    if (cp < 0x20)
        return 0;

    /* Non-ASCII as \uN with a '?' fallback (uc1 declared in the header) */
    if (cp <= 0x7FFF)
        return fprintf(fp, "\\u%u?", cp) < 0 ? -1 : 0;

    if (cp <= 0xFFFF)
        return fprintf(fp, "\\u%d?", (int)cp - 65536) < 0 ? -1 : 0;

    /* Beyond the BMP: UTF-16 surrogate pair */
    cp -= 0x10000;

    if (fprintf(fp, "\\u%d?", (int)(0xD800 + (cp >> 10)) - 65536) < 0)
        return -1;

    return fprintf(fp, "\\u%d?", (int)(0xDC00 + (cp & 0x3FF)) - 65536) < 0 ? -1 : 0;
}

int rtf_export(const struct Ed *ed, FILE *fp)
{
    static const char *aw[] = {"\\ql", "\\qc", "\\qr", "\\qj"};
    struct rtf_state cur;
    int row;
    int i;
    int at_para_start = 1;

    if (!ed || !fp)
        return -1;

    if (fputs("{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1{\\fonttbl{\\f0\\fnil Default;}}\n", fp) == EOF)
        return -1;

    for (row = 0; row < ed->count; row++)
    {
        const EdLine *ln = ed->lines[row];
        const EdAttrRun *runs = NULL;
        int n_runs = ed_attr_runs(ln, &runs);
        int r = 0;

        memset(&cur, 0, sizeof(cur));
        cur.font_id = -1;

        /* Only set paragraph defaults at the start of a paragraph, mid paragraph continuations keep the current setup */
        if (at_para_start)
        {
            if (fputs("\\pard", fp) == EOF)
                return -1;

            if (fputs(aw[ln->para_align & 3], fp) == EOF)
                return -1;

            if (fputc(' ', fp) == EOF)
                return -1;
        }

        for (i = 0; i < ln->len; i++)
        {
            struct rtf_state want;

            memset(&want, 0, sizeof(want));
            want.font_id = -1;

            /* Advance to the run covering this column, if any */
            while (r < n_runs && runs[r].end <= i)
                r++;

            if (r < n_runs && i >= runs[r].start && i < runs[r].end)
            {
                want.mask = runs[r].mask;
                want.font_id = runs[r].font_id;
                want.size = runs[r].size;
            }

            /* Emit toggles at the style boundary */
            if (want.mask != cur.mask || want.font_id != cur.font_id || want.size != cur.size)
            {
                if ((want.mask & EA_BOLD) != (cur.mask & EA_BOLD))
                    if (fputs(want.mask & EA_BOLD ? "\\b" : "\\b0", fp) == EOF)
                        return -1;

                if ((want.mask & EA_ITALIC) != (cur.mask & EA_ITALIC))
                    if (fputs(want.mask & EA_ITALIC ? "\\i" : "\\i0", fp) == EOF)
                        return -1;

                if ((want.mask & EA_UNDERLINE) != (cur.mask & EA_UNDERLINE))
                    if (fputs(want.mask & EA_UNDERLINE ? "\\ul" : "\\ulnone", fp) == EOF)
                        return -1;

                if (want.font_id != cur.font_id && want.font_id >= 0)
                    if (fprintf(fp, "\\f%d", want.font_id) < 0)
                        return -1;

                if (want.size != cur.size && want.size > 0)
                    if (fprintf(fp, "\\fs%d", want.size * 2) < 0)
                        return -1;

                if (fputc(' ', fp) == EOF)
                    return -1;

                cur = want;
            }

            if (rtf_write_cp(fp, ed_line_char(ln, i)) != 0)
                return -1;
        }

        /* Close any open style before the paragraph mark */
        if (cur.mask & EA_BOLD)
        {
            if (fputs("\\b0", fp) == EOF)
                return -1;
        }

        if (cur.mask & EA_ITALIC)
        {
            if (fputs("\\i0", fp) == EOF)
                return -1;
        }

        if (cur.mask & EA_UNDERLINE)
        {
            if (fputs("\\ulnone", fp) == EOF)
                return -1;
        }

        /* Pick the join, LB_SPACE glues within the paragraph, LB_HYPHEN adds a soft hyphen, LB_PARA closes the paragraph */
        at_para_start = 0;

        if (row < ed->count - 1)
        {
            if (ln->brk == LB_HYPHEN)
            {
                if (fputs("\\-", fp) == EOF)
                    return -1;
            }
            else if (ln->brk == LB_SPACE)
            {
                if (fputc(' ', fp) == EOF)
                    return -1;
            }
            else if (ln->brk != LB_WORD)
            {
                if (fputs("\\par\n", fp) == EOF)
                    return -1;

                at_para_start = 1;
            }
        }
        else
        {
            /* Terminate the final paragraph too so Word does not treat the tail as an unclosed run */
            if (fputs("\\par\n", fp) == EOF)
                return -1;
        }
    }

    return fputs("}\n", fp) == EOF ? -1 : 0;
}
