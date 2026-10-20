/*
 * station_fix.c
 *
 * The station menu draws the system info (faction emblem, system name, tech
 * level, race) at the top of the left panel. That layout assumes an iPad's
 * 768-pixel-high screen; at 544 pixels it lands on top of the first menu
 * buttons (Hangar, Space Lounge). Skip that block: right after the panel box
 * is drawn, branch straight to where the menu buttons are drawn. The same
 * details are on the Status screen.
 *
 *   dbafc: ldr.w r8, [sp, #20]   ->  b.w dbd52
 */

void patch__station_fix(void) {
    uint8_t *at = (uint8_t *)(so_mod.text_base + 0xdbafc);
    static const uint8_t expect[4] = { 0xdd, 0xf8, 0x14, 0x80 };
    static const uint8_t branch[4] = { 0x00, 0xf0, 0x29, 0xb9 };  // b.w +0x252
    if (memcmp(at, expect, 4) != 0) {
        log_info("station fix: unexpected code, not applied");
        return;
    }
    kuKernelCpuUnrestrictedMemcpy(at, branch, 4);
}
