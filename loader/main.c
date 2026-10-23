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
#include "utils/screen.h"
#include <vitaGL.h>
#include "utils/logger.h"
#include "utils/settings.h"
#include "utils/dialog.h"

#include <stdlib.h>
#include <stdio.h>
#include <psp2/io/fcntl.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>

#include <AFakeNative/keycodes.h>
#include <FalsoJNI/FalsoJNI.h>
#include <so_util/so_util.h>

// The game's own heap. It peaks around 85 MB in play; everything left over
// goes to vitaGL's texture pool (VGL_MEM_RAM). With the old 246 MB heap
// vitaGL got only ~56 MB, ran out on long sessions and spilled textures
// into this heap (heap corruption / std::bad_alloc, missing text).
int _newlib_heap_size_user = 170 * 1024 * 1024;

so_module so_mod;

#define JNI_PREFIX "Java_net_fishlabs_gof2hdallandroid2012_ToJNI_"
#define SCREEN_W GAME_W
#define SCREEN_H GAME_H
#define SC(v) ((v) * GAME_W / 960)   // scale a 960-wide layout value

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

/*
 * Random LiveArea background. The LiveArea shows a copy of bg0.png that the
 * system keeps in ur0:appmeta/<title>/livearea/contents/. Each launch copies
 * one of the backgrounds shipped in app0:livearea/bg/ over it (never the same
 * one twice in a row), so the next visit to the LiveArea shows another scene.
 */
#define LA_COUNT 11
static void livearea_rotate(void) {
    int last = -1;
    SceUID fd = sceIoOpen(DATA_PATH "livearea_last", SCE_O_RDONLY, 0);
    if (fd >= 0) { char c[4] = {0}; sceIoRead(fd, c, 3); last = atoi(c); sceIoClose(fd); }
    int pick = (int)((sceKernelGetProcessTimeWide() ^ (sceKernelGetSystemTimeWide() >> 3)) % LA_COUNT) + 1;
    if (pick == last) pick = pick % LA_COUNT + 1;
    char src[64];
    snprintf(src, sizeof(src), "app0:livearea/bg/bg_%02d.png", pick);
    SceUID in = sceIoOpen(src, SCE_O_RDONLY, 0);
    if (in < 0) { logv_info("livearea: %s missing (0x%x)", src, in); return; }
    const char *dst = "ur0:appmeta/GOF2HD012/livearea/contents/bg0.png";
    SceUID out = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (out < 0) { logv_info("livearea: can't write %s (0x%x)", dst, out); sceIoClose(in); return; }
    static char buf[64 * 1024];
    int n, total = 0;
    while ((n = sceIoRead(in, buf, sizeof(buf))) > 0) { sceIoWrite(out, buf, n); total += n; }
    sceIoClose(in); sceIoClose(out);
    fd = sceIoOpen(DATA_PATH "livearea_last", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd >= 0) { char c[8]; int l = snprintf(c, sizeof(c), "%d", pick); sceIoWrite(fd, c, l); sceIoClose(fd); }
    logv_info("livearea: background %d (%d bytes) installed for next time", pick, total);
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
    livearea_rotate();

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
        { extern void station_tick(void); station_tick(); }
        { extern void controls_music_tick(void); controls_music_tick(); }
        renderstep(&jni, NULL, (jlong)(t0 / 1000));
        {
            // Slow-frame report: what loaded during a frame that missed 60 fps
            // by a lot (shader links, sound loads, file opens).
            extern int stat_links, stat_sounds, stat_fopens;
            unsigned long long dt = (sceKernelGetProcessTimeWide() - t0) / 1000;
            static int reported = 0;
            if (dt > 50 && controls_in_space() && reported < 60) {
                reported++;
                logv_info("slow frame: %llums, shader links %d, sound loads %d, files opened %d",
                          dt, stat_links, stat_sounds, stat_fopens);
            }
            stat_links = stat_sounds = stat_fopens = 0;
        }
        draw_controls_overlay();
        gl_swap();

        // Heartbeat for debugging: once a second for the first minute, then every 10s.
        static unsigned int frame = 0;
        frame++;
        if ((frame <= 3600 && frame % 60 == 0) || frame % 600 == 0 || frame <= 3) {
            extern size_t memwatch_used(void), memwatch_peak(void), memwatch_arena(void);
            size_t used = memwatch_used();
            logv_info("heartbeat frame=%u logoShown=%d mainMenu=%d inSpace=%d renderstep=%llums heap=%uKB peak=%uKB arena=%uKB vram=%uKB glram=%uKB phycont=%uKB",
                      frame, getLogoShown(&jni, NULL), isInMainMenu(&jni, NULL), controls_in_space(),
                      (sceKernelGetProcessTimeWide() - t0) / 1000,
                      (unsigned)(used / 1024), (unsigned)(memwatch_peak() / 1024), (unsigned)(memwatch_arena() / 1024),
                      (unsigned)(vglMemFree(VGL_MEM_VRAM) / 1024), (unsigned)(vglMemFree(VGL_MEM_RAM) / 1024),
                      (unsigned)(vglMemFree(VGL_MEM_PHYCONT) / 1024));
        }
        // GL activity for exactly one frame right after each heartbeat
        if ((frame <= 3600 && frame % 60 == 0) || frame % 600 == 0)
            gl_stats_reset();
        if ((frame <= 3600 && frame % 60 == 1) || frame % 600 == 1)
            gl_stats_report();
        if (frame % 3600 == 2) { extern void memwatch_report(void); memwatch_report(); }

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
        // Front panel reports 1920x1088; the game renders at GAME_W x GAME_H.
        int x = touch.report[i].x * GAME_W / 1920;
        int y = touch.report[i].y * GAME_H / 1088;
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

static void assign_roles(void);
static int hud_cache_loaded = 0;

enum { HUD_FIRE, HUD_SEC_FIRE, HUD_BOOST, HUD_TURRET, HUD_AUTOPILOT, HUD_JUMP, HUD_ACTION, HUD_PAUSE, HUD_COUNT };

// Same layout as the original gof2-vita port (Xperia Play keys on the Vita):
static const struct { unsigned int button; int hud; } flight_map[] = {
    { SCE_CTRL_CROSS,    HUD_FIRE },       // Xperia "X"        -> fire
    { SCE_CTRL_CIRCLE,   HUD_TURRET },     // Xperia "O"/back   -> turret view
    { SCE_CTRL_SQUARE,   HUD_SEC_FIRE },   // Xperia "[]"       -> secondary weapon
    { SCE_CTRL_TRIANGLE, HUD_BOOST },      // Xperia "/\"       -> boost
    { SCE_CTRL_UP,       HUD_AUTOPILOT },  // D-pad up          -> autopilot
    { SCE_CTRL_DOWN,     HUD_JUMP },       // D-pad down        -> fast forward / jump drive
    { SCE_CTRL_RIGHT,    HUD_ACTION },     // D-pad right       -> action menu
    // L / R and the right stick: barrel rolls and throttle (controls_flight_tick).
};

typedef struct { unsigned int id; int x0, y0, x1, y1, n; } hud_box;
extern hud_box hud_boxes[32];
extern int hud_box_count;
int controls_probe_hud(void);
int controls_load_hud_cache(void);

// Which probed element plays which role, picked by where it sits on screen
// (see the HUD layout: stick + boost bottom-left, fire cluster bottom-right,
// pause top-right).
static int role_box[HUD_COUNT + 1];
#define ROLE_STICK HUD_COUNT

static void assign_roles(void) {
    for (int r = 0; r <= HUD_COUNT; r++) role_box[r] = -1;
    // The boost button (0x2) is only drawn while boost is ready, so a scan
    // can miss it. Keep its spot from a full scan so Triangle never lands
    // on a neighbouring button (autopilot) instead.
    {
        int have = 0;
        for (int i = 0; i < hud_box_count; i++) if (hud_boxes[i].id == 0x2) have = 1;
        if (!have && hud_box_count < 32)
            hud_boxes[hud_box_count++] = (hud_box){ 0x2, 156, 628, 308, 700, 1 };
    }
    int best_area = 0;
    // stick: biggest element in the left half
    for (int i = 0; i < hud_box_count; i++) {
        hud_box *b = &hud_boxes[i];
        int cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
        if (cx < GAME_W / 2 && cy > GAME_H / 2 && b->n > best_area) { best_area = b->n; role_box[ROLE_STICK] = i; }
    }
    // fire: biggest element in the right half, lower part
    best_area = 0;
    for (int i = 0; i < hud_box_count; i++) {
        hud_box *b = &hud_boxes[i];
        int cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
        if (cx > GAME_W / 2 && cy > GAME_H / 2 && b->n > best_area) { best_area = b->n; role_box[HUD_FIRE] = i; }
    }
    // boost: the other lower-left element
    for (int i = 0; i < hud_box_count; i++) {
        hud_box *b = &hud_boxes[i];
        int cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
        if (i != role_box[ROLE_STICK] && cx < GAME_W / 2 && cy > GAME_H / 2) role_box[HUD_BOOST] = i;
    }
    // pause: top-right corner
    for (int i = 0; i < hud_box_count; i++) {
        hud_box *b = &hud_boxes[i];
        int cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2;
        if (cx > SC(800) && cy < SC(100)) role_box[HUD_PAUSE] = i;
    }
    // Elements whose meaning is known from MGame::OnTouchBegin/OnTouchEnd
    // take their role by id: 0x2 boost, 0x4 action menu, 0x8 secondary,
    // 0x10 fire, 0x40 autopilot, 0x80 camera, 0x100 fast forward, 0x1 pause.
    // (0x4000000.. are docking-target buttons, not the action menu.)
    static const struct { unsigned id; int role; } by_id[] = {
        { 0x10, HUD_FIRE }, { 0x8, HUD_SEC_FIRE }, { 0x2, HUD_BOOST }, { 0x80, HUD_TURRET },
        { 0x40, HUD_AUTOPILOT }, { 0x100, HUD_JUMP }, { 0x4, HUD_ACTION }, { 0x1, HUD_PAUSE },
        { 0x20, ROLE_STICK },
    };
    for (unsigned k = 0; k < sizeof(by_id) / sizeof(by_id[0]); k++)
        for (int i = 0; i < hud_box_count; i++)
            if (hud_boxes[i].id == by_id[k].id) role_box[by_id[k].role] = i;
    // A role guessed by position must not sit on a button that belongs to
    // another role.
    for (int r = 0; r <= HUD_COUNT; r++) {
        if (role_box[r] < 0) continue;
        for (unsigned k = 0; k < sizeof(by_id) / sizeof(by_id[0]); k++)
            if (hud_boxes[role_box[r]].id == by_id[k].id && by_id[k].role != r) { role_box[r] = -1; break; }
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
            if (cx < GAME_W / 2) continue;
            if (hud_boxes[i].id >= 0x4000000) continue;   // docking targets
            int known = 0;
            for (int r = 0; r <= HUD_COUNT; r++) if (role_box[r] == i) known = 1;
            if (known) continue;
            int dx = cx - fx, dy = cy - fy;
            int role;
            if (dy > -SC(30) && dx < 0)              role = HUD_SEC_FIRE;
            else if (dx > -SC(40) && dx < SC(60) && dy < 0) role = HUD_ACTION;
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
static float cursor_x = GAME_W / 2, cursor_y = GAME_H / 2;
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

// Small dead zone, then rescaled so the stick still reaches full deflection
// at the edge (no jump from 0 to the dead-zone value).
static float axis(unsigned char v) {
    float a = ((float)v - 128.f) / 127.f;
    if (a > 1.f) a = 1.f;
    if (a < -1.f) a = -1.f;
    float dz = setting_leftStickDeadZone;
    if (dz > 0.15f) dz = 0.15f;
    if (a > -dz && a < dz) return 0.f;
    return a > 0 ? (a - dz) / (1.f - dz) : (a + dz) / (1.f - dz);
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
    int cx = *Globals__touch_stick_x, cy = *Globals__touch_stick_y;
    float r = (float)*Globals__smallButton_dim;
    if (hud_box_count && role_box[ROLE_STICK] >= 0) {
        hud_box *b = &hud_boxes[role_box[ROLE_STICK]];
        cx = (b->x0 + b->x1) / 2;
        cy = (b->y0 + b->y1) / 2;
        r = (float)(b->x1 - b->x0) / 2.f;
    }
    if (cx == 0 && cy == 0) { cx = SC(110); cy = SC(420); }  // stick spot on the default HUD
    if (r < SC(20)) r = SC(60);
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
    float dx = lx * SC(9.f), dy = ly * SC(9.f);
    if (pad->buttons & SCE_CTRL_LEFT)  dx -= SC(5.f);
    if (pad->buttons & SCE_CTRL_RIGHT) dx += SC(5.f);
    if (pad->buttons & SCE_CTRL_UP)    dy -= SC(5.f);
    if (pad->buttons & SCE_CTRL_DOWN)  dy += SC(5.f);
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

    // Hide the cursor again after 3 seconds without using it.
    static int idle = 0;
    if (dx != 0.f || dy != 0.f || cursor_down) idle = 0;
    else if (++idle > 180) cursor_shown = 0;

    if ((pressed & SCE_CTRL_CIRCLE) && BackButtonPressed)
        BackButtonPressed(&jni, NULL);

}

static void handle_controls(int screen_touched) {
    SceCtrlData pad;
    sceClibMemset(&pad, 0, sizeof(pad));
    sceCtrlPeekBufferPositive(0, &pad, 1);
    unsigned int pressed = pad.buttons & ~prev_buttons;
    unsigned int released = ~pad.buttons & prev_buttons;
    prev_buttons = pad.buttons;

    extern int hud_fade_touch_timer;
    if (screen_touched) { cursor_shown = 0; hud_fade_touch_timer = 180; }
    else if (hud_fade_touch_timer > 0) hud_fade_touch_timer--;

    static int last_module = -2;
    int module = controls_module();
    if (module != last_module) {
        logv_info("controls: game module %d -> %d", last_module, module);
        last_module = module;
    }

    // Start in flight taps the game's pause button. The controls follow the
    // game: cursor while its menu/star map/choice window is open, flight
    // controls again as soon as it closes, however it was closed.
    if (controls_in_space() && (pressed & SCE_CTRL_START)) {
        int x, y;
        if (hud_pos(HUD_PAUSE, &x, &y)) {
            touch(HUD_POINTER(HUD_PAUSE), ACTION_DOWN, x, y);
            touch(HUD_POINTER(HUD_PAUSE), ACTION_UP, x, y);
        }
        pressed &= ~SCE_CTRL_START;
    }
    int mode = (controls_in_space() && !controls_overlay_open()) ? 1 : 0;
    // The HUD is laid out a little after flight starts: scan once a second
    // (6 times a second) until its buttons are found.
    if (!hud_cache_loaded) {
        hud_cache_loaded = 1;
        if (controls_load_hud_cache()) assign_roles();
    }
    // Rescan once per session (cached boxes keep working meanwhile).
    static int probe_timer = 0, probed = 0;
    if (mode != 1) probe_timer = 0;
    else if (!probed && probe_timer-- <= 0) {
        probe_timer = 10;
        if (controls_probe_hud()) { assign_roles(); probed = 1; }
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
    }
    void controls_flight_tick(int flight, unsigned int pressed, SceCtrlData *pad);
    controls_flight_tick(mode, pressed, &pad);
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
