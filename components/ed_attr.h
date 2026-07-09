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

#ifndef ED_ATTR_H
#define ED_ATTR_H

/* Inline attribute mask bits */
#define EA_BOLD 0x01
#define EA_ITALIC 0x02
#define EA_UNDERLINE 0x04

/* Paragraph alignment values (EdLine.para_align) */
#define EA_ALIGN_LEFT 0
#define EA_ALIGN_CENTER 1
#define EA_ALIGN_RIGHT 2
#define EA_ALIGN_JUST 3

/* One styled run: [start, end) in character indices, full state carried */
typedef struct EdAttrRunStruct
{
    int start;
    int end;
    unsigned short mask; /* EA_* bits */
    short font_id;       /* -1 = document default */
    short size;          /* 0 = document default */
} EdAttrRun;

struct Ed;

#ifdef __cplusplus
extern "C"
{
#endif

/* EdLine is a typedef in editor.h; the hooks take it through void to keep the two headers decoupled would be worse, so editor.h must come first */
#ifdef WRAPPER_EDITOR_H

    /* Free the run array of a line (safe on NULL / empty) */
    void ed_attr_line_free(EdLine *ln);

    /* Apply attributes over [c1, c2): set bits, clear bits, or override font/size (font_id >= 0, size > 0). Returns 0 or -1 on alloc error */
    int ed_attr_line_apply(EdLine *ln, int c1, int c2, unsigned short mask_set, unsigned short mask_clear, short font_id, short size);

    /* State at a character position; font/size outputs may be NULL */
    unsigned short ed_attr_mask_at(const EdLine *ln, int col, short *font_id, short *size);

    /* Read-only access to the normalized run list; returns run count */
    int ed_attr_runs(const EdLine *ln, const EdAttrRun **out);

    /* Editing hooks: keep runs in step with the text primitives */
    void ed_attr_on_insert(EdLine *ln, int pos, int n);
    void ed_attr_on_delete(EdLine *ln, int pos, int n);
    void ed_attr_on_truncate(EdLine *ln, int pos);

    /* Copy the runs of src covering [start, start+count) into dst rebased to 0 (dst must have no runs yet). Returns 0 or -1 */
    int ed_attr_copy_slice(const EdLine *src, int start, int count, EdLine *dst);

    /* Append the runs of src covering [start, start+count) onto dst at position at (dst text length before the append). Returns 0 or -1 */
    int ed_attr_append_slice(EdLine *dst, int at, const EdLine *src, int start, int count);

    /* Multi-line apply across (r1,c1)..(r2,c2), inclusive rows. Returns 0 */
    int ed_attr_apply_range(struct Ed *ed, int r1, int c1, int r2, int c2, unsigned short mask_set, unsigned short mask_clear, short font_id, short size);

#endif /* WRAPPER_EDITOR_H */

#ifdef __cplusplus
}
#endif

#endif /* ED_ATTR_H */
