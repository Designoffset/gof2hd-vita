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
int controls_in_space() {
    if (!GetCurrentApplicationModule || !Globals__appManager || !*Globals__appManager)
        return 0;
    return GetCurrentApplicationModule(*Globals__appManager) == 2;
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

void patch__controls_fix() {
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

    Globals__appManager = (void *) so_symbol(&so_mod, "_ZN7Globals10appManagerE");
    gEngine = (void *) so_symbol(&so_mod, "gEngine");
}

// Referenced by AFakeNative's controls helper; unused in the 2012 build's input path.
void sendFakeTouch() { }
