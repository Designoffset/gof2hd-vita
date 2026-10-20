#include "utils/logger.h"
#include "utils/screen.h"
#include <math.h>

so_hook setGameOrientation_hook;
void setGameOrientation(void * this, int mode) {
    static int logs = 0;
    if (logs++ < 10) logv_info("SetGameOrientation(%d) -> 2", mode);
    SO_CONTINUE(void *, setGameOrientation_hook, this, 2);
}

int getDisplayHeight(void * this) {
    return GAME_H;
}

int getDisplayWidth(void * this) {
    return GAME_W;
}

so_hook NewsTicker_Constructor_hook;
void NewsTicker_Constructor(void* this, int posX, int posY, int width, int param4, int param5) {
    // In some parts of the game, it still thinks of portrait orientation as primary,
    // thus we need to invert to multiply horizontal coordinates by the aspect ratio here
    int posX_real = (int) floorf((float)posX * ((float)GAME_W / (float)GAME_H));
    int width_real = (int) floorf((float)width * ((float)GAME_W / (float)GAME_H));
    SO_CONTINUE(void *, NewsTicker_Constructor_hook, this, posX_real, posY, width_real, param4, param5 );
}

void patch__screen_fix() {
    hook_addr(so_symbol(&so_mod, "_ZN11AbyssEngine6Engine16GetDisplayHeightEv"), (uintptr_t)getDisplayHeight);
    hook_addr(so_symbol(&so_mod, "_ZN11AbyssEngine6Engine15GetDisplayWidthEv"), (uintptr_t)getDisplayWidth);

    setGameOrientation_hook = hook_addr(so_symbol(&so_mod, "_ZN11AbyssEngine11PaintCanvas18SetGameOrientationENS_13LandscapeModeE"), (uintptr_t)setGameOrientation);
    NewsTicker_Constructor_hook = hook_addr(so_symbol(&so_mod, "_ZN10NewsTickerC1Eiiiii"), (uintptr_t) NewsTicker_Constructor);
}

// GOF2 HD 2012: OnCreateApplication() only enables Globals::retinaDisplay when
// the display is >= 640 px, so at 960x544 the game asks for the plain
// low-res texture set, which this APK doesn't contain (it ships the
// *_iphone4 / *_ipad_large sets). NOP the "blt" so the Vita is treated like a
// retina phone (iPhone 4 is 960x640).
void patch__retina() {
    uint8_t *blt = (uint8_t *)(so_mod.text_base + 0xd07de);
    if (blt[0] == 0x0d && blt[1] == 0xdb) { // blt.n
        uint16_t nop = 0xbf00;
        kuKernelCpuUnrestrictedMemcpy(blt, &nop, sizeof(nop));
    }
}
