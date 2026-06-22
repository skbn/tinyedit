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

#ifndef UI_THES_GLUE_H
#define UI_THES_GLUE_H

#include "te.h"
#if defined(PLATFORM_AMIGA)
#include "../spellchecker/spell.h" /* SpellChecker typedef */
#else
#include "../spell/spell.h" /* SpellChecker typedef */
#endif

void te_status(TeApp *app, const char *fmt, ...);

#define UI_APP_T TeApp
#define UI_APP_THES_HANDLE(a) (*((ThesHandle **)&(a)->thes_handle))
#define UI_APP_SPELL_HANDLE(a) ((SpellChecker *)(a)->spell_handle)
#define UI_APP_EDITOR(a) (te_app_get_editor(a))
#define UI_APP_STATUS_FN te_status
#define UI_APP_CFG(a) ((a)->cfg) /* tinyedit stores cfg inline */

#endif
