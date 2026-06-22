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

#ifndef SPELL_H
#define SPELL_H

#include <stddef.h>

#ifndef SPELL_CACHE_N
#define SPELL_CACHE_N 64
#endif

#ifndef SPELL_MAX_WORD
#define SPELL_MAX_WORD 128
#endif

#ifndef SPELL_MAX_SUGGS
#define SPELL_MAX_SUGGS 32
#endif

/* opaque: internal structure in spell.c */
struct spell;
typedef struct spell SpellChecker;

/* open .aff + .dic, returns NULL on error */
struct spell *spell_new(const char *aff_path, const char *dic_path);
void spell_free(struct spell *s);

/* spell_check: 1 if correct, 0 if not, -1 error */
int spell_check(struct spell *s, const char *word);

/* suggest: returns array of suggestion pointers, writes count in *n_suggestions */
char **spell_suggest(struct spell *s, const char *word, int *n_suggestions);

/* free suggestion array returned by spell_suggest (no-op in this engine) */
void spell_free_suggestions(struct spell *s, char **suggestions, int n_suggestions);

void spell_cache_clear(struct spell *s);
const char *spell_get_encoding(struct spell *s);

/* return 1 if spell checker is available (compiled) */
int spell_is_available(void);

/* list available dictionaries in directory, returns base names (without .dic) */
char **spell_list_dictionaries(const char *dir_path, int *n_dicts);
void spell_free_dictionaries(char **dicts, int n_dicts);

/* stubs not supported by current engine, return error (-1/0) */
int spell_add_word(struct spell *s, const char *word);
int spell_remove_word(struct spell *s, const char *word);
int spell_stem(struct spell *s, const char *word, char ***out_list);
int spell_generate(struct spell *s, const char *word, const char *example, char ***out_list);
void spell_free_list(struct spell *s, char **list, int n);
int spell_load_custom(struct spell *s, const char *path);
int spell_add_to_custom_dict(struct spell *s, const char *word, const char *custom_dict_path);

#endif
