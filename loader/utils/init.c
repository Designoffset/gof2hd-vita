#include <kubridge.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
/*
 * utils/init.c
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include "dynlib.h"
#include "patch.h"

#include <malloc.h>
#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>
#include <psp2/touch.h>

#include <FalsoJNI/FalsoJNI.h>
#include <so_util/so_util.h>
#include <unzip/unzip.h>
#include <sys/syslimits.h>

// Base address for the Android .so to be loaded at
#define LOAD_ADDRESS 0xA0000000

// Weak symbols from AFakeNative to be overriden with our settings
float L_INNER_DEADZONE = 0.11f;
float R_INNER_DEADZONE = 0.11f;
int AInput_enableLeftStick = 1;
int AInput_enableRightStick = 0;

extern so_module so_mod;
so_module so_fmodex, so_fmodevent;

#define FMODEX_LOAD_ADDRESS    0x98000000
#define FMODEVENT_LOAD_ADDRESS 0x98400000
#define FMODEX_PATH    "assets/lib/libfmodex.so"
#define FMODEVENT_PATH "assets/lib/libfmodevent.so"
// libgof2hdaa.so from the GOF2 HD 2012 Android release
#define GAME_SO_SHA1 "F1F8904BF1ED8911F6D279B6946E691EE25F4088"

// FMOD opens its sound banks (.fev/.fsb) with plain file I/O, so they can't
// stay inside the APK. Unpack assets/audio/ to DATA_PATH audio/ once; later
// launches only compare sizes.

// Diagnostic: FMOD returns ERR_INTERNAL (33) from many places. Give every one
// of them its own code (100 + index) so the log shows which check failed.
static void fmod_tag_internal_errors(void) {
    static const uint32_t sites[][2] = {
    {0x1c094, 0xe3a00021},
    {0x1c180, 0x03a04021},
    {0x258a0, 0x03a00021},
    {0x29164, 0xe3a06021},
    {0x2a3f8, 0xb3a04021},
    {0x2a844, 0xe3a00021},
    {0x2a9cc, 0xe3a00021},
    {0x2b23c, 0xe3a00021},
    {0x2e1dc, 0xe3a00021},
    {0x3a240, 0x13a06021},
    {0x3aeac, 0xd3a00021},
    {0x3bcac, 0x03a00021},
    {0x414b4, 0xe3a00021},
    {0x49e7c, 0xe3a00021},
    {0x4b538, 0xe3a03021},
    {0x4e460, 0x03a00021},
    {0x53c6c, 0x03a00021},
    {0x6d0fc, 0xe3a03021},
    {0x7ac84, 0xe3a00021},
    {0x7b738, 0x13a09021},
    {0x7dc38, 0xe3a00021},
    {0x7ea5c, 0xe3a04021},
    {0xa1600, 0xe3a00021},
    {0xa213c, 0x03a05021},
    {0xa222c, 0x03a05021},
    {0xb8424, 0xe3a00021},
    {0xb91cc, 0xe3a00021},
    {0xb9238, 0xe3a00021},
    {0xb93a0, 0xe3a00021},
    {0xb9988, 0xe3a00021},
    {0xb9c90, 0xe3a00021},
    {0xb9ccc, 0xe3a00021},
    {0xb9f3c, 0xe3a00021}
    };
    for (int i = 0; i < (int)(sizeof(sites) / sizeof(sites[0])); i++) {
        uint32_t *p = (uint32_t *)(so_fmodex.text_base + sites[i][0]);
        if (*p != sites[i][1]) { logv_info("fmod tag: site %d mismatch", i); continue; }
        uint32_t ins = (sites[i][1] & ~0xffu) | (100 + i);
        kuKernelCpuUnrestrictedMemcpy(p, &ins, 4);
    }
}

void extract_audio(void) {
    unzFile apk = unzOpen(APK_PATH);
    if (!apk) return;
    char name[256], dest[PATH_MAX];
    const char *prefix = "assets/audio/";
    size_t plen = strlen(prefix);
    int copied = 0, present = 0;
    static uint8_t buf[256 * 1024];
    for (int r = unzGoToFirstFile(apk); r == UNZ_OK; r = unzGoToNextFile(apk)) {
        unz_file_info info;
        if (unzGetCurrentFileInfo(apk, &info, name, sizeof(name), NULL, 0, NULL, 0) != UNZ_OK) continue;
        if (strncmp(name, prefix, plen) != 0 || name[strlen(name) - 1] == '/') continue;
        snprintf(dest, sizeof(dest), "%saudio/%s", DATA_PATH, name + plen);
        SceIoStat st;
        if (sceIoGetstat(dest, &st) >= 0 && st.st_size == (SceOff)info.uncompressed_size) { present++; continue; }
        mkpath(dest, 0777);
        if (unzOpenCurrentFile(apk) != UNZ_OK) continue;
        SceUID fd = sceIoOpen(dest, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (fd >= 0) {
            int n;
            while ((n = unzReadCurrentFile(apk, buf, sizeof(buf))) > 0) sceIoWrite(fd, buf, n);
            sceIoClose(fd);
            copied++;
            logv_info("audio: unpacked %s (%lu KB)", dest, (unsigned long)(info.uncompressed_size / 1024));
        }
        unzCloseCurrentFile(apk);
    }
    unzClose(apk);
    logv_info("audio: %d files unpacked, %d already present", copied, present);
}

static void load_module(so_module *mod, const char *path_in_apk, uintptr_t addr, const char *sha1);

void soloader_init_all() {
    // Check if we want to start the configurator app
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    sceAppUtilReceiveAppEvent(&eventParam);
    if (eventParam.type == 0x05) {
        char buffer[2048];
        sceAppUtilAppEventParseLiveArea(&eventParam, buffer);
        // (The Xperia port's configurator app is not part of this build.)
    }

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    // Enable analog stick and touchscreen
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, 1);

    if (!module_loaded("kubridge"))
        fatal_error("Error: kubridge.skprx is not installed.");
    log_info("kubridge check passed.");

    if (!file_exists(APK_PATH)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", APK_PATH);
    }

    // GOF2 HD 2012: FMOD ships as two separate libraries next to the game.
    load_module(&so_fmodex, FMODEX_PATH, FMODEX_LOAD_ADDRESS, NULL);
    load_module(&so_fmodevent, FMODEVENT_PATH, FMODEVENT_LOAD_ADDRESS, NULL);
    load_module(&so_mod, SO_PATH, LOAD_ADDRESS, GAME_SO_SHA1);

    settings_load();
    L_INNER_DEADZONE = setting_leftStickDeadZone;
    R_INNER_DEADZONE = setting_rightStickDeadZone;
    log_info("settings_load() passed.");

    so_relocate(&so_fmodex);
    so_relocate(&so_fmodevent);
    so_relocate(&so_mod);
    log_info("so_relocate() passed.");

    resolve_imports(&so_fmodex);
    resolve_imports(&so_fmodevent);
    resolve_imports(&so_mod);
    log_info("so_resolve() passed.");

    so_patch();
    fmod_tag_internal_errors();
    log_info("so_patch() passed.");

    so_flush_caches(&so_fmodex);
    so_flush_caches(&so_fmodevent);
    so_flush_caches(&so_mod);
    log_info("so_flush_caches() passed.");

    so_initialize(&so_fmodex);
    so_initialize(&so_fmodevent);
    so_initialize(&so_mod);
    log_info("so_initialize() passed.");

    gl_preload();
    log_info("gl_preload() passed.");

    jni_init();
    log_info("jni_init() passed.");
}

static void load_module(so_module *mod, const char *path_in_apk, uintptr_t addr, const char *expected_sha1) {
    // An unpacked copy is cached next to the APK to speed up later launches.
    char unpacked[PATH_MAX];
    snprintf(unpacked, sizeof(unpacked), "%s%s", DATA_PATH, path_in_apk);
    if (file_exists(unpacked)) {
        if (so_file_load(mod, unpacked, addr) < 0)
            fatal_error("Error: could not load %s.", unpacked);
        logv_info("so_file_load(%s) passed.", unpacked);
        return;
    }

    unzFile apk_file = unzOpen(APK_PATH);
    if (!apk_file)
        fatal_error("Error: could not load %s as an archive. Corrupted file?", APK_PATH);

    unz_file_info file_info;
    if (unzLocateFile(apk_file, path_in_apk, NULL) != UNZ_OK)
        fatal_error("Error: could not locate %s inside the APK. "
                    "You may be using a wrong game version or corrupted APK.", path_in_apk);
    unzGetCurrentFileInfo(apk_file, &file_info, NULL, 0, NULL, 0, NULL, 0);
    unzOpenCurrentFile(apk_file);
    uint64_t so_size = file_info.uncompressed_size;
    uint8_t *so_buffer = (uint8_t *)malloc(so_size);
    unzReadCurrentFile(apk_file, so_buffer, so_size);
    unzCloseCurrentFile(apk_file);
    unzClose(apk_file);

    if (expected_sha1) {
        char *so_hash = get_string_sha1(so_buffer, so_size);
        if (strcmp(so_hash, expected_sha1) != 0)
            fatal_error("This build of the port is for the Galaxy on Fire 2 HD "
                        "2012 Android release. Expected SHA1 %s for %s, got %s.",
                        expected_sha1, path_in_apk, so_hash);
        free(so_hash);
    }

    if (so_mem_load(mod, so_buffer, so_size, addr) < 0)
        fatal_error("Error: could not load %s.", path_in_apk);

    mkpath(unpacked, 0777);
    file_save(unpacked, so_buffer, so_size);
    free(so_buffer);
    logv_info("so_mem_load(%s) passed.", path_in_apk);
}
