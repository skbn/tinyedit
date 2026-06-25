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

/* Portable directory creation function */
int port_mkdir_one(const char *path);

/* Portable empty file creation function */
int port_file_create_empty(const char *path);

/* Portable configuration directory path */
void port_get_config_dir(char *buf, size_t bufsz);

/* Portable directory iterator (based on crashedit implementation) */
typedef struct PfDir PfDir;
PfDir *pf_dir_open(const char *path);
const char *pf_dir_next(PfDir *d);
void pf_dir_close(PfDir *d);

/* Portable directory test */
int pf_is_directory(const char *path);

/* Portable file locking */
typedef struct PfLockFile PfLockFile;
PfLockFile *pf_lock_create(const char *path);
void pf_lock_release(PfLockFile *lk);

/* Portable atomic rename */
int pf_atomic_rename(const char *from, const char *to);

/* Portable file removal */
int pf_remove_file(const char *path);

/* Portable sleep in milliseconds */
void pf_sleep_ms(unsigned ms);

#ifdef PLATFORM_AMIGA
/* Sanitize UTF-8 filename to ASCII for AmigaOS filesystem */
char *port_sanitize_filename(const char *utf8_name);
#endif

#endif
