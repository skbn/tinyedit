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

#ifndef UI_TTS_H
#define UI_TTS_H

#include "te.h"

#ifdef HAVE_TTS

/* Load/reload TTS from config, idempotent, called at startup and after Setup */
void ui_tts_load_from_config(TeApp *app);

/* Free handle, safe if NULL */
void ui_tts_unload(TeApp *app);

/* Speak selection or paragraph, same logic as translate */
void ui_tts_speak_action(TeApp *app);

/* Speak from cursor to end of document */
void ui_tts_speak_all_action(TeApp *app);

/* Dictate entire document from line 0, uses overflow buffer for large docs */
void ui_tts_speak_doc_action(TeApp *app);

/* Toggle pause/resume */
void ui_tts_pause_toggle(TeApp *app);

/* Stop and clear queue */
void ui_tts_stop(TeApp *app);

/* Modal popup for voice/rate/pitch/volume, applies live and optionally persists */
void ui_tts_popup(TeApp *app);

/* Pump TTS state machine, return 1 if status bar needs refresh */
int ui_tts_tick(TeApp *app);

/* Return 1 if speech playing, used for short input timeout */
int ui_tts_is_busy(TeApp *app);

/* Amiga: OR returned mask into Wait() in ncursesw_amiga event loop */
unsigned long ui_tts_amiga_sigmask(TeApp *app);

#endif /* HAVE_TTS */

#endif /* UI_TTS_H */
