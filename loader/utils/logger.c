/*
 * utils/logger.c
 *
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/logger.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#define COLOR_RED ""
#define COLOR_ORANGE ""
#define COLOR_BLUE ""

#define COLOR_END ""

static SceKernelLwMutexWork _log_mutex;
static volatile short int _log_mutex_inited = 0;

static char buffer_a[2048];
static char buffer_b[2048];

#define LOG_LOCK \
    if (!_log_mutex_inited) { \
        int ret = sceKernelCreateLwMutex(&_log_mutex, "log_lock", 0, 0, NULL); \
        if (ret < 0) { \
            sceClibPrintf("Error: failed to create log mutex: 0x%x\n", ret); \
            return; \
        } \
        _log_mutex_inited = 1; \
    } \
    sceKernelLockLwMutex(&_log_mutex, 1, NULL);

#define LOG_UNLOCK \
    if (_log_mutex_inited) { \
        sceKernelUnlockLwMutex(&_log_mutex, 1); \
    }

#include <psp2/io/fcntl.h>
#include <string.h>

// Mirror everything to ux0:data/gof2hd/log.txt (capped at 4 MB).
static SceUID _log_fd = -1;
static unsigned int _log_written = 0;
static void _log_file(const char *s) {
    if (_log_fd < 0 && _log_written == 0) {
        _log_fd = sceIoOpen(DATA_PATH "log.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        _log_written = 1;
    }
    if (_log_fd < 0 || _log_written > 4 * 1024 * 1024) return;
    unsigned int n = strlen(s);
    sceIoWrite(_log_fd, s, n);
    _log_written += n;
}

void loader_log_raw(const char *s) { _log_file(s); }

// vitaGL's internal error log (built with LOG_ERRORS), capped so a
// per-frame error can't flood the card.
void loader_vgl_log(const char *fmt, ...) {
    static int count = 0;
    static char buf[1024];
    if (count >= 3000) return;
    count++;
    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(buf + 7, sizeof(buf) - 8, fmt, list);
    va_end(list);
    sceClibMemcpy(buf, "[VGL]  ", 7);
    _log_file(buf);
}

#define LOG_PRINT \
    va_list list; \
    va_start(list, fmt); \
    sceClibVsnprintf(buffer_b, sizeof(buffer_b), buffer_a, list); \
    va_end(list); \
    sceClibPrintf(buffer_b); \
    _log_file(buffer_b);

void _log_info(const char *fi, int li, const char *fn, const char* fmt, ...) {
#if 1 // always log in this build
    LOG_LOCK

    sceClibSnprintf(buffer_a, sizeof(buffer_a), "%s[INFO] %s%s\n",
                    COLOR_BLUE, fmt, COLOR_END);

    LOG_PRINT
    LOG_UNLOCK
#endif
}

void _log_warn(const char *fi, int li, const char *fn, const char* fmt, ...) {
#if 1 // always log in this build
    LOG_LOCK

    sceClibSnprintf(buffer_a, sizeof(buffer_a), "%s[WARN][%s:%d][%s] %s%s\n",
                    COLOR_ORANGE, fi, li, fn, fmt, COLOR_END);

    LOG_PRINT
    LOG_UNLOCK
#endif
}

void _log_debug(const char *fi, int li, const char *fn, const char* fmt, ...) {
#if 1 // always log in this build
    LOG_LOCK

    sceClibSnprintf(buffer_a, sizeof(buffer_a), "[DBG][%s:%d][%s] %s\n",
                    fi, li, fn, fmt);

    LOG_PRINT
    LOG_UNLOCK
#endif
}

void _log_error(const char *fi, int li, const char *fn, const char* fmt, ...) {
    LOG_LOCK

    sceClibSnprintf(buffer_a, sizeof(buffer_a), "%s[ERROR][%s:%d][%s] %s%s\n",
                    COLOR_RED, fi, li, fn, fmt, COLOR_END);

    LOG_PRINT
    LOG_UNLOCK
}
