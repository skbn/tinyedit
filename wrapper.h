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

/* wrapper.h -- FTN message library for CrashMail/CrashEdit */
#ifndef WRAPPER_H
#define WRAPPER_H

#include <stdio.h> /* snprintf used by wrapper_build_pid */

#include "core/utf8.h"
#include "core/charset.h"
#include "core/keys.h"
#include "components/config.h"
#include "components/editor.h"

/* OS detection for PID kludge (compile-time only, no runtime cost) */
#if defined(PLATFORM_AMIGA) || defined(__AMIGA__) || defined(AMIGA)
#define WRAPPER_OS_NAME "AmigaOS"
#elif defined(__APPLE__) && defined(__MACH__)
#define WRAPPER_OS_NAME "MacOS"
#elif defined(__linux__)
#define WRAPPER_OS_NAME "Linux"
#elif defined(__FreeBSD__)
#define WRAPPER_OS_NAME "FreeBSD"
#elif defined(__OpenBSD__)
#define WRAPPER_OS_NAME "OpenBSD"
#elif defined(__NetBSD__)
#define WRAPPER_OS_NAME "NetBSD"
#elif defined(__DragonFly__)
#define WRAPPER_OS_NAME "DragonFlyBSD"
#elif defined(__CYGWIN__)
#define WRAPPER_OS_NAME "Cygwin"
#elif defined(_WIN32) || defined(_WIN64)
#define WRAPPER_OS_NAME "Windows"
#elif defined(__unix__) || defined(__unix)
#define WRAPPER_OS_NAME "Unix"
#else
#define WRAPPER_OS_NAME "unknown"
#endif

#define WRAPPER_PID "TinyEdit 1.0.5 " WRAPPER_OS_NAME

#endif
