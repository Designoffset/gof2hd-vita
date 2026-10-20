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
#include <string.h>
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
static void *overlay_flags[5];
static const int overlay_size[5] = { 4, 1, 4, 4, 4 };
static const char *overlay_name[5] = { "menu", "starmap", "hacking", "choice", "dialogue" };

static int overlay_value(int i) {
    if (!overlay_flags[i]) return 0;
    return overlay_size[i] == 1 ? *(unsigned char *)overlay_flags[i] : *(int *)overlay_flags[i];
}

// Only an exact 1 counts as "open": a flag that holds anything else is not a
// plain bool and is ignored. Changes are logged.
int controls_overlay_open(void) {
    static int last[5] = { -1, -1, -1, -1, -1 };
    int open = 0;
    for (int i = 0; i < 5; i++) {
        int v = overlay_value(i);
        if (v != last[i]) {
            logv_info("controls: %s flag = %d", overlay_name[i], v);
            last[i] = v;
        }
        // The "menu" flag stays set for the whole flight and dialogue lines
        // don't stop the ship, so only the star map, hacking and choice
        // windows switch to the cursor.
        if (v == 1 && (i == 1 || i == 2 || i == 3)) open = 1;
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

// The scan only works once the HUD exists, a little while into each flight.
// Keep the last good result on the memory card so the buttons work from the
// first frame next time.
#define HUD_CACHE DATA_PATH "hud_layout.bin"

static void controls_save_hud_cache(void) {
    FILE *f = fopen(HUD_CACHE, "wb");
    if (!f) return;
    fwrite(&hud_box_count, sizeof(int), 1, f);
    fwrite(hud_boxes, sizeof(hud_box), hud_box_count, f);
    fclose(f);
}

int controls_load_hud_cache(void) {
    FILE *f = fopen(HUD_CACHE, "rb");
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
    for (int y = 4; y < 544; y += 8) {
        for (int x = 4; x < 960; x += 8) {
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

    ndk_getCurrentApplicationModule = (void *) so_symbol(&so_mod, "ndk_getCurrentApplicationModule");
    Globals__appManager = (void *) so_symbol(&so_mod, "_ZN7Globals10appManagerE");
    gEngine = (void *) so_symbol(&so_mod, "gEngine");
}

// Referenced by AFakeNative's controls helper; unused in the 2012 build's input path.
void sendFakeTouch() { }
