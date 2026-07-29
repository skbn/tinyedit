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

/* DOCX (Office Open XML) reader/writer via streaming ZIP with minimal XML */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "editor.h"
#include "ed_attr.h"
#include "config.h"
#include "fmt_docx.h"
#include "../core/zip_stream.h"
#include "../core/xml_lite.h"
#include "../core/utf8.h"

#if defined(USE_FREETYPE) || defined(HAVE_PRINTER)
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

/* Amiga uses stored ZIP to save RAM; compatible with all readers */
/*#if defined(PLATFORM_AMIGA)
#define DOCX_WRITE_METHOD ZIP_METHOD_STORED
#else*/
#define DOCX_WRITE_METHOD ZIP_METHOD_DEFLATE

/*#endif*/

/* Reader state passed through the XML callback */
typedef struct
{
    struct Ed *ed;

    /* Current run attribute mask */
    unsigned short cur_mask;

    /* Inside <w:rPr> element */
    int in_rpr;

    /* Inside <w:pPr> element */
    int in_ppr;

    /* Inside <w:t> element */
    int in_t;

    /* Current paragraph alignment */
    unsigned char cur_align;

    /* Run start column for attr flush */
    int run_start_col;

    /* First paragraph flag */
    int first_para;

    /* Paragraph has text flag */
    int para_has_text;

    /* Deferred runs: collected during paragraph, applied at paragraph end */
    unsigned short def_masks[64];
    int def_starts[64];
    int def_ends[64];
    int n_def;

    /* Hyphenation detected in settings.xml during import */
    int hyph_detected;

    /* Page geometry in twips captured from <w:pgMar> and <w:pgSz>, 0 = not seen */
    int pg_margin_left;
    int pg_margin_right;
    int pg_margin_top;
    int pg_margin_bottom;
    int pg_width;
    int pg_height;
} DocxReadCtx;

/* Buffered XML writer with escaping for ZIP entries */
typedef struct
{
    ZipWriter *zw;
    unsigned char buf[512];
    int len;
} DocxWriteCtx;

/* Static XML templates for .docx */
static const char DOCX_CT_TYPES[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
    "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
    "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
    "<Override PartName=\"/word/settings.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml\"/>"
    "</Types>";

static const char DOCX_ROOT_RELS[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
    "</Relationships>";

static const char DOCX_DOC_RELS[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
    "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" Target=\"settings.xml\"/>"
    "</Relationships>";

static const char DOCX_SETTINGS_HYPH[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
    "<w:autoHyphenation w:val=\"true\"/>"
    "<w:hyphenationZone w:val=\"0\"/>"
    "</w:settings>";

static const char DOCX_SETTINGS_PLAIN[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
    "</w:settings>";

static const char DOCX_STYLES_PROLOG[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
    "<w:docDefaults><w:rPrDefault><w:rPr>";

static const char DOCX_STYLES_EPILOG[] =
    "</w:rPr></w:rPrDefault></w:docDefaults></w:styles>";

static const char DOCX_DOC_PROLOG[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
    "<w:body>";

static const char DOCX_DOC_EPILOG[] = "</w:body></w:document>";

static void docx_apply_deferred(DocxReadCtx *rc);

static void docx_seterr(char *err, size_t errsz, const char *msg)
{
    if (err && errsz > 0)
    {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }
}

/* Insert one paragraph break into the editor */
static void docx_end_paragraph(DocxReadCtx *rc)
{
    EdLine *ln = NULL;

    if (!rc->ed)
        return;

    /* Apply deferred attribute runs before starting new paragraph */
    docx_apply_deferred(rc);

    /* Apply alignment to closing paragraph */
    if (rc->ed->row >= 0 && rc->ed->row < rc->ed->count)
    {
        ln = rc->ed->lines[rc->ed->row];

        if (ln)
            ln->para_align = rc->cur_align;
    }

    ed_enter(rc->ed);

    rc->cur_align = EA_ALIGN_LEFT;
    rc->para_has_text = 0;
    rc->run_start_col = rc->ed->col;
    rc->n_def = 0;
}

/* Feed UTF-8 text into the editor */
static void docx_emit_text(DocxReadCtx *rc, const char *utf8, int len)
{
    const char *p = NULL;
    const char *end = NULL;
    uint32_t cp;

    if (!rc->ed || len <= 0)
        return;

    p = utf8;
    end = utf8 + len;

    while (p < end)
    {
        cp = utf8_next(&p);

        if (p > end)
            break;

        if (cp == '\r')
            continue;

        if (cp == '\n')
        {
            /* Treat literal newlines as line breaks */
            ed_enter(rc->ed);

            continue;
        }

        ed_insert_char(rc->ed, (wchar_t)cp);

        rc->para_has_text = 1;
    }
}

/* Defer a run: record mask and column range, apply later at paragraph end */
static void docx_flush_run(DocxReadCtx *rc)
{
    int col_now;
    int start_col;

    if (!rc->ed || rc->cur_mask == 0)
        return;

    col_now = rc->ed->col;
    start_col = rc->run_start_col;

    /* Safe fallback: runs never cross paragraphs in DOCX */
    if (start_col < 0)
        start_col = 0;

    if (start_col < col_now && rc->n_def < 64)
    {
        rc->def_masks[rc->n_def] = rc->cur_mask;
        rc->def_starts[rc->n_def] = start_col;
        rc->def_ends[rc->n_def] = col_now;
        rc->n_def++;
    }
}

/* Apply all deferred runs to the current line */
static void docx_apply_deferred(DocxReadCtx *rc)
{
    EdLine *ln = NULL;
    int i;

    if (!rc->ed || rc->n_def <= 0)
        return;

    if (rc->ed->row < 0 || rc->ed->row >= rc->ed->count)
        return;

    ln = rc->ed->lines[rc->ed->row];

    if (!ln)
        return;

    for (i = 0; i < rc->n_def; i++)
        ed_attr_line_apply(ln, rc->def_starts[i], rc->def_ends[i], rc->def_masks[i], 0, -1, 0);

    rc->n_def = 0;
}

/* Find XML attribute by name */
static const char *docx_find_attr(const XlEvent *ev, const char *name)
{
    int i;

    for (i = 0; i < ev->n_attrs; i++)
    {
        if (strcmp(ev->attrs[i].name, name) == 0)
            return ev->attrs[i].value;
    }

    return NULL;
}

/* Map jc value to EA_ALIGN_* */
static unsigned char docx_map_jc(const char *v)
{
    if (!v || !v[0])
        return EA_ALIGN_LEFT;

    if (strcmp(v, "center") == 0)
        return EA_ALIGN_CENTER;

    if (strcmp(v, "right") == 0 || strcmp(v, "end") == 0)
        return EA_ALIGN_RIGHT;

    if (strcmp(v, "both") == 0 || strcmp(v, "distribute") == 0)
        return EA_ALIGN_JUST;

    return EA_ALIGN_LEFT;
}

/* Check if rPr boolean property is on */
static int docx_boolprop_on(const XlEvent *ev)
{
    const char *v = docx_find_attr(ev, "w:val");

    /* Word omits w:val for "on"; "0"/"false" means off */
    if (!v)
        return 1;

    if (strcmp(v, "0") == 0 || strcmp(v, "false") == 0)
        return 0;

    return 1;
}

/* Callback for XML events during import */
static int docx_read_cb(void *user, const XlEvent *ev)
{
    DocxReadCtx *rc = (DocxReadCtx *)user;

    if (ev->type == XL_START || ev->type == XL_SELFCLOSE)
    {
        if (strcmp(ev->tag, "w:p") == 0)
        {
            if (!rc->first_para)
                docx_end_paragraph(rc);

            rc->first_para = 0;
            rc->cur_mask = 0;
            rc->run_start_col = rc->ed->col;

            return 0;
        }

        if (strcmp(ev->tag, "w:pPr") == 0)
        {
            rc->in_ppr = 1;
            return 0;
        }

        if (strcmp(ev->tag, "w:jc") == 0 && rc->in_ppr)
        {
            rc->cur_align = docx_map_jc(docx_find_attr(ev, "w:val"));
            return 0;
        }

        if (strcmp(ev->tag, "w:r") == 0)
        {
            rc->cur_mask = 0;
            rc->run_start_col = rc->ed->col;
            return 0;
        }

        if (strcmp(ev->tag, "w:rPr") == 0)
        {
            rc->in_rpr = 1;
            return 0;
        }

        if (rc->in_rpr)
        {
            if (strcmp(ev->tag, "w:b") == 0 && docx_boolprop_on(ev))
                rc->cur_mask |= EA_BOLD;
            else if (strcmp(ev->tag, "w:i") == 0 && docx_boolprop_on(ev))
                rc->cur_mask |= EA_ITALIC;
            else if (strcmp(ev->tag, "w:u") == 0)
            {
                const char *v = docx_find_attr(ev, "w:val");

                if (!v || strcmp(v, "none") != 0)
                    rc->cur_mask |= EA_UNDERLINE;
            }
            else if (strcmp(ev->tag, "w:strike") == 0 && docx_boolprop_on(ev))
                rc->cur_mask |= EA_STRIKE;
            else if (strcmp(ev->tag, "w:dstrike") == 0 && docx_boolprop_on(ev))
                rc->cur_mask |= EA_STRIKE;

            return 0;
        }

        if (strcmp(ev->tag, "w:t") == 0)
        {
            rc->in_t = 1;
            rc->run_start_col = rc->ed->col;

            return 0;
        }

        if (strcmp(ev->tag, "w:br") == 0 || strcmp(ev->tag, "w:cr") == 0)
        {
            /* Soft line break within a paragraph, not a paragraph break */
            docx_flush_run(rc);
            ed_enter(rc->ed);

            /* ed_enter sets LB_PARA; fix previous line to LB_SPACE so join treats it as a soft break */
            if (rc->ed->row >= 1 && rc->ed->row - 1 < rc->ed->count)
                rc->ed->lines[rc->ed->row - 1]->brk = LB_SPACE;

            rc->run_start_col = rc->ed->col;

            return 0;
        }

        if (strcmp(ev->tag, "w:tab") == 0)
        {
            ed_insert_char(rc->ed, L'\t');
            return 0;
        }

        if (strcmp(ev->tag, "w:autoHyphenation") == 0)
        {
            rc->hyph_detected = 1;
            return 0;
        }

        /* Page margins live inside <w:sectPr> */
        if (strcmp(ev->tag, "w:pgMar") == 0)
        {
            int i;

            for (i = 0; i < ev->n_attrs; i++)
            {
                if (strcmp(ev->attrs[i].name, "w:left") == 0)
                    rc->pg_margin_left = atoi(ev->attrs[i].value);
                else if (strcmp(ev->attrs[i].name, "w:right") == 0)
                    rc->pg_margin_right = atoi(ev->attrs[i].value);
                else if (strcmp(ev->attrs[i].name, "w:top") == 0)
                    rc->pg_margin_top = atoi(ev->attrs[i].value);
                else if (strcmp(ev->attrs[i].name, "w:bottom") == 0)
                    rc->pg_margin_bottom = atoi(ev->attrs[i].value);
            }

            return 0;
        }

        /* Page size */
        if (strcmp(ev->tag, "w:pgSz") == 0)
        {
            int i;

            for (i = 0; i < ev->n_attrs; i++)
            {
                if (strcmp(ev->attrs[i].name, "w:w") == 0)
                    rc->pg_width = atoi(ev->attrs[i].value);
                else if (strcmp(ev->attrs[i].name, "w:h") == 0)
                    rc->pg_height = atoi(ev->attrs[i].value);
            }

            return 0;
        }
    }

    if (ev->type == XL_END)
    {
        if (strcmp(ev->tag, "w:pPr") == 0)
            rc->in_ppr = 0;
        else if (strcmp(ev->tag, "w:rPr") == 0)
            rc->in_rpr = 0;
        else if (strcmp(ev->tag, "w:t") == 0)
        {
            docx_flush_run(rc);
            rc->in_t = 0;
        }
        else if (strcmp(ev->tag, "w:r") == 0)
        {
            docx_flush_run(rc);
        }

        return 0;
    }

    if (ev->type == XL_TEXT && rc->in_t)
        docx_emit_text(rc, ev->text, ev->text_len);

    return 0;
}

/* Adapter: XML reader from ZIP entry */
static int docx_xl_read(void *user, unsigned char *buf, int buflen)
{
    return zip_read_entry((ZipReader *)user, buf, buflen);
}

int docx_import(struct Ed *ed, const char *path, char *err, size_t errsz, int *hyph_out)
{
    FILE *fp = NULL;
    ZipReader *zr = NULL;
    char name[ZIP_NAME_MAX];
    int rc = -1;
    int found_doc = 0;
    DocxReadCtx ctx;

    if (err && errsz > 0)
        err[0] = '\0';

    if (!ed || !path)
    {
        docx_seterr(err, errsz, "invalid arguments");
        return -1;
    }

    if (hyph_out)
        *hyph_out = 0;

    fp = fopen(path, "rb");

    if (!fp)
    {
        docx_seterr(err, errsz, "cannot open file");
        return -1;
    }

    zr = zip_open_read(fp);

    if (!zr)
    {
        docx_seterr(err, errsz, "not a valid docx (bad ZIP)");
        fclose(fp);
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));

    ctx.ed = ed;
    ctx.cur_align = EA_ALIGN_LEFT;
    ctx.first_para = 1;
    ctx.run_start_col = 0;

    while (zip_next_entry(zr, name, sizeof(name)) > 0)
    {
        if (strcmp(name, "word/document.xml") != 0)
            continue;

        found_doc = 1;
        rc = xl_parse(docx_xl_read, zr, docx_read_cb, &ctx);

        if (rc != 0)
        {
            docx_seterr(err, errsz, "XML parse failed in word/document.xml");
            break;
        }

        /* Apply deferred runs for last paragraph */
        docx_apply_deferred(&ctx);

        /* Flush alignment for last paragraph */
        if (ctx.ed->row >= 0 && ctx.ed->row < ctx.ed->count && ctx.ed->lines[ctx.ed->row])
            ctx.ed->lines[ctx.ed->row]->para_align = ctx.cur_align;

        break;
    }

    /* Second pass: parse word/settings.xml for autoHyphenation */
    if (rc == 0)
    {
        zip_close_read(zr);

        zr = zip_open_read(fp);

        if (zr)
        {
            while (zip_next_entry(zr, name, sizeof(name)) > 0)
            {
                if (strcmp(name, "word/settings.xml") != 0)
                    continue;

                xl_parse(docx_xl_read, zr, docx_read_cb, &ctx);

                break;
            }

            zip_close_read(zr);
        }
    }
    else
        zip_close_read(zr);

    fclose(fp);

    if (!found_doc)
    {
        docx_seterr(err, errsz, "word/document.xml not found");
        return -1;
    }

    /* Set hyph_out if autoHyphenation was detected */
    if (rc == 0 && hyph_out && ctx.hyph_detected)
        *hyph_out = 1;

    /* Convert twips to editor columns using the document's twips_per_col */
    if (rc == 0 && ctx.pg_margin_left > 0)
        ed->margin_left = (ctx.pg_margin_left + ed->twips_per_col / 2) / ed->twips_per_col;

    if (rc == 0 && ctx.pg_margin_right > 0)
    {
        int page_w = (ctx.pg_width > 0) ? ctx.pg_width : 12240;
        int text_tw = page_w - ctx.pg_margin_left - ctx.pg_margin_right;

        if (text_tw > 0)
            ed->margin_right = ed->margin_left + (text_tw + ed->twips_per_col / 2) / ed->twips_per_col;
    }

    /* Preserve page geometry and horizontal margins in twips for lossless round-trip export regardless of font CPI */
    if (rc == 0)
    {
        ed->page_w_tw = ctx.pg_width;
        ed->page_h_tw = ctx.pg_height;
        ed->margin_top_tw = ctx.pg_margin_top;
        ed->margin_bottom_tw = ctx.pg_margin_bottom;
        ed->margin_left_tw = ctx.pg_margin_left;
        ed->margin_right_tw = ctx.pg_margin_right;
    }

    return rc == 0 ? 0 : -1;
}

static int docx_write_flush(DocxWriteCtx *wc)
{
    int rc;

    if (wc->len == 0)
        return 0;

    rc = zip_write_entry(wc->zw, wc->buf, wc->len);

    wc->len = 0;

    return rc < 0 ? -1 : 0;
}

static int docx_write_bytes(DocxWriteCtx *wc, const void *data, int n)
{
    const unsigned char *p = (const unsigned char *)data;
    int off = 0;

    while (off < n)
    {
        int room = (int)sizeof(wc->buf) - wc->len;
        int take = n - off < room ? n - off : room;

        memcpy(wc->buf + wc->len, p + off, (size_t)take);

        wc->len += take;
        off += take;

        if (wc->len == (int)sizeof(wc->buf))
        {
            if (docx_write_flush(wc) != 0)
                return -1;
        }
    }

    return 0;
}

static int docx_write_str(DocxWriteCtx *wc, const char *s)
{
    return docx_write_bytes(wc, s, (int)strlen(s));
}

/* Write text with XML escaping */
static int docx_write_escaped(DocxWriteCtx *wc, const wchar_t *ws, int n)
{
    int i;

    for (i = 0; i < n; i++)
    {
        wchar_t c = ws[i];

        if (c == '<')
        {
            if (docx_write_str(wc, "&lt;") != 0)
                return -1;
        }
        else if (c == '>')
        {
            if (docx_write_str(wc, "&gt;") != 0)
                return -1;
        }
        else if (c == '&')
        {
            if (docx_write_str(wc, "&amp;") != 0)
                return -1;
        }
        else if (c == '"')
        {
            if (docx_write_str(wc, "&quot;") != 0)
                return -1;
        }
        else if (c == '\'')
        {
            if (docx_write_str(wc, "&apos;") != 0)
                return -1;
        }
        else if (c == '\t')
        {
            if (docx_write_str(wc, "</w:t><w:tab/><w:t xml:space=\"preserve\">") != 0)
                return -1;
        }
        else
        {
            char ubuf[4];
            int ulen;

            ulen = utf8_encode((uint32_t)c, ubuf);

            if (ulen > 0 && docx_write_bytes(wc, (unsigned char *)ubuf, ulen) != 0)
                return -1;
        }
    }

    return 0;
}

/* Emit <w:rPr> for non-zero mask */
static int docx_write_rpr(DocxWriteCtx *wc, unsigned short mask)
{
    if (mask == 0)
        return 0;

    if (docx_write_str(wc, "<w:rPr>") != 0)
        return -1;

    if ((mask & EA_BOLD) && docx_write_str(wc, "<w:b/>") != 0)
        return -1;

    if ((mask & EA_ITALIC) && docx_write_str(wc, "<w:i/>") != 0)
        return -1;

    if ((mask & EA_UNDERLINE) && docx_write_str(wc, "<w:u w:val=\"single\"/>") != 0)
        return -1;

    if ((mask & EA_STRIKE) && docx_write_str(wc, "<w:strike/>") != 0)
        return -1;

    return docx_write_str(wc, "</w:rPr>");
}

/* Map EA_ALIGN_* to jc keyword */
static const char *docx_map_align(unsigned char a)
{
    if (a == EA_ALIGN_CENTER)
        return "center";

    if (a == EA_ALIGN_RIGHT)
        return "right";

    if (a == EA_ALIGN_JUST)
        return "both";

    return NULL;
}

/* Write one EdLine's text as <w:r> runs (no <w:p> wrapper) */
static int docx_write_line_runs(DocxWriteCtx *wc, const struct Ed *ed, int line_idx)
{
    EdLine *ln = ed->lines[line_idx];
    const wchar_t *text = NULL;
    int len;
    const EdAttrRun *runs = NULL;
    int n_runs;
    int i;
    int cur = 0;

    text = ed_line_wcs(ed, line_idx);
    len = ed_line_len(ed, line_idx);

    if (!text || len < 0)
        len = 0;

    n_runs = ed_attr_runs(ln, &runs);

    /* Emit one <w:r> per attr run */
    if (n_runs > 0 && runs)
    {
        for (i = 0; i < n_runs; i++)
        {
            const EdAttrRun *r = &runs[i];

            /* Fill gap before run with unstyled text */
            if (r->start > cur)
            {
                if (docx_write_str(wc, "<w:r><w:t xml:space=\"preserve\">") != 0)
                    return -1;

                if (docx_write_escaped(wc, text + cur, r->start - cur) != 0)
                    return -1;

                if (docx_write_str(wc, "</w:t></w:r>") != 0)
                    return -1;
            }

            if (docx_write_str(wc, "<w:r>") != 0)
                return -1;

            if (docx_write_rpr(wc, r->mask) != 0)
                return -1;

            if (docx_write_str(wc, "<w:t xml:space=\"preserve\">") != 0)
                return -1;

            if (docx_write_escaped(wc, text + r->start, r->end - r->start) != 0)
                return -1;

            if (docx_write_str(wc, "</w:t></w:r>") != 0)
                return -1;

            cur = r->end;
        }
    }

    /* Tail text after last run */
    if (cur < len)
    {
        if (docx_write_str(wc, "<w:r><w:t xml:space=\"preserve\">") != 0)
            return -1;

        if (docx_write_escaped(wc, text + cur, len - cur) != 0)
            return -1;

        if (docx_write_str(wc, "</w:t></w:r>") != 0)
            return -1;
    }

    return 0;
}

/* Write one logical paragraph: group consecutive EdLines until LB_PARA, like RTF */
static int docx_write_para_group(DocxWriteCtx *wc, const struct Ed *ed, int start_idx, int *out_next)
{
    EdLine *ln = ed->lines[start_idx];
    const char *jc = NULL;
    int row = start_idx;
    int has_text = 0;
    EdLine *cur_ln = NULL;
    int len;

    /* Paragraph open */
    if (docx_write_str(wc, "<w:p>") != 0)
        return -1;

    /* Paragraph properties */
    jc = docx_map_align(ln ? ln->para_align : EA_ALIGN_LEFT);

    if (jc)
    {
        if (docx_write_str(wc, "<w:pPr><w:jc w:val=\"") != 0)
            return -1;

        if (docx_write_str(wc, jc) != 0)
            return -1;

        if (docx_write_str(wc, "\"/></w:pPr>") != 0)
            return -1;
    }

    /* Emit each EdLine's text, joining with space for LB_SPACE (like RTF) */
    while (row < ed->count)
    {
        cur_ln = ed->lines[row];
        len = ed_line_len(ed, row);

        if (len > 0)
            has_text = 1;

        if (docx_write_line_runs(wc, ed, row) != 0)
            return -1;

        /* LB_PARA closes the paragraph */
        if (cur_ln->brk == LB_PARA || row >= ed->count - 1)
        {
            row++;
            break;
        }

        /* LB_SPACE: join with a space; LB_HYPHEN/LB_WORD: join with nothing */
        if (cur_ln->brk == LB_SPACE)
        {
            if (docx_write_str(wc, "<w:r><w:t xml:space=\"preserve\"> </w:t></w:r>") != 0)
                return -1;
        }

        row++;
    }

    /* Empty paragraph needs empty run for Word */
    if (!has_text)
    {
        if (docx_write_str(wc, "<w:r><w:t xml:space=\"preserve\"></w:t></w:r>") != 0)
            return -1;
    }

    /* Paragraph close */
    if (docx_write_str(wc, "</w:p>") != 0)
        return -1;

    *out_next = row;
    return 0;
}

/* Resolve font name and size from config, matching RTF export behavior */
static void docx_resolve_font(const TeConfig *cfg, char *font_name, int font_name_sz, int *font_size_hp)
{
    const char *font_path = NULL;

    font_name[0] = '\0';
    *font_size_hp = 0;

    if (!cfg)
        return;

    font_path = cfg->print_font_path[0] ? cfg->print_font_path : (cfg->ttf_font[0] ? cfg->ttf_font : NULL);

#if defined(USE_FREETYPE) || defined(HAVE_PRINTER)
    if (font_path && font_path[0])
    {
        FT_Library lib = NULL;
        FT_Face face = NULL;

        if (FT_Init_FreeType(&lib) == 0)
        {
            if (FT_New_Face(lib, font_path, 0, &face) == 0)
            {
                if (face->family_name && face->family_name[0])
                {
                    strncpy(font_name, face->family_name, font_name_sz - 1);
                    font_name[font_name_sz - 1] = '\0';
                }

                FT_Done_Face(face);
            }

            FT_Done_FreeType(lib);
        }
    }
#endif

    if (cfg->print_font_size > 0)
        *font_size_hp = cfg->print_font_size * 2;
    else if (cfg->ttf_size > 0)
        *font_size_hp = cfg->ttf_size * 2;
}

/* Build word/styles.xml with docDefaults for font and size */
static int docx_build_styles_xml(char *buf, int buf_sz, const char *font_name, int font_size_hp)
{
    int len = 0;
    int n;

    n = snprintf(buf + len, buf_sz - len, "%s", DOCX_STYLES_PROLOG);

    if (n < 0 || n >= buf_sz - len)
        return -1;

    len += n;

    if (font_name && font_name[0])
    {
        n = snprintf(buf + len, buf_sz - len, "<w:rFonts w:ascii=\"%s\" w:hAnsi=\"%s\" w:cs=\"%s\"/>", font_name, font_name, font_name);

        if (n < 0 || n >= buf_sz - len)
            return -1;

        len += n;
    }

    if (font_size_hp > 0)
    {
        n = snprintf(buf + len, buf_sz - len, "<w:sz w:val=\"%d\"/><w:szCs w:val=\"%d\"/>", font_size_hp, font_size_hp);

        if (n < 0 || n >= buf_sz - len)
            return -1;
        len += n;
    }

    n = snprintf(buf + len, buf_sz - len, "%s", DOCX_STYLES_EPILOG);

    if (n < 0 || n >= buf_sz - len)
        return -1;

    len += n;

    return len;
}

int docx_export(const struct Ed *ed, const char *path, const TeConfig *cfg, int hyph, char *err, size_t errsz)
{
    FILE *fp = NULL;
    ZipWriter *zw = NULL;
    DocxWriteCtx wc;
    int i;
    int next;
    char font_name[128];
    int font_size_hp;
    char styles_buf[768];
    int styles_len;
    const char *settings_xml = NULL;
    int settings_len;

    if (err && errsz > 0)
        err[0] = '\0';

    if (!ed || !path)
    {
        docx_seterr(err, errsz, "invalid arguments");
        return -1;
    }

    fp = fopen(path, "wb");

    if (!fp)
    {
        docx_seterr(err, errsz, "cannot create file");
        return -1;
    }

    zw = zip_open_write(fp);

    if (!zw)
    {
        docx_seterr(err, errsz, "cannot start ZIP");

        fclose(fp);
        return -1;
    }

    memset(&wc, 0, sizeof(wc));

    wc.zw = zw;

    /* [Content_Types].xml */
    if (zip_write_stored_entry(zw, "[Content_Types].xml", (const unsigned char *)DOCX_CT_TYPES, (int)(sizeof(DOCX_CT_TYPES) - 1)) != 0)
    {
        docx_seterr(err, errsz, "ZIP write failed on [Content_Types].xml");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* _rels/.rels */
    if (zip_write_stored_entry(zw, "_rels/.rels", (const unsigned char *)DOCX_ROOT_RELS, (int)(sizeof(DOCX_ROOT_RELS) - 1)) != 0)
    {
        docx_seterr(err, errsz, "ZIP write failed on _rels/.rels");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* word/_rels/document.xml.rels */
    if (zip_write_stored_entry(zw, "word/_rels/document.xml.rels", (const unsigned char *)DOCX_DOC_RELS, (int)(sizeof(DOCX_DOC_RELS) - 1)) != 0)
    {
        docx_seterr(err, errsz, "ZIP write failed on word/_rels/document.xml.rels");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* Resolve font from config */
    docx_resolve_font(cfg, font_name, sizeof(font_name), &font_size_hp);

    /* word/styles.xml (only if font or size is configured) */
    if (font_name[0] || font_size_hp > 0)
    {
        styles_len = docx_build_styles_xml(styles_buf, sizeof(styles_buf), font_name, font_size_hp);

        if (styles_len < 0)
        {
            docx_seterr(err, errsz, "styles.xml build failed");

            zip_close_write(zw);
            fclose(fp);
            return -1;
        }

        if (zip_write_stored_entry(zw, "word/styles.xml", (const unsigned char *)styles_buf, styles_len) != 0)
        {
            docx_seterr(err, errsz, "ZIP write failed on word/styles.xml");

            zip_close_write(zw);
            fclose(fp);
            return -1;
        }
    }

    /* word/settings.xml: enable auto hyphenation when caller sets hyph */
    settings_xml = DOCX_SETTINGS_PLAIN;
    settings_len = (int)(sizeof(DOCX_SETTINGS_PLAIN) - 1);

    if (hyph)
    {
        settings_xml = DOCX_SETTINGS_HYPH;
        settings_len = (int)(sizeof(DOCX_SETTINGS_HYPH) - 1);
    }

    if (zip_write_stored_entry(zw, "word/settings.xml", (const unsigned char *)settings_xml, settings_len) != 0)
    {
        docx_seterr(err, errsz, "ZIP write failed on word/settings.xml");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* word/document.xml */
    if (zip_start_entry(zw, "word/document.xml", DOCX_WRITE_METHOD) != 0)
    {
        docx_seterr(err, errsz, "ZIP start failed on word/document.xml");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    wc.zw = zw;

    if (docx_write_str(&wc, DOCX_DOC_PROLOG) != 0)
    {
        docx_seterr(err, errsz, "write failed");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* Group EdLines into logical paragraphs using brk, like RTF */
    i = 0;
    while (i < ed->count)
    {
        next = i;

        if (docx_write_para_group(&wc, ed, i, &next) != 0)
        {
            docx_seterr(err, errsz, "write failed on paragraph");

            zip_close_write(zw);
            fclose(fp);
            return -1;
        }

        i = next;
    }

    /* Emit sectPr with page size/margins; captured import values or Letter defaults */
    if (ed->margin_left > 0 || ed->margin_right > 0)
    {
        char sect_buf[256];
        int left_tw, right_tw;
        int page_w = (ed->page_w_tw > 0) ? ed->page_w_tw : 12240;
        int page_h = (ed->page_h_tw > 0) ? ed->page_h_tw : 15840;
        int marg_t = (ed->margin_top_tw > 0) ? ed->margin_top_tw : 1440;
        int marg_b = (ed->margin_bottom_tw > 0) ? ed->margin_bottom_tw : 1440;

        /* Prefer exact twips captured on import; fall back to column-derived */
        if (ed->margin_left_tw > 0)
            left_tw = ed->margin_left_tw;
        else
            left_tw = ed->margin_left * ed->twips_per_col;

        if (ed->margin_right_tw > 0)
        {
            right_tw = ed->margin_right_tw;
        }
        else
        {
            right_tw = 1440;

            if (ed->margin_right > ed->margin_left)
            {
                int span_cols = ed->margin_right - ed->margin_left;
                int text_tw = span_cols * ed->twips_per_col;

                right_tw = page_w - left_tw - text_tw;

                if (right_tw < 0)
                    right_tw = 0;
            }
        }

        snprintf(sect_buf, sizeof(sect_buf), "<w:sectPr><w:pgSz w:w=\"%d\" w:h=\"%d\"/><w:pgMar w:top=\"%d\" w:right=\"%d\" w:bottom=\"%d\" w:left=\"%d\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>", page_w, page_h, marg_t, right_tw, marg_b, left_tw);

        if (docx_write_str(&wc, sect_buf) != 0)
        {
            docx_seterr(err, errsz, "write failed on sectPr");
            zip_close_write(zw);
            fclose(fp);
            return -1;
        }
    }

    if (docx_write_str(&wc, DOCX_DOC_EPILOG) != 0 || docx_write_flush(&wc) != 0 || zip_end_entry(zw) != 0)
    {
        docx_seterr(err, errsz, "write failed at close");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    if (zip_close_write(zw) != 0)
    {
        docx_seterr(err, errsz, "ZIP close failed");

        fclose(fp);
        return -1;
    }

    fclose(fp);

    return 0;
}
