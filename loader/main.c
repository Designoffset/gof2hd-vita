/*
 * main.c
 *
 * ARMv6 Shared Libraries loader. Galaxy on Fire 2 HD (2012 Android build) edition
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 *
 * The 2012 build is driven like Android's GLSurfaceView: the host creates the
 * GL context, calls ToJNI.initialize()/resize() once, then renderstep() every
 * frame, and feeds touches through ToJNI.handleTouchEvent(). This file plays
 * the part of GOF2HD2012 / GLViewRenderer / TouchHandler from classes.dex.
 */

#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/settings.h"
#include "utils/dialog.h"

#include <stdlib.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>

#include <AFakeNative/keycodes.h>
#include <FalsoJNI/FalsoJNI.h>
#include <so_util/so_util.h>

int _newlib_heap_size_user = 246 * 1024 * 1024;

so_module so_mod;

#define JNI_PREFIX "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_"
#define SCREEN_W 960
#define SCREEN_H 544

// Same pointer id offset GOF2's TouchHandler uses
#define POINTER_ID_ADD 722
#define STICK_POINTER_ID (POINTER_ID_ADD + 20)

enum { ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2 };

static void (*handleTouchEvent)(JNIEnv *, void *, int id, int action, int x, int y);

// From patch/controls_fix.c
void keyPressed(int keycode);
void keyReleased(int keycode);
void stickTouch(int pointerId, int action, float nx, float ny);
int controls_in_space();

static void setCountry();
static void handle_touch();
static void handle_buttons();
static void handle_stick();

static void *sym(const char *name) {
    void *s = (void *)so_symbol(&so_mod, name);
    if (!s) fatal_error("Error: symbol %s not found in the game library.", name);
    return s;
}

int main(int argc, char* argv[]) {
    soloader_init_all();

    int  (*JNI_OnLoad)(JavaVM *jvm) = sym("JNI_OnLoad");
    void (*setAPKPath)(JNIEnv *, void *, jstring) = sym(JNI_PREFIX "setAPKPath");
    void (*setZIPPath)(JNIEnv *, void *, jstring) = sym(JNI_PREFIX "setZIPPath");
    void (*SetDirectories)(JNIEnv *, void *, jstring, jstring) = sym(JNI_PREFIX "SetDirectories");
    void (*setEnvironmentVariables)(JNIEnv *, void *, jobject) = sym(JNI_PREFIX "setEnvironmentVariables");
    void (*initialize)(JNIEnv *, void *, int, int) = sym(JNI_PREFIX "initialize");
    void (*resize)(JNIEnv *, void *, int, int) = sym(JNI_PREFIX "resize");
    void (*renderstep)(JNIEnv *, void *, jlong) = sym(JNI_PREFIX "renderstep");
    int  (*getExitFlag)(JNIEnv *, void *) = sym(JNI_PREFIX "getExitFlag");
    handleTouchEvent = sym(JNI_PREFIX "handleTouchEvent");
    int  (*getLogoShown)(JNIEnv *, void *) = sym(JNI_PREFIX "getLogoShown");
    int  (*isInMainMenu)(JNIEnv *, void *) = sym(JNI_PREFIX "isInMainMenu");

    JNI_OnLoad(&jvm);
    log_info("JNI_OnLoad() passed");

    // The game hashes its save files with a 16-byte key that the Android side
    // normally provides through GOF2HD2012.SetOrigamiSuperClub(). Without it
    // the first save crashes in SHA256_Update. Any fixed value works as long
    // as it never changes (saves made with it verify against it).
    void (*SetOrigamiSuperClub)(JNIEnv *, void *, jstring) =
        sym("Java_net_fishlabs_gof2hdallandroid2012_GOF2HD2012_SetOrigamiSuperClub");
    SetOrigamiSuperClub(&jni, NULL, "GOF2HD-PSVITA-01");
    log_info("SetOrigamiSuperClub() passed");

    // GOF2HD2012.onCreate()
    setAPKPath(&jni, NULL, APK_PATH);
    setZIPPath(&jni, NULL, APK_PATH);
    SetDirectories(&jni, NULL, DATA_PATH, DATA_PATH);
    log_info("paths set");

    setCountry();
    log_info("setCountry() passed");

    // GLViewRenderer.onSurfaceCreated()
    gl_init();
    setEnvironmentVariables(&jni, NULL, (jobject)0x42424242);
    log_info("setEnvironmentVariables() passed");

    // GLViewRenderer.onSurfaceChanged()
    initialize(&jni, NULL, SCREEN_W, SCREEN_H);
    log_info("initialize() passed");
    resize(&jni, NULL, SCREEN_W, SCREEN_H);
    log_info("resize() passed");

    // GLViewRenderer.onDrawFrame() loop
    while (1) {
        handle_touch();
        if (setting_physicalControlsEnabled) {
            handle_buttons();
            handle_stick();
        }

        uint64_t t0 = sceKernelGetProcessTimeWide();
        renderstep(&jni, NULL, (jlong)(t0 / 1000));
        gl_swap();

        // Heartbeat for debugging: once a second for the first minute, then every 10s.
        static unsigned int frame = 0;
        frame++;
        if ((frame <= 3600 && frame % 60 == 0) || frame % 600 == 0 || frame <= 3) {
            logv_info("heartbeat frame=%u logoShown=%d mainMenu=%d inSpace=%d renderstep=%llums",
                      frame, getLogoShown(&jni, NULL), isInMainMenu(&jni, NULL), controls_in_space(),
                      (sceKernelGetProcessTimeWide() - t0) / 1000);
        }

        if (getExitFlag(&jni, NULL))
            sceKernelExitProcess(0);
    }
}

// ---------------------------------------------------------------- Touch input

static int touch_down[SCE_TOUCH_MAX_REPORT];
static int touch_x[SCE_TOUCH_MAX_REPORT], touch_y[SCE_TOUCH_MAX_REPORT];

static void handle_touch() {
    SceTouchData touch;
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

    int seen[SCE_TOUCH_MAX_REPORT] = {0};

    for (int i = 0; i < touch.reportNum; i++) {
        int id = touch.report[i].id % SCE_TOUCH_MAX_REPORT;
        // Front panel reports 1920x1088; the game surface is 960x544.
        int x = touch.report[i].x / 2;
        int y = touch.report[i].y / 2;
        seen[id] = 1;

        if (!touch_down[id]) {
            touch_down[id] = 1;
            handleTouchEvent(&jni, NULL, id + POINTER_ID_ADD, ACTION_DOWN, x, y);
        } else if (abs(x - touch_x[id]) > 5 || abs(y - touch_y[id]) > 5) {
            // TouchHandler only forwards moves bigger than its 5px neighborhood
            handleTouchEvent(&jni, NULL, id + POINTER_ID_ADD, ACTION_MOVE, x, y);
        } else {
            continue;
        }
        touch_x[id] = x;
        touch_y[id] = y;
    }

    for (int id = 0; id < SCE_TOUCH_MAX_REPORT; id++) {
        if (touch_down[id] && !seen[id]) {
            touch_down[id] = 0;
            handleTouchEvent(&jni, NULL, id + POINTER_ID_ADD, ACTION_UP, touch_x[id], touch_y[id]);
        }
    }
}

// --------------------------------------------------------- Physical controls

static const struct { unsigned int button; int keycode; } button_map[] = {
    { SCE_CTRL_CROSS,    AKEYCODE_DPAD_CENTER },  // primary fire
    { SCE_CTRL_RTRIGGER, AKEYCODE_DPAD_CENTER },  // primary fire
    { SCE_CTRL_SQUARE,   AKEYCODE_BUTTON_X },     // secondary fire
    { SCE_CTRL_CIRCLE,   AKEYCODE_BUTTON_Y },     // boost
    { SCE_CTRL_TRIANGLE, AKEYCODE_BACK },         // turret view
    { SCE_CTRL_LTRIGGER, AKEYCODE_BUTTON_L1 },    // autopilot
    { SCE_CTRL_UP,       AKEYCODE_BUTTON_R1 },    // fast forward / jump drive
    { SCE_CTRL_SELECT,   AKEYCODE_BUTTON_SELECT },// action menu
    { SCE_CTRL_START,    AKEYCODE_BUTTON_START }, // pause
};

static unsigned int prev_buttons = 0;

static void handle_buttons() {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);

    for (unsigned i = 0; i < sizeof(button_map) / sizeof(button_map[0]); i++) {
        unsigned int b = button_map[i].button;
        int now = (pad.buttons & b) != 0;
        int before = (prev_buttons & b) != 0;
        if (now && !before) keyPressed(button_map[i].keycode);
        if (!now && before) keyReleased(button_map[i].keycode);
    }
    prev_buttons = pad.buttons;
}

static int stick_active = 0;

static void handle_stick() {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(0, &pad, 1);

    float lx = ((float)pad.lx - 128.f) / 128.f;
    float ly = ((float)pad.ly - 128.f) / 128.f;
    float dz = setting_leftStickDeadZone;
    int outside = (lx * lx + ly * ly) > dz * dz;

    if (!controls_in_space()) {
        stick_active = 0;
        return;
    }

    float nx = (lx + 1.f) / 2.f;
    float ny = (ly + 1.f) / 2.f;

    if (outside && !stick_active) {
        stick_active = 1;
        stickTouch(STICK_POINTER_ID, ACTION_DOWN, nx, ny);
    } else if (outside) {
        stickTouch(STICK_POINTER_ID, ACTION_MOVE, nx, ny);
    } else if (stick_active) {
        stick_active = 0;
        stickTouch(STICK_POINTER_ID, ACTION_UP, 0.5f, 0.5f);
    }
}

// ------------------------------------------------------------------ Language

static void setCountry() {
    void (*setCountryCodeOfDevice)(JNIEnv *, void *, jint) = sym(JNI_PREFIX "setCountryCodeOfDevice");

    int lang = -1;
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang);
    int code;
    switch (lang) {
        case SCE_SYSTEM_PARAM_LANG_GERMAN:        code = 1; break;
        case SCE_SYSTEM_PARAM_LANG_FRENCH:        code = 2; break;
        case SCE_SYSTEM_PARAM_LANG_ITALIAN:       code = 3; break;
        case SCE_SYSTEM_PARAM_LANG_SPANISH:       code = 4; break;
        case SCE_SYSTEM_PARAM_LANG_RUSSIAN:       code = 5; break;
        case SCE_SYSTEM_PARAM_LANG_POLISH:        code = 6; break;
        case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT:
        case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR: code = 7; break;
        case SCE_SYSTEM_PARAM_LANG_KOREAN:        code = 14; break;
        case SCE_SYSTEM_PARAM_LANG_JAPANESE:      code = 15; break;
        default:                                  code = 0; break;
    }
    setCountryCodeOfDevice(&jni, NULL, code);
}
