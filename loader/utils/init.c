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
