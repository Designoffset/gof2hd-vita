/*
 * money_fix.c
 *
 * Infinite money: the player's credits (Status + 0x1b0) are kept at
 * 999,999,999. getCredits/setCredits are too short to hook safely, so the
 * value is pinned once per frame through Globals::status, and changeCredits
 * (every purchase and reward) is replaced so spending never lowers it.
 */

#define GOF2_CREDITS 999999999
#define STATUS_CREDITS(status) (*(volatile int *)((uintptr_t)(status) + 0x1b0))

static void **Globals__status = NULL;

static void Status_changeCredits(void *status, int delta) {
    STATUS_CREDITS(status) = GOF2_CREDITS;
}

void money_tick(void) {
    if (Globals__status && *Globals__status)
        STATUS_CREDITS(*Globals__status) = GOF2_CREDITS;
}

void patch__money(void) {
    Globals__status = (void **)so_symbol(&so_mod, "_ZN7Globals6statusE");
    uintptr_t f = so_symbol(&so_mod, "_ZN6Status13changeCreditsEi");
    if (f) hook_addr(f, (uintptr_t)Status_changeCredits);
}
