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
#include <vitaGL.h>
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
int controls_in_space();
void money_tick(void);

static void setCountry();
static int handle_touch();
static void handle_controls(int screen_touched);
static void draw_controls_overlay(void);
static void controls_init(void);

// Watchdog: logs which startup stage the main thread is in, and loading
// progress, every 5 seconds for the first two minutes.
static volatile const char *wd_stage = "start";
static volatile uint64_t wd_since = 0;
static volatile unsigned int wd_frames = 0;
extern volatile unsigned long long io_bytes_read;
extern volatile unsigned int io_reads, io_seeks;

static void wd_set(const char *stage) {
    wd_since = sceKernelGetProcessTimeWide();
    wd_stage = stage;
}

static int watchdog_thread(SceSize args, void *argp) {
    for (int n = 0; n < 24; n++) {
        sceKernelDelayThread(5 * 1000 * 1000);
        uint64_t now = sceKernelGetProcessTimeWide();
        unsigned int f = wd_frames;
        logv_info("watchdog: t=%llus stage=%s for %llus frames=%u read=%lluKB in %u reads, %u seeks",
                  now / 1000000, (const char *)wd_stage, (now - wd_since) / 1000000, f,
                  io_bytes_read / 1024, io_reads, io_seeks);
        // Startup normally takes ~25s; if it hangs, crash so the Vita writes a
        // core dump with every thread's position.
        if (f == 0 && now - wd_since > 50ull * 1000000) {
            log_info("watchdog: startup hung, crashing for a core dump");
            *(volatile int *)0 = 0xDEAD;
        }
    }
    return 0;
}

static void *sym(const char *name) {
    void *s = (void *)so_symbol(&so_mod, name);
    if (!s) fatal_error("Error: symbol %s not found in the game library.", name);
    return s;
}

int main(int argc, char* argv[]) {
    soloader_init_all();

    extern void extract_audio(void);
    extract_audio();

    wd_set("init");
    SceUID wd = sceKernelCreateThread("watchdog", watchdog_thread, 0x10000100, 0x4000, 0, 0, NULL);
    if (wd >= 0) sceKernelStartThread(wd, 0, NULL);

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
    controls_init();
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

    // GooglePlayIAP constructor in the repack: Valkyrie, Kaamo Club, Supernova
    // and the full package are owned from the start (VIP is item 3).
    extern void (*gof2_iapBoughtPremium)(JNIEnv *, void *, int, int);
    gof2_iapBoughtPremium = sym("Java_net_fishlabs_googleplay_ToJNI_iapBoughtPremium");
    for (int i = 0; i < 5; i++)
        gof2_iapBoughtPremium(&jni, NULL, i, 1);
    log_info("add-ons unlocked");

    // GLViewRenderer.onSurfaceCreated()
    gl_init();

    wd_set("setEnvironmentVariables");
    setEnvironmentVariables(&jni, NULL, (jobject)0x42424242);
    log_info("setEnvironmentVariables() passed");

    // GLViewRenderer.onSurfaceChanged()
    wd_set("initialize");
    initialize(&jni, NULL, SCREEN_W, SCREEN_H);
    log_info("initialize() passed");
    wd_set("resize");
    resize(&jni, NULL, SCREEN_W, SCREEN_H);
    log_info("resize() passed");

    // GLViewRenderer.onDrawFrame() loop
    wd_set("render loop");
    while (1) {
        wd_frames++;
        int touched = handle_touch();
        if (setting_physicalControlsEnabled)
            handle_controls(touched);

        uint64_t t0 = sceKernelGetProcessTimeWide();
        money_tick();
        renderstep(&jni, NULL, (jlong)(t0 / 1000));
        draw_controls_overlay();
        gl_swap();

        // Heartbeat for debugging: once a second for the first minute, then every 10s.
        static unsigned int frame = 0;
        frame++;
        if ((frame <= 3600 && frame % 60 == 0) || frame % 600 == 0 || frame <= 3) {
            logv_info("heartbeat frame=%u logoShown=%d mainMenu=%d inSpace=%d renderstep=%llums",
                      frame, getLogoShown(&jni, NULL), isInMainMenu(&jni, NULL), controls_in_space(),
                      (sceKernelGetProcessTimeWide() - t0) / 1000);
        }
        // GL activity for exactly one frame right after each heartbeat
        if ((frame <= 3600 && frame % 60 == 0) || frame % 600 == 0)
            gl_stats_reset();
        if ((frame <= 3600 && frame % 60 == 1) || frame % 600 == 1)
            gl_stats_report();

        if (getExitFlag(&jni, NULL))
            sceKernelExitProcess(0);
    }
}

// ---------------------------------------------------------------- Touch input

static int touch_down[SCE_TOUCH_MAX_REPORT];
static int touch_x[SCE_TOUCH_MAX_REPORT], touch_y[SCE_TOUCH_MAX_REPORT];

static int handle_touch() {
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
    return touch.reportNum > 0;
}

// --------------------------------------------------------- Physical controls
//
// Everything goes through handleTouchEvent, the same path a finger on the
// screen takes, so the game sees ordinary touches.
//
// In flight: left stick steers (drags the on-screen stick), buttons press the
// HUD buttons.
// In menus, and in flight while a menu/map/dialogue is open: a cursor.
//   left stick / D-pad move it, Cross (or R) taps / drags, Circle = back.

#include <psp2/kernel/clib.h>

extern int *Globals__turret_view_x, *Globals__turret_view_y;
extern int *Globals__fire_x, *Globals__fire_y;
extern int *Globals__sec_fire_x, *Globals__sec_fire_y;
extern int *Globals__boost_x, *Globals__boost_y;
extern int *Globals__autopilot_x, *Globals__autopilot_y;
extern int *Globals__fast_forward_x, *Globals__fast_forward_y;
extern int *Globals__pause_x, *Globals__pause_y;
extern int *Globals__action_menu_x, *Globals__action_menu_y;
extern int *Globals__smallButton_dim;
extern int *Globals__touch_stick_x, *Globals__touch_stick_y;
int controls_overlay_open(void);
int controls_module(void);

static void (*BackButtonPressed)(JNIEnv *, void *) = NULL;

static void controls_init(void) {
    BackButtonPressed = sym(JNI_PREFIX "BackButtonPressed");
}

enum { HUD_FIRE, HUD_SEC_FIRE, HUD_BOOST, HUD_TURRET, HUD_AUTOPILOT, HUD_JUMP, HUD_ACTION, HUD_PAUSE, HUD_COUNT };

// Same layout as the original gof2-vita port (Xperia Play keys on the Vita):
static const struct { unsigned int button; int hud; } flight_map[] = {
    { SCE_CTRL_CROSS,    HUD_FIRE },       // Xperia "X"        -> fire
    { SCE_CTRL_CIRCLE,   HUD_TURRET },     // Xperia "O"/back   -> turret view
    { SCE_CTRL_SQUARE,   HUD_SEC_FIRE },   // Xperia "[]"       -> secondary weapon
    { SCE_CTRL_TRIANGLE, HUD_BOOST },      // Xperia "/\"       -> boost
    { SCE_CTRL_LTRIGGER, HUD_AUTOPILOT },  // L1                -> autopilot
    { SCE_CTRL_RTRIGGER, HUD_JUMP },       // R1                -> fast forward / jump drive
    { SCE_CTRL_SELECT,   HUD_ACTION },
};

typedef struct { unsigned int id; int x0, y0, x1, y1, n; } hud_box;
extern hud_box hud_boxes[32];
extern int hud_box_count;
int controls_probe_hud(void);

// Which probed element plays which role, picked by where it sits on screen
// (see the HUD layout: stick + boost bottom-left, fire cluster bottom-right,
// pause top-right).
static int role_box[HUD_COUNT + 1];
#define ROLE_STICK HUD_COUNT

static void assign_roles(void) {
    for (int r = 0; r <= HUD_COUNT; r++) role_box[r] = -1;
    int best_area = 0;
    // stick: biggest element in the left half
    for (int i = 0; i < hud_box_count; i++) {
        hud_box *b = &hud_boxes[i];
        int cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
        if (cx < 480 && cy > 272 && b->n > best_area) { best_area = b->n; role_box[ROLE_STICK] = i; }
    }
    // fire: biggest element in the right half, lower part
    best_area = 0;
    for (int i = 0; i < hud_box_count; i++) {
        hud_box *b = &hud_boxes[i];
        int cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
        if (cx > 480 && cy > 272 && b->n > best_area) { best_area = b->n; role_box[HUD_FIRE] = i; }
    }
    // boost: the other lower-left element
    for (int i = 0; i < hud_box_count; i++) {
        hud_box *b = &hud_boxes[i];
        int cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
        if (i != role_box[ROLE_STICK] && cx < 480 && cy > 272) role_box[HUD_BOOST] = i;
    }
    // pause: top-right corner
    for (int i = 0; i < hud_box_count; i++) {
        hud_box *b = &hud_boxes[i];
        int cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
        if (cx > 800 && cy < 100) role_box[HUD_PAUSE] = i;
    }
    // the remaining lower-right elements around the fire button, by angle:
    // nearest-left -> secondary, above -> action menu, upper-left -> turret,
    // anything else -> autopilot / jump in order found
    int fire = role_box[HUD_FIRE];
    if (fire >= 0) {
        int fx = (hud_boxes[fire].x0 + hud_boxes[fire].x1) / 2, fy = (hud_boxes[fire].y0 + hud_boxes[fire].y1) / 2;
        for (int i = 0; i < hud_box_count; i++) {
            if (i == fire || i == role_box[HUD_PAUSE]) continue;
            hud_box *b = &hud_boxes[i];
            int cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
            if (cx < 480) continue;
            int dx = cx - fx, dy = cy - fy;
            int role;
            if (dy > -30 && dx < 0)              role = HUD_SEC_FIRE;
            else if (dx > -40 && dx < 60 && dy < 0) role = HUD_ACTION;
            else if (dx < 0 && dy < 0)           role = HUD_TURRET;
            else                                 role = role_box[HUD_AUTOPILOT] < 0 ? HUD_AUTOPILOT : HUD_JUMP;
            if (role_box[role] < 0) role_box[role] = i;
        }
    }
    static const char *names[] = { "fire", "secondary", "boost", "turret", "autopilot", "jump", "action", "pause", "stick" };
    for (int r = 0; r <= HUD_COUNT; r++)
        if (role_box[r] >= 0) logv_info("hud: %s -> element 0x%x", names[r], hud_boxes[role_box[r]].id);
}

static int hud_pos(int hud, int *x, int *y) {
    if (hud_box_count && role_box[hud] >= 0) {
        hud_box *b = &hud_boxes[role_box[hud]];
        *x = (b->x0 + b->x1) / 2;
        *y = (b->y0 + b->y1) / 2;
        return 1;
    }
    int *px = NULL, *py = NULL;
    switch (hud) {
        case HUD_FIRE:      px = Globals__fire_x;         py = Globals__fire_y; break;
        case HUD_SEC_FIRE:  px = Globals__sec_fire_x;     py = Globals__sec_fire_y; break;
        case HUD_BOOST:     px = Globals__boost_x;        py = Globals__boost_y; break;
        case HUD_TURRET:    px = Globals__turret_view_x;  py = Globals__turret_view_y; break;
        case HUD_AUTOPILOT: px = Globals__autopilot_x;    py = Globals__autopilot_y; break;
        case HUD_JUMP:      px = Globals__fast_forward_x; py = Globals__fast_forward_y; break;
        case HUD_ACTION:    px = Globals__action_menu_x;  py = Globals__action_menu_y; break;
        case HUD_PAUSE:     px = Globals__pause_x;        py = Globals__pause_y; break;
    }
    if (!px || !py) return 0;
    *x = *px + 4;
    *y = *py + 4;
    return 1;
}

#define HUD_POINTER(i)  (POINTER_ID_ADD + 30 + (i))
#define CURSOR_POINTER  (POINTER_ID_ADD + 50)

static int hud_down[HUD_COUNT], hud_x[HUD_COUNT], hud_y[HUD_COUNT];
static int stick_down = 0;
static float cursor_x = 480.f, cursor_y = 272.f;
static int cursor_down = 0, cursor_shown = 0;
static unsigned int prev_buttons = 0;
static int prev_mode = -1;

static void touch(int id, int action, int x, int y) {
    handleTouchEvent(&jni, NULL, id, action, x, y);
}

static void release_all(void) {
    for (int i = 0; i < HUD_COUNT; i++)
        if (hud_down[i]) { touch(HUD_POINTER(i), ACTION_UP, hud_x[i], hud_y[i]); hud_down[i] = 0; }
    if (stick_down) {
        touch(STICK_POINTER_ID, ACTION_UP, *Globals__touch_stick_x, *Globals__touch_stick_y);
        stick_down = 0;
    }
    if (cursor_down) { touch(CURSOR_POINTER, ACTION_UP, (int)cursor_x, (int)cursor_y); cursor_down = 0; }
}

static float axis(unsigned char v) {
    float a = ((float)v - 128.f) / 127.f;
    float dz = setting_leftStickDeadZone;
    if (a > -dz && a < dz) return 0.f;
    return a;
}

static void flight_controls(SceCtrlData *pad, unsigned int pressed, unsigned int released) {
    // Buttons -> HUD buttons
    int want[HUD_COUNT] = {0};
    for (unsigned i = 0; i < sizeof(flight_map) / sizeof(flight_map[0]); i++)
        if (pad->buttons & flight_map[i].button) want[flight_map[i].hud] = 1;
    for (int h = 0; h < HUD_COUNT; h++) {
        if (want[h] && !hud_down[h]) {
            if (hud_pos(h, &hud_x[h], &hud_y[h])) {
                touch(HUD_POINTER(h), ACTION_DOWN, hud_x[h], hud_y[h]);
                hud_down[h] = 1;
            }
        } else if (!want[h] && hud_down[h]) {
            touch(HUD_POINTER(h), ACTION_UP, hud_x[h], hud_y[h]);
            hud_down[h] = 0;
        }
    }

    // Left stick (or D-pad, like the original port) -> drag on the on-screen steering stick
    float lx = axis(pad->lx), ly = axis(pad->ly);
    if (pad->buttons & SCE_CTRL_LEFT)  lx = -1.f;
    if (pad->buttons & SCE_CTRL_RIGHT) lx =  1.f;
    if (pad->buttons & SCE_CTRL_UP)    ly = -1.f;
    if (pad->buttons & SCE_CTRL_DOWN)  ly =  1.f;
    int cx = *Globals__touch_stick_x, cy = *Globals__touch_stick_y;
    float r = (float)*Globals__smallButton_dim;
    if (hud_box_count && role_box[ROLE_STICK] >= 0) {
        hud_box *b = &hud_boxes[role_box[ROLE_STICK]];
        cx = (b->x0 + b->x1) / 2;
        cy = (b->y0 + b->y1) / 2;
        r = (float)(b->x1 - b->x0) / 2.f;
    }
    if (cx == 0 && cy == 0) { cx = 110; cy = 420; }  // stick spot on the default HUD
    if (r < 20.f) r = 60.f;
    int x = cx + (int)(lx * r), y = cy + (int)(ly * r);
    if (lx != 0.f || ly != 0.f) {
        if (!stick_down) { touch(STICK_POINTER_ID, ACTION_DOWN, cx, cy); stick_down = 1; }
        touch(STICK_POINTER_ID, ACTION_MOVE, x, y);
    } else if (stick_down) {
        touch(STICK_POINTER_ID, ACTION_UP, cx, cy);
        stick_down = 0;
    }
}

static void cursor_controls(SceCtrlData *pad, unsigned int pressed, unsigned int released) {
    float lx = axis(pad->lx), ly = axis(pad->ly);
    float dx = lx * 9.f, dy = ly * 9.f;
    if (pad->buttons & SCE_CTRL_LEFT)  dx -= 5.f;
    if (pad->buttons & SCE_CTRL_RIGHT) dx += 5.f;
    if (pad->buttons & SCE_CTRL_UP)    dy -= 5.f;
    if (pad->buttons & SCE_CTRL_DOWN)  dy += 5.f;
    if (dx != 0.f || dy != 0.f) {
        cursor_shown = 1;
        cursor_x += dx; cursor_y += dy;
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_x > SCREEN_W - 1) cursor_x = SCREEN_W - 1;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_y > SCREEN_H - 1) cursor_y = SCREEN_H - 1;
        if (cursor_down) touch(CURSOR_POINTER, ACTION_MOVE, (int)cursor_x, (int)cursor_y);
    }

    unsigned int tap = SCE_CTRL_CROSS | SCE_CTRL_RTRIGGER;
    if ((pressed & tap) && !cursor_down) {
        cursor_shown = 1;
        cursor_down = 1;
        touch(CURSOR_POINTER, ACTION_DOWN, (int)cursor_x, (int)cursor_y);
    } else if (cursor_down && !(pad->buttons & tap)) {
        cursor_down = 0;
        touch(CURSOR_POINTER, ACTION_UP, (int)cursor_x, (int)cursor_y);
    }

    if ((pressed & SCE_CTRL_CIRCLE) && BackButtonPressed)
        BackButtonPressed(&jni, NULL);

    // In flight with a menu open, Start/Select still close it via their HUD buttons.
    if (controls_in_space()) {
        int x, y;
        if ((pressed & SCE_CTRL_SELECT) && hud_pos(HUD_ACTION, &x, &y)) {
            touch(HUD_POINTER(HUD_ACTION), ACTION_DOWN, x, y);
            touch(HUD_POINTER(HUD_ACTION), ACTION_UP, x, y);
        }
    }
}

static void handle_controls(int screen_touched) {
    SceCtrlData pad;
    sceClibMemset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);
    unsigned int pressed = pad.buttons & ~prev_buttons;
    unsigned int released = ~pad.buttons & prev_buttons;
    prev_buttons = pad.buttons;

    if (screen_touched) cursor_shown = 0;

    static int last_module = -2;
    int module = controls_module();
    if (module != last_module) {
        logv_info("controls: game module %d -> %d", last_module, module);
        last_module = module;
    }

    // Start in flight pauses and brings up the cursor for the pause menu;
    // Start again (or leaving flight) goes back to flight controls.
    static int paused = 0;
    if (!controls_in_space()) paused = 0;
    else if (pressed & SCE_CTRL_START) {
        if (!paused) {
            int x, y;
            if (hud_pos(HUD_PAUSE, &x, &y)) {
                touch(HUD_POINTER(HUD_PAUSE), ACTION_DOWN, x, y);
                touch(HUD_POINTER(HUD_PAUSE), ACTION_UP, x, y);
            }
        }
        paused = !paused;
        pressed &= ~SCE_CTRL_START;
    }
    int mode = (controls_in_space() && !paused && !controls_overlay_open()) ? 1 : 0;
    // The HUD is laid out a little after flight starts: scan once a second
    // (6 times a second) until its buttons are found.
    static int probe_timer = 0;
    if (mode != 1) probe_timer = 0;
    else if (hud_box_count == 0 && probe_timer-- <= 0) {
        probe_timer = 10;
        if (controls_probe_hud()) assign_roles();
    }
    if (mode != prev_mode) {
        logv_info("controls: %s | fire %d,%d sec %d,%d boost %d,%d turret %d,%d auto %d,%d jump %d,%d action %d,%d pause %d,%d stick %d,%d r=%d",
                  mode ? "flight" : "cursor",
                  *Globals__fire_x, *Globals__fire_y, *Globals__sec_fire_x, *Globals__sec_fire_y,
                  *Globals__boost_x, *Globals__boost_y, *Globals__turret_view_x, *Globals__turret_view_y,
                  *Globals__autopilot_x, *Globals__autopilot_y, *Globals__fast_forward_x, *Globals__fast_forward_y,
                  *Globals__action_menu_x, *Globals__action_menu_y, *Globals__pause_x, *Globals__pause_y,
                  *Globals__touch_stick_x, *Globals__touch_stick_y, *Globals__smallButton_dim);
        release_all();
        prev_mode = mode;
        if (!mode) cursor_shown = 1;
    }
    if (mode) flight_controls(&pad, pressed, released);
    else cursor_controls(&pad, pressed, released);
}

void gl_draw_cursor(int x, int y, int pressed);

static void draw_controls_overlay(void) {
    if (prev_mode == 0 && cursor_shown)
        gl_draw_cursor((int)cursor_x, (int)cursor_y, cursor_down);
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
