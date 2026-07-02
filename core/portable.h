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

#ifndef PORTABLE_H
#define PORTABLE_H

#include <stddef.h>
#include <time.h>

/* Portable directory creation function */
int port_mkdir_one(const char *path);

/* Portable recursive directory creation */
int pf_mkdir_path(const char *path);

/* Portable ensure directory exists */
int pf_ensure_dir(const char *path);

/* Portable empty file creation function */
int port_file_create_empty(const char *path);

/* Portable configuration directory path */
void port_get_config_dir(char *buf, size_t bufsz);

/* Portable path existence test */
int pf_path_exists(const char *path);

/* Portable directory test */
int pf_is_directory(const char *path);

/* Portable regular-file test */
int pf_is_regular_file(const char *path);

/* Portable directory iterator (based on crashedit implementation) */
typedef struct PfDir PfDir;

PfDir *pf_dir_open(const char *path);
const char *pf_dir_next(PfDir *d);
const char *pf_dir_next_entry(PfDir *d, int *is_dir);
void pf_dir_close(PfDir *d);

/* Portable file locking */
typedef struct PfLockFile PfLockFile;
PfLockFile *pf_lock_create(const char *path);
void pf_lock_release(PfLockFile *lk);

/* Portable atomic rename */
int pf_atomic_rename(const char *from, const char *to);

/* Portable file removal */
int pf_remove_file(const char *path);

/* Remove the autosave swap companion for a file */
int pf_remove_swp(const char *path);

/* Portable binary file copy */
int pf_copy_file(const char *src, const char *dst);

/* Portable file move (rename, with copy+delete fallback) */
int pf_move_file(const char *src, const char *dst);

/* Portable file size */
long pf_get_file_size(const char *path);

/* Portable file modification time */
long pf_get_file_mtime(const char *path);

/* Portable path join */
void pf_path_join(char *out, size_t outsize, const char *base, const char *sub);

/* Portable safe strncpy (always NUL-terminates) */
void pf_safe_strncpy(char *dst, const char *src, size_t dstsize);

/* Portable wildcard match (* and ?, case-insensitive) */
int pf_wildmatch(const char *pat, const char *str);

/* Portable wildcard test */
int pf_is_wildcard(const char *s);

/* Portable sleep in milliseconds */
void pf_sleep_ms(unsigned ms);

/* Monotonic clock in milliseconds (0 if unsupported) */
unsigned long pf_now_ms(void);

/* Word counting helpers */
int pf_count_words_wcs(const wchar_t *s, int n);
int pf_count_words_utf8(const char *s);

/* Atomic write (tmp + rename) */
int pf_atomic_write(const char *path, const void *data, long len);

/* Compose autosave swap path */
void pf_swap_path(const char *path, char *out, size_t outsize);

/* Recursive grep helper */
typedef int (*pf_grep_cb_t)(void *user, const char *path, int line_no, const char *line_text);

int pf_grep_files(const char *root_dir, const char *ext_csv, const char *needle, int max_depth, pf_grep_cb_t cb, void *user);

#ifdef PLATFORM_AMIGA
/* Sanitize UTF-8 filename to ASCII for AmigaOS filesystem */
char *port_sanitize_filename(const char *utf8_name);
#endif

#ifdef PLATFORM_WIN32
/* Convert UTF-8 <-> UTF-16 for Windows Unicode API calls (caller frees result) */
wchar_t *pf_utf8_to_utf16(const char *s);
char *pf_utf16_to_utf8(const wchar_t *w);
#endif

#endif
