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

/* ODT (OpenDocument Text) reader/writer via streaming ZIP with minimal ODF XML */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "editor.h"
#include "ed_attr.h"
#include "config.h"
#include "fmt_odt.h"
#include "../core/zip_stream.h"
#include "../core/xml_lite.h"
#include "../core/utf8.h"

#if defined(USE_FREETYPE) || defined(HAVE_PRINTER)
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

/* Amiga uses stored ZIP to save RAM; compatible with all readers */
/*#if defined(PLATFORM_AMIGA)
#define ODT_WRITE_METHOD ZIP_METHOD_STORED
#else*/
#define ODT_WRITE_METHOD ZIP_METHOD_DEFLATE
/*#endif*/

/* ODF mimetype string */
static const char ODT_MIMETYPE[] = "application/vnd.oasis.opendocument.text";

/* META-INF/manifest.xml */
static const char ODT_MANIFEST[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\">"
    "<manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\" manifest:version=\"1.2\"/>"
    "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
    "<manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>"
    "</manifest:manifest>";

/* styles.xml prolog and epilog */
static const char ODT_STYLES_PROLOG[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<office:document-styles"
    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
    " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
    " office:version=\"1.2\">"
    "<office:styles>"
    "<style:default-style style:family=\"paragraph\">"
    "<style:text-properties";

static const char ODT_STYLES_MID[] =
    "/></style:default-style>"
    "<style:style style:name=\"Standard\" style:family=\"paragraph\"/>"
    "</office:styles>"
    "</office:document-styles>";

/* content.xml prolog and epilog */
static const char ODT_CONTENT_PROLOG[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<office:document-content"
    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
    " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
    " office:version=\"1.2\">";

static const char ODT_CONTENT_EPILOG[] =
    "</office:text>"
    "</office:body>"
    "</office:document-content>";

/* Reader state */
typedef struct
{
    struct Ed *ed;

    /* Current run attribute mask */
    unsigned short cur_mask;

    /* Inside text:p element */
    int in_p;

    /* Inside text:span element */
    int in_span;

    /* Collecting text content */
    int in_text;

    /* Current paragraph alignment */
    unsigned char cur_align;

    /* Run start column for attr flush */
    int run_start_col;

    /* First paragraph flag */
    int first_para;

    /* Paragraph has text flag */
    int para_has_text;

    /* Style name -> align mapping (simple table) */
    char p_styles[8][XL_ATTR_VAL_MAX];
    unsigned char p_aligns[8];
    int n_p_styles;

    /* Style name -> mask mapping */
    char t_styles[16][XL_ATTR_VAL_MAX];
    unsigned short t_masks[16];
    int n_t_styles;

    /* Deferred runs: collected during paragraph, applied at paragraph end */
    unsigned short def_masks[64];
    int def_starts[64];
    int def_ends[64];
    int n_def;

    /* Hyphenation detected in styles during import */
    int hyph_detected;
} OdtReadCtx;

typedef struct
{
    ZipWriter *zw;
    unsigned char buf[512];
    int len;
} OdtWriteCtx;

/* Style collection for export (first pass) */
typedef struct
{
    /* Paragraph styles: up to 3 non-default alignments */
    unsigned char p_aligns[3];
    int n_p_styles;

    /* Text styles: up to 15 non-zero mask combinations */
    unsigned short t_masks[15];
    int n_t_styles;
} OdtStyleMap;

static void odt_apply_deferred(OdtReadCtx *rc);

static void odt_seterr(char *err, size_t errsz, const char *msg)
{
    if (err && errsz > 0)
    {
        strncpy(err, msg, errsz - 1);
        err[errsz - 1] = '\0';
    }
}

static void odt_end_paragraph(OdtReadCtx *rc)
{
    EdLine *ln = NULL;

    if (!rc->ed)
        return;

    /* Apply deferred attribute runs before starting new paragraph */
    odt_apply_deferred(rc);

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

static void odt_emit_text(OdtReadCtx *rc, const char *utf8, int len)
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

        /* Soft hyphen is invisible layout, tinyedit re-hyphenates on rewrap */
        if (cp == 0xAD)
            continue;

        if (cp == '\n')
        {
            ed_enter(rc->ed);
            continue;
        }

        ed_insert_char(rc->ed, (wchar_t)cp);

        rc->para_has_text = 1;
    }
}

/* Defer a run: record mask and column range, apply later at paragraph end */
static void odt_flush_run(OdtReadCtx *rc)
{
    int col_now;
    int start_col;

    if (!rc->ed || rc->cur_mask == 0)
        return;

    col_now = rc->ed->col;
    start_col = rc->run_start_col;

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
static void odt_apply_deferred(OdtReadCtx *rc)
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
static const char *odt_find_attr(const XlEvent *ev, const char *name)
{
    int i;

    for (i = 0; i < ev->n_attrs; i++)
    {
        if (strcmp(ev->attrs[i].name, name) == 0)
            return ev->attrs[i].value;
    }

    return NULL;
}

/* Map fo:text-align value to EA_ALIGN_* */
static unsigned char odt_map_align(const char *v)
{
    if (!v || !v[0])
        return EA_ALIGN_LEFT;

    if (strcmp(v, "center") == 0)
        return EA_ALIGN_CENTER;

    if (strcmp(v, "end") == 0 || strcmp(v, "right") == 0)
        return EA_ALIGN_RIGHT;

    if (strcmp(v, "justify") == 0)
        return EA_ALIGN_JUST;

    return EA_ALIGN_LEFT;
}

/* Look up or register a paragraph style name -> alignment */
static unsigned char odt_lookup_p_style(OdtReadCtx *rc, const char *name)
{
    int i;

    if (!name || !name[0])
        return EA_ALIGN_LEFT;

    for (i = 0; i < rc->n_p_styles; i++)
    {
        if (strcmp(rc->p_styles[i], name) == 0)
            return rc->p_aligns[i];
    }

    return EA_ALIGN_LEFT;
}

/* Look up or register a text style name -> mask */
static unsigned short odt_lookup_t_style(OdtReadCtx *rc, const char *name)
{
    int i;

    if (!name || !name[0])
        return 0;

    for (i = 0; i < rc->n_t_styles; i++)
    {
        if (strcmp(rc->t_styles[i], name) == 0)
            return rc->t_masks[i];
    }

    return 0;
}

/* Register a paragraph style */
static void odt_reg_p_style(OdtReadCtx *rc, const char *name, unsigned char align)
{
    if (!name || !name[0] || rc->n_p_styles >= 8)
        return;

    strncpy(rc->p_styles[rc->n_p_styles], name, XL_ATTR_VAL_MAX - 1);
    rc->p_styles[rc->n_p_styles][XL_ATTR_VAL_MAX - 1] = '\0';

    rc->p_aligns[rc->n_p_styles] = align;
    rc->n_p_styles++;
}

/* Register a text style */
static void odt_reg_t_style(OdtReadCtx *rc, const char *name, unsigned short mask)
{
    if (!name || !name[0] || rc->n_t_styles >= 16)
        return;

    strncpy(rc->t_styles[rc->n_t_styles], name, XL_ATTR_VAL_MAX - 1);
    rc->t_styles[rc->n_t_styles][XL_ATTR_VAL_MAX - 1] = '\0';

    rc->t_masks[rc->n_t_styles] = mask;
    rc->n_t_styles++;
}

/* Parse text-properties attributes into a mask */
static unsigned short odt_parse_text_props(const XlEvent *ev)
{
    unsigned short mask = 0;
    const char *v = NULL;

    v = odt_find_attr(ev, "fo:font-weight");

    if (v && (strcmp(v, "bold") == 0))
        mask |= EA_BOLD;

    v = odt_find_attr(ev, "fo:font-style");

    if (v && (strcmp(v, "italic") == 0))
        mask |= EA_ITALIC;

    v = odt_find_attr(ev, "style:text-underline-style");

    if (v && (strcmp(v, "solid") == 0))
        mask |= EA_UNDERLINE;

    v = odt_find_attr(ev, "style:text-line-through-style");

    if (v && (strcmp(v, "solid") == 0))
        mask |= EA_STRIKE;

    return mask;
}

/* Parse paragraph-properties attributes into alignment */
static unsigned char odt_parse_para_props(const XlEvent *ev)
{
    return odt_map_align(odt_find_attr(ev, "fo:text-align"));
}

/* Import: XML callback */
static int odt_read_cb(void *user, const XlEvent *ev)
{
    OdtReadCtx *rc = (OdtReadCtx *)user;

    if (ev->type == XL_START || ev->type == XL_SELFCLOSE)
    {
        /* Style definitions in automatic-styles */
        if (strcmp(ev->tag, "style:style") == 0)
        {
            const char *family = odt_find_attr(ev, "style:family");
            const char *name = odt_find_attr(ev, "style:name");

            if (family && name && name[0])
            {
                if (strcmp(family, "paragraph") == 0)
                {
                    /* Default to left; will be updated by paragraph-properties child */
                    odt_reg_p_style(rc, name, EA_ALIGN_LEFT);
                }
                else if (strcmp(family, "text") == 0)
                {
                    odt_reg_t_style(rc, name, 0);
                }
            }

            return 0;
        }

        if (strcmp(ev->tag, "style:paragraph-properties") == 0)
        {
            const char *hyph = NULL;

            /* Update last registered paragraph style */
            if (rc->n_p_styles > 0)
                rc->p_aligns[rc->n_p_styles - 1] = odt_parse_para_props(ev);

            /* Detect hyphenation in paragraph properties */
            hyph = odt_find_attr(ev, "fo:hyphenate");

            if (hyph && strcmp(hyph, "true") == 0)
                rc->hyph_detected = 1;

            return 0;
        }

        if (strcmp(ev->tag, "style:text-properties") == 0)
        {
            const char *hyph = NULL;

            /* Update last registered text style */
            if (rc->n_t_styles > 0)
                rc->t_masks[rc->n_t_styles - 1] = odt_parse_text_props(ev);

            /* Detect hyphenation in text properties (LibreOffice puts it here) */
            hyph = odt_find_attr(ev, "fo:hyphenate");

            if (hyph && strcmp(hyph, "true") == 0)
                rc->hyph_detected = 1;

            return 0;
        }

        /* Body content */
        if (strcmp(ev->tag, "text:p") == 0)
        {
            if (!rc->first_para)
                odt_end_paragraph(rc);

            rc->first_para = 0;
            rc->cur_mask = 0;
            rc->run_start_col = rc->ed->col;

            /* Look up paragraph style for alignment */
            rc->cur_align = odt_lookup_p_style(rc, odt_find_attr(ev, "text:style-name"));

            rc->in_p = 1;
            return 0;
        }

        if (strcmp(ev->tag, "text:span") == 0)
        {
            rc->cur_mask = odt_lookup_t_style(rc, odt_find_attr(ev, "text:style-name"));
            rc->run_start_col = rc->ed->col;
            rc->in_span = 1;

            return 0;
        }

        if (strcmp(ev->tag, "text:tab") == 0)
        {
            ed_insert_char(rc->ed, L'\t');
            return 0;
        }

        if (strcmp(ev->tag, "text:line-break") == 0)
        {
            /* Soft line break within a paragraph, not a paragraph break */
            odt_flush_run(rc);
            ed_enter(rc->ed);

            /* ed_enter sets LB_PARA; fix previous line to LB_SPACE so join treats it as a soft break */
            if (rc->ed->row >= 1 && rc->ed->row - 1 < rc->ed->count)
                rc->ed->lines[rc->ed->row - 1]->brk = LB_SPACE;

            rc->run_start_col = rc->ed->col;

            return 0;
        }
    }

    if (ev->type == XL_END)
    {
        if (strcmp(ev->tag, "text:span") == 0)
        {
            odt_flush_run(rc);

            rc->cur_mask = 0;
            rc->in_span = 0;
            rc->run_start_col = rc->ed->col;

            return 0;
        }

        if (strcmp(ev->tag, "text:p") == 0)
        {
            odt_flush_run(rc);
            odt_apply_deferred(rc);

            rc->in_p = 0;

            return 0;
        }

        return 0;
    }

    if (ev->type == XL_TEXT && rc->in_p)
        odt_emit_text(rc, ev->text, ev->text_len);

    return 0;
}

/* Adapter: XML reader from ZIP entry */
static int odt_xl_read(void *user, unsigned char *buf, int buflen)
{
    return zip_read_entry((ZipReader *)user, buf, buflen);
}

int odt_import(struct Ed *ed, const char *path, char *err, size_t errsz, int *hyph_out)
{
    FILE *fp = NULL;
    ZipReader *zr = NULL;
    char name[ZIP_NAME_MAX];
    int rc = -1;
    int found_content = 0;
    OdtReadCtx ctx;

    if (err && errsz > 0)
        err[0] = '\0';

    if (!ed || !path)
    {
        odt_seterr(err, errsz, "invalid arguments");
        return -1;
    }

    fp = fopen(path, "rb");

    if (!fp)
    {
        odt_seterr(err, errsz, "cannot open file");

        return -1;
    }

    zr = zip_open_read(fp);

    if (!zr)
    {
        odt_seterr(err, errsz, "not a valid odt (bad ZIP)");

        fclose(fp);
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));

    ctx.ed = ed;
    ctx.cur_align = EA_ALIGN_LEFT;
    ctx.first_para = 1;
    ctx.run_start_col = 0;

    /* First pass: find content.xml and parse it */
    while (zip_next_entry(zr, name, sizeof(name)) > 0)
    {
        if (strcmp(name, "content.xml") != 0)
            continue;

        found_content = 1;

        rc = xl_parse(odt_xl_read, zr, odt_read_cb, &ctx);

        if (rc != 0)
        {
            odt_seterr(err, errsz, "XML parse failed in content.xml");
            break;
        }

        /* Apply deferred runs and flush alignment for last paragraph */
        odt_apply_deferred(&ctx);

        if (ctx.ed->row >= 0 && ctx.ed->row < ctx.ed->count && ctx.ed->lines[ctx.ed->row])
            ctx.ed->lines[ctx.ed->row]->para_align = ctx.cur_align;

        break;
    }

    zip_close_read(zr);
    fclose(fp);

    if (!found_content)
    {
        odt_seterr(err, errsz, "content.xml not found");
        return -1;
    }

    /* Set hyph_out if detected during import */
    if (rc == 0 && hyph_out && ctx.hyph_detected)
        *hyph_out = 1;

    return rc == 0 ? 0 : -1;
}

/* Export: buffered writer (same pattern as DOCX) */
static int odt_write_flush(OdtWriteCtx *wc)
{
    int rc;

    if (wc->len == 0)
        return 0;

    rc = zip_write_entry(wc->zw, wc->buf, wc->len);

    wc->len = 0;

    return rc < 0 ? -1 : 0;
}

static int odt_write_bytes(OdtWriteCtx *wc, const void *data, int n)
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
            if (odt_write_flush(wc) != 0)
                return -1;
        }
    }

    return 0;
}

static int odt_write_str(OdtWriteCtx *wc, const char *s)
{
    return odt_write_bytes(wc, s, (int)strlen(s));
}

/* Write text with XML escaping */
static int odt_write_escaped(OdtWriteCtx *wc, const wchar_t *ws, int n)
{
    int i;

    for (i = 0; i < n; i++)
    {
        wchar_t c = ws[i];

        if (c == '<')
        {
            if (odt_write_str(wc, "&lt;") != 0)
                return -1;
        }
        else if (c == '>')
        {
            if (odt_write_str(wc, "&gt;") != 0)
                return -1;
        }
        else if (c == '&')
        {
            if (odt_write_str(wc, "&amp;") != 0)
                return -1;
        }
        else if (c == '"')
        {
            if (odt_write_str(wc, "&quot;") != 0)
                return -1;
        }
        else if (c == '\'')
        {
            if (odt_write_str(wc, "&apos;") != 0)
                return -1;
        }
        else if (c == '\t')
        {
            if (odt_write_str(wc, "<text:tab/>") != 0)
                return -1;
        }
        else
        {
            char ubuf[4];
            int ulen;

            ulen = utf8_encode((uint32_t)c, ubuf);

            if (ulen > 0 && odt_write_bytes(wc, (unsigned char *)ubuf, ulen) != 0)
                return -1;
        }
    }

    return 0;
}

/* Find or register a paragraph alignment style; returns style index or -1 for default */
static int odt_map_p_style(OdtStyleMap *sm, unsigned char align)
{
    int i;

    if (align == EA_ALIGN_LEFT)
        return -1;

    for (i = 0; i < sm->n_p_styles; i++)
    {
        if (sm->p_aligns[i] == align)
            return i;
    }

    if (sm->n_p_styles < 3)
    {
        sm->p_aligns[sm->n_p_styles] = align;

        return sm->n_p_styles++;
    }

    return -1;
}

/* Find or register a text mask style; returns style index or -1 for no attrs */
static int odt_map_t_style(OdtStyleMap *sm, unsigned short mask)
{
    int i;

    if (mask == 0)
        return -1;

    for (i = 0; i < sm->n_t_styles; i++)
    {
        if (sm->t_masks[i] == mask)
            return i;
    }

    if (sm->n_t_styles < 15)
    {
        sm->t_masks[sm->n_t_styles] = mask;

        return sm->n_t_styles++;
    }

    return -1;
}

/* Generate paragraph style name: P1, P2, P3 */
static void odt_p_style_name(int idx, char *buf, int buf_sz)
{
    snprintf(buf, buf_sz, "P%d", idx + 1);
}

/* Generate text style name: T1, T2, ... */
static void odt_t_style_name(int idx, char *buf, int buf_sz)
{
    snprintf(buf, buf_sz, "T%d", idx + 1);
}

/* Map EA_ALIGN_* to fo:text-align value */
static const char *odt_align_str(unsigned char a)
{
    if (a == EA_ALIGN_CENTER)
        return "center";

    if (a == EA_ALIGN_RIGHT)
        return "right";

    if (a == EA_ALIGN_JUST)
        return "justify";

    return NULL;
}

/* Write text-properties for a mask */
static int odt_write_text_props(OdtWriteCtx *wc, unsigned short mask)
{
    if (mask & EA_BOLD)
    {
        if (odt_write_str(wc, " fo:font-weight=\"bold\"") != 0)
            return -1;
    }

    if (mask & EA_ITALIC)
    {
        if (odt_write_str(wc, " fo:font-style=\"italic\"") != 0)
            return -1;
    }

    if (mask & EA_UNDERLINE)
    {
        if (odt_write_str(wc, " style:text-underline-style=\"solid\"") != 0)
            return -1;
    }

    if (mask & EA_STRIKE)
    {
        if (odt_write_str(wc, " style:text-line-through-style=\"solid\"") != 0)
            return -1;
    }

    return 0;
}

/* Export: write automatic-styles section */
static int odt_write_auto_styles(OdtWriteCtx *wc, const OdtStyleMap *sm, int hyph)
{
    int i;

    if (odt_write_str(wc, "<office:automatic-styles>") != 0)
        return -1;

    /* Paragraph styles */
    for (i = 0; i < sm->n_p_styles; i++)
    {
        char nm[8];
        const char *al = NULL;

        odt_p_style_name(i, nm, sizeof(nm));

        al = odt_align_str(sm->p_aligns[i]);

        if (odt_write_str(wc, "<style:style style:name=\"") != 0)
            return -1;

        if (odt_write_str(wc, nm) != 0)
            return -1;

        if (odt_write_str(wc, "\" style:family=\"paragraph\" style:parent-style-name=\"Standard\">") != 0)
            return -1;

        if (al || hyph)
        {
            if (odt_write_str(wc, "<style:paragraph-properties") != 0)
                return -1;

            if (al)
            {
                if (odt_write_str(wc, " fo:text-align=\"") != 0)
                    return -1;

                if (odt_write_str(wc, al) != 0)
                    return -1;

                if (odt_write_str(wc, "\"") != 0)
                    return -1;
            }

            /* fo:hyphenate in paragraph-properties for OnlyOffice compatibility (non-standard) */
            if (hyph)
            {
                if (odt_write_str(wc, " fo:hyphenate=\"true\" fo:hyphenation-remain-char-count=\"2\" fo:hyphenation-push-char-count=\"2\"") != 0)
                    return -1;
            }

            if (odt_write_str(wc, "/>") != 0)
                return -1;

            if (hyph)
            {
                /* fo:hyphenate in text-properties (ODF standard / LibreOffice) */
                if (odt_write_str(wc, "<style:text-properties fo:hyphenate=\"true\" fo:hyphenation-remain-char-count=\"2\" fo:hyphenation-push-char-count=\"2\"/>") != 0)
                    return -1;
            }
        }

        if (odt_write_str(wc, "</style:style>") != 0)
            return -1;
    }

    /* Text styles */
    for (i = 0; i < sm->n_t_styles; i++)
    {
        char nm[8];

        odt_t_style_name(i, nm, sizeof(nm));

        if (odt_write_str(wc, "<style:style style:name=\"") != 0)
            return -1;

        if (odt_write_str(wc, nm) != 0)
            return -1;

        if (odt_write_str(wc, "\" style:family=\"text\">") != 0)
            return -1;

        if (odt_write_str(wc, "<style:text-properties") != 0)
            return -1;

        if (odt_write_text_props(wc, sm->t_masks[i]) != 0)
            return -1;

        if (hyph)
        {
            if (odt_write_str(wc, " fo:hyphenate=\"true\" fo:hyphenation-remain-char-count=\"2\" fo:hyphenation-push-char-count=\"2\"") != 0)
                return -1;
        }

        if (odt_write_str(wc, "/></style:style>") != 0)
            return -1;
    }

    if (odt_write_str(wc, "</office:automatic-styles>") != 0)
        return -1;

    return 0;
}

/* Export: write one EdLine's text as text:span runs (no <text:p> wrapper) */
static int odt_write_line_runs(OdtWriteCtx *wc, const struct Ed *ed, int line_idx, const OdtStyleMap *sm)
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

    /* Emit one text:span per attr run */
    if (n_runs > 0 && runs)
    {
        for (i = 0; i < n_runs; i++)
        {
            const EdAttrRun *r = &runs[i];
            int t_style;

            /* Fill gap before run with unstyled text */
            if (r->start > cur)
            {
                if (odt_write_escaped(wc, text + cur, r->start - cur) != 0)
                    return -1;
            }

            t_style = odt_map_t_style((OdtStyleMap *)sm, r->mask);

            if (t_style >= 0)
            {
                char nm[8];

                odt_t_style_name(t_style, nm, sizeof(nm));

                if (odt_write_str(wc, "<text:span text:style-name=\"") != 0)
                    return -1;

                if (odt_write_str(wc, nm) != 0)
                    return -1;

                if (odt_write_str(wc, "\">") != 0)
                    return -1;

                if (odt_write_escaped(wc, text + r->start, r->end - r->start) != 0)
                    return -1;

                if (odt_write_str(wc, "</text:span>") != 0)
                    return -1;
            }
            else
            {
                /* Mask is non-zero but no slot: emit raw text */
                if (odt_write_escaped(wc, text + r->start, r->end - r->start) != 0)
                    return -1;
            }

            cur = r->end;
        }
    }

    /* Tail text after last run */
    if (cur < len)
    {
        if (odt_write_escaped(wc, text + cur, len - cur) != 0)
            return -1;
    }

    return 0;
}

/* Export: write one logical paragraph, group consecutive EdLines until LB_PARA, like RTF */
static int odt_write_para_group(OdtWriteCtx *wc, const struct Ed *ed, int start_idx, const OdtStyleMap *sm, int *out_next)
{
    EdLine *ln = ed->lines[start_idx];
    int p_style;
    int row = start_idx;
    int has_text = 0;
    EdLine *cur_ln = NULL;
    int len;

    /* Paragraph style reference */
    p_style = odt_map_p_style((OdtStyleMap *)sm, ln ? ln->para_align : EA_ALIGN_LEFT);

    if (p_style >= 0)
    {
        char nm[8];

        odt_p_style_name(p_style, nm, sizeof(nm));

        if (odt_write_str(wc, "<text:p text:style-name=\"") != 0)
            return -1;

        if (odt_write_str(wc, nm) != 0)
            return -1;

        if (odt_write_str(wc, "\">") != 0)
            return -1;
    }
    else
    {
        if (odt_write_str(wc, "<text:p text:style-name=\"Standard\">") != 0)
            return -1;
    }

    /* Emit each EdLine's text, joining with space for LB_SPACE (like RTF) */
    while (row < ed->count)
    {
        cur_ln = ed->lines[row];
        len = ed_line_len(ed, row);

        if (len > 0)
            has_text = 1;

        if (odt_write_line_runs(wc, ed, row, sm) != 0)
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
            if (odt_write_str(wc, " ") != 0)
                return -1;
        }

        row++;
    }

    /* Empty paragraph: ODT allows <text:p></text:p> or self-closing */
    if (odt_write_str(wc, "</text:p>") != 0)
        return -1;

    *out_next = row;

    return 0;
}

/* Export: resolve font from config (same as DOCX) */
static void odt_resolve_font(const TeConfig *cfg, char *font_name, int font_name_sz, int *font_size_pt)
{
    const char *font_path = NULL;

    font_name[0] = '\0';
    *font_size_pt = 0;

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
        *font_size_pt = cfg->print_font_size;
    else if (cfg->ttf_size > 0)
        *font_size_pt = cfg->ttf_size;
}

int odt_export(const struct Ed *ed, const char *path, const TeConfig *cfg, char *err, size_t errsz)
{
    FILE *fp = NULL;
    ZipWriter *zw = NULL;
    OdtWriteCtx wc;
    OdtStyleMap sm;
    char font_name[128];
    int font_size_pt;
    int i;
    int next;
    char tmp[256];
    int n;
    const char *mid_ptr = NULL;
    int mid_len;
    int hyph = 0;

    if (err && errsz > 0)
        err[0] = '\0';

    if (!ed || !path)
    {
        odt_seterr(err, errsz, "invalid arguments");
        return -1;
    }

    fp = fopen(path, "wb");

    if (!fp)
    {
        odt_seterr(err, errsz, "cannot create file");
        return -1;
    }

    zw = zip_open_write(fp);

    if (!zw)
    {
        odt_seterr(err, errsz, "cannot start ZIP");
        fclose(fp);
        return -1;
    }

    memset(&wc, 0, sizeof(wc));

    wc.zw = zw;

    /* mimetype: must be first file, Stored, no extra field, no data descriptor. LibreOffice rejects mimetype entries that use a data descriptor (GP bit 3) */
    if (zip_write_stored_entry(zw, "mimetype", (const unsigned char *)ODT_MIMETYPE, (int)(sizeof(ODT_MIMETYPE) - 1)) != 0)
    {
        odt_seterr(err, errsz, "ZIP write failed on mimetype");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* META-INF/manifest.xml */
    if (zip_start_entry(zw, "META-INF/manifest.xml", ODT_WRITE_METHOD) != 0 || zip_write_entry(zw, (const unsigned char *)ODT_MANIFEST, (int)(sizeof(ODT_MANIFEST) - 1)) < 0 || zip_end_entry(zw) != 0)
    {
        odt_seterr(err, errsz, "ZIP write failed on manifest.xml");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* styles.xml: default font and size from config */
    odt_resolve_font(cfg, font_name, sizeof(font_name), &font_size_pt);

    if (zip_start_entry(zw, "styles.xml", ODT_WRITE_METHOD) != 0)
    {
        odt_seterr(err, errsz, "ZIP start failed on styles.xml");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* Write prolog directly: it is 423 bytes, too long for tmp[] */
    if (zip_write_entry(zw, (const unsigned char *)ODT_STYLES_PROLOG, (int)(sizeof(ODT_STYLES_PROLOG) - 1)) < 0)
    {
        odt_seterr(err, errsz, "ZIP write failed on styles.xml");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* Font family and size attributes */
    if (font_name[0])
    {
        n = snprintf(tmp, sizeof(tmp), " fo:font-family=\"%s\"", font_name);

        if (n > 0 && n < (int)sizeof(tmp))
        {
            if (zip_write_entry(zw, (const unsigned char *)tmp, n) < 0)
            {
                odt_seterr(err, errsz, "ZIP write failed on styles.xml");

                zip_close_write(zw);
                fclose(fp);
                return -1;
            }
        }
    }

    if (font_size_pt > 0)
    {
        n = snprintf(tmp, sizeof(tmp), " fo:font-size=\"%dpt\"", font_size_pt);

        if (n > 0 && n < (int)sizeof(tmp))
        {
            if (zip_write_entry(zw, (const unsigned char *)tmp, n) < 0)
            {
                odt_seterr(err, errsz, "ZIP write failed on styles.xml");

                zip_close_write(zw);
                fclose(fp);
                return -1;
            }
        }
    }

    /* Hyphenation properties in default paragraph style when Alt+E is on */
    mid_ptr = ODT_STYLES_MID;
    mid_len = (int)(sizeof(ODT_STYLES_MID) - 1);

#ifdef HAVE_HYPHEN
    if (cfg && cfg->hyph_wrap_enabled)
    {
        /* fo:hyphenate in text-properties (ODF standard / LibreOffice) */
        n = snprintf(tmp, sizeof(tmp), " fo:hyphenate=\"true\" fo:hyphenation-remain-char-count=\"2\" fo:hyphenation-push-char-count=\"2\"");

        if (n > 0 && n < (int)sizeof(tmp))
        {
            if (zip_write_entry(zw, (const unsigned char *)tmp, n) < 0)
            {
                odt_seterr(err, errsz, "ZIP write failed on styles.xml");

                zip_close_write(zw);
                fclose(fp);
                return -1;
            }
        }

        /* Also in paragraph-properties for OnlyOffice compatibility (non-standard but harmless) */
        n = snprintf(tmp, sizeof(tmp), "/><style:paragraph-properties fo:hyphenate=\"true\" fo:hyphenation-remain-char-count=\"2\" fo:hyphenation-push-char-count=\"2\"");

        if (n > 0 && n < (int)sizeof(tmp))
        {
            if (zip_write_entry(zw, (const unsigned char *)tmp, n) < 0)
            {
                odt_seterr(err, errsz, "ZIP write failed on styles.xml");

                zip_close_write(zw);
                fclose(fp);
                return -1;
            }
        }

        /* Don't skip the "/>" at start of ODT_STYLES_MID: it closes paragraph-properties */
    }
#endif

    if (zip_write_entry(zw, (const unsigned char *)mid_ptr, mid_len) < 0)
    {
        odt_seterr(err, errsz, "ZIP write failed on styles.xml");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    if (zip_end_entry(zw) != 0)
    {
        odt_seterr(err, errsz, "ZIP end failed on styles.xml");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* content.xml: first pass to collect styles, then write */
    memset(&sm, 0, sizeof(sm));

    for (i = 0; i < ed->count; i++)
    {
        EdLine *ln = ed->lines[i];
        const EdAttrRun *runs = NULL;
        int n_runs;
        int j;

        odt_map_p_style(&sm, ln ? ln->para_align : EA_ALIGN_LEFT);

        n_runs = ed_attr_runs(ln, &runs);

        for (j = 0; j < n_runs; j++)
            odt_map_t_style(&sm, runs[j].mask);
    }

    /* Now write content.xml with the collected styles */
    if (zip_start_entry(zw, "content.xml", ODT_WRITE_METHOD) != 0)
    {
        odt_seterr(err, errsz, "ZIP start failed on content.xml");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* Prolog */
    if (zip_write_entry(zw, (const unsigned char *)ODT_CONTENT_PROLOG, (int)(sizeof(ODT_CONTENT_PROLOG) - 1)) < 0)
    {
        odt_seterr(err, errsz, "ZIP write failed on content.xml prolog");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* Automatic styles */
#ifdef HAVE_HYPHEN
    if (cfg && cfg->hyph_wrap_enabled)
        hyph = 1;
#endif

    if (odt_write_auto_styles(&wc, &sm, hyph) != 0)
    {
        odt_seterr(err, errsz, "write failed on automatic-styles");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* Body */
    if (odt_write_str(&wc, "<office:body><office:text>") != 0)
    {
        odt_seterr(err, errsz, "write failed on body open");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    /* Group EdLines into logical paragraphs using brk, like RTF */
    i = 0;
    while (i < ed->count)
    {
        next = i;

        if (odt_write_para_group(&wc, ed, i, &sm, &next) != 0)
        {
            odt_seterr(err, errsz, "write failed on paragraph");

            zip_close_write(zw);
            fclose(fp);
            return -1;
        }

        i = next;
    }

    if (odt_write_str(&wc, ODT_CONTENT_EPILOG) != 0 || odt_write_flush(&wc) != 0 || zip_end_entry(zw) != 0)
    {
        odt_seterr(err, errsz, "write failed at content.xml close");

        zip_close_write(zw);
        fclose(fp);
        return -1;
    }

    if (zip_close_write(zw) != 0)
    {
        odt_seterr(err, errsz, "ZIP close failed");

        fclose(fp);
        return -1;
    }

    fclose(fp);

    return 0;
}
