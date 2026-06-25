/*
 * tinyedit - Text editor for AmigaOS
 *
 * Copyright (C) 2026 Tanausú M. 39:190/101@amiganet 2:341/207@fidonet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 or later.
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
void pf_dir_close(PfDir *d);

/* Portable file locking */
typedef struct PfLockFile PfLockFile;
PfLockFile *pf_lock_create(const char *path);
void pf_lock_release(PfLockFile *lk);

/* Portable atomic rename */
int pf_atomic_rename(const char *from, const char *to);

/* Portable file removal */
int pf_remove_file(const char *path);

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

#ifdef PLATFORM_AMIGA
/* Sanitize UTF-8 filename to ASCII for AmigaOS filesystem */
char *port_sanitize_filename(const char *utf8_name);
#endif

#endif
