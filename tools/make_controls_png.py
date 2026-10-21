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

# --- Vita (PCH-1000) front, drawn from measurements of the real unit's
# proportions (182 x 84 mm). Coordinates below are in a 1123 x 515 unit
# space (body width x height) and mapped onto the card.
VW, VH = 1123.0, 515.0
bx0, by0 = 258, 116
SC_ = 444 / VW
BW, BH = VW * SC_, VH * SC_
bx1, by1 = bx0 + BW, by0 + BH
def T(x, y): return (bx0 + x * SC_, by0 + y * SC_)
def TB(x0, y0, x1, y1):
    a, b = T(x0, y0); c, e = T(x1, y1); return box(a, b, c, e)
def circ(x, y, r, **kw): d.ellipse(TB(x - r, y - r, x + r, y + r), **kw)

# outer shell and inner bezel
d.rounded_rectangle(TB(0, 0, VW, VH), radius=int(250 * SC_ * S), outline=LINE, width=lw)
d.rounded_rectangle(TB(14, 14, VW - 14, VH - 14), radius=int(236 * SC_ * S), outline=DIM, width=S)
# screen (sits a little above centre; PlayStation lettering below it)
d.rectangle(TB(223, 40, 898, 425), outline=LINE, width=lw)
d.text(P(*T(561, 449)), "PlayStation", font=font("DejaVuSans-Bold.ttf", 9), fill=DIM, anchor="mm")
d.text(P(*T(158, 75)), "SONY", font=font("DejaVuSerif-Bold.ttf", 8), fill=DIM, anchor="mm")
# grip vents at the bottom corners
for (x0, x1) in ((95, 205), (918, 1028)):
    d.line([P(*T(x0, 455)), P(*T(x1, 455))], fill=DIM, width=S)
# shoulder buttons (top corners)
d.arc(TB(10, -18, 330, 300), 200, 258, fill=LINE, width=lw * 2)
d.arc(TB(VW - 330, -18, VW - 10, 300), 282, 340, fill=LINE, width=lw * 2)

def recess(c1, r1, c2, r2):
    # outline of two overlapping circles (D-pad/face-button well + stick well)
    for (c, r, col) in ((c1, r1, LINE), (c2, r2, LINE)):
        circ(c[0], c[1], r + 2.5 / SC_, fill=col)
    for (c, r) in ((c1, r1), (c2, r2)):
        circ(c[0], c[1], r, fill=BG)
DP, LS = (110, 172), (139, 308)
FB, RS = (1015, 172), (981, 308)
recess(DP, 76, LS, 54)
recess(FB, 76, RS, 54)
# D-pad
for (ox, oy) in ((0, -28), (0, 28), (-28, 0), (28, 0)):
    x, y = DP[0] + ox, DP[1] + oy
    d.rounded_rectangle(TB(x - 17, y - 17, x + 17, y + 17), radius=3 * S, outline=LINE, width=S)
# sticks
for c in (LS, RS):
    circ(c[0], c[1], 38, outline=LINE, width=S)
    circ(c[0], c[1], 27, outline=LINE, width=S)
# face buttons
face = {"tri": (FB[0], FB[1] - 49), "cir": (FB[0] + 50, FB[1]), "crs": (FB[0], FB[1] + 50), "sqr": (FB[0] - 50, FB[1])}
face = {k: v for k, v in face.items()}
for k, (x, y) in face.items():
    circ(x, y, 22, outline=LINE, width=S)
glyph = {"tri": "\u25b3", "cir": "\u25cb", "crs": "\u2715", "sqr": "\u25a1"}
for k, (x, y) in face.items():
    d.text(P(*T(x, y)), glyph[k], font=small, fill=LINE, anchor="mm")
face = {k: T(*v) for k, v in face.items()}   # card coordinates for call-outs
# PS button, SELECT / START, front camera, speakers
d.ellipse(TB(126 - 42, 408 - 22, 126 + 42, 408 + 22), outline=LINE, width=S)
d.text(P(*T(126, 408)), "PS", font=font("DejaVuSans-Bold.ttf", 7), fill=LINE, anchor="mm")
for (x, t) in ((966, "SELECT"), (1026, "START")):
    d.ellipse(TB(x - 25, 408 - 12, x + 25, 408 + 12), outline=LINE, width=S)
    d.text(P(*T(x, 408)), t, font=font("DejaVuSans.ttf", 4), fill=LINE, anchor="mm")
circ(955, 95, 9, outline=DIM, width=S)
for gx in (40, 1083):
    for i in range(3):
        for j in range(2):
            circ(gx - 12 + i * 12, 300 + j * 18, 3.5, fill=DIM)
dx, dy = T(*DP)
lsx, lsy = T(*LS)
rsx, rsy = T(*RS)
se, st = T(966, 408), T(1026, 408)

def callout(pts, text, anchor, color=TXT):
    d.line([P(*p) for p in pts], fill=ACC, width=S + 1)
    x, y = pts[-1]
    d.ellipse(box(pts[0][0] - 3, pts[0][1] - 3, pts[0][0] + 3, pts[0][1] + 3), fill=ACC)
    lines = text.split("\n")
    off = 6 if anchor[0] == "l" else -6
    for i, t in enumerate(lines):
        d.text(P(x + off, y + i * 19 - (len(lines) - 1) * 9.5), t, font=lab, fill=color, anchor=anchor)

# left side
callout([T(150, 12), (280, 80), (248, 80)], "L  Barrel roll left", "rm")
callout([(dx - 34, dy), (248, dy)], "D-pad \u2191  Autopilot\nD-pad \u2193  Fast forward / jump\nD-pad \u2192  Action menu", "rm")
callout([(lsx - 22, lsy + 4), (275, lsy + 26), (248, lsy + 26)], "Left stick  Steer", "rm")
callout([T(84, 408), (248, 300)], "PS  Home (system)", "rm", DIM)
# right side
callout([T(VW - 150, 12), (680, 80), (712, 80)], "R  Barrel roll right", "lm")
callout([(face["tri"][0] + 9, face["tri"][1] - 4), (700, 150), (712, 150)], "\u25b3  Boost", "lm")
callout([(face["cir"][0] + 10, face["cir"][1]), (712, face["cir"][1])], "\u25cb  Change camera", "lm", ACC)
callout([(face["crs"][0] + 9, face["crs"][1] + 4), (700, 216), (712, 216)], "\u2715  Fire", "lm")
callout([(face["sqr"][0] - 4, face["sqr"][1] - 8), (face["sqr"][0] - 4, 114), (712, 114)], "\u25a1  Secondary weapon", "lm")
callout([(rsx + 22, rsy + 4), (700, 270), (712, 270)], "R stick \u2191\u2193  Speed up / down", "lm")
callout([(st[0] + 6, st[1] + 4), (700, 318), (712, 318)], "START  Pause menu", "lm")
callout([(se[0], se[1] + 6), (se[0], 342), (712, 342)], "SELECT (hold)  This screen", "lm")
# screen label
d.text(P(480, 200), "Touch screen", font=lab, fill=DIM, anchor="mm")
d.text(P(480, 220), "works too", font=small, fill=DIM, anchor="mm")

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
