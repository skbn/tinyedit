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
#include <wchar.h>

#ifndef SPELL_CACHE_N
#ifdef PLATFORM_AMIGA
#define SPELL_CACHE_N 512
#else
#define SPELL_CACHE_N 2048
#endif
#endif

#ifndef SPELL_MAX_WORD
#define SPELL_MAX_WORD 128
#endif

#ifndef SPELL_MAX_SUGGS
#define SPELL_MAX_SUGGS 32
#endif

#ifndef SPELL_STEP_CUSTOM
#define SPELL_STEP_CUSTOM 16
#endif

#define MS_NGRAM_LONGER_WORSE 1
#define MS_NGRAM_ANY_MISMATCH 2
#define MS_NGRAM_WEIGHTED 4
#define MS_MAX_ROOTS 10

/* opaque: internal structure in spell.c */
struct spell;

#ifndef THES_SPELLCHECKER_TYPEDEF
#define THES_SPELLCHECKER_TYPEDEF
typedef struct spell SpellChecker;
#endif

/* open .aff + .dic, returns NULL on error */
struct spell *spell_new(const char *aff_path, const char *dic_path);
void spell_free(struct spell *s);

/* spell_check: 1 if correct, 0 if not, -1 error */
int spell_check(struct spell *s, const char *word);

/* suggest: returns array of suggestion pointers, writes count in *n_suggestions */
char **spell_suggest(struct spell *s, const char *word, int *n_suggestions);

/* free suggestion array returned by spell_suggest (no-op in this engine) */
void spell_free_suggestions(struct spell *s, char **suggestions, int n_suggestions);

const char *spell_get_encoding(struct spell *s);

/* Add word to in-memory custom dictionary returns 0 on success -1 on error duplicates ignored */
int spell_add_word(struct spell *s, const char *word);

/* Remove word previously added returns 1 if removed 0 if not present cannot remove from main .dic only custom */
int spell_remove_word(struct spell *s, const char *word);

/* Check if word is in user dictionary returns 1 yes 0 no */
int spell_is_custom_word(struct spell *s, const char *word);

/* Iterate custom dictionary returns NULL-terminated array owned by spell do not free n_out gets count */
const char *const *spell_custom_words(struct spell *s, int *n_out);

/* Load custom dictionary from UTF-8 text file one word per line comments start with # returns words loaded or -1 on error */
int spell_load_custom_dict(struct spell *s, const char *path);

/* Save custom dictionary to UTF-8 text file one word per line sorted returns 0 on success -1 on error */
int spell_save_custom_dict(struct spell *s, const char *path);

/* Clear in-memory custom dictionary does not touch file */
void spell_clear_custom_dict(struct spell *s);

/* Prefix search autocomplete find words in main and custom dict starting with prefix out buffer owned by spell valid until next call or spell_free */
int spell_prefix_search(struct spell *s, const char *prefix, const char **out, int out_cap);

/* Ignore list session-only not persisted words spell_check treats as correct this session cleared on spell_free or spell_clear_ignored */
int spell_ignore_word(struct spell *s, const char *word);
int spell_is_ignored(struct spell *s, const char *word);
void spell_clear_ignored(struct spell *s);

/* return 1 if spell checker is available (compiled) */

/* Unicode word-char classifier. Use instead of iswalnum() for word boundaries */
int te_is_word_char(wint_t wc);

/* Like te_is_word_char but also accepts WORDCHARS from .aff (e.g. .', digits). Pass NULL for 's' for plain behaviour. Use this to extract whole words like "etc.", "Dr.", "don't" */
int te_is_word_char_ex(struct spell *s, wint_t wc);

/* list available dictionaries in directory, returns base names (without .dic) */
char **spell_list_dictionaries(const char *dir_path, int *n_dicts);
void spell_free_dictionaries(char **dicts, int n_dicts);

/* stubs not supported by current engine, return error (-1/0) */
int spell_stem(struct spell *s, const char *word, char ***out_list);
int spell_generate(struct spell *s, const char *word, const char *example, char ***out_list);
void spell_free_list(struct spell *s, char **list, int n);
int spell_load_custom(struct spell *s, const char *path);
int spell_add_to_custom_dict(struct spell *s, const char *word, const char *custom_dict_path);

#endif /* SPELL_H */
