/*
 * station_fix.c
 *
 * The station menu puts the faction emblem and the system info (system name,
 * tech level, race) at the top of the left panel and stacks the five menu
 * buttons (Hangar ... Status) up from the bottom. That layout is made for an
 * iPad's 768-pixel-high screen; at 544 pixels the buttons reach up into the
 * info block. Once a station's menu exists, make the five buttons a little
 * shorter and start them below the info block, keeping their bottom edge
 * where it was.
 */

static void **Globals__layout_p;     // Layout*
static void **Globals__canvas_p;     // PaintCanvas*
static int *Globals__font_p;         // current font id
static uintptr_t ModStation_vtable;
static int (*Canvas_TextHeight)(void *, unsigned);
static int (*Canvas_ImgHeight)(void *, unsigned);

#define TB_Y(b)  (*(int *)((uintptr_t)(b) + 0x7c))
#define TB_H(b)  (*(int *)((uintptr_t)(b) + 0x88))
#define LAY(o)   (*(int *)((uintptr_t)*Globals__layout_p + (o)))

void station_tick(void) {
    if (!GetApplicationModule || !Globals__appManager || !*Globals__appManager) return;
    if (!Globals__layout_p || !*Globals__layout_p || !Globals__canvas_p || !*Globals__canvas_p) return;
    void *st = GetApplicationModule(*Globals__appManager, 5);  // 5 = station (log: "game module 1 -> 5")
    if (!st || *(uintptr_t *)st != ModStation_vtable) return;

    int *arr = *(int **)((uintptr_t)st + 0x8c);           // Array<TouchButton*>
    if (!arr || arr[0] != 5 || !arr[1]) return;
    void **b = (void **)(uintptr_t)arr[1];
    for (int i = 0; i < 5; i++) if (!b[i]) return;

    // Where the info block ends (same formulas ModStation::OnRender2D uses).
    int base = LAY(12) + LAY(32);
    int emblem_y = base + 2 * LAY(44);
    int line3_y = base + 2 * LAY(4) + 6 * LAY(44);
    int text_h = Canvas_TextHeight ? Canvas_TextHeight(*Globals__canvas_p, *Globals__font_p) : 2 * LAY(44);
    int emblem_h = Canvas_ImgHeight ? Canvas_ImgHeight(*(void **)((uintptr_t)st + 4), *(unsigned *)((uintptr_t)st + 0x4c)) : 0;
    int info_bottom = line3_y + text_h;
    if (emblem_y + emblem_h > info_bottom) info_bottom = emblem_y + emblem_h;
    info_bottom += LAY(44);

    int y0 = TB_Y(b[0]);
    if (y0 >= info_bottom) return;                         // already fits (or done)

    int h = TB_H(b[0]);
    int gap = TB_Y(b[1]) - y0 - h;
    if (gap < 0) gap = 0;
    int bottom = TB_Y(b[4]) + TB_H(b[4]);
    int new_h = (bottom - info_bottom - 4 * gap) / 5;
    if (new_h < h / 2) {                                    // squeeze the gaps too
        gap /= 2;
        new_h = (bottom - info_bottom - 4 * gap) / 5;
    }
    if (new_h < h / 2) return;                              // give up rather than break it

    for (int i = 0; i < 5; i++) {
        TB_H(b[i]) = new_h;
        TB_Y(b[i]) = info_bottom + i * (new_h + gap);
    }
    static int logged = 0;
    if (!logged++) logv_info("station: buttons %d..%d h %d -> %d..%d h %d (info ends %d)",
                             y0, bottom, h, info_bottom, bottom, new_h, info_bottom);
}

void patch__station_fix(void) {
    Globals__layout_p = (void **)so_symbol(&so_mod, "_ZN7Globals6layoutE");
    Globals__canvas_p = (void **)so_symbol(&so_mod, "_ZN7Globals6CanvasE");
    Globals__font_p = (int *)so_symbol(&so_mod, "_ZN7Globals4fontE");
    uintptr_t vt = so_symbol(&so_mod, "_ZTV10ModStation");
    ModStation_vtable = vt ? vt + 8 : 0;
    Canvas_TextHeight = (void *)so_symbol(&so_mod, "_ZN11AbyssEngine11PaintCanvas13GetTextHeightEj");
    Canvas_ImgHeight = (void *)so_symbol(&so_mod, "_ZN11AbyssEngine11PaintCanvas16GetImage2DHeightEj");
}

/*
 * Credits are pinned at 999,999,999 (money_fix.c). Show that balance as an
 * infinity sign instead of the number. Layout::formatCredits also formats
 * prices and rewards, so only the exact pinned value is replaced.
 */
typedef void (*FormatCredits_t)(void *out, void *layout, int credits);
static FormatCredits_t orig_formatCredits;
static void (*String_from_u16)(void *self, const unsigned short *s, int reverse);

static void formatCredits_hook(void *out, void *layout, int credits) {
    static const unsigned short infinity[] = { 0x221E, 0 };
    if (credits == 999999999 && String_from_u16) {
        String_from_u16(out, infinity, 0);  // copies; the flag means "reverse"
        return;
    }
    orig_formatCredits(out, layout, credits);
}

void patch__credits_display(void) {
    String_from_u16 = (void *)so_symbol(&so_mod, "_ZN11AbyssEngine6StringC1EPKtb");
    if (!String_from_u16) String_from_u16 = (void *)so_symbol(&so_mod, "_ZN11AbyssEngine6StringC2EPKtb");
    redirect(0x2128b4, "_ZN6Layout13formatCreditsEi", (void **)&orig_formatCredits, formatCredits_hook);
}
