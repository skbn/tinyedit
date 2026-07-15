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

/* undo.h -- undo and redo as line range deltas, never whole document copies */

#ifndef UNDO_H
#define UNDO_H

/* Capture the lines about to change, returns -1 when out of memory */
int undo_begin(Ed *ed, int row, int n_before);

/* Capture what those lines became and push the delta */
int undo_commit(Ed *ed, int n_after);

/* Drop the capture without recording it, for failed edits */
void undo_abort(Ed *ed);

/* Mark the next commit as coalescable with the previous one when typing */
void undo_typing_hint(Ed *ed, int on);

/* Commit whatever an edit left pending, called once per key */
void ed_undo_settle(Ed *ed);

void undo_free_all(Ed *ed);

/* True when row falls inside the open capture, doc delta adjusted */
int undo_pending_contains(Ed *ed, int row, int span);

#endif
