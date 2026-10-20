/*
 * hud_fade.c
 *
 * With the Vita's buttons doing the work, the on-screen touch controls only
 * cover the view. Draw them see-through: while Hud::draw runs, any HUD image
 * that sits on one of the touch buttons found by the HUD scan (stick, boost,
 * fire cluster...) is drawn at HUD_FADE_ALPHA. The pause button, bars, radar
 * and text are left alone. Touching the screen brings them back to full
 * strength for a few seconds.
 *
 * The draw calls go through the game's PLT, so the GOT slots are pointed at
 * the wrappers below and the wrappers call the originals.
 */

#define HUD_FADE_ALPHA 0.3f

static int hud_drawing = 0;
int hud_fade_touch_timer = 0;   // frames of full opacity after a screen touch

typedef void (*HudDraw_t)(void *, long long, long long, void *, int, unsigned, unsigned);
typedef void (*Draw3_t)(void *, unsigned, int, int);
typedef void (*Draw4_t)(void *, unsigned, int, int, unsigned char);
typedef void (*Draw5_t)(void *, unsigned, int, int, unsigned char, unsigned char);
typedef void (*Draw8_t)(void *, unsigned, int, int, int, int, unsigned char, unsigned char, unsigned char);

static HudDraw_t orig_HudDraw;
static Draw3_t orig_Draw3;
static Draw4_t orig_Draw4;
static Draw5_t orig_Draw5;
static Draw8_t orig_Draw8;
static void (*Canvas_SetColor)(void *, unsigned int);
static int (*Canvas_ImgW)(void *, unsigned);
static int (*Canvas_ImgH)(void *, unsigned);

static int point_on_button(int x, int y) {
    for (int i = 0; i < hud_box_count; i++) {
        hud_box *b = &hud_boxes[i];
        if (b->id == 0x1) continue;  // pause stays solid
        if (x >= b->x0 - 4 && x <= b->x1 + 4 && y >= b->y0 - 4 && y <= b->y1 + 4) return 1;
    }
    return 0;
}

// Returns 1 (and dims the colour) when this image should be faded.
static int fade_begin(void *canvas, unsigned img, int x, int y, float *saved) {
    if (!hud_drawing || hud_fade_touch_timer > 0 || !hud_box_count || !Canvas_SetColor) return 0;
    int w = Canvas_ImgW ? Canvas_ImgW(canvas, img) : 0;
    int h = Canvas_ImgH ? Canvas_ImgH(canvas, img) : 0;
    if (!point_on_button(x, y) && !point_on_button(x + w / 2, y + h / 2)) return 0;
    float *c = (float *)((uintptr_t)canvas + 0x1fc);
    for (int i = 0; i < 4; i++) saved[i] = c[i];
    unsigned r = (unsigned)(saved[0] * 255.f + .5f), g = (unsigned)(saved[1] * 255.f + .5f),
             b = (unsigned)(saved[2] * 255.f + .5f), a = (unsigned)(saved[3] * HUD_FADE_ALPHA * 255.f + .5f);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255; if (a > 255) a = 255;
    Canvas_SetColor(canvas, (r << 24) | (g << 16) | (b << 8) | a);
    return 1;
}

static void fade_end(void *canvas, const float *saved) {
    unsigned v[4];
    for (int i = 0; i < 4; i++) {
        float f = saved[i] * 255.f + .5f;
        v[i] = f < 0 ? 0 : f > 255 ? 255 : (unsigned)f;
    }
    Canvas_SetColor(canvas, (v[0] << 24) | (v[1] << 16) | (v[2] << 8) | v[3]);
}

static void HudDraw_hook(void *hud, long long a, long long b, void *ego, int c, unsigned d, unsigned e) {
    hud_drawing = 1;
    orig_HudDraw(hud, a, b, ego, c, d, e);
    hud_drawing = 0;
}

static void Draw3_hook(void *cv, unsigned img, int x, int y) {
    float s[4]; int f = fade_begin(cv, img, x, y, s);
    orig_Draw3(cv, img, x, y);
    if (f) fade_end(cv, s);
}
static void Draw4_hook(void *cv, unsigned img, int x, int y, unsigned char p) {
    float s[4]; int f = fade_begin(cv, img, x, y, s);
    orig_Draw4(cv, img, x, y, p);
    if (f) fade_end(cv, s);
}
static void Draw5_hook(void *cv, unsigned img, int x, int y, unsigned char p, unsigned char q) {
    float s[4]; int f = fade_begin(cv, img, x, y, s);
    orig_Draw5(cv, img, x, y, p, q);
    if (f) fade_end(cv, s);
}
static void Draw8_hook(void *cv, unsigned img, int x, int y, int w, int h, unsigned char p, unsigned char q, unsigned char r) {
    float s[4]; int f = fade_begin(cv, img, x, y, s);
    orig_Draw8(cv, img, x, y, w, h, p, q, r);
    if (f) fade_end(cv, s);
}

// GOT slot of a PLT import, from its file address (see readelf -r).
static uintptr_t *got_slot(uintptr_t file_addr) {
    static intptr_t delta = 0;
    if (!delta) {
        uintptr_t st = so_symbol(&so_mod, "_ZN7Globals6statusE");
        if (!st) return NULL;
        delta = (intptr_t)st - 0x2181b0;
    }
    return (uintptr_t *)(file_addr + delta);
}

static void redirect(uintptr_t file_addr, const char *sym, void **orig, void *hook) {
    uintptr_t *slot = got_slot(file_addr);
    uintptr_t want = so_symbol(&so_mod, sym);
    if (!slot || !want || (*slot | 1) != (want | 1)) {
        logv_info("hud fade: slot for %s not matched (%p)", sym, slot ? (void *)*slot : NULL);
        return;
    }
    *orig = (void *)*slot;
    *slot = (uintptr_t)hook;
}

void patch__hud_fade(void) {
    Canvas_SetColor = (void *)so_symbol(&so_mod, "_ZN11AbyssEngine11PaintCanvas8SetColorEj");
    Canvas_ImgW = (void *)so_symbol(&so_mod, "_ZN11AbyssEngine11PaintCanvas15GetImage2DWidthEj");
    Canvas_ImgH = (void *)so_symbol(&so_mod, "_ZN11AbyssEngine11PaintCanvas16GetImage2DHeightEj");
    redirect(0x213b9c, "_ZN3Hud4drawExxP9PlayerEgobjj", (void **)&orig_HudDraw, HudDraw_hook);
    redirect(0x211dd8, "_ZN11AbyssEngine11PaintCanvas11DrawImage2DEjii", (void **)&orig_Draw3, Draw3_hook);
    redirect(0x2128ac, "_ZN11AbyssEngine11PaintCanvas11DrawImage2DEjiih", (void **)&orig_Draw4, Draw4_hook);
    redirect(0x211868, "_ZN11AbyssEngine11PaintCanvas11DrawImage2DEjiihh", (void **)&orig_Draw5, Draw5_hook);
    redirect(0x212064, "_ZN11AbyssEngine11PaintCanvas11DrawImage2DEjiiiihhh", (void **)&orig_Draw8, Draw8_hook);
}
