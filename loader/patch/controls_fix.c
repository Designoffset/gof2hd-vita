/*
 * patch/controls_fix.c
 *
 * Physical controls for the GOF2 HD 2012 Android build.
 *
 * Based on the gof2-vita controls patch by Volodymyr Atamanenko. That build
 * received key events from the Xperia Play input layer; the 2012 build has no
 * key input at all, so the loader polls the Vita's buttons itself (see main.c)
 * and turns them into touches on the game's own HUD button positions.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "AFakeNative/keycodes.h"
#include "utils/settings.h"
#include "utils/logger.h"
#include "utils/screen.h"
#include <string.h>
#include <psp2/ctrl.h>
#include <stdio.h>

void ** Globals__appManager;
void ** gEngine;
int* Globals__turret_view_y;
int* Globals__turret_view_x;
int* Globals__fire_y;
int* Globals__fire_x;
int* Globals__sec_fire_y;
int* Globals__sec_fire_x;
int* Globals__boost_y;
int* Globals__boost_x;
int* Globals__autopilot_y;
int* Globals__autopilot_x;
int* Globals__fast_forward_y;
int* Globals__fast_forward_x;
int* Globals__pause_y;
int* Globals__pause_x;
int* Globals__action_menu_y;
int* Globals__action_menu_x;
int* Globals__smallButton_dim;
int* Globals__touch_stick_x;
int* Globals__touch_stick_y;

int (*GetCurrentApplicationModule)(void *appManager);
void (*OnTouchBegin)(void * this, int x, int y, int keycode);
void (*OnTouchEnd)(void * this, int x, int y, int keycode);
void (*AddTouch)(int pointerId, int action, int x, int y);

// Module 2 is in-flight (space). Buttons only map to HUD touches there.
// The game's own helper (what the Java side calls) reads the live app manager.
static int (*ndk_getCurrentApplicationModule)(void) = NULL;
int controls_module(void) {
    return ndk_getCurrentApplicationModule ? ndk_getCurrentApplicationModule() : -1;
}
int controls_in_space() {
    return controls_module() == 2;
}

// Menus, the star map, hacking, choices and dialogue drawn over the flight view.
#define OVERLAYS 6
static void *overlay_flags[OVERLAYS];
static const int overlay_size[OVERLAYS] = { 4, 1, 4, 4, 4, 4 };
static const char *overlay_name[OVERLAYS] = { "menu", "starmap", "hacking", "choice", "dialogue", "submenu" };

static int overlay_value(int i) {
    if (!overlay_flags[i]) return 0;
    int v = overlay_size[i] == 1 ? *(unsigned char *)overlay_flags[i] : *(int *)overlay_flags[i];
    // subMenuIndex is -1 when no in-game sub menu is open (the game's own
    // IsInGameSubMenuActive test); report it as a 0/1 flag like the rest.
    if (i == 5) return v != -1;
    return v;
}

// Only an exact 1 counts as "open": a flag that holds anything else is not a
// plain bool and is ignored. Changes are logged.
//
// The pause menu and the in-flight sub menus (is_menu_visible, subMenuIndex)
// only count once they have been seen closed during this flight: in some
// sessions they are left set for the whole flight, and a flag that never
// closes must not take the ship controls away. Dialogue lines don't stop the
// ship, so they never switch to the cursor.
int controls_overlay_open(void) {
    static int last[OVERLAYS] = { -1, -1, -1, -1, -1, -1 };
    static int armed[OVERLAYS];
    static int was_space = 0;
    int space = controls_in_space();
    if (space && !was_space)
        for (int i = 0; i < OVERLAYS; i++) armed[i] = 0;
    was_space = space;
    int open = 0;
    for (int i = 0; i < OVERLAYS; i++) {
        int v = overlay_value(i);
        if (v != last[i]) {
            logv_info("controls: %s flag = %d", overlay_name[i], v);
            last[i] = v;
        }
        if (v != 1) { armed[i] = 1; continue; }
        if (i == 1 || i == 2 || i == 3) open = 1;
        else if ((i == 0 || i == 5) && armed[i]) open = 1;
    }
    return open;
}

static void * app_manager() {
    return *(void **)(*(void **)(gEngine) + 0x28);
}

static int key_position(int keycode, int *x, int *y) {
    switch (keycode) {
        case AKEYCODE_DPAD_CENTER:   *x = *Globals__fire_x;          *y = *Globals__fire_y; return 1;
        case AKEYCODE_BACK:          *x = *Globals__turret_view_x;   *y = *Globals__turret_view_y; return 1;
        case AKEYCODE_BUTTON_START:  *x = *Globals__pause_x;         *y = *Globals__pause_y; return 1;
        case AKEYCODE_BUTTON_X:      *x = *Globals__sec_fire_x;      *y = *Globals__sec_fire_y; return 1;
        case AKEYCODE_BUTTON_Y:      *x = *Globals__boost_x;         *y = *Globals__boost_y; return 1;
        case AKEYCODE_BUTTON_L1:     *x = *Globals__autopilot_x;     *y = *Globals__autopilot_y; return 1;
        case AKEYCODE_BUTTON_R1:     *x = *Globals__fast_forward_x + 2; *y = *Globals__fast_forward_y + 2; return 1;
        case AKEYCODE_BUTTON_SELECT: *x = *Globals__action_menu_x;   *y = *Globals__action_menu_y; return 1;
        default: return 0;
    }
}

void keyPressed(int keycode) {
    int x, y;
    if (controls_in_space() && key_position(keycode, &x, &y))
        OnTouchBegin(app_manager(), x, y, keycode);
}

void keyReleased(int keycode) {
    int x, y;
    if (controls_in_space() && key_position(keycode, &x, &y))
        OnTouchEnd(app_manager(), x, y, keycode);
}

// Left stick -> the game's on-screen steering stick. nx/ny are 0..1.
void stickTouch(int pointerId, int action, float nx, float ny) {
    if (!controls_in_space()) return;
    float dim = (float)*Globals__smallButton_dim;
    float x = nx * dim * 2 + ((float)*Globals__touch_stick_x - dim);
    float y = ny * dim * 2 + ((float)*Globals__touch_stick_y - dim);
    AddTouch(pointerId, action, (int)x, (int)y);
}

// ---- HUD layout probing -------------------------------------------------
// In the 2012 build the HUD button positions live in the Hud object, not in
// the Globals the original port read. Hud::touchedElement(x, y) says which
// element is under a point, so a grid scan finds every button's box.

static void *(*GetApplicationModule)(void *appManager, unsigned int id) = NULL;
static unsigned int (*Hud_touchedElement)(void *hud, unsigned int x, unsigned int y) = NULL;

typedef struct { unsigned int id; int x0, y0, x1, y1, n; } hud_box;
hud_box hud_boxes[32];
int hud_box_count = 0;

static void *current_hud(void) {
    if (!GetApplicationModule || !Globals__appManager || !*Globals__appManager) return NULL;
    void *game = GetApplicationModule(*Globals__appManager, 2);
    if (!game) return NULL;
    return *(void **)((uintptr_t)game + 0x74);
}

// MGame::switchCamera cycles 0 chase -> 1 turret -> 3 free look -> 0 (the
// HUD camera button, mapped to Circle). In free look the stick turns the
// camera instead of the ship, so flight can start with the controls seemingly
// dead until Circle is pressed. Put each new flight back on the chase camera.
static void (*MGame_switchCamera)(void *game, int cam) = NULL;
static uintptr_t MGame_vtable;

static void *current_game(void) {
    if (!GetApplicationModule || !Globals__appManager || !*Globals__appManager) return NULL;
    void *game = GetApplicationModule(*Globals__appManager, 2);
    if (!game) return NULL;
    if (MGame_vtable && *(uintptr_t *)game != MGame_vtable) {
        static int warned = 0;
        if (!warned++) logv_info("controls: module 2 vtable %p != MGame %p", *(void **)game, (void *)MGame_vtable);
        return NULL;
    }
    return game;
}

#define MG_B(g, o) (*(unsigned char *)((uintptr_t)(g) + (o)))
#define MG_I(g, o) (*(int *)((uintptr_t)(g) + (o)))

// Globals::options+0x11 picks the steering source in MGame::OnUpdate:
// 0 = accelerometer (tilt), anything else = the on-screen analog stick.
// The Vita feeds no tilt, so with tilt selected the ship can't be steered
// at all (only the free-look camera moves). Always use the stick.
static unsigned char *Globals__options;
static void (*Ego_initManeuver)(void *ego, int kind);
static void (*Ego_changeThrust)(void *ego, float delta);
static void (*Ego_throttleChanged)(void *ego);

// First-person view. The engine has one (MGame camera 2: the camera sits at
// the ship's own matrix and the ship model is hidden), but MGame::switchCamera
// skips it (2 -> 3). Select toggles it directly; O still cycles the others.
static void (*Cam_enableFirstPerson)(void *cam, int on);
static void (*Cam_setRotationAroundTarget)(void *cam, int on);
static void (*Script_resetCamera)(void *script, void *level);
static void (*Ego_setFreeLookMode)(void *ego, int on);
static void (*Ego_setTurretMode)(void *ego, int on);

static void first_person(void *g, int on) {
    void *ego = *(void **)((uintptr_t)g + 0x58);
    if (!on) { if (MGame_switchCamera) MGame_switchCamera(g, 0); return; }
    if (!Cam_enableFirstPerson || !ego) return;
    MG_I(g, 0x14) = 2;
    MG_B(g, 0x18) = 0;
    MG_B(g, 0xd5) = 0;
    if (Ego_setTurretMode) Ego_setTurretMode(ego, 0);
    if (Script_resetCamera) Script_resetCamera(*(void **)((uintptr_t)g + 0x7c), *(void **)((uintptr_t)g + 0x78));
    void *cam = *(void **)((uintptr_t)g + 0xf4);
    if (Cam_setRotationAroundTarget) Cam_setRotationAroundTarget(cam, 0);
    if (Ego_setFreeLookMode) Ego_setFreeLookMode(ego, 0);
    Cam_enableFirstPerson(cam, 1);
}

static float rstick(unsigned char v) {
    float a = ((float)v - 128.f) / 127.f;
    if (a > -0.25f && a < 0.25f) return 0.f;
    if (a > 1.f) a = 1.f;
    if (a < -1.f) a = -1.f;
    return a;
}

void controls_flight_tick(int flight, unsigned int pressed, SceCtrlData *pad) {
    static int was_flight = 0, last_cam = -1, settle = 0;
    void *g = flight ? current_game() : NULL;
    if (!g) { was_flight = 0; return; }
    if (!was_flight) {
        was_flight = 1; settle = 30;
        if (Globals__options)
            logv_info("controls: options %02x %02x %02x %02x | 10:%02x 11:%02x 12:%02x 13:%02x",
                      Globals__options[0xc], Globals__options[0xd], Globals__options[0xe], Globals__options[0xf],
                      Globals__options[0x10], Globals__options[0x11], Globals__options[0x12], Globals__options[0x13]);
    }
    if (Globals__options && Globals__options[0x11] == 0) {
        Globals__options[0x11] = 1;
        log_info("controls: steering was set to tilt, switched to the analog stick");
    }
    int cam = MG_I(g, 0x14);
    if (cam != last_cam) {
        logv_info("controls: camera %d -> %d", last_cam, cam);
        last_cam = cam;
    }
    // Half a second into the flight (after the launch sequence has set up
    // its camera), switch away from turret/free look once.
    if (settle > 0 && --settle == 0 && cam != 0 && MGame_switchCamera &&
        !MG_B(g, 0x5d) && !MG_B(g, 0x5f)) {
        logv_info("controls: flight started on camera %d, switching to chase", cam);
        MGame_switchCamera(g, 0);
    }

    void *ego = *(void **)((uintptr_t)g + 0x58);
    int free_to_fly = ego && !MG_B(g, 0x5d) && !MG_B(g, 0x5f) && !MG_B(g, 0x60);
    if (free_to_fly && pad) {
        // L / R: barrel roll left / right (the game's swipe maneuver: 1 = left, 2 = right).
        float ry = rstick(pad->ry);
        int roll = 0;
        if (pressed & SCE_CTRL_LTRIGGER) roll = 1;
        if (pressed & SCE_CTRL_RTRIGGER) roll = 2;
        if (roll && Ego_initManeuver) Ego_initManeuver(ego, roll);
        // Select: cockpit (first-person) view on / off.
        if (pressed & SCE_CTRL_SELECT) {
            int fp = MG_I(g, 0x14) == 2;
            logv_info("controls: first-person view %s", fp ? "off" : "on");
            first_person(g, !fp);
        }
        // Right stick up/down: throttle (full range in about a second).
        if (ry != 0.f && Ego_changeThrust) {
            Ego_changeThrust(ego, -ry / 60.f);
            if (Ego_throttleChanged) Ego_throttleChanged(ego);
        }
    }

    if (pressed) {
        static int n = 0;
        if (n < 40) {
            n++;
            void *hud = *(void **)((uintptr_t)g + 0x74);
            logv_info("controls: press %08x cam %d hudbits %08x menu %d cut %d first %p stick %d,%d c %d,%d r %d",
                      pressed, MG_I(g, 0x14), MG_I(g, 0xf8), MG_B(g, 0x5d), MG_B(g, 0x5f),
                      *(void **)((uintptr_t)g + 0xc0),
                      hud ? *(unsigned short *)((uintptr_t)hud + 0x41e) : -1,
                      hud ? *(unsigned short *)((uintptr_t)hud + 0x420) : -1,
                      hud ? *(unsigned short *)((uintptr_t)hud + 0x424) : -1,
                      hud ? *(unsigned short *)((uintptr_t)hud + 0x426) : -1,
                      hud ? *(int *)((uintptr_t)hud + 0x4e0) : -1);
        }
    }
}

// The scan only works once the HUD exists, a little while into each flight.
// Keep the last good result on the memory card so the buttons work from the
// first frame next time.
#define HUD_CACHE DATA_PATH "hud_layout_1280.bin"

static void controls_save_hud_cache(void) {
    FILE *f = fopen(HUD_CACHE, "wb");
    if (!f) return;
    fwrite(&hud_box_count, sizeof(int), 1, f);
    fwrite(hud_boxes, sizeof(hud_box), hud_box_count, f);
    fclose(f);
}

// A layout scanned on a Vita ships inside the VPK, so the buttons work from
// the first second of the first flight; a fresh scan replaces it in ux0:data.
#define HUD_DEFAULT "app0:hud_layout_1280.bin"

int controls_load_hud_cache(void) {
    FILE *f = fopen(HUD_CACHE, "rb");
    if (!f) f = fopen(HUD_DEFAULT, "rb");
    if (!f) return 0;
    int n = 0;
    if (fread(&n, sizeof(int), 1, f) == 1 && n > 0 && n <= 32 &&
        fread(hud_boxes, sizeof(hud_box), n, f) == (size_t)n)
        hud_box_count = n;
    fclose(f);
    logv_info("hud: %d cached elements", hud_box_count);
    return hud_box_count;
}

int controls_probe_hud(void) {
    void *hud = current_hud();
    static int logged = 0;
    if (logged < 5) {
        logged++;
        logv_info("hud: probe hud=%p flag=%d", hud, hud ? *(unsigned char *)((uintptr_t)hud + 0x282) : -1);
    }
    if (!hud || !Hud_touchedElement) return 0;
    // touchedElement has a side-effect path (it presses TouchButtons) when
    // this flag is set; only scan when it isn't.
    if (*(unsigned char *)((uintptr_t)hud + 0x282)) return 0;
    // Scan into a scratch list so a scan that comes up short doesn't wipe
    // the boxes already known (from the cache or an earlier scan).
    static hud_box found[32];
    int found_count = 0;
    #define hud_boxes found
    #define hud_box_count found_count
    for (int y = 4; y < GAME_H; y += 8) {
        for (int x = 4; x < GAME_W; x += 8) {
            unsigned int id = Hud_touchedElement(hud, x, y);
            if (!id) continue;
            int i;
            for (i = 0; i < hud_box_count; i++) if (hud_boxes[i].id == id) break;
            if (i == hud_box_count) {
                if (hud_box_count == 32) continue;
                hud_boxes[i] = (hud_box){ id, x, y, x, y, 0 };
                hud_box_count++;
            }
            hud_box *b = &hud_boxes[i];
            if (x < b->x0) b->x0 = x;
            if (y < b->y0) b->y0 = y;
            if (x > b->x1) b->x1 = x;
            if (y > b->y1) b->y1 = y;
            b->n++;
        }
    }
    #undef hud_boxes
    #undef hud_box_count
    if (found_count < 4) return 0;
    memcpy(hud_boxes, found, sizeof(found));
    hud_box_count = found_count;
    controls_save_hud_cache();
    for (int i = 0; i < hud_box_count; i++)
        logv_info("hud: element 0x%x box %d,%d - %d,%d (center %d,%d, %d hits)", hud_boxes[i].id,
                  hud_boxes[i].x0, hud_boxes[i].y0, hud_boxes[i].x1, hud_boxes[i].y1,
                  (hud_boxes[i].x0 + hud_boxes[i].x1) / 2, (hud_boxes[i].y0 + hud_boxes[i].y1) / 2, hud_boxes[i].n);
    return hud_box_count;
}

void patch__controls_fix() {
    Cam_enableFirstPerson = (void *) so_symbol(&so_mod, "_ZN18TargetFollowCamera20enableFirstPersonCamEb");
    Cam_setRotationAroundTarget = (void *) so_symbol(&so_mod, "_ZN18TargetFollowCamera23setRotationAroundTargetEb");
    Script_resetCamera = (void *) so_symbol(&so_mod, "_ZN11LevelScript11resetCameraEP5Level");
    Ego_setFreeLookMode = (void *) so_symbol(&so_mod, "_ZN9PlayerEgo15setFreeLookModeEb");
    Ego_setTurretMode = (void *) so_symbol(&so_mod, "_ZN9PlayerEgo13setTurretModeEb");
    Globals__options = (unsigned char *) so_symbol(&so_mod, "_ZN7Globals7optionsE");
    Ego_initManeuver = (void *) so_symbol(&so_mod, "_ZN9PlayerEgo12initManeuverEi");
    Ego_changeThrust = (void *) so_symbol(&so_mod, "_ZN9PlayerEgo12changeThrustEf");
    Ego_throttleChanged = (void *) so_symbol(&so_mod, "_ZN9PlayerEgo15throttleChangedEv");
    MGame_switchCamera = (void *) so_symbol(&so_mod, "_ZN5MGame12switchCameraEi");
    { uintptr_t vt = so_symbol(&so_mod, "_ZTV5MGame"); MGame_vtable = vt ? vt + 8 : 0; }
    GetApplicationModule = (void *) so_symbol(&so_mod, "_ZN11AbyssEngine18ApplicationManager20GetApplicationModuleEj");
    Hud_touchedElement = (void *) so_symbol(&so_mod, "_ZN3Hud14touchedElementEjj");
    OnTouchBegin = (void *) so_symbol(&so_mod, "_ZN11AbyssEngine18ApplicationManager12OnTouchBeginEiiPv");
    OnTouchEnd = (void *) so_symbol(&so_mod, "_ZN11AbyssEngine18ApplicationManager10OnTouchEndEiiPv");
    GetCurrentApplicationModule = (void *) so_symbol(&so_mod, "_ZNK11AbyssEngine18ApplicationManager27GetCurrentApplicationModuleEv");
    AddTouch = (void *) so_symbol(&so_mod, "_Z8AddTouchiiii");

    Globals__turret_view_y = (int *) so_symbol(&so_mod, "_ZN7Globals13turret_view_yE");
    Globals__turret_view_x = (int *) so_symbol(&so_mod, "_ZN7Globals13turret_view_xE");
    Globals__fire_y = (int *) so_symbol(&so_mod, "_ZN7Globals6fire_yE");
    Globals__fire_x = (int *) so_symbol(&so_mod, "_ZN7Globals6fire_xE");
    Globals__sec_fire_y = (int *) so_symbol(&so_mod, "_ZN7Globals10sec_fire_yE");
    Globals__sec_fire_x = (int *) so_symbol(&so_mod, "_ZN7Globals10sec_fire_xE");
    Globals__boost_y = (int *) so_symbol(&so_mod, "_ZN7Globals7boost_yE");
    Globals__boost_x = (int *) so_symbol(&so_mod, "_ZN7Globals7boost_xE");
    Globals__autopilot_y = (int *) so_symbol(&so_mod, "_ZN7Globals11autopilot_yE");
    Globals__autopilot_x = (int *) so_symbol(&so_mod, "_ZN7Globals11autopilot_xE");
    Globals__fast_forward_x = (int *) so_symbol(&so_mod, "_ZN7Globals14fast_forward_xE");
    Globals__fast_forward_y = (int *) so_symbol(&so_mod, "_ZN7Globals14fast_forward_yE");
    Globals__pause_y = (int *) so_symbol(&so_mod, "_ZN7Globals7pause_yE");
    Globals__pause_x = (int *) so_symbol(&so_mod, "_ZN7Globals7pause_xE");
    Globals__action_menu_y = (int *) so_symbol(&so_mod, "_ZN7Globals13action_menu_yE");
    Globals__action_menu_x = (int *) so_symbol(&so_mod, "_ZN7Globals13action_menu_xE");
    Globals__smallButton_dim = (int *) so_symbol(&so_mod, "_ZN7Globals15smallButton_dimE");
    Globals__touch_stick_x = (int *) so_symbol(&so_mod, "_ZN7Globals13touch_stick_xE");
    Globals__touch_stick_y = (int *) so_symbol(&so_mod, "_ZN7Globals13touch_stick_yE");

    overlay_flags[0] = (unsigned char *) so_symbol(&so_mod, "_ZN7Globals15is_menu_visibleE");
    overlay_flags[1] = (unsigned char *) so_symbol(&so_mod, "_ZN7Globals16isStarMapVisibleE");
    overlay_flags[2] = (unsigned char *) so_symbol(&so_mod, "_ZN7Globals18is_hacking_visibleE");
    overlay_flags[3] = (unsigned char *) so_symbol(&so_mod, "_ZN7Globals24is_choice_window_visibleE");
    overlay_flags[4] = (unsigned char *) so_symbol(&so_mod, "_ZN7Globals26is_dialogue_window_visibleE");
    overlay_flags[5] = (unsigned char *) so_symbol(&so_mod, "_ZN7Globals12subMenuIndexE");

    ndk_getCurrentApplicationModule = (void *) so_symbol(&so_mod, "ndk_getCurrentApplicationModule");
    Globals__appManager = (void *) so_symbol(&so_mod, "_ZN7Globals10appManagerE");
    gEngine = (void *) so_symbol(&so_mod, "gEngine");
}

// Referenced by AFakeNative's controls helper; unused in the 2012 build's input path.
void sendFakeTouch() { }
