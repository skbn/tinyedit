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

/* undo.c -- undo and redo as line range deltas */

#include <stdlib.h>
#include <string.h>
#include "editor.h"
#include "undo.h"

EdLine *ed_line_clone(Ed *ed, const EdLine *src);
void ed_line_destroy(Ed *ed, EdLine *ln);
int ed_lines_splice(Ed *ed, int row, int n_remove, EdLine **insert, int n_insert);

static void lines_release(Ed *ed, EdLine **v, int n)
{
    int i;

    if (!v)
        return;

    for (i = 0; i < n; i++)
        ed_line_destroy(ed, v[i]);

    free(v);
}

/* Deep copy of the document lines [row, row+n) */
static EdLine **lines_capture(Ed *ed, int row, int n)
{
    EdLine **v = NULL;
    int i;

    if (n < 0 || row < 0 || row + n > ed->count)
        return NULL;

    v = (EdLine **)malloc((size_t)(n > 0 ? n : 1) * sizeof(EdLine *));

    if (!v)
        return NULL;

    for (i = 0; i < n; i++)
    {
        v[i] = ed_line_clone(ed, ed->lines[row + i]);

        if (!v[i])
        {
            lines_release(ed, v, i);
            return NULL;
        }
    }

    return v;
}

static void op_release(Ed *ed, UndoOp *op)
{
    lines_release(ed, op->before, op->n_before);
    lines_release(ed, op->after, op->n_after);

    op->before = NULL;
    op->after = NULL;
    op->n_before = 0;
    op->n_after = 0;
}

static void group_release(Ed *ed, UndoGroup *g)
{
    int i;

    for (i = 0; i < g->count; i++)
        op_release(ed, &g->ops[i]);

    free(g->ops);

    g->ops = NULL;
    g->count = 0;
    g->cap = 0;
}

static void stack_release(Ed *ed, UndoGroup *stack, int n)
{
    int i;

    for (i = 0; i < n; i++)
        group_release(ed, &stack[i]);
}

/* Make room for one more group, dropping the oldest past the limit */
static int stack_room(Ed *ed, UndoGroup **stack, int *top, int *cap, int max)
{
    UndoGroup *t = NULL;

    if (max > 0 && *top >= max)
    {
        group_release(ed, &(*stack)[0]);
        memmove(*stack, *stack + 1, (size_t)(*top - 1) * sizeof(UndoGroup));
        (*top)--;
    }

    if (*top + 1 > *cap)
    {
        int nc = *cap > 0 ? *cap * 2 : 16;

        t = (UndoGroup *)realloc(*stack, (size_t)nc * sizeof(UndoGroup));

        if (!t)
            return -1;

        *stack = t;
        *cap = nc;
    }

    return 0;
}

void undo_free_all(Ed *ed)
{
    stack_release(ed, ed->undo_stack, ed->undo_top);
    stack_release(ed, ed->redo_stack, ed->redo_top);

    free(ed->undo_stack);
    free(ed->redo_stack);

    ed->undo_stack = NULL;
    ed->redo_stack = NULL;
    ed->undo_top = 0;
    ed->redo_top = 0;
    ed->undo_cap = 0;
    ed->redo_cap = 0;

    lines_release(ed, ed->pending_before, ed->pending_n);

    ed->pending_before = NULL;
    ed->pending_n = 0;
    ed->pending_row = -1;
}

void ed_redo_clear(Ed *ed)
{
    if (!ed)
        return;

    stack_release(ed, ed->redo_stack, ed->redo_top);

    ed->redo_top = 0;
}

void ed_clear_undo_redo(Ed *ed)
{
    if (!ed)
        return;

    stack_release(ed, ed->undo_stack, ed->undo_top);

    ed->undo_top = 0;
    ed->undo_typing = 0;

    ed_redo_clear(ed);

    lines_release(ed, ed->pending_before, ed->pending_n);

    ed->pending_before = NULL;
    ed->pending_n = 0;
    ed->pending_row = -1;
}

void ed_set_undo_levels(Ed *ed, int levels)
{
    if (!ed || levels < 1)
        return;

    ed->undo_max = levels;
    ed->redo_max = levels;
}

int ed_undo_depth(const Ed *ed)
{
    if (!ed)
        return 0;

    /* An in-flight capture counts as one undoable step */
    return ed->undo_top + (ed->pending_before ? 1 : 0);
}

int ed_redo_depth(const Ed *ed)
{
    return ed ? ed->redo_top : 0;
}

void undo_typing_hint(Ed *ed, int on)
{
    if (ed)
        ed->undo_typing = on ? 1 : 0;
}

/* Close the current step: the next edit starts a fresh one and typing coalescing is broken here */
void ed_save_undo(Ed *ed)
{
    if (!ed)
        return;

    ed->undo_typing = 0;
}

int undo_begin(Ed *ed, int row, int n_before)
{
    if (!ed)
        return 0;

    /* An op outside the open capture settles it, recording resumes */
    if (ed->undo_snapshot_mode)
    {
        if (undo_pending_contains(ed, row, n_before))
            return 0;

        ed_undo_settle(ed);
    }

    /* A stray capture means an edit failed midway, drop it */
    if (ed->pending_before)
        undo_abort(ed);

    ed->pending_before = lines_capture(ed, row, n_before);

    if (!ed->pending_before)
        return -1;

    ed->pending_row = row;
    ed->pending_n = n_before;
    ed->pending_row_cur = ed->row;
    ed->pending_col_cur = ed->col;
    ed->pending_doc_count = ed->count;

    return 0;
}

void undo_abort(Ed *ed)
{
    if (!ed)
        return;

    lines_release(ed, ed->pending_before, ed->pending_n);

    ed->pending_before = NULL;
    ed->pending_n = 0;
    ed->pending_row = -1;
}

/* Append the delta to the current group, or start a new group */
static int push_delta(Ed *ed, int row, EdLine **before, int n_before, EdLine **after, int n_after, int cur_row, int cur_col)
{
    UndoGroup *g = NULL;
    UndoOp *op = NULL;

    ed_redo_clear(ed);

    /* Typing on the same single line just refreshes the after image */
    if (ed->undo_typing && ed->undo_top > 0 && ed->undo_stack && n_before == 1 && n_after == 1)
    {
        g = &ed->undo_stack[ed->undo_top - 1];

        if (g->coalesce && g->count == 1 && g->ops[0].row == row && g->ops[0].n_after == 1)
        {
            lines_release(ed, g->ops[0].after, g->ops[0].n_after);

            g->ops[0].after = after;
            g->ops[0].n_after = n_after;
            g->row_after = ed->row;
            g->col_after = ed->col;

            lines_release(ed, before, n_before);

            return 0;
        }
    }

    if (stack_room(ed, &ed->undo_stack, &ed->undo_top, &ed->undo_cap, ed->undo_max) != 0)
        return -1;

    g = &ed->undo_stack[ed->undo_top++];

    memset(g, 0, sizeof(*g));

    g->row_before = cur_row;
    g->col_before = cur_col;

    if (g->count + 1 > g->cap)
    {
        int nc = g->cap > 0 ? g->cap * 2 : 4;
        UndoOp *t = (UndoOp *)realloc(g->ops, (size_t)nc * sizeof(UndoOp));

        if (!t)
        {
            ed->undo_top--;

            return -1;
        }

        g->ops = t;
        g->cap = nc;
    }

    op = &g->ops[g->count++];

    op->row = row;
    op->before = before;
    op->n_before = n_before;
    op->after = after;
    op->n_after = n_after;

    g->row_after = ed->row;
    g->col_after = ed->col;
    g->coalesce = ed->undo_typing && g->count == 1 && n_before == 1 && n_after == 1;

    ed->modified = 1;

    return 0;
}

int undo_commit(Ed *ed, int n_after)
{
    EdLine **after = NULL;

    if (!ed || ed->undo_snapshot_mode)
        return 0;

    if (!ed->pending_before)
        return 0;

    after = lines_capture(ed, ed->pending_row, n_after);

    if (!after)
    {
        undo_abort(ed);
        return -1;
    }

    if (push_delta(ed, ed->pending_row, ed->pending_before, ed->pending_n, after, n_after, ed->pending_row_cur, ed->pending_col_cur) != 0)
    {
        lines_release(ed, after, n_after);
        undo_abort(ed);

        return -1;
    }

    ed->pending_before = NULL;
    ed->pending_n = 0;
    ed->pending_row = -1;

    return 0;
}

/* Put src lines into the document at row, replacing n_remove of them */
static int apply_lines(Ed *ed, int row, int n_remove, EdLine **src, int n_src)
{
    EdLine **copy = NULL;
    int i;

    copy = (EdLine **)malloc((size_t)(n_src > 0 ? n_src : 1) * sizeof(EdLine *));

    if (!copy)
        return -1;

    for (i = 0; i < n_src; i++)
    {
        copy[i] = ed_line_clone(ed, src[i]);

        if (!copy[i])
        {
            lines_release(ed, copy, i);
            return -1;
        }
    }

    if (ed_lines_splice(ed, row, n_remove, copy, n_src) != 0)
    {
        lines_release(ed, copy, n_src);
        return -1;
    }

    free(copy);

    return 0;
}

int ed_undo(Ed *ed)
{
    UndoGroup *g = NULL;
    int i;

    if (!ed)
        return -1;

    /* An in-flight capture becomes a delta so the edit is undoable */
    ed_undo_settle(ed);

    if (ed->undo_top <= 0)
        return -1;

    ed->undo_typing = 0;
    g = &ed->undo_stack[--ed->undo_top];

    /* Later deltas first, so earlier row numbers stay valid */
    for (i = g->count - 1; i >= 0; i--)
    {
        UndoOp *op = &g->ops[i];

        if (apply_lines(ed, op->row, op->n_after, op->before, op->n_before) != 0)
            return -1;
    }

    ed->row = g->row_before;
    ed->col = g->col_before;
    ed->block.active = 0;
    ed->modified = 1;

    ed_clamp(ed);

    if (stack_room(ed, &ed->redo_stack, &ed->redo_top, &ed->redo_cap, ed->redo_max) != 0)
    {
        group_release(ed, g);
        return 0;
    }

    ed->redo_stack[ed->redo_top++] = *g;

    memset(g, 0, sizeof(*g));

    return 0;
}

int ed_redo(Ed *ed)
{
    UndoGroup *g = NULL;
    int i;

    if (!ed)
        return -1;

    ed_undo_settle(ed);

    if (ed->redo_top <= 0)
        return -1;

    ed->undo_typing = 0;
    g = &ed->redo_stack[--ed->redo_top];

    for (i = 0; i < g->count; i++)
    {
        UndoOp *op = &g->ops[i];

        if (apply_lines(ed, op->row, op->n_before, op->after, op->n_after) != 0)
            return -1;
    }

    ed->row = g->row_after;
    ed->col = g->col_after;
    ed->block.active = 0;
    ed->modified = 1;

    ed_clamp(ed);

    if (stack_room(ed, &ed->undo_stack, &ed->undo_top, &ed->undo_cap, ed->undo_max) != 0)
    {
        group_release(ed, g);
        return 0;
    }

    ed->undo_stack[ed->undo_top++] = *g;

    memset(g, 0, sizeof(*g));

    return 0;
}

/* Close any capture an edit left open so the engine can never stay wedged */
void ed_undo_settle(Ed *ed)
{
    int n_after;

    if (!ed)
        return;

    if (ed->pending_before)
    {
        n_after = ed->pending_n + (ed->count - ed->pending_doc_count);

        if (n_after < 0)
            n_after = 0;

        ed->undo_snapshot_mode = 0;

        /* A snapshot delta must never merge into a typing run */
        ed->undo_typing = 0;

        undo_commit(ed, n_after);
        ed_save_undo(ed);
    }

    ed->undo_snapshot_mode = 0;
}

/* True when [row, row+span) falls inside the open capture, delta adjusted */
int undo_pending_contains(Ed *ed, int row, int span)
{
    int n;

    if (!ed || !ed->pending_before || ed->pending_row < 0)
        return 0;

    n = ed->pending_n + (ed->count - ed->pending_doc_count);

    if (n < 1)
        n = 1;

    if (span < 1)
        span = 1;

    return row >= ed->pending_row && row + span <= ed->pending_row + n;
}
