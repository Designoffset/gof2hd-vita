# Builds data/controls.png: the in-game controls card shown while Select is held.
# A line drawing of the Vita with call-outs, drawn at 2x and scaled down.
from PIL import Image, ImageDraw, ImageFont
S = 2
W, H = 960 * S, 544 * S
F = "/usr/share/fonts/truetype/dejavu/"
def font(n, s): return ImageFont.truetype(F + n, s * S)
title, lab, small, sym = font("DejaVuSans-Bold.ttf", 22), font("DejaVuSans.ttf", 15), font("DejaVuSans.ttf", 13), font("DejaVuSans-Bold.ttf", 13)
BG, GRID, LINE, TXT, DIM, ACC = (8, 20, 34), (16, 34, 54), (205, 222, 240), (235, 240, 248), (150, 170, 195), (255, 176, 40)
im = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(im)
for x in range(0, W, 40 * S): d.line([(x, 0), (x, H)], fill=GRID, width=1)
for y in range(0, H, 40 * S): d.line([(0, y), (W, y)], fill=GRID, width=1)
def P(x, y): return (x * S, y * S)
def box(x0, y0, x1, y1): return [x0 * S, y0 * S, x1 * S, y1 * S]
lw = 2 * S

d.text(P(480, 26), "CONTROLS", font=title, fill=TXT, anchor="mm")
d.text(P(480, 50), "hold SELECT to show this screen", font=small, fill=DIM, anchor="mm")

# --- Vita (PCH-1000) body, drawn to its real proportions (182 x 84 mm,
# 5" 16:9 screen in the middle, D-pad + left stick + PS button on the left,
# face buttons + right stick + SELECT/START on the right).
bx0, by0, BW, BH = 270, 140, 420, 194
bx1, by1 = bx0 + BW, by0 + BH
def V(fx, fy): return (bx0 + fx * BW, by0 + fy * BH)
d.rounded_rectangle(box(bx0, by0, bx1, by1), radius=int(0.46 * BH) * S, outline=LINE, width=lw)
sw, sh = 0.60 * BW, 0.60 * BW * 9 / 16
sx0, sy0 = bx0 + (BW - sw) / 2, by0 + (BH - sh) / 2
d.rounded_rectangle(box(sx0 - 6, sy0 - 6, sx0 + sw + 6, sy0 + sh + 6), radius=8 * S, outline=DIM, width=S)  # glass
d.rectangle(box(sx0, sy0, sx0 + sw, sy0 + sh), outline=LINE, width=lw)                                    # screen
# shoulder buttons
d.arc(box(bx0 + 4, by0 - 6, bx0 + 150, by0 + 90), 205, 262, fill=LINE, width=lw * 2)
d.arc(box(bx1 - 150, by0 - 6, bx1 - 4, by0 + 90), 278, 335, fill=LINE, width=lw * 2)
# D-pad
dx, dy = V(0.105, 0.33)
for (ox, oy) in ((0, -12), (0, 12), (-12, 0), (12, 0)):
    d.rounded_rectangle(box(dx + ox - 6, dy + oy - 6, dx + ox + 6, dy + oy + 6), radius=2 * S, outline=LINE, width=S)
# sticks
lsx, lsy = V(0.155, 0.66)
rsx, rsy = V(0.845, 0.66)
for (x, y) in ((lsx, lsy), (rsx, rsy)):
    d.ellipse(box(x - 13, y - 13, x + 13, y + 13), outline=LINE, width=lw)
    d.ellipse(box(x - 7, y - 7, x + 7, y + 7), outline=LINE, width=S)
# face buttons
fx, fy = V(0.895, 0.33)
face = {"tri": (fx, fy - 15), "cir": (fx + 15, fy), "crs": (fx, fy + 15), "sqr": (fx - 15, fy)}
for k, (x, y) in face.items():
    d.ellipse(box(x - 7, y - 7, x + 7, y + 7), outline=LINE, width=S)
glyph = {"tri": "\u25b3", "cir": "\u25cb", "crs": "\u2715", "sqr": "\u25a1"}
for k, (x, y) in face.items():
    d.text(P(x, y), glyph[k], font=small, fill=LINE, anchor="mm")
# PS button, SELECT / START, front camera, speakers
px, py = V(0.075, 0.85)
d.ellipse(box(px - 7, py - 7, px + 7, py + 7), outline=LINE, width=S)
d.text(P(px, py), "PS", font=font("DejaVuSans-Bold.ttf", 6), fill=LINE, anchor="mm")
se = V(0.875, 0.88); st = V(0.935, 0.88)
for (x, y) in (se, st):
    d.ellipse(box(x - 5, y - 4, x + 5, y + 4), outline=LINE, width=S)
cxm, cym = V(0.93, 0.10)
d.ellipse(box(cxm - 3, cym - 3, cxm + 3, cym + 3), outline=DIM, width=S)
for (fxs, fys) in ((0.04, 0.55), (0.96, 0.55)):
    gx, gy = V(fxs, fys)
    for k in range(3):
        d.ellipse(box(gx - 1.5, gy - 8 + k * 8 - 1.5, gx + 1.5, gy - 8 + k * 8 + 1.5), fill=DIM)

def callout(pts, text, anchor, color=TXT):
    d.line([P(*p) for p in pts], fill=ACC, width=S + 1)
    x, y = pts[-1]
    d.ellipse(box(pts[0][0] - 3, pts[0][1] - 3, pts[0][0] + 3, pts[0][1] + 3), fill=ACC)
    lines = text.split("\n")
    off = 6 if anchor[0] == "l" else -6
    for i, t in enumerate(lines):
        d.text(P(x + off, y + i * 19 - (len(lines) - 1) * 9.5), t, font=lab, fill=color, anchor=anchor)

# left side
callout([(bx0 + 60, by0 + 6), (300, 108), (250, 108)], "L  Barrel roll left", "rm")
callout([(dx - 19, dy), (250, dy)], "D-pad \u2191  Autopilot\nD-pad \u2193  Fast forward / jump\nD-pad \u2192  Action menu", "rm")
callout([(lsx - 14, lsy + 4), (290, lsy + 22), (250, lsy + 22)], "Left stick  Steer", "rm")
# right side
callout([(bx1 - 60, by0 + 6), (660, 108), (710, 108)], "R  Barrel roll right", "lm")
callout([(face["tri"][0] + 7, face["tri"][1] - 5), (690, 150), (710, 150)], "\u25b3  Boost", "lm")
callout([(face["cir"][0] + 8, face["cir"][1]), (710, face["cir"][1])], "\u25cb  Change camera", "lm", ACC)
callout([(face["crs"][0] + 7, face["crs"][1] + 4), (690, 228), (710, 228)], "\u2715  Fire", "lm")
callout([(face["sqr"][0] - 4, face["sqr"][1] + 7), (640, 250), (700, 256), (710, 256)], "\u25a1  Secondary weapon", "lm")
callout([(rsx + 13, rsy + 4), (695, 284), (710, 284)], "R stick \u2191\u2193  Speed up / down", "lm")
callout([(st[0] + 3, st[1] + 4), (700, 330), (710, 330)], "START  Pause menu", "lm")
callout([(se[0], se[1] + 5), (se[0], 352), (710, 352)], "SELECT (hold)  This screen", "lm")
# screen label
d.text(P(480, 228), "Touch screen", font=lab, fill=DIM, anchor="mm")
d.text(P(480, 248), "works too", font=small, fill=DIM, anchor="mm")

# menus line
d.text(P(480, 376), "IN MENUS & DIALOGUE:   Left stick / D-pad  move cursor     ✕ or R  select     ○  back",
       font=small, fill=DIM, anchor="mm")

# circle notice
y0 = 396
d.rounded_rectangle(box(30, y0, 930, 526), radius=10 * S, fill=(40, 30, 12), outline=ACC, width=S + 1)
d.text(P(50, y0 + 20), "○  CIRCLE CHANGES THE CAMERA", font=font("DejaVuSans-Bold.ttf", 16), fill=ACC, anchor="lm")
for i, t in enumerate([
    "Each press cycles:   Chase view  →  Turret view (if your ship has one)  →  Free look  →  back to Chase view.",
    "In free look the left stick moves the camera instead of the ship.",
    "If steering seems to stop working, press ○ until you are back in chase view. Every flight starts in chase view.",
]):
    d.text(P(50, y0 + 50 + i * 24), t, font=small, fill=TXT, anchor="lm")

im.resize((960, 544), Image.LANCZOS).save("data/controls.png")
